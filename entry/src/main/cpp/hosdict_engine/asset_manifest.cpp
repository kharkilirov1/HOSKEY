#include "asset_manifest.h"

namespace hoskey::engine {

namespace {

bool StartsWith(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

}  // namespace

const AssetDescriptor* HosdictManifest::FindByKey(std::string_view key) const {
  for (const auto& asset : assets) {
    if (asset.key == key) {
      return &asset;
    }
  }
  return nullptr;
}

std::vector<const AssetDescriptor*> HosdictManifest::FindByKind(AssetKind kind) const {
  std::vector<const AssetDescriptor*> result;
  for (const auto& asset : assets) {
    if (asset.kind == kind) {
      result.push_back(&asset);
    }
  }
  return result;
}

AssetKind KindFromKey(std::string_view key) {
  if (key == "config") return AssetKind::kConfig;
  if (key == "lexicon.default" || key == "lexicon.trie") return AssetKind::kSuccinctTrie;
  if (StartsWith(key, "lexicon")) return AssetKind::kLexicon;
  if (StartsWith(key, "rules")) return AssetKind::kRules;
  if (StartsWith(key, "normdict")) return AssetKind::kNormDict;
  if (key == "lemma.hash") return AssetKind::kLemmaHash;
  if (key == "lemma.rules") return AssetKind::kLemmaRules;
  if (StartsWith(key, "policy")) return AssetKind::kPolicy;
  if (StartsWith(key, "features")) return AssetKind::kFeatureConfig;
  if (StartsWith(key, "ranking.")) return AssetKind::kRankerModel;
  if (StartsWith(key, "neural.") || StartsWith(key, "autocc.") ||
      StartsWith(key, "tap.") || StartsWith(key, "emoji.") ||
      StartsWith(key, "rnnt.")) {
    return AssetKind::kContextModel;
  }
  return AssetKind::kConfig;
}

}  // namespace hoskey::engine
