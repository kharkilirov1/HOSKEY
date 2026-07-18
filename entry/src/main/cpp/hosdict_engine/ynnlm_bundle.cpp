#include "ynnlm_bundle.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>

namespace hoskey::engine {
namespace {

uint16_t ReadU16LE(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) |
         static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8U);
}

uint32_t ReadU32LE(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8U) |
         (static_cast<uint32_t>(p[2]) << 16U) |
         (static_cast<uint32_t>(p[3]) << 24U);
}

uint64_t ReadU64LE(const uint8_t* p) {
  return static_cast<uint64_t>(ReadU32LE(p)) |
         (static_cast<uint64_t>(ReadU32LE(p + 4)) << 32U);
}

bool SpanOk(size_t off, size_t bytes, size_t size) {
  return off <= size && bytes <= size - off;
}

bool AddBytes(size_t base, size_t bytes, size_t size, size_t* out) {
  if (!out || !SpanOk(base, bytes, size)) return false;
  *out = base + bytes;
  return true;
}

bool IsJsonSpace(uint8_t c) {
  return c == 0 || c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

Status FindBalancedJsonEnd(const uint8_t* data, size_t size, size_t begin,
                           size_t* end) {
  if (!data || !end || begin >= size || data[begin] != '{') {
    return Status::ParseError("YnnlmV1Bundle: JSON object not found");
  }

  int depth = 0;
  bool in_string = false;
  bool escaped = false;
  for (size_t i = begin; i < size; ++i) {
    const char c = static_cast<char>(data[i]);
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }

    if (c == '"') {
      in_string = true;
    } else if (c == '{') {
      ++depth;
    } else if (c == '}') {
      --depth;
      if (depth == 0) {
        *end = i + 1U;
        return Status::Ok();
      }
      if (depth < 0) {
        return Status::ParseError("YnnlmV1Bundle: malformed JSON braces");
      }
    }
  }
  return Status::ParseError("YnnlmV1Bundle: unterminated JSON config");
}

Status FindBalancedJsonArrayEnd(const uint8_t* data, size_t size, size_t begin,
                                size_t* end) {
  if (!data || !end || begin >= size || data[begin] != '[') {
    return Status::ParseError("YnnlmV1Bundle: JSON array not found");
  }
  int depth = 0;
  bool in_string = false;
  bool escaped = false;
  for (size_t i = begin; i < size; ++i) {
    const char c = static_cast<char>(data[i]);
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }
    if (c == '"') {
      in_string = true;
    } else if (c == '[') {
      ++depth;
    } else if (c == ']') {
      --depth;
      if (depth == 0) {
        *end = i + 1U;
        return Status::Ok();
      }
      if (depth < 0) {
        return Status::ParseError("YnnlmV1Bundle: malformed JSON brackets");
      }
    }
  }
  return Status::ParseError("YnnlmV1Bundle: unterminated application map");
}

bool ParseJsonString(std::string_view json, size_t* pos, std::string* out) {
  if (!pos || !out || *pos >= json.size() || json[*pos] != '"') return false;
  out->clear();
  ++*pos;
  while (*pos < json.size()) {
    const char c = json[(*pos)++];
    if (c == '"') return true;
    if (c != '\\') {
      out->push_back(c);
      continue;
    }
    if (*pos >= json.size()) return false;
    const char escaped = json[(*pos)++];
    switch (escaped) {
      case '"':
      case '\\':
      case '/':
        out->push_back(escaped);
        break;
      case 'b': out->push_back('\b'); break;
      case 'f': out->push_back('\f'); break;
      case 'n': out->push_back('\n'); break;
      case 'r': out->push_back('\r'); break;
      case 't': out->push_back('\t'); break;
      default:
        return false;
    }
  }
  return false;
}

Status ParseApplicationMap(
    std::string_view json,
    std::unordered_map<std::string, uint32_t>* application_ids) {
  if (!application_ids || json.size() < 2 || json.front() != '[' ||
      json.back() != ']') {
    return Status::ParseError("YnnlmV1Bundle: invalid application map");
  }
  size_t pos = 1;
  const auto skip = [&]() {
    while (pos < json.size() &&
           (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' ||
            json[pos] == '\r' || json[pos] == ',' || json[pos] == '{' ||
            json[pos] == '}' || json[pos] == ':' || json[pos] == '[')) {
      ++pos;
    }
  };
  while (pos + 1 < json.size()) {
    skip();
    if (pos >= json.size() - 1 || json[pos] == ']') break;
    std::string id_text;
    if (!ParseJsonString(json, &pos, &id_text) || id_text.empty()) {
      return Status::ParseError("YnnlmV1Bundle: malformed application id");
    }
    uint32_t id = 0;
    for (char c : id_text) {
      if (c < '0' || c > '9' ||
          id > (std::numeric_limits<uint32_t>::max() - (c - '0')) / 10U) {
        return Status::ParseError("YnnlmV1Bundle: invalid application id");
      }
      id = id * 10U + static_cast<uint32_t>(c - '0');
    }
    skip();
    bool found_package = false;
    while (pos < json.size() && json[pos] != ']') {
      if (json[pos] == ',' || json[pos] == ' ' || json[pos] == '\t' ||
          json[pos] == '\n' || json[pos] == '\r') {
        ++pos;
        continue;
      }
      std::string package_name;
      if (!ParseJsonString(json, &pos, &package_name) || package_name.empty()) {
        return Status::ParseError("YnnlmV1Bundle: malformed package name");
      }
      application_ids->emplace(std::move(package_name), id);
      found_package = true;
    }
    if (pos >= json.size() || json[pos] != ']' || !found_package) {
      return Status::ParseError("YnnlmV1Bundle: malformed package list");
    }
    ++pos;
  }
  return Status::Ok();
}

bool ExtractJsonInt(std::string_view json, std::string_view key, int* out) {
  if (!out) return false;
  const std::string needle = "\"" + std::string(key) + "\"";
  size_t pos = json.find(needle);
  if (pos == std::string_view::npos) return false;
  pos += needle.size();
  while (pos < json.size() &&
         (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' ||
          json[pos] == '\r' || json[pos] == ':')) {
    ++pos;
  }
  if (pos >= json.size() || json[pos] < '0' || json[pos] > '9') return false;
  int value = 0;
  while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
    const int digit = json[pos] - '0';
    if (value > (std::numeric_limits<int>::max() - digit) / 10) return false;
    value = value * 10 + digit;
    ++pos;
  }
  *out = value;
  return true;
}

bool IsUtf8Continuation(uint8_t byte) {
  return (byte & 0xC0U) == 0x80U;
}

bool IsValidUtf8(std::string_view text) {
  size_t i = 0;
  while (i < text.size()) {
    const uint8_t b0 = static_cast<uint8_t>(text[i]);
    if (b0 <= 0x7FU) {
      ++i;
    } else if (b0 >= 0xC2U && b0 <= 0xDFU) {
      if (i + 1U >= text.size() ||
          !IsUtf8Continuation(static_cast<uint8_t>(text[i + 1U]))) {
        return false;
      }
      i += 2U;
    } else if (b0 >= 0xE0U && b0 <= 0xEFU) {
      if (i + 2U >= text.size()) return false;
      const uint8_t b1 = static_cast<uint8_t>(text[i + 1U]);
      const uint8_t b2 = static_cast<uint8_t>(text[i + 2U]);
      if (!IsUtf8Continuation(b1) || !IsUtf8Continuation(b2)) return false;
      if (b0 == 0xE0U && b1 < 0xA0U) return false;
      if (b0 == 0xEDU && b1 >= 0xA0U) return false;
      i += 3U;
    } else if (b0 >= 0xF0U && b0 <= 0xF4U) {
      if (i + 3U >= text.size()) return false;
      const uint8_t b1 = static_cast<uint8_t>(text[i + 1U]);
      const uint8_t b2 = static_cast<uint8_t>(text[i + 2U]);
      const uint8_t b3 = static_cast<uint8_t>(text[i + 3U]);
      if (!IsUtf8Continuation(b1) || !IsUtf8Continuation(b2) ||
          !IsUtf8Continuation(b3)) {
        return false;
      }
      if (b0 == 0xF0U && b1 < 0x90U) return false;
      if (b0 == 0xF4U && b1 > 0x8FU) return false;
      i += 4U;
    } else {
      return false;
    }
  }
  return true;
}

struct VocabTable {
  std::vector<std::string> strings;
  std::vector<uint32_t> rank_to_lex;
  std::vector<uint32_t> lex_to_rank;
  size_t end = 0;
};

Status VectorTarget(const uint8_t* data, size_t size, size_t loc,
                    size_t* target) {
  if (!target || !SpanOk(loc, 4, size)) {
    return Status::ParseError("YnnlmV1Bundle: vector offset out of range");
  }
  const uint32_t rel = ReadU32LE(data + loc);
  if (!AddBytes(loc, static_cast<size_t>(rel), size, target) ||
      !SpanOk(*target, 4, size)) {
    return Status::ParseError("YnnlmV1Bundle: vector target out of range");
  }
  return Status::Ok();
}

Status ParseU32Vector(const uint8_t* data, size_t size, size_t vec,
                      std::vector<uint32_t>* out) {
  if (!out || !SpanOk(vec, 4, size)) {
    return Status::ParseError("YnnlmV1Bundle: uint32 vector header out of range");
  }
  const uint32_t count = ReadU32LE(data + vec);
  if (count > (size - vec - 4U) / 4U) {
    return Status::ParseError("YnnlmV1Bundle: uint32 vector payload out of range");
  }
  if (count > 2000000U) {
    return Status::ParseError("YnnlmV1Bundle: uint32 vector is unreasonably large");
  }

  out->clear();
  out->reserve(count);
  size_t pos = vec + 4U;
  for (uint32_t i = 0; i < count; ++i, pos += 4U) {
    out->push_back(ReadU32LE(data + pos));
  }
  return Status::Ok();
}

Status ParseStringVector(const uint8_t* data, size_t size, size_t vec,
                         std::vector<std::string>* out, size_t* max_end) {
  if (!out || !max_end || !SpanOk(vec, 4, size)) {
    return Status::ParseError("YnnlmV1Bundle: string vector header out of range");
  }
  const uint32_t count = ReadU32LE(data + vec);
  if (count > (size - vec - 4U) / 4U) {
    return Status::ParseError("YnnlmV1Bundle: string vector offsets out of range");
  }
  if (count > 2000000U) {
    return Status::ParseError("YnnlmV1Bundle: string vector is unreasonably large");
  }

  out->clear();
  out->reserve(count);
  *max_end = vec + 4U + static_cast<size_t>(count) * 4U;

  for (uint32_t i = 0; i < count; ++i) {
    const size_t loc = vec + 4U + static_cast<size_t>(i) * 4U;
    size_t str = 0;
    Status st = VectorTarget(data, size, loc, &str);
    if (!st.ok()) return st;

    const uint32_t len = ReadU32LE(data + str);
    size_t text_begin = str + 4U;
    if (!SpanOk(text_begin, static_cast<size_t>(len), size)) {
      return Status::ParseError("YnnlmV1Bundle: string bytes out of range");
    }
    size_t text_end = 0;
    if (!AddBytes(text_begin, static_cast<size_t>(len), size, &text_end) ||
        !SpanOk(text_end, 1, size) || data[text_end] != 0) {
      return Status::ParseError("YnnlmV1Bundle: string terminator out of range");
    }

    std::string_view text(reinterpret_cast<const char*>(data + text_begin), len);
    if (!IsValidUtf8(text)) {
      return Status::ParseError("YnnlmV1Bundle: invalid UTF-8 string");
    }
    out->push_back(std::string(text));

    size_t padded_end = text_end + 1U;
    const size_t pad = (4U - (padded_end % 4U)) % 4U;
    if (!AddBytes(padded_end, pad, size, &padded_end)) {
      return Status::ParseError("YnnlmV1Bundle: string padding out of range");
    }
    *max_end = std::max(*max_end, padded_end);
  }
  return Status::Ok();
}

Status ValidatePermutation(const std::vector<uint32_t>& rank_to_lex,
                           const std::vector<uint32_t>& lex_to_rank,
                           size_t count, std::string_view label) {
  if (rank_to_lex.size() != count || lex_to_rank.size() != count) {
    return Status::ParseError("YnnlmV1Bundle: " + std::string(label) +
                              " map size mismatch");
  }
  for (size_t rank = 0; rank < count; ++rank) {
    const uint32_t lex = rank_to_lex[rank];
    if (lex >= count) {
      return Status::ParseError("YnnlmV1Bundle: " + std::string(label) +
                                " rank_to_lex value out of range");
    }
    if (lex_to_rank[lex] != rank) {
      return Status::ParseError("YnnlmV1Bundle: " + std::string(label) +
                                " permutation maps are not inverse");
    }
  }
  return Status::Ok();
}

Status ParseVocabTableAt(const uint8_t* data, size_t size, size_t base,
                         VocabTable* out) {
  if (!out || !SpanOk(base, 4, size)) {
    return Status::ParseError("YnnlmV1Bundle: vocab table root out of range");
  }

  const uint32_t root_rel = ReadU32LE(data + base);
  size_t root = 0;
  if (!AddBytes(base, static_cast<size_t>(root_rel), size, &root) ||
      !SpanOk(root, 4, size)) {
    return Status::ParseError("YnnlmV1Bundle: vocab table object out of range");
  }

  const uint32_t vtable_rel = ReadU32LE(data + root);
  if (vtable_rel > root) {
    return Status::ParseError("YnnlmV1Bundle: vocab vtable offset out of range");
  }
  const size_t vtable = root - static_cast<size_t>(vtable_rel);
  if (!SpanOk(vtable, 4, size)) {
    return Status::ParseError("YnnlmV1Bundle: vocab vtable header out of range");
  }

  const uint16_t vtable_size = ReadU16LE(data + vtable);
  const uint16_t object_size = ReadU16LE(data + vtable + 2U);
  if (vtable_size < 12U || object_size < 16U ||
      !SpanOk(vtable, vtable_size, size) || !SpanOk(root, object_size, size)) {
    return Status::ParseError("YnnlmV1Bundle: malformed vocab table");
  }

  auto field_loc = [&](size_t field, size_t* loc) -> Status {
    const size_t field_entry = vtable + 4U + field * 2U;
    if (!SpanOk(field_entry, 2, size) || field_entry + 2U > vtable + vtable_size) {
      return Status::ParseError("YnnlmV1Bundle: missing vocab table field");
    }
    const uint16_t off = ReadU16LE(data + field_entry);
    if (off == 0 || off >= object_size || !SpanOk(root + off, 4, size)) {
      return Status::ParseError("YnnlmV1Bundle: vocab table field out of range");
    }
    *loc = root + off;
    return Status::Ok();
  };

  size_t rank_to_lex_loc = 0;
  size_t lex_to_rank_loc = 0;
  size_t strings_loc = 0;
  Status st = field_loc(3, &rank_to_lex_loc);
  if (!st.ok()) return st;
  st = field_loc(2, &lex_to_rank_loc);
  if (!st.ok()) return st;
  st = field_loc(1, &strings_loc);
  if (!st.ok()) return st;

  size_t rank_to_lex_vec = 0;
  size_t lex_to_rank_vec = 0;
  size_t strings_vec = 0;
  st = VectorTarget(data, size, rank_to_lex_loc, &rank_to_lex_vec);
  if (!st.ok()) return st;
  st = VectorTarget(data, size, lex_to_rank_loc, &lex_to_rank_vec);
  if (!st.ok()) return st;
  st = VectorTarget(data, size, strings_loc, &strings_vec);
  if (!st.ok()) return st;

  st = ParseU32Vector(data, size, rank_to_lex_vec, &out->rank_to_lex);
  if (!st.ok()) return st;
  st = ParseU32Vector(data, size, lex_to_rank_vec, &out->lex_to_rank);
  if (!st.ok()) return st;

  size_t strings_end = 0;
  st = ParseStringVector(data, size, strings_vec, &out->strings, &strings_end);
  if (!st.ok()) return st;

  st = ValidatePermutation(out->rank_to_lex, out->lex_to_rank,
                           out->strings.size(), "vocab");
  if (!st.ok()) return st;

  out->end = strings_end;
  return Status::Ok();
}

Status FindAndParseVocabTable(const uint8_t* data, size_t size, size_t begin,
                              VocabTable* out, size_t* base_out) {
  if (!out || !base_out) {
    return Status::InvalidArgument("YnnlmV1Bundle: null vocab output");
  }
  const size_t limit = std::min(size, begin + 64U);
  for (size_t base = begin; base < limit; ++base) {
    if (!IsJsonSpace(data[base])) {
      VocabTable candidate;
      Status st = ParseVocabTableAt(data, size, base, &candidate);
      if (st.ok()) {
        *out = std::move(candidate);
        *base_out = base;
        return Status::Ok();
      }
    }
  }
  return Status::ParseError("YnnlmV1Bundle: vocab table not found");
}

}  // namespace

void YnnlmV1Bundle::Reset() {
  order_ = 0;
  max_word_len_ = 0;
  words_.clear();
  subwords_.clear();
  word_rank_to_lex_.clear();
  word_lex_to_rank_.clear();
  subword_rank_to_lex_.clear();
  subword_lex_to_rank_.clear();
  word_rank_by_text_.clear();
  application_id_by_package_.clear();
}

Status YnnlmV1Bundle::Parse(const uint8_t* data, size_t size) {
  Reset();
  if (!data || size < 32U) {
    return Status::InvalidArgument("YnnlmV1Bundle: null or too small input");
  }

  size_t json_begin = 0;
  uint64_t header_order = 0;
  if (size >= 17U && std::memcmp(data, "ynnlm_v1\n", 9U) == 0) {
    const uint64_t json_len = ReadU64LE(data + 9U);
    if (json_len <= size - 17U && data[17U] == '{') {
      json_begin = 17U;
    }
  }
  if (json_begin == 0) {
    if (std::memcmp(data, "ynnlm_v1", 8U) != 0 || !SpanOk(8U, 8U, size)) {
      return Status::ParseError("YnnlmV1Bundle: magic mismatch");
    }
    header_order = ReadU64LE(data + 8U);
    json_begin = 16U;
  }
  if (json_begin >= size || data[json_begin] != '{') {
    return Status::ParseError("YnnlmV1Bundle: JSON config missing");
  }

  size_t json_end = 0;
  Status st = FindBalancedJsonEnd(data, size, json_begin, &json_end);
  if (!st.ok()) return st;
  std::string_view json(reinterpret_cast<const char*>(data + json_begin),
                        json_end - json_begin);
  if (!ExtractJsonInt(json, "Order", &order_) ||
      !ExtractJsonInt(json, "MaxWordLen", &max_word_len_)) {
    return Status::ParseError("YnnlmV1Bundle: required JSON fields missing");
  }
  if (order_ <= 0 || max_word_len_ <= 0) {
    return Status::ParseError("YnnlmV1Bundle: invalid JSON config values");
  }
  if (header_order != 0 && header_order != static_cast<uint64_t>(order_)) {
    return Status::ParseError("YnnlmV1Bundle: header order mismatch");
  }

  VocabTable word_table;
  size_t word_base = 0;
  st = FindAndParseVocabTable(data, size, json_end, &word_table, &word_base);
  if (!st.ok()) return st;

  VocabTable subword_table;
  size_t subword_base = 0;
  st = FindAndParseVocabTable(data, size, word_table.end, &subword_table,
                              &subword_base);
  if (!st.ok()) return st;
  if (subword_base <= word_base) {
    return Status::ParseError("YnnlmV1Bundle: subword table order mismatch");
  }

  words_ = std::move(word_table.strings);
  word_rank_to_lex_ = std::move(word_table.rank_to_lex);
  word_lex_to_rank_ = std::move(word_table.lex_to_rank);
  subwords_ = std::move(subword_table.strings);
  subword_rank_to_lex_ = std::move(subword_table.rank_to_lex);
  subword_lex_to_rank_ = std::move(subword_table.lex_to_rank);

  size_t app_begin = subword_table.end;
  const size_t app_search_end = std::min(size, app_begin + 64U);
  while (app_begin < app_search_end && IsJsonSpace(data[app_begin])) ++app_begin;
  if (app_begin < app_search_end && data[app_begin] == '[') {
    size_t app_end = 0;
    st = FindBalancedJsonArrayEnd(data, size, app_begin, &app_end);
    if (!st.ok()) return st;
    st = ParseApplicationMap(
        std::string_view(reinterpret_cast<const char*>(data + app_begin),
                         app_end - app_begin),
        &application_id_by_package_);
    if (!st.ok()) return st;
  }

  word_rank_by_text_.reserve(words_.size());
  for (size_t lex = 0; lex < words_.size(); ++lex) {
    const uint32_t rank = word_lex_to_rank_[lex];
    word_rank_by_text_.emplace(words_[lex], rank);
  }
  return Status::Ok();
}

int YnnlmV1Bundle::WordRank(const std::string& word) const {
  const auto it = word_rank_by_text_.find(word);
  if (it == word_rank_by_text_.end()) return -1;
  if (it->second > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
    return -1;
  }
  return static_cast<int>(it->second);
}

uint32_t YnnlmV1Bundle::ApplicationId(const std::string& package_name) const {
  const auto it = application_id_by_package_.find(package_name);
  return it == application_id_by_package_.end() ? 0U : it->second;
}

}  // namespace hoskey::engine
