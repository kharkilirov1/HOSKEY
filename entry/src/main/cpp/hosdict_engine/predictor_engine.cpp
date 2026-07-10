#include "predictor_engine.h"
#include "prototype_transition_data.h"
#include "prototype_verified_transition_data.h"

#include <algorithm>
#include <cstring>
#include <queue>
#include <unordered_map>

namespace hoskey::engine {

namespace {

uint32_t ReadU32LE(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8U) |
         (static_cast<uint32_t>(p[2]) << 16U) |
         (static_cast<uint32_t>(p[3]) << 24U);
}

int32_t ReadI32LE(const uint8_t* p) {
  return static_cast<int32_t>(ReadU32LE(p));
}

}  // namespace

// ============================================================================
// UTF-8 / Cyrillic helpers
// ============================================================================

std::vector<char32_t> PredictorEngine::ToCodepoints(std::string_view utf8) {
  std::vector<char32_t> result;
  const auto* d = reinterpret_cast<const uint8_t*>(utf8.data());
  size_t pos = 0, len = utf8.size();
  while (pos < len) {
    uint8_t b = d[pos];
    char32_t ch = 0;
    if (b <= 0x7F) {
      ch = b;
      pos += 1;
    } else if ((b & 0xE0) == 0xC0 && pos + 1 < len) {
      ch = ((b & 0x1F) << 6) | (d[pos + 1] & 0x3F);
      pos += 2;
    } else if ((b & 0xF0) == 0xE0 && pos + 2 < len) {
      ch = ((b & 0x0F) << 12) | ((d[pos + 1] & 0x3F) << 6) | (d[pos + 2] & 0x3F);
      pos += 3;
    } else if ((b & 0xF8) == 0xF0 && pos + 3 < len) {
      ch = ((b & 0x07) << 18) | ((d[pos + 1] & 0x3F) << 12) |
           ((d[pos + 2] & 0x3F) << 6) | (d[pos + 3] & 0x3F);
      pos += 4;
    } else {
      pos += 1;
      continue;
    }
    result.push_back(ch);
  }
  return result;
}

std::string PredictorEngine::ToUtf8(const std::vector<char32_t>& codepoints) {
  std::string result;
  result.reserve(codepoints.size() * 2);
  for (char32_t ch : codepoints) {
    if (ch <= 0x7F) {
      result.push_back(static_cast<char>(ch));
    } else if (ch <= 0x7FF) {
      result.push_back(static_cast<char>(0xC0 | (ch >> 6)));
      result.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
    } else if (ch <= 0xFFFF) {
      result.push_back(static_cast<char>(0xE0 | (ch >> 12)));
      result.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
      result.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
    } else {
      result.push_back(static_cast<char>(0xF0 | (ch >> 18)));
      result.push_back(static_cast<char>(0x80 | ((ch >> 12) & 0x3F)));
      result.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
      result.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
    }
  }
  return result;
}

char32_t PredictorEngine::CyrillicLower(char32_t ch) {
  if (ch >= 0x0410 && ch <= 0x042F) return ch + 0x20;
  if (ch == 0x0401) return 0x0451;
  if (ch >= U'A' && ch <= U'Z') return ch + 32;
  return ch;
}

std::vector<char32_t> PredictorEngine::Normalize(std::string_view utf8) {
  auto cps = ToCodepoints(utf8);
  for (auto& ch : cps) {
    ch = CyrillicLower(ch);
  }
  return cps;
}

// ============================================================================
// TrieNode
// ============================================================================

uint32_t TrieNode::find_child(char32_t label) const {
  // Binary search on sorted children.
  int lo = 0, hi = static_cast<int>(children.size()) - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (children[mid].label == label) return children[mid].child;
    if (children[mid].label < label)
      lo = mid + 1;
    else
      hi = mid - 1;
  }
  return kNoChild;
}

// ============================================================================
// Build
// ============================================================================

void PredictorEngine::Insert(const std::vector<char32_t>& codepoints,
                              int32_t weight) {
  uint32_t current = 0;  // root
  for (char32_t ch : codepoints) {
    uint32_t child = nodes_[current].find_child(ch);
    if (child == TrieNode::kNoChild) {
      child = static_cast<uint32_t>(nodes_.size());
      nodes_.emplace_back();
      // Insert edge in sorted order.
      auto& edges = nodes_[current].children;
      auto it = std::lower_bound(edges.begin(), edges.end(), ch,
                                  [](const TrieNode::Edge& e, char32_t c) {
                                    return e.label < c;
                                  });
      edges.insert(it, TrieNode::Edge{ch, child});
    }
    current = child;
  }
  nodes_[current].weight = weight;
}

void PredictorEngine::Build(const std::vector<std::string>& words,
                             const std::vector<int32_t>& weights) {
  nodes_.clear();
  nodes_.emplace_back();  // root node
  word_count_ = 0;

  // Reserve approximate space.
  nodes_.reserve(words.size() * 4);

  for (size_t i = 0; i < words.size(); ++i) {
    auto cps = Normalize(words[i]);
    if (cps.size() < 2) continue;

    int32_t w = (i < weights.size()) ? weights[i] : 0;
    Insert(cps, w);
    ++word_count_;
  }

  // Shrink to fit.
  nodes_.shrink_to_fit();
  for (auto& node : nodes_) {
    node.children.shrink_to_fit();
  }
  ComputeMaxSubtreeWeight(0);
}

Status PredictorEngine::LoadBinary(const uint8_t* data, size_t size) {
  static constexpr uint32_t kMagic = 0x31545048;  // "HPT1" little-endian.
  static constexpr uint32_t kVersion = 1;
  static constexpr size_t kHeaderSize = 24;
  static constexpr size_t kNodeRecordSize = 16;
  static constexpr size_t kEdgeRecordSize = 8;

  if (!data || size < kHeaderSize) {
    return Status::InvalidArgument("PredictorEngine: null or too small binary trie");
  }
  if (ReadU32LE(data) != kMagic) {
    return Status::ParseError("PredictorEngine: predictor_trie_v1 magic mismatch");
  }
  if (ReadU32LE(data + 4) != kVersion) {
    return Status::UnsupportedFormat(
        "PredictorEngine: unsupported predictor_trie_v1 version");
  }

  const uint32_t node_count = ReadU32LE(data + 8);
  const uint32_t binary_word_count = ReadU32LE(data + 12);
  const uint32_t edge_count = ReadU32LE(data + 16);
  if (node_count == 0) {
    return Status::ParseError("PredictorEngine: binary trie has no root node");
  }
  if (binary_word_count > node_count) {
    return Status::ParseError("PredictorEngine: binary word count exceeds node count");
  }

  const uint64_t node_table_size =
      static_cast<uint64_t>(node_count) * kNodeRecordSize;
  const uint64_t edge_table_offset = kHeaderSize + node_table_size;
  const uint64_t edge_table_size =
      static_cast<uint64_t>(edge_count) * kEdgeRecordSize;
  if (edge_table_offset > size || edge_table_size > size - edge_table_offset) {
    return Status::ParseError("PredictorEngine: binary trie tables out of range");
  }

  std::vector<TrieNode> loaded_nodes;
  loaded_nodes.resize(node_count);
  for (uint32_t i = 0; i < node_count; ++i) {
    const uint8_t* node_ptr = data + kHeaderSize + i * kNodeRecordSize;
    const uint32_t first_edge = ReadU32LE(node_ptr);
    const uint32_t child_count = ReadU32LE(node_ptr + 4);
    if (first_edge > edge_count || child_count > edge_count - first_edge) {
      return Status::ParseError("PredictorEngine: node edge span out of range");
    }

    TrieNode& node = loaded_nodes[i];
    node.weight = ReadI32LE(node_ptr + 8);
    node.max_subtree_weight = ReadI32LE(node_ptr + 12);
    node.children.reserve(child_count);

    uint32_t previous_label = 0;
    for (uint32_t j = 0; j < child_count; ++j) {
      const uint64_t edge_index = static_cast<uint64_t>(first_edge) + j;
      const uint8_t* edge_ptr = data + edge_table_offset +
                                edge_index * kEdgeRecordSize;
      const uint32_t label = ReadU32LE(edge_ptr);
      const uint32_t child = ReadU32LE(edge_ptr + 4);
      if (child >= node_count) {
        return Status::ParseError("PredictorEngine: child index out of range");
      }
      if (label > 0x10FFFFU) {
        return Status::ParseError("PredictorEngine: invalid unicode label");
      }
      if (j != 0 && label <= previous_label) {
        return Status::ParseError("PredictorEngine: child labels are not sorted/unique");
      }
      previous_label = label;
      node.children.push_back(
          TrieNode::Edge{static_cast<char32_t>(label), child});
    }
  }

  nodes_ = std::move(loaded_nodes);
  word_count_ = binary_word_count;
  return Status::Ok();
}

int32_t PredictorEngine::ComputeMaxSubtreeWeight(uint32_t node_idx) {
  if (node_idx >= nodes_.size()) return -1;

  auto& node = nodes_[node_idx];
  int32_t best = node.weight;
  for (const auto& edge : node.children) {
    best = std::max(best, ComputeMaxSubtreeWeight(edge.child));
  }
  node.max_subtree_weight = best;
  return best;
}

// ============================================================================
// Walk / Contains / GetWeight
// ============================================================================

uint32_t PredictorEngine::WalkPath(const std::vector<char32_t>& path) const {
  uint32_t current = 0;
  for (char32_t ch : path) {
    uint32_t child = nodes_[current].find_child(ch);
    if (child == TrieNode::kNoChild) return TrieNode::kNoChild;
    current = child;
  }
  return current;
}

ActiveNodeState PredictorEngine::MakeActiveNodeState(
    uint32_t node_idx, ActiveNodeState::Kind kind) const {
  ActiveNodeState state;
  if (node_idx == TrieNode::kNoChild || node_idx >= nodes_.size()) return state;

  const auto& node = nodes_[node_idx];
  state.node = node_idx;
  state.kind = kind;
  state.terminal_here = node.is_terminal();
  state.has_children = !node.children.empty();
  state.weight = node.weight;
  return state;
}

ActivePrefixState PredictorEngine::BuildActivePrefixState(
    std::string_view prefix) const {
  ActivePrefixState active;
  if (nodes_.empty()) return active;

  auto cps = Normalize(prefix);
  active.visible_prefix = cps;
  active.states.push_back(MakeActiveNodeState(0));
  for (char32_t ch : cps) {
    std::vector<ActiveNodeState> next_states;
    next_states.reserve(active.states.size());
    for (const auto& state : active.states) {
      if (state.node == TrieNode::kNoChild || state.node >= nodes_.size()) continue;
      uint32_t child = nodes_[state.node].find_child(ch);
      if (child == TrieNode::kNoChild) continue;
      next_states.push_back(MakeActiveNodeState(child, state.kind));
    }

    if (next_states.empty()) {
      active.states.clear();
      return active;
    }

    std::sort(next_states.begin(), next_states.end(),
              [](const ActiveNodeState& a, const ActiveNodeState& b) {
                if (a.node != b.node) return a.node < b.node;
                return static_cast<int>(a.kind) < static_cast<int>(b.kind);
              });
    next_states.erase(
        std::unique(next_states.begin(), next_states.end(),
                    [](const ActiveNodeState& a, const ActiveNodeState& b) {
                      return a.node == b.node && a.kind == b.kind;
                    }),
        next_states.end());
    active.states = std::move(next_states);
  }

  MaterializeTerminalHandoffStates(cps, active);
  return active;
}

void PredictorEngine::MaterializeTerminalHandoffStates(
    const std::vector<char32_t>& prefix, ActivePrefixState& active) const {
  // Prototype only:
  // reverse notes show the first meaningful terminal boundary appears once a
  // visible prefix can already be a word while still having continuations.
  // Until the succinct-trie epsilon transition is ported, we expose that shape
  // by duplicating such states into a separate typed branch.
  if (prefix.size() < 2 || active.empty()) return;
  const auto filters = GetPrototypeContinuationFilters(prefix);
  if (!filters.has_handoff()) return;

  std::vector<ActiveNodeState> handoff_states;
  handoff_states.reserve(active.states.size());
  for (const auto& state : active.states) {
    if (state.kind != ActiveNodeState::Kind::kDirect) continue;
    if (!state.terminal_here || !state.has_children) continue;
    handoff_states.push_back(
        MakeActiveNodeState(state.node, ActiveNodeState::Kind::kTerminalHandoff));
  }

  if (handoff_states.empty()) return;
  active.states.insert(active.states.end(), handoff_states.begin(),
                       handoff_states.end());
  std::sort(active.states.begin(), active.states.end(),
            [](const ActiveNodeState& a, const ActiveNodeState& b) {
              if (a.node != b.node) return a.node < b.node;
              return static_cast<int>(a.kind) < static_cast<int>(b.kind);
            });
  active.states.erase(
      std::unique(active.states.begin(), active.states.end(),
                  [](const ActiveNodeState& a, const ActiveNodeState& b) {
                    return a.node == b.node && a.kind == b.kind;
                  }),
      active.states.end());
}

PredictorEngine::PrototypeContinuationFilters
PredictorEngine::GetPrototypeContinuationFilters(
    const std::vector<char32_t>& prefix) const {
  PrototypeContinuationFilters filters;
  const PrototypeTransitionRecord* records = kPrototypeTransitionRecords;
  std::size_t record_count = kPrototypeTransitionRecordCount;
  if (prototype_transition_data_mode_ ==
      PrototypeTransitionDataMode::kVerifiedSplitOnly) {
    records = kPrototypeVerifiedTransitionRecords;
    record_count = kPrototypeVerifiedTransitionRecordCount;
  }

  for (std::size_t i = 0; i < record_count; ++i) {
    const auto& record = records[i];
    if (ToCodepoints(record.prefix_utf8) != prefix) continue;
    filters.filter_direct = record.filter_direct;
    filters.filter_handoff = record.filter_handoff;
    if (record.filter_direct && record.direct_utf8[0] != '\0') {
      filters.direct = ToCodepoints(record.direct_utf8);
      std::sort(filters.direct.begin(), filters.direct.end());
    }
    if (record.filter_handoff && record.handoff_utf8[0] != '\0') {
      filters.handoff = ToCodepoints(record.handoff_utf8);
      std::sort(filters.handoff.begin(), filters.handoff.end());
    }
    return filters;
  }
  return filters;
}

std::vector<char32_t> PredictorEngine::GetMergedNextLabels(
    const ActivePrefixState& active) const {
  return GetMergedNextLabelGroups(active).all;
}

NextLabelGroups PredictorEngine::GetMergedNextLabelGroups(
    const ActivePrefixState& active) const {
  NextLabelGroups groups;
  const auto filters = GetPrototypeContinuationFilters(active.visible_prefix);
  for (const auto& state : active.states) {
    if (state.node == TrieNode::kNoChild || state.node >= nodes_.size()) continue;
    const auto& node = nodes_[state.node];
    for (const auto& edge : node.children) {
      if (state.kind == ActiveNodeState::Kind::kTerminalHandoff) {
        if (filters.has_handoff() &&
            !std::binary_search(filters.handoff.begin(), filters.handoff.end(),
                                edge.label)) {
          continue;
        }
        groups.terminal_handoff.push_back(edge.label);
        groups.all.push_back(edge.label);
      } else {
        if (filters.has_direct() &&
            !std::binary_search(filters.direct.begin(), filters.direct.end(),
                                edge.label)) {
          continue;
        }
        groups.direct.push_back(edge.label);
        groups.all.push_back(edge.label);
      }
    }
  }

  auto dedup = [](std::vector<char32_t>& labels) {
    std::sort(labels.begin(), labels.end());
    labels.erase(std::unique(labels.begin(), labels.end()), labels.end());
  };
  dedup(groups.all);
  dedup(groups.direct);
  dedup(groups.terminal_handoff);
  return groups;
}

bool PredictorEngine::Contains(std::string_view word) const {
  if (nodes_.empty()) return false;
  auto active = BuildActivePrefixState(word);
  for (const auto& state : active.states) {
    if (state.terminal_here) return true;
  }
  return false;
}

int32_t PredictorEngine::GetWeight(std::string_view word) const {
  if (nodes_.empty()) return -1;
  auto active = BuildActivePrefixState(word);
  int32_t best = -1;
  for (const auto& state : active.states) {
    if (!state.terminal_here) continue;
    best = std::max(best, state.weight);
  }
  return best;
}

// ============================================================================
// Prefix Search
// ============================================================================

void PredictorEngine::CollectWords(uint32_t node_idx,
                                    std::vector<char32_t>& path,
                                    std::vector<PredictorResult>& results,
                                    size_t max_results) const {
  if (node_idx >= nodes_.size()) return;
  if (max_results == 0) return;

  struct QueueItem {
    uint32_t node = TrieNode::kNoChild;
    int32_t priority = -1;
    std::vector<char32_t> path;
  };

  struct QueueItemLess {
    bool operator()(const QueueItem& a, const QueueItem& b) const {
      if (a.priority != b.priority) return a.priority < b.priority;
      return a.path > b.path;
    }
  };

  const std::size_t base_size = results.size();
  std::priority_queue<QueueItem, std::vector<QueueItem>, QueueItemLess> queue;
  queue.push(QueueItem{node_idx, nodes_[node_idx].max_subtree_weight, path});

  while (!queue.empty() && results.size() - base_size < max_results) {
    QueueItem item = queue.top();
    queue.pop();

    if (item.node >= nodes_.size()) continue;
    const auto& node = nodes_[item.node];
    if (node.max_subtree_weight < 0) continue;

    if (node.is_terminal()) {
      PredictorResult r;
      r.word = ToUtf8(item.path);
      r.score = static_cast<float>(node.weight);
      r.source = PredictorResult::kPrefix;
      results.push_back(std::move(r));
      if (results.size() - base_size >= max_results) break;
    }

    for (const auto& edge : node.children) {
      if (edge.child >= nodes_.size()) continue;
      const auto& child_node = nodes_[edge.child];
      if (child_node.max_subtree_weight < 0) continue;

      QueueItem child;
      child.node = edge.child;
      child.priority = child_node.max_subtree_weight;
      child.path = item.path;
      child.path.push_back(edge.label);
      queue.push(std::move(child));
    }
  }
}

void PredictorEngine::CollectWordsFromState(
    uint32_t node_idx, std::vector<char32_t>& path,
    std::vector<PredictorResult>& results, size_t max_results,
    const std::vector<char32_t>* first_step_filter, float score_bias) const {
  if (node_idx >= nodes_.size() || results.size() >= max_results) return;

  if (!first_step_filter) {
    const std::size_t base_size = results.size();
    CollectWords(node_idx, path, results, max_results);
    for (std::size_t i = base_size; i < results.size(); ++i) {
      results[i].score += score_bias;
    }
    return;
  }

  const auto& node = nodes_[node_idx];
  if (node.is_terminal()) {
    PredictorResult r;
    r.word = ToUtf8(path);
    r.score = static_cast<float>(node.weight) + score_bias;
    r.source = PredictorResult::kPrefix;
    results.push_back(std::move(r));
  }

  for (const auto& edge : node.children) {
    if (!std::binary_search(first_step_filter->begin(), first_step_filter->end(),
                            edge.label)) {
      continue;
    }
    path.push_back(edge.label);
    const std::size_t base_size = results.size();
    CollectWords(edge.child, path, results, max_results);
    for (std::size_t i = base_size; i < results.size(); ++i) {
      results[i].score += score_bias;
    }
    path.pop_back();
  }
}

void PredictorEngine::SortAndTrimResults(std::vector<PredictorResult>& results,
                                         size_t max_results) {
  std::unordered_map<std::string, PredictorResult> best_by_word;
  best_by_word.reserve(results.size());

  for (auto& result : results) {
    auto it = best_by_word.find(result.word);
    if (it == best_by_word.end() || result.score > it->second.score) {
      best_by_word[result.word] = std::move(result);
    }
  }

  results.clear();
  results.reserve(best_by_word.size());
  for (auto& [_, result] : best_by_word) {
    results.push_back(std::move(result));
  }

  std::sort(results.begin(), results.end(),
            [](const PredictorResult& a, const PredictorResult& b) {
              if (a.score != b.score) return a.score > b.score;
              return a.word < b.word;
            });
  if (results.size() > max_results) results.resize(max_results);
}

float PredictorEngine::HandoffScoreBias(
    const PrototypeContinuationFilters& filters) {
  if (!filters.has_handoff()) return 0.0f;

  // If reverse data says there is no direct continuation family, treat handoff
  // as the primary continuation path instead of a weak add-on.
  if (filters.has_direct() && filters.direct.empty()) return 0.0f;

  // If both families exist, handoff candidates are still useful but secondary.
  if (filters.has_direct() && !filters.direct.empty()) return -5.0f;

  // Handoff-only without explicit direct constraints: keep a light penalty.
  return -2.5f;
}

std::vector<PredictorResult> PredictorEngine::PrefixSearchInternal(
    std::string_view prefix, size_t max_results, bool include_handoff) const {
  std::vector<PredictorResult> results;
  if (nodes_.empty() || prefix.empty()) return results;

  auto cps = Normalize(prefix);
  auto active = BuildActivePrefixState(prefix);
  if (active.empty()) return results;
  const auto filters = GetPrototypeContinuationFilters(active.visible_prefix);

  // Collect words from direct states first.
  for (const auto& state : active.states) {
    if (state.node == TrieNode::kNoChild) continue;
    if (state.kind != ActiveNodeState::Kind::kDirect) continue;
    std::vector<char32_t> path = cps;
    const std::vector<char32_t>* direct_filter =
        filters.has_direct() ? &filters.direct : nullptr;
    CollectWordsFromState(state.node, path, results, max_results * 3,
                          direct_filter);
  }

  // Experimental handoff candidates: same visible prefix, but filtered through
  // the reverse-derived terminal-handoff continuation family.
  if (include_handoff && filters.has_handoff()) {
    const float handoff_score_bias = HandoffScoreBias(filters);
    for (const auto& state : active.states) {
      if (state.node == TrieNode::kNoChild) continue;
      if (state.kind != ActiveNodeState::Kind::kTerminalHandoff) continue;
      std::vector<char32_t> path = cps;
      CollectWordsFromState(state.node, path, results, max_results * 3,
                            &filters.handoff, handoff_score_bias);
    }
  }

  SortAndTrimResults(results, max_results);
  return results;
}

std::vector<PredictorResult> PredictorEngine::PrefixSearch(
    std::string_view prefix, size_t max_results) const {
  return PrefixSearchInternal(prefix, max_results, experimental_handoff_enabled_);
}

std::vector<PredictorResult> PredictorEngine::PrefixSearchDirectOnly(
    std::string_view prefix, size_t max_results) const {
  return PrefixSearchInternal(prefix, max_results, false);
}

std::vector<PredictorResult> PredictorEngine::PrefixSearchWithHandoff(
    std::string_view prefix, size_t max_results) const {
  return PrefixSearchInternal(prefix, max_results, true);
}

// ============================================================================
// Fuzzy Search (Levenshtein distance 1)
// ============================================================================

void PredictorEngine::FuzzyDFS(uint32_t node_idx,
                                const std::vector<char32_t>& target,
                                size_t target_pos, size_t edits_left,
                                std::vector<char32_t>& path,
                                std::vector<PredictorResult>& results,
                                size_t max_results) const {
  if (results.size() >= max_results) return;
  if (node_idx >= nodes_.size()) return;
  const auto& node = nodes_[node_idx];

  // At end of target: remaining path must be 0 or 1 chars (from edits).
  if (target_pos == target.size()) {
    if (node.is_terminal()) {
      PredictorResult r;
      r.word = ToUtf8(path);
      r.score = static_cast<float>(node.weight) * 0.7f;
      r.source = PredictorResult::kFuzzy;
      results.push_back(std::move(r));
    }
    // Try 1 insertion at end.
    if (edits_left > 0) {
      for (const auto& edge : node.children) {
        if (nodes_[edge.child].is_terminal()) {
          path.push_back(edge.label);
          PredictorResult r;
          r.word = ToUtf8(path);
          r.score = static_cast<float>(nodes_[edge.child].weight) * 0.5f;
          r.source = PredictorResult::kFuzzy;
          results.push_back(std::move(r));
          path.pop_back();
          if (results.size() >= max_results) return;
        }
      }
    }
    return;
  }

  char32_t expected = target[target_pos];

  for (const auto& edge : node.children) {
    // Match: no edit.
    if (edge.label == expected) {
      path.push_back(edge.label);
      FuzzyDFS(edge.child, target, target_pos + 1, edits_left, path, results,
               max_results);
      path.pop_back();
    }

    if (edits_left == 0) continue;

    // Substitution: wrong char, consume both.
    if (edge.label != expected) {
      path.push_back(edge.label);
      FuzzyDFS(edge.child, target, target_pos + 1, edits_left - 1, path,
               results, max_results);
      path.pop_back();
    }

    // Insertion: extra char in trie (consume trie edge, keep target pos).
    path.push_back(edge.label);
    FuzzyDFS(edge.child, target, target_pos, edits_left - 1, path, results,
             max_results);
    path.pop_back();

    if (results.size() >= max_results) return;
  }

  // Deletion: skip target char (keep trie node, advance target pos).
  if (edits_left > 0 && target_pos + 1 <= target.size()) {
    FuzzyDFS(node_idx, target, target_pos + 1, edits_left - 1, path, results,
             max_results);
  }
}

std::vector<PredictorResult> PredictorEngine::FuzzySearch(
    std::string_view query, size_t max_results) const {
  std::vector<PredictorResult> results;
  if (nodes_.empty() || query.empty()) return results;

  auto target = Normalize(query);
  std::vector<char32_t> path;
  FuzzyDFS(0, target, 0, 1, path, results, max_results * 3);

  // Deduplicate.
  std::sort(results.begin(), results.end(),
            [](const PredictorResult& a, const PredictorResult& b) {
              return a.word < b.word;
            });
  results.erase(std::unique(results.begin(), results.end(),
                             [](const PredictorResult& a,
                                const PredictorResult& b) {
                               return a.word == b.word;
                             }),
                results.end());

  // Sort by score.
  std::sort(results.begin(), results.end(),
            [](const PredictorResult& a, const PredictorResult& b) {
              return a.score > b.score;
            });
  if (results.size() > max_results) results.resize(max_results);
  return results;
}

// ============================================================================
// Full suggestion pipeline
// ============================================================================

std::vector<PredictorResult> PredictorEngine::GetSuggestions(
    std::string_view token, std::string_view /*context*/,
    size_t max_results) const {
  if (nodes_.empty() || token.empty()) return {};

  std::vector<PredictorResult> results;
  auto norm = Normalize(token);
  std::string norm_str = ToUtf8(norm);
  auto active = BuildActivePrefixState(token);

  // 1. Exact match.
  int32_t exact_weight = -1;
  for (const auto& state : active.states) {
    if (!state.terminal_here) continue;
    exact_weight = std::max(exact_weight, state.weight);
  }
  if (exact_weight >= 0) {
    PredictorResult r;
    r.word = norm_str;
    r.score = static_cast<float>(exact_weight) + 100.0f;
    r.source = PredictorResult::kExact;
    results.push_back(std::move(r));
  }

  // 2. Prefix completions.
  auto prefix_results = PrefixSearch(token, max_results);
  for (auto& pr : prefix_results) {
    if (pr.word != norm_str) {
      pr.score += 50.0f;
      results.push_back(std::move(pr));
    }
  }

  // 3. Fuzzy matches (Levenshtein 1).
  // Do not inject fuzzy corrections when the visible token is already an
  // exact dictionary word. Otherwise high-frequency near words ("он", "то")
  // can outrank useful completions for exact tokens ("она", "что").
  if (exact_weight < 0 && results.size() < max_results) {
    auto fuzzy_results = FuzzySearch(token, max_results);
    for (auto& fr : fuzzy_results) {
      bool dup = false;
      for (const auto& existing : results) {
        if (existing.word == fr.word) {
          dup = true;
          break;
        }
      }
      if (!dup) {
        results.push_back(std::move(fr));
      }
    }
  }

  // 4. Sort by score descending.
  SortAndTrimResults(results, max_results);
  return results;
}

}  // namespace hoskey::engine
