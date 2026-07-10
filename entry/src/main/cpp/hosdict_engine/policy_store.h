#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "status.h"

namespace hoskey::engine {

// PolicyStore parses binary policy blobs used by the Yandex keyboard engine.
//
// Observed file formats:
//   - blacklist (374,455 bytes): byte-level succinct trie over UTF-8 strings.
//   - autocorrect_blocker (6,389 bytes): same trie format.
//   - blacklist_suggest (311 bytes): trie body + trailing SCHM/y3ie schema.
//   - abbr (569 bytes): trie body (abbreviation map).
//
// The trie encoding stores individual UTF-8 bytes as edge labels in a compact
// binary trie (similar to Yandex comptrie). Perfect trie traversal requires
// full format reverse engineering; this implementation uses a robust hybrid:
//   1. Attempt comptrie-style traversal (primary path).
//   2. Fall back to raw byte scan for Contains() if traversal is unavailable.
//
// The class preserves API compatibility: Load(), Contains(), entry_count(),
// loaded(), entries().
class PolicyStore {
 public:
  // Load from raw binary blob. Detects format automatically.
  Status Load(const uint8_t* data, size_t size);

  // Check if word is in this policy set.
  bool Contains(const std::string& word) const;

  size_t entry_count() const;

  bool loaded() const;

  // Export extracted entries (for storing as word list).
  const std::unordered_set<std::string>& entries() const;

  // Diagnostic report of what was actually parsed.
  const std::vector<std::string>& load_notes() const;

 private:
  // --- Trailing metadata parsing ---

  // Some policy blobs have trailing schema sections (SCHM, y3ie).
  // Returns the offset where the trie body ends (start of trailing metadata).
  size_t DetectTrailingMetadata(const uint8_t* data, size_t size);

  // --- Comptrie parsing ---

  // Attempt to walk the comptrie and extract all accepted words.
  // Returns number of words extracted.
  size_t TryParseComptrie(const uint8_t* data, size_t trie_size);

  // Walk the comptrie to check if a specific UTF-8 byte sequence exists.
  bool ComptrieContains(const uint8_t* trie, size_t trie_size,
                        const uint8_t* key, size_t key_len) const;

  // Recursive comptrie word extraction (depth-limited).
  void ComptrieExtract(const uint8_t* trie, size_t trie_size,
                       size_t offset, std::string& prefix,
                       size_t depth_limit);

  // --- Raw scan fallback ---

  // Scan raw bytes for a UTF-8 string surrounded by structural boundaries.
  bool RawScanContains(const std::string& word) const;

  // --- Data ---

  std::unordered_set<std::string> entries_;
  std::vector<uint8_t> trie_data_;     // Raw trie body (no trailing metadata).
  std::vector<std::string> notes_;
  size_t trie_body_size_ = 0;         // Size of trie body excluding metadata.
  bool trie_parsed_ = false;           // True if comptrie walk succeeded.
  bool loaded_ = false;
};

}  // namespace hoskey::engine
