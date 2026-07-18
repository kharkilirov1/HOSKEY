#include "hosdict_engine/char_trigram_model.h"
#include "hosdict_engine/hosdict_container.h"
#include "hosdict_engine/ml_ranker.h"
#include "hosdict_engine/rnnt_swipe_model.h"
#include "hosdict_engine/tfmtrie_decoder.h"
#include "hosdict_engine/swipe_decoder.h"
#include "hosdict_engine/ynnlm_word_chain.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

using hoskey::engine::HosdictContainer;
using hoskey::engine::CharTrigramModel;
using hoskey::engine::NgramCalibration;
using hoskey::engine::NeuralRankerFeatureInput;
using hoskey::engine::TfmTrieDecoder;
using hoskey::engine::MLRanker;

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: test_neural_ranker_features <ru.hoskey.hosdict>\n");
    return 2;
  }

  HosdictContainer package;
  auto status = package.Open(std::filesystem::path(argv[1]));
  Check(status.ok(), "open real hosdict");

  size_t rnnt_component_bytes = 0;
  for (const char* key : {
           "rnnt.sampling", "rnnt.pre0", "rnnt.pre1", "rnnt.pre2",
           "rnnt.pre3", "rnnt.post0", "rnnt.post1", "rnnt.post2",
           "rnnt.post3", "rnnt.joint", "rnnt.predictor.char_dec"}) {
    const uint8_t* component_data = nullptr;
    size_t component_size = 0;
    const auto component_status =
        package.GetAssetData(key, &component_data, &component_size);
    Check(component_status.ok() && component_data != nullptr &&
              component_size >= 10000,
          "load converted RNNT component");
    rnnt_component_bytes += component_size;
  }
  Check(rnnt_component_bytes >= 1400000,
        "converted RNNT component set has expected size");

  hoskey::engine::RnntSwipeModelBuffers rnnt_buffers;
  const auto load_rnnt_buffer = [&](const char* key,
                                    hoskey::engine::RnntModelBuffer* out) {
    const uint8_t* bytes = nullptr;
    size_t byte_count = 0;
    const auto get_status = package.GetAssetData(key, &bytes, &byte_count);
    Check(get_status.ok() && bytes != nullptr && byte_count >= 8,
          "materialize RNNT negative-test buffer");
    *out = {bytes, byte_count};
  };
  load_rnnt_buffer("rnnt.sampling", &rnnt_buffers.sampling);
  for (size_t i = 0; i < rnnt_buffers.pre.size(); ++i) {
    const std::string pre_key = "rnnt.pre" + std::to_string(i);
    const std::string post_key = "rnnt.post" + std::to_string(i);
    load_rnnt_buffer(pre_key.c_str(), &rnnt_buffers.pre[i]);
    load_rnnt_buffer(post_key.c_str(), &rnnt_buffers.post[i]);
  }
  load_rnnt_buffer("rnnt.joint", &rnnt_buffers.joint);
  load_rnnt_buffer("rnnt.predictor.char_dec", &rnnt_buffers.predictor);
  std::string rnnt_buffer_diagnostics;
  status = hoskey::engine::RnntSwipeModel::ValidateBuffers(
      rnnt_buffers, &rnnt_buffer_diagnostics);
  Check(status.ok() && rnnt_buffer_diagnostics == "valid:11_models",
        "RNNT serialized component contract accepts coherent set");

  const std::array<std::string, 11> rnnt_component_names = {
      "sampling", "pre0", "pre1", "pre2", "pre3", "post0",
      "post1", "post2", "post3", "joint", "predictor"};
  for (size_t omitted = 0; omitted < rnnt_component_names.size(); ++omitted) {
    auto missing = rnnt_buffers;
    if (omitted == 0) {
      missing.sampling = {};
    } else if (omitted <= 4) {
      missing.pre[omitted - 1] = {};
    } else if (omitted <= 8) {
      missing.post[omitted - 5] = {};
    } else if (omitted == 9) {
      missing.joint = {};
    } else {
      missing.predictor = {};
    }
    rnnt_buffer_diagnostics.clear();
    status = hoskey::engine::RnntSwipeModel::ValidateBuffers(
        missing, &rnnt_buffer_diagnostics);
    Check(!status.ok() &&
              rnnt_buffer_diagnostics.find(rnnt_component_names[omitted]) !=
                  std::string::npos &&
              rnnt_buffer_diagnostics.find(":missing") != std::string::npos,
          "RNNT serialized contract rejects each omitted component by name");
  }
  std::array<uint8_t, 8> corrupt_mindir{};
  auto corrupt_rnnt = rnnt_buffers;
  corrupt_rnnt.sampling = {corrupt_mindir.data(), corrupt_mindir.size()};
  rnnt_buffer_diagnostics.clear();
  status = hoskey::engine::RnntSwipeModel::ValidateBuffers(
      corrupt_rnnt, &rnnt_buffer_diagnostics);
  Check(!status.ok() && rnnt_buffer_diagnostics == "invalid:sampling:magic",
        "RNNT serialized contract rejects corrupt MindIR magic");

  const uint8_t* ynnlm_emb_data = nullptr;
  size_t ynnlm_emb_size = 0;
  status = package.GetAssetData("neural.word.emb", &ynnlm_emb_data,
                                &ynnlm_emb_size);
  Check(status.ok() && ynnlm_emb_data != nullptr && ynnlm_emb_size >= 8,
        "load YNNLM word embedding MindIR");
  status = hoskey::engine::YnnlmWordChain::ValidateSerializedModel(
      ynnlm_emb_data, ynnlm_emb_size, "emb");
  Check(status.ok(), "YNNLM serialized contract accepts packaged MindIR");
  status = hoskey::engine::YnnlmWordChain::ValidateSerializedModel(
      nullptr, 0, "emb");
  Check(!status.ok() && status.message.find("missing") != std::string::npos,
        "YNNLM serialized contract rejects a missing graph");
  status = hoskey::engine::YnnlmWordChain::ValidateSerializedModel(
      corrupt_mindir.data(), corrupt_mindir.size(), "emb");
  Check(!status.ok() &&
            status.message.find("invalid MindIR magic") != std::string::npos,
        "YNNLM serialized contract rejects a corrupt graph");

  const auto position0 =
      hoskey::engine::RnntSwipeModel::PositionalEmbedding(0);
  bool position0_matches = true;
  for (size_t i = 0; i < position0.size(); i += 2) {
    position0_matches = position0_matches &&
                        std::fabs(position0[i]) < 1.0e-7f &&
                        std::fabs(position0[i + 1] - 1.0f) < 1.0e-7f;
  }
  Check(position0_matches, "RNNT position zero is exact sin/cos identity");
  const auto position1 =
      hoskey::engine::RnntSwipeModel::PositionalEmbedding(1);
  Check(std::fabs(position1[0] - std::sin(1.0f)) < 1.0e-6f &&
            std::fabs(position1[1] - std::cos(1.0f)) < 1.0e-6f,
        "RNNT position one matches recovered first sin/cos pair");
  const float position1_scale2 = std::pow(10000.0f, -0.02f);
  Check(std::fabs(position1[2] - std::sin(position1_scale2)) < 1.0e-6f &&
            std::fabs(position1[3] - std::cos(position1_scale2)) < 1.0e-6f,
        "RNNT position one matches recovered frequency schedule");

  const uint8_t* data = nullptr;
  size_t size = 0;
  status = package.GetAssetData("neural.char_model", &data, &size);
  Check(status.ok() && data != nullptr && size > 0,
        "load neural.char_model");
  CharTrigramModel char_model;
  status = char_model.Load(data, size);
  Check(status.ok() && char_model.is_loaded(), "parse 3GRM char model");
  float char_score = 0.0f;
  Check(char_model.Score("кот", &char_score) &&
            std::fabs(char_score - 14.8707685f) < 2.0e-5f,
        "3GRM reproduces Android кот letter_lm_main");
  Check(char_model.Score("кто", &char_score) &&
            std::fabs(char_score - 16.2557411f) < 2.0e-5f,
        "3GRM reproduces Android кто letter_lm_main");
  Check(!char_model.Score("cat", &char_score),
        "3GRM rejects words outside its alphabet");

  data = nullptr;
  size = 0;
  status = package.GetAssetData("rules.transfeme", &data, &size);
  Check(status.ok() && data != nullptr && size > 0, "load rules.transfeme");

  TfmTrieDecoder tfm;
  status = tfm.Parse(data, size);
  Check(status.ok() && tfm.is_parsed(), "parse rules.transfeme");

  const float exact = tfm.Proximity("кот", "кот");
  const float completion = tfm.Proximity("кот", "котик");
  const float confusable = tfm.Proximity("кот", "кат");
  const float permutation = tfm.Proximity("кот", "кто");
  // The parsed Cyrillic classes are broad enough that arbitrary Cyrillic
  // pairs may still share a substitution edge. ASCII is outside this asset's
  // substitution alphabet and is a deterministic unrelated control.
  const float unrelated = tfm.Proximity("кот", "abc");

  Check(exact > 0.0f, "exact transfeme proximity is non-zero");
  Check(std::fabs(completion - exact) < 1.0e-4f,
        "completion suffix does not change aligned proximity");
  Check(confusable > 0.0f, "parsed confusable edge contributes weight");
  Check(std::fabs(unrelated) < 1.0e-6f,
        "unrelated aligned letters contribute zero");

  const NgramCalibration calibration{
      .scale = 0.7130374807f, .bias = -3.5576391953f,
      .low = 0.0f, .high = 64.0f};
  float calibrated = 0.0f;
  const float android_exact = 13.38831901550293f;
  const float android_candidate = 8.873350143432617f;
  const float hoskey_exact_nll = 23.765873f;
  const float hoskey_candidate_nll = 17.4338512f;
  Check(hoskey::engine::CalibrateNgramLogit(
            std::exp(-hoskey_exact_nll), calibration, &calibrated) &&
            std::fabs(calibrated - android_exact) < 1.0e-5f,
        "device calibration reproduces Android original feature");
  Check(hoskey::engine::CalibrateNgramLogit(
            std::exp(-hoskey_candidate_nll), calibration, &calibrated) &&
            std::fabs(calibrated - android_candidate) < 1.0e-5f,
        "device calibration reproduces Android correction feature");
  Check(hoskey::engine::CalibrateNgramLogit(0.0f, calibration, &calibrated) &&
            std::fabs(calibrated - calibration.high) < 1.0e-6f,
        "zero probability maps to high clamp");
  Check(!hoskey::engine::CalibrateNgramLogit(-0.1f, calibration, &calibrated),
        "negative probability is rejected");
  Check(!hoskey::engine::CalibrateNgramLogit(1.1f, calibration, &calibrated),
        "probability above one is rejected");
  Check(!hoskey::engine::CalibrateNgramLogit(
            std::numeric_limits<float>::quiet_NaN(), calibration, &calibrated),
        "ngram calibration rejects NaN");
  Check(!hoskey::engine::CalibrateNgramLogit(
            std::numeric_limits<float>::infinity(), calibration, &calibrated),
        "ngram calibration rejects infinity");

  NeuralRankerFeatureInput input;
  input.original = "кот";
  input.correction = "котик";
  input.raw_original_logit = std::exp(-hoskey_exact_nll);
  input.raw_correction_logit = std::exp(-hoskey_candidate_nll);
  input.has_original_logit = false;
  input.original_static = 0.2f;
  input.correction_static = 0.7f;
  input.transfeme_proximity = confusable;
  input.original_known = true;

  float android_exact_distance = 0.0f;
  float android_candidate_distance = 0.0f;
  Check(hoskey::engine::CalibrateTransfemeDistance(
            exact, &android_exact_distance) &&
            std::fabs(android_exact_distance - 0.6086180210f) < 1.0e-5f,
        "transfeme calibration reproduces Android exact distance");
  Check(hoskey::engine::CalibrateTransfemeDistance(
            permutation, &android_candidate_distance) &&
            std::fabs(android_candidate_distance - 10.1002511978f) < 1.0e-5f,
        "transfeme calibration reproduces Android permutation distance");

  float android_geometry = 0.0f;
  Check(hoskey::engine::CalibrateGeometryDistancePerLetter(
            android_exact_distance, &android_geometry) &&
            std::fabs(android_geometry - 12.5285615921f) < 1.0e-5f,
        "geometry calibration reproduces Android exact distance");
  Check(hoskey::engine::CalibrateGeometryDistancePerLetter(
            android_candidate_distance, &android_geometry) &&
            std::fabs(android_geometry - 24.1208953857f) < 1.0e-5f,
        "geometry calibration reproduces Android permutation distance");

  float static_feature = 0.0f;
  Check(hoskey::engine::CalibrateStaticUnigramRank(998, &static_feature) &&
            std::fabs(static_feature - 13.8706169128f) < 1.0e-5f,
        "static unigram calibration reproduces Android кот feature");
  Check(hoskey::engine::CalibrateStaticUnigramRank(73, &static_feature) &&
            std::fabs(static_feature - 10.0891437531f) < 1.0e-5f,
        "static unigram calibration reproduces Android кто feature");
  Check(hoskey::engine::CalibrateStaticNgramRank(998, &static_feature) &&
            std::fabs(static_feature - 13.7442169189f) < 1.0e-5f,
        "static ngram calibration reproduces Android кот feature");
  Check(hoskey::engine::CalibrateStaticNgramRank(73, &static_feature) &&
            std::fabs(static_feature - 10.0984344482f) < 1.0e-5f,
        "static ngram calibration reproduces Android кто feature");

  const auto features =
      hoskey::engine::BuildNeuralRankerFeatures(input, calibration);
  const auto x44 = features.ToVector44();
  Check(std::fabs(x44[1] - calibration.high) < 1.0e-6f,
        "absent original probability falls back to worst negative log");
  Check(std::fabs(x44[8] - android_candidate) < 1.0e-5f,
        "candidate probability maps to Android feature 8 scale");
  Check(std::fabs(x44[16] - confusable) < 1.0e-6f,
        "transfeme proximity maps to feature 16");
  Check(std::fabs(x44[17] - confusable / 5.0f) < 1.0e-6f,
        "per-letter transfeme maps to feature 17");
  Check(std::fabs(x44[28] - (confusable + android_candidate)) < 1.0e-5f,
        "combo feature 28 uses calibrated candidate probability");
  Check(std::fabs(x44[30] - (calibration.high - android_candidate)) < 1.0e-5f,
        "combo feature 30 uses worst absent original probability");

  NeuralRankerFeatureInput geometry_exact;
  geometry_exact.original = "кот";
  geometry_exact.correction = "кот";
  geometry_exact.original_letter_lm_main = 14.8707714081f;
  geometry_exact.correction_letter_lm_main = 14.8707714081f;
  geometry_exact.original_transfeme_proximity = android_exact_distance;
  geometry_exact.transfeme_proximity = android_exact_distance;
  geometry_exact.has_geometry = true;
  const auto exact44 =
      hoskey::engine::BuildNeuralRankerFeatures(geometry_exact, calibration)
          .ToVector44();
  const float expected_exact_geometry[] = {
      12.5285615921f, 37.5856857300f, 12.5285615921f, 1.0f,
      5.2902560234f, 12.5285615921f, 52.4564552307f,
      37.5856857300f, 12.5285615921f, 1.0f};
  for (size_t i = 0; i < 10; ++i) {
    Check(std::fabs(exact44[33 + i] - expected_exact_geometry[i]) < 2.0e-5f,
          "exact geometry feature matches Android vector");
  }

  const float blended_kotoryi =
      hoskey::engine::BlendMatrixNetWithVocabularyRank(9.4034309387f, 245);
  const float blended_kotoraya =
      hoskey::engine::BlendMatrixNetWithVocabularyRank(11.8636732101f, 456);
  const float blended_kotorye =
      hoskey::engine::BlendMatrixNetWithVocabularyRank(9.2596235275f, 268);
  const float blended_kotorym =
      hoskey::engine::BlendMatrixNetWithVocabularyRank(12.0245780945f, 2187);
  Check(blended_kotoryi > blended_kotoraya &&
            blended_kotoraya > blended_kotorye &&
            blended_kotorye > blended_kotorym,
        "vocabulary prior reproduces Android messenger top completion family");
  Check(hoskey::engine::BlendMatrixNetWithVocabularyRank(
            0.3f, -1, 107989) < blended_kotorym,
        "out-of-vocabulary completion cannot bypass the rank prior");

  NeuralRankerFeatureInput geometry_permutation = geometry_exact;
  geometry_permutation.correction = "кто";
  geometry_permutation.correction_letter_lm_main = 16.2557430267f;
  geometry_permutation.transfeme_proximity = android_candidate_distance;
  const auto permutation44 = hoskey::engine::BuildNeuralRankerFeatures(
                                 geometry_permutation, calibration)
                                 .ToVector44();
  const float expected_permutation_geometry[] = {
      24.1208953857f, 37.5856857300f, 12.5285615921f, 1.9252724648f,
      5.5986804962f, 24.1208953857f, 87.2334518433f,
      37.5856857300f, 12.5285615921f, 1.9252724648f};
  for (size_t i = 0; i < 10; ++i) {
    Check(std::fabs(permutation44[33 + i] -
                    expected_permutation_geometry[i]) < 2.0e-5f,
          "permutation geometry feature matches Android vector");
  }

  const std::vector<std::string> rank_words = {
      "кот", "котик", "котя", "коть", "кошка", "котz"};
  const std::vector<float> logits = {
      1.0f, 5.0f, 5.0f, 4.0f, 100.0f,
      std::numeric_limits<float>::quiet_NaN()};
  const auto selected = hoskey::engine::SelectYnnlmPredictions(
      "кот", rank_words, logits, 2);
  const auto lexical_index =
      hoskey::engine::BuildYnnlmLexicographicIndex(rank_words);
  const auto indexed_selected =
      hoskey::engine::SelectYnnlmPredictionsIndexed(
          "кот", rank_words, logits, lexical_index, 2);
  Check(selected.candidates.size() == 2,
        "prefix selector returns requested top-k");
  Check(selected.candidates.size() >= 2 &&
            selected.candidates[0].word == "котик" &&
            selected.candidates[1].word == "котя",
        "prefix selector is descending and deterministic for ties");
  Check(selected.has_original_logit &&
            std::fabs(selected.original_raw_logit - 1.0f) < 1.0e-6f,
        "exact prefix logit retained outside candidate top-k");
  bool saw_non_finite = false;
  for (const auto& candidate : selected.candidates) {
    if (!std::isfinite(candidate.raw_logit)) saw_non_finite = true;
  }
  Check(!saw_non_finite, "non-finite logits are excluded");
  Check(indexed_selected.candidates.size() == selected.candidates.size() &&
            indexed_selected.candidates[0].word ==
                selected.candidates[0].word &&
            indexed_selected.candidates[1].word ==
                selected.candidates[1].word &&
            indexed_selected.has_original_logit ==
                selected.has_original_logit &&
            std::fabs(indexed_selected.original_raw_logit -
                      selected.original_raw_logit) < 1.0e-6f,
        "lexicographic prefix index preserves exact selector output");
  float arbitrary_probability = 0.0f;
  Check(hoskey::engine::LookupYnnlmProbability(
            "котя", rank_words, logits, &arbitrary_probability) &&
            std::fabs(arbitrary_probability - 5.0f) < 1.0e-6f,
        "decoder probability lookup retains non-prefix candidate scores");
  Check(!hoskey::engine::LookupYnnlmProbability(
            "нет", rank_words, logits, &arbitrary_probability),
        "decoder probability lookup reports missing words");
  Check(!hoskey::engine::ShouldRunYnnlmAuxiliary(64, 64),
        "dense word candidates skip auxiliary decoders");
  Check(!hoskey::engine::ShouldRunYnnlmAuxiliary(6, 6),
        "full UI candidate pool skips auxiliary decoders");
  Check(hoskey::engine::ShouldRunYnnlmAuxiliary(5, 64),
        "sparse word candidates run auxiliary decoders");

  hoskey::engine::YnnlmCharScore prevet_score;
  prevet_score.available = true;
  prevet_score.total_log2_probability = -29.55447135f;
  prevet_score.eos_log2_probability = -1.49915751f;
  hoskey::engine::YnnlmCharScore privet_score;
  privet_score.available = true;
  privet_score.total_log2_probability = -11.22234741f;
  privet_score.eos_log2_probability = -0.25275129f;
  std::array<float, 6> autocc_char_features{};
  Check(hoskey::engine::BuildYnnlmAutoccCharFeatures(
            prevet_score, 6, privet_score, 7, &autocc_char_features),
        "YNNLM Autocc char tail builds from available scores");
  const float expected_autocc_char_features[] = {
      -29.55447135f, -1.0f / 6.0f, -1.49915751f,
      -11.22234741f, -1.0f / 7.0f, -0.25275129f};
  for (size_t i = 0; i < autocc_char_features.size(); ++i) {
    Check(std::fabs(autocc_char_features[i] -
                    expected_autocc_char_features[i]) < 2.0e-6f,
          "YNNLM Autocc char tail matches recovered feature order");
  }
  prevet_score.available = false;
  Check(!hoskey::engine::BuildYnnlmAutoccCharFeatures(
            prevet_score, 6, privet_score, 7, &autocc_char_features),
        "YNNLM Autocc char tail rejects unavailable model scores");

  std::vector<hoskey::engine::SwipePoint> swipe_path;
  hoskey::engine::SwipePoint previous;
  bool has_previous = false;
  for (const char32_t cp : std::u32string(U"привет")) {
    hoskey::engine::SwipePoint center;
    Check(hoskey::engine::RussianSwipeKeyCenter(cp, 1000.0, 400.0, &center),
          "Russian swipe key center exists");
    if (has_previous) {
      for (int step = 1; step <= 4; ++step) {
        const double t = static_cast<double>(step) / 4.0;
        swipe_path.push_back(
            {previous.x + (center.x - previous.x) * t,
             previous.y + (center.y - previous.y) * t});
      }
    } else {
      swipe_path.push_back(center);
    }
    previous = center;
    has_previous = true;
  }
  const std::vector<std::string> swipe_words = {
      "пример", "привет", "привез", "проект", "просто", "приз"};
  const std::unordered_map<std::string, uint32_t> swipe_ranks = {
      {"привет", 1}, {"пример", 20}, {"просто", 30},
      {"привез", 100}, {"проект", 200}, {"приз", 300}};
  for (size_t i = 0; i < swipe_path.size(); ++i) {
    swipe_path[i].time_ms = static_cast<uint32_t>(i * 12);
  }
  const auto rnnt_windows = hoskey::engine::BuildRussianSwipeRnntWindows(
      swipe_path, 1000.0, 400.0);
  Check(!rnnt_windows.empty(),
        "RNNT distance sampler produces sliding five-point windows");
  if (!rnnt_windows.empty()) {
    const auto& first_window = rnnt_windows.front();
    Check(std::fabs(first_window[2]) < 1.0e-7f,
          "RNNT first timestamp is gesture-relative");
    Check(std::fabs(first_window[9 + 15] - 1.0f) < 1.0e-7f,
          "RNNT hitbox order maps row-2 п to recovered slot 15");
    bool layout_contract = true;
    for (size_t row = 0; row < 5; ++row) {
      layout_contract =
          layout_contract &&
          std::fabs(first_window[row * 52 + 50] - 1.0f) < 1.0e-7f &&
          std::fabs(first_window[row * 52 + 51]) < 1.0e-7f;
    }
    Check(layout_contract,
          "RNNT standard Russian layout_default/layout_extra contract");
    const auto& last_window = rnnt_windows.back();
    Check(std::fabs(last_window[4 * 52 + 49] - 1.0f) < 1.0e-7f,
          "RNNT final sampled point carries is_last");
  }
  const auto swipe = hoskey::engine::DecodeRussianSwipe(
      swipe_path, 1000.0, 400.0, swipe_words, swipe_ranks, 3);
  Check(!swipe.empty() && swipe.front().word == "привет",
        "synthetic Russian gesture decodes to привет");
  hoskey::engine::RussianSwipeDecoder indexed_swipe_decoder;
  indexed_swipe_decoder.Build(swipe_words, swipe_ranks);
  size_t swipe_pool_size = 0;
  const auto indexed_swipe = indexed_swipe_decoder.Decode(
      swipe_path, 1000.0, 400.0, 3, &swipe_pool_size);
  Check(!indexed_swipe.empty() && indexed_swipe.front().word == "привет",
        "endpoint-indexed Russian gesture decodes to привет");
  Check(swipe_pool_size == 2,
        "endpoint index limits п..т gesture to matching vocabulary bucket");

  MLRanker swipe_primary_ranker;
  MLRanker swipe_backoff_ranker;
  MLRanker swipe_blocker_ranker;
  data = nullptr;
  size = 0;
  status = package.GetAssetData("ranking.swipe.matrixnet", &data, &size);
  Check(status.ok() && data != nullptr && size > 0,
        "load primary swipe HRNK asset");
  status = swipe_primary_ranker.LoadMatrixNet(data, size);
  Check(status.ok() && swipe_primary_ranker.matrixnet_feature_count() == 47,
        "parse primary 47-feature swipe MatrixNet");
  status = package.GetAssetData("ranking.swipe_backoff.matrixnet", &data, &size);
  Check(status.ok() && data != nullptr && size > 0,
        "load swipe backoff HRNK asset");
  status = swipe_backoff_ranker.LoadMatrixNet(data, size);
  Check(status.ok() && swipe_backoff_ranker.matrixnet_feature_count() == 42,
        "parse 42-feature swipe backoff MatrixNet");
  Check(std::isnan(swipe_backoff_ranker.ScoreMatrixNet(
            std::vector<float>(41, 0.0f))),
        "generic MatrixNet rejects a mismatched feature vector");
  status = package.GetAssetData("ranking.swipe_blocker.matrixnet", &data, &size);
  Check(status.ok() && data != nullptr && size > 0,
        "load swipe blocker HRNK asset");
  status = swipe_blocker_ranker.LoadMatrixNet(data, size);
  Check(status.ok() && swipe_blocker_ranker.matrixnet_feature_count() == 31,
        "parse 31-feature swipe blocker MatrixNet");

  for (size_t i = 0; i < swipe_path.size(); ++i) {
    swipe_path[i].time_ms = static_cast<uint32_t>(100 + i * 24);
  }
  hoskey::engine::SwipeDecodeOptions swipe_options;
  swipe_options.max_results = 3;
  swipe_options.backoff_ranker = &swipe_backoff_ranker;
  swipe_options.blocker_ranker = &swipe_blocker_ranker;
  hoskey::engine::SwipeDecodeDiagnostics swipe_diagnostics;
  const auto matrixnet_swipe = indexed_swipe_decoder.Decode(
      swipe_path, 1000.0, 400.0, swipe_options, &swipe_diagnostics);
  std::printf("swipe MatrixNet scored=%zu blocker=%.6f blocked=%d top=%.6f\n",
              swipe_diagnostics.model_scored,
              swipe_diagnostics.blocker_score,
              swipe_diagnostics.blocked ? 1 : 0,
              swipe_diagnostics.top_matrixnet_score);
  Check(swipe_diagnostics.backoff_model_engaged &&
            swipe_diagnostics.model_scored == swipe_pool_size,
        "swipe backoff MatrixNet scores the endpoint candidate pool");
  Check(swipe_diagnostics.blocker_model_engaged &&
            !swipe_diagnostics.blocked,
        "swipe blocker permits a valid synthetic gesture");
  Check(!matrixnet_swipe.empty() && matrixnet_swipe.front().word == "привет",
        "MatrixNet-reranked synthetic gesture retains привет at top-1");

  if (failures != 0) return 1;
  std::printf("PASS: transfeme proximity exact=%.6f completion=%.6f "
              "confusable=%.6f permutation=%.6f unrelated=%.6f\n",
              exact, completion, confusable, permutation, unrelated);
  return 0;
}
