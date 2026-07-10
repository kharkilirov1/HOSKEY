#include <cstdio>
#include "succinct_trie.h"
using namespace hoskey::engine;
int main() {
    SuccinctTrie t;
    FILE* f = fopen("entry/src/main/resources/rawfile/dict/ru.hoskey.hosdict", "rb");
    fseek(f, 0, SEEK_END); size_t sz = ftell(f); rewind(f);
    uint8_t* data = new uint8_t[sz]; fread(data, 1, sz, f); fclose(f);
    Status st = t.Parse(data, sz, true);
    printf("Parse: %s loaded=%d words=%zu\n", st.message.c_str(), t.loaded(), t.word_count());
    delete[] data;
    return 0;
}
