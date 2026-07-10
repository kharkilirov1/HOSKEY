#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

#include "status.h"

namespace hoskey::engine {

struct TransfemeRule {
  std::string input;       // Original/error pattern
  std::string output;      // Correction/replacement
  float weight = 0.0f;     // Confidence score
  uint32_t flags = 0;
};

// Represents a character substitution class.
struct SubstitutionClass {
  char32_t anchor;                    // Base letter
  std::vector<char32_t> companions;   // Substitutable characters
  std::vector<float> weights;         // Per-companion weights
};

class TfmTrieDecoder {
 public:
  Status Parse(const uint8_t* data, size_t size);

  // Get substitution class for a character.
  const SubstitutionClass* GetClass(char32_t ch) const;

  // Get all characters that could be confused with `ch`.
  std::vector<char32_t> GetConfusable(char32_t ch) const;

  // Generate correction candidates for a word.
  std::vector<std::pair<std::string, float>> GenerateCandidates(
      const std::string& word, size_t max_distance = 1) const;

  // Diagnostic: human-readable summary of parsed state.
  std::string DiagnosticDump() const;

  bool is_parsed() const;
  size_t class_count() const;
  size_t rule_count() const;

 private:
  static constexpr uint32_t kMagicWord0 = 0x546D6654;  // "TfmT"
  static constexpr uint32_t kMagicWord1 = 0x00656972;  // "rie\0"
  static constexpr uint32_t kNeg1       = 0xBF800000;  // float -1.0
  static constexpr uint32_t kSentinel   = 0xFEFEFEFE;
  static constexpr uint32_t kPipeSym    = 0x0001007C;  // flags=1, U+007C '|'

  static constexpr size_t kHeaderSize          = 0x1000;
  static constexpr size_t kOffsetTableStart    = 0x1000;
  static constexpr size_t kCountOffset         = 0x10;
  static constexpr size_t kSecondaryCountOffset = 0x44;
  static constexpr size_t kDefaultFloatOffset  = 0x08;

  // Packed symbol helpers.
  static char32_t SymCodepoint(uint32_t word);
  static uint16_t SymFlags(uint32_t word);
  static bool     IsPackedSymbol(uint32_t word);

  Status ParseHeader(const uint8_t* data, size_t size);
  Status ParseOffsetTable(const uint8_t* data, size_t size);
  Status ParsePayload(const uint8_t* data, size_t size);

  // Locate payload start — first NEG1 after offset tables.
  size_t FindPayloadStart(const uint8_t* data, size_t size) const;

  // Build reverse lookup (companion -> anchor set).
  void BuildReverseLookup();

  // Header fields.
  float  default_weight_ = -1.0f;
  uint32_t primary_count_ = 0;
  uint32_t secondary_count_ = 0;

  std::vector<uint32_t> offset_table_;
  std::vector<uint32_t> secondary_index_;

  // Forward: anchor -> class.
  std::unordered_map<char32_t, SubstitutionClass> classes_;
  // Reverse: any char -> set of anchors it belongs to.
  std::unordered_map<char32_t, std::vector<char32_t>> reverse_lookup_;

  std::vector<TransfemeRule> rules_;
  bool parsed_ = false;
};

}  // namespace hoskey::engine
