#include "hosdict_engine/tap_model_runner.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

using hoskey::engine::TapModelAssets;
using hoskey::engine::TapModelPairRequest;
using hoskey::engine::TapModelPairResult;
using hoskey::engine::TapModelRunner;
using hoskey::engine::TapModelTouchPoint;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

std::vector<uint8_t> ReadFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  Check(input.good(), "cannot open fixture");
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

bool Near(double actual, double expected, double tolerance = 1e-7) {
  return std::isfinite(actual) && std::abs(actual - expected) <= tolerance;
}

}  // namespace

int main(int argc, char** argv) {
  Check(argc == 5,
        "usage: test_tap_model_runner bundle schema ranker classifier");
  const std::vector<uint8_t> bundle = ReadFile(argv[1]);
  const std::vector<uint8_t> schema = ReadFile(argv[2]);
  const std::vector<uint8_t> ranker = ReadFile(argv[3]);
  const std::vector<uint8_t> classifier = ReadFile(argv[4]);

  TapModelAssets assets{
      {bundle.data(), bundle.size()},
      {schema.data(), schema.size()},
      {ranker.data(), ranker.size()},
      {classifier.data(), classifier.size()},
  };
  TapModelRunner runner;
  const auto load = runner.Load(assets);
  Check(load.ok(), load.message.c_str());
  Check(runner.is_loaded(), "runner must load atomically");
  Check(runner.layout_count() == 6, "exact bundle layout count");
  Check(runner.key_count() == 2684, "exact bundle key count");
  Check(runner.ranker_feature_count() == 38, "ranker feature contract");
  Check(runner.classifier_feature_count() == 78,
        "classifier 2N+2 feature contract");

  const std::array<float, 38> original = {
      -14.008148193359375f, -14.008148193359375f, -8.844351768493652f,
      -24.339218139648438f, -16.0f, -30.551244735717773f,
      3.574657014837612e-08f, -24.737619400024414f,
      -6.110249042510986f, 4.0f, 4.0f, 0.05627557635307312f,
      0.05627557635307312f, 0.7342569828033447f, 0.7342569828033447f,
      0.5f, 0.7621023654937744f, 5.510204315185547f, 0.5f,
      0.7012448310852051f, 5.510204315185547f, 0.0f, 0.0f,
      0.3055555522441864f, 0.060857538133859634f, 2.940000057220459f,
      -0.7638888955116272f, 1.0f, 0.3055555522441864f,
      0.09336419403553009f, 0.020389730110764503f,
      0.020389730110764503f, 0.020389730110764503f,
      -14.008148193359375f, 0.9980363249778748f, 1.026530146598816f,
      -1.1632921695709229f, 0.026538142934441566f};
  const std::array<float, 38> correction = {
      -26.71222496032715f, -26.71222496032715f, -8.982261657714844f,
      -8.339218139648438f, 0.0f, -5.816461086273193f,
      0.9980363249778748f, -0.0028357694391161203f,
      -1.1632921695709229f, 4.0f, 4.0f, 0.05627557635307312f,
      0.07575368136167526f, 4.6649980545043945f, 4.486692905426025f,
      0.5f, 0.7621023654937744f, 5.510204315185547f,
      0.40925925970077515f, 0.7012448310852051f, 4.510204315185547f,
      -0.9800000190734863f, -0.09074074029922485f,
      0.3055555522441864f, 0.060857538133859634f, 1.9600000381469727f,
      -0.7638888955116272f, 0.0f, 1.026530146598816f,
      1.0537642240524292f, 0.026538142934441566f,
      0.04860756918787956f, 0.0008837739587761462f,
      -14.008148193359375f, 0.9980363249778748f, 1.026530146598816f,
      -1.1632921695709229f, 0.026538142934441566f};

  const double original_score = runner.ScoreRankerFeatures(original);
  const double correction_score = runner.ScoreRankerFeatures(correction);
  Check(Near(original_score, 0.02705077826976776),
        "ranker original golden vector");
  Check(Near(correction_score, 0.9059374332427979),
        "ranker correction golden vector");
  const double classifier_score = runner.ScoreClassifierFeatures(
      original_score, original, correction_score, correction);
  Check(Near(classifier_score, 0.9584593221888383),
        "classifier golden vector");
  Check(runner.ranker_invocation_count() == 2,
        "ranker invocation counter must engage");
  Check(runner.ranker_scored_count() == 2,
        "ranker scored counter must engage");
  Check(runner.classifier_invocation_count() == 1,
        "classifier invocation counter must engage");
  Check(runner.classifier_scored_count() == 1,
        "classifier scored counter must engage");

  auto invalid = original;
  invalid[7] = std::numeric_limits<float>::quiet_NaN();
  Check(std::isnan(runner.ScoreRankerFeatures(invalid)),
        "non-finite ranker input must be rejected");
  Check(runner.ranker_invocation_count() == 3,
        "rejected invocation must still be counted");
  Check(runner.ranker_scored_count() == 2,
        "rejected invocation must not be scored");

  TapModelPairRequest center;
  center.original = "\xD0\xBD";    // н
  center.correction = "\xD0\xB5";  // е
  center.layout = "android_ru_east_slavic_separate_comma";
  center.keyboard_width = 1080;
  center.keyboard_height = 631;
  center.touches.push_back(TapModelTouchPoint{540.0, 178.2575, 0x043Du});
  center.original_semantics.context_prefix_len = 1.0f;
  center.original_semantics.context_last_word_len = 1.0f;
  center.correction_semantics = center.original_semantics;

  TapModelPairResult center_result;
  Check(runner.EvaluatePair(center, &center_result).ok(),
        "center pair must evaluate");
  Check(center_result.model_engaged, "pair evaluation must engage both models");
  Check(center_result.original_features[27] == 1.0f,
        "original is_same feature");
  Check(center_result.correction_features[27] == 0.0f,
        "correction is_same feature");

  TapModelPairRequest edge = center;
  edge.touches[0].x += 40.0;
  TapModelPairResult edge_result;
  Check(runner.EvaluatePair(edge, &edge_result).ok(),
        "edge pair must evaluate");
  Check(edge_result.correction_features[21] !=
            center_result.correction_features[21],
        "center/edge geometry delta must reach feature vector");
  Check(edge_result.correction_rank_score != center_result.correction_rank_score,
        "center/edge delta must reach TapModel ranker");

  TapModelPairRequest out_of_bounds = center;
  out_of_bounds.touches[0].x = 2000.0;
  TapModelPairResult rejected;
  Check(!runner.EvaluatePair(out_of_bounds, &rejected).ok(),
        "out-of-bounds touch must be rejected");

  std::vector<uint8_t> bad_classifier = classifier;
  bad_classifier[0] ^= 0xFFu;
  TapModelAssets bad_assets = assets;
  bad_assets.classifier = {bad_classifier.data(), bad_classifier.size()};
  Check(!runner.Load(bad_assets).ok(), "corrupt atomic reload must fail");
  Check(runner.is_loaded(), "failed atomic reload must preserve prior model");
  Check(Near(runner.ScoreRankerFeatures(original), original_score),
        "prior model must remain usable after failed reload");

  std::cout << "PASS: exact TapModel runner, golden vectors, geometry delta, "
               "strict rejection and atomic reload\n";
  return 0;
}
