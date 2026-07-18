#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "status.h"

namespace hoskey::engine {

// hosdict container: manifest-driven binary asset package.
//
// Binary format:
//   [4]  magic "HSKD"
//   [4]  version (1)
//   [4]  asset_count
//   [4]  manifest_size
//   [manifest_size] JSON manifest (UTF-8)
//   For each asset (asset_count times):
//     [4]  key_length
//     [key_length] key (UTF-8)
//     [4]  format_length
//     [format_length] format string (UTF-8)
//     [8]  data_offset (from file start)
//     [8]  data_size
//   [padding to 16-byte alignment]
//   Asset data blobs (concatenated, each 16-byte aligned)

class HosdictContainer {
 public:
  static constexpr uint32_t kMagic = 0x444B5348;  // "HSKD" little-endian
  static constexpr uint32_t kCurrentVersion = 1;
  static constexpr size_t kAlignment = 16;

  struct AssetEntry {
    std::string key;
    std::string format;
    uint64_t offset = 0;
    uint64_t size = 0;
  };

  HosdictContainer() = default;
  ~HosdictContainer() { Close(); }

  HosdictContainer(const HosdictContainer&) = delete;
  HosdictContainer& operator=(const HosdictContainer&) = delete;
  HosdictContainer(HosdictContainer&&) = default;
  HosdictContainer& operator=(HosdictContainer&&) = default;

  // Load from file path.
  [[nodiscard]] Status Open(const std::filesystem::path& path);

  // Load from memory buffer (copies data).
  [[nodiscard]] Status Open(const uint8_t* data, size_t size);

  // Get asset entry by key. Returns nullptr if not found.
  [[nodiscard]] const AssetEntry* FindAsset(std::string_view key) const;

  // List all asset keys.
  [[nodiscard]] std::vector<std::string> AssetKeys() const;

  // Get zero-copy pointer into loaded data for a given asset key.
  [[nodiscard]] Status GetAssetData(std::string_view key,
                                    const uint8_t** data,
                                    size_t* size) const;

  // Metadata accessors.
  [[nodiscard]] std::string package_name() const { return package_name_; }
  [[nodiscard]] std::string locale() const { return locale_; }
  [[nodiscard]] uint32_t version() const { return version_; }
  [[nodiscard]] bool is_open() const { return open_; }
  [[nodiscard]] const std::string& manifest_json() const { return manifest_json_; }
  [[nodiscard]] size_t retained_payload_bytes() const { return file_data_.size(); }

  // Keep the parsed manifest/index but release the duplicated source file once
  // every runtime subsystem has copied or compiled the assets it needs.
  void ReleasePayload();

  void Close();

 private:
  [[nodiscard]] Status ParseHeader();
  [[nodiscard]] Status ParseManifestJson();

  static uint32_t ReadU32(const uint8_t* p);
  static uint64_t ReadU64(const uint8_t* p);

  std::vector<uint8_t> file_data_;
  std::vector<AssetEntry> entries_;
  std::string manifest_json_;
  std::string package_name_;
  std::string locale_;
  uint32_t version_ = 0;
  bool open_ = false;
};

}  // namespace hoskey::engine
