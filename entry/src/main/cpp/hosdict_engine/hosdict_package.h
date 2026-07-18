#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "asset_manifest.h"
#include "hosdict_container.h"
#include "loaders.h"
#include "status.h"

namespace hoskey::engine {

class HosdictPackage {
 public:
  // Open a .hosdict container file.
  [[nodiscard]] Status Open(const std::filesystem::path& container_path);

  // Scan directory for .hosdict and open the first one found.
  [[nodiscard]] Status OpenFromDir(const std::filesystem::path& dir);

  void Close();

  [[nodiscard]] bool is_open() const { return is_open_; }
  [[nodiscard]] const HosdictManifest& manifest() const { return manifest_; }
  [[nodiscard]] const DictionaryStore& store() const { return store_; }
  [[nodiscard]] const HosdictContainer& container() const { return container_; }
  [[nodiscard]] const std::string& raw_config_json() const { return raw_config_json_; }
  [[nodiscard]] const std::vector<LoadReport>& reports() const { return reports_; }
  [[nodiscard]] std::string DiagnosticsSummary() const;
  [[nodiscard]] size_t retained_source_payload_bytes() const {
    return container_.retained_payload_bytes() + store_.BinaryPayloadBytes();
  }
  void ReleaseSourcePayloads();

 private:
  [[nodiscard]] Status LoadContainerAssets();

  bool is_open_ = false;
  HosdictManifest manifest_;
  DictionaryStore store_;
  HosdictContainer container_;
  std::string raw_config_json_;
  std::vector<LoadReport> reports_;
};

}  // namespace hoskey::engine
