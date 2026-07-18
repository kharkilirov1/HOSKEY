#include "char_trigram_model.h"

#include <cmath>
#include <cstring>
#include <limits>

#include "text_utils.h"

namespace hoskey::engine {
namespace {

constexpr uint8_t kMagic[] = {'3', 'G', 'R', 'M'};
constexpr uint16_t kFormatVersion = 0x0100;
constexpr size_t kHeaderSize = 8;
constexpr uint8_t kUnknownSymbol = 0xff;
constexpr std::u32string_view kAlphabet =
    U"абвгдежзийклмнопрстуфхцчшщъыьэюяё !?():;-";

uint16_t ReadU16Le(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) |
         (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t ReadU32Le(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

}  // namespace

Status CharTrigramModel::Load(const uint8_t* data, size_t size) {
  Reset();
  if (!data) return Status::InvalidArgument("3GRM data is null");

  constexpr size_t kPayloadSize = kTrigramCount * sizeof(uint32_t);
  constexpr size_t kMinimumSize = kHeaderSize + kPayloadSize;
  if (size < kMinimumSize) {
    return Status::ParseError("3GRM payload is truncated");
  }
  if (std::memcmp(data, kMagic, sizeof(kMagic)) != 0) {
    return Status::UnsupportedFormat("3GRM magic mismatch");
  }
  if (ReadU16Le(data + 4) != kFormatVersion) {
    return Status::UnsupportedFormat("unsupported 3GRM version");
  }

  trigrams_.resize(kTrigramCount);
  const uint8_t* payload = data + kHeaderSize;
  for (size_t i = 0; i < kTrigramCount; ++i) {
    trigrams_[i] = ReadU32Le(payload + i * sizeof(uint32_t));
  }

  for (size_t first = 0; first < kAlphabetSize; ++first) {
    uint64_t unigram = 0;
    for (size_t second = 0; second < kAlphabetSize; ++second) {
      const size_t context = first * kAlphabetSize + second;
      uint64_t bigram = 0;
      const size_t base = context * kAlphabetSize;
      for (size_t third = 0; third < kAlphabetSize; ++third) {
        bigram += trigrams_[base + third];
      }
      bigrams_[context] = bigram;
      unigram += bigram;
    }
    unigrams_[first] = unigram;
  }

  loaded_ = true;
  return Status::Ok();
}

void CharTrigramModel::Reset() {
  trigrams_.clear();
  bigrams_.fill(0);
  unigrams_.fill(0);
  loaded_ = false;
}

uint8_t CharTrigramModel::SymbolId(char32_t codepoint) {
  const size_t index = kAlphabet.find(codepoint);
  if (index == std::u32string_view::npos) return kUnknownSymbol;
  // Symbol zero is the boundary marker; serialized alphabet symbols start at 1.
  return static_cast<uint8_t>(index + 1);
}

bool CharTrigramModel::Score(std::string_view word, float* score) const {
  if (!score || !loaded_) return false;

  const std::vector<char32_t> codepoints = DecodeUtf8(word);
  if (codepoints.empty()) {
    *score = 0.0f;
    return word.empty();
  }

  std::vector<uint8_t> symbols;
  symbols.reserve(codepoints.size() + 2);
  symbols.push_back(0);
  for (char32_t codepoint : codepoints) {
    const uint8_t symbol = SymbolId(codepoint);
    if (symbol == kUnknownSymbol) return false;
    symbols.push_back(symbol);
  }
  symbols.push_back(0);

  double log_probability = 0.0;
  for (size_t i = 0; i < codepoints.size(); ++i) {
    const size_t first = symbols[i];
    const size_t second = symbols[i + 1];
    const size_t third = symbols[i + 2];
    const size_t context = first * kAlphabetSize + second;
    const size_t trigram = context * kAlphabetSize + third;
    const uint64_t denominator =
        i == 0 ? unigrams_[first] : bigrams_[context];
    log_probability +=
        std::log2(static_cast<double>(trigrams_[trigram]) + 1.0) -
        std::log2(static_cast<double>(denominator) + 1.0);
  }

  const double negative_log_probability = -log_probability;
  if (!std::isfinite(negative_log_probability) ||
      negative_log_probability > std::numeric_limits<float>::max()) {
    return false;
  }
  *score = static_cast<float>(negative_log_probability);
  return true;
}

std::string CharTrigramModel::diagnostics() const {
  return loaded_ ? "3grm:k=42" : "not_loaded";
}

}  // namespace hoskey::engine
