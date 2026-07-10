/*
 * 1nc7 Succinct Trie - Prefix Matcher for Neural LM
 * 
 * LOUDS-based trie for subword token vocabulary.
 * Supports: prefix validation, completion generation, tokenization.
 */

#pragma once

#include "status.h"

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace hoskey::engine {

// ============================================================================
// Ranked Bitvector with Rank/Select operations
// ============================================================================
struct RankedBitvector {
  uint64_t num_words = 0;
  uint64_t popcount = 0;
  const uint8_t* rank_data = nullptr;
  const uint64_t* bits = nullptr;
  
  uint64_t GetBit(uint64_t pos) const;
  uint64_t Rank1(uint64_t pos) const;
  uint64_t Select1(uint64_t n) const;
  uint64_t Select0(uint64_t n) const;
};

// ============================================================================
// Trie Level (Main or Sub)
// ============================================================================
struct TrieLevel {
  RankedBitvector louds;
  RankedBitvector has_child;
  const uint8_t* labels = nullptr;
  size_t labels_count = 0;
  
  uint8_t GetLabel(uint64_t node_id) const;
  bool HasChildren(uint64_t node_id) const;
  std::optional<uint64_t> FirstChild(uint64_t node_id) const;
  std::optional<uint64_t> NextSibling(uint64_t node_id) const;
  std::optional<uint64_t> Parent(uint64_t node_id) const;
};

// ============================================================================
// 1nc7 File Header
// ============================================================================
struct Trie1nc7Header {
  uint32_t magic;        // "1nc7" = 0x37636E31
  uint32_t version;
  uint32_t payload_hint;
  uint32_t reserved;
  uint32_t item_count;
};

// ============================================================================
// Succinct Trie Decoder
// ============================================================================
class SuccinctTrie {
 public:
  SuccinctTrie() = default;
  
  // Parse 1nc7 file
  Status Parse(const uint8_t* data, size_t size, bool extract_words = false);
  
  // Prefix operations
  bool IsValidPrefix(const std::string& utf8_str) const;
  std::optional<uint64_t> FindPrefixNode(const std::string& utf8_str) const;
  
  // Completion generation
  std::vector<std::string> GetCompletions(
      const std::string& prefix, 
      size_t max_depth = 3) const;
  
  // Tokenization for neural LM
  std::vector<uint32_t> Tokenize(const std::string& utf8_str, size_t vocab_size = 7000) const;
  
  std::string GetTokenString(uint32_t token_id) const;
  
  // Metadata
  bool loaded() const { return loaded_; }
  size_t token_count() const { return sub_level_.labels_count; }
  const std::string& notes() const { return notes_; }
  
  // Extracted words from trie
  const std::vector<std::string>& words() const { return words_; }
  const std::vector<int32_t>& weights() const { return weights_; }
  size_t word_count() const { return words_.size(); }

  /// Full DFS word extraction from the 1nc7 trie.
  /// Populates words_ and weights_ using correct Yandex CP866-based labels.
  Status ExtractWords();

 private:
  bool loaded_ = false;
  bool has_terminal_bv_ = false;
  RankedBitvector terminal_bv_;  // Terminal markers
  Trie1nc7Header header_{};
  TrieLevel main_level_;
  TrieLevel sub_level_;
  const uint8_t* data_ = nullptr;
  size_t data_size_ = 0;
  std::string notes_;
  
  // Extracted words
  std::vector<std::string> words_;
  std::vector<int32_t> weights_;
  
  Status ParseBitvectors();
  Status ParseLabels();
};

}  // namespace hoskey::engine
