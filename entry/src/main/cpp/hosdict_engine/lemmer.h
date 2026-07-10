#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "status.h"

namespace hoskey::engine {

// Lemmer implements morphological lemmatization using two binary assets:
//
// 1. lemmer_mhash (CHD perfect hash table, ~1.8 MB):
//    Header layout:
//      [0x00] "ChdTbl\0\0"     -- table magic (8 bytes)
//      [0x08] u32 version=1    -- format version
//      [0x0C] u32 pad=0
//      [0x10] "ChdHash\0"     -- hash section magic (8 bytes)
//      [0x18] u32 version=1
//      [0x1C] u32 field=1
//      [0x20] u32 bucket_count -- hash table size (observed: 524257)
//      [0x24] u32 num_hashes   -- number of hash functions (observed: 2)
//      [0x28] u32 seed0        -- first hash seed
//      [0x2C] u32 seed1        -- second hash seed (observed: 0xFFFFFFFF)
//      [0x30] u32 section1_size -- displacement section byte count
//      [0x34] u32 pad=0
//      [0x38] u32 num_keys     -- number of keys (observed: 103803)
//      [0x3C] u32 section2_ofs -- values section offset (same as section1_size)
//      [0x40] u32 pad=0
//      [0x44] u32 extra_param  -- additional parameter (observed: 9298)
//    Body:
//      [0x48] displacement table (section1_size bytes)
//      [0x48 + section1_size] value/key data
//
// 2. lemmer_rules (morphological rule program, ~539 KB):
//    Header:
//      [0x00] u32 header_size=12 -- FlatBuffer-like root offset
//      [0x04] u32 = 786440
//      [0x08] u32 = 524292
//      [0x0C] u32 = 8            -- entry size or alignment
//      [0x10] u32 = 8
//      [0x14] u32 = rules_data_offset (observed: 325112)
//      [0x18] u32 = num_rules (observed: 3229)
//    Followed by an offset table (u32 offsets into the rules body), then
//    the rules body containing compact symbolic morphological instructions.
//
// Lemmatization pipeline:
//   1. Hash the input word -> get bucket via CHD
//   2. Look up displacement -> compute slot
//   3. Retrieve rule index from slot
//   4. Apply rule to the word suffix to produce the lemma
class Lemmer {
 public:
  Lemmer() = default;
  ~Lemmer() = default;

  // Non-copyable, non-movable (raw pointers into owned vectors).
  Lemmer(const Lemmer&) = delete;
  Lemmer& operator=(const Lemmer&) = delete;
  Lemmer(Lemmer&&) = delete;
  Lemmer& operator=(Lemmer&&) = delete;

  // Reset to clean state.
  void Reset();

  // Load CHD hash table from lemmer_mhash blob.
  Status LoadHash(const uint8_t* data, size_t size);

  // Load morphological rule program from lemmer_rules blob.
  Status LoadRules(const uint8_t* data, size_t size);

  // Lemmatize a word. Returns the lemma (base form) if lookup succeeds,
  // or the original word if lookup fails or data is not fully decoded.
  std::string Lemmatize(const std::string& word) const;

  bool loaded() const;
  bool hash_loaded() const;
  bool rules_loaded() const;

  size_t hash_size() const;
  size_t rules_size() const;

  // Diagnostic info.
  const std::vector<std::string>& load_notes() const;

  // Trace the full CHD lookup pipeline for a word, returning a multi-line
  // string with all intermediate values (hashes, bucket, displacement, slot,
  // rule index, resulting lemma). Useful for debugging misses.
  std::string DiagnosticLookup(const std::string& word) const;

  // Parsed CHD parameters.
  uint32_t bucket_count() const;
  uint32_t num_keys() const;
  uint32_t num_hashes() const;
  uint32_t num_rules() const;

 private:
  // Validate ChdTbl magic at the start of hash data.
  bool ValidateChdMagic(const uint8_t* data, size_t size) const;

  // Parse the CHD header and extract parameters.
  Status ParseChdHeader(const uint8_t* data, size_t size);

  // Parse the rules header and offset table.
  Status ParseRulesHeader(const uint8_t* data, size_t size);

  // Compute FNV-1a hash of a byte sequence.
  static uint32_t Fnv1aHash(const uint8_t* data, size_t len, uint32_t seed);

  // Compute MurmurHash2 of a byte sequence.
  static uint32_t MurmurHash2(const uint8_t* data, size_t len, uint32_t seed);

  // Look up a word in the CHD hash table. Returns the rule index,
  // or UINT32_MAX if not found.
  uint32_t ChdLookup(const std::string& word) const;

  // Apply a morphological rule to produce the lemma.
  std::string ApplyRule(const std::string& word, uint32_t rule_index) const;

  // --- CHD hash table data ---
  struct ChdHeader {
    uint32_t version = 0;
    uint32_t bucket_count = 0;
    uint32_t num_hashes = 0;
    uint32_t seed0 = 0;
    uint32_t seed1 = 0;
    uint32_t section1_size = 0;   // Displacement section byte count.
    uint32_t num_keys = 0;
    uint32_t section2_offset = 0; // Offset of value data from file start.
    uint32_t extra_param = 0;
  };

  ChdHeader chd_header_;
  std::vector<uint8_t> hash_data_;   // Full hash blob (header + displacement + values).
  const uint8_t* displacement_ = nullptr;  // Pointer into hash_data_.
  size_t displacement_size_ = 0;
  const uint8_t* values_ = nullptr;        // Pointer into hash_data_.
  size_t values_size_ = 0;

  // --- Rules data ---
  struct RulesHeader {
    uint32_t header_size = 0;
    uint32_t rules_data_offset = 0;
    uint32_t num_rules = 0;
  };

  RulesHeader rules_header_;
  std::vector<uint8_t> rules_data_;        // Full rules blob.
  std::vector<uint32_t> rule_offsets_;     // Offset table into rules body.
  const uint8_t* rules_body_ = nullptr;    // Pointer into rules_data_.
  size_t rules_body_size_ = 0;

  // --- State ---
  bool hash_loaded_ = false;
  bool rules_loaded_ = false;
  bool hash_parsed_ = false;   // True if CHD header was successfully parsed.
  bool rules_parsed_ = false;  // True if rules header was successfully parsed.
  std::vector<std::string> notes_;

  static constexpr size_t kChdHeaderSize = 0x48;  // Total header before data.
};

}  // namespace hoskey::engine
