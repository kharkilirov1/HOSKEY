#include "hosdict_container.h"
#include "one_nc7_trie.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using hoskey::engine::HosdictContainer;
using hoskey::engine::OneNc7NextLetters;
using hoskey::engine::OneNc7NodeState;
using hoskey::engine::OneNc7Trie;

namespace {

struct Failure {
  std::string message;
};

void Require(bool condition, std::string message, std::vector<Failure>* failures) {
  if (!condition && failures) failures->push_back({std::move(message)});
}

std::string JoinNodes(const std::vector<OneNc7NodeState>& states) {
  std::ostringstream out;
  for (size_t i = 0; i < states.size(); ++i) {
    if (i != 0) out << ",";
    out << states[i].node_id << ":term=" << (states[i].terminal ? 1 : 0)
        << ":child=" << (states[i].has_child ? 1 : 0);
  }
  return out.str();
}

std::string JoinExpectedNodes(const std::vector<uint32_t>& expected) {
  std::ostringstream out;
  for (size_t i = 0; i < expected.size(); ++i) {
    if (i != 0) out << ",";
    out << expected[i];
  }
  return out.str();
}

std::vector<uint32_t> NodeIds(const std::vector<OneNc7NodeState>& states) {
  std::vector<uint32_t> ids;
  ids.reserve(states.size());
  for (const auto& state : states) ids.push_back(state.node_id);
  return ids;
}

bool HasCompletion(const std::vector<hoskey::engine::OneNc7Completion>& items,
                   const std::string& text) {
  return std::any_of(items.begin(), items.end(), [&](const auto& item) {
    return item.text == text;
  });
}

void CheckPrefix(OneNc7Trie* trie, const std::string& prefix,
                 const std::vector<uint32_t>& expected_nodes,
                 const std::string& expected_all,
                 const std::string& expected_nonterminal,
                 const std::string& expected_terminal,
                 std::vector<Failure>* failures) {
  const auto states = trie->WalkPrefix(prefix);
  const auto ids = NodeIds(states);
  std::cout << "prefix." << prefix << ".states=" << JoinNodes(states) << "\n";
  Require(ids == expected_nodes,
          "prefix '" + prefix + "' nodes mismatch: got [" + JoinNodes(states) +
              "], expected [" + JoinExpectedNodes(expected_nodes) + "]",
          failures);

  const OneNc7NextLetters next = trie->NextLetters(prefix);
  std::cout << "prefix." << prefix << ".next.all=" << next.all
            << " nonterm=" << next.nonterminal
            << " term=" << next.terminal << "\n";
  Require(next.all == expected_all,
          "prefix '" + prefix + "' next.all mismatch: got '" + next.all +
              "', expected '" + expected_all + "'",
          failures);
  Require(next.nonterminal == expected_nonterminal,
          "prefix '" + prefix + "' next.nonterminal mismatch: got '" +
              next.nonterminal + "', expected '" + expected_nonterminal + "'",
          failures);
  Require(next.terminal == expected_terminal,
          "prefix '" + prefix + "' next.terminal mismatch: got '" +
              next.terminal + "', expected '" + expected_terminal + "'",
          failures);
}

std::filesystem::path FindContainer(const std::filesystem::path& package_dir) {
  for (const auto& entry : std::filesystem::directory_iterator(package_dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".hosdict") {
      return entry.path();
    }
  }
  return {};
}

}  // namespace

int main(int argc, char** argv) {
  namespace fs = std::filesystem;
  const fs::path package_dir =
      argc > 1 ? fs::path(argv[1])
               : fs::path(
                     R"(C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\src\main\resources\rawfile\dict)");

  std::vector<Failure> failures;
  const fs::path container_path = FindContainer(package_dir);
  Require(!container_path.empty(), "no .hosdict container found in package dir", &failures);
  if (!failures.empty()) {
    for (const auto& failure : failures) std::cerr << failure.message << "\n";
    return 1;
  }

  HosdictContainer container;
  auto st = container.Open(container_path);
  Require(st.ok(), "container open failed: " + st.message, &failures);

  const uint8_t* data = nullptr;
  size_t data_size = 0;
  if (st.ok()) {
    st = container.GetAssetData("lexicon.default", &data, &data_size);
    Require(st.ok(), "lexicon.default missing: " + st.message, &failures);
  }

  OneNc7Trie trie;
  if (st.ok()) {
    st = trie.Parse(data, data_size);
    Require(st.ok(), "OneNc7Trie parse failed: " + st.message, &failures);
  }

  if (trie.loaded()) {
    std::cout << "one_nc7.loaded=1 node_capacity=" << trie.node_capacity() << "\n";
    std::cout << "one_nc7.root_letters=" << trie.RootLettersUtf8() << "\n";
    std::cout << "one_nc7.levels=" << trie.levels().size()
              << " " << trie.FormatLevelSummary() << "\n";
    const auto suffix_head = trie.SuffixEntries(2, 12);
    std::cout << "one_nc7.level2.suffix_count=" << trie.SuffixEntryCount(2)
              << " head=";
    for (size_t i = 0; i < suffix_head.size(); ++i) {
      if (i != 0) std::cout << "|";
      std::cout << suffix_head[i].offset << ":" << suffix_head[i].text;
    }
    std::cout << "\n";
    Require(trie.RootLettersUtf8() == "абвгдежзийклмнопрстуфхцчшщыэюя",
            "root alphabet mismatch: " + trie.RootLettersUtf8(), &failures);
    Require(trie.levels().size() >= 3,
            "recursive level skeleton should expose at least 3 levels",
            &failures);
    if (trie.levels().size() >= 3) {
      const auto& l0 = trie.levels()[0];
      const auto& l1 = trie.levels()[1];
      const auto& l2 = trie.levels()[2];
      Require(l0.start_offset == 0x8 && l0.label_blob_offset == 0xFDB98 &&
                  l0.label_blob_size == 999288 && l0.child_flag == 1,
              "level0 recursive layout mismatch", &failures);
      Require(l1.start_offset == 0x1F1B20 &&
                  l1.label_blob_offset == 0x1FA530 &&
                  l1.label_blob_size == 42248 && l1.child_flag == 1,
              "level1 recursive layout mismatch", &failures);
      Require(l2.start_offset == 0x204A48 &&
                  l2.label_blob_offset == 0x205A20 &&
                  l2.label_blob_size == 4752 &&
                  l2.suffix_blob_offset == 0x206CB8 &&
                  l2.suffix_blob_size == 3208 && l2.child_flag == 0,
              "level2 recursive layout mismatch", &failures);
      Require(l0.has_child_raw_offset == 0x8 &&
                  l0.louds_raw_offset == 0x278D8 &&
                  l0.terminal_raw_offset == 0x753C8 &&
                  l0.terminal_rank_ds_offset == 0x93BC8,
              "level0 raw/rank layout mismatch", &failures);
      Require(l0.intvector_offset == 0x9B5E8 &&
                  l0.intvector_data_offset == 0x9B608 &&
                  l0.intvector_bitwidth == 8 &&
                  l0.intvector_mask == 0xFF &&
                  l0.intvector_total_bits == 3222592 &&
                  l0.intvector_word_count == 50353,
              "level0 intvector layout mismatch", &failures);
      Require(l1.intvector_offset == 0x1F85C0 &&
                  l1.intvector_data_offset == 0x1F85E0 &&
                  l1.intvector_bitwidth == 5 &&
                  l1.intvector_mask == 0x1F &&
                  l1.intvector_total_bits == 64055 &&
                  l1.intvector_word_count == 1001,
              "level1 intvector layout mismatch", &failures);
      Require(l2.intvector_offset == 0x205758 &&
                  l2.intvector_data_offset == 0x205778 &&
                  l2.intvector_bitwidth == 4 &&
                  l2.intvector_mask == 0xF &&
                  l2.intvector_total_bits == 5372 &&
                  l2.intvector_word_count == 84,
              "level2 intvector layout mismatch", &failures);
      Require(trie.SuffixEntryCount(2) == 770,
              "level2 suffix pool entry count mismatch", &failures);
      Require(trie.HasSuffixEntry(2, "про"),
              "level2 suffix pool missing 'про'", &failures);
      Require(trie.HasSuffixEntry(2, " собач"),
              "level2 suffix pool missing ' собач'", &failures);
      Require(trie.HasSuffixEntry(2, "кого-нельзя"),
              "level2 suffix pool missing 'кого-нельзя'", &failures);
      Require(!suffix_head.empty() && suffix_head[0].offset == 1 &&
                  suffix_head[0].text == " а",
              "level2 suffix pool head mismatch", &failures);

      uint64_t upper = 0;
      uint64_t key = 0;
      const uint64_t la_rank = trie.RootTerminalRank(325);
      std::cout << "one_nc7.root_terminal.la.rank=" << la_rank << "\n";
      Require(la_rank == 29, "root terminal rank for node 325 mismatch",
              &failures);
      Require(trie.PackedUpper(0, la_rank, &upper) && upper == 14,
              "level0 packed upper for node 325 terminal rank mismatch",
              &failures);
      Require(trie.TerminalKey(0, 325, la_rank, &key) && key == 0xEE0,
              "FUN_00661462 terminal key for node 325 mismatch", &failures);
      std::cout << "one_nc7.root_terminal.la.key=0x" << std::hex << key
                << std::dec << "\n";
      Require(trie.RootTerminalKey(325, &key) && key == 0xEE0,
              "RootTerminalKey for node 325 mismatch", &failures);
      Require(!trie.RootTerminalKey(308, &key),
              "RootTerminalKey should reject non-terminal node 308", &failures);
    }

    const auto l_completions = trie.TerminalCompletions("л", 2, 32);
    std::cout << "one_nc7.completions.л=";
    for (size_t i = 0; i < l_completions.size(); ++i) {
      if (i != 0) std::cout << "|";
      std::cout << l_completions[i].text << "@" << l_completions[i].terminal_node_id;
    }
    std::cout << "\n";
    Require(HasCompletion(l_completions, "ла"),
            "native root-phase terminal completions should include 'ла'",
            &failures);
    const auto al_completions = trie.TerminalCompletions("ал", 1, 32);
    Require(HasCompletion(al_completions, "алв"),
            "native root-phase terminal completions should include 'алв'",
            &failures);
    const auto ya_completions = trie.TerminalCompletions("я", 1, 32);
    Require(HasCompletion(ya_completions, "яу"),
            "native root-phase terminal completions should include 'яу'",
            &failures);

    CheckPrefix(&trie, "ла", {308, 325},
                "абвгдежзийклмнопрстуфхцчшыюя",
                "бвгдежзийклмнопрстуфхцчшюя",
                "аеуы", &failures);
    CheckPrefix(&trie, "лаб", {3401},
                "аируы", "аируы", "", &failures);
    CheckPrefix(&trie, "алв", {964, 982},
                "икюя", "", "икюя", &failures);
    CheckPrefix(&trie, "бое", {1340, 1346, 1355},
                "вкмнпсц", "вкмнпсц", "", &failures);
    CheckPrefix(&trie, "ст", {490},
                "адеикорсуыэя", "адеикорсуыэя", "", &failures);
    CheckPrefix(&trie, "пр", {431},
                "аеиоуыя", "аеиоуыя", "", &failures);
    CheckPrefix(&trie, "не", {368},
                "абвгдежзийклмнопрстуфхцчшщэюя",
                "абвгдежзийклмнопрстуфхцчшщэюя",
                "", &failures);
    CheckPrefix(&trie, "она", {4361},
                "гмхця", "гмхця", "", &failures);
    CheckPrefix(&trie, "что", {6634},
                "б", "б", "", &failures);
    CheckPrefix(&trie, "яу", {720, 739},
                "зо", "з", "о", &failures);

    Require(trie.HasTerminalBoundary("ла"),
            "prefix 'ла' should expose terminal handoff state", &failures);
    Require(!trie.HasTerminalBoundary("пр"),
            "prefix 'пр' should not expose terminal handoff state in R3 root phase",
            &failures);
    Require(trie.Contains("хор"),
            "iterator-backed exact lookup should accept materialized word 'хор'",
            &failures);
    Require(!trie.Contains("прив"),
            "iterator-backed exact lookup should not accept prefix-only 'прив'",
            &failures);
    Require(trie.StaticWeight("хор") > 0,
            "StaticWeight should expose native 1nc7 weight for exact 'хор'",
            &failures);
    Require(trie.StaticWeight("прив") == 0,
            "StaticWeight should reject prefix-only 'прив'", &failures);
  }

  if (!failures.empty()) {
    std::cerr << "\nONE_NC7 ACCEPTANCE FAILURES: " << failures.size() << "\n";
    for (const auto& failure : failures) {
      std::cerr << " - " << failure.message << "\n";
    }
    return 1;
  }

  std::cout << "\nONE_NC7 ACCEPTANCE OK\n";
  return 0;
}
