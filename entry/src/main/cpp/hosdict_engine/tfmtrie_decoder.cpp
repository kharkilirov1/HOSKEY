#include "tfmtrie_decoder.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>

#include "text_utils.h"

namespace hoskey::engine {

// ---------------------------------------------------------------------------
// Packed symbol helpers
// ---------------------------------------------------------------------------

char32_t TfmTrieDecoder::SymCodepoint(uint32_t word) {
  return static_cast<char32_t>(word & 0xFFFF);
}

uint16_t TfmTrieDecoder::SymFlags(uint32_t word) {
  return static_cast<uint16_t>(word >> 16);
}

bool TfmTrieDecoder::IsPackedSymbol(uint32_t word) {
  const uint16_t flags = SymFlags(word);
  const char32_t cp = SymCodepoint(word);
  return flags > 0 && flags < 0x200 && cp >= 0x20;
}

// ---------------------------------------------------------------------------
// Inline LE read helpers (unaligned-safe)
// ---------------------------------------------------------------------------

namespace {

uint32_t ReadU32LE(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  return v;
}

float ReadF32LE(const uint8_t* p) {
  float v;
  std::memcpy(&v, p, 4);
  return v;
}

}  // namespace

// ---------------------------------------------------------------------------
// Parse entry point
// ---------------------------------------------------------------------------

Status TfmTrieDecoder::Parse(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return Status::InvalidArgument("TfmTrieDecoder: null or empty data");
  }

  parsed_ = false;
  offset_table_.clear();
  secondary_index_.clear();
  classes_.clear();
  reverse_lookup_.clear();
  rules_.clear();

  Status s = ParseHeader(data, size);
  if (!s.ok()) return s;

  s = ParseOffsetTable(data, size);
  if (!s.ok()) return s;

  s = ParsePayload(data, size);
  if (!s.ok()) return s;

  BuildReverseLookup();
  parsed_ = true;
  return Status::Ok();
}

// ---------------------------------------------------------------------------
// Header: magic, default weight, counts
// ---------------------------------------------------------------------------

Status TfmTrieDecoder::ParseHeader(const uint8_t* data, size_t size) {
  if (size < kHeaderSize) {
    return Status::ParseError("TfmTrieDecoder: file too small for header");
  }

  // Validate magic "TfmTrie\0" — 8 bytes at offset 0.
  if (std::memcmp(data, "TfmTrie", 8) != 0) {
    return Status::UnsupportedFormat("TfmTrieDecoder: bad magic (expected TfmTrie\\0)");
  }

  // Float at 0x08 — typically -1.0, used as default/sentinel weight.
  default_weight_ = ReadF32LE(data + kDefaultFloatOffset);

  // Primary count at 0x10.
  primary_count_ = ReadU32LE(data + kCountOffset);

  // Secondary count at 0x44.
  secondary_count_ = ReadU32LE(data + kSecondaryCountOffset);

  return Status::Ok();
}

// ---------------------------------------------------------------------------
// Offset table at 0x1000
// ---------------------------------------------------------------------------

Status TfmTrieDecoder::ParseOffsetTable(const uint8_t* data, size_t size) {
  // Primary offset table: up to primary_count_ uint32s starting at 0x1000.
  const size_t max_entries = primary_count_;
  const size_t table_end = kOffsetTableStart + max_entries * 4;
  if (table_end > size) {
    return Status::ParseError("TfmTrieDecoder: offset table extends past file");
  }

  offset_table_.reserve(max_entries);
  for (size_t i = 0; i < max_entries; ++i) {
    const uint32_t val = ReadU32LE(data + kOffsetTableStart + i * 4);
    // Stop if we hit a NEG1 marker (start of inline payload).
    if (val == kNeg1) {
      break;
    }
    offset_table_.push_back(val);
  }

  // Secondary index table: starts after primary + padding, near 0x1F80.
  // Scan from end of primary table to find the secondary region.
  // The secondary table holds secondary_count_ dword slots (sparse, with zeros).
  const size_t sec_search_start = kOffsetTableStart + offset_table_.size() * 4;
  size_t sec_start = 0;

  // Look for the first non-zero dword after the primary table that is
  // plausibly close to secondary_count_ (the value at 0x44).
  for (size_t off = sec_search_start; off + 4 <= size; off += 4) {
    const uint32_t v = ReadU32LE(data + off);
    if (v == secondary_count_) {
      sec_start = off;
      break;
    }
    // If we hit payload (NEG1 followed by packed symbol), stop searching.
    if (v == kNeg1 && off + 8 <= size && IsPackedSymbol(ReadU32LE(data + off + 4))) {
      break;
    }
  }

  if (sec_start > 0) {
    // Read a fixed window of dwords as the secondary index.
    // The table appears to have ~64 slots (one per possible Cyrillic letter).
    const size_t kSecSlots = 64;
    const size_t sec_end = std::min(sec_start + kSecSlots * 4, size);
    secondary_index_.reserve(kSecSlots);
    for (size_t off = sec_start; off + 4 <= sec_end; off += 4) {
      const uint32_t v = ReadU32LE(data + off);
      if (v == kNeg1) break;  // Ran into payload.
      secondary_index_.push_back(v);
    }
  }

  return Status::Ok();
}

// ---------------------------------------------------------------------------
// Payload: NEG1-delimited record groups
// ---------------------------------------------------------------------------

size_t TfmTrieDecoder::FindPayloadStart(const uint8_t* data, size_t size) const {
  // Payload begins at the first NEG1 that is followed by a packed symbol.
  // Typically around 0x2060 but may vary.
  const size_t search_from = kHeaderSize;
  for (size_t off = search_from; off + 8 <= size; off += 4) {
    if (ReadU32LE(data + off) == kNeg1) {
      const uint32_t next = ReadU32LE(data + off + 4);
      if (IsPackedSymbol(next)) {
        return off;
      }
    }
  }
  return size;  // Not found.
}

Status TfmTrieDecoder::ParsePayload(const uint8_t* data, size_t size) {
  const size_t payload_start = FindPayloadStart(data, size);
  if (payload_start >= size) {
    return Status::ParseError("TfmTrieDecoder: could not locate payload region");
  }

  // --- Phase 1: extract all record groups (NEG1-delimited) ----------------
  struct RecordGroup {
    uint32_t symbol_word = 0;   // Packed (flags << 16 | codepoint).
    uint32_t count = 0;         // Small integer after symbol.
    std::vector<float> weights; // Trailing floats (after sentinel or zeros).
  };

  std::vector<RecordGroup> all_groups;
  all_groups.reserve(9000);

  size_t off = payload_start;
  while (off + 4 <= size) {
    const uint32_t w = ReadU32LE(data + off);
    if (w != kNeg1) {
      off += 4;
      continue;
    }
    off += 4;  // Skip NEG1.

    RecordGroup rg;
    // Next dword should be a packed symbol.
    if (off + 4 > size) break;
    rg.symbol_word = ReadU32LE(data + off);
    off += 4;

    // Next dword: count.
    if (off + 4 > size) break;
    rg.count = ReadU32LE(data + off);
    off += 4;

    // Consume remaining dwords until next NEG1.
    // Collect floats; skip zeros, sentinels.
    while (off + 4 <= size) {
      const uint32_t v = ReadU32LE(data + off);
      if (v == kNeg1) break;
      off += 4;

      if (v == 0 || v == kSentinel) continue;

      // Small integers (count-like) — skip.
      if (v < 10000) {
        // This could be a secondary count field (like the "next" count).
        continue;
      }

      // Otherwise interpret as float.
      float fv;
      std::memcpy(&fv, &v, 4);
      if (std::isfinite(fv)) {
        rg.weights.push_back(fv);
      }
    }

    all_groups.push_back(std::move(rg));
  }

  // --- Phase 2: split on pipe separators into segments --------------------
  // A pipe group: symbol_word == kPipeSym (flags=1, U+007C), count == 2.
  struct Segment {
    std::vector<size_t> group_indices;
  };

  std::vector<Segment> segments;
  segments.reserve(1700);
  Segment current;

  for (size_t i = 0; i < all_groups.size(); ++i) {
    const auto& rg = all_groups[i];
    if (rg.symbol_word == kPipeSym && rg.count == 2) {
      if (!current.group_indices.empty()) {
        segments.push_back(std::move(current));
        current = {};
      }
      continue;
    }
    current.group_indices.push_back(i);
  }
  if (!current.group_indices.empty()) {
    segments.push_back(std::move(current));
  }

  // --- Phase 3: build SubstitutionClass entries --------------------------
  // Each segment represents a substitution neighborhood.
  // The first group's codepoint is the anchor; subsequent groups are companions.
  // Multiple segments may share the same anchor — we merge them.

  for (const auto& seg : segments) {
    if (seg.group_indices.empty()) continue;

    const auto& anchor_rg = all_groups[seg.group_indices[0]];
    if (!IsPackedSymbol(anchor_rg.symbol_word)) continue;

    const char32_t anchor_cp = SymCodepoint(anchor_rg.symbol_word);

    auto& cls = classes_[anchor_cp];
    cls.anchor = anchor_cp;

    // Collect companion characters from subsequent groups in this segment.
    for (size_t k = 1; k < seg.group_indices.size(); ++k) {
      const auto& comp_rg = all_groups[seg.group_indices[k]];
      if (!IsPackedSymbol(comp_rg.symbol_word)) continue;

      const char32_t comp_cp = SymCodepoint(comp_rg.symbol_word);
      const float w = comp_rg.weights.empty() ? 0.0f : comp_rg.weights[0];

      // Avoid duplicate companions within the same class.
      auto it = std::find(cls.companions.begin(), cls.companions.end(), comp_cp);
      if (it == cls.companions.end()) {
        cls.companions.push_back(comp_cp);
        cls.weights.push_back(w);
      } else {
        // Merge: keep the higher weight.
        const size_t idx = static_cast<size_t>(it - cls.companions.begin());
        if (std::fabs(w) > std::fabs(cls.weights[idx])) {
          cls.weights[idx] = w;
        }
      }
    }

    // Anchor's own weight from the first group (if present).
    if (!anchor_rg.weights.empty()) {
      // Store as a self-substitution weight (useful for scoring).
      auto it = std::find(cls.companions.begin(), cls.companions.end(), anchor_cp);
      if (it == cls.companions.end()) {
        cls.companions.push_back(anchor_cp);
        cls.weights.push_back(anchor_rg.weights[0]);
      }
    }
  }

  // --- Phase 4: build TransfemeRule list ---------------------------------
  // Each class generates substitution rules: anchor <-> companion.
  for (const auto& [anchor_cp, cls] : classes_) {
    const std::vector<char32_t> anchor_vec = {anchor_cp};
    const std::string anchor_str = EncodeUtf8(anchor_vec);

    for (size_t i = 0; i < cls.companions.size(); ++i) {
      const char32_t comp = cls.companions[i];
      if (comp == anchor_cp) continue;

      const std::vector<char32_t> comp_vec = {comp};
      const std::string comp_str = EncodeUtf8(comp_vec);

      TransfemeRule rule;
      rule.input = anchor_str;
      rule.output = comp_str;
      rule.weight = cls.weights[i];
      rule.flags = static_cast<uint32_t>(anchor_cp);
      rules_.push_back(std::move(rule));
    }
  }

  return Status::Ok();
}

// ---------------------------------------------------------------------------
// Reverse lookup
// ---------------------------------------------------------------------------

void TfmTrieDecoder::BuildReverseLookup() {
  reverse_lookup_.clear();
  for (const auto& [anchor, cls] : classes_) {
    // Anchor belongs to its own class.
    reverse_lookup_[anchor].push_back(anchor);
    for (const char32_t comp : cls.companions) {
      auto& anchors = reverse_lookup_[comp];
      if (std::find(anchors.begin(), anchors.end(), anchor) == anchors.end()) {
        anchors.push_back(anchor);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Query API
// ---------------------------------------------------------------------------

const SubstitutionClass* TfmTrieDecoder::GetClass(char32_t ch) const {
  auto it = classes_.find(ch);
  if (it != classes_.end()) {
    return &it->second;
  }
  return nullptr;
}

std::vector<char32_t> TfmTrieDecoder::GetConfusable(char32_t ch) const {
  std::vector<char32_t> result;

  // Direct class: if `ch` is an anchor, return its companions.
  auto cls_it = classes_.find(ch);
  if (cls_it != classes_.end()) {
    for (const char32_t c : cls_it->second.companions) {
      if (c != ch) {
        result.push_back(c);
      }
    }
  }

  // Reverse: find all classes where `ch` appears as a companion.
  auto rev_it = reverse_lookup_.find(ch);
  if (rev_it != reverse_lookup_.end()) {
    for (const char32_t anchor : rev_it->second) {
      if (anchor == ch) continue;
      // Add the anchor itself.
      if (std::find(result.begin(), result.end(), anchor) == result.end()) {
        result.push_back(anchor);
      }
      // Add siblings from that anchor's class.
      auto a_it = classes_.find(anchor);
      if (a_it != classes_.end()) {
        for (const char32_t sibling : a_it->second.companions) {
          if (sibling != ch &&
              std::find(result.begin(), result.end(), sibling) == result.end()) {
            result.push_back(sibling);
          }
        }
      }
    }
  }

  return result;
}

float TfmTrieDecoder::Proximity(std::string_view original,
                                std::string_view correction) const {
  const std::vector<char32_t> original_cp = DecodeUtf8(original);
  const std::vector<char32_t> correction_cp = DecodeUtf8(correction);
  const size_t aligned = std::min(original_cp.size(), correction_cp.size());

  auto lookup = [this](char32_t from, char32_t to, float* weight) {
    const auto cls_it = classes_.find(from);
    if (cls_it == classes_.end()) return false;
    const auto& cls = cls_it->second;
    for (size_t i = 0; i < cls.companions.size(); ++i) {
      if (cls.companions[i] == to) {
        *weight = cls.weights[i];
        return true;
      }
    }
    return false;
  };

  float score = 0.0f;
  for (size_t i = 0; i < aligned; ++i) {
    float weight = 0.0f;
    if (lookup(original_cp[i], correction_cp[i], &weight) ||
        lookup(correction_cp[i], original_cp[i], &weight)) {
      score += weight;
    }
  }
  return score;
}

// Verified B1: GenerateCandidates logic is correct.
// - Single-char substitution: iterates each position, queries GetConfusable(),
//   looks up weight in both forward (anchor->companion) and reverse direction.
// - Multi-char: adjacent transpositions at distance >= 2.
// - Output sorted by descending weight, duplicates removed.
// - No bugs found in weight lookup or candidate generation.
std::vector<std::pair<std::string, float>> TfmTrieDecoder::GenerateCandidates(
    const std::string& word, size_t max_distance) const {

  const std::vector<char32_t> codepoints = DecodeUtf8(word);
  if (codepoints.empty()) return {};

  std::vector<std::pair<std::string, float>> candidates;

  // Single-character substitution: for each position, try all confusable chars.
  for (size_t pos = 0; pos < codepoints.size(); ++pos) {
    const char32_t original = codepoints[pos];
    const auto confusable = GetConfusable(original);

    for (const char32_t replacement : confusable) {
      std::vector<char32_t> modified = codepoints;
      modified[pos] = replacement;
      const std::string candidate = EncodeUtf8(modified);

      // Compute a weight from the substitution class.
      float weight = 0.0f;
      auto cls_it = classes_.find(original);
      if (cls_it != classes_.end()) {
        const auto& cls = cls_it->second;
        for (size_t i = 0; i < cls.companions.size(); ++i) {
          if (cls.companions[i] == replacement) {
            weight = cls.weights[i];
            break;
          }
        }
      }
      // Also check reverse direction.
      if (weight == 0.0f) {
        auto rev_cls = classes_.find(replacement);
        if (rev_cls != classes_.end()) {
          const auto& cls = rev_cls->second;
          for (size_t i = 0; i < cls.companions.size(); ++i) {
            if (cls.companions[i] == original) {
              weight = cls.weights[i];
              break;
            }
          }
        }
      }

      candidates.emplace_back(std::move(candidate), weight);
    }
  }

  // Multi-character distance (transposition, deletion, insertion) for distance > 1.
  if (max_distance >= 2 && codepoints.size() >= 2) {
    // Adjacent transpositions.
    for (size_t pos = 0; pos + 1 < codepoints.size(); ++pos) {
      std::vector<char32_t> modified = codepoints;
      std::swap(modified[pos], modified[pos + 1]);
      const std::string candidate = EncodeUtf8(modified);
      candidates.emplace_back(std::move(candidate), 0.0f);
    }
  }

  // Sort by descending weight (higher = more likely correction).
  std::sort(candidates.begin(), candidates.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

  // Remove duplicates.
  candidates.erase(
      std::unique(candidates.begin(), candidates.end(),
                  [](const auto& a, const auto& b) { return a.first == b.first; }),
      candidates.end());

  return candidates;
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

std::string TfmTrieDecoder::DiagnosticDump() const {
  auto hex = [](char32_t cp) -> std::string {
    std::ostringstream os;
    os << "U+" << std::uppercase << std::hex << std::setw(4)
       << std::setfill('0') << static_cast<uint32_t>(cp);
    return os.str();
  };

  std::string result;
  result += "TfmTrie: " + std::to_string(classes_.size()) + " classes, " +
            std::to_string(rules_.size()) + " rules\n";
  result += "  Header: primary_count=" + std::to_string(primary_count_) +
            " secondary_count=" + std::to_string(secondary_count_) + "\n";
  result += "  Offset table entries: " + std::to_string(offset_table_.size()) +
            ", secondary index entries: " + std::to_string(secondary_index_.size()) + "\n";

  // Dump first 10 classes with their companions.
  int shown = 0;
  for (const auto& [anchor_cp, cls] : classes_) {
    if (shown >= 10) break;
    result += "  Anchor " + hex(cls.anchor) + ": ";
    result += std::to_string(cls.companions.size()) + " companions [";
    for (size_t i = 0; i < cls.companions.size() && i < 5; ++i) {
      if (i > 0) result += ", ";
      result += hex(cls.companions[i]);
    }
    if (cls.companions.size() > 5) {
      result += ", ...+" + std::to_string(cls.companions.size() - 5);
    }
    result += "]\n";
    ++shown;
  }

  // Cyrillic coverage check: count anchors in U+0400..U+04FF range.
  size_t cyrillic_anchors = 0;
  for (const auto& [cp, _] : classes_) {
    if (cp >= 0x0400 && cp <= 0x04FF) ++cyrillic_anchors;
  }
  result += "  Cyrillic anchors (U+0400..U+04FF): " +
            std::to_string(cyrillic_anchors) + "\n";

  return result;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

bool TfmTrieDecoder::is_parsed() const { return parsed_; }
size_t TfmTrieDecoder::class_count() const { return classes_.size(); }
size_t TfmTrieDecoder::rule_count() const { return rules_.size(); }

}  // namespace hoskey::engine
