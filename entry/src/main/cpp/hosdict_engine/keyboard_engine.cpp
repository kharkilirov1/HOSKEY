#include "keyboard_engine.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <initializer_list>
#include <numeric>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "text_utils.h"

namespace hoskey::engine {

namespace {

// ---------------------------------------------------------------------------
// Helper: character n-gram BLEU precision
// ---------------------------------------------------------------------------
size_t LevenshteinDistanceCodepoints(const std::vector<char32_t>& a,
                                     const std::vector<char32_t>& b) {
  if (a.empty()) return b.size();
  if (b.empty()) return a.size();

  std::vector<size_t> prev(b.size() + 1);
  std::vector<size_t> curr(b.size() + 1);
  for (size_t j = 0; j <= b.size(); ++j) {
    prev[j] = j;
  }

  for (size_t i = 1; i <= a.size(); ++i) {
    curr[0] = i;
    for (size_t j = 1; j <= b.size(); ++j) {
      const size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
      curr[j] = std::min({prev[j] + 1, curr[j - 1] + 1, prev[j - 1] + cost});
    }
    std::swap(prev, curr);
  }
  return prev[b.size()];
}

float NormalizedCodepointDistance(const std::vector<char32_t>& a,
                                  const std::vector<char32_t>& b) {
  const size_t denom = std::max<size_t>(std::max(a.size(), b.size()), 1);
  return static_cast<float>(LevenshteinDistanceCodepoints(a, b)) /
         static_cast<float>(denom);
}

constexpr uint32_t kTouchPhaseMask = 0xF0000000u;
constexpr uint32_t kTouchKeyCodeMask = 0x0FFFFFFFu;
constexpr uint32_t kTouchKeyCodeBackspace = 8u;
constexpr uint32_t kTouchKeyCodeEnter = 13u;
constexpr uint32_t kTouchKeyCodeSpace = 32u;

// Matches the recovered Yandex Suggestion.typeFlags layout (Ff.u1):
// completion=0x01, prediction=0x02, autocorrect=0x04, exact=0x100.
constexpr uint32_t kSuggestionFlagCompletion = 0x01u;
constexpr uint32_t kSuggestionFlagPrediction = 0x02u;
constexpr uint32_t kSuggestionFlagAutocorrect = 0x04u;
constexpr uint32_t kSuggestionFlagExact = 0x100u;

std::vector<char32_t> ReconstructTouchInputCodepoints(
    const std::vector<SuggestRequest::TouchPoint>& points) {
  std::vector<char32_t> result;
  result.reserve(points.size());

  for (const auto& point : points) {
    const uint32_t phase = point.key_code & kTouchPhaseMask;
    if (phase != 0) {
      continue;
    }

    const uint32_t code = point.key_code & kTouchKeyCodeMask;
    if (code == 0) {
      continue;
    }
    if (code == kTouchKeyCodeBackspace) {
      if (!result.empty()) {
        result.pop_back();
      }
      continue;
    }
    if (code == kTouchKeyCodeSpace || code == kTouchKeyCodeEnter) {
      result.clear();
      continue;
    }
    result.push_back(static_cast<char32_t>(code));
  }

  return result;
}

const LoadedBinaryAsset* FindFirstBinary(
    const DictionaryStore& store,
    std::initializer_list<std::string_view> keys) {
  for (std::string_view key : keys) {
    const auto* asset = store.FindBinary(key);
    if (asset && !asset->bytes.empty()) return asset;
  }
  return nullptr;
}

float CharNgramPrecision(const std::vector<char32_t>& ref,
                         const std::vector<char32_t>& hyp, size_t n) {
  if (hyp.size() < n || ref.size() < n) return 0.0f;
  std::unordered_map<size_t, int> ref_counts;
  auto hash_ngram = [&](const std::vector<char32_t>& v, size_t start) {
    size_t h = 0;
    for (size_t i = 0; i < n; ++i)
      h = h * 1000003u ^ static_cast<size_t>(v[start + i]);
    return h;
  };
  for (size_t i = 0; i + n <= ref.size(); ++i)
    ref_counts[hash_ngram(ref, i)]++;
  int matched = 0, total = 0;
  for (size_t i = 0; i + n <= hyp.size(); ++i) {
    size_t h = hash_ngram(hyp, i);
    auto it = ref_counts.find(h);
    if (it != ref_counts.end() && it->second > 0) {
      ++matched;
      --(it->second);
    }
    ++total;
  }
  return total > 0 ? static_cast<float>(matched) / static_cast<float>(total) : 0.0f;
}

// ---------------------------------------------------------------------------
// Helper: is permutation (anagram)?
// ---------------------------------------------------------------------------
bool IsPermutation(const std::vector<char32_t>& a, const std::vector<char32_t>& b) {
  if (a.size() != b.size()) return false;
  auto sa = a, sb = b;
  std::sort(sa.begin(), sa.end());
  std::sort(sb.begin(), sb.end());
  return sa == sb;
}

// ---------------------------------------------------------------------------
// Helper: Kendall tau distance (normalized)
// ---------------------------------------------------------------------------
float KendallTauNormalized(const std::vector<char32_t>& a,
                           const std::vector<char32_t>& b) {
  // Build position map of b, count inversions between a's order and b's order.
  if (a.empty() || b.empty()) return 0.0f;
  // Only meaningful for same-length permutations.
  if (a.size() != b.size()) return 0.0f;
  std::unordered_map<char32_t, int> pos_in_b;
  for (int i = 0; i < static_cast<int>(b.size()); ++i)
    pos_in_b[b[i]] = i;
  std::vector<int> mapped;
  for (auto c : a) {
    auto it = pos_in_b.find(c);
    if (it != pos_in_b.end()) mapped.push_back(it->second);
  }
  int inversions = 0;
  for (size_t i = 0; i < mapped.size(); ++i)
    for (size_t j = i + 1; j < mapped.size(); ++j)
      if (mapped[i] > mapped[j]) ++inversions;
  int n = static_cast<int>(mapped.size());
  int max_inv = n * (n - 1) / 2;
  return max_inv > 0 ? static_cast<float>(inversions) / static_cast<float>(max_inv) : 0.0f;
}

// Manual autocorrect rules parsed from config JSON.
std::unordered_map<std::string, std::string> ParseManualRules(const std::string& json) {
  std::unordered_map<std::string, std::string> rules;
  if (json.empty()) return rules;

  static constexpr std::string_view kOrig = "\"original\":\"";
  static constexpr std::string_view kCorr = "\"correction\":\"";

  size_t cursor = 0;
  while (true) {
    const size_t ok = json.find(kOrig, cursor);
    if (ok == std::string::npos) break;
    const size_t os = ok + kOrig.size();
    const size_t oe = json.find('"', os);
    if (oe == std::string::npos) break;

    const size_t ck = json.find(kCorr, oe);
    if (ck == std::string::npos) break;
    const size_t cs = ck + kCorr.size();
    const size_t ce = json.find('"', cs);
    if (ce == std::string::npos) break;

    std::string original(json, os, oe - os);
    std::string correction(json, cs, ce - cs);
    if (!original.empty() && !correction.empty()) {
      rules.emplace(std::move(original), std::move(correction));
    }
    cursor = ce + 1;
  }
  return rules;
}

bool SameSuggestionTexts(const std::vector<Suggestion>& a,
                        const std::vector<Suggestion>& b,
                        size_t limit = 6) {
  const size_t na = std::min(limit, a.size());
  const size_t nb = std::min(limit, b.size());
  if (na != nb) return false;
  for (size_t i = 0; i < na; ++i) {
    if (a[i].text != b[i].text) return false;
  }
  return true;
}

}  // namespace

// ============================================================================
// Token normalization (lowercase, yo→e)
// ============================================================================

std::string KeyboardEngine::NormalizeToken(std::string_view text) const {
  // Trim
  size_t begin = 0, end = text.size();
  while (begin < end && static_cast<unsigned char>(text[begin]) <= 0x20) ++begin;
  while (end > begin && static_cast<unsigned char>(text[end - 1]) <= 0x20) --end;
  std::string trimmed(text.substr(begin, end - begin));

  std::vector<char32_t> decoded = DecodeUtf8(trimmed);

  for (char32_t& ch : decoded) {
    if (ch >= U'A' && ch <= U'Z') ch = ch - U'A' + U'a';
    else if (ch >= U'\u0410' && ch <= U'\u042F') ch = ch + 0x20;
    else if (ch == U'\u0401') ch = U'\u0451';
  }
  return EncodeUtf8(decoded);
}

// ============================================================================
// Policy helpers
// ============================================================================

bool KeyboardEngine::IsBlacklisted(const std::string& normalized) const {
  return blacklist_.count(normalized) > 0;
}

bool KeyboardEngine::IsAutocorrectBlocked(const std::string& normalized) const {
  return autocorrect_blockers_.count(normalized) > 0;
}

bool KeyboardEngine::IsKnownWord(const std::string& normalized) const {
  if (predictor_.is_built() && predictor_.Contains(normalized)) {
    return true;
  }
  if (one_nc7_trie_.loaded() && one_nc7_trie_.Contains(normalized)) {
    return true;
  }
  if (nnlm_.has_vocabulary() && nnlm_.VocabularyContains(normalized)) {
    return true;
  }
  return false;
}

void KeyboardEngine::LoadBlacklist() {
  blacklist_.clear();
  const auto* wl = package_.store().FindWordList("policy.blacklist");
  if (wl) {
    for (const auto& w : wl->normalized_words) {
      blacklist_.insert(NormalizeToken(w));
    }
  }
}

void KeyboardEngine::LoadAutocorrectBlocker() {
  autocorrect_blockers_.clear();
  const auto* wl = package_.store().FindWordList("policy.autocorrect_blocker");
  if (wl) {
    for (const auto& w : wl->normalized_words) {
      autocorrect_blockers_.insert(NormalizeToken(w));
    }
  }
  // Also try binary variant.
  const auto* bin = package_.store().FindBinary("policy.autocorrect_blocker");
  if (bin && !bin->bytes.empty()) {
    std::string_view data(reinterpret_cast<const char*>(bin->bytes.data()), bin->bytes.size());
    size_t pos = 0;
    while (pos < data.size()) {
      size_t nl = data.find('\n', pos);
      if (nl == std::string_view::npos) nl = data.size();
      std::string word = NormalizeToken(data.substr(pos, nl - pos));
      if (!word.empty()) autocorrect_blockers_.insert(std::move(word));
      pos = nl + 1;
    }
  }
}

// ============================================================================
// Init subsystems from loaded assets
// ============================================================================

Status KeyboardEngine::InitSubsystems() {
  ranker_model_asset_present_ = false;
  nnlm_model_asset_present_ = false;
  predictor_asset_present_ = false;

  // Parse manual autocorrect rules once; the config JSON is fixed after the
  // package is opened, so GetSuggestions/CheckWord reuse manual_rules_ instead
  // of re-parsing the config on every request.
  manual_rules_ = ParseManualRules(package_.raw_config_json());

  // 1. Load 1nc7 trie from lexicon.default — the primary dictionary.
  // OneNc7Trie: multi-level LOUDS subword trie for prefix matching.
  // SuccinctTrie: tokenizer for Neural LM input.
  {
    const uint8_t* data = nullptr;
    size_t data_size = 0;
    if (package_.container().GetAssetData("lexicon.default", &data, &data_size).ok()) {
      (void)one_nc7_trie_.Parse(data, data_size);
      (void)succinct_trie_.Parse(data, data_size, false);
    }
  }

  // 2. Load TfmTrie (keyboard typo correction rules)
  const auto* rules_bin = package_.store().FindBinary("rules.transfeme");
  if (rules_bin && !rules_bin->bytes.empty()) {
    tfmtrie_.Parse(rules_bin->bytes.data(), rules_bin->bytes.size());
  }

  // 3. Load Lemmer (morphological lemmatization)
  const auto* hash_bin = package_.store().FindBinary("lemma.hash");
  const auto* rules_data = package_.store().FindBinary("lemma.rules");
  if (hash_bin && !hash_bin->bytes.empty()) {
    lemmer_.LoadHash(hash_bin->bytes.data(), hash_bin->bytes.size());
  }
  if (rules_data && !rules_data->bytes.empty()) {
    lemmer_.LoadRules(rules_data->bytes.data(), rules_data->bytes.size());
  }

  // 4. Load ML ranker model (optional)
  const auto* ranker_bin =
      FindFirstBinary(package_.store(), {"ranking.ranker", "ranker.default"});
  if (ranker_bin && !ranker_bin->bytes.empty()) {
    ranker_model_asset_present_ = true;
    (void)ranker_.Load(ranker_bin->bytes.data(), ranker_bin->bytes.size());
  }
  // 4b. Reverse-engineered Yandex MatrixNet oblivious-tree ranker (HRNK). Pure
  // C++ (host + device); when present it takes priority in ranker_.ScoreOne over
  // the .ms/heuristic path. Evaluator is bit-exact to the decoded reference.
  const auto* mn_bin =
      FindFirstBinary(package_.store(), {"ranking.matrixnet", "neural.ranker"});
  if (mn_bin && !mn_bin->bytes.empty()) {
    ranker_model_asset_present_ = true;
    (void)ranker_.LoadMatrixNet(mn_bin->bytes.data(), mn_bin->bytes.size());
  }

  // 5. Load Neural LM — primary candidate generator (Yandex-style)
  const auto* nnlm_bin = FindFirstBinary(
      package_.store(),
      {"ranking.neural_model", "ranking.neural", "nnlm.default",
       "neural.default"});
  if (nnlm_bin && !nnlm_bin->bytes.empty()) {
    nnlm_model_asset_present_ = true;
    (void)nnlm_.Load(nnlm_bin->bytes.data(), nnlm_bin->bytes.size());
  }
  const auto* raw_nnlm_bin = FindFirstBinary(
      package_.store(),
      {"neural.word_model.raw", "neural.word_model",
       "neural.subword_nnlm.KEYBOARD-13321"});
  if (raw_nnlm_bin && !raw_nnlm_bin->bytes.empty()) {
    nnlm_model_asset_present_ = true;
    (void)nnlm_.LoadVocabulary(raw_nnlm_bin->bytes.data(),
                               raw_nnlm_bin->bytes.size());
  }

  // 5.5. Load compact word predictor fallback.  This is not the target Yandex
  // neural path; it is a packaged safety net for local/debug builds where the
  // neural runtime is unavailable and raw 1nc7 subword traversal is not yet
  // safe to expose directly as user-visible words.
  const auto* predictor_bin = package_.store().FindBinary("lexicon.predictor");
  if (predictor_bin && !predictor_bin->bytes.empty()) {
    predictor_asset_present_ = true;
    (void)predictor_.LoadBinary(predictor_bin->bytes.data(),
                                predictor_bin->bytes.size());
  }

  // 5.6. Load the neural word-suggestion chain: RWKV encoder (pure C++ float
  // weights) + emb/enc_dec/word-decoder .ms + the ynnlm word vocabulary. This is
  // an additive candidate generator that runs the reverse-engineered Yandex word
  // LM. It stays inert unless all five assets are present AND the MindSpore Lite
  // runtime is available (device build), so host/debug builds are unaffected.
  {
    const auto* wc_emb = package_.store().FindBinary("neural.word.emb");
    const auto* wc_bridge = package_.store().FindBinary("neural.word.enc_dec");
    const auto* wc_dec = package_.store().FindBinary("neural.word.dec");
    const auto* wc_enc = package_.store().FindBinary("neural.word.encoder");
    const auto* wc_vocab = FindFirstBinary(
        package_.store(), {"neural.word_model.raw", "neural.word_model"});
    if (wc_emb && wc_bridge && wc_dec && wc_enc && wc_vocab &&
        !wc_emb->bytes.empty() && !wc_bridge->bytes.empty() &&
        !wc_dec->bytes.empty() && !wc_enc->bytes.empty() &&
        !wc_vocab->bytes.empty()) {
      word_chain_asset_present_ = true;
      (void)word_chain_.Load(wc_emb->bytes.data(), wc_emb->bytes.size(),
                             wc_bridge->bytes.data(), wc_bridge->bytes.size(),
                             wc_dec->bytes.data(), wc_dec->bytes.size(),
                             wc_enc->bytes.data(), wc_enc->bytes.size(),
                             wc_vocab->bytes.data(), wc_vocab->bytes.size());
    }
  }

  // 6. Load NormDict
  const auto* normdict_bin = package_.store().FindBinary("normdict.default");
  if (normdict_bin && !normdict_bin->bytes.empty()) {
    normdict_.Load(normdict_bin->bytes.data(), normdict_bin->bytes.size());
  }

  // 7. Load policy sets
  LoadBlacklist();
  LoadAutocorrectBlocker();
  
  return Status::Ok();
}

// ============================================================================
// Open / Close
// ============================================================================

Status KeyboardEngine::Open(const std::filesystem::path& package_root) {
  return Open(package_root, KeyboardDebugConfig{});
}

Status KeyboardEngine::Open(const std::filesystem::path& package_root,
                            const KeyboardDebugConfig& debug_config) {
  Close();
  debug_config_ = debug_config;
  package_root_ = package_root;
  Status s = package_.OpenFromDir(package_root);
  if (!s.ok()) {
    package_root_.clear();
    return s;
  }

  s = InitSubsystems();
  if (!s.ok()) {
    Close();
    return s;
  }
  return Status::Ok();
}

void KeyboardEngine::Close() {
  package_.Close();
  tfmtrie_ = TfmTrieDecoder{};
  one_nc7_trie_ = OneNc7Trie{};
  predictor_ = PredictorEngine{};
  lemmer_.Reset();
  ranker_.Reset();
  nnlm_.Reset();
  word_chain_.Reset();
  normdict_ = NormDictReader{};
  blacklist_.clear();
  autocorrect_blockers_.clear();
  ranker_model_asset_present_ = false;
  nnlm_model_asset_present_ = false;
  predictor_asset_present_ = false;
  word_chain_asset_present_ = false;
  debug_config_ = KeyboardDebugConfig{};
  package_root_.clear();
}

// ============================================================================
// GetSuggestions — main prediction pipeline
// ============================================================================

SuggestResponse KeyboardEngine::GetSuggestions(const SuggestRequest& request) const {
  SuggestResponse response;

  if (!package_.is_open()) {
    response.status = Status::InvalidState("Engine not open");
    return response;
  }
  const std::string& request_input =
      !request.token.empty() ? request.token : request.input;
  if (request_input.empty()) {
    response.status = Status::InvalidArgument("Empty token");
    return response;
  }

  const std::string normalized = normdict_.loaded()
      ? normdict_.Normalize(request_input)
      : NormalizeToken(request_input);
  const size_t max = std::max<size_t>(request.max_suggestions, 1);
  const size_t prev_context_count = std::max(request.previous_tokens.size(),
                                             request.prev_words_info.size());
  // getSuggestions is called synchronously from ArkTS.  Keep every live
  // dictionary probe bounded: OneNc7Trie::Contains()/StaticWeight() may run
  // iterator materialization and was observed in appfreeze stacks.
  const auto is_known_word_fast_for_request = [&](const std::string& token) {
    if (predictor_.is_built() && predictor_.Contains(token)) {
      return true;
    }
    if (one_nc7_trie_.loaded() &&
        one_nc7_trie_.HasTerminalBoundary(token)) {
      return true;
    }
    if (nnlm_.has_vocabulary() && nnlm_.VocabularyContains(token)) {
      return true;
    }
    return false;
  };
  const auto static_weight_for_request = [&](const std::string& token) -> int32_t {
    if (nnlm_.has_vocabulary()) {
      return nnlm_.VocabularyWeight(token);
    }
    if (predictor_.is_built()) {
      const int32_t predictor_weight = predictor_.GetWeight(token);
      if (predictor_weight > 0) return predictor_weight;
    }
    if (one_nc7_trie_.loaded()) {
      const int32_t native_weight = one_nc7_trie_.DirectStaticWeight(token);
      if (native_weight > 0) return native_weight;
    }
    return 0;
  };
  const bool native_prefix_input_known =
      one_nc7_trie_.loaded() && one_nc7_trie_.HasPrefix(normalized);
  // Exactness in the live path means cheap direct terminal/vocabulary proof.
  // Native iterator exact proof remains available to CheckWord, not keystrokes.
  const bool exact_input_known = is_known_word_fast_for_request(normalized);

  // Check blacklist on input
  if (IsBlacklisted(normalized)) {
    response.is_blocked = true;
    Suggestion s;
    s.text = request_input;
    s.tag = "blocked";
    s.score = 0.0f;
    response.suggestions.push_back(std::move(s));
    return response;
  }

  // Normalized text of every accepted suggestion, maintained as suggestions are
  // added so each stage's dedup is an O(1) set lookup instead of re-normalizing
  // the whole accepted list (previously O(stages * candidates^2) per keystroke).
  std::unordered_set<std::string> seen_normalized;

  // --- Stage 1: Manual autocorrect rules ---
  if (auto it = manual_rules_.find(normalized); it != manual_rules_.end()) {
    Suggestion s;
    s.text = it->second;
    s.tag = "manual_rule";
    s.type_flags = kSuggestionFlagAutocorrect;
    s.score = 1000.0f;
    seen_normalized.insert(NormalizeToken(s.text));
    response.suggestions.push_back(std::move(s));
    response.will_autocorrect = request.want_autocorrect;
  }

  // --- Stage 2: Neural LM generation (primary, Yandex-style) ---
  if (nnlm_.is_loaded() && succinct_trie_.loaded()) {
    std::vector<uint32_t> context_tokens;
    if (!normalized.empty()) {
      context_tokens = succinct_trie_.Tokenize(normalized);
    }
    auto nn_results = nnlm_.GenerateCompletions(
        succinct_trie_, context_tokens, normalized, 5, 8);
    for (auto& text : nn_results) {
      if (IsBlacklisted(text)) continue;
      if (text == normalized) continue;

      if (seen_normalized.count(text) != 0) continue;

      Suggestion s;
      s.text = std::move(text);
      s.tag = "nnlm";
      s.type_flags = normalized.empty() ? kSuggestionFlagPrediction
                                        : kSuggestionFlagCompletion;
      s.score = 50.0f;
      seen_normalized.insert(NormalizeToken(s.text));
      response.suggestions.push_back(std::move(s));
    }
  }

  // --- Stage 2.1: Raw Yandex ynnlm_v1 vocabulary bridge ---
  //
  // This is not the old binary predictor and not an external flat word list:
  // it parses the UTF-8 vocabulary embedded in the original Yandex neural
  // model bundle.  Without MindSpore Lite it gives us an exact/vocab signal and
  // a conservative word-level candidate source; true probabilities still come
  // from NNLM inference when the runtime is available.
  if (nnlm_.has_vocabulary() && response.suggestions.size() < max + 4) {
    const size_t vocab_limit = std::min<size_t>(max, 6);
    auto vocab_results = nnlm_.VocabularyCompletions(normalized, vocab_limit);
    for (auto& c : vocab_results) {
      if (IsBlacklisted(c.text)) continue;
      if (c.text == normalized) continue;

      if (seen_normalized.count(c.text) != 0) continue;

      Suggestion s;
      s.text = std::move(c.text);
      s.tag = "ynnlm_vocab";
      s.type_flags = kSuggestionFlagCompletion;
      s.score = static_cast<float>(c.weight);
      seen_normalized.insert(NormalizeToken(s.text));
      response.suggestions.push_back(std::move(s));
    }
  }

  // --- Stage 2.2: Optional packaged predictor fallback ---
  //
  // This is intentionally inactive in normal packages: build_hosdict.py only
  // includes lexicon.predictor when the explicit debug fallback flag is set.
  // The primary Yandex-style path is NNLM/ranker + 1nc7 materialization below.
  if (predictor_.is_built() && response.suggestions.size() < max + 4) {
    auto predictor_results =
        predictor_.GetSuggestions(normalized, request.full_context, max + 4);
    for (auto& pr : predictor_results) {
      if (IsBlacklisted(pr.word)) continue;

      if (seen_normalized.count(pr.word) != 0) continue;

      Suggestion s;
      s.text = std::move(pr.word);
      switch (pr.source) {
        case PredictorResult::kExact:
          s.tag = "predictor_exact";
          s.type_flags = kSuggestionFlagExact;
          break;
        case PredictorResult::kFuzzy:
          s.tag = "predictor_fuzzy";
          s.type_flags = kSuggestionFlagAutocorrect;
          break;
        case PredictorResult::kAutocorrect:
          s.tag = "predictor_autocorrect";
          s.type_flags = kSuggestionFlagAutocorrect;
          break;
        case PredictorResult::kPrefix:
        default:
          s.tag = "predictor";
          s.type_flags = kSuggestionFlagCompletion;
          break;
      }
      s.score = pr.score;
      seen_normalized.insert(NormalizeToken(s.text));
      response.suggestions.push_back(std::move(s));
    }
  }

  // --- Stage 2.5: OneNc7Trie subword completions (supplemental) ---
  if (!predictor_.is_built() && one_nc7_trie_.loaded() &&
      response.suggestions.size() < max + 4) {
    auto native = one_nc7_trie_.Complete(normalized, max + 4, 8);
    for (auto& c : native) {
      if (IsBlacklisted(c.text)) continue;

      if (seen_normalized.count(c.text) != 0) continue;

      Suggestion s;
      s.text = std::move(c.text);
      s.tag = "1nc7";
      s.type_flags = kSuggestionFlagCompletion;
      s.score = static_cast<float>(c.weight);
      seen_normalized.insert(NormalizeToken(s.text));
      response.suggestions.push_back(std::move(s));
    }
  }

  // --- Stage 2.6: SuccinctTrie completions (supplemental) ---
  if (!predictor_.is_built() && succinct_trie_.loaded() &&
      response.suggestions.size() < max + 4) {
    auto completions = succinct_trie_.GetCompletions(normalized, 10);
    for (const auto& c : completions) {
      if (response.suggestions.size() >= max + 4) break;
      if (IsBlacklisted(c)) continue;
      
      if (seen_normalized.count(c) != 0) continue;

      Suggestion s;
      s.text = c;
      s.tag = "succinct";
      s.type_flags = kSuggestionFlagCompletion;
      s.score = 100.0f;
      seen_normalized.insert(NormalizeToken(s.text));
      response.suggestions.push_back(std::move(s));
    }
  }

  // --- Stage 3: TfmTrie keyboard correction candidates ---
  if (!exact_input_known && !native_prefix_input_known && tfmtrie_.is_parsed() &&
      response.suggestions.size() < max) {
    auto tfm_candidates = tfmtrie_.GenerateCandidates(normalized, 1);
    for (const auto& [candidate, weight] : tfm_candidates) {
      if (response.suggestions.size() >= max) break;
      if (IsBlacklisted(candidate)) continue;

      // Only add if it's a known word
      if (!is_known_word_fast_for_request(candidate)) continue;

      if (seen_normalized.count(candidate) != 0) continue;

      Suggestion s;
      s.text = candidate;
      s.tag = "tfmtrie";
      s.type_flags = kSuggestionFlagAutocorrect;
      s.score = 50.0f * weight;
      seen_normalized.insert(NormalizeToken(s.text));
      response.suggestions.push_back(std::move(s));
    }
  }

  // --- Stage 4: Lemmer morphological expansion ---
  if (!exact_input_known && lemmer_.loaded() && response.suggestions.size() < max) {
    std::string lemma = lemmer_.Lemmatize(normalized);
    if (lemma != normalized && !lemma.empty() &&
        is_known_word_fast_for_request(lemma)) {
      if (!seen_normalized.count(lemma) && !IsBlacklisted(lemma)) {
        Suggestion s;
        s.text = std::move(lemma);
        s.tag = "lemma";
        s.type_flags = kSuggestionFlagAutocorrect;
        s.score = 40.0f;
        seen_normalized.insert(NormalizeToken(s.text));
        response.suggestions.push_back(std::move(s));
      }
    }
  }

  // --- ML Reranking (Yandex-aligned feature vector) ---
  {
    const auto original_cp = DecodeUtf8(normalized);
    const float orig_len = static_cast<float>(original_cp.size());
    const auto touch_input_cp = ReconstructTouchInputCodepoints(request.points);
    const bool has_touch_geometry =
        request.proximity_info.enabled && !touch_input_cp.empty();
    const float original_geometry_distance = has_touch_geometry
        ? NormalizedCodepointDistance(touch_input_cp, original_cp)
        : 0.0f;

    const auto normalize_static_weight = [](float weight) {
      if (weight <= 0.0f) return 0.0f;
      return std::min(1.0f, std::log1p(weight) / std::log(1101.0f));
    };

    // Original LM weight (shared across all candidates).
    float orig_lm_1gr = 0.0f;
    const int32_t original_static_weight =
        exact_input_known ? static_weight_for_request(normalized) : 0;
    if (original_static_weight > 0) {
      orig_lm_1gr = normalize_static_weight(
          static_cast<float>(original_static_weight));
    }
    bool orig_in_dict = nnlm_.has_vocabulary()
        ? nnlm_.VocabularyContains(normalized)
        : exact_input_known;

    std::vector<RankerFeatures> feature_batch;
    feature_batch.reserve(response.suggestions.size());

    for (const auto& s : response.suggestions) {
      RankerFeatures f;

      const std::string corr_normalized = NormalizeToken(s.text);
      const auto corr_cp = DecodeUtf8(corr_normalized);
      const float corr_len = static_cast<float>(corr_cp.size());

      // -- Basic string features --
      f.original_length = orig_len;
      f.correction_length = corr_len;
      const bool native_completion =
          s.tag == "1nc7" || s.tag == "ynnlm_vocab";
      const int32_t correction_vocab_weight = nnlm_.has_vocabulary()
          ? nnlm_.VocabularyWeight(corr_normalized)
          : 0;
      const bool correction_in_word_vocab = correction_vocab_weight > 0;
      f.original_ndict = orig_in_dict ? 1.0f : 0.0f;
      f.correction_ndict = nnlm_.has_vocabulary()
          ? (correction_in_word_vocab ? 1.0f : 0.0f)
          : ((native_completion ||
              is_known_word_fast_for_request(corr_normalized))
                 ? 1.0f
                 : 0.0f);
      f.neural_oov = (nnlm_.has_vocabulary() && !correction_in_word_vocab)
          ? 1.0f
          : 0.0f;
      f.no_change = (normalized == corr_normalized) ? 1.0f : 0.0f;

      // -- Edit distance --
      f.levenshtein = static_cast<float>(
          LevenshteinDistanceUtf8(normalized, corr_normalized));
      f.levenshtein_per_letter = f.levenshtein / std::max(corr_len, 1.0f);

      // -- Transfeme (keyboard proximity) --
      if (s.tag == "tfmtrie" || s.tag == "fuzzy") {
        // TfmTrie candidates have inherent proximity; estimate distance.
        f.distance_transfeme_fwd = std::max(0.0f, 1.0f - f.levenshtein_per_letter);
        f.distance_transfeme_rev = f.distance_transfeme_fwd;
      }
      f.distance_per_letter_fwd = (corr_len > 0.0f)
          ? f.distance_transfeme_fwd / corr_len : 0.0f;
      f.distance_per_letter_rev = (orig_len > 0.0f)
          ? f.distance_transfeme_rev / orig_len : 0.0f;

      // -- Permutation / anagram --
      f.is_permutation = IsPermutation(original_cp, corr_cp) ? 1.0f : 0.0f;

      // -- Kendall tau --
      f.kendall_tau_distance = KendallTauNormalized(original_cp, corr_cp);

      // -- Character n-gram BLEU --
      f.bleu_2 = CharNgramPrecision(original_cp, corr_cp, 2);
      f.bleu_3 = CharNgramPrecision(original_cp, corr_cp, 3);
      f.bleu_4 = CharNgramPrecision(original_cp, corr_cp, 4);

      // -- Letter precision (character overlap) --
      if (!corr_cp.empty()) {
        // Unigram precision: fraction of correction chars present in original.
        std::unordered_map<char32_t, int> orig_chars;
        for (auto c : original_cp) orig_chars[c]++;
        int matched1 = 0;
        for (auto c : corr_cp) {
          auto it = orig_chars.find(c);
          if (it != orig_chars.end() && it->second > 0) {
            ++matched1;
            --(it->second);
          }
        }
        f.letter_precision_1 = static_cast<float>(matched1) / corr_len;
      }
      f.letter_precision_2 = CharNgramPrecision(original_cp, corr_cp, 2);
      f.letter_precision_3 = CharNgramPrecision(original_cp, corr_cp, 3);

      // -- Trie weight features --
      f.original_lm_1gr_static = orig_lm_1gr;
      const int32_t correction_static_weight = nnlm_.has_vocabulary()
          ? correction_vocab_weight
          : (native_completion
                 ? static_cast<int32_t>(std::max(0.0f, s.score))
                 : static_weight_for_request(corr_normalized));
      f.correction_lm_1gr_static = normalize_static_weight(
          correction_static_weight > 0
              ? static_cast<float>(correction_static_weight)
              : std::max(0.0f, s.score));
      // 2-gram and n-gram static: same as unigram for now (no bigram trie).
      f.original_lm_2gr_static = f.original_lm_1gr_static;
      f.correction_lm_2gr_static = f.correction_lm_1gr_static;
      f.original_lm_ngr_static = f.original_lm_1gr_static;
      f.correction_lm_ngr_static = f.correction_lm_1gr_static;

      // -- Neural features: 0.0 until models wired --
      // (original_lm_ngr_main, correction_lm_ngr_main, etc. stay at default 0.0)

      // -- Diacritics-only change --
      // For Russian: yo/e normalization. If normalized forms are identical
      // but original texts differ, it's a diacritics-only correction.
      f.diacr_only = (f.no_change != 0.0f && request_input != s.text) ? 1.0f : 0.0f;

      // -- Combo features (derived) --
      f.combo_dist_lm = f.distance_transfeme_fwd * f.correction_lm_1gr_static;
      f.combo_lm_diff = f.correction_lm_ngr_main - f.original_lm_ngr_main;
      f.combo_len_diff = f.correction_length - f.original_length;
      f.combo_dist_lm_rev = f.distance_transfeme_rev * f.original_lm_1gr_static;
      f.combo_lm_static_diff = f.correction_lm_1gr_static - f.original_lm_1gr_static;

      // -- Geometry from live touch/proximity request --
      if (has_touch_geometry) {
        const float correction_geometry_distance =
            NormalizedCodepointDistance(touch_input_cp, corr_cp);
        f.geometry_distance_per_letter = correction_geometry_distance;
        f.geometry_distance_original = original_geometry_distance;
        f.geometry_distance_rel_gap =
            correction_geometry_distance - original_geometry_distance;
      }

      feature_batch.push_back(f);
    }

    auto scores = ranker_.Score(feature_batch);
    for (size_t i = 0; i < scores.size() && i < response.suggestions.size(); ++i) {
      if (response.suggestions[i].tag == "manual_rule") {
        response.suggestions[i].score = 10000.0f;
      } else {
        response.suggestions[i].score = scores[i];
      }
    }
  }

  // --- Sort by ML score, truncate ---
  std::stable_sort(response.suggestions.begin(), response.suggestions.end(),
                   [](const Suggestion& a, const Suggestion& b) {
                     return a.score > b.score;
                   });

  // --- Prepend neural word-chain suggestions (Yandex word LM) ---
  // The neural decoder ranks completions far better than the heuristic pools, so
  // its own order is preserved: leading manual_rule/exact entries stay first,
  // then the neural words (deduped against the existing list), then the rest.
  // Requires >=2 Cyrillic letters to keep single-key latency low. Additive: with
  // the chain unloaded (host / missing assets) this is a no-op.
  // >=4 bytes (>=2 Cyrillic letters) = word completion; empty = next-word
  // prediction (decode the sentence continuation from the context state).
  if (word_chain_.is_loaded() && (normalized.size() >= 4 || normalized.empty())) {
    // Up to 5 most-recent previous words (oldest-first, normalized) as the
    // neural encoder context, so completions are conditioned on the sentence.
    std::vector<std::string> wc_context;
    {
      const auto& pw = request.prev_words_info;
      const size_t take = std::min<size_t>(pw.size(), 5);
      for (size_t i = pw.size() - take; i < pw.size(); ++i) {
        std::string t = NormalizeToken(pw[i].token);
        if (!t.empty()) wc_context.push_back(std::move(t));
      }
    }
    std::vector<std::pair<std::string, float>> neural =
        word_chain_.Predict(normalized, wc_context, max);
    // Re-rank the neural candidates so the order matches the real Yandex ranker
    // (which strongly favors frequent inflected words; the raw neural logit
    // surfaces rare stems — phone "прог" -> Программа/Проголосовала, raw logit ->
    // прогами/прогул). stable_sort keeps the logit (context) order among ties.
    // MatrixNet re-ranking of the neural candidates regressed vs the step-1
    // frequency sort on device (its top features lm_ngr_main / distance_transfeme
    // are not faithfully fed for pure completions — left 0 — so the trees float
    // less-frequent forms like "прогрел" above "программа"). Neural therefore uses
    // the validated frequency re-rank; MatrixNet stays active for the heuristic
    // candidate pool via ranker_.Score. Flip to re-enable once features 1/8/16/17
    // are recovered.
    constexpr bool kMatrixNetForNeural = false;
    if (kMatrixNetForNeural && !normalized.empty() &&
        ranker_.is_matrixnet_loaded() && nnlm_.has_vocabulary()) {
      // Step 2: score each candidate through the reverse-engineered MatrixNet
      // over the features we can compute faithfully (word frequency via
      // lm_1gr_static, in-dict, length, edit + string metrics vs the prefix).
      // The neural n-gram (lm_ngr_main) is left 0 — its phone-side scale is not
      // recovered — so the trees rank on frequency + string structure.
      const auto orig_cp = DecodeUtf8(normalized);
      const auto nsw = [](int32_t w) {
        return w <= 0 ? 0.0f
                      : std::min(1.0f, std::log1p(static_cast<float>(w)) /
                                           std::log(1101.0f));
      };
      const float orig_lm = nsw(nnlm_.VocabularyWeight(normalized));
      for (auto& nw : neural) {
        const auto corr_cp = DecodeUtf8(nw.first);
        RankerFeatures f;
        f.original_length = static_cast<float>(orig_cp.size());
        f.correction_length = static_cast<float>(corr_cp.size());
        f.original_ndict = exact_input_known ? 1.0f : 0.0f;
        f.correction_ndict = 1.0f;  // neural candidates are vocabulary words
        f.neural_oov = 0.0f;
        f.no_change = (nw.first == normalized) ? 1.0f : 0.0f;
        f.levenshtein =
            static_cast<float>(LevenshteinDistanceUtf8(normalized, nw.first));
        f.levenshtein_per_letter =
            f.levenshtein / std::max(f.correction_length, 1.0f);
        const float corr_lm = nsw(nnlm_.VocabularyWeight(nw.first));
        f.original_lm_1gr_static = orig_lm;
        f.correction_lm_1gr_static = corr_lm;
        f.original_lm_2gr_static = orig_lm;
        f.correction_lm_2gr_static = corr_lm;
        f.original_lm_ngr_static = orig_lm;
        f.correction_lm_ngr_static = corr_lm;
        f.bleu_2 = CharNgramPrecision(orig_cp, corr_cp, 2);
        f.bleu_3 = CharNgramPrecision(orig_cp, corr_cp, 3);
        f.bleu_4 = CharNgramPrecision(orig_cp, corr_cp, 4);
        f.letter_precision_2 = f.bleu_2;
        f.letter_precision_3 = f.bleu_3;
        if (!corr_cp.empty()) {
          std::unordered_map<char32_t, int> oc;
          for (auto c : orig_cp) oc[c]++;
          int m1 = 0;
          for (auto c : corr_cp) {
            auto it = oc.find(c);
            if (it != oc.end() && it->second > 0) { ++m1; --(it->second); }
          }
          f.letter_precision_1 = static_cast<float>(m1) / f.correction_length;
        }
        f.is_permutation = IsPermutation(orig_cp, corr_cp) ? 1.0f : 0.0f;
        f.kendall_tau_distance = KendallTauNormalized(orig_cp, corr_cp);
        f.combo_len_diff = f.correction_length - f.original_length;
        f.combo_lm_static_diff = corr_lm - orig_lm;
        nw.second = ranker_.ScoreOne(f);
      }
      std::stable_sort(neural.begin(), neural.end(),
                       [](const std::pair<std::string, float>& a,
                          const std::pair<std::string, float>& b) {
                         return a.second > b.second;
                       });
    } else if (!normalized.empty() && nnlm_.has_vocabulary()) {
      // Fallback (no MatrixNet asset): frequency-dominant re-rank, logit tie-break.
      for (auto& nw : neural) {
        const int32_t w = nnlm_.VocabularyWeight(nw.first);
        nw.second = static_cast<float>(std::max(w, 0)) + 0.01f * nw.second;
      }
      std::stable_sort(neural.begin(), neural.end(),
                       [](const std::pair<std::string, float>& a,
                          const std::pair<std::string, float>& b) {
                         return a.second > b.second;
                       });
    }
    if (!neural.empty()) {
      std::unordered_set<std::string> neural_set;
      for (const auto& nw : neural) {
        neural_set.insert(nw.first);
      }
      std::vector<Suggestion> lead;
      std::vector<Suggestion> rest;
      for (auto& s : response.suggestions) {
        if (neural_set.find(s.text) != neural_set.end()) {
          continue;  // dedup against the neural list
        }
        if (s.tag == "manual_rule" || s.tag == "exact") {
          lead.push_back(std::move(s));
        } else {
          rest.push_back(std::move(s));
        }
      }
      std::vector<Suggestion> merged;
      merged.reserve(lead.size() + neural.size() + rest.size());
      for (auto& s : lead) {
        merged.push_back(std::move(s));
      }
      for (const auto& nw : neural) {
        Suggestion s;
        s.text = nw.first;
        s.tag = "neural";
        s.type_flags = normalized.empty() ? kSuggestionFlagPrediction
                                          : kSuggestionFlagCompletion;
        s.score = nw.second;
        merged.push_back(std::move(s));
      }
      for (auto& s : rest) {
        merged.push_back(std::move(s));
      }
      response.suggestions = std::move(merged);
    }
  }

  if (response.suggestions.size() > max) {
    response.suggestions.resize(max);
  }

  // --- Identity fallback ---
  if (response.suggestions.empty()) {
    Suggestion s;
    s.text = request_input;
    s.tag = "identity";
    s.type_flags = kSuggestionFlagExact;
    s.score = 0.0f;
    response.suggestions.push_back(std::move(s));
  }

  // --- Autocorrect decision ---
  if (request.want_autocorrect && !response.will_autocorrect &&
      !response.suggestions.empty()) {
    const auto& top = response.suggestions.front();

    // Autocorrect if: input not known + top is close (Lev ≤ 1) + not blocked
    if (top.tag != "exact" && top.tag != "identity") {
      bool input_known = exact_input_known;
      if (!input_known && !native_prefix_input_known) {
        const std::string norm_top = NormalizeToken(top.text);
        size_t dist = LevenshteinDistanceUtf8(normalized, norm_top);
        const bool safe_autocorrect_source =
            top.tag == "autocorrect" ||
            top.tag == "tfmtrie" ||
            (top.tag == "fuzzy" && norm_top.size() >= normalized.size());
        if (safe_autocorrect_source && dist <= 1 && !IsAutocorrectBlocked(norm_top)) {
          response.will_autocorrect = true;
        }
      }
    }
  }

  // --- Diagnostics ---
  response.diagnostics = "engine=hoskey";
  if (succinct_trie_.loaded()) {
    response.diagnostics += ", succinct=ok";
  }
  if (one_nc7_trie_.loaded()) {
    response.diagnostics += ", one_nc7=ok";
    if (!one_nc7_trie_.levels().empty()) {
      response.diagnostics += ", one_nc7_levels=" +
                              std::to_string(one_nc7_trie_.levels().size());
      const size_t suffix_count = one_nc7_trie_.SuffixEntryCount(2);
      if (suffix_count != 0) {
        response.diagnostics += ", one_nc7_suffix=" +
                                std::to_string(suffix_count);
      }
    }
  }
  if (tfmtrie_.is_parsed()) {
    response.diagnostics += ", tfm=" + std::to_string(tfmtrie_.class_count()) + "cls";
  }
  if (lemmer_.loaded()) {
    response.diagnostics += ", lemmer=ok";
  }
  if (normdict_.loaded()) {
    response.diagnostics += ", normdict=" + normdict_.language();
  }
  if (ranker_.is_loaded()) {
    response.diagnostics += ", ranker=" + ranker_.diagnostics();
  } else if (ranker_model_asset_present_) {
    response.diagnostics += ", ranker=" + ranker_.diagnostics();
  } else {
    response.diagnostics += ", ranker=none";
  }
  if (nnlm_.is_loaded() || nnlm_.has_vocabulary()) {
    response.diagnostics += ", nnlm=" + nnlm_.diagnostics();
  } else if (nnlm_model_asset_present_) {
    response.diagnostics += ", nnlm=" + nnlm_.diagnostics();
  } else {
    response.diagnostics += ", nnlm=none";
  }
  if (predictor_.is_built()) {
    response.diagnostics += ", predictor=loaded:words=" +
                            std::to_string(predictor_.word_count()) +
                            ",nodes=" +
                            std::to_string(predictor_.node_count());
  } else if (predictor_asset_present_) {
    response.diagnostics += ", predictor=invalid";
  } else {
    response.diagnostics += ", predictor=none";
  }
  if (word_chain_.is_loaded()) {
    response.diagnostics += ", word_chain=" + word_chain_.diagnostics();
  } else if (word_chain_asset_present_) {
    response.diagnostics += ", word_chain=" + word_chain_.diagnostics();
  } else {
    response.diagnostics += ", word_chain=none";
  }
  if (!blacklist_.empty()) {
    response.diagnostics += ", bl=" + std::to_string(blacklist_.size());
  }
  if (prev_context_count > 0) {
    response.diagnostics += ", prev_ctx=" + std::to_string(prev_context_count);
  }
  if (!request.full_context.empty()) {
    response.diagnostics +=
        ", full_ctx=" + std::to_string(request.full_context.size());
  }
  if (!request.options_array.empty()) {
    response.diagnostics += ", opts=" + std::to_string(request.options_array.size());
  }
  if (!request.dictionary_sessions.empty()) {
    response.diagnostics +=
        ", sessions=" + std::to_string(request.dictionary_sessions.size());
  }
  if (request.proximity_info.enabled) {
    response.diagnostics += ", prox=" + request.proximity_info.layout;
    response.diagnostics += ", pts=" + std::to_string(request.points.size());
    const auto touch_input_cp = ReconstructTouchInputCodepoints(request.points);
    if (!touch_input_cp.empty()) {
      response.diagnostics += ", touch_seq=" + std::to_string(touch_input_cp.size());
    }
  }
  if (!request.package_name.empty()) {
    response.diagnostics += ", pkg=" + request.package_name;
  }
  if (request.field_action_id != 0) {
    response.diagnostics += ", action=" + std::to_string(request.field_action_id);
  }
  if (request.emoji_factor > 0.0) {
    response.diagnostics +=
        ", emoji=" + std::to_string(request.emoji_factor);
  }
  if (request.is_urgent) {
    response.diagnostics += ", urgent=1";
  }
  if (request.gender != 0) {
    response.diagnostics += ", gender=" + std::to_string(request.gender);
  }
  if (request.age != 0) {
    response.diagnostics += ", age=" + std::to_string(request.age);
  }

  return response;
}

// ============================================================================
// CheckWord — word validation with lemmatization
// ============================================================================

CheckWordResponse KeyboardEngine::CheckWord(const CheckWordRequest& request) const {
  CheckWordResponse response;

  if (!package_.is_open()) {
    response.status = Status::InvalidState("Engine not open");
    return response;
  }
  if (request.token.empty()) {
    response.status = Status::InvalidArgument("Empty token");
    return response;
  }

  const std::string normalized = NormalizeToken(request.token);
  response.normalized_form = normalized;

  // Check blacklist
  if (IsBlacklisted(normalized)) {
    response.is_blocked = true;
    response.diagnostics = "blacklisted";
    return response;
  }

  // Direct trie lookup
  if (IsKnownWord(normalized)) {
    response.is_known = true;
    response.diagnostics = "exact";
    return response;
  }

  // Manual rules
  if (auto it = manual_rules_.find(normalized); it != manual_rules_.end()) {
    response.is_known = true;
    response.normalized_form = it->second;
    response.diagnostics = "manual_rule";
    return response;
  }

  // Lemmatization fallback
  if (lemmer_.loaded()) {
    std::string lemma = lemmer_.Lemmatize(normalized);
    if (lemma != normalized && !lemma.empty()) {
      if (IsKnownWord(lemma)) {
        response.is_known = true;
        response.normalized_form = lemma;
        response.diagnostics = "lemma";
        return response;
      }
    }
  }

  response.diagnostics = "unknown";
  return response;
}

PredictorModeCompareResponse KeyboardEngine::ComparePredictorModes(
    const SuggestRequest& request) const {
  PredictorModeCompareResponse response;

  if (!package_.is_open() || package_root_.empty()) {
    response.status = Status::InvalidState("Engine not open");
    return response;
  }

  response.status = Status::Ok();
  response.diagnostics = "predictor_mode_compare_removed";
  return response;
}

}  // namespace hoskey::engine
