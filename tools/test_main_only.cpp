#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>
#include <stack>
#include <limits>

struct RankedBitvector {
    uint64_t num_words = 0, popcount = 0;
    const uint8_t* rank_data = nullptr;
    const uint64_t* bits = nullptr;
    
    uint64_t GetBit(uint64_t pos) const {
        if (!bits || pos >= num_words * 64) return 0;
        return (bits[pos / 64] >> (pos % 64)) & 1ULL;
    }
    
    uint64_t Rank1(uint64_t pos) const {
        if (!bits || !rank_data || pos == 0) return 0;
        if (pos >= num_words * 64) return popcount;
        uint64_t word_idx = pos / 64, sb_idx = word_idx / 8, word_in_sb = word_idx % 8;
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
    
    uint64_t Select0(uint64_t n) const {
        if (!bits || n >= (num_words * 64 - popcount)) return std::numeric_limits<uint64_t>::max();
        uint64_t count = 0;
        for (uint64_t pos = 0; pos < num_words * 64; ++pos) {
            if (!GetBit(pos)) { if (count == n) return pos; ++count; }
        }
        return std::numeric_limits<uint64_t>::max();
    }
};

int main() {
    FILE* f = fopen("C:/Users/Kharki/Desktop/yandex_extracted/extracted_sections/trie", "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = new uint8_t[size];
    fread(data, 1, size, f);
    fclose(f);
    
    uint32_t item_count = *(uint32_t*)(data + 16);
    const size_t bits_section_size = 124992;
    
    size_t bv_offsets[4] = {0};
    int bv_count = 0;
    for (size_t i = 24; i < size - 32 && bv_count < 4; i += 8) {
        if (*(uint64_t*)(data + i) == item_count && *(uint64_t*)(data + i + 8) == 3904) {
            bv_offsets[bv_count++] = i;
        }
    }
    
    printf("Found %d BVs\n", bv_count);
    
    // Parse main level
    RankedBitvector main_louds, main_has_child;
    main_louds.num_words = *(uint64_t*)(data + bv_offsets[0]);
    main_louds.popcount = *(uint64_t*)(data + bv_offsets[0] + 16);
    main_louds.rank_data = data + bv_offsets[0] + 24;
    main_louds.bits = (uint64_t*)(data + bv_offsets[0] + 24 - bits_section_size);
    
    main_has_child.num_words = *(uint64_t*)(data + bv_offsets[1]);
    main_has_child.popcount = *(uint64_t*)(data + bv_offsets[1] + 16);
    main_has_child.rank_data = data + bv_offsets[1] + 24;
    main_has_child.bits = (uint64_t*)(data + bv_offsets[1] + 24 - bits_section_size);
    
    printf("Main: louds_pop=%llu has_child_pop=%llu\n", main_louds.popcount, main_has_child.popcount);
    
    // Count nodes at each depth in main trie
    std::vector<int> depth_counts(10, 0);
    
    struct Entry { uint64_t node; int depth; };
    std::stack<Entry> stack;
    stack.push({0, 0});
    
    while (!stack.empty()) {
        auto e = stack.top(); stack.pop();
        if (e.depth >= 10) continue;
        depth_counts[e.depth]++;
        
        if (main_has_child.GetBit(e.node)) {
            uint64_t rank = main_louds.Rank1(e.node);
            uint64_t child_start = main_louds.Select0(rank);
            if (child_start != std::numeric_limits<uint64_t>::max()) {
                child_start++;
                for (uint64_t pos = child_start; pos < child_start + 50; ++pos) {
                    if (!main_louds.GetBit(pos)) break;
                    uint64_t child_id = main_louds.Rank1(pos + 1);
                    if (child_id > 0) stack.push({child_id, e.depth + 1});
                }
            }
        }
    }
    
    printf("Nodes at depth:\n");
    for (int i = 0; i < 10; i++) {
        if (depth_counts[i] > 0) printf("  depth %d: %d nodes\n", i, depth_counts[i]);
    }
    
    delete[] data;
    return 0;
}
