/**
 * Patricia Trie implementation
 */

#include "trie.h"
#include "trie_node.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>

namespace hoskey {

Trie::Trie() : root_(std::make_unique<TrieNode>()), wordCount_(0) {}

Trie::~Trie() = default;

bool Trie::loadFromFile(const std::string& path) {
    // Determine format by extension
    if (path.size() >= 5 && path.substr(path.size() - 5) == ".dict") {
        return loadFromBinaryFile(path);
    }
    return loadFromTextFile(path);
}

bool Trie::loadFromTextFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    clear();

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        // Format: word<TAB>frequency or just word
        std::string word;
        int frequency = 100; // Default frequency

        size_t tabPos = line.find('\t');
        if (tabPos != std::string::npos) {
            word = line.substr(0, tabPos);
            try {
                frequency = std::stoi(line.substr(tabPos + 1));
            } catch (...) {
                frequency = 100;
            }
        } else {
            word = line;
        }

        // Normalize: trim whitespace
        while (!word.empty() && (word.back() == ' ' || word.back() == '\r')) {
            word.pop_back();
        }

        if (!word.empty()) {
            insert(word, frequency);
        }
    }

    return wordCount_ > 0;
}

// loadFromBinaryFile is implemented in binary_dict_reader.cpp

void Trie::insert(const std::string& word, int frequency) {
    if (word.empty()) return;

    TrieNode* current = root_.get();

    // Handle UTF-8: iterate by bytes (works for both ASCII and Cyrillic)
    for (size_t i = 0; i < word.size(); i++) {
        char c = word[i];
        current = current->getOrCreateChild(c);
    }

    if (!current->isEndOfWord()) {
        wordCount_++;
    }

    current->setEndOfWord(true);
    current->setFrequency(frequency);
}

bool Trie::contains(const std::string& word) const {
    if (word.empty()) return false;

    const TrieNode* current = root_.get();

    for (size_t i = 0; i < word.size(); i++) {
        char c = word[i];
        current = current->getChild(c);
        if (!current) return false;
    }

    return current->isEndOfWord();
}

int Trie::getFrequency(const std::string& word) const {
    if (word.empty()) return 0;

    const TrieNode* current = root_.get();

    for (size_t i = 0; i < word.size(); i++) {
        char c = word[i];
        current = current->getChild(c);
        if (!current) return 0;
    }

    return current->isEndOfWord() ? current->getFrequency() : 0;
}

std::vector<WordEntry> Trie::findByPrefix(const std::string& prefix, int limit) const {
    std::vector<WordEntry> results;

    if (prefix.empty()) {
        // Return most frequent words
        collectWords(root_.get(), "", results, limit);
        return results;
    }

    // Navigate to prefix node
    const TrieNode* current = root_.get();
    for (size_t i = 0; i < prefix.size(); i++) {
        char c = prefix[i];
        current = current->getChild(c);
        if (!current) return results; // No words with this prefix
    }

    // Collect all words from this point
    collectWords(current, prefix, results, limit);

    // Sort by frequency (descending)
    std::sort(results.begin(), results.end(),
              [](const WordEntry& a, const WordEntry& b) {
                  return a.frequency > b.frequency;
              });

    // Limit results
    if (results.size() > static_cast<size_t>(limit)) {
        results.resize(limit);
    }

    return results;
}

void Trie::collectWords(const TrieNode* node, const std::string& prefix,
                        std::vector<WordEntry>& results, int limit) const {
    if (!node || results.size() >= static_cast<size_t>(limit * 3)) {
        // Collect more than needed, will sort and trim later
        return;
    }

    if (node->isEndOfWord()) {
        results.emplace_back(prefix, node->getFrequency(), node->getProbability());
    }

    for (const auto& pair : node->getChildren()) {
        collectWords(pair.second.get(), prefix + pair.first, results, limit);
    }
}

std::vector<std::string> Trie::getWordsByFirstLetter(char letter, int limit) const {
    std::vector<std::string> results;

    const TrieNode* letterNode = root_->getChild(letter);
    if (!letterNode) return results;

    std::vector<WordEntry> entries;
    collectWords(letterNode, std::string(1, letter), entries, limit);

    // Sort by frequency
    std::sort(entries.begin(), entries.end(),
              [](const WordEntry& a, const WordEntry& b) {
                  return a.frequency > b.frequency;
              });

    // Extract words
    for (const auto& entry : entries) {
        if (results.size() >= static_cast<size_t>(limit)) break;
        results.push_back(entry.word);
    }

    return results;
}

size_t Trie::getMemoryUsage() const {
    return root_ ? root_->getMemoryUsage() : 0;
}

void Trie::clear() {
    root_ = std::make_unique<TrieNode>();
    wordCount_ = 0;
}

} // namespace hoskey
