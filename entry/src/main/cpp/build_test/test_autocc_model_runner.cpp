#include "hosdict_engine/autocc_model_runner.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

using hoskey::engine::AutoccFeatureRequest;
using hoskey::engine::AutoccModelAssets;
using hoskey::engine::AutoccModelRunner;
using hoskey::engine::AutoccVariant;
using hoskey::engine::ResolveAutoccPolicyGate;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

std::vector<uint8_t> ReadFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  Check(input.good(), "cannot open Autocc fixture");
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

bool Near(double actual, double expected, double tolerance = 1e-9) {
  return std::isfinite(actual) && std::abs(actual - expected) <= tolerance;
}

}  // namespace

int main(int argc, char** argv) {
  Check(argc == 5,
        "usage: test_autocc_model_runner schema default k13321 k13421");
  const std::vector<uint8_t> schema = ReadFile(argv[1]);
  const std::vector<uint8_t> model_default = ReadFile(argv[2]);
  const std::vector<uint8_t> model_k13321 = ReadFile(argv[3]);
  const std::vector<uint8_t> model_k13421 = ReadFile(argv[4]);

  const AutoccModelAssets assets{
      {schema.data(), schema.size()},
      {model_default.data(), model_default.size()},
      {model_k13321.data(), model_k13321.size()},
      {model_k13421.data(), model_k13421.size()},
  };
  AutoccModelRunner runner;
  const auto load = runner.Load(assets);
  Check(load.ok(), load.message.c_str());
  Check(runner.is_loaded(), "Autocc models must load atomically");
  Check(runner.feature_count() == 79, "Autocc exact 79-feature contract");
  Check(runner.tree_count(AutoccVariant::kDefault) == 2000,
        "Autocc default tree count");
  Check(runner.tree_count(AutoccVariant::kKeyboard13321) == 2000,
        "Autocc K13321 tree count");
  Check(runner.tree_count(AutoccVariant::kKeyboard13421) == 2000,
        "Autocc K13421 tree count");
  Check(ResolveAutoccPolicyGate(false, false, false),
        "missing/ineligible Autocc may use the tagged legacy fallback");
  Check(!ResolveAutoccPolicyGate(true, false, false),
        "attempted Autocc inference failure must fail closed");
  Check(!ResolveAutoccPolicyGate(true, true, false) &&
            ResolveAutoccPolicyGate(true, true, true),
        "engaged Autocc decision must be authoritative");

  AutoccFeatureRequest request;
  request.original = "\xD0\xBF\xD1\x80\xD0\xB5\xD0\xB2\xD0\xB5\xD1\x82";  // превет
  request.correction = "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";  // привет
  request.original_signals.oov = 1.0f;
  request.original_signals.lm_ngr_main = 12.5f;
  request.original_signals.lm_1gr_static = 10.0f;
  request.original_signals.lm_2gr_static = 9.0f;
  request.original_signals.lm_ngr_static = 8.0f;
  request.original_signals.letter_lm_main = 7.0f;
  request.original_signals.ndict = 0.0f;
  request.correction_signals.lm_ngr_main = 4.5f;
  request.correction_signals.lm_1gr_static = 3.0f;
  request.correction_signals.lm_2gr_static = 2.5f;
  request.correction_signals.lm_ngr_static = 2.0f;
  request.correction_signals.letter_lm_main = 1.5f;
  request.correction_signals.ndict = 1.0f;
  request.forward_distance.distance = 0.75f;
  request.reverse_distance.distance = 0.8f;
  request.same_stem = true;
  for (size_t i = 0; i < request.keyboard_features.size(); ++i) {
    request.keyboard_features[i] = static_cast<float>(i + 1) * 0.05f;
  }

  std::array<float, AutoccModelRunner::kFeatureCount> features{};
  const auto build = runner.BuildFeatures(request, &features);
  Check(build.ok(), build.message.c_str());
  Check(features[0] == 1.0f && features[1] == 6.0f,
        "original base feature order");
  Check(features[4] == 12.5f && features[13] == 4.5f,
        "LM feature order");
  Check(features[19] == 0.0f && features[36] == 1.0f,
        "change and Levenshtein features");
  Check(Near(features[37], 1.0 / 6.0, 1e-7),
        "Levenshtein per-letter feature");
  Check(Near(features[38], 5.0 / 6.0, 1e-7), "BLEU-1 feature");
  Check(Near(features[39], std::sqrt((5.0 / 6.0) * (3.0 / 5.0)),
             1e-7),
        "BLEU-2 cumulative geometric mean");
  Check(Near(features[40], 0.5, 1e-7),
        "BLEU-3 cumulative geometric mean");
  Check(features[41] == 0.0f, "BLEU-4 feature");
  Check(Near(features[42], 5.0 / 6.0, 1e-7) &&
            Near(features[43], 3.0 / 5.0, 1e-7) &&
            Near(features[44], 1.0 / 4.0, 1e-7) &&
            features[45] == 0.0f,
        "raw letter precision order");
  Check(Near(features[23], -0.25, 1e-7) &&
            Near(features[27], -4.25, 1e-7) &&
            Near(features[28], 0.75 / 6.0, 1e-7) &&
            Near(features[30], -0.2, 1e-7) &&
            Near(features[35], 0.8 / 6.0, 1e-7),
        "recovered transfeme distance offsets and normalization");
  Check(features[46] == 0.0f, "equal-length non-permutation feature");
  Check(features[48] == 1.0f, "same-stem feature");
  Check(features[49] == 5.25f && features[50] == 2.75f,
        "Autocc sum combos");
  Check(features[51] == 8.0f && features[52] == 6.0f &&
            features[53] == 0.0f,
        "Autocc subtraction combos");
  Check(Near(features[54], 0.05, 1e-7) &&
            Near(features[78], 1.25, 1e-7),
        "keyboard-specific feature span");

  AutoccFeatureRequest unequal = request;
  unequal.original = "\xD0\xBF\xD1\x80\xD0\xB5";  // пре
  unequal.forward_distance.distance = 21.406187057495117f;
  unequal.reverse_distance.distance = 30.342243194580078f;
  const auto unequal_build = runner.BuildFeatures(unequal, &features);
  Check(unequal_build.ok(), unequal_build.message.c_str());
  Check(features[46] == -1.0f,
        "length-mismatched permutation feature must use Yandex sentinel");
  Check(Near(features[28], 7.135395526885986, 1e-6) &&
            Near(features[35], 5.057040691375732, 1e-6),
        "forward distance divides by original and reverse by correction");

  std::array<float, AutoccModelRunner::kFeatureCount> zeros{};
  const double score_default =
      runner.ScoreFeatures(zeros, AutoccVariant::kDefault);
  const double score_k13321 =
      runner.ScoreFeatures(zeros, AutoccVariant::kKeyboard13321);
  const double score_k13421 =
      runner.ScoreFeatures(zeros, AutoccVariant::kKeyboard13421);
  Check(Near(score_default, 0.2221801844202315),
        "current default forest independent golden vector");
  Check(Near(score_k13321, 0.15390414547251574),
        "K13321 forest independent golden vector");
  Check(Near(score_k13421, 0.2221801844202315),
        "K13421 forest independent golden vector");
  Check(Near(score_default, score_k13421, 1e-15),
        "Yandex 122.5 default must alias the promoted K13421 forest");

  auto variant_probe = zeros;
  variant_probe[4] = 20.0f;
  const double variant_default =
      runner.ScoreFeatures(variant_probe, AutoccVariant::kDefault);
  const double variant_k13321 =
      runner.ScoreFeatures(variant_probe, AutoccVariant::kKeyboard13321);
  Check(Near(variant_default, 0.26955624661312566) &&
            Near(variant_k13321, 0.9572359294660544) &&
            variant_default < AutoccModelRunner::kThreshold &&
            variant_k13321 >= AutoccModelRunner::kThreshold,
        "legacy K13321 selection must change the model-backed decision");

  // Live oracle captured from Yandex Keyboard 122.5 while it corrected
  // "превет" to "привет". Provenance: tools/fixtures/
  // autocc_yandex_1225_prevet_privet.json.
  const std::array<float, AutoccModelRunner::kFeatureCount>
      yandex_1225_prevet_to_privet = {
          1.0f,          6.0f,          0.0f,
          0.0f,          20.120281219482422f,
          37.661148071289062f,           42.661148071289062f,
          42.661148071289062f,           21.702564239501953f,
          1.0f,          6.0f,          0.0f,
          0.0f,          5.2866168022155762f,
          8.8398838043212891f,           7.7097001075744629f,
          7.7097001075744629f,           21.875040054321289f,
          1.0f,          0.0f,          0.0f,
          0.0f,          8.2340679168701172f,
          7.2340679168701172f,           6.2340679168701172f,
          5.2340679168701172f,           4.2340679168701172f,
          3.2340679168701172f,           1.3723446130752563f,
          7.7629570960998535f,           6.7629570960998535f,
          5.7629570960998535f,           4.7629570960998535f,
          3.7629570960998535f,           2.7629570960998535f,
          1.2938262224197388f,           1.0f,
          0.1666666716337204f,           0.83333331346511841f,
          0.70710676908493042f,          0.5f,
          0.0f,          0.83333331346511841f,
          0.60000002384185791f,          0.25f,
          0.0f,          0.0f,          1.0f,
          0.0f,          13.520685195922852f,
          15.943767547607422f,           14.833663940429688f,
          34.951446533203125f,           0.0f,
          131.07817077636719f,           21.84636116027832f,
          152.78073120117188f,           25.463455200195312f,
          85.452491760253906f,           14.242081642150879f,
          1.5339303016662598f,           0.25565505027770996f,
          3.8727486133575439f,           131.07817077636719f,
          21.84636116027832f,            152.78073120117188f,
          25.463455200195312f,           85.452491760253906f,
          14.242081642150879f,           1.5339303016662598f,
          0.25565505027770996f,          3.8727486133575439f,
          0.0f,          -26.766494750976562f,
          -0.1666666716337204f,          -0.65038615465164185f,
          -6.0383334159851074f,          -0.1428571492433548f,
          -0.21602062880992889f,
      };
  const double yandex_1225_score = runner.ScoreFeatures(
      yandex_1225_prevet_to_privet, AutoccVariant::kDefault);
  if (!Near(yandex_1225_score, 0.9897022145044378, 1e-12)) {
    std::cerr << std::setprecision(17)
              << "Yandex 122.5 oracle mismatch: actual=" << yandex_1225_score
              << ", expected=" << 0.9897022145044378 << '\n';
  }
  Check(Near(yandex_1225_score, 0.9897022145044378, 1e-12),
        "default forest must match live Yandex 122.5 Autocc oracle");

  auto invalid = zeros;
  invalid[7] = std::numeric_limits<float>::quiet_NaN();
  Check(std::isnan(runner.ScoreFeatures(invalid, AutoccVariant::kDefault)),
        "non-finite Autocc feature must be rejected");
  Check(runner.invocation_count() == 7 && runner.scored_count() == 6,
        "Autocc engagement counters");

  std::vector<uint8_t> corrupt_schema = schema;
  corrupt_schema.back() ^= 0x01u;
  AutoccModelAssets bad_assets = assets;
  bad_assets.feature_schema = {corrupt_schema.data(), corrupt_schema.size()};
  Check(!runner.Load(bad_assets).ok(), "corrupt schema reload must fail");
  Check(runner.is_loaded(), "failed atomic reload must preserve prior models");
  Check(Near(runner.ScoreFeatures(zeros, AutoccVariant::kDefault),
             score_default),
        "prior Autocc model must survive failed reload");

  const std::string diagnostics = runner.diagnostics();
  Check(diagnostics.find("forest_contract=exact_hobl79") !=
            std::string::npos,
        "Autocc diagnostics exact forest contract");
  Check(diagnostics.find("variant_default_count=") != std::string::npos,
        "Autocc diagnostics variant counters");

  std::cout << "PASS: exact Autocc default/K13321/K13421 forests, 79-feature "
               "schema, live Yandex 122.5 oracle, variant delta and atomic "
               "reload\n";
  return 0;
}
