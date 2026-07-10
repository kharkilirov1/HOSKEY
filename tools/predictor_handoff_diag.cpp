#include "predictor_engine.h"
#include "prototype_transition_data.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

using hoskey::engine::PredictorEngine;
using hoskey::engine::PredictorResult;
using hoskey::engine::PrototypeTransitionRecord;
using hoskey::engine::kPrototypeTransitionRecordCount;
using hoskey::engine::kPrototypeTransitionRecords;

namespace {

std::vector<char32_t> ToCodepoints(std::string_view utf8) {
  std::vector<char32_t> result;
  const auto* d = reinterpret_cast<const uint8_t*>(utf8.data());
  size_t pos = 0;
  while (pos < utf8.size()) {
    const uint8_t b = d[pos];
    char32_t ch = 0;
    if (b <= 0x7F) {
      ch = b;
      pos += 1;
    } else if ((b & 0xE0) == 0xC0 && pos + 1 < utf8.size()) {
      ch = ((b & 0x1F) << 6) | (d[pos + 1] & 0x3F);
      pos += 2;
    } else if ((b & 0xF0) == 0xE0 && pos + 2 < utf8.size()) {
      ch = ((b & 0x0F) << 12) | ((d[pos + 1] & 0x3F) << 6) | (d[pos + 2] & 0x3F);
      pos += 3;
    } else {
      pos += 1;
      continue;
    }
    result.push_back(ch);
  }
  return result;
}

std::string ToUtf8(const std::vector<char32_t>& codepoints) {
  std::string result;
  for (char32_t ch : codepoints) {
    if (ch <= 0x7F) {
      result.push_back(static_cast<char>(ch));
    } else if (ch <= 0x7FF) {
      result.push_back(static_cast<char>(0xC0 | (ch >> 6)));
      result.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
    } else {
      result.push_back(static_cast<char>(0xE0 | (ch >> 12)));
      result.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
      result.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
    }
  }
  return result;
}

std::vector<char32_t> SortedUnique(std::string_view utf8) {
  auto cps = ToCodepoints(utf8);
  std::sort(cps.begin(), cps.end());
  cps.erase(std::unique(cps.begin(), cps.end()), cps.end());
  return cps;
}

std::string FormatWords(const std::vector<PredictorResult>& results) {
  std::string out;
  for (size_t i = 0; i < results.size(); ++i) {
    if (i != 0) out += "|";
    out += results[i].word;
  }
  return out;
}

std::unordered_set<std::string> BuildExpectedWordSet(
    const std::string& prefix, const std::vector<char32_t>& chars) {
  std::unordered_set<std::string> out;
  for (char32_t ch : chars) {
    std::vector<char32_t> cps = ToCodepoints(prefix);
    cps.push_back(ch);
    out.insert(ToUtf8(cps));
  }
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  std::string report_path;
  if (argc > 1) report_path = argv[1];

  std::vector<std::string> words;
  std::vector<int32_t> weights;
  words.reserve(kPrototypeTransitionRecordCount * 8);
  weights.reserve(kPrototypeTransitionRecordCount * 8);

  for (std::size_t i = 0; i < kPrototypeTransitionRecordCount; ++i) {
    const PrototypeTransitionRecord& record = kPrototypeTransitionRecords[i];
    words.push_back(record.prefix_utf8);
    weights.push_back(100);

    for (char32_t ch : SortedUnique(record.direct_utf8)) {
      auto cps = ToCodepoints(record.prefix_utf8);
      cps.push_back(ch);
      words.push_back(ToUtf8(cps));
      weights.push_back(10);
    }
    for (char32_t ch : SortedUnique(record.handoff_utf8)) {
      auto cps = ToCodepoints(record.prefix_utf8);
      cps.push_back(ch);
      words.push_back(ToUtf8(cps));
      weights.push_back(5);
    }
  }

  PredictorEngine engine;
  engine.Build(words, weights);

  std::vector<std::string> lines;
  lines.push_back("Predictor handoff diagnostics over generated prototype transition data");
  lines.push_back("Synthetic lexicon built from prefix + direct/handoff continuation records.");
  lines.push_back("");

  std::size_t split_cases = 0;
  for (std::size_t i = 0; i < kPrototypeTransitionRecordCount; ++i) {
    const PrototypeTransitionRecord& record = kPrototypeTransitionRecords[i];
    auto direct_results = engine.PrefixSearchDirectOnly(record.prefix_utf8, 16);
    auto handoff_results = engine.PrefixSearchWithHandoff(record.prefix_utf8, 16);

    if (FormatWords(direct_results) == FormatWords(handoff_results)) continue;
    ++split_cases;

    lines.push_back("prefix=" + std::string(record.prefix_utf8));
    lines.push_back("  direct=" + FormatWords(direct_results));
    lines.push_back("  handoff=" + FormatWords(handoff_results));
    lines.push_back("  expected_direct=" + std::string(record.direct_utf8));
    lines.push_back("  expected_handoff=" + std::string(record.handoff_utf8));
  }

  lines.push_back("");
  lines.push_back("split_cases=" + std::to_string(split_cases));
  lines.push_back("record_count=" + std::to_string(kPrototypeTransitionRecordCount));

  std::string report;
  for (const auto& line : lines) {
    report += line;
    report.push_back('\n');
  }

  if (!report_path.empty()) {
    std::ofstream out(report_path, std::ios::binary);
    out << report;
  }

  std::cout << report;
  return 0;
}
