/**
 * Trie Node for Patricia Trie implementation
 * Uses fixed array for children - stable and fast
 */

#ifndef HOSKEY_TRIE_NODE_H
#define HOSKEY_TRIE_NODE_H

#include <memory>
#include <cstring>

namespace hoskey {

/**
 * Node in Patricia Trie
 * Uses fixed array of 256 pointers for all possible byte values
 * - No hash collisions
 * - No rehashing
 * - Direct O(1) access
 */
class TrieNode {
public:
    TrieNode() : isEndOfWord_(false), frequency_(0), probability_(0), childCount_(0) {
        // Initialize all children to nullptr
        std::memset(children_, 0, sizeof(children_));
    }

    ~TrieNode() {
        // Delete all children
        for (int i = 0; i < 256; i++) {
            if (children_[i]) {
                delete children_[i];
                children_[i] = nullptr;
            }
        }
    }

    // Prevent copying (we use raw pointers)
    TrieNode(const TrieNode&) = delete;
    TrieNode& operator=(const TrieNode&) = delete;

    // Children access - direct array index
    TrieNode* getChild(unsigned char c) const {
        return children_[c];
    }

    TrieNode* getOrCreateChild(unsigned char c) {
        if (children_[c]) {
            return children_[c];
        }

        // Create new child
        children_[c] = new (std::nothrow) TrieNode();
        if (children_[c]) {
            childCount_++;
        }
        return children_[c];
    }

    bool hasChildren() const { return childCount_ > 0; }

    int getChildCount() const { return childCount_; }

    // Iterate over children (for prefix search)
    template<typename Func>
    void forEachChild(Func&& func) const {
        for (int i = 0; i < 256; i++) {
            if (children_[i]) {
                func(static_cast<char>(i), children_[i]);
            }
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
        size_t usage = sizeof(TrieNode);
        for (int i = 0; i < 256; i++) {
            if (children_[i]) {
                usage += children_[i]->getMemoryUsage();
            }
        }
        return usage;
    }

private:
    TrieNode* children_[256];  // Direct array - all possible byte values
    bool isEndOfWord_;
    int frequency_;
    int probability_;
    int childCount_;
};

} // namespace hoskey

#endif // HOSKEY_TRIE_NODE_H
