#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "autocc_model_runner.h"
#include "hosdict_package.h"
#include "char_trigram_model.h"
#include "emoji_search_model.h"
#include "emoji_suggest_runner.h"
#include "lemmer.h"
#include "ml_ranker.h"
#include "normdict_reader.h"
#include "one_nc7_trie.h"
#include "neural_lm.h"
#include "predictor_engine.h"
#include "rnnt_swipe_model.h"
#include "status.h"
#include "swipe_decoder.h"
#include "tap_model_runner.h"
#include "tfmtrie_decoder.h"
#include "ynnlm_word_chain.h"

namespace hoskey::engine {

enum class PredictorOverlayMode {
  kDisabled,
  kAll,
  kSplitOnly,
};

struct KeyboardDebugConfig {
  bool override_predictor_overlay_mode = false;
  PredictorOverlayMode predictor_overlay_mode = PredictorOverlayMode::kDisabled;
  bool override_predictor_split_allowlist = false;
  bool predictor_split_allowlist = false;
  // Runs the destructive-input matrix against temporary model instances while
  // preserving the live engine. Intended only for explicit device diagnostics.
  bool run_model_negative_audit = false;
};

struct SuggestRequest {
  std::string token;
  std::vector<std::string> previous_tokens;
  std::string locale = "ru-RU";
  bool want_autocorrect = true;
  size_t max_suggestions = 6;
  std::string input;
  std::string full_context;
  std::vector<std::string> options_array;
  double emoji_factor = 0.0;
  std::string package_name;
  bool is_urgent = false;
  bool is_gesture = false;
  // Diagnostic-only A/B switches. They bypass the recovered neural route while
  // retaining the same request, candidate pools, and static fallbacks.
  bool disable_ynnlm = false;
  bool disable_rnnt = false;
  bool disable_tap_model = false;
  bool disable_autocc_model = false;
  std::string autocc_variant = "default";
  bool sensitive_context = false;
  uint32_t field_action_id = 0;
  uint32_t gender = 0;
  uint32_t age = 0;

  struct PrevWordInfo {
    std::string token;
    bool was_autocorrected = false;
  };
  std::vector<PrevWordInfo> prev_words_info;

  struct DictionarySession {
    std::string id;
    std::string locale;
  };
  std::vector<DictionarySession> dictionary_sessions;

  struct ProximityInfo {
    std::string layout;
    uint32_t keyboard_width = 0;
    uint32_t keyboard_height = 0;
    bool enabled = false;
  };
  ProximityInfo proximity_info;

  struct TouchPoint {
    double x = 0.0;
    double y = 0.0;
    uint32_t time_ms = 0;
    uint32_t key_code = 0;
  };
  std::vector<TouchPoint> points;
};

struct Suggestion {
  std::string text;
  uint32_t type_flags = 0;
  uint32_t group_id = 0;
  std::string tag;
  float score = 0.0f;
};

struct SuggestResponse {
  Status status = Status::Ok();
  std::vector<Suggestion> suggestions;
  bool will_autocorrect = false;
  bool is_blocked = false;
  std::string diagnostics;
};

struct CheckWordRequest {
  std::string token;
  std::string locale = "ru-RU";
};

struct CheckWordResponse {
  Status status = Status::Ok();
  bool is_known = false;
  bool is_blocked = false;
  std::string normalized_form;
  std::string diagnostics;
};

struct EmojiSearchRequest {
  std::string query;
  size_t max_results = 64;
  // Diagnostic A/B switch. A disabled request keeps the same input and asset
  // state but must not invoke the recovered compact-trie model.
  bool disable_model = false;
};

struct EmojiSearchResponse {
  Status status = Status::Ok();
  std::vector<std::string> emojis;
  bool model_engaged = false;
  std::string diagnostics;
};

struct EmojiSuggestRequest {
  std::vector<std::string> completed_words;
  std::string package_name;
  size_t max_results = 3;
  // Diagnostic A/B switch. It must preserve the loaded assets and counters
  // while preventing this request from entering the model chain.
  bool disable_model = false;
  // The UI privacy gate is duplicated here so a direct native caller cannot
  // send protected editor context through the model path.
  bool sensitive_context = false;
  uint32_t generation = 0;
};

struct EmojiSuggestResponse {
  Status status = Status::Ok();
  std::vector<ContextEmojiCandidate> candidates;
  bool model_engaged = false;
  uint32_t generation = 0;
  std::string diagnostics;
};

struct PredictorModeCompareResponse {
  Status status = Status::Ok();
  SuggestResponse baseline;
  SuggestResponse verified_split;
  bool changed = false;
  std::string diagnostics;
};

struct ModelInvocationSnapshot {
  uint64_t ynnlm = 0;
  uint64_t rnnt = 0;
  uint64_t tap_ranker = 0;
  uint64_t tap_classifier = 0;
  uint64_t autocc = 0;
  uint64_t emoji_search = 0;
  uint64_t emoji_suggest = 0;

  [[nodiscard]] bool operator==(
      const ModelInvocationSnapshot& other) const noexcept {
    return ynnlm == other.ynnlm && rnnt == other.rnnt &&
           tap_ranker == other.tap_ranker &&
           tap_classifier == other.tap_classifier &&
           autocc == other.autocc && emoji_search == other.emoji_search &&
           emoji_suggest == other.emoji_suggest;
  }
};

class KeyboardEngine {
 public:
  [[nodiscard]] Status Open(const std::filesystem::path& package_root);
  [[nodiscard]] Status Open(const std::filesystem::path& package_root,
                            const KeyboardDebugConfig& debug_config);
  void Close();

  [[nodiscard]] SuggestResponse GetSuggestions(const SuggestRequest& request) const;
  [[nodiscard]] CheckWordResponse CheckWord(const CheckWordRequest& request) const;
  [[nodiscard]] EmojiSearchResponse SearchEmojis(
      const EmojiSearchRequest& request) const;
  [[nodiscard]] EmojiSuggestResponse SuggestEmojis(
      const EmojiSuggestRequest& request) const;
  [[nodiscard]] PredictorModeCompareResponse ComparePredictorModes(
      const SuggestRequest& request) const;
  [[nodiscard]] bool is_open() const { return package_.is_open(); }
  [[nodiscard]] const HosdictPackage& package() const { return package_; }
  [[nodiscard]] const TfmTrieDecoder& tfmtrie() const { return tfmtrie_; }
  [[nodiscard]] const Lemmer& lemmer() const { return lemmer_; }
  [[nodiscard]] const NormDictReader& normdict() const { return normdict_; }
  [[nodiscard]] const OneNc7Trie& one_nc7_trie() const { return one_nc7_trie_; }
  [[nodiscard]] const std::string& model_negative_audit_diagnostics() const {
    return model_negative_audit_diagnostics_;
  }
  [[nodiscard]] ModelInvocationSnapshot model_invocation_snapshot() const {
    ModelInvocationSnapshot snapshot;
    snapshot.ynnlm = ynnlm_invoke_count_.load(std::memory_order_relaxed);
    snapshot.rnnt = rnnt_invoke_count_.load(std::memory_order_relaxed);
    snapshot.tap_ranker = tap_model_runner_.ranker_invocation_count();
    snapshot.tap_classifier =
        tap_model_runner_.classifier_invocation_count();
    snapshot.autocc = autocc_model_runner_.invocation_count();
    snapshot.emoji_search = emoji_search_model_.invocation_count();
    snapshot.emoji_suggest = emoji_suggest_runner_.invocation_count();
    return snapshot;
  }

 private:
  [[nodiscard]] Status InitSubsystems();
  void RunModelNegativeAuditPreload();
  void RunModelNegativeAuditInputs();
  void RecordModelNegativeAuditCase(std::string_view name, bool passed);
  void FinalizeModelNegativeAuditDiagnostics();
  void LoadBlacklist();
  void LoadAutocorrectBlocker();

  [[nodiscard]] std::string NormalizeToken(std::string_view text) const;
  [[nodiscard]] bool IsBlacklisted(const std::string& normalized) const;
  [[nodiscard]] bool IsAutocorrectBlocked(const std::string& normalized) const;
  [[nodiscard]] bool IsKnownWord(const std::string& normalized) const;

  HosdictPackage package_;
  CharTrigramModel char_model_;
  EmojiSearchModel emoji_search_model_;
  EmojiSuggestRunner emoji_suggest_runner_;
  NeuralLM nnlm_;
  RussianSwipeDecoder swipe_decoder_;
  // OneNc7Trie and SuccinctTrie expose zero-copy views. Keep only their 4.5 MB
  // lexicon slice, not the complete 132 MB container backing buffer.
  std::vector<uint8_t> lexicon_asset_storage_;
  OneNc7Trie one_nc7_trie_;
  PredictorEngine predictor_;
  SuccinctTrie succinct_trie_;
  TfmTrieDecoder tfmtrie_;
  Lemmer lemmer_;
  MLRanker ranker_;
  MLRanker swipe_ranker_;
  MLRanker swipe_backoff_ranker_;
  MLRanker swipe_blocker_;
  RnntSwipeModel rnnt_swipe_model_;
  TapModelRunner tap_model_runner_;
  AutoccModelRunner autocc_model_runner_;
  YnnlmWordChain word_chain_;
  NormDictReader normdict_;
  std::unordered_set<std::string> blacklist_;
  std::unordered_set<std::string> autocorrect_blockers_;
  // Manual autocorrect rules parsed once from the (fixed) config JSON at init,
  // instead of re-parsing package_.raw_config_json() on every request.
  std::unordered_map<std::string, std::string> manual_rules_;
  bool ranker_model_asset_present_ = false;
  bool swipe_ranker_asset_present_ = false;
  bool swipe_backoff_asset_present_ = false;
  bool swipe_blocker_asset_present_ = false;
  bool rnnt_components_asset_present_ = false;
  bool nnlm_model_asset_present_ = false;
  bool predictor_asset_present_ = false;
  bool word_chain_asset_present_ = false;
  bool char_model_asset_present_ = false;
  bool emoji_search_asset_present_ = false;
  bool emoji_suggest_asset_present_ = false;
  bool tap_model_asset_present_ = false;
  bool autocc_model_asset_present_ = false;
  mutable std::atomic<uint64_t> ynnlm_invoke_count_{0};
  mutable std::atomic<uint64_t> ynnlm_scored_count_{0};
  mutable std::atomic<uint64_t> rnnt_invoke_count_{0};
  mutable std::atomic<uint64_t> rnnt_scored_count_{0};
  KeyboardDebugConfig debug_config_;
  std::filesystem::path package_root_;
  std::string model_negative_audit_diagnostics_;
  size_t model_negative_audit_passed_ = 0;
  size_t model_negative_audit_total_ = 0;
  size_t model_negative_audit_rnnt_missing_passed_ = 0;
  size_t model_negative_audit_ynnlm_missing_passed_ = 0;
  std::vector<std::string> model_negative_audit_failures_;
};

}  // namespace hoskey::engine
