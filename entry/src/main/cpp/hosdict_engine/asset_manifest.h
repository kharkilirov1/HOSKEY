#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace hoskey::engine {

enum class AssetKind {
  kConfig,
  kLexicon,
  kRules,
  kNormDict,
  kLemmaHash,
  kLemmaRules,
  kPolicy,
  kFeatureConfig,
  kSuccinctTrie,
  kRankerModel,
  kContextModel
};

struct AssetDescriptor {
  std::string key;
  AssetKind kind = AssetKind::kConfig;
  std::string format;
  std::string relative_path;
  bool required = true;
};

struct HosdictManifest {
  std::string package_name;
  std::string locale = "ru-RU";
  std::vector<AssetDescriptor> assets;

  [[nodiscard]] const AssetDescriptor* FindByKey(std::string_view key) const;
  [[nodiscard]] std::vector<const AssetDescriptor*> FindByKind(AssetKind kind) const;
};

[[nodiscard]] AssetKind KindFromKey(std::string_view key);

}  // namespace hoskey::engine
