/*
 * OneNc7Trie — active-state decoder for Yandex 1nc7 trie.
 *
 * Multi-level LOUDS trie: root (CP1251) → child levels (CP866-based labels).
 * Terminal nodes carry frequency weights via IntVector; leaf levels use suffix
 * pools for efficient continuation materialization.
 *
 * This replaces the word-list-based PredictorEngine with true Yandex-style
 * direct trie traversal — no intermediate flat word list.
 */

#pragma once

#include "status.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hoskey::engine {

struct OneNc7NodeState {
  uint32_t node_id = 0;
  uint8_t label_cp1251 = 0;
  bool terminal = false;
  bool has_child = false;
};

struct OneNc7NextLetters {
  std::string all;
  std::string nonterminal;
  std::string terminal;
};

struct OneNc7LevelInfo {
  uint32_t depth = 0;
  size_t start_offset = 0;
  uint64_t node_count_hint = 0;
  uint64_t label_bitwidth = 0;
  size_t has_child_raw_offset = 0;
  size_t has_child_bits_offset = 0;
  uint64_t has_child_word_count = 0;
  size_t louds_raw_offset = 0;
  size_t louds_bits_offset = 0;
  uint64_t louds_word_count = 0;
  size_t terminal_raw_offset = 0;
  size_t terminal_bits_offset = 0;
  uint64_t terminal_word_count = 0;
  size_t terminal_rank_ds_offset = 0;
  size_t intvector_offset = 0;
  size_t intvector_data_offset = 0;
  size_t intvector_data_size = 0;
  uint64_t intvector_bitwidth = 0;
  uint64_t intvector_mask = 0;
  uint64_t intvector_total_bits = 0;
  uint64_t intvector_word_count = 0;
  size_t label_blob_offset = 0;
  size_t label_blob_size = 0;
  size_t suffix_blob_offset = 0;
  size_t suffix_blob_size = 0;
  uint64_t child_flag = 0;
  size_t child_start_offset = 0;
};

struct OneNc7SuffixEntry {
  uint32_t offset = 0;
  std::string text;
};

struct OneNc7Completion {
  std::string text;
  uint32_t terminal_node_id = 0;
  uint32_t extra_letters = 0;
};

/// A ranked candidate with frequency weight from native 1nc7 traversal.
struct OneNc7Candidate {
  std::string text;
  int32_t weight = 0;
  uint32_t depth = 0;        // trie depth (letters)
  uint32_t level = 0;        // which trie level the terminal was found in
};

class OneNc7Trie {
 public:
  OneNc7Trie() = default;

  Status Parse(const uint8_t* data, size_t size);

  [[nodiscard]] bool loaded() const { return loaded_; }
  [[nodiscard]] size_t node_capacity() const;

  [[nodiscard]] std::string RootLettersUtf8() const;
  [[nodiscard]] std::vector<OneNc7NodeState> WalkPrefix(
      std::string_view utf8_prefix) const;
  [[nodiscard]] OneNc7NextLetters NextLetters(std::string_view utf8_prefix) const;

  [[nodiscard]] bool HasPrefix(std::string_view utf8_prefix) const;
  [[nodiscard]] bool HasTerminalBoundary(std::string_view utf8_prefix) const;
  [[nodiscard]] const std::vector<OneNc7LevelInfo>& levels() const {
    return levels_;
  }
  [[nodiscard]] std::string FormatLevelSummary() const;
  [[nodiscard]] std::vector<OneNc7SuffixEntry> SuffixEntries(
      size_t level_index, size_t max_entries = 0) const;
  [[nodiscard]] size_t SuffixEntryCount(size_t level_index) const;
  [[nodiscard]] bool HasSuffixEntry(size_t level_index,
                                    std::string_view utf8_text) const;
  [[nodiscard]] bool PackedUpper(size_t level_index, uint64_t rank_index,
                                 uint64_t* out) const;
  [[nodiscard]] bool TerminalKey(size_t level_index, uint64_t node_id,
                                 uint64_t rank_index, uint64_t* out) const;
  [[nodiscard]] uint64_t RootTerminalRank(uint64_t node_id) const;
  [[nodiscard]] bool RootTerminalKey(uint64_t node_id, uint64_t* out) const;
  [[nodiscard]] std::vector<OneNc7Completion> TerminalCompletions(
      std::string_view utf8_prefix, size_t max_extra_letters = 4,
      size_t max_results = 32) const;

  /// Multi-level DFS: extract all terminal words with frequency weights.
  /// Traverses root level and all recursive child levels.
  /// Suffix pool materializes continuations at leaf levels.
  [[nodiscard]] bool ExtractAllWords(std::vector<std::string>* words,
                                     std::vector<int32_t>* weights,
                                     size_t max_words = 0) const;

  /// Native Yandex-style candidate generation from a prefix.
  /// Walks prefix to active states, then does bounded DFS through all trie
  /// levels to collect terminal candidates ranked by frequency weight.
  /// This is the primary candidate provider — no flat word list involved.
  [[nodiscard]] std::vector<OneNc7Candidate> Complete(
      std::string_view utf8_prefix, size_t max_results = 12,
      size_t max_extra_letters = 8) const;

  /// Exact word lookup via multi-level trie traversal.
  [[nodiscard]] bool Contains(std::string_view utf8_word) const;

  /// Static unigram-like weight for an exact visible word when 1nc7 can prove
  /// the terminal boundary. Returns 0 for prefixes/unknowns.
  [[nodiscard]] int32_t StaticWeight(std::string_view utf8_word) const;
  /// Cheap direct-root terminal weight. Does not run iterator materialization.
  [[nodiscard]] int32_t DirectStaticWeight(std::string_view utf8_word) const;

  /// Total terminal words across all levels (cached after ExtractAllWords).
  [[nodiscard]] size_t total_word_count() const { return total_word_count_; }

 private:
  struct RawBitVector {
    static constexpr uint64_t kRankBlockWords = 256;

    const uint8_t* bytes = nullptr;
    uint64_t word_count = 0;
    std::vector<uint64_t> rank1_superblocks;

    [[nodiscard]] bool valid() const { return bytes != nullptr && word_count != 0; }
    [[nodiscard]] uint64_t bit_capacity() const { return word_count * 64ULL; }
    [[nodiscard]] uint64_t Word(uint64_t idx) const;
    [[nodiscard]] bool Bit(uint64_t pos) const;
    [[nodiscard]] uint64_t Rank1(uint64_t pos) const;
    [[nodiscard]] uint64_t Select1(uint64_t n) const;
    void BuildRankCache();
  };

  /// Live bitvector + label view for one recursive trie level.
  struct LeveledView {
    RawBitVector has_child;
    RawBitVector louds;
    RawBitVector terminal;
    const uint8_t* labels = nullptr;
    size_t labels_size = 0;
    const uint8_t* suffix_blob = nullptr;
    size_t suffix_size = 0;
    size_t intvector_data_offset = 0;
    uint64_t intvector_bitwidth = 0;
    uint64_t intvector_mask = 0;
    uint64_t intvector_word_count = 0;
    uint64_t intvector_total_bits = 0;
    bool is_root = false;

    // FUN_00661462 fields: IntVector for upper-bit extension.
    // From decomp: param_1+0x160 = intvector data ptr, +0x168 = bitwidth,
    // +0x170 = mask, +0x178 = labels ptr.
    const uint8_t* fun_00661462_data = nullptr;   // +0x160
    uint64_t fun_00661462_bitwidth = 0;            // +0x168
    uint64_t fun_00661462_mask = 0;                // +0x170
    const uint8_t* fun_00661462_labels = nullptr;  // +0x178 (same as labels_)

    [[nodiscard]] size_t node_capacity() const {
      return static_cast<size_t>(
          std::min(has_child.bit_capacity(), louds.bit_capacity()));
    }
    [[nodiscard]] bool BitVectorBit(const RawBitVector& bv, uint64_t pos) const;
    [[nodiscard]] uint8_t LabelByte(uint64_t node_id) const;
    [[nodiscard]] bool HasChild(uint64_t node_id) const;
    [[nodiscard]] bool IsTerminal(uint64_t node_id) const;
    [[nodiscard]] uint64_t FirstChild(uint64_t node_id) const;
    [[nodiscard]] std::vector<uint64_t> Children(uint64_t node_id) const;
    [[nodiscard]] bool LabelToUtf8(uint64_t node_id, std::string* out) const;
    /// For non-root levels: decode CP866-like label to CP1251, then to UTF-8.
    [[nodiscard]] bool SubLabelToUtf8(uint64_t node_id, std::string* out) const;

    /// FUN_00661462: compute terminal handoff key for a terminal node.
    /// Returns (upper << 8) | label_byte.
    /// upper = intvector[rank_index] & mask, shifted by bitwidth.
    [[nodiscard]] uint64_t TerminalHandoffKey(uint64_t node_id,
                                              uint64_t rank_index) const;
  };

  /// Reversed iterator state used by FUN_00661926/FUN_00661baa.
  /// q[depth] is the active key.  depth==0 is the root-phase; depth>0 is the
  /// recursive materializer.
  struct MaterializerState {
    uint64_t q[4] = {0, 0, 0, 0};
    uint8_t depth = 0;
  };

  enum class MaterializerStepStatus {
    kEmit,
    kEnd,
    kInvalid,
  };

  static constexpr uint32_t kMagic1nc7 = 0x37636E31;
  static constexpr size_t kHasChildOffset = 0x000008;
  static constexpr size_t kLoudsOffset = 0x0278D8;
  static constexpr size_t kTerminalOffset = 0x0753C8;
  static constexpr size_t kLabelsOffset = 0x0FDB98;
  static constexpr size_t kMaxBranchKids = 64;
  static constexpr int32_t kDefaultWeight = 500;
  static constexpr int32_t kMinWeight = 1;

  bool loaded_ = false;
  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
  RawBitVector has_child_;         // root level only (kept for existing API)
  RawBitVector louds_;
  RawBitVector terminal_;
  const uint8_t* labels_ = nullptr;
  size_t labels_size_ = 0;
  std::vector<OneNc7LevelInfo> levels_;
  std::vector<LeveledView> level_views_;
  mutable size_t total_word_count_ = 0;

  [[nodiscard]] Status LoadBitVector(size_t offset, RawBitVector* out) const;
  [[nodiscard]] Status ParseRecursiveLevels();
  [[nodiscard]] Status BuildLevelViews();
  [[nodiscard]] const OneNc7LevelInfo* Level(size_t level_index) const;
  [[nodiscard]] const LeveledView* LevelView(size_t level_index) const;
  [[nodiscard]] std::string DecodeCp1251Slice(size_t offset, size_t size) const;
  [[nodiscard]] uint8_t LabelByte(uint64_t node_id) const;
  [[nodiscard]] bool HasChild(uint64_t node_id) const;
  [[nodiscard]] bool IsTerminal(uint64_t node_id) const;
  [[nodiscard]] uint64_t FirstChild(uint64_t node_id) const;
  [[nodiscard]] std::vector<uint64_t> Children(uint64_t node_id) const;
  [[nodiscard]] std::vector<OneNc7NodeState> RootStates() const;

  int32_t TerminalWeightForLevel(const LeveledView& view,
                                 uint64_t node_id) const;

  [[nodiscard]] MaterializerStepStatus StepRecursiveMaterializer(
      const MaterializerState& state,
      uint8_t* emitted,
      MaterializerState* next_state) const;
  [[nodiscard]] bool MaterializeRootTerminalContinuation(
      uint64_t terminal_node_id,
      const std::string& base_text,
      size_t max_extra_letters,
      OneNc7Candidate* out) const;
  [[nodiscard]] static bool AppendMaterializedByte(uint8_t byte,
                                                   std::string* out);
  [[nodiscard]] static bool IsPlausibleSuggestionText(
      std::string_view utf8_text);

  [[nodiscard]] static bool Cp1251ToCodepoint(uint8_t cp1251, char32_t* out);
  [[nodiscard]] static bool CodepointToCp1251Lower(char32_t codepoint, uint8_t* out);
  [[nodiscard]] static bool DecodePrefixToCp1251(std::string_view utf8,
                                                 std::vector<uint8_t>* out);
  [[nodiscard]] static std::string CodepointsToUtf8(const std::vector<char32_t>& cps);
  [[nodiscard]] static std::string SortedLettersToUtf8(
      const std::vector<char32_t>& letters);
};

}  // namespace hoskey::engine
