#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "hosdict_package.h"
#include "lemmer.h"
#include "ml_ranker.h"
#include "normdict_reader.h"
#include "one_nc7_trie.h"
#include "neural_lm.h"
#include "predictor_engine.h"
#include "status.h"
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

struct PredictorModeCompareResponse {
  Status status = Status::Ok();
  SuggestResponse baseline;
  SuggestResponse verified_split;
  bool changed = false;
  std::string diagnostics;
};

class KeyboardEngine {
 public:
  [[nodiscard]] Status Open(const std::filesystem::path& package_root);
  [[nodiscard]] Status Open(const std::filesystem::path& package_root,
                            const KeyboardDebugConfig& debug_config);
  void Close();

  [[nodiscard]] SuggestResponse GetSuggestions(const SuggestRequest& request) const;
  [[nodiscard]] CheckWordResponse CheckWord(const CheckWordRequest& request) const;
  [[nodiscard]] PredictorModeCompareResponse ComparePredictorModes(
      const SuggestRequest& request) const;
  [[nodiscard]] bool is_open() const { return package_.is_open(); }
  [[nodiscard]] const HosdictPackage& package() const { return package_; }
  [[nodiscard]] const TfmTrieDecoder& tfmtrie() const { return tfmtrie_; }
  [[nodiscard]] const Lemmer& lemmer() const { return lemmer_; }
  [[nodiscard]] const NormDictReader& normdict() const { return normdict_; }
  [[nodiscard]] const OneNc7Trie& one_nc7_trie() const { return one_nc7_trie_; }

 private:
  [[nodiscard]] Status InitSubsystems();
  void LoadBlacklist();
  void LoadAutocorrectBlocker();

  [[nodiscard]] std::string NormalizeToken(std::string_view text) const;
  [[nodiscard]] bool IsBlacklisted(const std::string& normalized) const;
  [[nodiscard]] bool IsAutocorrectBlocked(const std::string& normalized) const;
  [[nodiscard]] bool IsKnownWord(const std::string& normalized) const;

  HosdictPackage package_;
  NeuralLM nnlm_;
  OneNc7Trie one_nc7_trie_;
  PredictorEngine predictor_;
  SuccinctTrie succinct_trie_;
  TfmTrieDecoder tfmtrie_;
  Lemmer lemmer_;
  MLRanker ranker_;
  YnnlmWordChain word_chain_;
  NormDictReader normdict_;
  std::unordered_set<std::string> blacklist_;
  std::unordered_set<std::string> autocorrect_blockers_;
  // Manual autocorrect rules parsed once from the (fixed) config JSON at init,
  // instead of re-parsing package_.raw_config_json() on every request.
  std::unordered_map<std::string, std::string> manual_rules_;
  bool ranker_model_asset_present_ = false;
  bool nnlm_model_asset_present_ = false;
  bool predictor_asset_present_ = false;
  bool word_chain_asset_present_ = false;
  KeyboardDebugConfig debug_config_;
  std::filesystem::path package_root_;
};

}  // namespace hoskey::engine
