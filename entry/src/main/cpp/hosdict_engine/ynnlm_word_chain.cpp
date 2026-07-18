#include "ynnlm_word_chain.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <queue>

#include "ynnlm_bundle.h"

#ifdef MINDSPORE_LITE_AVAILABLE
#include "mindspore/context.h"
#include "mindspore/model.h"
#include "mindspore/status.h"
#include "mindspore/tensor.h"
#endif

namespace hoskey::engine {

namespace {

// Alphabet: index 0 = '-', 1..32 = а..я (no ё). Padding/unknown id = 33.
// Must match the char-CNN input contract used to train m4/emb.
constexpr int kMaxWordLen = 24;
constexpr float kPadId = 33.0f;
constexpr size_t kEmbSize = 525;
constexpr size_t kHiddenSize = 500;
constexpr size_t kStateSize = 4000;
constexpr size_t kSubwordStateSize = 2500;
constexpr size_t kCharStateSize = 1500;
constexpr size_t kCharVocabularySize = 38;
constexpr size_t kCharStartId = 1;
constexpr size_t kCharEndId = 2;
constexpr size_t kCharFirstAlphabetId = 5;

const std::array<const char*, 33>& Alphabet() {
  static const std::array<const char*, 33> chars = {
      "-", "а", "б", "в", "г", "д", "е", "ж", "з", "и", "й",
      "к", "л", "м", "н", "о", "п", "р", "с", "т", "у", "ф",
      "х", "ц", "ч", "ш", "щ", "ъ", "ы", "ь", "э", "ю", "я"};
  return chars;
}

// Maps a UTF-8 Cyrillic prefix to char ids. Returns false if any character is
// outside the alphabet.
bool CharIds(const std::string& utf8, std::vector<int>* out) {
  const auto& chars = Alphabet();
  out->clear();
  for (size_t i = 0; i < utf8.size();) {
    int hit = -1;
    size_t len = 0;
    for (size_t id = 1; id < chars.size(); ++id) {
      const size_t clen = std::strlen(chars[id]);
      if (i + clen <= utf8.size() &&
          std::memcmp(utf8.data() + i, chars[id], clen) == 0) {
        hit = static_cast<int>(id);
        len = clen;
        break;
      }
    }
    if (hit < 0) return false;
    out->push_back(hit);
    i += len;
  }
  return true;
}

bool StartsWith(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() &&
         std::memcmp(s.data(), prefix.data(), prefix.size()) == 0;
}

std::string PredictionCacheKey(const std::string& package_name,
                               const std::vector<std::string>& context) {
  std::string key;
  key.reserve(package_name.size() + context.size() * 12U + 16U);
  const auto append_part = [&key](const std::string& part) {
    key += std::to_string(part.size());
    key.push_back(':');
    key += part;
    key.push_back('|');
  };
  append_part(package_name);
  for (const std::string& word : context) append_part(word);
  return key;
}

// Builds rank-ordered tables (logit index -> token) from the ynnlm bundle.
Status BuildRankTables(const uint8_t* vocab, size_t vocab_size,
                       std::vector<std::string>* rank_words,
                       std::vector<std::string>* rank_subwords,
                       std::unordered_map<std::string, uint32_t>* application_ids,
                       std::string* diag) {
  YnnlmV1Bundle bundle;
  Status s = bundle.Parse(vocab, vocab_size);
  if (!s.ok()) return s;
  const auto& words = bundle.words();
  const auto& word_rank_to_lex = bundle.word_rank_to_lex();
  rank_words->assign(word_rank_to_lex.size(), std::string());
  for (size_t r = 0; r < word_rank_to_lex.size(); ++r) {
    const uint32_t lex = word_rank_to_lex[r];
    if (lex < words.size()) (*rank_words)[r] = words[lex];
  }
  const auto& subwords = bundle.subwords();
  const auto& subword_rank_to_lex = bundle.subword_rank_to_lex();
  rank_subwords->assign(subword_rank_to_lex.size(), std::string());
  for (size_t r = 0; r < subword_rank_to_lex.size(); ++r) {
    const uint32_t lex = subword_rank_to_lex[r];
    if (lex < subwords.size()) (*rank_subwords)[r] = subwords[lex];
  }
  *application_ids = bundle.application_ids();
  *diag = "words=" + std::to_string(rank_words->size()) +
          ",subwords=" + std::to_string(rank_subwords->size()) +
          ",apps=" + std::to_string(application_ids->size());
  return Status::Ok();
}

#ifdef MINDSPORE_LITE_AVAILABLE

OH_AI_ContextHandle CreateCpuContext() {
  OH_AI_ContextHandle ctx = OH_AI_ContextCreate();
  if (!ctx) return nullptr;
  OH_AI_ContextSetThreadNum(ctx, 2);
  OH_AI_ContextSetEnableParallel(ctx, false);
  OH_AI_DeviceInfoHandle cpu = OH_AI_DeviceInfoCreate(OH_AI_DEVICETYPE_CPU);
  if (cpu) {
    OH_AI_DeviceInfoSetEnableFP16(cpu, false);
    OH_AI_ContextAddDeviceInfo(ctx, cpu);
  }
  return ctx;
}

// Builds a model with its OWN context (buffer-build consumes the context).
// On success *out_ctx owns the context the returned model depends on.
OH_AI_ModelHandle BuildModel(const uint8_t* data, size_t size,
                             OH_AI_ContextHandle* out_ctx, int* out_status) {
  *out_ctx = nullptr;
  if (out_status) *out_status = -100;
  if (!data || size == 0 || size < 8 ||
      std::memcmp(data + 4, "MSL2", 4) != 0) {
    return nullptr;
  }
  OH_AI_ContextHandle ctx = CreateCpuContext();
  if (!ctx) {
    if (out_status) *out_status = -101;
    return nullptr;
  }
  OH_AI_ModelHandle model = OH_AI_ModelCreate();
  if (!model) {
    OH_AI_ContextDestroy(&ctx);
    if (out_status) *out_status = -102;
    return nullptr;
  }
  const OH_AI_Status st =
      OH_AI_ModelBuild(model, data, size, OH_AI_MODELTYPE_MINDIR, ctx);
  if (out_status) *out_status = static_cast<int>(st);
  if (st != OH_AI_STATUS_SUCCESS) {
    OH_AI_ModelDestroy(&model);
    OH_AI_ContextDestroy(&ctx);
    return nullptr;
  }
  *out_ctx = ctx;
  return model;
}

// Runs a single-input, single-output model. Returns false on any shape/status
// mismatch. output is resized to the flat float count of output tensor 0.
bool RunModel(OH_AI_ModelHandle model, const float* in, size_t in_count,
              std::vector<float>* output) {
  if (!model) return false;
  OH_AI_TensorHandleArray inputs = OH_AI_ModelGetInputs(model);
  if (!inputs.handle_list || inputs.handle_num == 0) return false;
  OH_AI_TensorHandle in_t = inputs.handle_list[0];
  if (OH_AI_TensorGetDataSize(in_t) != in_count * sizeof(float)) return false;
  void* dst = OH_AI_TensorGetMutableData(in_t);
  if (!dst) return false;
  std::memcpy(dst, in, in_count * sizeof(float));

  OH_AI_TensorHandleArray outputs;
  const OH_AI_Status st =
      OH_AI_ModelPredict(model, inputs, &outputs, nullptr, nullptr);
  if (st != OH_AI_STATUS_SUCCESS || !outputs.handle_list ||
      outputs.handle_num == 0) {
    return false;
  }
  OH_AI_TensorHandle out_t = outputs.handle_list[0];
  const float* p = static_cast<const float*>(OH_AI_TensorGetData(out_t));
  const size_t n = OH_AI_TensorGetDataSize(out_t) / sizeof(float);
  if (!p || n == 0) return false;
  output->assign(p, p + n);
  return true;
}

bool RunEncoderModel(OH_AI_ModelHandle model, const float* embedding,
                     const float* state, std::vector<float>* hidden,
                     std::vector<float>* next_state) {
  if (!model || !embedding || !state || !hidden || !next_state) return false;
  OH_AI_TensorHandleArray inputs = OH_AI_ModelGetInputs(model);
  if (!inputs.handle_list || inputs.handle_num < 2) return false;

  bool embedding_written = false;
  bool state_written = false;
  for (size_t i = 0; i < inputs.handle_num; ++i) {
    OH_AI_TensorHandle tensor = inputs.handle_list[i];
    const size_t bytes = OH_AI_TensorGetDataSize(tensor);
    void* dst = OH_AI_TensorGetMutableData(tensor);
    if (!dst) return false;
    if (!embedding_written && bytes == kEmbSize * sizeof(float)) {
      std::memcpy(dst, embedding, bytes);
      embedding_written = true;
    } else if (!state_written && bytes == kStateSize * sizeof(float)) {
      std::memcpy(dst, state, bytes);
      state_written = true;
    }
  }
  if (!embedding_written || !state_written) return false;

  OH_AI_TensorHandleArray outputs;
  const OH_AI_Status status =
      OH_AI_ModelPredict(model, inputs, &outputs, nullptr, nullptr);
  if (status != OH_AI_STATUS_SUCCESS || !outputs.handle_list ||
      outputs.handle_num < 2) {
    return false;
  }

  const float* hidden_data = nullptr;
  const float* state_data = nullptr;
  for (size_t i = 0; i < outputs.handle_num; ++i) {
    OH_AI_TensorHandle tensor = outputs.handle_list[i];
    const size_t count = OH_AI_TensorGetDataSize(tensor) / sizeof(float);
    const float* data =
        static_cast<const float*>(OH_AI_TensorGetData(tensor));
    if (!data) return false;
    if (!hidden_data && count == kHiddenSize) {
      hidden_data = data;
    } else if (!state_data && count == kStateSize) {
      state_data = data;
    }
  }
  if (!hidden_data || !state_data) return false;
  hidden->assign(hidden_data, hidden_data + kHiddenSize);
  next_state->assign(state_data, state_data + kStateSize);
  return true;
}

bool RunStateDecoder(OH_AI_ModelHandle model, float token,
                     const std::vector<float>& state, size_t state_size,
                     size_t vocab_size, std::vector<float>* logits,
                     std::vector<float>* next_state) {
  if (!model || state.size() < state_size) return false;
  OH_AI_TensorHandleArray inputs = OH_AI_ModelGetInputs(model);
  if (!inputs.handle_list || inputs.handle_num < 2) return false;

  bool token_written = false;
  bool state_written = false;
  for (size_t i = 0; i < inputs.handle_num; ++i) {
    OH_AI_TensorHandle in_t = inputs.handle_list[i];
    const size_t bytes = OH_AI_TensorGetDataSize(in_t);
    void* dst = OH_AI_TensorGetMutableData(in_t);
    if (!dst) return false;
    if (!token_written && bytes == sizeof(float)) {
      std::memcpy(dst, &token, sizeof(float));
      token_written = true;
    } else if (!state_written && bytes == state_size * sizeof(float)) {
      std::memcpy(dst, state.data(), state_size * sizeof(float));
      state_written = true;
    }
  }
  if (!token_written || !state_written) return false;

  OH_AI_TensorHandleArray outputs;
  const OH_AI_Status st =
      OH_AI_ModelPredict(model, inputs, &outputs, nullptr, nullptr);
  if (st != OH_AI_STATUS_SUCCESS || !outputs.handle_list ||
      outputs.handle_num == 0) {
    return false;
  }

  const float* logits_ptr = nullptr;
  size_t logits_count = 0;
  const float* state_ptr = nullptr;
  size_t state_count = 0;
  for (size_t i = 0; i < outputs.handle_num; ++i) {
    OH_AI_TensorHandle out_t = outputs.handle_list[i];
    const float* p = static_cast<const float*>(OH_AI_TensorGetData(out_t));
    const size_t n = OH_AI_TensorGetDataSize(out_t) / sizeof(float);
    if (!p || n == 0) continue;
    if (!logits_ptr && n == vocab_size) {
      logits_ptr = p;
      logits_count = n;
    } else if (!state_ptr && n == state_size) {
      state_ptr = p;
      state_count = n;
    }
  }
  if (!logits_ptr || logits_count == 0) return false;
  logits->assign(logits_ptr, logits_ptr + logits_count);
  if (next_state && state_ptr && state_count == state_size) {
    next_state->assign(state_ptr, state_ptr + state_count);
  }
  return true;
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

bool MatchesYnnlmContract(OH_AI_ModelHandle model,
                          YnnlmModelContract contract,
                          size_t vocabulary_size) {
  switch (contract) {
    case YnnlmModelContract::kWordEmbedding:
      return ModelContractMatches(model, {kMaxWordLen}, {kEmbSize});
    case YnnlmModelContract::kMetadataEmbedding:
      return ModelContractMatches(model, {1}, {kEmbSize});
    case YnnlmModelContract::kWordEncoder:
      return ModelContractMatches(model, {kEmbSize, kStateSize},
                                  {kHiddenSize, kStateSize}, false);
    case YnnlmModelContract::kCharBridge:
      return ModelContractMatches(model, {kHiddenSize}, {kCharStateSize});
    case YnnlmModelContract::kWordDecoder:
      return vocabulary_size != 0 &&
             ModelContractMatches(model, {kHiddenSize}, {vocabulary_size});
    case YnnlmModelContract::kSubwordBridge:
      return ModelContractMatches(model, {kHiddenSize}, {kSubwordStateSize});
    case YnnlmModelContract::kSubwordDecoder:
      return vocabulary_size != 0 &&
             ModelContractMatches(model, {1, kSubwordStateSize},
                                  {vocabulary_size, kHiddenSize,
                                   kSubwordStateSize}, false);
  }
  return false;
}

#endif  // MINDSPORE_LITE_AVAILABLE

}  // namespace

YnnlmWordChain::~YnnlmWordChain() { Reset(); }

Status YnnlmWordChain::ValidateSerializedModel(const uint8_t* data, size_t size,
                                               std::string_view role) {
  const std::string prefix = "YnnlmWordChain: " + std::string(role);
  if (data == nullptr || size == 0) {
    return Status::InvalidArgument(prefix + " model missing");
  }
  if (size < 8) return Status::ParseError(prefix + " model truncated");
  if (std::memcmp(data + 4, "MSL2", 4) != 0) {
    return Status::UnsupportedFormat(prefix + " model has invalid MindIR magic");
  }
  return Status::Ok();
}

Status YnnlmWordChain::ProbeModelContract(const uint8_t* data, size_t size,
                                          YnnlmModelContract contract,
                                          size_t vocabulary_size) {
  Status serialized = ValidateSerializedModel(data, size, "probe");
  if (!serialized.ok()) return serialized;
#ifdef MINDSPORE_LITE_AVAILABLE
  OH_AI_ContextHandle context = nullptr;
  int build_status = 0;
  OH_AI_ModelHandle model = BuildModel(data, size, &context, &build_status);
  if (!model) {
    return Status::UnsupportedFormat(
        "YnnlmWordChain: probe model build failed=" +
        std::to_string(build_status));
  }
  const bool matches = MatchesYnnlmContract(model, contract, vocabulary_size);
  OH_AI_ModelDestroy(&model);
  OH_AI_ContextDestroy(&context);
  return matches ? Status::Ok()
                 : Status::InvalidArgument(
                       "YnnlmWordChain: probe model contract mismatch");
#else
  (void)contract;
  (void)vocabulary_size;
  return Status::NotImplemented(
      "YnnlmWordChain: MindSpore Lite unavailable for contract probe");
#endif
}

void YnnlmWordChain::Reset() {
#ifdef MINDSPORE_LITE_AVAILABLE
  auto destroy_model = [](void** h) {
    if (*h) {
      OH_AI_ModelHandle m = static_cast<OH_AI_ModelHandle>(*h);
      OH_AI_ModelDestroy(&m);
      *h = nullptr;
    }
  };
  auto destroy_ctx = [](void** h) {
    if (*h) {
      OH_AI_ContextHandle c = static_cast<OH_AI_ContextHandle>(*h);
      OH_AI_ContextDestroy(&c);
      *h = nullptr;
    }
  };
  destroy_model(&emb_model_);
  destroy_model(&emb_age_model_);
  destroy_model(&emb_app_model_);
  destroy_model(&emb_sex_model_);
  destroy_model(&enc_model_);
  destroy_model(&enc_dec_model_);
  destroy_model(&subword_enc_dec_model_);
  destroy_model(&subword_dec_model_);
  destroy_model(&dec_model_);
  destroy_ctx(&emb_ctx_);
  destroy_ctx(&emb_age_ctx_);
  destroy_ctx(&emb_app_ctx_);
  destroy_ctx(&emb_sex_ctx_);
  destroy_ctx(&enc_ctx_);
  destroy_ctx(&enc_dec_ctx_);
  destroy_ctx(&subword_enc_dec_ctx_);
  destroy_ctx(&subword_dec_ctx_);
  destroy_ctx(&dec_ctx_);
#endif
  char_decoder_.Reset();
  loaded_ = false;
  fallback_encoder_loaded_ = false;
  runtime_encoder_loaded_ = false;
  subword_decoder_loaded_ = false;
  char_decoder_loaded_ = false;
  rank_words_.clear();
  rank_subwords_.clear();
  rank_lexicographic_ids_.clear();
  rank_by_word_.clear();
  rank_by_subword_.clear();
  application_id_by_package_.clear();
  subword_start_id_ = 1;
  subword_end_id_ = 2;
  subword_pad_id_ = 0;
  subword_unk_id_ = 3;
  {
    std::lock_guard<std::mutex> lock(prediction_cache_mutex_);
    prediction_cache_key_.clear();
    prediction_cache_hidden_.clear();
    prediction_cache_logits_.clear();
    prediction_cache_runtime_encoder_used_ = false;
    prediction_cache_application_embedding_used_ = false;
    prediction_cache_application_id_ = 0;
  }
  diagnostics_ = "not_loaded";
}

Status YnnlmWordChain::Load(const uint8_t* emb_model, size_t emb_size,
                            const uint8_t* enc_dec_model, size_t enc_dec_size,
                            const uint8_t* dec_model, size_t dec_size,
                            const uint8_t* encoder_weights, size_t encoder_size,
                            const uint8_t* vocab_bundle, size_t vocab_size,
                            const uint8_t* emb_age_model, size_t emb_age_size,
                            const uint8_t* emb_app_model, size_t emb_app_size,
                            const uint8_t* emb_sex_model, size_t emb_sex_size,
                            const uint8_t* enc_model, size_t enc_size,
                            const uint8_t* subword_enc_dec_model,
                            size_t subword_enc_dec_size,
                            const uint8_t* subword_dec_model,
                            size_t subword_dec_size,
                            const uint8_t* char_dec_model,
                            size_t char_dec_size) {
  Reset();

  Status serialized = ValidateSerializedModel(emb_model, emb_size, "emb");
  if (!serialized.ok()) {
    diagnostics_ = "invalid:emb_serialized";
    return serialized;
  }
  serialized =
      ValidateSerializedModel(enc_dec_model, enc_dec_size, "enc_dec");
  if (!serialized.ok()) {
    diagnostics_ = "invalid:enc_dec_serialized";
    return serialized;
  }
  serialized = ValidateSerializedModel(dec_model, dec_size, "dec");
  if (!serialized.ok()) {
    diagnostics_ = "invalid:dec_serialized";
    return serialized;
  }

  // Vocabulary + encoder are host-safe (no MindSpore dependency).
  std::string vocab_diag;
  Status vs = BuildRankTables(vocab_bundle, vocab_size, &rank_words_,
                              &rank_subwords_, &application_id_by_package_,
                              &vocab_diag);
  if (!vs.ok()) {
    diagnostics_ = "invalid:vocab";
    return vs;
  }
  rank_by_word_.reserve(rank_words_.size());
  for (size_t i = 0; i < rank_words_.size(); ++i) {
    if (!rank_words_[i].empty()) rank_by_word_.emplace(rank_words_[i], i);
  }
  rank_lexicographic_ids_ = BuildYnnlmLexicographicIndex(rank_words_);
  rank_by_subword_.reserve(rank_subwords_.size());
  for (size_t i = 0; i < rank_subwords_.size(); ++i) {
    if (!rank_subwords_[i].empty()) {
      rank_by_subword_.emplace(rank_subwords_[i], i);
    }
  }
  const auto set_subword_id = [this](const char* token, size_t* out) {
    const auto it = rank_by_subword_.find(token);
    if (it != rank_by_subword_.end()) *out = it->second;
  };
  set_subword_id("<pad>", &subword_pad_id_);
  set_subword_id("<s>", &subword_start_id_);
  set_subword_id("</s>", &subword_end_id_);
  set_subword_id("<unk>", &subword_unk_id_);
  if (encoder_weights && encoder_size != 0) {
    const rwkv::Status es = encoder_.Load(encoder_weights, encoder_size);
    if (!es.ok) {
      diagnostics_ = "invalid:encoder:" + es.message;
      Reset();
      return Status::ParseError("YnnlmWordChain: encoder load failed: " +
                                es.message);
    }
    fallback_encoder_loaded_ = true;
  }

#ifdef MINDSPORE_LITE_AVAILABLE
  int st_emb = 0, st_bridge = 0, st_dec = 0;
  int st_emb_age = -200, st_emb_app = -200, st_emb_sex = -200;
  int st_enc = -200, st_subword_bridge = -200;
  int st_subword_dec = -200;
  OH_AI_ContextHandle emb_ctx = nullptr, bridge_ctx = nullptr, dec_ctx = nullptr;
  OH_AI_ModelHandle emb = BuildModel(emb_model, emb_size, &emb_ctx, &st_emb);
  OH_AI_ModelHandle bridge =
      BuildModel(enc_dec_model, enc_dec_size, &bridge_ctx, &st_bridge);
  OH_AI_ModelHandle dec = BuildModel(dec_model, dec_size, &dec_ctx, &st_dec);
  if (!emb || !bridge || !dec) {
    if (emb) OH_AI_ModelDestroy(&emb);
    if (bridge) OH_AI_ModelDestroy(&bridge);
    if (dec) OH_AI_ModelDestroy(&dec);
    if (emb_ctx) OH_AI_ContextDestroy(&emb_ctx);
    if (bridge_ctx) OH_AI_ContextDestroy(&bridge_ctx);
    if (dec_ctx) OH_AI_ContextDestroy(&dec_ctx);
    diagnostics_ = "unsupported:model_build_failed:emb=" +
                   std::to_string(st_emb) + ",bridge=" +
                   std::to_string(st_bridge) + ",dec=" +
                   std::to_string(st_dec) + "(" +
                   std::to_string(emb_size) + "/" +
                   std::to_string(enc_dec_size) + "/" +
                   std::to_string(dec_size) + ")";
    return Status::Ok();
  }

  const auto build_optional =
      [](const uint8_t* data, size_t size, OH_AI_ContextHandle* ctx,
         int* status) -> OH_AI_ModelHandle {
    if (!data || size == 0) {
      if (status) *status = -200;  // optional asset not packaged
      if (ctx) *ctx = nullptr;
      return nullptr;
    }
    return BuildModel(data, size, ctx, status);
  };

  OH_AI_ContextHandle emb_age_ctx = nullptr, emb_app_ctx = nullptr;
  OH_AI_ContextHandle emb_sex_ctx = nullptr, enc_ctx = nullptr;
  OH_AI_ContextHandle subword_bridge_ctx = nullptr, subword_dec_ctx = nullptr;
  OH_AI_ModelHandle emb_age =
      build_optional(emb_age_model, emb_age_size, &emb_age_ctx, &st_emb_age);
  OH_AI_ModelHandle emb_app =
      build_optional(emb_app_model, emb_app_size, &emb_app_ctx, &st_emb_app);
  OH_AI_ModelHandle emb_sex =
      build_optional(emb_sex_model, emb_sex_size, &emb_sex_ctx, &st_emb_sex);
  OH_AI_ModelHandle enc =
      build_optional(enc_model, enc_size, &enc_ctx, &st_enc);
  OH_AI_ModelHandle subword_bridge = build_optional(
      subword_enc_dec_model, subword_enc_dec_size, &subword_bridge_ctx,
      &st_subword_bridge);
  OH_AI_ModelHandle subword_dec = build_optional(
      subword_dec_model, subword_dec_size, &subword_dec_ctx, &st_subword_dec);
  std::string char_decoder_diag = "absent";
  if (char_dec_model && char_dec_size != 0) {
    const Status char_status = char_decoder_.Load(char_dec_model, char_dec_size);
    char_decoder_diag = char_status.ok()
                            ? char_decoder_.diagnostics()
                            : "invalid:" + char_status.message;
  }

  emb_model_ = emb;
  emb_ctx_ = emb_ctx;
  emb_age_model_ = emb_age;
  emb_age_ctx_ = emb_age_ctx;
  emb_app_model_ = emb_app;
  emb_app_ctx_ = emb_app_ctx;
  emb_sex_model_ = emb_sex;
  emb_sex_ctx_ = emb_sex_ctx;
  enc_model_ = enc;
  enc_ctx_ = enc_ctx;
  enc_dec_model_ = bridge;
  enc_dec_ctx_ = bridge_ctx;
  subword_enc_dec_model_ = subword_bridge;
  subword_enc_dec_ctx_ = subword_bridge_ctx;
  subword_dec_model_ = subword_dec;
  subword_dec_ctx_ = subword_dec_ctx;
  dec_model_ = dec;
  dec_ctx_ = dec_ctx;
  std::string contract_failure;
  if (!MatchesYnnlmContract(emb, YnnlmModelContract::kWordEmbedding, 0)) {
    contract_failure = "invalid:emb_contract";
  } else if (!MatchesYnnlmContract(
                 bridge, YnnlmModelContract::kCharBridge, 0)) {
    contract_failure = "invalid:enc_dec_contract";
  } else if (!MatchesYnnlmContract(
                 dec, YnnlmModelContract::kWordDecoder,
                 rank_words_.size())) {
    contract_failure = "invalid:dec_contract";
  } else if (emb_age && !MatchesYnnlmContract(
                            emb_age, YnnlmModelContract::kMetadataEmbedding, 0)) {
    contract_failure = "invalid:emb_age_contract";
  } else if (emb_app && !MatchesYnnlmContract(
                            emb_app, YnnlmModelContract::kMetadataEmbedding, 0)) {
    contract_failure = "invalid:emb_app_contract";
  } else if (emb_sex && !MatchesYnnlmContract(
                            emb_sex, YnnlmModelContract::kMetadataEmbedding, 0)) {
    contract_failure = "invalid:emb_sex_contract";
  } else if (enc && !MatchesYnnlmContract(
                         enc, YnnlmModelContract::kWordEncoder, 0)) {
    contract_failure = "invalid:enc_contract";
  } else if (subword_bridge && !MatchesYnnlmContract(
                                    subword_bridge,
                                    YnnlmModelContract::kSubwordBridge, 0)) {
    contract_failure = "invalid:subword_enc_dec_contract";
  } else if (subword_dec && !MatchesYnnlmContract(
                                 subword_dec,
                                 YnnlmModelContract::kSubwordDecoder,
                                 rank_subwords_.size())) {
    contract_failure = "invalid:subword_dec_contract";
  }
  if (!contract_failure.empty()) {
    Reset();
    diagnostics_ = contract_failure;
    return Status::Ok();
  }
  runtime_encoder_loaded_ = enc != nullptr;
  if (!runtime_encoder_loaded_ && !fallback_encoder_loaded_) {
    Reset();
    diagnostics_ = "unsupported:no_encoder";
    return Status::Ok();
  }
  subword_decoder_loaded_ = subword_bridge != nullptr && subword_dec != nullptr;
  char_decoder_loaded_ = char_decoder_.is_loaded();
  loaded_ = true;
  diagnostics_ = "loaded:" + vocab_diag +
                 ",fallback_enc=" +
                 std::to_string(fallback_encoder_loaded_ ? 1 : 0) +
                  ",aux{emb_age=" + std::to_string(st_emb_age) +
                  ",emb_app=" + std::to_string(st_emb_app) +
                  ",emb_sex=" + std::to_string(st_emb_sex) +
                  ",enc=" + std::to_string(st_enc) +
                 ",subword_bridge=" + std::to_string(st_subword_bridge) +
                  ",subword_dec=" + std::to_string(st_subword_dec) +
                  ",char_dec=" + char_decoder_diag + "}";
  return Status::Ok();
#else
  (void)emb_model;
  (void)emb_size;
  (void)enc_dec_model;
  (void)enc_dec_size;
  (void)dec_model;
  (void)dec_size;
  (void)emb_age_model;
  (void)emb_age_size;
  (void)emb_app_model;
  (void)emb_app_size;
  (void)emb_sex_model;
  (void)emb_sex_size;
  (void)enc_model;
  (void)enc_size;
  (void)subword_enc_dec_model;
  (void)subword_enc_dec_size;
  (void)subword_dec_model;
  (void)subword_dec_size;
  (void)char_dec_model;
  (void)char_dec_size;
  diagnostics_ = "unsupported:mindspore_lite_unavailable:" + vocab_diag;
  return Status::Ok();
#endif
}

YnnlmPredictionBatch SelectYnnlmPredictions(
    std::string_view prefix, const std::vector<std::string>& rank_words,
    const std::vector<float>& logits, size_t topk) {
  YnnlmPredictionBatch result;
  std::vector<std::pair<float, size_t>> candidates;
  const size_t count = std::min(rank_words.size(), logits.size());
  for (size_t i = 0; i < count; ++i) {
    const float logit = logits[i];
    if (!std::isfinite(logit)) continue;
    const std::string& word = rank_words[i];
    if (word == prefix) {
      result.has_original_logit = true;
      result.original_raw_logit = logit;
    }
    if (!word.empty() && word.size() >= prefix.size() &&
        word.compare(0, prefix.size(), prefix) == 0) {
      candidates.push_back({logit, i});
    }
  }

  const size_t selected = std::min(topk, candidates.size());
  std::partial_sort(
      candidates.begin(), candidates.begin() + selected, candidates.end(),
      [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first > b.first;
        return a.second < b.second;
      });
  result.candidates.reserve(selected);
  for (size_t i = 0; i < selected; ++i) {
    result.candidates.push_back(
        {rank_words[candidates[i].second], candidates[i].first});
  }
  return result;
}

std::vector<size_t> BuildYnnlmLexicographicIndex(
    const std::vector<std::string>& rank_words) {
  std::vector<size_t> ids;
  ids.reserve(rank_words.size());
  for (size_t i = 0; i < rank_words.size(); ++i) {
    if (!rank_words[i].empty()) ids.push_back(i);
  }
  std::sort(ids.begin(), ids.end(), [&](size_t lhs, size_t rhs) {
    if (rank_words[lhs] != rank_words[rhs]) {
      return rank_words[lhs] < rank_words[rhs];
    }
    return lhs < rhs;
  });
  return ids;
}

YnnlmPredictionBatch SelectYnnlmPredictionsIndexed(
    std::string_view prefix, const std::vector<std::string>& rank_words,
    const std::vector<float>& logits,
    const std::vector<size_t>& lexicographic_ids, size_t topk) {
  YnnlmPredictionBatch result;
  using Candidate = std::pair<float, size_t>;
  const auto better = [](const Candidate& lhs, const Candidate& rhs) {
    if (lhs.first != rhs.first) return lhs.first > rhs.first;
    return lhs.second < rhs.second;
  };
  struct BetterComesLater {
    bool operator()(const Candidate& lhs, const Candidate& rhs) const {
      if (lhs.first != rhs.first) return lhs.first > rhs.first;
      return lhs.second < rhs.second;
    }
  };
  std::priority_queue<Candidate, std::vector<Candidate>, BetterComesLater>
      selected;

  auto begin = lexicographic_ids.begin();
  if (!prefix.empty()) {
    begin = std::lower_bound(
        lexicographic_ids.begin(), lexicographic_ids.end(), prefix,
        [&](size_t id, std::string_view value) {
          if (id >= rank_words.size()) return true;
          const std::string& word = rank_words[id];
          return word.compare(0, word.size(), value.data(), value.size()) < 0;
        });
  }

  for (auto it = begin; it != lexicographic_ids.end(); ++it) {
    const size_t id = *it;
    if (id >= rank_words.size() || id >= logits.size()) continue;
    const std::string& word = rank_words[id];
    if (!prefix.empty() &&
        (word.size() < prefix.size() ||
         word.compare(0, prefix.size(), prefix) != 0)) {
      break;
    }
    const float logit = logits[id];
    if (!std::isfinite(logit)) continue;
    if (word == prefix) {
      result.has_original_logit = true;
      result.original_raw_logit = logit;
    }
    if (topk == 0) continue;
    const Candidate candidate{logit, id};
    if (selected.size() < topk) {
      selected.push(candidate);
    } else if (better(candidate, selected.top())) {
      selected.pop();
      selected.push(candidate);
    }
  }

  std::vector<Candidate> ordered;
  ordered.reserve(selected.size());
  while (!selected.empty()) {
    ordered.push_back(selected.top());
    selected.pop();
  }
  std::sort(ordered.begin(), ordered.end(), better);
  result.candidates.reserve(ordered.size());
  for (const auto& candidate : ordered) {
    result.candidates.push_back(
        {rank_words[candidate.second], candidate.first});
  }
  return result;
}

bool LookupYnnlmProbability(
    std::string_view word, const std::vector<std::string>& rank_words,
    const std::vector<float>& probabilities, float* out) {
  if (out == nullptr) return false;
  const size_t count = std::min(rank_words.size(), probabilities.size());
  for (size_t i = 0; i < count; ++i) {
    if (rank_words[i] == word && std::isfinite(probabilities[i])) {
      *out = probabilities[i];
      return true;
    }
  }
  return false;
}

bool ShouldRunYnnlmAuxiliary(size_t candidate_count, size_t requested_topk) {
  constexpr size_t kUsefulPrimaryCandidateCount = 8;
  if (requested_topk == 0) return false;
  return candidate_count <
         std::min(requested_topk, kUsefulPrimaryCandidateCount);
}

bool BuildYnnlmAutoccCharFeatures(
    const YnnlmCharScore& original, size_t original_length,
    const YnnlmCharScore& correction, size_t correction_length,
    std::array<float, 6>* output) {
  if (!output || !original.available || !correction.available ||
      original_length == 0 || correction_length == 0 ||
      !std::isfinite(original.total_log2_probability) ||
      !std::isfinite(original.eos_log2_probability) ||
      !std::isfinite(correction.total_log2_probability) ||
      !std::isfinite(correction.eos_log2_probability)) {
    return false;
  }
  (*output)[0] = original.total_log2_probability;
  (*output)[1] = -1.0f / static_cast<float>(original_length);
  (*output)[2] = original.eos_log2_probability;
  (*output)[3] = correction.total_log2_probability;
  (*output)[4] = -1.0f / static_cast<float>(correction_length);
  (*output)[5] = correction.eos_log2_probability;
  return true;
}

bool YnnlmWordChain::BuildContextState(
    const std::vector<std::string>& context,
    const std::string& package_name, ContextState* output) const {
  if (!output) return false;
  *output = ContextState{};
  output->hidden.assign(kHiddenSize, 0.0f);
  const auto app_it = application_id_by_package_.find(package_name);
  output->application_id = app_it == application_id_by_package_.end()
                               ? 0U
                               : app_it->second;

  // Native TNeuralCharLM receives an empty context vector as exactly 500
  // zeros.  Demographic/application metadata is attached to each context word;
  // it is not an independent encoder token.
  if (context.empty()) return true;

#ifdef MINDSPORE_LITE_AVAILABLE
  OH_AI_ModelHandle emb_m = static_cast<OH_AI_ModelHandle>(emb_model_);
  OH_AI_ModelHandle emb_age_m =
      static_cast<OH_AI_ModelHandle>(emb_age_model_);
  OH_AI_ModelHandle emb_app_m =
      static_cast<OH_AI_ModelHandle>(emb_app_model_);
  OH_AI_ModelHandle emb_sex_m =
      static_cast<OH_AI_ModelHandle>(emb_sex_model_);
  OH_AI_ModelHandle enc_m = static_cast<OH_AI_ModelHandle>(enc_model_);
  if (!emb_m || !emb_age_m || !emb_app_m || !emb_sex_m ||
      (!enc_m && !fallback_encoder_loaded_)) {
    output->application_embedding_failed = true;
    return false;
  }

  const float age_id = 0.0f;
  const float app_id = static_cast<float>(output->application_id);
  const float sex_id = 0.0f;
  std::vector<float> age;
  std::vector<float> app;
  std::vector<float> sex;
  if (!RunModel(emb_age_m, &age_id, 1, &age) || age.size() < kEmbSize ||
      !RunModel(emb_app_m, &app_id, 1, &app) || app.size() < kEmbSize ||
      !RunModel(emb_sex_m, &sex_id, 1, &sex) || sex.size() < kEmbSize) {
    output->application_embedding_failed = true;
    return false;
  }

  std::vector<float> state(kStateSize, 0.0f);
  std::vector<float> state_out(kStateSize, 0.0f);
  std::vector<float> word_embedding;
  auto embed_word = [&](const std::string& word) -> bool {
    std::vector<int> ids;
    if (!CharIds(word, &ids)) return false;
    float chars[kMaxWordLen];
    std::fill(chars, chars + kMaxWordLen, kPadId);
    for (size_t i = 0;
         i < ids.size() && i < static_cast<size_t>(kMaxWordLen); ++i) {
      chars[i] = static_cast<float>(ids[i]);
    }
    if (!RunModel(emb_m, chars, kMaxWordLen, &word_embedding) ||
        word_embedding.size() < kEmbSize) {
      output->application_embedding_failed = true;
      return false;
    }
    for (size_t i = 0; i < kEmbSize; ++i) {
      word_embedding[i] += age[i] + app[i] + sex[i];
    }
    return true;
  };
  auto step_encoder = [&](const std::vector<float>& embedding) -> bool {
    if (enc_m) {
      if (!RunEncoderModel(enc_m, embedding.data(), state.data(),
                           &output->hidden, &state_out)) {
        output->runtime_encoder_failed = true;
        return false;
      }
      output->runtime_encoder_used = true;
      return true;
    }
    encoder_.Step(embedding.data(), state.data(), output->hidden.data(),
                  state_out.data());
    return true;
  };

  for (const std::string& word : context) {
    if (word.empty()) continue;
    if (!embed_word(word)) {
      // Unsupported alphabet input is intentionally skipped, but a failed
      // embedding invocation must fail closed instead of silently changing
      // the context seen by every downstream decoder.
      if (output->application_embedding_failed) return false;
      continue;
    }
    if (!step_encoder(word_embedding)) return false;
    state.swap(state_out);
    ++output->encoded_words;
  }
  output->application_embedding_used = output->encoded_words != 0;
  return true;
#else
  (void)package_name;
  output->application_embedding_failed = true;
  return false;
#endif
}

bool YnnlmWordChain::ProbabilityForWord(
    const YnnlmPredictionBatch& batch, std::string_view word,
    float* out) const {
  if (out == nullptr) return false;
  const auto it = rank_by_word_.find(std::string(word));
  if (it == rank_by_word_.end() || it->second >= batch.all_probabilities.size()) {
    return false;
  }
  const float value = batch.all_probabilities[it->second];
  if (!std::isfinite(value)) return false;
  *out = value;
  return true;
}

bool YnnlmWordChain::RunCharDecoder(
    size_t token, const std::vector<float>& state,
    std::vector<float>* probabilities, std::vector<float>* next_state) const {
  if (!probabilities || !next_state || state.size() != kCharStateSize) {
    return false;
  }
  std::array<float, YnnlmCharDecoder::kVocabularySize> probability_values{};
  std::array<float, YnnlmCharDecoder::kStateSize> state_values{};
  if (!char_decoder_.Step(token, state.data(), state.size(),
                          &probability_values, &state_values)) {
    return false;
  }
  probabilities->assign(probability_values.begin(), probability_values.end());
  next_state->assign(state_values.begin(), state_values.end());
  return true;
}

YnnlmCharScore YnnlmWordChain::ScoreCharacters(
    const std::string& word, const std::vector<std::string>& context,
    const std::string& package_name) const {
  YnnlmCharScore result;
  if (!loaded_ || !char_decoder_loaded_ || word.empty()) return result;

  std::vector<int> word_ids;
  if (!CharIds(word, &word_ids) || word_ids.empty()) return result;

#ifdef MINDSPORE_LITE_AVAILABLE
  std::lock_guard<std::mutex> inference_lock(inference_mutex_);
  OH_AI_ModelHandle bridge_m =
      static_cast<OH_AI_ModelHandle>(enc_dec_model_);
  if (!bridge_m) return result;

  std::vector<float> hidden(kHiddenSize, 0.0f);
  const std::string cache_key = PredictionCacheKey(package_name, context);
  bool context_cache_hit = false;
  {
    std::lock_guard<std::mutex> lock(prediction_cache_mutex_);
    if (prediction_cache_key_ == cache_key &&
        prediction_cache_hidden_.size() == kHiddenSize) {
      hidden = prediction_cache_hidden_;
      context_cache_hit = true;
    }
  }
  if (!context_cache_hit) {
    ContextState context_state;
    if (!BuildContextState(context, package_name, &context_state)) {
      result.model_failed = context_state.runtime_encoder_failed ||
                            context_state.application_embedding_failed;
      return result;
    }
    hidden = std::move(context_state.hidden);
    {
      std::lock_guard<std::mutex> lock(prediction_cache_mutex_);
      prediction_cache_key_ = cache_key;
      prediction_cache_hidden_ = hidden;
      prediction_cache_logits_.clear();
      prediction_cache_runtime_encoder_used_ =
          context_state.runtime_encoder_used;
      prediction_cache_application_embedding_used_ =
          context_state.application_embedding_used;
      prediction_cache_application_id_ = context_state.application_id;
    }
  }

  std::vector<float> state;
  if (!RunModel(bridge_m, hidden.data(), kHiddenSize, &state) ||
      state.size() < kCharStateSize) {
    result.model_failed = true;
    return result;
  }
  state.resize(kCharStateSize);

  std::vector<size_t> expected_ids;
  expected_ids.reserve(word_ids.size() + 1);
  for (const int id : word_ids) {
    expected_ids.push_back(kCharFirstAlphabetId + static_cast<size_t>(id));
  }
  expected_ids.push_back(kCharEndId);

  size_t input_token = kCharStartId;
  double total_log2 = 0.0;
  double eos_log2 = 0.0;
  for (const size_t expected_id : expected_ids) {
    std::vector<float> probabilities;
    std::vector<float> next_state;
    if (!RunCharDecoder(input_token, state, &probabilities, &next_state) ||
        expected_id >= probabilities.size() ||
        next_state.size() < kCharStateSize) {
      result.model_failed = true;
      return result;
    }
    const float probability = probabilities[expected_id];
    if (!std::isfinite(probability)) {
      result.model_failed = true;
      return result;
    }
    const double step_log2 = std::log2(static_cast<double>(
        std::max(probability, std::numeric_limits<float>::min())));
    total_log2 += step_log2;
    if (expected_id == kCharEndId) eos_log2 = step_log2;
    input_token = expected_id;
    state = std::move(next_state);
  }

  result.available = std::isfinite(total_log2) && std::isfinite(eos_log2);
  result.model_failed = !result.available;
  result.context_conditioned = !context.empty();
  result.total_log2_probability = static_cast<float>(total_log2);
  result.eos_log2_probability = static_cast<float>(eos_log2);
#else
  (void)context;
  (void)package_name;
#endif
  return result;
}

YnnlmPredictionBatch YnnlmWordChain::Predict(
    const std::string& prefix, const std::vector<std::string>& context,
    size_t topk, const std::string& package_name) const {
  YnnlmPredictionBatch result;
  if (!loaded_ || topk == 0) return result;
  // Empty prefix = next-word prediction (decode from the context state). With no
  // prefix AND no context there is nothing to condition on.
  if (prefix.empty() && context.empty()) return result;

#ifdef MINDSPORE_LITE_AVAILABLE
  std::vector<int> prefix_ids;
  if (!CharIds(prefix, &prefix_ids)) return result;
  std::lock_guard<std::mutex> inference_lock(inference_mutex_);

  OH_AI_ModelHandle char_bridge_m =
      static_cast<OH_AI_ModelHandle>(enc_dec_model_);
  OH_AI_ModelHandle dec_m = static_cast<OH_AI_ModelHandle>(dec_model_);

  std::vector<float> hidden(kHiddenSize, 0.0f);
  std::vector<float> logits;
  const std::string cache_key = PredictionCacheKey(package_name, context);
  {
    std::lock_guard<std::mutex> lock(prediction_cache_mutex_);
    if (prediction_cache_key_ == cache_key &&
        prediction_cache_hidden_.size() == kHiddenSize &&
        prediction_cache_logits_.size() == rank_words_.size()) {
      hidden = prediction_cache_hidden_;
      logits = prediction_cache_logits_;
      result.runtime_encoder_used = prediction_cache_runtime_encoder_used_;
      result.application_embedding_used =
          prediction_cache_application_embedding_used_;
      result.application_id = prediction_cache_application_id_;
      result.word_decoder_cache_hit = true;
    }
  }

  if (!result.word_decoder_cache_hit) {
    ContextState context_state;
    if (!BuildContextState(context, package_name, &context_state)) {
      result.runtime_encoder_failed = context_state.runtime_encoder_failed;
      result.application_embedding_failed =
          context_state.application_embedding_failed;
      result.application_id = context_state.application_id;
      return result;
    }
    hidden = std::move(context_state.hidden);
    result.runtime_encoder_used = context_state.runtime_encoder_used;
    result.runtime_encoder_failed = context_state.runtime_encoder_failed;
    result.application_embedding_used =
        context_state.application_embedding_used;
    result.application_embedding_failed =
        context_state.application_embedding_failed;
    result.application_id = context_state.application_id;

    if (!RunModel(dec_m, hidden.data(), kHiddenSize, &logits)) return result;
    {
      std::lock_guard<std::mutex> lock(prediction_cache_mutex_);
      prediction_cache_key_ = cache_key;
      prediction_cache_hidden_ = hidden;
      prediction_cache_logits_ = logits;
      prediction_cache_runtime_encoder_used_ = result.runtime_encoder_used;
      prediction_cache_application_embedding_used_ =
          result.application_embedding_used;
      prediction_cache_application_id_ = result.application_id;
    }
  }

  YnnlmPredictionBatch selected = SelectYnnlmPredictionsIndexed(
      prefix, rank_words_, logits, rank_lexicographic_ids_, topk);
  selected.all_probabilities = std::move(logits);
  selected.runtime_encoder_used = result.runtime_encoder_used;
  selected.runtime_encoder_failed = result.runtime_encoder_failed;
  selected.word_decoder_cache_hit = result.word_decoder_cache_hit;
  selected.application_embedding_used = result.application_embedding_used;
  selected.application_embedding_failed = result.application_embedding_failed;
  selected.application_id = result.application_id;

  const bool run_char_decoder =
      ShouldRunYnnlmAuxiliary(selected.candidates.size(), topk);
  if (char_decoder_loaded_ && !run_char_decoder) {
    selected.char_decoder_skipped_dense = true;
  }
  if (char_decoder_loaded_ && run_char_decoder && char_bridge_m) {
    std::vector<float> char_state;
    if (!RunModel(char_bridge_m, hidden.data(), kHiddenSize, &char_state) ||
        char_state.size() < kCharStateSize) {
      selected.char_decoder_failed = true;
      return selected;
    }
    char_state.resize(kCharStateSize);

    std::vector<float> char_logits;
    std::vector<float> next_char_state;
    if (!RunCharDecoder(kCharStartId, char_state, &char_logits,
                        &next_char_state)) {
      selected.char_decoder_failed = true;
      return selected;
    }
    char_state = std::move(next_char_state);
    for (const int id : prefix_ids) {
      const size_t token = kCharFirstAlphabetId + static_cast<size_t>(id);
      if (!RunCharDecoder(token, char_state, &char_logits,
                          &next_char_state)) {
        selected.char_decoder_failed = true;
        return selected;
      }
      char_state = std::move(next_char_state);
    }
    selected.char_decoder_used = true;

    struct CharBeam {
      std::string suffix;
      float score = 0.0f;
      std::vector<float> state;
      std::vector<float> logits;
      size_t steps = 0;
    };
    // m0 is a sparse-prefix fallback, not the primary candidate source. Keep a
    // greedy-sized beam so a miss does not stall the synchronous keyboard path.
    constexpr size_t kCharBeamWidth = 1;
    constexpr size_t kCharExpandTopK = 2;
    const size_t max_steps = prefix_ids.size() >= kMaxWordLen
                                 ? 0
                                 : std::min<size_t>(8, kMaxWordLen -
                                                           prefix_ids.size());
    std::vector<CharBeam> beams;
    beams.push_back(CharBeam{"", 0.0f, char_state, char_logits, 0});
    std::unordered_map<std::string, float> best_by_word;
    bool branch_failed = false;
    for (size_t step = 0; step < max_steps && !beams.empty(); ++step) {
      std::vector<CharBeam> next_beams;
      for (const CharBeam& beam : beams) {
        std::vector<std::pair<float, size_t>> token_top;
        const size_t count =
            std::min(kCharVocabularySize, beam.logits.size());
        for (size_t i = 0; i < count; ++i) {
          if (i == 0 || i == kCharStartId || i == 3 || i == 4 ||
              !std::isfinite(beam.logits[i])) {
            continue;
          }
          token_top.push_back({beam.logits[i], i});
        }
        const size_t token_count = std::min(kCharExpandTopK, token_top.size());
        std::partial_sort(
            token_top.begin(), token_top.begin() + token_count, token_top.end(),
            [](const auto& a, const auto& b) {
              if (a.first != b.first) return a.first > b.first;
              return a.second < b.second;
            });
        for (size_t i = 0; i < token_count; ++i) {
          const size_t token_id = token_top[i].second;
          const float score = beam.score + token_top[i].first;
          if (token_id == kCharEndId) {
            const std::string word = prefix + beam.suffix;
            if (word != prefix && rank_by_word_.find(word) != rank_by_word_.end()) {
              const float normalized_score =
                  score / static_cast<float>(std::max<size_t>(beam.steps + 1, 1));
              auto [it, inserted] = best_by_word.emplace(word, normalized_score);
              if (!inserted && normalized_score > it->second) {
                it->second = normalized_score;
              }
            }
            continue;
          }
          if (token_id < kCharFirstAlphabetId ||
              token_id >= kCharFirstAlphabetId + Alphabet().size()) {
            continue;
          }
          std::vector<float> child_logits;
          std::vector<float> child_state;
          if (!RunCharDecoder(token_id, beam.state, &child_logits,
                              &child_state)) {
            branch_failed = true;
            continue;
          }
          CharBeam child;
          child.suffix = beam.suffix + Alphabet()[token_id -
                                                   kCharFirstAlphabetId];
          child.score = score;
          child.state = std::move(child_state);
          child.logits = std::move(child_logits);
          child.steps = beam.steps + 1;
          next_beams.push_back(std::move(child));
        }
      }
      const size_t beam_count = std::min(kCharBeamWidth, next_beams.size());
      std::partial_sort(
          next_beams.begin(), next_beams.begin() + beam_count, next_beams.end(),
          [](const CharBeam& a, const CharBeam& b) {
            if (a.score != b.score) return a.score > b.score;
            return a.suffix < b.suffix;
          });
      next_beams.resize(beam_count);
      beams = std::move(next_beams);
    }
    selected.char_decoder_failed = branch_failed && best_by_word.empty();
    std::vector<std::pair<float, std::string>> ranked;
    ranked.reserve(best_by_word.size());
    for (const auto& [word, score] : best_by_word) {
      ranked.push_back({score, word});
    }
    const size_t char_count = std::min(topk, ranked.size());
    std::partial_sort(
        ranked.begin(), ranked.begin() + char_count, ranked.end(),
        [](const auto& a, const auto& b) {
          if (a.first != b.first) return a.first > b.first;
          return a.second < b.second;
        });
    selected.char_candidates.reserve(char_count);
    for (size_t i = 0; i < char_count; ++i) {
      selected.char_candidates.push_back({ranked[i].second, ranked[i].first});
    }
  }
  return selected;
#else
  (void)topk;
#endif
  return result;
}

YnnlmPredictionBatch YnnlmWordChain::PredictSubwords(
    const std::string& prefix, const std::vector<std::string>& context,
    size_t topk, const std::string& package_name) const {
  YnnlmPredictionBatch result;
  if (!loaded_ || !subword_decoder_loaded_ || topk == 0 ||
      rank_subwords_.empty()) {
    return result;
  }
  if (prefix.empty() && context.empty()) return result;

#ifdef MINDSPORE_LITE_AVAILABLE
  {
    std::vector<int> pids;
    if (!CharIds(prefix, &pids)) return result;
  }
  std::lock_guard<std::mutex> inference_lock(inference_mutex_);

  OH_AI_ModelHandle bridge_m =
      static_cast<OH_AI_ModelHandle>(subword_enc_dec_model_);
  OH_AI_ModelHandle dec_m = static_cast<OH_AI_ModelHandle>(subword_dec_model_);

  std::vector<float> hidden(kHiddenSize, 0.0f);
  const std::string cache_key = PredictionCacheKey(package_name, context);
  {
    std::lock_guard<std::mutex> lock(prediction_cache_mutex_);
    if (prediction_cache_key_ == cache_key &&
        prediction_cache_hidden_.size() == kHiddenSize) {
      hidden = prediction_cache_hidden_;
      result.runtime_encoder_used = prediction_cache_runtime_encoder_used_;
      result.application_embedding_used =
          prediction_cache_application_embedding_used_;
      result.application_id = prediction_cache_application_id_;
      result.word_decoder_cache_hit = true;
    }
  }

  if (!result.word_decoder_cache_hit) {
    ContextState context_state;
    if (!BuildContextState(context, package_name, &context_state)) {
      result.runtime_encoder_failed = context_state.runtime_encoder_failed;
      result.application_embedding_failed =
          context_state.application_embedding_failed;
      result.application_id = context_state.application_id;
      return result;
    }
    hidden = std::move(context_state.hidden);
    result.runtime_encoder_used = context_state.runtime_encoder_used;
    result.runtime_encoder_failed = context_state.runtime_encoder_failed;
    result.application_embedding_used =
        context_state.application_embedding_used;
    result.application_embedding_failed =
        context_state.application_embedding_failed;
    result.application_id = context_state.application_id;
    {
      std::lock_guard<std::mutex> lock(prediction_cache_mutex_);
      prediction_cache_key_ = cache_key;
      prediction_cache_hidden_ = hidden;
      prediction_cache_logits_.clear();
      prediction_cache_runtime_encoder_used_ = result.runtime_encoder_used;
      prediction_cache_application_embedding_used_ =
          result.application_embedding_used;
      prediction_cache_application_id_ = result.application_id;
    }
  }

  std::vector<float> subword_state;
  if (!RunModel(bridge_m, hidden.data(), kHiddenSize, &subword_state) ||
      subword_state.size() < kSubwordStateSize) {
    return result;
  }
  if (subword_state.size() > kSubwordStateSize) {
    subword_state.resize(kSubwordStateSize);
  }

  constexpr size_t kSubwordBeamWidth = 2;
  constexpr size_t kSubwordExpandTopK = 4;
  constexpr size_t kSubwordMaxSteps = 2;

  struct Beam {
    std::string text;
    float score = 0.0f;
    std::vector<float> state;
    size_t next_token = 0;
    size_t steps = 0;
  };

  std::vector<Beam> beams;
  beams.push_back(Beam{"", 0.0f, subword_state, subword_start_id_, 0});

  std::unordered_map<std::string, float> best_by_word;
  std::vector<float> first_step_logits;
  for (size_t step = 0; step < kSubwordMaxSteps && !beams.empty(); ++step) {
    std::vector<Beam> next_beams;
    for (const Beam& beam : beams) {
      std::vector<float> logits;
      std::vector<float> next_state;
      if (!RunStateDecoder(dec_m, static_cast<float>(beam.next_token),
                           beam.state, kSubwordStateSize,
                           rank_subwords_.size(), &logits, &next_state)) {
        continue;
      }
      if (step == 0 && first_step_logits.empty()) {
        first_step_logits = logits;
      }
      if (next_state.empty()) {
        next_state = beam.state;
      }

      std::vector<std::pair<float, size_t>> token_top;
      const size_t count = std::min(rank_subwords_.size(), logits.size());
      token_top.reserve(count);
      for (size_t i = 0; i < count; ++i) {
        const float logit = logits[i];
        if (!std::isfinite(logit)) continue;
        if (i == subword_pad_id_ || i == subword_start_id_ ||
            i == subword_unk_id_) {
          continue;
        }
        token_top.push_back({logit, i});
      }
      const size_t selected_tokens =
          std::min(kSubwordExpandTopK, token_top.size());
      std::partial_sort(
          token_top.begin(), token_top.begin() + selected_tokens,
          token_top.end(),
          [](const auto& a, const auto& b) {
            if (a.first != b.first) return a.first > b.first;
            return a.second < b.second;
          });

      for (size_t ti = 0; ti < selected_tokens; ++ti) {
        const size_t token_id = token_top[ti].second;
        const std::string& token = rank_subwords_[token_id];
        if (token.empty() || token.front() == '<') {
          continue;
        }
        const bool terminal = token_id == subword_end_id_ ||
                              (!token.empty() && token.back() == '_');
        std::string clean = token;
        if (!clean.empty() && clean.back() == '_') {
          clean.pop_back();
        }
        if (clean.empty() && token_id != subword_end_id_) continue;

        Beam child;
        child.text = beam.text + clean;
        child.score = beam.score + token_top[ti].first;
        child.state = next_state;
        child.next_token = token_id;
        child.steps = beam.steps + 1;

        if (terminal) {
          if (child.text.empty()) continue;
          std::array<std::string, 2> variants{};
          size_t variant_count = 0;
          if (prefix.empty() || StartsWith(child.text, prefix)) {
            variants[variant_count++] = child.text;
          }
          if (!prefix.empty()) {
            variants[variant_count++] = prefix + child.text;
          }

          for (size_t v = 0; v < variant_count; ++v) {
            const std::string& word = variants[v];
            if (word.empty()) continue;
            if (!prefix.empty()) {
              if (word == prefix || !StartsWith(word, prefix)) continue;
            }
            if (rank_by_word_.find(word) == rank_by_word_.end()) continue;
            const float normalized_score =
                child.score / static_cast<float>(std::max<size_t>(child.steps, 1));
            auto [it, inserted] = best_by_word.emplace(word, normalized_score);
            if (!inserted && normalized_score > it->second) {
              it->second = normalized_score;
            }
          }
        } else {
          next_beams.push_back(std::move(child));
        }
      }
    }

    if (next_beams.empty()) break;
    const size_t selected_beams =
        std::min(kSubwordBeamWidth, next_beams.size());
    std::partial_sort(
        next_beams.begin(), next_beams.begin() + selected_beams,
        next_beams.end(),
        [](const Beam& a, const Beam& b) {
          if (a.score != b.score) return a.score > b.score;
          return a.text < b.text;
        });
    next_beams.resize(selected_beams);
    beams = std::move(next_beams);
  }

  if (first_step_logits.empty()) {
    return result;
  }
  result.all_probabilities = std::move(first_step_logits);

  std::vector<std::pair<float, std::string>> ranked;
  ranked.reserve(best_by_word.size());
  for (const auto& [word, score] : best_by_word) {
    ranked.push_back({score, word});
  }
  const size_t selected = std::min(topk, ranked.size());
  std::partial_sort(
      ranked.begin(), ranked.begin() + selected, ranked.end(),
      [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first > b.first;
        return a.second < b.second;
      });
  result.candidates.reserve(selected);
  for (size_t i = 0; i < selected; ++i) {
    result.candidates.push_back({ranked[i].second, ranked[i].first});
  }
  return result;
#else
  (void)context;
  (void)topk;
#endif
  return result;
}

}  // namespace hoskey::engine
