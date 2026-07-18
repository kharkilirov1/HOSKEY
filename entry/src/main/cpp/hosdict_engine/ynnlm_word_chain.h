#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "rwkv_encoder.h"
#include "status.h"
#include "ynnlm_char_decoder.h"

namespace hoskey::engine {

enum class YnnlmModelContract {
  kWordEmbedding,
  kMetadataEmbedding,
  kWordEncoder,
  kCharBridge,
  kWordDecoder,
  kSubwordBridge,
  kSubwordDecoder,
};

struct YnnlmPrediction {
  std::string word;
  float raw_logit = 0.0f;
};

struct YnnlmPredictionBatch {
  std::vector<YnnlmPrediction> candidates;
  std::vector<YnnlmPrediction> char_candidates;
  bool has_original_logit = false;
  float original_raw_logit = 0.0f;
  std::vector<float> all_probabilities;
  bool runtime_encoder_used = false;
  bool runtime_encoder_failed = false;
  bool word_decoder_cache_hit = false;
  bool application_embedding_used = false;
  bool application_embedding_failed = false;
  uint32_t application_id = 0;
  bool char_decoder_used = false;
  bool char_decoder_failed = false;
  bool char_decoder_skipped_dense = false;
};

// Exact TNeuralCharLM score used by the recovered Autocc features 73..78.
// The total includes the fixed end-of-word symbol; eos_log2_probability is the
// final conditional P(EOS | word) term.  `available` is false for unsupported
// alphabet input or when the runtime chain is not loaded.
struct YnnlmCharScore {
  bool available = false;
  bool model_failed = false;
  bool context_conditioned = false;
  float total_log2_probability = 0.0f;
  float eos_log2_probability = 0.0f;
};

// Materializes the six recovered KeyboardSpecific neural-char coordinates:
// total(original), -1/len(original), EOS(original), then the same triplet for
// correction.  Kept pure so the producer contract is host-testable without
// MindSpore Lite.
bool BuildYnnlmAutoccCharFeatures(
    const YnnlmCharScore& original, size_t original_length,
    const YnnlmCharScore& correction, size_t correction_length,
    std::array<float, 6>* output);

// Pure selection step over a decoder output. It retains the exact prefix logit
// independently of candidate top-k so ranking features need no second model
// invocation.
YnnlmPredictionBatch SelectYnnlmPredictions(
    std::string_view prefix, const std::vector<std::string>& rank_words,
    const std::vector<float>& logits, size_t topk);

// Prefix lookup index used by the live synchronous IME path. The decoder
// distribution is prefix-independent, so sorting word ids once avoids scanning
// all 107,989 strings again after every typed character.
std::vector<size_t> BuildYnnlmLexicographicIndex(
    const std::vector<std::string>& rank_words);
YnnlmPredictionBatch SelectYnnlmPredictionsIndexed(
    std::string_view prefix, const std::vector<std::string>& rank_words,
    const std::vector<float>& logits,
    const std::vector<size_t>& lexicographic_ids, size_t topk);

bool LookupYnnlmProbability(
    std::string_view word, const std::vector<std::string>& rank_words,
    const std::vector<float>& probabilities, float* out);

// Auxiliary autoregressive decoders are substantially more expensive than the
// full-vocabulary word decoder. Run them only when the primary decoder did not
// already fill a small useful candidate pool.
bool ShouldRunYnnlmAuxiliary(size_t candidate_count, size_t requested_topk);

// Neural word-suggestion chain — a runnable composition of the
// reverse-engineered Yandex word language model:
//
//   age/app/sex ids -> m3/m4/m5 --+
//   previous words -> emb (m6) ----+-> element-wise sum -> encoder (m9)
//                                      -> hidden (500)
//                                      +-> word decoder (m2)
//                                      +-> char bridge (m8)
//                                          -> exact native char decoder (m0)
//
// Device builds run the converted m7 encoder through OH_AI. RwkvEncoder remains
// the host/runtime-absence fallback only; it is not used after m7 was selected
// successfully. On a host build without MindSpore Lite this chain is inert.
class YnnlmWordChain {
 public:
  YnnlmWordChain() = default;
  ~YnnlmWordChain();
  YnnlmWordChain(const YnnlmWordChain&) = delete;
  YnnlmWordChain& operator=(const YnnlmWordChain&) = delete;

  // Loads from in-memory asset bytes (borrowed for the duration of the call —
  // the encoder copies its weights, the models are built into OH_AI handles and
  // the vocabulary is parsed into owned strings).
  Status Load(const uint8_t* emb_model, size_t emb_size,
              const uint8_t* enc_dec_model, size_t enc_dec_size,
              const uint8_t* dec_model, size_t dec_size,
              const uint8_t* encoder_weights, size_t encoder_size,
              const uint8_t* vocab_bundle, size_t vocab_size,
              const uint8_t* emb_age_model = nullptr, size_t emb_age_size = 0,
              const uint8_t* emb_app_model = nullptr, size_t emb_app_size = 0,
              const uint8_t* emb_sex_model = nullptr, size_t emb_sex_size = 0,
              const uint8_t* enc_model = nullptr, size_t enc_size = 0,
              const uint8_t* subword_enc_dec_model = nullptr,
              size_t subword_enc_dec_size = 0,
              const uint8_t* subword_dec_model = nullptr,
              size_t subword_dec_size = 0,
              const uint8_t* char_dec_model = nullptr,
              size_t char_dec_size = 0);

  // Serialized and runtime contract probes used by production loading and by
  // the on-device negative matrix. ProbeModelContract builds only the supplied
  // model and destroys it before returning.
  static Status ValidateSerializedModel(const uint8_t* data, size_t size,
                                        std::string_view role);
  static Status ProbeModelContract(const uint8_t* data, size_t size,
                                   YnnlmModelContract contract,
                                   size_t vocabulary_size = 0);

  void Reset();

  [[nodiscard]] bool is_loaded() const { return loaded_; }
  [[nodiscard]] bool has_runtime_encoder() const { return runtime_encoder_loaded_; }
  [[nodiscard]] bool has_subword_decoder() const { return subword_decoder_loaded_; }
  [[nodiscard]] bool has_char_decoder() const { return char_decoder_loaded_; }
  [[nodiscard]] const std::string& diagnostics() const { return diagnostics_; }

  // Prefix-filtered top-k neural word suggestions (highest logit first). The
  // encoder is stepped through `context` (oldest-first complete previous
  // words), with age/app/sex embeddings added to each word embedding. The
  // current prefix only filters the word decoder and teacher-forces the
  // character decoder; it is not encoded as a complete word. A context word
  // with a non-alphabet character is skipped.
  [[nodiscard]] YnnlmPredictionBatch Predict(
      const std::string& prefix, const std::vector<std::string>& context,
      size_t topk, const std::string& package_name = {}) const;

  // One-step subword decoder suggestions. The decoder is initialized from the
  // same word-encoder hidden state as Predict(), then fed <s>. Returned entries
  // are terminal subword candidates materialized as full vocabulary words.
  [[nodiscard]] YnnlmPredictionBatch PredictSubwords(
      const std::string& prefix, const std::vector<std::string>& context,
      size_t topk, const std::string& package_name = {}) const;

  [[nodiscard]] bool ProbabilityForWord(
      const YnnlmPredictionBatch& batch, std::string_view word,
      float* out) const;

  // Scores `{word}` with the recovered start/EOS protocol.  Empty context is
  // exact: the char bridge receives 500 zeros and metadata embeddings are not
  // emitted as standalone encoder tokens.  Non-empty context uses the same
  // metadata-summed encoder state as Predict().
  [[nodiscard]] YnnlmCharScore ScoreCharacters(
      const std::string& word, const std::vector<std::string>& context,
      const std::string& package_name = {}) const;

 private:
  struct ContextState {
    std::vector<float> hidden;
    bool runtime_encoder_used = false;
    bool runtime_encoder_failed = false;
    bool application_embedding_used = false;
    // Covers demographic/application and per-word embedding model failures.
    bool application_embedding_failed = false;
    uint32_t application_id = 0;
    size_t encoded_words = 0;
  };

  [[nodiscard]] bool BuildContextState(
      const std::vector<std::string>& context,
      const std::string& package_name, ContextState* output) const;
  [[nodiscard]] bool RunCharDecoder(
      size_t token, const std::vector<float>& state,
      std::vector<float>* probabilities, std::vector<float>* next_state) const;

  bool loaded_ = false;
  bool fallback_encoder_loaded_ = false;
  bool runtime_encoder_loaded_ = false;
  bool subword_decoder_loaded_ = false;
  bool char_decoder_loaded_ = false;
  std::string diagnostics_ = "not_loaded";
  rwkv::RwkvEncoder encoder_;
  YnnlmCharDecoder char_decoder_;
  std::vector<std::string> rank_words_;  // logit index -> word
  std::vector<std::string> rank_subwords_;  // logit index -> subword
  std::vector<size_t> rank_lexicographic_ids_;  // word-sorted logit indices
  std::unordered_map<std::string, size_t> rank_by_word_;
  std::unordered_map<std::string, size_t> rank_by_subword_;
  std::unordered_map<std::string, uint32_t> application_id_by_package_;
  size_t subword_start_id_ = 1;
  size_t subword_end_id_ = 2;
  size_t subword_pad_id_ = 0;
  size_t subword_unk_id_ = 3;

  // The word distribution depends on app + complete previous words, not on the
  // current prefix. Cache one active typing context so successive keystrokes do
  // not rerun emb_app, encoder and the 107,989-way decoder.
  mutable std::mutex prediction_cache_mutex_;
  mutable std::string prediction_cache_key_;
  mutable std::vector<float> prediction_cache_hidden_;
  mutable std::vector<float> prediction_cache_logits_;
  mutable bool prediction_cache_runtime_encoder_used_ = false;
  mutable bool prediction_cache_application_embedding_used_ = false;
  mutable uint32_t prediction_cache_application_id_ = 0;
  mutable std::mutex inference_mutex_;

  // One MindSpore Lite context PER model: OH_AI_ModelBuild(from buffer) consumes
  // the context, so a shared context lets only the first model build.
  void* emb_ctx_ = nullptr;
  void* emb_age_ctx_ = nullptr;
  void* emb_app_ctx_ = nullptr;
  void* emb_sex_ctx_ = nullptr;
  void* enc_ctx_ = nullptr;
  void* enc_dec_ctx_ = nullptr;
  void* subword_enc_dec_ctx_ = nullptr;
  void* subword_dec_ctx_ = nullptr;
  void* dec_ctx_ = nullptr;
  void* emb_model_ = nullptr;
  void* emb_age_model_ = nullptr;
  void* emb_app_model_ = nullptr;
  void* emb_sex_model_ = nullptr;
  void* enc_model_ = nullptr;
  void* enc_dec_model_ = nullptr;
  void* subword_enc_dec_model_ = nullptr;
  void* subword_dec_model_ = nullptr;
  void* dec_model_ = nullptr;
};

}  // namespace hoskey::engine
