/**
 * HOSKEY Keyboard - Dictionary Engine
 *
 * Manages main dictionary (trie) and personal dictionary.
 * Provides prefix-based suggestions with frequency ranking.
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#ifndef KEYBOARD_NATIVE_DICT_ENGINE_H
#define KEYBOARD_NATIVE_DICT_ENGINE_H

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <mutex>

namespace keyboard {
namespace core {

/**
 * Dictionary suggestion
 */
struct DictSuggestion {
    std::string word;
    float score;
    uint8_t frequency;
    bool isPersonal;
};

/**
 * Trie node for in-memory dictionary
 */
struct TrieNode {
    std::unordered_map<char32_t, std::unique_ptr<TrieNode>> children;
    bool isTerminal = false;
    uint8_t frequency = 0;
};

/**
 * Dictionary engine
 */
class DictEngine {
public:
    DictEngine();
    ~DictEngine();

    // ========================================================================
    // Loading
    // ========================================================================

    /**
     * Load main dictionary from file
     */
    bool loadMainDict(const std::string& path);

    /**
     * Load main dictionary from file descriptor
     */
    bool loadMainDictFd(int fd, size_t offset, size_t length);

    /**
     * Load personal dictionary from file
     */
    bool loadPersonalDict(const std::string& path);

    /**
     * Load personal dictionary from file descriptor
     */
    bool loadPersonalDictFd(int fd, size_t offset, size_t length);

    /**
     * Save personal dictionary to file
     */
    bool savePersonalDict();

    // ========================================================================
    // Queries
    // ========================================================================

    /**
     * Check if word exists in any dictionary
     */
    bool contains(const std::string& word) const;

    /**
     * Get word frequency (0 if not found)
     */
    uint8_t getFrequency(const std::string& word) const;

    /**
     * Get suggestions for prefix
     */
    std::vector<DictSuggestion> getSuggestions(const std::string& prefix,
                                                int maxResults = 10) const;

    // ========================================================================
    // Personal Dictionary
    // ========================================================================

    /**
     * Add word to personal dictionary
     */
    void addToPersonalDict(const std::string& word, int frequency = 1);

    /**
     * Remove word from personal dictionary
     */
    void removeFromPersonalDict(const std::string& word);

    /**
     * Boost word frequency in personal dictionary
     */
    void boostPersonalWord(const std::string& word, int boost = 1);

    // ========================================================================
    // Info
    // ========================================================================

    bool isLoaded() const { return mainDictLoaded_; }
    bool hasPersonalDict() const { return personalDictLoaded_; }
    size_t getWordCount() const { return mainWordCount_; }
    size_t getPersonalWordCount() const { return personalDict_.size(); }
    size_t getMemoryUsage() const;

private:
    // Main dictionary trie
    std::unique_ptr<TrieNode> mainRoot_;
    bool mainDictLoaded_ = false;
    size_t mainWordCount_ = 0;

    // Personal dictionary (simple map for now)
    std::unordered_map<std::string, int> personalDict_;
    std::string personalDictPath_;
    bool personalDictLoaded_ = false;
    bool personalDictDirty_ = false;

    mutable std::mutex mutex_;

    // Helpers
    void insertWord(TrieNode* root, const std::string& word, uint8_t frequency);
    const TrieNode* findNode(const TrieNode* root, const std::string& prefix) const;
    void collectSuggestions(const TrieNode* node, const std::string& prefix,
                            std::vector<DictSuggestion>& results,
                            int maxResults, bool isPersonal) const;

    static std::vector<char32_t> toCodePoints(const std::string& text);
    static std::string fromCodePoints(const std::vector<char32_t>& codePoints);
};

} // namespace core
} // namespace keyboard

#endif // KEYBOARD_NATIVE_DICT_ENGINE_H
