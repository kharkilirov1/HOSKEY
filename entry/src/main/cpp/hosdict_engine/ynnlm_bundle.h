#pragma once

#include "status.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace hoskey::engine {

class YnnlmV1Bundle {
 public:
  [[nodiscard]] Status Parse(const uint8_t* data, size_t size);

  [[nodiscard]] size_t word_count() const { return words_.size(); }
  [[nodiscard]] size_t subword_count() const { return subwords_.size(); }
  [[nodiscard]] size_t application_count() const {
    return application_id_by_package_.size();
  }
  [[nodiscard]] int order() const { return order_; }
  [[nodiscard]] int max_word_len() const { return max_word_len_; }

  [[nodiscard]] const std::vector<std::string>& words() const { return words_; }
  [[nodiscard]] const std::vector<std::string>& subwords() const { return subwords_; }
  [[nodiscard]] const std::vector<uint32_t>& word_rank_to_lex() const {
    return word_rank_to_lex_;
  }
  [[nodiscard]] const std::vector<uint32_t>& word_lex_to_rank() const {
    return word_lex_to_rank_;
  }
  [[nodiscard]] const std::vector<uint32_t>& subword_rank_to_lex() const {
    return subword_rank_to_lex_;
  }
  [[nodiscard]] const std::vector<uint32_t>& subword_lex_to_rank() const {
    return subword_lex_to_rank_;
  }
  [[nodiscard]] const std::unordered_map<std::string, uint32_t>&
  application_ids() const {
    return application_id_by_package_;
  }

  [[nodiscard]] int WordRank(const std::string& word) const;
  [[nodiscard]] uint32_t ApplicationId(const std::string& package_name) const;

 private:
  void Reset();

  int order_ = 0;
  int max_word_len_ = 0;
  std::vector<std::string> words_;
  std::vector<std::string> subwords_;
  std::vector<uint32_t> word_rank_to_lex_;
  std::vector<uint32_t> word_lex_to_rank_;
  std::vector<uint32_t> subword_rank_to_lex_;
  std::vector<uint32_t> subword_lex_to_rank_;
  std::unordered_map<std::string, uint32_t> word_rank_by_text_;
  std::unordered_map<std::string, uint32_t> application_id_by_package_;
};

}  // namespace hoskey::engine
