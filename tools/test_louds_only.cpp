#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>
#include <stack>
#include <limits>

struct Bitvector {
    uint64_t num_words = 0, popcount = 0;
    const uint64_t* bits = nullptr;
    
    uint64_t GetBit(uint64_t pos) const {
        if (!bits || pos >= num_words * 64) return 0;
        return (bits[pos / 64] >> (pos % 64)) & 1ULL;
    }
    
    uint64_t Rank1(uint64_t pos) const {
        if (!bits || pos == 0) return 0;
        if (pos >= num_words * 64) return popcount;
        uint64_t count = 0;
        for (uint64_t i = 0; i < pos; i++) if (GetBit(i)) count++;
        return count;
    }
    
    uint64_t Select0(uint64_t n) const {
        if (n >= (num_words * 64 - popcount)) return std::numeric_limits<uint64_t>::max();
        uint64_t count = 0;
        for (uint64_t pos = 0; pos < num_words * 64; ++pos) {
            if (!GetBit(pos)) { if (count == n) return pos; ++count; }
        }
        return std::numeric_limits<uint64_t>::max();
    }
};

int main() {
    FILE* f = fopen("C:/Users/Kharki/Desktop/yandex_extracted/extracted_sections/trie", "rb");
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = new uint8_t[size];
    fread(data, 1, size, f);
    fclose(f);
    
    uint32_t item_count = *(uint32_t*)(data + 16);
    const size_t bits_section_size = 124992;
    
    // Sub LOUDS (BV2)
    size_t bv2_offset = 448936;
    Bitvector louds;
    louds.num_words = *(uint64_t*)(data + bv2_offset);
    louds.popcount = *(uint64_t*)(data + bv2_offset + 16);
    louds.bits = (uint64_t*)(data + bv2_offset + 24 - bits_section_size);
    
    printf("LOUDS: num_words=%llu popcount=%llu\n", louds.num_words, louds.popcount);
    
    // Sub has_child (BV3)  
    size_t bv3_offset = 605128;
    Bitvector has_child;
    has_child.num_words = *(uint64_t*)(data + bv3_offset);
    has_child.popcount = *(uint64_t*)(data + bv3_offset + 16);
    has_child.bits = (uint64_t*)(data + bv3_offset + 24 - bits_section_size);
    
    // Labels at 636424
    uint8_t* labels = data + 636424;
    
    // DFS using LOUDS only
    struct Entry { uint64_t node; std::vector<uint32_t> path; };
    std::stack<Entry> stack;
    // Try starting from different nodes
    for (int start = 0; start < 10; start++) {
        stack.push({(uint64_t)start, {}});
    }
    
    std::vector<std::string> words;
    int iter = 0;
    
    while (!stack.empty() && iter < 1000000 && words.size() < 10000) {
        iter++;
        auto e = stack.top(); stack.pop();
        
        if (e.node > 100000) continue;
        
        // Get label
        if (e.node != 0) {
            uint8_t lid = labels[e.node];
            if (lid < 32) e.path.push_back(0x0430 + lid);
            else if (lid == 32) e.path.push_back('-');
            else if (lid >= 64 && lid < 96) e.path.push_back(0x0410 + lid - 64);
        }
        
        // Check terminal
        bool has_c = has_child.GetBit(e.node);
        if (!has_c && e.path.size() > 1) {
            // Build string
            std::string s;
            for (auto c : e.path) {
                if (c <= 0x7F) s += (char)c;
                else {
                    s += (char)(0xE0 | (c >> 12));
                    s += (char)(0x80 | ((c >> 6) & 0x3F));
                    s += (char)(0x80 | (c & 0x3F));
                }
            }
            words.push_back(s);
        }
        
        // Get children using LOUDS
        if (has_c) {
            uint64_t rank = louds.Rank1(e.node);
            uint64_t child_start = louds.Select0(rank);
            if (child_start != std::numeric_limits<uint64_t>::max()) {
                child_start++;
                for (uint64_t pos = child_start; pos < child_start + 40; ++pos) {
                    if (!louds.GetBit(pos)) break;
                    uint64_t child_id = louds.Rank1(pos + 1);
                    if (child_id > 0 && child_id < 400000) {
                        stack.push({child_id, e.path});
                    }
                }
            }
        }
    }
    
    printf("Iterations: %d\n", iter);
    printf("Words: %zu\n", words.size());
    for (size_t i = 0; i < std::min(size_t(20), words.size()); ++i) {
        printf("  %zu: %s\n", i + 1, words[i].c_str());
    }
    
    delete[] data;
    return 0;
}
