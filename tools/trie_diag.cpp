#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include "hosdict_container.h"
#include "one_nc7_trie.h"
using namespace hoskey::engine;

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    const fs::path package_dir =
        argc > 1 ? fs::path(argv[1])
                 : fs::path("entry/src/main/resources/rawfile/dict/ru.hoskey.hosdict");

    HosdictContainer container;
    auto st = container.Open(package_dir);
    printf("Open: %s\n", st.message.c_str());

    const uint8_t* data = nullptr;
    size_t size = 0;
    st = container.GetAssetData("lexicon.default", &data, &size);
    printf("GetAssetData: %s size=%zu\n", st.message.c_str(), size);

    OneNc7Trie t;
    st = t.Parse(data, size);
    printf("Parse: %s loaded=%d\n", st.message.c_str(), t.loaded());
    printf("Levels: %s\n", t.FormatLevelSummary().c_str());

    // Walk prefix for "пр" and show all states
    auto st_pr = t.WalkPrefix("пр");
    printf("\nWalkPrefix(пр): %zu states\n", st_pr.size());
    for (auto& s : st_pr) {
        printf("  node=%u label=0x%02x term=%d child=%d\n",
               s.node_id, s.label_cp1251, s.terminal, s.has_child);
        if (s.terminal) {
            uint64_t rank = t.RootTerminalRank(s.node_id);
            uint64_t key = 0;
            t.RootTerminalKey(s.node_id, &key);
            printf("    terminal rank=%llu key=0x%llx\n",
                   (unsigned long long)rank, (unsigned long long)key);
        }
    }

    // Walk prefix for "про" and show all states
    auto st_pro = t.WalkPrefix("про");
    printf("\nWalkPrefix(про): %zu states\n", st_pro.size());
    for (auto& s : st_pro) {
        printf("  node=%u label=0x%02x term=%d child=%d\n",
               s.node_id, s.label_cp1251, s.terminal, s.has_child);
        if (s.terminal) {
            uint64_t rank = t.RootTerminalRank(s.node_id);
            uint64_t key = 0;
            t.RootTerminalKey(s.node_id, &key);
            printf("    terminal rank=%llu key=0x%llx\n",
                   (unsigned long long)rank, (unsigned long long)key);
        }
    }

    // Walk prefix for "не" and show all states
    auto st_ne = t.WalkPrefix("не");
    printf("\nWalkPrefix(не): %zu states\n", st_ne.size());
    for (auto& s : st_ne) {
        printf("  node=%u label=0x%02x term=%d child=%d\n",
               s.node_id, s.label_cp1251, s.terminal, s.has_child);
        if (s.terminal) {
            uint64_t rank = t.RootTerminalRank(s.node_id);
            uint64_t key = 0;
            t.RootTerminalKey(s.node_id, &key);
            printf("    terminal rank=%llu key=0x%llx\n",
                   (unsigned long long)rank, (unsigned long long)key);
        }
    }

    // Walk prefix for "она" and show all states
    auto st_ona = t.WalkPrefix("она");
    printf("\nWalkPrefix(она): %zu states\n", st_ona.size());
    for (auto& s : st_ona) {
        printf("  node=%u label=0x%02x term=%d child=%d\n",
               s.node_id, s.label_cp1251, s.terminal, s.has_child);
        if (s.terminal) {
            uint64_t rank = t.RootTerminalRank(s.node_id);
            uint64_t key = 0;
            t.RootTerminalKey(s.node_id, &key);
            printf("    terminal rank=%llu key=0x%llx\n",
                   (unsigned long long)rank, (unsigned long long)key);
        }
    }

    // Walk prefix for "хор" and show all states
    auto st_hor = t.WalkPrefix("хор");
    printf("\nWalkPrefix(хор): %zu states\n", st_hor.size());
    for (auto& s : st_hor) {
        printf("  node=%u label=0x%02x term=%d child=%d\n",
               s.node_id, s.label_cp1251, s.terminal, s.has_child);
        if (s.terminal) {
            uint64_t rank = t.RootTerminalRank(s.node_id);
            uint64_t key = 0;
            t.RootTerminalKey(s.node_id, &key);
            printf("    terminal rank=%llu key=0x%llx\n",
                   (unsigned long long)rank, (unsigned long long)key);
        }
    }

    // Check suffix entries
    printf("\nSuffix entries (level 2):\n");
    auto entries = t.SuffixEntries(2, 20);
    for (auto& e : entries) {
        printf("  off=%u text=%s\n", e.offset, e.text.c_str());
    }

    // Complete for "пр"
    printf("\nComplete(пр, 20, 8):\n");
    auto comp = t.Complete("пр", 20, 8);
    for (auto& c : comp) {
        printf("  %s d=%u w=%d\n", c.text.c_str(), c.depth, c.weight);
    }

    return 0;
}
