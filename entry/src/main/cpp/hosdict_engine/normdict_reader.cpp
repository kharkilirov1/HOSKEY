#include "normdict_reader.h"

#include <algorithm>
#include <cstring>

namespace hoskey::engine {

namespace {

constexpr uint8_t kDataMagic[] = {'D', 'A', 'T', 'A'};
constexpr uint8_t kMetaMagic[] = {'M', 'E', 'T', 'A'};
constexpr size_t kSectionHeaderSize = 16;  // magic(4) + pad(4) + size/offset(8)

// Read a little-endian u32 from a byte pointer.
inline uint32_t ReadU32LE(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

// Read a little-endian u64 from a byte pointer.
inline uint64_t ReadU64LE(const uint8_t* p) {
  return static_cast<uint64_t>(ReadU32LE(p)) |
         (static_cast<uint64_t>(ReadU32LE(p + 4)) << 32);
}

std::string TrimWhitespace(const std::string& s) {
  size_t begin = 0;
  size_t end = s.size();
  while (begin < end && static_cast<unsigned char>(s[begin]) <= 0x20) {
    ++begin;
  }
  while (end > begin && static_cast<unsigned char>(s[end - 1]) <= 0x20) {
    --end;
  }
  return s.substr(begin, end - begin);
}

}  // namespace

bool NormDictReader::FindDataSection(const uint8_t* data, size_t size) {
  // Search for "DATA" marker. It should be near the end of the file,
  // before the text metadata section.
  // Scan backward from the end (but not too far) to find it.
  const size_t search_limit = std::min(size, static_cast<size_t>(4096));
  for (size_t i = size - 4; i >= size - search_limit && i < size; --i) {
    if (std::memcmp(data + i, kDataMagic, 4) == 0) {
      data_section_offset_ = i;

      // DATA section header: "DATA" + u32(0) + u64(text_offset_in_file)
      if (i + kSectionHeaderSize <= size) {
        uint64_t text_offset = ReadU64LE(data + i + 8);
        if (text_offset < size) {
          text_metadata_offset_ = static_cast<size_t>(text_offset);
        } else {
          // Offset points beyond file; text starts right after DATA header.
          text_metadata_offset_ = i + kSectionHeaderSize;
        }
      } else {
        text_metadata_offset_ = i + kSectionHeaderSize;
      }

      has_data_section_ = true;
      return true;
    }
    if (i == 0) break;
  }
  return false;
}

bool NormDictReader::FindMetaSection(const uint8_t* data, size_t size) {
  // Search for "META" marker after the DATA/text section.
  const size_t search_start = has_data_section_ ? data_section_offset_ : 0;
  for (size_t i = search_start; i + 4 <= size; ++i) {
    if (std::memcmp(data + i, kMetaMagic, 4) == 0) {
      meta_section_offset_ = i;
      has_meta_section_ = true;

      // META header: "META" + u32(0) + u32(compressed_size) + u32(0)
      // Followed by zlib-compressed data (starts with 0x78 0x9c).
      if (i + kSectionHeaderSize <= size) {
        uint32_t compressed_size = ReadU32LE(data + i + 8);
        notes_.push_back("META section at offset " + std::to_string(i) +
                         ", compressed size: " + std::to_string(compressed_size));
        // Check for zlib header.
        if (i + kSectionHeaderSize < size && data[i + kSectionHeaderSize] == 0x78) {
          notes_.push_back("META contains zlib-compressed metadata (not decoded)");
        }
      }
      return true;
    }
  }
  return false;
}

void NormDictReader::ParseTextMetadata(const uint8_t* data, size_t offset, size_t end) {
  // Parse ASCII "Key = Value\n" lines.
  const std::string text(reinterpret_cast<const char*>(data + offset), end - offset);

  size_t cursor = 0;
  while (cursor <= text.size()) {
    const size_t next = text.find('\n', cursor);
    const size_t line_end = next == std::string::npos ? text.size() : next;
    std::string line = TrimWhitespace(text.substr(cursor, line_end - cursor));
    cursor = next == std::string::npos ? text.size() + 1 : next + 1;

    if (line.empty()) continue;

    const size_t eq_pos = line.find('=');
    if (eq_pos == std::string::npos) continue;

    std::string key = TrimWhitespace(line.substr(0, eq_pos));
    std::string value = TrimWhitespace(line.substr(eq_pos + 1));
    if (!key.empty()) {
      metadata_[std::move(key)] = std::move(value);
    }
  }
}

void NormDictReader::AnalyzeTrieBody(const uint8_t* data, size_t trie_end) {
  if (trie_end < 16) {
    notes_.push_back("trie body too small for analysis");
    return;
  }

  // Report first few bytes for diagnostic.
  std::string header_hex;
  for (size_t i = 0; i < std::min(trie_end, static_cast<size_t>(16)); ++i) {
    char buf[4];
    std::snprintf(buf, sizeof(buf), "%02x", data[i]);
    if (!header_hex.empty()) header_hex += ' ';
    header_hex += buf;
  }
  notes_.push_back("trie header bytes: " + header_hex);

  // Check for frequency index table near the end of trie body.
  // Observed pattern: descending u32 values (byte offsets into trie) in the last
  // few hundred bytes before the DATA section. These form a frequency rank table.
  if (trie_end > 128) {
    // Check if the last N u32 values before DATA are plausible descending offsets.
    size_t check_start = trie_end - 128;
    // Align to 4 bytes.
    check_start = (check_start + 3) & ~static_cast<size_t>(3);

    bool descending = true;
    size_t desc_count = 0;
    uint32_t prev_val = 0xFFFFFFFF;
    for (size_t i = check_start; i + 4 <= trie_end; i += 4) {
      uint32_t val = ReadU32LE(data + i);
      if (val == 0) break;
      if (val < trie_end && val <= prev_val) {
        ++desc_count;
        prev_val = val;
      } else {
        descending = false;
        break;
      }
    }
    if (descending && desc_count > 4) {
      notes_.push_back("frequency index table detected: ~" +
                       std::to_string(desc_count) + " entries (descending offsets)");
    }
  }

  notes_.push_back("trie body size: " + std::to_string(trie_end) + " bytes");
  notes_.push_back("trie format: SuggestionTrie v" +
                   std::to_string(trie_format_version()) +
                   " (traversal not yet implemented)");
}

Status NormDictReader::ParseSections(const uint8_t* data, size_t size) {
  // Step 1: Find DATA section (contains pointer to text metadata).
  if (!FindDataSection(data, size)) {
    // Fallback: scan backward from end for printable ASCII text metadata.
    size_t pos = size;
    while (pos > 0) {
      uint8_t b = data[pos - 1];
      if (b == '\n' || b == '\r' || b == '\t' || (b >= 0x20 && b <= 0x7E)) {
        --pos;
      } else {
        break;
      }
    }
    if (pos < size - 8) {
      text_metadata_offset_ = pos;
      data_section_offset_ = pos;
      notes_.push_back("no DATA marker found, text metadata detected by scan at offset " +
                       std::to_string(pos));
    } else {
      return Status::ParseError("NormDictReader: no metadata section found");
    }
  } else {
    notes_.push_back("DATA section at offset " + std::to_string(data_section_offset_));
    notes_.push_back("text metadata offset: " + std::to_string(text_metadata_offset_));
  }

  // Step 2: Find where text metadata ends (null byte or META section).
  text_metadata_end_ = size;
  // Look for null terminator after text.
  for (size_t i = text_metadata_offset_; i < size; ++i) {
    if (data[i] == 0x00) {
      text_metadata_end_ = i;
      break;
    }
  }

  // Step 3: Parse text metadata.
  if (text_metadata_offset_ < text_metadata_end_) {
    ParseTextMetadata(data, text_metadata_offset_, text_metadata_end_);
    notes_.push_back("parsed " + std::to_string(metadata_.size()) + " metadata key-value pairs");
  }

  // Step 4: Find META section (optional, contains zlib blob).
  FindMetaSection(data, size);

  // Step 5: Determine trie body extent.
  trie_body_end_ = data_section_offset_;

  return Status::Ok();
}

Status NormDictReader::Load(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return Status::InvalidArgument("NormDictReader: null or empty data");
  }

  metadata_.clear();
  trie_data_.clear();
  notes_.clear();
  loaded_ = false;
  has_data_section_ = false;
  has_meta_section_ = false;
  trie_body_end_ = 0;

  // Parse trailing sections (DATA, text metadata, META).
  Status status = ParseSections(data, size);
  if (!status.ok()) {
    return status;
  }

  // Store trie body for future use.
  if (trie_body_end_ > 0) {
    trie_data_.assign(data, data + trie_body_end_);
  }

  // Diagnostic: identify trie body format by magic bytes.
  if (trie_body_end_ >= 4) {
    uint32_t magic = 0;
    std::memcpy(&magic, data, 4);
    char hex[16];
    std::snprintf(hex, sizeof(hex), "0x%08X", magic);
    notes_.push_back(std::string("trie body magic: ") + hex);
    notes_.push_back("trie body size: " + std::to_string(trie_body_end_));
  }

  // Analyze trie body structure.
  AnalyzeTrieBody(data, trie_body_end_);

  loaded_ = true;
  return Status::Ok();
}

std::string NormDictReader::Normalize(const std::string& word) const {
  if (!loaded_) {
    return word;
  }

  // Full trie-based normalization requires SuggestionTrie v2 traversal,
  // which is not yet implemented. Use UTF-8 lowercase + yo normalization.
  if (wants_lowercase_query()) {
    return Utf8NormalizeLower(word);
  }
  return Utf8NormalizeLower(word);
}

uint32_t NormDictReader::GetSuggestionFrequency(const std::string& /*word*/) const {
  // TODO: If trie body is 1nc7 format, reuse SuccinctTrie decoder.
  // For now, trie weights from lexicon.default serve as frequency proxy.
  // See spec contingency: NormDict frequency deferred.
  return 0;
}

bool NormDictReader::loaded() const {
  return loaded_;
}

std::string NormDictReader::language() const {
  auto it = metadata_.find("Meta.NormDict.Language");
  return it != metadata_.end() ? it->second : std::string{};
}

int NormDictReader::max_order() const {
  auto it = metadata_.find("Meta.NormDict.MaxOrder");
  if (it != metadata_.end()) {
    try { return std::stoi(it->second); } catch (...) {}
  }
  return 0;
}

bool NormDictReader::wants_lowercase_query() const {
  auto it = metadata_.find("Caps.WantsLowercaseQuery");
  if (it != metadata_.end()) {
    return it->second == "true" || it->second == "True" || it->second == "1";
  }
  return false;
}

bool NormDictReader::computes_suggestion_frequencies() const {
  auto it = metadata_.find("Caps.ComputesSuggestionFrequencies");
  if (it != metadata_.end()) {
    return it->second == "true" || it->second == "True" || it->second == "1";
  }
  return false;
}

bool NormDictReader::ranks_suggestions() const {
  auto it = metadata_.find("Caps.RanksSuggestions");
  if (it != metadata_.end()) {
    return it->second == "true" || it->second == "True" || it->second == "1";
  }
  return false;
}

int NormDictReader::trie_format_version() const {
  auto it = metadata_.find("SuggestionTrieFormatVersion");
  if (it != metadata_.end()) {
    try { return std::stoi(it->second); } catch (...) {}
  }
  return 0;
}

std::string NormDictReader::meta_type() const {
  auto it = metadata_.find("Meta.Type");
  return it != metadata_.end() ? it->second : std::string{};
}

const std::map<std::string, std::string>& NormDictReader::metadata() const {
  return metadata_;
}

const std::vector<std::string>& NormDictReader::load_notes() const {
  return notes_;
}

size_t NormDictReader::trie_body_size() const {
  return trie_body_end_;
}

std::string NormDictReader::Utf8NormalizeLower(const std::string& input) {
  std::string result;
  result.reserve(input.size());

  size_t i = 0;
  while (i < input.size()) {
    const auto b = static_cast<uint8_t>(input[i]);

    // ASCII fast path.
    if (b < 0x80) {
      if (b >= 'A' && b <= 'Z') {
        result.push_back(static_cast<char>(b + 32));
      } else {
        result.push_back(input[i]);
      }
      ++i;
      continue;
    }

    // Two-byte UTF-8 sequences (U+0080..U+07FF), covers Cyrillic.
    if (b >= 0xC0 && b < 0xE0 && i + 1 < input.size()) {
      const auto b1 = static_cast<uint8_t>(input[i + 1]);
      const char32_t cp = ((static_cast<char32_t>(b) & 0x1F) << 6) |
                          (static_cast<char32_t>(b1) & 0x3F);

      char32_t lower_cp = cp;

      // Russian uppercase A-Ya (U+0410..U+042F) -> a-ya (U+0430..U+044F).
      if (cp >= 0x0410 && cp <= 0x042F) {
        lower_cp = cp + 0x20;
      }
      // Uppercase Yo (U+0401) -> lowercase e (U+0435).
      // Normalization: yo -> e (standard for Russian keyboard normalization).
      else if (cp == 0x0401) {
        lower_cp = 0x0435;  // е instead of ё
      }
      // Lowercase yo (U+0451) -> lowercase e (U+0435).
      else if (cp == 0x0451) {
        lower_cp = 0x0435;  // е instead of ё
      }

      // Re-encode as two-byte UTF-8.
      result.push_back(static_cast<char>(0xC0 | ((lower_cp >> 6) & 0x1F)));
      result.push_back(static_cast<char>(0x80 | (lower_cp & 0x3F)));
      i += 2;
      continue;
    }

    // Three-byte UTF-8.
    if (b >= 0xE0 && b < 0xF0 && i + 2 < input.size()) {
      result.push_back(input[i]);
      result.push_back(input[i + 1]);
      result.push_back(input[i + 2]);
      i += 3;
      continue;
    }

    // Four-byte UTF-8.
    if (b >= 0xF0 && i + 3 < input.size()) {
      result.push_back(input[i]);
      result.push_back(input[i + 1]);
      result.push_back(input[i + 2]);
      result.push_back(input[i + 3]);
      i += 4;
      continue;
    }

    // Malformed byte: pass through.
    result.push_back(input[i]);
    ++i;
  }

  return result;
}

}  // namespace hoskey::engine
