#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "hosdict_engine/ynnlm_char_decoder.h"

namespace {

void Require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

std::vector<uint8_t> ReadBytes(const char* path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

std::vector<float> ReadFloats(const char* path) {
  std::ifstream input(path, std::ios::binary);
  std::vector<float> values(1500);
  input.read(reinterpret_cast<char*>(values.data()),
             static_cast<std::streamsize>(values.size() * sizeof(float)));
  Require(input.gcount() ==
              static_cast<std::streamsize>(values.size() * sizeof(float)),
          "initial state fixture size");
  return values;
}

std::pair<double, double> Score(
    const hoskey::engine::YnnlmCharDecoder& decoder,
    const std::vector<size_t>& expected_ids,
    const std::vector<float>& initial_state) {
  std::array<float, hoskey::engine::YnnlmCharDecoder::kStateSize> state{};
  std::copy(initial_state.begin(), initial_state.end(), state.begin());
  size_t token = 1;
  double total = 0.0;
  double eos = 0.0;
  for (const size_t expected : expected_ids) {
    std::array<float, hoskey::engine::YnnlmCharDecoder::kVocabularySize>
        probabilities{};
    std::array<float, hoskey::engine::YnnlmCharDecoder::kStateSize> next{};
    Require(decoder.Step(token, state.data(), state.size(), &probabilities,
                         &next),
            "decoder step");
    Require(expected < probabilities.size() && probabilities[expected] > 0.0f,
            "expected probability");
    const double log2_probability = std::log2(probabilities[expected]);
    total += log2_probability;
    if (expected == 2) eos = log2_probability;
    token = expected;
    state = next;
  }
  return {total, eos};
}

void RequireNear(double actual, double expected, double tolerance,
                 const std::string& label) {
  if (std::fabs(actual - expected) > tolerance) {
    throw std::runtime_error(label + ": actual=" + std::to_string(actual) +
                             " expected=" + std::to_string(expected));
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: test_ynnlm_char_decoder char_dec_exact.bin state.bin\n";
    return 2;
  }
  try {
    const std::vector<uint8_t> model = ReadBytes(argv[1]);
    const std::vector<float> initial_state = ReadFloats(argv[2]);
    Require(!model.empty(), "model fixture");

    hoskey::engine::YnnlmCharDecoder decoder;
    const hoskey::engine::Status status = decoder.Load(model.data(), model.size());
    Require(status.ok(), status.message);
    Require(decoder.is_loaded(), "decoder loaded");
    Require(decoder.diagnostics().find("hybrid_asymmetric_int8_v1") !=
                std::string::npos,
            "engagement diagnostic");

    std::array<float, hoskey::engine::YnnlmCharDecoder::kVocabularySize>
        first_probabilities{};
    std::array<float, hoskey::engine::YnnlmCharDecoder::kStateSize> first_state{};
    Require(decoder.Step(1, initial_state.data(), initial_state.size(),
                         &first_probabilities, &first_state),
            "first decoder step");
    RequireNear(first_probabilities[21], 0.0984744877, 2e-7,
                "first P(п|start)");

    const auto prevet = Score(decoder, {21, 22, 11, 8, 11, 24, 2}, initial_state);
    const auto privet = Score(decoder, {21, 22, 14, 8, 11, 24, 2}, initial_state);
    RequireNear(prevet.first, -29.55447135, 2e-5, "превет total");
    RequireNear(prevet.second, -1.49915751, 2e-5, "превет eos");
    RequireNear(privet.first, -11.22234741, 2e-5, "привет total");
    RequireNear(privet.second, -0.25275129, 2e-5, "привет eos");

    std::vector<uint8_t> corrupt = model;
    corrupt[0] ^= 0xFFU;
    hoskey::engine::YnnlmCharDecoder rejected;
    Require(!rejected.Load(corrupt.data(), corrupt.size()).ok(),
            "corrupt magic rejected");
    Require(!rejected.is_loaded(), "corrupt decoder stays unloaded");

    std::cout << "YNNLM CHAR DECODER PASS"
              << " prevet=" << prevet.first << '/' << prevet.second
              << " privet=" << privet.first << '/' << privet.second << '\n';
  } catch (const std::exception& error) {
    std::cerr << "YNNLM CHAR DECODER FAIL: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
