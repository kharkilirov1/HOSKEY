#include <cstdio>
#include <cstdint>
#include <cstring>

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
    
    // Parse BV2 (sub louds)
    size_t bv2_offset = 448936;
    RankedBitvector louds;
    louds.num_words = *(uint64_t*)(data + bv2_offset);
    louds.popcount = *(uint64_t*)(data + bv2_offset + 16);
    louds.bits = (const uint64_t*)(data + bv2_offset + 24 - bits_section_size);
    
    // Try both BV1 and BV3 for has_child
    size_t bv1_offset = 292744;  // main has_child
    size_t bv3_offset = 605128;  // sub has_child
    
    RankedBitvector has_child_bv1, has_child_bv3;
    has_child_bv1.bits = (const uint64_t*)(data + bv1_offset + 24 - bits_section_size);
    has_child_bv3.bits = (const uint64_t*)(data + bv3_offset + 24 - bits_section_size);
    
    printf("Tree structure from LOUDS (first 32 positions):\n");
    printf("Pos: ");
    for (int i = 0; i < 32; i++) printf("%2d ", i);
    printf("\nBit: ");
    for (int i = 0; i < 32; i++) printf(" %d ", (int)louds.GetBit(i));
    printf("\n\n");
    
    printf("Node assignment:\n");
    int node_id = 0;
    for (int pos = 0; pos < 32; pos++) {
        if (louds.GetBit(pos)) {
            printf("  Position %2d -> node %2d, has_child_BV1[%d]=%d, has_child_BV3[%d]=%d\n",
                   pos, node_id, node_id, (int)has_child_bv1.GetBit(node_id),
                   node_id, (int)has_child_bv3.GetBit(node_id));
            node_id++;
        }
    }
    
    printf("\nTree interpretation:\n");
    printf("Root (node 0) at pos 1\n");
    printf("  -> Child at pos 2 (node 1, label from labels[1])\n");
    printf("     -> Children at pos 4,5,6 (nodes 2,3,4)\n");
    printf("  -> Next sibling check: pos 3 has bit %d (should be 0 if node 1 is last child of root)\n",
           (int)louds.GetBit(3));
    
    delete[] data;
    return 0;
}
