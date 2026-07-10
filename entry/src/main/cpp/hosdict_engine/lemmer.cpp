#include "lemmer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace hoskey::engine {

namespace {

constexpr uint8_t kChdTblMagic[] = {'C', 'h', 'd', 'T', 'b', 'l', 0, 0};
constexpr uint8_t kChdHashMagic[] = {'C', 'h', 'd', 'H', 'a', 's', 'h', 0};
constexpr size_t kChdTblMagicLen = sizeof(kChdTblMagic);
constexpr size_t kChdHashMagicLen = sizeof(kChdHashMagic);

// Read a little-endian u32.
inline uint32_t ReadU32LE(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

// Read a little-endian u16.
inline uint16_t ReadU16LE(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

}  // namespace

void Lemmer::Reset() {
  hash_data_.clear();
  displacement_ = nullptr;
  displacement_size_ = 0;
  values_ = nullptr;
  values_size_ = 0;
  chd_header_ = ChdHeader{};
  rules_data_.clear();
  rule_offsets_.clear();
  rules_body_ = nullptr;
  rules_body_size_ = 0;
  rules_header_ = RulesHeader{};
  hash_loaded_ = false;
  rules_loaded_ = false;
  hash_parsed_ = false;
  rules_parsed_ = false;
  notes_.clear();
}

bool Lemmer::ValidateChdMagic(const uint8_t* data, size_t size) const {
  if (size < kChdTblMagicLen) return false;
  return std::memcmp(data, kChdTblMagic, kChdTblMagicLen) == 0;
}

Status Lemmer::ParseChdHeader(const uint8_t* data, size_t size) {
  if (size < kChdHeaderSize) {
    return Status::ParseError("Lemmer: hash data too small for CHD header (" +
                              std::to_string(size) + " < " +
                              std::to_string(kChdHeaderSize) + ")");
  }

  // Validate ChdTbl magic.
  if (!ValidateChdMagic(data, size)) {
    notes_.push_back("WARNING: ChdTbl magic not found at offset 0");
    // Continue anyway; some versions may lack the magic.
  } else {
    notes_.push_back("ChdTbl magic validated");
  }

  // Validate ChdHash magic at offset 0x10.
  if (size >= 0x18 && std::memcmp(data + 0x10, kChdHashMagic, kChdHashMagicLen) == 0) {
    notes_.push_back("ChdHash magic validated at offset 0x10");
  }

  // Parse header fields.
  chd_header_.version = ReadU32LE(data + 0x08);
  chd_header_.bucket_count = ReadU32LE(data + 0x20);
  chd_header_.num_hashes = ReadU32LE(data + 0x24);
  chd_header_.seed0 = ReadU32LE(data + 0x28);
  chd_header_.seed1 = ReadU32LE(data + 0x2C);
  chd_header_.section1_size = ReadU32LE(data + 0x30);
  chd_header_.num_keys = ReadU32LE(data + 0x38);
  chd_header_.section2_offset = ReadU32LE(data + 0x3C);
  chd_header_.extra_param = ReadU32LE(data + 0x44);

  // Validate parsed values.
  if (chd_header_.bucket_count == 0 || chd_header_.bucket_count > 10000000) {
    return Status::ParseError("Lemmer: implausible bucket_count: " +
                              std::to_string(chd_header_.bucket_count));
  }
  if (chd_header_.num_keys == 0 || chd_header_.num_keys > 10000000) {
    return Status::ParseError("Lemmer: implausible num_keys: " +
                              std::to_string(chd_header_.num_keys));
  }

  // Identify displacement table and values sections.
  displacement_ = data + kChdHeaderSize;
  displacement_size_ = chd_header_.section1_size;

  if (kChdHeaderSize + displacement_size_ > size) {
    return Status::ParseError("Lemmer: displacement section extends beyond file (" +
                              std::to_string(kChdHeaderSize + displacement_size_) +
                              " > " + std::to_string(size) + ")");
  }

  values_ = data + kChdHeaderSize + displacement_size_;
  values_size_ = size - kChdHeaderSize - displacement_size_;

  notes_.push_back("CHD header parsed successfully");
  notes_.push_back("  version: " + std::to_string(chd_header_.version));
  notes_.push_back("  bucket_count: " + std::to_string(chd_header_.bucket_count));
  notes_.push_back("  num_hashes: " + std::to_string(chd_header_.num_hashes));
  notes_.push_back("  seed0: 0x" + [&] {
    char buf[16]; std::snprintf(buf, sizeof(buf), "%08x", chd_header_.seed0); return std::string(buf);
  }());
  notes_.push_back("  seed1: 0x" + [&] {
    char buf[16]; std::snprintf(buf, sizeof(buf), "%08x", chd_header_.seed1); return std::string(buf);
  }());
  notes_.push_back("  section1 (displacement): " + std::to_string(displacement_size_) + " bytes");
  notes_.push_back("  num_keys: " + std::to_string(chd_header_.num_keys));
  notes_.push_back("  values section: " + std::to_string(values_size_) + " bytes");
  notes_.push_back("  extra_param: " + std::to_string(chd_header_.extra_param));

  // Estimate displacement entry size.
  if (chd_header_.bucket_count > 0) {
    double bytes_per_bucket = static_cast<double>(displacement_size_) /
                              static_cast<double>(chd_header_.bucket_count);
    notes_.push_back("  displacement bytes/bucket: " + std::to_string(bytes_per_bucket));
  }

  // Estimate value entry size.
  if (chd_header_.num_keys > 0) {
    double bytes_per_key = static_cast<double>(values_size_) /
                           static_cast<double>(chd_header_.num_keys);
    notes_.push_back("  values bytes/key: " + std::to_string(bytes_per_key));
  }

  return Status::Ok();
}

Status Lemmer::ParseRulesHeader(const uint8_t* data, size_t size) {
  if (size < 28) {
    return Status::ParseError("Lemmer: rules data too small for header");
  }

  // Parse header fields.
  rules_header_.header_size = ReadU32LE(data + 0x00);

  // The rules data offset and count are at specific positions in the header.
  // Observed layout:
  //   [0x14] u32 rules_data_offset = 325112
  //   [0x18] u32 num_rules = 3229
  rules_header_.rules_data_offset = ReadU32LE(data + 0x14);
  rules_header_.num_rules = ReadU32LE(data + 0x18);

  // Validate.
  if (rules_header_.num_rules == 0 || rules_header_.num_rules > 100000) {
    // Try alternate interpretation: maybe num_rules is at a different offset.
    // Check offset 0x18 value; if unreasonable, try others.
    uint32_t alt_count = ReadU32LE(data + 0x10);
    if (alt_count > 0 && alt_count < 100000) {
      rules_header_.num_rules = alt_count;
      rules_header_.rules_data_offset = ReadU32LE(data + 0x14);
      notes_.push_back("rules: using alternate header layout (num_rules at 0x10)");
    } else {
      notes_.push_back("WARNING: rules num_rules looks implausible: " +
                       std::to_string(rules_header_.num_rules));
    }
  }

  if (rules_header_.rules_data_offset >= size) {
    notes_.push_back("WARNING: rules_data_offset beyond file size: " +
                     std::to_string(rules_header_.rules_data_offset) +
                     " >= " + std::to_string(size));
    // Still continue with what we have.
    rules_header_.rules_data_offset = 0;
  }

  // Parse offset table.
  // The offset table starts after the fixed header and contains u32 offsets
  // into the rules body. Each offset points to a rule's instruction data.
  // The table appears to be at offset 0x1C (after the header fields we read),
  // with num_rules entries.
  size_t table_start = 0x1C;
  // num_rules * 4 (the offset-table byte size) overflows uint32 for a corrupt
  // header; compare via division so a huge num_rules cannot wrap past this check.
  if (rules_header_.num_rules > 0 && table_start <= size &&
      rules_header_.num_rules <= (size - table_start) / 4) {
    rule_offsets_.resize(rules_header_.num_rules);
    for (uint32_t i = 0; i < rules_header_.num_rules; ++i) {
      rule_offsets_[i] = ReadU32LE(data + table_start + i * 4);
    }
    notes_.push_back("rules offset table: " + std::to_string(rules_header_.num_rules) +
                     " entries parsed");

    // Identify rules body.
    size_t table_end = table_start + rules_header_.num_rules * 4;
    rules_body_ = data + table_end;
    rules_body_size_ = size - table_end;
    notes_.push_back("rules body: " + std::to_string(rules_body_size_) +
                     " bytes (offset " + std::to_string(table_end) + ")");
  } else {
    notes_.push_back("WARNING: could not parse rules offset table");
    // Store the whole blob as rules body for raw access.
    rules_body_ = data;
    rules_body_size_ = size;
  }

  notes_.push_back("rules header parsed");
  notes_.push_back("  header_size: " + std::to_string(rules_header_.header_size));
  notes_.push_back("  rules_data_offset: " + std::to_string(rules_header_.rules_data_offset));
  notes_.push_back("  num_rules: " + std::to_string(rules_header_.num_rules));

  // Report some sample rule content for diagnostics.
  if (!rule_offsets_.empty() && rules_body_ != nullptr) {
    // Check first and last offsets.
    notes_.push_back("  first rule offset: " + std::to_string(rule_offsets_.front()));
    notes_.push_back("  last rule offset: " + std::to_string(rule_offsets_.back()));

    // Count rules that have printable ASCII content at their offset.
    size_t printable_rules = 0;
    for (uint32_t i = 0; i < std::min(rules_header_.num_rules, 100u); ++i) {
      uint32_t off = rule_offsets_[i];
      if (off < rules_body_size_ && rules_body_[off] >= 0x20 && rules_body_[off] < 0x7F) {
        ++printable_rules;
      }
    }
    notes_.push_back("  printable-start rules (first 100): " +
                     std::to_string(printable_rules));
  }

  return Status::Ok();
}

// Verified 2026-03-31: Standard FNV-1a (basis=2166136261 XOR seed, prime=16777619).
uint32_t Lemmer::Fnv1aHash(const uint8_t* data, size_t len, uint32_t seed) {
  uint32_t hash = 2166136261u ^ seed;
  for (size_t i = 0; i < len; ++i) {
    hash ^= static_cast<uint32_t>(data[i]);
    hash *= 16777619u;
  }
  return hash;
}

// Verified 2026-03-31: Standard MurmurHash2 (m=0x5bd1e995, r=24, LE reads,
// tail fallthrough, finalization: h^=h>>13, h*=m, h^=h>>15).
uint32_t Lemmer::MurmurHash2(const uint8_t* data, size_t len, uint32_t seed) {
  const uint32_t m = 0x5bd1e995;
  const int r = 24;
  uint32_t h = seed ^ static_cast<uint32_t>(len);

  const uint8_t* p = data;
  size_t remaining = len;

  while (remaining >= 4) {
    uint32_t k = static_cast<uint32_t>(p[0]) |
                 (static_cast<uint32_t>(p[1]) << 8) |
                 (static_cast<uint32_t>(p[2]) << 16) |
                 (static_cast<uint32_t>(p[3]) << 24);
    k *= m;
    k ^= k >> r;
    k *= m;
    h *= m;
    h ^= k;
    p += 4;
    remaining -= 4;
  }

  switch (remaining) {
    case 3: h ^= static_cast<uint32_t>(p[2]) << 16; [[fallthrough]];
    case 2: h ^= static_cast<uint32_t>(p[1]) << 8;  [[fallthrough]];
    case 1: h ^= static_cast<uint32_t>(p[0]);
            h *= m;
  }

  h ^= h >> 13;
  h *= m;
  h ^= h >> 15;
  return h;
}

uint32_t Lemmer::ChdLookup(const std::string& word) const {
  if (!hash_parsed_ || displacement_ == nullptr || values_ == nullptr) {
    return UINT32_MAX;
  }
  if (chd_header_.bucket_count == 0 || chd_header_.num_keys == 0) {
    return UINT32_MAX;
  }

  const auto* key = reinterpret_cast<const uint8_t*>(word.data());
  const size_t key_len = word.size();

  // Compute primary hash -> bucket index.
  uint32_t h1 = MurmurHash2(key, key_len, chd_header_.seed0);
  uint32_t bucket = h1 % chd_header_.bucket_count;

  // Read displacement value.
  // The displacement entry size is estimated from the table.
  // Try u16 entries first (most common for CHD with moderate bucket counts),
  // then u32 if u16 doesn't fit.
  uint32_t displacement_value = 0;
  size_t entry_size = displacement_size_ / chd_header_.bucket_count;
  if (entry_size == 0) entry_size = 1;

  if (entry_size >= 4) {
    // u32 displacement entries.
    size_t disp_offset = bucket * 4;
    if (disp_offset + 4 <= displacement_size_) {
      displacement_value = ReadU32LE(displacement_ + disp_offset);
    } else {
      return UINT32_MAX;
    }
  } else if (entry_size >= 2) {
    // u16 displacement entries.
    size_t disp_offset = bucket * 2;
    if (disp_offset + 2 <= displacement_size_) {
      displacement_value = ReadU16LE(displacement_ + disp_offset);
    } else {
      return UINT32_MAX;
    }
  } else {
    // u8 displacement entries.
    if (bucket < displacement_size_) {
      displacement_value = displacement_[bucket];
    } else {
      return UINT32_MAX;
    }
  }

  // Compute secondary hash -> final slot.
  uint32_t h2 = Fnv1aHash(key, key_len, chd_header_.seed1);
  uint32_t slot = (h2 + displacement_value) % chd_header_.num_keys;

  // The slot indexes into the values section.
  // Value entries contain a hash fingerprint for verification + rule index.
  // Exact entry format is unknown; return the slot as a candidate rule index.
  // Without being able to verify the key, we assume the CHD is minimal and
  // the hash is perfect, so any valid input word maps to a unique slot.
  //
  // Try to read a rule index from the values section.
  // Observed: last 64 bytes show 16-byte value entries: [8B hash] [4B value] [4B pad]
  // But this may not be uniform. Try several entry sizes.

  // Estimate value entry size.
  double approx_entry_size = static_cast<double>(values_size_) /
                             static_cast<double>(chd_header_.num_keys);

  if (approx_entry_size >= 15.5 && approx_entry_size <= 16.5) {
    // 16-byte entries: [8B hash_fingerprint] [4B rule_index] [4B padding]
    size_t val_offset = slot * 16;
    if (val_offset + 16 <= values_size_) {
      uint32_t rule_index = ReadU32LE(values_ + val_offset + 8);
      return rule_index;
    }
  } else if (approx_entry_size >= 11.5 && approx_entry_size <= 12.5) {
    // 12-byte entries: [8B hash_fingerprint] [4B rule_index]
    size_t val_offset = slot * 12;
    if (val_offset + 12 <= values_size_) {
      uint32_t rule_index = ReadU32LE(values_ + val_offset + 8);
      return rule_index;
    }
  } else if (approx_entry_size >= 7.5 && approx_entry_size <= 8.5) {
    // 8-byte entries: [4B hash_fingerprint] [4B rule_index]
    size_t val_offset = slot * 8;
    if (val_offset + 8 <= values_size_) {
      uint32_t rule_index = ReadU32LE(values_ + val_offset + 4);
      return rule_index;
    }
  } else if (approx_entry_size >= 3.5 && approx_entry_size <= 4.5) {
    // 4-byte entries: [4B rule_index] (no fingerprint)
    size_t val_offset = slot * 4;
    if (val_offset + 4 <= values_size_) {
      return ReadU32LE(values_ + val_offset);
    }
  }

  // Unknown entry size; cannot decode.
  return UINT32_MAX;
}

std::string Lemmer::ApplyRule(const std::string& word, uint32_t rule_index) const {
  if (!rules_parsed_ || rules_body_ == nullptr) {
    return word;
  }

  if (rule_index >= rule_offsets_.size()) {
    return word;
  }

  uint32_t rule_offset = rule_offsets_[rule_index];
  if (rule_offset >= rules_body_size_) {
    return word;
  }

  // Read rule instruction at the given offset.
  // Morphological rules are compact symbolic programs.
  // Common pattern: strip N bytes from suffix, then append M bytes.
  //
  // Observed instruction format (hypothesis based on reverse analysis):
  //   byte 0: strip_count (number of bytes to remove from end of word)
  //   byte 1: append_length (number of bytes to append)
  //   bytes 2..2+append_length: replacement suffix bytes
  //
  // This is a simplified interpretation. The actual format likely has more
  // opcodes, but this handles the most common suffix-replacement rules.

  const uint8_t* rule = rules_body_ + rule_offset;
  size_t rule_remaining = rules_body_size_ - rule_offset;

  if (rule_remaining < 2) {
    return word;
  }

  uint8_t strip_count = rule[0];
  uint8_t append_length = rule[1];

  // Sanity checks.
  if (strip_count > word.size()) {
    return word;
  }
  if (static_cast<size_t>(append_length) + 2 > rule_remaining) {
    return word;
  }
  if (append_length > 64) {
    return word;  // Unreasonable suffix length.
  }

  // Apply: remove strip_count bytes from the end, then snap the cut back to a
  // UTF-8 character boundary so a byte count that lands mid-character cannot
  // produce an invalid-UTF-8 lemma.
  size_t cut = word.size() - strip_count;
  while (cut > 0 && (static_cast<unsigned char>(word[cut]) & 0xC0) == 0x80) {
    --cut;
  }
  std::string lemma = word.substr(0, cut);
  lemma.append(reinterpret_cast<const char*>(rule + 2), append_length);

  return lemma;
}

Status Lemmer::LoadHash(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return Status::InvalidArgument("Lemmer::LoadHash: null or empty data");
  }

  hash_data_.assign(data, data + size);
  hash_loaded_ = true;
  hash_parsed_ = false;

  // Attempt to parse the CHD header.
  Status parse_status = ParseChdHeader(data, size);
  if (parse_status.ok()) {
    hash_parsed_ = true;
    // Update pointers to point into our stored copy.
    displacement_ = hash_data_.data() + kChdHeaderSize;
    values_ = hash_data_.data() + kChdHeaderSize + displacement_size_;
  } else {
    notes_.push_back("CHD header parse failed: " + parse_status.message);
    notes_.push_back("hash blob stored (" + std::to_string(size) +
                     " bytes) but lookups will not work");
  }

  return Status::Ok();
}

Status Lemmer::LoadRules(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return Status::InvalidArgument("Lemmer::LoadRules: null or empty data");
  }

  rules_data_.assign(data, data + size);
  rules_loaded_ = true;
  rules_parsed_ = false;

  // Attempt to parse the rules header and offset table.
  Status parse_status = ParseRulesHeader(data, size);
  if (parse_status.ok()) {
    rules_parsed_ = true;
    // Update body pointer to our stored copy.
    if (rules_body_ != nullptr) {
      size_t body_offset = static_cast<size_t>(rules_body_ - data);
      rules_body_ = rules_data_.data() + body_offset;
    }
  } else {
    notes_.push_back("rules header parse failed: " + parse_status.message);
    notes_.push_back("rules blob stored (" + std::to_string(size) +
                     " bytes) but rule application will not work");
  }

  return Status::Ok();
}

std::string Lemmer::DiagnosticLookup(const std::string& word) const {
  std::string out;
  out += "=== DiagnosticLookup(\"" + word + "\") ===\n";

  if (!hash_parsed_ || displacement_ == nullptr || values_ == nullptr) {
    out += "ABORT: hash not parsed (hash_parsed_=" +
           std::string(hash_parsed_ ? "true" : "false") + ")\n";
    return out;
  }
  if (chd_header_.bucket_count == 0 || chd_header_.num_keys == 0) {
    out += "ABORT: bucket_count=" + std::to_string(chd_header_.bucket_count) +
           " num_keys=" + std::to_string(chd_header_.num_keys) + "\n";
    return out;
  }

  const auto* key = reinterpret_cast<const uint8_t*>(word.data());
  const size_t key_len = word.size();

  // Step 1: primary hash -> bucket.
  uint32_t h1 = MurmurHash2(key, key_len, chd_header_.seed0);
  uint32_t bucket = h1 % chd_header_.bucket_count;

  char hex[32];
  std::snprintf(hex, sizeof(hex), "0x%08x", h1);
  out += "h1 = MurmurHash2(word, seed0=0x" +
         ([&]{ char b[16]; std::snprintf(b, sizeof(b), "%08x", chd_header_.seed0); return std::string(b); })() +
         ") = " + std::string(hex) + "\n";
  out += "bucket = h1 % " + std::to_string(chd_header_.bucket_count) +
         " = " + std::to_string(bucket) + "\n";

  // Step 2: displacement.
  size_t entry_size = displacement_size_ / chd_header_.bucket_count;
  if (entry_size == 0) entry_size = 1;
  out += "displacement entry_size = " + std::to_string(entry_size) +
         " (section=" + std::to_string(displacement_size_) + " / buckets=" +
         std::to_string(chd_header_.bucket_count) + ")\n";

  uint32_t displacement_value = 0;
  bool disp_ok = false;
  if (entry_size >= 4) {
    size_t disp_offset = bucket * 4;
    if (disp_offset + 4 <= displacement_size_) {
      displacement_value = ReadU32LE(displacement_ + disp_offset);
      disp_ok = true;
    }
  } else if (entry_size >= 2) {
    size_t disp_offset = bucket * 2;
    if (disp_offset + 2 <= displacement_size_) {
      displacement_value = ReadU16LE(displacement_ + disp_offset);
      disp_ok = true;
    }
  } else {
    if (bucket < displacement_size_) {
      displacement_value = displacement_[bucket];
      disp_ok = true;
    }
  }

  if (!disp_ok) {
    out += "ABORT: displacement read out of bounds\n";
    return out;
  }
  out += "displacement[" + std::to_string(bucket) + "] = " +
         std::to_string(displacement_value) + "\n";

  // Step 3: secondary hash -> slot.
  uint32_t h2 = Fnv1aHash(key, key_len, chd_header_.seed1);
  std::snprintf(hex, sizeof(hex), "0x%08x", h2);
  out += "h2 = Fnv1aHash(word, seed1=0x" +
         ([&]{ char b[16]; std::snprintf(b, sizeof(b), "%08x", chd_header_.seed1); return std::string(b); })() +
         ") = " + std::string(hex) + "\n";

  uint32_t slot = (h2 + displacement_value) % chd_header_.num_keys;
  out += "slot = (h2 + disp) % " + std::to_string(chd_header_.num_keys) +
         " = " + std::to_string(slot) + "\n";

  // Step 4: read value entry.
  double approx_entry_size = static_cast<double>(values_size_) /
                             static_cast<double>(chd_header_.num_keys);
  out += "values: " + std::to_string(values_size_) + " bytes, approx_entry_size=" +
         std::to_string(approx_entry_size) + "\n";

  uint32_t rule_index = UINT32_MAX;
  size_t actual_entry_size = 0;
  if (approx_entry_size >= 15.5 && approx_entry_size <= 16.5) {
    actual_entry_size = 16;
    size_t val_offset = slot * 16;
    if (val_offset + 16 <= values_size_) {
      // Dump the 16 raw bytes for diagnosis.
      out += "value_entry[" + std::to_string(slot) + "] @offset " +
             std::to_string(val_offset) + ": ";
      for (size_t i = 0; i < 16; ++i) {
        std::snprintf(hex, sizeof(hex), "%02x", values_[val_offset + i]);
        out += std::string(hex) + " ";
      }
      out += "\n";
      rule_index = ReadU32LE(values_ + val_offset + 8);
    }
  } else if (approx_entry_size >= 11.5 && approx_entry_size <= 12.5) {
    actual_entry_size = 12;
    size_t val_offset = slot * 12;
    if (val_offset + 12 <= values_size_) {
      rule_index = ReadU32LE(values_ + val_offset + 8);
    }
  } else if (approx_entry_size >= 7.5 && approx_entry_size <= 8.5) {
    actual_entry_size = 8;
    size_t val_offset = slot * 8;
    if (val_offset + 8 <= values_size_) {
      rule_index = ReadU32LE(values_ + val_offset + 4);
    }
  } else if (approx_entry_size >= 3.5 && approx_entry_size <= 4.5) {
    actual_entry_size = 4;
    size_t val_offset = slot * 4;
    if (val_offset + 4 <= values_size_) {
      rule_index = ReadU32LE(values_ + val_offset);
    }
  }

  out += "actual_entry_size=" + std::to_string(actual_entry_size) + "\n";

  if (rule_index == UINT32_MAX) {
    out += "RESULT: rule_index=UINT32_MAX (lookup failed)\n";
    return out;
  }
  out += "rule_index=" + std::to_string(rule_index) + "\n";

  if (rule_index >= rules_header_.num_rules) {
    out += "RESULT: rule_index out of range (>= num_rules=" +
           std::to_string(rules_header_.num_rules) + ")\n";
    return out;
  }

  // Step 5: apply rule.
  if (!rules_parsed_ || rules_body_ == nullptr) {
    out += "RESULT: rules not parsed, cannot apply\n";
    return out;
  }

  if (rule_index < rule_offsets_.size()) {
    uint32_t rule_offset = rule_offsets_[rule_index];
    out += "rule_offset=" + std::to_string(rule_offset) + "\n";
    if (rule_offset < rules_body_size_) {
      const uint8_t* rule = rules_body_ + rule_offset;
      size_t rule_remaining = rules_body_size_ - rule_offset;
      if (rule_remaining >= 2) {
        out += "rule bytes: strip=" + std::to_string(rule[0]) +
               " append_len=" + std::to_string(rule[1]);
        if (rule[1] > 0 && static_cast<size_t>(rule[1]) + 2 <= rule_remaining) {
          out += " suffix=[";
          for (uint8_t j = 0; j < rule[1]; ++j) {
            std::snprintf(hex, sizeof(hex), "%02x", rule[2 + j]);
            out += std::string(hex);
            if (j + 1 < rule[1]) out += " ";
          }
          out += "]";
        }
        out += "\n";
      }
    }
  }

  std::string lemma = ApplyRule(word, rule_index);
  out += "RESULT: lemma=\"" + lemma + "\"\n";

  return out;
}

std::string Lemmer::Lemmatize(const std::string& word) const {
  if (word.empty()) return word;

  // Attempt CHD lookup -> rule application pipeline.
  if (hash_parsed_ && rules_parsed_) {
    uint32_t rule_index = ChdLookup(word);
    if (rule_index != UINT32_MAX && rule_index < rules_header_.num_rules) {
      std::string lemma = ApplyRule(word, rule_index);
      if (!lemma.empty() && lemma != word) {
        return lemma;
      }
    }
  }

  // Fallback: return word as-is.
  return word;
}

bool Lemmer::loaded() const {
  return hash_loaded_ && rules_loaded_;
}

bool Lemmer::hash_loaded() const {
  return hash_loaded_;
}

bool Lemmer::rules_loaded() const {
  return rules_loaded_;
}

size_t Lemmer::hash_size() const {
  return hash_data_.size();
}

size_t Lemmer::rules_size() const {
  return rules_data_.size();
}

const std::vector<std::string>& Lemmer::load_notes() const {
  return notes_;
}

uint32_t Lemmer::bucket_count() const {
  return chd_header_.bucket_count;
}

uint32_t Lemmer::num_keys() const {
  return chd_header_.num_keys;
}

uint32_t Lemmer::num_hashes() const {
  return chd_header_.num_hashes;
}

uint32_t Lemmer::num_rules() const {
  return rules_header_.num_rules;
}

}  // namespace hoskey::engine
