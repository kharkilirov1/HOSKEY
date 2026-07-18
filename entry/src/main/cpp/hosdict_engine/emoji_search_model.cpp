#include "emoji_search_model.h"

#include <algorithm>
#include <limits>
#include <sstream>

#include "text_utils.h"

namespace hoskey::engine {
namespace {

bool HasRange(size_t total, size_t offset, size_t length) {
  return offset <= total && length <= total - offset;
}

uint16_t ReadU16Le(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) |
         (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t ReadU32Le(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

int32_t ReadI32Le(const uint8_t* data) {
  return static_cast<int32_t>(ReadU32Le(data));
}

class FlatBufferReader {
 public:
  FlatBufferReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

  bool Root(size_t* table) const {
    if (!table || !HasRange(size_, 0, 4)) return false;
    *table = ReadU32Le(data_);
    return HasRange(size_, *table, 4);
  }

  bool TableField(size_t table, size_t index, size_t* field) const {
    if (!field || !HasRange(size_, table, 4)) return false;
    const int32_t distance = ReadI32Le(data_ + table);
    const int64_t signed_vtable =
        static_cast<int64_t>(table) - static_cast<int64_t>(distance);
    if (signed_vtable < 0 ||
        static_cast<uint64_t>(signed_vtable) > size_) {
      return false;
    }
    const size_t vtable = static_cast<size_t>(signed_vtable);
    if (!HasRange(size_, vtable, 4)) return false;
    const uint16_t vtable_size = ReadU16Le(data_ + vtable);
    const uint16_t object_size = ReadU16Le(data_ + vtable + 2);
    if (vtable_size < 4 || (vtable_size & 1) != 0 ||
        !HasRange(size_, vtable, vtable_size) ||
        !HasRange(size_, table, object_size)) {
      return false;
    }
    const size_t entry = vtable + 4 + index * 2;
    if (!HasRange(vtable + vtable_size, entry, 2)) return false;
    const uint16_t relative = ReadU16Le(data_ + entry);
    if (relative == 0 || relative >= object_size) return false;
    *field = table + relative;
    return HasRange(size_, *field, 4);
  }

  bool UOffset(size_t position, size_t* target) const {
    if (!target || !HasRange(size_, position, 4)) return false;
    const size_t relative = ReadU32Le(data_ + position);
    if (relative > size_ - position) return false;
    *target = position + relative;
    return HasRange(size_, *target, 4);
  }

  bool Vector(size_t field, size_t element_size, size_t* start,
              size_t* count) const {
    size_t vector = 0;
    if (!start || !count || !UOffset(field, &vector)) return false;
    *count = ReadU32Le(data_ + vector);
    *start = vector + 4;
    return element_size == 0 ||
           (*count <= (size_ - *start) / element_size &&
            HasRange(size_, *start, *count * element_size));
  }

  bool StringFromSlot(size_t slot, std::string* value) const {
    size_t string = 0;
    if (!value || !UOffset(slot, &string)) return false;
    const size_t length = ReadU32Le(data_ + string);
    const size_t start = string + 4;
    if (!HasRange(size_, start, length + 1) || data_[start + length] != 0) {
      return false;
    }
    value->assign(reinterpret_cast<const char*>(data_ + start), length);
    return true;
  }

  const uint8_t* data() const { return data_; }
  size_t size() const { return size_; }

 private:
  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
};

size_t PackedLeafSize(uint8_t first) {
  if (first < 0x80) return 1;
  size_t leading_ones = 0;
  for (uint8_t mask = 0x80; mask != 0 && (first & mask) != 0;
       mask >>= 1) {
    ++leading_ones;
  }
  return std::min<size_t>(leading_ones + 1, 9);
}

void AppendUtf16Be(uint16_t code_unit, std::string* result) {
  result->push_back(static_cast<char>(code_unit >> 8));
  result->push_back(static_cast<char>(code_unit & 0xff));
}

}  // namespace

Status EmojiSearchModel::Load(const uint8_t* data, size_t size) {
  Reset();
  if (!data) return Status::InvalidArgument("emoji search data is null");
  if (size < 20) return Status::ParseError("emoji search asset is truncated");

  const FlatBufferReader reader(data, size);
  size_t root = 0;
  size_t groups_field = 0;
  size_t trie_field = 0;
  if (!reader.Root(&root) || !reader.TableField(root, 0, &groups_field) ||
      !reader.TableField(root, 1, &trie_field)) {
    return Status::ParseError("emoji search FlatBuffer root is invalid");
  }

  size_t groups_start = 0;
  size_t group_count = 0;
  if (!reader.Vector(groups_field, 4, &groups_start, &group_count) ||
      group_count == 0) {
    return Status::ParseError("emoji search groups vector is invalid");
  }
  groups_.reserve(group_count);
  for (size_t group_index = 0; group_index < group_count; ++group_index) {
    const size_t slot = groups_start + group_index * 4;
    size_t group_table = 0;
    size_t strings_field = 0;
    size_t strings_start = 0;
    size_t string_count = 0;
    if (!reader.UOffset(slot, &group_table) ||
        !reader.TableField(group_table, 0, &strings_field) ||
        !reader.Vector(strings_field, 4, &strings_start, &string_count) ||
        string_count == 0) {
      Reset();
      return Status::ParseError("emoji search group table is invalid");
    }
    std::vector<std::string> group;
    group.reserve(string_count);
    for (size_t string_index = 0; string_index < string_count; ++string_index) {
      std::string emoji;
      if (!reader.StringFromSlot(strings_start + string_index * 4, &emoji)) {
        Reset();
        return Status::ParseError("emoji search group string is invalid");
      }
      group.push_back(std::move(emoji));
    }
    groups_.push_back(std::move(group));
  }

  size_t trie_start = 0;
  size_t trie_size = 0;
  if (!reader.Vector(trie_field, 1, &trie_start, &trie_size) || trie_size == 0) {
    Reset();
    return Status::ParseError("emoji search compact trie is invalid");
  }
  trie_.assign(reader.data() + trie_start,
               reader.data() + trie_start + trie_size);

  size_t visited = 0;
  std::string error;
  if (!WalkTrie(0, {}, &visited, &error) || entries_.empty()) {
    Reset();
    return Status::ParseError(error.empty() ? "emoji search trie is empty"
                                            : std::move(error));
  }
  for (size_t i = 0; i < entries_.size(); ++i) {
    if ((entries_[i].utf16be_key.size() & 1) != 0 ||
        (i != 0 && entries_[i - 1].utf16be_key >= entries_[i].utf16be_key)) {
      Reset();
      return Status::ParseError("emoji search trie keys are not ordered UTF-16BE");
    }
  }

  loaded_ = true;
  return Status::Ok();
}

void EmojiSearchModel::Reset() {
  trie_.clear();
  groups_.clear();
  entries_.clear();
  loaded_ = false;
  invocation_count_.store(0, std::memory_order_relaxed);
  scored_count_.store(0, std::memory_order_relaxed);
}

bool EmojiSearchModel::ReadTrieOffset(size_t position, size_t length,
                                      size_t* value) const {
  if (!value || !HasRange(trie_.size(), position, length)) return false;
  size_t decoded = 0;
  for (size_t i = 0; i < length; ++i) {
    if (decoded > (std::numeric_limits<size_t>::max() >> 8)) return false;
    decoded = (decoded << 8) | trie_[position + i];
  }
  *value = decoded;
  return true;
}

bool EmojiSearchModel::ReadTrieLeaf(size_t position, int32_t* value,
                                    size_t* length) const {
  if (!value || !length || !HasRange(trie_.size(), position, 1)) return false;
  *length = PackedLeafSize(trie_[position]);
  if (!HasRange(trie_.size(), position, *length)) return false;
  uint64_t decoded = trie_[position] & (0x7full >> (*length - 1));
  for (size_t i = 1; i < *length; ++i) {
    decoded = (decoded << 8) | trie_[position + i];
  }
  const uint64_t magnitude = decoded >> 1;
  if (magnitude > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
    return false;
  }
  *value = (decoded & 1) != 0 ? -static_cast<int32_t>(magnitude)
                              : static_cast<int32_t>(magnitude);
  return true;
}

bool EmojiSearchModel::WalkTrie(size_t position, const std::string& prefix,
                                size_t* visited, std::string* error) {
  if (!visited || !error || !HasRange(trie_.size(), position, 1)) {
    if (error) *error = "emoji search trie node is out of bounds";
    return false;
  }
  if (++(*visited) > trie_.size()) {
    *error = "emoji search trie node count exceeds payload size";
    return false;
  }

  const size_t start = position;
  const uint8_t flags = trie_[position++];
  if ((flags & 0xc0) == 0) {
    size_t offset = 0;
    if (!ReadTrieOffset(position, flags & 0x07, &offset) || offset == 0 ||
        offset >= trie_.size() - start) {
      *error = "emoji search trie epsilon link is invalid";
      return false;
    }
    return WalkTrie(start + offset, prefix, visited, error);
  }

  if (!HasRange(trie_.size(), position, 1)) {
    *error = "emoji search trie label is truncated";
    return false;
  }
  const uint8_t label = trie_[position++];
  const size_t left_length = (flags >> 3) & 0x07;
  const size_t right_length = flags & 0x07;
  size_t left_offset = 0;
  size_t right_offset = 0;
  if (!ReadTrieOffset(position, left_length, &left_offset)) {
    *error = "emoji search trie left offset is truncated";
    return false;
  }
  position += left_length;
  if (!ReadTrieOffset(position, right_length, &right_offset)) {
    *error = "emoji search trie right offset is truncated";
    return false;
  }
  position += right_length;

  const auto valid_forward_offset = [this, start](size_t offset) {
    return offset != 0 && offset < trie_.size() - start;
  };
  if (left_offset != 0 &&
      (!valid_forward_offset(left_offset) ||
       !WalkTrie(start + left_offset, prefix, visited, error))) {
    if (error->empty()) *error = "emoji search trie left link is invalid";
    return false;
  }

  std::string key = prefix;
  key.push_back(static_cast<char>(label));
  if ((flags & 0x80) != 0) {
    int32_t group_index = -1;
    size_t leaf_length = 0;
    if (!ReadTrieLeaf(position, &group_index, &leaf_length) ||
        group_index < 0 || static_cast<size_t>(group_index) >= groups_.size()) {
      *error = "emoji search trie group leaf is invalid";
      return false;
    }
    entries_.push_back({key, group_index});
    position += leaf_length;
  }
  if ((flags & 0x40) != 0 && !WalkTrie(position, key, visited, error)) {
    return false;
  }
  if (right_offset != 0 &&
      (!valid_forward_offset(right_offset) ||
       !WalkTrie(start + right_offset, prefix, visited, error))) {
    if (error->empty()) *error = "emoji search trie right link is invalid";
    return false;
  }
  return true;
}

std::string EmojiSearchModel::Utf16BeKey(std::string_view query) {
  std::vector<char32_t> codepoints = DecodeUtf8(query);
  size_t begin = 0;
  size_t end = codepoints.size();
  while (begin < end && codepoints[begin] <= 0x20) ++begin;
  while (end > begin && codepoints[end - 1] <= 0x20) --end;

  std::string result;
  result.reserve((end - begin) * 2);
  for (size_t i = begin; i < end; ++i) {
    char32_t codepoint = codepoints[i];
    if (codepoint >= U'A' && codepoint <= U'Z') {
      codepoint += U'a' - U'A';
    } else if (codepoint >= U'\u0410' && codepoint <= U'\u042f') {
      codepoint += 0x20;
    } else if (codepoint == U'\u0401') {
      codepoint = U'\u0451';
    }
    if (codepoint <= 0xffff &&
        !(codepoint >= 0xd800 && codepoint <= 0xdfff)) {
      AppendUtf16Be(static_cast<uint16_t>(codepoint), &result);
    } else if (codepoint <= 0x10ffff) {
      codepoint -= 0x10000;
      AppendUtf16Be(static_cast<uint16_t>(0xd800 + (codepoint >> 10)),
                    &result);
      AppendUtf16Be(static_cast<uint16_t>(0xdc00 + (codepoint & 0x3ff)),
                    &result);
    }
  }
  return result;
}

EmojiSearchResult EmojiSearchModel::Search(std::string_view query,
                                           size_t max_results,
                                           size_t max_entries) const {
  EmojiSearchResult result;
  if (!loaded_ || max_results == 0 || max_entries == 0) return result;
  const std::string prefix = Utf16BeKey(query);
  if (prefix.empty()) return result;
  invocation_count_.fetch_add(1, std::memory_order_relaxed);

  const auto first = std::lower_bound(
      entries_.begin(), entries_.end(), prefix,
      [](const Entry& entry, const std::string& key) {
        return entry.utf16be_key < key;
      });
  std::unordered_set<int32_t> seen_groups;
  std::unordered_set<std::string> seen_emoji;
  seen_groups.reserve(64);
  seen_emoji.reserve(max_results * 2);
  for (auto it = first; it != entries_.end() &&
                          result.trie_entries_examined < max_entries;
       ++it) {
    if (it->utf16be_key.size() < prefix.size() ||
        it->utf16be_key.compare(0, prefix.size(), prefix) != 0) {
      break;
    }
    ++result.trie_entries_examined;
    if (!seen_groups.insert(it->group_index).second) continue;
    ++result.groups_scored;
    for (const std::string& emoji : groups_[it->group_index]) {
      if (!seen_emoji.insert(emoji).second) continue;
      result.emojis.push_back(emoji);
      if (result.emojis.size() >= max_results) {
        scored_count_.fetch_add(result.emojis.size(),
                                std::memory_order_relaxed);
        return result;
      }
    }
  }
  scored_count_.fetch_add(result.emojis.size(), std::memory_order_relaxed);
  return result;
}

std::string EmojiSearchModel::diagnostics() const {
  if (!loaded_) return "not_loaded";
  std::ostringstream stream;
  stream << "comptrie:utf16be,keys=" << entries_.size()
         << ",groups=" << groups_.size()
         << ",invoke_count=" << invocation_count()
         << ",scored_count=" << scored_count();
  return stream.str();
}

}  // namespace hoskey::engine
