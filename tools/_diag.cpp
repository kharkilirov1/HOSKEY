
#include <cstdio>
#include "one_nc7_trie.h"
using namespace hoskey::engine;
int main() {
    OneNc7Trie t;
    FILE* f = fopen("entry/src/main/resources/rawfile/dict/trie", "rb");
    fseek(f, 0, SEEK_END); size_t sz = ftell(f); rewind(f);
    uint8_t* data = new uint8_t[sz]; fread(data, 1, sz, f); fclose(f);
    t.Parse(data, sz);
    
    for (auto w : {"про", "стал", "нет", "она", "привет", "хорош"}) {
        auto st = t.WalkPrefix(w);
        bool term = false;
        for (auto& s : st) if (s.terminal) term = true;
        printf("WalkPrefix(%s): states=%zu terminal=%d
", w, st.size(), term);
    }
    auto comp = t.Complete("про", 10, 8);
    printf("
Complete(про,10,8):
");
    for (auto& c : comp) printf("  %s d=%u w=%d
", c.text.c_str(), c.depth, c.weight);
    return 0;
}
