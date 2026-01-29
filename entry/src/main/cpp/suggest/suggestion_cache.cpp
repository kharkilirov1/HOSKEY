/*
 * Copyright (c) 2024 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#include "suggestion_cache.h"
#include <chrono>

namespace latinime {

SuggestionCache::SuggestionCache() 
    : mCache(DEFAULT_CACHE_SIZE), mHitCount(0), mMissCount(0) {
}

SuggestionCache::SuggestionCache(size_t maxSize)
    : mCache(maxSize), mHitCount(0), mMissCount(0) {
}

SuggestionCache::~SuggestionCache() = default;

bool SuggestionCache::lookup(const int* inputCodePoints, int inputLength,
                            const int* prevWordCodePoints, int prevWordLength,
                            std::vector<Suggestion>& outSuggestions) {
    if (!inputCodePoints || inputLength <= 0) {
        return false;
    }
    
    std::string key = generateKey(inputCodePoints, inputLength,
                                  prevWordCodePoints, prevWordLength);
    
    CachedSuggestions cached;
    if (mCache.get(key, cached)) {
        outSuggestions = cached.suggestions;
        ++mHitCount;
        return true;
    }
    
    ++mMissCount;
    return false;
}

void SuggestionCache::store(const int* inputCodePoints, int inputLength,
                           const int* prevWordCodePoints, int prevWordLength,
                           const std::vector<Suggestion>& suggestions) {
    if (!inputCodePoints || inputLength <= 0 || suggestions.empty()) {
        return;
    }
    
    std::string key = generateKey(inputCodePoints, inputLength,
                                  prevWordCodePoints, prevWordLength);
    
    CachedSuggestions entry(suggestions);
    entry.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    entry.hitCount = 0;
    
    mCache.put(key, entry);
}

void SuggestionCache::clear() {
    mCache.clear();
}

void SuggestionCache::invalidateWord(const int* codePoints, int length) {
    // For full invalidation, we'd need to iterate through cache
    // For now, just clear the whole cache as a simple solution
    // A more sophisticated implementation would track word -> key mappings
    clear();
}

float SuggestionCache::getHitRate() const {
    int64_t total = mHitCount + mMissCount;
    if (total == 0) {
        return 0.0f;
    }
    return static_cast<float>(mHitCount) / static_cast<float>(total);
}

void SuggestionCache::resetStats() {
    mHitCount = 0;
    mMissCount = 0;
}

std::string SuggestionCache::generateKey(const int* inputCodePoints, int inputLength,
                                         const int* prevWordCodePoints, int prevWordLength) {
    std::string key;
    key.reserve(inputLength * 4 + prevWordLength * 4 + 2);
    
    // Append previous word if present
    if (prevWordCodePoints && prevWordLength > 0) {
        for (int i = 0; i < prevWordLength; ++i) {
            // Encode as UTF-8 like characters (simplified)
            int cp = prevWordCodePoints[i];
            if (cp < 0x80) {
                key.push_back(static_cast<char>(cp));
            } else if (cp < 0x800) {
                key.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                key.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else if (cp < 0x10000) {
                key.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                key.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                key.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else {
                key.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                key.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                key.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                key.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }
        key.push_back('|');  // Separator
    }
    
    // Append current input
    for (int i = 0; i < inputLength; ++i) {
        int cp = inputCodePoints[i];
        if (cp < 0x80) {
            key.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            key.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            key.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            key.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            key.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            key.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            key.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            key.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            key.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            key.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    
    return key;
}

} // namespace latinime
