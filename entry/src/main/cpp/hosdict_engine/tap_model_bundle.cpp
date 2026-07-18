#include "tap_model_bundle.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace hoskey::engine {
namespace {

struct Slice {
  const uint8_t* data = nullptr;
  size_t size = 0;
};

struct Field {
  uint32_t number = 0;
  uint8_t wire = 0;
  uint64_t integer = 0;
  Slice bytes;
};

Status ReadVarint(const uint8_t* data, size_t size, size_t* position,
                  uint64_t* value) {
  if (!data || !position || !value) {
    return Status::InvalidArgument("TapModel varint null argument");
  }
  uint64_t result = 0;
  for (unsigned shift = 0; shift < 70; shift += 7) {
    if (*position >= size) {
      return Status::ParseError("TapModel truncated protobuf varint");
    }
    const uint8_t byte = data[(*position)++];
    if (shift == 63 && (byte & 0xFEu) != 0) {
      return Status::ParseError("TapModel protobuf varint overflow");
    }
    result |= static_cast<uint64_t>(byte & 0x7Fu) << shift;
    if ((byte & 0x80u) == 0) {
      *value = result;
      return Status::Ok();
    }
  }
  return Status::ParseError("TapModel protobuf varint exceeds 10 bytes");
}

Status ParseFields(Slice input, std::vector<Field>* fields) {
  if (!input.data || !fields) {
    return Status::InvalidArgument("TapModel protobuf null input");
  }
  fields->clear();
  size_t position = 0;
  while (position < input.size) {
    uint64_t tag = 0;
    Status status =
        ReadVarint(input.data, input.size, &position, &tag);
    if (!status.ok()) return status;
    const uint64_t number = tag >> 3;
    const uint8_t wire = static_cast<uint8_t>(tag & 7u);
    if (number == 0 || number > std::numeric_limits<uint32_t>::max()) {
      return Status::ParseError("TapModel invalid protobuf field number");
    }
    Field field;
    field.number = static_cast<uint32_t>(number);
    field.wire = wire;
    if (wire == 0) {
      status = ReadVarint(input.data, input.size, &position, &field.integer);
      if (!status.ok()) return status;
    } else if (wire == 2) {
      uint64_t length = 0;
      status = ReadVarint(input.data, input.size, &position, &length);
      if (!status.ok()) return status;
      if (length > input.size - position) {
        return Status::ParseError(
            "TapModel truncated length-delimited protobuf field");
      }
      field.bytes = {input.data + position, static_cast<size_t>(length)};
      position += static_cast<size_t>(length);
    } else if (wire == 5) {
      if (input.size - position < sizeof(uint32_t)) {
        return Status::ParseError("TapModel truncated fixed32 protobuf field");
      }
      field.bytes = {input.data + position, sizeof(uint32_t)};
      position += sizeof(uint32_t);
    } else {
      return Status::ParseError("TapModel unsupported protobuf wire type");
    }
    fields->push_back(field);
  }
  return Status::Ok();
}

bool IsValidUtf8(Slice value) {
  if (!value.data || value.size == 0) return false;
  size_t i = 0;
  while (i < value.size) {
    const uint8_t first = value.data[i++];
    if (first < 0x80u) {
      if (first == 0) return false;
      continue;
    }
    unsigned continuation = 0;
    uint32_t codepoint = 0;
    if ((first & 0xE0u) == 0xC0u) {
      continuation = 1;
      codepoint = first & 0x1Fu;
      if (codepoint < 2) return false;
    } else if ((first & 0xF0u) == 0xE0u) {
      continuation = 2;
      codepoint = first & 0x0Fu;
    } else if ((first & 0xF8u) == 0xF0u) {
      continuation = 3;
      codepoint = first & 0x07u;
    } else {
      return false;
    }
    if (continuation > value.size - i) return false;
    for (unsigned n = 0; n < continuation; ++n) {
      const uint8_t byte = value.data[i++];
      if ((byte & 0xC0u) != 0x80u) return false;
      codepoint = (codepoint << 6) | (byte & 0x3Fu);
    }
    if ((continuation == 2 && codepoint < 0x800u) ||
        (continuation == 3 && codepoint < 0x10000u) ||
        (codepoint >= 0xD800u && codepoint <= 0xDFFFu) ||
        codepoint > 0x10FFFFu) {
      return false;
    }
  }
  return true;
}

Status RejectUnknown(const std::vector<Field>& fields,
                     std::initializer_list<std::pair<uint32_t, uint8_t>> allowed,
                     const char* label) {
  for (const Field& field : fields) {
    bool found = false;
    for (const auto& entry : allowed) {
      if (entry.first == field.number && entry.second == field.wire) {
        found = true;
        break;
      }
    }
    if (!found) {
      return Status::ParseError(std::string(label) +
                                ": unexpected protobuf field/wire");
    }
  }
  return Status::Ok();
}

const Field* Single(const std::vector<Field>& fields, uint32_t number,
                    uint8_t wire, bool* duplicate) {
  const Field* result = nullptr;
  for (const Field& field : fields) {
    if (field.number != number) continue;
    if (field.wire != wire || result != nullptr) {
      if (duplicate) *duplicate = true;
      return nullptr;
    }
    result = &field;
  }
  return result;
}

float ReadFloat(const Field* field) {
  if (!field) return 0.0f;
  float value = 0.0f;
  std::memcpy(&value, field->bytes.data, sizeof(value));
  return value;
}

Status ParseKeyParams(Slice input, TapKeyParams* output) {
  std::vector<Field> fields;
  Status status = ParseFields(input, &fields);
  if (!status.ok()) return status;
  status = RejectUnknown(fields,
                         {{1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 5},
                          {6, 5}, {7, 5}, {8, 5}, {9, 5}, {10, 5}},
                         "TKeyParams");
  if (!status.ok()) return status;
  bool duplicate = false;
  const Field* x = Single(fields, 1, 0, &duplicate);
  const Field* y = Single(fields, 2, 0, &duplicate);
  const Field* width = Single(fields, 3, 0, &duplicate);
  const Field* height = Single(fields, 4, 0, &duplicate);
  const Field* prior = Single(fields, 5, 5, &duplicate);
  const Field* mean_x = Single(fields, 6, 5, &duplicate);
  const Field* mean_y = Single(fields, 7, 5, &duplicate);
  const Field* variance_x = Single(fields, 8, 5, &duplicate);
  const Field* variance_y = Single(fields, 9, 5, &duplicate);
  const Field* correlation = Single(fields, 10, 5, &duplicate);
  if (duplicate) {
    return Status::ParseError("TKeyParams repeated field/wrong wire");
  }
  const auto narrow = [](const Field* field, uint32_t* value) {
    if (!field) {
      *value = 0;
      return true;
    }
    if (field->integer > std::numeric_limits<uint32_t>::max()) return false;
    *value = static_cast<uint32_t>(field->integer);
    return true;
  };
  TapKeyParams parsed;
  if (!narrow(x, &parsed.x) || !narrow(y, &parsed.y) ||
      !narrow(width, &parsed.width) || !narrow(height, &parsed.height)) {
    return Status::ParseError("TKeyParams integer overflow");
  }
  parsed.log_prior = ReadFloat(prior);
  parsed.mean_x = ReadFloat(mean_x);
  parsed.mean_y = ReadFloat(mean_y);
  parsed.variance_x = ReadFloat(variance_x);
  parsed.variance_y = ReadFloat(variance_y);
  parsed.correlation = ReadFloat(correlation);
  const float values[] = {parsed.log_prior, parsed.mean_x, parsed.mean_y,
                          parsed.variance_x, parsed.variance_y,
                          parsed.correlation};
  for (float value : values) {
    if (!std::isfinite(value)) {
      return Status::ParseError("TKeyParams non-finite parameter");
    }
  }
  if (parsed.width == 0 || parsed.height == 0 ||
      parsed.variance_x < 0.0f || parsed.variance_y < 0.0f ||
      parsed.correlation < -1.0f || parsed.correlation > 1.0f) {
    return Status::ParseError("TKeyParams invalid dimensions/Gaussian");
  }
  *output = parsed;
  return Status::Ok();
}

Status ParseKeyEntry(Slice input, std::string* key, TapKeyParams* params) {
  std::vector<Field> fields;
  Status status = ParseFields(input, &fields);
  if (!status.ok()) return status;
  status = RejectUnknown(fields, {{1, 2}, {2, 2}}, "KeyParamsEntry");
  if (!status.ok()) return status;
  bool duplicate = false;
  const Field* name = Single(fields, 1, 2, &duplicate);
  const Field* value = Single(fields, 2, 2, &duplicate);
  if (duplicate || !name || !value || !IsValidUtf8(name->bytes)) {
    return Status::ParseError("TapModel invalid key map entry");
  }
  *key = std::string(reinterpret_cast<const char*>(name->bytes.data),
                     name->bytes.size);
  return ParseKeyParams(value->bytes, params);
}

Status ParseStaticModel(Slice input, StaticTapModel* output) {
  std::vector<Field> fields;
  Status status = ParseFields(input, &fields);
  if (!status.ok()) return status;
  status = RejectUnknown(fields, {{1, 2}, {2, 0}, {3, 0}},
                         "TStaticTapModelParams");
  if (!status.ok()) return status;
  bool duplicate = false;
  const Field* width = Single(fields, 2, 0, &duplicate);
  const Field* height = Single(fields, 3, 0, &duplicate);
  if (duplicate || !width || !height || width->integer == 0 ||
      height->integer == 0 ||
      width->integer > std::numeric_limits<uint32_t>::max() ||
      height->integer > std::numeric_limits<uint32_t>::max()) {
    return Status::ParseError("TapModel invalid static model dimensions");
  }
  StaticTapModel parsed;
  parsed.width = static_cast<uint32_t>(width->integer);
  parsed.height = static_cast<uint32_t>(height->integer);
  for (const Field& field : fields) {
    if (field.number != 1) continue;
    std::string key;
    TapKeyParams params;
    status = ParseKeyEntry(field.bytes, &key, &params);
    if (!status.ok()) return status;
    if (!parsed.key_params.emplace(std::move(key), params).second) {
      return Status::ParseError("TapModel duplicate key map entry");
    }
  }
  if (parsed.key_params.empty()) {
    return Status::ParseError("TapModel static model has empty key map");
  }
  *output = std::move(parsed);
  return Status::Ok();
}

Status ParseLayoutEntry(Slice input, std::string* name,
                        StaticTapModel* model) {
  std::vector<Field> fields;
  Status status = ParseFields(input, &fields);
  if (!status.ok()) return status;
  status = RejectUnknown(fields, {{1, 2}, {2, 2}}, "ParamsEntry");
  if (!status.ok()) return status;
  bool duplicate = false;
  const Field* key = Single(fields, 1, 2, &duplicate);
  const Field* value = Single(fields, 2, 2, &duplicate);
  if (duplicate || !key || !value || !IsValidUtf8(key->bytes)) {
    return Status::ParseError("TapModel invalid layout map entry");
  }
  *name = std::string(reinterpret_cast<const char*>(key->bytes.data),
                      key->bytes.size);
  return ParseStaticModel(value->bytes, model);
}

}  // namespace

Status TapModelBundle::Load(const uint8_t* data, size_t size) {
  if (!data || size == 0) {
    return Status::InvalidArgument("TapModel bundle is empty");
  }
  size_t position = 0;
  uint64_t frame_size = 0;
  Status status = ReadVarint(data, size, &position, &frame_size);
  if (!status.ok()) return status;
  if (frame_size != size - position) {
    return Status::ParseError("TapModel frame size mismatch");
  }
  std::vector<Field> fields;
  status = ParseFields({data + position, size - position}, &fields);
  if (!status.ok()) return status;
  status = RejectUnknown(fields, {{1, 2}}, "TTapModelBundleParams");
  if (!status.ok()) return status;

  std::unordered_map<std::string, StaticTapModel> layouts;
  size_t key_count = 0;
  for (const Field& field : fields) {
    std::string name;
    StaticTapModel model;
    status = ParseLayoutEntry(field.bytes, &name, &model);
    if (!status.ok()) return status;
    key_count += model.key_params.size();
    if (!layouts.emplace(std::move(name), std::move(model)).second) {
      return Status::ParseError("TapModel duplicate layout map entry");
    }
  }
  if (layouts.empty()) {
    return Status::ParseError("TapModel bundle has no layouts");
  }

  layouts_ = std::move(layouts);
  key_count_ = key_count;
  loaded_ = true;
  diagnostics_ = "asset=loaded,contract=protobuf_exact,layouts=" +
                 std::to_string(layouts_.size()) + ",keys=" +
                 std::to_string(key_count_);
  return Status::Ok();
}

void TapModelBundle::Reset() {
  loaded_ = false;
  key_count_ = 0;
  layouts_.clear();
  diagnostics_ = "not_loaded";
}

const StaticTapModel* TapModelBundle::FindLayout(std::string_view name) const {
  const auto it = layouts_.find(std::string(name));
  return it == layouts_.end() ? nullptr : &it->second;
}

const TapKeyParams* TapModelBundle::FindKey(const StaticTapModel& layout,
                                            std::string_view key) const {
  const auto it = layout.key_params.find(std::string(key));
  return it == layout.key_params.end() ? nullptr : &it->second;
}

}  // namespace hoskey::engine
