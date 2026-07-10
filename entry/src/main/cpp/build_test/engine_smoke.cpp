// Host end-to-end smoke test: open the real ru.hoskey.hosdict dictionary and
// run GetSuggestions / CheckWord through the full engine. On host, MindSpore is
// absent, so the neural ranker falls back to the heuristic and NNLM is off, but
// the trie / lemmer / tfmtrie / policy / normdict paths all run for real.
//
// Build (from build_test/, engine .obj already produced by ninja):
//   g++ -std=gnu++17 -I.. -I../hosdict_engine engine_smoke.cpp \
//       CMakeFiles/hoskey_bridge.dir/hosdict_engine/*.obj -o engine_smoke -lstdc++fs

#include "hosdict_engine/keyboard_engine.h"

#include <cstdio>
#include <string>

using namespace hoskey::engine;

static const char* kDictDir =
    "C:/Users/Kharki/DevEcoStudioProjects/HOSKEY/entry/src/main/resources/rawfile/dict";

int main() {
  KeyboardEngine engine;
  Status s = engine.Open(kDictDir);
  std::printf("Open: ok=%d msg=%s\n", (int)s.ok(), s.message.c_str());
  if (!s.ok()) return 1;
  std::printf("is_open=%d\n", (int)engine.is_open());
  std::printf("assets: %s\n", engine.package().DiagnosticsSummary().c_str());

  // Prefixes (Cyrillic UTF-8) + Latin + empty, exercising completion/prediction.
  struct Q { const char* label; const char* token; };
  const Q queries[] = {
    {"при",    "\xD0\xBF\xD1\x80\xD0\xB8"},                          // при
    {"мага",   "\xD0\xBC\xD0\xB0\xD0\xB3\xD0\xB0"},                  // мага
    {"кот",    "\xD0\xBA\xD0\xBE\xD1\x82"},                          // кот
    {"здравству", "\xD0\xB7\xD0\xB4\xD1\x80\xD0\xB0\xD0\xB2\xD1\x81\xD1\x82\xD0\xB2\xD1\x83"}, // здравству
    {"я",      "\xD1\x8F"},                                          // я
    {"latin",  "programm"},
    {"empty",  ""},
  };
  for (const auto& q : queries) {
    SuggestRequest req;
    req.token = q.token;
    req.input = q.token;
    req.max_suggestions = 5;
    SuggestResponse resp = engine.GetSuggestions(req);
    std::printf("suggest '%s' -> %zu: ", q.label, resp.suggestions.size());
    for (size_t i = 0; i < resp.suggestions.size(); ++i) {
      std::printf("[%s|%.1f|%s] ", resp.suggestions[i].text.c_str(),
                  resp.suggestions[i].score, resp.suggestions[i].tag.c_str());
    }
    std::printf("%s\n", resp.will_autocorrect ? "(autocorrect)" : "");
  }

  // CheckWord on known/unknown words.
  const char* words[] = {
    "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82",  // привет
    "\xD0\xBA\xD0\xBE\xD1\x82",                          // кот
    "asdfghjkl",
  };
  for (const char* w : words) {
    CheckWordRequest req;
    req.token = w;
    CheckWordResponse r = engine.CheckWord(req);
    std::printf("checkWord '%s': known=%d normalized='%s'\n", w, (int)r.is_known,
                r.normalized_form.c_str());
  }

  std::printf("SMOKE OK\n");
  return 0;
}
