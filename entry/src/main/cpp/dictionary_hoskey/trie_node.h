/**
 * Trie Node for Patricia Trie implementation
 * OPTIMIZED: Uses unordered_map instead of fixed array
 *
 * Memory comparison:
 * - Old: children_[256] = 2048 bytes per node
 * - New: unordered_map = ~50 bytes per node (average)
 *
 * For 400k nodes: 800MB -> 20MB
 */

#ifndef HOSKEY_TRIE_NODE_H
#define HOSKEY_TRIE_NODE_H

#include <unordered_map>
#include <cstdint>

namespace hoskey {

/**
 * Node in Patricia Trie
 * Uses unordered_map for memory efficiency
 * - ~40x less memory than fixed array
 * - O(1) average access (hash map)
 * - Faster allocation (no memset of 2KB)
 */
class TrieNode {
public:
    TrieNode() : isEndOfWord_(false), frequency_(0), probability_(0) {}

    ~TrieNode() {
        // Delete all children
        for (auto& pair : children_) {
            delete pair.second;
        }
        children_.clear();
    }

    // Prevent copying (we use raw pointers)
    TrieNode(const TrieNode&) = delete;
    TrieNode& operator=(const TrieNode&) = delete;

    // Children access - hash map lookup
    TrieNode* getChild(unsigned char c) const {
        auto it = children_.find(c);
        return (it != children_.end()) ? it->second : nullptr;
    }

    TrieNode* getOrCreateChild(unsigned char c) {
        auto it = children_.find(c);
        if (it != children_.end()) {
            return it->second;
        }

        // Create new child
        TrieNode* child = new (std::nothrow) TrieNode();
        if (child) {
            children_[c] = child;
        }
        return child;
    }

    bool hasChildren() const { return !children_.empty(); }

    int getChildCount() const { return static_cast<int>(children_.size()); }

    // Iterate over children (for prefix search)
    template<typename Func>
    void forEachChild(Func&& func) const {
        for (const auto& pair : children_) {
            func(static_cast<char>(pair.first), pair.second);
        }
    }

    // Word termination
    bool isEndOfWord() const { return isEndOfWord_; }
    void setEndOfWord(bool value) { isEndOfWord_ = value; }

    // Frequency/probability
    int getFrequency() const { return frequency_; }
    void setFrequency(int freq) { frequency_ = freq; }

    int getProbability() const { return probability_; }
    void setProbability(int prob) { probability_ = prob; }

    // Memory estimation
    size_t getMemoryUsage() const {
        // Base size: map overhead + fields
        size_t usage = sizeof(TrieNode) + children_.size() * (sizeof(uint8_t) + sizeof(TrieNode*) + 16);
        for (const auto& pair : children_) {
            if (pair.second) {
                usage += pair.second->getMemoryUsage();
            }
        }
        return usage;
    }

private:
    std::unordered_map<uint8_t, TrieNode*> children_;  // Sparse storage - only used chars
    bool isEndOfWord_;
    int frequency_;
    int probability_;
};

} // namespace hoskey

#endif // HOSKEY_TRIE_NODE_H
