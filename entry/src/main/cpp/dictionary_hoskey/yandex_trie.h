/**
 * YandexTrie - Parser for Yandex Keyboard dictionary (main_ru)
 * 
 * Format: Yandex CompactTrie with VarInt frequencies
 * Based on reverse-engineering of libjni_ykeyboard3.so and Catboost comptrie
 * 
 * V2 (2026-02-04): Uses mmap-based CompTrieReader for instant loading
 */

#ifndef YANDEX_TRIE_H
#define YANDEX_TRIE_H

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "comptrie_reader.h"

namespace yandex {

// File format constants (YANDEX_MAGIC defined in comptrie_reader.h)
constexpr size_t HEADER_SIZE = 32;
constexpr size_t JSON_START = 32;
// Dictionary structure (from Y1 parser analysis):
//   blacklist: offset=0x2720 (10016), size=374455
//   trie:      offset=0x5DE30 (384496), size=4498484
constexpr size_t BLACKLIST_START = 0x2720;   // 10016 - where blacklist begins
constexpr size_t TRIE_START = 0x5DE30;       // 384496 - where main trie begins

// Inline marker bytes (from Ghidra analysis)
constexpr uint8_t MARKER_NODE = 0x40;      // @ - regular node
constexpr uint8_t MARKER_NODE_H = 0x48;    // H - node with property
constexpr uint8_t MARKER_END = 0x50;       // P - end of word
constexpr uint8_t MARKER_TERMINAL = 0x58;  // X - terminal node

// UTF-8 Cyrillic prefixes (defined in comptrie_reader.h)
// constexpr uint8_t UTF8_CYR_D0 = 0xD0;  // а-п, А-Я
// constexpr uint8_t UTF8_CYR_D1 = 0xD1;  // р-я

/**
 * Trie node for prefix tree (built from extracted words)
 */
struct TrieNode {
    std::unordered_map<char32_t, std::unique_ptr<TrieNode>> children;
    bool isTerminal = false;
    uint8_t frequency = 0;
    uint32_t wordId = 0;
};

/**
 * Word entry with metadata
 */
struct WordEntry {
    std::string word;
    uint8_t frequency;
    uint32_t wordId;
};

/**
 * Suggestion result
 */
struct Suggestion {
    std::string word;
    float score;
    
    bool operator<(const Suggestion& other) const {
        return score > other.score;  // Higher score first
    }
};

/**
 * Main Yandex Dictionary class
 */
class YandexDict {
public:
    YandexDict();
    ~YandexDict();
    
    // Load dictionary from binary file (main_ru ynnlm format)
    bool load(const std::string& path);

    // Load dictionary from file descriptor (for HarmonyOS rawfile)
    bool loadFromFd(int fd, size_t offset, size_t length);

    // Load dictionary from text file (word\tfreq format)
    bool loadFromTextFile(const std::string& path);

    // Load dictionary from text file via file descriptor (for HarmonyOS rawfile)
    bool loadFromTextFd(int fd, size_t offset, size_t length);
    
    // Check if word exists
    bool contains(const std::string& word) const;
    
    // Get word frequency (0-255, 0 if not found)
    uint8_t getFrequency(const std::string& word) const;
    
    // Get word ID for neural model input
    uint32_t getWordId(const std::string& word) const;
    
    // Get suggestions for prefix
    std::vector<Suggestion> getSuggestions(const std::string& prefix, int maxCount = 10) const;
    
    // Get all words (for debugging)
    const std::vector<WordEntry>& getWords() const { return words_; }
    
    // Stats
    size_t getWordCount() const;
    size_t getMemoryUsage() const;
    bool isLoaded() const { return loaded_; }
    
private:
    // Parse file structure
    bool parseHeader(const uint8_t* data, size_t size);
    bool parseJson(const uint8_t* data, size_t size);
    bool extractWords(const uint8_t* data, size_t trieStart, size_t trieEnd);
    
    // Build prefix tree from extracted words
    void buildPrefixTree();
    
    // Helper: decode UTF-8 character
    static bool decodeUtf8Char(const uint8_t* data, size_t pos, size_t maxPos,
                               char32_t& outChar, size_t& outLen);
    
    // Helper: encode UTF-8
    static std::string encodeUtf8(char32_t ch);
    
    // Helper: collect words from trie node
    void collectWords(const TrieNode* node, const std::string& prefix,
                      std::vector<Suggestion>& results, int maxCount) const;
    
    // Find node for prefix
    const TrieNode* findNode(const std::string& prefix) const;
    
private:
    // V2: mmap-based CompTrieReader (fast path)
    std::unique_ptr<CompTrieReader> compTrie_;
    bool useCompTrie_ = false;
    
    // V1 fallback: in-memory trie (for text files)
    std::unique_ptr<uint8_t[]> fileData_;
    size_t fileSize_ = 0;
    
    std::string jsonConfig_;
    std::vector<WordEntry> words_;
    std::unordered_map<std::string, size_t> wordIndex_;  // word -> index in words_
    
    std::unique_ptr<TrieNode> root_;
    bool loaded_ = false;
};

} // namespace yandex

#endif // YANDEX_TRIE_H
