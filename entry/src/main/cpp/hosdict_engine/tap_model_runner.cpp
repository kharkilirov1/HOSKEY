#include "tap_model_runner.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <sstream>
#include <string_view>
#include <utility>

namespace hoskey::engine {
namespace {

constexpr uint32_t kTouchPhaseMask = 0xF0000000u;
constexpr uint32_t kTouchKeyCodeMask = 0x0FFFFFFFu;
constexpr uint32_t kTouchKeyCodeBackspace = 8u;
constexpr uint32_t kTouchKeyCodeEnter = 13u;
constexpr uint32_t kTouchKeyCodeSpace = 32u;

constexpr std::array<std::string_view, 33> kExpectedFeatures = {
    "tap_log_likelihood_unigram",
    "tap_log_likelihood_ngram",
    "char_lm_mod",
    "word_lm_1",
    "word_lm_cond_1",
    "neural_char_lm_n",
    "neural_char_lm_cond_n",
    "neural_char_lm_cond_log_n",
    "neural_char_lm_per_letter_n",
    "context_prefix_len",
    "context_last_word_len",
    "prior_probability_static",
    "prior_probability",
    "distance_transfeme_original_correction_m",
    "distance_transfeme_correction_original_m",
    "tap_x",
    "tap_y_m",
    "tap_key_x",
    "letter_x",
    "letter_y_m",
    "letter_key_x",
    "tap_distance_key_x",
    "tap_distance_x",
    "tap_distance_key_y",
    "tap_distance_y_m",
    "prev_tap_distance_letter_key_x",
    "prev_tap_distance_letter_key_y",
    "is_same",
    "tap_distance_key_rel",
    "tap_distance_key_rel_squared",
    "neural_word_lm_cond_max",
    "neural_word_lm_cond_sum",
    "neural_word_lm_cond_avg",
};

constexpr std::array<std::string_view, 10> kExpectedStatsStrings = {
    "max", "tap_log_likelihood_ngram",
    "max", "neural_char_lm_cond_n",
    "max", "tap_distance_key_rel",
    "max", "neural_char_lm_per_letter_n",
    "max", "neural_word_lm_cond_max",
};

struct Codepoint {
  char32_t value = 0;
  std::string utf8;
};

struct ModelPoint {
  double x = 0.0;
  double y = 0.0;
};

struct DownTouch {
  TapModelTouchPoint point;
  char32_t codepoint = 0;
};

bool AllFinite(const TapModelSemanticFeatures& values) {
  const float fields[] = {
      values.char_lm_mod,
      values.word_lm_1,
      values.word_lm_cond_1,
      values.neural_char_lm_n,
      values.neural_char_lm_cond_n,
      values.neural_char_lm_cond_log_n,
      values.neural_char_lm_per_letter_n,
      values.context_prefix_len,
      values.context_last_word_len,
      values.distance_transfeme_original_correction_m,
      values.distance_transfeme_correction_original_m,
      values.neural_word_lm_cond_max,
      values.neural_word_lm_cond_sum,
      values.neural_word_lm_cond_avg,
  };
  return std::all_of(std::begin(fields), std::end(fields),
                     [](float value) { return std::isfinite(value); });
}

Status DecodeUtf8Strict(std::string_view text, std::vector<Codepoint>* output) {
  if (!output) return Status::InvalidArgument("TapModel null UTF-8 output");
  output->clear();
  size_t i = 0;
  while (i < text.size()) {
    const size_t start = i;
    const uint8_t first = static_cast<uint8_t>(text[i++]);
    char32_t codepoint = 0;
    unsigned continuation = 0;
    if (first < 0x80u) {
      if (first == 0) return Status::InvalidArgument("TapModel NUL token");
      codepoint = first;
    } else if ((first & 0xE0u) == 0xC0u) {
      continuation = 1;
      codepoint = first & 0x1Fu;
      if (codepoint < 2) return Status::InvalidArgument("TapModel overlong UTF-8");
    } else if ((first & 0xF0u) == 0xE0u) {
      continuation = 2;
      codepoint = first & 0x0Fu;
    } else if ((first & 0xF8u) == 0xF0u) {
      continuation = 3;
      codepoint = first & 0x07u;
    } else {
      return Status::InvalidArgument("TapModel invalid UTF-8 lead byte");
    }
    if (continuation > text.size() - i) {
      return Status::InvalidArgument("TapModel truncated UTF-8");
    }
    for (unsigned n = 0; n < continuation; ++n) {
      const uint8_t byte = static_cast<uint8_t>(text[i++]);
      if ((byte & 0xC0u) != 0x80u) {
        return Status::InvalidArgument("TapModel invalid UTF-8 continuation");
      }
      codepoint = (codepoint << 6) | (byte & 0x3Fu);
    }
    if ((continuation == 2 && codepoint < 0x800u) ||
        (continuation == 3 && codepoint < 0x10000u) ||
        (codepoint >= 0xD800u && codepoint <= 0xDFFFu) ||
        codepoint > 0x10FFFFu) {
      return Status::InvalidArgument("TapModel invalid UTF-8 scalar");
    }
    output->push_back(
        {codepoint, std::string(text.substr(start, i - start))});
  }
  if (output->empty()) {
    return Status::InvalidArgument("TapModel empty token");
  }
  return Status::Ok();
}

char32_t LowerRussian(char32_t value) {
  if (value >= U'А' && value <= U'Я') return value + (U'а' - U'А');
  if (value == U'Ё') return U'ё';
  return value;
}

Status ExtractDownTouches(const std::vector<TapModelTouchPoint>& points,
                          uint32_t width, uint32_t height,
                          std::vector<DownTouch>* output) {
  if (!output || width == 0 || height == 0) {
    return Status::InvalidArgument("TapModel invalid touch geometry");
  }
  output->clear();
  for (const TapModelTouchPoint& point : points) {
    const uint32_t phase = point.key_code & kTouchPhaseMask;
    if (phase != 0) continue;
    const uint32_t code = point.key_code & kTouchKeyCodeMask;
    if (code == 0) continue;
    if (code == kTouchKeyCodeBackspace) {
      if (!output->empty()) output->pop_back();
      continue;
    }
    if (code == kTouchKeyCodeSpace || code == kTouchKeyCodeEnter) {
      output->clear();
      continue;
    }
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || point.x < 0.0 ||
        point.y < 0.0 || point.x > static_cast<double>(width) ||
        point.y > static_cast<double>(height) || code > 0x10FFFFu) {
      return Status::InvalidArgument("TapModel touch outside keyboard");
    }
    output->push_back({point, LowerRussian(static_cast<char32_t>(code))});
  }
  if (output->empty()) {
    return Status::InvalidArgument("TapModel has no key-down touches");
  }
  return Status::Ok();
}

bool RussianKeyGeometry(char32_t codepoint, double keyboard_width,
                        double keyboard_height, ModelPoint* center,
                        double* key_width, double* key_height) {
  if (!center || !key_width || !key_height || keyboard_width <= 0.0 ||
      keyboard_height <= 0.0) {
    return false;
  }
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
      *key_width = keyboard_width / 11.0;
      break;
    }
  }
  if (row < 0) {
    for (size_t i = 0; i + 1 < std::size(row1); ++i) {
      if (row1[i] == codepoint) {
        row = 1;
        x = (static_cast<double>(i) + 0.5) / 11.0;
        *key_width = keyboard_width / 11.0;
        break;
      }
    }
  }
  if (row < 0) {
    for (size_t i = 0; i + 1 < std::size(row2); ++i) {
      if (row2[i] == codepoint) {
        row = 2;
        x = (1.5 + static_cast<double>(i) + 0.5) / 12.0;
        *key_width = keyboard_width / 12.0;
        break;
      }
    }
  }
  if (row < 0) return false;
  const double top = keyboard_height * 0.18;
  const double rows_height = keyboard_height - top;
  *key_height = rows_height / 4.0;
  center->x = x * keyboard_width;
  center->y = top + (static_cast<double>(row) + 0.5) * *key_height;
  return true;
}

Status MapTouchToModel(const TapModelTouchPoint& touch, char32_t typed,
                       uint32_t keyboard_width, uint32_t keyboard_height,
                       const StaticTapModel& model,
                       const TapKeyParams& typed_params, ModelPoint* output) {
  ModelPoint keyboard_center;
  double keyboard_key_width = 0.0;
  double keyboard_key_height = 0.0;
  if (!RussianKeyGeometry(typed, keyboard_width, keyboard_height,
                          &keyboard_center, &keyboard_key_width,
                          &keyboard_key_height)) {
    return Status::InvalidArgument("TapModel unsupported keyboard key");
  }
  const double model_center_x =
      static_cast<double>(typed_params.x) + typed_params.width * 0.5;
  const double model_center_y =
      static_cast<double>(typed_params.y) + typed_params.height * 0.5;
  output->x = model_center_x +
              (touch.x - keyboard_center.x) *
                  (typed_params.width / keyboard_key_width);
  output->y = model_center_y +
              (touch.y - keyboard_center.y) *
                  (typed_params.height / keyboard_key_height);
  if (!std::isfinite(output->x) || !std::isfinite(output->y) ||
      output->x < 0.0 || output->y < 0.0 ||
      output->x > model.width || output->y > model.height) {
    return Status::InvalidArgument("TapModel mapped touch outside model");
  }
  return Status::Ok();
}

double GaussianLogLikelihoodLog2(const TapKeyParams& params,
                                 const ModelPoint& point) {
  if (!(params.variance_x > 0.0f) || !(params.variance_y > 0.0f) ||
      !std::isfinite(params.correlation) ||
      std::abs(params.correlation) >= 1.0f) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  const double sigma_x = std::sqrt(static_cast<double>(params.variance_x));
  const double sigma_y = std::sqrt(static_cast<double>(params.variance_y));
  const double rho = params.correlation;
  const double one_minus_rho2 = 1.0 - rho * rho;
  const double dx = point.x - params.mean_x;
  const double dy = point.y - params.mean_y;
  const double quadratic =
      dx * dx / params.variance_x + dy * dy / params.variance_y -
      2.0 * rho * dx * dy / (sigma_x * sigma_y);
  constexpr double kTwoPi = 6.283185307179586476925286766559;
  constexpr double kLn2 = 0.693147180559945309417232121458;
  const double log_probability =
      -quadratic / (2.0 * one_minus_rho2) -
      std::log(kTwoPi * sigma_x * sigma_y * std::sqrt(one_minus_rho2));
  return log_probability / kLn2;
}

Status ExtractJsonArrayStrings(std::string_view json, std::string_view key,
                               std::vector<std::string>* output,
                               bool* structurally_empty = nullptr) {
  output->clear();
  const std::string needle = "\"" + std::string(key) + "\"";
  size_t position = json.find(needle);
  if (position == std::string_view::npos) {
    return Status::ParseError("TapModel feature schema missing " +
                              std::string(key));
  }
  position = json.find('[', position + needle.size());
  if (position == std::string_view::npos) {
    return Status::ParseError("TapModel feature schema invalid array");
  }
  const size_t begin = position++;
  int depth = 1;
  bool in_string = false;
  std::string current;
  bool saw_non_space_structure = false;
  while (position < json.size() && depth > 0) {
    const char ch = json[position++];
    if (in_string) {
      if (ch == '\\') {
        return Status::ParseError("TapModel schema escapes are unsupported");
      }
      if (ch == '"') {
        in_string = false;
        output->push_back(std::move(current));
        current.clear();
      } else {
        current.push_back(ch);
      }
      continue;
    }
    if (ch == '"') {
      in_string = true;
    } else if (ch == '[') {
      ++depth;
      saw_non_space_structure = true;
    } else if (ch == ']') {
      --depth;
    } else if (!std::isspace(static_cast<unsigned char>(ch)) && ch != ',') {
      saw_non_space_structure = true;
    }
  }
  if (depth != 0 || in_string) {
    return Status::ParseError("TapModel unterminated feature schema array");
  }
  if (structurally_empty) {
    *structurally_empty = output->empty() && !saw_non_space_structure &&
                          position == begin + 2;
  }
  return Status::Ok();
}

Status ValidateFeatureSchema(const uint8_t* data, size_t size) {
  if (!data || size == 0) {
    return Status::InvalidArgument("TapModel feature schema is empty");
  }
  const std::string_view json(reinterpret_cast<const char*>(data), size);
  std::vector<std::string> values;
  Status status = ExtractJsonArrayStrings(json, "Features", &values);
  if (!status.ok()) return status;
  if (values.size() != kExpectedFeatures.size()) {
    return Status::ParseError("TapModel base feature count is not 33");
  }
  for (size_t i = 0; i < values.size(); ++i) {
    if (values[i] != kExpectedFeatures[i]) {
      return Status::ParseError("TapModel base feature order mismatch");
    }
  }
  bool combos_empty = false;
  status = ExtractJsonArrayStrings(json, "Combos", &values, &combos_empty);
  if (!status.ok()) return status;
  if (!values.empty()) {
    return Status::ParseError("TapModel unexpected feature combos");
  }
  status = ExtractJsonArrayStrings(json, "Stats", &values);
  if (!status.ok()) return status;
  if (values.size() != kExpectedStatsStrings.size()) {
    return Status::ParseError("TapModel stats feature count is not five");
  }
  for (size_t i = 0; i < values.size(); ++i) {
    if (values[i] != kExpectedStatsStrings[i]) {
      return Status::ParseError("TapModel stats feature order mismatch");
    }
  }
  return Status::Ok();
}

void FillSemantic(const TapModelSemanticFeatures& input,
                  std::array<float, TapModelRunner::kRankerFeatureCount>* out) {
  (*out)[2] = input.char_lm_mod;
  (*out)[3] = input.word_lm_1;
  (*out)[4] = input.word_lm_cond_1;
  (*out)[5] = input.neural_char_lm_n;
  (*out)[6] = input.neural_char_lm_cond_n;
  (*out)[7] = input.neural_char_lm_cond_log_n;
  (*out)[8] = input.neural_char_lm_per_letter_n;
  (*out)[9] = input.context_prefix_len;
  (*out)[10] = input.context_last_word_len;
  (*out)[13] = input.distance_transfeme_original_correction_m;
  (*out)[14] = input.distance_transfeme_correction_original_m;
  (*out)[30] = input.neural_word_lm_cond_max;
  (*out)[31] = input.neural_word_lm_cond_sum;
  (*out)[32] = input.neural_word_lm_cond_avg;
}

Status BuildCandidateBase(
    const TapModelBundle& bundle, const StaticTapModel& layout,
    const std::vector<Codepoint>& typed,
    const std::vector<Codepoint>& candidate,
    const std::vector<DownTouch>& touches,
    const std::vector<ModelPoint>& mapped_touches, size_t focus,
    const TapModelSemanticFeatures& semantics, uint32_t keyboard_width,
    uint32_t keyboard_height,
    std::array<float, TapModelRunner::kRankerFeatureCount>* features) {
  (void)keyboard_width;
  (void)keyboard_height;
  features->fill(0.0f);
  FillSemantic(semantics, features);
  const TapKeyParams* typed_params = bundle.FindKey(layout, typed[focus].utf8);
  const TapKeyParams* candidate_params =
      bundle.FindKey(layout, candidate[focus].utf8);
  if (!typed_params || !candidate_params) {
    return Status::InvalidArgument("TapModel candidate key absent from layout");
  }
  const ModelPoint& tap = mapped_touches[focus];
  const double unigram = GaussianLogLikelihoodLog2(*candidate_params, tap);
  if (!std::isfinite(unigram)) {
    return Status::InvalidArgument("TapModel invalid unigram Gaussian");
  }
  (*features)[0] = static_cast<float>(unigram);
  const TapKeyParams* ngram_params = candidate_params;
  if (focus > 0) {
    const std::string ngram_key =
        candidate[focus - 1].utf8 + candidate[focus].utf8;
    const TapKeyParams* found = bundle.FindKey(layout, ngram_key);
    if (found) ngram_params = found;
  }
  const double ngram = GaussianLogLikelihoodLog2(*ngram_params, tap);
  if (!std::isfinite(ngram)) {
    return Status::InvalidArgument("TapModel invalid ngram Gaussian");
  }
  (*features)[1] = static_cast<float>(ngram);
  (*features)[11] = typed_params->log_prior;
  (*features)[12] = ngram_params->log_prior;

  const double model_width = layout.width;
  const double model_height = layout.height;
  const double model_key_width =
      std::max(1.0, std::floor(model_width / 11.0));
  const double candidate_x =
      static_cast<double>(candidate_params->x) + candidate_params->width * 0.5;
  const double candidate_y =
      static_cast<double>(candidate_params->y) + candidate_params->height * 0.5;
  const double dx = candidate_x - tap.x;
  const double dy = candidate_y - tap.y;
  const double dx_key = dx / std::max<uint32_t>(candidate_params->width, 1u);
  const double dy_key = dy / std::max<uint32_t>(candidate_params->height, 1u);
  (*features)[15] = static_cast<float>(tap.x / model_width);
  (*features)[16] = static_cast<float>((model_height - tap.y) / model_height);
  (*features)[17] = static_cast<float>(tap.x / model_key_width);
  (*features)[18] = static_cast<float>(candidate_x / model_width);
  (*features)[19] =
      static_cast<float>((model_height - candidate_y) / model_height);
  (*features)[20] = static_cast<float>(candidate_x / model_key_width);
  (*features)[21] = static_cast<float>(dx_key);
  (*features)[22] = static_cast<float>(dx / model_width);
  (*features)[23] = static_cast<float>(dy_key);
  (*features)[24] = static_cast<float>(dy / model_height);
  if (focus > 0) {
    const ModelPoint& previous_tap = mapped_touches[focus - 1];
    (*features)[25] = static_cast<float>(
        (candidate_x - previous_tap.x) /
        std::max<uint32_t>(candidate_params->width, 1u));
    (*features)[26] = static_cast<float>(
        (candidate_y - previous_tap.y) /
        std::max<uint32_t>(candidate_params->height, 1u));
  }
  (*features)[27] =
      candidate[focus].value == typed[focus].value ? 1.0f : 0.0f;
  const double relative = std::sqrt(dx_key * dx_key + dy_key * dy_key);
  (*features)[28] = static_cast<float>(relative);
  (*features)[29] = static_cast<float>(relative * relative);
  if (!std::all_of(features->begin(), features->begin() + 33,
                   [](float value) { return std::isfinite(value); })) {
    return Status::InvalidArgument("TapModel generated non-finite feature");
  }
  return Status::Ok();
}

void AppendPairStats(
    std::array<float, TapModelRunner::kRankerFeatureCount>* original,
    std::array<float, TapModelRunner::kRankerFeatureCount>* correction) {
  const auto set_max = [&](size_t output, size_t input) {
    const float value = std::max((*original)[input], (*correction)[input]);
    (*original)[output] = value;
    (*correction)[output] = value;
  };
  set_max(33, 1);
  set_max(34, 6);
  set_max(35, 28);
  set_max(36, 8);
  set_max(37, 30);
}

}  // namespace

Status TapModelRunner::Load(const TapModelAssets& assets) {
  TapModelBundle bundle;
  Status status = bundle.Load(assets.bundle.data, assets.bundle.size);
  if (!status.ok()) return status;
  status = ValidateFeatureSchema(assets.feature_schema.data,
                                 assets.feature_schema.size);
  if (!status.ok()) return status;
  ObliviousTreeModel ranker;
  status = ranker.Load(assets.ranker.data, assets.ranker.size, "tap_default_1225");
  if (!status.ok()) return status;
  if (ranker.feature_count() != kRankerFeatureCount) {
    return Status::ParseError("TapModel ranker feature count is not 38");
  }
  ObliviousTreeModel classifier;
  status = classifier.Load(assets.classifier.data, assets.classifier.size,
                           "tap_default_1225");
  if (!status.ok()) return status;
  if (classifier.feature_count() != kClassifierFeatureCount) {
    return Status::ParseError("TapModel classifier feature count is not 78");
  }

  std::lock_guard<std::mutex> lock(score_mutex_);
  bundle_ = std::move(bundle);
  ranker_ = std::move(ranker);
  classifier_ = std::move(classifier);
  loaded_ = true;
  ranker_invocation_count_.store(0, std::memory_order_relaxed);
  ranker_scored_count_.store(0, std::memory_order_relaxed);
  classifier_invocation_count_.store(0, std::memory_order_relaxed);
  classifier_scored_count_.store(0, std::memory_order_relaxed);
  rejected_count_.store(0, std::memory_order_relaxed);
  return Status::Ok();
}

void TapModelRunner::Reset() {
  std::lock_guard<std::mutex> lock(score_mutex_);
  loaded_ = false;
  bundle_.Reset();
  ranker_.Reset();
  classifier_.Reset();
  ranker_invocation_count_.store(0, std::memory_order_relaxed);
  ranker_scored_count_.store(0, std::memory_order_relaxed);
  classifier_invocation_count_.store(0, std::memory_order_relaxed);
  classifier_scored_count_.store(0, std::memory_order_relaxed);
  rejected_count_.store(0, std::memory_order_relaxed);
}

Status TapModelRunner::BuildPairFeatures(
    const TapModelPairRequest& request,
    std::array<float, kRankerFeatureCount>* original,
    std::array<float, kRankerFeatureCount>* correction) const {
  if (!loaded_ || !original || !correction) {
    return Status::InvalidState("TapModel runner not loaded/null output");
  }
  if (!AllFinite(request.original_semantics) ||
      !AllFinite(request.correction_semantics)) {
    return Status::InvalidArgument("TapModel non-finite semantic feature");
  }
  const StaticTapModel* layout = bundle_.FindLayout(request.layout);
  if (!layout) {
    return Status::InvalidArgument("TapModel unsupported layout");
  }
  std::vector<Codepoint> typed;
  std::vector<Codepoint> candidate;
  Status status = DecodeUtf8Strict(request.original, &typed);
  if (!status.ok()) return status;
  status = DecodeUtf8Strict(request.correction, &candidate);
  if (!status.ok()) return status;
  if (typed.size() != candidate.size()) {
    return Status::InvalidArgument(
        "TapModel insertion/deletion alignment unavailable");
  }
  size_t focus = typed.size();
  for (size_t i = 0; i < typed.size(); ++i) {
    typed[i].value = LowerRussian(typed[i].value);
    candidate[i].value = LowerRussian(candidate[i].value);
    if (focus == typed.size() && typed[i].value != candidate[i].value) {
      focus = i;
    }
  }
  if (focus == typed.size()) {
    return Status::InvalidArgument("TapModel correction equals original");
  }

  std::vector<DownTouch> touches;
  status = ExtractDownTouches(request.touches, request.keyboard_width,
                              request.keyboard_height, &touches);
  if (!status.ok()) return status;
  if (touches.size() != typed.size()) {
    return Status::InvalidArgument("TapModel missing/mixed touch sequence");
  }
  std::vector<ModelPoint> mapped(touches.size());
  for (size_t i = 0; i < touches.size(); ++i) {
    if (touches[i].codepoint != typed[i].value) {
      return Status::InvalidArgument("TapModel touch/token mismatch");
    }
    const TapKeyParams* params = bundle_.FindKey(*layout, typed[i].utf8);
    if (!params) {
      return Status::InvalidArgument("TapModel typed key absent from layout");
    }
    status = MapTouchToModel(touches[i].point, typed[i].value,
                             request.keyboard_width, request.keyboard_height,
                             *layout, *params, &mapped[i]);
    if (!status.ok()) return status;
  }
  status = BuildCandidateBase(bundle_, *layout, typed, typed, touches, mapped,
                              focus, request.original_semantics,
                              request.keyboard_width, request.keyboard_height,
                              original);
  if (!status.ok()) return status;
  status = BuildCandidateBase(bundle_, *layout, typed, candidate, touches,
                              mapped, focus, request.correction_semantics,
                              request.keyboard_width, request.keyboard_height,
                              correction);
  if (!status.ok()) return status;
  AppendPairStats(original, correction);
  return Status::Ok();
}

double TapModelRunner::ScoreRankerFeatures(
    const std::array<float, kRankerFeatureCount>& features) const {
  ranker_invocation_count_.fetch_add(1, std::memory_order_relaxed);
  if (!loaded_ || !std::all_of(features.begin(), features.end(),
                               [](float value) { return std::isfinite(value); })) {
    rejected_count_.fetch_add(1, std::memory_order_relaxed);
    return std::numeric_limits<double>::quiet_NaN();
  }
  const std::vector<float> vector(features.begin(), features.end());
  std::lock_guard<std::mutex> lock(score_mutex_);
  const double score = ranker_.ScoreProbability(vector);
  if (std::isfinite(score)) {
    ranker_scored_count_.fetch_add(1, std::memory_order_relaxed);
  } else {
    rejected_count_.fetch_add(1, std::memory_order_relaxed);
  }
  return score;
}

double TapModelRunner::ScoreClassifierFeatures(
    double original_score,
    const std::array<float, kRankerFeatureCount>& original,
    double correction_score,
    const std::array<float, kRankerFeatureCount>& correction) const {
  classifier_invocation_count_.fetch_add(1, std::memory_order_relaxed);
  if (!loaded_ || !std::isfinite(original_score) ||
      !std::isfinite(correction_score) ||
      !std::all_of(original.begin(), original.end(),
                   [](float value) { return std::isfinite(value); }) ||
      !std::all_of(correction.begin(), correction.end(),
                   [](float value) { return std::isfinite(value); })) {
    rejected_count_.fetch_add(1, std::memory_order_relaxed);
    return std::numeric_limits<double>::quiet_NaN();
  }
  std::vector<float> features;
  features.reserve(kClassifierFeatureCount);
  features.push_back(static_cast<float>(original_score));
  features.insert(features.end(), original.begin(), original.end());
  features.push_back(static_cast<float>(correction_score));
  features.insert(features.end(), correction.begin(), correction.end());
  std::lock_guard<std::mutex> lock(score_mutex_);
  const double score = classifier_.ScoreProbability(features);
  if (std::isfinite(score)) {
    classifier_scored_count_.fetch_add(1, std::memory_order_relaxed);
  } else {
    rejected_count_.fetch_add(1, std::memory_order_relaxed);
  }
  return score;
}

Status TapModelRunner::EvaluatePair(const TapModelPairRequest& request,
                                    TapModelPairResult* result) const {
  if (!result) return Status::InvalidArgument("TapModel null pair result");
  TapModelPairResult evaluated;
  Status status = BuildPairFeatures(request, &evaluated.original_features,
                                    &evaluated.correction_features);
  if (!status.ok()) {
    rejected_count_.fetch_add(1, std::memory_order_relaxed);
    return status;
  }
  evaluated.original_rank_score =
      ScoreRankerFeatures(evaluated.original_features);
  evaluated.correction_rank_score =
      ScoreRankerFeatures(evaluated.correction_features);
  evaluated.classifier_probability = ScoreClassifierFeatures(
      evaluated.original_rank_score, evaluated.original_features,
      evaluated.correction_rank_score, evaluated.correction_features);
  evaluated.model_engaged =
      std::isfinite(evaluated.original_rank_score) &&
      std::isfinite(evaluated.correction_rank_score) &&
      std::isfinite(evaluated.classifier_probability);
  if (!evaluated.model_engaged) {
    return Status::InvalidState("TapModel inference failed");
  }
  evaluated.should_autocorrect =
      evaluated.correction_rank_score > evaluated.original_rank_score &&
      evaluated.classifier_probability >= kClassifierThreshold;
  evaluated.diagnostics =
      "engaged=1,rank_original=" +
      std::to_string(evaluated.original_rank_score) +
      ",rank_correction=" + std::to_string(evaluated.correction_rank_score) +
      ",classifier=" + std::to_string(evaluated.classifier_probability) +
      ",accept=" + (evaluated.should_autocorrect ? "1" : "0");
  *result = std::move(evaluated);
  return Status::Ok();
}

std::string TapModelRunner::diagnostics() const {
  std::ostringstream output;
  output << "asset=" << (loaded_ ? "loaded" : "missing")
         << ",contract=" << (loaded_ ? "exact_33_plus_5_2n_plus_2"
                                      : "not_loaded")
         << ",variant=default_1225"
         << ",ranker_invoke_count=" << ranker_invocation_count()
         << ",ranker_scored_count=" << ranker_scored_count()
         << ",classifier_invoke_count=" << classifier_invocation_count()
         << ",classifier_scored_count=" << classifier_scored_count()
         << ",rejected_count="
         << rejected_count_.load(std::memory_order_relaxed)
         << ",layouts=" << layout_count() << ",keys=" << key_count();
  return output.str();
}

}  // namespace hoskey::engine
