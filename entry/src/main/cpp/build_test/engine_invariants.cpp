// Invariant sweep over the real engine: run ~1200 real Cyrillic prefix queries
// through GetSuggestions and assert output invariants the pipeline must hold:
//   - no suggestion text is empty
//   - no duplicate suggestion text within one response (dedup works)
//   - scores are non-increasing (final sort holds)
//   - result size <= max_suggestions
// A violation is a real defect in the suggestion pipeline. Host-only (heuristic
// ranker; NNLM off) but these structural invariants are ranker-independent.

#include "hosdict_engine/keyboard_engine.h"

#include <cstdio>
#include <string>
#include <vector>
#include <unordered_set>

using namespace hoskey::engine;

static const char* kDictDir =
    "C:/Users/Kharki/DevEcoStudioProjects/HOSKEY/entry/src/main/resources/rawfile/dict";

static std::string cp2utf8(char32_t cp) {
  std::string s;
  if (cp < 0x80) {
    s.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
  return s;
}

int main() {
  KeyboardEngine engine;
  Status s = engine.Open(kDictDir);
  if (!s.ok()) { std::printf("Open FAILED: %s\n", s.message.c_str()); return 1; }

  std::vector<char32_t> letters;
  for (char32_t c = 0x0430; c <= 0x044F; ++c) letters.push_back(c);  // а..я
  letters.push_back(0x0451);                                         // ё

  int queries = 0, v_empty = 0, v_dup = 0, v_sort = 0, v_size = 0;
  std::vector<std::string> examples;
  auto note = [&](const std::string& e) { if (examples.size() < 8) examples.push_back(e); };

  auto probe = [&](const std::string& token) {
    SuggestRequest req;
    req.token = token;
    req.input = token;
    req.max_suggestions = 6;
    SuggestResponse resp = engine.GetSuggestions(req);
    ++queries;
    std::unordered_set<std::string> seen;
    float prev = 1e30f;
    for (const auto& sg : resp.suggestions) {
      if (sg.text.empty()) { ++v_empty; note("empty@'" + token + "'"); }
      if (!seen.insert(sg.text).second) { ++v_dup; note("dup '" + sg.text + "'@'" + token + "'"); }
      if (sg.score > prev + 1e-3f) { ++v_sort; note("unsorted@'" + token + "' " + sg.text); }
      prev = sg.score;
    }
    if (resp.suggestions.size() > req.max_suggestions) { ++v_size; note("oversize@'" + token + "'"); }
  };

  for (char32_t a : letters) probe(cp2utf8(a));                                  // 1-char
  for (char32_t a : letters) for (char32_t b : letters) probe(cp2utf8(a) + cp2utf8(b));  // 2-char
  // 3-char sample: <letter> + о/а/е + т  (e.g. кот, мат, лет...)
  const char32_t mids[] = {0x043E, 0x0430, 0x0435};
  for (char32_t a : letters) for (char32_t m : mids) probe(cp2utf8(a) + cp2utf8(m) + cp2utf8(0x0442));

  // Second pass: exercise the geometry (touch) + context (bigram) code paths,
  // which the prefix-only sweep never touches. Mismatched point/char counts
  // probe the ranker's codepoint-distance math for out-of-bounds indexing.
  auto probe_geo = [&](const std::string& token, size_t npoints,
                       const std::vector<std::string>& prev) {
    SuggestRequest req;
    req.token = token;
    req.input = token;
    req.max_suggestions = 6;
    req.previous_tokens = prev;
    for (const auto& p : prev) {
      SuggestRequest::PrevWordInfo pw;
      pw.token = p;
      req.prev_words_info.push_back(pw);
    }
    req.proximity_info.enabled = true;
    req.proximity_info.keyboard_width = 1080;
    req.proximity_info.keyboard_height = 360;
    req.proximity_info.layout = "ru:alphabet";
    for (size_t i = 0; i < npoints; ++i) {
      SuggestRequest::TouchPoint tp;
      tp.x = 40.0 + static_cast<double>((i * 97) % 1000);
      tp.y = 40.0 + static_cast<double>((i * 53) % 320);
      tp.key_code = 0x0430 + static_cast<uint32_t>(i % 32);
      tp.time_ms = static_cast<uint32_t>(i * 30);
      req.points.push_back(tp);
    }
    SuggestResponse resp = engine.GetSuggestions(req);
    ++queries;
    std::unordered_set<std::string> seen;
    float prev_score = 1e30f;
    for (const auto& sg : resp.suggestions) {
      if (sg.text.empty()) { ++v_empty; note("geo-empty@'" + token + "'"); }
      if (!seen.insert(sg.text).second) { ++v_dup; note("geo-dup@'" + token + "'"); }
      if (sg.score > prev_score + 1e-3f) { ++v_sort; note("geo-unsorted@'" + token + "'"); }
      prev_score = sg.score;
    }
    if (resp.suggestions.size() > req.max_suggestions) { ++v_size; note("geo-oversize"); }
  };
  const std::vector<std::string> ctx = {
      cp2utf8(0x043A) + cp2utf8(0x043E) + cp2utf8(0x0442)};  // "кот"
  for (char32_t a : letters) for (char32_t b : letters) {
    std::string t = cp2utf8(a) + cp2utf8(b);
    probe_geo(t, 0, {});      // enabled geometry but no points
    probe_geo(t, 2, ctx);     // points == chars, with context
    probe_geo(t, 5, ctx);     // more points than chars -> length mismatch
  }
  probe_geo(std::string(200, 'a'), 250, ctx);  // long token + many points
  probe_geo("", 3, ctx);                        // empty token with points
  probe_geo("  ", 3, ctx);                      // whitespace token

  std::printf("queries=%d empty=%d dup=%d unsorted=%d oversize=%d\n",
              queries, v_empty, v_dup, v_sort, v_size);
  for (const auto& e : examples) std::printf("  e.g. %s\n", e.c_str());
  bool clean = (v_empty | v_dup | v_sort | v_size) == 0;
  std::printf(clean ? "ALL INVARIANTS HOLD\n" : "INVARIANT VIOLATIONS FOUND\n");
  return clean ? 0 : 2;
}
