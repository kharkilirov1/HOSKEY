#include "hosdict_engine/hosdict_container.h"
#include "hosdict_engine/tap_model_runner.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

hoskey::engine::TapModelAssetBuffer Asset(
    const hoskey::engine::HosdictContainer& container,
    std::string_view key, std::string_view format) {
  const auto* entry = container.FindAsset(key);
  Check(entry != nullptr, "packaged TapModel asset missing");
  Check(entry->format == format, "packaged TapModel format mismatch");
  const uint8_t* data = nullptr;
  size_t size = 0;
  const auto status = container.GetAssetData(key, &data, &size);
  Check(status.ok() && data != nullptr && size != 0,
        "packaged TapModel bytes unavailable");
  return {data, size};
}

}  // namespace

int main(int argc, char** argv) {
  Check(argc == 2, "usage: test_tap_model_package ru.hoskey.hosdict");
  hoskey::engine::HosdictContainer container;
  const auto open = container.Open(argv[1]);
  Check(open.ok(), open.message.c_str());

  hoskey::engine::TapModelAssets assets{
      Asset(container, "tap.models_default", "tap_model_bundle"),
      Asset(container, "features.tap_model", "feature_json"),
      Asset(container, "tap.ranker.hobl", "oblivious_tree_v1"),
      Asset(container, "tap.classifier.hobl", "oblivious_tree_v1"),
  };
  hoskey::engine::TapModelRunner runner;
  const auto load = runner.Load(assets);
  Check(load.ok(), load.message.c_str());
  Check(runner.is_loaded(), "packaged TapModel chain did not activate");
  Check(runner.layout_count() == 6 && runner.key_count() == 2684,
        "packaged TapModel bundle identity mismatch");
  Check(runner.ranker_feature_count() == 38 &&
            runner.classifier_feature_count() == 78,
        "packaged TapModel model contract mismatch");
  std::cout << "PASS: packaged TapModel 122.5 atomic chain loads; "
               "layouts=6 keys=2684 features=38/78\n";
  return 0;
}
