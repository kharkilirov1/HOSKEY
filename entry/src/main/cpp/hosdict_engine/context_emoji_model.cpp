#include "context_emoji_model.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <queue>
#include <sstream>
#include <string_view>

namespace hoskey::engine {
namespace {

constexpr size_t kHeaderSize = 16;
constexpr uint32_t kExpectedOrder = 10;
constexpr size_t kExpectedCatalogSize = 1752;
constexpr size_t kEncoderStateSize = 4000;
constexpr size_t kEncoderHiddenSize = 500;
constexpr size_t kDecoderStateSize = 2500;

bool HasRange(size_t total, size_t offset, size_t length) {
  return offset <= total && length <= total - offset;
}

uint16_t ReadU16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) |
         static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8U);
}

uint32_t ReadU32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8U) |
         (static_cast<uint32_t>(data[2]) << 16U) |
         (static_cast<uint32_t>(data[3]) << 24U);
}

int32_t ReadI32(const uint8_t* data) {
  return static_cast<int32_t>(ReadU32(data));
}

size_t Align16(size_t value) { return (value + 15U) & ~size_t{15U}; }

class FlatBufferReader {
 public:
  FlatBufferReader(const uint8_t* data, size_t size, size_t base)
      : data_(data), size_(size), base_(base) {}

  bool Root(size_t* table) const {
    if (!table || !HasRange(size_, base_, 4)) return false;
    const size_t relative = ReadU32(data_ + base_);
    if (relative > size_ - base_) return false;
    *table = base_ + relative;
    return HasRange(size_, *table, 4);
  }

  bool TableField(size_t table, size_t index, size_t* field) const {
    if (!field || !HasRange(size_, table, 4)) return false;
    const int64_t vtable_signed = static_cast<int64_t>(table) -
                                  static_cast<int64_t>(ReadI32(data_ + table));
    if (vtable_signed < static_cast<int64_t>(base_) ||
        static_cast<uint64_t>(vtable_signed) > size_) {
      return false;
    }
    const size_t vtable = static_cast<size_t>(vtable_signed);
    if (!HasRange(size_, vtable, 4)) return false;
    const uint16_t vtable_size = ReadU16(data_ + vtable);
    const uint16_t object_size = ReadU16(data_ + vtable + 2);
    const size_t entry = vtable + 4 + index * 2;
    if (vtable_size < 4 || (vtable_size & 1U) != 0 ||
        !HasRange(size_, vtable, vtable_size) ||
        !HasRange(vtable + vtable_size, entry, 2)) {
      return false;
    }
    const uint16_t relative = ReadU16(data_ + entry);
    if (relative == 0 || relative >= object_size ||
        !HasRange(size_, table, object_size)) {
      return false;
    }
    *field = table + relative;
    return HasRange(size_, *field, 4);
  }

  bool Vector(size_t field, size_t element_size, size_t* start,
              size_t* count) const {
    if (!start || !count || !HasRange(size_, field, 4)) return false;
    const size_t relative = ReadU32(data_ + field);
    if (relative > size_ - field) return false;
    const size_t vector = field + relative;
    if (!HasRange(size_, vector, 4)) return false;
    *count = ReadU32(data_ + vector);
    *start = vector + 4;
    return *count <= (size_ - *start) / element_size;
  }

  bool StringAtSlot(size_t slot, std::string* value) const {
    if (!value || !HasRange(size_, slot, 4)) return false;
    const size_t relative = ReadU32(data_ + slot);
    if (relative > size_ - slot) return false;
    const size_t string = slot + relative;
    if (!HasRange(size_, string, 4)) return false;
    const size_t length = ReadU32(data_ + string);
    const size_t start = string + 4;
    if (!HasRange(size_, start, length + 1) || data_[start + length] != 0) {
      return false;
    }
    value->assign(reinterpret_cast<const char*>(data_ + start), length);
    return true;
  }

 private:
  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
  size_t base_ = 0;
};

bool ParseU32Vector(const uint8_t* data, size_t size, size_t field,
                    const FlatBufferReader& reader,
                    std::vector<uint32_t>* values) {
  size_t start = 0;
  size_t count = 0;
  if (!values || !reader.Vector(field, 4, &start, &count)) return false;
  values->resize(count);
  for (size_t i = 0; i < count; ++i) {
    (*values)[i] = ReadU32(data + start + i * 4);
  }
  return true;
}

}  // namespace

void ContextEmojiModel::Reset() {
  loaded_ = false;
  order_ = 0;
  decoder_offset_ = 0;
  decoder_size_ = 0;
  catalog_.clear();
  lex_to_rank_.clear();
  rank_to_lex_.clear();
  invocation_count_.store(0, std::memory_order_relaxed);
  scored_count_.store(0, std::memory_order_relaxed);
  fallback_ = "not_loaded";
  latency_us_ = 0;
}

Status ContextEmojiModel::Load(const uint8_t* data, size_t size) {
  Reset();
  if (!data) return Status::InvalidArgument("context emoji data is null");
  if (size < 1024 || std::memcmp(data, "ynnlm_v1", 8) != 0) {
    return Status::ParseError("context emoji ynnlm_v1 header is invalid");
  }
  order_ = ReadU32(data + 8);
  if (order_ != kExpectedOrder || ReadU32(data + 12) != 0) {
    Reset();
    return Status::UnsupportedFormat("context emoji order/reserved mismatch");
  }

  size_t json_end = kHeaderSize;
  while (json_end < size && json_end < 4096 && data[json_end] != 0) {
    ++json_end;
  }
  if (json_end == size || json_end == 4096) {
    Reset();
    return Status::ParseError("context emoji config JSON is unterminated");
  }
  const std::string_view config(reinterpret_cast<const char*>(data + 16),
                                json_end - 16);
  if (config.find("\"Order\": 10") == std::string_view::npos ||
      config.find("\"MaxWordLen\": 24") == std::string_view::npos ||
      config.find("\"UseEncFullState\": false") == std::string_view::npos ||
      config.find("\"EncStateSpec\": \"rwkv:tx;cx;h\"") ==
          std::string_view::npos) {
    Reset();
    return Status::UnsupportedFormat("context emoji config contract mismatch");
  }

  const size_t table_base = Align16(json_end + 1);
  const FlatBufferReader reader(data, size, table_base);
  size_t root = 0;
  size_t catalog_field = 0;
  size_t lex_to_rank_field = 0;
  size_t rank_to_lex_field = 0;
  if (!reader.Root(&root) || !reader.TableField(root, 1, &catalog_field) ||
      !reader.TableField(root, 2, &lex_to_rank_field) ||
      !reader.TableField(root, 3, &rank_to_lex_field)) {
    Reset();
    return Status::ParseError("context emoji vocabulary table is invalid");
  }

  size_t catalog_start = 0;
  size_t catalog_count = 0;
  if (!reader.Vector(catalog_field, 4, &catalog_start, &catalog_count) ||
      catalog_count != kExpectedCatalogSize) {
    Reset();
    return Status::ParseError("context emoji catalog size mismatch");
  }
  catalog_.reserve(catalog_count);
  for (size_t i = 0; i < catalog_count; ++i) {
    std::string value;
    if (!reader.StringAtSlot(catalog_start + i * 4, &value) || value.empty()) {
      Reset();
      return Status::ParseError("context emoji catalog string is invalid");
    }
    catalog_.push_back(std::move(value));
  }
  if (!ParseU32Vector(data, size, lex_to_rank_field, reader, &lex_to_rank_) ||
      !ParseU32Vector(data, size, rank_to_lex_field, reader, &rank_to_lex_) ||
      lex_to_rank_.size() != catalog_count ||
      rank_to_lex_.size() != catalog_count) {
    Reset();
    return Status::ParseError("context emoji rank tables are invalid");
  }
  for (size_t rank = 0; rank < catalog_count; ++rank) {
    const uint32_t lex = rank_to_lex_[rank];
    if (lex >= catalog_count || lex_to_rank_[lex] != rank) {
      Reset();
      return Status::ParseError("context emoji rank tables are not inverse");
    }
  }

  for (size_t offset = table_base; offset + 8 <= size; offset += 4) {
    if (std::memcmp(data + offset + 4, "TFL3", 4) == 0) {
      if (decoder_offset_ != 0) {
        Reset();
        return Status::ParseError("context emoji decoder signature is ambiguous");
      }
      decoder_offset_ = offset;
    }
  }
  if (decoder_offset_ == 0) {
    Reset();
    return Status::ParseError("context emoji decoder is missing");
  }
  decoder_size_ = size - decoder_offset_;
  loaded_ = true;
  fallback_ = "none";
  return Status::Ok();
}

bool ContextEmojiModel::ComposeRwkvTxCxH(
    const std::vector<float>& encoder_state,
    const std::vector<float>& encoder_hidden,
    std::vector<float>* decoder_state) {
  if (!decoder_state || encoder_state.size() != kEncoderStateSize ||
      encoder_hidden.size() != kEncoderHiddenSize) {
    return false;
  }
  decoder_state->clear();
  decoder_state->reserve(kDecoderStateSize);
  const auto append_state = [&](size_t begin) {
    decoder_state->insert(decoder_state->end(), encoder_state.begin() + begin,
                          encoder_state.begin() + begin + 500);
  };
  append_state(0);
  append_state(1500);
  append_state(2000);
  append_state(3500);
  decoder_state->insert(decoder_state->end(), encoder_hidden.begin(),
                        encoder_hidden.end());
  return decoder_state->size() == kDecoderStateSize;
}

std::vector<std::string> ContextEmojiModel::DecodeScores(
    const std::vector<float>& decoder_scores, size_t top_k) const {
  const auto started = std::chrono::steady_clock::now();
  invocation_count_.fetch_add(1, std::memory_order_relaxed);
  const auto finish = [this, started](const char* fallback) {
    fallback_ = fallback;
    latency_us_ = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started)
            .count());
  };
  if (!loaded_) {
    finish("not_loaded");
    return {};
  }
  if (decoder_scores.size() != catalog_.size() || top_k == 0) {
    finish("score_shape");
    return {};
  }
  for (float score : decoder_scores) {
    if (!std::isfinite(score)) {
      finish("nonfinite_scores");
      return {};
    }
  }
  top_k = std::min(top_k, decoder_scores.size());

  struct RankedScore {
    float score;
    size_t rank;
  };
  struct MinimumScore {
    bool operator()(const RankedScore& left, const RankedScore& right) const {
      return left.score > right.score;
    }
  };
  std::priority_queue<RankedScore, std::vector<RankedScore>, MinimumScore> heap;
  for (size_t rank = 0; rank < decoder_scores.size(); ++rank) {
    const RankedScore candidate{decoder_scores[rank], rank};
    if (heap.size() < top_k) {
      heap.push(candidate);
    } else if (candidate.score > heap.top().score) {
      heap.pop();
      heap.push(candidate);
    }
  }
  std::vector<RankedScore> ranked;
  ranked.reserve(heap.size());
  while (!heap.empty()) {
    ranked.push_back(heap.top());
    heap.pop();
  }
  std::sort(ranked.begin(), ranked.end(),
            [](const RankedScore& left, const RankedScore& right) {
              return left.score > right.score;
            });

  std::vector<std::string> result;
  result.reserve(ranked.size());
  for (const RankedScore& item : ranked) {
    const std::string& emoji = emoji_for_rank(item.rank);
    if (emoji != "<unk>") result.push_back(emoji);
  }
  scored_count_.fetch_add(result.size(), std::memory_order_relaxed);
  finish("none");
  return result;
}

const std::string& ContextEmojiModel::emoji_for_rank(size_t rank) const {
  static const std::string kEmpty;
  if (rank >= rank_to_lex_.size()) return kEmpty;
  const uint32_t lex = rank_to_lex_[rank];
  return lex < catalog_.size() ? catalog_[lex] : kEmpty;
}

std::string ContextEmojiModel::diagnostics() const {
  std::ostringstream stream;
  stream << "asset=" << (loaded_ ? "present" : "missing")
         << ",contract=" << (loaded_ ? "valid" : "unsupported:not_loaded")
         << ",variant=default,load=" << (loaded_ ? "loaded" : "not_attempted")
         << ",invoke_count="
         << invocation_count_.load(std::memory_order_relaxed)
         << ",scored_count=" << scored_count_.load(std::memory_order_relaxed)
         << ",fallback=" << fallback_ << ",latency_us=" << latency_us_
         << ",route=wrapper_postprocess_only"
         << ",catalog=" << catalog_.size() << ",order=" << order_
         << ",decoder_offset=" << decoder_offset_;
  return stream.str();
}

}  // namespace hoskey::engine
