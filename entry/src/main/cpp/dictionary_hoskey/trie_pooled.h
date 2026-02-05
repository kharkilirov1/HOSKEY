/**
 * Optimized Patricia Trie with Memory Pool
 *
 * This is a drop-in replacement for the original Trie class
 * with significant performance improvements:
 *
 * 1. Memory Pool allocation (~10x faster loading)
 * 2. Inline children storage (~2x faster insertion)
 * 3. No debug logging overhead in release builds
 *
 * Expected improvement: 5000ms -> 400-500ms for 220k words
 */

#ifndef HOSKEY_TRIE_POOLED_H
#define HOSKEY_TRIE_POOLED_H

#include <string>
#include <vector>
#include <memory>
#include "trie_node_pooled.h"
#include "trie.h"  // For WordEntry compatibility

namespace hoskey {

// Uses WordEntry from trie.h for compatibility with SuggestEngine

/**
 * Optimized Patricia Trie with Memory Pool
 *
 * Same interface as original Trie class for easy replacement.
 */
class TriePooled {
public:
    /**
     * Create trie with pre-allocated node pool
     * @param expectedWords Expected number of words (for pool sizing)
     *        Default 400000 to handle large English dictionaries (~1.5M nodes)
     */
    explicit TriePooled(size_t expectedWords = 400000);
    ~TriePooled();

    // Prevent copying (pool has raw pointers)
    TriePooled(const TriePooled&) = delete;
    TriePooled& operator=(const TriePooled&) = delete;

    // Dictionary loading
    bool loadFromFile(const std::string& path);
    bool loadFromTextFile(const std::string& path);
    bool loadFromBinaryFile(const std::string& path);
    
    // Memory-mapped loading (instant, no file read into RAM)
    bool loadFromFd(int fd, size_t offset, size_t length);

    // Basic operations
    bool contains(const std::string& word) const;
    int getFrequency(const std::string& word) const;
    void insert(const std::string& word, int frequency);

    // Prefix search
    std::vector<WordEntry> findByPrefix(const std::string& prefix, int limit = 10) const;

    // Get words by first letter
    std::vector<std::string> getWordsByFirstLetter(char letter, int limit = 1000) const;

    // Statistics
    int getWordCount() const { return wordCount_; }
    size_t getMemoryUsage() const;

    // Memory management
    void clear();

    // Access to pool (for advanced usage)
    TrieNodePool& getPool() { return pool_; }
    const TrieNodePool& getPool() const { return pool_; }

private:
    TrieNodePool pool_;
    uint32_t rootIndex_;  // Index of root node in pool
    int wordCount_;

    // Internal helpers
    void collectWords(uint32_t nodeIndex, const std::string& prefix,
                     std::vector<WordEntry>& results, int limit) const;

    // Get node by index (convenience)
    TrieNodePooled& node(uint32_t index) { return pool_.node(index); }
    const TrieNodePooled& node(uint32_t index) const { return pool_.node(index); }
};

} // namespace hoskey

#endif // HOSKEY_TRIE_POOLED_H