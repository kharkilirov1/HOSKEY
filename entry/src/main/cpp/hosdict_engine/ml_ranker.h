#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "status.h"

namespace hoskey::engine {

struct NgramCalibration {
  float scale = 1.0f;
  float bias = 0.0f;
  float low = 0.0f;
  float high = 0.0f;
};

struct NeuralRankerFeatureInput {
  std::string original;
  std::string correction;
  float raw_original_logit = 0.0f;
  float raw_correction_logit = 0.0f;
  bool has_original_logit = false;
  float original_static = 0.0f;
  float correction_static = 0.0f;
  float original_static_ngram = 0.0f;
  float correction_static_ngram = 0.0f;
  float original_letter_lm_main = 0.0f;
  float correction_letter_lm_main = 0.0f;
  float original_transfeme_proximity = 0.0f;
  float transfeme_proximity = 0.0f;
  bool has_geometry = false;
  bool original_known = false;
};

// Feature vector for a single suggestion candidate.
// Matches Yandex features_ranker.json catalog (~40 features).
// Features grouped by computability: some filled now, others need
// neural models or touch coordinates and default to 0.0f.
struct RankerFeatures {
  // -- Computable now: string metrics --
  float original_length = 0.0f;           // len(original) in codepoints
  float correction_length = 0.0f;         // len(correction) in codepoints
  float original_ndict = 0.0f;            // 1.0 if original in dictionary
  float correction_ndict = 0.0f;          // 1.0 if correction in dictionary
  float no_change = 0.0f;                 // 1.0 if original == correction
  float levenshtein = 0.0f;              // edit distance
  float levenshtein_per_letter = 0.0f;   // levenshtein / max(len, 1)
  float distance_transfeme_fwd = 0.0f;   // tfmtrie proximity original->correction
  float distance_transfeme_rev = 0.0f;   // correction->original
  float distance_per_letter_fwd = 0.0f;
  float distance_per_letter_rev = 0.0f;
  float is_permutation = 0.0f;           // 1.0 if anagram
  float kendall_tau_distance = 0.0f;
  float bleu_2 = 0.0f;
  float bleu_3 = 0.0f;
  float bleu_4 = 0.0f;
  float letter_precision_1 = 0.0f;
  float letter_precision_2 = 0.0f;
  float letter_precision_3 = 0.0f;

  // -- Trie weight features --
  float original_lm_1gr_static = 0.0f;   // log(weight) of original
  float correction_lm_1gr_static = 0.0f; // log(weight) of correction
  float original_lm_2gr_static = 0.0f;   // placeholder (bigram)
  float correction_lm_2gr_static = 0.0f;
  float original_lm_ngr_static = 0.0f;
  float correction_lm_ngr_static = 0.0f;

  // -- Neural model features (0.0 until models available) --
  float original_lm_ngr_main = 0.0f;
  float correction_lm_ngr_main = 0.0f;
  float original_letter_lm_main = 0.0f;
  float correction_letter_lm_main = 0.0f;
  float diacr_only = 0.0f;

  // -- Combo features (derived from above) --
  float combo_dist_lm = 0.0f;
  float combo_lm_diff = 0.0f;
  float combo_len_diff = 0.0f;
  float combo_dist_lm_rev = 0.0f;
  float combo_lm_static_diff = 0.0f;

  // -- Keyboard geometry (0.0 without touch coords) --
  float geometry_distance_per_letter = 0.0f;
  float geometry_distance_original = 0.0f;
  float geometry_distance_per_letter_original = 0.0f;
  float geometry_distance_rel_gap = 0.0f;
  float geometry_distance_rel_gap_and_char_lm_per_letter = 0.0f;
  float geometry_distance_ngram_per_letter = 0.0f;
  float geometry_distance_ngram_and_char_lm = 0.0f;
  float geometry_distance_ngram_original = 0.0f;
  float geometry_distance_ngram_per_letter_original = 0.0f;
  float geometry_distance_ngram_rel_gap = 0.0f;
  float neural_oov = 0.0f;

  // Pack into float array for model input.
  std::vector<float> ToVector() const;
  static constexpr size_t kFeatureCount = 39;

  // Pack into the exact 44-feature vector in the original Yandex
  // features_ranker.json order (feature id 0..43), with the 5 Combos computed as
  // the real sum/sub the catalog specifies. This is the input the reverse-
  // engineered MatrixNet tree ensemble is indexed against.
  std::vector<float> ToVector44() const;
  static constexpr size_t kMatrixNetFeatureCount = 44;
};

// Convert the YnnLM decoder probability to the recovered ranker's negative-log
// scale. Android ground truth uses values such as -log(p)=13.388319. Invalid
// probabilities/configuration are rejected instead of injecting NaN/Inf.
bool CalibrateNgramLogit(float raw, const NgramCalibration& calibration,
                         float* out);

// Convert the parsed tfmtrie similarity sum to Android's distance_transfeme
// feature scale. Constants are fitted from the captured кот and кто vectors.
bool CalibrateTransfemeDistance(float raw_similarity, float* out);

// Proxy the recovered touch-geometry scale from the calibrated transfeme
// distance. The affine constants are fixed by Android's retained кот/кто
// vectors; the remaining geometry features are exact algebraic derivatives.
bool CalibrateGeometryDistancePerLetter(float transfeme_distance, float* out);

bool CalibrateStaticUnigramRank(int32_t rank, float* out);
bool CalibrateStaticNgramRank(int32_t rank, float* out);

// Blend MatrixNet with the word-vocabulary prior while CharLMJoint is not
// executable on Harmony. This restores the strong frequent-word prior present
// in the Android/iOS ranker without hard-coding individual completions.
float BlendMatrixNetWithVocabularyRank(float matrixnet_score, int32_t rank,
                                       size_t vocabulary_size = 0);

RankerFeatures BuildNeuralRankerFeatures(
    const NeuralRankerFeatureInput& input,
    const NgramCalibration& calibration);

// ML-based suggestion ranker using MindSpore Lite.
class MLRanker {
 public:
  MLRanker() = default;
  ~MLRanker();

  MLRanker(const MLRanker&) = delete;
  MLRanker& operator=(const MLRanker&) = delete;
  MLRanker(MLRanker&&) = delete;
  MLRanker& operator=(MLRanker&&) = delete;

  // Load model from .ms file bytes.
  [[nodiscard]] Status Load(const uint8_t* data, size_t size);

  // Load the reverse-engineered Yandex MatrixNet oblivious-tree ranker
  // (ranker_model.bin, 'HRNK' format: 1000 depth-6 trees). Pure C++, no
  // MindSpore — runs on host and device. When loaded, ScoreOne evaluates the
  // ensemble over ToVector44() instead of the heuristic/.ms path.
  [[nodiscard]] Status LoadMatrixNet(const uint8_t* data, size_t size);

  [[nodiscard]] bool is_matrixnet_loaded() const { return matrixnet_loaded_; }
  [[nodiscard]] uint32_t matrixnet_feature_count() const {
    return mn_feature_count_;
  }

  // Evaluate an arbitrary HRNK feature catalog.  The spelling ranker uses 44
  // features, while swipe/full, swipe/backoff and swipe/blocker use 47/42/31.
  // Returns NaN when no MatrixNet is loaded or the vector contract mismatches.
  [[nodiscard]] float ScoreMatrixNet(
      const std::vector<float>& features) const;

  // Score a batch of candidates. Returns scores (higher = better).
  [[nodiscard]] std::vector<float> Score(
      const std::vector<RankerFeatures>& candidates) const;

  // Single candidate score.
  [[nodiscard]] float ScoreOne(const RankerFeatures& features) const;

  [[nodiscard]] bool is_loaded() const { return loaded_; }
  [[nodiscard]] const std::string& diagnostics() const { return diagnostics_; }

  void Reset();

 private:
  // Heuristic fallback when model is not loaded.
  float HeuristicScore(const RankerFeatures& features) const;

  // Sum the depth-6 oblivious trees over an already validated feature vector.
  float EvalMatrixNet(const std::vector<float>& features) const;

  bool loaded_ = false;
  void* model_ = nullptr;    // mindspore::Model*
  void* context_ = nullptr;  // mindspore::Context*
  std::string diagnostics_ = "not_loaded";

  // MatrixNet ensemble (flat, cache-friendly): per tree, 6 (feature id,
  // threshold) splits + 64 leaf values. leaf index = OR_i (x[feat_i] > thr_i)<<i.
  bool matrixnet_loaded_ = false;
  uint32_t mn_num_trees_ = 0;
  uint32_t mn_feature_count_ = 0;
  std::vector<uint8_t> mn_feat_;   // mn_num_trees_ * 6
  std::vector<float> mn_thr_;      // mn_num_trees_ * 6
  std::vector<float> mn_leaf_;     // mn_num_trees_ * 64
  std::string mn_diagnostics_ = "mn_not_loaded";
};

}  // namespace hoskey::engine
