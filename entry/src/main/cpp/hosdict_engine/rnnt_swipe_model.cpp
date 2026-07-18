#include "rnnt_swipe_model.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

#ifdef MINDSPORE_LITE_AVAILABLE
#include "mindspore/context.h"
#include "mindspore/model.h"
#include "mindspore/status.h"
#include "mindspore/tensor.h"
#endif

namespace hoskey::engine {
namespace {

const std::array<RnntSwipeModel::EncoderVector, 64>&
CachedPositionalEmbeddings() {
  static const std::array<RnntSwipeModel::EncoderVector, 64> cache = [] {
    std::array<RnntSwipeModel::EncoderVector, 64> result{};
    for (size_t i = 0; i < result.size(); ++i) {
      result[i] = RnntSwipeModel::PositionalEmbedding(static_cast<uint32_t>(i));
    }
    return result;
  }();
  return cache;
}

#ifdef MINDSPORE_LITE_AVAILABLE

OH_AI_ContextHandle CreateCpuContext() {
  OH_AI_ContextHandle context = OH_AI_ContextCreate();
  if (!context) return nullptr;
  OH_AI_ContextSetThreadNum(context, 2);
  OH_AI_ContextSetEnableParallel(context, false);
  OH_AI_DeviceInfoHandle cpu = OH_AI_DeviceInfoCreate(OH_AI_DEVICETYPE_CPU);
  if (cpu) {
    OH_AI_DeviceInfoSetEnableFP16(cpu, false);
    OH_AI_ContextAddDeviceInfo(context, cpu);
  }
  return context;
}

OH_AI_ModelHandle BuildModel(const RnntModelBuffer& buffer,
                             OH_AI_ContextHandle* out_context,
                             int* out_status) {
  *out_context = nullptr;
  if (out_status) *out_status = -100;
  if (!buffer.data || buffer.size < 8 ||
      std::memcmp(buffer.data + 4, "MSL2", 4) != 0) {
    return nullptr;
  }
  OH_AI_ContextHandle context = CreateCpuContext();
  if (!context) {
    if (out_status) *out_status = -101;
    return nullptr;
  }
  OH_AI_ModelHandle model = OH_AI_ModelCreate();
  if (!model) {
    OH_AI_ContextDestroy(&context);
    if (out_status) *out_status = -102;
    return nullptr;
  }
  const OH_AI_Status status = OH_AI_ModelBuild(
      model, buffer.data, buffer.size, OH_AI_MODELTYPE_MINDIR, context);
  if (out_status) *out_status = static_cast<int>(status);
  if (status != OH_AI_STATUS_SUCCESS) {
    OH_AI_ModelDestroy(&model);
    OH_AI_ContextDestroy(&context);
    return nullptr;
  }
  *out_context = context;
  return model;
}

struct TensorView {
  const float* data = nullptr;
  size_t count = 0;
};

struct MutableTensorView {
  float* data = nullptr;
  size_t count = 0;
};

bool RunModel(OH_AI_ModelHandle model,
              std::initializer_list<TensorView> input_views,
              std::initializer_list<MutableTensorView> output_views) {
  if (!model) return false;
  OH_AI_TensorHandleArray inputs = OH_AI_ModelGetInputs(model);
  if (!inputs.handle_list || inputs.handle_num != input_views.size()) {
    return false;
  }
  size_t index = 0;
  for (const TensorView& source : input_views) {
    OH_AI_TensorHandle tensor = inputs.handle_list[index++];
    const size_t bytes = source.count * sizeof(float);
    if (!source.data || OH_AI_TensorGetDataSize(tensor) != bytes) return false;
    void* destination = OH_AI_TensorGetMutableData(tensor);
    if (!destination) return false;
    std::memcpy(destination, source.data, bytes);
  }

  OH_AI_TensorHandleArray outputs;
  const OH_AI_Status status =
      OH_AI_ModelPredict(model, inputs, &outputs, nullptr, nullptr);
  if (status != OH_AI_STATUS_SUCCESS || !outputs.handle_list ||
      outputs.handle_num != output_views.size()) {
    return false;
  }
  index = 0;
  for (const MutableTensorView& destination : output_views) {
    OH_AI_TensorHandle tensor = outputs.handle_list[index++];
    const size_t bytes = destination.count * sizeof(float);
    const void* source = OH_AI_TensorGetData(tensor);
    if (!destination.data || !source || OH_AI_TensorGetDataSize(tensor) != bytes) {
      return false;
    }
    std::memcpy(destination.data, source, bytes);
  }
  return true;
}

bool RunPredictorModel(OH_AI_ModelHandle model, float token,
                       const RnntSwipeModel::PredictorState& state,
                       RnntSwipeModel::JointLogits* probabilities,
                       RnntSwipeModel::PredictorVector* hidden,
                       RnntSwipeModel::PredictorState* next_state) {
  if (!model || !probabilities || !hidden || !next_state) return false;
  OH_AI_TensorHandleArray inputs = OH_AI_ModelGetInputs(model);
  if (!inputs.handle_list || inputs.handle_num != 2) return false;

  bool token_written = false;
  bool state_written = false;
  for (size_t i = 0; i < inputs.handle_num; ++i) {
    OH_AI_TensorHandle tensor = inputs.handle_list[i];
    const size_t bytes = OH_AI_TensorGetDataSize(tensor);
    void* destination = OH_AI_TensorGetMutableData(tensor);
    if (!destination) return false;
    if (!token_written && bytes == sizeof(float)) {
      std::memcpy(destination, &token, sizeof(token));
      token_written = true;
    } else if (!state_written && bytes == state.size() * sizeof(float)) {
      std::memcpy(destination, state.data(), bytes);
      state_written = true;
    }
  }
  if (!token_written || !state_written) return false;

  OH_AI_TensorHandleArray outputs;
  const OH_AI_Status status =
      OH_AI_ModelPredict(model, inputs, &outputs, nullptr, nullptr);
  if (status != OH_AI_STATUS_SUCCESS || !outputs.handle_list ||
      outputs.handle_num != 3) {
    return false;
  }

  bool probabilities_read = false;
  bool hidden_read = false;
  bool state_read = false;
  for (size_t i = 0; i < outputs.handle_num; ++i) {
    OH_AI_TensorHandle tensor = outputs.handle_list[i];
    const size_t count = OH_AI_TensorGetDataSize(tensor) / sizeof(float);
    const float* source =
        static_cast<const float*>(OH_AI_TensorGetData(tensor));
    if (!source) return false;
    if (!probabilities_read && count == probabilities->size()) {
      std::copy_n(source, probabilities->size(), probabilities->data());
      probabilities_read = true;
    } else if (!hidden_read && count == hidden->size()) {
      std::copy_n(source, hidden->size(), hidden->data());
      hidden_read = true;
    } else if (!state_read && count == next_state->size()) {
      std::copy_n(source, next_state->size(), next_state->data());
      state_read = true;
    }
  }
  return probabilities_read && hidden_read && state_read;
}

std::vector<size_t> TensorCounts(OH_AI_TensorHandleArray tensors,
                                 bool* valid) {
  std::vector<size_t> counts;
  *valid = tensors.handle_list != nullptr;
  if (!*valid) return counts;
  counts.reserve(tensors.handle_num);
  for (size_t i = 0; i < tensors.handle_num; ++i) {
    OH_AI_TensorHandle tensor = tensors.handle_list[i];
    const size_t bytes = OH_AI_TensorGetDataSize(tensor);
    if (OH_AI_TensorGetDataType(tensor) !=
            OH_AI_DATATYPE_NUMBERTYPE_FLOAT32 ||
        bytes == 0 || bytes % sizeof(float) != 0) {
      *valid = false;
      counts.clear();
      return counts;
    }
    counts.push_back(bytes / sizeof(float));
  }
  return counts;
}

bool ModelContractMatches(OH_AI_ModelHandle model,
                          std::vector<size_t> expected_inputs,
                          std::vector<size_t> expected_outputs,
                          bool order_sensitive = true) {
  if (!model) return false;
  bool inputs_valid = false;
  bool outputs_valid = false;
  std::vector<size_t> inputs =
      TensorCounts(OH_AI_ModelGetInputs(model), &inputs_valid);
  std::vector<size_t> outputs =
      TensorCounts(OH_AI_ModelGetOutputs(model), &outputs_valid);
  if (!inputs_valid || !outputs_valid) return false;
  if (!order_sensitive) {
    std::sort(inputs.begin(), inputs.end());
    std::sort(outputs.begin(), outputs.end());
    std::sort(expected_inputs.begin(), expected_inputs.end());
    std::sort(expected_outputs.begin(), expected_outputs.end());
  }
  return inputs == expected_inputs && outputs == expected_outputs;
}

#endif

}  // namespace

RnntSwipeModel::~RnntSwipeModel() { Reset(); }

Status RnntSwipeModel::ValidateBuffers(const RnntSwipeModelBuffers& buffers,
                                       std::string* diagnostics) {
  const auto validate = [&](const char* name,
                            const RnntModelBuffer& buffer) -> Status {
    if (buffer.data == nullptr || buffer.size == 0) {
      if (diagnostics) *diagnostics = std::string("invalid:") + name + ":missing";
      return Status::InvalidArgument(std::string("RnntSwipeModel: missing ") +
                                     name);
    }
    if (buffer.size < 8) {
      if (diagnostics) *diagnostics = std::string("invalid:") + name + ":truncated";
      return Status::ParseError(std::string("RnntSwipeModel: truncated ") +
                                name);
    }
    if (std::memcmp(buffer.data + 4, "MSL2", 4) != 0) {
      if (diagnostics) *diagnostics = std::string("invalid:") + name + ":magic";
      return Status::UnsupportedFormat(
          std::string("RnntSwipeModel: invalid MindIR magic for ") + name);
    }
    return Status::Ok();
  };

  Status status = validate("sampling", buffers.sampling);
  if (!status.ok()) return status;
  for (size_t i = 0; i < buffers.pre.size(); ++i) {
    const std::string name = "pre" + std::to_string(i);
    status = validate(name.c_str(), buffers.pre[i]);
    if (!status.ok()) return status;
  }
  for (size_t i = 0; i < buffers.post.size(); ++i) {
    const std::string name = "post" + std::to_string(i);
    status = validate(name.c_str(), buffers.post[i]);
    if (!status.ok()) return status;
  }
  status = validate("joint", buffers.joint);
  if (!status.ok()) return status;
  status = validate("predictor", buffers.predictor);
  if (!status.ok()) return status;
  if (diagnostics) *diagnostics = "valid:11_models";
  return Status::Ok();
}

void RnntSwipeModel::Reset() {
#ifdef MINDSPORE_LITE_AVAILABLE
  const auto destroy_model = [](void** handle) {
    if (!*handle) return;
    OH_AI_ModelHandle model = static_cast<OH_AI_ModelHandle>(*handle);
    OH_AI_ModelDestroy(&model);
    *handle = nullptr;
  };
  const auto destroy_context = [](void** handle) {
    if (!*handle) return;
    OH_AI_ContextHandle context = static_cast<OH_AI_ContextHandle>(*handle);
    OH_AI_ContextDestroy(&context);
    *handle = nullptr;
  };
  destroy_model(&sampling_model_);
  destroy_model(&joint_model_);
  destroy_model(&predictor_model_);
  destroy_context(&sampling_ctx_);
  destroy_context(&joint_ctx_);
  destroy_context(&predictor_ctx_);
  for (size_t i = 0; i < pre_model_.size(); ++i) {
    destroy_model(&pre_model_[i]);
    destroy_model(&post_model_[i]);
    destroy_context(&pre_ctx_[i]);
    destroy_context(&post_ctx_[i]);
  }
#else
  sampling_ctx_ = nullptr;
  joint_ctx_ = nullptr;
  predictor_ctx_ = nullptr;
  sampling_model_ = nullptr;
  joint_model_ = nullptr;
  predictor_model_ = nullptr;
  pre_ctx_.fill(nullptr);
  post_ctx_.fill(nullptr);
  pre_model_.fill(nullptr);
  post_model_.fill(nullptr);
#endif
  components_loaded_ = false;
  diagnostics_ = "not_loaded";
}

Status RnntSwipeModel::Load(const RnntSwipeModelBuffers& buffers) {
  Reset();
  Status buffer_status = ValidateBuffers(buffers, &diagnostics_);
  if (!buffer_status.ok()) return buffer_status;

#ifdef MINDSPORE_LITE_AVAILABLE
  int status = 0;
  OH_AI_ContextHandle context = nullptr;
  OH_AI_ModelHandle model = BuildModel(buffers.sampling, &context, &status);
  if (!model) {
    diagnostics_ = "unsupported:sampling_build=" + std::to_string(status);
    return Status::Ok();
  }
  sampling_model_ = model;
  sampling_ctx_ = context;
  if (!ModelContractMatches(model, {FeatureWindow{}.size()},
                            {EncoderVector{}.size()})) {
    Reset();
    diagnostics_ = "invalid:sampling_contract";
    return Status::Ok();
  }

  for (size_t i = 0; i < pre_model_.size(); ++i) {
    context = nullptr;
    model = BuildModel(buffers.pre[i], &context, &status);
    if (!model) {
      const std::string failure = "unsupported:pre" + std::to_string(i) +
                                  "_build=" + std::to_string(status);
      Reset();
      diagnostics_ = failure;
      return Status::Ok();
    }
    pre_model_[i] = model;
    pre_ctx_[i] = context;
    if (!ModelContractMatches(model,
                              {EncoderVector{}.size(), EncoderVector{}.size()},
                              {EncoderVector{}.size(), QkvVector{}.size()})) {
      const std::string failure =
          "invalid:pre" + std::to_string(i) + "_contract";
      Reset();
      diagnostics_ = failure;
      return Status::Ok();
    }

    context = nullptr;
    model = BuildModel(buffers.post[i], &context, &status);
    if (!model) {
      const std::string failure = "unsupported:post" + std::to_string(i) +
                                  "_build=" + std::to_string(status);
      Reset();
      diagnostics_ = failure;
      return Status::Ok();
    }
    post_model_[i] = model;
    post_ctx_[i] = context;
    if (!ModelContractMatches(
            model,
            {EncoderVector{}.size(), QkvVector{}.size(), QkvLeftCache{}.size(),
             QkvRightCache{}.size(), ProjectionCache{}.size()},
            {AttentionCoefficients{}.size(), EncoderVector{}.size(),
             EncoderVector{}.size()})) {
      const std::string failure =
          "invalid:post" + std::to_string(i) + "_contract";
      Reset();
      diagnostics_ = failure;
      return Status::Ok();
    }
  }

  context = nullptr;
  model = BuildModel(buffers.joint, &context, &status);
  if (!model) {
    const std::string failure =
        "unsupported:joint_build=" + std::to_string(status);
    Reset();
    diagnostics_ = failure;
    return Status::Ok();
  }
  joint_model_ = model;
  joint_ctx_ = context;
  if (!ModelContractMatches(model,
                            {EncoderVector{}.size(), PredictorVector{}.size()},
                            {JointLogits{}.size()})) {
    Reset();
    diagnostics_ = "invalid:joint_contract";
    return Status::Ok();
  }

  context = nullptr;
  model = BuildModel(buffers.predictor, &context, &status);
  if (!model) {
    const std::string failure =
        "unsupported:predictor_build=" + std::to_string(status);
    Reset();
    diagnostics_ = failure;
    return Status::Ok();
  }
  predictor_model_ = model;
  predictor_ctx_ = context;
  if (!ModelContractMatches(
          model, {1, PredictorState{}.size()},
          {JointLogits{}.size(), PredictorVector{}.size(),
           PredictorState{}.size()},
          false)) {
    Reset();
    diagnostics_ = "invalid:predictor_contract";
    return Status::Ok();
  }

  // Runtime witness for the recovered cross-model contract.  ModelBuild alone
  // would not prove that char-decoder hidden[750] can actually drive
  // joint/in/pred[750] on this MindSpore runtime.
  PredictorState predictor_state{};
  PredictorState next_predictor_state{};
  PredictorVector predictor_hidden{};
  JointLogits predictor_probabilities{};
  JointLogits joint_logits{};
  EncoderVector zero_encoder{};
  const bool predictor_ok = RunPredictorModel(
      static_cast<OH_AI_ModelHandle>(predictor_model_), 1.0f,
      predictor_state, &predictor_probabilities, &predictor_hidden,
      &next_predictor_state);
  const bool joint_ok = predictor_ok && RunModel(
      static_cast<OH_AI_ModelHandle>(joint_model_),
      {{zero_encoder.data(), zero_encoder.size()},
       {predictor_hidden.data(), predictor_hidden.size()}},
      {{joint_logits.data(), joint_logits.size()}});
  const auto finite = [](const auto& values) {
    return std::all_of(values.begin(), values.end(),
                       [](float value) { return std::isfinite(value); });
  };
  const float probability_sum = std::accumulate(
      predictor_probabilities.begin(), predictor_probabilities.end(), 0.0f);
  if (!joint_ok || !finite(predictor_probabilities) ||
      !finite(predictor_hidden) || !finite(next_predictor_state) ||
      !finite(joint_logits) || probability_sum < 0.99f ||
      probability_sum > 1.01f) {
    Reset();
    diagnostics_ = "unsupported:predictor_joint_smoke";
    return Status::Ok();
  }
  components_loaded_ = true;
  diagnostics_ =
      "loaded:11_models,pos=sinusoidal,predictor=char_dec_750,joint_smoke=ok";
#else
  (void)buffers;
  diagnostics_ = "unsupported:mindspore_lite_unavailable";
#endif
  return Status::Ok();
}

RnntSwipeModel::EncoderVector RnntSwipeModel::PositionalEmbedding(
    uint32_t position) {
  EncoderVector result{};
  constexpr float kWidth = 100.0f;
  for (size_t i = 0; i < result.size(); i += 2) {
    const float exponent = -2.0f * static_cast<float>(i / 2) / kWidth;
    const float scale = std::pow(10000.0f, exponent);
    const float phase = static_cast<float>(position) * scale;
    result[i] = std::sin(phase);
    result[i + 1] = std::cos(phase);
  }
  return result;
}

bool RnntSwipeModel::RunSampling(const FeatureWindow& input,
                                 EncoderVector* output) const {
#ifdef MINDSPORE_LITE_AVAILABLE
  if (!components_loaded_ || !output) return false;
  std::lock_guard<std::mutex> lock(inference_mutex_);
  return RunModel(static_cast<OH_AI_ModelHandle>(sampling_model_),
                  {{input.data(), input.size()}},
                  {{output->data(), output->size()}});
#else
  (void)input;
  (void)output;
  return false;
#endif
}

bool RnntSwipeModel::RunPre(size_t layer, const EncoderVector& embedding,
                            const EncoderVector& position,
                            EncoderVector* ffm1, QkvVector* qkv) const {
#ifdef MINDSPORE_LITE_AVAILABLE
  if (!components_loaded_ || layer >= pre_model_.size() || !ffm1 || !qkv) {
    return false;
  }
  std::lock_guard<std::mutex> lock(inference_mutex_);
  return RunModel(static_cast<OH_AI_ModelHandle>(pre_model_[layer]),
                  {{embedding.data(), embedding.size()},
                   {position.data(), position.size()}},
                  {{ffm1->data(), ffm1->size()}, {qkv->data(), qkv->size()}});
#else
  (void)layer;
  (void)embedding;
  (void)position;
  (void)ffm1;
  (void)qkv;
  return false;
#endif
}

bool RnntSwipeModel::RunPost(
    size_t layer, const EncoderVector& ffm1, const QkvVector& qkv,
    const QkvLeftCache& qkv_left, const QkvRightCache& qkv_right,
    const ProjectionCache& projection, AttentionCoefficients* coefficients,
    EncoderVector* ffm2, EncoderVector* next_projection) const {
#ifdef MINDSPORE_LITE_AVAILABLE
  if (!components_loaded_ || layer >= post_model_.size() || !coefficients ||
      !ffm2 || !next_projection) {
    return false;
  }
  std::lock_guard<std::mutex> lock(inference_mutex_);
  return RunModel(
      static_cast<OH_AI_ModelHandle>(post_model_[layer]),
      {{ffm1.data(), ffm1.size()}, {qkv.data(), qkv.size()},
       {qkv_left.data(), qkv_left.size()},
       {qkv_right.data(), qkv_right.size()},
       {projection.data(), projection.size()}},
      {{coefficients->data(), coefficients->size()},
       {ffm2->data(), ffm2->size()},
       {next_projection->data(), next_projection->size()}});
#else
  (void)layer;
  (void)ffm1;
  (void)qkv;
  (void)qkv_left;
  (void)qkv_right;
  (void)projection;
  (void)coefficients;
  (void)ffm2;
  (void)next_projection;
  return false;
#endif
}

bool RnntSwipeModel::RunJoint(const EncoderVector& encoder,
                              const PredictorVector& predictor,
                              JointLogits* logits) const {
#ifdef MINDSPORE_LITE_AVAILABLE
  if (!components_loaded_ || !logits) return false;
  std::lock_guard<std::mutex> lock(inference_mutex_);
  return RunModel(static_cast<OH_AI_ModelHandle>(joint_model_),
                  {{encoder.data(), encoder.size()},
                   {predictor.data(), predictor.size()}},
                  {{logits->data(), logits->size()}});
#else
  (void)encoder;
  (void)predictor;
  (void)logits;
  return false;
#endif
}

bool RnntSwipeModel::RunPredictor(float token, const PredictorState& state,
                                  JointLogits* probabilities,
                                  PredictorVector* hidden,
                                  PredictorState* next_state) const {
#ifdef MINDSPORE_LITE_AVAILABLE
  if (!components_loaded_) return false;
  std::lock_guard<std::mutex> lock(inference_mutex_);
  return RunPredictorModel(
      static_cast<OH_AI_ModelHandle>(predictor_model_), token, state,
      probabilities, hidden, next_state);
#else
  (void)token;
  (void)state;
  (void)probabilities;
  (void)hidden;
  (void)next_state;
  return false;
#endif
}

bool RnntSwipeModel::Encode(const std::vector<FeatureWindow>& windows,
                            std::vector<EncoderVector>* output) const {
  if (!components_loaded_ || !output || windows.empty() || windows.size() > 64) {
    return false;
  }

  std::vector<EncoderVector> layer_input(windows.size());
  for (size_t i = 0; i < windows.size(); ++i) {
    if (!RunSampling(windows[i], &layer_input[i])) return false;
  }

  const auto& positions = CachedPositionalEmbeddings();
  for (size_t layer = 0; layer < pre_model_.size(); ++layer) {
    std::vector<EncoderVector> ffm1(windows.size());
    std::vector<QkvVector> qkv(windows.size());
    for (size_t i = 0; i < windows.size(); ++i) {
      if (!RunPre(layer, layer_input[i], positions[i], &ffm1[i], &qkv[i])) {
        return false;
      }
    }

    std::vector<EncoderVector> layer_output(windows.size());
    std::vector<EncoderVector> projections(windows.size());
    for (size_t i = 0; i < windows.size(); ++i) {
      QkvLeftCache left{};
      QkvRightCache right{};
      ProjectionCache projection{};

      for (size_t row = 0; row < 10; ++row) {
        const ptrdiff_t source = static_cast<ptrdiff_t>(i) - 10 +
                                 static_cast<ptrdiff_t>(row);
        if (source >= 0) {
          std::copy(qkv[static_cast<size_t>(source)].begin(),
                    qkv[static_cast<size_t>(source)].end(),
                    left.begin() + row * QkvVector{}.size());
        }
      }
      for (size_t row = 0; row < 5; ++row) {
        const size_t source = i + row + 1;
        if (source < qkv.size()) {
          std::copy(qkv[source].begin(), qkv[source].end(),
                    right.begin() + row * QkvVector{}.size());
        }
      }
      for (size_t row = 0; row < 9; ++row) {
        const ptrdiff_t source = static_cast<ptrdiff_t>(i) - 9 +
                                 static_cast<ptrdiff_t>(row);
        if (source >= 0) {
          std::copy(projections[static_cast<size_t>(source)].begin(),
                    projections[static_cast<size_t>(source)].end(),
                    projection.begin() + row * EncoderVector{}.size());
        }
      }

      AttentionCoefficients coefficients{};
      if (!RunPost(layer, ffm1[i], qkv[i], left, right, projection,
                   &coefficients, &layer_output[i], &projections[i])) {
        return false;
      }
    }
    layer_input = std::move(layer_output);
  }

  *output = std::move(layer_input);
  return true;
}

bool RnntSwipeModel::ScoreTokens(
    const std::vector<EncoderVector>& encoder, const std::vector<int>& tokens,
    float* rnnt_score, float* predictor_score) const {
  if (!components_loaded_ || !rnnt_score || !predictor_score ||
      encoder.empty() || tokens.empty() || tokens.size() > 24 ||
      encoder.size() > 64) {
    return false;
  }
  for (const int token : tokens) {
    if (token < 2 || token >= static_cast<int>(JointLogits{}.size())) {
      return false;
    }
  }

  std::vector<PredictorVector> hidden(tokens.size() + 1);
  std::vector<JointLogits> probabilities(tokens.size() + 1);
  PredictorState state{};
  PredictorState next_state{};
  if (!RunPredictor(1.0f, state, &probabilities[0], &hidden[0],
                    &next_state)) {
    return false;
  }
  state = next_state;
  float predictor_total = 0.0f;
  constexpr float kProbabilityFloor = 1.0e-12f;
  for (size_t u = 0; u < tokens.size(); ++u) {
    const float probability = probabilities[u][static_cast<size_t>(tokens[u])];
    if (!std::isfinite(probability) || probability < 0.0f) return false;
    predictor_total += std::log(std::max(probability, kProbabilityFloor));
    if (!RunPredictor(static_cast<float>(tokens[u]), state,
                      &probabilities[u + 1], &hidden[u + 1], &next_state)) {
      return false;
    }
    state = next_state;
  }

  const float negative_infinity = -std::numeric_limits<float>::infinity();
  std::vector<float> current(tokens.size() + 1, negative_infinity);
  std::vector<float> next(tokens.size() + 1, negative_infinity);
  current[0] = 0.0f;
  for (const EncoderVector& frame : encoder) {
    std::fill(next.begin(), next.end(), negative_infinity);
    for (size_t u = 0; u <= tokens.size(); ++u) {
      if (!std::isfinite(current[u])) continue;
      JointLogits logits{};
      if (!RunJoint(frame, hidden[u], &logits)) return false;
      const float maximum =
          *std::max_element(logits.begin(), logits.end());
      float normalizer = 0.0f;
      for (const float value : logits) {
        if (!std::isfinite(value)) return false;
        normalizer += std::exp(value - maximum);
      }
      const float log_normalizer = maximum + std::log(normalizer);
      next[u] = std::max(next[u], current[u] + logits[0] - log_normalizer);
      if (u < tokens.size()) {
        const size_t token = static_cast<size_t>(tokens[u]);
        current[u + 1] =
            std::max(current[u + 1],
                     current[u] + logits[token] - log_normalizer);
      }
    }
    current.swap(next);
  }
  if (!std::isfinite(current.back()) || !std::isfinite(predictor_total)) {
    return false;
  }
  *rnnt_score = current.back();
  *predictor_score = predictor_total;
  return true;
}

}  // namespace hoskey::engine
