/*
 * 1nc7 Succinct Trie - Implementation for HOSKEY
 * 
 * Verified logic:
 * - node_id is the BIT POSITION in LOUDS bitvector.
 * - children are found via Select1(LOUDS, Rank1(has_child, node_id + 1) - 1).
 * - labels are indexed by Rank1(LOUDS, node_id + 1) - 1.
 */

#include "succinct_trie.h"
#include <cstring>
#include <limits>
#include <algorithm>

namespace hoskey::engine {

// ============================================================================
// Ranked Bitvector
// ============================================================================

uint64_t RankedBitvector::GetBit(uint64_t pos) const {
  if (!bits || pos >= num_words * 64) return 0;
  return (bits[pos / 64] >> (pos % 64)) & 1ULL;
}

uint64_t RankedBitvector::Rank1(uint64_t pos) const {
  if (!bits || !rank_data || pos == 0) return 0;
  if (pos >= num_words * 64) return popcount;
  
  uint64_t word_idx = pos / 64;
  uint64_t sb_idx = word_idx / 8;
  uint64_t word_in_sb = word_idx % 8;
  
  const uint8_t* sb_base = rank_data + sb_idx * 16;
  uint64_t l1 = 0; std::memcpy(&l1, sb_base, 8);
  
  uint64_t l2_sum = 0;
  if (word_in_sb > 0) {
    uint64_t packed_l2 = 0; std::memcpy(&packed_l2, sb_base + 8, 8);
    l2_sum = (packed_l2 >> ((word_in_sb - 1) * 9)) & 0x1FFULL;
  }
  
  uint64_t bit_in_word = pos % 64;
  uint64_t mask = (bit_in_word == 0) ? 0ULL : ((1ULL << bit_in_word) - 1);
  return l1 + l2_sum + __builtin_popcountll(bits[word_idx] & mask);
}

uint64_t RankedBitvector::Select1(uint64_t n) const {
  if (!bits || !rank_data || n >= popcount) return std::numeric_limits<uint64_t>::max();
  
  uint64_t sb_lo = 0, sb_hi = (num_words + 7) / 8;
  while (sb_lo + 1 < sb_hi) {
    uint64_t mid = sb_lo + (sb_hi - sb_lo) / 2;
    uint64_t l1_mid = 0; std::memcpy(&l1_mid, rank_data + mid * 16, 8);
    if (l1_mid <= n) sb_lo = mid; else sb_hi = mid;
  }
  
  uint64_t l1 = 0; std::memcpy(&l1, rank_data + sb_lo * 16, 8);
  uint64_t remaining = n - l1;
  uint64_t packed_l2 = 0; std::memcpy(&packed_l2, rank_data + sb_lo * 16 + 8, 8);
  uint64_t w = 0;
  for (; w < 7; ++w) {
    uint64_t l2_val = (packed_l2 >> (w * 9)) & 0x1FFULL;
    if (l2_val > remaining) break;
  }
  
  uint64_t target_word_idx = sb_lo * 8 + w;
  if (w > 0) remaining -= (packed_l2 >> ((w - 1) * 9)) & 0x1FFULL;
  
  if (target_word_idx >= num_words) return std::numeric_limits<uint64_t>::max();
  
  uint64_t word = bits[target_word_idx];
  for (uint64_t k = 0; k < remaining; ++k) word &= word - 1;
  return target_word_idx * 64 + __builtin_ctzll(word);
}

uint64_t RankedBitvector::Select0(uint64_t n) const {
  // Linear scan for Select0 (rarely used in this 1nc7 flavor)
  uint64_t remaining = n + 1;
  for (uint64_t w = 0; w < num_words; ++w) {
    uint64_t word = ~bits[w];
    uint64_t zeros = __builtin_popcountll(word);
    if (zeros >= remaining) {
      for (uint64_t bit = 0; bit < 64; ++bit) {
        if ((word >> bit) & 1ULL) { if (--remaining == 0) return w * 64 + bit; }
      }
    }
    remaining -= zeros;
  }
  return std::numeric_limits<uint64_t>::max();
}

// ============================================================================
// Trie Level - LOUDS-DS 
// ============================================================================

uint8_t TrieLevel::GetLabel(uint64_t pos) const {
  uint64_t node_idx = louds.Rank1(pos + 1);
  if (node_idx == 0 || node_idx > labels_count) return 0xFF;  // 0xFF = invalid sentinel
  return labels[node_idx - 1];
}

bool TrieLevel::HasChildren(uint64_t pos) const {
  return has_child.GetBit(pos) != 0;
}

std::optional<uint64_t> TrieLevel::FirstChild(uint64_t pos) const {
  if (!has_child.GetBit(pos)) return std::nullopt;
  uint64_t rank = has_child.Rank1(pos + 1);
  uint64_t child_pos = louds.Select1(rank - 1);
  if (child_pos == std::numeric_limits<uint64_t>::max()) return std::nullopt;
  return child_pos;
}

std::optional<uint64_t> TrieLevel::NextSibling(uint64_t pos) const {
  uint64_t next = pos + 1;
  if (next < louds.num_words * 64 && louds.GetBit(next)) return next;
  return std::nullopt;
}

std::optional<uint64_t> TrieLevel::Parent(uint64_t pos) const {
  return std::nullopt;
}

// ============================================================================
// SuccinctTrie
// ============================================================================

static char32_t DecodeUTF8(const std::string& str, size_t& pos) {
  if (pos >= str.size()) return 0;
  unsigned char c = str[pos];
  if ((c & 0x80) == 0) { pos++; return c; }
  if ((c & 0xE0) == 0xC0 && pos + 1 < str.size()) {
    char32_t ch = ((c & 0x1F) << 6) | (str[pos + 1] & 0x3F);
    pos += 2; return ch;
  }
  if ((c & 0xF0) == 0xE0 && pos + 2 < str.size()) {
    char32_t ch = ((c & 0x0F) << 12) | ((str[pos + 1] & 0x3F) << 6) | (str[pos + 2] & 0x3F);
    pos += 3; return ch;
  }
  pos++; return 0;
}

static uint8_t Utf8ToLabelIndex(char32_t ch) {
  if (ch >= 0x0410 && ch <= 0x042F) return (uint8_t)(ch - 0x0410);      // А-Я: 0-31
  if (ch >= 0x0430 && ch <= 0x043F) return (uint8_t)(ch - 0x0430 + 32); // а-п: 32-47
  if (ch >= 0x0440 && ch <= 0x044F) return (uint8_t)(ch - 0x0440 + 96); // р-я: 96-111
  if (ch == 0x0401) return 112; // Ё
  if (ch == 0x0451) return 113; // ё
  if (ch == ' ' || ch == '-') return 48;
  return 255;
}

static char32_t LabelIndexToUtf8(uint8_t l) {
  if (l < 32) return 0x0410 + l;
  if (l >= 32 && l <= 47) return 0x0430 + (l - 32);
  if (l >= 96 && l <= 111) return 0x0440 + (l - 96);
  if (l == 112) return 0x0401;
  if (l == 113) return 0x0451;
  if (l == 48) return ' ';
  return 0;
}

Status SuccinctTrie::Parse(const uint8_t* data, size_t size, bool extract_words) {
  if (size < 24) return Status::ParseError("Too small");
  data_ = data; data_size_ = size;
  std::memcpy(&header_, data, sizeof(header_));
  if (header_.magic != 0x37636E31) return Status::ParseError("Magic mismatch");
  auto st = ParseBitvectors(); if (!st.ok()) return st;
  st = ParseLabels(); if (!st.ok()) return st;
  loaded_ = true; return Status::Ok();
}

Status SuccinctTrie::ParseBitvectors() {
  const size_t rank_size = 31232;
  const size_t N = header_.item_count;
  
  std::vector<size_t> h;
  // Bound the header scan by data_size_ (i + 16 <= data_size_) so a file shorter
  // than 32 bytes cannot underflow the loop limit into a huge value.
  for (size_t i = 0; i + 16 <= data_size_; i += 8) {
    uint64_t w0 = 0, w1 = 0;
    std::memcpy(&w0, data_ + i, 8);
    std::memcpy(&w1, data_ + i + 8, 8);
    if (w0 == N && w1 == 3904) {
      h.push_back(i); i += 32;
      if (h.size() >= 5) break;
    }
  }
  if (h.size() < 4) return Status::ParseError("BVs missing");

  auto load_bv = [&](size_t h_off, bool special_bv0 = false) -> RankedBitvector {
    RankedBitvector bv;
    // The bitvector header (popcount at +16, rank_data at +24) must fit.
    if (h_off + 24 > data_size_) return bv;
    bv.num_words = N;
    std::memcpy(&bv.popcount, data_ + h_off + 16, 8);
    bv.rank_data = data_ + h_off + 24;

    if (special_bv0) {
      if (40 <= data_size_) bv.bits = (const uint64_t*)(data_ + 40);  // BV0 bits per spec
    } else {
      // Find bits DataSection near header, bounded so the scan cannot read past
      // the end of the buffer.
      size_t scan = h_off + 24 + rank_size;
      for (size_t p = scan; p + 16 <= data_size_ && p < scan + 128; p += 8) {
        uint64_t w0 = 0, w1 = 0;
        std::memcpy(&w0, data_ + p, 8);
        std::memcpy(&w1, data_ + p + 8, 8);
        if (w0 == 999283 && w1 == N) {
          if (p + 32 <= data_size_) bv.bits = (const uint64_t*)(data_ + p + 32);
          break;
        }
      }
    }

    // num_words is the untrusted header item_count. Reject it (return an empty
    // bitvector, whose accessors return safe defaults) if the rank/bits spans it
    // drives would read past the buffer. Division avoids overflow in *16 / *8.
    const size_t rank_off = static_cast<size_t>(bv.rank_data - data_);
    const uint64_t superblocks = (bv.num_words + 7) / 8;
    bool ok = rank_off <= data_size_ &&
              superblocks <= (data_size_ - rank_off) / 16;
    if (ok && bv.bits) {
      const size_t bits_off =
          static_cast<size_t>(reinterpret_cast<const uint8_t*>(bv.bits) - data_);
      ok = bits_off <= data_size_ && bv.num_words <= (data_size_ - bits_off) / 8;
    }
    if (!ok) return RankedBitvector{};
    return bv;
  };

  main_level_.louds = load_bv(h[0], true);
  main_level_.has_child = load_bv(h[0]); // Fallback if headers are packed
  // Real assignment from spec
  sub_level_.louds = load_bv(h[1]);
  sub_level_.has_child = load_bv(h[2]);
  
  if (h.size() >= 4) {
    terminal_bv_ = load_bv(h[3]);
    has_terminal_bv_ = true;
  }
  
  return Status::Ok();
}

Status SuccinctTrie::ParseLabels() {
  size_t payload_hint = header_.payload_hint;
  // Clamp the payload hint into the buffer: the label pointer and count below
  // are derived from it, so it must never point past the end.
  if (payload_hint >= data_size_ || payload_hint < 600000) {
    payload_hint = std::min<size_t>(999284, data_size_);
  }
  for (size_t p = 600000; p + 24 <= data_size_ && p < payload_hint; p += 8) {
    uint64_t w1 = 0, w2 = 0;
    std::memcpy(&w1, data_ + p + 8, 8);
    std::memcpy(&w2, data_ + p + 16, 8);
    if (w1 == 8 && w2 == 255) {
      const size_t labels_off = p + 40;
      if (labels_off <= payload_hint) {
        sub_level_.labels = data_ + labels_off;
        sub_level_.labels_count = payload_hint - labels_off;
        return Status::Ok();
      }
    }
  }
  if (payload_hint <= data_size_) {
    sub_level_.labels = data_ + payload_hint;
    sub_level_.labels_count = data_size_ - payload_hint;
  }
  return Status::Ok();
}

std::optional<uint64_t> SuccinctTrie::FindPrefixNode(const std::string& utf8_str) const {
  if (!loaded_) return std::nullopt;
  uint64_t node = 0; size_t pos = 0;
  while (pos < utf8_str.size()) {
    char32_t ch = DecodeUTF8(utf8_str, pos);
    uint8_t target = Utf8ToLabelIndex(ch); if (target == 255) return std::nullopt;
    
    auto child = sub_level_.FirstChild(node);
    bool found = false;
    while (child) {
      if (sub_level_.GetLabel(*child) == target) { node = *child; found = true; break; }
      child = sub_level_.NextSibling(*child);
    }
    if (!found) return std::nullopt;
  }
  return node;
}

bool SuccinctTrie::IsValidPrefix(const std::string& utf8_str) const {
  return FindPrefixNode(utf8_str).has_value();
}

std::vector<std::string> SuccinctTrie::GetCompletions(const std::string& prefix, size_t max_depth) const {
  if (!loaded_) return {};
  auto node_opt = FindPrefixNode(prefix); if (!node_opt) return {};
  std::vector<std::string> results;
  struct Frame { uint64_t id; std::optional<uint64_t> curr_child; size_t depth; };
  std::vector<uint8_t> path; std::vector<Frame> stack;
  stack.push_back({*node_opt, sub_level_.FirstChild(*node_opt), 0});
  while (!stack.empty()) {
    auto& f = stack.back();
    if (f.curr_child && f.depth < max_depth) {
      uint64_t cid = *f.curr_child; f.curr_child = sub_level_.NextSibling(cid);
      path.push_back(sub_level_.GetLabel(cid));
      bool is_t = has_terminal_bv_ ? terminal_bv_.GetBit(cid) : !sub_level_.HasChildren(cid);
      if (is_t) {
        std::string s = "";
        for (uint8_t l : path) {
          char32_t c = LabelIndexToUtf8(l); if (c == 0) continue;
          if (c <= 0x7F) s.push_back((char)c);
          else { s.push_back((char)(0xC0 | (c >> 6))); s.push_back((char)(0x80 | (c & 0x3F))); }
        }
        if (!s.empty()) results.push_back(prefix + s);
        if (results.size() >= 15) return results;
      }
      stack.push_back({cid, sub_level_.FirstChild(cid), f.depth + 1});
    } else {
      stack.pop_back(); if (!path.empty()) path.pop_back();
    }
  }
  return results;
}

std::vector<uint32_t> SuccinctTrie::Tokenize(const std::string& utf8_str, size_t vocab_size) const { return {}; }
std::string SuccinctTrie::GetTokenString(uint32_t token_id) const { return ""; }
Status SuccinctTrie::ExtractWords() {
  if (!loaded_) return Status::InvalidState("Trie not loaded");

  words_.clear();
  weights_.clear();
  words_.reserve(150000);
  weights_.reserve(150000);

  struct Frame {
    uint64_t node;
    std::optional<uint64_t> child_iter;
    size_t depth;
  };

  std::vector<uint8_t> path_bytes;
  std::vector<Frame> stack;

  auto start = sub_level_.FirstChild(0);
  if (!start) {
    // Root may not have children in sub_level (structure starts at transition).
    // Fall back: use main_level_ for root entry.
    start = main_level_.FirstChild(0);
  }
  if (start) {
    stack.push_back({0, start, 0});
  }

  while (!stack.empty()) {
    auto& f = stack.back();

    if (f.child_iter) {
      uint64_t child = *f.child_iter;
      f.child_iter = sub_level_.NextSibling(child);

      uint8_t label = sub_level_.GetLabel(child);
      if (label == 0xFF) {  // 0xFF = invalid label sentinel
        // Try main_level_ GetLabel as fallback
        label = main_level_.GetLabel(child);
        if (label == 0xFF) {
          stack.pop_back();
          if (!path_bytes.empty()) path_bytes.pop_back();
          continue;
        }
      }
      path_bytes.push_back(label);

      // Terminal detection: prefer terminal_bv_, fallback to HasChildren
      bool is_terminal = has_terminal_bv_
          ? (terminal_bv_.GetBit(child) != 0)
          : !sub_level_.HasChildren(child);

      if (is_terminal) {
        std::string word;
        for (uint8_t l : path_bytes) {
          char32_t c = LabelIndexToUtf8(l);
          if (c == 0) continue;
          if (c <= 0x7F) word.push_back(static_cast<char>(c));
          else {
            word.push_back(static_cast<char>(0xC0 | (c >> 6)));
            word.push_back(static_cast<char>(0x80 | (c & 0x3F)));
          }
        }
        if (!word.empty()) {
          words_.push_back(word);
          weights_.push_back(1000);
        }
      }

      auto grandchild = sub_level_.FirstChild(child);
      if (grandchild) {
        stack.push_back({child, grandchild, f.depth + 1});
      } else {
        path_bytes.pop_back();
      }
    } else {
      stack.pop_back();
      if (!path_bytes.empty()) path_bytes.pop_back();
    }
  }

  notes_ += " extracted=" + std::to_string(words_.size()) + "w";
  return Status::Ok();
}

}  // namespace hoskey::engine
