// Diagnose the ranking gap: request a LARGE top-k and print every suggestion with
// its tag + score. Confirms whether 1nc7 real-word completions are generated but
// out-ranked by ynnlm_vocab subword fragments (a ranking issue, not stage-skip).

#include "hosdict_engine/keyboard_engine.h"
#include <cstdio>

using namespace hoskey::engine;

int main() {
  KeyboardEngine engine;
  Status s = engine.Open("C:/Users/Kharki/DevEcoStudioProjects/HOSKEY/entry/src/main/resources/rawfile/dict");
  if (!s.ok()) { std::printf("open FAILED\n"); return 1; }

  const char* qs[] = {"\xD0\xBC\xD0\xB0\xD0\xB3\xD0\xB0",          // мага
                      "\xD0\xBF\xD1\x80\xD0\xBE\xD0\xB3"};          // прог
  for (const char* q : qs) {
    SuggestRequest req; req.token = q; req.input = q; req.max_suggestions = 20;
    SuggestResponse r = engine.GetSuggestions(req);
    std::printf("=== '%s' -> %zu ===\n", q, r.suggestions.size());
    for (auto& sg : r.suggestions)
      std::printf("  %-22s %8.2f  %s\n", sg.text.c_str(), sg.score, sg.tag.c_str());
  }
  return 0;
}
