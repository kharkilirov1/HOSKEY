#include "ml_ranker.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>

#include "text_utils.h"

// MindSpore Lite headers — available in HarmonyOS NDK.
// Build-gated: only compiled when MINDSPORE_LITE_AVAILABLE is defined.
#ifdef MINDSPORE_LITE_AVAILABLE
#include "mindspore/context.h"
#include "mindspore/model.h"
#include "mindspore/status.h"
#include "mindspore/tensor.h"
#endif

namespace hoskey::engine {

#ifdef MINDSPORE_LITE_AVAILABLE
namespace {

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

// Compact IO topology for diagnostics, e.g.
// "io{in:input:1x500/t43;out:output:1x1/t43}".
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

}  // namespace
#endif

namespace {

float CharNgramPrecision(const std::vector<char32_t>& reference,
                         const std::vector<char32_t>& hypothesis, size_t n) {
  if (hypothesis.size() < n || reference.size() < n) return 0.0f;
  std::unordered_map<size_t, int> counts;
  auto hash_ngram = [n](const std::vector<char32_t>& value, size_t start) {
    size_t hash = 0;
    for (size_t i = 0; i < n; ++i) {
      hash = hash * 1000003u ^ static_cast<size_t>(value[start + i]);
    }
    return hash;
  };
  for (size_t i = 0; i + n <= reference.size(); ++i) {
    ++counts[hash_ngram(reference, i)];
  }
  int matched = 0;
  int total = 0;
  for (size_t i = 0; i + n <= hypothesis.size(); ++i) {
    auto it = counts.find(hash_ngram(hypothesis, i));
    if (it != counts.end() && it->second > 0) {
      ++matched;
      --it->second;
    }
    ++total;
  }
  return total == 0 ? 0.0f
                    : static_cast<float>(matched) / static_cast<float>(total);
}

bool IsPermutation(const std::vector<char32_t>& a,
                   const std::vector<char32_t>& b) {
  if (a.size() != b.size()) return false;
  auto sorted_a = a;
  auto sorted_b = b;
  std::sort(sorted_a.begin(), sorted_a.end());
  std::sort(sorted_b.begin(), sorted_b.end());
  return sorted_a == sorted_b;
}

float KendallTauNormalized(const std::vector<char32_t>& a,
                           const std::vector<char32_t>& b) {
  if (a.empty() || b.empty() || a.size() != b.size()) return 0.0f;
  std::unordered_map<char32_t, int> position;
  for (int i = 0; i < static_cast<int>(b.size()); ++i) position[b[i]] = i;
  std::vector<int> mapped;
  mapped.reserve(a.size());
  for (char32_t value : a) {
    const auto it = position.find(value);
    if (it != position.end()) mapped.push_back(it->second);
  }
  int inversions = 0;
  for (size_t i = 0; i < mapped.size(); ++i) {
    for (size_t j = i + 1; j < mapped.size(); ++j) {
      if (mapped[i] > mapped[j]) ++inversions;
    }
  }
  const int count = static_cast<int>(mapped.size());
  const int maximum = count * (count - 1) / 2;
  return maximum == 0 ? 0.0f
                      : static_cast<float>(inversions) /
                            static_cast<float>(maximum);
}

}  // namespace

// ============================================================================
// RankerFeatures
// ============================================================================

bool CalibrateNgramLogit(float raw, const NgramCalibration& calibration,
                         float* out) {
  if (out == nullptr || !std::isfinite(raw) ||
      !std::isfinite(calibration.scale) ||
      !std::isfinite(calibration.bias) ||
      !std::isfinite(calibration.low) ||
      !std::isfinite(calibration.high) || calibration.scale <= 0.0f ||
      calibration.low > calibration.high || raw < 0.0f || raw > 1.0f) {
    return false;
  }
  if (raw == 0.0f) {
    *out = calibration.high;
    return true;
  }
  const float negative_log =
      -std::log(std::max(raw, std::numeric_limits<float>::min()));
  const float mapped = negative_log * calibration.scale + calibration.bias;
  if (!std::isfinite(mapped)) return false;
  *out = std::clamp(mapped, calibration.low, calibration.high);
  return true;
}

bool CalibrateTransfemeDistance(float raw_similarity, float* out) {
  if (out == nullptr || !std::isfinite(raw_similarity)) return false;
  constexpr float kScale = -0.7758315358f;
  constexpr float kBias = 26.7383875174f;
  const float mapped = raw_similarity * kScale + kBias;
  if (!std::isfinite(mapped)) return false;
  *out = std::clamp(mapped, 0.0f, 64.0f);
  return true;
}

bool CalibrateGeometryDistancePerLetter(float transfeme_distance, float* out) {
  if (out == nullptr || !std::isfinite(transfeme_distance)) return false;
  constexpr float kScale = 1.2213213024f;
  constexpr float kBias = 11.7852434380f;
  *out = std::clamp(kScale * transfeme_distance + kBias, 0.0f, 256.0f);
  return std::isfinite(*out);
}

bool CalibrateStaticUnigramRank(int32_t rank, float* out) {
  if (out == nullptr || rank < 0) return false;
  constexpr float kScale = 1.4529097268f;
  constexpr float kBias = 3.8357257144f;
  *out = std::clamp(kScale * std::log1p(static_cast<float>(rank)) + kBias,
                    0.0f, 64.0f);
  return std::isfinite(*out);
}

bool CalibrateStaticNgramRank(int32_t rank, float* out) {
  if (out == nullptr || rank < 0) return false;
  constexpr float kScale = 1.4007749334f;
  constexpr float kBias = 4.0694079541f;
  *out = std::clamp(kScale * std::log1p(static_cast<float>(rank)) + kBias,
                    0.0f, 64.0f);
  return std::isfinite(*out);
}

float BlendMatrixNetWithVocabularyRank(float matrixnet_score, int32_t rank,
                                       size_t vocabulary_size) {
  if (!std::isfinite(matrixnet_score)) return matrixnet_score;
  const size_t effective_rank = rank >= 0
      ? static_cast<size_t>(rank)
      : vocabulary_size;
  if (effective_rank == 0 && rank < 0) return matrixnet_score;
  constexpr float kRankPriorScale = 4.4f;
  return matrixnet_score -
      kRankPriorScale * std::log1p(static_cast<float>(effective_rank));
}

RankerFeatures BuildNeuralRankerFeatures(
    const NeuralRankerFeatureInput& input,
    const NgramCalibration& calibration) {
  RankerFeatures features;
  const auto original = DecodeUtf8(input.original);
  const auto correction = DecodeUtf8(input.correction);
  features.original_length = static_cast<float>(original.size());
  features.correction_length = static_cast<float>(correction.size());
  features.original_ndict = input.original_known ? 1.0f : 0.0f;
  features.correction_ndict = 1.0f;
  features.no_change = input.original == input.correction ? 1.0f : 0.0f;

  features.levenshtein = static_cast<float>(
      LevenshteinDistanceUtf8(input.original, input.correction));
  features.levenshtein_per_letter =
      features.levenshtein / std::max(features.correction_length, 1.0f);

  features.distance_transfeme_fwd = input.transfeme_proximity;
  features.distance_transfeme_rev = input.transfeme_proximity;
  features.distance_per_letter_fwd = features.correction_length > 0.0f
      ? input.transfeme_proximity / features.correction_length
      : 0.0f;
  features.distance_per_letter_rev = features.original_length > 0.0f
      ? input.transfeme_proximity / features.original_length
      : 0.0f;

  features.is_permutation = IsPermutation(original, correction) ? 1.0f : 0.0f;
  features.kendall_tau_distance = KendallTauNormalized(original, correction);
  features.bleu_2 = CharNgramPrecision(original, correction, 2);
  features.bleu_3 = CharNgramPrecision(original, correction, 3);
  features.bleu_4 = CharNgramPrecision(original, correction, 4);
  features.letter_precision_2 = features.bleu_2;
  features.letter_precision_3 = features.bleu_3;
  if (!correction.empty()) {
    std::unordered_map<char32_t, int> counts;
    for (char32_t value : original) ++counts[value];
    int matched = 0;
    for (char32_t value : correction) {
      auto it = counts.find(value);
      if (it != counts.end() && it->second > 0) {
        ++matched;
        --it->second;
      }
    }
    features.letter_precision_1 =
        static_cast<float>(matched) / features.correction_length;
  }

  features.original_lm_1gr_static = input.original_static;
  features.original_lm_2gr_static = input.original_static_ngram;
  features.original_lm_ngr_static = input.original_static_ngram;
  features.correction_lm_1gr_static = input.correction_static;
  features.correction_lm_2gr_static = input.correction_static_ngram;
  features.correction_lm_ngr_static = input.correction_static_ngram;
  features.original_letter_lm_main = input.original_letter_lm_main;
  features.correction_letter_lm_main = input.correction_letter_lm_main;

  float original_main = calibration.high;
  if (input.has_original_logit) {
    (void)CalibrateNgramLogit(input.raw_original_logit, calibration,
                              &original_main);
  }
  float correction_main = calibration.high;
  (void)CalibrateNgramLogit(input.raw_correction_logit, calibration,
                            &correction_main);
  features.original_lm_ngr_main = original_main;
  features.correction_lm_ngr_main = correction_main;

  if (input.has_geometry) {
    float original_per_letter = 0.0f;
    float correction_per_letter = 0.0f;
    if (CalibrateGeometryDistancePerLetter(
            input.original_transfeme_proximity, &original_per_letter) &&
        CalibrateGeometryDistancePerLetter(
            input.transfeme_proximity, &correction_per_letter) &&
        original_per_letter > 0.0f) {
      const float relative_gap = correction_per_letter / original_per_letter;
      features.geometry_distance_per_letter = correction_per_letter;
      features.geometry_distance_original =
          original_per_letter * features.original_length;
      features.geometry_distance_per_letter_original = original_per_letter;
      features.geometry_distance_rel_gap = relative_gap;
      features.geometry_distance_rel_gap_and_char_lm_per_letter =
          features.original_length > 0.0f
              ? (relative_gap + features.original_letter_lm_main) /
                    features.original_length
              : 0.0f;
      // The recovered tap path emits identical scalar and n-gram geometry for
      // the captured кот/кто vectors. Keep the two catalog branches explicit
      // so a future coordinate kernel can replace this proxy without changing
      // the MatrixNet contract.
      features.geometry_distance_ngram_per_letter = correction_per_letter;
      features.geometry_distance_ngram_and_char_lm =
          correction_per_letter * features.correction_length +
          features.original_letter_lm_main;
      features.geometry_distance_ngram_original =
          features.geometry_distance_original;
      features.geometry_distance_ngram_per_letter_original =
          original_per_letter;
      features.geometry_distance_ngram_rel_gap = relative_gap;
    }
  }
  return features;
}

std::vector<float> RankerFeatures::ToVector() const {
  return {
    // String metrics (19)
    original_length,
    correction_length,
    original_ndict,
    correction_ndict,
    no_change,
    levenshtein,
    levenshtein_per_letter,
    distance_transfeme_fwd,
    distance_transfeme_rev,
    distance_per_letter_fwd,
    distance_per_letter_rev,
    is_permutation,
    kendall_tau_distance,
    bleu_2,
    bleu_3,
    bleu_4,
    letter_precision_1,
    letter_precision_2,
    letter_precision_3,
    // Trie weights (6)
    original_lm_1gr_static,
    correction_lm_1gr_static,
    original_lm_2gr_static,
    correction_lm_2gr_static,
    original_lm_ngr_static,
    correction_lm_ngr_static,
    // Neural (5)
    original_lm_ngr_main,
    correction_lm_ngr_main,
    original_letter_lm_main,
    correction_letter_lm_main,
    diacr_only,
    // Combos (5)
    combo_dist_lm,
    combo_lm_diff,
    combo_len_diff,
    combo_dist_lm_rev,
    combo_lm_static_diff,
    // Geometry (4)
    geometry_distance_per_letter,
    geometry_distance_original,
    geometry_distance_rel_gap,
    neural_oov
  };
}

std::vector<float> RankerFeatures::ToVector44() const {
  // Exact features_ranker.json order (feature id 0..43). The 5 Combos are the
  // real sum/sub the catalog specifies (not the heuristic products above).
  const float dt = distance_transfeme_fwd;  // distance_transfeme(original,correction)
  return {
      original_length,            // 0  length (original)
      original_lm_ngr_main,       // 1  lm_ngr_main (original)
      original_lm_1gr_static,     // 2  lm_1gr_static (original)
      original_lm_2gr_static,     // 3  lm_2gr_static (original)
      original_lm_ngr_static,     // 4  lm_ngr_static (original)
      original_letter_lm_main,    // 5  letter_lm_main (original)
      original_ndict,             // 6  ndict (original)
      correction_length,          // 7  length (correction)
      correction_lm_ngr_main,     // 8  lm_ngr_main (correction)
      correction_lm_1gr_static,   // 9  lm_1gr_static (correction)
      correction_lm_2gr_static,   // 10 lm_2gr_static (correction)
      correction_lm_ngr_static,   // 11 lm_ngr_static (correction)
      correction_letter_lm_main,  // 12 letter_lm_main (correction)
      correction_ndict,           // 13 ndict (correction)
      no_change,                  // 14 no_change
      diacr_only,                 // 15 diacr_only
      dt,                         // 16 distance_transfeme
      distance_per_letter_fwd,    // 17 distance_per_letter_transfeme
      levenshtein,                // 18 levenshtein
      levenshtein_per_letter,     // 19 levenshtein_per_letter
      bleu_2,                     // 20
      bleu_3,                     // 21
      bleu_4,                     // 22
      letter_precision_1,         // 23
      letter_precision_2,         // 24
      letter_precision_3,         // 25
      is_permutation,             // 26
      kendall_tau_distance,       // 27
      dt + correction_lm_ngr_main,        // 28 sum(distance_transfeme, lm_ngr_main_c)
      dt + correction_lm_ngr_static,      // 29 sum(distance_transfeme, lm_ngr_static_c)
      original_lm_ngr_main - correction_lm_ngr_main,      // 30 sub(lm_ngr_main o,c)
      original_lm_1gr_static - correction_lm_1gr_static,  // 31 sub(lm_1gr_static o,c)
      original_length - correction_length,               // 32 sub(length o,c)
      geometry_distance_per_letter,   // 33 geometry_distance_per_letter
      geometry_distance_original,     // 34 geometry_distance_original
      geometry_distance_per_letter_original,  // 35
      geometry_distance_rel_gap,      // 36 geometry_distance_rel_gap
      geometry_distance_rel_gap_and_char_lm_per_letter,  // 37
      geometry_distance_ngram_per_letter,                // 38
      geometry_distance_ngram_and_char_lm,               // 39
      geometry_distance_ngram_original,                  // 40
      geometry_distance_ngram_per_letter_original,       // 41
      geometry_distance_ngram_rel_gap,                   // 42
      neural_oov,                     // 43 neural_oov
  };
}

// ============================================================================
// MLRanker
// ============================================================================

MLRanker::~MLRanker() {
  Reset();
}

void MLRanker::Reset() {
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
  matrixnet_loaded_ = false;
  mn_num_trees_ = 0;
  mn_feature_count_ = 0;
  mn_feat_.clear();
  mn_thr_.clear();
  mn_leaf_.clear();
  mn_diagnostics_ = "mn_not_loaded";
}

Status MLRanker::Load(const uint8_t* data, size_t size) {
  Reset();
  if (!data || size == 0) {
    diagnostics_ = "invalid:empty_model";
    return Status::InvalidArgument("MLRanker: null or empty model data");
  }

  // Validate MSL2 header.
  if (size < 8) {
    diagnostics_ = "invalid:too_small";
    return Status::ParseError("MLRanker: model too small");
  }
  // MSL2 magic is at offset 4 (first 4 bytes are size prefix).
  if (std::memcmp(data + 4, "MSL2", 4) != 0) {
    diagnostics_ = "invalid:no_msl2_header";
    return Status::ParseError("MLRanker: not a MindSpore Lite model (no MSL2 header)");
  }

#ifdef MINDSPORE_LITE_AVAILABLE
  OH_AI_ContextHandle ctx = CreateCpuInferenceContext();
  if (!ctx) {
    // MindSpore runtime not available — use heuristic fallback.
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
  if (inputs.handle_num != 1 || !inputs.handle_list ||
      outputs.handle_num == 0 || !outputs.handle_list) {
    diagnostics_ = "unsupported:io_count," + DescribeModelIo(model);
    OH_AI_ModelDestroy(&model);
    OH_AI_ContextDestroy(&ctx);
    loaded_ = false;
    return Status::Ok();
  }
  const size_t input_floats =
      OH_AI_TensorGetDataSize(inputs.handle_list[0]) / sizeof(float);
  if (input_floats != RankerFeatures::kFeatureCount) {
    diagnostics_ = "unsupported:feature_count=" + std::to_string(input_floats) +
                   ",expected=" +
                   std::to_string(RankerFeatures::kFeatureCount) + "," +
                   DescribeModelIo(model);
    OH_AI_ModelDestroy(&model);
    OH_AI_ContextDestroy(&ctx);
    loaded_ = false;
    return Status::Ok();
  }
  if (OH_AI_TensorGetDataSize(outputs.handle_list[0]) < sizeof(float)) {
    OH_AI_ModelDestroy(&model);
    OH_AI_ContextDestroy(&ctx);
    loaded_ = false;
    diagnostics_ = "unsupported:empty_output";
    return Status::Ok();
  }

  model_ = model;
  context_ = ctx;
  loaded_ = true;
  diagnostics_ = "loaded:features=" +
                 std::to_string(RankerFeatures::kFeatureCount);
  return Status::Ok();
#else
  // No MindSpore runtime — use heuristic fallback.
  // Still validate header so we know the data is valid.
  loaded_ = false;
  diagnostics_ = "unsupported:mindspore_lite_unavailable";
  return Status::Ok();
#endif
}

float MLRanker::HeuristicScore(const RankerFeatures& f) const {
  // Weighted linear combination mimicking a trained ranker.
  // Uses the expanded Yandex-style feature set.
  float score = 0.0f;

  // Exact match is the strongest signal.
  score += f.no_change * 200.0f;

  // Dictionary membership bonus.
  score += f.correction_ndict * 80.0f;

  // Language model static weight (log-frequency from trie).
  score += f.correction_lm_1gr_static * 12.0f;

  // Penalize edit distance.
  score -= f.levenshtein * 15.0f;
  score -= f.levenshtein_per_letter * 10.0f;

  // Transfeme (keyboard proximity) correction bonus.
  score += f.distance_transfeme_fwd * 30.0f;

  // Prefer shorter completions (inverse length).
  if (f.correction_length > 0.0f) {
    score += (1.0f / f.correction_length) * 10.0f;
  }

  // N-gram overlap signals.
  score += f.bleu_2 * 15.0f;
  score += f.bleu_3 * 10.0f;
  score += f.bleu_4 * 5.0f;

  // Letter precision: character-level overlap.
  score += f.letter_precision_1 * 20.0f;
  score += f.letter_precision_2 * 10.0f;

  // Combo features.
  score += f.combo_dist_lm * 5.0f;
  score += f.combo_lm_static_diff * 8.0f;

  // Length difference penalty.
  score -= std::abs(f.combo_len_diff) * 3.0f;

  // Geometry / touch alignment.
  score -= f.geometry_distance_per_letter * 18.0f;
  score -= std::max(0.0f, f.geometry_distance_rel_gap) * 22.0f;
  score += std::max(0.0f, -f.geometry_distance_rel_gap) * 12.0f;

  // Permutation bonus (anagram — likely intended word).
  score += f.is_permutation * 25.0f;

  // Neural LM features (will be 0.0 until models are wired).
  score += f.correction_lm_ngr_main * 20.0f;

  return score;
}

Status MLRanker::LoadMatrixNet(const uint8_t* data, size_t size) {
  matrixnet_loaded_ = false;
  mn_num_trees_ = 0;
  mn_feature_count_ = 0;
  mn_feat_.clear();
  mn_thr_.clear();
  mn_leaf_.clear();
  if (!data || size < 12 || std::memcmp(data, "HRNK", 4) != 0) {
    mn_diagnostics_ = "mn_invalid:header";
    return Status::ParseError("MatrixNet: bad HRNK header");
  }
  auto u32 = [&](size_t o) -> uint32_t {
    return static_cast<uint32_t>(data[o]) | (static_cast<uint32_t>(data[o + 1]) << 8) |
           (static_cast<uint32_t>(data[o + 2]) << 16) |
           (static_cast<uint32_t>(data[o + 3]) << 24);
  };
  auto u16 = [&](size_t o) -> uint16_t {
    return static_cast<uint16_t>(data[o] | (data[o + 1] << 8));
  };
  auto f32 = [&](size_t o) -> float {
    float v;
    std::memcpy(&v, data + o, sizeof(float));
    return v;
  };
  const uint32_t n_trees = u32(4);
  const uint32_t n_feat = u32(8);
  if (n_feat == 0 || n_feat > 255 || n_trees == 0 || n_trees > 100000) {
    mn_diagnostics_ = "mn_invalid:counts";
    return Status::ParseError("MatrixNet: unexpected tree/feature count");
  }
  // Skip the per-feature border table: n_feat blocks of [u16 nb][nb x f32].
  size_t o = 12;
  for (uint32_t f = 0; f < n_feat; ++f) {
    if (o + 2 > size) {
      mn_diagnostics_ = "mn_invalid:borders_trunc";
      return Status::ParseError("MatrixNet: truncated border table");
    }
    const uint16_t nb = u16(o);
    const size_t block_bytes = 2 + static_cast<size_t>(nb) * 4;
    if (block_bytes > size - o) {
      mn_diagnostics_ = "mn_invalid:borders_trunc";
      return Status::ParseError("MatrixNet: truncated border values");
    }
    o += block_bytes;
  }
  // Trees: each = 6 x (u8 feat, f32 thr) + 64 x f32 leaf = 30 + 256 = 286 bytes.
  const size_t tree_bytes = 6 * 5 + 64 * 4;
  if (o + static_cast<size_t>(n_trees) * tree_bytes > size) {
    mn_diagnostics_ = "mn_invalid:trees_trunc";
    return Status::ParseError("MatrixNet: truncated tree section");
  }
  mn_feat_.resize(static_cast<size_t>(n_trees) * 6);
  mn_thr_.resize(static_cast<size_t>(n_trees) * 6);
  mn_leaf_.resize(static_cast<size_t>(n_trees) * 64);
  for (uint32_t t = 0; t < n_trees; ++t) {
    for (int i = 0; i < 6; ++i) {
      const uint8_t feat = data[o];
      const float thr = f32(o + 1);
      o += 5;
      if (feat >= n_feat) {
        mn_feat_.clear();
        mn_thr_.clear();
        mn_leaf_.clear();
        mn_diagnostics_ = "mn_invalid:feature_id";
        return Status::ParseError("MatrixNet: split feature is out of range");
      }
      mn_feat_[t * 6 + i] = feat;
      mn_thr_[t * 6 + i] = thr;
    }
    for (int l = 0; l < 64; ++l) {
      mn_leaf_[static_cast<size_t>(t) * 64 + l] = f32(o);
      o += 4;
    }
  }
  mn_num_trees_ = n_trees;
  mn_feature_count_ = n_feat;
  matrixnet_loaded_ = true;
  mn_diagnostics_ = "mn_loaded:trees=" + std::to_string(n_trees) +
                    ",features=" + std::to_string(n_feat);
  if (loaded_) {
    diagnostics_ += "," + mn_diagnostics_;
  } else if (diagnostics_ == "not_loaded") {
    diagnostics_ = mn_diagnostics_;
  } else {
    diagnostics_ = mn_diagnostics_ + ",ms=" + diagnostics_;
  }
  return Status::Ok();
}

float MLRanker::EvalMatrixNet(const std::vector<float>& x) const {
  // Oblivious (symmetric) trees: every node at depth i tests the same
  // (feature, threshold); the leaf index is the 6-bit word of those tests.
  // Convention: bit i = (x[feat_i] > thr_i), i is the split order (cond0 = LSB).
  double sum = 0.0;
  const size_t n = mn_num_trees_;
  for (size_t t = 0; t < n; ++t) {
    unsigned idx = 0;
    const size_t base = t * 6;
    for (int i = 0; i < 6; ++i) {
      const uint8_t f = mn_feat_[base + i];
      if (f < x.size() && x[f] > mn_thr_[base + i]) {
        idx |= (1u << i);
      }
    }
    sum += mn_leaf_[t * 64 + idx];
  }
  return static_cast<float>(sum);
}

float MLRanker::ScoreMatrixNet(const std::vector<float>& features) const {
  if (!matrixnet_loaded_ || features.size() != mn_feature_count_) {
    return std::numeric_limits<float>::quiet_NaN();
  }
  return EvalMatrixNet(features);
}

float MLRanker::ScoreOne(const RankerFeatures& features) const {
  // Reverse-engineered Yandex MatrixNet takes priority when available (pure C++,
  // works on host and device).
  if (matrixnet_loaded_ &&
      mn_feature_count_ == RankerFeatures::kMatrixNetFeatureCount) {
    return ScoreMatrixNet(features.ToVector44());
  }
#ifdef MINDSPORE_LITE_AVAILABLE
  if (loaded_ && model_) {
    OH_AI_ModelHandle model = static_cast<OH_AI_ModelHandle>(model_);
    OH_AI_TensorHandleArray inputs = OH_AI_ModelGetInputs(model);
    if (inputs.handle_num > 0 && inputs.handle_list) {
      OH_AI_TensorHandle input = inputs.handle_list[0];
      auto fv = features.ToVector();
      void* input_data = OH_AI_TensorGetMutableData(input);
      const size_t input_bytes = OH_AI_TensorGetDataSize(input);
      if (input_data && input_bytes >= fv.size() * sizeof(float)) {
        std::memcpy(input_data, fv.data(), fv.size() * sizeof(float));

        OH_AI_TensorHandleArray outputs = OH_AI_ModelGetOutputs(model);
        const OH_AI_Status status =
            OH_AI_ModelPredict(model, inputs, &outputs, nullptr, nullptr);
        if (status == OH_AI_STATUS_SUCCESS && outputs.handle_num > 0 &&
            outputs.handle_list) {
          OH_AI_TensorHandle output = outputs.handle_list[0];
          const float* out_data =
              static_cast<const float*>(OH_AI_TensorGetData(output));
          if (out_data && OH_AI_TensorGetDataSize(output) >= sizeof(float)) {
            return out_data[0];
          }
        }
      }
    }
  }
#endif
  return HeuristicScore(features);
}

std::vector<float> MLRanker::Score(
    const std::vector<RankerFeatures>& candidates) const {
  std::vector<float> scores;
  scores.reserve(candidates.size());
  for (const auto& c : candidates) {
    scores.push_back(ScoreOne(c));
  }
  return scores;
}

}  // namespace hoskey::engine
