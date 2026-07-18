#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "status.h"

namespace hoskey::engine {

// Variable-depth oblivious-tree ensemble used as the common execution backend
// for recovered Yandex tree models. Role-specific runners remain responsible
// for feature semantics, variant selection, thresholds and fallback policy.
class ObliviousTreeModel {
 public:
  [[nodiscard]] Status Load(const uint8_t* data, size_t size,
                            const std::string& variant = "unbound");
  void Reset();

  // Returns NaN when the model is unavailable, the feature contract does not
  // match, or any feature is non-finite. The recovered evaluator uses strict
  // `>`, LSB-first leaf bits and float64 accumulation.
  [[nodiscard]] double ScoreRaw(const std::vector<float>& features) const;
  [[nodiscard]] double ScoreProbability(
      const std::vector<float>& features) const;
  [[nodiscard]] float Score(const std::vector<float>& features) const {
    return static_cast<float>(ScoreProbability(features));
  }

  [[nodiscard]] bool is_loaded() const { return loaded_; }
  [[nodiscard]] uint8_t depth() const { return depth_; }
  [[nodiscard]] uint32_t feature_count() const { return feature_count_; }
  [[nodiscard]] uint32_t tree_count() const { return tree_count_; }
  [[nodiscard]] const std::string& diagnostics() const;

 private:
  void SetLoadState(const char* asset, const char* contract, const char* load);
  void RefreshDiagnostics() const;

  bool loaded_ = false;
  uint8_t depth_ = 0;
  uint32_t feature_count_ = 0;
  uint32_t tree_count_ = 0;
  uint32_t leaves_per_tree_ = 0;
  std::vector<uint8_t> split_features_;
  std::vector<float> split_thresholds_;
  std::vector<double> leaves_;
  std::string asset_state_ = "missing";
  std::string contract_state_ = "unsupported:not_loaded";
  std::string variant_ = "unbound";
  std::string load_state_ = "not_attempted";
  mutable uint64_t invoke_count_ = 0;
  mutable uint64_t scored_count_ = 0;
  mutable std::string fallback_state_ = "not_loaded";
  mutable uint64_t latency_us_ = 0;
  mutable std::string diagnostics_;
};

}  // namespace hoskey::engine
