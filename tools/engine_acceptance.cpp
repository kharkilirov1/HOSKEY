#include "keyboard_engine.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

using hoskey::engine::CheckWordRequest;
using hoskey::engine::EmojiSearchRequest;
using hoskey::engine::EmojiSuggestRequest;
using hoskey::engine::KeyboardEngine;
using hoskey::engine::SuggestRequest;
using hoskey::engine::SuggestResponse;

namespace {

struct Failure {
  std::string message;
};

std::string JoinSuggestions(const SuggestResponse& response) {
  std::string out;
  for (size_t i = 0; i < response.suggestions.size(); ++i) {
    if (i != 0) out += "|";
    out += response.suggestions[i].text + ":" + response.suggestions[i].tag;
  }
  return out;
}

bool HasSuggestion(const SuggestResponse& response, const std::string& text) {
  return std::any_of(response.suggestions.begin(), response.suggestions.end(),
                     [&](const auto& s) { return s.text == text; });
}

bool HasAnySuggestion(const SuggestResponse& response,
                      std::initializer_list<std::string_view> texts) {
  return std::any_of(response.suggestions.begin(), response.suggestions.end(),
                     [&](const auto& s) {
                       return std::any_of(
                           texts.begin(), texts.end(),
                           [&](std::string_view text) { return s.text == text; });
                     });
}

void Require(bool condition, const std::string& message,
             std::vector<Failure>* failures) {
  if (!condition) failures->push_back({message});
}

int ExtractDiagnosticInt(const std::string& diagnostics,
                         const std::string& marker) {
  const size_t begin = diagnostics.find(marker);
  if (begin == std::string::npos) return -1;
  size_t pos = begin + marker.size();
  int value = 0;
  bool has_digit = false;
  while (pos < diagnostics.size() &&
         std::isdigit(static_cast<unsigned char>(diagnostics[pos]))) {
    has_digit = true;
    value = value * 10 + (diagnostics[pos] - '0');
    ++pos;
  }
  return has_digit ? value : -1;
}

int ExtractRouteDiagnosticInt(const std::string& diagnostics,
                              const std::string& route,
                              const std::string& field) {
  const size_t route_begin = diagnostics.find(route + "={");
  if (route_begin == std::string::npos) return -1;
  const size_t route_end = diagnostics.find('}', route_begin);
  const std::string marker = field + "=";
  const size_t field_begin = diagnostics.find(marker, route_begin);
  if (field_begin == std::string::npos ||
      (route_end != std::string::npos && field_begin > route_end)) {
    return -1;
  }
  return ExtractDiagnosticInt(diagnostics.substr(field_begin), marker);
}

SuggestResponse Suggest(KeyboardEngine* engine, const std::string& token,
                         size_t max_suggestions = 6,
                         bool disable_ynnlm = false,
                         bool disable_autocc_model = false,
                         const std::string& autocc_variant = "default") {
  SuggestRequest request;
  request.token = token;
  request.input = token;
  request.locale = "ru-RU";
  request.want_autocorrect = true;
  request.max_suggestions = max_suggestions;
  request.dictionary_sessions.push_back({"hoskey-acceptance", "ru-RU"});
  request.package_name = "com.opentech.openboard";
  request.disable_ynnlm = disable_ynnlm;
  request.disable_autocc_model = disable_autocc_model;
  request.autocc_variant = autocc_variant;
  return engine->GetSuggestions(request);
}

SuggestResponse Swipe(KeyboardEngine* engine, const std::u32string& word,
                      bool disable_rnnt = false) {
  constexpr double kWidth = 1000.0;
  constexpr double kHeight = 400.0;
  SuggestRequest request;
  request.token = "п";
  request.input = request.token;
  request.locale = "ru-RU";
  request.is_gesture = true;
  request.want_autocorrect = false;
  request.max_suggestions = 12;
  request.disable_rnnt = disable_rnnt;
  request.proximity_info = {"ru:alphabet", static_cast<uint32_t>(kWidth),
                            static_cast<uint32_t>(kHeight), true};

  hoskey::engine::SwipePoint previous;
  bool has_previous = false;
  uint32_t time_ms = 100;
  for (const char32_t codepoint : word) {
    hoskey::engine::SwipePoint center;
    if (!hoskey::engine::RussianSwipeKeyCenter(codepoint, kWidth, kHeight,
                                                &center)) {
      continue;
    }
    if (!has_previous) {
      request.points.push_back(
          {center.x, center.y, time_ms, static_cast<uint32_t>(codepoint)});
      time_ms += 24;
    } else {
      for (int step = 1; step <= 4; ++step) {
        const double t = static_cast<double>(step) / 4.0;
        request.points.push_back(
            {previous.x + (center.x - previous.x) * t,
             previous.y + (center.y - previous.y) * t, time_ms,
             0x20000000u | static_cast<uint32_t>(word.front())});
        time_ms += 24;
      }
    }
    previous = center;
    has_previous = true;
  }
  if (has_previous) {
    request.points.push_back(
        {previous.x, previous.y, time_ms, 0x10000000u |
                                           static_cast<uint32_t>(word.front())});
  }
  return engine->GetSuggestions(request);
}

}  // namespace

int main(int argc, char** argv) {
  namespace fs = std::filesystem;

  const fs::path package_dir =
      argc > 1 ? fs::path(argv[1])
               : fs::path(
                     R"(C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\src\main\resources\rawfile\dict)");

  std::vector<Failure> failures;
  Require(!fs::exists(package_dir / "words.txt"),
          "runtime package still contains external words.txt", &failures);

  KeyboardEngine missing_directory_engine;
  const auto missing_directory_status = missing_directory_engine.Open(
      package_dir / "__hoskey_missing_directory_acceptance__");
  Require(!missing_directory_status.ok() &&
              missing_directory_status.message.find("not found") !=
                  std::string::npos,
          "missing dictionary directory must return an explicit status, not throw",
          &failures);

  KeyboardEngine engine;
  const auto open_status = engine.Open(package_dir);
  if (!open_status.ok()) {
    std::cerr << "FAIL open: " << open_status.message << "\n";
    return 2;
  }
  Require(engine.package().container().FindAsset("lexicon.words") == nullptr,
          "container still contains lexicon.words text bridge", &failures);
  Require(engine.package().container().FindAsset("lexicon.default") != nullptr,
          "container does not contain lexicon.default", &failures);
  Require(engine.package().retained_source_payload_bytes() == 0,
          "parsed package retained duplicate source payloads", &failures);

  EmojiSearchRequest emoji_request;
  emoji_request.query = "кот";
  emoji_request.max_results = 10;
  const auto emoji_on = engine.SearchEmojis(emoji_request);
  std::cout << "probe.emoji=";
  for (const auto& emoji : emoji_on.emojis) std::cout << emoji << "|";
  std::cout << "\nprobe.emoji.diag=" << emoji_on.diagnostics << "\n";
  Require(emoji_on.status.ok() && emoji_on.model_engaged &&
              emoji_on.emojis.size() == 10 && emoji_on.emojis.front() == "😻",
          "emoji compact-trie model did not produce the native cat route: " +
              emoji_on.diagnostics,
          &failures);
  for (const char* field : {"asset=present", "contract=valid",
                            "variant=default", "load=loaded",
                            "invoke_count=", "scored_count=", "fallback=none",
                            "latency_us=", "route=utf16be_comptrie_prefix"}) {
    Require(emoji_on.diagnostics.find(field) != std::string::npos,
            std::string("strict emoji diagnostic field missing: ") + field +
                " in " + emoji_on.diagnostics,
            &failures);
  }
  const int emoji_invoke_before =
      ExtractDiagnosticInt(emoji_on.diagnostics, "invoke_count=");
  emoji_request.disable_model = true;
  const auto emoji_off = engine.SearchEmojis(emoji_request);
  const int emoji_invoke_after =
      ExtractDiagnosticInt(emoji_off.diagnostics, "invoke_count=");
  Require(emoji_off.status.ok() && !emoji_off.model_engaged &&
              emoji_off.emojis.empty() &&
              emoji_off.diagnostics.find("fallback=diagnostic_bypass") !=
                  std::string::npos,
          "emoji OFF request did not expose an empty diagnostic bypass: " +
              emoji_off.diagnostics,
          &failures);
  Require(emoji_invoke_before == 1 && emoji_invoke_after == emoji_invoke_before,
          "emoji OFF request changed the model invocation counter", &failures);

  EmojiSearchRequest fire_request;
  fire_request.query = "огонь";
  const auto emoji_fire = engine.SearchEmojis(fire_request);
  Require(emoji_fire.status.ok() && emoji_fire.model_engaged &&
              emoji_fire.emojis.size() == 9 &&
              emoji_fire.emojis.front() == "🔥",
          "emoji fire route diverged from the native model", &failures);

  const auto probe = Suggest(&engine, "пр");
  std::cout << "probe.pr=" << JoinSuggestions(probe) << "\n";
  std::cout << "probe.diag=" << probe.diagnostics << "\n";
  Require(probe.status.ok(), "suggest('пр') returned non-ok status", &failures);
  Require(probe.diagnostics.find("succinct=ok") != std::string::npos,
          "succinct trie is not loaded: " + probe.diagnostics, &failures);
  Require(probe.diagnostics.find("one_nc7=ok") != std::string::npos,
          "one_nc7 trie is not loaded: " + probe.diagnostics, &failures);
  Require(probe.diagnostics.find("ranker=loaded") != std::string::npos ||
              probe.diagnostics.find("ranker=mn_loaded") != std::string::npos ||
              probe.diagnostics.find("ranker=unsupported") != std::string::npos,
          "ranker model asset is not visible: " + probe.diagnostics, &failures);
  Require(probe.diagnostics.find("nnlm=loaded") != std::string::npos ||
              probe.diagnostics.find("nnlm=unsupported") != std::string::npos ||
              probe.diagnostics.find("raw_vocab=") != std::string::npos,
          "neural model asset is not visible: " + probe.diagnostics, &failures);
  for (const char* field : {
           "ynnlm_route={asset=", "contract=", "variant=", "load=",
           "invoke_count=", "scored_count=", "fallback=", "latency_us="}) {
    Require(probe.diagnostics.find(field) != std::string::npos,
            std::string("strict NNLM diagnostic field missing: ") + field +
                " in " + probe.diagnostics,
            &failures);
  }
  const bool predictor_loaded =
      probe.diagnostics.find("predictor=loaded") != std::string::npos;
  const int raw_vocab_count =
      ExtractDiagnosticInt(probe.diagnostics, "raw_vocab=");
  const bool neural_vocab_loaded = raw_vocab_count > 0;
  Require(neural_vocab_loaded,
          "raw Yandex ynnlm_v1 vocabulary was not loaded: " +
              probe.diagnostics,
          &failures);

  const int ynnlm_invoke_before = ExtractRouteDiagnosticInt(
      probe.diagnostics, "ynnlm_route", "invoke_count");
  const auto ynnlm_off = Suggest(&engine, "пр", 6, true);
  std::cout << "probe.pr.ynnlm_off=" << JoinSuggestions(ynnlm_off) << "\n";
  std::cout << "probe.pr.ynnlm_off.diag=" << ynnlm_off.diagnostics << "\n";
  const int ynnlm_invoke_after = ExtractRouteDiagnosticInt(
      ynnlm_off.diagnostics, "ynnlm_route", "invoke_count");
  Require(ynnlm_off.status.ok() && !ynnlm_off.suggestions.empty(),
          "NNLM-off request did not retain the static suggestion path: " +
              ynnlm_off.diagnostics,
          &failures);
  Require(ynnlm_off.diagnostics.find("ynnlm_skipped=debug_disabled") !=
              std::string::npos &&
              ynnlm_off.diagnostics.find(
                  "fallback=disabled:debug_switch") != std::string::npos,
          "NNLM-off request did not expose the explicit debug bypass: " +
              ynnlm_off.diagnostics,
          &failures);
  Require(ynnlm_invoke_before >= 0 &&
              ynnlm_invoke_after == ynnlm_invoke_before,
          "NNLM-off request changed the cumulative invoke counter: before=" +
              std::to_string(ynnlm_invoke_before) +
              " after=" + std::to_string(ynnlm_invoke_after),
          &failures);

  const auto swipe = Swipe(&engine, U"привет");
  std::cout << "probe.swipe=" << JoinSuggestions(swipe) << "\n";
  std::cout << "probe.swipe.diag=" << swipe.diagnostics << "\n";
  const int swipe_pool = ExtractDiagnosticInt(swipe.diagnostics, "swipe_pool=");
  const int swipe_model_scored =
      ExtractDiagnosticInt(swipe.diagnostics, "swipe_model_scored=");
  Require(swipe.status.ok() &&
              swipe.diagnostics.find("swipe=matrixnet_backoff_v1") !=
                  std::string::npos,
          "synthetic swipe did not execute MatrixNet backoff: " +
              swipe.diagnostics,
          &failures);
  Require(swipe_model_scored > 0 &&
              swipe.diagnostics.find("swipe_blocker=") != std::string::npos &&
              swipe.diagnostics.find(":allow") != std::string::npos &&
              swipe.diagnostics.find("swipe_full=rnnt_runtime_failed") !=
                  std::string::npos &&
              swipe.diagnostics.find(
                  "rnnt_components=unsupported:mindspore_lite_unavailable") !=
                  std::string::npos,
          "host swipe fallback/runtime diagnostics are inconsistent: " +
              swipe.diagnostics,
          &failures);
  for (const char* field : {
           "rnnt_route={asset=", "contract=", "variant=", "load=",
           "invoke_count=", "scored_count=", "fallback=", "latency_us="}) {
    Require(swipe.diagnostics.find(field) != std::string::npos,
            std::string("strict RNNT diagnostic field missing: ") + field +
                " in " + swipe.diagnostics,
            &failures);
  }
  Require(HasSuggestion(swipe, "привет"),
          "synthetic привет path did not retain привет: " +
              JoinSuggestions(swipe),
          &failures);
  Require(swipe_pool > 0 && swipe_pool < raw_vocab_count,
          "swipe endpoint index did not reduce the vocabulary pool: " +
              swipe.diagnostics,
          &failures);

  const int rnnt_invoke_before = ExtractRouteDiagnosticInt(
      swipe.diagnostics, "rnnt_route", "invoke_count");
  const auto swipe_rnnt_off = Swipe(&engine, U"привет", true);
  std::cout << "probe.swipe.rnnt_off=" << JoinSuggestions(swipe_rnnt_off)
            << "\n";
  std::cout << "probe.swipe.rnnt_off.diag="
            << swipe_rnnt_off.diagnostics << "\n";
  const int rnnt_invoke_after = ExtractRouteDiagnosticInt(
      swipe_rnnt_off.diagnostics, "rnnt_route", "invoke_count");
  Require(swipe_rnnt_off.status.ok() &&
              swipe_rnnt_off.diagnostics.find("swipe_full=disabled") !=
                  std::string::npos &&
              swipe_rnnt_off.diagnostics.find(
                  "fallback=disabled:debug_switch") != std::string::npos,
          "RNNT-off request did not expose the explicit debug bypass: " +
              swipe_rnnt_off.diagnostics,
          &failures);
  Require(rnnt_invoke_before >= 0 &&
              rnnt_invoke_after == rnnt_invoke_before,
          "RNNT-off request changed the cumulative invoke counter: before=" +
              std::to_string(rnnt_invoke_before) +
              " after=" + std::to_string(rnnt_invoke_after),
          &failures);

  SuggestRequest invalid_swipe;
  invalid_swipe.token = "п";
  invalid_swipe.input = "п";
  invalid_swipe.locale = "ru-RU";
  invalid_swipe.is_gesture = true;
  invalid_swipe.want_autocorrect = false;
  invalid_swipe.proximity_info = {"ru:alphabet", 1000, 400, true};
  const SuggestResponse empty_swipe = engine.GetSuggestions(invalid_swipe);
  Require(empty_swipe.status.ok() && empty_swipe.suggestions.empty() &&
              empty_swipe.diagnostics.find(
                  "swipe_input=invalid:empty_coordinates") !=
                  std::string::npos,
          "empty RNNT coordinates did not fail closed: " +
              empty_swipe.diagnostics,
          &failures);
  invalid_swipe.points = {
      {std::numeric_limits<double>::quiet_NaN(), 100.0, 100, 1087},
      {200.0, 100.0, 120, 0x20000000u | 1088u},
      {300.0, 100.0, 140, 0x10000000u | 1080u}};
  const SuggestResponse nan_swipe = engine.GetSuggestions(invalid_swipe);
  Require(nan_swipe.status.ok() && nan_swipe.suggestions.empty() &&
              nan_swipe.diagnostics.find(
                  "swipe_input=invalid:nonfinite_coordinates") !=
                  std::string::npos,
          "NaN RNNT coordinates did not fail closed: " +
              nan_swipe.diagnostics,
          &failures);
  invalid_swipe.points = {
      {100.0, 100.0, 100, 1087},
      {200.0, 100.0, 120, 0x20000000u | 1088u},
      {300.0, 100.0, 140, 0x10000000u | 1080u}};
  invalid_swipe.proximity_info.layout = "en:alphabet";
  const SuggestResponse layout_swipe = engine.GetSuggestions(invalid_swipe);
  Require(layout_swipe.status.ok() && layout_swipe.suggestions.empty() &&
              layout_swipe.diagnostics.find(
                  "swipe_input=invalid:unsupported_layout") !=
                  std::string::npos,
          "unsupported RNNT layout did not fail closed: " +
              layout_swipe.diagnostics,
          &failures);

  // Neural LM/ranker require MindSpore Lite runtime. On local test builds
  // without MINDSPORE_LITE_AVAILABLE, the compact binary predictor is the
  // word-level safety net over the raw 1nc7 subword materializer.
  Require(probe.diagnostics.find("ranker=loaded") != std::string::npos ||
              probe.diagnostics.find("ranker=mn_loaded") != std::string::npos ||
              probe.diagnostics.find("ranker=unsupported") != std::string::npos,
          "ranker model asset is not visible: " + probe.diagnostics, &failures);
  Require(probe.diagnostics.find("nnlm=loaded") != std::string::npos ||
              probe.diagnostics.find("nnlm=unsupported") != std::string::npos ||
              neural_vocab_loaded,
          "neural model asset is not visible: " + probe.diagnostics, &failures);
  if (predictor_loaded) {
    Require(HasSuggestion(probe, "про") && HasSuggestion(probe, "привет"),
            "prefix 'пр' did not return word-level predictor suggestions: " +
                JoinSuggestions(probe),
            &failures);
  } else if (neural_vocab_loaded) {
    Require(HasAnySuggestion(probe, {"привет", "привык", "прусь", "пруфы",
                                     "прыща", "прыщи", "пряди", "пряма",
                                     "прещения", "праматер", "праистория"}),
            "prefix 'пр' returned neither raw neural-vocab nor 1nc7 completions: " +
                JoinSuggestions(probe),
            &failures);
  } else {
    Require(HasSuggestion(probe, "пралине") || HasSuggestion(probe, "прашива") ||
                HasSuggestion(probe, "прароди") || HasSuggestion(probe, "прэтт") ||
                HasSuggestion(probe, "прещения") || HasSuggestion(probe, "праматер") ||
                HasSuggestion(probe, "праистория"),
            "prefix 'пр' returned no materialized 1nc7 completions: " +
                JoinSuggestions(probe),
            &failures);
  }

  const auto st = Suggest(&engine, "ст");
  std::cout << "probe.st=" << JoinSuggestions(st) << "\n";
  if (predictor_loaded) {
    Require(HasSuggestion(st, "стал") || HasSuggestion(st, "стоит"),
            "prefix 'ст' did not return word-level predictor suggestions: " +
                JoinSuggestions(st),
            &failures);
  } else if (neural_vocab_loaded) {
    Require(HasAnySuggestion(st, {"стал", "став", "стад", "стаж", "стан",
                                  "стар", "стэплтон", "статкевич",
                                  "стоэтажный"}),
            "prefix 'ст' returned neither raw neural-vocab nor 1nc7 completions: " +
                JoinSuggestions(st),
            &failures);
  } else {
    Require(HasSuggestion(st, "ство") || HasSuggestion(st, "стеви") ||
                HasSuggestion(st, "стича") || HasSuggestion(st, "стюар") ||
                HasSuggestion(st, "стэплтон") || HasSuggestion(st, "статкевич") ||
                HasSuggestion(st, "стоэтажный"),
            "prefix 'ст' returned no materialized 1nc7 completions: " +
                JoinSuggestions(st),
            &failures);
  }

  const auto ne = Suggest(&engine, "не");
  std::cout << "probe.ne=" << JoinSuggestions(ne) << "\n";
  Require(!ne.suggestions.empty(),
          "prefix 'не' returned no completions: " + JoinSuggestions(ne),
          &failures);
  Require(!HasSuggestion(ne, "неавтоматизиро") &&
              !HasSuggestion(ne, "неаргументиров"),
          "prefix 'не' exposed a clipped iterator fragment: " +
              JoinSuggestions(ne),
          &failures);

  const auto hor = Suggest(&engine, "хор");
  std::cout << "probe.hor=" << JoinSuggestions(hor) << "\n";
  if (predictor_loaded) {
    Require(HasSuggestion(hor, "хорош") || HasSuggestion(hor, "хорошо"),
            "prefix 'хор' did not return word-level predictor suggestions: " +
                JoinSuggestions(hor),
            &failures);
  } else if (neural_vocab_loaded) {
    Require(HasAnySuggestion(hor, {"хорош", "хорошо", "хорда", "хорек",
                                   "хором", "хорхе", "хореограф"}),
            "prefix 'хор' returned neither raw neural-vocab nor 1nc7 completions: " +
                JoinSuggestions(hor),
            &failures);
  } else {
    Require(HasSuggestion(hor, "хорват") || HasSuggestion(hor, "хоррор") ||
                HasSuggestion(hor, "хорист") || HasSuggestion(hor, "хорезм") ||
                HasSuggestion(hor, "хорхе") || HasSuggestion(hor, "хореограф") ||
                HasSuggestion(hor, "хорохорит"),
            "prefix 'хор' returned no materialized 1nc7 completions: " +
                JoinSuggestions(hor),
            &failures);
  }

  const auto manual = Suggest(&engine, "вообщем");
  std::cout << "probe.voobshem=" << JoinSuggestions(manual)
            << " will=" << (manual.will_autocorrect ? "1" : "0") << "\n";
  Require(!manual.suggestions.empty() && manual.suggestions[0].text == "в общем",
          "manual autocorrect 'вообщем' is not top: " + JoinSuggestions(manual),
          &failures);
  Require(manual.will_autocorrect,
          "manual autocorrect 'вообщем' did not set will_autocorrect",
          &failures);

  const auto fuzzy = Suggest(&engine, "алв");
  std::cout << "probe.alv=" << JoinSuggestions(fuzzy)
            << " will=" << (fuzzy.will_autocorrect ? "1" : "0") << "\n";
  Require(!fuzzy.will_autocorrect,
          "unsafe fuzzy typo 'алв' should not autocorrect: " +
              JoinSuggestions(fuzzy),
          &failures);

  const auto autocc_default = Suggest(&engine, "превет");
  const auto autocc_off = Suggest(&engine, "превет", 6, false, true);
  const auto autocc_k13321 =
      Suggest(&engine, "превет", 6, false, false, "KEYBOARD-13321");
  const auto autocc_k13421 =
      Suggest(&engine, "превет", 6, false, false, "KEYBOARD-13421");
  std::cout << "probe.autocc.default=" << JoinSuggestions(autocc_default)
            << " will=" << (autocc_default.will_autocorrect ? "1" : "0")
            << " diag=" << autocc_default.diagnostics << "\n";
  std::cout << "probe.autocc.off=" << JoinSuggestions(autocc_off)
            << " will=" << (autocc_off.will_autocorrect ? "1" : "0")
            << " diag=" << autocc_off.diagnostics << "\n";
  std::cout << "probe.autocc.k13321=" << JoinSuggestions(autocc_k13321)
            << " will=" << (autocc_k13321.will_autocorrect ? "1" : "0")
            << " diag=" << autocc_k13321.diagnostics << "\n";
  std::cout << "probe.autocc.k13421=" << JoinSuggestions(autocc_k13421)
            << " will=" << (autocc_k13421.will_autocorrect ? "1" : "0")
            << " diag=" << autocc_k13421.diagnostics << "\n";
  // Host acceptance deliberately has no MindSpore Lite runtime.  The forest
  // must not run with fabricated neural-char features; the direct forest
  // fixture test covers HOBL execution, while device acceptance covers this
  // producer + forest composition.
  Require(autocc_default.diagnostics.find(
              "autocc_model_path=producer_unavailable") != std::string::npos &&
              autocc_default.diagnostics.find(
                  "autocc_producer_contract=unavailable") != std::string::npos &&
              autocc_default.diagnostics.find(
                  "autocc_model_attempted=0") != std::string::npos,
          "Autocc host path did not refuse fabricated neural features: " +
              autocc_default.diagnostics,
          &failures);
  Require(autocc_off.diagnostics.find("autocc_model_path=debug_disabled") !=
              std::string::npos,
          "Autocc A/B disable switch did not bypass only the model path",
          &failures);
  Require(autocc_k13321.diagnostics.find("autocc_variant=KEYBOARD-13321") !=
              std::string::npos &&
              autocc_k13321.diagnostics.find(
                  "autocc_model_path=producer_unavailable") !=
                  std::string::npos,
          "Autocc K13321 host route lost variant or producer gate",
          &failures);
  Require(autocc_k13421.diagnostics.find("autocc_variant=KEYBOARD-13421") !=
              std::string::npos &&
              autocc_k13421.diagnostics.find(
                  "autocc_model_path=producer_unavailable") !=
                  std::string::npos,
          "Autocc K13421 host route lost variant or producer gate",
          &failures);
  Require(JoinSuggestions(autocc_default) == JoinSuggestions(autocc_off) &&
              JoinSuggestions(autocc_default) == JoinSuggestions(autocc_k13321) &&
              JoinSuggestions(autocc_default) == JoinSuggestions(autocc_k13421),
          "Autocc A/B changed the candidate pool instead of only policy",
          &failures);
  Require(autocc_default.will_autocorrect ==
              autocc_k13421.will_autocorrect,
          "Yandex 122.5 default/K13421 promoted model alias diverged",
          &failures);
  Require(autocc_default.diagnostics.find("forest_contract=exact_hobl79") !=
              std::string::npos &&
              autocc_default.diagnostics.find("autocc_probability=") ==
                  std::string::npos &&
              autocc_k13321.diagnostics.find("autocc_probability=") ==
                  std::string::npos &&
              autocc_k13421.diagnostics.find("autocc_probability=") ==
                  std::string::npos,
          "Autocc host path emitted a model decision without neural producer",
          &failures);

  SuggestRequest sensitive_request;
  sensitive_request.token = "превет";
  sensitive_request.input = sensitive_request.token;
  sensitive_request.previous_tokens = {"это", "секрет"};
  sensitive_request.is_gesture = true;
  sensitive_request.points.push_back({10.0, 20.0, 1, 1087});
  sensitive_request.sensitive_context = true;
  const auto counters_before_sensitive = engine.model_invocation_snapshot();
  const auto sensitive = engine.GetSuggestions(sensitive_request);
  const auto counters_after_sensitive = engine.model_invocation_snapshot();
  Require(sensitive.status.ok() && sensitive.suggestions.empty() &&
              sensitive.diagnostics ==
                  "engine=hoskey, privacy=sensitive_blocked",
          "native sensitive-context gate allowed suggestion/model processing",
          &failures);
  Require(counters_before_sensitive == counters_after_sensitive,
          "native sensitive-context gate changed neural invocation counters",
          &failures);

  EmojiSuggestRequest sensitive_emoji_request;
  sensitive_emoji_request.completed_words = {"это", "секрет"};
  sensitive_emoji_request.sensitive_context = true;
  sensitive_emoji_request.generation = 9001;
  const auto counters_before_sensitive_emoji =
      engine.model_invocation_snapshot();
  const auto sensitive_emoji =
      engine.SuggestEmojis(sensitive_emoji_request);
  const auto counters_after_sensitive_emoji =
      engine.model_invocation_snapshot();
  Require(sensitive_emoji.status.ok() && sensitive_emoji.candidates.empty() &&
              !sensitive_emoji.model_engaged &&
              sensitive_emoji.diagnostics.find(
                  "fallback=privacy_suppressed") != std::string::npos,
          "native sensitive-context gate allowed context emoji processing",
          &failures);
  Require(counters_before_sensitive_emoji == counters_after_sensitive_emoji,
          "context emoji sensitive gate changed neural invocation counters",
          &failures);

  const auto exact = Suggest(&engine, "она");
  std::cout << "probe.ona=" << JoinSuggestions(exact)
            << " will=" << (exact.will_autocorrect ? "1" : "0") << "\n";
  Require(!exact.suggestions.empty(),
          "exact word 'она' returned no suggestions: " + JoinSuggestions(exact),
          &failures);
  Require(!exact.will_autocorrect,
          "valid native prefix/exact-like input 'она' should not autocorrect: " +
              JoinSuggestions(exact),
          &failures);
  Require(!HasSuggestion(exact, "зна"),
          "valid native prefix/exact-like input 'она' should not surface tfm correction 'зна': " +
              JoinSuggestions(exact),
          &failures);

  auto check = engine.CheckWord(CheckWordRequest{"привет", "ru-RU"});
  std::cout << "check.privet known=" << (check.is_known ? "1" : "0")
            << " diag=" << check.diagnostics << "\n";
  if (predictor_loaded || neural_vocab_loaded) {
    Require(check.status.ok() && check.is_known,
            "checkWord('привет') should be known with predictor or raw Yandex neural vocab loaded",
            &failures);
  }

  auto native_exact = engine.CheckWord(CheckWordRequest{"хор", "ru-RU"});
  std::cout << "check.hor known=" << (native_exact.is_known ? "1" : "0")
            << " diag=" << native_exact.diagnostics << "\n";
  Require(native_exact.status.ok() && native_exact.is_known,
          "checkWord('хор') should use native 1nc7 exact iterator lookup",
          &failures);

  auto prefix_only = engine.CheckWord(CheckWordRequest{"прив", "ru-RU"});
  std::cout << "check.priv_prefix known=" << (prefix_only.is_known ? "1" : "0")
            << " diag=" << prefix_only.diagnostics << "\n";
  Require(prefix_only.status.ok() && !prefix_only.is_known,
          "checkWord('прив') incorrectly treats a prefix as an exact word",
          &failures);

  auto subword_only = engine.CheckWord(CheckWordRequest{"приве", "ru-RU"});
  std::cout << "check.prive_subword known="
            << (subword_only.is_known ? "1" : "0")
            << " diag=" << subword_only.diagnostics << "\n";
  Require(subword_only.status.ok() && !subword_only.is_known,
          "checkWord('приве') incorrectly treats an ynnlm subword as an exact word",
          &failures);

  if (!failures.empty()) {
    std::cerr << "\nACCEPTANCE FAILURES: " << failures.size() << "\n";
    for (const auto& failure : failures) {
      std::cerr << " - " << failure.message << "\n";
    }
    return 1;
  }

  std::cout << "\nACCEPTANCE OK\n";
  return 0;
}
