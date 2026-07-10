#pragma once

#include "status.h"
#include "succinct_trie.h"
#include "ynnlm_bundle.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hoskey::engine {

struct NeuralVocabCandidate {
  std::string text;
  int32_t weight = 0;
};

struct BeamNode {
  std::vector<uint32_t> tokens;
  std::string text;
  float log_prob;
  std::vector<float> hidden_state; // typically 500 or 2500 depending on the model
};

class NeuralLM {
 public:
  NeuralLM() = default;
  ~NeuralLM();

  NeuralLM(const NeuralLM&) = delete;
  NeuralLM& operator=(const NeuralLM&) = delete;
  NeuralLM(NeuralLM&&) = delete;
  NeuralLM& operator=(NeuralLM&&) = delete;

  // Load the MindSpore Lite model (.ms file)
  [[nodiscard]] Status Load(const uint8_t* data, size_t size);

  // Load a raw Yandex ynnlm_v1 bundle vocabulary. This does not enable neural
  // inference by itself; it provides the exact word/vocab signal used as a
  // local fallback when MindSpore Lite is unavailable.
  [[nodiscard]] Status LoadVocabulary(const uint8_t* data, size_t size);

  void Reset();

  [[nodiscard]] bool is_loaded() const { return loaded_; }
  [[nodiscard]] bool has_vocabulary() const { return vocabulary_loaded_; }
  [[nodiscard]] size_t vocabulary_size() const { return vocabulary_.size(); }
  [[nodiscard]] const std::string& diagnostics() const { return diagnostics_; }

  [[nodiscard]] bool VocabularyContains(std::string_view word) const;
  [[nodiscard]] int32_t VocabularyWeight(std::string_view word) const;
  [[nodiscard]] std::vector<NeuralVocabCandidate> VocabularyCompletions(
      std::string_view prefix, size_t max_results = 12) const;

  // Generate completions using Beam Search
  // context_tokens: tokens already typed (from SuccinctTrie::Tokenize)
  // prefix: current word prefix to constrain generation
  // beam_width: number of beams to keep
  // max_len: maximum tokens to generate
  std::vector<std::string> GenerateCompletions(
      const SuccinctTrie& trie,
      const std::vector<uint32_t>& context_tokens,
      const std::string& prefix,
      size_t beam_width = 5,
      size_t max_len = 10) const;

 private:
  // Helper to run a single step of the model
  // Returns top-K (token_id, log_prob) and the new hidden state
  bool PredictNextTokens(
      uint32_t current_token,
      const std::vector<float>& current_state,
      size_t top_k,
      std::vector<std::pair<uint32_t, float>>& out_top_tokens,
      std::vector<float>& out_next_state) const;

  bool loaded_ = false;
  void* model_ = nullptr;    // mindspore::Model*
  void* context_ = nullptr;  // mindspore::Context*
  std::string diagnostics_ = "not_loaded";
  bool vocabulary_loaded_ = false;
  YnnlmV1Bundle vocabulary_bundle_;
  std::vector<std::string> vocabulary_;
  std::unordered_map<std::string, uint32_t> vocabulary_rank_;
  
  size_t state_size_ = 2500; // Default state size from analysis
  size_t vocab_size_ = 7000; // Default subword vocabulary size
};

}  // namespace hoskey::engine
