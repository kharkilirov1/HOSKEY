#include "policy_store.h"

#include <algorithm>
#include <cstring>

namespace hoskey::engine {

namespace {

// --- Comptrie node layout ---
//
// The policy files use a byte-level binary trie encoding where each node
// represents one byte of a UTF-8 key string. The observed encoding:
//
// Each node group is a binary search tree of sibling labels at one trie level.
// Node encoding (observed pattern):
//   [label: 1 byte] [core: 1+ bytes]
//
// Core byte bit layout (Yandex comptrie family):
//   bits 0-1: offset size indicator (0=1byte, 1=2byte, 2=3byte, 3=4byte)
//   bit 2:    is_final (this node is an accepting state)
//   bit 3:    has_forward (has child subtree for next byte)
//   bit 4:    has_right (has right sibling with larger label)
//   bit 5:    has_left (has left sibling with smaller label)
//   bits 6-7: reserved / value encoding
//
// Because the exact bit layout may vary between Yandex versions, we also
// implement a robust fallback that searches for UTF-8 byte sequences in the
// raw trie data with boundary heuristics.

// Markers found in trailing metadata sections.
constexpr uint8_t kSchmMagic[] = {'S', 'C', 'H', 'M'};
constexpr uint8_t kY3ieMagic[] = {'y', '3', 'i', 'e'};

// Maximum depth for trie extraction (prevents runaway on malformed data).
constexpr size_t kMaxTrieDepth = 128;

// Maximum entries to extract (safety limit).
constexpr size_t kMaxExtractedEntries = 500000;

// Minimum valid UTF-8 Cyrillic word length in bytes (1 Cyrillic char = 2 bytes).
constexpr size_t kMinWordBytes = 2;
constexpr size_t kMaxWordBytes = 128;

// Check if a byte could be a comptrie structural byte (not a UTF-8 data byte).
// Structural bytes tend to be small values encoding offsets/flags.
bool IsLikelyStructuralByte(uint8_t b) {
  return b < 0x20;
}

// Check if a byte sequence looks like a valid UTF-8 word boundary.
// At a word boundary in the trie, we expect either:
//   - A structural byte (small value) before the word's first byte
//   - Start of buffer
bool IsWordBoundary(const uint8_t* data, size_t offset) {
  if (offset == 0) return true;
  uint8_t prev = data[offset - 1];
  // Structural bytes, null, or the 0x40 flag byte (observed as common separator)
  return prev < 0x20 || prev == 0x00;
}

// Find a 4-byte magic marker in the data (searching from the end).
ptrdiff_t FindMarkerFromEnd(const uint8_t* data, size_t size,
                            const uint8_t* marker, size_t marker_len) {
  if (size < marker_len) return -1;
  for (size_t i = size - marker_len; i > 0; --i) {
    if (std::memcmp(data + i, marker, marker_len) == 0) {
      return static_cast<ptrdiff_t>(i);
    }
  }
  if (std::memcmp(data, marker, marker_len) == 0) {
    return 0;
  }
  return -1;
}

}  // namespace

size_t PolicyStore::DetectTrailingMetadata(const uint8_t* data, size_t size) {
  // Look for SCHM or y3ie markers in the last portion of the file.
  // These indicate trailing schema/metadata sections that are NOT part of the trie.
  const size_t search_start = size > 512 ? size - 512 : 0;
  const size_t search_len = size - search_start;

  ptrdiff_t schm_pos = FindMarkerFromEnd(data + search_start, search_len,
                                          kSchmMagic, sizeof(kSchmMagic));
  ptrdiff_t y3ie_pos = FindMarkerFromEnd(data + search_start, search_len,
                                          kY3ieMagic, sizeof(kY3ieMagic));

  size_t metadata_start = size;

  if (schm_pos >= 0) {
    metadata_start = std::min(metadata_start, search_start + static_cast<size_t>(schm_pos));
  }
  if (y3ie_pos >= 0) {
    metadata_start = std::min(metadata_start, search_start + static_cast<size_t>(y3ie_pos));
  }

  // Also check for null-padding region before metadata markers.
  // Walk backward from metadata_start to skip null padding and whitespace.
  while (metadata_start > 0 &&
         (data[metadata_start - 1] == 0x00 || data[metadata_start - 1] == 0x20)) {
    --metadata_start;
  }

  return metadata_start;
}

size_t PolicyStore::TryParseComptrie(const uint8_t* data, size_t trie_size) {
  // Attempt comptrie traversal to extract all accepted words.
  // The trie is a binary search tree at each level, with byte labels as keys.
  //
  // Strategy: recursive DFS from root (offset 0), collecting words at
  // final/accepting nodes.
  //
  // Because the exact comptrie variant is not fully reverse-engineered,
  // we use a heuristic approach: try multiple known bit layouts for the
  // core byte flags, and pick the interpretation that yields valid results.

  if (trie_size < 2) return 0;

  entries_.clear();
  std::string prefix;
  prefix.reserve(64);

  ComptrieExtract(data, trie_size, 0, prefix, kMaxTrieDepth);

  return entries_.size();
}

void PolicyStore::ComptrieExtract(const uint8_t* trie, size_t trie_size,
                                  size_t offset, std::string& prefix,
                                  size_t depth_limit) {
  if (depth_limit == 0) return;

  // Right-siblings are walked ITERATIVELY (a long sibling chain would otherwise
  // recurse once per node and overflow the stack on a large/corrupt blob). Only
  // forward descent recurses, and that is bounded by depth_limit. Extraction
  // results are identical to the previous recursive form: each sibling shares
  // the incoming depth_limit and its forward child gets depth_limit - 1.
  while (offset < trie_size - 1) {
    if (entries_.size() >= kMaxExtractedEntries) return;

    // Read label and core byte.
    uint8_t label = trie[offset];
    uint8_t core = trie[offset + 1];

    // Interpret core byte flags (Yandex comptrie variant).
    //   bits 0-1: extra offset bytes (0-3)
    //   bit 2:    is_final
    //   bit 3:    has_forward (child for next string byte)
    //   bit 4:    has_right (right sibling, larger label)
    uint32_t extra_bytes = core & 0x03;
    bool is_final = (core & 0x04) != 0;
    bool has_forward = (core & 0x08) != 0;
    bool has_right = (core & 0x10) != 0;

    size_t node_size = 2 + extra_bytes;  // label + core + extra
    if (offset + node_size > trie_size) return;

    uint32_t forward_offset = 0;
    for (uint32_t i = 0; i < extra_bytes; ++i) {
      forward_offset |= static_cast<uint32_t>(trie[offset + 2 + i]) << (8 * i);
    }

    // Append label to current prefix.
    prefix.push_back(static_cast<char>(label));

    // If this is a final node, the current prefix is an accepted word.
    if (is_final) {
      if (prefix.size() >= kMinWordBytes && prefix.size() <= kMaxWordBytes) {
        entries_.insert(prefix);
      }
    }

    // Recurse into forward child (next byte of string).
    if (has_forward && offset + node_size < trie_size) {
      size_t child_offset = offset + node_size;
      if (forward_offset > 0 && forward_offset < trie_size) {
        child_offset = forward_offset;
      }
      if (child_offset < trie_size) {
        ComptrieExtract(trie, trie_size, child_offset, prefix, depth_limit - 1);
      }
    }

    // Remove label from prefix (backtrack).
    prefix.pop_back();

    // Advance to the right sibling (same trie level, larger label). node_size
    // is >= 2, so offset strictly increases and the loop always terminates.
    if (!has_right) return;
    offset += node_size;
  }
}

bool PolicyStore::ComptrieContains(const uint8_t* trie, size_t trie_size,
                                   const uint8_t* key, size_t key_len) const {
  if (trie_size < 2 || key_len == 0) return false;

  size_t offset = 0;
  size_t key_pos = 0;

  while (offset < trie_size - 1 && key_pos <= key_len) {
    uint8_t label = trie[offset];
    uint8_t core = trie[offset + 1];

    uint32_t extra_bytes = core & 0x03;
    bool is_final = (core & 0x04) != 0;
    bool has_forward = (core & 0x08) != 0;
    bool has_right = (core & 0x10) != 0;
    bool has_left = (core & 0x20) != 0;

    size_t node_size = 2 + extra_bytes;
    if (offset + node_size > trie_size) return false;

    uint32_t forward_offset = 0;
    for (uint32_t i = 0; i < extra_bytes; ++i) {
      forward_offset |= static_cast<uint32_t>(trie[offset + 2 + i]) << (8 * i);
    }

    if (key_pos >= key_len) {
      // We've consumed all key bytes. Check if current node is final.
      return is_final && label == key[key_pos - 1];
    }

    uint8_t target = key[key_pos];

    if (target == label) {
      // Match: advance to next key byte.
      ++key_pos;

      if (key_pos == key_len) {
        // All key bytes consumed. This node must be final.
        return is_final;
      }

      // Follow forward pointer to child.
      if (!has_forward) return false;

      if (forward_offset > 0 && forward_offset < trie_size) {
        offset = forward_offset;
      } else {
        offset += node_size;
      }
    } else if (target > label && has_right) {
      // Go to right sibling.
      offset += node_size;
    } else if (target < label && has_left) {
      // Left sibling: in some comptrie layouts, left is encoded before current.
      // Without knowing the exact layout, we can't navigate left reliably.
      return false;
    } else {
      // No matching sibling.
      return false;
    }
  }

  return false;
}

bool PolicyStore::RawScanContains(const std::string& word) const {
  if (trie_data_.empty() || word.empty()) return false;

  const auto* haystack = trie_data_.data();
  const size_t haystack_len = trie_body_size_;
  const auto* needle = reinterpret_cast<const uint8_t*>(word.data());
  const size_t needle_len = word.size();

  if (needle_len > haystack_len) return false;

  // Search for the UTF-8 byte sequence in the trie data.
  // Each byte of the word should appear as edge labels in sequence.
  // We look for the complete byte sequence with structural-byte boundaries.
  const uint8_t* pos = haystack;
  const uint8_t* end = haystack + haystack_len - needle_len;

  while (pos <= end) {
    const uint8_t* found = std::search(pos, haystack + haystack_len,
                                       needle, needle + needle_len);
    if (found >= haystack + haystack_len) break;

    // Heuristic validation: check that we're at a plausible word boundary.
    // In the trie encoding, word bytes are interspersed with structural bytes
    // (small values < 0x20, or the 0x40 flag byte). A raw match is useful
    // but may produce false positives on large blobs.
    //
    // Accept the match if:
    // 1. It's preceded by a structural byte or buffer start.
    // 2. It's followed by a structural byte or buffer end.
    size_t match_offset = static_cast<size_t>(found - haystack);
    bool boundary_before = (match_offset == 0) ||
                           (haystack[match_offset - 1] < 0x20) ||
                           (haystack[match_offset - 1] == 0x40);
    size_t after = match_offset + needle_len;
    bool boundary_after = (after >= haystack_len) ||
                          (haystack[after] < 0x20) ||
                          (haystack[after] == 0x40);

    if (boundary_before && boundary_after) {
      return true;
    }

    pos = found + 1;
  }

  // Lenient fallback: for small blobs, accept any substring match.
  if (haystack_len < 8192) {
    const uint8_t* found = std::search(haystack, haystack + haystack_len,
                                       needle, needle + needle_len);
    return found < haystack + haystack_len;
  }

  return false;
}

Status PolicyStore::Load(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    loaded_ = true;
    notes_.push_back("empty policy blob, no entries");
    return Status::Ok();
  }

  entries_.clear();
  trie_data_.clear();
  notes_.clear();
  trie_parsed_ = false;

  // Step 1: Detect and strip trailing metadata (SCHM, y3ie sections).
  trie_body_size_ = DetectTrailingMetadata(data, size);
  if (trie_body_size_ < size) {
    notes_.push_back("trailing metadata detected at offset " +
                     std::to_string(trie_body_size_) +
                     " (" + std::to_string(size - trie_body_size_) + " bytes)");
  }

  // Store the trie body.
  trie_data_.assign(data, data + size);

  // Step 2: Attempt comptrie extraction.
  size_t extracted = TryParseComptrie(data, trie_body_size_);
  if (extracted > 0) {
    trie_parsed_ = true;
    notes_.push_back("comptrie extraction: " + std::to_string(extracted) + " entries");
  } else {
    notes_.push_back("comptrie extraction yielded 0 entries, using raw scan fallback");

    // Step 3: Attempt alternate interpretations.
    // Try with inverted flag bits (some comptrie versions use different layouts).
    // For now, we rely on the raw scan fallback for Contains().
  }

  // Report trie statistics.
  notes_.push_back("trie body: " + std::to_string(trie_body_size_) + " bytes");
  notes_.push_back("total blob: " + std::to_string(size) + " bytes");

  // Count approximate number of entries from trie body byte patterns.
  // In the observed encoding, final/accepting nodes can be estimated by
  // counting structural patterns. This is purely diagnostic.
  size_t final_count_estimate = 0;
  for (size_t i = 1; i < trie_body_size_; ++i) {
    uint8_t core = data[i];
    // Count bytes that have bit 2 set (is_final in our layout hypothesis).
    if ((core & 0x04) != 0 && data[i - 1] >= 0x20) {
      ++final_count_estimate;
    }
  }
  if (final_count_estimate > 0) {
    notes_.push_back("estimated final nodes (heuristic): ~" +
                     std::to_string(final_count_estimate));
  }

  loaded_ = true;
  return Status::Ok();
}

bool PolicyStore::Contains(const std::string& word) const {
  if (!loaded_) return false;
  if (word.empty()) return false;

  // Check extracted entries first (fast hash lookup).
  if (!entries_.empty() && entries_.count(word) > 0) {
    return true;
  }

  // Attempt direct comptrie traversal.
  if (trie_body_size_ >= 2) {
    bool found = ComptrieContains(trie_data_.data(), trie_body_size_,
                                  reinterpret_cast<const uint8_t*>(word.data()),
                                  word.size());
    if (found) return true;
  }

  // Raw scan fallback.
  return RawScanContains(word);
}

size_t PolicyStore::entry_count() const {
  return entries_.size();
}

bool PolicyStore::loaded() const {
  return loaded_;
}

const std::unordered_set<std::string>& PolicyStore::entries() const {
  return entries_;
}

const std::vector<std::string>& PolicyStore::load_notes() const {
  return notes_;
}

}  // namespace hoskey::engine
