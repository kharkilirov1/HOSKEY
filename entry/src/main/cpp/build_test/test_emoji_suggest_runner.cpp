#include "hosdict_engine/emoji_suggest_runner.h"

#include <cstdio>
#include <string>

using hoskey::engine::EmojiSuggestRunner;

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (condition) {
    std::printf("PASS: %s\n", message);
  } else {
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
  }
}

bool HasPrefix(const EmojiSuggestRunner::WordIds& ids,
               std::initializer_list<int> expected) {
  size_t index = 0;
  for (int value : expected) {
    if (ids[index++] != static_cast<float>(value)) return false;
  }
  for (; index < ids.size(); ++index) {
    if (ids[index] != 0.0f) return false;
  }
  return true;
}

}  // namespace

int main() {
  EmojiSuggestRunner::WordIds ids;

  Check(EmojiSuggestRunner::EncodeWord("я", &ids) &&
            HasPrefix(ids, {1, 37, 2}),
        "Russian lowercase uses recovered BOS/character/EOS ids");
  Check(EmojiSuggestRunner::EncodeWord("Я", &ids) &&
            HasPrefix(ids, {1, 37, 2}),
        "Russian uppercase is normalized");
  Check(EmojiSuggestRunner::EncodeWord("люблю", &ids) &&
            HasPrefix(ids, {1, 17, 36, 7, 17, 36, 2}),
        "multi-character word matches the recovered alphabet");
  Check(EmojiSuggestRunner::EncodeWord("Ёж", &ids) &&
            HasPrefix(ids, {1, 11, 12, 2}),
        "yo is normalized to e before encoding");
  Check(EmojiSuggestRunner::EncodeWord("по-русски", &ids) && ids[3] == 5.0f,
        "hyphen uses recovered id 5");

  Check(!EmojiSuggestRunner::EncodeWord("", &ids),
        "empty context token is rejected");
  Check(!EmojiSuggestRunner::EncodeWord("hello", &ids) &&
            HasPrefix(ids, {}),
        "unsupported alphabet is rejected without partial ids");
  Check(!EmojiSuggestRunner::EncodeWord(std::string(23, '\xD0'), &ids),
        "malformed or overlong input is rejected safely");
  Check(!EmojiSuggestRunner::EncodeWord("слово!", &ids) &&
            HasPrefix(ids, {}),
        "punctuation inside a model token is rejected");
  Check(!EmojiSuggestRunner::EncodeWord("я", nullptr),
        "null output is rejected safely");

  const std::string max_word =
      "аааааааааааааааааааааа";  // 22 characters plus BOS/EOS.
  Check(EmojiSuggestRunner::EncodeWord(max_word, &ids) && ids[23] == 2.0f,
        "22-character model limit is accepted exactly");
  Check(!EmojiSuggestRunner::EncodeWord(max_word + "а", &ids),
        "23-character token is rejected");

  if (failures != 0) {
    std::fprintf(stderr, "%d emoji suggest runner test(s) failed\n", failures);
    return 1;
  }
  std::printf("All emoji suggest frontend tests passed.\n");
  return 0;
}
