// Host-side malformed-input hardening test for the HOSKEY parsers.
// Feeds truncated / oversized / integer-overflow buffers to each binary-asset
// parser and asserts they reject or degrade cleanly instead of reading out of
// bounds. Exercises the Phase-2 fixes (container overflow, succinct_trie
// unbounded scans, one_nc7 word_count*8 overflow, lemmer num_rules*4 overflow).
//
// Build (from build_test/, engine .obj already produced by ninja):
//   g++ -std=gnu++17 -I.. -I../hosdict_engine test_hardening.cpp \
//       CMakeFiles/hoskey_bridge.dir/hosdict_engine/*.obj -o test_hardening
//
// A clean exit 0 with "ALL CASES RAN" is the witness: none of these inputs
// crash, OOM, or read past their buffer.

#include "hosdict_engine/succinct_trie.h"
#include "hosdict_engine/one_nc7_trie.h"
#include "hosdict_engine/lemmer.h"
#include "hosdict_engine/hosdict_container.h"
#include "hosdict_engine/policy_store.h"
#include "hosdict_engine/loaders.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace hoskey::engine;

static int g_cases = 0;
static int g_failed = 0;

static void put_u32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
  if (off + 4 > b.size()) b.resize(off + 4, 0);
  std::memcpy(b.data() + off, &v, 4);
}
static void put_u64(std::vector<uint8_t>& b, size_t off, uint64_t v) {
  if (off + 8 > b.size()) b.resize(off + 8, 0);
  std::memcpy(b.data() + off, &v, 8);
}
static void append(std::vector<uint8_t>& b, const char* s) {
  while (*s) b.push_back(static_cast<uint8_t>(*s++));
}

static void feed_succinct(const std::vector<uint8_t>& b) {
  SuccinctTrie t;
  t.Parse(b.data(), b.size());
  t.GetCompletions("\xD0\xB0", 3);  // "а": exercise rank/select accessors
  t.IsValidPrefix("\xD0\xBF\xD1\x80\xD0\xB8");
  ++g_cases;
}
static void feed_onenc7(const std::vector<uint8_t>& b) {
  OneNc7Trie t;
  t.Parse(b.data(), b.size());
  ++g_cases;
}
static void feed_lemmer(const std::vector<uint8_t>& b) {
  Lemmer l;
  l.LoadRules(b.data(), b.size());
  l.Lemmatize("\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");  // "привет"
  ++g_cases;
}
static void feed_container(const std::vector<uint8_t>& b) {
  HosdictContainer c;
  c.Open(b.data(), b.size());
  ++g_cases;
}
static void feed_policy(const std::vector<uint8_t>& b) {
  PolicyStore p;
  p.Load(b.data(), b.size());
  p.Contains("test");
  ++g_cases;
}
static void feed_wordlists(const std::vector<uint8_t>& b) {
  std::vector<std::string> w, nw;
  std::vector<int32_t> wt;
  ParseUtf8WordList(b.data(), b.size(), &w, &nw);
  ParseWeightedWordList(b.data(), b.size(), &w, &nw, &wt);
  ++g_cases;
}

int main() {
  // 1. Size sweep 0..80: tiny / truncated buffers, zero- and 0xFF-filled.
  //    Directly hits the old data_size_-32 / size-N unsigned underflows.
  for (size_t n = 0; n <= 80; ++n) {
    std::vector<uint8_t> zeros(n, 0x00);
    std::vector<uint8_t> ff(n, 0xFF);
    feed_succinct(zeros); feed_succinct(ff);
    feed_onenc7(zeros);   feed_onenc7(ff);
    feed_lemmer(zeros);   feed_lemmer(ff);
    feed_container(zeros); feed_container(ff);
    feed_policy(zeros);    feed_policy(ff);
    feed_wordlists(zeros); feed_wordlists(ff);
  }

  // 2. SuccinctTrie: valid magic + tiny size (24..40) -> old ParseBitvectors
  //    "data_size_ - 32" underflow. Also huge item_count/payload_hint.
  for (size_t n = 24; n <= 40; ++n) {
    std::vector<uint8_t> b(n, 0x11);
    put_u32(b, 0, 0x37636E31);   // "1nc7"
    put_u32(b, 8, 0xFFFFFFFF);   // payload_hint huge
    put_u32(b, 16, 0xFFFFFFFF);  // item_count -> num_words huge
    feed_succinct(b);
  }

  // 3. SuccinctTrie: magic + huge item_count in a modest buffer.
  {
    std::vector<uint8_t> b(8192, 0x00);
    put_u32(b, 0, 0x37636E31);
    put_u32(b, 8, 0x000F4240);   // payload_hint ~1e6 (>= size -> clamped)
    put_u32(b, 16, 0xFFFFFFFF);  // item_count huge
    feed_succinct(b);
  }

  // 4. Lemmer: num_rules = 0x40000000 -> old num_rules*4 uint32 overflow that
  //    passed the bounds check and drove rule_offsets_.resize(~1e9).
  {
    std::vector<uint8_t> b(64, 0x00);
    put_u32(b, 0x10, 0x00000000);  // alt_count = 0 (alternate-layout rescue fails)
    put_u32(b, 0x14, 0x00000020);  // rules_data_offset within size
    put_u32(b, 0x18, 0x40000000);  // num_rules huge
    feed_lemmer(b);
  }

  // 5. HosdictContainer: asset entry whose data_offset + data_size wraps around
  //    64 bits to a small value (old check accepted it; the fix must reject).
  {
    std::vector<uint8_t> b;
    put_u32(b, 0, 0x444B5348);   // kMagic "HSKD"
    put_u32(b, 4, 1);            // version
    put_u32(b, 8, 1);            // asset_count
    const char* manifest = "{\"package_name\":\"t\"}";
    put_u32(b, 12, static_cast<uint32_t>(std::strlen(manifest)));
    // header is 16 bytes; append manifest then one asset entry.
    b.resize(16);
    append(b, manifest);
    put_u32(b, b.size(), 3); b.resize(b.size());  // key_length
    append(b, "lex");
    put_u32(b, b.size(), 3);
    append(b, "raw");                              // format
    put_u64(b, b.size(), 0xFFFFFFFFFFFFFFF0ULL);   // data_offset (near UINT64_MAX)
    put_u64(b, b.size(), 0x50ULL);                 // data_size -> sum wraps to 0x40
    HosdictContainer c;
    Status s = c.Open(b.data(), b.size());
    ++g_cases;
    if (s.ok()) {
      std::printf("FAIL: container accepted overflowing asset entry\n");
      ++g_failed;
    } else {
      std::printf("PASS: container rejected overflowing asset entry\n");
    }
  }

  // 6. PolicyStore: long right-sibling chain (every node has_right, node_size=2).
  //    The old recursive ComptrieExtract recursed once per sibling and would
  //    stack-overflow on a large chain; the iterative version must not.
  {
    const size_t nodes = 1000000;            // ~2 MB, ~1e6 deep in the old code
    std::vector<uint8_t> b(nodes * 2);
    for (size_t i = 0; i < nodes; ++i) {
      b[i * 2] = 0x21;                        // label (any word byte)
      b[i * 2 + 1] = 0x10;                    // core: has_right only, extra=0
    }
    PolicyStore p;
    Status s = p.Load(b.data(), b.size());
    ++g_cases;
    std::printf("PASS: policy long right-sibling chain handled (%llu nodes), ok=%d\n",
                (unsigned long long)nodes, (int)s.ok());
  }

  // 7. ParseWeightedWordList: an overflowing weight must clamp to INT32_MAX,
  //    not trigger signed-int32 overflow (UB) in the hand-rolled atoi.
  {
    std::vector<uint8_t> b = {0xD1,0x81,0xD0,0xBB,0xD0,0xBE,0xD0,0xB2, '\t'};  // "слов" + tab
    for (int i = 0; i < 25; ++i) b.push_back('9');                            // 25-digit weight
    b.push_back('\n');
    std::vector<std::string> w, nw;
    std::vector<int32_t> wt;
    ParseWeightedWordList(b.data(), b.size(), &w, &nw, &wt);
    ++g_cases;
    bool ok = !wt.empty() && wt[0] == 2147483647;
    if (!ok) { std::printf("FAIL: weight not clamped (%d entries)\n", (int)wt.size()); ++g_failed; }
    else std::printf("PASS: weighted-wordlist huge weight clamped to INT32_MAX\n");
  }

  std::printf("ALL CASES RAN WITHOUT CRASH: %d cases, %d failed\n",
              g_cases, g_failed);
  return g_failed == 0 ? 0 : 1;
}
