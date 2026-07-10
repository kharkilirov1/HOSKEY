#include <cstdio>
#include <cstdint>

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
    
    size_t bv1_offset = 292744;
    size_t bv1_bits = bv1_offset + 24 - bits_section_size;
    
    printf("BV1 bits at offset %zu:\n", bv1_bits);
    printf("First 16 bytes: ");
    for (int i = 0; i < 16; i++) printf("%02X ", data[bv1_bits + i]);
    printf("\nBits 0-31: ");
    for (int i = 0; i < 32; i++) {
        int bit = (data[bv1_bits + i/8] >> (i%8)) & 1;
        printf("%d", bit);
    }
    printf("\n");
    
    delete[] data;
    return 0;
}
