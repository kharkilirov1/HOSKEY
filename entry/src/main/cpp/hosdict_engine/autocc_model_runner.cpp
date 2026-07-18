#include "autocc_model_runner.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hoskey::engine {
namespace {

constexpr size_t kExactSchemaSize = 4718;
constexpr uint64_t kExactSchemaFnv1a64 = 0x392666aa7a7a479aULL;

size_t VariantIndex(AutoccVariant variant) {
  return static_cast<size_t>(variant);
}

bool IsValidVariant(AutoccVariant variant) {
  return VariantIndex(variant) < 3;
}

Status ValidateFeatureSchema(const AutoccAssetBuffer& schema) {
  if (!schema.data || schema.size == 0) {
    return Status::InvalidArgument("Autocc feature schema is empty");
  }
  if (schema.size != kExactSchemaSize) {
    return Status::ParseError("Autocc feature schema size mismatch");
  }
  uint64_t hash = 0xcbf29ce484222325ULL;
  for (size_t i = 0; i < schema.size; ++i) {
    hash ^= schema.data[i];
    hash *= 0x100000001b3ULL;
  }
  if (hash != kExactSchemaFnv1a64) {
    return Status::ParseError("Autocc feature schema order/hash mismatch");
  }
  return Status::Ok();
}

Status DecodeUtf8Strict(std::string_view text, std::vector<char32_t>* output) {
  if (!output) return Status::InvalidArgument("Autocc null UTF-8 output");
  output->clear();
  size_t i = 0;
  while (i < text.size()) {
    const uint8_t first = static_cast<uint8_t>(text[i++]);
    char32_t codepoint = 0;
    unsigned continuation = 0;
    if (first < 0x80u) {
      if (first == 0) return Status::InvalidArgument("Autocc NUL token");
      codepoint = first;
    } else if ((first & 0xE0u) == 0xC0u) {
      continuation = 1;
      codepoint = first & 0x1Fu;
      if (codepoint < 2) {
        return Status::InvalidArgument("Autocc overlong UTF-8");
      }
    } else if ((first & 0xF0u) == 0xE0u) {
      continuation = 2;
      codepoint = first & 0x0Fu;
    } else if ((first & 0xF8u) == 0xF0u) {
      continuation = 3;
      codepoint = first & 0x07u;
    } else {
      return Status::InvalidArgument("Autocc invalid UTF-8 lead byte");
    }
    if (continuation > text.size() - i) {
      return Status::InvalidArgument("Autocc truncated UTF-8");
    }
    for (unsigned n = 0; n < continuation; ++n) {
      const uint8_t byte = static_cast<uint8_t>(text[i++]);
      if ((byte & 0xC0u) != 0x80u) {
        return Status::InvalidArgument("Autocc invalid UTF-8 continuation");
      }
      codepoint = (codepoint << 6) | (byte & 0x3Fu);
    }
    if ((continuation == 2 && codepoint < 0x800u) ||
        (continuation == 3 && codepoint < 0x10000u) ||
        (codepoint >= 0xD800u && codepoint <= 0xDFFFu) ||
        codepoint > 0x10FFFFu) {
      return Status::InvalidArgument("Autocc invalid UTF-8 scalar");
    }
    output->push_back(codepoint);
  }
  if (output->empty()) return Status::InvalidArgument("Autocc empty token");
  return Status::Ok();
}

bool AllFinite(const AutoccTokenSignals& signals) {
  const float values[] = {
      signals.oov,          signals.lm_ngr_main,
      signals.lm_1gr_static, signals.lm_2gr_static,
      signals.lm_ngr_static, signals.letter_lm_main,
      signals.ndict,
  };
  return std::all_of(std::begin(values), std::end(values),
                     [](float value) { return std::isfinite(value); });
}

bool AllFinite(const AutoccDistanceSignals& signals) {
  return std::isfinite(signals.distance);
}

size_t LevenshteinDistance(const std::vector<char32_t>& left,
                           const std::vector<char32_t>& right) {
  std::vector<size_t> previous(right.size() + 1);
  std::vector<size_t> current(right.size() + 1);
  for (size_t j = 0; j <= right.size(); ++j) previous[j] = j;
  for (size_t i = 1; i <= left.size(); ++i) {
    current[0] = i;
    for (size_t j = 1; j <= right.size(); ++j) {
      const size_t substitution = left[i - 1] == right[j - 1] ? 0u : 1u;
      current[j] = std::min({previous[j] + 1u, current[j - 1] + 1u,
                             previous[j - 1] + substitution});
    }
    std::swap(previous, current);
  }
  return previous[right.size()];
}

float NgramPrecision(const std::vector<char32_t>& reference,
                     const std::vector<char32_t>& hypothesis, size_t order) {
  if (order == 0 || reference.size() < order || hypothesis.size() < order) {
    return 0.0f;
  }
  const size_t reference_count = reference.size() - order + 1;
  const size_t hypothesis_count = hypothesis.size() - order + 1;
  std::vector<bool> used(reference_count, false);
  size_t matched = 0;
  for (size_t h = 0; h < hypothesis_count; ++h) {
    for (size_t r = 0; r < reference_count; ++r) {
      if (used[r]) continue;
      bool equal = true;
      for (size_t n = 0; n < order; ++n) {
        if (reference[r + n] != hypothesis[h + n]) {
          equal = false;
          break;
        }
      }
      if (equal) {
        used[r] = true;
        ++matched;
        break;
      }
    }
  }
  return static_cast<float>(matched) /
         static_cast<float>(hypothesis_count);
}

bool IsPermutation(std::vector<char32_t> left,
                   std::vector<char32_t> right) {
  if (left.size() != right.size()) return false;
  std::sort(left.begin(), left.end());
  std::sort(right.begin(), right.end());
  return left == right;
}

float KendallTauNormalized(const std::vector<char32_t>& left,
                           const std::vector<char32_t>& right) {
  if (!IsPermutation(left, right) || left.size() < 2) return 0.0f;
  std::vector<bool> consumed(right.size(), false);
  std::vector<size_t> positions;
  positions.reserve(left.size());
  for (char32_t value : left) {
    for (size_t i = 0; i < right.size(); ++i) {
      if (!consumed[i] && right[i] == value) {
        consumed[i] = true;
        positions.push_back(i);
        break;
      }
    }
  }
  size_t inversions = 0;
  for (size_t i = 0; i < positions.size(); ++i) {
    for (size_t j = i + 1; j < positions.size(); ++j) {
      if (positions[i] > positions[j]) ++inversions;
    }
  }
  const size_t maximum = positions.size() * (positions.size() - 1) / 2;
  return maximum == 0
             ? 0.0f
             : static_cast<float>(inversions) / static_cast<float>(maximum);
}

char32_t FoldRussianDiacritic(char32_t value) {
  if (value == U'\u0451') return U'\u0435';
  if (value == U'\u0401') return U'\u0415';
  return value;
}

bool IsDiacriticsOnly(const std::vector<char32_t>& original,
                      const std::vector<char32_t>& correction) {
  if (original == correction || original.size() != correction.size()) {
    return false;
  }
  for (size_t i = 0; i < original.size(); ++i) {
    if (FoldRussianDiacritic(original[i]) !=
        FoldRussianDiacritic(correction[i])) {
      return false;
    }
  }
  return true;
}

bool ContainsApostrophe(const std::vector<char32_t>& value) {
  return std::any_of(value.begin(), value.end(), [](char32_t codepoint) {
    return codepoint == U'\'' || codepoint == U'\u2019' ||
           codepoint == U'\u02BC';
  });
}

bool ContainsHyphen(const std::vector<char32_t>& value) {
  return std::any_of(value.begin(), value.end(), [](char32_t codepoint) {
    return codepoint == U'-' || codepoint == U'\u2010' ||
           codepoint == U'\u2011';
  });
}

void FillTokenSignals(const AutoccTokenSignals& signals,
                      const std::vector<char32_t>& token, bool original,
                      std::array<float, AutoccModelRunner::kFeatureCount>* out) {
  const size_t offset = original ? 0 : 10;
  if (original) (*out)[0] = signals.oov;
  (*out)[offset + (original ? 1 : 0)] = static_cast<float>(token.size());
  (*out)[offset + (original ? 2 : 1)] = ContainsApostrophe(token) ? 1.0f : 0.0f;
  (*out)[offset + (original ? 3 : 2)] = ContainsHyphen(token) ? 1.0f : 0.0f;
  (*out)[offset + (original ? 4 : 3)] = signals.lm_ngr_main;
  (*out)[offset + (original ? 5 : 4)] = signals.lm_1gr_static;
  (*out)[offset + (original ? 6 : 5)] = signals.lm_2gr_static;
  (*out)[offset + (original ? 7 : 6)] = signals.lm_ngr_static;
  (*out)[offset + (original ? 8 : 7)] = signals.letter_lm_main;
  (*out)[offset + (original ? 9 : 8)] = signals.ndict;
}

}  // namespace

const char* AutoccVariantName(AutoccVariant variant) {
  switch (variant) {
    case AutoccVariant::kDefault:
      return "default";
    case AutoccVariant::kKeyboard13321:
      return "KEYBOARD-13321";
    case AutoccVariant::kKeyboard13421:
      return "KEYBOARD-13421";
  }
  return "invalid";
}

bool ParseAutoccVariant(const std::string& value, AutoccVariant* variant) {
  if (!variant) return false;
  if (value.empty() || value == "default") {
    *variant = AutoccVariant::kDefault;
    return true;
  }
  if (value == "KEYBOARD-13321" || value == "K13321" || value == "13321") {
    *variant = AutoccVariant::kKeyboard13321;
    return true;
  }
  if (value == "KEYBOARD-13421" || value == "K13421" || value == "13421") {
    *variant = AutoccVariant::kKeyboard13421;
    return true;
  }
  return false;
}

Status AutoccModelRunner::Load(const AutoccModelAssets& assets) {
  Status status = ValidateFeatureSchema(assets.feature_schema);
  if (!status.ok()) return status;

  std::array<ObliviousTreeModel, kVariantCount> models;
  const AutoccAssetBuffer buffers[] = {
      assets.model_default, assets.model_k13321, assets.model_k13421};
  const AutoccVariant variants[] = {
      AutoccVariant::kDefault, AutoccVariant::kKeyboard13321,
      AutoccVariant::kKeyboard13421};
  for (size_t i = 0; i < kVariantCount; ++i) {
    status = models[i].Load(buffers[i].data, buffers[i].size,
                            AutoccVariantName(variants[i]));
    if (!status.ok()) return status;
    if (models[i].feature_count() != kFeatureCount ||
        models[i].tree_count() != 2000 || models[i].depth() != 6) {
      return Status::ParseError("Autocc forest contract mismatch");
    }
  }

  std::lock_guard<std::mutex> lock(model_mutex_);
  models_ = std::move(models);
  invocation_count_.store(0, std::memory_order_relaxed);
  scored_count_.store(0, std::memory_order_relaxed);
  rejected_count_.store(0, std::memory_order_relaxed);
  for (auto& count : variant_counts_) {
    count.store(0, std::memory_order_relaxed);
  }
  last_variant_.store(0, std::memory_order_relaxed);
  loaded_.store(true, std::memory_order_release);
  return Status::Ok();
}

void AutoccModelRunner::Reset() {
  std::lock_guard<std::mutex> lock(model_mutex_);
  loaded_.store(false, std::memory_order_release);
  for (auto& model : models_) model.Reset();
  invocation_count_.store(0, std::memory_order_relaxed);
  scored_count_.store(0, std::memory_order_relaxed);
  rejected_count_.store(0, std::memory_order_relaxed);
  for (auto& count : variant_counts_) {
    count.store(0, std::memory_order_relaxed);
  }
  last_variant_.store(0, std::memory_order_relaxed);
}

Status AutoccModelRunner::BuildFeatures(
    const AutoccFeatureRequest& request,
    std::array<float, kFeatureCount>* features) const {
  if (!features) return Status::InvalidArgument("Autocc null feature output");
  if (!AllFinite(request.original_signals) ||
      !AllFinite(request.correction_signals) ||
      !AllFinite(request.forward_distance) ||
      !AllFinite(request.reverse_distance) ||
      !std::all_of(request.keyboard_features.begin(),
                   request.keyboard_features.end(),
                   [](float value) { return std::isfinite(value); })) {
    return Status::InvalidArgument("Autocc non-finite feature input");
  }

  std::vector<char32_t> original;
  std::vector<char32_t> correction;
  Status status = DecodeUtf8Strict(request.original, &original);
  if (!status.ok()) return status;
  status = DecodeUtf8Strict(request.correction, &correction);
  if (!status.ok()) return status;

  features->fill(0.0f);
  FillTokenSignals(request.original_signals, original, true, features);
  FillTokenSignals(request.correction_signals, correction, false, features);
  (*features)[19] = original == correction ? 1.0f : 0.0f;
  (*features)[20] = IsDiacriticsOnly(original, correction) ? 1.0f : 0.0f;
  (*features)[21] = request.normalization_only ? 1.0f : 0.0f;

  (*features)[22] = request.forward_distance.distance;
  for (size_t i = 0; i < 5; ++i) {
    (*features)[23 + i] =
        request.forward_distance.distance - static_cast<float>(i + 1);
  }
  (*features)[28] = request.forward_distance.distance /
                    static_cast<float>(std::max<size_t>(original.size(), 1));
  (*features)[29] = request.reverse_distance.distance;
  for (size_t i = 0; i < 5; ++i) {
    (*features)[30 + i] =
        request.reverse_distance.distance - static_cast<float>(i + 1);
  }
  (*features)[35] = request.reverse_distance.distance /
                    static_cast<float>(std::max<size_t>(correction.size(), 1));

  const float levenshtein =
      static_cast<float>(LevenshteinDistance(original, correction));
  (*features)[36] = levenshtein;
  (*features)[37] =
      levenshtein / static_cast<float>(std::max<size_t>(correction.size(), 1));
  float precision_product = 1.0f;
  for (size_t order = 1; order <= 4; ++order) {
    const float precision = NgramPrecision(original, correction, order);
    (*features)[41 + order] = precision;
    precision_product *= precision;
    (*features)[37 + order] = precision_product <= 0.0f
                                  ? 0.0f
                                  : std::pow(precision_product,
                                             1.0f / static_cast<float>(order));
  }
  (*features)[46] = original.size() != correction.size()
                        ? -1.0f
                        : (IsPermutation(original, correction) ? 1.0f : 0.0f);
  (*features)[47] = KendallTauNormalized(original, correction);
  (*features)[48] = request.same_stem ? 1.0f : 0.0f;

  // Exact feature-schema combo semantics.
  (*features)[49] = (*features)[22] + (*features)[13];
  (*features)[50] = (*features)[22] + (*features)[16];
  (*features)[51] = (*features)[4] - (*features)[13];
  (*features)[52] = (*features)[7] - (*features)[16];
  (*features)[53] = (*features)[1] - (*features)[10];
  std::copy(request.keyboard_features.begin(), request.keyboard_features.end(),
            features->begin() + 54);
  return Status::Ok();
}

double AutoccModelRunner::ScoreFeatures(
    const std::array<float, kFeatureCount>& features,
    AutoccVariant variant) const {
  invocation_count_.fetch_add(1, std::memory_order_relaxed);
  if (!IsValidVariant(variant) ||
      !std::all_of(features.begin(), features.end(),
                   [](float value) { return std::isfinite(value); })) {
    rejected_count_.fetch_add(1, std::memory_order_relaxed);
    return std::numeric_limits<double>::quiet_NaN();
  }
  const size_t index = VariantIndex(variant);
  std::lock_guard<std::mutex> lock(model_mutex_);
  if (!loaded_.load(std::memory_order_acquire)) {
    rejected_count_.fetch_add(1, std::memory_order_relaxed);
    return std::numeric_limits<double>::quiet_NaN();
  }
  variant_counts_[index].fetch_add(1, std::memory_order_relaxed);
  last_variant_.store(static_cast<uint8_t>(index), std::memory_order_relaxed);
  const std::vector<float> vector(features.begin(), features.end());
  const double probability = models_[index].ScoreProbability(vector);
  if (std::isfinite(probability)) {
    scored_count_.fetch_add(1, std::memory_order_relaxed);
  } else {
    rejected_count_.fetch_add(1, std::memory_order_relaxed);
  }
  return probability;
}

Status AutoccModelRunner::Evaluate(const AutoccFeatureRequest& request,
                                   AutoccVariant variant,
                                   AutoccEvaluation* result) const {
  if (!result) return Status::InvalidArgument("Autocc null evaluation output");
  AutoccEvaluation evaluated;
  Status status = BuildFeatures(request, &evaluated.features);
  if (!status.ok()) return status;
  evaluated.variant = variant;
  evaluated.probability = ScoreFeatures(evaluated.features, variant);
  evaluated.model_engaged = std::isfinite(evaluated.probability);
  if (!evaluated.model_engaged) {
    return Status::InvalidState("Autocc inference failed");
  }
  evaluated.should_autocorrect = evaluated.probability >= kThreshold;
  evaluated.diagnostics =
      "engaged=1,variant=" + std::string(AutoccVariantName(variant)) +
      ",probability=" + std::to_string(evaluated.probability) +
      ",threshold=" + std::to_string(kThreshold) +
      ",accept=" + (evaluated.should_autocorrect ? "1" : "0");
  *result = std::move(evaluated);
  return Status::Ok();
}

uint32_t AutoccModelRunner::tree_count(AutoccVariant variant) const {
  if (!IsValidVariant(variant)) return 0;
  std::lock_guard<std::mutex> lock(model_mutex_);
  return loaded_.load(std::memory_order_acquire)
             ? models_[VariantIndex(variant)].tree_count()
             : 0u;
}

std::string AutoccModelRunner::diagnostics() const {
  const uint8_t last = last_variant_.load(std::memory_order_relaxed);
  const AutoccVariant last_variant =
      last < kVariantCount ? static_cast<AutoccVariant>(last)
                           : AutoccVariant::kDefault;
  std::ostringstream output;
  output << "asset=" << (is_loaded() ? "loaded" : "missing")
         << ",forest_contract="
         << (is_loaded() ? "exact_hobl79" : "not_loaded")
         << ",schema=fnv1a64:392666aa7a7a479a"
         << ",threshold=" << kThreshold
         << ",last_variant=" << AutoccVariantName(last_variant)
         << ",invoke_count=" << invocation_count()
         << ",scored_count=" << scored_count()
         << ",rejected_count="
         << rejected_count_.load(std::memory_order_relaxed)
         << ",variant_default_count="
         << variant_counts_[0].load(std::memory_order_relaxed)
         << ",variant_k13321_count="
         << variant_counts_[1].load(std::memory_order_relaxed)
         << ",variant_k13421_count="
         << variant_counts_[2].load(std::memory_order_relaxed);
  return output.str();
}

}  // namespace hoskey::engine
