/*
 * Copyright (c) 2024 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 * 
 * LRU Cache for suggestions (inspired by Yandex Keyboard caching)
 */

#ifndef HOSKEY_SUGGESTION_CACHE_H
#define HOSKEY_SUGGESTION_CACHE_H

#include <string>
#include <vector>
#include <list>
#include <unordered_map>
#include <mutex>

#include "multi_predictor.h"

namespace latinime {

/**
 * LRU (Least Recently Used) Cache for suggestions
 * 
 * Thread-safe cache that stores recent suggestion results
 * to avoid recomputing for repeated queries.
 */
template<typename Key, typename Value>
class LRUCache {
public:
    explicit LRUCache(size_t maxSize) : mMaxSize(maxSize) {}
    
    /**
     * Look up a value in the cache
     * @param key The key to look up
     * @param outValue Output parameter for the value
     * @return true if found, false if not in cache
     */
    bool get(const Key& key, Value& outValue) {
        std::lock_guard<std::mutex> lock(mMutex);
        
        auto it = mMap.find(key);
        if (it == mMap.end()) {
            return false;
        }
        
        // Move to front (most recently used)
        mList.splice(mList.begin(), mList, it->second);
        outValue = it->second->second;
        return true;
    }
    
    /**
     * Store a value in the cache
     * @param key The key
     * @param value The value to store
     */
    void put(const Key& key, const Value& value) {
        std::lock_guard<std::mutex> lock(mMutex);
        
        auto it = mMap.find(key);
        if (it != mMap.end()) {
            // Update existing entry and move to front
            it->second->second = value;
            mList.splice(mList.begin(), mList, it->second);
            return;
        }
        
        // Add new entry
        mList.emplace_front(key, value);
        mMap[key] = mList.begin();
        
        // Evict if over capacity
        while (mMap.size() > mMaxSize) {
            auto last = mList.end();
            --last;
            mMap.erase(last->first);
            mList.pop_back();
        }
    }
    
    /**
     * Check if key exists in cache
     */
    bool contains(const Key& key) const {
        std::lock_guard<std::mutex> lock(mMutex);
        return mMap.find(key) != mMap.end();
    }
    
    /**
     * Remove a specific key from cache
     */
    void remove(const Key& key) {
        std::lock_guard<std::mutex> lock(mMutex);
        
        auto it = mMap.find(key);
        if (it != mMap.end()) {
            mList.erase(it->second);
            mMap.erase(it);
        }
    }
    
    /**
     * Clear all entries
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mMutex);
        mList.clear();
        mMap.clear();
    }
    
    /**
     * Get current cache size
     */
    size_t size() const {
        std::lock_guard<std::mutex> lock(mMutex);
        return mMap.size();
    }
    
    /**
     * Get maximum cache size
     */
    size_t maxSize() const { return mMaxSize; }
    
    /**
     * Resize cache (evicts if necessary)
     */
    void setMaxSize(size_t newMaxSize) {
        std::lock_guard<std::mutex> lock(mMutex);
        mMaxSize = newMaxSize;
        
        while (mMap.size() > mMaxSize) {
            auto last = mList.end();
            --last;
            mMap.erase(last->first);
            mList.pop_back();
        }
    }
    
private:
    size_t mMaxSize;
    std::list<std::pair<Key, Value>> mList;
    std::unordered_map<Key, typename std::list<std::pair<Key, Value>>::iterator> mMap;
    mutable std::mutex mMutex;
};

/**
 * Cached suggestions entry
 */
struct CachedSuggestions {
    std::vector<Suggestion> suggestions;
    int64_t timestamp;  // When cached (for TTL if needed)
    int hitCount;       // Access count for statistics
    
    CachedSuggestions() : timestamp(0), hitCount(0) {}
    explicit CachedSuggestions(std::vector<Suggestion> s) 
        : suggestions(std::move(s)), timestamp(0), hitCount(0) {}
};

/**
 * Suggestion Cache for keyboard predictions
 * 
 * Caches prediction results to improve responsiveness.
 * Key is generated from input context (word + previous word).
 */
class SuggestionCache {
public:
    static constexpr size_t DEFAULT_CACHE_SIZE = 128;
    static constexpr int64_t DEFAULT_TTL_MS = 60000;  // 1 minute
    
    SuggestionCache();
    explicit SuggestionCache(size_t maxSize);
    ~SuggestionCache();
    
    /**
     * Look up suggestions in cache
     * @param inputCodePoints Current input as code points
     * @param inputLength Length of input
     * @param prevWordCodePoints Previous word (can be null)
     * @param prevWordLength Previous word length
     * @param outSuggestions Output suggestions
     * @return true if found in cache
     */
    bool lookup(const int* inputCodePoints, int inputLength,
               const int* prevWordCodePoints, int prevWordLength,
               std::vector<Suggestion>& outSuggestions);
    
    /**
     * Store suggestions in cache
     */
    void store(const int* inputCodePoints, int inputLength,
              const int* prevWordCodePoints, int prevWordLength,
              const std::vector<Suggestion>& suggestions);
    
    /**
     * Clear all cached suggestions
     */
    void clear();
    
    /**
     * Invalidate cache entries containing a specific word
     * (useful when user dictionary changes)
     */
    void invalidateWord(const int* codePoints, int length);
    
    /**
     * Get cache statistics
     */
    size_t getCacheSize() const { return mCache.size(); }
    int64_t getHitCount() const { return mHitCount; }
    int64_t getMissCount() const { return mMissCount; }
    float getHitRate() const;
    
    /**
     * Reset statistics
     */
    void resetStats();
    
private:
    LRUCache<std::string, CachedSuggestions> mCache;
    
    // Statistics
    int64_t mHitCount;
    int64_t mMissCount;
    
    /**
     * Generate cache key from input context
     */
    static std::string generateKey(const int* inputCodePoints, int inputLength,
                                   const int* prevWordCodePoints, int prevWordLength);
};

} // namespace latinime

#endif // HOSKEY_SUGGESTION_CACHE_H
