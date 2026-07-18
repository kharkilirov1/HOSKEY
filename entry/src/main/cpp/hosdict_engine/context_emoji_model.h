#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "status.h"

namespace hoskey::engine {

// Parser and exact postprocessor for the recovered ynnlm_v1 context-emoji
// wrapper. This class deliberately does not pretend to execute the embedded
// TFLite decoder: production activation still requires a numerically verified
// HarmonyOS runtime/conversion path and the exact raw-text/tag frontend.
class ContextEmojiModel {
 public:
  [[nodiscard]] Status Load(const uint8_t* data, size_t size);
  void Reset();

  [[nodiscard]] std::vector<std::string> DecodeScores(
      const std::vector<float>& decoder_scores, size_t top_k = 12) const;

  // Native TRwkvComposer contract for EncStateSpec="rwkv:tx;cx;h".
  [[nodiscard]] static bool ComposeRwkvTxCxH(
      const std::vector<float>& encoder_state,
      const std::vector<float>& encoder_hidden,
      std::vector<float>* decoder_state);

  [[nodiscard]] bool is_loaded() const { return loaded_; }
  [[nodiscard]] uint32_t order() const { return order_; }
  [[nodiscard]] size_t catalog_size() const { return catalog_.size(); }
  [[nodiscard]] size_t decoder_offset() const { return decoder_offset_; }
  [[nodiscard]] size_t decoder_size() const { return decoder_size_; }
  [[nodiscard]] const std::string& emoji_for_rank(size_t rank) const;
  [[nodiscard]] std::string diagnostics() const;

 private:
  bool loaded_ = false;
  uint32_t order_ = 0;
  size_t decoder_offset_ = 0;
  size_t decoder_size_ = 0;
  std::vector<std::string> catalog_;
  std::vector<uint32_t> lex_to_rank_;
  std::vector<uint32_t> rank_to_lex_;
  mutable std::atomic<uint64_t> invocation_count_{0};
  mutable std::atomic<uint64_t> scored_count_{0};
  mutable std::string fallback_ = "not_loaded";
  mutable uint64_t latency_us_ = 0;
};

}  // namespace hoskey::engine
