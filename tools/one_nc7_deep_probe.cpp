// Probe: does Complete() find short words like "кот" with high candidate limit?
// Uses only public API.
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

  // Search for short words via Complete() with high candidate limit
  auto search = [&](const char* label, const char* prefix_utf8, size_t extra, const char* target_utf8) {
    auto completions = t.Complete(prefix_utf8, 4096, extra);
    bool found = false;
    for (auto& c : completions) {
      if (c.text == target_utf8) { found = true; break; }
    }
    std::printf("%-20s extra=%zu found=%d total=%zu\n", label, extra, (int)found, completions.size());
  };

  std::printf("=== Searching for short complete words ===\n");
  search("ко→кот", "\xD0\xBA\xD0\xBE", 1, "\xD0\xBA\xD0\xBE\xD1\x82");
  search("ко→кот(2)", "\xD0\xBA\xD0\xBE", 2, "\xD0\xBA\xD0\xBE\xD1\x82");
  search("ко→кот(5)", "\xD0\xBA\xD0\xBE", 5, "\xD0\xBA\xD0\xBE\xD1\x82");
  search("к→кот", "\xD0\xBA", 2, "\xD0\xBA\xD0\xBE\xD1\x82");
  search("к→кот(5)", "\xD0\xBA", 5, "\xD0\xBA\xD0\xBE\xD1\x82");
  search("до→дом", "\xD0\xB4\xD0\xBE", 1, "\xD0\xB4\xD0\xBE\xD0\xBC");
  search("ст→стол", "\xD1\x81\xD1\x82", 2, "\xD1\x81\xD1\x82\xD0\xBE\xD0\xBB");
  search("при→привет", "\xD0\xBF\xD1\x80\xD0\xB8", 3, "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");

  // Diagnostic: check WalkPrefix for various prefixes to see terminal/has_child
  std::printf("\n=== WalkPrefix diagnostics ===\n");
  auto diag = [&](const char* label, const char* utf8) {
    auto states = t.WalkPrefix(utf8);
    std::printf("%-10s states=%zu", label, states.size());
    for (auto& st : states)
      std::printf(" [n=%u t=%d h=%d]", st.node_id, (int)st.terminal, (int)st.has_child);
    std::printf(" term-bound=%d contains=%d has-prefix=%d\n",
                (int)t.HasTerminalBoundary(utf8), (int)t.Contains(utf8), (int)t.HasPrefix(utf8));
  };
  diag("к", "\xD0\xBA");
  diag("ко", "\xD0\xBA\xD0\xBE");
  diag("кот", "\xD0\xBA\xD0\xBE\xD1\x82");
  diag("кот(cont)", "\xD0\xBA\xD0\xBE\xD1\x82"); // same, just label
  diag("д", "\xD0\xB4");
  diag("до", "\xD0\xB4\xD0\xBE");
  diag("дом", "\xD0\xB4\xD0\xBE\xD0\xBC");
  diag("ст", "\xD1\x81\xD1\x82");
  diag("сто", "\xD1\x81\xD1\x82\xD0\xBE");
  diag("стол", "\xD1\x81\xD1\x82\xD0\xBE\xD0\xBB");

  return 0;
}
