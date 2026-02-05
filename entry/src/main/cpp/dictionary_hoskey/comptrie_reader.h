/**
 * Yandex CompactTrie Reader
 * 
 * Reads Yandex keyboard dictionary format (main_ru) directly via mmap.
 * Based on reverse-engineering of libjni_ykeyboard3.so and Catboost comptrie.
 * 
 * Format:
 *   [Header 32B][JSON config][CompactTrie with VarInt frequencies][TFLite models]
 * 
 * Node format:
 *   [flags 1B][label 1B][left_offset 0-7B][right_offset 0-7B][value VarInt?]
 * 
 * Flags:
 *   0x80 (MT_FINAL) = node has value (word end)
 *   0x40 (MT_NEXT)  = has continuation
 *   bits 3-5       = left offset length (0-7)
 *   bits 0-2       = right offset length (0-7)
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace yandex {

// Yandex trie node markers (from reverse engineering)
constexpr uint8_t NODE_REGULAR = 0x40;    // '@' - Regular node
constexpr uint8_t NODE_PROPERTY = 0x48;   // 'H' - Node with property
constexpr uint8_t NODE_END = 0x50;        // 'P' - End-of-word marker
constexpr uint8_t NODE_TERMINAL = 0x58;   // 'X' - Terminal node with data

// Legacy CompactTrie constants (kept for reference)
constexpr uint8_t MT_FINAL = 0x80;
constexpr uint8_t MT_NEXT = 0x40;
constexpr uint8_t MT_SIZEMASK = 0x07;
constexpr size_t MT_LEFTSHIFT = 3;

// UTF-8 Cyrillic lead bytes
constexpr uint8_t UTF8_CYR_D0 = 0xD0;
constexpr uint8_t UTF8_CYR_D1 = 0xD1;

// Dictionary magic
constexpr uint32_t YANDEX_MAGIC = 0xFE3AC19B;

/**
 * Suggestion result
 */
struct CompTrieSuggestion {
    std::string word;
    uint64_t frequency;
    float score;  // normalized 0-1
};

/**
 * CompactTrie Reader - mmap-based, instant loading
 */
class CompTrieReader {
public:
    CompTrieReader();
    ~CompTrieReader();

    // Non-copyable
    CompTrieReader(const CompTrieReader&) = delete;
    CompTrieReader& operator=(const CompTrieReader&) = delete;

    /**
     * Load dictionary via mmap (instant, no parsing)
     * @param path Path to main_ru file
     * @return true if loaded successfully
     */
    bool load(const std::string& path);

    /**
     * Load dictionary via mmap from file descriptor (for HarmonyOS rawfile)
     * @param fd File descriptor
     * @param offset Offset in file where dictionary starts
     * @param length Length of dictionary data
     * @return true if loaded successfully
     */
    bool loadFromFd(int fd, size_t offset, size_t length);

    /**
     * Unload and release mmap
     */
    void unload();

    /**
     * Check if word exists in dictionary
     */
    bool contains(const std::string& word) const;

    /**
     * Get word frequency (0 if not found)
     */
    uint64_t getFrequency(const std::string& word) const;

    /**
     * Get suggestions for prefix
     * @param prefix Input prefix (UTF-8)
     * @param maxResults Maximum results to return
     * @return Vector of suggestions sorted by frequency
     */
    std::vector<CompTrieSuggestion> getSuggestions(const std::string& prefix, int maxResults = 10) const;

    /**
     * Iterate all words with given prefix
     * @param prefix Prefix to match
     * @param callback Called for each word (return false to stop)
     */
    void iteratePrefix(const std::string& prefix,
                       std::function<bool(const std::string& word, uint64_t freq)> callback) const;

    /**
     * Get memory usage
     */
    size_t getMemoryUsage() const;

    /**
     * Get word count (requires full scan, cached after first call)
     */
    size_t getWordCount() const;

    /**
     * Check if loaded
     */
    bool isLoaded() const { return data_ != nullptr; }

private:
    // Mmap data
    void* mapHandle_ = nullptr;  // Platform-specific handle
    const uint8_t* data_ = nullptr;
    size_t fileSize_ = 0;
    size_t mapLength_ = 0;  // Actual mmap length (may differ from fileSize_ due to alignment)

    // Trie boundaries
    size_t trieStart_ = 0;
    size_t trieEnd_ = 0;
    uint64_t maxFrequency_ = 1;

    // Cached stats
    mutable size_t wordCount_ = 0;
    mutable bool wordCountCached_ = false;

    // Internal navigation
    
    /**
     * Unpack variable-length offset
     */
    static size_t unpackOffset(const uint8_t* p, size_t len);

    /**
     * Unpack VarInt value (frequency)
     */
    static uint64_t unpackVarInt(const uint8_t* p, size_t& bytesRead);

    /**
     * Skip VarInt and return its length
     */
    static size_t skipVarInt(const uint8_t* p);

    /**
     * Navigate to child by label byte
     * @param datapos Current position (updated)
     * @param dataend End of trie data
     * @param label Byte to find
     * @return Flags of found node, or 0 if not found (datapos set to nullptr)
     */
    uint8_t leapByte(const uint8_t*& datapos, const uint8_t* dataend, uint8_t label) const;

    /**
     * Find node for given key
     * @param key Key bytes
     * @param keylen Key length
     * @param value Output: pointer to value if found
     * @return true if exact match found
     */
    bool findKey(const uint8_t* key, size_t keylen, const uint8_t** value) const;

    /**
     * Collect words from subtrie (DFS)
     */
    void collectWords(const uint8_t* pos, const uint8_t* end,
                      const std::string& prefix,
                      std::vector<CompTrieSuggestion>& results,
                      int maxResults, int depth) const;

    /**
     * Parse header and find trie boundaries
     */
    bool parseStructure();
};

} // namespace yandex
