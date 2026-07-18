#include "keyboard_engine.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <numeric>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "text_utils.h"
#include "swipe_decoder.h"

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

std::string FormatDiagnosticFloat(float value) {
  char buffer[32] = {};
  std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
  return buffer;
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
constexpr float kContextEmojiConfidenceThreshold = 0.5f;

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

std::vector<SwipePoint> ExtractGesturePath(
    const std::vector<SuggestRequest::TouchPoint>& points) {
  if (points.size() < 3) return {};
  size_t begin = points.size();
  for (size_t i = points.size(); i-- > 0;) {
    if ((points[i].key_code & kTouchPhaseMask) != 0) continue;
    bool has_move = false;
    for (size_t j = i + 1; j < points.size(); ++j) {
      if ((points[j].key_code & kTouchPhaseMask) == 0x20000000u) {
        has_move = true;
        break;
      }
    }
    if (has_move) {
      begin = i;
      break;
    }
  }
  if (begin == points.size()) return {};
  std::vector<SwipePoint> path;
  path.reserve(points.size() - begin);
  for (size_t i = begin; i < points.size(); ++i) {
    path.push_back({points[i].x, points[i].y, points[i].time_ms});
  }
  return path;
}

std::string ValidateGestureInput(const SuggestRequest& request) {
  if (request.points.empty()) return "invalid:empty_coordinates";
  if (std::any_of(request.points.begin(), request.points.end(),
                  [](const SuggestRequest::TouchPoint& point) {
                    return !std::isfinite(point.x) || !std::isfinite(point.y);
                  })) {
    return "invalid:nonfinite_coordinates";
  }
  if (!request.proximity_info.enabled) return "invalid:proximity_disabled";
  if (request.proximity_info.keyboard_width == 0 ||
      request.proximity_info.keyboard_height == 0) {
    return "invalid:keyboard_geometry";
  }
  if (request.proximity_info.layout.rfind("ru:", 0) != 0) {
    return "invalid:unsupported_layout";
  }
  return "valid";
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
  swipe_ranker_asset_present_ = false;
  swipe_backoff_asset_present_ = false;
  swipe_blocker_asset_present_ = false;
  nnlm_model_asset_present_ = false;
  predictor_asset_present_ = false;
  char_model_asset_present_ = false;
  emoji_search_asset_present_ = false;
  emoji_suggest_asset_present_ = false;
  tap_model_asset_present_ = false;
  autocc_model_asset_present_ = false;

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
      lexicon_asset_storage_.assign(data, data + data_size);
      (void)one_nc7_trie_.Parse(lexicon_asset_storage_.data(),
                                lexicon_asset_storage_.size());
      (void)succinct_trie_.Parse(lexicon_asset_storage_.data(),
                                 lexicon_asset_storage_.size(), false);
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

  // 3.5. Load the exact Yandex 3GRM character language model used by the
  // MatrixNet letter_lm_main features. This is the compact raw count model,
  // not the unrelated CharLMJoint TFLite projection graph.
  const auto* char_model_bin =
      FindFirstBinary(package_.store(), {"neural.char_model"});
  if (char_model_bin && !char_model_bin->bytes.empty()) {
    char_model_asset_present_ = true;
    (void)char_model_.Load(char_model_bin->bytes.data(),
                           char_model_bin->bytes.size());
  }

  // 4. Prefer the exact reverse-engineered MatrixNet ranker.  The older .ms
  // export has a different 500-feature contract; only try it as a fallback so
  // a valid HRNK asset does not trigger a known-unsupported runtime build.
  const auto* ranker_bin =
      FindFirstBinary(package_.store(), {"ranking.ranker", "ranker.default"});
  const auto* mn_bin =
      FindFirstBinary(package_.store(), {"ranking.matrixnet", "neural.ranker"});
  if (mn_bin && !mn_bin->bytes.empty()) {
    ranker_model_asset_present_ = true;
    (void)ranker_.LoadMatrixNet(mn_bin->bytes.data(), mn_bin->bytes.size());
  }
  if (!ranker_.is_matrixnet_loaded() && ranker_bin &&
      !ranker_bin->bytes.empty()) {
    ranker_model_asset_present_ = true;
    (void)ranker_.Load(ranker_bin->bytes.data(), ranker_bin->bytes.size());
  }

  // 4.1. Load the installed Yandex 122.5 TapModel chain atomically. A partial
  // bundle/schema/ranker/classifier set is reported but never activated.
  {
    const auto* bundle = package_.store().FindBinary("tap.models_default");
    const auto* schema =
        package_.store().FindFeatureConfig("features.tap_model");
    const auto* tap_ranker = package_.store().FindBinary("tap.ranker.hobl");
    const auto* classifier =
        package_.store().FindBinary("tap.classifier.hobl");
    const auto present = [](const LoadedBinaryAsset* asset) {
      return asset != nullptr && !asset->bytes.empty();
    };
    const bool schema_present = schema != nullptr && !schema->raw_json.empty();
    tap_model_asset_present_ = present(bundle) || schema_present ||
                               present(tap_ranker) || present(classifier);
    if (present(bundle) && schema_present && present(tap_ranker) &&
        present(classifier)) {
      TapModelAssets assets{
          {bundle->bytes.data(), bundle->bytes.size()},
          {reinterpret_cast<const uint8_t*>(schema->raw_json.data()),
           schema->raw_json.size()},
          {tap_ranker->bytes.data(), tap_ranker->bytes.size()},
          {classifier->bytes.data(), classifier->bytes.size()},
      };
      (void)tap_model_runner_.Load(assets);
    }
  }

  // 4.2. Load the recovered Autocc default/K13321/K13421 forests as one
  // schema-bound unit. Any missing or invalid member leaves the neural decision
  // disabled while the existing TfmTrie/TapModel policy remains available.
  {
    const auto* schema = package_.store().FindFeatureConfig("features.autocc");
    const auto* model_default =
        package_.store().FindBinary("autocc.tree.default.hobl");
    const auto* model_k13321 =
        package_.store().FindBinary("autocc.tree.k13321.hobl");
    const auto* model_k13421 =
        package_.store().FindBinary("autocc.tree.k13421.hobl");
    const auto present = [](const LoadedBinaryAsset* asset) {
      return asset != nullptr && !asset->bytes.empty();
    };
    const bool schema_present = schema != nullptr && !schema->raw_json.empty();
    autocc_model_asset_present_ = schema_present || present(model_default) ||
                                  present(model_k13321) || present(model_k13421);
    if (schema_present && present(model_default) && present(model_k13321) &&
        present(model_k13421)) {
      const AutoccModelAssets assets{
          {reinterpret_cast<const uint8_t*>(schema->raw_json.data()),
           schema->raw_json.size()},
          {model_default->bytes.data(), model_default->bytes.size()},
          {model_k13321->bytes.data(), model_k13321->bytes.size()},
          {model_k13421->bytes.data(), model_k13421->bytes.size()},
      };
      (void)autocc_model_runner_.Load(assets);
    }
  }

  // 4.5. Load the three recovered swipe ensembles.  HOSKEY's geometry decoder
  // keeps the 42-feature Backoff model as the bounded fallback.  The primary
  // 47-feature path engages only after RNNT encoder/predictor scoring succeeds.
  const auto* swipe_mn = FindFirstBinary(
      package_.store(), {"ranking.swipe.matrixnet", "ranking.swipe"});
  if (swipe_mn && !swipe_mn->bytes.empty()) {
    swipe_ranker_asset_present_ = true;
    (void)swipe_ranker_.LoadMatrixNet(swipe_mn->bytes.data(),
                                      swipe_mn->bytes.size());
  }
  const auto* swipe_backoff_mn = FindFirstBinary(
      package_.store(),
      {"ranking.swipe_backoff.matrixnet", "ranking.swipe_backoff"});
  if (swipe_backoff_mn && !swipe_backoff_mn->bytes.empty()) {
    swipe_backoff_asset_present_ = true;
    (void)swipe_backoff_ranker_.LoadMatrixNet(
        swipe_backoff_mn->bytes.data(), swipe_backoff_mn->bytes.size());
  }
  const auto* swipe_blocker_mn = FindFirstBinary(
      package_.store(),
      {"ranking.swipe_blocker.matrixnet", "ranking.swipe_blocker"});
  if (swipe_blocker_mn && !swipe_blocker_mn->bytes.empty()) {
    swipe_blocker_asset_present_ = true;
    (void)swipe_blocker_.LoadMatrixNet(swipe_blocker_mn->bytes.data(),
                                       swipe_blocker_mn->bytes.size());
  }

  // 4.6. Load the ten learned RNNT encoder/joint graphs plus the dedicated
  // converted m0 predictor. Its 750-wide hidden output is consumed by the RNNT
  // joint graph. neural.char.dec is a separate exact native YCD1 scorer.
  {
    const auto* sampling = package_.store().FindBinary("rnnt.sampling");
    const std::array<const LoadedBinaryAsset*, 4> pre = {
        package_.store().FindBinary("rnnt.pre0"),
        package_.store().FindBinary("rnnt.pre1"),
        package_.store().FindBinary("rnnt.pre2"),
        package_.store().FindBinary("rnnt.pre3")};
    const std::array<const LoadedBinaryAsset*, 4> post = {
        package_.store().FindBinary("rnnt.post0"),
        package_.store().FindBinary("rnnt.post1"),
        package_.store().FindBinary("rnnt.post2"),
        package_.store().FindBinary("rnnt.post3")};
    const auto* joint = package_.store().FindBinary("rnnt.joint");
    const auto* predictor =
        package_.store().FindBinary("rnnt.predictor.char_dec");
    const auto present = [](const LoadedBinaryAsset* binary) {
      return binary != nullptr && !binary->bytes.empty();
    };
    const bool all_present =
        present(sampling) && present(joint) && present(predictor) &&
        std::all_of(pre.begin(), pre.end(), present) &&
        std::all_of(post.begin(), post.end(), present);
    if (all_present) {
      rnnt_components_asset_present_ = true;
      RnntSwipeModelBuffers buffers;
      buffers.sampling = {sampling->bytes.data(), sampling->bytes.size()};
      buffers.joint = {joint->bytes.data(), joint->bytes.size()};
      buffers.predictor = {predictor->bytes.data(), predictor->bytes.size()};
      for (size_t i = 0; i < pre.size(); ++i) {
        buffers.pre[i] = {pre[i]->bytes.data(), pre[i]->bytes.size()};
        buffers.post[i] = {post[i]->bytes.data(), post[i]->bytes.size()};
      }
      (void)rnnt_swipe_model_.Load(buffers);
    }
  }

  // 5. Load the raw ynnlm vocabulary first. The legacy standalone .ms model has
  // a single decoder input/output and does not satisfy NeuralLM's recurrent
  // 2-input/3-output contract; use it only when the target raw bundle is absent.
  const auto* raw_nnlm_bin = FindFirstBinary(
      package_.store(),
      {"neural.word_model.raw", "neural.word_model",
       "neural.subword_nnlm.KEYBOARD-13321"});
  if (raw_nnlm_bin && !raw_nnlm_bin->bytes.empty()) {
    nnlm_model_asset_present_ = true;
    (void)nnlm_.LoadVocabulary(raw_nnlm_bin->bytes.data(),
                               raw_nnlm_bin->bytes.size());
  }
  const auto* nnlm_bin = FindFirstBinary(
      package_.store(),
      {"ranking.neural_model", "ranking.neural", "nnlm.default",
       "neural.default"});
  if (!raw_nnlm_bin && nnlm_bin && !nnlm_bin->bytes.empty()) {
    nnlm_model_asset_present_ = true;
    (void)nnlm_.Load(nnlm_bin->bytes.data(), nnlm_bin->bytes.size());
  }
  if (nnlm_.has_vocabulary()) {
    swipe_decoder_.Build(nnlm_.vocabulary_words(), nnlm_.vocabulary_ranks());
  }

  // 5.1. Load the recovered FlatBuffer catalog + UTF-16BE compact trie used
  // by Native$DictionaryFacilitator.getEmojis. This is a mapping model, not
  // the unrelated 525-wide NNLM embedding graph previously inferred from a
  // same-basename conversion.
  const auto* emoji_search_bin = FindFirstBinary(
      package_.store(), {"emoji.search_model.raw", "emoji.search_model"});
  if (emoji_search_bin && !emoji_search_bin->bytes.empty()) {
    emoji_search_asset_present_ = true;
    (void)emoji_search_model_.Load(emoji_search_bin->bytes.data(),
                                   emoji_search_bin->bytes.size());
  }

  // 5.2. Load the coherent K13321 context-emoji chain atomically. The K13321
  // and Default ynnlm wrappers have byte-for-byte equivalent vocabulary and
  // ApplicationId maps, so the already packaged core wrapper supplies that
  // metadata without duplicating another 44 MB blob.
  {
    const auto* wrapper =
        package_.store().FindBinary("emoji.context.wrapper.k13321");
    const auto* age =
        package_.store().FindBinary("emoji.context.emb_age.k13321");
    const auto* app =
        package_.store().FindBinary("emoji.context.emb_app.k13321");
    const auto* sex =
        package_.store().FindBinary("emoji.context.emb_sex.k13321");
    const auto* word =
        package_.store().FindBinary("emoji.context.word_emb.k13321");
    const auto* encoder =
        package_.store().FindBinary("emoji.context.encoder.k13321");
    const auto* decoder =
        package_.store().FindBinary("emoji.context.decoder.k13321");
    const auto present = [](const LoadedBinaryAsset* asset) {
      return asset != nullptr && !asset->bytes.empty();
    };
    emoji_suggest_asset_present_ =
        present(wrapper) || present(age) || present(app) || present(sex) ||
        present(word) || present(encoder) || present(decoder);
    const bool all_present =
        raw_nnlm_bin && !raw_nnlm_bin->bytes.empty() && present(wrapper) &&
        present(age) && present(app) && present(sex) && present(word) &&
        present(encoder) && present(decoder);
    if (all_present) {
      EmojiSuggestModelBuffers buffers;
      buffers.age_embedding = {age->bytes.data(), age->bytes.size()};
      buffers.application_embedding = {app->bytes.data(), app->bytes.size()};
      buffers.sex_embedding = {sex->bytes.data(), sex->bytes.size()};
      buffers.word_embedding = {word->bytes.data(), word->bytes.size()};
      buffers.encoder = {encoder->bytes.data(), encoder->bytes.size()};
      buffers.decoder = {decoder->bytes.data(), decoder->bytes.size()};
      (void)emoji_suggest_runner_.Load(
          buffers, raw_nnlm_bin->bytes.data(), raw_nnlm_bin->bytes.size(),
          wrapper->bytes.data(), wrapper->bytes.size());
    }
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

  // 5.6. Load the neural word-suggestion chain: emb/encoder/decoder .ms models,
  // optional C++ fallback encoder weights, and the ynnlm word vocabulary. This is
  // an additive candidate generator that runs the reverse-engineered Yandex word
  // LM. Production uses the runtime encoder; --full-models may additionally pack
  // the large C++ fallback weights.
  {
    const auto* wc_emb = package_.store().FindBinary("neural.word.emb");
    const auto* wc_emb_age =
        package_.store().FindBinary("neural.word.emb_age");
    const auto* wc_emb_app = package_.store().FindBinary("neural.word.emb_app");
    const auto* wc_emb_sex =
        package_.store().FindBinary("neural.word.emb_sex");
    const auto* wc_runtime_enc = package_.store().FindBinary("neural.word.enc");
    const auto* wc_bridge = package_.store().FindBinary("neural.word.enc_dec");
    const auto* wc_dec = package_.store().FindBinary("neural.word.dec");
    const auto* sw_bridge =
        package_.store().FindBinary("neural.subword.enc_dec");
    const auto* sw_dec = package_.store().FindBinary("neural.subword.dec");
    const auto* ch_dec = package_.store().FindBinary("neural.char.dec");
    const auto* wc_enc = package_.store().FindBinary("neural.word.encoder");
    const auto* wc_vocab = FindFirstBinary(
        package_.store(), {"neural.word_model.raw", "neural.word_model"});
    if (wc_emb && wc_bridge && wc_dec && wc_vocab &&
        !wc_emb->bytes.empty() && !wc_bridge->bytes.empty() &&
        !wc_dec->bytes.empty() && !wc_vocab->bytes.empty()) {
      word_chain_asset_present_ = true;
      (void)word_chain_.Load(wc_emb->bytes.data(), wc_emb->bytes.size(),
                             wc_bridge->bytes.data(), wc_bridge->bytes.size(),
                             wc_dec->bytes.data(), wc_dec->bytes.size(),
                             wc_enc && !wc_enc->bytes.empty()
                                 ? wc_enc->bytes.data() : nullptr,
                             wc_enc ? wc_enc->bytes.size() : 0,
                             wc_vocab->bytes.data(), wc_vocab->bytes.size(),
                             wc_emb_age && !wc_emb_age->bytes.empty()
                                 ? wc_emb_age->bytes.data() : nullptr,
                             wc_emb_age ? wc_emb_age->bytes.size() : 0,
                             wc_emb_app && !wc_emb_app->bytes.empty()
                                 ? wc_emb_app->bytes.data() : nullptr,
                             wc_emb_app ? wc_emb_app->bytes.size() : 0,
                             wc_emb_sex && !wc_emb_sex->bytes.empty()
                                 ? wc_emb_sex->bytes.data() : nullptr,
                             wc_emb_sex ? wc_emb_sex->bytes.size() : 0,
                             wc_runtime_enc && !wc_runtime_enc->bytes.empty()
                                 ? wc_runtime_enc->bytes.data() : nullptr,
                             wc_runtime_enc ? wc_runtime_enc->bytes.size() : 0,
                             sw_bridge && !sw_bridge->bytes.empty()
                                 ? sw_bridge->bytes.data() : nullptr,
                             sw_bridge ? sw_bridge->bytes.size() : 0,
                             sw_dec && !sw_dec->bytes.empty()
                                 ? sw_dec->bytes.data() : nullptr,
                             sw_dec ? sw_dec->bytes.size() : 0,
                             ch_dec && !ch_dec->bytes.empty()
                                 ? ch_dec->bytes.data() : nullptr,
                             ch_dec ? ch_dec->bytes.size() : 0);
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

void KeyboardEngine::RecordModelNegativeAuditCase(std::string_view name,
                                                  bool passed) {
  ++model_negative_audit_total_;
  if (passed) {
    ++model_negative_audit_passed_;
  } else {
    model_negative_audit_failures_.emplace_back(name);
  }
}

void KeyboardEngine::RunModelNegativeAuditPreload() {
  model_negative_audit_diagnostics_ = "model_negative_audit={status=running}";
  model_negative_audit_passed_ = 0;
  model_negative_audit_total_ = 0;
  model_negative_audit_rnnt_missing_passed_ = 0;
  model_negative_audit_ynnlm_missing_passed_ = 0;
  model_negative_audit_failures_.clear();

  HosdictPackage malformed_package;
  const Status malformed_status = malformed_package.OpenFromDir(
      package_root_ / "__hoskey_model_negative_audit_missing__");
  RecordModelNegativeAuditCase("malformed_path", !malformed_status.ok());

  RnntSwipeModelBuffers rnnt_buffers;
  const auto load_rnnt_buffer = [&](const char* key,
                                    RnntModelBuffer* output) -> bool {
    const auto* asset = package_.store().FindBinary(key);
    if (asset == nullptr || asset->bytes.empty()) return false;
    *output = {asset->bytes.data(), asset->bytes.size()};
    return true;
  };
  bool rnnt_assets_present =
      load_rnnt_buffer("rnnt.sampling", &rnnt_buffers.sampling) &&
      load_rnnt_buffer("rnnt.joint", &rnnt_buffers.joint) &&
      load_rnnt_buffer("rnnt.predictor.char_dec", &rnnt_buffers.predictor);
  for (size_t i = 0; i < rnnt_buffers.pre.size(); ++i) {
    const std::string pre_key = "rnnt.pre" + std::to_string(i);
    const std::string post_key = "rnnt.post" + std::to_string(i);
    rnnt_assets_present =
        load_rnnt_buffer(pre_key.c_str(), &rnnt_buffers.pre[i]) &&
        load_rnnt_buffer(post_key.c_str(), &rnnt_buffers.post[i]) &&
        rnnt_assets_present;
  }
  std::string rnnt_diagnostics;
  const Status rnnt_coherent =
      rnnt_assets_present
          ? RnntSwipeModel::ValidateBuffers(rnnt_buffers, &rnnt_diagnostics)
          : Status::NotFound("RNNT audit assets missing");
  RecordModelNegativeAuditCase(
      "rnnt_coherent_serialized",
      rnnt_coherent.ok() && rnnt_diagnostics == "valid:11_models");

  if (rnnt_assets_present) {
    const std::array<std::string, 11> names = {
        "sampling", "pre0", "pre1", "pre2", "pre3", "post0",
        "post1", "post2", "post3", "joint", "predictor"};
    for (size_t omitted = 0; omitted < names.size(); ++omitted) {
      auto missing = rnnt_buffers;
      if (omitted == 0) {
        missing.sampling = {};
      } else if (omitted <= 4) {
        missing.pre[omitted - 1] = {};
      } else if (omitted <= 8) {
        missing.post[omitted - 5] = {};
      } else if (omitted == 9) {
        missing.joint = {};
      } else {
        missing.predictor = {};
      }
      rnnt_diagnostics.clear();
      const Status missing_status =
          RnntSwipeModel::ValidateBuffers(missing, &rnnt_diagnostics);
      const bool passed =
          !missing_status.ok() &&
          rnnt_diagnostics.find(names[omitted]) != std::string::npos &&
          rnnt_diagnostics.find(":missing") != std::string::npos;
      if (passed) ++model_negative_audit_rnnt_missing_passed_;
      RecordModelNegativeAuditCase("rnnt_missing_" + names[omitted], passed);
    }

    std::array<uint8_t, 8> corrupt_mindir{};
    auto corrupt = rnnt_buffers;
    corrupt.sampling = {corrupt_mindir.data(), corrupt_mindir.size()};
    rnnt_diagnostics.clear();
    const Status corrupt_status =
        RnntSwipeModel::ValidateBuffers(corrupt, &rnnt_diagnostics);
    RecordModelNegativeAuditCase(
        "rnnt_corrupt",
        !corrupt_status.ok() &&
            rnnt_diagnostics == "invalid:sampling:magic");

    auto mixed = rnnt_buffers;
    mixed.sampling = rnnt_buffers.joint;
    RnntSwipeModel mixed_model;
    const Status mixed_status = mixed_model.Load(mixed);
    RecordModelNegativeAuditCase(
        "rnnt_wrong_shape_variant_mix",
        mixed_status.ok() && !mixed_model.components_loaded() &&
            mixed_model.diagnostics() == "invalid:sampling_contract");
  } else {
    for (size_t i = 0; i < 13; ++i) {
      RecordModelNegativeAuditCase("rnnt_audit_precondition", false);
    }
  }

  const auto* ynnlm_emb = package_.store().FindBinary("neural.word.emb");
  const auto* ynnlm_bridge =
      package_.store().FindBinary("neural.word.enc_dec");
  const auto* ynnlm_dec = package_.store().FindBinary("neural.word.dec");
  const std::array<const LoadedBinaryAsset*, 3> ynnlm_required = {
      ynnlm_emb, ynnlm_bridge, ynnlm_dec};
  const std::array<const char*, 3> ynnlm_names = {"emb", "enc_dec", "dec"};
  bool ynnlm_assets_present = true;
  for (size_t i = 0; i < ynnlm_required.size(); ++i) {
    const auto* asset = ynnlm_required[i];
    const bool present = asset != nullptr && !asset->bytes.empty();
    ynnlm_assets_present = ynnlm_assets_present && present;
    const Status serialized = present
        ? YnnlmWordChain::ValidateSerializedModel(
              asset->bytes.data(), asset->bytes.size(), ynnlm_names[i])
        : Status::NotFound("YNNLM audit asset missing");
    RecordModelNegativeAuditCase(
        std::string("ynnlm_serialized_") + ynnlm_names[i], serialized.ok());

    const Status missing = YnnlmWordChain::ValidateSerializedModel(
        nullptr, 0, ynnlm_names[i]);
    const bool missing_passed =
        !missing.ok() && missing.message.find("missing") != std::string::npos;
    if (missing_passed) ++model_negative_audit_ynnlm_missing_passed_;
    RecordModelNegativeAuditCase(
        std::string("ynnlm_missing_") + ynnlm_names[i], missing_passed);
  }
  std::array<uint8_t, 8> corrupt_ynnlm{};
  const Status corrupt_ynnlm_status =
      YnnlmWordChain::ValidateSerializedModel(
          corrupt_ynnlm.data(), corrupt_ynnlm.size(), "emb");
  RecordModelNegativeAuditCase(
      "ynnlm_corrupt",
      !corrupt_ynnlm_status.ok() &&
          corrupt_ynnlm_status.message.find("invalid MindIR magic") !=
              std::string::npos);
  if (ynnlm_assets_present) {
    const Status wrong_shape = YnnlmWordChain::ProbeModelContract(
        ynnlm_bridge->bytes.data(), ynnlm_bridge->bytes.size(),
        YnnlmModelContract::kWordEmbedding);
    RecordModelNegativeAuditCase(
        "ynnlm_wrong_shape_variant_mix",
        !wrong_shape.ok() &&
            wrong_shape.message.find("contract mismatch") != std::string::npos);
  } else {
    RecordModelNegativeAuditCase("ynnlm_wrong_shape_variant_mix", false);
  }
}

void KeyboardEngine::RunModelNegativeAuditInputs() {
  SuggestRequest base;
  base.token = "п";
  base.input = "п";
  base.locale = "ru-RU";
  base.want_autocorrect = false;
  base.max_suggestions = 6;
  base.is_gesture = true;
  base.proximity_info.enabled = true;
  base.proximity_info.layout = "ru:alphabet";
  base.proximity_info.keyboard_width = 1000;
  base.proximity_info.keyboard_height = 400;

  const SuggestResponse empty = GetSuggestions(base);
  RecordModelNegativeAuditCase(
      "rnnt_empty_coordinates",
      empty.status.ok() && empty.suggestions.empty() &&
          empty.diagnostics.find("swipe_input=invalid:empty_coordinates") !=
              std::string::npos);

  SuggestRequest nonfinite = base;
  nonfinite.points = {
      {std::numeric_limits<double>::quiet_NaN(), 100.0, 100, 1087},
      {200.0, 100.0, 120, 0x20000000u | 1088u},
      {300.0, 100.0, 140, 0x40000000u | 1080u}};
  const SuggestResponse nonfinite_response = GetSuggestions(nonfinite);
  RecordModelNegativeAuditCase(
      "rnnt_nan_coordinates",
      nonfinite_response.status.ok() &&
          nonfinite_response.suggestions.empty() &&
          nonfinite_response.diagnostics.find(
              "swipe_input=invalid:nonfinite_coordinates") !=
              std::string::npos);

  SuggestRequest unsupported_layout = base;
  unsupported_layout.proximity_info.layout = "en:alphabet";
  unsupported_layout.points = {
      {100.0, 100.0, 100, 1087},
      {200.0, 100.0, 120, 0x20000000u | 1088u},
      {300.0, 100.0, 140, 0x40000000u | 1080u}};
  const SuggestResponse layout_response = GetSuggestions(unsupported_layout);
  RecordModelNegativeAuditCase(
      "rnnt_unsupported_layout",
      layout_response.status.ok() && layout_response.suggestions.empty() &&
          layout_response.diagnostics.find(
              "swipe_input=invalid:unsupported_layout") !=
              std::string::npos);
}

void KeyboardEngine::FinalizeModelNegativeAuditDiagnostics() {
  const bool passed =
      model_negative_audit_total_ != 0 &&
      model_negative_audit_passed_ == model_negative_audit_total_;
  std::ostringstream diagnostics;
  diagnostics << "model_negative_audit={status="
              << (passed ? "pass" : "fail")
              << ";passed=" << model_negative_audit_passed_
              << ";total=" << model_negative_audit_total_
              << ";rnnt_missing="
              << model_negative_audit_rnnt_missing_passed_ << "/11"
              << ";ynnlm_missing="
              << model_negative_audit_ynnlm_missing_passed_ << "/3"
              << ";rnnt_corrupt=covered"
              << ";rnnt_wrong_shape=covered"
              << ";ynnlm_corrupt=covered"
              << ";ynnlm_wrong_shape=covered"
              << ";empty_nan_layout=covered"
              << ";malformed_path=covered";
  if (!model_negative_audit_failures_.empty()) {
    diagnostics << ";failures=";
    for (size_t i = 0; i < model_negative_audit_failures_.size(); ++i) {
      if (i != 0) diagnostics << '|';
      diagnostics << model_negative_audit_failures_[i];
    }
  }
  diagnostics << '}';
  model_negative_audit_diagnostics_ = diagnostics.str();
}

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

  if (debug_config_.run_model_negative_audit) {
    RunModelNegativeAuditPreload();
  }

  s = InitSubsystems();
  if (!s.ok()) {
    Close();
    return s;
  }
  if (debug_config_.run_model_negative_audit) {
    RunModelNegativeAuditInputs();
    FinalizeModelNegativeAuditDiagnostics();
  }
  // OH_AI_ModelBuild compiles model buffers into model handles; all native
  // parsers above also own their decoded state. Drop the duplicate container
  // and per-asset byte vectors before the synchronous keyboard path starts.
  package_.ReleaseSourcePayloads();
  return Status::Ok();
}

void KeyboardEngine::Close() {
  package_.Close();
  tfmtrie_ = TfmTrieDecoder{};
  one_nc7_trie_ = OneNc7Trie{};
  predictor_ = PredictorEngine{};
  lemmer_.Reset();
  ranker_.Reset();
  swipe_ranker_.Reset();
  swipe_backoff_ranker_.Reset();
  swipe_blocker_.Reset();
  rnnt_swipe_model_.Reset();
  swipe_decoder_.Reset();
  std::vector<uint8_t>().swap(lexicon_asset_storage_);
  nnlm_.Reset();
  char_model_.Reset();
  emoji_search_model_.Reset();
  emoji_suggest_runner_.Reset();
  tap_model_runner_.Reset();
  autocc_model_runner_.Reset();
  word_chain_.Reset();
  normdict_ = NormDictReader{};
  blacklist_.clear();
  autocorrect_blockers_.clear();
  ranker_model_asset_present_ = false;
  swipe_ranker_asset_present_ = false;
  swipe_backoff_asset_present_ = false;
  swipe_blocker_asset_present_ = false;
  rnnt_components_asset_present_ = false;
  nnlm_model_asset_present_ = false;
  predictor_asset_present_ = false;
  word_chain_asset_present_ = false;
  char_model_asset_present_ = false;
  emoji_search_asset_present_ = false;
  emoji_suggest_asset_present_ = false;
  tap_model_asset_present_ = false;
  autocc_model_asset_present_ = false;
  ynnlm_invoke_count_.store(0, std::memory_order_relaxed);
  ynnlm_scored_count_.store(0, std::memory_order_relaxed);
  rnnt_invoke_count_.store(0, std::memory_order_relaxed);
  rnnt_scored_count_.store(0, std::memory_order_relaxed);
  debug_config_ = KeyboardDebugConfig{};
  package_root_.clear();
  model_negative_audit_diagnostics_.clear();
  model_negative_audit_passed_ = 0;
  model_negative_audit_total_ = 0;
  model_negative_audit_rnnt_missing_passed_ = 0;
  model_negative_audit_ynnlm_missing_passed_ = 0;
  model_negative_audit_failures_.clear();
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
  if (request.sensitive_context) {
    response.diagnostics = "engine=hoskey, privacy=sensitive_blocked";
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

  if (request.is_gesture) {
    const auto started = std::chrono::steady_clock::now();
    std::string swipe_input = ValidateGestureInput(request);
    std::vector<SwipePoint> path;
    if (swipe_input == "valid") {
      path = ExtractGesturePath(request.points);
      if (path.empty()) swipe_input = "invalid:no_continuous_path";
    }
    std::vector<SwipeCandidate> decoded;
    SwipeDecodeDiagnostics swipe_diagnostics;
    if (swipe_input == "valid" && nnlm_.has_vocabulary()) {
      SwipeDecodeOptions options;
      options.max_results = max;
      options.context_size = std::max(request.previous_tokens.size(),
                                      request.prev_words_info.size());
      options.backoff_ranker = &swipe_backoff_ranker_;
      options.primary_ranker = &swipe_ranker_;
      options.blocker_ranker = &swipe_blocker_;
      options.rnnt_model = request.disable_rnnt ? nullptr : &rnnt_swipe_model_;
      options.enforce_blocker = true;
      decoded = swipe_decoder_.Decode(
          path, static_cast<double>(request.proximity_info.keyboard_width),
          static_cast<double>(request.proximity_info.keyboard_height),
          options, &swipe_diagnostics);
    }
    response.is_blocked = swipe_diagnostics.blocked;
    for (const SwipeCandidate& candidate : decoded) {
      if (IsBlacklisted(candidate.word)) continue;
      Suggestion suggestion;
      suggestion.text = candidate.word;
      suggestion.tag = swipe_diagnostics.primary_model_engaged
                           ? "swipe_rnnt_matrixnet"
                           : (swipe_diagnostics.backoff_model_engaged
                                  ? "swipe_matrixnet_backoff"
                                  : "swipe_geom");
      suggestion.type_flags = kSuggestionFlagPrediction;
      suggestion.score = candidate.score;
      response.suggestions.push_back(std::move(suggestion));
    }
    const int64_t elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started)
            .count();
    response.diagnostics =
        "engine=hoskey, swipe=" +
        std::string(swipe_diagnostics.primary_model_engaged
                        ? "rnnt_matrixnet_v1"
                        : (swipe_diagnostics.backoff_model_engaged
                               ? "matrixnet_backoff_v1"
                               : "geometry_v1")) +
        ", swipe_points=" +
        std::to_string(path.size()) + ", swipe_candidates=" +
        std::to_string(response.suggestions.size()) + ", swipe_pool=" +
        std::to_string(swipe_diagnostics.candidate_pool_size) +
        ", swipe_index_words=" +
        std::to_string(swipe_decoder_.indexed_word_count()) +
        ", swipe_elapsed_ms=" +
        std::to_string(elapsed);
    response.diagnostics += ", swipe_input=" + swipe_input;
    response.diagnostics +=
        ", swipe_model_scored=" +
        std::to_string(swipe_diagnostics.model_scored) +
        ", swipe_full=" +
        std::string(request.disable_rnnt
                        ? "disabled"
                        : (swipe_ranker_.is_matrixnet_loaded()
                               ? (rnnt_components_asset_present_
                                      ? (rnnt_swipe_model_.components_loaded()
                                             ? "ready"
                                             : "rnnt_runtime_failed")
                                      : "waiting_rnnt_components")
                               : (swipe_ranker_asset_present_ ? "invalid"
                                                               : "none")));
    response.diagnostics +=
        ", rnnt_components=" +
        std::string(rnnt_components_asset_present_
                        ? rnnt_swipe_model_.diagnostics()
                        : "none");
    response.diagnostics +=
        ", rnnt_frames=" + std::to_string(swipe_diagnostics.rnnt_frames) +
        ", rnnt_scored=" +
        std::to_string(swipe_diagnostics.rnnt_candidates_scored) +
        ", rnnt_joint_upper=" +
        std::to_string(
            swipe_diagnostics.rnnt_joint_evaluations_upper_bound) +
        ", rnnt_compute_units=" +
        std::to_string(swipe_diagnostics.rnnt_compute_units_upper_bound) +
        ", rnnt_budget_limited=" +
        std::string(swipe_diagnostics.rnnt_budget_limited ? "1" : "0") +
        ", rnnt_encode_ms=" +
        std::to_string(swipe_diagnostics.rnnt_encode_ms) +
        ", rnnt_score_ms=" +
        std::to_string(swipe_diagnostics.rnnt_score_ms) +
        ", rnnt_elapsed_ms=" +
        std::to_string(swipe_diagnostics.rnnt_elapsed_ms);
    if (std::isfinite(swipe_diagnostics.first_rnnt_score)) {
      response.diagnostics +=
          ", rnnt_first_score=" +
          FormatDiagnosticFloat(swipe_diagnostics.first_rnnt_score) +
          ", rnnt_first_predictor=" +
          FormatDiagnosticFloat(
              swipe_diagnostics.first_rnnt_predictor_score);
    }
    if (swipe_diagnostics.rnnt_frames > 0) {
      rnnt_invoke_count_.fetch_add(1, std::memory_order_relaxed);
      rnnt_scored_count_.fetch_add(
          swipe_diagnostics.rnnt_candidates_scored,
          std::memory_order_relaxed);
    }
    const std::string rnnt_contract =
        !rnnt_components_asset_present_
            ? "unsupported:missing_asset"
            : (rnnt_swipe_model_.components_loaded()
                   ? "valid"
                   : "unsupported:runtime");
    const std::string rnnt_load =
        !rnnt_components_asset_present_
            ? "not_attempted"
            : (rnnt_swipe_model_.components_loaded() ? "loaded"
                                                      : "failed:runtime");
    const std::string rnnt_fallback =
        request.disable_rnnt
            ? "disabled:debug_switch"
            : (swipe_diagnostics.rnnt_candidates_scored > 0
            ? "none"
            : (swipe_diagnostics.backoff_model_engaged
                   ? "matrixnet_backoff:no_rnnt_score"
                   : "geometry:no_rnnt_score"));
    response.diagnostics +=
        ", rnnt_route={asset=" +
        std::string(rnnt_components_asset_present_ ? "present" : "missing") +
        ";contract=" + rnnt_contract + ";variant=default;load=" + rnnt_load +
        ";invoke_count=" +
        std::to_string(rnnt_invoke_count_.load(std::memory_order_relaxed)) +
        ";scored_count=" +
        std::to_string(rnnt_scored_count_.load(std::memory_order_relaxed)) +
        ";fallback=" + rnnt_fallback + ";latency_us=" +
        std::to_string(swipe_diagnostics.rnnt_elapsed_ms * 1000ULL) + "}";
    if (swipe_diagnostics.blocker_model_engaged) {
      response.diagnostics +=
          ", swipe_blocker=" +
          FormatDiagnosticFloat(swipe_diagnostics.blocker_score) +
          (swipe_diagnostics.blocked ? ":blocked" : ":allow");
    } else {
      response.diagnostics +=
          ", swipe_blocker=" +
          std::string(swipe_blocker_asset_present_ ? "invalid" : "none");
    }
    if (!decoded.empty()) {
      response.diagnostics += ", swipe_top_cost=" +
                              FormatDiagnosticFloat(decoded.front().geometry_cost);
      if (std::isfinite(decoded.front().matrixnet_score)) {
        response.diagnostics +=
            ", swipe_top_mn=" +
            FormatDiagnosticFloat(decoded.front().matrixnet_score);
      }
    }
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
    const bool matrixnet_pool =
        ranker_.is_matrixnet_loaded() && word_chain_.is_loaded();
    const size_t vocab_limit = matrixnet_pool
        ? 64
        : std::min<size_t>(max, 6);
    auto vocab_results = nnlm_.VocabularyCompletions(
        normalized, vocab_limit, matrixnet_pool);
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
  bool ynnlm_batch_ran = false;
  std::string ynnlm_skip_reason;
  bool ynnlm_has_original_logit = false;
  float ynnlm_original_raw_logit = 0.0f;
  std::unordered_map<std::string, float> ynnlm_word_probabilities;
  std::string ynnlm_rerank_path = "none";
  std::string ynnlm_encoder_path = "not_run";
  bool ynnlm_word_cache_hit = false;
  bool ynnlm_app_embedding_used = false;
  bool ynnlm_app_embedding_failed = false;
  uint32_t ynnlm_app_id = 0;
  size_t ynnlm_context_words = 0;
  size_t ynnlm_model_scored = 0;
  std::string ynnlm_top_raw;
  bool ynnlm_char_ran = false;
  bool ynnlm_char_failed = false;
  bool ynnlm_char_skipped_dense = false;
  size_t ynnlm_char_raw_count = 0;
  size_t ynnlm_char_count = 0;
  std::string ynnlm_char_top_raw;
  std::string ynnlm_char_top_filtered;
  bool ynnlm_subword_ran = false;
  bool ynnlm_subword_skipped_dense = false;
  bool ynnlm_subword_context_cache_hit = false;
  size_t ynnlm_subword_raw_count = 0;
  size_t ynnlm_subword_count = 0;
  std::string ynnlm_subword_top_raw;
  std::string ynnlm_subword_top_filtered;
  bool ynnlm_has_kto_probability = false;
  float ynnlm_kto_probability = 0.0f;
  int64_t ynnlm_elapsed_ms = 0;
  constexpr NgramCalibration kAndroidNgramCalibration{
      0.7130374807f, -3.5576391953f, 0.0f, 64.0f};
  const auto static_feature = [this](std::string_view word, bool ngram) {
    float value = 64.0f;
    const int32_t rank = nnlm_.VocabularyRank(word);
    if (ngram) {
      (void)CalibrateStaticNgramRank(rank, &value);
    } else {
      (void)CalibrateStaticUnigramRank(rank, &value);
    }
    return value;
  };
  const auto letter_feature = [this](std::string_view word,
                                     float static_unigram) {
    float score = 0.0f;
    if (char_model_.Score(word, &score)) return score;
    // Keep the previous scale-compatible fallback for malformed/out-of-
    // alphabet candidates and packages built without the compact 3GRM asset.
    constexpr float kAndroidExactOffset = 1.0001544952f;
    return std::clamp(static_unigram + kAndroidExactOffset, 0.0f, 80.0f);
  };
  const bool neural_has_touch_geometry =
      request.proximity_info.enabled &&
      !ReconstructTouchInputCodepoints(request.points).empty();
  float original_transfeme_distance = 0.0f;
  if (!normalized.empty()) {
    (void)CalibrateTransfemeDistance(
        tfmtrie_.Proximity(normalized, normalized),
        &original_transfeme_distance);
  }
  const auto ynnlm_started = std::chrono::steady_clock::now();
  const bool has_decisive_manual_rule = std::any_of(
      response.suggestions.begin(), response.suggestions.end(),
      [](const Suggestion& suggestion) {
        return suggestion.tag == "manual_rule";
      });
  // Shared exact order-10 context for word prediction and Autocc char-LM
  // features.  Build it even when candidate generation is skipped so the final
  // policy scorer cannot accidentally use a different sentence state.
  std::vector<std::string> wc_context;
  {
    const auto& previous_words = request.prev_words_info;
    constexpr size_t kYnnlmPreviousWordLimit = 9;
    const size_t take =
        std::min<size_t>(previous_words.size(), kYnnlmPreviousWordLimit);
    for (size_t i = previous_words.size() - take;
         i < previous_words.size(); ++i) {
      std::string token = NormalizeToken(previous_words[i].token);
      if (!token.empty()) wc_context.push_back(std::move(token));
    }
  }
  ynnlm_context_words = wc_context.size();
  if (request.disable_ynnlm) {
    ynnlm_skip_reason = "debug_disabled";
  } else if (has_decisive_manual_rule) {
    ynnlm_skip_reason = "manual_rule";
  }
  if (!request.disable_ynnlm && word_chain_.is_loaded() &&
      !has_decisive_manual_rule &&
      (normalized.size() >= 4 || normalized.empty())) {
    // Order-10 LM: use up to nine complete previous words (oldest-first,
    // normalized). The current prefix conditions decoder-side generation.
    // Yandex generates a wider candidate pool and lets MatrixNet pull lower-LM
    // forms (Android кот -> котики/коть/котик) above the raw decoder top-k.
    // Truncating to the UI limit before ranking made that impossible.
    constexpr size_t kMatrixNetCandidatePool = 64;
    const size_t neural_pool = ranker_.is_matrixnet_loaded()
        ? std::max(max, kMatrixNetCandidatePool)
        : max;
    YnnlmPredictionBatch neural_batch =
        word_chain_.Predict(normalized, wc_context, neural_pool,
                            request.package_name);
    ynnlm_batch_ran = true;
    ynnlm_invoke_count_.fetch_add(1, std::memory_order_relaxed);
    ynnlm_model_scored += neural_batch.candidates.size() +
                          neural_batch.char_candidates.size();
    ynnlm_scored_count_.fetch_add(
        neural_batch.candidates.size() + neural_batch.char_candidates.size(),
        std::memory_order_relaxed);
    ynnlm_encoder_path = neural_batch.runtime_encoder_failed
                             ? "runtime_failed"
                             : (neural_batch.runtime_encoder_used ? "runtime"
                                                                  : "cpp");
    ynnlm_has_original_logit = neural_batch.has_original_logit;
    ynnlm_original_raw_logit = neural_batch.original_raw_logit;
    ynnlm_word_cache_hit = neural_batch.word_decoder_cache_hit;
    ynnlm_app_embedding_used = neural_batch.application_embedding_used;
    ynnlm_app_embedding_failed = neural_batch.application_embedding_failed;
    ynnlm_app_id = neural_batch.application_id;
    ynnlm_has_kto_probability = word_chain_.ProbabilityForWord(
        neural_batch, "кто", &ynnlm_kto_probability);
    std::vector<std::pair<std::string, float>> neural;
    neural.reserve(neural_batch.candidates.size());
    for (const auto& candidate : neural_batch.candidates) {
      neural.push_back({candidate.word, candidate.raw_logit});
      if (ynnlm_top_raw.size() < 192 &&
          std::count(ynnlm_top_raw.begin(), ynnlm_top_raw.end(), ';') < 3) {
        if (!ynnlm_top_raw.empty()) ynnlm_top_raw += ";";
        ynnlm_top_raw += candidate.word + ":" +
                         FormatDiagnosticFloat(candidate.raw_logit);
      }
    }
    std::vector<std::pair<std::string, float>> char_neural;
    ynnlm_char_ran = neural_batch.char_decoder_used ||
                     neural_batch.char_decoder_failed;
    ynnlm_char_failed = neural_batch.char_decoder_failed;
    ynnlm_char_skipped_dense = neural_batch.char_decoder_skipped_dense;
    ynnlm_char_raw_count = neural_batch.char_candidates.size();
    char_neural.reserve(neural_batch.char_candidates.size());
    const std::vector<char32_t> prefix_cp = DecodeUtf8(normalized);
    for (const auto& candidate : neural_batch.char_candidates) {
      if (ynnlm_char_top_raw.size() < 192 &&
          std::count(ynnlm_char_top_raw.begin(), ynnlm_char_top_raw.end(), ';') <
              3) {
        if (!ynnlm_char_top_raw.empty()) ynnlm_char_top_raw += ";";
        ynnlm_char_top_raw += candidate.word + ":" +
                              FormatDiagnosticFloat(candidate.raw_logit);
      }
      if (candidate.word == normalized || IsBlacklisted(candidate.word) ||
          (!normalized.empty() && candidate.word.rfind(normalized, 0) != 0)) {
        continue;
      }
      const std::vector<char32_t> candidate_cp = DecodeUtf8(candidate.word);
      const size_t min_extra = prefix_cp.size() <= 2 ? 2 : 1;
      if (candidate_cp.size() < prefix_cp.size() + min_extra ||
          (nnlm_.has_vocabulary() && nnlm_.VocabularyRank(candidate.word) < 0)) {
        continue;
      }
      const int32_t weight = nnlm_.VocabularyWeight(candidate.word);
      const float score = static_cast<float>(std::max(weight, 0)) +
                          0.01f * candidate.raw_logit;
      char_neural.push_back({candidate.word, score});
      if (ynnlm_char_top_filtered.size() < 192 &&
          std::count(ynnlm_char_top_filtered.begin(),
                     ynnlm_char_top_filtered.end(), ';') < 3) {
        if (!ynnlm_char_top_filtered.empty()) ynnlm_char_top_filtered += ";";
        ynnlm_char_top_filtered += candidate.word + ":" +
                                   FormatDiagnosticFloat(score);
      }
    }
    ynnlm_char_count = char_neural.size();
    std::stable_sort(char_neural.begin(), char_neural.end(),
                     [](const auto& a, const auto& b) {
                       if (a.second != b.second) return a.second > b.second;
                       return a.first < b.first;
                     });
    YnnlmPredictionBatch subword_batch;
    std::vector<std::pair<std::string, float>> subword_neural;
    const bool run_subword_decoder = ShouldRunYnnlmAuxiliary(
        neural_batch.candidates.size(), neural_pool);
    if (word_chain_.has_subword_decoder() && !run_subword_decoder) {
      ynnlm_subword_skipped_dense = true;
    }
    if (word_chain_.has_subword_decoder() && run_subword_decoder) {
      subword_batch = word_chain_.PredictSubwords(
          normalized, wc_context, 16, request.package_name);
      ynnlm_invoke_count_.fetch_add(1, std::memory_order_relaxed);
      ynnlm_model_scored += subword_batch.candidates.size();
      ynnlm_scored_count_.fetch_add(subword_batch.candidates.size(),
                                    std::memory_order_relaxed);
      if (subword_batch.runtime_encoder_failed) {
        ynnlm_encoder_path = "runtime_failed";
      } else if (subword_batch.runtime_encoder_used) {
        ynnlm_encoder_path = "runtime";
      }
      ynnlm_subword_ran = true;
      ynnlm_subword_context_cache_hit =
          subword_batch.word_decoder_cache_hit;
      ynnlm_subword_raw_count = subword_batch.candidates.size();
      subword_neural.reserve(subword_batch.candidates.size());
      const std::vector<char32_t> prefix_cp = DecodeUtf8(normalized);
      const auto is_quality_subword = [&](const YnnlmPrediction& candidate) {
        if (candidate.word == normalized || IsBlacklisted(candidate.word)) {
          return false;
        }
        if (!normalized.empty()) {
          if (candidate.word.rfind(normalized, 0) != 0) {
            return false;
          }
          const std::vector<char32_t> candidate_cp =
              DecodeUtf8(candidate.word);
          const size_t min_extra = prefix_cp.size() <= 2 ? 2 : 1;
          if (candidate_cp.size() < prefix_cp.size() + min_extra) {
            return false;
          }
        }
        if (nnlm_.has_vocabulary() && nnlm_.VocabularyRank(candidate.word) < 0) {
          return false;
        }
        return true;
      };
      for (const auto& candidate : subword_batch.candidates) {
        if (ynnlm_subword_top_raw.size() < 192 &&
            std::count(ynnlm_subword_top_raw.begin(),
                       ynnlm_subword_top_raw.end(), ';') < 3) {
          if (!ynnlm_subword_top_raw.empty()) ynnlm_subword_top_raw += ";";
          ynnlm_subword_top_raw += candidate.word + ":" +
                                   FormatDiagnosticFloat(candidate.raw_logit);
        }
        if (!is_quality_subword(candidate)) {
          continue;
        }
        subword_neural.push_back({candidate.word, candidate.raw_logit});
        if (ynnlm_subword_top_filtered.size() < 192 &&
            std::count(ynnlm_subword_top_filtered.begin(),
                       ynnlm_subword_top_filtered.end(), ';') < 3) {
          if (!ynnlm_subword_top_filtered.empty()) {
            ynnlm_subword_top_filtered += ";";
          }
          ynnlm_subword_top_filtered +=
              candidate.word + ":" + FormatDiagnosticFloat(candidate.raw_logit);
        }
      }
      ynnlm_subword_count = subword_neural.size();
    }
    // Re-rank the neural candidates so the order matches the real Yandex ranker
    // (which strongly favors frequent inflected words; the raw neural logit
    // surfaces rare stems — phone "прог" -> Программа/Проголосовала, raw logit ->
    // прогами/прогул). stable_sort keeps the logit (context) order among ties.
    // Android lm_ngr_main features are decoder negative log-probabilities
    // (e.g. p=exp(-13.388319)). Bounds keep zero/underflow and malformed model
    // output from contaminating MatrixNet comparisons.
    if (!normalized.empty() &&
        ranker_.is_matrixnet_loaded() && nnlm_.has_vocabulary()) {
      ynnlm_rerank_path = "matrixnet";
      const float orig_lm = static_feature(normalized, false);
      const float orig_lm_ngram = static_feature(normalized, true);
      for (size_t i = 0; i < neural.size(); ++i) {
        auto& nw = neural[i];
        const float corr_lm = static_feature(nw.first, false);
        NeuralRankerFeatureInput input;
        input.original = normalized;
        input.correction = nw.first;
        input.raw_original_logit = neural_batch.original_raw_logit;
        input.raw_correction_logit =
            neural_batch.candidates[i].raw_logit;
        input.has_original_logit = neural_batch.has_original_logit;
        input.original_static = orig_lm;
        input.correction_static = corr_lm;
        input.original_static_ngram = orig_lm_ngram;
        input.correction_static_ngram = static_feature(nw.first, true);
        input.original_letter_lm_main = letter_feature(normalized, orig_lm);
        input.correction_letter_lm_main = letter_feature(nw.first, corr_lm);
        input.original_transfeme_proximity = original_transfeme_distance;
        const float raw_transfeme =
            tfmtrie_.Proximity(normalized, nw.first);
        (void)CalibrateTransfemeDistance(
            raw_transfeme, &input.transfeme_proximity);
        input.has_geometry = neural_has_touch_geometry;
        input.original_known = exact_input_known;
        RankerFeatures f =
            BuildNeuralRankerFeatures(input, kAndroidNgramCalibration);
        nw.second = ranker_.ScoreOne(f);
      }
      std::stable_sort(neural.begin(), neural.end(),
                       [](const std::pair<std::string, float>& a,
                          const std::pair<std::string, float>& b) {
                         return a.second > b.second;
                       });
    } else if (!normalized.empty() && nnlm_.has_vocabulary()) {
      ynnlm_rerank_path = "frequency";
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
    if (!neural.empty() || !char_neural.empty() || !subword_neural.empty()) {
      std::unordered_set<std::string> neural_set;
      for (const auto& nw : neural) {
        neural_set.insert(nw.first);
      }
      for (const auto& nw : char_neural) {
        neural_set.insert(nw.first);
      }
      for (const auto& nw : subword_neural) {
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
      merged.reserve(lead.size() + neural.size() + char_neural.size() +
                     subword_neural.size() + rest.size());
      for (auto& s : lead) {
        merged.push_back(std::move(s));
      }
      std::unordered_set<std::string> emitted;
      for (const auto& nw : neural) {
        if (!emitted.insert(nw.first).second) continue;
        Suggestion s;
        s.text = nw.first;
        s.tag = "neural";
        s.type_flags = normalized.empty() ? kSuggestionFlagPrediction
                                          : kSuggestionFlagCompletion;
        s.score = nw.second;
        merged.push_back(std::move(s));
      }
      for (const auto& nw : char_neural) {
        if (!emitted.insert(nw.first).second) continue;
        Suggestion s;
        s.text = nw.first;
        s.tag = "ynnlm_char";
        s.type_flags = normalized.empty() ? kSuggestionFlagPrediction
                                          : kSuggestionFlagCompletion;
        s.score = nw.second;
        merged.push_back(std::move(s));
      }
      for (const auto& nw : subword_neural) {
        if (!emitted.insert(nw.first).second) continue;
        Suggestion s;
        s.text = nw.first;
        s.tag = "ynnlm_subword";
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

    // The decoder distribution covers the whole vocabulary, not only prefix
    // completions. Re-score the merged correction+completion pool so typo
    // candidates such as Android's retained кот->кто vector receive their real
    // lm_ngr_main probability instead of the old zero placeholder.
    if (!normalized.empty() && ranker_.is_matrixnet_loaded() &&
        nnlm_.has_vocabulary()) {
      const float original_static =
          static_feature(normalized, false);
      const float original_static_ngram =
          static_feature(normalized, true);
      for (auto& suggestion : response.suggestions) {
        if (suggestion.tag == "manual_rule") {
          suggestion.score = 10000.0f;
          continue;
        }
        const std::string correction = NormalizeToken(suggestion.text);
        float correction_probability = 0.0f;
        if (word_chain_.ProbabilityForWord(
                neural_batch, correction, &correction_probability)) {
          ynnlm_word_probabilities[correction] = correction_probability;
        }
        float transfeme_distance = 0.0f;
        (void)CalibrateTransfemeDistance(
            tfmtrie_.Proximity(normalized, correction),
            &transfeme_distance);

        NeuralRankerFeatureInput input;
        input.original = normalized;
        input.correction = correction;
        input.raw_original_logit = neural_batch.original_raw_logit;
        input.raw_correction_logit = correction_probability;
        input.has_original_logit = neural_batch.has_original_logit;
        input.original_static = original_static;
        input.correction_static = static_feature(correction, false);
        input.original_static_ngram = original_static_ngram;
        input.correction_static_ngram = static_feature(correction, true);
        input.original_letter_lm_main =
            letter_feature(normalized, original_static);
        input.correction_letter_lm_main =
            letter_feature(correction, input.correction_static);
        input.original_transfeme_proximity = original_transfeme_distance;
        input.transfeme_proximity = transfeme_distance;
        input.has_geometry = neural_has_touch_geometry;
        input.original_known = exact_input_known;
        suggestion.score = ranker_.ScoreOne(
            BuildNeuralRankerFeatures(input, kAndroidNgramCalibration));
        const int32_t correction_rank = nnlm_.VocabularyRank(correction);
        if (correction.rfind(normalized, 0) == 0) {
          suggestion.score = BlendMatrixNetWithVocabularyRank(
              suggestion.score, correction_rank, nnlm_.vocabulary_size());
        }
      }
      std::stable_sort(response.suggestions.begin(), response.suggestions.end(),
                       [](const Suggestion& a, const Suggestion& b) {
                         return a.score > b.score;
                       });
      ynnlm_rerank_path = "matrixnet_all";
    }
    ynnlm_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - ynnlm_started)
                           .count();
  }

  // --- Original TapModel 122.5 correction ranker/classifier ---
  // Run only on same-length, one-edit correction candidates. The exact runner
  // consumes the down-touch sequence and atomically loaded 38/78 tree models;
  // completion/prediction candidates retain their existing LM order.
  std::string tap_model_path = "not_run";
  size_t tap_model_evaluated = 0;
  bool tap_model_decision_available = false;
  bool tap_model_accept = false;
  double tap_model_top_rank = 0.0;
  double tap_model_classifier = 0.0;
  if (request.disable_tap_model) {
    tap_model_path = "debug_disabled";
  } else if (!tap_model_runner_.is_loaded()) {
    tap_model_path = tap_model_asset_present_ ? "invalid" : "missing";
  } else if (!request.proximity_info.enabled || request.points.empty()) {
    tap_model_path = "no_touch";
  } else if (has_decisive_manual_rule) {
    tap_model_path = "manual_rule";
  } else {
    struct TapRankedSlot {
      size_t slot = 0;
      TapModelPairResult result;
    };
    std::vector<TapRankedSlot> ranked_slots;
    const size_t original_length = DecodeUtf8(normalized).size();
    const auto safe_tap_source = [](const Suggestion& suggestion) {
      return suggestion.tag == "autocorrect" ||
             suggestion.tag == "tfmtrie" || suggestion.tag == "fuzzy" ||
             suggestion.tag == "predictor_fuzzy" ||
             suggestion.tag == "predictor_autocorrect";
    };
    for (size_t i = 0; i < response.suggestions.size(); ++i) {
      const Suggestion& suggestion = response.suggestions[i];
      if (!safe_tap_source(suggestion)) continue;
      const std::string correction = NormalizeToken(suggestion.text);
      if (correction.empty() || correction == normalized ||
          DecodeUtf8(correction).size() != original_length ||
          LevenshteinDistanceUtf8(normalized, correction) != 1) {
        continue;
      }

      const float original_word_lm = -static_feature(normalized, false);
      const float correction_word_lm = -static_feature(correction, false);
      const float best_word_lm =
          std::max(original_word_lm, correction_word_lm);
      const float original_char_lm =
          -letter_feature(normalized, -original_word_lm);
      const float correction_char_lm =
          -letter_feature(correction, -correction_word_lm);
      float forward_distance = 0.0f;
      float reverse_distance = 0.0f;
      (void)CalibrateTransfemeDistance(
          tfmtrie_.Proximity(normalized, correction), &forward_distance);
      (void)CalibrateTransfemeDistance(
          tfmtrie_.Proximity(correction, normalized), &reverse_distance);
      const float previous_length = request.prev_words_info.empty()
          ? 0.0f
          : static_cast<float>(
                DecodeUtf8(request.prev_words_info.back().token).size());
      const auto semantics = [&](bool is_original) {
        TapModelSemanticFeatures values;
        const float word_lm =
            is_original ? original_word_lm : correction_word_lm;
        const float char_lm =
            is_original ? original_char_lm : correction_char_lm;
        const float conditional = word_lm - best_word_lm;
        const float conditional_probability =
            std::exp(std::clamp(conditional, -80.0f, 0.0f));
        const float length = static_cast<float>(std::max<size_t>(
            1, is_original ? original_length : DecodeUtf8(correction).size()));
        values.char_lm_mod = char_lm;
        values.word_lm_1 = word_lm;
        values.word_lm_cond_1 = conditional;
        values.neural_char_lm_n = char_lm;
        values.neural_char_lm_cond_n = conditional_probability;
        values.neural_char_lm_cond_log_n = conditional;
        values.neural_char_lm_per_letter_n = char_lm / length;
        values.context_prefix_len = static_cast<float>(prev_context_count);
        values.context_last_word_len = previous_length;
        values.distance_transfeme_original_correction_m =
            is_original ? original_transfeme_distance : forward_distance;
        values.distance_transfeme_correction_original_m =
            is_original ? original_transfeme_distance : reverse_distance;
        values.neural_word_lm_cond_max = conditional_probability;
        values.neural_word_lm_cond_sum = conditional_probability;
        values.neural_word_lm_cond_avg = conditional_probability;
        return values;
      };

      TapModelPairRequest tap_request;
      tap_request.original = normalized;
      tap_request.correction = correction;
      tap_request.layout = "android_ru_east_slavic_separate_comma";
      tap_request.keyboard_width = request.proximity_info.keyboard_width;
      tap_request.keyboard_height = request.proximity_info.keyboard_height;
      tap_request.original_semantics = semantics(true);
      tap_request.correction_semantics = semantics(false);
      tap_request.touches.reserve(request.points.size());
      for (const auto& point : request.points) {
        tap_request.touches.push_back(
            {point.x, point.y, point.key_code});
      }
      TapModelPairResult tap_result;
      const Status tap_status =
          tap_model_runner_.EvaluatePair(tap_request, &tap_result);
      if (!tap_status.ok() || !tap_result.model_engaged) continue;
      ranked_slots.push_back({i, std::move(tap_result)});
    }
    tap_model_evaluated = ranked_slots.size();
    if (!ranked_slots.empty()) {
      std::vector<size_t> slots;
      std::vector<Suggestion> ranked_suggestions;
      slots.reserve(ranked_slots.size());
      ranked_suggestions.reserve(ranked_slots.size());
      for (const auto& entry : ranked_slots) {
        slots.push_back(entry.slot);
      }
      std::sort(slots.begin(), slots.end());
      std::stable_sort(
          ranked_slots.begin(), ranked_slots.end(),
          [](const TapRankedSlot& left, const TapRankedSlot& right) {
            return left.result.correction_rank_score >
                   right.result.correction_rank_score;
          });
      for (const auto& entry : ranked_slots) {
        ranked_suggestions.push_back(response.suggestions[entry.slot]);
      }
      for (size_t i = 0; i < slots.size(); ++i) {
        response.suggestions[slots[i]] = std::move(ranked_suggestions[i]);
      }
      tap_model_path = "ranker_classifier";

      if (!slots.empty() && slots.front() == 0) {
        tap_model_decision_available = true;
        tap_model_accept = ranked_slots.front().result.should_autocorrect;
        tap_model_top_rank =
            ranked_slots.front().result.correction_rank_score;
        tap_model_classifier =
            ranked_slots.front().result.classifier_probability;
      }
    } else {
      tap_model_path = "rejected_or_no_correction";
    }
  }

  // --- Original TreeAutocorrectModel (default/K13321/K13421) ---
  // This is the final correction policy scorer, not a candidate generator. It
  // sees only the current top correction after the existing rankers and cannot
  // bypass known-word, blacklist, blocker or manual-rule gates.
  std::string autocc_model_path = "not_run";
  std::string autocc_producer_contract = "not_run";
  bool autocc_model_attempted = false;
  bool autocc_model_decision_available = false;
  bool autocc_model_accept = false;
  double autocc_model_probability = 0.0;
  uint64_t autocc_model_elapsed_us = 0;
  bool autocc_char_score_available = false;
  bool autocc_char_context_conditioned = false;
  float autocc_original_char_total = 0.0f;
  float autocc_original_char_eos = 0.0f;
  float autocc_correction_char_total = 0.0f;
  float autocc_correction_char_eos = 0.0f;
  AutoccVariant autocc_variant = AutoccVariant::kDefault;
  const bool autocc_variant_valid =
      ParseAutoccVariant(request.autocc_variant, &autocc_variant);
  if (request.disable_autocc_model) {
    autocc_model_path = "debug_disabled";
  } else if (!autocc_model_runner_.is_loaded()) {
    autocc_model_path = autocc_model_asset_present_ ? "invalid" : "missing";
  } else if (!request.want_autocorrect) {
    autocc_model_path = "not_requested";
  } else if (has_decisive_manual_rule) {
    autocc_model_path = "manual_rule";
  } else if (!autocc_variant_valid) {
    autocc_model_path = "invalid_variant";
  } else if (response.suggestions.empty()) {
    autocc_model_path = "no_candidate";
  } else {
    const Suggestion& top = response.suggestions.front();
    const std::string correction = NormalizeToken(top.text);
    const bool safe_source =
        top.tag == "autocorrect" || top.tag == "tfmtrie" ||
        (top.tag == "fuzzy" && correction.size() >= normalized.size());
    bool input_known = exact_input_known || native_prefix_input_known;
    if (!input_known) input_known = IsKnownWord(normalized);
    const size_t edit_distance =
        correction.empty() ? std::numeric_limits<size_t>::max()
                           : LevenshteinDistanceUtf8(normalized, correction);
    if (!safe_source) {
      autocc_model_path = "unsafe_source";
    } else if (input_known) {
      autocc_model_path = "known_input";
    } else if (correction.empty() || correction == normalized ||
               edit_distance > 1) {
      autocc_model_path = "outside_policy";
    } else if (IsBlacklisted(correction) ||
               IsAutocorrectBlocked(correction)) {
      autocc_model_path = "policy_blocked";
    } else {
      const auto started = std::chrono::steady_clock::now();
      const auto original_cp = DecodeUtf8(normalized);
      const auto correction_cp = DecodeUtf8(correction);
      const float original_length =
          static_cast<float>(std::max<size_t>(original_cp.size(), 1));
      const float correction_length =
          static_cast<float>(std::max<size_t>(correction_cp.size(), 1));

      YnnlmCharScore original_char_score;
      YnnlmCharScore correction_char_score;
      if (!request.disable_ynnlm) {
        original_char_score = word_chain_.ScoreCharacters(
            normalized, wc_context, request.package_name);
        correction_char_score = word_chain_.ScoreCharacters(
            correction, wc_context, request.package_name);
      }
      std::array<float, 6> autocc_char_features{};
      if (!BuildYnnlmAutoccCharFeatures(
              original_char_score, original_cp.size(), correction_char_score,
              correction_cp.size(), &autocc_char_features)) {
        const bool producer_failed = original_char_score.model_failed ||
                                     correction_char_score.model_failed;
        autocc_model_attempted = producer_failed;
        autocc_model_path = request.disable_ynnlm
                                ? "producer_debug_disabled"
                                : (producer_failed ? "producer_failed"
                                                   : "producer_unavailable");
        autocc_producer_contract = request.disable_ynnlm
                                       ? "debug_disabled"
                                       : (producer_failed ? "model_failed"
                                                          : "unavailable");
        autocc_model_elapsed_us = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started)
                .count());
      } else {
        autocc_char_score_available = true;
        autocc_char_context_conditioned =
            original_char_score.context_conditioned &&
            correction_char_score.context_conditioned;
        autocc_original_char_total =
            original_char_score.total_log2_probability;
        autocc_original_char_eos = original_char_score.eos_log2_probability;
        autocc_correction_char_total =
            correction_char_score.total_log2_probability;
        autocc_correction_char_eos =
            correction_char_score.eos_log2_probability;
        autocc_producer_contract = wc_context.empty()
            ? "ynnlm1225_char_eos_empty_v1"
            : "ynnlm1225_char_eos_context_v1";

      const float original_static = static_feature(normalized, false);
      const float original_static_ngram = static_feature(normalized, true);
      const float correction_static = static_feature(correction, false);
      const float correction_static_ngram = static_feature(correction, true);
      const float original_letter =
          letter_feature(normalized, original_static);
      const float correction_letter =
          letter_feature(correction, correction_static);

      float original_main = kAndroidNgramCalibration.high;
      if (ynnlm_has_original_logit) {
        (void)CalibrateNgramLogit(ynnlm_original_raw_logit,
                                  kAndroidNgramCalibration, &original_main);
      }
      float correction_main = kAndroidNgramCalibration.high;
      const auto probability = ynnlm_word_probabilities.find(correction);
      if (probability != ynnlm_word_probabilities.end()) {
        (void)CalibrateNgramLogit(probability->second,
                                  kAndroidNgramCalibration, &correction_main);
      }

      float forward_distance = 0.0f;
      float reverse_distance = 0.0f;
      (void)CalibrateTransfemeDistance(
          tfmtrie_.Proximity(normalized, correction), &forward_distance);
      (void)CalibrateTransfemeDistance(
          tfmtrie_.Proximity(correction, normalized), &reverse_distance);

      AutoccFeatureRequest autocc_request;
      autocc_request.original = normalized;
      autocc_request.correction = correction;
      autocc_request.original_signals.oov =
          nnlm_.has_vocabulary() && !nnlm_.VocabularyContains(normalized)
              ? 1.0f
              : 0.0f;
      autocc_request.original_signals.lm_ngr_main = original_main;
      autocc_request.original_signals.lm_1gr_static = original_static;
      autocc_request.original_signals.lm_2gr_static = original_static_ngram;
      autocc_request.original_signals.lm_ngr_static = original_static_ngram;
      autocc_request.original_signals.letter_lm_main = original_letter;
      autocc_request.original_signals.ndict = exact_input_known ? 1.0f : 0.0f;
      autocc_request.correction_signals.lm_ngr_main = correction_main;
      autocc_request.correction_signals.lm_1gr_static = correction_static;
      autocc_request.correction_signals.lm_2gr_static = correction_static_ngram;
      autocc_request.correction_signals.lm_ngr_static = correction_static_ngram;
      autocc_request.correction_signals.letter_lm_main = correction_letter;
      const bool correction_known = is_known_word_fast_for_request(correction);
      autocc_request.correction_signals.ndict = correction_known ? 1.0f : 0.0f;
      autocc_request.forward_distance.distance = forward_distance;
      autocc_request.reverse_distance.distance = reverse_distance;
      autocc_request.same_stem =
          lemmer_.loaded() &&
          lemmer_.Lemmatize(normalized) == lemmer_.Lemmatize(correction);

      float geometry_per = 0.0f;
      float geometry_original_per = 0.0f;
      (void)CalibrateGeometryDistancePerLetter(forward_distance,
                                               &geometry_per);
      (void)CalibrateGeometryDistancePerLetter(original_transfeme_distance,
                                               &geometry_original_per);
      const float geometry = geometry_per * original_length;
      const float geometry_original = geometry_original_per * original_length;
      const float geometry_ratio = geometry_original > 0.0f
                                       ? geometry / geometry_original
                                       : 0.0f;
      const float geometry_ratio_per = geometry_ratio / original_length;
      const float original_letter_per = original_letter / original_length;
      auto& keyboard = autocc_request.keyboard_features;
      keyboard[0] = geometry;
      keyboard[1] = geometry_per;
      keyboard[2] = geometry + original_letter;
      keyboard[3] = geometry_per + original_letter_per;
      keyboard[4] = geometry_original;
      keyboard[5] = geometry_original_per;
      keyboard[6] = geometry_ratio;
      keyboard[7] = geometry_ratio_per;
      keyboard[8] = geometry_ratio_per + original_letter_per;
      // The captured Autocc route emitted identical scalar and n-gram
      // geometry branches. Keep both branches explicit until a counterexample
      // proves a distinct n-gram coordinate kernel.
      keyboard[9] = geometry;
      keyboard[10] = geometry_per;
      keyboard[11] = geometry + original_letter;
      keyboard[12] = geometry_per + original_letter_per;
      keyboard[13] = geometry_original;
      keyboard[14] = geometry_original_per;
      keyboard[15] = geometry_ratio;
      keyboard[16] = geometry_ratio_per;
      keyboard[17] = geometry_ratio_per + original_letter_per;
      keyboard[18] = correction_known ? 0.0f : 1.0f;
      for (size_t i = 0; i < autocc_char_features.size(); ++i) {
        keyboard[19 + i] = autocc_char_features[i];
      }

      AutoccEvaluation evaluation;
      autocc_model_attempted = true;
      const Status status = autocc_model_runner_.Evaluate(
          autocc_request, autocc_variant, &evaluation);
      autocc_model_elapsed_us = static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - started)
              .count());
      if (status.ok() && evaluation.model_engaged) {
        autocc_model_decision_available = true;
        autocc_model_accept = evaluation.should_autocorrect;
        autocc_model_probability = evaluation.probability;
        autocc_model_path = "tree_policy";
      } else {
        autocc_model_path = "rejected";
      }
      }
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

    // Autocorrect if: input not known + top is close (Lev <= 1) + not blocked.
    // The recovered Autocc forest is the final neural policy gate. TapModel is
    // an additional veto when touch-backed classification was available; the
    // legacy heuristic remains only the missing/disabled/invalid fallback.
    if (top.tag != "exact" && top.tag != "identity") {
      bool input_known = exact_input_known;
      if (!input_known && !native_prefix_input_known) {
        const std::string norm_top = NormalizeToken(top.text);
        size_t dist = LevenshteinDistanceUtf8(normalized, norm_top);
        const bool safe_autocorrect_source =
            top.tag == "autocorrect" ||
            top.tag == "tfmtrie" ||
            (top.tag == "fuzzy" && norm_top.size() >= normalized.size());
        if (safe_autocorrect_source && dist <= 1 &&
            !IsBlacklisted(norm_top) && !IsAutocorrectBlocked(norm_top)) {
          bool policy_accept = ResolveAutoccPolicyGate(
              autocc_model_attempted, autocc_model_decision_available,
              autocc_model_accept);
          if (tap_model_decision_available) {
            policy_accept = policy_accept && tap_model_accept;
          }
          response.will_autocorrect = policy_accept;
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
  if (char_model_.is_loaded()) {
    response.diagnostics += ", char_lm=" + char_model_.diagnostics();
  } else if (char_model_asset_present_) {
    response.diagnostics += ", char_lm=invalid";
  } else {
    response.diagnostics += ", char_lm=none";
  }
  if (ranker_.is_loaded()) {
    response.diagnostics += ", ranker=" + ranker_.diagnostics();
  } else if (ranker_model_asset_present_) {
    response.diagnostics += ", ranker=" + ranker_.diagnostics();
  } else {
    response.diagnostics += ", ranker=none";
  }
  response.diagnostics += ", tap_model_path=" + tap_model_path;
  response.diagnostics += ", tap_model_evaluated=" +
                          std::to_string(tap_model_evaluated);
  if (tap_model_decision_available) {
    response.diagnostics += ", tap_model_top_rank=" +
                            FormatDiagnosticFloat(
                                static_cast<float>(tap_model_top_rank));
    response.diagnostics += ", tap_model_classifier=" +
                            FormatDiagnosticFloat(
                                static_cast<float>(tap_model_classifier));
    response.diagnostics += ", tap_model_accept=" +
                            std::to_string(tap_model_accept ? 1 : 0);
  }
  response.diagnostics += ", tap_model=" + tap_model_runner_.diagnostics();
  response.diagnostics += ", autocc_model_path=" + autocc_model_path;
  response.diagnostics += ", autocc_model_attempted=" +
                          std::to_string(autocc_model_attempted ? 1 : 0);
  response.diagnostics +=
      ", autocc_producer_contract=" + autocc_producer_contract;
  response.diagnostics += ", autocc_char_score_available=" +
                          std::to_string(autocc_char_score_available ? 1 : 0);
  if (autocc_char_score_available) {
    response.diagnostics += ", autocc_char_context=" +
                            std::to_string(
                                autocc_char_context_conditioned ? 1 : 0);
    response.diagnostics += ", autocc_char_original_total=" +
                            FormatDiagnosticFloat(autocc_original_char_total);
    response.diagnostics += ", autocc_char_original_eos=" +
                            FormatDiagnosticFloat(autocc_original_char_eos);
    response.diagnostics += ", autocc_char_correction_total=" +
                            FormatDiagnosticFloat(autocc_correction_char_total);
    response.diagnostics += ", autocc_char_correction_eos=" +
                            FormatDiagnosticFloat(autocc_correction_char_eos);
  }
  response.diagnostics += ", autocc_variant=" +
                          std::string(AutoccVariantName(autocc_variant));
  response.diagnostics += ", autocc_elapsed_us=" +
                          std::to_string(autocc_model_elapsed_us);
  if (autocc_model_decision_available) {
    response.diagnostics += ", autocc_probability=" +
                            FormatDiagnosticFloat(
                                static_cast<float>(autocc_model_probability));
    response.diagnostics += ", autocc_accept=" +
                            std::to_string(autocc_model_accept ? 1 : 0);
  }
  response.diagnostics += ", autocc_model=" +
                          autocc_model_runner_.diagnostics();
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
  response.diagnostics +=
      ", rnnt_components=" +
      std::string(rnnt_components_asset_present_
                      ? rnnt_swipe_model_.diagnostics()
                      : "none");
  if (ynnlm_batch_ran) {
    response.diagnostics += ", ynnlm_rerank=" + ynnlm_rerank_path;
    response.diagnostics += ", ynnlm_encoder=" + ynnlm_encoder_path;
    response.diagnostics += ", ynnlm_word_cache=" +
                            std::string(ynnlm_word_cache_hit ? "hit" : "miss");
    response.diagnostics += ", ynnlm_app_id=" +
                            std::to_string(ynnlm_app_id);
    response.diagnostics += ", ynnlm_app_embedding=" +
                            std::string(ynnlm_app_embedding_failed
                                            ? "runtime_failed"
                                            : (ynnlm_app_embedding_used
                                                   ? "runtime"
                                                   : "not_run"));
    response.diagnostics += ", ynnlm_context_words=" +
                            std::to_string(ynnlm_context_words);
    response.diagnostics += ", ynnlm_elapsed_ms=" +
                            std::to_string(ynnlm_elapsed_ms);
    response.diagnostics += ", ynnlm_orig_present=" +
                            std::to_string(ynnlm_has_original_logit ? 1 : 0);
    if (ynnlm_has_original_logit) {
      response.diagnostics += ", ynnlm_orig_raw=" +
                              FormatDiagnosticFloat(ynnlm_original_raw_logit);
      float original_feature = 0.0f;
      if (CalibrateNgramLogit(ynnlm_original_raw_logit,
                              kAndroidNgramCalibration, &original_feature)) {
        response.diagnostics += ", ynnlm_orig_feature=" +
                                FormatDiagnosticFloat(original_feature);
      }
    }
    if (!ynnlm_top_raw.empty()) {
      response.diagnostics += ", ynnlm_top_raw=" + ynnlm_top_raw;
    }
    if (ynnlm_char_ran) {
      response.diagnostics += ", ynnlm_char_failed=" +
                              std::to_string(ynnlm_char_failed ? 1 : 0);
      response.diagnostics += ", ynnlm_char_raw_count=" +
                              std::to_string(ynnlm_char_raw_count);
      response.diagnostics += ", ynnlm_char_count=" +
                              std::to_string(ynnlm_char_count);
      if (!ynnlm_char_top_raw.empty()) {
        response.diagnostics += ", ynnlm_char_top_raw=" + ynnlm_char_top_raw;
      }
      if (!ynnlm_char_top_filtered.empty()) {
        response.diagnostics += ", ynnlm_char_top_filtered=" +
                                ynnlm_char_top_filtered;
      }
    }
    if (ynnlm_char_skipped_dense) {
      response.diagnostics += ", ynnlm_char=skipped_dense_word";
    }
    if (ynnlm_subword_ran) {
      response.diagnostics += ", ynnlm_subword_raw_count=" +
                              std::to_string(ynnlm_subword_raw_count);
      response.diagnostics += ", ynnlm_subword_count=" +
                              std::to_string(ynnlm_subword_count);
      response.diagnostics += ", ynnlm_subword_context_cache=" +
                              std::string(ynnlm_subword_context_cache_hit
                                              ? "hit"
                                              : "miss");
      if (!ynnlm_subword_top_raw.empty()) {
        response.diagnostics += ", ynnlm_subword_top_raw=" +
                                ynnlm_subword_top_raw;
      }
      if (!ynnlm_subword_top_filtered.empty()) {
        response.diagnostics += ", ynnlm_subword_top_filtered=" +
                                ynnlm_subword_top_filtered;
      }
    }
    if (ynnlm_subword_skipped_dense) {
      response.diagnostics += ", ynnlm_subword=skipped_dense_word";
    }
    if (ynnlm_has_kto_probability) {
      response.diagnostics += ", ynnlm_kto_raw=" +
                              FormatDiagnosticFloat(ynnlm_kto_probability);
      float kto_feature = 0.0f;
      if (CalibrateNgramLogit(ynnlm_kto_probability,
                              kAndroidNgramCalibration, &kto_feature)) {
        response.diagnostics += ", ynnlm_kto_feature=" +
                                FormatDiagnosticFloat(kto_feature);
      }
    }
  }
  if (!ynnlm_skip_reason.empty()) {
    response.diagnostics += ", ynnlm_skipped=" + ynnlm_skip_reason;
  }
  const std::string ynnlm_contract =
      !word_chain_asset_present_
          ? "unsupported:missing_asset"
          : (word_chain_.is_loaded() ? "valid" : "unsupported:runtime");
  const std::string ynnlm_load =
      !word_chain_asset_present_
          ? "not_attempted"
          : (word_chain_.is_loaded() ? "loaded" : "failed:runtime");
  std::string ynnlm_fallback;
  if (request.disable_ynnlm) {
    ynnlm_fallback = "disabled:debug_switch";
  } else if (ynnlm_batch_ran && ynnlm_encoder_path == "runtime" &&
      ynnlm_model_scored > 0) {
    ynnlm_fallback = "none";
  } else if (ynnlm_batch_ran && ynnlm_encoder_path == "runtime_failed") {
    ynnlm_fallback = "word_chain:runtime_failed";
  } else if (ynnlm_batch_ran && ynnlm_encoder_path == "cpp") {
    ynnlm_fallback = "cpp_encoder:runtime_unavailable";
  } else if (!word_chain_asset_present_) {
    ynnlm_fallback = "static:missing_asset";
  } else if (!word_chain_.is_loaded()) {
    ynnlm_fallback = "static:load_failed";
  } else {
    ynnlm_fallback = "static:" +
        std::string(ynnlm_skip_reason.empty() ? "prefix_gate"
                                               : ynnlm_skip_reason);
  }
  response.diagnostics +=
      ", ynnlm_route={asset=" +
      std::string(word_chain_asset_present_ ? "present" : "missing") +
      ";contract=" + ynnlm_contract + ";variant=default;load=" + ynnlm_load +
      ";invoke_count=" +
      std::to_string(ynnlm_invoke_count_.load(std::memory_order_relaxed)) +
      ";scored_count=" +
      std::to_string(ynnlm_scored_count_.load(std::memory_order_relaxed)) +
      ";fallback=" + ynnlm_fallback + ";latency_us=" +
      std::to_string(static_cast<uint64_t>(std::max<int64_t>(
          0, ynnlm_elapsed_ms)) * 1000ULL) + "}";
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

EmojiSearchResponse KeyboardEngine::SearchEmojis(
    const EmojiSearchRequest& request) const {
  EmojiSearchResponse response;
  if (!package_.is_open()) {
    response.status = Status::InvalidState("Engine not open");
    response.diagnostics = "asset=unknown,contract=not_checked,load=closed";
    return response;
  }

  const std::string normalized = NormalizeToken(request.query);
  if (normalized.empty()) {
    response.status = Status::InvalidArgument("Empty emoji query");
    response.diagnostics = "asset=" +
        std::string(emoji_search_asset_present_ ? "present" : "missing") +
        ",contract=not_invoked,load=" +
        std::string(emoji_search_model_.is_loaded() ? "loaded" : "not_loaded");
    return response;
  }

  if (request.disable_model) {
    response.diagnostics =
        "asset=" +
        std::string(emoji_search_asset_present_ ? "present" : "missing") +
        ",contract=" +
        std::string(emoji_search_model_.is_loaded() ? "valid" : "invalid") +
        ",variant=default,load=" +
        std::string(emoji_search_model_.is_loaded() ? "loaded" : "not_loaded") +
        ",invoke_count=" +
        std::to_string(emoji_search_model_.invocation_count()) +
        ",scored_count=" + std::to_string(emoji_search_model_.scored_count()) +
        ",fallback=diagnostic_bypass,latency_us=0";
    return response;
  }

  if (!emoji_search_model_.is_loaded()) {
    response.status = emoji_search_asset_present_
                          ? Status::UnsupportedFormat(
                                "Emoji search model failed contract validation")
                          : Status::NotFound("Emoji search model asset missing");
    response.diagnostics =
        "asset=" +
        std::string(emoji_search_asset_present_ ? "present" : "missing") +
        ",contract=" +
        std::string(emoji_search_asset_present_ ? "invalid" : "not_checked") +
        ",variant=default,load=not_loaded,invoke_count=0,scored_count=0,"
        "fallback=none,latency_us=0";
    return response;
  }

  const auto started = std::chrono::steady_clock::now();
  const size_t max_results = std::min<size_t>(request.max_results, 128);
  EmojiSearchResult result =
      emoji_search_model_.Search(normalized, max_results, 200);
  const uint64_t latency_us = static_cast<uint64_t>(std::max<int64_t>(
      0, std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now() - started)
             .count()));
  response.emojis = std::move(result.emojis);
  response.model_engaged = true;
  response.diagnostics =
      "asset=present,contract=valid,variant=default,load=loaded,invoke_count=" +
      std::to_string(emoji_search_model_.invocation_count()) +
      ",scored_count=" + std::to_string(emoji_search_model_.scored_count()) +
      ",fallback=none,latency_us=" + std::to_string(latency_us) +
      ",route=utf16be_comptrie_prefix,entries=" +
      std::to_string(result.trie_entries_examined) + ",groups=" +
      std::to_string(result.groups_scored);
  return response;
}

EmojiSuggestResponse KeyboardEngine::SuggestEmojis(
    const EmojiSuggestRequest& request) const {
  EmojiSuggestResponse response;
  response.generation = request.generation;
  if (!package_.is_open()) {
    response.status = Status::InvalidState("Engine not open");
    response.diagnostics = "asset=unknown,contract=not_checked,load=closed";
    return response;
  }

  if (request.sensitive_context) {
    response.diagnostics =
        "asset=" +
        std::string(emoji_suggest_asset_present_ ? "present" : "missing") +
        ",contract=not_invoked,variant=KEYBOARD-13321,"
        "fallback=privacy_suppressed,route=k13321_context_emoji_runtime";
    return response;
  }

  if (request.disable_model) {
    response.diagnostics =
        "asset=" +
        std::string(emoji_suggest_asset_present_ ? "present" : "missing") +
        ",contract=" +
        std::string(emoji_suggest_runner_.is_loaded() ? "valid" : "invalid") +
        ",variant=KEYBOARD-13321,invoke_count=" +
        std::to_string(emoji_suggest_runner_.invocation_count()) +
        ",scored_count=" +
        std::to_string(emoji_suggest_runner_.scored_count()) +
        ",fallback=diagnostic_bypass,route=k13321_context_emoji_runtime";
    return response;
  }

  if (!emoji_suggest_runner_.is_loaded()) {
    response.status =
        emoji_suggest_asset_present_
            ? Status::UnsupportedFormat(
                  "Context emoji model failed atomic contract validation")
            : Status::NotFound("Context emoji model assets missing");
    response.diagnostics = emoji_suggest_runner_.diagnostics();
    return response;
  }

  const size_t max_results = std::min<size_t>(request.max_results, 3);
  if (request.completed_words.empty() || max_results == 0) {
    response.diagnostics = emoji_suggest_runner_.diagnostics() +
                           ",ui_gate=empty_context";
    return response;
  }

  ContextEmojiInference inference = emoji_suggest_runner_.Suggest(
      request.completed_words, request.package_name, max_results);
  response.model_engaged = inference.model_engaged;
  response.diagnostics = std::move(inference.diagnostics);
  if (!inference.model_engaged) return response;

  if (inference.top_probability < kContextEmojiConfidenceThreshold) {
    response.diagnostics +=
        ",ui_gate=below_confidence,top_probability=" +
        FormatDiagnosticFloat(inference.top_probability) +
        ",threshold=" +
        FormatDiagnosticFloat(kContextEmojiConfidenceThreshold);
    return response;
  }

  response.candidates = std::move(inference.candidates);
  response.diagnostics +=
      ",ui_gate=accepted,top_probability=" +
      FormatDiagnosticFloat(inference.top_probability) +
      ",threshold=" + FormatDiagnosticFloat(kContextEmojiConfidenceThreshold);
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
