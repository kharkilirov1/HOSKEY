/**
 * Flat Trie - Pre-serialized dictionary format for instant loading
 *
 * Key benefits:
 * - Load time: <50ms (vs 400-500ms for pooled trie)
 * - Zero allocations at load time (just mmap or read)
 * - Memory-efficient: nodes stored contiguously
 *
 * File format (.flat):
 * +------------------+
 * | Header (64 bytes)|
 * +------------------+
 * | Nodes array      |
 * | (fixed size)     |
 * +------------------+
 * | Children refs    |
 * | (variable)       |
 * +------------------+
 * | String data      |
 * | (optional)       |
 * +------------------+
 *
 * Usage:
 *   // Build time (once):
 *   FlatTrieBuilder::convert("main_ru.dict", "main_ru.flat");
 *
 *   // Runtime (every load):
 *   FlatTrie trie;
 *   trie.load("main_ru.flat");  // ~50ms
 *   auto results = trie.findByPrefix("пр", 10);
 */

#ifndef HOSKEY_FLAT_TRIE_H
#define HOSKEY_FLAT_TRIE_H

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace hoskey {

// Forward declaration
struct WordEntry;

// Magic number for .flat files: "HKFT" (HosKey Flat Trie)
constexpr uint32_t FLAT_TRIE_MAGIC = 0x54464B48;
constexpr uint16_t FLAT_TRIE_VERSION = 1;

/**
 * Flat Trie file header (64 bytes, fixed)
 */
struct FlatTrieHeader {
    uint32_t magic;          // 4 bytes - FLAT_TRIE_MAGIC
    uint16_t version;        // 2 bytes - File format version
    uint16_t flags;          // 2 bytes - Reserved flags (total: 8)
    uint32_t nodeCount;      // 4 bytes - Total number of nodes (total: 12)
    uint32_t wordCount;      // 4 bytes - Total number of words (total: 16)
    uint32_t nodesOffset;    // 4 bytes - Offset to nodes array (total: 20)
    uint32_t childrenOffset; // 4 bytes - Offset to children data (total: 24)
    uint32_t stringsOffset;  // 4 bytes - Offset to string data (0 if none) (total: 28)
    uint32_t fileSize;       // 4 bytes - Total file size (total: 32)
    char locale[8];          // 8 bytes - Language code (e.g., "ru", "en") (total: 40)
    uint8_t reserved[24];    // 24 bytes - Reserved for future use (total: 64)
};

static_assert(sizeof(FlatTrieHeader) == 64, "Header must be 64 bytes");

/**
 * Flat Trie node (16 bytes, fixed)
 * Compact representation for cache-friendly access
 */
struct FlatTrieNode {
    uint32_t childrenStart;  // Index in children array (0 = no children)
    uint16_t childCount;     // Number of children
    uint16_t frequency;      // Word frequency (0-65535)
    uint8_t  flags;          // Bit 0: isEndOfWord, Bit 1-7: reserved
    uint8_t  reserved[7];    // Padding to 16 bytes

    bool isEndOfWord() const { return (flags & 0x01) != 0; }
    void setEndOfWord(bool v) { flags = v ? (flags | 0x01) : (flags & ~0x01); }
};

static_assert(sizeof(FlatTrieNode) == 16, "Node must be 16 bytes");

/**
 * Child reference (4 bytes)
 * Maps UTF-8 byte to node index
 */
struct FlatChildRef {
    uint8_t  byte;           // UTF-8 byte value
    uint8_t  reserved;       // Padding
    uint16_t reserved2;      // More padding
    uint32_t nodeIndex;      // Index of child node

    // Total: 8 bytes - allows binary search
};

static_assert(sizeof(FlatChildRef) == 8, "ChildRef must be 8 bytes");

/**
 * Flat Trie - Read-only pre-serialized dictionary
 *
 * Provides same interface as Trie for easy integration
 */
class FlatTrie {
public:
    FlatTrie();
    ~FlatTrie();

    // Prevent copying (holds memory-mapped data)
    FlatTrie(const FlatTrie&) = delete;
    FlatTrie& operator=(const FlatTrie&) = delete;

    /**
     * Load from .flat file
     * @param path Path to .flat file
     * @return true if loaded successfully
     */
    bool load(const std::string& path);

    /**
     * Unload and free resources
     */
    void unload();

    /**
     * Check if loaded
     */
    bool isLoaded() const { return data_ != nullptr; }

    // Same interface as Trie

    bool contains(const std::string& word) const;
    int getFrequency(const std::string& word) const;

    std::vector<WordEntry> findByPrefix(const std::string& prefix, int limit = 10) const;
    std::vector<std::string> getWordsByFirstLetter(char letter, int limit = 1000) const;

    int getWordCount() const { return header_ ? header_->wordCount : 0; }
    size_t getMemoryUsage() const { return dataSize_; }

    const std::string& getLocale() const { return locale_; }

private:
    // Memory-mapped or loaded data
    uint8_t* data_;
    size_t dataSize_;
    bool isMapped_;  // true if mmap, false if allocated

    // Parsed pointers (point into data_)
    const FlatTrieHeader* header_;
    const FlatTrieNode* nodes_;
    const FlatChildRef* children_;

    std::string locale_;

    // Helper methods
    const FlatTrieNode* getNode(uint32_t index) const;
    const FlatTrieNode* findChild(const FlatTrieNode* node, uint8_t byte) const;

    void collectWords(const FlatTrieNode* node, const std::string& prefix,
                     std::vector<WordEntry>& results, int limit) const;
};

/**
 * Builder for Flat Trie format
 *
 * Converts OpenBoard .dict files to optimized .flat format
 */
class FlatTrieBuilder {
public:
    /**
     * Convert .dict file to .flat format
     * @param inputPath Path to input .dict file
     * @param outputPath Path to output .flat file
     * @param locale Language code (e.g., "ru", "en")
     * @return true if conversion successful
     */
    static bool convert(const std::string& inputPath,
                       const std::string& outputPath,
                       const std::string& locale = "");

    /**
     * Build from existing Trie/TriePooled
     * @tparam TrieType Trie or TriePooled
     * @param trie Source trie
     * @param outputPath Path to output .flat file
     * @param locale Language code
     */
    template<typename TrieType>
    static bool buildFromTrie(const TrieType& trie,
                             const std::string& outputPath,
                             const std::string& locale = "");
};

} // namespace hoskey

#endif // HOSKEY_FLAT_TRIE_H
