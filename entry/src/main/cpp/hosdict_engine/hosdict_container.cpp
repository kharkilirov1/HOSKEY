#include "hosdict_container.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

namespace hoskey::engine {

uint32_t HosdictContainer::ReadU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8U) |
         (static_cast<uint32_t>(p[2]) << 16U) |
         (static_cast<uint32_t>(p[3]) << 24U);
}

uint64_t HosdictContainer::ReadU64(const uint8_t* p) {
  return static_cast<uint64_t>(ReadU32(p)) |
         (static_cast<uint64_t>(ReadU32(p + 4)) << 32U);
}

Status HosdictContainer::Open(const std::filesystem::path& path) {
  Close();

  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return Status::NotFound("HosdictContainer: unable to open file: " + path.string());
  }

  file_data_.assign(std::istreambuf_iterator<char>(input),
                    std::istreambuf_iterator<char>());
  if (file_data_.empty()) {
    return Status::ParseError("HosdictContainer: file is empty: " + path.string());
  }

  return ParseHeader();
}

Status HosdictContainer::Open(const uint8_t* data, size_t size) {
  Close();

  if (data == nullptr || size == 0) {
    return Status::InvalidArgument("HosdictContainer: null or empty data");
  }

  file_data_.assign(data, data + size);
  return ParseHeader();
}

Status HosdictContainer::ParseHeader() {
  const size_t kMinHeaderSize = 16;  // magic + version + asset_count + manifest_size
  if (file_data_.size() < kMinHeaderSize) {
    return Status::ParseError("HosdictContainer: file too small for header");
  }

  const uint8_t* p = file_data_.data();

  // Validate magic.
  const uint32_t magic = ReadU32(p);
  if (magic != kMagic) {
    return Status::ParseError("HosdictContainer: invalid magic (expected HSKD)");
  }

  version_ = ReadU32(p + 4);
  if (version_ == 0 || version_ > kCurrentVersion) {
    return Status::ParseError("HosdictContainer: unsupported version: " +
                              std::to_string(version_));
  }

  const uint32_t asset_count = ReadU32(p + 8);
  const uint32_t manifest_size = ReadU32(p + 12);

  size_t cursor = kMinHeaderSize;

  // Read manifest JSON.
  if (cursor + manifest_size > file_data_.size()) {
    return Status::ParseError("HosdictContainer: manifest overflows file");
  }
  manifest_json_.assign(reinterpret_cast<const char*>(p + cursor), manifest_size);
  cursor += manifest_size;

  // Parse manifest JSON for metadata.
  Status manifest_status = ParseManifestJson();
  if (!manifest_status.ok()) {
    return manifest_status;
  }

  // Read asset index entries.
  entries_.clear();
  entries_.reserve(asset_count);

  for (uint32_t i = 0; i < asset_count; ++i) {
    // key_length + key + format_length + format + offset(8) + size(8)
    if (cursor + 4 > file_data_.size()) {
      return Status::ParseError("HosdictContainer: unexpected EOF in asset index (key_length)");
    }
    const uint32_t key_length = ReadU32(p + cursor);
    cursor += 4;

    if (cursor + key_length > file_data_.size()) {
      return Status::ParseError("HosdictContainer: unexpected EOF in asset index (key)");
    }
    std::string key(reinterpret_cast<const char*>(p + cursor), key_length);
    cursor += key_length;

    if (cursor + 4 > file_data_.size()) {
      return Status::ParseError("HosdictContainer: unexpected EOF in asset index (format_length)");
    }
    const uint32_t format_length = ReadU32(p + cursor);
    cursor += 4;

    if (cursor + format_length > file_data_.size()) {
      return Status::ParseError("HosdictContainer: unexpected EOF in asset index (format)");
    }
    std::string format(reinterpret_cast<const char*>(p + cursor), format_length);
    cursor += format_length;

    if (cursor + 16 > file_data_.size()) {
      return Status::ParseError("HosdictContainer: unexpected EOF in asset index (offset/size)");
    }
    const uint64_t data_offset = ReadU64(p + cursor);
    cursor += 8;
    const uint64_t data_size = ReadU64(p + cursor);
    cursor += 8;

    // Validate that the asset data region is within bounds. Compare without
    // computing data_offset + data_size: both are attacker-controlled uint64
    // values from the file and their sum would wrap around this 64-bit check.
    if (data_offset > file_data_.size() ||
        data_size > file_data_.size() - data_offset) {
      return Status::ParseError("HosdictContainer: asset '" + key +
                                "' data region overflows file");
    }

    entries_.push_back({std::move(key), std::move(format), data_offset, data_size});
  }

  open_ = true;
  return Status::Ok();
}

Status HosdictContainer::ParseManifestJson() {
  // Minimal JSON parsing: extract "package_name" and "locale" strings.
  // We avoid a full JSON parser dependency; the manifest is simple and controlled.

  auto extract_string_value = [](const std::string& json,
                                  const std::string& field) -> std::string {
    const std::string needle = "\"" + field + "\"";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) {
      return {};
    }
    // Skip past the key, find colon, then opening quote.
    size_t i = pos + needle.size();
    while (i < json.size() && (json[i] == ' ' || json[i] == ':' || json[i] == '\t' ||
                                json[i] == '\n' || json[i] == '\r')) {
      ++i;
    }
    if (i >= json.size() || json[i] != '"') {
      return {};
    }
    ++i;  // skip opening quote
    size_t start = i;
    while (i < json.size() && json[i] != '"') {
      ++i;
    }
    return json.substr(start, i - start);
  };

  package_name_ = extract_string_value(manifest_json_, "package_name");
  locale_ = extract_string_value(manifest_json_, "locale");

  // package_name is required.
  if (package_name_.empty()) {
    return Status::ParseError("HosdictContainer: manifest missing 'package_name'");
  }

  return Status::Ok();
}

const HosdictContainer::AssetEntry* HosdictContainer::FindAsset(std::string_view key) const {
  for (const auto& entry : entries_) {
    if (entry.key == key) {
      return &entry;
    }
  }
  return nullptr;
}

std::vector<std::string> HosdictContainer::AssetKeys() const {
  std::vector<std::string> keys;
  keys.reserve(entries_.size());
  for (const auto& entry : entries_) {
    keys.push_back(entry.key);
  }
  return keys;
}

Status HosdictContainer::GetAssetData(std::string_view key,
                                      const uint8_t** data,
                                      size_t* size) const {
  if (!open_) {
    return Status::InvalidState("HosdictContainer: not open");
  }
  if (data == nullptr || size == nullptr) {
    return Status::InvalidArgument("HosdictContainer::GetAssetData: null output pointer");
  }

  const AssetEntry* entry = FindAsset(key);
  if (entry == nullptr) {
    return Status::NotFound("HosdictContainer: asset not found: " + std::string(key));
  }

  *data = file_data_.data() + entry->offset;
  *size = static_cast<size_t>(entry->size);
  return Status::Ok();
}

void HosdictContainer::Close() {
  file_data_.clear();
  entries_.clear();
  manifest_json_.clear();
  package_name_.clear();
  locale_.clear();
  version_ = 0;
  open_ = false;
}

}  // namespace hoskey::engine
