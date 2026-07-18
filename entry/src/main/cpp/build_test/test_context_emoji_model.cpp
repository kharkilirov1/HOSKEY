#include "hosdict_engine/context_emoji_model.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

using hoskey::engine::ContextEmojiModel;

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

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <emoji_model>\n", argv[0]);
    return 2;
  }
  std::ifstream stream(argv[1], std::ios::binary);
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                                   std::istreambuf_iterator<char>());
  Check(!bytes.empty(), "context emoji wrapper is readable");

  ContextEmojiModel model;
  const auto load = model.Load(bytes.data(), bytes.size());
  Check(load.ok(), "ynnlm_v1 context emoji wrapper loads");
  Check(model.is_loaded(), "loaded state is observable");
  Check(model.order() == 10, "history order is 10");
  Check(model.catalog_size() == 1752, "catalog has 1,752 entries");
  Check(model.decoder_offset() == 44480, "decoder byte provenance is exact");
  Check(model.decoder_size() == bytes.size() - 44480,
        "decoder slice consumes the remaining wrapper bytes");
  Check(model.emoji_for_rank(0) == "<unk>",
        "decoder rank zero maps through rank_to_lex");

  std::vector<float> encoder_state(4000);
  std::vector<float> hidden(500);
  for (size_t i = 0; i < encoder_state.size(); ++i) {
    encoder_state[i] = static_cast<float>(i);
  }
  for (size_t i = 0; i < hidden.size(); ++i) {
    hidden[i] = static_cast<float>(10000 + i);
  }
  std::vector<float> decoder_state;
  Check(ContextEmojiModel::ComposeRwkvTxCxH(encoder_state, hidden,
                                            &decoder_state),
        "rwkv:tx;cx;h state composition accepts exact shapes");
  Check(decoder_state.size() == 2500, "composed decoder state has 2,500 floats");
  Check(decoder_state[0] == 0.0f && decoder_state[499] == 499.0f &&
            decoder_state[500] == 1500.0f &&
            decoder_state[999] == 1999.0f &&
            decoder_state[1000] == 2000.0f &&
            decoder_state[1499] == 2499.0f &&
            decoder_state[1500] == 3500.0f &&
            decoder_state[1999] == 3999.0f &&
            decoder_state[2000] == 10000.0f &&
            decoder_state[2499] == 10499.0f,
        "composer preserves per-block tx,cx order followed by hidden state");

  std::vector<float> scores(model.catalog_size(), -1000.0f);
  for (size_t rank = 0; rank < 12; ++rank) {
    scores[rank] = static_cast<float>(100 - rank);
  }
  const std::vector<std::string> decoded = model.DecodeScores(scores);
  Check(decoded.size() == 11,
        "native top-12 removes <unk> after selection without refill");
  bool mapping_ok = true;
  for (size_t rank = 1; rank < 12; ++rank) {
    mapping_ok = mapping_ok && decoded[rank - 1] == model.emoji_for_rank(rank);
  }
  Check(mapping_ok, "score ranks map through rank_to_lex in descending order");
  Check(model.diagnostics().find("invoke_count=1") != std::string::npos &&
            model.diagnostics().find("scored_count=11") != std::string::npos &&
            model.diagnostics().find("fallback=none") != std::string::npos,
        "postprocessor exposes strict counters and route diagnostics");

  scores[17] = std::numeric_limits<float>::quiet_NaN();
  Check(model.DecodeScores(scores).empty(), "non-finite logits are rejected safely");
  Check(model.diagnostics().find("fallback=nonfinite_scores") !=
            std::string::npos,
        "non-finite rejection is diagnostic");

  ContextEmojiModel truncated;
  Check(!truncated.Load(bytes.data(), 1000).ok(),
        "truncated context emoji wrapper is rejected");

  if (failures != 0) {
    std::fprintf(stderr, "%d context emoji test(s) failed\n", failures);
    return 1;
  }
  std::printf("All context emoji contract tests passed.\n");
  return 0;
}
