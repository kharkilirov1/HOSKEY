#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "asset_manifest.h"
#include "status.h"

namespace hoskey::engine {

struct LoadReport {
  std::string asset_key;
  std::string format;
  size_t byte_size = 0;
  std::vector<std::string> notes;
};

struct LoadedBinaryAsset {
  AssetDescriptor descriptor;
  std::vector<uint8_t> bytes;
  std::vector<std::string> notes;
};

struct LoadedFeatureConfig {
  AssetDescriptor descriptor;
  std::string raw_json;
};

struct LoadedWordListAsset {
  AssetDescriptor descriptor;
  std::vector<std::string> words;
  std::vector<std::string> normalized_words;
  std::vector<int32_t> weights;
  std::vector<std::string> notes;
};

class DictionaryStore {
 public:
  void Reset();
  void PutBinary(LoadedBinaryAsset asset);
  void PutFeatureConfig(LoadedFeatureConfig config);
  void PutWordList(LoadedWordListAsset asset);

  [[nodiscard]] const LoadedBinaryAsset* FindBinary(std::string_view key) const;
  [[nodiscard]] const LoadedFeatureConfig* FindFeatureConfig(std::string_view key) const;
  [[nodiscard]] const LoadedWordListAsset* FindWordList(std::string_view key) const;
  [[nodiscard]] std::vector<std::string> LoadedKeys() const;

 private:
  std::map<std::string, LoadedBinaryAsset, std::less<>> binary_assets_;
  std::map<std::string, LoadedFeatureConfig, std::less<>> feature_configs_;
  std::map<std::string, LoadedWordListAsset, std::less<>> word_list_assets_;
};

// Parse UTF-8 word list (one word per line) into words/normalized_words vectors.
void ParseUtf8WordList(const uint8_t* data, size_t size,
                       std::vector<std::string>* words,
                       std::vector<std::string>* normalized_words);

// Parse weighted word list (word\tweight per line) into words + weights.
void ParseWeightedWordList(const uint8_t* data, size_t size,
                           std::vector<std::string>* words,
                           std::vector<std::string>* normalized_words,
                           std::vector<int32_t>* weights);

}  // namespace hoskey::engine
