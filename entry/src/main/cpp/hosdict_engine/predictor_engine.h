#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "status.h"

namespace hoskey::engine {

/// Weighted word result from predictor.
struct PredictorResult {
  std::string word;
  float score = 0.0f;
  enum Source { kExact, kPrefix, kFuzzy, kAutocorrect } source = kExact;
};

/// Active trie state for one visible prefix.
/// For the current in-memory trie this usually contains one node,
/// but the structure matches the reverse-engineered direction where
/// one spelling can fan out into multiple runtime states.
struct ActiveNodeState {
  enum class Kind {
    kDirect,
    kTerminalHandoff,
  };

  uint32_t node = UINT32_MAX;
  Kind kind = Kind::kDirect;
  bool terminal_here = false;
  bool has_children = false;
  int32_t weight = -1;
};

struct ActivePrefixState {
  std::vector<char32_t> visible_prefix;
  std::vector<ActiveNodeState> states;

  bool empty() const { return states.empty(); }
  size_t size() const { return states.size(); }
};

struct NextLabelGroups {
  std::vector<char32_t> all;
  std::vector<char32_t> direct;
  std::vector<char32_t> terminal_handoff;

  bool empty() const { return all.empty(); }
};

enum class PrototypeTransitionDataMode {
  kAll,
  kVerifiedSplitOnly,
};

/// Compact trie node for in-memory predictor.
/// Stores children as sorted array of (char32, child_index) pairs.
/// Terminal nodes carry frequency weight.
struct TrieNode {
  static constexpr uint32_t kNoChild = UINT32_MAX;

  struct Edge {
    char32_t label = 0;
    uint32_t child = kNoChild;
  };

  std::vector<Edge> children;
  int32_t weight = -1;  // -1 = not terminal, >= 0 = word frequency
  int32_t max_subtree_weight = -1;  // max terminal weight in this subtree
  bool is_terminal() const { return weight >= 0; }

  /// Binary search for child with given label. Returns child index or kNoChild.
  uint32_t find_child(char32_t label) const;
};

/// Real predictor engine built on a proper in-memory trie.
///
/// Replaces the flat word list + binary search approach with:
///   - O(m) exact lookup (m = query length)
///   - O(m + k) prefix search (k = results)
///   - Levenshtein-1 fuzzy search within trie
///   - Frequency-based ranking
class PredictorEngine {
 public:
  /// Build trie from word list (from DFS decoder or extracted file).
  void Build(const std::vector<std::string>& words,
             const std::vector<int32_t>& weights = {});

  /// Load a prebuilt binary predictor_trie_v1 asset.
  [[nodiscard]] Status LoadBinary(const uint8_t* data, size_t size);

  /// Toggle experimental reverse-driven handoff continuation branch.
  void SetExperimentalHandoffEnabled(bool enabled) {
    experimental_handoff_enabled_ = enabled;
  }
  [[nodiscard]] bool experimental_handoff_enabled() const {
    return experimental_handoff_enabled_;
  }

  void SetPrototypeTransitionDataMode(PrototypeTransitionDataMode mode) {
    prototype_transition_data_mode_ = mode;
  }
  [[nodiscard]] PrototypeTransitionDataMode prototype_transition_data_mode()
      const {
    return prototype_transition_data_mode_;
  }

  /// Check if word exists in the trie.
  bool Contains(std::string_view word) const;

  /// Get frequency weight for a word. Returns -1 if not found.
  int32_t GetWeight(std::string_view word) const;

  /// Build active trie states for a visible prefix.
  /// This is the implementation-facing API for the future multi-state
  /// succinct-trie transition model.
  [[nodiscard]] ActivePrefixState BuildActivePrefixState(
      std::string_view prefix) const;

  /// Return merged next labels across all active states.
  [[nodiscard]] std::vector<char32_t> GetMergedNextLabels(
      const ActivePrefixState& active) const;

  /// Return merged next labels split by transition kind.
  /// The current in-memory trie only produces direct branches, but the API is
  /// shaped for future terminal epsilon-handoff transitions.
  [[nodiscard]] NextLabelGroups GetMergedNextLabelGroups(
      const ActivePrefixState& active) const;

  /// Find completions for a prefix, ranked by frequency.
  std::vector<PredictorResult> PrefixSearch(std::string_view prefix,
                                             size_t max_results = 10) const;

  /// Deterministic baseline: collect completions only from direct states.
  std::vector<PredictorResult> PrefixSearchDirectOnly(
      std::string_view prefix, size_t max_results = 10) const;

  /// Experimental path: collect completions from direct and handoff states.
  std::vector<PredictorResult> PrefixSearchWithHandoff(
      std::string_view prefix, size_t max_results = 10) const;

  /// Find words within Levenshtein distance 1 of the query.
  std::vector<PredictorResult> FuzzySearch(std::string_view query,
                                            size_t max_results = 10) const;

  /// Full suggestion pipeline: exact → prefix → fuzzy → autocorrect.
  std::vector<PredictorResult> GetSuggestions(std::string_view token,
                                               std::string_view context = "",
                                               size_t max_results = 5) const;

  size_t node_count() const { return nodes_.size(); }
  size_t word_count() const { return word_count_; }
  bool is_built() const { return !nodes_.empty(); }

 private:
  /// Walk trie to the node matching the given codepoint path.
  /// Returns node index or kNoChild if path doesn't exist.
  uint32_t WalkPath(const std::vector<char32_t>& path) const;

  /// Build a typed active node state from raw node index.
  [[nodiscard]] ActiveNodeState MakeActiveNodeState(
      uint32_t node_idx,
      ActiveNodeState::Kind kind = ActiveNodeState::Kind::kDirect) const;

  /// Prototype reverse-driven materialization hook.
  /// For now this lifts terminal+continuation prefixes into a parallel
  /// terminal-handoff state so the engine can already exercise a multi-state
  /// API before the real succinct-trie handoff logic lands.
  void MaterializeTerminalHandoffStates(const std::vector<char32_t>& prefix,
                                        ActivePrefixState& active) const;

  struct PrototypeContinuationFilters {
    bool filter_direct = false;
    bool filter_handoff = false;
    std::vector<char32_t> direct;
    std::vector<char32_t> handoff;

    bool has_direct() const { return filter_direct; }
    bool has_handoff() const { return filter_handoff; }
  };

  /// Experimental reverse-driven continuation families for a few short mixed
  /// prefixes observed in the 1nc7 reverse notes.
  [[nodiscard]] PrototypeContinuationFilters GetPrototypeContinuationFilters(
      const std::vector<char32_t>& prefix) const;

  /// Insert a word into the trie with given weight.
  void Insert(const std::vector<char32_t>& codepoints, int32_t weight);

  /// Compute max terminal weight for every subtree after build.
  int32_t ComputeMaxSubtreeWeight(uint32_t node_idx);

  /// Collect top terminal words under a node using subtree max weights.
  void CollectWords(uint32_t node_idx, std::vector<char32_t>& path,
                    std::vector<PredictorResult>& results,
                    size_t max_results) const;

  /// Collect words from a state, optionally constraining the first transition.
  void CollectWordsFromState(uint32_t node_idx, std::vector<char32_t>& path,
                             std::vector<PredictorResult>& results,
                             size_t max_results,
                             const std::vector<char32_t>* first_step_filter,
                             float score_bias = 0.0f) const;

  /// Deduplicate and sort active-state results.
  static void SortAndTrimResults(std::vector<PredictorResult>& results,
                                 size_t max_results);

  std::vector<PredictorResult> PrefixSearchInternal(
      std::string_view prefix, size_t max_results, bool include_handoff) const;

  static float HandoffScoreBias(const PrototypeContinuationFilters& filters);

  /// Levenshtein-1 DFS: explore trie with at most 1 edit.
  void FuzzyDFS(uint32_t node_idx, const std::vector<char32_t>& target,
                size_t target_pos, size_t edits_left,
                std::vector<char32_t>& path,
                std::vector<PredictorResult>& results,
                size_t max_results) const;

  /// UTF-8 helpers.
  static std::vector<char32_t> ToCodepoints(std::string_view utf8);
  static std::string ToUtf8(const std::vector<char32_t>& codepoints);
  static char32_t CyrillicLower(char32_t ch);
  static std::vector<char32_t> Normalize(std::string_view utf8);

  std::vector<TrieNode> nodes_;
  size_t word_count_ = 0;
  bool experimental_handoff_enabled_ = true;
  PrototypeTransitionDataMode prototype_transition_data_mode_ =
      PrototypeTransitionDataMode::kAll;
};

}  // namespace hoskey::engine
