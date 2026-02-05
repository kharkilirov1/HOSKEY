/**
 * Patricia Trie implementation
 */

#include "trie.h"
#include "trie_node.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <hilog/log.h>

#undef LOG_TAG
#define LOG_TAG "HOSKEY-TRIE"

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

// Static counter for debug logging
static int s_insertCallCount = 0;

void Trie::insert(const std::string& word, int frequency) {
    s_insertCallCount++;

    if (word.empty()) {
        if (s_insertCallCount < 10) OH_LOG_WARN(LOG_APP, "Trie::insert SKIP empty word");
        return;
    }
    if (!root_) {
        if (s_insertCallCount < 10) OH_LOG_WARN(LOG_APP, "Trie::insert SKIP no root");
        return;
    }

    TrieNode* current = root_.get();
    if (!current) {
        if (s_insertCallCount < 10) OH_LOG_WARN(LOG_APP, "Trie::insert SKIP null root.get()");
        return;
    }

    // DEBUG: Log first few insertions with this pointer
    if (s_insertCallCount <= 5) {
        std::string hexWord;
        for (unsigned char c : word) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02X ", c);
            hexWord += buf;
        }
        OH_LOG_INFO(LOG_APP, "Trie::insert[%{public}d] this=%{public}p root=%{public}p word=\"%{public}s\" bytes=[%{public}s] freq=%{public}d",
                    s_insertCallCount, static_cast<void*>(this), static_cast<void*>(root_.get()),
                    word.c_str(), hexWord.c_str(), frequency);
    }

    // Handle UTF-8: iterate by bytes (works for both ASCII and Cyrillic)
    for (size_t i = 0; i < word.size(); i++) {
        unsigned char c = static_cast<unsigned char>(word[i]);
        TrieNode* nextNode = current->getOrCreateChild(c);
        if (s_insertCallCount <= 3) {
            OH_LOG_INFO(LOG_APP, "Trie::insert[%{public}d] step %{public}zu byte=0x%{public}02X current=%{public}p next=%{public}p",
                        s_insertCallCount, i, c, static_cast<void*>(current), static_cast<void*>(nextNode));
        }
        current = nextNode;
        if (!current) {
            if (s_insertCallCount < 10) OH_LOG_ERROR(LOG_APP, "Trie::insert FAIL getOrCreateChild returned null at i=%{public}zu", i);
            return;  // Safety: bail if allocation failed
        }
    }

    if (!current->isEndOfWord()) {
        wordCount_++;
    }

    current->setEndOfWord(true);
    current->setFrequency(frequency);

    // DEBUG: Verify insertion worked (for first few words)
    if (s_insertCallCount <= 5) {
        bool found = contains(word);
        OH_LOG_INFO(LOG_APP, "Trie::insert[%{public}d] VERIFY contains(\"%{public}s\")=%{public}s wordCount=%{public}d",
                    s_insertCallCount, word.c_str(), found ? "YES" : "NO", wordCount_);
    }
}

bool Trie::contains(const std::string& word) const {
    static int s_containsCount = 0;
    s_containsCount++;
    bool shouldLog = (s_containsCount <= 20);

    if (word.empty()) return false;

    if (shouldLog) {
        OH_LOG_INFO(LOG_APP, "contains[%{public}d]: word=\"%{public}s\" this=%{public}p root=%{public}p",
                    s_containsCount, word.c_str(), static_cast<const void*>(this), static_cast<const void*>(root_.get()));
    }

    const TrieNode* current = root_.get();

    for (size_t i = 0; i < word.size(); i++) {
        unsigned char c = static_cast<unsigned char>(word[i]);
        const TrieNode* nextNode = current->getChild(c);
        if (shouldLog && i < 3) {
            OH_LOG_INFO(LOG_APP, "contains[%{public}d]: step %{public}zu byte=0x%{public}02X current=%{public}p next=%{public}p",
                        s_containsCount, i, c, static_cast<const void*>(current), static_cast<const void*>(nextNode));
        }
        current = nextNode;
        if (!current) {
            if (shouldLog) {
                OH_LOG_INFO(LOG_APP, "contains[%{public}d]: FAILED at step %{public}zu - no child for 0x%{public}02X",
                            s_containsCount, i, c);
            }
            return false;
        }
    }

    bool result = current->isEndOfWord();
    if (shouldLog) {
        OH_LOG_INFO(LOG_APP, "contains[%{public}d]: reached end node=%{public}p isEndOfWord=%{public}s",
                    s_containsCount, static_cast<const void*>(current), result ? "YES" : "NO");
    }
    return result;
}

int Trie::getFrequency(const std::string& word) const {
    if (word.empty()) return 0;

    const TrieNode* current = root_.get();

    for (size_t i = 0; i < word.size(); i++) {
        unsigned char c = static_cast<unsigned char>(word[i]);
        current = current->getChild(c);
        if (!current) return 0;
    }

    return current->isEndOfWord() ? current->getFrequency() : 0;
}

std::vector<WordEntry> Trie::findByPrefix(const std::string& prefix, int limit) const {
    std::vector<WordEntry> results;

    // DEBUG: Log entry
    static int s_findPrefixCount = 0;
    s_findPrefixCount++;
    bool shouldLog = (s_findPrefixCount <= 10);

    if (shouldLog) {
        std::string hexPrefix;
        for (unsigned char c : prefix) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02X ", c);
            hexPrefix += buf;
        }
        OH_LOG_INFO(LOG_APP, "findByPrefix[%{public}d]: prefix=\"%{public}s\" bytes=[%{public}s] limit=%{public}d this=%{public}p root=%{public}p wordCount=%{public}d",
                    s_findPrefixCount, prefix.c_str(), hexPrefix.c_str(), limit,
                    static_cast<const void*>(this), static_cast<const void*>(root_.get()), wordCount_);
    }

    if (prefix.empty()) {
        // Return most frequent words
        collectWords(root_.get(), "", results, limit);
        if (shouldLog) {
            OH_LOG_INFO(LOG_APP, "findByPrefix[%{public}d]: empty prefix, collected %{public}zu results", s_findPrefixCount, results.size());
        }
        return results;
    }

    // Navigate to prefix node
    const TrieNode* current = root_.get();
    for (size_t i = 0; i < prefix.size(); i++) {
        unsigned char c = static_cast<unsigned char>(prefix[i]);
        const TrieNode* nextNode = current->getChild(c);
        if (shouldLog) {
            OH_LOG_INFO(LOG_APP, "findByPrefix[%{public}d]: step %{public}zu byte=0x%{public}02X current=%{public}p next=%{public}p",
                        s_findPrefixCount, i, c, static_cast<const void*>(current), static_cast<const void*>(nextNode));
        }
        current = nextNode;
        if (!current) {
            if (shouldLog) {
                OH_LOG_INFO(LOG_APP, "findByPrefix[%{public}d]: FAILED at step %{public}zu - no child for byte 0x%{public}02X",
                            s_findPrefixCount, i, c);
            }
            return results; // No words with this prefix
        }
    }

    if (shouldLog) {
        OH_LOG_INFO(LOG_APP, "findByPrefix[%{public}d]: reached prefix node=%{public}p, collecting words...",
                    s_findPrefixCount, static_cast<const void*>(current));
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

    // Use forEachChild with new array-based TrieNode
    node->forEachChild([this, &prefix, &results, limit](char c, const TrieNode* child) {
        collectWords(child, prefix + c, results, limit);
    });
}

std::vector<std::string> Trie::getWordsByFirstLetter(char letter, int limit) const {
    std::vector<std::string> results;

    unsigned char uc = static_cast<unsigned char>(letter);
    const TrieNode* letterNode = root_->getChild(uc);
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
    OH_LOG_INFO(LOG_APP, "Trie::clear() called, current wordCount=%{public}d, this=%{public}p",
                wordCount_, static_cast<void*>(this));
    // First release the old tree explicitly
    // This ensures memory is freed before allocating new root
    root_.reset();
    wordCount_ = 0;

    // Create new empty root
    root_ = std::make_unique<TrieNode>();
    OH_LOG_INFO(LOG_APP, "Trie::clear() done, new root=%{public}p", static_cast<void*>(root_.get()));
}

bool Trie::saveToFile(const std::string& path) const {
    OH_LOG_INFO(LOG_APP, "Trie::saveToFile: saving %{public}d words to %{public}s",
                wordCount_, path.c_str());

    std::ofstream file(path);
    if (!file.is_open()) {
        OH_LOG_ERROR(LOG_APP, "Trie::saveToFile: failed to open file %{public}s", path.c_str());
        return false;
    }

    // Header
    file << "# HOSKEY User Dictionary\n";
    file << "# Format: word<TAB>frequency\n";

    // Collect all words
    std::vector<WordEntry> allWords;
    collectWords(root_.get(), "", allWords, wordCount_ + 100);  // Get all

    // Write each word
    int savedCount = 0;
    for (const auto& entry : allWords) {
        if (entry.frequency > 0) {  // Skip "deleted" words (freq=0)
            file << entry.word << "\t" << entry.frequency << "\n";
            savedCount++;
        }
    }

    file.close();

    OH_LOG_INFO(LOG_APP, "Trie::saveToFile: saved %{public}d words", savedCount);
    return true;
}

} // namespace hoskey
