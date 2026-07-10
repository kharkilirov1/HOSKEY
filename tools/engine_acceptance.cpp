#include "keyboard_engine.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using hoskey::engine::CheckWordRequest;
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

SuggestResponse Suggest(KeyboardEngine* engine, const std::string& token,
                        size_t max_suggestions = 6) {
  SuggestRequest request;
  request.token = token;
  request.input = token;
  request.locale = "ru-RU";
  request.want_autocorrect = true;
  request.max_suggestions = max_suggestions;
  request.dictionary_sessions.push_back({"hoskey-acceptance", "ru-RU"});
  request.package_name = "com.opentech.openboard";
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

  const auto probe = Suggest(&engine, "пр");
  std::cout << "probe.pr=" << JoinSuggestions(probe) << "\n";
  std::cout << "probe.diag=" << probe.diagnostics << "\n";
  Require(probe.status.ok(), "suggest('пр') returned non-ok status", &failures);
  Require(probe.diagnostics.find("succinct=ok") != std::string::npos,
          "succinct trie is not loaded: " + probe.diagnostics, &failures);
  Require(probe.diagnostics.find("one_nc7=ok") != std::string::npos,
          "one_nc7 trie is not loaded: " + probe.diagnostics, &failures);
  Require(probe.diagnostics.find("ranker=loaded") != std::string::npos ||
              probe.diagnostics.find("ranker=unsupported") != std::string::npos,
          "ranker model asset is not visible: " + probe.diagnostics, &failures);
  Require(probe.diagnostics.find("nnlm=loaded") != std::string::npos ||
              probe.diagnostics.find("nnlm=unsupported") != std::string::npos ||
              probe.diagnostics.find("raw_vocab=") != std::string::npos,
          "neural model asset is not visible: " + probe.diagnostics, &failures);
  const bool predictor_loaded =
      probe.diagnostics.find("predictor=loaded") != std::string::npos;
  const int raw_vocab_count =
      ExtractDiagnosticInt(probe.diagnostics, "raw_vocab=");
  const bool neural_vocab_loaded = raw_vocab_count > 0;
  Require(neural_vocab_loaded,
          "raw Yandex ynnlm_v1 vocabulary was not loaded: " +
              probe.diagnostics,
          &failures);

  // Neural LM/ranker require MindSpore Lite runtime. On local test builds
  // without MINDSPORE_LITE_AVAILABLE, the compact binary predictor is the
  // word-level safety net over the raw 1nc7 subword materializer.
  Require(probe.diagnostics.find("ranker=loaded") != std::string::npos ||
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
