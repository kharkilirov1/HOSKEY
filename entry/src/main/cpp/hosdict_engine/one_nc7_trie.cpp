#include "one_nc7_trie.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <queue>
#include <sstream>
#include <unordered_set>

namespace hoskey::engine {
namespace {

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

bool DecodeOneUtf8(std::string_view text, size_t* pos, char32_t* out) {
  if (!pos || !out || *pos >= text.size()) return false;
  const auto b0 = static_cast<uint8_t>(text[*pos]);
  if (b0 < 0x80) {
    *out = b0;
    ++(*pos);
    return true;
  }
  size_t len = 0;
  char32_t cp = 0;
  if ((b0 & 0xE0U) == 0xC0U) {
    len = 2;
    cp = b0 & 0x1FU;
  } else if ((b0 & 0xF0U) == 0xE0U) {
    len = 3;
    cp = b0 & 0x0FU;
  } else if ((b0 & 0xF8U) == 0xF0U) {
    len = 4;
    cp = b0 & 0x07U;
  } else {
    return false;
  }
  if (*pos + len > text.size()) return false;
  for (size_t i = 1; i < len; ++i) {
    const auto b = static_cast<uint8_t>(text[*pos + i]);
    if ((b & 0xC0U) != 0x80U) return false;
    cp = (cp << 6U) | (b & 0x3FU);
  }
  *pos += len;
  *out = cp;
  return true;
}

void AppendUtf8(char32_t cp, std::string* out) {
  if (!out) return;
  if (cp < 0x80) {
    out->push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out->push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

// ---- Recursive level parser helpers ------------------------------------------

struct RawSectionTrace {
  size_t off = 0;
  uint64_t field0 = 0;
  uint64_t count = 0;
  size_t next = 0;
};

struct BitVectorSectionTrace {
  size_t off = 0;
  uint64_t word_count = 0;
  uint64_t rank_size = 0;
  uint64_t popcount = 0;
  size_t ds_off = 0;
  uint64_t ds_count = 0;
  size_t next = 0;
};

struct DataSectionTrace {
  size_t off = 0;
  uint64_t field0 = 0;
  uint64_t count = 0;
  uint64_t field2 = 0;
  size_t next = 0;
};

struct IntVectorTailTrace {
  size_t off = 0;
  uint64_t bitwidth = 0;
  uint64_t hdr1 = 0;
  uint64_t hdr2 = 0;
  uint64_t span = 0;
  size_t tail = 0;
  uint64_t len1 = 0;
  size_t blob1 = 0;
  uint64_t len2 = 0;
  size_t blob2 = 0;
  uint64_t child_flag = 0;
  size_t child_start = 0;
};

bool SpanOk(size_t off, uint64_t bytes, size_t size) {
  return off <= size && bytes <= static_cast<uint64_t>(size - off);
}

bool AddBytes(size_t base, uint64_t bytes, size_t size, size_t* out) {
  if (!out || !SpanOk(base, bytes, size)) return false;
  *out = base + static_cast<size_t>(bytes);
  return true;
}

Status ParseRawTrace(const uint8_t* data, size_t size, size_t off,
                     RawSectionTrace* out) {
  if (!out || !data) return Status::InvalidArgument("OneNc7Trie: null raw trace");
  if (!SpanOk(off, 16, size)) {
    return Status::ParseError("OneNc7Trie: raw section out of range");
  }
  out->off = off;
  out->field0 = ReadU64LE(data + off);
  out->count = ReadU64LE(data + off + 8);
  if (!AddBytes(off + 0x10, out->count * 8ULL, size, &out->next)) {
    return Status::ParseError("OneNc7Trie: raw section payload out of range");
  }
  return Status::Ok();
}

Status ParseBitVectorTrace(const uint8_t* data, size_t size, size_t off,
                           BitVectorSectionTrace* out) {
  if (!out || !data) {
    return Status::InvalidArgument("OneNc7Trie: null bitvector trace");
  }
  if (!SpanOk(off, 28, size)) {
    return Status::ParseError("OneNc7Trie: bitvector trace header out of range");
  }
  out->off = off;
  out->word_count = ReadU64LE(data + off);
  out->rank_size = ReadU64LE(data + off + 8);
  out->popcount = ReadU64LE(data + off + 16);
  const uint64_t rank_bytes =
      out->word_count * 4ULL + ((out->word_count & 1ULL) == 0 ? 4ULL : 0ULL) +
      0x1CULL;
  if (!AddBytes(off, rank_bytes, size, &out->ds_off)) {
    return Status::ParseError("OneNc7Trie: bitvector trace rank out of range");
  }
  if (!SpanOk(out->ds_off + 8, 8, size)) {
    return Status::ParseError("OneNc7Trie: bitvector trace data header out of range");
  }
  out->ds_count = ReadU64LE(data + out->ds_off + 8);
  if (!AddBytes(out->ds_off + 0x20, out->ds_count * 8ULL, size,
                &out->next)) {
    return Status::ParseError("OneNc7Trie: bitvector trace data out of range");
  }
  return Status::Ok();
}

Status ParseDataSectionTrace(const uint8_t* data, size_t size, size_t off,
                             DataSectionTrace* out) {
  if (!out || !data) return Status::InvalidArgument("OneNc7Trie: null data trace");
  if (!SpanOk(off, 32, size)) {
    return Status::ParseError("OneNc7Trie: data section header out of range");
  }
  out->off = off;
  out->field0 = ReadU64LE(data + off);
  out->count = ReadU64LE(data + off + 8);
  out->field2 = ReadU64LE(data + off + 16);
  if (!AddBytes(off + 0x20, out->count * 8ULL, size, &out->next)) {
    return Status::ParseError("OneNc7Trie: data section payload out of range");
  }
  return Status::Ok();
}

Status ParseIntVectorTailTrace(const uint8_t* data, size_t size, size_t off,
                                IntVectorTailTrace* out) {
  if (!out || !data) {
    return Status::InvalidArgument("OneNc7Trie: null intvector tail trace");
  }
  if (!SpanOk(off, 32, size)) {
    return Status::ParseError("OneNc7Trie: intvector trace header out of range");
  }
  out->off = off;
  out->bitwidth = ReadU64LE(data + off);
  out->hdr1 = ReadU64LE(data + off + 8);
  out->hdr2 = ReadU64LE(data + off + 16);
  out->span = ReadU64LE(data + off + 24);

  if (!AddBytes(off + 0x20, out->span * 8ULL, size, &out->tail)) {
    return Status::ParseError("OneNc7Trie: intvector payload out of range");
  }
  if (!SpanOk(out->tail, 8, size)) {
    return Status::ParseError("OneNc7Trie: tail blob1 length out of range");
  }
  out->len1 = ReadU64LE(data + out->tail);
  out->blob1 = out->tail + 8;

  size_t off2 = 0;
  if (!AddBytes(out->blob1, out->len1, size, &off2) ||
      !SpanOk(off2, 8, size)) {
    return Status::ParseError("OneNc7Trie: tail blob1 out of range");
  }
  out->len2 = ReadU64LE(data + off2);
  out->blob2 = off2 + 8;

  size_t off3 = 0;
  if (!AddBytes(out->blob2, out->len2, size, &off3) ||
      !SpanOk(off3, 8, size)) {
    return Status::ParseError("OneNc7Trie: tail blob2 out of range");
  }
  out->child_flag = ReadU64LE(data + off3);
  out->child_start = off3 + 8;
  if (out->child_flag != 0 && out->child_start >= size) {
    return Status::ParseError("OneNc7Trie: child trie start out of range");
  }
  return Status::Ok();
}

// ---- CP866 → UTF-8 (for sub-level labels) -----------------------------------

/// Yandex CP866-based charset for non-root trie levels.
/// Maps 0x80-0xAF → А-п, 0xE0-0xEF → р-я, 0xF0 → Ё, 0xF1 → ё.
bool Cp866ToLowerCodepoint(uint8_t b, char32_t* out) {
  if (!out) return false;
  if (b >= 0x80 && b <= 0xAF) {
    *out = U'а' + static_cast<char32_t>(b - 0x80);
    return true;
  }
  if (b >= 0xE0 && b <= 0xEF) {
    *out = U'р' + static_cast<char32_t>(b - 0xE0);
    return true;
  }
  if (b == 0xF0) { *out = U'ё'; return true; }  // Ё → ё
  if (b == 0xF1) { *out = U'ё'; return true; }  // ё
  // ASCII pass-through
  if (b >= 0x20 && b < 0x7F) {
    *out = static_cast<char32_t>(b);
    return true;
  }
  return false;
}

}  // namespace

// ============================================================================
// RawBitVector
// ============================================================================

uint64_t OneNc7Trie::RawBitVector::Word(uint64_t idx) const {
  if (!bytes || idx >= word_count) return 0;
  return ReadU64LE(bytes + idx * 8ULL);
}

bool OneNc7Trie::RawBitVector::Bit(uint64_t pos) const {
  if (!bytes || pos >= bit_capacity()) return false;
  return ((bytes[pos >> 3U] >> (pos & 7U)) & 1U) != 0;
}

void OneNc7Trie::RawBitVector::BuildRankCache() {
  rank1_superblocks.clear();
  if (!bytes || word_count == 0) return;

  rank1_superblocks.reserve(
      static_cast<size_t>((word_count + kRankBlockWords - 1ULL) /
                          kRankBlockWords) +
      1U);

  uint64_t total = 0;
  for (uint64_t wi = 0; wi < word_count; ++wi) {
    if ((wi % kRankBlockWords) == 0) {
      rank1_superblocks.push_back(total);
    }
    total += static_cast<uint64_t>(__builtin_popcountll(Word(wi)));
  }
  rank1_superblocks.push_back(total);
}

uint64_t OneNc7Trie::RawBitVector::Rank1(uint64_t pos) const {
  if (!bytes || pos == 0) return 0;
  const uint64_t full_words = std::min<uint64_t>(pos >> 6U, word_count);
  const uint64_t rem = pos & 63U;

  uint64_t start_word = 0;
  uint64_t total = 0;
  if (!rank1_superblocks.empty()) {
    const uint64_t block = full_words / kRankBlockWords;
    if (block < rank1_superblocks.size()) {
      start_word = std::min<uint64_t>(block * kRankBlockWords, word_count);
      total = rank1_superblocks[static_cast<size_t>(block)];
    }
  }

  for (uint64_t i = start_word; i < full_words; ++i) {
    total += static_cast<uint64_t>(__builtin_popcountll(Word(i)));
  }
  if (full_words < word_count && rem != 0) {
    const uint64_t mask = (1ULL << rem) - 1ULL;
    total += static_cast<uint64_t>(__builtin_popcountll(Word(full_words) & mask));
  }
  return total;
}

uint64_t OneNc7Trie::RawBitVector::Select1(uint64_t n) const {
  if (!bytes) return std::numeric_limits<uint64_t>::max();
  uint64_t seen = 0;
  uint64_t start_word = 0;

  if (!rank1_superblocks.empty()) {
    const auto it =
        std::upper_bound(rank1_superblocks.begin(), rank1_superblocks.end(), n);
    const size_t block =
        it == rank1_superblocks.begin()
            ? 0U
            : static_cast<size_t>((it - rank1_superblocks.begin()) - 1);
    if (block < rank1_superblocks.size()) {
      seen = rank1_superblocks[block];
      start_word = std::min<uint64_t>(
          static_cast<uint64_t>(block) * kRankBlockWords, word_count);
    }
  }

  for (uint64_t wi = start_word; wi < word_count; ++wi) {
    const uint64_t word = Word(wi);
    const uint64_t count = static_cast<uint64_t>(__builtin_popcountll(word));
    if (seen + count <= n) {
      seen += count;
      continue;
    }
    for (uint64_t bi = 0; bi < 64; ++bi) {
      if (((word >> bi) & 1ULL) != 0) {
        if (seen == n) return wi * 64ULL + bi;
        ++seen;
      }
    }
  }
  return std::numeric_limits<uint64_t>::max();
}

// ============================================================================
// LeveledView
// ============================================================================

bool OneNc7Trie::LeveledView::BitVectorBit(const RawBitVector& bv,
                                           uint64_t pos) const {
  return bv.Bit(pos);
}

uint8_t OneNc7Trie::LeveledView::LabelByte(uint64_t node_id) const {
  if (!labels || node_id >= labels_size) return 0;
  return labels[node_id];
}

bool OneNc7Trie::LeveledView::HasChild(uint64_t node_id) const {
  return has_child.Bit(node_id);
}

bool OneNc7Trie::LeveledView::IsTerminal(uint64_t node_id) const {
  return terminal.Bit(node_id);
}

uint64_t OneNc7Trie::LeveledView::FirstChild(uint64_t node_id) const {
  if (!HasChild(node_id)) return std::numeric_limits<uint64_t>::max();
  const uint64_t rank = has_child.Rank1(node_id);
  const uint64_t pos = louds.Select1(rank);
  if (pos == std::numeric_limits<uint64_t>::max()) {
    return std::numeric_limits<uint64_t>::max();
  }
  return pos + 1ULL;
}

std::vector<uint64_t> OneNc7Trie::LeveledView::Children(uint64_t node_id) const {
  std::vector<uint64_t> out;
  const uint64_t first = FirstChild(node_id);
  if (first == std::numeric_limits<uint64_t>::max()) return out;
  const size_t cap = node_capacity();
  uint64_t node = first;
  for (size_t i = 0; i < kMaxBranchKids && node < cap; ++i, ++node) {
    out.push_back(node);
    if (louds.Bit(node)) break;
  }
  return out;
}

bool OneNc7Trie::LeveledView::LabelToUtf8(uint64_t node_id,
                                          std::string* out) const {
  if (!out || !labels || node_id >= labels_size) return false;
  const uint8_t b = labels[node_id];
  if (is_root) {
    char32_t cp = 0;
    if (!Cp1251ToCodepoint(b, &cp)) return false;
    AppendUtf8(cp, out);
    return true;
  }
  return SubLabelToUtf8(node_id, out);
}

bool OneNc7Trie::LeveledView::SubLabelToUtf8(uint64_t node_id,
                                             std::string* out) const {
  if (!out || !labels || node_id >= labels_size) return false;
  const uint8_t b = labels[node_id];
  char32_t cp = 0;
  if (Cp866ToLowerCodepoint(b, &cp)) {
    AppendUtf8(cp, out);
    return true;
  }
  return false;
}

uint64_t OneNc7Trie::LeveledView::TerminalHandoffKey(
    uint64_t node_id, uint64_t rank_index) const {
  uint64_t upper = 0;
  if (fun_00661462_bitwidth != 0 && fun_00661462_data) {
    const uint64_t product = rank_index * fun_00661462_bitwidth;
    const uint64_t word_idx = product >> 6;
    const uint64_t bit_off = product & 0x3F;
    const uint64_t remaining = 64 - bit_off;

    uint64_t word = 0;
    if (word_idx < intvector_word_count) {
      word = ReadU64LE(fun_00661462_data + word_idx * 8);
    }
    uint64_t value = word >> bit_off;
    if (remaining < fun_00661462_bitwidth) {
      uint64_t next_word = 0;
      if (word_idx + 1 < intvector_word_count) {
        next_word = ReadU64LE(fun_00661462_data + (word_idx + 1) * 8);
      }
      value |= next_word << remaining;
    }
    upper = (value & fun_00661462_mask) << 8;
  }
  const uint8_t low = (fun_00661462_labels && node_id < labels_size)
                          ? fun_00661462_labels[node_id]
                          : 0;
  return upper | low;
}

// ============================================================================
// Parse
// ============================================================================

Status OneNc7Trie::Parse(const uint8_t* data, size_t size) {
  loaded_ = false;
  data_ = nullptr;
  size_ = 0;
  has_child_ = RawBitVector{};
  louds_ = RawBitVector{};
  terminal_ = RawBitVector{};
  labels_ = nullptr;
  labels_size_ = 0;
  levels_.clear();
  level_views_.clear();
  total_word_count_ = 0;

  if (!data || size < kLabelsOffset + 64) {
    return Status::InvalidArgument("OneNc7Trie: null or too small input");
  }
  if (ReadU32LE(data) != kMagic1nc7) {
    return Status::ParseError("OneNc7Trie: magic mismatch");
  }

  data_ = data;
  size_ = size;

  Status st = LoadBitVector(kHasChildOffset, &has_child_);
  if (!st.ok()) return st;
  st = LoadBitVector(kLoudsOffset, &louds_);
  if (!st.ok()) return st;
  st = LoadBitVector(kTerminalOffset, &terminal_);
  if (!st.ok()) return st;

  labels_ = data_ + kLabelsOffset;
  labels_size_ = size_ - kLabelsOffset;
  if (labels_size_ < node_capacity()) {
    return Status::ParseError(
        "OneNc7Trie: labels section is shorter than node capacity");
  }

  if (RootLettersUtf8() != "абвгдежзийклмнопрстуфхцчшщыэюя") {
    return Status::UnsupportedFormat(
        "OneNc7Trie: root alphabet does not match R3 layout");
  }

  st = ParseRecursiveLevels();
  if (st.ok()) {
    st = BuildLevelViews();
  }
  // BuildLevelViews failure is non-fatal for root-phase operations.

  loaded_ = true;
  return Status::Ok();
}

Status OneNc7Trie::LoadBitVector(size_t offset, RawBitVector* out) const {
  if (!out) return Status::InvalidArgument("OneNc7Trie: null bitvector output");
  if (!data_ || offset + 16 > size_) {
    return Status::ParseError("OneNc7Trie: bitvector header out of range");
  }
  const uint64_t word_count = ReadU64LE(data_ + offset + 8);
  const size_t bytes_offset = offset + 0x10;
  // word_count * 8 (the byte length) can overflow uint64 for a corrupt header,
  // so compare via division: a huge word_count cannot wrap past this check.
  if (word_count == 0 || bytes_offset > size_ ||
      word_count > static_cast<uint64_t>(size_ - bytes_offset) / 8ULL) {
    return Status::ParseError("OneNc7Trie: bitvector payload out of range");
  }
  out->bytes = data_ + bytes_offset;
  out->word_count = word_count;
  out->BuildRankCache();
  return Status::Ok();
}

Status OneNc7Trie::ParseRecursiveLevels() {
  levels_.clear();
  if (!data_ || size_ < 16) {
    return Status::InvalidState("OneNc7Trie: no data for recursive levels");
  }

  size_t off = 8;
  static constexpr uint32_t kMaxLevels = 8;
  for (uint32_t depth = 0; depth < kMaxLevels; ++depth) {
    RawSectionTrace raw1;
    BitVectorSectionTrace bv1;
    RawSectionTrace raw2;
    BitVectorSectionTrace bv2;
    RawSectionTrace raw3;
    DataSectionTrace ds3;
    RawSectionTrace raw4;
    DataSectionTrace ds4;
    IntVectorTailTrace tail;

    Status st = ParseRawTrace(data_, size_, off, &raw1);
    if (!st.ok()) return st;
    st = ParseBitVectorTrace(data_, size_, raw1.next, &bv1);
    if (!st.ok()) return st;
    st = ParseRawTrace(data_, size_, bv1.next, &raw2);
    if (!st.ok()) return st;
    st = ParseBitVectorTrace(data_, size_, raw2.next, &bv2);
    if (!st.ok()) return st;
    st = ParseRawTrace(data_, size_, bv2.next, &raw3);
    if (!st.ok()) return st;
    st = ParseDataSectionTrace(data_, size_, raw3.next, &ds3);
    if (!st.ok()) return st;
    st = ParseRawTrace(data_, size_, ds3.next, &raw4);
    if (!st.ok()) return st;
    st = ParseDataSectionTrace(data_, size_, raw4.next, &ds4);
    if (!st.ok()) return st;
    st = ParseIntVectorTailTrace(data_, size_, ds4.next, &tail);
    if (!st.ok()) return st;

    OneNc7LevelInfo level;
    level.depth = depth;
    level.start_offset = off;
    level.node_count_hint = raw4.field0;
    level.label_bitwidth = tail.bitwidth;

    level.has_child_raw_offset = raw1.off;
    level.has_child_bits_offset = raw1.off + 0x10;
    level.has_child_word_count = raw1.count;
    level.louds_raw_offset = raw2.off;
    level.louds_bits_offset = raw2.off + 0x10;
    level.louds_word_count = raw2.count;
    level.terminal_raw_offset = raw4.off;
    level.terminal_bits_offset = raw4.off + 0x10;
    level.terminal_word_count = raw4.count;
    level.terminal_rank_ds_offset = ds4.off;

    level.intvector_offset = tail.off;
    level.intvector_data_offset = tail.off + 0x20;
    level.intvector_data_size = static_cast<size_t>(tail.span * 8ULL);
    level.intvector_bitwidth = tail.bitwidth;
    level.intvector_mask = tail.hdr1;
    level.intvector_total_bits = tail.hdr2;
    level.intvector_word_count = tail.span;

    level.label_blob_offset = tail.blob1;
    level.label_blob_size = static_cast<size_t>(tail.len1);
    level.suffix_blob_offset = tail.blob2;
    level.suffix_blob_size = static_cast<size_t>(tail.len2);
    level.child_flag = tail.child_flag;
    level.child_start_offset = tail.child_start;
    levels_.push_back(level);

    if (tail.child_flag == 0) break;
    off = tail.child_start;
  }

  return levels_.empty()
             ? Status::ParseError("OneNc7Trie: no recursive levels parsed")
             : Status::Ok();
}

Status OneNc7Trie::BuildLevelViews() {
  level_views_.clear();
  for (size_t i = 0; i < levels_.size(); ++i) {
    const auto& info = levels_[i];
    LeveledView view;

    if (i == 0) {
      // Root level: use the hardcoded confirmed offsets for bitvectors
      // and the flat CP1251 label array at kLabelsOffset.
      view.has_child = has_child_;
      view.louds = louds_;
      view.terminal = terminal_;
      view.labels = labels_;
      view.labels_size = labels_size_;
      view.is_root = true;
    } else {
      // Sub-levels: load bitvectors from level-specific offsets.
      Status st = LoadBitVector(info.has_child_raw_offset, &view.has_child);
      if (!st.ok()) return st;
      st = LoadBitVector(info.louds_raw_offset, &view.louds);
      if (!st.ok()) return st;
      st = LoadBitVector(info.terminal_raw_offset, &view.terminal);
      if (!st.ok()) return st;

      if (info.label_blob_size > 0 &&
          info.label_blob_offset + info.label_blob_size <= size_) {
        view.labels = data_ + info.label_blob_offset;
        view.labels_size = info.label_blob_size;
      }
      view.is_root = false;
    }

    // Suffix pool and intvector are level-specific for all levels.
    if (info.suffix_blob_size > 0 &&
        info.suffix_blob_offset + info.suffix_blob_size <= size_) {
      view.suffix_blob = data_ + info.suffix_blob_offset;
      view.suffix_size = info.suffix_blob_size;
    }

    view.intvector_data_offset = info.intvector_data_offset;
    view.intvector_bitwidth = info.intvector_bitwidth;
    view.intvector_mask = info.intvector_mask;
    view.intvector_word_count = info.intvector_word_count;
    view.intvector_total_bits = info.intvector_total_bits;

    // FUN_00661462 fields: intvector provides upper-bit extension for
    // terminal handoff keys. Same data as the weight intvector.
    view.fun_00661462_data = data_ + info.intvector_data_offset;
    view.fun_00661462_bitwidth = info.intvector_bitwidth;
    view.fun_00661462_mask = info.intvector_mask;
    view.fun_00661462_labels = view.labels;

    level_views_.push_back(view);
  }
  return Status::Ok();
}

const OneNc7LevelInfo* OneNc7Trie::Level(size_t level_index) const {
  return level_index < levels_.size() ? &levels_[level_index] : nullptr;
}

const OneNc7Trie::LeveledView* OneNc7Trie::LevelView(size_t level_index) const {
  return level_index < level_views_.size() ? &level_views_[level_index]
                                            : nullptr;
}

// ============================================================================
// Root-level helpers (existing API)
// ============================================================================

std::string OneNc7Trie::DecodeCp1251Slice(size_t offset, size_t size) const {
  std::string out;
  if (!data_ || offset > size_ || size > size_ - offset) return out;
  for (size_t i = 0; i < size; ++i) {
    const uint8_t b = data_[offset + i];
    char32_t cp = 0;
    if (Cp1251ToCodepoint(b, &cp)) {
      AppendUtf8(cp, &out);
    } else if (b >= 0x20 && b < 0x7F) {
      out.push_back(static_cast<char>(b));
    }
  }
  return out;
}

size_t OneNc7Trie::node_capacity() const {
  return static_cast<size_t>(
      std::min(has_child_.bit_capacity(), louds_.bit_capacity()));
}

uint8_t OneNc7Trie::LabelByte(uint64_t node_id) const {
  if (!labels_ || node_id >= labels_size_) return 0;
  return labels_[node_id];
}

bool OneNc7Trie::HasChild(uint64_t node_id) const {
  return has_child_.Bit(node_id);
}

bool OneNc7Trie::IsTerminal(uint64_t node_id) const {
  return terminal_.Bit(node_id);
}

uint64_t OneNc7Trie::FirstChild(uint64_t node_id) const {
  if (!HasChild(node_id)) return std::numeric_limits<uint64_t>::max();
  const uint64_t rank = has_child_.Rank1(node_id);
  const uint64_t pos = louds_.Select1(rank);
  if (pos == std::numeric_limits<uint64_t>::max()) {
    return std::numeric_limits<uint64_t>::max();
  }
  return pos + 1ULL;
}

std::vector<uint64_t> OneNc7Trie::Children(uint64_t node_id) const {
  std::vector<uint64_t> out;
  const uint64_t first = FirstChild(node_id);
  if (first == std::numeric_limits<uint64_t>::max()) return out;
  uint64_t node = first;
  for (size_t i = 0; i < kMaxBranchKids && node < node_capacity();
       ++i, ++node) {
    out.push_back(node);
    if (louds_.Bit(node)) break;
  }
  return out;
}

std::vector<OneNc7NodeState> OneNc7Trie::RootStates() const {
  std::vector<OneNc7NodeState> out;
  for (const uint64_t child : Children(0)) {
    char32_t cp = 0;
    const uint8_t label = LabelByte(child);
    if (!Cp1251ToCodepoint(label, &cp)) continue;
    out.push_back({static_cast<uint32_t>(child), label, IsTerminal(child),
                   HasChild(child)});
  }
  return out;
}

std::string OneNc7Trie::RootLettersUtf8() const {
  std::string out;
  for (const auto& state : RootStates()) {
    char32_t cp = 0;
    if (Cp1251ToCodepoint(state.label_cp1251, &cp)) {
      AppendUtf8(cp, &out);
    }
  }
  return out;
}

std::vector<OneNc7NodeState> OneNc7Trie::WalkPrefix(
    std::string_view utf8_prefix) const {
  if (!loaded_) return {};
  std::vector<uint8_t> labels;
  if (!DecodePrefixToCp1251(utf8_prefix, &labels)) return {};
  if (labels.empty()) return RootStates();

  std::vector<OneNc7NodeState> states;
  states.push_back({0, LabelByte(0), IsTerminal(0), HasChild(0)});

  for (uint8_t target : labels) {
    std::vector<OneNc7NodeState> next;
    std::unordered_set<uint64_t> seen;
    for (const auto& state : states) {
      for (const uint64_t child : Children(state.node_id)) {
        if (LabelByte(child) != target) continue;
        if (!seen.insert(child).second) continue;
        next.push_back({static_cast<uint32_t>(child), target,
                        IsTerminal(child), HasChild(child)});
      }
    }
    std::sort(next.begin(), next.end(),
              [](const OneNc7NodeState& a, const OneNc7NodeState& b) {
                return a.node_id < b.node_id;
              });
    states = std::move(next);
    if (states.empty()) break;
  }
  return states;
}

OneNc7NextLetters OneNc7Trie::NextLetters(std::string_view utf8_prefix) const {
  OneNc7NextLetters out;
  const auto states = WalkPrefix(utf8_prefix);
  std::vector<char32_t> all;
  std::vector<char32_t> nonterminal;
  std::vector<char32_t> terminal;
  for (const auto& state : states) {
    for (const uint64_t child : Children(state.node_id)) {
      char32_t cp = 0;
      if (!Cp1251ToCodepoint(LabelByte(child), &cp)) continue;
      all.push_back(cp);
      if (state.terminal) {
        terminal.push_back(cp);
      } else {
        nonterminal.push_back(cp);
      }
    }
  }
  out.all = SortedLettersToUtf8(all);
  out.nonterminal = SortedLettersToUtf8(nonterminal);
  out.terminal = SortedLettersToUtf8(terminal);
  return out;
}

bool OneNc7Trie::HasPrefix(std::string_view utf8_prefix) const {
  return !WalkPrefix(utf8_prefix).empty();
}

bool OneNc7Trie::HasTerminalBoundary(std::string_view utf8_prefix) const {
  const auto states = WalkPrefix(utf8_prefix);
  return std::any_of(states.begin(), states.end(),
                     [](const auto& st) { return st.terminal; });
}

std::string OneNc7Trie::FormatLevelSummary() const {
  std::ostringstream out;
  for (size_t i = 0; i < levels_.size(); ++i) {
    if (i != 0) out << ";";
    const auto& level = levels_[i];
    out << "d" << level.depth << "@0x" << std::hex << level.start_offset
        << std::dec << ":nodes=" << level.node_count_hint
        << ":bw=" << level.label_bitwidth
        << ":labels=" << level.label_blob_size
        << ":suffix=" << level.suffix_blob_size
        << ":child=" << (level.child_flag != 0 ? 1 : 0);
    if (i < level_views_.size()) {
      out << ":bv=ok";
    }
  }
  return out.str();
}

// ============================================================================
// Suffix pool
// ============================================================================

std::vector<OneNc7SuffixEntry> OneNc7Trie::SuffixEntries(
    size_t level_index, size_t max_entries) const {
  std::vector<OneNc7SuffixEntry> out;
  const OneNc7LevelInfo* level = Level(level_index);
  if (!level || !data_ || level->suffix_blob_size == 0) return out;
  if (level->suffix_blob_offset > size_ ||
      level->suffix_blob_size > size_ - level->suffix_blob_offset) {
    return out;
  }

  const size_t begin = level->suffix_blob_offset;
  const size_t end = begin + level->suffix_blob_size;
  size_t pos = begin;
  while (pos < end) {
    while (pos < end && data_[pos] == 0) ++pos;
    if (pos >= end) break;
    const size_t entry_begin = pos;
    while (pos < end && data_[pos] != 0) ++pos;
    std::string text = DecodeCp1251Slice(entry_begin, pos - entry_begin);
    if (!text.empty()) {
      out.push_back(OneNc7SuffixEntry{
          static_cast<uint32_t>(entry_begin - begin), std::move(text)});
      if (max_entries != 0 && out.size() >= max_entries) break;
    }
  }
  return out;
}

size_t OneNc7Trie::SuffixEntryCount(size_t level_index) const {
  const OneNc7LevelInfo* level = Level(level_index);
  if (!level || !data_ || level->suffix_blob_size == 0) return 0;
  if (level->suffix_blob_offset > size_ ||
      level->suffix_blob_size > size_ - level->suffix_blob_offset) {
    return 0;
  }
  size_t count = 0;
  const size_t begin = level->suffix_blob_offset;
  const size_t end = begin + level->suffix_blob_size;
  size_t pos = begin;
  while (pos < end) {
    while (pos < end && data_[pos] == 0) ++pos;
    if (pos >= end) break;
    const size_t entry_begin = pos;
    while (pos < end && data_[pos] != 0) ++pos;
    if (pos > entry_begin) ++count;
  }
  return count;
}

bool OneNc7Trie::HasSuffixEntry(size_t level_index,
                                std::string_view utf8_text) const {
  const auto entries = SuffixEntries(level_index);
  return std::any_of(entries.begin(), entries.end(), [&](const auto& entry) {
    return entry.text == utf8_text;
  });
}

// ============================================================================
// Terminal key / weight extraction
// ============================================================================

bool OneNc7Trie::PackedUpper(size_t level_index, uint64_t rank_index,
                             uint64_t* out) const {
  if (!out) return false;
  *out = 0;
  const OneNc7LevelInfo* level = Level(level_index);
  if (!level || !data_) return false;

  const uint64_t bitwidth = level->intvector_bitwidth;
  if (bitwidth == 0) return true;
  if (bitwidth >= 64) return false;
  if (rank_index > std::numeric_limits<uint64_t>::max() / bitwidth) {
    return false;
  }

  const uint64_t bitpos = rank_index * bitwidth;
  if (level->intvector_total_bits != 0) {
    if (bitpos >= level->intvector_total_bits ||
        bitwidth > level->intvector_total_bits - bitpos) {
      return false;
    }
  }

  const uint64_t word_index = bitpos >> 6U;
  const uint64_t bit_offset = bitpos & 63U;
  if (word_index >= level->intvector_word_count) return false;

  const size_t word_off =
      level->intvector_data_offset + static_cast<size_t>(word_index) * 8ULL;
  if (word_off > size_ || size_ - word_off < 8) return false;

  uint64_t value = ReadU64LE(data_ + word_off) >> bit_offset;
  if (bit_offset + bitwidth > 64U) {
    if (word_index + 1 >= level->intvector_word_count) return false;
    const size_t next_off = word_off + 8;
    if (next_off > size_ || size_ - next_off < 8) return false;
    value |= ReadU64LE(data_ + next_off) << ((64U - bit_offset) & 63U);
  }

  *out = value & level->intvector_mask;
  return true;
}

bool OneNc7Trie::TerminalKey(size_t level_index, uint64_t node_id,
                             uint64_t rank_index, uint64_t* out) const {
  if (!out) return false;
  *out = 0;
  const OneNc7LevelInfo* level = Level(level_index);
  if (!level || !data_) return false;
  if (node_id >= level->label_blob_size) return false;
  if (level->label_blob_offset > size_ ||
      level->label_blob_size > size_ - level->label_blob_offset) {
    return false;
  }

  uint64_t upper = 0;
  if (!PackedUpper(level_index, rank_index, &upper)) return false;
  const uint8_t low =
      data_[level->label_blob_offset + static_cast<size_t>(node_id)];
  *out = (upper << 8U) | low;
  return true;
}

uint64_t OneNc7Trie::RootTerminalRank(uint64_t node_id) const {
  if (!loaded_ || node_id >= node_capacity() || !IsTerminal(node_id)) {
    return std::numeric_limits<uint64_t>::max();
  }
  return terminal_.Rank1(node_id);
}

bool OneNc7Trie::RootTerminalKey(uint64_t node_id, uint64_t* out) const {
  if (!out) return false;
  *out = 0;
  const uint64_t rank = RootTerminalRank(node_id);
  if (rank == std::numeric_limits<uint64_t>::max()) return false;
  return TerminalKey(0, node_id, rank, out);
}

// ============================================================================
// Terminal weight lookup for a leveled view
// ============================================================================

int32_t OneNc7Trie::TerminalWeightForLevel(const LeveledView& view,
                                           uint64_t node_id) const {
  if (!view.IsTerminal(node_id)) return kMinWeight;
  const uint64_t rank = view.terminal.Rank1(node_id);

  // Use intvector packed upper as a weight hint.
  if (view.intvector_bitwidth != 0 && view.intvector_word_count != 0 &&
      view.intvector_data_offset + view.intvector_word_count * 8ULL <= size_) {
    const uint64_t bitpos = rank * view.intvector_bitwidth;
    const uint64_t word_index = bitpos >> 6U;
    const uint64_t bit_offset = bitpos & 63U;

    if (word_index < view.intvector_word_count) {
      const size_t word_off =
          view.intvector_data_offset + static_cast<size_t>(word_index) * 8ULL;
      if (word_off + 8 <= size_) {
        uint64_t value =
            (ReadU64LE(data_ + word_off) >> bit_offset) & view.intvector_mask;
        if (value > 0) {
          // Scale packed upper to a reasonable weight range.
          const int32_t w = static_cast<int32_t>(value);
          return std::max(kMinWeight, w * 10);
        }
      }
    }
  }
  return kDefaultWeight;
}

// ============================================================================
// TerminalCompletions (root-phase only, existing API)
// ============================================================================

std::vector<OneNc7Completion> OneNc7Trie::TerminalCompletions(
    std::string_view utf8_prefix, size_t max_extra_letters,
    size_t max_results) const {
  std::vector<OneNc7Completion> out;
  if (!loaded_ || max_results == 0) return out;

  struct Frame {
    OneNc7NodeState state;
    std::string text;
    uint32_t extra = 0;
  };

  std::vector<Frame> queue;
  if (utf8_prefix.empty()) {
    for (const auto& state : RootStates()) {
      char32_t cp = 0;
      if (!Cp1251ToCodepoint(state.label_cp1251, &cp)) continue;
      std::string text;
      AppendUtf8(cp, &text);
      queue.push_back({state, std::move(text), 1});
    }
  } else {
    for (const auto& state : WalkPrefix(utf8_prefix)) {
      queue.push_back({state, std::string(utf8_prefix), 0});
    }
  }

  std::unordered_set<std::string> seen;
  for (size_t cursor = 0; cursor < queue.size() && out.size() < max_results;
       ++cursor) {
    const Frame frame = queue[cursor];
    if (frame.state.terminal && !frame.text.empty() &&
        seen.insert(frame.text).second) {
      out.push_back(
          OneNc7Completion{frame.text, frame.state.node_id, frame.extra});
      if (out.size() >= max_results) break;
    }

    if (frame.extra >= max_extra_letters) continue;
    for (const uint64_t child : Children(frame.state.node_id)) {
      char32_t cp = 0;
      const uint8_t label = LabelByte(child);
      if (!Cp1251ToCodepoint(label, &cp)) continue;
      std::string text = frame.text;
      AppendUtf8(cp, &text);
      queue.push_back(
          {OneNc7NodeState{static_cast<uint32_t>(child), label,
                           IsTerminal(child), HasChild(child)},
           std::move(text), frame.extra + 1});
    }
  }
  return out;
}

bool OneNc7Trie::AppendMaterializedByte(uint8_t byte, std::string* out) {
  if (!out || byte == 0) return false;

  // The reversed code emits raw charset bytes.  The Russian package's level
  // blobs are direct cp1251 in the confirmed probes; keep CP866 as a fallback
  // for older experimental blobs.
  char32_t cp = 0;
  if (Cp1251ToCodepoint(byte, &cp) || Cp866ToLowerCodepoint(byte, &cp)) {
    AppendUtf8(cp, out);
    return true;
  }

  if (byte == static_cast<uint8_t>(' ') || byte == static_cast<uint8_t>('-')) {
    out->push_back(static_cast<char>(byte));
    return true;
  }
  return false;
}

bool OneNc7Trie::IsPlausibleSuggestionText(std::string_view utf8_text) {
  if (utf8_text.empty()) return false;
  bool has_cyrillic = false;
  size_t pos = 0;
  while (pos < utf8_text.size()) {
    char32_t cp = 0;
    if (!DecodeOneUtf8(utf8_text, &pos, &cp)) return false;
    if ((cp >= U'а' && cp <= U'я') || cp == U'ё') {
      has_cyrillic = true;
      continue;
    }
    if (cp == U' ' || cp == U'-') continue;
    return false;
  }
  return has_cyrillic;
}

OneNc7Trie::MaterializerStepStatus OneNc7Trie::StepRecursiveMaterializer(
    const MaterializerState& state,
    uint8_t* emitted,
    MaterializerState* next_state) const {
  if (!emitted || !next_state || level_views_.empty()) {
    return MaterializerStepStatus::kInvalid;
  }

  MaterializerState cur = state;
  // Terminal edges are epsilon transitions.  A small guard prevents malformed
  // blobs from looping forever while still allowing chained terminal handoffs.
  for (size_t guard = 0; guard < 16; ++guard) {
    if (cur.depth == 0 || cur.depth >= 4) {
      return MaterializerStepStatus::kInvalid;
    }

    const uint64_t key = cur.q[cur.depth];
    if (key == 0) {
      if (cur.depth == 1) return MaterializerStepStatus::kEnd;
      --cur.depth;
      continue;
    }

    // Leaf path: this is the `lVar6 == 0` branch in FUN_00661926.
    // q[depth] is an offset into the previous level's +0x180 suffix pool.
    if (cur.depth >= level_views_.size()) {
      const size_t leaf_level = static_cast<size_t>(cur.depth) - 1U;
      if (leaf_level >= level_views_.size()) {
        return MaterializerStepStatus::kInvalid;
      }
      const auto& leaf = level_views_[leaf_level];
      if (!leaf.suffix_blob || key >= leaf.suffix_size) {
        return MaterializerStepStatus::kInvalid;
      }
      const auto off = static_cast<size_t>(key);
      const uint8_t byte = leaf.suffix_blob[off];
      if (byte == 0) return MaterializerStepStatus::kEnd;
      cur.q[cur.depth] =
          (off + 1U < leaf.suffix_size && leaf.suffix_blob[off + 1U] != 0)
              ? key + 1U
              : 0U;
      *emitted = byte;
      *next_state = cur;
      return MaterializerStepStatus::kEmit;
    }

    // Recursive non-leaf path.  This mirrors:
    //   rank = FUN_00661538(child+0xA0, child+0x78, key)
    //   next = FUN_00661646(child+0x18, child+0x10, rank - 1)
    const auto& view = level_views_[cur.depth];
    if (key >= view.labels_size) return MaterializerStepStatus::kInvalid;
    const uint64_t rank = view.louds.Rank1(key);
    if (rank == 0) return MaterializerStepStatus::kInvalid;
    const uint64_t next_key = view.has_child.Select1(rank - 1U);
    if (next_key == std::numeric_limits<uint64_t>::max()) {
      return MaterializerStepStatus::kInvalid;
    }

    if (!view.IsTerminal(key)) {
      const uint8_t byte = view.LabelByte(key);
      if (byte == 0) return MaterializerStepStatus::kInvalid;
      cur.q[cur.depth] = next_key;
      *emitted = byte;
      *next_state = cur;
      return MaterializerStepStatus::kEmit;
    }

    const uint64_t terminal_rank = view.terminal.Rank1(key);
    const uint64_t handoff_key =
        view.TerminalHandoffKey(key, terminal_rank);
    cur.q[cur.depth] = next_key;
    if (cur.depth + 1U >= 4) return MaterializerStepStatus::kInvalid;
    cur.q[cur.depth + 1U] = handoff_key;
    ++cur.depth;
  }

  return MaterializerStepStatus::kInvalid;
}

bool OneNc7Trie::MaterializeRootTerminalContinuation(
    uint64_t terminal_node_id,
    const std::string& base_text,
    size_t max_extra_letters,
    OneNc7Candidate* out) const {
  if (!out || level_views_.empty()) return false;
  const auto& root = level_views_[0];
  if (!root.IsTerminal(terminal_node_id)) return false;

  const uint64_t rank = root.terminal.Rank1(terminal_node_id);
  MaterializerState state;
  state.q[0] = terminal_node_id;
  state.q[1] = root.TerminalHandoffKey(terminal_node_id, rank);
  state.depth = 1;

  std::string text = base_text;
  size_t emitted_count = 0;
  bool ended_cleanly = false;
  while (emitted_count < max_extra_letters) {
    uint8_t byte = 0;
    MaterializerState next;
    const MaterializerStepStatus step =
        StepRecursiveMaterializer(state, &byte, &next);
    if (step == MaterializerStepStatus::kEnd) {
      ended_cleanly = true;
      break;
    }
    if (step == MaterializerStepStatus::kInvalid) return false;
    if (!AppendMaterializedByte(byte, &text)) return false;
    state = next;
    ++emitted_count;
  }

  // Do not expose a candidate that was cut only because our UI/diagnostic
  // bound was reached.  The decompiled iterator emits a byte stream; without
  // an explicit end-of-branch we would otherwise surface truncated fragments
  // such as long prefixes clipped at max_extra_letters.
  if (emitted_count >= max_extra_letters) {
    uint8_t extra_byte = 0;
    MaterializerState extra_state;
    const MaterializerStepStatus extra =
        StepRecursiveMaterializer(state, &extra_byte, &extra_state);
    if (extra == MaterializerStepStatus::kEmit ||
        extra == MaterializerStepStatus::kInvalid) {
      return false;
    }
    ended_cleanly = true;
  }

  if (!ended_cleanly || emitted_count == 0 || text == base_text ||
      !IsPlausibleSuggestionText(text)) {
    return false;
  }

  out->text = std::move(text);
  out->weight = TerminalWeightForLevel(root, terminal_node_id);
  out->depth = static_cast<uint32_t>(emitted_count);
  out->level = 0;
  return true;
}

// ============================================================================
// Complete — native candidate provider with multi-level terminal handoff
// ============================================================================

std::vector<OneNc7Candidate> OneNc7Trie::Complete(
    std::string_view utf8_prefix, size_t max_results,
    size_t max_extra_letters) const {
  std::vector<OneNc7Candidate> results;
  if (!loaded_ || max_results == 0 || level_views_.empty()) return results;

  const auto states = WalkPrefix(utf8_prefix);
  if (states.empty()) return results;
  const std::string prefix_str(utf8_prefix);
  const size_t candidate_limit =
      std::max(max_results, std::min<size_t>(max_results * 8U + 16U, 512U));

  std::unordered_set<std::string> seen;
  auto add_candidate = [&](OneNc7Candidate candidate) {
    if (results.size() >= candidate_limit) return;
    if (candidate.text.empty() || candidate.text == prefix_str) return;
    if (!IsPlausibleSuggestionText(candidate.text)) return;
    if (!seen.insert(candidate.text).second) return;
    results.push_back(std::move(candidate));
  };

  // Root phase mirrors FUN_00661412/FUN_0066242c/FUN_00662502:
  // non-terminal root nodes emit their label, while terminal root nodes are
  // epsilon handoffs into FUN_00661926 via FUN_00661462/FUN_00661baa.  This
  // intentionally replaces the old "record every terminal node as a word" DFS.
  struct RootFrame {
    uint64_t node_id;
    std::string text;
    size_t extra_letters;
    int32_t weight;
  };

  std::vector<RootFrame> queue;
  queue.reserve(128);

  for (const auto& state : states) {
    if (state.terminal && !prefix_str.empty()) {
      OneNc7Candidate materialized;
      if (MaterializeRootTerminalContinuation(
              state.node_id, prefix_str, max_extra_letters, &materialized)) {
        add_candidate(std::move(materialized));
      }
    }

    // Reverse-backed semantics: a terminal root state is an epsilon handoff
    // state, not an ordinary visible-prefix continuation node.  Its child
    // family is still useful for NextLetters diagnostics, but candidate
    // materialization must go through FUN_00661462/FUN_00661baa above; otherwise
    // we mix two distinct Yandex branches and surface noisy sibling fragments.
    if (state.has_child && !state.terminal) {
      queue.push_back({state.node_id, prefix_str, 0,
                       TerminalWeightForLevel(level_views_[0],
                                               state.node_id)});
    }
  }

  static constexpr size_t kMaxRootFrames = 512;
  for (size_t cursor = 0; cursor < queue.size() &&
                         cursor < kMaxRootFrames &&
                         results.size() < candidate_limit;
       ++cursor) {
    const RootFrame frame = queue[cursor];
    if (frame.extra_letters >= max_extra_letters) continue;

    for (const uint64_t child : level_views_[0].Children(frame.node_id)) {
      if (level_views_[0].IsTerminal(child)) {
        OneNc7Candidate materialized;
        const size_t remaining =
            max_extra_letters > frame.extra_letters
                ? max_extra_letters - frame.extra_letters
                : 0;
        if (MaterializeRootTerminalContinuation(
                child, frame.text, remaining, &materialized)) {
          materialized.depth += static_cast<uint32_t>(frame.extra_letters);
          add_candidate(std::move(materialized));
          if (results.size() >= candidate_limit) break;
        }
        continue;
      }

      std::string child_text = frame.text;
      if (!level_views_[0].LabelToUtf8(child, &child_text)) continue;
      if (!IsPlausibleSuggestionText(child_text)) continue;
      queue.push_back({child, std::move(child_text), frame.extra_letters + 1U,
                       TerminalWeightForLevel(level_views_[0], child)});
    }
  }

  // The iterator enumerates candidates in trie storage order, not user-visible
  // rank order.  Native Yandex then feeds them into LM/ranker stages.  Until the
  // full model stack is available on every target, keep the provider itself
  // deterministic and frequency-aware: collect a wider beam, then retain the
  // strongest static 1nc7 candidates.
  std::stable_sort(results.begin(), results.end(),
                   [](const OneNc7Candidate& a, const OneNc7Candidate& b) {
                     if (a.weight != b.weight) return a.weight > b.weight;
                     if (a.depth != b.depth) return a.depth < b.depth;
                     return a.text < b.text;
                   });
  if (results.size() > max_results) {
    results.resize(max_results);
  }
  return results;
}

// ============================================================================
// Contains — exact word lookup via multi-level trie
// ============================================================================

bool OneNc7Trie::Contains(std::string_view utf8_word) const {
  if (!loaded_ || utf8_word.empty()) return false;

  // Fast path: direct visible-prefix terminal boundary in the root graph.
  if (HasTerminalBoundary(utf8_word)) return true;

  if (!IsPlausibleSuggestionText(utf8_word)) return false;

  std::vector<size_t> boundaries;
  std::vector<size_t> cp_counts;
  boundaries.push_back(0);
  cp_counts.push_back(0);
  size_t pos = 0;
  size_t cp_count = 0;
  while (pos < utf8_word.size()) {
    char32_t cp = 0;
    if (!DecodeOneUtf8(utf8_word, &pos, &cp)) return false;
    ++cp_count;
    boundaries.push_back(pos);
    cp_counts.push_back(cp_count);
  }
  if (boundaries.size() < 2) return false;

  // Exact materialized lookup.  Terminal nodes are epsilon handoffs, so many
  // real words are visible only as iterator-produced strings from an earlier
  // prefix, not as a direct terminal at the full visible prefix.
  //
  // Walk from the longest proper prefix backwards: this keeps runtime bounded
  // and mirrors the actual suggestion path where the visible prefix is already
  // typed and the iterator supplies the remaining suffix.
  static constexpr size_t kMaxExactMaterializedSuffix = 24;
  static constexpr size_t kExactLookupLimit = 4096;
  const std::string target(utf8_word);
  for (size_t i = boundaries.size() - 1; i-- > 0;) {
    const size_t prefix_bytes = boundaries[i];
    const size_t remaining_cp = cp_count - cp_counts[i];
    if (remaining_cp == 0) continue;
    if (remaining_cp > kMaxExactMaterializedSuffix) break;

    const std::string_view prefix(utf8_word.data(), prefix_bytes);
    const auto completions =
        Complete(prefix, kExactLookupLimit, remaining_cp);
    if (std::any_of(completions.begin(), completions.end(),
                    [&](const OneNc7Candidate& c) {
                      return c.text == target;
                    })) {
      return true;
    }
  }

  return false;
}

int32_t OneNc7Trie::DirectStaticWeight(std::string_view utf8_word) const {
  if (!loaded_ || utf8_word.empty() || level_views_.empty()) return 0;

  int32_t best_weight = 0;
  for (const auto& state : WalkPrefix(utf8_word)) {
    if (!state.terminal) continue;
    best_weight = std::max(best_weight,
                           TerminalWeightForLevel(level_views_[0],
                                                  state.node_id));
  }
  return best_weight;
}

int32_t OneNc7Trie::StaticWeight(std::string_view utf8_word) const {
  if (!loaded_ || utf8_word.empty() || level_views_.empty()) return 0;

  int32_t best_weight = 0;
  for (const auto& state : WalkPrefix(utf8_word)) {
    if (!state.terminal) continue;
    best_weight = std::max(best_weight,
                           TerminalWeightForLevel(level_views_[0],
                                                  state.node_id));
  }
  if (best_weight > 0) return best_weight;

  if (!IsPlausibleSuggestionText(utf8_word)) return 0;

  std::vector<size_t> boundaries;
  std::vector<size_t> cp_counts;
  boundaries.push_back(0);
  cp_counts.push_back(0);
  size_t pos = 0;
  size_t cp_count = 0;
  while (pos < utf8_word.size()) {
    char32_t cp = 0;
    if (!DecodeOneUtf8(utf8_word, &pos, &cp)) return 0;
    ++cp_count;
    boundaries.push_back(pos);
    cp_counts.push_back(cp_count);
  }
  if (boundaries.size() < 2) return 0;

  // Same proof model as Contains(): exact means the word is a clean iterator
  // materialization from one of its proper prefixes.  This is a dictionary
  // signal, not a flat word-list lookup.
  static constexpr size_t kMaxExactMaterializedSuffix = 24;
  static constexpr size_t kWeightLookupLimit = 4096;
  const std::string target(utf8_word);
  for (size_t i = boundaries.size() - 1; i-- > 0;) {
    const size_t prefix_bytes = boundaries[i];
    const size_t remaining_cp = cp_count - cp_counts[i];
    if (remaining_cp == 0) continue;
    if (remaining_cp > kMaxExactMaterializedSuffix) break;

    const std::string_view prefix(utf8_word.data(), prefix_bytes);
    const auto completions = Complete(prefix, kWeightLookupLimit, remaining_cp);
    for (const auto& candidate : completions) {
      if (candidate.text == target) {
        best_weight = std::max(best_weight, candidate.weight);
      }
    }
    if (best_weight > 0) return best_weight;
  }

  return 0;
}

// ============================================================================
// ExtractAllWords — full DFS multi-level extraction
// ============================================================================

bool OneNc7Trie::ExtractAllWords(std::vector<std::string>* words,
                                 std::vector<int32_t>* weights,
                                 size_t max_words) const {
  if (!loaded_ || !words) return false;

  const size_t limit =
      (max_words != 0) ? max_words : std::numeric_limits<size_t>::max();

  // Iterative DFS with explicit stack. Each frame stores (node_id, text, child_idx).
  struct Frame {
    uint64_t node_id;
    std::string text;
    size_t child_idx = 0;
  };

  std::vector<Frame> stack;
  words->clear();
  if (weights) weights->clear();

  // Seed with root children.
  for (const uint64_t child : Children(0)) {
    std::string text;
    if (!level_views_.empty() && level_views_[0].LabelToUtf8(child, &text)) {
      stack.push_back({child, std::move(text), 0});
    } else {
      char32_t cp = 0;
      if (!Cp1251ToCodepoint(LabelByte(child), &cp)) continue;
      AppendUtf8(cp, &text);
      stack.push_back({child, std::move(text), 0});
    }
  }

  while (!stack.empty() && words->size() < limit) {
    Frame& frame = stack.back();

    // If this is the first visit to this frame, check terminal.
    if (frame.child_idx == 0) {
      const bool is_term = level_views_.empty()
                               ? IsTerminal(frame.node_id)
                               : level_views_[0].IsTerminal(frame.node_id);
      if (is_term && !frame.text.empty()) {
        words->push_back(frame.text);
        if (weights) {
          int32_t w = kDefaultWeight;
          if (!level_views_.empty()) {
            w = TerminalWeightForLevel(level_views_[0], frame.node_id);
          } else if (!levels_.empty()) {
            uint64_t key = 0;
            if (RootTerminalKey(frame.node_id, &key)) {
              w = static_cast<int32_t>(key & 0xFFFF);
              if (w < kMinWeight) w = kDefaultWeight;
            }
          }
          weights->push_back(w);
        }
        if (words->size() >= limit) break;
      }
    }

    // Get children list (lazily computed once).
    const auto kids = level_views_.empty()
                          ? Children(frame.node_id)
                          : level_views_[0].Children(frame.node_id);

    if (frame.child_idx < kids.size()) {
      const uint64_t child = kids[frame.child_idx];
      ++frame.child_idx;

      std::string child_text = frame.text;
      bool label_ok = false;
      if (!level_views_.empty()) {
        label_ok = level_views_[0].LabelToUtf8(child, &child_text);
      }
      if (!label_ok) {
        char32_t cp = 0;
        if (!Cp1251ToCodepoint(LabelByte(child), &cp)) continue;
        child_text = frame.text;
        AppendUtf8(cp, &child_text);
      }
      stack.push_back({child, std::move(child_text), 0});
    } else {
      stack.pop_back();
    }
  }

  total_word_count_ = words->size();
  return words->size() > 0;
}

// ============================================================================
// Codepage helpers
// ============================================================================

bool OneNc7Trie::Cp1251ToCodepoint(uint8_t cp1251, char32_t* out) {
  if (!out) return false;
  if (cp1251 >= 0xE0 && cp1251 <= 0xFF) {
    *out = U'а' + static_cast<char32_t>(cp1251 - 0xE0);
    return true;
  }
  if (cp1251 == 0xB8) {
    *out = U'ё';
    return true;
  }
  return false;
}

bool OneNc7Trie::CodepointToCp1251Lower(char32_t codepoint, uint8_t* out) {
  if (!out) return false;
  if (codepoint >= U'А' && codepoint <= U'Я') {
    codepoint = U'а' + (codepoint - U'А');
  } else if (codepoint == U'Ё') {
    codepoint = U'ё';
  }
  if (codepoint >= U'а' && codepoint <= U'я') {
    *out = static_cast<uint8_t>(0xE0 + (codepoint - U'а'));
    return true;
  }
  if (codepoint == U'ё') {
    *out = 0xB8;
    return true;
  }
  return false;
}

bool OneNc7Trie::DecodePrefixToCp1251(std::string_view utf8,
                                      std::vector<uint8_t>* out) {
  if (!out) return false;
  out->clear();
  size_t pos = 0;
  while (pos < utf8.size()) {
    char32_t cp = 0;
    if (!DecodeOneUtf8(utf8, &pos, &cp)) return false;
    uint8_t label = 0;
    if (!CodepointToCp1251Lower(cp, &label)) return false;
    out->push_back(label);
  }
  return true;
}

std::string OneNc7Trie::CodepointsToUtf8(const std::vector<char32_t>& cps) {
  std::string out;
  for (const char32_t cp : cps) {
    AppendUtf8(cp, &out);
  }
  return out;
}

std::string OneNc7Trie::SortedLettersToUtf8(
    const std::vector<char32_t>& letters) {
  std::vector<char32_t> unique = letters;
  std::sort(unique.begin(), unique.end());
  unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
  return CodepointsToUtf8(unique);
}

}  // namespace hoskey::engine
