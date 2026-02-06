/**
 * Yandex LOUDS Trie Reader
 *
 * Reads Yandex keyboard dictionary format (main_ru.dict) via mmap.
 * Based on reverse-engineering of libjni_ykeyboard3.so.
 *
 * Format: LOUDS (Level-Order Unary Degree Sequence)
 *
 * File structure:
 *   [Global Header][JSON config][Data sections][Trie '1nc7'][TFLite 'TFL3']
 *
 * Trie section (starts with '1nc7'):
 *   [Header 24B][LOUDS bitvector][Labels array][Payloads]
 *
 * Header (24 bytes):
 *   magic[4]       = "1nc7" (0x37636E31)
 *   version[4]     = 1
 *   node_count[8]  = N (number of nodes)
 *   louds_chunks[8]= M (LOUDS size in 16-byte chunks)
 *
 * Label byte format:
 *   Bit 7 (0x80): IS_TERMINAL (word ends here)
 *   Bit 6 (0x40): HAS_PAYLOAD (has weight data)
 *   Bits 0-5 (0x3F): CHAR_INDEX (0-63)
 *
 * Alphabet mapping:
 *   Index 32-63: Linear Russian 'а' + (index - 32)
 *   Index 0-31:  Frequency-sorted (partially known)
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>

namespace yandex {

// Dictionary magic signatures
constexpr uint32_t YANDEX_MAGIC = 0xFE3AC19B;      // Global file header
constexpr uint32_t TRIE_MAGIC = 0x37636E31;        // "1nc7" little-endian

// Label byte masks
constexpr uint8_t LABEL_TERMINAL = 0x80;   // Bit 7: word ends here
constexpr uint8_t LABEL_PAYLOAD = 0x40;    // Bit 6: has payload/weight
constexpr uint8_t LABEL_INDEX_MASK = 0x3F; // Bits 0-5: character index

/**
 * LOUDS Trie Header (24 bytes, packed)
 */
#pragma pack(push, 1)
struct LOUDSHeader {
    char magic[4];           // "1nc7"
    uint32_t version;        // Usually 1
    uint64_t node_count;     // N: number of nodes
    uint64_t louds_chunks;   // M: LOUDS size in 16-byte (128-bit) chunks
};
#pragma pack(pop)

/**
 * Suggestion result
 */
struct CompTrieSuggestion {
    std::string word;
    uint64_t frequency;
    float score;  // normalized 0-1
};

/**
 * LOUDS Trie Reader - mmap-based, instant loading
 */
class CompTrieReader {
public:
    CompTrieReader();
    ~CompTrieReader();

    // Non-copyable
    CompTrieReader(const CompTrieReader&) = delete;
    CompTrieReader& operator=(const CompTrieReader&) = delete;

    /**
     * Load dictionary via mmap
     * @param path Path to dictionary file
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
     * Get memory usage (minimal - only mmap overhead)
     */
    size_t getMemoryUsage() const;

    /**
     * Get word count (requires full scan, cached after first call)
     */
    size_t getWordCount() const;

    /**
     * Check if loaded
     */
    bool isLoaded() const { return data_ != nullptr && louds_ != nullptr; }

private:
    // Mmap data
    void* mapHandle_ = nullptr;
    const uint8_t* data_ = nullptr;
    size_t fileSize_ = 0;
    size_t mapLength_ = 0;

    // LOUDS structure pointers (within mmap)
    const LOUDSHeader* header_ = nullptr;
    const uint64_t* louds_ = nullptr;      // LOUDS bitvector
    const uint8_t* labels_ = nullptr;      // Labels array
    size_t loudsSize_ = 0;                 // Size in bits
    size_t nodeCount_ = 0;

    // Alphabet mapping table (index -> UTF-8 char)
    // Filled during initialization
    std::string alphabet_[64];

    // Cached stats
    mutable size_t wordCount_ = 0;
    mutable bool wordCountCached_ = false;

    // =========================================================================
    // LOUDS bit operations
    // =========================================================================

    /**
     * Get bit at position
     */
    inline int getBit(size_t bitIdx) const {
        size_t wordIdx = bitIdx / 64;
        size_t bitPos = bitIdx % 64;
        return (louds_[wordIdx] >> bitPos) & 1;
    }

    /**
     * Count ones in bits[0..bitIdx) - exclusive
     * rank1(i) = number of 1s before position i
     */
    size_t rank1(size_t bitIdx) const;

    /**
     * Count zeros in bits[0..bitIdx) - exclusive
     * rank0(i) = i - rank1(i)
     */
    inline size_t rank0(size_t bitIdx) const {
        return bitIdx - rank1(bitIdx);
    }

    /**
     * Find position of k-th zero (1-indexed)
     * select0(k) = position of k-th 0-bit
     */
    size_t select0(size_t k) const;

    /**
     * Find position of k-th one (1-indexed)
     * select1(k) = position of k-th 1-bit
     */
    size_t select1(size_t k) const;

    // =========================================================================
    // Tree navigation
    // =========================================================================

    /**
     * Get first child of node
     * FirstChild(i) = Select0(Rank1(i)) + 1
     * @return Child node index, or 0 if no children
     */
    size_t firstChild(size_t nodeIdx) const;

    /**
     * Get parent of node
     * Parent(i) = Select1(Rank0(i))
     * @return Parent node index
     */
    size_t parent(size_t nodeIdx) const;

    /**
     * Check if node has children
     */
    bool hasChildren(size_t nodeIdx) const;

    /**
     * Get all children of a node
     * @param nodeIdx Node index
     * @param children Output vector of (char, child_node_idx) pairs
     */
    void getChildren(size_t nodeIdx, std::vector<std::pair<std::string, size_t>>& children) const;

    // =========================================================================
    // Label/Alphabet handling
    // =========================================================================

    /**
     * Initialize alphabet mapping table
     */
    void initAlphabet();

    /**
     * Decode label byte to UTF-8 character
     * @param labelByte Raw label byte from labels_ array
     * @return UTF-8 character string
     */
    std::string decodeLabel(uint8_t labelByte) const;

    /**
     * Check if node is terminal (word ends here)
     */
    inline bool isTerminal(size_t nodeIdx) const {
        if (nodeIdx == 0 || nodeIdx > nodeCount_) return false;
        return (labels_[nodeIdx - 1] & LABEL_TERMINAL) != 0;
    }

    /**
     * Encode UTF-8 character to label index for search
     * @param utf8Char UTF-8 character (1-4 bytes)
     * @return Label index (0-63), or -1 if not found
     */
    int encodeChar(const std::string& utf8Char) const;

    // =========================================================================
    // Parsing
    // =========================================================================

    /**
     * Parse file structure and locate LOUDS trie
     */
    bool parseStructure();

    /**
     * Find node matching prefix, starting from root
     * @param prefix UTF-8 prefix string
     * @return Node index, or 0 if not found
     */
    size_t findPrefixNode(const std::string& prefix) const;

    /**
     * Collect words from subtree (DFS)
     */
    void collectWords(size_t nodeIdx, const std::string& prefix,
                      std::vector<CompTrieSuggestion>& results,
                      int maxResults, int depth) const;
};

} // namespace yandex
