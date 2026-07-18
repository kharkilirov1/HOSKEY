#include "hosdict_engine/emoji_search_model.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using hoskey::engine::EmojiSearchModel;

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

std::string WithoutVariationSelector(std::string value) {
  constexpr char kVs16[] = "\xef\xb8\x8f";
  size_t position = 0;
  while ((position = value.find(kVs16, position)) != std::string::npos) {
    value.erase(position, sizeof(kVs16) - 1);
  }
  return value;
}

bool HasCanonicalPrefix(const std::vector<std::string>& actual,
                        const std::vector<std::string>& expected) {
  if (actual.size() < expected.size()) return false;
  for (size_t i = 0; i < expected.size(); ++i) {
    if (WithoutVariationSelector(actual[i]) != expected[i]) return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <search_emoji_model>\n", argv[0]);
    return 2;
  }
  std::ifstream stream(argv[1], std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                             std::istreambuf_iterator<char>());
  Check(!bytes.empty(), "reference emoji model is readable");

  EmojiSearchModel model;
  const auto load = model.Load(bytes.data(), bytes.size());
  if (!load.ok()) std::fprintf(stderr, "load error: %s\n", load.message.c_str());
  Check(load.ok(), "recovered FlatBuffer/comptrie contract loads");
  Check(model.is_loaded(), "emoji model loaded state is observable");
  Check(model.key_count() == 21804, "all 21,804 query keys are decoded");
  Check(model.group_count() == 6450, "all 6,450 groups are decoded");

  const auto cat = model.Search(u8"кот", 10);
  Check(HasCanonicalPrefix(
            cat.emojis,
            {u8"😻", u8"😹", u8"😽", u8"🐈", u8"😿", u8"😼",
             u8"🙀", u8"😸", u8"😾", u8"🐱"}),
        "cat top-10 matches the live native oracle");
  const auto smile = model.Search(u8"улыбка", 10);
  Check(HasCanonicalPrefix(
            smile.emojis,
            {u8"😅", u8"☺", u8"🙂", u8"😬", u8"🤣", u8"😃",
             u8"🙃", u8"😊", u8"😁", u8"😼"}),
        "smile top-10 matches the live native oracle");
  const auto heart = model.Search(u8"сердце", 10);
  Check(HasCanonicalPrefix(
            heart.emojis,
            {u8"🤍", u8"💛", u8"💚", u8"🤎", u8"❤", u8"💜",
             u8"🧡", u8"💔", u8"💖", u8"♥"}),
        "heart top-10 matches the live native oracle");
  const auto car = model.Search(u8"машина", 10);
  Check(HasCanonicalPrefix(
            car.emojis,
            {u8"🚘", u8"🚗", u8"🚖", u8"🚕", u8"🚚", u8"🚌",
             u8"🏎", u8"🚒", u8"🚓", u8"🚛"}),
        "car top-10 matches the live native oracle");
  const auto fire = model.Search(u8"огонь", 64);
  Check(HasCanonicalPrefix(
            fire.emojis,
            {u8"🔥", u8"📛", u8"🚒", u8"🧯", u8"🎇", u8"🚨",
             u8"🎆", u8"🕯", u8"♨"}),
        "fire complete result matches the live native oracle");
  Check(fire.emojis.size() == 9, "fire route has no fabricated fallback items");

  const auto normalized = model.Search(u8"  КОТ  ", 10);
  Check(normalized.emojis == cat.emojis,
        "ASCII whitespace and Russian uppercase normalize deterministically");
  Check(model.invocation_count() == 6, "model invocation counter advances");
  Check(model.scored_count() >= 59, "model scored counter advances");
  Check(model.diagnostics().find("comptrie:utf16be") != std::string::npos,
        "diagnostics identify the recovered route");

  EmojiSearchModel truncated;
  const auto rejected = truncated.Load(bytes.data(), 1000);
  Check(!rejected.ok(), "truncated model is rejected");
  Check(!truncated.is_loaded(), "failed load leaves no stale model state");

  if (failures != 0) {
    std::fprintf(stderr, "%d emoji search checks failed\n", failures);
    return 1;
  }
  std::printf("All emoji search model checks passed.\n");
  return 0;
}
