#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "status.h"

namespace hoskey::engine {

// Yandex 3GRM character language model. The serialized payload is a dense
// 42x42x42 table of uint32 trigram counts; unigram and bigram marginals are
// reconstructed exactly as the Android loader does.
class CharTrigramModel {
 public:
  [[nodiscard]] Status Load(const uint8_t* data, size_t size);
  void Reset();

  // Returns false when the model is unavailable or the word contains a
  // character outside the model alphabet.
  [[nodiscard]] bool Score(std::string_view word, float* score) const;

  [[nodiscard]] bool is_loaded() const { return loaded_; }
  [[nodiscard]] size_t alphabet_size() const { return kAlphabetSize; }
  [[nodiscard]] std::string diagnostics() const;

 private:
  static constexpr size_t kAlphabetSize = 42;
  static constexpr size_t kTrigramCount =
      kAlphabetSize * kAlphabetSize * kAlphabetSize;

  [[nodiscard]] static uint8_t SymbolId(char32_t codepoint);

  std::vector<uint32_t> trigrams_;
  std::array<uint64_t, kAlphabetSize * kAlphabetSize> bigrams_{};
  std::array<uint64_t, kAlphabetSize> unigrams_{};
  bool loaded_ = false;
};

}  // namespace hoskey::engine
