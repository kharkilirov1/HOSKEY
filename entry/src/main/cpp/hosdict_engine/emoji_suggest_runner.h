#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "context_emoji_model.h"
#include "status.h"
#include "ynnlm_bundle.h"

namespace hoskey::engine {

struct EmojiSuggestModelBuffer {
  const uint8_t* data = nullptr;
  size_t size = 0;
};

struct EmojiSuggestModelBuffers {
  EmojiSuggestModelBuffer age_embedding;
  EmojiSuggestModelBuffer application_embedding;
  EmojiSuggestModelBuffer sex_embedding;
  EmojiSuggestModelBuffer word_embedding;
  EmojiSuggestModelBuffer encoder;
  EmojiSuggestModelBuffer decoder;
};

struct ContextEmojiCandidate {
  std::string emoji;
  float probability = 0.0f;
};

struct ContextEmojiInference {
  std::vector<ContextEmojiCandidate> candidates;
  bool model_engaged = false;
  float top_probability = 0.0f;
  size_t encoded_words = 0;
  std::string diagnostics;
};

// Exact K13321 context-emoji chain recovered from TNeuralEmojiSuggest:
//
//   age=0 + ApplicationId(package) + sex=0 + CNN(word ids)
//       -> RWKV encoder(state[4000], hidden[500])
//       -> composer "rwkv:tx;cx;h"[2500] -> emoji decoder[1752]
//
// Requests are deliberately stateless: the last `Order` completed words are
// replayed oldest-first from a zero state. This makes editor/cursor changes and
// tentative-word rollback exact without retaining stale cross-request state.
class EmojiSuggestRunner {
 public:
  using WordIds = std::array<float, 24>;

  EmojiSuggestRunner() = default;
  ~EmojiSuggestRunner();
  EmojiSuggestRunner(const EmojiSuggestRunner&) = delete;
  EmojiSuggestRunner& operator=(const EmojiSuggestRunner&) = delete;

  Status Load(const EmojiSuggestModelBuffers& models,
              const uint8_t* ynnlm_bundle, size_t ynnlm_bundle_size,
              const uint8_t* emoji_wrapper, size_t emoji_wrapper_size);
  void Reset();

  [[nodiscard]] ContextEmojiInference Suggest(
      const std::vector<std::string>& completed_words,
      std::string_view package_name, size_t top_k) const;

  [[nodiscard]] bool is_loaded() const { return loaded_; }
  [[nodiscard]] uint64_t invocation_count() const {
    return invocation_count_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] uint64_t scored_count() const {
    return scored_count_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::string diagnostics() const;

  // Host-testable exact raw-text frontend. PAD=0, BOS=1, EOS=2, '-'=5,
  // Russian а..я=6..37; uppercase and ё are normalized before encoding.
  static bool EncodeWord(std::string_view word, WordIds* ids);

 private:
  bool loaded_ = false;
  YnnlmV1Bundle bundle_;
  ContextEmojiModel wrapper_;
  mutable std::mutex inference_mutex_;
  mutable std::atomic<uint64_t> invocation_count_{0};
  mutable std::atomic<uint64_t> scored_count_{0};
  mutable std::atomic<uint64_t> failed_count_{0};
  mutable uint64_t latency_us_ = 0;
  mutable std::string fallback_ = "not_loaded";
  std::string load_diagnostics_ = "not_loaded";

  void* age_ctx_ = nullptr;
  void* app_ctx_ = nullptr;
  void* sex_ctx_ = nullptr;
  void* word_ctx_ = nullptr;
  void* encoder_ctx_ = nullptr;
  void* decoder_ctx_ = nullptr;
  void* age_model_ = nullptr;
  void* app_model_ = nullptr;
  void* sex_model_ = nullptr;
  void* word_model_ = nullptr;
  void* encoder_model_ = nullptr;
  void* decoder_model_ = nullptr;
};

}  // namespace hoskey::engine
