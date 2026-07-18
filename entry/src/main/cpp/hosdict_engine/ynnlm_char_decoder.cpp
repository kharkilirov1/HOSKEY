#include "ynnlm_char_decoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>

namespace hoskey::engine {

namespace {

constexpr size_t kHeaderSize = 160;
constexpr uint32_t kVersion = 1;
constexpr std::array<size_t, 6> kExpectedSegmentSizes = {
    YnnlmCharDecoder::kVocabularySize * YnnlmCharDecoder::kEmbeddingSize,
    YnnlmCharDecoder::kGateSize * YnnlmCharDecoder::kEmbeddingSize,
    YnnlmCharDecoder::kGateSize * YnnlmCharDecoder::kHiddenSize,
    YnnlmCharDecoder::kGateSize * sizeof(float),
    YnnlmCharDecoder::kVocabularySize * YnnlmCharDecoder::kHiddenSize,
    YnnlmCharDecoder::kVocabularySize * sizeof(float),
};

uint32_t ReadU32(const uint8_t* data, size_t offset) {
  uint32_t value = 0;
  std::memcpy(&value, data + offset, sizeof(value));
  return value;
}

float ReadF32(const uint8_t* data, size_t offset) {
  float value = 0.0f;
  std::memcpy(&value, data + offset, sizeof(value));
  return value;
}

bool SpanInside(size_t offset, size_t length, size_t total) {
  return offset <= total && length <= total - offset;
}

float Sigmoid(float value) {
  return 1.0f / (1.0f + std::exp(-value));
}

bool AllFinite(const float* values, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    if (!std::isfinite(values[i])) return false;
  }
  return true;
}

}  // namespace

void YnnlmCharDecoder::Reset() {
  loaded_ = false;
  diagnostics_ = "not_loaded";
  embedding_scale_ = 0.0f;
  input_weight_scale_ = 0.0f;
  recurrent_weight_scale_ = 0.0f;
  output_weight_scale_ = 0.0f;
  embedding_.clear();
  input_weights_.clear();
  recurrent_weights_.clear();
  lstm_bias_.clear();
  output_weights_.clear();
  output_bias_.clear();
  input_weight_row_sums_.clear();
  recurrent_weight_row_sums_.clear();
  output_weight_row_sums_.clear();
}

Status YnnlmCharDecoder::Load(const uint8_t* data, size_t size) {
  Reset();
  if (!data || size < kHeaderSize) {
    return Status::InvalidArgument("YnnlmCharDecoder: truncated YCD1 asset");
  }
  if (std::memcmp(data, "YCD1", 4) != 0) {
    return Status::UnsupportedFormat("YnnlmCharDecoder: bad YCD1 magic");
  }
  if (ReadU32(data, 4) != kVersion || ReadU32(data, 8) != kHeaderSize ||
      ReadU32(data, 12) != size) {
    return Status::UnsupportedFormat("YnnlmCharDecoder: bad header contract");
  }
  if (ReadU32(data, 16) != kVocabularySize ||
      ReadU32(data, 20) != kEmbeddingSize ||
      ReadU32(data, 24) != kHiddenSize ||
      ReadU32(data, 28) != kGateSize ||
      ReadU32(data, 32) != kStateSize) {
    return Status::UnsupportedFormat("YnnlmCharDecoder: dimension mismatch");
  }

  embedding_scale_ = ReadF32(data, 36);
  input_weight_scale_ = ReadF32(data, 40);
  recurrent_weight_scale_ = ReadF32(data, 44);
  output_weight_scale_ = ReadF32(data, 48);
  const std::array<float, 4> scales = {
      embedding_scale_, input_weight_scale_, recurrent_weight_scale_,
      output_weight_scale_};
  for (const float scale : scales) {
    if (!std::isfinite(scale) || scale <= 0.0f) {
      Reset();
      return Status::ParseError("YnnlmCharDecoder: invalid quantization scale");
    }
  }

  std::array<const uint8_t*, 6> segments{};
  size_t expected_offset = kHeaderSize;
  for (size_t i = 0; i < segments.size(); ++i) {
    const size_t offset = ReadU32(data, 52 + i * 8);
    const size_t length = ReadU32(data, 56 + i * 8);
    if (offset != expected_offset || length != kExpectedSegmentSizes[i] ||
        !SpanInside(offset, length, size)) {
      Reset();
      return Status::ParseError("YnnlmCharDecoder: invalid tensor span");
    }
    segments[i] = data + offset;
    expected_offset += length;
  }
  if (expected_offset != size) {
    Reset();
    return Status::ParseError("YnnlmCharDecoder: trailing tensor data");
  }

  embedding_.assign(reinterpret_cast<const int8_t*>(segments[0]),
                    reinterpret_cast<const int8_t*>(segments[0]) +
                        kExpectedSegmentSizes[0]);
  input_weights_.assign(reinterpret_cast<const int8_t*>(segments[1]),
                        reinterpret_cast<const int8_t*>(segments[1]) +
                            kExpectedSegmentSizes[1]);
  recurrent_weights_.assign(
      reinterpret_cast<const int8_t*>(segments[2]),
      reinterpret_cast<const int8_t*>(segments[2]) + kExpectedSegmentSizes[2]);
  lstm_bias_.resize(kGateSize);
  std::memcpy(lstm_bias_.data(), segments[3], kExpectedSegmentSizes[3]);
  output_weights_.assign(reinterpret_cast<const int8_t*>(segments[4]),
                         reinterpret_cast<const int8_t*>(segments[4]) +
                             kExpectedSegmentSizes[4]);
  output_bias_.resize(kVocabularySize);
  std::memcpy(output_bias_.data(), segments[5], kExpectedSegmentSizes[5]);
  if (!AllFinite(lstm_bias_.data(), lstm_bias_.size()) ||
      !AllFinite(output_bias_.data(), output_bias_.size())) {
    Reset();
    return Status::ParseError("YnnlmCharDecoder: non-finite bias");
  }

  input_weight_row_sums_ =
      BuildRowSums(input_weights_, kGateSize, kEmbeddingSize);
  recurrent_weight_row_sums_ =
      BuildRowSums(recurrent_weights_, kGateSize, kHiddenSize);
  output_weight_row_sums_ =
      BuildRowSums(output_weights_, kVocabularySize, kHiddenSize);

  std::ostringstream source;
  source << std::hex << std::setfill('0');
  for (size_t i = 100; i < 108; ++i) {
    source << std::setw(2) << static_cast<unsigned>(data[i]);
  }
  loaded_ = true;
  diagnostics_ = "loaded:hybrid_asymmetric_int8_v1,source_sha256=" +
                 source.str() + "...";
  return Status::Ok();
}

std::vector<int32_t> YnnlmCharDecoder::BuildRowSums(
    const std::vector<int8_t>& weights, size_t rows, size_t columns) {
  std::vector<int32_t> sums(rows, 0);
  for (size_t row = 0; row < rows; ++row) {
    int32_t sum = 0;
    const int8_t* values = weights.data() + row * columns;
    for (size_t column = 0; column < columns; ++column) {
      sum += static_cast<int32_t>(values[column]);
    }
    sums[row] = sum;
  }
  return sums;
}

bool YnnlmCharDecoder::HybridFullyConnected(
    const float* input, size_t input_size, const int8_t* weights,
    size_t output_size, float weight_scale,
    const std::vector<int32_t>& weight_row_sums, float* output) {
  if (!input || !weights || !output || input_size == 0 ||
      input_size > kHiddenSize || weight_row_sums.size() != output_size ||
      !AllFinite(input, input_size)) {
    return false;
  }

  float minimum = 0.0f;
  float maximum = 0.0f;
  for (size_t i = 0; i < input_size; ++i) {
    minimum = std::min(minimum, input[i]);
    maximum = std::max(maximum, input[i]);
  }
  if (minimum == maximum) {
    std::fill(output, output + output_size, 0.0f);
    return true;
  }

  const float input_scale = (maximum - minimum) / 255.0f;
  if (!std::isfinite(input_scale) || input_scale <= 0.0f) return false;
  int32_t zero_point = static_cast<int32_t>(
      std::nearbyint(-128.0f - minimum / input_scale));
  zero_point = std::clamp<int32_t>(zero_point, -128, 127);

  std::array<int8_t, kHiddenSize> quantized{};
  for (size_t i = 0; i < input_size; ++i) {
    int32_t value = static_cast<int32_t>(
                        std::nearbyint(input[i] / input_scale)) +
                    zero_point;
    value = std::clamp<int32_t>(value, -128, 127);
    quantized[i] = static_cast<int8_t>(value);
  }

  const float combined_scale = input_scale * weight_scale;
  for (size_t row = 0; row < output_size; ++row) {
    const int8_t* row_weights = weights + row * input_size;
    int32_t accumulator = -zero_point * weight_row_sums[row];
    for (size_t column = 0; column < input_size; ++column) {
      accumulator += static_cast<int32_t>(row_weights[column]) *
                     static_cast<int32_t>(quantized[column]);
    }
    output[row] = static_cast<float>(accumulator) * combined_scale;
  }
  return true;
}

bool YnnlmCharDecoder::Step(
    size_t token_id, const float* state, size_t state_size,
    std::array<float, kVocabularySize>* probabilities,
    std::array<float, kStateSize>* next_state) const {
  if (!loaded_ || token_id >= kVocabularySize || !state ||
      state_size != kStateSize || !probabilities || !next_state ||
      !AllFinite(state, state_size)) {
    return false;
  }

  std::array<float, kEmbeddingSize> embedding{};
  const int8_t* embedding_row = embedding_.data() + token_id * kEmbeddingSize;
  for (size_t i = 0; i < kEmbeddingSize; ++i) {
    embedding[i] = static_cast<float>(embedding_row[i]) * embedding_scale_;
  }

  std::array<float, kGateSize> input_projection{};
  std::array<float, kGateSize> recurrent_projection{};
  if (!HybridFullyConnected(
          embedding.data(), embedding.size(), input_weights_.data(), kGateSize,
          input_weight_scale_, input_weight_row_sums_, input_projection.data()) ||
      !HybridFullyConnected(
          state, kHiddenSize, recurrent_weights_.data(), kGateSize,
          recurrent_weight_scale_, recurrent_weight_row_sums_,
          recurrent_projection.data())) {
    return false;
  }

  std::array<float, kGateSize> gates{};
  for (size_t i = 0; i < kGateSize; ++i) {
    gates[i] = (input_projection[i] + recurrent_projection[i]) + lstm_bias_[i];
  }
  constexpr size_t kGateWidth = kHiddenSize;
  float* hidden_out = next_state->data();
  float* cell_out = next_state->data() + kHiddenSize;
  const float* cell_previous = state + kHiddenSize;
  for (size_t i = 0; i < kHiddenSize; ++i) {
    const float input_gate = Sigmoid(gates[i]);
    const float forget_gate = Sigmoid(gates[kGateWidth + i]);
    const float candidate = std::tanh(gates[2 * kGateWidth + i]);
    const float output_gate = Sigmoid(gates[3 * kGateWidth + i]);
    const float input_cell = input_gate * candidate;
    const float forget_cell = forget_gate * cell_previous[i];
    cell_out[i] = forget_cell + input_cell;
    hidden_out[i] = output_gate * std::tanh(cell_out[i]);
  }

  std::array<float, kVocabularySize> logits{};
  if (!HybridFullyConnected(
          hidden_out, kHiddenSize, output_weights_.data(), kVocabularySize,
          output_weight_scale_, output_weight_row_sums_, logits.data())) {
    return false;
  }
  for (size_t i = 0; i < kVocabularySize; ++i) logits[i] += output_bias_[i];

  const float maximum = *std::max_element(logits.begin(), logits.end());
  float denominator = 0.0f;
  for (size_t i = 0; i < kVocabularySize; ++i) {
    (*probabilities)[i] = std::exp(logits[i] - maximum);
    denominator += (*probabilities)[i];
  }
  if (!std::isfinite(denominator) || denominator <= 0.0f) return false;
  for (float& probability : *probabilities) probability /= denominator;
  return AllFinite(probabilities->data(), probabilities->size()) &&
         AllFinite(next_state->data(), next_state->size());
}

}  // namespace hoskey::engine
