#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "rwkv_encoder.h"
#include "status.h"

namespace hoskey::engine {

// Neural word-suggestion chain — a runnable composition of the
// reverse-engineered Yandex word language model:
//
//   char ids -> emb (m4) -> RWKV encoder (pure C++) -> enc_dec bridge (m6)
//            -> word decoder (m2) -> 107,989 logits -> prefix filter -> top-k.
//
// The three matrix stages are MindSpore Lite models (device OH_AI runtime); the
// RWKV encoder is the float weights driven by RwkvEncoder (its original int8
// layer-norm/MEAN op is unsupported by NNACL, so it can't ship as an .ms). On a
// host build without MindSpore Lite this is inert: Load reports the models as
// unavailable and Predict returns an empty list.
class YnnlmWordChain {
 public:
  YnnlmWordChain() = default;
  ~YnnlmWordChain();
  YnnlmWordChain(const YnnlmWordChain&) = delete;
  YnnlmWordChain& operator=(const YnnlmWordChain&) = delete;

  // Loads from in-memory asset bytes (borrowed for the duration of the call —
  // the encoder copies its weights, the models are built into OH_AI handles and
  // the vocabulary is parsed into owned strings).
  Status Load(const uint8_t* emb_model, size_t emb_size,
              const uint8_t* enc_dec_model, size_t enc_dec_size,
              const uint8_t* dec_model, size_t dec_size,
              const uint8_t* encoder_weights, size_t encoder_size,
              const uint8_t* vocab_bundle, size_t vocab_size);

  void Reset();

  [[nodiscard]] bool is_loaded() const { return loaded_; }
  [[nodiscard]] const std::string& diagnostics() const { return diagnostics_; }

  // Prefix-filtered top-k neural word suggestions (highest logit first). The
  // encoder is first stepped through `context` (oldest-first previous words) to
  // build an Order-N context state, then through the current prefix — so
  // predictions are context-aware. Empty if not loaded, if the prefix contains a
  // non-alphabet character, or on any inference error. A context word with a
  // non-alphabet character is skipped (state carried unchanged).
  [[nodiscard]] std::vector<std::pair<std::string, float>> Predict(
      const std::string& prefix, const std::vector<std::string>& context,
      size_t topk) const;

 private:
  bool loaded_ = false;
  std::string diagnostics_ = "not_loaded";
  rwkv::RwkvEncoder encoder_;
  std::vector<std::string> rank_words_;  // logit index -> word

  // One MindSpore Lite context PER model: OH_AI_ModelBuild(from buffer) consumes
  // the context, so a shared context lets only the first model build.
  void* emb_ctx_ = nullptr;
  void* enc_dec_ctx_ = nullptr;
  void* dec_ctx_ = nullptr;
  void* emb_model_ = nullptr;
  void* enc_dec_model_ = nullptr;
  void* dec_model_ = nullptr;
};

}  // namespace hoskey::engine
