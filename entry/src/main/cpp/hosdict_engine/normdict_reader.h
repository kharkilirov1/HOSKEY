#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "status.h"

namespace hoskey::engine {

// NormDictReader parses the normdict binary blob.
//
// File layout (1,159,136 bytes for Russian):
//   [trie body]                    -- succinct trie (SuggestionTrieFormatVersion=2)
//   [frequency index tables]       -- descending u32 offsets near the end of trie body
//   [DATA section header: 16 bytes]  -- magic "DATA" + u32(0) + u64(text_offset)
//   [key=value text metadata]      -- ASCII lines: "Key = Value\n"
//   [null padding]
//   [META section header: 16 bytes]  -- magic "META" + u32(0) + u32(size) + u32(0)
//   [zlib-compressed metadata blob]  -- additional metadata (optional)
//
// Metadata keys observed:
//   Caps.ComputesSuggestionFrequencies = true
//   Caps.RanksSuggestions = true
//   Caps.WantsLowercaseQuery = true
//   Meta.NormDict.Language = ru
//   Meta.NormDict.MaxOrder = 1
//   Meta.Type = NormDictionary
//   SuggestionTrieFormatVersion = 2
//
// The trie body uses a format related to the Yandex SuggestionTrie (version 2).
// Full trie traversal is not yet implemented; normalization uses UTF-8 lowercase
// with Cyrillic-specific rules (including e/yo normalization).
class NormDictReader {
 public:
  Status Load(const uint8_t* data, size_t size);

  // Normalize a word. If the trie is fully decoded, returns the canonical form.
  // Otherwise, returns UTF-8 lowercase with yo->e normalization.
  std::string Normalize(const std::string& word) const;

  // Get suggestion frequency for a word (if available from trie data).
  // Returns 0 if not available or trie is not decoded.
  uint32_t GetSuggestionFrequency(const std::string& word) const;

  bool loaded() const;
  std::string language() const;
  int max_order() const;
  bool wants_lowercase_query() const;
  bool computes_suggestion_frequencies() const;
  bool ranks_suggestions() const;
  int trie_format_version() const;
  std::string meta_type() const;

  const std::map<std::string, std::string>& metadata() const;

  // Diagnostic info about what was actually decoded.
  const std::vector<std::string>& load_notes() const;

  // Raw trie body size (without trailing metadata sections).
  size_t trie_body_size() const;

 private:
  // Parse the DATA section and extract key=value text metadata.
  Status ParseSections(const uint8_t* data, size_t size);

  // Find and parse the DATA section marker.
  // Sets data_section_offset_ and text_metadata_offset_.
  bool FindDataSection(const uint8_t* data, size_t size);

  // Find and parse the META section marker.
  bool FindMetaSection(const uint8_t* data, size_t size);

  // Parse key=value text from the given range.
  void ParseTextMetadata(const uint8_t* data, size_t offset, size_t end);

  // Analyze trie body structure (header, format detection).
  void AnalyzeTrieBody(const uint8_t* data, size_t trie_end);

  // Simple UTF-8 lowercase (Cyrillic + ASCII) with yo->e normalization.
  static std::string Utf8NormalizeLower(const std::string& input);

  // --- Data ---

  std::map<std::string, std::string> metadata_;
  std::vector<uint8_t> trie_data_;     // Full trie body bytes.
  std::vector<std::string> notes_;

  size_t trie_body_end_ = 0;           // End of trie body (start of DATA section).
  size_t data_section_offset_ = 0;     // Offset of "DATA" magic.
  size_t text_metadata_offset_ = 0;    // Offset of key=value text.
  size_t text_metadata_end_ = 0;       // End of key=value text.
  size_t meta_section_offset_ = 0;     // Offset of "META" magic.

  bool loaded_ = false;
  bool has_data_section_ = false;
  bool has_meta_section_ = false;
};

}  // namespace hoskey::engine
