#include "emoji_suggest_runner.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <sstream>
#include <utility>

#include "text_utils.h"

#ifdef MINDSPORE_LITE_AVAILABLE
#include "mindspore/context.h"
#include "mindspore/model.h"
#include "mindspore/status.h"
#include "mindspore/tensor.h"
#endif

namespace hoskey::engine {

namespace {

constexpr size_t kEmbeddingSize = 525;
constexpr size_t kEncoderStateSize = 4000;
constexpr size_t kHiddenSize = 500;
constexpr size_t kDecoderStateSize = 2500;
constexpr size_t kExpectedOrder = 10;
constexpr size_t kMaxRankedCandidates = 12;

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

OH_AI_ModelHandle BuildModel(const EmojiSuggestModelBuffer& buffer,
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

bool HasTensorSizes(OH_AI_TensorHandleArray tensors,
                    std::vector<size_t> expected) {
  if (!tensors.handle_list || tensors.handle_num != expected.size()) return false;
  std::vector<size_t> observed;
  observed.reserve(tensors.handle_num);
  for (size_t i = 0; i < tensors.handle_num; ++i) {
    observed.push_back(OH_AI_TensorGetDataSize(tensors.handle_list[i]) /
                       sizeof(float));
  }
  std::sort(observed.begin(), observed.end());
  std::sort(expected.begin(), expected.end());
  return observed == expected;
}

bool ValidateSingleModel(OH_AI_ModelHandle model, size_t input_count,
                         size_t output_count) {
  return model && HasTensorSizes(OH_AI_ModelGetInputs(model), {input_count}) &&
         HasTensorSizes(OH_AI_ModelGetOutputs(model), {output_count});
}

bool ValidateEncoder(OH_AI_ModelHandle model) {
  return model &&
         HasTensorSizes(OH_AI_ModelGetInputs(model),
                        {kEmbeddingSize, kEncoderStateSize}) &&
         HasTensorSizes(OH_AI_ModelGetOutputs(model),
                        {kHiddenSize, kEncoderStateSize});
}

bool RunSingle(OH_AI_ModelHandle model, const float* input, size_t input_count,
               size_t expected_output_count, std::vector<float>* output) {
  if (!model || !input || !output) return false;
  OH_AI_TensorHandleArray inputs = OH_AI_ModelGetInputs(model);
  if (!inputs.handle_list || inputs.handle_num != 1) return false;
  OH_AI_TensorHandle tensor = inputs.handle_list[0];
  if (OH_AI_TensorGetDataSize(tensor) != input_count * sizeof(float)) {
    return false;
  }
  void* destination = OH_AI_TensorGetMutableData(tensor);
  if (!destination) return false;
  std::memcpy(destination, input, input_count * sizeof(float));

  OH_AI_TensorHandleArray outputs;
  const OH_AI_Status status =
      OH_AI_ModelPredict(model, inputs, &outputs, nullptr, nullptr);
  if (status != OH_AI_STATUS_SUCCESS || !outputs.handle_list ||
      outputs.handle_num != 1) {
    return false;
  }
  OH_AI_TensorHandle output_tensor = outputs.handle_list[0];
  const size_t count = OH_AI_TensorGetDataSize(output_tensor) / sizeof(float);
  const float* data =
      static_cast<const float*>(OH_AI_TensorGetData(output_tensor));
  if (!data || count != expected_output_count) return false;
  output->assign(data, data + count);
  return true;
}

bool RunEncoder(OH_AI_ModelHandle model, const std::vector<float>& embedding,
                const std::vector<float>& state, std::vector<float>* hidden,
                std::vector<float>* next_state) {
  if (!model || embedding.size() != kEmbeddingSize ||
      state.size() != kEncoderStateSize || !hidden || !next_state) {
    return false;
  }
  OH_AI_TensorHandleArray inputs = OH_AI_ModelGetInputs(model);
  if (!inputs.handle_list || inputs.handle_num != 2) return false;
  bool wrote_embedding = false;
  bool wrote_state = false;
  for (size_t i = 0; i < inputs.handle_num; ++i) {
    OH_AI_TensorHandle tensor = inputs.handle_list[i];
    const size_t count = OH_AI_TensorGetDataSize(tensor) / sizeof(float);
    void* destination = OH_AI_TensorGetMutableData(tensor);
    if (!destination) return false;
    if (!wrote_embedding && count == kEmbeddingSize) {
      std::memcpy(destination, embedding.data(), count * sizeof(float));
      wrote_embedding = true;
    } else if (!wrote_state && count == kEncoderStateSize) {
      std::memcpy(destination, state.data(), count * sizeof(float));
      wrote_state = true;
    }
  }
  if (!wrote_embedding || !wrote_state) return false;

  OH_AI_TensorHandleArray outputs;
  const OH_AI_Status status =
      OH_AI_ModelPredict(model, inputs, &outputs, nullptr, nullptr);
  if (status != OH_AI_STATUS_SUCCESS || !outputs.handle_list ||
      outputs.handle_num != 2) {
    return false;
  }
  const float* hidden_data = nullptr;
  const float* state_data = nullptr;
  for (size_t i = 0; i < outputs.handle_num; ++i) {
    OH_AI_TensorHandle tensor = outputs.handle_list[i];
    const size_t count = OH_AI_TensorGetDataSize(tensor) / sizeof(float);
    const float* data = static_cast<const float*>(OH_AI_TensorGetData(tensor));
    if (!data) return false;
    if (count == kHiddenSize) hidden_data = data;
    if (count == kEncoderStateSize) state_data = data;
  }
  if (!hidden_data || !state_data) return false;
  hidden->assign(hidden_data, hidden_data + kHiddenSize);
  next_state->assign(state_data, state_data + kEncoderStateSize);
  return true;
}

void DestroyModel(void** handle) {
  if (!*handle) return;
  OH_AI_ModelHandle model = static_cast<OH_AI_ModelHandle>(*handle);
  OH_AI_ModelDestroy(&model);
  *handle = nullptr;
}

void DestroyContext(void** handle) {
  if (!*handle) return;
  OH_AI_ContextHandle context = static_cast<OH_AI_ContextHandle>(*handle);
  OH_AI_ContextDestroy(&context);
  *handle = nullptr;
}

#endif  // MINDSPORE_LITE_AVAILABLE

}  // namespace

EmojiSuggestRunner::~EmojiSuggestRunner() { Reset(); }

bool EmojiSuggestRunner::EncodeWord(std::string_view word, WordIds* ids) {
  if (!ids) return false;
  ids->fill(0.0f);
  std::vector<char32_t> characters = DecodeUtf8(word);
  if (characters.empty() || characters.size() > ids->size() - 2 ||
      EncodeUtf8(characters) != word) {
    return false;
  }
  (*ids)[0] = 1.0f;
  size_t output = 1;
  for (char32_t character : characters) {
    if (character >= U'А' && character <= U'Я') character += U'а' - U'А';
    if (character == U'Ё' || character == U'ё') character = U'е';
    int id = -1;
    if (character == U'-') {
      id = 5;
    } else if (character >= U'а' && character <= U'я') {
      id = static_cast<int>(character - U'а') + 6;
    }
    if (id < 0) {
      ids->fill(0.0f);
      return false;
    }
    (*ids)[output++] = static_cast<float>(id);
  }
  (*ids)[output] = 2.0f;
  return true;
}

void EmojiSuggestRunner::Reset() {
#ifdef MINDSPORE_LITE_AVAILABLE
  DestroyModel(&age_model_);
  DestroyModel(&app_model_);
  DestroyModel(&sex_model_);
  DestroyModel(&word_model_);
  DestroyModel(&encoder_model_);
  DestroyModel(&decoder_model_);
  DestroyContext(&age_ctx_);
  DestroyContext(&app_ctx_);
  DestroyContext(&sex_ctx_);
  DestroyContext(&word_ctx_);
  DestroyContext(&encoder_ctx_);
  DestroyContext(&decoder_ctx_);
#endif
  loaded_ = false;
  bundle_ = YnnlmV1Bundle{};
  wrapper_.Reset();
  invocation_count_.store(0, std::memory_order_relaxed);
  scored_count_.store(0, std::memory_order_relaxed);
  failed_count_.store(0, std::memory_order_relaxed);
  latency_us_ = 0;
  fallback_ = "not_loaded";
  load_diagnostics_ = "not_loaded";
}

Status EmojiSuggestRunner::Load(const EmojiSuggestModelBuffers& models,
                                const uint8_t* ynnlm_bundle,
                                size_t ynnlm_bundle_size,
                                const uint8_t* emoji_wrapper,
                                size_t emoji_wrapper_size) {
  Reset();
  Status status = bundle_.Parse(ynnlm_bundle, ynnlm_bundle_size);
  if (!status.ok()) {
    load_diagnostics_ = "invalid:ynnlm_wrapper";
    return status;
  }
  status = wrapper_.Load(emoji_wrapper, emoji_wrapper_size);
  if (!status.ok()) {
    load_diagnostics_ = "invalid:emoji_wrapper";
    return status;
  }
  if (bundle_.order() != static_cast<int>(kExpectedOrder) ||
      bundle_.max_word_len() != static_cast<int>(WordIds{}.size()) ||
      wrapper_.order() != kExpectedOrder || wrapper_.catalog_size() == 0) {
    load_diagnostics_ = "invalid:wrapper_contract";
    return Status::UnsupportedFormat(
        "EmojiSuggestRunner: K13321 wrapper contract mismatch");
  }

#ifdef MINDSPORE_LITE_AVAILABLE
  int age_status = 0;
  int app_status = 0;
  int sex_status = 0;
  int word_status = 0;
  int encoder_status = 0;
  int decoder_status = 0;
  age_model_ = BuildModel(models.age_embedding,
                          reinterpret_cast<OH_AI_ContextHandle*>(&age_ctx_),
                          &age_status);
  app_model_ = BuildModel(models.application_embedding,
                          reinterpret_cast<OH_AI_ContextHandle*>(&app_ctx_),
                          &app_status);
  sex_model_ = BuildModel(models.sex_embedding,
                          reinterpret_cast<OH_AI_ContextHandle*>(&sex_ctx_),
                          &sex_status);
  word_model_ = BuildModel(models.word_embedding,
                           reinterpret_cast<OH_AI_ContextHandle*>(&word_ctx_),
                           &word_status);
  encoder_model_ = BuildModel(
      models.encoder, reinterpret_cast<OH_AI_ContextHandle*>(&encoder_ctx_),
      &encoder_status);
  decoder_model_ = BuildModel(
      models.decoder, reinterpret_cast<OH_AI_ContextHandle*>(&decoder_ctx_),
      &decoder_status);

  const bool valid =
      ValidateSingleModel(static_cast<OH_AI_ModelHandle>(age_model_), 1,
                          kEmbeddingSize) &&
      ValidateSingleModel(static_cast<OH_AI_ModelHandle>(app_model_), 1,
                          kEmbeddingSize) &&
      ValidateSingleModel(static_cast<OH_AI_ModelHandle>(sex_model_), 1,
                          kEmbeddingSize) &&
      ValidateSingleModel(static_cast<OH_AI_ModelHandle>(word_model_),
                          WordIds{}.size(), kEmbeddingSize) &&
      ValidateEncoder(static_cast<OH_AI_ModelHandle>(encoder_model_)) &&
      ValidateSingleModel(static_cast<OH_AI_ModelHandle>(decoder_model_),
                          kDecoderStateSize, wrapper_.catalog_size());
  if (!valid) {
    std::ostringstream diagnostics;
    diagnostics << "unsupported:model_contract:build=" << age_status << '/'
                << app_status << '/' << sex_status << '/' << word_status << '/'
                << encoder_status << '/' << decoder_status;
    Reset();
    load_diagnostics_ = diagnostics.str();
    return Status::Ok();
  }
  loaded_ = true;
  fallback_ = "none";
  load_diagnostics_ = "loaded:k13321,order=10,catalog=" +
                      std::to_string(wrapper_.catalog_size());
  return Status::Ok();
#else
  (void)models;
  load_diagnostics_ = "unsupported:mindspore_lite_unavailable";
  fallback_ = "runtime_unavailable";
  return Status::Ok();
#endif
}

ContextEmojiInference EmojiSuggestRunner::Suggest(
    const std::vector<std::string>& completed_words,
    std::string_view package_name, size_t top_k) const {
  ContextEmojiInference result;
  std::lock_guard<std::mutex> lock(inference_mutex_);
  if (!loaded_) {
    result.diagnostics = diagnostics();
    return result;
  }
  invocation_count_.fetch_add(1, std::memory_order_relaxed);

#ifdef MINDSPORE_LITE_AVAILABLE
  const auto started = std::chrono::steady_clock::now();
  const auto fail = [&](const char* reason) {
    failed_count_.fetch_add(1, std::memory_order_relaxed);
    fallback_ = reason;
    latency_us_ = static_cast<uint64_t>(std::max<int64_t>(
        0, std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now() - started)
               .count()));
    result.diagnostics = diagnostics();
    return result;
  };

  const float age_id = 0.0f;
  const float app_id = static_cast<float>(bundle_.ApplicationId(
      std::string(package_name)));
  const float sex_id = 0.0f;
  std::vector<float> age;
  std::vector<float> app;
  std::vector<float> sex;
  if (!RunSingle(static_cast<OH_AI_ModelHandle>(age_model_), &age_id, 1,
                 kEmbeddingSize, &age) ||
      !RunSingle(static_cast<OH_AI_ModelHandle>(app_model_), &app_id, 1,
                 kEmbeddingSize, &app) ||
      !RunSingle(static_cast<OH_AI_ModelHandle>(sex_model_), &sex_id, 1,
                 kEmbeddingSize, &sex)) {
    return fail("tag_inference_failed");
  }

  std::vector<float> state(kEncoderStateSize, 0.0f);
  std::vector<float> hidden(kHiddenSize, 0.0f);
  std::vector<float> next_state;
  const size_t begin = completed_words.size() > kExpectedOrder
                           ? completed_words.size() - kExpectedOrder
                           : 0;
  for (size_t index = begin; index < completed_words.size(); ++index) {
    WordIds ids;
    if (!EncodeWord(completed_words[index], &ids)) continue;
    std::vector<float> embedding;
    if (!RunSingle(static_cast<OH_AI_ModelHandle>(word_model_), ids.data(),
                   ids.size(), kEmbeddingSize, &embedding)) {
      return fail("word_embedding_failed");
    }
    for (size_t i = 0; i < embedding.size(); ++i) {
      embedding[i] += age[i] + app[i] + sex[i];
    }
    if (!RunEncoder(static_cast<OH_AI_ModelHandle>(encoder_model_), embedding,
                    state, &hidden, &next_state)) {
      return fail("encoder_failed");
    }
    state.swap(next_state);
    ++result.encoded_words;
  }
  if (result.encoded_words == 0) return fail("no_supported_context");

  std::vector<float> decoder_state;
  if (!ContextEmojiModel::ComposeRwkvTxCxH(state, hidden, &decoder_state)) {
    return fail("composer_failed");
  }
  std::vector<float> probabilities;
  if (!RunSingle(static_cast<OH_AI_ModelHandle>(decoder_model_),
                 decoder_state.data(), decoder_state.size(),
                 wrapper_.catalog_size(), &probabilities)) {
    return fail("decoder_failed");
  }
  for (float probability : probabilities) {
    if (!std::isfinite(probability) || probability < 0.0f) {
      return fail("nonfinite_probability");
    }
  }

  std::vector<size_t> ranks(probabilities.size());
  for (size_t i = 0; i < ranks.size(); ++i) ranks[i] = i;
  top_k = std::min({top_k, kMaxRankedCandidates, ranks.size()});
  std::partial_sort(
      ranks.begin(), ranks.begin() + top_k, ranks.end(),
      [&](size_t left, size_t right) {
        if (probabilities[left] != probabilities[right]) {
          return probabilities[left] > probabilities[right];
        }
        return left < right;
      });
  result.candidates.reserve(top_k);
  for (size_t i = 0; i < top_k; ++i) {
    const size_t rank = ranks[i];
    const std::string& emoji = wrapper_.emoji_for_rank(rank);
    if (!emoji.empty() && emoji != "<unk>") {
      result.candidates.push_back({emoji, probabilities[rank]});
    }
  }
  result.model_engaged = true;
  if (!result.candidates.empty()) {
    result.top_probability = result.candidates.front().probability;
  }
  scored_count_.fetch_add(result.candidates.size(), std::memory_order_relaxed);
  fallback_ = "none";
  latency_us_ = static_cast<uint64_t>(std::max<int64_t>(
      0, std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now() - started)
             .count()));
  result.diagnostics = diagnostics();
  return result;
#else
  (void)completed_words;
  (void)package_name;
  (void)top_k;
  result.diagnostics = diagnostics();
  return result;
#endif
}

std::string EmojiSuggestRunner::diagnostics() const {
  std::ostringstream stream;
  stream << "asset=" << (loaded_ ? "present" : "missing")
         << ",contract=" << (loaded_ ? "valid" : "unsupported")
         << ",variant=KEYBOARD-13321,load=" << load_diagnostics_
         << ",invoke_count=" << invocation_count()
         << ",scored_count=" << scored_count()
         << ",failed_count="
         << failed_count_.load(std::memory_order_relaxed)
         << ",fallback=" << fallback_ << ",latency_us=" << latency_us_
         << ",route=k13321_context_emoji_runtime";
  return stream.str();
}

}  // namespace hoskey::engine
