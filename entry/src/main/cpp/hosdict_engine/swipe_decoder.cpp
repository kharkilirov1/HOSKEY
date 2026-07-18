#include "swipe_decoder.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iterator>
#include <limits>
#include <numeric>

#include "ml_ranker.h"
#include "rnnt_swipe_model.h"
#include "text_utils.h"

namespace hoskey::engine {
namespace {

constexpr size_t kResampledPointCount = 24;
constexpr size_t kRussianKeyCount = 31;
constexpr size_t kRnntFeatureCount = 52;
constexpr size_t kRnntWindowSize = 5;
constexpr size_t kMaxRnntEncoderFrames = 64;
// MindSpore Lite 2.6 Linux smoke timings put one complete encoder frame at
// roughly 12 joint calls and one predictor step at roughly three joint calls.
// This conservative normalized budget keeps the synchronous IME path bounded
// while preserving the recovered six-candidate beam on short gestures.
constexpr size_t kRnntEncoderFrameComputeUnits = 12;
constexpr size_t kRnntPredictorStepComputeUnits = 3;
constexpr size_t kMaxRnntComputeUnitsPerGesture = 512;
constexpr double kRnntSamplingDistance = 0.1;

double Distance(const SwipePoint& a, const SwipePoint& b) {
  const double dx = a.x - b.x;
  const double dy = a.y - b.y;
  return std::sqrt(dx * dx + dy * dy);
}

SwipePoint NormalizePanelPoint(const SwipePoint& point, double width,
                               double height) {
  // The toolbar occupies the top part of the IME panel. The four keyboard rows
  // divide the remaining region equally in the current ArkUI layout.
  const double keyboard_top = height * 0.18;
  const double keyboard_height = std::max(1.0, height - keyboard_top);
  return {std::clamp(point.x / std::max(1.0, width), 0.0, 1.0),
          std::clamp((point.y - keyboard_top) / keyboard_height, 0.0, 1.0),
          point.time_ms};
}

std::vector<SwipePoint> Resample(const std::vector<SwipePoint>& input,
                                 size_t count) {
  std::vector<SwipePoint> compact;
  compact.reserve(input.size());
  for (const SwipePoint& point : input) {
    if (compact.empty() || Distance(compact.back(), point) > 1.0e-5) {
      compact.push_back(point);
    }
  }
  if (compact.size() < 2 || count < 2) return {};

  std::vector<double> cumulative(compact.size(), 0.0);
  for (size_t i = 1; i < compact.size(); ++i) {
    cumulative[i] = cumulative[i - 1] + Distance(compact[i - 1], compact[i]);
  }
  const double total = cumulative.back();
  if (total < 1.0e-6) return {};

  std::vector<SwipePoint> result;
  result.reserve(count);
  size_t segment = 1;
  for (size_t i = 0; i < count; ++i) {
    const double target = total * static_cast<double>(i) /
                          static_cast<double>(count - 1);
    while (segment + 1 < cumulative.size() && cumulative[segment] < target) {
      ++segment;
    }
    const double begin = cumulative[segment - 1];
    const double end = cumulative[segment];
    const double t = end > begin ? (target - begin) / (end - begin) : 0.0;
    result.push_back({compact[segment - 1].x +
                          (compact[segment].x - compact[segment - 1].x) * t,
                      compact[segment - 1].y +
                          (compact[segment].y - compact[segment - 1].y) * t,
                      static_cast<uint32_t>(std::llround(
                          static_cast<double>(compact[segment - 1].time_ms) +
                          (static_cast<double>(compact[segment].time_ms) -
                           static_cast<double>(compact[segment - 1].time_ms)) *
                              t))});
  }
  return result;
}

bool CandidatePath(const std::string& word, double width, double height,
                   std::vector<SwipePoint>* out) {
  out->clear();
  const std::vector<char32_t> codepoints = DecodeUtf8(word);
  if (codepoints.size() < 2 || codepoints.size() > 24) return false;
  out->reserve(codepoints.size());
  for (const char32_t cp : codepoints) {
    SwipePoint point;
    if (!RussianSwipeKeyCenter(cp, width, height, &point)) return false;
    out->push_back(NormalizePanelPoint(point, width, height));
  }
  return true;
}

int RussianSwipeKeyIndex(char32_t codepoint) {
  if (codepoint == U'ё') codepoint = U'е';
  constexpr char32_t row0[] = U"йцукенгшщзх";
  constexpr char32_t row1[] = U"фывапролджэ";
  constexpr char32_t row2[] = U"ячсмитьбю";
  for (size_t i = 0; i + 1 < std::size(row0); ++i) {
    if (row0[i] == codepoint) return static_cast<int>(i);
  }
  for (size_t i = 0; i + 1 < std::size(row1); ++i) {
    if (row1[i] == codepoint) return static_cast<int>(11 + i);
  }
  for (size_t i = 0; i + 1 < std::size(row2); ++i) {
    if (row2[i] == codepoint) return static_cast<int>(22 + i);
  }
  return -1;
}

int NearestRussianSwipeKey(const SwipePoint& normalized) {
  int nearest = -1;
  double nearest_distance = std::numeric_limits<double>::max();
  constexpr char32_t keys[] =
      U"йцукенгшщзхфывапролджэячсмитьбю";
  for (size_t i = 0; i + 1 < std::size(keys); ++i) {
    SwipePoint center;
    if (!RussianSwipeKeyCenter(keys[i], 1.0, 1.0, &center)) continue;
    center = NormalizePanelPoint(center, 1.0, 1.0);
    const double distance = Distance(normalized, center);
    if (distance < nearest_distance) {
      nearest_distance = distance;
      nearest = static_cast<int>(i);
    }
  }
  return nearest;
}

size_t RnntHitboxIndex(int key_index) {
  if (key_index < 0) return 40;
  // The recovered feature registry follows visual key order.  Shift precedes
  // row 3 and Backspace follows it, so its nine letters are shifted by one.
  if (key_index < 22) return static_cast<size_t>(key_index);
  if (key_index < 31) return static_cast<size_t>(key_index + 1);
  return 40;
}

std::vector<SwipePoint> RnntSample(
    const std::vector<SwipePoint>& normalized) {
  std::vector<SwipePoint> sampled;
  sampled.reserve(normalized.size());
  for (size_t i = 0; i < normalized.size(); ++i) {
    const bool is_last = i + 1 == normalized.size();
    if (sampled.empty() || is_last ||
        Distance(sampled.back(), normalized[i]) >= kRnntSamplingDistance) {
      if (!sampled.empty() && is_last &&
          Distance(sampled.back(), normalized[i]) < 1.0e-7) {
        sampled.back() = normalized[i];
      } else {
        sampled.push_back(normalized[i]);
      }
    }
  }
  return sampled;
}

std::array<float, kRnntFeatureCount> BuildRnntPointFeatures(
    const SwipePoint& point, const SwipePoint& previous, bool is_last) {
  std::array<float, kRnntFeatureCount> values{};
  values[0] = static_cast<float>(point.x);
  values[1] = static_cast<float>(point.y);
  values[2] = static_cast<float>(point.time_ms) / 1000.0f;
  const float previous_time = static_cast<float>(previous.time_ms) / 1000.0f;
  const float dt = values[2] - previous_time + 1.0e-6f;
  values[3] = static_cast<float>(point.x - previous.x) / dt;
  values[4] = static_cast<float>(point.y - previous.y) / dt;
  values[5] = std::sqrt(values[3] * values[3] + values[4] * values[4]);

  const int key_index = NearestRussianSwipeKey(point);
  SwipePoint center;
  bool has_center = false;
  constexpr char32_t keys[] =
      U"йцукенгшщзхфывапролджэячсмитьбю";
  if (key_index >= 0 &&
      static_cast<size_t>(key_index) + 1 < std::size(keys)) {
    has_center = RussianSwipeKeyCenter(
        keys[static_cast<size_t>(key_index)], 1.0, 1.0, &center);
    if (has_center) center = NormalizePanelPoint(center, 1.0, 1.0);
  }
  if (has_center) {
    values[6] = static_cast<float>(point.x - center.x);
    values[7] = static_cast<float>(point.y - center.y);
    values[8] =
        std::sqrt(values[6] * values[6] + values[7] * values[7]);
    const size_t hitbox = RnntHitboxIndex(key_index);
    if (hitbox < 40) values[9 + hitbox] = 1.0f;
  } else {
    values[6] = -1.0f;
    values[7] = -1.0f;
    values[8] = -1.0f;
  }
  values[49] = is_last ? 1.0f : 0.0f;
  values[50] = 1.0f;  // layout_default: standard Russian (no direct ъ key)
  values[51] = 0.0f;  // layout_extra
  return values;
}

bool RnntTokens(const std::string& word, std::vector<int>* tokens) {
  tokens->clear();
  for (char32_t codepoint : DecodeUtf8(word)) {
    if (codepoint == U'ё') codepoint = U'е';
    if (codepoint == U'-') {
      tokens->push_back(5);
    } else if (codepoint >= U'а' && codepoint <= U'я') {
      tokens->push_back(6 + static_cast<int>(codepoint - U'а'));
    } else {
      tokens->clear();
      return false;
    }
  }
  return !tokens->empty();
}

struct Aggregate {
  double minimum = 0.0;
  double maximum = 0.0;
  double total = 0.0;
  double average = 0.0;
  double stddev = 0.0;
};

Aggregate Summarize(const std::vector<double>& values) {
  Aggregate result;
  if (values.empty()) return result;
  const auto [minimum, maximum] =
      std::minmax_element(values.begin(), values.end());
  result.minimum = *minimum;
  result.maximum = *maximum;
  result.total = std::accumulate(values.begin(), values.end(), 0.0);
  result.average = result.total / static_cast<double>(values.size());
  double variance = 0.0;
  for (const double value : values) {
    const double delta = value - result.average;
    variance += delta * delta;
  }
  result.stddev = std::sqrt(variance / static_cast<double>(values.size()));
  return result;
}

double PixelSquaredDistance(const SwipePoint& a, const SwipePoint& b,
                            double keyboard_width, double keyboard_height) {
  const double dx = (a.x - b.x) * keyboard_width;
  const double dy = (a.y - b.y) * keyboard_height * 0.82;
  return dx * dx + dy * dy;
}

double PathLengthPixels(const std::vector<SwipePoint>& points,
                        double keyboard_width, double keyboard_height) {
  double length = 0.0;
  for (size_t i = 1; i < points.size(); ++i) {
    length += std::sqrt(PixelSquaredDistance(
        points[i - 1], points[i], keyboard_width, keyboard_height));
  }
  return length;
}

std::vector<double> GestureSegmentTimes(
    const std::vector<SwipePoint>& points) {
  std::vector<double> times;
  if (points.size() < 2) return times;
  times.reserve(points.size() - 1);
  bool has_real_time = false;
  for (size_t i = 1; i < points.size(); ++i) {
    double delta = 0.0;
    if (points[i].time_ms >= points[i - 1].time_ms) {
      delta = static_cast<double>(points[i].time_ms - points[i - 1].time_ms);
    }
    if (delta > 0.0) has_real_time = true;
    times.push_back(delta);
  }
  if (!has_real_time) std::fill(times.begin(), times.end(), 16.0);
  return times;
}

std::array<float, 38> BuildSwipeBackoffFeatures(
    const std::string& word, uint32_t rank,
    const std::vector<SwipePoint>& raw_gesture,
    const std::vector<SwipePoint>& key_path, double keyboard_width,
    double keyboard_height, size_t context_size) {
  std::array<float, 38> values{};
  const std::vector<char32_t> codepoints = DecodeUtf8(word);

  std::vector<double> transit_distances;
  transit_distances.reserve(key_path.size() > 1 ? key_path.size() - 1 : 0);
  for (size_t i = 1; i < key_path.size(); ++i) {
    // The recovered RuleSwipe config caps one transition at 10000 squared px.
    transit_distances.push_back(std::min(
        10000.0, PixelSquaredDistance(key_path[i - 1], key_path[i],
                                      keyboard_width, keyboard_height)));
  }
  std::vector<double> inner_transits;
  if (transit_distances.size() > 2) {
    inner_transits.assign(transit_distances.begin() + 1,
                          transit_distances.end() - 1);
  }
  const Aggregate inner = Summarize(inner_transits);

  std::vector<double> path_penalties;
  path_penalties.reserve(key_path.size());
  for (const SwipePoint& key : key_path) {
    double nearest = std::numeric_limits<double>::max();
    for (const SwipePoint& point : raw_gesture) {
      nearest = std::min(nearest, PixelSquaredDistance(
                                      key, point, keyboard_width,
                                      keyboard_height));
    }
    path_penalties.push_back(
        nearest == std::numeric_limits<double>::max() ? 0.0 : nearest);
  }
  const Aggregate path = Summarize(path_penalties);
  const std::vector<double> segment_times = GestureSegmentTimes(raw_gesture);
  const Aggregate timing = Summarize(segment_times);
  const Aggregate transit = Summarize(transit_distances);

  float static_1 = 0.0f;
  float static_ngram = 0.0f;
  (void)CalibrateStaticUnigramRank(static_cast<int32_t>(rank), &static_1);
  (void)CalibrateStaticNgramRank(static_cast<int32_t>(rank), &static_ngram);
  const double distance = path.total;
  const double lm_weight = static_1;
  const double weight =
      std::sqrt(std::max(0.0, path.average)) + lm_weight;

  values[0] = static_cast<float>(distance);
  values[1] = static_cast<float>(weight);
  values[2] = static_cast<float>(lm_weight);
  values[3] = static_cast<float>(std::min<size_t>(context_size, 16));
  values[4] = static_cast<float>(codepoints.size());
  values[5] = static_1;
  values[6] = static_ngram;
  values[7] = static_ngram;
  values[8] = static_cast<float>(raw_gesture.size());
  values[9] = static_cast<float>(
      PathLengthPixels(raw_gesture, keyboard_width, keyboard_height));
  values[10] = static_cast<float>(transit.total);
  values[11] = transit_distances.empty()
                   ? 0.0f
                   : static_cast<float>(transit_distances.front());
  values[12] = transit_distances.empty()
                   ? 0.0f
                   : static_cast<float>(transit_distances.back());
  values[13] = static_cast<float>(inner.minimum);
  values[14] = static_cast<float>(inner.maximum);
  values[15] = static_cast<float>(inner.total);
  values[16] = static_cast<float>(inner.average);
  values[17] = static_cast<float>(inner.stddev);
  values[18] = static_cast<float>(path.minimum);
  values[19] = static_cast<float>(path.maximum);
  values[20] = static_cast<float>(path.total);
  values[21] = static_cast<float>(path.average);
  values[22] = static_cast<float>(path.stddev);
  if (!raw_gesture.empty() && !key_path.empty()) {
    values[23] = static_cast<float>(raw_gesture.back().x - key_path.back().x);
    values[24] = static_cast<float>(raw_gesture.back().y - key_path.back().y);
  }
  if (key_path.size() >= 2) {
    values[25] = static_cast<float>(key_path.back().x -
                                    key_path[key_path.size() - 2].x);
    values[26] = static_cast<float>(key_path.back().y -
                                    key_path[key_path.size() - 2].y);
  }
  values[27] = static_cast<float>(timing.total);
  values[28] = segment_times.empty()
                   ? 0.0f
                   : static_cast<float>(segment_times.front());
  values[29] = segment_times.empty()
                   ? 0.0f
                   : static_cast<float>(segment_times.back());
  values[30] = static_cast<float>(timing.minimum);
  values[31] = static_cast<float>(timing.maximum);
  values[32] = static_cast<float>(timing.average);
  values[33] = static_cast<float>(timing.stddev);
  values[34] = codepoints.empty()
                   ? 0.0f
                   : static_cast<float>(timing.total / codepoints.size());
  values[35] = path_penalties.empty()
                   ? 0.0f
                   : static_cast<float>(path_penalties.back());
  values[36] = static_cast<float>(path.minimum);
  values[37] = static_cast<float>(path.stddev);
  return values;
}

float Sigmoid(float value) {
  if (value >= 0.0f) {
    const float z = std::exp(-value);
    return 1.0f / (1.0f + z);
  }
  const float z = std::exp(value);
  return z / (1.0f + z);
}

std::vector<float> BuildSwipeBlockerFeatures(
    const std::vector<SwipePoint>& raw_points,
    const std::vector<SwipePoint>& normalized_points, double keyboard_width,
    double keyboard_height, size_t context_size,
    const std::vector<SwipeCandidate>& candidates) {
  std::vector<float> values(31, 0.0f);
  values[0] = static_cast<float>(raw_points.size());
  size_t out_points = 0;
  const double keyboard_top = keyboard_height * 0.18;
  for (const SwipePoint& point : raw_points) {
    if (point.x < 0.0 || point.x > keyboard_width || point.y < keyboard_top ||
        point.y > keyboard_height) {
      ++out_points;
    }
  }
  values[1] = static_cast<float>(out_points);
  values[2] = raw_points.empty()
                  ? 0.0f
                  : static_cast<float>(out_points) / raw_points.size();
  double curvature = 0.0;
  for (size_t i = 2; i < normalized_points.size(); ++i) {
    const double ax = normalized_points[i - 1].x - normalized_points[i - 2].x;
    const double ay = normalized_points[i - 1].y - normalized_points[i - 2].y;
    const double bx = normalized_points[i].x - normalized_points[i - 1].x;
    const double by = normalized_points[i].y - normalized_points[i - 1].y;
    const double cross = ax * by - ay * bx;
    const double dot = ax * bx + ay * by;
    if ((ax != 0.0 || ay != 0.0) && (bx != 0.0 || by != 0.0)) {
      curvature += std::fabs(std::atan2(cross, dot));
    }
  }
  values[3] = static_cast<float>(curvature);
  values[4] = static_cast<float>(std::min<size_t>(context_size, 16));
  values[5] = static_cast<float>(PathLengthPixels(
      normalized_points, keyboard_width, keyboard_height));
  const Aggregate timing = Summarize(GestureSegmentTimes(raw_points));
  values[6] = static_cast<float>(timing.total);
  values[7] = static_cast<float>(timing.minimum);
  values[8] = static_cast<float>(timing.maximum);
  values[9] = static_cast<float>(timing.average);

  const size_t top_k = std::min<size_t>(4, candidates.size());
  const auto aggregate_feature = [&](size_t feature) {
    std::vector<double> source;
    source.reserve(top_k);
    for (size_t i = 0; i < top_k; ++i) {
      source.push_back(candidates[i].swipe_backoff_features[feature]);
    }
    return Summarize(source);
  };
  const Aggregate distance = aggregate_feature(0);
  const Aggregate lm_weight = aggregate_feature(2);
  const Aggregate weight = aggregate_feature(1);
  const Aggregate text_size = aggregate_feature(4);
  const Aggregate transit = aggregate_feature(10);
  const Aggregate path_penalty = aggregate_feature(21);
  values[10] = static_cast<float>(distance.minimum);
  values[11] = static_cast<float>(distance.maximum);
  values[12] = static_cast<float>(distance.average);
  values[13] = static_cast<float>(lm_weight.minimum);
  values[14] = static_cast<float>(lm_weight.maximum);
  values[15] = static_cast<float>(lm_weight.average);
  values[16] = static_cast<float>(weight.minimum);
  values[17] = static_cast<float>(weight.maximum);
  values[18] = static_cast<float>(weight.average);
  values[19] = static_cast<float>(text_size.minimum);
  values[20] = static_cast<float>(text_size.maximum);
  values[21] = static_cast<float>(text_size.average);
  values[22] = static_cast<float>(transit.minimum);
  values[23] = static_cast<float>(transit.maximum);
  values[24] = static_cast<float>(transit.average);
  values[25] = static_cast<float>(path_penalty.minimum);
  values[26] = static_cast<float>(path_penalty.maximum);
  values[27] = static_cast<float>(path_penalty.average);
  values[28] = static_cast<float>(text_size.minimum);
  values[29] = static_cast<float>(text_size.maximum);
  values[30] = static_cast<float>(text_size.average);
  return values;
}

std::vector<SwipeCandidate> ScoreCandidates(
    const std::vector<SwipePoint>& raw_points,
    const std::vector<SwipePoint>& normalized_points,
    const std::vector<SwipePoint>& gesture, double keyboard_width,
    double keyboard_height, const std::vector<std::string>& vocabulary,
    const std::unordered_map<std::string, uint32_t>& vocabulary_rank,
    const std::vector<uint32_t>* candidate_indices,
    const SwipeDecodeOptions& options, SwipeDecodeDiagnostics* diagnostics) {
  std::vector<SwipeCandidate> scored;
  const size_t reserve = candidate_indices ? candidate_indices->size() : 256;
  scored.reserve(std::min<size_t>(reserve, 4096));
  std::vector<SwipePoint> key_path;

  const auto score_word = [&](const std::string& word) {
    if (!CandidatePath(word, keyboard_width, keyboard_height, &key_path)) {
      return;
    }
    if (Distance(gesture.front(), key_path.front()) > 0.24 ||
        Distance(gesture.back(), key_path.back()) > 0.24) {
      return;
    }
    const std::vector<SwipePoint> candidate =
        Resample(key_path, kResampledPointCount);
    if (candidate.size() != gesture.size()) return;

    double geometry = 0.0;
    for (size_t i = 0; i < gesture.size(); ++i) {
      const double d = Distance(gesture[i], candidate[i]);
      geometry += d * d;
    }
    geometry /= static_cast<double>(gesture.size());
    geometry += 0.75 * Distance(gesture.front(), candidate.front());
    geometry += 0.75 * Distance(gesture.back(), candidate.back());

    uint32_t rank = 100000U;
    const auto rank_it = vocabulary_rank.find(word);
    if (rank_it != vocabulary_rank.end()) rank = rank_it->second;
    const double frequency =
        1.0 - std::min<double>(rank, 100000.0) / 100000.0;
    const float score = static_cast<float>(frequency * 0.12 - geometry);
    SwipeCandidate item;
    item.word = word;
    item.score = score;
    item.geometry_cost = static_cast<float>(geometry);
    scored.push_back(std::move(item));
  };

  if (candidate_indices) {
    for (const uint32_t index : *candidate_indices) {
      if (index < vocabulary.size()) score_word(vocabulary[index]);
    }
  } else {
    for (const std::string& word : vocabulary) score_word(word);
  }

  const auto better = [](const SwipeCandidate& a, const SwipeCandidate& b) {
    if (a.score != b.score) return a.score > b.score;
    return a.word < b.word;
  };

  // Yandex Swipe/Linear emits TopK=24 into RankingSwipe.  Mirror that contract:
  // geometry screens the endpoint bucket first, then only this compact set pays
  // for the 38 path features and 500-tree ensembles.
  const bool can_rank = options.backoff_ranker &&
      options.backoff_ranker->is_matrixnet_loaded() &&
      options.backoff_ranker->matrixnet_feature_count() == 42;
  const bool can_rank_primary =
      options.primary_ranker && options.primary_ranker->is_matrixnet_loaded() &&
      options.primary_ranker->matrixnet_feature_count() == 47 &&
      options.rnnt_model && options.rnnt_model->components_loaded();
  const bool can_block = options.blocker_ranker &&
      options.blocker_ranker->is_matrixnet_loaded() &&
      options.blocker_ranker->matrixnet_feature_count() == 31;
  const size_t preselect = std::min<size_t>(24, scored.size());
  if (preselect < scored.size()) {
    std::partial_sort(scored.begin(), scored.begin() + preselect, scored.end(),
                      better);
    scored.resize(preselect);
  } else {
    std::sort(scored.begin(), scored.end(), better);
  }
  if ((can_rank || can_rank_primary || can_block) && !scored.empty()) {
    for (SwipeCandidate& candidate : scored) {
      if (!CandidatePath(candidate.word, keyboard_width, keyboard_height,
                         &key_path)) {
        continue;
      }
      uint32_t rank = 100000U;
      const auto rank_it = vocabulary_rank.find(candidate.word);
      if (rank_it != vocabulary_rank.end()) rank = rank_it->second;
      candidate.swipe_backoff_features = BuildSwipeBackoffFeatures(
          candidate.word, rank, normalized_points, key_path, keyboard_width,
          keyboard_height, options.context_size);
    }
  }

  if (can_rank && !scored.empty()) {
    float min_weight = std::numeric_limits<float>::max();
    float min_distance = std::numeric_limits<float>::max();
    float total_weight = 0.0f;
    float min_path_penalty = std::numeric_limits<float>::max();
    for (const SwipeCandidate& candidate : scored) {
      min_weight = std::min(min_weight, candidate.swipe_backoff_features[1]);
      min_distance =
          std::min(min_distance, candidate.swipe_backoff_features[0]);
      total_weight += candidate.swipe_backoff_features[1];
      min_path_penalty =
          std::min(min_path_penalty, candidate.swipe_backoff_features[21]);
    }
    const float mean_weight = total_weight / scored.size();
    for (SwipeCandidate& candidate : scored) {
      std::vector<float> vector(candidate.swipe_backoff_features.begin(),
                                candidate.swipe_backoff_features.end());
      vector.push_back(min_weight);
      vector.push_back(min_distance);
      vector.push_back(mean_weight);
      vector.push_back(min_path_penalty);
      const float model_score = options.backoff_ranker->ScoreMatrixNet(vector);
      if (!std::isfinite(model_score)) continue;
      candidate.matrixnet_score = model_score;
      // The recovered ranker emits an additive score around 0..15.  Keep the
      // geometry decoder as the guardrail while allowing MatrixNet to resolve
      // close paths (the original config uses a small adjustment multiplier).
      candidate.score += 0.01f * model_score;
      if (diagnostics) ++diagnostics->model_scored;
    }
    if (diagnostics) {
      diagnostics->backoff_model_engaged = diagnostics->model_scored > 0;
    }
  }

  if (can_rank_primary && !scored.empty()) {
    const auto rnnt_started = std::chrono::steady_clock::now();
    size_t rnnt_scored = 0;
    size_t rnnt_joint_evaluations_upper_bound = 0;
    size_t rnnt_compute_units_upper_bound = 0;
    const auto rnnt_windows = BuildRussianSwipeRnntWindows(
        raw_points, keyboard_width, keyboard_height);
    std::vector<RnntSwipeModel::EncoderVector> encoder;
    const size_t encoder_compute_units =
        rnnt_windows.size() * kRnntEncoderFrameComputeUnits;
    const bool encoder_ok = !rnnt_windows.empty() &&
                            rnnt_windows.size() <= kMaxRnntEncoderFrames &&
                            encoder_compute_units <=
                                kMaxRnntComputeUnitsPerGesture &&
                            options.rnnt_model->Encode(rnnt_windows, &encoder);
    const auto rnnt_encoded = std::chrono::steady_clock::now();
    if (diagnostics) {
      diagnostics->rnnt_encode_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              rnnt_encoded - rnnt_started)
              .count();
    }
    if (encoder_ok) {
      rnnt_compute_units_upper_bound = encoder_compute_units;
      if (diagnostics) {
        diagnostics->rnnt_compute_units_upper_bound =
            rnnt_compute_units_upper_bound;
      }
    } else if (diagnostics &&
               (rnnt_windows.size() > kMaxRnntEncoderFrames ||
                encoder_compute_units > kMaxRnntComputeUnitsPerGesture)) {
      diagnostics->rnnt_budget_limited = true;
    }
    if (diagnostics) diagnostics->rnnt_frames = encoder.size();
    if (encoder_ok) {
      constexpr size_t kRnntBeamWidth = 6;
      const size_t count = std::min(kRnntBeamWidth, scored.size());
      std::vector<int> tokens;
      for (size_t i = 0; i < count; ++i) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - rnnt_started);
        // Encoding is the dominant fixed cost.  Once it has completed, always
        // score the first geometry candidate so a slow device does not pay the
        // full encoder cost and then discard every RNNT result.  The timeout
        // still bounds every additional beam candidate.
        if (i > 0 && elapsed.count() >= options.rnnt_timeout_ms) {
          if (diagnostics) diagnostics->rnnt_budget_limited = true;
          break;
        }
        if (!RnntTokens(scored[i].word, &tokens)) continue;
        const size_t joint_upper_bound =
            encoder.size() * (tokens.size() + 1);
        const size_t candidate_compute_units =
            joint_upper_bound +
            (tokens.size() + 1) * kRnntPredictorStepComputeUnits;
        if (candidate_compute_units >
            kMaxRnntComputeUnitsPerGesture -
                std::min(rnnt_compute_units_upper_bound,
                         kMaxRnntComputeUnitsPerGesture)) {
          if (diagnostics) diagnostics->rnnt_budget_limited = true;
          break;
        }
        rnnt_joint_evaluations_upper_bound += joint_upper_bound;
        rnnt_compute_units_upper_bound += candidate_compute_units;
        if (diagnostics) {
          diagnostics->rnnt_joint_evaluations_upper_bound =
              rnnt_joint_evaluations_upper_bound;
          diagnostics->rnnt_compute_units_upper_bound =
              rnnt_compute_units_upper_bound;
        }
        float rnnt_score = 0.0f;
        float predictor_score = 0.0f;
        if (!options.rnnt_model->ScoreTokens(
                encoder, tokens, &rnnt_score, &predictor_score)) {
          continue;
        }
        scored[i].rnnt_score = rnnt_score;
        scored[i].rnnt_predictor_score = predictor_score;
        ++rnnt_scored;
        if (diagnostics) {
          ++diagnostics->rnnt_candidates_scored;
          if (diagnostics->rnnt_candidates_scored == 1) {
            diagnostics->first_rnnt_score = rnnt_score;
            diagnostics->first_rnnt_predictor_score = predictor_score;
          }
        }
      }
    }

    if (rnnt_scored > 0) {
      float min_weight = std::numeric_limits<float>::max();
      float min_distance = std::numeric_limits<float>::max();
      float total_weight = 0.0f;
      float min_path_penalty = std::numeric_limits<float>::max();
      for (const SwipeCandidate& candidate : scored) {
        min_weight = std::min(min_weight, candidate.swipe_backoff_features[1]);
        min_distance =
            std::min(min_distance, candidate.swipe_backoff_features[0]);
        total_weight += candidate.swipe_backoff_features[1];
        min_path_penalty =
            std::min(min_path_penalty, candidate.swipe_backoff_features[21]);
      }
      const float mean_weight = total_weight / scored.size();
      for (SwipeCandidate& candidate : scored) {
        if (!std::isfinite(candidate.rnnt_score) ||
            !std::isfinite(candidate.rnnt_predictor_score)) {
          continue;
        }
        const float letters = static_cast<float>(
            std::max<size_t>(1, DecodeUtf8(candidate.word).size()));
        std::vector<float> vector(candidate.swipe_backoff_features.begin(),
                                  candidate.swipe_backoff_features.end());
        vector.push_back(1.0f);  // layout_default
        vector.push_back(candidate.rnnt_score);
        vector.push_back(candidate.rnnt_predictor_score);
        vector.push_back(candidate.rnnt_score / letters);
        vector.push_back(candidate.rnnt_predictor_score / letters);
        vector.push_back(min_weight);
        vector.push_back(min_distance);
        vector.push_back(mean_weight);
        vector.push_back(min_path_penalty);
        const float model_score =
            options.primary_ranker->ScoreMatrixNet(vector);
        if (!std::isfinite(model_score)) continue;
        candidate.matrixnet_score = model_score;
        // Recovered config Swipe.AdjustMultiplier = 0.001.
        candidate.score += 0.001f * model_score;
        if (diagnostics) diagnostics->primary_model_engaged = true;
      }
    }
    if (diagnostics) {
      const auto rnnt_finished = std::chrono::steady_clock::now();
      diagnostics->rnnt_elapsed_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              rnnt_finished - rnnt_started)
              .count();
      diagnostics->rnnt_score_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              rnnt_finished - rnnt_encoded)
              .count();
    }
  }

  std::sort(scored.begin(), scored.end(), better);

  if (can_block && !scored.empty()) {
    const std::vector<float> blocker_features = BuildSwipeBlockerFeatures(
        raw_points, normalized_points, keyboard_width, keyboard_height,
        options.context_size, scored);
    const float blocker_raw =
        options.blocker_ranker->ScoreMatrixNet(blocker_features);
    if (std::isfinite(blocker_raw)) {
      const float blocker_probability = Sigmoid(blocker_raw);
      if (diagnostics) {
        diagnostics->blocker_model_engaged = true;
        diagnostics->blocker_score = blocker_probability;
      }
      // Yandex config: Swipe/Blocker classifier Threshold = 0.5.
      if (options.enforce_blocker && blocker_probability >= 0.5f) {
        if (diagnostics) diagnostics->blocked = true;
        scored.clear();
        return scored;
      }
    }
  }

  const size_t selected = std::min(options.max_results, scored.size());
  scored.resize(selected);
  if (diagnostics && !scored.empty()) {
    diagnostics->top_matrixnet_score = scored.front().matrixnet_score;
  }
  return scored;
}

}  // namespace

bool RussianSwipeKeyCenter(char32_t codepoint, double keyboard_width,
                           double keyboard_height, SwipePoint* out) {
  if (!out || keyboard_width <= 0.0 || keyboard_height <= 0.0) return false;
  if (codepoint == U'ё') codepoint = U'е';
  constexpr char32_t row0[] = U"йцукенгшщзх";
  constexpr char32_t row1[] = U"фывапролджэ";
  constexpr char32_t row2[] = U"ячсмитьбю";

  int row = -1;
  double x = 0.0;
  for (size_t i = 0; i + 1 < std::size(row0); ++i) {
    if (row0[i] == codepoint) {
      row = 0;
      x = (static_cast<double>(i) + 0.5) / 11.0;
      break;
    }
  }
  if (row < 0) {
    for (size_t i = 0; i + 1 < std::size(row1); ++i) {
      if (row1[i] == codepoint) {
        row = 1;
        x = (static_cast<double>(i) + 0.5) / 11.0;
        break;
      }
    }
  }
  if (row < 0) {
    for (size_t i = 0; i + 1 < std::size(row2); ++i) {
      if (row2[i] == codepoint) {
        row = 2;
        // Third row has 1.5-flex Shift and Backspace around nine letters.
        x = (1.5 + static_cast<double>(i) + 0.5) / 12.0;
        break;
      }
    }
  }
  if (row < 0) return false;

  const double top = keyboard_height * 0.18;
  const double rows_height = keyboard_height - top;
  out->x = x * keyboard_width;
  out->y = top + (static_cast<double>(row) + 0.5) * rows_height / 4.0;
  return true;
}

std::vector<SwipeRnntFeatureWindow> BuildRussianSwipeRnntWindows(
    const std::vector<SwipePoint>& points, double keyboard_width,
    double keyboard_height) {
  static_assert(SwipeRnntFeatureWindow{}.size() ==
                RnntSwipeModel::FeatureWindow{}.size());
  if (points.size() < kRnntWindowSize || keyboard_width <= 0.0 ||
      keyboard_height <= 0.0) {
    return {};
  }

  std::vector<SwipePoint> normalized;
  normalized.reserve(points.size());
  const uint32_t start_time = points.front().time_ms;
  for (const SwipePoint& point : points) {
    SwipePoint value = NormalizePanelPoint(point, keyboard_width,
                                           keyboard_height);
    value.time_ms = point.time_ms - start_time;
    normalized.push_back(value);
  }
  const std::vector<SwipePoint> sampled = RnntSample(normalized);
  if (sampled.size() < kRnntWindowSize) return {};

  std::vector<std::array<float, kRnntFeatureCount>> features(sampled.size());
  for (size_t i = 0; i < sampled.size(); ++i) {
    const SwipePoint& previous = i == 0 ? sampled[i] : sampled[i - 1];
    features[i] =
        BuildRnntPointFeatures(sampled[i], previous, i + 1 == sampled.size());
  }

  std::vector<SwipeRnntFeatureWindow> windows;
  windows.reserve(sampled.size() - kRnntWindowSize + 1);
  for (size_t begin = 0; begin + kRnntWindowSize <= features.size(); ++begin) {
    SwipeRnntFeatureWindow window{};
    for (size_t row = 0; row < kRnntWindowSize; ++row) {
      std::copy(features[begin + row].begin(), features[begin + row].end(),
                window.begin() + row * kRnntFeatureCount);
    }
    windows.push_back(window);
  }
  return windows;
}

void RussianSwipeDecoder::Reset() {
  vocabulary_ = nullptr;
  vocabulary_rank_ = nullptr;
  endpoint_buckets_.clear();
  indexed_word_count_ = 0;
}

void RussianSwipeDecoder::Build(
    const std::vector<std::string>& vocabulary,
    const std::unordered_map<std::string, uint32_t>& vocabulary_rank) {
  Reset();
  vocabulary_ = &vocabulary;
  vocabulary_rank_ = &vocabulary_rank;
  endpoint_buckets_.resize(kRussianKeyCount * kRussianKeyCount);

  for (size_t i = 0; i < vocabulary.size(); ++i) {
    const std::vector<char32_t> codepoints = DecodeUtf8(vocabulary[i]);
    if (codepoints.size() < 2 || codepoints.size() > 24) continue;
    bool valid = true;
    for (const char32_t codepoint : codepoints) {
      if (RussianSwipeKeyIndex(codepoint) < 0) {
        valid = false;
        break;
      }
    }
    if (!valid) continue;
    const int first = RussianSwipeKeyIndex(codepoints.front());
    const int last = RussianSwipeKeyIndex(codepoints.back());
    if (first < 0 || last < 0) continue;
    endpoint_buckets_[static_cast<size_t>(first) * kRussianKeyCount +
                      static_cast<size_t>(last)]
        .push_back(static_cast<uint32_t>(i));
    ++indexed_word_count_;
  }
}

std::vector<SwipeCandidate> RussianSwipeDecoder::Decode(
    const std::vector<SwipePoint>& points, double keyboard_width,
    double keyboard_height, size_t max_results,
    size_t* candidate_pool_size) const {
  SwipeDecodeOptions options;
  options.max_results = max_results;
  SwipeDecodeDiagnostics diagnostics;
  std::vector<SwipeCandidate> result =
      Decode(points, keyboard_width, keyboard_height, options, &diagnostics);
  if (candidate_pool_size) {
    *candidate_pool_size = diagnostics.candidate_pool_size;
  }
  return result;
}

std::vector<SwipeCandidate> RussianSwipeDecoder::Decode(
    const std::vector<SwipePoint>& points, double keyboard_width,
    double keyboard_height, const SwipeDecodeOptions& options,
    SwipeDecodeDiagnostics* diagnostics) const {
  if (diagnostics) *diagnostics = SwipeDecodeDiagnostics{};
  if (!is_ready() || points.size() < 3 || keyboard_width <= 0.0 ||
      keyboard_height <= 0.0 || options.max_results == 0) {
    return {};
  }

  std::vector<SwipePoint> normalized;
  normalized.reserve(points.size());
  for (const SwipePoint& point : points) {
    normalized.push_back(
        NormalizePanelPoint(point, keyboard_width, keyboard_height));
  }
  const std::vector<SwipePoint> gesture =
      Resample(normalized, kResampledPointCount);
  if (gesture.empty() || Distance(gesture.front(), gesture.back()) < 0.04) {
    return {};
  }

  const int first = NearestRussianSwipeKey(gesture.front());
  const int last = NearestRussianSwipeKey(gesture.back());
  if (first < 0 || last < 0) return {};
  const auto& pool =
      endpoint_buckets_[static_cast<size_t>(first) * kRussianKeyCount +
                        static_cast<size_t>(last)];
  if (diagnostics) diagnostics->candidate_pool_size = pool.size();
  return ScoreCandidates(points, normalized, gesture, keyboard_width,
                         keyboard_height, *vocabulary_, *vocabulary_rank_,
                         &pool, options, diagnostics);
}

std::vector<SwipeCandidate> DecodeRussianSwipe(
    const std::vector<SwipePoint>& points, double keyboard_width,
    double keyboard_height, const std::vector<std::string>& vocabulary,
    const std::unordered_map<std::string, uint32_t>& vocabulary_rank,
    size_t max_results) {
  std::vector<SwipeCandidate> result;
  if (points.size() < 3 || keyboard_width <= 0.0 || keyboard_height <= 0.0 ||
      vocabulary.empty() || max_results == 0) {
    return result;
  }

  std::vector<SwipePoint> normalized;
  normalized.reserve(points.size());
  for (const SwipePoint& point : points) {
    normalized.push_back(
        NormalizePanelPoint(point, keyboard_width, keyboard_height));
  }
  const std::vector<SwipePoint> gesture =
      Resample(normalized, kResampledPointCount);
  if (gesture.empty() || Distance(gesture.front(), gesture.back()) < 0.04) {
    return result;
  }

  SwipeDecodeOptions options;
  options.max_results = max_results;
  return ScoreCandidates(points, normalized, gesture, keyboard_width,
                         keyboard_height, vocabulary, vocabulary_rank, nullptr,
                         options, nullptr);
}

}  // namespace hoskey::engine
