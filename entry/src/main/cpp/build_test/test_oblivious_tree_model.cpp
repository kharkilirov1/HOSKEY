#include "hosdict_engine/oblivious_tree_model.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

using hoskey::engine::ObliviousTreeModel;

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
  } else {
    std::printf("PASS: %s\n", message);
  }
}

void AppendU16(std::vector<uint8_t>* out, uint16_t value) {
  out->push_back(static_cast<uint8_t>(value));
  out->push_back(static_cast<uint8_t>(value >> 8U));
}

void AppendU32(std::vector<uint8_t>* out, uint32_t value) {
  out->push_back(static_cast<uint8_t>(value));
  out->push_back(static_cast<uint8_t>(value >> 8U));
  out->push_back(static_cast<uint8_t>(value >> 16U));
  out->push_back(static_cast<uint8_t>(value >> 24U));
}

void AppendF32(std::vector<uint8_t>* out, float value) {
  uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&bits, &value, sizeof(bits));
  AppendU32(out, bits);
}

void AppendF64(std::vector<uint8_t>* out, double value) {
  uint64_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&bits, &value, sizeof(bits));
  for (unsigned shift = 0; shift < 64; shift += 8) {
    out->push_back(static_cast<uint8_t>(bits >> shift));
  }
}

std::vector<uint8_t> BuildDepth8Fixture() {
  std::vector<uint8_t> data = {'H', 'O', 'B', 'L'};
  AppendU16(&data, 2);  // format version with float64 leaves
  data.push_back(8);    // tree depth
  data.push_back(0);    // reserved
  AppendU32(&data, 1);  // trees
  AppendU32(&data, 2);  // features

  // The compact format keeps the recovered border catalog for provenance even
  // though runtime evaluation only needs the resolved split thresholds.
  AppendU16(&data, 0);
  AppendU16(&data, 0);

  for (uint8_t split = 0; split < 8; ++split) {
    data.push_back(split % 2);
    AppendF32(&data, 0.0f);
  }
  for (uint32_t leaf = 0; leaf < 256; ++leaf) {
    AppendF64(&data, static_cast<double>(leaf) * 0.25);
  }
  return data;
}

}  // namespace

int main(int argc, char** argv) {
  const std::vector<uint8_t> fixture = BuildDepth8Fixture();

  ObliviousTreeModel model;
  const auto status = model.Load(fixture.data(), fixture.size(), "tap.default");
  Check(status.ok(), "HOBL depth-8 fixture loads");
  Check(model.is_loaded(), "loaded state is observable");
  Check(model.depth() == 8, "depth contract is preserved");
  Check(model.feature_count() == 2, "feature contract is preserved");
  Check(model.tree_count() == 1, "tree count is preserved");
  const std::string loaded_diag = model.diagnostics();
  Check(loaded_diag.find("asset=present") != std::string::npos,
        "diagnostics report asset state");
  Check(loaded_diag.find("contract=valid") != std::string::npos,
        "diagnostics report contract state");
  Check(loaded_diag.find("variant=tap.default") != std::string::npos,
        "diagnostics report bound variant");
  Check(loaded_diag.find("load=loaded") != std::string::npos,
        "diagnostics report load state");
  Check(loaded_diag.find("invoke_count=0") != std::string::npos,
        "diagnostics initialize invoke count");
  Check(loaded_diag.find("scored_count=0") != std::string::npos,
        "diagnostics initialize scored count");
  Check(loaded_diag.find("fallback=none") != std::string::npos,
        "diagnostics initialize fallback state");
  Check(loaded_diag.find("latency_us=0") != std::string::npos,
        "diagnostics initialize latency");

  // x[0] selects bits 0,2,4,6 while x[1] leaves bits 1,3,5,7 clear:
  // leaf index = 0b01010101 = 85.
  const double raw_score = model.ScoreRaw({1.0f, -1.0f});
  Check(std::isfinite(raw_score), "finite input produces finite raw score");
  Check(std::fabs(raw_score - 21.25) < 1.0e-12,
        "depth-8 leaf index uses recovered LSB split order");
  const double probability = model.ScoreProbability({1.0f, -1.0f});
  Check(std::fabs(probability - 1.0 / (1.0 + std::exp(-21.25))) < 1.0e-15,
        "probability applies the recovered sigmoid calibration");
  const std::string scored_diag = model.diagnostics();
  Check(scored_diag.find("invoke_count=2") != std::string::npos,
        "successful score increments invoke count");
  Check(scored_diag.find("scored_count=2") != std::string::npos,
        "successful score increments scored count");
  Check(scored_diag.find("fallback=none") != std::string::npos,
        "successful score does not claim fallback");
  Check(scored_diag.find("latency_us=") != std::string::npos,
        "successful score reports latency");

  Check(model.ScoreRaw({0.0f, 0.0f}) == 0.0,
        "feature equality follows strict greater-than branch semantics");

  Check(std::isnan(model.ScoreRaw({1.0f})),
        "wrong feature count is rejected");
  Check(model.diagnostics().find("fallback=feature_count") !=
            std::string::npos,
        "feature mismatch reports explicit fallback");
  Check(std::isnan(model.ScoreRaw(
            {std::numeric_limits<float>::quiet_NaN(), 0.0f})),
        "non-finite features are rejected");

  std::vector<uint8_t> truncated = fixture;
  truncated.pop_back();
  ObliviousTreeModel truncated_model;
  Check(!truncated_model.Load(truncated.data(), truncated.size()).ok(),
        "truncated tree payload is rejected");

  std::vector<uint8_t> bad_depth = fixture;
  bad_depth[6] = 0;
  ObliviousTreeModel bad_depth_model;
  Check(!bad_depth_model.Load(bad_depth.data(), bad_depth.size()).ok(),
        "zero depth is rejected");

  for (int argument = 1; argument < argc; ++argument) {
    std::ifstream input(argv[argument], std::ios::binary);
    Check(input.good(), "real structural artifact is readable");
    if (!input.good()) {
      continue;
    }
    const std::vector<uint8_t> artifact{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    ObliviousTreeModel real_model;
    const auto real_status = real_model.Load(
        artifact.data(), artifact.size(), "host.structural_only");
    Check(real_status.ok(), "real structural artifact loads in C++ backend");
    if (!real_status.ok()) {
      continue;
    }
    const double real_score = real_model.ScoreRaw(
        std::vector<float>(real_model.feature_count(), 0.0f));
    Check(std::isfinite(real_score),
          "real structural artifact produces a finite host score");
    const std::string real_diag = real_model.diagnostics();
    Check(real_diag.find("variant=host.structural_only") != std::string::npos &&
              real_diag.find("invoke_count=1") != std::string::npos &&
              real_diag.find("scored_count=1") != std::string::npos &&
              real_diag.find("fallback=none") != std::string::npos &&
              real_diag.find("latency_us=") != std::string::npos,
          "real structural artifact exposes strict diagnostics");
    std::printf("ARTIFACT: %s score=%g %s\n", argv[argument], real_score,
                real_diag.c_str());
  }

  if (failures != 0) {
    std::fprintf(stderr, "%d oblivious-tree test(s) failed\n", failures);
    return 1;
  }
  std::printf("All oblivious-tree tests passed\n");
  return 0;
}
