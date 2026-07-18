#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "status.h"

namespace hoskey::engine {

// Exact native executor for the Yandex 122.5 YNNLM m0 character decoder.
//
// converter_lite keeps the int8 weights but changes TFLite's hybrid
// FullyConnected arithmetic to float matmul.  This executor preserves the
// original per-step asymmetric activation quantization and therefore avoids
// the model-dependent score drift of the converted char_dec.ms graph.
class YnnlmCharDecoder {
 public:
  static constexpr size_t kVocabularySize = 38;
  static constexpr size_t kEmbeddingSize = 32;
  static constexpr size_t kHiddenSize = 750;
  static constexpr size_t kGateSize = 3000;
  static constexpr size_t kStateSize = 1500;

  Status Load(const uint8_t* data, size_t size);
  void Reset();

  [[nodiscard]] bool is_loaded() const { return loaded_; }
  [[nodiscard]] const std::string& diagnostics() const { return diagnostics_; }

  [[nodiscard]] bool Step(
      size_t token_id, const float* state, size_t state_size,
      std::array<float, kVocabularySize>* probabilities,
      std::array<float, kStateSize>* next_state) const;

 private:
  static bool HybridFullyConnected(
      const float* input, size_t input_size, const int8_t* weights,
      size_t output_size, float weight_scale,
      const std::vector<int32_t>& weight_row_sums, float* output);
  static std::vector<int32_t> BuildRowSums(
      const std::vector<int8_t>& weights, size_t rows, size_t columns);

  bool loaded_ = false;
  std::string diagnostics_ = "not_loaded";
  float embedding_scale_ = 0.0f;
  float input_weight_scale_ = 0.0f;
  float recurrent_weight_scale_ = 0.0f;
  float output_weight_scale_ = 0.0f;
  std::vector<int8_t> embedding_;
  std::vector<int8_t> input_weights_;
  std::vector<int8_t> recurrent_weights_;
  std::vector<float> lstm_bias_;
  std::vector<int8_t> output_weights_;
  std::vector<float> output_bias_;
  std::vector<int32_t> input_weight_row_sums_;
  std::vector<int32_t> recurrent_weight_row_sums_;
  std::vector<int32_t> output_weight_row_sums_;
};

}  // namespace hoskey::engine
