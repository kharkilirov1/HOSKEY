// Diagnostic: what does Complete() actually produce for various prefixes?
#include "hosdict_container.h"
#include "one_nc7_trie.h"

#include <cstdio>
#include <filesystem>
#include <string>

using namespace hoskey::engine;

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1]
      : "C:/Users/Kharki/DevEcoStudioProjects/HOSKEY/entry/src/main/resources/rawfile/dict";
  std::string container;
  for (auto& e : std::filesystem::directory_iterator(dir))
    if (e.path().extension() == ".hosdict") { container = e.path().string(); break; }
  HosdictContainer c;
  if (!c.Open(container).ok()) { std::printf("open FAILED\n"); return 1; }
  const uint8_t* data = nullptr; size_t data_size = 0;
  if (!c.GetAssetData("lexicon.default", &data, &data_size).ok()) { std::printf("no asset\n"); return 1; }
  OneNc7Trie t;
  if (!t.Parse(data, data_size).ok()) { std::printf("parse FAILED\n"); return 1; }

  auto dump = [&](const char* label, const char* prefix_utf8, size_t max_results, size_t max_extra) {
    std::printf("--- %s (prefix='%s', max=%zu, extra=%zu) ---\n", label, prefix_utf8, max_results, max_extra);
    auto completions = t.Complete(prefix_utf8, max_results, max_extra);
    for (const auto& c : completions) {
      std::printf("  [%s] w=%d d=%u\n", c.text.c_str(), c.weight, c.depth);
    }
    if (completions.empty()) std::printf("  (empty)\n");
  };

  // Test prefixes that should lead to known words
  dump("ко→кот", "\xD0\xBA\xD0\xBE", 20, 3);          // "ко"
  dump("до→дом", "\xD0\xB4\xD0\xBE", 20, 3);          // "до"
  dump("ст→стол", "\xD1\x81\xD1\x82", 20, 3);         // "ст"
  dump("кот full", "\xD0\xBA\xD0\xBE\xD1\x82", 20, 3); // "кот"
  dump("дом full", "\xD0\xB4\xD0\xBE\xD0\xBC", 20, 3); // "дом"
  dump("при", "\xD0\xBF\xD1\x80\xD0\xB8", 20, 3);      // "при"

  // Direct root-graph terminal check
  std::printf("\n--- Root graph state for 'кот' ---\n");
  auto states = t.WalkPrefix("\xD0\xBA\xD0\xBE\xD1\x82"); // "кот"
  std::printf("states=%zu\n", states.size());
  for (const auto& st : states) {
    std::printf("  node=%u terminal=%d has_child=%d\n", st.node_id, (int)st.terminal, (int)st.has_child);
  }

  // Check intermediate prefixes
  std::printf("\n--- Intermediate states ---\n");
  for (const char* pfx : {"\xD0\xBA", "\xD0\xBA\xD0\xBE", "\xD0\xBA\xD0\xBE\xD1\x82"}) {
    auto s = t.WalkPrefix(pfx);
    std::printf("WalkPrefix('%s'): states=%zu", pfx, s.size());
    for (const auto& st : s)
      std::printf(" [n=%u t=%d h=%d]", st.node_id, (int)st.terminal, (int)st.has_child);
    std::printf("\n");
  }

  return 0;
}
