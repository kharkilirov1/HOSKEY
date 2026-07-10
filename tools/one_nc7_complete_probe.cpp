// Probe the deep 1nc7 candidate materializer: load lexicon.default from the real
// container, Parse via OneNc7Trie, and print Complete() output for several
// prefixes. Shows whether the recursive materializer produces full words or
// short/fragment tails (the parity question past root-phase).

#include "hosdict_container.h"
#include "one_nc7_trie.h"

#include <cstdio>
#include <filesystem>
#include <string>

using namespace hoskey::engine;

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1]
      : "C:/Users/Kharki/DevEcoStudioProjects/HOSKEY/entry/src/main/resources/rawfile/dict";
  // Find the .hosdict container in dir.
  std::string container;
  for (auto& e : std::filesystem::directory_iterator(dir)) {
    if (e.path().extension() == ".hosdict") { container = e.path().string(); break; }
  }
  if (container.empty()) { std::printf("no .hosdict in %s\n", dir); return 1; }

  HosdictContainer c;
  Status s = c.Open(container);
  if (!s.ok()) { std::printf("container open FAILED: %s\n", s.message.c_str()); return 1; }

  const uint8_t* data = nullptr; size_t data_size = 0;
  s = c.GetAssetData("lexicon.default", &data, &data_size);
  if (!s.ok()) { std::printf("no lexicon.default: %s\n", s.message.c_str()); return 1; }

  OneNc7Trie trie;
  s = trie.Parse(data, data_size);
  std::printf("OneNc7Trie.Parse ok=%d size=%zu\n", (int)s.ok(), data_size);
  if (!s.ok()) return 1;

  const char* prefixes[] = {
    "\xD0\xBC\xD0\xB0\xD0\xB3",                          // маг  (-> магазин, магистр?)
    "\xD0\xBF\xD1\x80\xD0\xBE\xD0\xB3",                  // прог (-> программа?)
    "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2",                  // прив (-> привет?)
    "\xD0\xBA\xD0\xBE\xD1\x82",                          // кот
    "\xD1\x81\xD1\x82\xD0\xBE\xD0\xBB",                  // стол
    "\xD1\x80\xD0\xB0\xD0\xB1\xD0\xBE\xD1\x82",          // работ (-> работа?)
  };
  for (const char* p : prefixes) {
    auto items = trie.Complete(p, 8, 12);
    std::printf("Complete('%s') -> %zu: ", p, items.size());
    for (auto& it : items) std::printf("%s(%d) ", it.text.c_str(), (int)it.weight);
    std::printf("\n");
  }
  return 0;
}
