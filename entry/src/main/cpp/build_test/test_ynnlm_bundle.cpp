#include "hosdict_engine/hosdict_container.h"
#include "hosdict_engine/ynnlm_bundle.h"

#include <cstdint>
#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

using namespace hoskey::engine;

static int g_failed = 0;

static void Check(bool ok, const char* message) {
  if (!ok) {
    std::printf("FAIL: %s\n", message);
    ++g_failed;
  } else {
    std::printf("PASS: %s\n", message);
  }
}

static std::string U(std::initializer_list<uint8_t> bytes) {
  return std::string(reinterpret_cast<const char*>(bytes.begin()), bytes.size());
}

int main() {
  HosdictContainer container;
  Status st = container.Open(
      "C:/Users/Kharki/DevEcoStudioProjects/HOSKEY/entry/src/main/resources/rawfile/dict/ru.hoskey.hosdict");
  if (!st.ok()) {
    std::printf("FAIL: open container: %s\n", st.message.c_str());
    return 1;
  }

  const uint8_t* data = nullptr;
  size_t size = 0;
  st = container.GetAssetData("neural.word_model.raw", &data, &size);
  if (!st.ok()) {
    std::printf("FAIL: get neural.word_model.raw: %s\n", st.message.c_str());
    return 1;
  }

  YnnlmV1Bundle bundle;
  st = bundle.Parse(data, size);
  if (!st.ok()) {
    std::printf("FAIL: parse bundle: %s\n", st.message.c_str());
    return 1;
  }

  Check(bundle.order() == 10, "Order == 10");
  Check(bundle.max_word_len() == 24, "MaxWordLen == 24");
  Check(bundle.word_count() == 107989, "word_count == 107989");
  Check(bundle.subword_count() == 7000, "subword_count == 7000");

  const auto& wr2l = bundle.word_rank_to_lex();
  const auto& wl2r = bundle.word_lex_to_rank();
  bool word_inverse = wr2l.size() == bundle.word_count() &&
                      wl2r.size() == bundle.word_count();
  for (size_t i = 0; word_inverse && i < wr2l.size(); ++i) {
    word_inverse = wr2l[i] < wl2r.size() && wl2r[wr2l[i]] == i;
  }
  Check(word_inverse, "word permutation maps are mutual inverses");

  const auto& sr2l = bundle.subword_rank_to_lex();
  const auto& sl2r = bundle.subword_lex_to_rank();
  bool subword_inverse = sr2l.size() == bundle.subword_count() &&
                         sl2r.size() == bundle.subword_count();
  for (size_t i = 0; subword_inverse && i < sr2l.size(); ++i) {
    subword_inverse = sr2l[i] < sl2r.size() && sl2r[sr2l[i]] == i;
  }
  Check(subword_inverse, "subword permutation maps are mutual inverses");

  const std::string and_ru = U({0xD0, 0xB8});  // и
  const std::string in_ru = U({0xD0, 0xB2});   // в
  const std::string not_ru = U({0xD0, 0xBD, 0xD0, 0xB5});  // не
  const std::string on_ru = U({0xD0, 0xBD, 0xD0, 0xB0});   // на
  const std::string what_ru =
      U({0xD1, 0x87, 0xD1, 0x82, 0xD0, 0xBE});  // что
  const std::string tail1 =
      U({0xD0, 0xB3, 0xD1, 0x80, 0xD0, 0xB5, 0xD0, 0xB1, 0xD0, 0xB5,
         0xD0, 0xBD, 0xD1, 0x8E, 0xD0, 0xBA});  // гребенюк
  const std::string tail2 =
      U({0xD1, 0x8E, 0xD1, 0x80, 0xD1, 0x8C, 0xD0, 0xB5, 0xD0, 0xB2,
         0xD0, 0xBE});  // юрьево

  const int tail_rank = std::min(bundle.WordRank(tail1), bundle.WordRank(tail2));
  Check(bundle.WordRank(and_ru) >= 0 && bundle.WordRank(and_ru) < tail_rank,
        "rank(и) is much smaller than tail words");
  Check(bundle.WordRank(in_ru) >= 0 && bundle.WordRank(in_ru) < tail_rank,
        "rank(в) is much smaller than tail words");
  Check(bundle.WordRank(not_ru) >= 0 && bundle.WordRank(not_ru) < tail_rank,
        "rank(не) is much smaller than tail words");
  Check(bundle.WordRank(on_ru) >= 0 && bundle.WordRank(on_ru) < tail_rank,
        "rank(на) is much smaller than tail words");
  Check(bundle.WordRank(what_ru) >= 0 && bundle.WordRank(what_ru) < tail_rank,
        "rank(что) is much smaller than tail words");

  YnnlmV1Bundle bad;
  st = bad.Parse(data, 50);
  Check(!st.ok(), "first 50 bytes are rejected");
  st = bad.Parse(data, 5000);
  Check(!st.ok(), "first 5000 bytes are rejected");
  const std::vector<uint8_t> garbage(256, 0xA5);
  st = bad.Parse(garbage.data(), garbage.size());
  Check(!st.ok(), "garbage buffer is rejected");

  if (g_failed != 0) {
    std::printf("%d checks failed\n", g_failed);
    return 1;
  }
  std::printf("ALL PASS\n");
  return 0;
}
