#include "ynnlm_word_chain.h"

#include <algorithm>
#include <array>
#include <cstring>

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
constexpr size_t kBridgeSlice = 500;

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

// Builds the rank-ordered word list (logit index -> word) from the ynnlm bundle.
Status BuildRankWords(const uint8_t* vocab, size_t vocab_size,
                      std::vector<std::string>* rank_words,
                      std::string* diag) {
  YnnlmV1Bundle bundle;
  Status s = bundle.Parse(vocab, vocab_size);
  if (!s.ok()) return s;
  const auto& words = bundle.words();
  const auto& rank_to_lex = bundle.word_rank_to_lex();
  rank_words->assign(rank_to_lex.size(), std::string());
  for (size_t r = 0; r < rank_to_lex.size(); ++r) {
    const uint32_t lex = rank_to_lex[r];
    if (lex < words.size()) (*rank_words)[r] = words[lex];
  }
  *diag = "words=" + std::to_string(rank_words->size()) +
          ",subwords=" + std::to_string(bundle.subword_count());
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

#endif  // MINDSPORE_LITE_AVAILABLE

}  // namespace

YnnlmWordChain::~YnnlmWordChain() { Reset(); }

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
  destroy_model(&enc_dec_model_);
  destroy_model(&dec_model_);
  destroy_ctx(&emb_ctx_);
  destroy_ctx(&enc_dec_ctx_);
  destroy_ctx(&dec_ctx_);
#endif
  loaded_ = false;
  rank_words_.clear();
  diagnostics_ = "not_loaded";
}

Status YnnlmWordChain::Load(const uint8_t* emb_model, size_t emb_size,
                            const uint8_t* enc_dec_model, size_t enc_dec_size,
                            const uint8_t* dec_model, size_t dec_size,
                            const uint8_t* encoder_weights, size_t encoder_size,
                            const uint8_t* vocab_bundle, size_t vocab_size) {
  Reset();

  // Vocabulary + encoder are host-safe (no MindSpore dependency).
  std::string vocab_diag;
  Status vs = BuildRankWords(vocab_bundle, vocab_size, &rank_words_, &vocab_diag);
  if (!vs.ok()) {
    diagnostics_ = "invalid:vocab";
    return vs;
  }
  const rwkv::Status es = encoder_.Load(encoder_weights, encoder_size);
  if (!es.ok) {
    diagnostics_ = "invalid:encoder:" + es.message;
    Reset();
    return Status::ParseError("YnnlmWordChain: encoder load failed: " + es.message);
  }

#ifdef MINDSPORE_LITE_AVAILABLE
  int st_emb = 0, st_bridge = 0, st_dec = 0;
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
  emb_model_ = emb;
  emb_ctx_ = emb_ctx;
  enc_dec_model_ = bridge;
  enc_dec_ctx_ = bridge_ctx;
  dec_model_ = dec;
  dec_ctx_ = dec_ctx;
  loaded_ = true;
  diagnostics_ = "loaded:" + vocab_diag;
  return Status::Ok();
#else
  (void)emb_model;
  (void)emb_size;
  (void)enc_dec_model;
  (void)enc_dec_size;
  (void)dec_model;
  (void)dec_size;
  diagnostics_ = "unsupported:mindspore_lite_unavailable:" + vocab_diag;
  return Status::Ok();
#endif
}

std::vector<std::pair<std::string, float>> YnnlmWordChain::Predict(
    const std::string& prefix, const std::vector<std::string>& context,
    size_t topk) const {
  std::vector<std::pair<std::string, float>> result;
  if (!loaded_ || topk == 0) return result;
  // Empty prefix = next-word prediction (decode from the context state). With no
  // prefix AND no context there is nothing to condition on.
  if (prefix.empty() && context.empty()) return result;

#ifdef MINDSPORE_LITE_AVAILABLE
  {
    std::vector<int> pids;
    if (!CharIds(prefix, &pids)) return result;  // prefix must be alphabet-only
  }

  OH_AI_ModelHandle emb_m = static_cast<OH_AI_ModelHandle>(emb_model_);
  OH_AI_ModelHandle bridge_m = static_cast<OH_AI_ModelHandle>(enc_dec_model_);
  OH_AI_ModelHandle dec_m = static_cast<OH_AI_ModelHandle>(dec_model_);

  // Embed a word into the 525-dim word embedding via the char-CNN (m4).
  auto embed = [&](const std::string& w, std::vector<float>* emb) -> bool {
    std::vector<int> ids;
    if (!CharIds(w, &ids)) return false;
    float ch[kMaxWordLen];
    std::fill(ch, ch + kMaxWordLen, kPadId);
    for (size_t i = 0; i < ids.size() && i < static_cast<size_t>(kMaxWordLen);
         ++i) {
      ch[i] = static_cast<float>(ids[i]);
    }
    return RunModel(emb_m, ch, kMaxWordLen, emb) && emb->size() >= kEmbSize;
  };

  std::vector<float> state(kStateSize, 0.0f);
  std::vector<float> state_out(kStateSize, 0.0f);
  std::vector<float> hidden(kHiddenSize, 0.0f);
  std::vector<float> emb;

  // Step the encoder through the previous words (oldest-first) to build the
  // context state. Words with a non-alphabet char are skipped (state unchanged).
  for (const std::string& cw : context) {
    if (cw.empty() || !embed(cw, &emb)) continue;
    encoder_.Step(emb.data(), state.data(), hidden.data(), state_out.data());
    state.swap(state_out);
  }

  // Current prefix, conditioned on the accumulated context state. For next-word
  // prediction (empty prefix) we skip this step and decode directly from the
  // hidden produced by the last context word.
  if (!prefix.empty()) {
    if (!embed(prefix, &emb)) return result;
    encoder_.Step(emb.data(), state.data(), hidden.data(), state_out.data());
  }

  std::vector<float> bridge;
  if (!RunModel(bridge_m, hidden.data(), kHiddenSize, &bridge) ||
      bridge.size() < kBridgeSlice + kHiddenSize) {
    return result;
  }

  std::vector<float> logits;
  if (!RunModel(dec_m, bridge.data() + kBridgeSlice, kHiddenSize, &logits)) {
    return result;
  }

  std::vector<std::pair<float, int>> cand;
  const size_t n = std::min(rank_words_.size(), logits.size());
  for (size_t i = 0; i < n; ++i) {
    if (!rank_words_[i].empty() && StartsWith(rank_words_[i], prefix)) {
      cand.push_back({logits[i], static_cast<int>(i)});
    }
  }
  const size_t k = std::min(topk, cand.size());
  std::partial_sort(
      cand.begin(), cand.begin() + k, cand.end(),
      [](const std::pair<float, int>& a, const std::pair<float, int>& b) {
        return a.first > b.first;
      });
  result.reserve(k);
  for (size_t i = 0; i < k; ++i) {
    result.push_back({rank_words_[cand[i].second], cand[i].first});
  }
#else
  (void)topk;
#endif
  return result;
}

}  // namespace hoskey::engine
