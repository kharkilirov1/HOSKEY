# Yandex Quality Parity — Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Achieve full 5-layer suggestion quality parity with Yandex Keyboard for tap input.

**Architecture:** 3-stream hybrid — Stream A (1nc7 trie decoder) and Stream B (tfmtrie + normdict + lemmer) run in parallel. Stream C (ML models) follows after A+B. Final pipeline assembly integrates all layers.

**Tech Stack:** C++17, HarmonyOS NDK, MindSpore Lite, NAPI, ArkTS

**Spec:** `docs/superpowers/specs/2026-03-31-yandex-quality-parity-design.md`

---

## Chunk 1: Stream A — 1nc7 Succinct Trie Decoder

### Reference Documents
- `docs/1NC7_FORMAT_DECODED.md` — section layout, bitvector roles, traversal algorithm
- `C:\Users\Kharki\clawd\v600_analysis\FINAL_FORMAT_SPEC.md` — struct offsets, CP866 mapping
- `C:\Users\Kharki\clawd\v600_analysis\KEY_FUNCTIONS_ANALYSIS.md` — rank/select/label decompiled functions
- `reverse/TRIE_AND_LEXICON_WORKING_SPEC_2026-03-25.md` — file format overview

### File Map
- **Create:** `entry/src/main/cpp/hosdict_engine/succinct_trie.h`
- **Create:** `entry/src/main/cpp/hosdict_engine/succinct_trie.cpp`
- **Modify:** `entry/src/main/cpp/CMakeLists.txt` — add succinct_trie.cpp to sources
- **Modify:** `entry/src/main/cpp/hosdict_engine/asset_manifest.h` — add kSuccinctTrie to AssetKind
- **Modify:** `entry/src/main/cpp/hosdict_engine/asset_manifest.cpp` — add succinct_lexicon_v1 format mapping
- **Modify:** `entry/src/main/cpp/hosdict_engine/hosdict_package.cpp` — add succinct_lexicon_v1 format handler (format dispatch lives here, NOT in loaders.cpp)
- **Verify:** `entry/src/main/cpp/hosdict_engine/keyboard_engine.cpp:127` — `FindWordList("lexicon.default")` already picks up the succinct trie result; no code change needed
- **Test data:** `entry/src/main/resources/rawfile/dict/` — trie asset inside ru.hoskey.hosdict

---

### Task A1: Create SuccinctTrie header with core data structures

**Files:**
- Create: `entry/src/main/cpp/hosdict_engine/succinct_trie.h`

- [ ] **Step 1: Create header file with bitvector and trie structs**

```cpp
// entry/src/main/cpp/hosdict_engine/succinct_trie.h
#pragma once
#include "status.h"
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace hoskey::engine {

// 1nc7 file header (24 bytes)
struct Trie1nc7Header {
    uint32_t magic;          // 0x37636E31 "1nc7"
    uint32_t version;        // 1
    uint32_t payload_hint;   // byte offset where tail begins
    uint32_t reserved0;
    uint32_t item_count;     // number of entries in trie
    uint32_t reserved1;
};

// Bitvector with rank acceleration (from Ghidra: FUN_00661806)
struct RankedBitvector {
    uint64_t num_words = 0;      // number of 64-bit words
    uint64_t field1 = 0;         // rank config (observed: 3904)
    uint64_t popcount = 0;       // total 1-bits
    const uint8_t* rank_data = nullptr;  // L1/L2 at 16-byte stride
    size_t rank_size = 0;
    const uint64_t* bits = nullptr;      // actual bitvector words
    size_t bits_count = 0;               // number of u64 words in bits

    // Rank1: count 1-bits in positions [0, pos)
    // From FUN_00661538 decompilation
    uint64_t Rank1(uint64_t pos) const;

    // Select1: find position of the n-th 1-bit (0-based)
    // From FUN_00661646
    uint64_t Select1(uint64_t n) const;

    // Test single bit
    bool GetBit(uint64_t pos) const;
};

// Data section (from FUN_006615a4)
struct DataSection {
    uint64_t field0 = 0;
    uint64_t count = 0;
    uint64_t field2 = 0;
    const uint64_t* data = nullptr;
};

// IntVector — packed array with variable bit width (from FUN_0066084e)
struct IntVector {
    uint8_t bit_width = 0;   // bits per element (5-8 typical)
    uint64_t count = 0;      // number of elements
    const uint8_t* data = nullptr;
    size_t data_size = 0;

    // Get element at index
    uint64_t Get(uint64_t index) const;
};

// One trie level (main or sub-trie)
struct TrieLevel {
    RankedBitvector louds;       // LOUDS bitvector (child structure)
    RankedBitvector has_child;   // has_child markers
    DataSection louds_data;
    DataSection has_child_data;
    IntVector labels;            // label indices per node
    IntVector weights;           // weight values per node
    DataSection labels_data;
    DataSection weights_data;
};

// CP866 label-to-Unicode mapping
// From FINAL_FORMAT_SPEC.md:
//   0-31: А-Я, 32-47: а-п, 96-111: р-я, 112-113: Ё/ё
struct LabelCodec {
    char32_t table[256] = {};

    void BuildCP866Table();
    char32_t Decode(uint8_t label_id) const;
};

class SuccinctTrie {
public:
    // Parse entire 1nc7 file
    Status Parse(const uint8_t* data, size_t size);

    // Results after parse
    const std::vector<std::string>& words() const { return words_; }
    const std::vector<int32_t>& weights() const { return weights_; }
    size_t word_count() const { return words_.size(); }
    bool is_parsed() const { return parsed_; }
    const std::vector<std::string>& notes() const { return notes_; }

private:
    // Sequential parse phases
    Status ParseHeader(const uint8_t* data, size_t size);
    Status ParseLevel(const uint8_t*& cursor, const uint8_t* end, TrieLevel& level);
    Status ParseBitvector(const uint8_t*& cursor, const uint8_t* end, RankedBitvector& bv);
    Status ParseDataSection(const uint8_t*& cursor, const uint8_t* end, DataSection& ds);
    Status ParseIntVector(const uint8_t*& cursor, const uint8_t* end, IntVector& iv);

    // DFS word extraction using LOUDS traversal
    void ExtractWords();
    void DFS(const TrieLevel& level, uint64_t node_id,
             std::vector<char32_t>& path);

    // Encode codepoints to UTF-8
    static std::string ToUtf8(const std::vector<char32_t>& codepoints);

    Trie1nc7Header header_ = {};
    TrieLevel main_level_;
    TrieLevel sub_level_;
    bool has_sub_level_ = false;
    LabelCodec codec_;

    std::vector<std::string> words_;
    std::vector<int32_t> weights_;
    bool parsed_ = false;
    std::vector<std::string> notes_;
};

}  // namespace hoskey::engine
```

- [ ] **Step 2: Verify file compiles (header-only check)**

Add a minimal .cpp that includes the header to confirm no syntax errors:
```cpp
// entry/src/main/cpp/hosdict_engine/succinct_trie.cpp
#include "succinct_trie.h"
namespace hoskey::engine {
// Implementation follows in subsequent tasks
}
```

- [ ] **Step 3: Add to CMakeLists.txt**

In `entry/src/main/cpp/CMakeLists.txt`, add `hosdict_engine/succinct_trie.cpp` to the source list after `ml_ranker.cpp`.

- [ ] **Step 4: Verify build compiles**

Run: `cd entry/src/main/cpp && cmake -B build_test -DCMAKE_CXX_STANDARD=17` (or full hvigor build)
Expected: no compilation errors.

- [ ] **Step 5: Commit**

```
feat: add SuccinctTrie header with 1nc7 data structures
```

---

### Task A2: Implement CP866 LabelCodec and basic utilities

**Files:**
- Modify: `entry/src/main/cpp/hosdict_engine/succinct_trie.cpp`

- [ ] **Step 1: Implement LabelCodec::BuildCP866Table()**

```cpp
void LabelCodec::BuildCP866Table() {
    // Initialize all to replacement char
    for (int i = 0; i < 256; i++) table[i] = 0xFFFD;

    // 0-31: А-Я (uppercase Cyrillic, U+0410-U+042F)
    for (int i = 0; i <= 31; i++) table[i] = 0x0410 + i;

    // 32-47: а-п (lowercase first half, U+0430-U+043F)
    for (int i = 32; i <= 47; i++) table[i] = 0x0430 + (i - 32);

    // 96-111: р-я (lowercase second half, U+0440-U+044F)
    for (int i = 96; i <= 111; i++) table[i] = 0x0440 + (i - 96);

    // 112: Ё (U+0401), 113: ё (U+0451)
    table[112] = 0x0401;
    table[113] = 0x0451;

    // Common punctuation/symbols at known positions
    table[45] = U'-';   // hyphen (if used)
}

char32_t LabelCodec::Decode(uint8_t label_id) const {
    return table[label_id];
}
```

- [ ] **Step 2: Implement ToUtf8()**

```cpp
std::string SuccinctTrie::ToUtf8(const std::vector<char32_t>& codepoints) {
    std::string result;
    result.reserve(codepoints.size() * 2);
    for (char32_t cp : codepoints) {
        if (cp < 0x80) {
            result += static_cast<char>(cp);
        } else if (cp < 0x800) {
            result += static_cast<char>(0xC0 | (cp >> 6));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            result += static_cast<char>(0xE0 | (cp >> 12));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            result += static_cast<char>(0xF0 | (cp >> 18));
            result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return result;
}
```

- [ ] **Step 3: Implement RankedBitvector::GetBit()**

```cpp
bool RankedBitvector::GetBit(uint64_t pos) const {
    if (pos >= num_words * 64) return false;
    uint64_t word_idx = pos / 64;
    uint64_t bit_idx = pos % 64;
    return (bits[word_idx] >> bit_idx) & 1;
}
```

- [ ] **Step 4: Commit**

```
feat: implement CP866 label codec and bitvector utilities
```

---

### Task A3: Implement Rank1 and Select1 operations

**Files:**
- Modify: `entry/src/main/cpp/hosdict_engine/succinct_trie.cpp`

Reference: `KEY_FUNCTIONS_ANALYSIS.md` — FUN_00661538 (rank), FUN_00661646 (select)

- [ ] **Step 1: Implement Rank1 from Ghidra decompilation**

```cpp
uint64_t RankedBitvector::Rank1(uint64_t pos) const {
    if (pos == 0) return 0;
    if (pos >= num_words * 64) return popcount;

    uint64_t word_idx = pos / 64;
    uint64_t bit_idx = pos % 64;

    // Superblock = 512 bits = 8 words
    // L1/L2 rank data at 16-byte stride per superblock
    uint64_t superblock_idx = pos / 512;

    // L1: cumulative popcount up to this superblock (u64 at stride*16)
    uint64_t L1 = 0;
    if (rank_data) {
        const uint64_t* rank_entry = reinterpret_cast<const uint64_t*>(
            rank_data + superblock_idx * 16);
        L1 = rank_entry[0];

        // L2: packed 9-bit sub-counts within superblock
        // word_in_superblock ranges 0-7, L2 for words 1-7
        uint64_t word_in_sb = (pos / 64) % 8;
        uint64_t L2 = 0;
        if (word_in_sb > 0) {
            uint64_t packed_L2 = rank_entry[1];
            L2 = (packed_L2 >> ((word_in_sb - 1) * 9)) & 0x1FF;
        }

        // Popcount of partial word
        uint64_t mask = (bit_idx == 0) ? 0 : ((uint64_t(1) << bit_idx) - 1);
        uint64_t pc = __builtin_popcountll(bits[word_idx] & mask);

        return L1 + L2 + pc;
    }

    // Fallback: brute force (no rank table)
    uint64_t count = 0;
    for (uint64_t i = 0; i < word_idx; i++) {
        count += __builtin_popcountll(bits[i]);
    }
    uint64_t mask = (bit_idx == 0) ? 0 : ((uint64_t(1) << bit_idx) - 1);
    count += __builtin_popcountll(bits[word_idx] & mask);
    return count;
}
```

- [ ] **Step 2: Implement Select1**

```cpp
uint64_t RankedBitvector::Select1(uint64_t n) const {
    if (n >= popcount) return num_words * 64; // out of range

    // Binary search on L1 entries to find superblock
    uint64_t num_superblocks = (num_words + 7) / 8;
    uint64_t lo = 0, hi = num_superblocks;

    while (lo < hi) {
        uint64_t mid = lo + (hi - lo) / 2;
        const uint64_t* rank_entry = reinterpret_cast<const uint64_t*>(
            rank_data + mid * 16);
        if (rank_entry[0] <= n) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    uint64_t sb = (lo > 0) ? lo - 1 : 0;

    // Get L1 count at start of this superblock
    const uint64_t* rank_entry = reinterpret_cast<const uint64_t*>(
        rank_data + sb * 16);
    uint64_t remaining = n - rank_entry[0];

    // Scan words within superblock
    uint64_t base_word = sb * 8;
    for (uint64_t w = base_word; w < num_words && w < base_word + 8; w++) {
        uint64_t pc = __builtin_popcountll(bits[w]);
        if (remaining < pc) {
            // Find the exact bit position within this word
            uint64_t word = bits[w];
            for (uint64_t b = 0; b < 64; b++) {
                if ((word >> b) & 1) {
                    if (remaining == 0) return w * 64 + b;
                    remaining--;
                }
            }
        }
        remaining -= pc;
    }
    return num_words * 64; // not found
}
```

- [ ] **Step 3: Implement IntVector::Get()**

```cpp
uint64_t IntVector::Get(uint64_t index) const {
    if (bit_width == 0 || index >= count) return 0;

    uint64_t bit_offset = index * bit_width;
    uint64_t byte_offset = bit_offset / 8;
    uint64_t bit_shift = bit_offset % 8;

    // Read up to 8 bytes starting at byte_offset
    uint64_t raw = 0;
    size_t bytes_to_read = (bit_width + bit_shift + 7) / 8;
    if (bytes_to_read > 8) bytes_to_read = 8;
    for (size_t i = 0; i < bytes_to_read && byte_offset + i < data_size; i++) {
        raw |= uint64_t(data[byte_offset + i]) << (i * 8);
    }

    uint64_t mask = (uint64_t(1) << bit_width) - 1;
    return (raw >> bit_shift) & mask;
}
```

- [ ] **Step 4: Commit**

```
feat: implement rank1, select1, and packed IntVector access
```

---

### Task A4: Implement sequential section parser

**Files:**
- Modify: `entry/src/main/cpp/hosdict_engine/succinct_trie.cpp`

Reference: `1NC7_FORMAT_DECODED.md` — section reading order from FUN_00660496

- [ ] **Step 1: Implement ParseHeader()**

```cpp
Status SuccinctTrie::ParseHeader(const uint8_t* data, size_t size) {
    if (size < 24) return Status::ParseError("File too small for 1nc7 header");

    memcpy(&header_, data, sizeof(Trie1nc7Header));

    if (header_.magic != 0x37636E31) {
        return Status::ParseError("Not a 1nc7 file (bad magic)");
    }
    if (header_.version != 1) {
        return Status::ParseError("Unsupported 1nc7 version: " +
                                  std::to_string(header_.version));
    }

    notes_.push_back("1nc7 header: version=" + std::to_string(header_.version) +
                      " payload_hint=" + std::to_string(header_.payload_hint) +
                      " item_count=" + std::to_string(header_.item_count));
    return Status::Ok();
}
```

- [ ] **Step 2: Implement ParseDataSection()**

```cpp
Status SuccinctTrie::ParseDataSection(const uint8_t*& cursor,
                                       const uint8_t* end,
                                       DataSection& ds) {
    if (cursor + 24 > end) return Status::ParseError("DataSection: not enough data for header");

    const uint64_t* hdr = reinterpret_cast<const uint64_t*>(cursor);
    ds.field0 = hdr[0];
    ds.count = hdr[1];
    ds.field2 = hdr[2];
    cursor += 24;

    size_t data_bytes = ds.count * 8 + 8;
    if (cursor + data_bytes > end) {
        return Status::ParseError("DataSection: not enough data for payload");
    }
    ds.data = reinterpret_cast<const uint64_t*>(cursor);
    cursor += data_bytes;

    return Status::Ok();
}
```

- [ ] **Step 3: Implement ParseBitvector()**

```cpp
Status SuccinctTrie::ParseBitvector(const uint8_t*& cursor,
                                     const uint8_t* end,
                                     RankedBitvector& bv) {
    // BV header: 32 bytes (4 x u64)
    if (cursor + 32 > end) return Status::ParseError("BV: not enough data for header");

    const uint64_t* hdr = reinterpret_cast<const uint64_t*>(cursor);
    bv.num_words = hdr[0];
    bv.field1 = hdr[1];
    bv.popcount = hdr[2];
    // hdr[3] is reserved (0)
    cursor += 32;

    // Rank data: 16-byte stride, ceil(num_words / 8) entries
    size_t num_superblocks = (bv.num_words + 7) / 8;
    size_t rank_bytes = num_superblocks * 16;
    if (cursor + rank_bytes > end) {
        return Status::ParseError("BV: not enough data for rank table");
    }
    bv.rank_data = cursor;
    bv.rank_size = rank_bytes;
    cursor += rank_bytes;

    // Bits: read as DataSection (FUN_006615a4 format)
    DataSection bits_ds;
    auto s = ParseDataSection(cursor, end, bits_ds);
    if (!s.ok()) return s;

    bv.bits = bits_ds.data;
    bv.bits_count = bits_ds.count;

    notes_.push_back("BV: num_words=" + std::to_string(bv.num_words) +
                      " popcount=" + std::to_string(bv.popcount));
    return Status::Ok();
}
```

- [ ] **Step 4: Implement ParseIntVector()**

```cpp
Status SuccinctTrie::ParseIntVector(const uint8_t*& cursor,
                                     const uint8_t* end,
                                     IntVector& iv) {
    // IntVector header: bitwidth(u64) + count(u64) = 16 bytes minimum
    if (cursor + 16 > end) return Status::ParseError("IntVector: not enough header");

    const uint64_t* hdr = reinterpret_cast<const uint64_t*>(cursor);
    uint64_t raw_bitwidth = hdr[0];
    if (raw_bitwidth >= 256) {
        return Status::ParseError("IntVector: bitwidth too large: " +
                                  std::to_string(raw_bitwidth));
    }
    iv.bit_width = static_cast<uint8_t>(raw_bitwidth);
    iv.count = hdr[1];
    cursor += 16;

    // Data: packed bits
    size_t total_bits = iv.count * iv.bit_width;
    size_t data_bytes = (total_bits + 7) / 8;
    // Align to 8 bytes
    data_bytes = (data_bytes + 7) & ~size_t(7);
    if (data_bytes < 8) data_bytes = 8;

    if (cursor + data_bytes > end) {
        return Status::ParseError("IntVector: not enough data");
    }
    iv.data = cursor;
    iv.data_size = data_bytes;
    cursor += data_bytes;

    notes_.push_back("IntVector: bit_width=" + std::to_string(iv.bit_width) +
                      " count=" + std::to_string(iv.count));
    return Status::Ok();
}
```

- [ ] **Step 5: Implement ParseLevel() — sequential section reader**

```cpp
Status SuccinctTrie::ParseLevel(const uint8_t*& cursor,
                                 const uint8_t* end,
                                 TrieLevel& level) {
    // Reading order from FUN_00660496:
    // 1. Bitvector 1 (LOUDS)
    auto s = ParseBitvector(cursor, end, level.louds);
    if (!s.ok()) return s;

    // 2. Data section for LOUDS
    s = ParseDataSection(cursor, end, level.louds_data);
    if (!s.ok()) return s;

    // 3. Bitvector 2 (has_child)
    s = ParseBitvector(cursor, end, level.has_child);
    if (!s.ok()) return s;

    // 4. Data section for has_child
    s = ParseDataSection(cursor, end, level.has_child_data);
    if (!s.ok()) return s;

    // 5. IntVector 1 (labels)
    s = ParseIntVector(cursor, end, level.labels);
    if (!s.ok()) return s;

    // 6. Data section for labels
    s = ParseDataSection(cursor, end, level.labels_data);
    if (!s.ok()) return s;

    // 7. IntVector 2 (weights)
    s = ParseIntVector(cursor, end, level.weights);
    if (!s.ok()) return s;

    // 8. Data section for weights
    s = ParseDataSection(cursor, end, level.weights_data);
    if (!s.ok()) return s;

    return Status::Ok();
}
```

- [ ] **Step 6: Implement Parse() — top-level**

```cpp
Status SuccinctTrie::Parse(const uint8_t* data, size_t size) {
    words_.clear();
    weights_.clear();
    notes_.clear();
    parsed_ = false;

    auto s = ParseHeader(data, size);
    if (!s.ok()) return s;

    codec_.BuildCP866Table();

    // First DataSection starts at offset 8 (after magic+version, before BV sections)
    // Per 1NC7_FORMAT_DECODED.md: file+8 is BV0 bits as DataSection
    const uint8_t* cursor = data + 8;
    const uint8_t* end = data + size;

    // Read pre-section DataSection (BV0 bits)
    DataSection pre_data;
    s = ParseDataSection(cursor, end, pre_data);
    if (!s.ok()) {
        notes_.push_back("Warning: pre-data section parse failed, trying offset 24");
        cursor = data + 24;
    }

    // Gap data (structure mapping, ~5792 bytes)
    // Skip to first BV header — scan for known pattern
    // BV headers start with num_words == header_.item_count
    while (cursor + 32 <= end) {
        const uint64_t* probe = reinterpret_cast<const uint64_t*>(cursor);
        if (probe[0] == header_.item_count && probe[1] == 3904) {
            break;  // Found BV header
        }
        cursor += 8;
    }

    if (cursor + 32 > end) {
        return Status::ParseError("Could not locate first BV section");
    }

    // Parse main level
    s = ParseLevel(cursor, end, main_level_);
    if (!s.ok()) return s;

    // Check if sub-level exists (remaining data before tail)
    size_t remaining = end - cursor;
    if (remaining > 1024 && cursor < data + header_.payload_hint) {
        // Try parsing sub-level
        auto sub_s = ParseLevel(cursor, end, sub_level_);
        if (sub_s.ok()) {
            has_sub_level_ = true;
            notes_.push_back("Sub-trie level parsed successfully");
        } else {
            notes_.push_back("Sub-trie parse failed: " + sub_s.message);
        }
    }

    // Extract words via DFS traversal
    ExtractWords();

    if (!words_.empty()) {
        parsed_ = true;
        notes_.push_back("Extracted " + std::to_string(words_.size()) + " words");
    } else {
        notes_.push_back("WARNING: No words extracted from trie");
    }

    return Status::Ok();
}
```

- [ ] **Step 7: Commit**

```
feat: implement 1nc7 sequential section parser
```

---

### Task A5: Implement DFS word extraction

**Files:**
- Modify: `entry/src/main/cpp/hosdict_engine/succinct_trie.cpp`

Reference: `1NC7_FORMAT_DECODED.md` — traversal algorithm from FUN_00661926

- [ ] **Step 1: Implement ExtractWords() and DFS()**

```cpp
void SuccinctTrie::ExtractWords() {
    // LOUDS traversal:
    // Root children start after root node in LOUDS
    // child = select1(LOUDS, rank1(has_child, node_id))
    // Terminal check: has_child BV or separate terminal bits

    const TrieLevel& level = main_level_;

    // Find root's children: in LOUDS, root is node 0
    // First child position = select0(louds, 0) + 1 = position after first 0
    // But simpler: root children are consecutive 1-bits at start of LOUDS

    std::vector<char32_t> path;
    DFS(level, 0, path);

    notes_.push_back("DFS traversal: visited nodes, found " +
                      std::to_string(words_.size()) + " words");
}

void SuccinctTrie::DFS(const TrieLevel& level, uint64_t node_id,
                        std::vector<char32_t>& path) {
    // Check terminal: if has_child bit is 0, this is a leaf
    // Or check weight: non-negative weight means terminal
    bool is_terminal = false;

    if (node_id < level.weights.count) {
        int32_t w = static_cast<int32_t>(level.weights.Get(node_id));
        if (w >= 0) {
            is_terminal = true;
        }
    }

    if (is_terminal && !path.empty()) {
        words_.push_back(ToUtf8(path));
        if (node_id < level.weights.count) {
            weights_.push_back(static_cast<int32_t>(level.weights.Get(node_id)));
        } else {
            weights_.push_back(0);
        }
    }

    // Find children using LOUDS
    if (!level.has_child.GetBit(node_id)) {
        return;  // No children (leaf)
    }

    // child_rank = rank1(has_child, node_id)
    uint64_t child_rank = level.has_child.Rank1(node_id);

    // First child position in LOUDS = select1(louds, child_rank)
    // But LOUDS encodes degrees: 1...10 for each node
    // Children of node N start at select0(louds, N) + 1
    // Number of children = next 0 position - start

    // Alternative interpretation: child_pos = select1(louds, child_rank)
    uint64_t child_start = level.louds.Select1(child_rank);

    // Iterate children until we hit a 0 in LOUDS
    for (uint64_t pos = child_start; pos < level.louds.num_words * 64; pos++) {
        if (!level.louds.GetBit(pos)) {
            break;  // End of children
        }

        // Child node ID = rank1(louds, pos + 1) - rank1 of 1-bits
        // Actually in LOUDS: child_node_id = rank0(louds, pos)
        // where rank0(pos) = pos - rank1(pos)
        uint64_t child_node = pos - level.louds.Rank1(pos);

        // Get label for child
        if (child_node < level.labels.count) {
            uint8_t label_id = static_cast<uint8_t>(level.labels.Get(child_node));
            char32_t ch = codec_.Decode(label_id);
            if (ch != 0xFFFD) {
                path.push_back(ch);
                DFS(level, child_node, path);
                path.pop_back();
            }
        }
    }
}
```

**Note:** The exact LOUDS traversal logic may need adjustment based on testing against the actual trie file. The relationship between node IDs, LOUDS positions, and label indices depends on whether the format uses standard LOUDS or a variant. The implementation above follows the most common convention. If word extraction produces garbage, the alternatives to try are:
1. Swap LOUDS and has_child roles
2. Use `select0` instead of `select1` for child lookup
3. Adjust node_id computation: `rank1(louds, pos)` instead of `rank0`

- [ ] **Step 2: Commit**

```
feat: implement DFS word extraction from 1nc7 trie
```

---

### Task A6: Wire SuccinctTrie into asset loading pipeline

**Files:**
- Modify: `entry/src/main/cpp/hosdict_engine/hosdict_package.cpp` — format dispatch is here (line 71-163), NOT in loaders.cpp
- Modify: `entry/src/main/cpp/hosdict_engine/asset_manifest.h` — add AssetKind
- Modify: `entry/src/main/cpp/hosdict_engine/asset_manifest.cpp` — fix routing

**Important context:** Format dispatching happens in `HosdictPackage::LoadContainerAssets()` at `hosdict_package.cpp:48-169`. Each format string (e.g. "weighted_wordlist_v1", "normdict_v2") has an `else if` branch. Word source selection is at `keyboard_engine.cpp:127`: `FindWordList("lexicon.default")` — no `GetDefaultWordSource()` function exists.

- [ ] **Step 1: Add kSuccinctTrie to AssetKind**

In `asset_manifest.h`, add to the enum:
```cpp
enum class AssetKind {
    kConfig, kLexicon, kRules, kNormDict, kLemmaHash,
    kLemmaRules, kPolicy, kFeatureConfig,
    kSuccinctTrie  // NEW: 1nc7 succinct trie lexicon
};
```

In `asset_manifest.cpp`, update `KindFromKey()` — use exact key match, NOT substring:
```cpp
// Add BEFORE the generic "lexicon" prefix check to avoid conflicts with tfmtrie:
if (key == "lexicon.default" || key == "lexicon.trie") return AssetKind::kSuccinctTrie;
```

- [ ] **Step 2: Add succinct trie format handler in hosdict_package.cpp**

Add `#include "succinct_trie.h"` at top of hosdict_package.cpp, then add a new `else if` branch in `LoadContainerAssets()` after the "utf8_wordlist_v1" branch (line ~94):

```cpp
    } else if (entry->format == "succinct_lexicon_v1" || entry->format == "1nc7") {
      SuccinctTrie trie;
      Status ps = trie.Parse(data, data_size);
      if (ps.ok() && trie.word_count() > 0) {
        LoadedWordListAsset wl;
        wl.descriptor = descriptor;
        wl.words = trie.words();
        wl.normalized_words = trie.words();  // same as words for initial load
        wl.weights = trie.weights();
        wl.notes = trie.notes();
        report.notes.push_back("succinct trie: " + std::to_string(trie.word_count()) + " words");
        store_.PutWordList(std::move(wl));
      } else {
        // Store as binary for diagnostics
        LoadedBinaryAsset bin;
        bin.descriptor = descriptor;
        bin.bytes.assign(data, data + data_size);
        bin.notes.push_back("succinct trie parse failed: " + ps.message);
        for (const auto& n : trie.notes()) bin.notes.push_back(n);
        report.notes = bin.notes;
        store_.PutBinary(std::move(bin));
      }
```

Note: `normalized_words` MUST be populated — the struct requires it and downstream code reads it. Use `trie.words()` as initial value; normalization (lowercase, ё→е) can be applied later.

- [ ] **Step 3: Verify word source selection**

In `keyboard_engine.cpp:127`, the code already does:
```cpp
const auto* lexicon = package_.store().FindWordList("lexicon.default");
if (lexicon && !lexicon->words.empty()) {
    predictor_.Build(lexicon->words, lexicon->weights);
}
```

This will automatically pick up the succinct trie result if the container's `lexicon.default` asset has format `succinct_lexicon_v1`. No change needed here — just verify the asset key in the .hosdict container is `lexicon.default`.

- [ ] **Step 4: Commit**

```
feat: wire SuccinctTrie into hosdict_package format dispatch
```

---

### Task A7: Test and iterate on trie decoder

**Files:**
- Modify: `entry/src/main/cpp/hosdict_engine/succinct_trie.cpp` (iterate based on test results)

- [ ] **Step 1: Build the project**

Run full hvigor build: `hvigor assembleApp -p product=default -p buildMode=debug`

- [ ] **Step 2: Check diagnostic output**

Deploy to device or check logs. The `notes_` vector will be output via `getLoadReports()` NAPI call. Check SetupGuidePage for:
- "1nc7 header: version=1 payload_hint=999284 item_count=15614"
- BV parse notes with popcount values
- Word count result

- [ ] **Step 3: Iterate on LOUDS traversal if needed**

If word count is 0 or words are garbage:
1. Check BV popcount values match expected (369,743 / 369,743 / 801,071 / 402,824)
2. Try swapping LOUDS ↔ has_child bitvector roles
3. Try different node_id computation in DFS
4. Add diagnostic: dump first 20 labels, first 20 weights, first few DFS paths

- [ ] **Step 4: Verify word quality**

Compare extracted words against `extracted_words.txt` (108,995 known good words). At least 80% overlap expected once decoder works correctly.

- [ ] **Step 5: Commit verified decoder**

```
fix: correct 1nc7 trie traversal after testing
```

---

## Chunk 2: Stream B — TfmTrie + NormDict + Lemmer

### Task B1: Verify and fix TfmTrie substitution class parsing

**Files:**
- Modify: `entry/src/main/cpp/hosdict_engine/tfmtrie_decoder.cpp`

Reference: `reverse/TFMTRIE_WORKING_SPEC_2026-03-25.md`

- [ ] **Step 1: Add diagnostic dump of parsed classes**

Add a method to TfmTrieDecoder to dump all substitution classes for verification:

```cpp
// In tfmtrie_decoder.h, add public method:
std::string DiagnosticDump() const;

// In tfmtrie_decoder.cpp:
std::string TfmTrieDecoder::DiagnosticDump() const {
    std::string result;
    result += "TfmTrie: " + std::to_string(classes_.size()) + " classes, " +
              std::to_string(rules_.size()) + " rules\n";
    for (const auto& cls : classes_) {
        // Encode anchor as UTF-8
        std::string anchor_utf8;
        // ... (use EncodeUtf8 from text_utils)
        result += "Anchor U+" + std::to_string(cls.anchor) + ": " +
                  std::to_string(cls.companions.size()) + " companions\n";
    }
    return result;
}
```

- [ ] **Step 2: Check parse results match JCUKEN layout**

Expected substitution classes for Russian JCUKEN:
- 'а' (U+0430) → companions: 'п','р','о','в' (neighbors on JCUKEN)
- 'е' (U+0435) → companions: 'н','к','у','г'
- etc.

Total expected: ~33 classes (each Russian letter + common punctuation)

If class_count() is 0 or companions don't match JCUKEN adjacency, the ParsePayload() logic needs fixing.

- [ ] **Step 3: Test GenerateCandidates()**

Manually test with known typo:
```cpp
auto candidates = tfmtrie_.GenerateCandidates("поивет", 1);
// Expected: "привет" should appear with score > 0.5
```

If the result is empty or wrong, check:
1. Are companions being looked up correctly (forward AND reverse)?
2. Is the word being split into codepoints properly?
3. Are substitution weights being applied?

- [ ] **Step 4: Fix any issues found and commit**

```
fix: verify and correct TfmTrie substitution class parsing
```

---

### Task B2: Verify and fix Lemmer CHD lookup + ApplyRule

**Files:**
- Modify: `entry/src/main/cpp/hosdict_engine/lemmer.cpp`

Reference: `reverse/EXPERIMENTAL_RULES_AND_LEMMER_WORKING_SPEC_2026-03-25.md`

- [ ] **Step 1: Add diagnostic for CHD lookup**

```cpp
// Add to lemmer.h:
std::string DiagnosticLookup(const std::string& word) const;

// In lemmer.cpp:
std::string Lemmer::DiagnosticLookup(const std::string& word) const {
    std::string result = "Lookup: " + word + "\n";
    uint32_t h1 = MurmurHash2(word, chd_header_.seed0) % chd_header_.bucket_count;
    result += "  h1 (MurmurHash2)=" + std::to_string(h1) + "\n";

    // Read displacement
    // ... (existing ChdLookup logic with logging)

    int32_t rule_idx = ChdLookup(word);
    result += "  rule_idx=" + std::to_string(rule_idx) + "\n";

    if (rule_idx >= 0) {
        std::string lemma = ApplyRule(word, rule_idx);
        result += "  lemma=" + lemma + "\n";
    }
    return result;
}
```

- [ ] **Step 2: Test with known Russian word forms**

Test cases:
- `Lemmatize("бежал")` → should return "бежать" or "бег"
- `Lemmatize("домов")` → should return "дом"
- `Lemmatize("красивая")` → should return "красивый"
- `Lemmatize("привет")` → should return "привет" (already a lemma)

If results are wrong, check:
1. Hash function correctness (MurmurHash2 seed, Fnv1aHash seed)
2. Displacement table entry size (u8/u16/u32 — depends on num_keys)
3. Rule application logic (suffix stripping + replacement)

- [ ] **Step 3: Verify ApplyRule() suffix logic**

The rule format from reverse spec: rules are compact symbolic programs. Each rule:
- Strips N characters from word suffix
- Appends replacement suffix

Verify the offset/size calculations in ApplyRule() match the actual binary layout.

- [ ] **Step 4: Fix and commit**

```
fix: verify and correct Lemmer CHD lookup and rule application
```

---

### Task B3: Implement NormDict frequency lookup (or contingency)

**Files:**
- Modify: `entry/src/main/cpp/hosdict_engine/normdict_reader.cpp`

- [ ] **Step 1: Inspect trie body format**

The NormDict trie body is at `trie_data_` (set during Load()). Check first bytes:

```cpp
// Add diagnostic in Load() after trie body is identified:
if (trie_body_size_ > 4) {
    uint32_t first4;
    memcpy(&first4, trie_data_, 4);
    notes_.push_back("NormDict trie body magic: 0x" + hex(first4));
    // Check for 1nc7, LOUDS markers, etc.
}
```

- [ ] **Step 2: If 1nc7 format — reuse SuccinctTrie**

```cpp
#include "succinct_trie.h"

uint32_t NormDictReader::GetSuggestionFrequency(const std::string& word) const {
    if (!trie_data_ || trie_body_size_ == 0) return 0;

    // Try parsing as succinct trie
    SuccinctTrie trie;
    auto s = trie.Parse(trie_data_, trie_body_size_);
    if (s.ok()) {
        // Lookup word in parsed trie
        // ... binary search or trie lookup
    }
    return 0;  // Not found
}
```

- [ ] **Step 3: If unknown format — apply contingency**

If the trie body is NOT 1nc7 format:
- Document the actual magic bytes in notes_
- Keep GetSuggestionFrequency() returning 0
- Use trie weights from Stream A (SuccinctTrie) as frequency proxy
- Add note: "NormDict trie format TBD — using trie weights as frequency proxy"

- [ ] **Step 4: Wire NormDictReader into KeyboardEngine**

In `keyboard_engine.h`:
1. Add `#include "normdict_reader.h"` at top (among existing includes)
2. Add private member: `NormDictReader normdict_;`

In `keyboard_engine.cpp` InitSubsystems() (after lemmer loading at line ~146), add:
```cpp
// 3b. Load NormDict
auto* normdict_asset = package_.store().FindBinary("normdict.default");
if (normdict_asset && !normdict_asset->bytes.empty()) {
    normdict_.Load(normdict_asset->bytes.data(), normdict_asset->bytes.size());
}
```

In GetSuggestions(), use normdict for normalization (replace existing NormalizeToken call):
```cpp
std::string normalized = normdict_.loaded()
    ? normdict_.Normalize(request.token)
    : NormalizeToken(request.token);
```

Note: `normdict_reader.h` declares the method as `loaded()` (verified in actual header at line 37).

- [ ] **Step 5: Commit**

```
feat: integrate NormDictReader into KeyboardEngine pipeline
```

---

### Task B4: Integration test for Stream B components

**Files:**
- Modify: `entry/src/main/cpp/napi_bridge/hosdict_bridge.cpp` (add diagnostic methods)

- [ ] **Step 1: Expose B-stream diagnostics via NAPI**

Add a `getDecoderDiagnostics()` NAPI function that returns:
- TfmTrie: class_count, rule_count, sample substitution classes
- Lemmer: hash_loaded, rules_loaded, sample lemmatizations
- NormDict: loaded, language, trie_format_version

- [ ] **Step 2: Test on device via SetupGuidePage**

Check that:
- TfmTrie reports > 30 substitution classes
- Lemmer reports hash_loaded=true, rules_loaded=true, num_keys=103803
- NormDict reports loaded=true, language=ru

- [ ] **Step 3: Test suggestion quality with typos**

Via getSuggestions():
- "поивет" → should include "привет" (tfmtrie correction)
- "бежал" → should include "бежать" forms (lemmer expansion)
- "вообщем" → should include "в общем" (autocorrect)

- [ ] **Step 4: Commit**

```
feat: add decoder diagnostics and verify Stream B integration
```

---

## Chunk 3: Stream C — ML Model Conversion + Integration

### Task C1: Analyze feature catalogs and model tensor shapes

**Files:**
- Feature catalogs at: `C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\features_*.json`
- Model files at: `C:\Users\Kharki\Desktop\yandex_extracted\ms_models\` and `C:\Users\Kharki\clawd\yandex_extracted\yandex_extracted\`

- [ ] **Step 1: Parse features_ranker.json — identify exact feature vector**

The ranker model expects 54 features (38 base + 5 combos + 11 keyboard-specific):
- Base: length, lm_ngr_main, lm_1gr_static, lm_2gr_static, lm_ngr_static, letter_lm_main, ndict (x2 for original+correction), distance_transfeme (x2), levenshtein, bleu_2-4, letter_precision_1-3, is_permutation, kendall_tau_distance, no_change, diacr_only
- Combos: distance+LM combinations, LM differences, length differences
- Keyboard: geometry_distance variants, neural_oov

Map each to our engine equivalent or mark as "needs approximation."

- [ ] **Step 2: Parse features_tap_model.json — identify tap feature vector**

36+5 = 41 features focused on:
- tap coordinates (tap_x, tap_y_m, letter_x, letter_y_m)
- language model scores (tap_log_likelihood_unigram, tap_log_likelihood_ngram)
- neural scores (neural_char_lm variants)
- context (context_prefix_len, has_continuation)

**Critical:** Tap model needs actual key coordinates — this requires touch position data from ArkTS keyboard.

- [ ] **Step 3: Parse features_autocc.json — identify autocorrect feature vector**

77 features. Most complex model. Many overlap with ranker.

- [ ] **Step 4: Determine which features we CAN compute now**

| Feature Category | Available? | Source |
|-----------------|-----------|--------|
| word length | Yes | string length |
| levenshtein | Yes | text_utils.cpp |
| is_permutation | Yes | compute |
| distance_transfeme | Yes | tfmtrie_decoder |
| ndict (in dictionary) | Yes | predictor_engine |
| lm_ngr_main | Partial | need neural_model inference |
| lm_1gr_static | Partial | need frequency data from trie |
| letter_lm_main | No | need char_model inference |
| geometry_distance | No | need key coordinate data |
| neural_char_lm | No | need char_model inference |
| tap coordinates | No | need touch event forwarding from ArkTS |
| bleu scores | Yes | compute from strings |

- [ ] **Step 5: Document feature mapping and save**

Write a feature mapping document listing each model's expected features and our computation strategy.

- [ ] **Step 6: Commit documentation**

```
docs: document ML feature mapping for ranker, tap, and autocorrect models
```

---

### Task C2: Convert TFLite models to MindSpore Lite

- [ ] **Step 1: Install MindSpore Lite converter**

Download `converter_lite` tool from MindSpore releases. Or use the version bundled with HarmonyOS SDK if available.

- [ ] **Step 2: Test operator coverage on each model**

```bash
converter_lite --fmk=TFLITE --modelFile=ranker.tflite --outputFile=ranker_test.ms
converter_lite --fmk=TFLITE --modelFile=tree_autocorrect_model.tflite --outputFile=autocc_test.ms
converter_lite --fmk=TFLITE --modelFile=char_model.tflite --outputFile=char_test.ms
converter_lite --fmk=TFLITE --modelFile=tap_model_ranker.tflite --outputFile=tap_test.ms
```

Log results: success/failure per model, any unsupported operators.

- [ ] **Step 3: Convert verified models**

For each successful conversion, verify output tensor shapes match expected feature counts.

- [ ] **Step 4: Copy neural_model.ms (already in .ms format)**

```bash
cp "C:\Users\Kharki\Desktop\yandex_extracted\ms_models\neural_model.ms" \
   "entry/src/main/resources/rawfile/models/"
```

- [ ] **Step 5: Copy all converted .ms models to rawfile/models/**

```bash
mkdir -p entry/src/main/resources/rawfile/models
cp ranker.ms entry/src/main/resources/rawfile/models/
cp tree_autocorrect_model.ms entry/src/main/resources/rawfile/models/
cp char_model.ms entry/src/main/resources/rawfile/models/
cp neural_model.ms entry/src/main/resources/rawfile/models/
# tap_model_ranker.ms — if conversion succeeded
```

- [ ] **Step 6: Commit**

```
feat: add converted MindSpore Lite models to rawfile
```

---

### Task C3: Expand MLRanker to load real models

**Files:**
- Modify: `entry/src/main/cpp/hosdict_engine/ml_ranker.h`
- Modify: `entry/src/main/cpp/hosdict_engine/ml_ranker.cpp`
- Modify: `entry/src/main/cpp/hosdict_engine/asset_manifest.h`

- [ ] **Step 1: Add model asset kinds**

In `asset_manifest.h`:
```cpp
enum class AssetKind {
    kConfig, kLexicon, kRules, kNormDict, kLemmaHash,
    kLemmaRules, kPolicy, kFeatureConfig, kSuccinctTrie,
    kRankerModel, kAutocorrectModel, kContextModel, kCharModel  // NEW
};
```

- [ ] **Step 2: Expand RankerFeatures based on feature catalog analysis**

Update `RankerFeatures` in `ml_ranker.h` to match Yandex's `features_ranker.json`:

```cpp
struct RankerFeatures {
    // Original word features
    float original_length = 0;
    float original_ndict = 0;        // 1.0 if in dictionary
    float original_lm_1gr_static = 0; // unigram frequency (log)

    // Correction features
    float correction_length = 0;
    float correction_ndict = 0;
    float correction_lm_1gr_static = 0;

    // Comparison
    float no_change = 0;     // 1.0 if original == correction
    float diacr_only = 0;    // 1.0 if only diacritics differ

    // Distance
    float distance_transfeme_fwd = 0;   // tfmtrie distance original→correction
    float distance_transfeme_rev = 0;   // correction→original
    float distance_per_letter_fwd = 0;
    float distance_per_letter_rev = 0;
    float levenshtein = 0;
    float levenshtein_per_letter = 0;

    // BLEU
    float bleu_2 = 0, bleu_3 = 0, bleu_4 = 0;

    // Letter precision
    float letter_precision_1 = 0, letter_precision_2 = 0, letter_precision_3 = 0;

    // Structural
    float is_permutation = 0;
    float kendall_tau_distance = 0;

    // Combos (computed from above)
    float combo_dist_lm = 0;
    float combo_lm_diff = 0;
    float combo_len_diff = 0;
    float combo_dist_lm_rev = 0;
    float combo_lm_static_diff = 0;

    // Keyboard-specific
    float geometry_distance_per_letter = 0;
    float geometry_distance_original = 0;
    float geometry_distance_per_letter_original = 0;
    float geometry_distance_rel_gap = 0;
    float geometry_distance_ngram = 0;
    float neural_oov = 0;

    // ... remaining keyboard features

    std::vector<float> ToVector() const;
    static constexpr size_t kFeatureCount = 54;
};
```

- [ ] **Step 3: Implement MindSpore Lite inference in Score()**

In `ml_ranker.cpp`, under `#ifdef MINDSPORE_LITE_AVAILABLE`:

```cpp
std::vector<float> MLRanker::Score(const std::vector<RankerFeatures>& candidates) const {
    if (!model_) {
        // Heuristic fallback
        std::vector<float> scores;
        for (const auto& c : candidates) scores.push_back(HeuristicScore(c));
        return scores;
    }

    auto inputs = model_->GetInputs();
    if (inputs.empty()) return {};

    auto& input_tensor = inputs[0];
    auto* input_data = reinterpret_cast<float*>(input_tensor.MutableData());

    std::vector<mindspore::MSTensor> outputs;  // local, not member
    std::vector<float> scores;
    for (const auto& c : candidates) {
        auto fv = c.ToVector();
        memcpy(input_data, fv.data(), fv.size() * sizeof(float));

        auto status = model_->Predict(inputs, &outputs);
        if (status != mindspore::kSuccess) {
            scores.push_back(HeuristicScore(c));
            continue;
        }

        auto& output = outputs[0];
        float score = *reinterpret_cast<const float*>(output.Data());
        scores.push_back(score);
    }
    return scores;
}
```

**BREAKING CHANGE:** Changing `kFeatureCount` from 10 to 54 breaks all existing callsites that construct `RankerFeatures` in `keyboard_engine.cpp` (lines ~300-330). Task C4 MUST update those callsites immediately after this task. Do not commit C3 and C4 separately if it would leave a broken build — combine them or ensure C4 follows immediately.

- [ ] **Step 4: Commit**

```
feat: expand MLRanker with full feature vector and MindSpore inference
```

---

### Task C4: Integrate models into KeyboardEngine pipeline

**Files:**
- Modify: `entry/src/main/cpp/hosdict_engine/keyboard_engine.h`
- Modify: `entry/src/main/cpp/hosdict_engine/keyboard_engine.cpp`
- Modify: `entry/src/main/cpp/hosdict_engine/loaders.cpp`

- [ ] **Step 1: Add model loading in InitSubsystems()**

```cpp
// Load ranker model
auto* ranker_asset = package_.store().FindBinary("model.ranker");
if (ranker_asset) {
    ranker_.Load(ranker_asset->bytes.data(), ranker_asset->bytes.size());
}

// Load autocorrect model
auto* autocc_asset = package_.store().FindBinary("model.autocorrect");
if (autocc_asset) {
    autocc_ranker_.Load(autocc_asset->bytes.data(), autocc_asset->bytes.size());
}
```

- [ ] **Step 2: Implement feature extraction in GetSuggestions()**

Replace the simple RankerFeatures construction with full feature extraction:

```cpp
RankerFeatures ExtractFeatures(const std::string& original,
                                const std::string& correction,
                                const PredictorResult& result,
                                const TfmTrieDecoder& tfmtrie) {
    RankerFeatures f;
    f.original_length = static_cast<float>(DecodeUtf8(original).size());
    f.correction_length = static_cast<float>(DecodeUtf8(correction).size());
    f.no_change = (original == correction) ? 1.0f : 0.0f;
    f.levenshtein = static_cast<float>(LevenshteinDistanceUtf8(original, correction));
    f.levenshtein_per_letter = f.levenshtein / std::max(f.correction_length, 1.0f);
    f.original_ndict = predictor_.Contains(original) ? 1.0f : 0.0f;
    f.correction_ndict = predictor_.Contains(correction) ? 1.0f : 0.0f;
    // ... compute remaining features
    return f;
}
```

- [ ] **Step 3: Wire autocorrect model into AutocorrectEngine**

Replace Levenshtein-based autocorrect confidence with model inference:

```cpp
bool should_autocorrect = false;
if (autocc_ranker_.is_loaded() && !suggestions.empty()) {
    RankerFeatures af = ExtractAutocorrectFeatures(normalized, suggestions[0].text);
    float confidence = autocc_ranker_.ScoreOne(af);
    should_autocorrect = confidence > 0.7f;  // threshold TBD
} else {
    // Existing heuristic fallback
    should_autocorrect = /* existing logic */;
}
```

- [ ] **Step 4: Commit**

```
feat: integrate ML models into suggestion and autocorrect pipeline
```

---

## Chunk 4: Final Pipeline Assembly + Verification

### Task F1: Pack all assets into .hosdict container

**Files:**
- Modify: `tools/build_hosdict.py` or `tools/pack_hosdict.py`

- [ ] **Step 1: Update packing script to include ML models**

Add model files to the hosdict manifest:
```json
{
  "assets": [
    {"key": "model.ranker", "format": "mindspore_lite_v1", "path": "models/ranker.ms"},
    {"key": "model.autocorrect", "format": "mindspore_lite_v1", "path": "models/tree_autocorrect_model.ms"},
    {"key": "model.context", "format": "mindspore_lite_v1", "path": "models/neural_model.ms"},
    {"key": "model.char_lm", "format": "mindspore_lite_v1", "path": "models/char_model.ms"}
  ]
}
```

- [ ] **Step 2: Rebuild .hosdict container**

```bash
python tools/pack_hosdict.py --manifest tools/hosdict_v1_manifest.json --output entry/src/main/resources/rawfile/dict/ru.hoskey.hosdict
```

- [ ] **Step 3: Verify container integrity**

Check all assets are discoverable by key and have correct sizes.

- [ ] **Step 4: Commit**

```
feat: pack ML models into hosdict container
```

---

### Task F2: Full pipeline build and device test

- [ ] **Step 1: Build release**

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_final.ps1
```
Expected: BUILD SUCCESSFUL

- [ ] **Step 2: Deploy to device**

Install unsigned .hap via DevEco Studio or hdc.

- [ ] **Step 3: Run SetupGuidePage diagnostics**

Verify:
- Engine opens successfully
- All assets loaded (trie, tfmtrie, normdict, lemmer, models)
- getSuggestions("вообщем", "", "ru-RU") returns meaningful results

- [ ] **Step 4: Test suggestion quality**

Manual test set:
| Input | Previous | Expected top suggestion |
|-------|----------|----------------------|
| "привет" | "" | "привет" (exact) |
| "поивет" | "" | "привет" (tfmtrie correction) |
| "вообщем" | "" | "в общем" (autocorrect) |
| "хор" | "" | "хорошо", "хороший" (prefix) |
| "есть" | "я хочу" | "есть" (context-aware) |
| "красив" | "" | "красивый", "красиво" (lemmer expansion) |

- [ ] **Step 5: Measure latency**

Check that getSuggestions() completes in < 50ms via hilog timestamps.

- [ ] **Step 6: Commit final adjustments**

```
feat: complete Yandex quality parity pipeline v1
```

---

### Task F3: Create and run verification benchmark

**Files:**
- Create: `tools/benchmark_suggestions.py`

- [ ] **Step 1: Create 200-query benchmark test set**

Create `tools/benchmark_queries.tsv` with columns: `input`, `previous`, `locale`, `expected_top1`, `expected_top3`, `category`

Categories: exact_word, typo, prefix, contextual, autocorrect, morphological

Sources:
- 50 correct words (should return exact match as top-1)
- 50 typos (JCUKEN neighbors, missing letters, transpositions)
- 30 prefix completions (2-4 chars → full word)
- 30 contextual (with previous word)
- 20 autocorrect targets ("вообщем" → "в общем")
- 20 morphological (lemma forms)

Reference data: collect top-3 from Yandex Keyboard on Android device.

- [ ] **Step 2: Create benchmark runner script**

Python script that:
1. Calls getSuggestions() via adb/hdc bridge or reads device logs
2. Compares output against expected_top3
3. Computes: Precision@3, Autocorrect accuracy, MRR

- [ ] **Step 3: Run benchmark and document results**

Targets from spec:
- Precision@3 >= 0.8
- Autocorrect accuracy >= 0.9

- [ ] **Step 4: Memory profiling**

Check RSS on device via `hidumper` or equivalent:
- Target: RSS < 100MB with all models loaded

- [ ] **Step 5: Commit benchmark and results**

```
test: add 200-query suggestion quality benchmark
```

---

### Task F5: Remove fallback workarounds

**Files:**
- Modify: `entry/src/main/cpp/hosdict_engine/keyboard_engine.cpp`
- Possibly remove: `entry/src/main/resources/rawfile/dict/extracted_words.txt`

- [ ] **Step 1: Remove lexicon.default_payload priority**

Once trie decoder is verified, remove the fallback path that prefers extracted_words.txt.

- [ ] **Step 2: Remove extracted_words.txt from rawfile**

This saves ~2MB in the APK.

- [ ] **Step 3: Update asset_manifest.cpp**

Remove the `lexicon.default_payload` entry from the manifest builder.

- [ ] **Step 4: Verify engine still works without fallback**

Run SetupGuidePage diagnostics — word count should come from succinct trie.

- [ ] **Step 5: Commit**

```
refactor: remove fallback word list, use succinct trie as sole lexicon
```
