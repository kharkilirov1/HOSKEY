#include "keyboard_engine.h"
#include "prototype_transition_data.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <string>
#include <vector>

using hoskey::engine::KeyboardEngine;
using hoskey::engine::KeyboardDebugConfig;
using hoskey::engine::PredictorOverlayMode;
using hoskey::engine::SuggestRequest;
using hoskey::engine::PrototypeTransitionRecord;
using hoskey::engine::kPrototypeTransitionRecordCount;
using hoskey::engine::kPrototypeTransitionRecords;

namespace {

std::string Quote(const std::string& s) {
  return s;
}

std::string JoinSuggestions(
    const std::vector<hoskey::engine::Suggestion>& suggestions,
    bool with_scores) {
  std::string out;
  for (size_t i = 0; i < suggestions.size(); ++i) {
    if (i != 0) out += "|";
    out += suggestions[i].text;
    if (with_scores) {
      out += "(" + std::to_string(static_cast<int>(suggestions[i].score)) + ")";
    }
  }
  return out;
}

PredictorOverlayMode ParseOverlayMode(std::string_view value) {
  if (value == "split") return PredictorOverlayMode::kSplitOnly;
  if (value == "all") return PredictorOverlayMode::kAll;
  return PredictorOverlayMode::kDisabled;
}

}  // namespace

int main(int argc, char** argv) {
  namespace fs = std::filesystem;

  fs::path package_dir =
      argc > 1 ? fs::path(argv[1])
               : fs::path(
                     R"(C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\src\main\resources\rawfile\dict)");
  fs::path report_path =
      argc > 2 ? fs::path(argv[2])
               : fs::path(
                     R"(C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\reverse\R3_keyboard_engine_diag_2026-04-10.txt)");
  const PredictorOverlayMode overlay_mode =
      argc > 3 ? ParseOverlayMode(argv[3]) : PredictorOverlayMode::kDisabled;
  const bool split_allowlist =
      argc > 4 ? std::string_view(argv[4]) == "1" : false;

  KeyboardEngine engine;
  KeyboardDebugConfig debug_config;
  if (overlay_mode != PredictorOverlayMode::kDisabled) {
    debug_config.override_predictor_overlay_mode = true;
    debug_config.predictor_overlay_mode = overlay_mode;
  }
  if (split_allowlist) {
    debug_config.override_predictor_split_allowlist = true;
    debug_config.predictor_split_allowlist = true;
  }
  auto status = engine.Open(package_dir, debug_config);
  if (!status.ok()) {
    std::cerr << "Open failed: " << status.message << "\n";
    return 1;
  }

  std::vector<std::string> lines;
  lines.push_back("KeyboardEngine diagnostic run over prototype transition prefixes");
  lines.push_back("package_dir=" + package_dir.string());
  lines.push_back("overlay_mode=" +
                  std::string(overlay_mode == PredictorOverlayMode::kSplitOnly
                                  ? "split"
                                  : overlay_mode == PredictorOverlayMode::kAll
                                        ? "all"
                                        : "disabled"));
  lines.push_back("split_allowlist=" + std::string(split_allowlist ? "1" : "0"));
  lines.push_back("");

  std::size_t split_cases = 0;
  std::size_t success_cases = 0;
  std::size_t predictor_cases = 0;
  std::size_t succinct_cases = 0;
  std::size_t sample_logged = 0;
  for (std::size_t i = 0; i < kPrototypeTransitionRecordCount; ++i) {
    const PrototypeTransitionRecord& record = kPrototypeTransitionRecords[i];
    SuggestRequest request;
    request.token = record.prefix_utf8;
    request.max_suggestions = 6;
    auto response = engine.GetSuggestions(request);
    if (!response.status.ok()) {
      lines.push_back("prefix=" + std::string(record.prefix_utf8) +
                      " ERROR=" + response.status.message);
      continue;
    }
    ++success_cases;
    if (response.diagnostics.find("trie=") != std::string::npos) ++predictor_cases;
    if (response.diagnostics.find("succinct=ok") != std::string::npos) ++succinct_cases;
    const bool is_split =
        response.diagnostics.find("pred_split=1") != std::string::npos;
    if (sample_logged < 12) {
      ++sample_logged;
      lines.push_back("sample_prefix=" + std::string(record.prefix_utf8));
      lines.push_back("  diagnostics=" + response.diagnostics);
      lines.push_back("  suggestions=" +
                      JoinSuggestions(response.suggestions, /*with_scores=*/false));
    }
    if (is_split) {
      ++split_cases;
      lines.push_back("prefix=" + std::string(record.prefix_utf8));
      lines.push_back("  diagnostics=" + response.diagnostics);
      lines.push_back("  suggestions=" +
                      JoinSuggestions(response.suggestions, /*with_scores=*/true));
    }
    lines.push_back("CASE\t" + std::string(record.prefix_utf8) + "\t" +
                    (is_split ? "1" : "0") + "\t" +
                    JoinSuggestions(response.suggestions, /*with_scores=*/false) +
                    "\t" + response.diagnostics);
  }

  lines.push_back("");
  lines.push_back("success_cases=" + std::to_string(success_cases));
  lines.push_back("predictor_cases=" + std::to_string(predictor_cases));
  lines.push_back("succinct_cases=" + std::to_string(succinct_cases));
  lines.push_back("split_cases=" + std::to_string(split_cases));
  lines.push_back("record_count=" + std::to_string(kPrototypeTransitionRecordCount));

  std::string report;
  for (const auto& line : lines) {
    report += line;
    report.push_back('\n');
  }

  std::ofstream out(report_path, std::ios::binary);
  out << report;
  out.close();

  std::cout << report;
  return 0;
}
