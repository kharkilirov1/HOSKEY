#include "hosdict_package.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

#include "lemmer.h"
#include "normdict_reader.h"
#include "policy_store.h"
#include "succinct_trie.h"

namespace hoskey::engine {

namespace {

bool IsPackagedModelFormat(const std::string& format) {
  return format == "mindspore_model" || format == "ynnlm_v1_bundle" ||
         format == "tflite_model" || format == "catboost_model" ||
         format == "tap_model_bundle" || format == "oblivious_tree_v1" ||
         format == "raw_model_blob";
}

}  // namespace

Status HosdictPackage::Open(const std::filesystem::path& container_path) {
  Close();

  Status status = container_.Open(container_path);
  if (!status.ok()) return status;

  manifest_.package_name = container_.package_name();
  manifest_.locale = container_.locale();

  status = LoadContainerAssets();
  if (!status.ok()) {
    Close();
    return status;
  }

  is_open_ = true;
  return Status::Ok();
}

Status HosdictPackage::OpenFromDir(const std::filesystem::path& dir) {
  // Load .hosdict container
  std::filesystem::path container_path;
  std::filesystem::path first_container;
  std::error_code error;
  std::filesystem::directory_iterator iterator(dir, error);
  if (error) {
    return error == std::errc::no_such_file_or_directory
               ? Status::NotFound("Dictionary directory not found: " +
                                  dir.string())
               : Status::IoError("Cannot scan dictionary directory: " +
                                 dir.string() + ": " + error.message());
  }
  const std::filesystem::directory_iterator end;
  for (; iterator != end; iterator.increment(error)) {
    if (error) break;
    std::error_code entry_error;
    const bool regular = iterator->is_regular_file(entry_error);
    if (entry_error || !regular || iterator->path().extension() != ".hosdict") {
      continue;
    }
    if (first_container.empty()) first_container = iterator->path();
    if (iterator->path().stem().string().find("hoskey") != std::string::npos) {
      container_path = iterator->path();
      break;
    }
  }
  if (error) {
    return Status::IoError("Cannot scan dictionary directory: " + dir.string() +
                           ": " + error.message());
  }
  if (container_path.empty()) container_path = first_container;
  
  if (container_path.empty()) {
    return Status::NotFound("No .hosdict container in: " + dir.string());
  }
  
  Status s = Open(container_path);
  if (!s.ok()) return s;
  
  // No external words.txt fallback here.
  // Runtime lexicon must come from the .hosdict container (binary predictor /
  // lexicon.default), matching the real packaged-dictionary model instead of
  // a loose text-file side channel.
  return Status::Ok();
}

Status HosdictPackage::LoadContainerAssets() {
  const auto keys = container_.AssetKeys();

  for (const auto& key : keys) {
    const auto* entry = container_.FindAsset(key);
    if (!entry) continue;

    const uint8_t* data = nullptr;
    size_t data_size = 0;
    Status s = container_.GetAssetData(key, &data, &data_size);
    if (!s.ok()) continue;

    AssetKind kind = KindFromKey(key);
    AssetDescriptor descriptor;
    descriptor.key = key;
    descriptor.kind = kind;
    descriptor.format = entry->format;

    LoadReport report;
    report.asset_key = key;
    report.format = entry->format;
    report.byte_size = data_size;

    if (entry->format == "feature_json") {
      LoadedFeatureConfig config;
      config.descriptor = descriptor;
      config.raw_json.assign(reinterpret_cast<const char*>(data), data_size);

      if (kind == AssetKind::kConfig) {
        raw_config_json_ = config.raw_json;
      }
      report.notes.push_back("loaded");
      store_.PutFeatureConfig(std::move(config));

    } else if (entry->format == "predictor_trie_v1") {
      LoadedBinaryAsset bin;
      bin.descriptor = descriptor;
      bin.bytes.assign(data, data + data_size);
      bin.notes.push_back("binary predictor trie");
      report.notes = bin.notes;
      store_.PutBinary(std::move(bin));

    } else if (entry->format == "weighted_wordlist_v1") {
      LoadedWordListAsset wl;
      wl.descriptor = descriptor;
      ParseWeightedWordList(data, data_size, &wl.words, &wl.normalized_words, &wl.weights);
      report.notes.push_back("words: " + std::to_string(wl.words.size()) + " (weighted)");
      store_.PutWordList(std::move(wl));

    } else if (entry->format == "utf8_wordlist_v1") {
      LoadedWordListAsset wl;
      wl.descriptor = descriptor;
      ParseUtf8WordList(data, data_size, &wl.words, &wl.normalized_words);
      report.notes.push_back("words: " + std::to_string(wl.words.size()));
      store_.PutWordList(std::move(wl));

    } else if (entry->format == "succinct_lexicon_v1" || entry->format == "1nc7") {
      SuccinctTrie trie;
      // Do NOT extract all 108k words by default to avoid a 6-second UI block.
      // The trie will be used directly via GetCompletions() or Tokenize().
      Status ps = trie.Parse(data, data_size, false);
      if (ps.ok()) {
        LoadedWordListAsset wl;
        wl.descriptor = descriptor;
        // Keep words/weights empty to prevent PredictorEngine from redundantly building a second tree
        wl.notes.push_back("succinct trie (1nc7): loaded for direct matching");
        report.notes.push_back("succinct trie (1nc7): loaded for direct matching");
        store_.PutWordList(std::move(wl));
      } else {
        LoadedBinaryAsset bin;
        bin.descriptor = descriptor;
        bin.bytes.assign(data, data + data_size);
        bin.notes.push_back("succinct trie parse failed: " + ps.message);
        if (!trie.notes().empty()) bin.notes.push_back(trie.notes());
        report.notes = bin.notes;
        store_.PutBinary(std::move(bin));
      }

    } else if (entry->format == "policy_blob") {
      // Try structured decode via PolicyStore.
      PolicyStore policy;
      Status ps = policy.Load(data, data_size);
      if (ps.ok() && policy.loaded() && policy.entry_count() > 0) {
        LoadedWordListAsset wl;
        wl.descriptor = descriptor;
        for (const auto& e : policy.entries()) {
          wl.words.push_back(e);
          wl.normalized_words.push_back(e);
        }
        report.notes.push_back("policy entries: " + std::to_string(wl.words.size()));
        store_.PutWordList(std::move(wl));
      } else {
        // Store as binary for raw lookup.
        LoadedBinaryAsset bin;
        bin.descriptor = descriptor;
        bin.bytes.assign(data, data + data_size);
        report.notes.push_back("binary blob");
        store_.PutBinary(std::move(bin));
      }

    } else if (entry->format == "normdict_v2") {
      NormDictReader reader;
      Status ps = reader.Load(data, data_size);
      LoadedBinaryAsset bin;
      bin.descriptor = descriptor;
      bin.bytes.assign(data, data + data_size);
      if (ps.ok() && reader.loaded()) {
        bin.notes.push_back("lang=" + reader.language());
        bin.notes.push_back("order=" + std::to_string(reader.max_order()));
      }
      report.notes = bin.notes;
      store_.PutBinary(std::move(bin));

    } else if (IsPackagedModelFormat(entry->format)) {
      // Store as binary — specialized model runners decide how to execute it.
      LoadedBinaryAsset bin;
      bin.descriptor = descriptor;
      bin.bytes.assign(data, data + data_size);
      if (entry->format == "mindspore_model") {
        bin.notes.push_back("ms model");
      } else {
        bin.notes.push_back("model blob: " + entry->format);
      }
      report.notes = bin.notes;
      store_.PutBinary(std::move(bin));

    } else if (entry->format == "chd_hash" || entry->format == "lemma_rules_program") {
      LoadedBinaryAsset bin;
      bin.descriptor = descriptor;
      bin.bytes.assign(data, data + data_size);

      Lemmer lemmer;
      if (entry->format == "chd_hash") {
        Status ls = lemmer.LoadHash(data, data_size);
        if (ls.ok()) bin.notes.push_back("CHD validated, keys=" + std::to_string(lemmer.num_keys()));
      } else {
        Status ls = lemmer.LoadRules(data, data_size);
        if (ls.ok()) bin.notes.push_back("rules validated, count=" + std::to_string(lemmer.num_rules()));
      }
      report.notes = bin.notes;
      store_.PutBinary(std::move(bin));

    } else {
      // Generic binary.
      LoadedBinaryAsset bin;
      bin.descriptor = descriptor;
      bin.bytes.assign(data, data + data_size);
      report.notes.push_back("binary");
      store_.PutBinary(std::move(bin));
    }

    manifest_.assets.push_back(descriptor);
    reports_.push_back(report);
  }

  return Status::Ok();
}

void HosdictPackage::Close() {
  is_open_ = false;
  manifest_ = HosdictManifest{};
  store_.Reset();
  container_.Close();
  raw_config_json_.clear();
  reports_.clear();
}

void HosdictPackage::ReleaseSourcePayloads() {
  store_.ReleaseBinaryPayloads();
  container_.ReleasePayload();
}

std::string HosdictPackage::DiagnosticsSummary() const {
  if (reports_.empty()) return {};
  std::ostringstream oss;
  bool first = true;
  for (const auto& r : reports_) {
    if (r.notes.empty()) continue;
    if (!first) oss << "; ";
    first = false;
    oss << r.asset_key << "[";
    for (size_t i = 0; i < r.notes.size(); ++i) {
      if (i) oss << ", ";
      oss << r.notes[i];
    }
    oss << "]";
  }
  return oss.str();
}

}  // namespace hoskey::engine
