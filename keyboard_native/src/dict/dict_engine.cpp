/**
 * HOSKEY Keyboard - Dictionary Engine Implementation
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#include "dict_engine.h"
#include "../platform/harmony_log.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

namespace keyboard {
namespace core {

static constexpr const char* TAG = "DictEngine";

DictEngine::DictEngine() {
    mainRoot_ = std::make_unique<TrieNode>();
}

DictEngine::~DictEngine() {
    if (personalDictDirty_) {
        savePersonalDict();
    }
}

bool DictEngine::loadMainDict(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ifstream file(path);
    if (!file.is_open()) {
        platform::logError(TAG, "Failed to open dictionary: %s", path.c_str());
        return false;
    }

    // Reset trie
    mainRoot_ = std::make_unique<TrieNode>();
    mainWordCount_ = 0;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        // Format: word<TAB>frequency or just word
        std::string word;
        uint8_t frequency = 128;  // Default middle frequency

        size_t tabPos = line.find('\t');
        if (tabPos != std::string::npos) {
            word = line.substr(0, tabPos);
            try {
                int freq = std::stoi(line.substr(tabPos + 1));
                frequency = static_cast<uint8_t>(std::min(255, std::max(0, freq)));
            } catch (...) {
                // Use default frequency
            }
        } else {
            word = line;
        }

        if (!word.empty()) {
            insertWord(mainRoot_.get(), word, frequency);
            mainWordCount_++;
        }
    }

    mainDictLoaded_ = true;
    platform::logInfo(TAG, "Loaded %zu words from %s", mainWordCount_, path.c_str());

    return true;
}

bool DictEngine::loadMainDictFd(int fd, size_t offset, size_t length) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Memory map the file
    void* mapped = mmap(nullptr, length, PROT_READ, MAP_PRIVATE, fd, offset);
    if (mapped == MAP_FAILED) {
        platform::logError(TAG, "Failed to mmap dictionary fd");
        return false;
    }

    // Reset trie
    mainRoot_ = std::make_unique<TrieNode>();
    mainWordCount_ = 0;

    // Parse memory-mapped content
    const char* data = static_cast<const char*>(mapped);
    const char* end = data + length;
    const char* lineStart = data;

    while (lineStart < end) {
        // Find end of line
        const char* lineEnd = lineStart;
        while (lineEnd < end && *lineEnd != '\n') {
            lineEnd++;
        }

        if (lineEnd > lineStart && *lineStart != '#') {
            std::string line(lineStart, lineEnd - lineStart);

            std::string word;
            uint8_t frequency = 128;

            size_t tabPos = line.find('\t');
            if (tabPos != std::string::npos) {
                word = line.substr(0, tabPos);
                try {
                    int freq = std::stoi(line.substr(tabPos + 1));
                    frequency = static_cast<uint8_t>(std::min(255, std::max(0, freq)));
                } catch (...) {}
            } else {
                word = line;
            }

            if (!word.empty()) {
                insertWord(mainRoot_.get(), word, frequency);
                mainWordCount_++;
            }
        }

        lineStart = lineEnd + 1;
    }

    munmap(mapped, length);

    mainDictLoaded_ = true;
    platform::logInfo(TAG, "Loaded %zu words from fd", mainWordCount_);

    return true;
}

bool DictEngine::loadPersonalDict(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);

    personalDictPath_ = path;
    personalDict_.clear();

    std::ifstream file(path);
    if (!file.is_open()) {
        // Personal dict might not exist yet, that's OK
        personalDictLoaded_ = true;
        return true;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::string word;
        int frequency = 1;

        size_t tabPos = line.find('\t');
        if (tabPos != std::string::npos) {
            word = line.substr(0, tabPos);
            try {
                frequency = std::stoi(line.substr(tabPos + 1));
            } catch (...) {}
        } else {
            word = line;
        }

        if (!word.empty()) {
            personalDict_[word] = frequency;
        }
    }

    personalDictLoaded_ = true;
    platform::logInfo(TAG, "Loaded %zu personal words from %s",
                     personalDict_.size(), path.c_str());

    return true;
}

bool DictEngine::loadPersonalDictFd(int fd, size_t offset, size_t length) {
    // Similar to loadMainDictFd but for personal dict
    void* mapped = mmap(nullptr, length, PROT_READ, MAP_PRIVATE, fd, offset);
    if (mapped == MAP_FAILED) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    personalDict_.clear();

    const char* data = static_cast<const char*>(mapped);
    const char* end = data + length;
    const char* lineStart = data;

    while (lineStart < end) {
        const char* lineEnd = lineStart;
        while (lineEnd < end && *lineEnd != '\n') {
            lineEnd++;
        }

        if (lineEnd > lineStart && *lineStart != '#') {
            std::string line(lineStart, lineEnd - lineStart);

            std::string word;
            int frequency = 1;

            size_t tabPos = line.find('\t');
            if (tabPos != std::string::npos) {
                word = line.substr(0, tabPos);
                try {
                    frequency = std::stoi(line.substr(tabPos + 1));
                } catch (...) {}
            } else {
                word = line;
            }

            if (!word.empty()) {
                personalDict_[word] = frequency;
            }
        }

        lineStart = lineEnd + 1;
    }

    munmap(mapped, length);
    personalDictLoaded_ = true;

    return true;
}

bool DictEngine::savePersonalDict() {
    if (personalDictPath_.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    std::ofstream file(personalDictPath_);
    if (!file.is_open()) {
        platform::logError(TAG, "Failed to save personal dict to %s",
                          personalDictPath_.c_str());
        return false;
    }

    file << "# HOSKEY Personal Dictionary\n";
    for (const auto& [word, freq] : personalDict_) {
        file << word << "\t" << freq << "\n";
    }

    personalDictDirty_ = false;
    return true;
}

bool DictEngine::contains(const std::string& word) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check personal dict first
    if (personalDict_.count(word) > 0) {
        return true;
    }

    // Check main dict
    const TrieNode* node = findNode(mainRoot_.get(), word);
    return node && node->isTerminal;
}

uint8_t DictEngine::getFrequency(const std::string& word) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // Personal dict boost
    auto it = personalDict_.find(word);
    if (it != personalDict_.end()) {
        return static_cast<uint8_t>(std::min(255, 200 + it->second));
    }

    // Main dict
    const TrieNode* node = findNode(mainRoot_.get(), word);
    if (node && node->isTerminal) {
        return node->frequency;
    }

    return 0;
}

std::vector<DictSuggestion> DictEngine::getSuggestions(const std::string& prefix,
                                                        int maxResults) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<DictSuggestion> results;

    // Get from main dict
    const TrieNode* node = findNode(mainRoot_.get(), prefix);
    if (node) {
        collectSuggestions(node, prefix, results, maxResults * 2, false);
    }

    // Add matching personal dict words
    for (const auto& [word, freq] : personalDict_) {
        if (word.length() >= prefix.length() &&
            word.substr(0, prefix.length()) == prefix) {
            DictSuggestion s;
            s.word = word;
            s.frequency = static_cast<uint8_t>(std::min(255, 200 + freq));
            s.score = s.frequency / 255.0f;
            s.isPersonal = true;
            results.push_back(s);
        }
    }

    // Sort by score (frequency)
    std::sort(results.begin(), results.end(),
        [](const DictSuggestion& a, const DictSuggestion& b) {
            return a.score > b.score;
        });

    // Limit results
    if (static_cast<int>(results.size()) > maxResults) {
        results.resize(maxResults);
    }

    return results;
}

void DictEngine::addToPersonalDict(const std::string& word, int frequency) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = personalDict_.find(word);
    if (it != personalDict_.end()) {
        it->second += frequency;
    } else {
        personalDict_[word] = frequency;
    }

    personalDictDirty_ = true;
}

void DictEngine::removeFromPersonalDict(const std::string& word) {
    std::lock_guard<std::mutex> lock(mutex_);

    personalDict_.erase(word);
    personalDictDirty_ = true;
}

void DictEngine::boostPersonalWord(const std::string& word, int boost) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = personalDict_.find(word);
    if (it != personalDict_.end()) {
        it->second += boost;
        personalDictDirty_ = true;
    }
}

size_t DictEngine::getMemoryUsage() const {
    // Rough estimate
    size_t usage = sizeof(*this);
    usage += mainWordCount_ * 100;  // Approximate per-word overhead
    usage += personalDict_.size() * 50;
    return usage;
}

void DictEngine::insertWord(TrieNode* root, const std::string& word, uint8_t frequency) {
    auto codePoints = toCodePoints(word);
    TrieNode* node = root;

    for (char32_t cp : codePoints) {
        auto& child = node->children[cp];
        if (!child) {
            child = std::make_unique<TrieNode>();
        }
        node = child.get();
    }

    node->isTerminal = true;
    node->frequency = std::max(node->frequency, frequency);
}

const TrieNode* DictEngine::findNode(const TrieNode* root, const std::string& prefix) const {
    if (!root) return nullptr;

    auto codePoints = toCodePoints(prefix);
    const TrieNode* node = root;

    for (char32_t cp : codePoints) {
        auto it = node->children.find(cp);
        if (it == node->children.end()) {
            return nullptr;
        }
        node = it->second.get();
    }

    return node;
}

void DictEngine::collectSuggestions(const TrieNode* node, const std::string& prefix,
                                     std::vector<DictSuggestion>& results,
                                     int maxResults, bool isPersonal) const {
    if (!node) return;

    if (static_cast<int>(results.size()) >= maxResults) {
        return;
    }

    if (node->isTerminal) {
        DictSuggestion s;
        s.word = prefix;
        s.frequency = node->frequency;
        s.score = node->frequency / 255.0f;
        s.isPersonal = isPersonal;
        results.push_back(s);
    }

    // DFS to collect more
    for (const auto& [cp, child] : node->children) {
        if (static_cast<int>(results.size()) >= maxResults) {
            break;
        }

        std::string newPrefix = prefix + fromCodePoints({cp});
        collectSuggestions(child.get(), newPrefix, results, maxResults, isPersonal);
    }
}

std::vector<char32_t> DictEngine::toCodePoints(const std::string& text) {
    std::vector<char32_t> result;
    result.reserve(text.size());

    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        char32_t cp = 0;
        size_t len = 1;

        if ((c & 0x80) == 0) {
            cp = c;
            len = 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
            cp = (c & 0x1F) << 6;
            cp |= (static_cast<unsigned char>(text[i + 1]) & 0x3F);
            len = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < text.size()) {
            cp = (c & 0x0F) << 12;
            cp |= (static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6;
            cp |= (static_cast<unsigned char>(text[i + 2]) & 0x3F);
            len = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < text.size()) {
            cp = (c & 0x07) << 18;
            cp |= (static_cast<unsigned char>(text[i + 1]) & 0x3F) << 12;
            cp |= (static_cast<unsigned char>(text[i + 2]) & 0x3F) << 6;
            cp |= (static_cast<unsigned char>(text[i + 3]) & 0x3F);
            len = 4;
        }

        result.push_back(cp);
        i += len;
    }

    return result;
}

std::string DictEngine::fromCodePoints(const std::vector<char32_t>& codePoints) {
    std::string result;
    result.reserve(codePoints.size() * 4);

    for (char32_t cp : codePoints) {
        if (cp < 0x80) {
            result.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            result.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            result.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            result.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            result.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    return result;
}

} // namespace core
} // namespace keyboard
