#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

#include "oblivious_tree_model.h"
#include "status.h"

namespace hoskey::engine {

enum class AutoccVariant : uint8_t {
  kDefault = 0,
  kKeyboard13321 = 1,
  kKeyboard13421 = 2,
};

[[nodiscard]] const char* AutoccVariantName(AutoccVariant variant);
[[nodiscard]] bool ParseAutoccVariant(const std::string& value,
                                      AutoccVariant* variant);

// Missing/disabled/ineligible models may use the explicitly tagged legacy
// fallback. Once inference starts, however, an error must veto autocorrection
// rather than silently falling through to that heuristic.
[[nodiscard]] constexpr bool ResolveAutoccPolicyGate(
    bool inference_attempted, bool decision_available,
    bool should_autocorrect) noexcept {
  return !inference_attempted ||
         (decision_available && should_autocorrect);
}

struct AutoccAssetBuffer {
  const uint8_t* data = nullptr;
  size_t size = 0;
};

// The three forests are an atomic unit: a partial or schema-mismatched set is
// never activated, and a failed reload preserves the previously loaded set.
struct AutoccModelAssets {
  AutoccAssetBuffer feature_schema;
  AutoccAssetBuffer model_default;
  AutoccAssetBuffer model_k13321;
  AutoccAssetBuffer model_k13421;
};

struct AutoccTokenSignals {
  float oov = 0.0f;
  float lm_ngr_main = 0.0f;
  float lm_1gr_static = 0.0f;
  float lm_2gr_static = 0.0f;
  float lm_ngr_static = 0.0f;
  float letter_lm_main = 0.0f;
  float ndict = 0.0f;
};

struct AutoccDistanceSignals {
  float distance = 0.0f;
};

struct AutoccFeatureRequest {
  std::string original;
  std::string correction;
  AutoccTokenSignals original_signals;
  AutoccTokenSignals correction_signals;
  AutoccDistanceSignals forward_distance;
  AutoccDistanceSignals reverse_distance;
  bool normalization_only = false;
  bool same_stem = false;

  // Exact KeyboardSpecific feature order from features_autocc.json, indices
  // 54..78. Geometry/LM producers stay outside the forest runner so that each
  // source is independently testable and missing signals remain explicit.
  std::array<float, 25> keyboard_features{};
};

struct AutoccEvaluation {
  std::array<float, 79> features{};
  double probability = 0.0;
  bool should_autocorrect = false;
  bool model_engaged = false;
  AutoccVariant variant = AutoccVariant::kDefault;
  std::string diagnostics;
};

// Executor for the recovered Yandex Autocc oblivious forests. The HOBL
// evaluator is exact (strict `>`, LSB-first leaves, float64 accumulation and
// sigmoid). This class owns the role-specific 79-feature schema, experiment
// selection and the recovered 0.65 policy threshold.
class AutoccModelRunner {
 public:
  static constexpr size_t kFeatureCount = 79;
  static constexpr size_t kKeyboardFeatureCount = 25;
  static constexpr double kThreshold = 0.65;

  [[nodiscard]] Status Load(const AutoccModelAssets& assets);
  void Reset();

  [[nodiscard]] Status BuildFeatures(
      const AutoccFeatureRequest& request,
      std::array<float, kFeatureCount>* features) const;
  [[nodiscard]] double ScoreFeatures(
      const std::array<float, kFeatureCount>& features,
      AutoccVariant variant = AutoccVariant::kDefault) const;
  [[nodiscard]] Status Evaluate(
      const AutoccFeatureRequest& request, AutoccVariant variant,
      AutoccEvaluation* result) const;

  [[nodiscard]] bool is_loaded() const {
    return loaded_.load(std::memory_order_acquire);
  }
  [[nodiscard]] uint32_t feature_count() const {
    return is_loaded() ? static_cast<uint32_t>(kFeatureCount) : 0u;
  }
  [[nodiscard]] uint32_t tree_count(AutoccVariant variant) const;
  [[nodiscard]] uint64_t invocation_count() const {
    return invocation_count_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] uint64_t scored_count() const {
    return scored_count_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::string diagnostics() const;

 private:
  static constexpr size_t kVariantCount = 3;

  mutable std::mutex model_mutex_;
  std::array<ObliviousTreeModel, kVariantCount> models_;
  std::atomic<bool> loaded_{false};
  mutable std::atomic<uint64_t> invocation_count_{0};
  mutable std::atomic<uint64_t> scored_count_{0};
  mutable std::atomic<uint64_t> rejected_count_{0};
  mutable std::array<std::atomic<uint64_t>, kVariantCount> variant_counts_{};
  mutable std::atomic<uint8_t> last_variant_{0};
};

}  // namespace hoskey::engine
