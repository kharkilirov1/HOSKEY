// Verify HasTerminalBoundary / Contains reliability: does it cleanly separate
// real complete words from prefix fragments? If yes, the ranker's "in dictionary"
// feature can safely use it instead of subword-vocab membership.

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

  auto probe = [&](const char* label, const char* w) {
    std::printf("  %-8s %-16s term=%d contains=%d prefix=%d\n", label, w,
                (int)t.HasTerminalBoundary(w), (int)t.Contains(w), (int)t.HasPrefix(w));
  };
  std::printf("REAL WORDS (expect term=1):\n");
  probe("word", "\xD0\xBC\xD0\xB0\xD0\xB3\xD0\xB0\xD0\xB7\xD0\xB8\xD0\xBD");   // магазин
  probe("word", "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");           // привет
  probe("word", "\xD0\xBA\xD0\xBE\xD1\x82");                                   // кот
  probe("word", "\xD1\x81\xD1\x82\xD0\xBE\xD0\xBB");                           // стол
  probe("word", "\xD1\x80\xD0\xB0\xD0\xB1\xD0\xBE\xD1\x82\xD0\xB0");           // работа
  probe("word", "\xD0\xB4\xD0\xBE\xD0\xBC");                                   // дом
  probe("word", "\xD0\xBA\xD0\xBD\xD0\xB8\xD0\xB3\xD0\xB0");                   // книга
  probe("word", "\xD0\xBE\xD0\xBA\xD0\xBD\xD0\xBE");                           // окно
  std::printf("FRAGMENTS (expect term=0):\n");
  probe("frag", "\xD0\xBC\xD0\xB0\xD0\xB3\xD0\xB0\xD0\xB7\xD0\xB0");           // магаза
  probe("frag", "\xD0\xBC\xD0\xB0\xD0\xB3\xD0\xB0\xD0\xB7\xD0\xB5");           // магазе
  probe("frag", "\xD0\xBC\xD0\xB0\xD0\xB3\xD0\xB0");                           // мага
  probe("frag", "\xD0\xBF\xD1\x80\xD0\xBE\xD0\xB3");                           // прог
  probe("frag", "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2");                           // прив
  probe("frag", "\xD1\x80\xD0\xB0\xD0\xB1\xD0\xBE\xD1\x82");                   // работ
  return 0;
}
