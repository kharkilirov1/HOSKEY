#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace hoskey::engine {

class MLRanker;
class RnntSwipeModel;

struct SwipePoint {
  double x = 0.0;
  double y = 0.0;
  uint32_t time_ms = 0;
};

using SwipeRnntFeatureWindow = std::array<float, 5 * 52>;

struct SwipeCandidate {
  std::string word;
  float score = 0.0f;
  float geometry_cost = 0.0f;
  float matrixnet_score = std::numeric_limits<float>::quiet_NaN();
  float rnnt_score = std::numeric_limits<float>::quiet_NaN();
  float rnnt_predictor_score = std::numeric_limits<float>::quiet_NaN();
  // Base ids 0..37 from features_ranker_swipe_backoff.json.  Candidate-set
  // Stats ids 38..41 are appended immediately before MatrixNet evaluation.
  std::array<float, 38> swipe_backoff_features{};
};

struct SwipeDecodeOptions {
  size_t max_results = 12;
  size_t context_size = 0;
  const MLRanker* backoff_ranker = nullptr;
  const MLRanker* primary_ranker = nullptr;
  const MLRanker* blocker_ranker = nullptr;
  const RnntSwipeModel* rnnt_model = nullptr;
  uint32_t rnnt_timeout_ms = 200;
  bool enforce_blocker = true;
};

struct SwipeDecodeDiagnostics {
  size_t candidate_pool_size = 0;
  size_t model_scored = 0;
  bool backoff_model_engaged = false;
  bool primary_model_engaged = false;
  bool blocker_model_engaged = false;
  bool blocked = false;
  float top_matrixnet_score = std::numeric_limits<float>::quiet_NaN();
  float blocker_score = std::numeric_limits<float>::quiet_NaN();
  size_t rnnt_frames = 0;
  size_t rnnt_candidates_scored = 0;
  size_t rnnt_joint_evaluations_upper_bound = 0;
  size_t rnnt_compute_units_upper_bound = 0;
  bool rnnt_budget_limited = false;
  float first_rnnt_score = std::numeric_limits<float>::quiet_NaN();
  float first_rnnt_predictor_score =
      std::numeric_limits<float>::quiet_NaN();
  int64_t rnnt_encode_ms = 0;
  int64_t rnnt_score_ms = 0;
  int64_t rnnt_elapsed_ms = 0;
};

// Immutable endpoint index over the ynnlm vocabulary. Building it once when the
// engine opens keeps a gesture request from decoding every vocabulary word.
class RussianSwipeDecoder {
 public:
  void Build(
      const std::vector<std::string>& vocabulary,
      const std::unordered_map<std::string, uint32_t>& vocabulary_rank);
  void Reset();

  [[nodiscard]] bool is_ready() const { return vocabulary_ != nullptr; }
  [[nodiscard]] size_t indexed_word_count() const {
    return indexed_word_count_;
  }

  std::vector<SwipeCandidate> Decode(
      const std::vector<SwipePoint>& points, double keyboard_width,
      double keyboard_height, size_t max_results = 12,
      size_t* candidate_pool_size = nullptr) const;

  std::vector<SwipeCandidate> Decode(
      const std::vector<SwipePoint>& points, double keyboard_width,
      double keyboard_height, const SwipeDecodeOptions& options,
      SwipeDecodeDiagnostics* diagnostics) const;

 private:
  static constexpr size_t kRussianKeyCount = 31;
  const std::vector<std::string>* vocabulary_ = nullptr;
  const std::unordered_map<std::string, uint32_t>* vocabulary_rank_ = nullptr;
  std::vector<std::vector<uint32_t>> endpoint_buckets_;
  size_t indexed_word_count_ = 0;
};

// Center of a Russian alphabet key in panel pixel coordinates. Exposed so the
// host test can generate deterministic gesture paths from the same geometry.
bool RussianSwipeKeyCenter(char32_t codepoint, double keyboard_width,
                           double keyboard_height, SwipePoint* out);

// Reverse-backed RNNT frontend.  The native sampler retains points at least
// 0.1 normalized units apart (always retaining the final point); two valid
// kernel-3 convs make each encoder frame a sliding window of five points.
std::vector<SwipeRnntFeatureWindow> BuildRussianSwipeRnntWindows(
    const std::vector<SwipePoint>& points, double keyboard_width,
    double keyboard_height);

// Geometry-first swipe decoder. The input is a continuous path captured between
// TouchDown and TouchUp. Candidates come from the exact ynnlm word vocabulary;
// frequency rank only breaks close geometric ties.
std::vector<SwipeCandidate> DecodeRussianSwipe(
    const std::vector<SwipePoint>& points, double keyboard_width,
    double keyboard_height, const std::vector<std::string>& vocabulary,
    const std::unordered_map<std::string, uint32_t>& vocabulary_rank,
    size_t max_results = 12);

}  // namespace hoskey::engine
