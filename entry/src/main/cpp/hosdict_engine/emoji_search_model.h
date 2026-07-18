#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "status.h"

namespace hoskey::engine {

struct EmojiSearchResult {
  std::vector<std::string> emojis;
  size_t trie_entries_examined = 0;
  size_t groups_scored = 0;
};

// Recovered Yandex emoji-search model.
//
// The raw asset is a FlatBuffer containing:
//   field 0: vector<vector<UTF-8 emoji>> candidate groups;
//   field 1: NCompactTrie<wchar16, int32> payload.
//
// Trie keys are UTF-16BE byte sequences. Signed integral leaves use the
// Yandex prefix-varint packer and index field-0 groups. Native prefix search
// visits at most 200 leaves and deduplicates groups/candidates in encounter
// order; this class implements that model-backed route without a static alias
// table or filename-based fallback.
class EmojiSearchModel {
 public:
  [[nodiscard]] Status Load(const uint8_t* data, size_t size);
  void Reset();

  [[nodiscard]] EmojiSearchResult Search(std::string_view query,
                                         size_t max_results = 64,
                                         size_t max_entries = 200) const;

  [[nodiscard]] bool is_loaded() const { return loaded_; }
  [[nodiscard]] size_t key_count() const { return entries_.size(); }
  [[nodiscard]] size_t group_count() const { return groups_.size(); }
  [[nodiscard]] uint64_t invocation_count() const {
    return invocation_count_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] uint64_t scored_count() const {
    return scored_count_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::string diagnostics() const;

 private:
  struct Entry {
    std::string utf16be_key;
    int32_t group_index = -1;
  };

  [[nodiscard]] bool WalkTrie(size_t position, const std::string& prefix,
                              size_t* visited, std::string* error);
  [[nodiscard]] bool ReadTrieOffset(size_t position, size_t length,
                                    size_t* value) const;
  [[nodiscard]] bool ReadTrieLeaf(size_t position, int32_t* value,
                                  size_t* length) const;
  [[nodiscard]] static std::string Utf16BeKey(std::string_view query);

  std::vector<uint8_t> trie_;
  std::vector<std::vector<std::string>> groups_;
  std::vector<Entry> entries_;
  bool loaded_ = false;
  mutable std::atomic<uint64_t> invocation_count_{0};
  mutable std::atomic<uint64_t> scored_count_{0};
};

}  // namespace hoskey::engine
