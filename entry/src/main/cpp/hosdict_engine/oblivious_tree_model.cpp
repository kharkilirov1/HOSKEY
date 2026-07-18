#include "oblivious_tree_model.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>

namespace hoskey::engine {
namespace {

constexpr uint16_t kFormatVersion = 2;
constexpr uint8_t kMinDepth = 1;
constexpr uint8_t kMaxDepth = 12;
constexpr uint32_t kMaxFeatures = 255;
constexpr uint32_t kMaxTrees = 100000;

uint16_t ReadU16(const uint8_t* data, size_t offset) {
  return static_cast<uint16_t>(data[offset]) |
         static_cast<uint16_t>(static_cast<uint16_t>(data[offset + 1]) << 8U);
}

uint32_t ReadU32(const uint8_t* data, size_t offset) {
  return static_cast<uint32_t>(data[offset]) |
         (static_cast<uint32_t>(data[offset + 1]) << 8U) |
         (static_cast<uint32_t>(data[offset + 2]) << 16U) |
         (static_cast<uint32_t>(data[offset + 3]) << 24U);
}

float ReadF32(const uint8_t* data, size_t offset) {
  float value = 0.0f;
  std::memcpy(&value, data + offset, sizeof(value));
  return value;
}

double ReadF64(const uint8_t* data, size_t offset) {
  double value = 0.0;
  std::memcpy(&value, data + offset, sizeof(value));
  return value;
}

}  // namespace

void ObliviousTreeModel::Reset() {
  loaded_ = false;
  depth_ = 0;
  feature_count_ = 0;
  tree_count_ = 0;
  leaves_per_tree_ = 0;
  split_features_.clear();
  split_thresholds_.clear();
  leaves_.clear();
  asset_state_ = "missing";
  contract_state_ = "unsupported:not_loaded";
  variant_ = "unbound";
  load_state_ = "not_attempted";
  invoke_count_ = 0;
  scored_count_ = 0;
  fallback_state_ = "not_loaded";
  latency_us_ = 0;
  RefreshDiagnostics();
}

void ObliviousTreeModel::SetLoadState(const char* asset, const char* contract,
                                      const char* load) {
  asset_state_ = asset;
  contract_state_ = contract;
  load_state_ = load;
  RefreshDiagnostics();
}

void ObliviousTreeModel::RefreshDiagnostics() const {
  diagnostics_ = "asset=" + asset_state_ + ",contract=" + contract_state_ +
                 ",variant=" + variant_ + ",load=" + load_state_ +
                 ",invoke_count=" + std::to_string(invoke_count_) +
                 ",scored_count=" + std::to_string(scored_count_) +
                 ",fallback=" + fallback_state_ +
                 ",latency_us=" + std::to_string(latency_us_);
  if (loaded_) {
    diagnostics_ += ",trees=" + std::to_string(tree_count_) +
                    ",features=" + std::to_string(feature_count_) +
                    ",depth=" + std::to_string(depth_);
  }
}

const std::string& ObliviousTreeModel::diagnostics() const {
  RefreshDiagnostics();
  return diagnostics_;
}

Status ObliviousTreeModel::Load(const uint8_t* data, size_t size,
                                const std::string& variant) {
  Reset();
  variant_ = variant.empty() ? "unbound" : variant;
  if (!data) {
    SetLoadState("missing", "unsupported:null", "failed:null");
    return Status::InvalidArgument("ObliviousTreeModel: null data");
  }
  if (size < 16 || std::memcmp(data, "HOBL", 4) != 0) {
    SetLoadState("present", "unsupported:header", "failed:header");
    return Status::ParseError("ObliviousTreeModel: bad HOBL header");
  }

  const uint16_t version = ReadU16(data, 4);
  const uint8_t depth = data[6];
  const uint8_t reserved = data[7];
  const uint32_t tree_count = ReadU32(data, 8);
  const uint32_t feature_count = ReadU32(data, 12);
  if (version != kFormatVersion || reserved != 0) {
    SetLoadState("present", "unsupported:version", "failed:version");
    return Status::UnsupportedFormat(
        "ObliviousTreeModel: unsupported HOBL version");
  }
  if (depth < kMinDepth || depth > kMaxDepth || tree_count == 0 ||
      tree_count > kMaxTrees || feature_count == 0 ||
      feature_count > kMaxFeatures) {
    SetLoadState("present", "unsupported:counts", "failed:counts");
    return Status::ParseError(
        "ObliviousTreeModel: invalid depth/tree/feature count");
  }

  size_t offset = 16;
  for (uint32_t feature = 0; feature < feature_count; ++feature) {
    if (offset > size || size - offset < 2) {
      SetLoadState("present", "unsupported:borders", "failed:truncated");
      return Status::ParseError(
          "ObliviousTreeModel: truncated border catalog");
    }
    const uint16_t border_count = ReadU16(data, offset);
    offset += 2;
    const size_t border_bytes = static_cast<size_t>(border_count) * 4U;
    if (offset > size || border_bytes > size - offset) {
      SetLoadState("present", "unsupported:borders", "failed:truncated");
      return Status::ParseError(
          "ObliviousTreeModel: truncated border values");
    }
    float previous = -std::numeric_limits<float>::infinity();
    for (uint16_t border = 0; border < border_count; ++border) {
      const float value = ReadF32(data, offset + static_cast<size_t>(border) * 4U);
      if (!std::isfinite(value) || value < previous) {
        SetLoadState("present", "unsupported:borders", "failed:invalid");
        return Status::ParseError(
            "ObliviousTreeModel: invalid border values");
      }
      previous = value;
    }
    offset += border_bytes;
  }

  const uint32_t leaves_per_tree = 1U << depth;
  const size_t split_bytes = static_cast<size_t>(depth) * 5U;
  const size_t leaf_bytes = static_cast<size_t>(leaves_per_tree) * 8U;
  const size_t tree_bytes = split_bytes + leaf_bytes;
  if (tree_bytes == 0 || offset > size ||
      static_cast<size_t>(tree_count) > (size - offset) / tree_bytes ||
      static_cast<size_t>(tree_count) * tree_bytes != size - offset) {
    SetLoadState("present", "unsupported:trees", "failed:truncated");
    return Status::ParseError(
        "ObliviousTreeModel: truncated or trailing tree payload");
  }

  split_features_.resize(static_cast<size_t>(tree_count) * depth);
  split_thresholds_.resize(static_cast<size_t>(tree_count) * depth);
  leaves_.resize(static_cast<size_t>(tree_count) * leaves_per_tree);
  for (uint32_t tree = 0; tree < tree_count; ++tree) {
    const size_t split_base = static_cast<size_t>(tree) * depth;
    for (uint8_t split = 0; split < depth; ++split) {
      const uint8_t feature = data[offset];
      const float threshold = ReadF32(data, offset + 1);
      offset += 5;
      if (feature >= feature_count || !std::isfinite(threshold)) {
        SetLoadState("present", "unsupported:split", "failed:invalid");
        return Status::ParseError(
            "ObliviousTreeModel: invalid split feature/threshold");
      }
      split_features_[split_base + split] = feature;
      split_thresholds_[split_base + split] = threshold;
    }
    const size_t leaf_base = static_cast<size_t>(tree) * leaves_per_tree;
    for (uint32_t leaf = 0; leaf < leaves_per_tree; ++leaf) {
      const double value = ReadF64(data, offset);
      offset += 8;
      if (!std::isfinite(value)) {
        SetLoadState("present", "unsupported:leaf", "failed:nonfinite");
        return Status::ParseError(
            "ObliviousTreeModel: non-finite leaf value");
      }
      leaves_[leaf_base + leaf] = value;
    }
  }

  depth_ = depth;
  feature_count_ = feature_count;
  tree_count_ = tree_count;
  leaves_per_tree_ = leaves_per_tree;
  loaded_ = true;
  fallback_state_ = "none";
  SetLoadState("present", "valid", "loaded");
  return Status::Ok();
}

double ObliviousTreeModel::ScoreRaw(
    const std::vector<float>& features) const {
  const auto started = std::chrono::steady_clock::now();
  ++invoke_count_;
  auto finish = [this, started](const char* fallback) {
    fallback_state_ = fallback;
    latency_us_ = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started)
            .count());
    RefreshDiagnostics();
  };
  if (!loaded_) {
    finish("not_loaded");
    return std::numeric_limits<float>::quiet_NaN();
  }
  if (features.size() != feature_count_) {
    finish("feature_count");
    return std::numeric_limits<float>::quiet_NaN();
  }
  for (float feature : features) {
    if (!std::isfinite(feature)) {
      finish("nonfinite_feature");
      return std::numeric_limits<float>::quiet_NaN();
    }
  }

  double sum = 0.0;
  for (uint32_t tree = 0; tree < tree_count_; ++tree) {
    uint32_t leaf_index = 0;
    const size_t split_base = static_cast<size_t>(tree) * depth_;
    for (uint8_t split = 0; split < depth_; ++split) {
      const uint8_t feature = split_features_[split_base + split];
      if (features[feature] > split_thresholds_[split_base + split]) {
        leaf_index |= 1U << split;
      }
    }
    sum += leaves_[static_cast<size_t>(tree) * leaves_per_tree_ + leaf_index];
  }
  if (!std::isfinite(sum)) {
    finish("nonfinite_score");
    return std::numeric_limits<float>::quiet_NaN();
  }
  ++scored_count_;
  finish("none");
  return sum;
}

double ObliviousTreeModel::ScoreProbability(
    const std::vector<float>& features) const {
  const double raw = ScoreRaw(features);
  if (!std::isfinite(raw)) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  return 1.0 / (1.0 + std::exp(-raw));
}

}  // namespace hoskey::engine
