// Test: does the Complete()-based fallback ever work? 
// Theory: it works when a PARENT node is root-terminal and materializes the suffix.
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
  HosdictContainer c; c.Open(container);
  const uint8_t* data = nullptr; size_t data_size = 0;
  c.GetAssetData("lexicon.default", &data, &data_size);
  OneNc7Trie t; t.Parse(data, data_size);

  // Check: which prefixes of "магазин" have HasTerminalBoundary?
  auto diag = [&](const char* label, const char* utf8, const char* full_word) {
    bool htb = t.HasTerminalBoundary(utf8);
    bool cont = t.Contains(utf8);
    bool hp = t.HasPrefix(utf8);
    std::printf("%-10s htb=%d cont=%d hp=%d", label, (int)htb, (int)cont, (int)hp);
    if (full_word) {
      // Check if Complete() from this prefix + remaining length finds the full word
      std::string prefix(utf8);
      std::string target(full_word);
      if (target.size() > prefix.size()) {
        size_t remaining = target.size() - prefix.size();
        // Convert byte count to approximate CP count — rough but sufficient
        auto completions = t.Complete(utf8, 4096, 10);
        bool found = false;
        for (auto& c : completions) {
          if (c.text == target) { found = true; break; }
        }
        std::printf(" complete->full=%d (n=%zu)", (int)found, completions.size());
      }
    }
    std::printf("\n");
  };

  const char* mag = "\xD0\xBC\xD0\xB0\xD0\xB3\xD0\xB0\xD0\xB7\xD0\xB8\xD0\xBD"; // магазин
  std::printf("=== Prefix chain for магазин ===\n");
  // Prefixes: м, ма, маг, мага, магаз, магази, магазин (all proper prefixes + full word)
  diag("м", "\xD0\xBC", mag);
  diag("ма", "\xD0\xBC\xD0\xB0", mag);
  diag("маг", "\xD0\xBC\xD0\xB0\xD0\xB3", mag);
  diag("мага", "\xD0\xBC\xD0\xB0\xD0\xB3\xD0\xB0", mag);
  diag("магаз", "\xD0\xBC\xD0\xB0\xD0\xB3\xD0\xB0\xD0\xB7", mag);
  diag("магази", "\xD0\xBC\xD0\xB0\xD0\xB3\xD0\xB0\xD0\xB7\xD0\xB8", mag);
  diag("магазин", mag, nullptr);

  // Quick check: are there ANY root-terminals at all?
  std::printf("\n=== Terminal scan: prefix lengths of words from Complete('маг',N,10) ===\n");
  auto completions = t.Complete("\xD0\xBC\xD0\xB0\xD0\xB3", 4096, 10); // маг
  int with_terminal = 0, total = 0;
  for (auto& c : completions) {
    total++;
    // Check each prefix of c.text for HasTerminalBoundary
    for (size_t len = 2; len < c.text.size(); len += 2) { // UTF-8 Cyrillic = 2 bytes
      if (t.HasTerminalBoundary(c.text.substr(0, len))) {
        with_terminal++;
        if (total <= 5) std::printf("  %s: terminal at prefix %zu\n", c.text.c_str(), len);
        break;
      }
    }
    if (total <= 10) std::printf("  [%s]\n", c.text.c_str());
  }
  std::printf("Total: %d, with root-terminal prefix: %d\n", total, with_terminal);

  return 0;
}
