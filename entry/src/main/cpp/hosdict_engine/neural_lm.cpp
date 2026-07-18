#include "neural_lm.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <queue>
#include <string_view>

// MindSpore Lite headers — available in HarmonyOS NDK.
// Build-gated: only compiled when MINDSPORE_LITE_AVAILABLE is defined.
#ifdef MINDSPORE_LITE_AVAILABLE
#include "mindspore/context.h"
#include "mindspore/model.h"
#include "mindspore/status.h"
#include "mindspore/tensor.h"
#endif

namespace hoskey::engine {

namespace {

bool IsUtf8Continuation(uint8_t byte) {
  return (byte & 0xC0u) == 0x80u;
}

bool DecodeOneUtf8Strict(std::string_view text, size_t* pos, char32_t* out) {
  if (!pos || !out || *pos >= text.size()) return false;

  const auto byte_at = [&](size_t idx) {
    return static_cast<uint8_t>(text[idx]);
  };

  const size_t i = *pos;
  const uint8_t b0 = byte_at(i);
  char32_t cp = 0;
  size_t len = 0;

  if (b0 <= 0x7Fu) {
    cp = b0;
    len = 1;
  } else if (b0 >= 0xC2u && b0 <= 0xDFu) {
    if (i + 1 >= text.size()) return false;
    const uint8_t b1 = byte_at(i + 1);
    if (!IsUtf8Continuation(b1)) return false;
    cp = ((b0 & 0x1Fu) << 6) | (b1 & 0x3Fu);
    len = 2;
  } else if (b0 >= 0xE0u && b0 <= 0xEFu) {
    if (i + 2 >= text.size()) return false;
    const uint8_t b1 = byte_at(i + 1);
    const uint8_t b2 = byte_at(i + 2);
    if (!IsUtf8Continuation(b1) || !IsUtf8Continuation(b2)) return false;
    if (b0 == 0xE0u && b1 < 0xA0u) return false;  // overlong
    if (b0 == 0xEDu && b1 >= 0xA0u) return false; // surrogate
    cp = ((b0 & 0x0Fu) << 12) | ((b1 & 0x3Fu) << 6) | (b2 & 0x3Fu);
    len = 3;
  } else if (b0 >= 0xF0u && b0 <= 0xF4u) {
    if (i + 3 >= text.size()) return false;
    const uint8_t b1 = byte_at(i + 1);
    const uint8_t b2 = byte_at(i + 2);
    const uint8_t b3 = byte_at(i + 3);
    if (!IsUtf8Continuation(b1) || !IsUtf8Continuation(b2) ||
        !IsUtf8Continuation(b3)) {
      return false;
    }
    if (b0 == 0xF0u && b1 < 0x90u) return false;  // overlong
    if (b0 == 0xF4u && b1 > 0x8Fu) return false;  // > U+10FFFF
    cp = ((b0 & 0x07u) << 18) | ((b1 & 0x3Fu) << 12) |
         ((b2 & 0x3Fu) << 6) | (b3 & 0x3Fu);
    len = 4;
  } else {
    return false;
  }

  *pos += len;
  *out = cp;
  return true;
}

bool IsAllowedRussianVocabCodepoint(char32_t cp) {
  return (cp >= U'\u0430' && cp <= U'\u044f') || cp == U'\u0451' ||
         cp == U' ' || cp == U'-';
}

bool IsAcceptedRussianVocabText(std::string_view text, size_t* cp_count = nullptr) {
  if (text.empty()) return false;

  size_t pos = 0;
  size_t count = 0;
  bool has_russian_letter = false;
  bool previous_separator = false;
  while (pos < text.size()) {
    char32_t cp = 0;
    if (!DecodeOneUtf8Strict(text, &pos, &cp)) return false;
    if (!IsAllowedRussianVocabCodepoint(cp)) return false;

    const bool separator = cp == U' ' || cp == U'-';
    if (separator) {
      // Avoid exposing model fragments like "-то" or malformed separator runs.
      if (count == 0 || previous_separator) return false;
      previous_separator = true;
    } else {
      has_russian_letter = true;
      previous_separator = false;
    }
    ++count;
  }

  if (previous_separator || !has_russian_letter) return false;
  if (cp_count) *cp_count = count;
  return true;
}

size_t Utf8CodepointCount(std::string_view text) {
  size_t pos = 0;
  size_t count = 0;
  while (pos < text.size()) {
    char32_t cp = 0;
    if (!DecodeOneUtf8Strict(text, &pos, &cp)) return count;
    ++count;
  }
  return count;
}

bool StartsWith(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() &&
         std::memcmp(text.data(), prefix.data(), prefix.size()) == 0;
}

void AppendRawVocabDiagnostics(std::string* diagnostics, size_t vocab_size,
                               size_t subword_size = 0) {
  if (!diagnostics) return;
  if (diagnostics->empty() || *diagnostics == "not_loaded") {
    *diagnostics = "vocab_only";
  }
  *diagnostics += ",raw_vocab=" + std::to_string(vocab_size);
  *diagnostics += ",subwords=" + std::to_string(subword_size);
}

#ifdef MINDSPORE_LITE_AVAILABLE
OH_AI_ContextHandle CreateCpuInferenceContext() {
  OH_AI_ContextHandle ctx = OH_AI_ContextCreate();
  if (!ctx) return nullptr;

  OH_AI_ContextSetThreadNum(ctx, 2);
  OH_AI_ContextSetEnableParallel(ctx, false);

  OH_AI_DeviceInfoHandle cpu = OH_AI_DeviceInfoCreate(OH_AI_DEVICETYPE_CPU);
  if (cpu) {
    OH_AI_DeviceInfoSetEnableFP16(cpu, false);
    OH_AI_ContextAddDeviceInfo(ctx, cpu);
    // DeviceInfo lifetime is owned by the context after AddDeviceInfo().
  }
  return ctx;
}

size_t LastShapeDim(OH_AI_TensorHandle tensor, size_t fallback) {
  size_t shape_num = 0;
  const int64_t* shape = OH_AI_TensorGetShape(tensor, &shape_num);
  if (!shape || shape_num == 0) return fallback;
  const int64_t dim = shape[shape_num - 1];
  return dim > 0 ? static_cast<size_t>(dim) : fallback;
}

// Compact IO topology for diagnostics, e.g.
// "io{in:dec/input_h:1x128/t43;out:...:1x107986/t43}".
std::string DescribeModelIo(OH_AI_ModelHandle model) {
  auto describe = [](OH_AI_TensorHandle t) {
    std::string d;
    const char* name = OH_AI_TensorGetName(t);
    d += name ? name : "?";
    d += ":";
    size_t shape_num = 0;
    const int64_t* shape = OH_AI_TensorGetShape(t, &shape_num);
    for (size_t i = 0; i < shape_num; ++i) {
      if (i) d += "x";
      d += std::to_string(shape[i]);
    }
    d += "/t" + std::to_string(static_cast<int>(OH_AI_TensorGetDataType(t)));
    return d;
  };
  std::string s = "io{in:";
  OH_AI_TensorHandleArray ins = OH_AI_ModelGetInputs(model);
  for (size_t i = 0; i < ins.handle_num && ins.handle_list; ++i) {
    if (i) s += ",";
    s += describe(ins.handle_list[i]);
  }
  s += ";out:";
  OH_AI_TensorHandleArray outs = OH_AI_ModelGetOutputs(model);
  for (size_t i = 0; i < outs.handle_num && outs.handle_list; ++i) {
    if (i) s += ",";
    s += describe(outs.handle_list[i]);
  }
  s += "}";
  return s;
}

#endif

}  // namespace

NeuralLM::~NeuralLM() {
  Reset();
}

void NeuralLM::Reset() {
#ifdef MINDSPORE_LITE_AVAILABLE
  if (model_) {
    OH_AI_ModelHandle model = static_cast<OH_AI_ModelHandle>(model_);
    OH_AI_ModelDestroy(&model);
    model_ = nullptr;
  }
  if (context_) {
    OH_AI_ContextHandle context = static_cast<OH_AI_ContextHandle>(context_);
    OH_AI_ContextDestroy(&context);
    context_ = nullptr;
  }
#endif
  loaded_ = false;
  diagnostics_ = "not_loaded";
  vocabulary_loaded_ = false;
  vocabulary_bundle_ = YnnlmV1Bundle();
  vocabulary_.clear();
  vocabulary_rank_.clear();
}

Status NeuralLM::Load(const uint8_t* data, size_t size) {
  Reset();
  if (!data || size == 0) {
    diagnostics_ = "invalid:empty_model";
    return Status::InvalidArgument("NeuralLM: null or empty model data");
  }

  if (size < 8 || std::memcmp(data + 4, "MSL2", 4) != 0) {
    diagnostics_ = "invalid:no_msl2_header";
    return Status::ParseError("NeuralLM: not a MindSpore Lite model (no MSL2 header)");
  }

#ifdef MINDSPORE_LITE_AVAILABLE
  OH_AI_ContextHandle ctx = CreateCpuInferenceContext();
  if (!ctx) {
    loaded_ = false;
    diagnostics_ = "unsupported:context_create_failed";
    return Status::Ok();
  }

  OH_AI_ModelHandle model = OH_AI_ModelCreate();
  if (!model) {
    OH_AI_ContextDestroy(&ctx);
    loaded_ = false;
    diagnostics_ = "unsupported:model_create_failed";
    return Status::Ok();
  }

  const OH_AI_Status ret =
      OH_AI_ModelBuild(model, data, size, OH_AI_MODELTYPE_MINDIR, ctx);
  if (ret != OH_AI_STATUS_SUCCESS) {
    OH_AI_ModelDestroy(&model);
    OH_AI_ContextDestroy(&ctx);
    loaded_ = false;
    diagnostics_ = "unsupported:model_build_failed";
    return Status::Ok();
  }

  OH_AI_TensorHandleArray inputs = OH_AI_ModelGetInputs(model);
  OH_AI_TensorHandleArray outputs = OH_AI_ModelGetOutputs(model);
  if (inputs.handle_num < 2 || !inputs.handle_list ||
      outputs.handle_num < 3 || !outputs.handle_list) {
    diagnostics_ = "unsupported:io_count," + DescribeModelIo(model);
    OH_AI_ModelDestroy(&model);
    OH_AI_ContextDestroy(&ctx);
    loaded_ = false;
    return Status::Ok();
  }

  model_ = model;
  context_ = ctx;
  loaded_ = true;
  
  // Extract tensor shapes to dynamically set state/vocab size if needed
  if (inputs.handle_num >= 2 && inputs.handle_list) {
    state_size_ = LastShapeDim(inputs.handle_list[1], state_size_);
  }
  if (outputs.handle_num > 0 && outputs.handle_list) {
    vocab_size_ = LastShapeDim(outputs.handle_list[0], vocab_size_);
  }
  diagnostics_ = "loaded:state=" + std::to_string(state_size_) +
                 ",vocab=" + std::to_string(vocab_size_);
  
  return Status::Ok();
#else
  // Fallback if MSL is not linked
  loaded_ = false;
  diagnostics_ = "unsupported:mindspore_lite_unavailable";
  return Status::Ok();
#endif
}

Status NeuralLM::LoadVocabulary(const uint8_t* data, size_t size) {
  vocabulary_loaded_ = false;
  vocabulary_bundle_ = YnnlmV1Bundle();
  vocabulary_.clear();
  vocabulary_rank_.clear();

  if (!data || size == 0) {
    AppendRawVocabDiagnostics(&diagnostics_, 0);
    return Status::InvalidArgument("NeuralLM: null or empty raw vocabulary data");
  }

  Status st = vocabulary_bundle_.Parse(data, size);
  if (!st.ok()) {
    AppendRawVocabDiagnostics(&diagnostics_, 0);
    return st;
  }

  const auto& words = vocabulary_bundle_.words();
  vocabulary_.reserve(words.size());
  vocabulary_rank_.reserve(words.size());
  for (size_t lex = 0; lex < words.size(); ++lex) {
    const std::string& word = words[lex];
    if (!IsAcceptedRussianVocabText(word)) continue;
    const int rank = vocabulary_bundle_.WordRank(word);
    if (rank < 0) continue;
    vocabulary_.push_back(word);
    vocabulary_rank_.emplace(word, static_cast<uint32_t>(rank));
  }

  vocabulary_loaded_ = !vocabulary_.empty();
  AppendRawVocabDiagnostics(&diagnostics_, vocabulary_bundle_.word_count(),
                            vocabulary_bundle_.subword_count());
  return vocabulary_loaded_
      ? Status::Ok()
      : Status::ParseError("NeuralLM: no Russian vocabulary entries found");
}

bool NeuralLM::VocabularyContains(std::string_view word) const {
  if (!vocabulary_loaded_ || word.empty()) return false;
  return vocabulary_rank_.find(std::string(word)) != vocabulary_rank_.end();
}

int32_t NeuralLM::VocabularyWeight(std::string_view word) const {
  if (!vocabulary_loaded_ || word.empty()) return 0;
  const auto it = vocabulary_rank_.find(std::string(word));
  if (it == vocabulary_rank_.end()) return 0;

  // The raw bundle rank is not the final Yandex LM probability; real scoring
  // comes from NNLM inference + CatBoost.  Keep this bounded and conservative
  // so it acts as an exact/vocab signal, not as a fake neural score.
  const uint32_t rank = it->second;
  const uint32_t decay = std::min<uint32_t>(rank / 128U, 900U);
  return static_cast<int32_t>(std::max<uint32_t>(100U, 1100U - decay));
}

int32_t NeuralLM::VocabularyRank(std::string_view word) const {
  if (!vocabulary_loaded_ || word.empty()) return -1;
  const auto it = vocabulary_rank_.find(std::string(word));
  if (it == vocabulary_rank_.end()) return -1;
  return static_cast<int32_t>(it->second);
}

std::vector<NeuralVocabCandidate> NeuralLM::VocabularyCompletions(
    std::string_view prefix, size_t max_results,
    bool include_short_suffix) const {
  std::vector<NeuralVocabCandidate> results;
  if (!vocabulary_loaded_ || prefix.empty() || max_results == 0) {
    return results;
  }

  const size_t prefix_cp = Utf8CodepointCount(prefix);
  if (prefix_cp == 0) return results;

  struct ScoredCandidate {
    std::string_view text;
    int32_t weight = 0;
    size_t cp_count = 0;
    size_t suffix_cp = 0;
  };

  std::vector<ScoredCandidate> scored;
  scored.reserve(std::min<size_t>(vocabulary_.size(), max_results * 16U + 64U));

  for (const std::string& candidate : vocabulary_) {
    if (!StartsWith(candidate, prefix) || candidate.size() == prefix.size()) {
      continue;
    }

    const size_t candidate_cp = Utf8CodepointCount(candidate);
    if (candidate_cp <= prefix_cp) continue;
    const size_t suffix_cp = candidate_cp - prefix_cp;
    if (suffix_cp > 24) continue;

    // Raw ynnlm_v1 vocabulary is a word/subword vocabulary.  For UI
    // completions, suppress very short suffix fragments; exact token signal is
    // still available via VocabularyContains().
    if (!include_short_suffix) {
      if (prefix_cp >= 2 && suffix_cp < 2) continue;
      if (prefix_cp >= 3 && suffix_cp <= 2 &&
          VocabularyWeight(candidate) < 1050) {
        continue;
      }
    }

    const int32_t weight = VocabularyWeight(candidate);

    scored.push_back({candidate, weight, candidate_cp, suffix_cp});
  }

  std::stable_sort(scored.begin(), scored.end(),
                   [](const ScoredCandidate& a, const ScoredCandidate& b) {
                     if (a.weight != b.weight) return a.weight > b.weight;
                     if (a.cp_count != b.cp_count) return a.cp_count < b.cp_count;
                     return a.text < b.text;
                   });

  const size_t limit = std::min(max_results, scored.size());
  results.reserve(limit);
  for (size_t i = 0; i < limit; ++i) {
    results.push_back({std::string(scored[i].text), scored[i].weight});
  }
  return results;
}

bool NeuralLM::PredictNextTokens(
    uint32_t current_token,
    const std::vector<float>& current_state,
    size_t top_k,
    std::vector<std::pair<uint32_t, float>>& out_top_tokens,
    std::vector<float>& out_next_state) const {
    
#ifdef MINDSPORE_LITE_AVAILABLE
  if (!loaded_ || !model_) return false;

  OH_AI_ModelHandle model = static_cast<OH_AI_ModelHandle>(model_);
  OH_AI_TensorHandleArray inputs = OH_AI_ModelGetInputs(model);
  if (inputs.handle_num < 2 || !inputs.handle_list) return false;

  // Input 0: Token ID (shape [1x1], float32 in original Yandex model)
  float token_f = static_cast<float>(current_token);
  void* token_data = OH_AI_TensorGetMutableData(inputs.handle_list[0]);
  if (!token_data || OH_AI_TensorGetDataSize(inputs.handle_list[0]) < sizeof(float)) {
    return false;
  }
  std::memcpy(token_data, &token_f, sizeof(float));

  // Input 1: Hidden state (shape [1x2500])
  void* state_data = OH_AI_TensorGetMutableData(inputs.handle_list[1]);
  const size_t state_bytes = OH_AI_TensorGetDataSize(inputs.handle_list[1]);
  if (!state_data || state_bytes < state_size_ * sizeof(float)) {
    return false;
  }
  if (current_state.size() == state_size_) {
    std::memcpy(state_data, current_state.data(), state_size_ * sizeof(float));
  } else {
    std::memset(state_data, 0, state_size_ * sizeof(float));
  }

  OH_AI_TensorHandleArray outputs = OH_AI_ModelGetOutputs(model);
  const OH_AI_Status status =
      OH_AI_ModelPredict(model, inputs, &outputs, nullptr, nullptr);
  if (status != OH_AI_STATUS_SUCCESS || outputs.handle_num == 0 ||
      !outputs.handle_list) {
    return false;
  }

  // Output 0: logits [1x1x7000]
  OH_AI_TensorHandle logits_tensor = outputs.handle_list[0];
  const float* logits =
      static_cast<const float*>(OH_AI_TensorGetData(logits_tensor));
  const size_t logits_count =
      OH_AI_TensorGetDataSize(logits_tensor) / sizeof(float);
  if (!logits || logits_count == 0) return false;
  const size_t vocab_limit = std::min(vocab_size_, logits_count);
  top_k = std::min(top_k, vocab_limit);
  if (top_k == 0) return false;
  
  // Apply Softmax and get Top-K
  // For numerical stability:
  // Seed from -infinity (not a finite sentinel) and skip NaN so max_logit is
  // the true maximum even for pathological logits.
  float max_logit = -INFINITY;
  for (size_t i = 0; i < vocab_limit; ++i) {
    if (std::isfinite(logits[i]) && logits[i] > max_logit) max_logit = logits[i];
  }
  if (!std::isfinite(max_logit)) return false;  // no usable logits
  
  float sum_exp = 0.0f;
  std::vector<float> probs(vocab_limit);
  for (size_t i = 0; i < vocab_limit; ++i) {
    probs[i] = std::isfinite(logits[i]) ? std::exp(logits[i] - max_logit) : 0.0f;
    sum_exp += probs[i];
  }
  if (!(sum_exp > 0.0f)) return false;  // degenerate distribution, avoid divide-by-zero
  
  std::vector<std::pair<uint32_t, float>> token_probs;
  token_probs.reserve(vocab_limit);
  for (size_t i = 0; i < vocab_limit; ++i) {
    token_probs.push_back({static_cast<uint32_t>(i), std::log(probs[i] / sum_exp)});
  }
  
  std::partial_sort(token_probs.begin(), token_probs.begin() + top_k, token_probs.end(),
      [](const std::pair<uint32_t, float>& a, const std::pair<uint32_t, float>& b) {
        return a.second > b.second;
      });
      
  out_top_tokens.assign(token_probs.begin(), token_probs.begin() + top_k);

  // Output 2: Next hidden state [1x2500] (based on model analysis)
  const size_t state_output_index = outputs.handle_num >= 3 ? 2 : 1;
  if (outputs.handle_num <= state_output_index) {
    return false;
  }
  OH_AI_TensorHandle next_state_tensor = outputs.handle_list[state_output_index];
  const float* next_state_data =
      static_cast<const float*>(OH_AI_TensorGetData(next_state_tensor));
  const size_t next_state_count =
      OH_AI_TensorGetDataSize(next_state_tensor) / sizeof(float);
  if (!next_state_data || next_state_count < state_size_) {
    return false;
  }
  out_next_state.assign(next_state_data, next_state_data + state_size_);

  return true;
#else
  return false;
#endif
}

std::vector<std::string> NeuralLM::GenerateCompletions(
    const SuccinctTrie& trie,
    const std::vector<uint32_t>& context_tokens,
    const std::string& prefix,
    size_t beam_width,
    size_t max_len) const {
    
  if (!loaded_) return {};

  std::vector<std::string> results;

  // Initial state is zeros
  std::vector<float> state(state_size_, 0.0f);
  
  // Process context tokens to warm up the hidden state
  uint32_t last_token = 0; // typically <S> or space token
  for (uint32_t token : context_tokens) {
    std::vector<std::pair<uint32_t, float>> top_tokens;
    std::vector<float> next_state;
    if (PredictNextTokens(token, state, 1, top_tokens, next_state)) {
      state = std::move(next_state);
      last_token = token;
    }
  }

  // Initialize Beam Search
  std::vector<BeamNode> beams;
  beams.push_back({{}, prefix, 0.0f, state});

  for (size_t step = 0; step < max_len; ++step) {
    std::vector<BeamNode> next_beams;
    
    for (const auto& beam : beams) {
      uint32_t input_token = beam.tokens.empty() ? last_token : beam.tokens.back();
      
      std::vector<std::pair<uint32_t, float>> top_tokens;
      std::vector<float> next_state;
      
      if (!PredictNextTokens(input_token, beam.hidden_state, beam_width, top_tokens, next_state)) {
        continue;
      }
      
      for (const auto& [token_id, log_prob] : top_tokens) {
        BeamNode new_beam = beam;
        new_beam.tokens.push_back(token_id);
        new_beam.log_prob += log_prob;
        new_beam.hidden_state = next_state;
        
        // Map the vocab ID back to a string fragment using the SuccinctTrie.
        // In the Yandex model, the vocab index *is* the node ID of the 1nc7 trie.
        new_beam.text += trie.GetTokenString(token_id);
        
        next_beams.push_back(std::move(new_beam));
      }
    }
    
    // Sort and prune
    std::sort(next_beams.begin(), next_beams.end(), 
        [](const BeamNode& a, const BeamNode& b) {
          return a.log_prob > b.log_prob;
        });
        
    if (next_beams.size() > beam_width) {
      next_beams.resize(beam_width);
    }
    
    beams = std::move(next_beams);
  }
  
  for (const auto& beam : beams) {
    // Only add if we actually generated new tokens
    if (!beam.tokens.empty()) {
      results.push_back(beam.text);
    }
  }

  return results;
}

}  // namespace hoskey::engine
