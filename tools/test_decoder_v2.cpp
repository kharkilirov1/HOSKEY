// Standalone test for 1nc7 decoder v2
// Build: g++ -std=c++17 -O2 -o test_decoder_v2 test_decoder_v2.cpp

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <numeric>
#include <limits>
#include <stack>

// ===========================================================================
// Minimal Status implementation
// ===========================================================================

struct Status {
  int code;
  std::string msg;
  
  static Status Ok() { return {0, ""}; }
  static Status ParseError(const char* m) { return {1, m}; }
  static Status NotImplemented(const char* m) { return {2, m}; }
  static Status IOError(const char* m) { return {3, m}; }
  
  bool ok() const { return code == 0; }
  const char* ToString() const { return msg.empty() ? "OK" : msg.c_str(); }
};

// ===========================================================================
// Data Structures (copied from succinct_trie.h)
// ===========================================================================

struct Trie1nc7Header {
  uint32_t magic;
  uint32_t version;
  uint32_t payload_hint;
  uint32_t u12;
  uint32_t item_count;
  uint32_t reserved[3];
};

struct RankedBitvector {
  uint64_t num_words = 0;
  uint64_t field1 = 0;
  uint64_t popcount = 0;
  const uint8_t* rank_data = nullptr;
  size_t rank_size = 0;
  const uint64_t* bits = nullptr;
  uint64_t bits_count = 0;
  
  uint64_t GetBit(uint64_t pos) const {
    if (!bits || pos >= num_words * 64) return 0;
    return (bits[pos / 64] >> (pos % 64)) & 1ULL;
  }
  
  uint64_t Rank1(uint64_t pos) const {
    if (!bits || !rank_data || pos == 0) return 0;
    if (pos >= num_words * 64) return popcount;
    
    uint64_t word_idx = pos / 64;
    uint64_t sb_idx = word_idx / 8;
    uint64_t word_in_sb = word_idx % 8;
    
    const uint8_t* sb_base = rank_data + sb_idx * 16;
    
    uint64_t l1 = 0;
    std::memcpy(&l1, sb_base, 8);
    
    uint64_t l2_sum = 0;
    if (word_in_sb > 0) {
      uint64_t packed_l2 = 0;
      std::memcpy(&packed_l2, sb_base + 8, 8);
      l2_sum = (packed_l2 >> ((word_in_sb - 1) * 9)) & 0x1FFULL;
    }
    
    uint64_t bit_in_word = pos % 64;
    uint64_t mask = (bit_in_word == 0) ? 0ULL : ((1ULL << bit_in_word) - 1);
    uint64_t partial = __builtin_popcountll(bits[word_idx] & mask);
    
    return l1 + l2_sum + partial;
  }
  
  uint64_t Select1(uint64_t n) const {
    if (!bits || !rank_data || n >= popcount) {
      return std::numeric_limits<uint64_t>::max();
    }
    
    uint64_t num_superblocks = (num_words + 7) / 8;
    uint64_t sb_lo = 0;
    uint64_t sb_hi = num_superblocks;
    
    while (sb_lo + 1 < sb_hi) {
      uint64_t mid = sb_lo + (sb_hi - sb_lo) / 2;
      uint64_t l1_mid = 0;
      std::memcpy(&l1_mid, rank_data + mid * 16, 8);
      if (l1_mid <= n) sb_lo = mid;
      else sb_hi = mid;
    }
    
    uint64_t l1 = 0;
    std::memcpy(&l1, rank_data + sb_lo * 16, 8);
    uint64_t remaining = n - l1;
    
    uint64_t base_word = sb_lo * 8;
    uint64_t words_in_sb = std::min<uint64_t>(8, num_words - base_word);
    
    for (uint64_t w = 0; w < words_in_sb; ++w) {
      uint64_t word = bits[base_word + w];
      uint64_t wpc = __builtin_popcountll(word);
      
      if (wpc > remaining) {
        uint64_t r = remaining;
        for (uint64_t bit = 0; bit < 64; ++bit) {
          if ((word >> bit) & 1ULL) {
            if (r == 0) return (base_word + w) * 64 + bit;
            --r;
          }
        }
      }
      remaining -= wpc;
    }
    
    return std::numeric_limits<uint64_t>::max();
  }
  
  uint64_t Select0(uint64_t n) const {
    // Find position of n-th 0-bit (0-indexed)
    if (!bits || n >= (num_words * 64 - popcount)) {
      return std::numeric_limits<uint64_t>::max();
    }
    
    // Linear scan (can be optimized with index)
    uint64_t count = 0;
    for (uint64_t pos = 0; pos < num_words * 64; ++pos) {
      if (!GetBit(pos)) {
        if (count == n) return pos;
        ++count;
      }
    }
    return std::numeric_limits<uint64_t>::max();
  }
};

struct LabelsArray {
  uint64_t bit_width = 8;
  uint64_t count = 0;
  const uint8_t* data = nullptr;
  size_t data_size = 0;
};

struct TrieLevel {
  RankedBitvector louds;
  RankedBitvector has_child;
  LabelsArray labels;
  bool valid() const { return labels.data != nullptr && labels.count > 0; }
};

struct LabelCodec {
  char32_t table[256];
  void BuildCP866Table() {
    for (int i = 0; i < 256; ++i) table[i] = 0xFFFD;
    for (int i = 0; i < 32; ++i) table[i] = static_cast<char32_t>(0x0410 + i);
    for (int i = 32; i < 48; ++i) table[i] = static_cast<char32_t>(0x0430 + (i - 32));
    for (int i = 96; i < 112; ++i) table[i] = static_cast<char32_t>(0x0440 + (i - 96));
    table[112] = 0x0401; table[113] = 0x0451;
  }
  char32_t Decode(uint8_t id) const { return table[id]; }
};

// ===========================================================================
// Succinct Trie Decoder
// ===========================================================================

class SuccinctTrie {
 public:
  Status Parse(const uint8_t* data, size_t size);
  const std::vector<std::string>& words() const { return words_; }
  const std::string& notes() const { return notes_; }
  
 private:
  Trie1nc7Header header_{};
  TrieLevel main_level_;
  TrieLevel sub_level_;
  bool has_sub_level_ = false;
  LabelCodec codec_;
  std::vector<std::string> words_;
  std::string notes_;
  
  Status ParseHeader(const uint8_t* data, size_t size);
  Status ExtractWords();
  static std::string ToUtf8(const std::vector<char32_t>& codepoints);
};

Status SuccinctTrie::ParseHeader(const uint8_t* data, size_t size) {
  if (size < 24) return Status::ParseError("Size < 24");
  std::memcpy(&header_, data, sizeof(Trie1nc7Header));
  if (header_.magic != 0x37636E31) {
    return Status::ParseError("Bad magic (not 1nc7)");
  }
  notes_ += "1nc7: version=" + std::to_string(header_.version) +
           " items=" + std::to_string(header_.item_count) +
           " payload=" + std::to_string(header_.payload_hint) + "\n";
  return Status::Ok();
}

std::string SuccinctTrie::ToUtf8(const std::vector<char32_t>& codepoints) {
  std::string result;
  result.reserve(codepoints.size() * 2);
  for (char32_t cp : codepoints) {
    if (cp <= 0x7F) {
      result.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
      result.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      result.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  return result;
}

Status SuccinctTrie::Parse(const uint8_t* data, size_t size) {
  auto st = ParseHeader(data, size);
  if (!st.ok()) return st;
  
  codec_.BuildCP866Table();
  
  const uint8_t* end = data + size;
  
  // Find all BV headers
  std::vector<size_t> bv_offsets;
  const uint8_t* scan = data + 24;
  while (scan + 32 <= end && bv_offsets.size() < 8) {
    uint64_t v0 = 0, v1 = 0;
    std::memcpy(&v0, scan, 8);
    std::memcpy(&v1, scan + 8, 8);
    if (v0 == header_.item_count && v1 == 3904) {
      bv_offsets.push_back(scan - data);
      scan += 32;
    } else {
      scan += 8;
    }
  }
  
  notes_ += "Found " + std::to_string(bv_offsets.size()) + " BVs\n";
  
  if (bv_offsets.size() < 4) {
    return Status::ParseError("Need at least 4 BVs");
  }
  
  const size_t bits_section_size = 124992;
  std::vector<RankedBitvector> bvs;
  
  for (size_t offset : bv_offsets) {
    const uint8_t* rank_ptr = data + offset;
    
    uint64_t num_words = 0, field1 = 0, popcount = 0;
    std::memcpy(&num_words, rank_ptr, 8);
    std::memcpy(&field1, rank_ptr + 8, 8);
    std::memcpy(&popcount, rank_ptr + 16, 8);
    
    RankedBitvector bv;
    bv.num_words = num_words;
    bv.field1 = field1;
    bv.popcount = popcount;
    bv.rank_data = rank_ptr + 24;
    bv.rank_size = ((num_words + 7) / 8) * 16;
    
    size_t bits_offset = offset + 24 - bits_section_size;
    if (bits_offset < size) {
      bv.bits = reinterpret_cast<const uint64_t*>(data + bits_offset);
      bv.bits_count = num_words;
    }
    
    bvs.push_back(bv);
    notes_ += "BV pop=" + std::to_string(popcount) + "\n";
  }
  
  main_level_.louds = bvs[0];
  main_level_.has_child = bvs[1];
  
  if (bvs.size() >= 4) {
    sub_level_.louds = bvs[2];
    sub_level_.has_child = bvs[3];
    has_sub_level_ = true;
    notes_ += "Using sub-level BVs 2,3\n";
  }
  
  // Find labels
  const uint8_t* after_bv3 = data + bv_offsets.back() + 24 + bvs.back().rank_size;
  const uint8_t* label_start = nullptr;
  size_t label_count = 0;
  
  for (const uint8_t* p = after_bv3; p + 40 <= data + header_.payload_hint; p += 8) {
    uint64_t m0 = 0, m1 = 0, m2 = 0;
    std::memcpy(&m0, p, 8);
    std::memcpy(&m1, p + 8, 8);
    std::memcpy(&m2, p + 16, 8);
    
    if (m1 == 8 && m2 == 255 && m0 > 400000 && m0 < 410000) {
      label_start = p + 40;
      label_count = (data + header_.payload_hint) - label_start;
      notes_ += "Labels at offset " + std::to_string(label_start - data) +
               " count=" + std::to_string(label_count) + "\n";
      break;
    }
  }
  
  if (!label_start) {
    label_start = after_bv3 + 40;
    if (label_start < data + header_.payload_hint) {
      label_count = (data + header_.payload_hint) - label_start;
    }
  }
  
  if (label_start && label_count > 0) {
    sub_level_.labels.bit_width = 8;
    sub_level_.labels.count = label_count;
    sub_level_.labels.data = label_start;
    sub_level_.labels.data_size = label_count;
    notes_ += "Labels: count=" + std::to_string(label_count) + "\n";
  }
  
  st = ExtractWords();
  notes_ += "Total words: " + std::to_string(words_.size()) + "\n";
  
  return Status::Ok();
}

Status SuccinctTrie::ExtractWords() {
  words_.clear();
  
  if (!has_sub_level_ || !sub_level_.labels.data) {
    notes_ += "No sub-level or labels\n";
    return Status::Ok();
  }
  
  // Simpler approach: use has_child for structure, assume sequential node IDs
  // Children of node i are at positions determined by LOUDS select/rank
  //
  // In LOUDS, the tree is encoded as a sequence of bits where:
  // - Each node contributes "1" followed by one "1" per child, then "0"
  // - Or: positions of "1" bits in LOUDS mark node boundaries
  
  struct StackEntry {
    uint64_t node_id;
    std::vector<char32_t> path;
  };
  
  std::stack<StackEntry> stack;
  stack.push({0, {}});  // Start at root (node 0)
  
  const int MAX_WORDS = 2000000;
  const int MAX_DEPTH = 100;
  
  int iterations = 0;
  const int MAX_ITERATIONS = 100000000;
  
  // Track visited nodes to avoid cycles
  std::vector<bool> visited(sub_level_.labels.count, false);
  
  
  
  while (!stack.empty() && words_.size() < MAX_WORDS && iterations < MAX_ITERATIONS) {
    iterations++;
    StackEntry entry = stack.top();
    stack.pop();
    
    if (entry.node_id == 0 && iterations <= 2) {
      notes_ += "Processing root, has_children=" + 
               std::to_string(sub_level_.has_child.GetBit(0)) + "\n";
    }
    
    if (entry.node_id >= sub_level_.labels.count) {
      continue;
    }
    
    // Cycle detection
    if (visited[entry.node_id]) {
      continue;
    }
    visited[entry.node_id] = true;
    
    // Get label (except root) - packed with bit_width=6
    if (entry.node_id != 0) {
      // Read 8-bit label
      uint8_t label_id = sub_level_.labels.data[entry.node_id];
      
      // Map label to Unicode (alphabet index)
      if (label_id < 32) {
        entry.path.push_back(0x0430 + label_id);  // а-я (0-31)
      } else if (label_id == 32) {
        entry.path.push_back('-');  // hyphen
      } else if (label_id == 33) {
        entry.path.push_back(0x0451);  // ё
      } else if (label_id < 64) {
        entry.path.push_back('?');  // unknown
      } else if (label_id >= 64 && label_id < 96) {
        entry.path.push_back(0x0410 + label_id - 64);  // А-Я uppercase
      } else {
        entry.path.push_back('?');
      }
    }
    
    // Check if terminal (no children in has_child)
    bool has_children = sub_level_.has_child.GetBit(entry.node_id);

    
    if (!has_children && !entry.path.empty()) {
      words_.push_back(ToUtf8(entry.path));
      if (words_.size() <= 10) {
        notes_ += "  word[" + std::to_string(words_.size()-1) + "]: " + 
                 ToUtf8(entry.path) + "\n";
      }
    }
    
    // Get children using LOUDS navigation
    // Based on Ghidra FUN_00661926: 
    // rank = rank1(louds, node); child_start = select0(louds, rank) + 1
    if (has_children && entry.path.size() < MAX_DEPTH) {
      std::vector<uint64_t> children;
      
      uint64_t rank = sub_level_.louds.Rank1(entry.node_id);
      uint64_t child_start = sub_level_.louds.Select0(rank);
      

      if (child_start != std::numeric_limits<uint64_t>::max()) {
        child_start++;  // +1 for actual child position
        
        // Iterate through children (consecutive '1's in louds)
        uint64_t max_bits = sub_level_.louds.num_words * 64;
        uint64_t found_children = 0;
        for (uint64_t pos = child_start; pos < max_bits && pos < child_start + 100; ++pos) {
          if (!sub_level_.louds.GetBit(pos)) break;  // End of children
          
          uint64_t child_id = sub_level_.louds.Rank1(pos + 1);

          if (child_id > 0 && child_id < sub_level_.labels.count) {
            children.push_back(child_id);
            found_children++;
          }
          if (found_children >= 50) break;  // Limit children per node
        }
        if (entry.node_id <= 2) {
          notes_ += "  total children=" + std::to_string(found_children) + "\n";
        }
      }
      
      for (auto it = children.rbegin(); it != children.rend(); ++it) {
        stack.push({*it, entry.path});
      }
    }
  }
  
  if (iterations >= MAX_ITERATIONS) {
    notes_ += "WARNING: Max iterations reached, possible loop\n";
  }
  notes_ += "Iterations: " + std::to_string(iterations) + "\n";
  
  return Status::Ok();
}

// ===========================================================================
// Main
// ===========================================================================

int main(int argc, char** argv) {
  const char* path = "entry/src/main/resources/rawfile/dict/ru.hoskey.hosdict";
  if (argc > 1) path = argv[1];
  
  FILE* f = fopen(path, "rb");
  if (!f) {
    printf("Failed to open: %s\n", path);
    return 1;
  }
  
  fseek(f, 0, SEEK_END);
  size_t size = ftell(f);
  fseek(f, 0, SEEK_SET);
  
  std::vector<uint8_t> data(size);
  fread(data.data(), 1, size, f);
  fclose(f);
  
  printf("Loaded: %zu bytes\n", size);
  
  SuccinctTrie trie;
  Status st = trie.Parse(data.data(), size);
  
  printf("Parse result: %s\n", st.ToString());
  printf("Notes:\n%s\n", trie.notes().c_str());
  printf("\nExtracted %zu words\n", trie.words().size());
  
  if (!trie.words().empty()) {
    printf("\nFirst 20 words:\n");
    for (size_t i = 0; i < std::min(size_t(20), trie.words().size()); ++i) {
      printf("  %zu: %s\n", i + 1, trie.words()[i].c_str());
    }
    
    // Save to file
    FILE* out = fopen("extracted_words_v2.txt", "w");
    if (out) {
      for (const auto& w : trie.words()) {
        fprintf(out, "%s\n", w.c_str());
      }
      fclose(out);
      printf("\nSaved to extracted_words_v2.txt\n");
    }
  }
  
  return 0;
}
