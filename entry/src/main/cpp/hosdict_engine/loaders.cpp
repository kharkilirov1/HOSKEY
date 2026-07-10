#include "loaders.h"

#include <algorithm>
#include <unordered_set>

namespace hoskey::engine {

namespace {

std::string TrimWhitespace(std::string_view text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && static_cast<unsigned char>(text[begin]) <= 0x20) ++begin;
  while (end > begin && static_cast<unsigned char>(text[end - 1]) <= 0x20) --end;
  return std::string(text.substr(begin, end - begin));
}

bool IsCyrillicChar(char32_t ch) {
  return (ch >= U'\u0430' && ch <= U'\u044F') ||
         (ch >= U'\u0410' && ch <= U'\u042F') ||
         ch == U'\u0451' || ch == U'\u0401';
}

bool IsAcceptedWord(const std::string& word) {
  if (word.size() < 2 || word.size() > 64) return false;

  std::vector<char32_t> decoded;
  size_t pos = 0;
  while (pos < word.size()) {
    unsigned char c = word[pos];
    if ((c & 0x80) == 0) {
      decoded.push_back(c);
      pos++;
    } else if ((c & 0xE0) == 0xC0 && pos + 1 < word.size()) {
      decoded.push_back(((c & 0x1F) << 6) | (word[pos + 1] & 0x3F));
      pos += 2;
    } else if ((c & 0xF0) == 0xE0 && pos + 2 < word.size()) {
      decoded.push_back(((c & 0x0F) << 12) | ((word[pos + 1] & 0x3F) << 6) | (word[pos + 2] & 0x3F));
      pos += 3;
    } else if ((c & 0xF8) == 0xF0 && pos + 3 < word.size()) {
      decoded.push_back(((c & 0x07) << 18) | ((word[pos + 1] & 0x3F) << 12) | ((word[pos + 2] & 0x3F) << 6) | (word[pos + 3] & 0x3F));
      pos += 4;
    } else {
      return false; // Invalid UTF-8
    }
  }

  if (decoded.size() < 2 || decoded.size() > 32) return false;
  for (char32_t ch : decoded) {
    if (!IsCyrillicChar(ch) && ch != U'-') return false;
  }

  return true;
}

}  // namespace

// ============================================================================
// DictionaryStore
// ============================================================================

void DictionaryStore::Reset() {
  binary_assets_.clear();
  feature_configs_.clear();
  word_list_assets_.clear();
}

void DictionaryStore::PutBinary(LoadedBinaryAsset asset) {
  binary_assets_[asset.descriptor.key] = std::move(asset);
}

void DictionaryStore::PutFeatureConfig(LoadedFeatureConfig config) {
  feature_configs_[config.descriptor.key] = std::move(config);
}

void DictionaryStore::PutWordList(LoadedWordListAsset asset) {
  word_list_assets_[asset.descriptor.key] = std::move(asset);
}

const LoadedBinaryAsset* DictionaryStore::FindBinary(std::string_view key) const {
  auto it = binary_assets_.find(key);
  return it == binary_assets_.end() ? nullptr : &it->second;
}

const LoadedFeatureConfig* DictionaryStore::FindFeatureConfig(std::string_view key) const {
  auto it = feature_configs_.find(key);
  return it == feature_configs_.end() ? nullptr : &it->second;
}

const LoadedWordListAsset* DictionaryStore::FindWordList(std::string_view key) const {
  auto it = word_list_assets_.find(key);
  return it == word_list_assets_.end() ? nullptr : &it->second;
}

std::vector<std::string> DictionaryStore::LoadedKeys() const {
  std::vector<std::string> result;
  result.reserve(binary_assets_.size() + feature_configs_.size() + word_list_assets_.size());
  for (const auto& [key, _] : binary_assets_) result.push_back(key);
  for (const auto& [key, _] : feature_configs_) result.push_back(key);
  for (const auto& [key, _] : word_list_assets_) result.push_back(key);
  return result;
}

// ============================================================================
// UTF-8 word list parser
// ============================================================================

void ParseUtf8WordList(const uint8_t* data, size_t size,
                       std::vector<std::string>* words,
                       std::vector<std::string>* normalized_words) {
  if (!words || !normalized_words || !data || size == 0) return;

  std::string_view text(reinterpret_cast<const char*>(data), size);
  std::unordered_set<std::string> seen;
  size_t cursor = 0;

  while (cursor <= text.size()) {
    const size_t nl = text.find('\n', cursor);
    const size_t end = (nl == std::string::npos) ? text.size() : nl;
    std::string word = TrimWhitespace(text.substr(cursor, end - cursor));
    cursor = (nl == std::string::npos) ? text.size() + 1 : nl + 1;

    if (word.empty() || !IsAcceptedWord(word)) continue;
    if (seen.insert(word).second) {
      words->push_back(word);
      normalized_words->push_back(word);
    }
  }
}

void ParseWeightedWordList(const uint8_t* data, size_t size,
                           std::vector<std::string>* words,
                           std::vector<std::string>* normalized_words,
                           std::vector<int32_t>* weights) {
  if (!words || !normalized_words || !weights || !data || size == 0) return;

  std::string_view text(reinterpret_cast<const char*>(data), size);
  std::unordered_set<std::string> seen;
  size_t cursor = 0;

  while (cursor <= text.size()) {
    const size_t nl = text.find('\n', cursor);
    const size_t end = (nl == std::string::npos) ? text.size() : nl;
    std::string_view line = text.substr(cursor, end - cursor);
    cursor = (nl == std::string::npos) ? text.size() + 1 : nl + 1;

    // Parse "word\tweight"
    const size_t tab = line.find('\t');
    std::string word;
    int32_t weight = 0;

    if (tab != std::string_view::npos) {
      word = TrimWhitespace(line.substr(0, tab));
      auto weight_str = line.substr(tab + 1);
      // Simple atoi. Accumulate in int64 and clamp so a long or crafted digit
      // run cannot overflow int32 (signed overflow is undefined behavior).
      int64_t w = 0;
      for (char c : weight_str) {
        if (c < '0' || c > '9') break;
        w = w * 10 + (c - '0');
        if (w > 2147483647LL) { w = 2147483647LL; break; }
      }
      weight = static_cast<int32_t>(w);
    } else {
      word = TrimWhitespace(line);
    }

    if (word.empty() || !IsAcceptedWord(word)) continue;
    if (seen.insert(word).second) {
      words->push_back(word);
      normalized_words->push_back(word);
      weights->push_back(weight);
    }
  }
}

}  // namespace hoskey::engine
