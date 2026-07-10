#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <optional>

struct RankedBitvector {
    uint64_t num_words = 0, popcount = 0;
    const uint8_t* rank_data = nullptr;
    const uint64_t* bits = nullptr;
    
    uint64_t GetBit(uint64_t pos) const {
        if (!bits || pos >= num_words * 64) return 0;
        return (bits[pos / 64] >> (pos % 64)) & 1ULL;
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
    
    const size_t bits_section_size = 124992;
    
    // Try different combinations
    // BV0=130744 (main louds), BV1=292744 (main has_child)
    // BV2=448936 (sub louds), BV3=605128 (sub has_child)
    // Try: main = BV0+BV3, sub = BV2+BV1
    size_t bv_louds_offset = 448936;   // BV2 - sub louds
    size_t bv_haschild_offset = 292744; // BV1 - main has_child for sub?
    
    RankedBitvector louds, has_child;
    louds.num_words = *(uint64_t*)(data + bv2_offset);
    louds.popcount = *(uint64_t*)(data + bv2_offset + 16);
    louds.rank_data = data + bv2_offset + 24;
    louds.bits = (uint64_t*)(data + bv2_offset + 24 - bits_section_size);
    
    has_child.num_words = *(uint64_t*)(data + bv3_offset);
    has_child.popcount = *(uint64_t*)(data + bv3_offset + 16);
    has_child.bits = (uint64_t*)(data + bv3_offset + 24 - bits_section_size);
    
    printf("LOUDS popcount: %llu\n", louds.popcount);
    printf("has_child popcount: %llu\n", has_child.popcount);
    
    // Map LOUDS positions to node IDs
    printf("\nLOUDS positions with '1' (first 20):\n");
    int node_id = 0;
    for (int pos = 0; pos < 64 && node_id < 20; pos++) {
        if (louds.GetBit(pos)) {
            printf("  Position %d -> node %d, has_child[%d]=%d\n", 
                   pos, node_id, node_id, (int)has_child.GetBit(node_id));
            node_id++;
        }
    }
    
    // Check has_child values
    printf("\nhas_child for first 20 nodes:\n");
    for (int i = 0; i < 20; i++) {
        printf("  has_child[%d] = %d\n", i, (int)has_child.GetBit(i));
    }
    
    delete[] data;
    return 0;
}
