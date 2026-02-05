/**
 * Patricia Trie for efficient dictionary storage
 * Based on OpenBoard's ver4_patricia_trie implementation
 *
 * Features:
 * - O(log n) prefix search
 * - Memory-efficient storage
 * - Binary dictionary format support
 */

#ifndef HOSKEY_TRIE_H
#define HOSKEY_TRIE_H

#include <string>
#include <vector>
#include <memory>

namespace hoskey {

// Forward declaration
class TrieNode;

/**
 * Word entry with frequency information
 */
struct WordEntry {
    std::string word;
    int frequency;      // Usage frequency (0-255 typically)
    int probability;    // Log probability for ranking

    WordEntry() : frequency(0), probability(0) {}
    WordEntry(const std::string& w, int f, int p = 0)
        : word(w), frequency(f), probability(p) {}
};

/**
 * Patricia Trie implementation
 * Optimized for dictionary lookup and prefix search
 */
class Trie {
public:
    Trie();
    ~Trie();

    // Dictionary loading
    bool loadFromFile(const std::string& path);
    bool loadFromTextFile(const std::string& path);  // Fallback for .txt
    bool loadFromBinaryFile(const std::string& path); // .dict format

    // Dictionary saving (for user dictionary)
    bool saveToFile(const std::string& path) const;

    // Basic operations
    bool contains(const std::string& word) const;
    int getFrequency(const std::string& word) const;
    void insert(const std::string& word, int frequency);

    // Prefix search (core functionality)
    std::vector<WordEntry> findByPrefix(const std::string& prefix, int limit = 10) const;

    // Get all words starting with first letter
    std::vector<std::string> getWordsByFirstLetter(char letter, int limit = 1000) const;

    // Statistics
    int getWordCount() const { return wordCount_; }
    size_t getMemoryUsage() const;

    // Memory management
    void clear();

private:
    std::unique_ptr<TrieNode> root_;
    int wordCount_;

    // Internal helpers
    void collectWords(const TrieNode* node, const std::string& prefix,
                     std::vector<WordEntry>& results, int limit) const;
};

} // namespace hoskey

#endif // HOSKEY_TRIE_H
