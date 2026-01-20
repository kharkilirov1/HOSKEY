/**
 * Trie Node for Patricia Trie implementation
 */

#ifndef HOSKEY_TRIE_NODE_H
#define HOSKEY_TRIE_NODE_H

#include <unordered_map>
#include <memory>
#include <string>

namespace hoskey {

/**
 * Node in Patricia Trie
 * Uses unordered_map for children (good balance of speed/memory for Cyrillic)
 */
class TrieNode {
public:
    TrieNode() : isEndOfWord_(false), frequency_(0), probability_(0) {}

    // Children access
    TrieNode* getChild(char c) const {
        auto it = children_.find(c);
        return it != children_.end() ? it->second.get() : nullptr;
    }

    TrieNode* getOrCreateChild(char c) {
        auto& child = children_[c];
        if (!child) {
            child = std::make_unique<TrieNode>();
        }
        return child.get();
    }

    bool hasChildren() const { return !children_.empty(); }

    const std::unordered_map<char, std::unique_ptr<TrieNode>>& getChildren() const {
        return children_;
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
        for (const auto& pair : children_) {
            usage += sizeof(char) + sizeof(std::unique_ptr<TrieNode>);
            if (pair.second) {
                usage += pair.second->getMemoryUsage();
            }
        }
        return usage;
    }

private:
    std::unordered_map<char, std::unique_ptr<TrieNode>> children_;
    bool isEndOfWord_;
    int frequency_;
    int probability_;
};

} // namespace hoskey

#endif // HOSKEY_TRIE_NODE_H
