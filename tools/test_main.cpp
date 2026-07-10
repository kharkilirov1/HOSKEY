#include <cstdio>
#include <cstring>
#include <cstdint>

int main() {
    FILE* f = fopen("C:/Users/Kharki/Desktop/yandex_extracted/extracted_sections/trie", "rb");
    if (!f) { printf("Failed to open\n"); return 1; }
    
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    uint8_t* data = new uint8_t[size];
    fread(data, 1, size, f);
    fclose(f);
    
    printf("File size: %zu\n", size);
    
    uint32_t magic = *(uint32_t*)data;
    printf("Magic: 0x%08X\n", magic);
    
    uint32_t item_count = *(uint32_t*)(data + 16);
    printf("item_count: %u\n", item_count);
    
    const size_t bits_section_size = 124992;
    size_t bv_offsets[4] = {0};
    int bv_count = 0;
    
    for (size_t i = 24; i < size - 32 && bv_count < 4; i += 8) {
        uint64_t v0 = *(uint64_t*)(data + i);
        uint64_t v1 = *(uint64_t*)(data + i + 8);
        if (v0 == item_count && v1 == 3904) {
            bv_offsets[bv_count++] = i;
            printf("BV%d at offset %zu: popcount=%llu\n", bv_count-1, i, 
                   *(uint64_t*)(data + i + 16));
        }
    }
    
    if (bv_count >= 2) {
        size_t bv1_bits_offset = bv_offsets[1] + 24 - bits_section_size;
        printf("Main has_child first byte: 0x%02X\n", data[bv1_bits_offset]);
        
        uint64_t* bits = (uint64_t*)(data + bv1_bits_offset);
        int nodes_with_children = 0;
        for (int i = 0; i < 100; i++) {
            if ((bits[i/64] >> (i%64)) & 1) nodes_with_children++;
        }
        printf("First 100 main nodes with children: %d\n", nodes_with_children);
    }
    
    if (bv_count >= 4) {
        size_t bv3_bits_offset = bv_offsets[3] + 24 - bits_section_size;
        printf("Sub has_child first byte: 0x%02X\n", data[bv3_bits_offset]);
        
        uint64_t* bits = (uint64_t*)(data + bv3_bits_offset);
        int nodes_with_children = 0;
        for (int i = 0; i < 100; i++) {
            if ((bits[i/64] >> (i%64)) & 1) nodes_with_children++;
        }
        printf("First 100 sub nodes with children: %d\n", nodes_with_children);
    }
    
    delete[] data;
    return 0;
}
