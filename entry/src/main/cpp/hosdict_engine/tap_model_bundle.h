#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include "status.h"

namespace hoskey::engine {

struct TapKeyParams {
  uint32_t x = 0;
  uint32_t y = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  float log_prior = 0.0f;
  float mean_x = 0.0f;
  float mean_y = 0.0f;
  float variance_x = 0.0f;
  float variance_y = 0.0f;
  float correlation = 0.0f;
};

struct StaticTapModel {
  std::unordered_map<std::string, TapKeyParams> key_params;
  uint32_t width = 0;
  uint32_t height = 0;
};

// Strict parser for the length-delimited TTapModelBundleParams protobuf used by
// Yandex Keyboard 122.5. Unknown fields/wires, duplicate map keys, malformed
// UTF-8, non-finite Gaussian parameters and partial frames are rejected.
class TapModelBundle {
 public:
  [[nodiscard]] Status Load(const uint8_t* data, size_t size);
  void Reset();

  [[nodiscard]] const StaticTapModel* FindLayout(
      std::string_view name) const;
  [[nodiscard]] const TapKeyParams* FindKey(const StaticTapModel& layout,
                                            std::string_view key) const;
  [[nodiscard]] bool is_loaded() const { return loaded_; }
  [[nodiscard]] size_t layout_count() const { return layouts_.size(); }
  [[nodiscard]] size_t key_count() const { return key_count_; }
  [[nodiscard]] const std::string& diagnostics() const { return diagnostics_; }

 private:
  bool loaded_ = false;
  size_t key_count_ = 0;
  std::unordered_map<std::string, StaticTapModel> layouts_;
  std::string diagnostics_ = "not_loaded";
};

}  // namespace hoskey::engine
