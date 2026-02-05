/**
 * HOSKEY Keyboard - N-gram Engine
 *
 * Simple N-gram language model for fallback predictions.
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#ifndef KEYBOARD_NATIVE_NGRAM_ENGINE_H
#define KEYBOARD_NATIVE_NGRAM_ENGINE_H

#include <string>
#include <vector>
#include <unordered_map>

namespace keyboard {
namespace core {

/**
 * Simple N-gram language model
 * Used as fallback when neural is unavailable
 */
class NgramEngine {
public:
    NgramEngine() = default;
    ~NgramEngine() = default;

    /**
     * Load n-gram model from file
     */
    bool load(const std::string& /*path*/) {
        // Stub - return true
        return true;
    }

    /**
     * Get predictions based on previous words
     */
    std::vector<std::string> predict(const std::string& prevWord,
                                      const std::string& prefix,
                                      int maxResults) {
        (void)prevWord;
        (void)prefix;
        (void)maxResults;
        // Stub - return empty
        return {};
    }

    /**
     * Learn from word sequence
     */
    void learn(const std::string& /*prevWord*/, const std::string& /*word*/) {
        // Stub
    }

    /**
     * Check if loaded
     */
    bool isLoaded() const { return true; }

private:
    std::unordered_map<std::string, std::vector<std::pair<std::string, float>>> bigrams_;
};

} // namespace core
} // namespace keyboard

#endif // KEYBOARD_NATIVE_NGRAM_ENGINE_H
