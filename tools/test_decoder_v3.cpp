// Standalone test for 1nc7 decoder v3 - two-level trie
// Build: g++ -std=c++17 -O2 -o test_decoder_v3 test_decoder_v3.cpp

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <stack>
#include <limits>

struct Status {
    int code;
    const char* msg;
    static Status Ok() { return {0, ""}; }
    static Status ParseError(const char* m) { return {1, m}; }
    bool ok() const { return code == 0; }
};

struct RankedBitvector {
    uint64_t num_words = 0;
    uint64_t popcount = 0;
    const uint8_t* rank_data = nullptr;
    const uint64_t* bits = nullptr;
    
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
    
    uint64_t Select0(uint64_t n) const {
        if (!bits || n >= (num_words * 64 - popcount)) {
            return std::numeric_limits<uint64_t>::max();
        }
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

struct TrieLevel {
    RankedBitvector louds;
    RankedBitvector has_child;
    const uint8_t* labels = nullptr;
    size_t labels_size = 0;
};

struct SuccinctTrie {
    TrieLevel main_level_;
    TrieLevel sub_level_;
    std::vector<std::string> words_;
    std::string notes_;
    
    Status Parse(const uint8_t* data, size_t size);
    void ExtractWords();
    static std::string ToUtf8(const std::vector<uint32_t>& codepoints);
};

std::string SuccinctTrie::ToUtf8(const std::vector<uint32_t>& codepoints) {
    std::string result;
    for (uint32_t cp : codepoints) {
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
    if (size < 24) return Status::ParseError("Size < 24");
    
    uint32_t magic = *(uint32_t*)data;
    if (magic != 0x37636E31) return Status::ParseError("Bad magic");
    
    uint32_t item_count = *(uint32_t*)(data + 16);
    notes_ += "item_count=" + std::to_string(item_count) + "\n";
    
    const size_t bits_section_size = 124992;
    std::vector<size_t> bv_offsets;
    
    for (size_t i = 24; i < size - 32 && bv_offsets.size() < 4; i += 8) {
        uint64_t v0 = *(uint64_t*)(data + i);
        uint64_t v1 = *(uint64_t*)(data + i + 8);
        if (v0 == item_count && v1 == 3904) {
            bv_offsets.push_back(i);
        }
    }
    
    if (bv_offsets.size() < 4) return Status::ParseError("Need 4 BVs");
    
    auto parse_bv = [&](size_t offset) -> RankedBitvector {
        RankedBitvector bv;
        bv.num_words = *(uint64_t*)(data + offset);
        bv.popcount = *(uint64_t*)(data + offset + 16);
        bv.rank_data = data + offset + 24;
        size_t bits_offset = offset + 24 - bits_section_size;
        if (bits_offset < size) {
            bv.bits = (const uint64_t*)(data + bits_offset);
        }
        return bv;
    };
    
    main_level_.louds = parse_bv(bv_offsets[0]);
    main_level_.has_child = parse_bv(bv_offsets[1]);
    sub_level_.louds = parse_bv(bv_offsets[2]);
    sub_level_.has_child = parse_bv(bv_offsets[3]);
    
    notes_ += "main pop=" + std::to_string(main_level_.has_child.popcount) + "\n";
    notes_ += "sub pop=" + std::to_string(sub_level_.has_child.popcount) + "\n";
    
    // Find labels
    size_t after_bv3 = bv_offsets[3] + 24 + ((sub_level_.has_child.num_words + 7) / 8) * 16;
    for (size_t p = after_bv3; p + 40 < size; p += 8) {
        uint64_t m0 = *(uint64_t*)(data + p);
        uint64_t m1 = *(uint64_t*)(data + p + 8);
        uint64_t m2 = *(uint64_t*)(data + p + 16);
        if (m1 == 8 && m2 == 255 && m0 > 400000 && m0 < 410000) {
            sub_level_.labels = data + p + 40;
            uint32_t payload_hint = *(uint32_t*)(data + 8);
            sub_level_.labels_size = (data + payload_hint) - sub_level_.labels;
            notes_ += "labels at " + std::to_string(p + 40) + " size=" + std::to_string(sub_level_.labels_size) + "\n";
            break;
        }
    }
    
    ExtractWords();
    return Status::Ok();
}

void SuccinctTrie::ExtractWords() {
    // DFS starting from main trie root
    struct StackEntry {
        uint64_t node_id;
        bool in_sub;
        std::vector<uint32_t> path;
    };
    
    std::stack<StackEntry> stack;
    stack.push({0, false, {}});  // Start in main trie
    
    int iterations = 0;
    const int MAX_ITER = 10000000;
    
    while (!stack.empty() && iterations < MAX_ITER && words_.size() < 200000) {
        iterations++;
        auto entry = stack.top();
        stack.pop();
        
        if (entry.in_sub) {
            // In sub trie
            if (entry.node_id >= sub_level_.labels_size) continue;
            
            // Get label
            if (entry.node_id != 0 && sub_level_.labels) {
                uint8_t label_id = sub_level_.labels[entry.node_id];
                if (label_id < 32) {
                    entry.path.push_back(0x0430 + label_id);  // а-я
                } else if (label_id == 32) {
                    entry.path.push_back('-');
                } else if (label_id == 33) {
                    entry.path.push_back(0x0451);  // ё
                } else if (label_id >= 64 && label_id < 96) {
                    entry.path.push_back(0x0410 + label_id - 64);  // А-Я
                }
            }
            
            bool has_children = sub_level_.has_child.GetBit(entry.node_id);
            if (!has_children && entry.path.size() > 1) {
                words_.push_back(ToUtf8(entry.path));
            }
            
            if (has_children) {
                uint64_t rank = sub_level_.louds.Rank1(entry.node_id);
                uint64_t child_start = sub_level_.louds.Select0(rank);
                if (child_start != std::numeric_limits<uint64_t>::max()) {
                    child_start++;
                    for (uint64_t pos = child_start; pos < child_start + 50; ++pos) {
                        if (!sub_level_.louds.GetBit(pos)) break;
                        uint64_t child_id = sub_level_.louds.Rank1(pos + 1);
                        if (child_id > 0 && child_id < sub_level_.labels_size) {
                            stack.push({child_id, true, entry.path});
                        }
                    }
                }
            }
        } else {
            // In main trie
            bool has_children = main_level_.has_child.GetBit(entry.node_id);
            
            // When main has children, enter sub trie
            if (has_children) {
                stack.push({entry.node_id, true, entry.path});
            }
            
            // Continue in main trie too?
            uint64_t rank = main_level_.louds.Rank1(entry.node_id);
            uint64_t child_start = main_level_.louds.Select0(rank);
            if (child_start != std::numeric_limits<uint64_t>::max()) {
                child_start++;
                for (uint64_t pos = child_start; pos < child_start + 50; ++pos) {
                    if (!main_level_.louds.GetBit(pos)) break;
                    uint64_t child_id = main_level_.louds.Rank1(pos + 1);
                    if (child_id > 0) {
                        stack.push({child_id, false, entry.path});
                    }
                }
            }
        }
    }
    
    notes_ += "iterations=" + std::to_string(iterations) + "\n";
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "C:/Users/Kharki/Desktop/yandex_extracted/extracted_sections/trie";
    
    FILE* f = fopen(path, "rb");
    if (!f) { printf("Failed to open\n"); return 1; }
    
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    std::vector<uint8_t> data(size);
    fread(data.data(), 1, size, f);
    fclose(f);
    
    SuccinctTrie trie;
    Status st = trie.Parse(data.data(), size);
    
    printf("Parse: %s\n", st.ok() ? "OK" : st.msg);
    printf("Notes:\n%s\n", trie.notes_.c_str());
    printf("Words: %zu\n", trie.words_.size());
    
    for (size_t i = 0; i < std::min(size_t(20), trie.words_.size()); ++i) {
        printf("  %zu: %s\n", i + 1, trie.words_[i].c_str());
    }
    
    return 0;
}
