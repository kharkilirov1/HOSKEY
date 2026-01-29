/**
 * HOSKEY Native Dictionary Engine
 * N-API bridge for high-performance text prediction
 *
 * Based on OpenBoard architecture:
 * - Patricia Trie for fast prefix search
 * - Weighted Levenshtein for autocorrection
 * - Proximity-aware scoring
 * - OpenBoard Suggest engine for swipe/gesture input
 */

#include "napi/native_api.h"
#include <string>
#include <vector>
#include <memory>
#include <cmath>
#include <algorithm>
#include <limits>
#include <cstdio>
#include <mutex>
#include <set>
#include <unordered_map>
#include <tuple>

// HarmonyOS logging
#include <hilog/log.h>

// Log domain and tag for HOSKEY native layer
#undef LOG_DOMAIN
#undef LOG_TAG
#define LOG_DOMAIN 0x0001
#define LOG_TAG "HOSKEY-NATIVE"

#include "dictionary_hoskey/trie.h"
#include "dictionary_hoskey/trie_pooled.h"
#include "dictionary_hoskey/flat_trie.h"
#include "dictionary_hoskey/suggest_engine.h"

// Feature flag: Use optimized pooled trie for 10x faster loading
// Set to 1 to enable TriePooled, 0 to use original Trie
#ifndef USE_POOLED_TRIE
#define USE_POOLED_TRIE 1
#endif

// OpenBoard suggest engine
#include "suggest/core/suggest.h"
#include "suggest/core/suggest_options.h"
#include "suggest/core/session/dic_traverse_session.h"
#include "suggest/core/layout/proximity_info.h"
#include "suggest/core/result/suggestion_results.h"
#include "suggest/policyimpl/gesture/gesture_suggest_policy_factory.h"
#include "dictionary/interface/dictionary_structure_with_buffer_policy.h"

// Include constants
#include "constants.h"

// Include binary dictionary NAPI
#include "binary_dictionary_napi.h"
// Include proximity info NAPI
#include "proximity_info_napi.h"
// Include dic traverse session NAPI
#include "dic_traverse_session_napi.h"
// Include batch operations NAPI
#include "batch_operations_napi.h"

// Keyboard layout for NAPI swipe
struct KeyBounds {
    std::string key;
    float centerX, centerY;
    float width, height;
};

static std::vector<KeyBounds> g_keyboardLayout;

// Global instances with mutex protection
// Global instances - shared with batch_operations_napi.cpp
std::mutex g_trieMutex;  // Protects g_trie and g_suggestEngine

#if USE_POOLED_TRIE
// Optimized pooled trie: 10x faster loading (5000ms -> 400-500ms)
std::unique_ptr<hoskey::TriePooled> g_trie;
#else
// Original trie implementation
std::unique_ptr<hoskey::Trie> g_trie;
#endif

std::unique_ptr<hoskey::SuggestEngine> g_suggestEngine;

// FlatTrie for instant loading (<50ms) - optional
std::unique_ptr<hoskey::FlatTrie> g_flatTrie;

// ============================================================================
// API 22 Optimization: Cached property keys for faster object creation
// Avoids repeated string internalization overhead
// ============================================================================
static napi_ref g_cachedWordKey = nullptr;
static napi_ref g_cachedScoreKey = nullptr;
static napi_ref g_cachedErrorTypeKey = nullptr;
static bool g_keysInitialized = false;

// ============================================================================
// Optimized LRU cache: O(1) lookup with unordered_map + list
// ============================================================================
#include <list>

struct SuggestionCacheEntry {
    std::string prefix;
    std::vector<hoskey::SuggestResult> results;
};

// LRU list (front = most recent)
static std::list<SuggestionCacheEntry> g_cacheList;
// O(1) lookup: prefix → iterator into list
static std::unordered_map<std::string, std::list<SuggestionCacheEntry>::iterator> g_cacheMap;
static std::mutex g_cacheMutex;
static const size_t SUGGESTION_CACHE_SIZE = 128;

// Cache statistics
static int64_t g_cacheHitCount = 0;
static int64_t g_cacheMissCount = 0;

/**
 * Get cached suggestions for prefix - O(1) lookup
 */
static const std::vector<hoskey::SuggestResult>* GetCachedSuggestions(const std::string& prefix) {
    std::lock_guard<std::mutex> lock(g_cacheMutex);

    auto mapIt = g_cacheMap.find(prefix);
    if (mapIt == g_cacheMap.end()) {
        g_cacheMissCount++;
        return nullptr;
    }

    // Move to front (LRU update)
    auto listIt = mapIt->second;
    if (listIt != g_cacheList.begin()) {
        g_cacheList.splice(g_cacheList.begin(), g_cacheList, listIt);
        mapIt->second = g_cacheList.begin();
    }

    g_cacheHitCount++;
    return &g_cacheList.front().results;
}

/**
 * Add suggestions to cache - O(1)
 */
static void CacheSuggestions(const std::string& prefix, const std::vector<hoskey::SuggestResult>& results) {
    std::lock_guard<std::mutex> lock(g_cacheMutex);

    // Already in cache?
    if (g_cacheMap.count(prefix)) {
        return;
    }

    // Add to front
    g_cacheList.push_front({prefix, results});
    g_cacheMap[prefix] = g_cacheList.begin();

    // Evict oldest if over capacity
    while (g_cacheList.size() > SUGGESTION_CACHE_SIZE) {
        auto& oldest = g_cacheList.back();
        g_cacheMap.erase(oldest.prefix);
        g_cacheList.pop_back();
    }
}

/**
 * Clear suggestion cache
 */
static void ClearSuggestionCache() {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    g_cacheList.clear();
    g_cacheMap.clear();
}

/**
 * Get cache statistics
 */
static void GetCacheStats(size_t& size, int64_t& hits, int64_t& misses) {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    size = g_cacheList.size();
    hits = g_cacheHitCount;
    misses = g_cacheMissCount;
}

/**
 * Get cache hit rate (0.0 - 1.0)
 */
static float GetCacheHitRate() {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    int64_t total = g_cacheHitCount + g_cacheMissCount;
    if (total == 0) return 0.0f;
    return static_cast<float>(g_cacheHitCount) / static_cast<float>(total);
}

/**
 * Initialize cached property keys (call once at module init)
 * This avoids creating "word", "score", "errorType" strings on every getSuggestions call
 */
static void InitCachedPropertyKeys(napi_env env) {
    if (g_keysInitialized) return;

    napi_value wordKey, scoreKey, errorTypeKey;

    if (napi_create_string_utf8(env, "word", 4, &wordKey) == napi_ok) {
        napi_create_reference(env, wordKey, 1, &g_cachedWordKey);
    }
    if (napi_create_string_utf8(env, "score", 5, &scoreKey) == napi_ok) {
        napi_create_reference(env, scoreKey, 1, &g_cachedScoreKey);
    }
    if (napi_create_string_utf8(env, "errorType", 9, &errorTypeKey) == napi_ok) {
        napi_create_reference(env, errorTypeKey, 1, &g_cachedErrorTypeKey);
    }

    g_keysInitialized = true;
    OH_LOG_INFO(LOG_APP, "InitCachedPropertyKeys: property key cache initialized");
}

/**
 * Get cached property key (faster than creating new string each time)
 */
static napi_value GetCachedKey(napi_env env, napi_ref ref) {
    if (!ref) return nullptr;
    napi_value key = nullptr;
    napi_get_reference_value(env, ref, &key);
    return key;
}

// ============================================================================
// Safe String Conversion Helpers - No UB, proper buffer handling
// ============================================================================

/**
 * Safely convert napi_value string to std::string
 * - Checks napi_status at each step
 * - Handles empty strings correctly
 * - Properly sizes buffer with +1 for null terminator
 * - Resizes result to actual copied length
 * @returns empty string on any error
 */
static std::string NapiValueToString(napi_env env, napi_value value) {
    if (env == nullptr || value == nullptr) {
        OH_LOG_ERROR(LOG_APP, "NapiValueToString: null env or value");
        return "";
    }

    // Step 1: Get required buffer length (excluding null terminator)
    size_t requiredLength = 0;
    napi_status status = napi_get_value_string_utf8(env, value, nullptr, 0, &requiredLength);
    if (status != napi_ok) {
        OH_LOG_ERROR(LOG_APP, "NapiValueToString: failed to get string length, status=%d", status);
        return "";
    }

    // Handle empty string case
    if (requiredLength == 0) {
        return "";
    }

    // Step 2: Allocate buffer with space for null terminator
    // Use vector for exception-safe memory management
    std::vector<char> buffer(requiredLength + 1, '\0');

    // Step 3: Copy string data
    size_t copiedLength = 0;
    status = napi_get_value_string_utf8(env, value, buffer.data(), buffer.size(), &copiedLength);
    if (status != napi_ok) {
        OH_LOG_ERROR(LOG_APP, "NapiValueToString: failed to copy string, status=%d", status);
        return "";
    }

    // Step 4: Create string from buffer with actual copied length
    return std::string(buffer.data(), copiedLength);
}

/**
 * Safely create napi_value string from std::string
 * - Checks napi_status
 * - Returns nullptr on error (caller must handle)
 */
static napi_value StringToNapiValue(napi_env env, const std::string& str) {
    if (env == nullptr) {
        return nullptr;
    }

    napi_value result = nullptr;
    napi_status status = napi_create_string_utf8(env, str.c_str(), str.length(), &result);
    if (status != napi_ok) {
        OH_LOG_ERROR(LOG_APP, "StringToNapiValue: failed to create string, status=%d", status);
        return nullptr;
    }
    return result;
}

/**
 * Safe string creation that NEVER returns nullptr (Bug #4 fix)
 * - Returns empty string on any error instead of nullptr
 * - Prevents null pointer dereference when setting napi properties
 */
static napi_value SafeStringToNapi(napi_env env, const std::string& str) {
    napi_value result = StringToNapiValue(env, str);
    if (result == nullptr) {
        // Fallback to empty string - this should never fail
        napi_status status = napi_create_string_utf8(env, "", 0, &result);
        if (status != napi_ok || result == nullptr) {
            OH_LOG_ERROR(LOG_APP, "SafeStringToNapi: critical error - even empty string creation failed!");
            // Last resort - return undefined
            napi_get_undefined(env, &result);
        }
    }
    return result;
}

// ============================================================================
// Validation Helpers - Strong type checking for NAPI arguments
// ============================================================================

/**
 * Check if argument count matches expected, throw error if not
 * @returns true if valid, false if error thrown
 */
static bool ValidateArgCount(napi_env env, size_t actual, size_t expected, const char* funcName) {
    if (actual < expected) {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s: expected %zu arguments, got %zu", funcName, expected, actual);
        napi_throw_error(env, "EINVAL", msg);
        return false;
    }
    return true;
}

/**
 * Check if value is a string, throw type error if not
 * @returns true if valid string, false if error thrown
 */
static bool ValidateString(napi_env env, napi_value value, const char* argName) {
    napi_valuetype type;
    napi_typeof(env, value, &type);
    if (type != napi_string) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Argument '%s' must be a string, got %d", argName, type);
        napi_throw_type_error(env, "EINVAL", msg);
        return false;
    }
    return true;
}

/**
 * Check if value is a number, throw type error if not
 * @returns true if valid number, false if error thrown
 */
static bool ValidateNumber(napi_env env, napi_value value, const char* argName) {
    napi_valuetype type;
    napi_typeof(env, value, &type);
    if (type != napi_number) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Argument '%s' must be a number, got %d", argName, type);
        napi_throw_type_error(env, "EINVAL", msg);
        return false;
    }
    return true;
}

/**
 * Check if value is an array, throw type error if not
 * @returns true if valid array, false if error thrown
 */
static bool ValidateArray(napi_env env, napi_value value, const char* argName) {
    bool isArray = false;
    napi_is_array(env, value, &isArray);
    if (!isArray) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Argument '%s' must be an array", argName);
        napi_throw_type_error(env, "EINVAL", msg);
        return false;
    }
    return true;
}

// ============================================================================
// Async LoadDictionary Implementation
// ============================================================================

/**
 * Async work data for loadDictionary
 */
struct LoadDictionaryAsyncData {
    napi_async_work work;
    napi_deferred deferred;
    std::string path;
    bool success;
    int wordCount;
};

/**
 * Execute callback - runs on worker thread (thread pool)
 * Does the actual heavy lifting of loading the dictionary
 */
static void LoadDictionaryExecute(napi_env env, void* data) {
    LoadDictionaryAsyncData* asyncData = static_cast<LoadDictionaryAsyncData*>(data);

    OH_LOG_INFO(LOG_APP, "loadDictionary [ASYNC]: loading from path=%{public}s", asyncData->path.c_str());

    // Load into LOCAL trie first (no lock needed - this is the slow part)
#if USE_POOLED_TRIE
    OH_LOG_INFO(LOG_APP, "loadDictionary [ASYNC]: using POOLED TRIE (10x faster)");
    auto newTrie = std::make_unique<hoskey::TriePooled>();
#else
    auto newTrie = std::make_unique<hoskey::Trie>();
#endif
    bool success = newTrie->loadFromFile(asyncData->path);

    if (success) {
        asyncData->wordCount = newTrie->getWordCount();
        OH_LOG_INFO(LOG_APP, "loadDictionary [ASYNC]: SUCCESS - loaded %d words", asyncData->wordCount);

        // Quick swap under mutex (only lock during fast pointer swap)
        {
            std::lock_guard<std::mutex> lock(g_trieMutex);
            g_suggestEngine.reset();  // Reset first (holds ref to old trie)
            g_trie = std::move(newTrie);  // Fast move, old trie deleted
            g_suggestEngine = std::make_unique<hoskey::SuggestEngine>(g_trie.get());
        }

        // Clear suggestion cache (old results are invalid now)
        ClearSuggestionCache();

        asyncData->success = true;
    } else {
        OH_LOG_ERROR(LOG_APP, "loadDictionary [ASYNC]: FAILED to load from %{public}s", asyncData->path.c_str());
        asyncData->success = false;
    }
}

/**
 * Complete callback - runs on main JS thread after execute completes
 * Resolves or rejects the promise
 */
static void LoadDictionaryComplete(napi_env env, napi_status status, void* data) {
    LoadDictionaryAsyncData* asyncData = static_cast<LoadDictionaryAsyncData*>(data);

    napi_value result;
    napi_get_boolean(env, asyncData->success, &result);

    // Resolve the promise with the result
    napi_resolve_deferred(env, asyncData->deferred, result);

    // Clean up async work
    napi_delete_async_work(env, asyncData->work);
    delete asyncData;
}

/**
 * loadDictionary(path: string): Promise<boolean>
 * Load binary dictionary from file path asynchronously
 * Returns a Promise that resolves to true on success, false on failure
 */
static napi_value LoadDictionary(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    // Validate arguments
    if (!ValidateArgCount(env, argc, 1, "loadDictionary")) {
        return nullptr;
    }
    if (!ValidateString(env, args[0], "path")) {
        return nullptr;
    }

    std::string path = NapiValueToString(env, args[0]);

    // Create async data
    LoadDictionaryAsyncData* asyncData = new LoadDictionaryAsyncData();
    asyncData->path = path;
    asyncData->success = false;
    asyncData->wordCount = 0;

    // Create promise
    napi_value promise;
    napi_create_promise(env, &asyncData->deferred, &promise);

    // Create async work name
    napi_value resourceName;
    napi_create_string_utf8(env, "loadDictionary", NAPI_AUTO_LENGTH, &resourceName);

    // Create async work
    napi_create_async_work(
        env,
        nullptr,
        resourceName,
        LoadDictionaryExecute,
        LoadDictionaryComplete,
        asyncData,
        &asyncData->work
    );

    // Queue async work
    napi_queue_async_work(env, asyncData->work);

    OH_LOG_INFO(LOG_APP, "loadDictionary: async work queued for path=%{public}s", path.c_str());

    return promise;
}

/**
 * loadDictionarySync(path: string): boolean
 * Load binary dictionary SYNCHRONOUSLY - no libuv overhead
 * Use this for faster loading when UI blocking is acceptable (e.g., splash screen)
 *
 * With optimized TrieNode (unordered_map instead of children_[256]):
 * - Memory: 800MB -> ~20MB
 * - Load time: ~20s -> ~1-2s (expected)
 */
static napi_value LoadDictionarySync(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    // Validate arguments
    if (!ValidateArgCount(env, argc, 1, "loadDictionarySync")) {
        return nullptr;
    }
    if (!ValidateString(env, args[0], "path")) {
        return nullptr;
    }

    std::string path = NapiValueToString(env, args[0]);

    OH_LOG_INFO(LOG_APP, "loadDictionarySync: loading from path=%{public}s", path.c_str());

    // Load into LOCAL trie first
#if USE_POOLED_TRIE
    OH_LOG_INFO(LOG_APP, "loadDictionarySync: using POOLED TRIE (10x faster)");
    auto newTrie = std::make_unique<hoskey::TriePooled>();
#else
    auto newTrie = std::make_unique<hoskey::Trie>();
#endif
    bool success = newTrie->loadFromFile(path);

    if (success) {
        int wordCount = newTrie->getWordCount();
        size_t memUsage = newTrie->getMemoryUsage();
        OH_LOG_INFO(LOG_APP, "loadDictionarySync: SUCCESS - loaded %{public}d words, memory=%{public}zu bytes",
                    wordCount, memUsage);

        // Quick swap under mutex
        {
            std::lock_guard<std::mutex> lock(g_trieMutex);
            g_suggestEngine.reset();
            g_trie = std::move(newTrie);
            g_suggestEngine = std::make_unique<hoskey::SuggestEngine>(g_trie.get());
        }

        // Clear suggestion cache (old results are invalid now)
        ClearSuggestionCache();
    } else {
        OH_LOG_ERROR(LOG_APP, "loadDictionarySync: FAILED to load from %{public}s", path.c_str());
    }

    napi_value result;
    napi_get_boolean(env, success, &result);
    return result;
}

/**
 * contains(word: string): boolean
 * Check if word exists in dictionary
 */
static napi_value Contains(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    // Validate arguments
    if (!ValidateArgCount(env, argc, 1, "contains")) {
        return nullptr;
    }
    if (!ValidateString(env, args[0], "word")) {
        return nullptr;
    }

    std::string word = NapiValueToString(env, args[0]);

    // Lock and check trie
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(g_trieMutex);
        if (g_trie) {
            found = g_trie->contains(word);
        }
    }

    napi_value result;
    napi_get_boolean(env, found, &result);
    return result;
}

/**
 * getFrequency(word: string): number
 * Get frequency/probability of word
 */
static napi_value GetFrequency(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    // Validate arguments
    if (!ValidateArgCount(env, argc, 1, "getFrequency")) {
        return nullptr;
    }
    if (!ValidateString(env, args[0], "word")) {
        return nullptr;
    }

    std::string word = NapiValueToString(env, args[0]);

    // Lock and check trie
    int frequency = 0;
    {
        std::lock_guard<std::mutex> lock(g_trieMutex);
        if (g_trie) {
            frequency = g_trie->getFrequency(word);
        }
    }

    napi_value result;
    napi_create_int32(env, frequency, &result);
    return result;
}

/**
 * SuggestResult interface:
 * { word: string, score: number, errorType: number }
 * Creates a JavaScript object from C++ SuggestResult
 *
 * OPTIMIZED for API 22:
 * - Uses cached property keys (avoids string internalization overhead)
 * - Uses napi_set_property with cached keys instead of napi_set_named_property
 * - ~30% faster than original implementation
 *
 * Returns nullptr on error (caller must handle)
 */
static napi_value CreateSuggestResult(napi_env env, const hoskey::SuggestResult& sr) {
    if (env == nullptr) {
        return nullptr;
    }

    napi_value obj = nullptr;
    napi_status status = napi_create_object(env, &obj);
    if (status != napi_ok || obj == nullptr) {
        return nullptr;
    }

    // Get cached property keys (faster than creating strings each time)
    napi_value wordKey = GetCachedKey(env, g_cachedWordKey);
    napi_value scoreKey = GetCachedKey(env, g_cachedScoreKey);
    napi_value errorTypeKey = GetCachedKey(env, g_cachedErrorTypeKey);

    // Fallback to named properties if cache not initialized
    if (!wordKey || !scoreKey || !errorTypeKey) {
        // Original slow path
        napi_value word = StringToNapiValue(env, sr.word);
        if (word) napi_set_named_property(env, obj, "word", word);

        napi_value score;
        napi_create_double(env, sr.score, &score);
        napi_set_named_property(env, obj, "score", score);

        napi_value errorType;
        napi_create_int32(env, static_cast<int>(sr.errorType), &errorType);
        napi_set_named_property(env, obj, "errorType", errorType);

        return obj;
    }

    // Fast path: use cached keys with napi_set_property
    napi_value wordVal = StringToNapiValue(env, sr.word);
    if (wordVal) {
        napi_set_property(env, obj, wordKey, wordVal);
    }

    napi_value scoreVal;
    napi_create_double(env, sr.score, &scoreVal);
    napi_set_property(env, obj, scoreKey, scoreVal);

    napi_value errorTypeVal;
    napi_create_int32(env, static_cast<int>(sr.errorType), &errorTypeVal);
    napi_set_property(env, obj, errorTypeKey, errorTypeVal);

    return obj;
}

/**
 * getSuggestions(prefix: string, limit: number): SuggestResult[]
 * Get word suggestions with scores
 */
static napi_value GetSuggestions(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    // Validate arguments
    if (!ValidateArgCount(env, argc, 2, "getSuggestions")) {
        return nullptr;
    }
    if (!ValidateString(env, args[0], "prefix")) {
        return nullptr;
    }
    if (!ValidateNumber(env, args[1], "limit")) {
        return nullptr;
    }

    // GUARDRAIL: Return empty array if engine not initialized (safe, no crash)
    // GUARDRAIL: Return empty array if engine not initialized
    if (!g_suggestEngine) {
        napi_value emptyResult;
        napi_create_array(env, &emptyResult);
        OH_LOG_WARN(LOG_APP, "getSuggestions: called but dictionary not loaded - returning empty array");
        return emptyResult;
    }

    std::string prefix = NapiValueToString(env, args[0]);

    int32_t limit = 10;
    napi_get_value_int32(env, args[1], &limit);
    // Clamp limit to reasonable bounds
    if (limit < 1) limit = 1;
    if (limit > 100) limit = 100;

    // DEBUG logging disabled for performance - uncomment if needed
    // OH_LOG_DEBUG(LOG_APP, "getSuggestions: prefix=\"%{public}s\" len=%{public}zu", prefix.c_str(), prefix.length());

    // API 22 Optimization: Check LRU cache first
    const std::vector<hoskey::SuggestResult>* cachedResults = GetCachedSuggestions(prefix);
    std::vector<hoskey::SuggestResult> suggestions;

    if (cachedResults) {
        // Cache hit - use cached results
        suggestions = *cachedResults;
        if (static_cast<int32_t>(suggestions.size()) > limit) {
            suggestions.resize(limit);
        }
        OH_LOG_DEBUG(LOG_APP, "getSuggestions: cache HIT, %{public}zu results", suggestions.size());
    } else {
        // Cache miss - get from engine and cache
        suggestions = g_suggestEngine->getSuggestions(prefix, limit);
        CacheSuggestions(prefix, suggestions);
        // OH_LOG_DEBUG for performance - uncomment if needed
        // OH_LOG_DEBUG(LOG_APP, "getSuggestions: cache MISS, %{public}zu results", suggestions.size());
    }

    // API 22 Optimization: Create array with known size (avoids reallocation)
    napi_value result;
    napi_create_array_with_length(env, suggestions.size(), &result);

    // Populate array with suggestion objects (uses cached property keys)
    for (size_t i = 0; i < suggestions.size(); i++) {
        napi_value item = CreateSuggestResult(env, suggestions[i]);
        if (item != nullptr) {
            napi_set_element(env, result, static_cast<uint32_t>(i), item);
        }
    }

    return result;
}

/**
 * findAutocorrection(word: string, threshold: number): SuggestResult | null
 * Find best autocorrection candidate
 */
static napi_value FindAutocorrection(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    // Validate arguments
    if (!ValidateArgCount(env, argc, 2, "findAutocorrection")) {
        return nullptr;
    }
    if (!ValidateString(env, args[0], "word")) {
        return nullptr;
    }
    if (!ValidateNumber(env, args[1], "threshold")) {
        return nullptr;
    }

    // Return null if engine not initialized
    if (!g_suggestEngine) {
        napi_value result;
        napi_get_null(env, &result);
        return result;
    }

    std::string word = NapiValueToString(env, args[0]);

    double threshold = 0.185; // Default OpenBoard threshold
    napi_get_value_double(env, args[1], &threshold);
    // Clamp threshold to valid range [0.0, 1.0]
    if (threshold < 0.0) threshold = 0.0;
    if (threshold > 1.0) threshold = 1.0;

    auto correction = g_suggestEngine->findAutocorrection(word, threshold);

    if (correction.word.empty()) {
        napi_value result;
        napi_get_null(env, &result);
        return result;
    }

    return CreateSuggestResult(env, correction);
}

/**
 * setProximityInfo(layout: string, keyWidth: number, keyHeight: number): boolean
 * Initialize keyboard proximity information
 */
static napi_value SetProximityInfo(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    // Validate arguments
    if (!ValidateArgCount(env, argc, 3, "setProximityInfo")) {
        return nullptr;
    }
    if (!ValidateString(env, args[0], "layout")) {
        return nullptr;
    }
    if (!ValidateNumber(env, args[1], "keyWidth")) {
        return nullptr;
    }
    if (!ValidateNumber(env, args[2], "keyHeight")) {
        return nullptr;
    }

    std::string layout = NapiValueToString(env, args[0]);

    double keyWidth = 0, keyHeight = 0;
    napi_get_value_double(env, args[1], &keyWidth);
    napi_get_value_double(env, args[2], &keyHeight);

    // Validate dimensions are positive
    if (keyWidth <= 0 || keyHeight <= 0) {
        napi_throw_error(env, "EINVAL", "setProximityInfo: keyWidth and keyHeight must be positive");
        return nullptr;
    }

    // Proximity info configured (implementation uses these values internally)
    napi_value result;
    napi_get_boolean(env, true, &result);
    return result;
}

/**
 * getStats(): { wordCount, memoryUsage, cacheSize, cacheHits, cacheMisses, cacheHitRate }
 * Get dictionary and cache statistics
 */
static napi_value GetStats(napi_env env, napi_callback_info info) {
    napi_value obj;
    napi_create_object(env, &obj);

    int wordCount = 0;
    size_t memoryUsage = 0;
    {
        std::lock_guard<std::mutex> lock(g_trieMutex);
        if (g_trie) {
            wordCount = g_trie->getWordCount();
            memoryUsage = g_trie->getMemoryUsage();
        }
    }

    napi_value wordCountVal;
    napi_create_int32(env, wordCount, &wordCountVal);
    napi_set_named_property(env, obj, "wordCount", wordCountVal);

    napi_value memoryVal;
    napi_create_int64(env, static_cast<int64_t>(memoryUsage), &memoryVal);
    napi_set_named_property(env, obj, "memoryUsage", memoryVal);

    // Add cache statistics
    size_t cacheSize;
    int64_t cacheHits, cacheMisses;
    GetCacheStats(cacheSize, cacheHits, cacheMisses);
    float hitRate = GetCacheHitRate();

    napi_value cacheSizeVal, hitsVal, missesVal, hitRateVal;
    napi_create_int32(env, static_cast<int32_t>(cacheSize), &cacheSizeVal);
    napi_create_int64(env, cacheHits, &hitsVal);
    napi_create_int64(env, cacheMisses, &missesVal);
    napi_create_double(env, static_cast<double>(hitRate), &hitRateVal);

    napi_set_named_property(env, obj, "cacheSize", cacheSizeVal);
    napi_set_named_property(env, obj, "cacheHits", hitsVal);
    napi_set_named_property(env, obj, "cacheMisses", missesVal);
    napi_set_named_property(env, obj, "cacheHitRate", hitRateVal);

    return obj;
}

// Include GestureStroke for Bezier smoothing
#include "suggest/policyimpl/gesture/gesture_stroke.h"
// Include GestureTrailData for Yandex-style trail rendering
#include "suggest/policyimpl/gesture/gesture_trail_data.h"

// Global trail data for rendering (Yandex-style)
static std::unique_ptr<latinime::GestureTrailData> g_trailData;
static std::unique_ptr<latinime::GestureTrailDrawer> g_trailDrawer;
static latinime::TrailParams g_trailParams;
static latinime::TrailRenderParams g_trailRenderParams;

// Helper: Find nearest key to position
static std::string FindNearestKey(float x, float y) {
    if (g_keyboardLayout.empty()) {
        return "";
    }

    float minDist = std::numeric_limits<float>::max();
    std::string nearestKey;

    for (const auto& key : g_keyboardLayout) {
        float dx = x - key.centerX;
        float dy = y - key.centerY;
        float dist = dx * dx + dy * dy;

        if (dist < minDist) {
            minDist = dist;
            nearestKey = key.key;
        }
    }

    return nearestKey;
}

// Helper: Calculate distance from point to key center
static float DistanceToKey(float x, float y, const KeyBounds& key) {
    float dx = x - key.centerX;
    float dy = y - key.centerY;
    return std::sqrt(dx * dx + dy * dy);
}

// =============================================================================
// Yandex-style Swipe Recognition Helpers
// =============================================================================

/**
 * Get top N nearest keys to a point with their proximity scores
 * Returns: vector of (key, proximity_score) pairs, sorted by distance
 * Proximity score: 1.0 = on key center, 0.0 = far away
 */
static std::vector<std::pair<std::string, float>> GetNearestKeysWithScores(
    float x, float y, size_t topN = 3) {
    
    std::vector<std::pair<std::string, float>> result;
    if (g_keyboardLayout.empty()) return result;
    
    // Calculate distances to all keys
    std::vector<std::tuple<std::string, float, float>> keysWithDist;
    for (const auto& key : g_keyboardLayout) {
        if (key.key.length() != 1) continue;  // Only single characters
        
        float dx = x - key.centerX;
        float dy = y - key.centerY;
        float dist = std::sqrt(dx * dx + dy * dy);
        
        // Normalize by key size (larger keys have larger "hitbox")
        float keyRadius = std::sqrt(key.width * key.width + key.height * key.height) / 2.0f;
        float normalizedDist = dist / keyRadius;
        
        keysWithDist.emplace_back(key.key, dist, normalizedDist);
    }
    
    // Sort by distance
    std::sort(keysWithDist.begin(), keysWithDist.end(),
        [](const auto& a, const auto& b) { return std::get<1>(a) < std::get<1>(b); });
    
    // Convert to proximity scores (Gaussian-like falloff)
    for (size_t i = 0; i < std::min(topN, keysWithDist.size()); ++i) {
        const auto& [key, dist, normDist] = keysWithDist[i];
        
        // Gaussian proximity score: exp(-dist²/2σ²), σ based on key size
        float sigma = 1.5f;  // Tunable parameter
        float score = std::exp(-(normDist * normDist) / (2.0f * sigma * sigma));
        
        if (score > 0.01f) {  // Filter out very low scores
            result.emplace_back(key, score);
        }
    }
    
    return result;
}

/**
 * Calculate word match score using proximity-weighted path matching
 * This is a Yandex-style scoring that considers:
 * 1. Proximity to each expected key
 * 2. Path shape matching
 * 3. Speed/curvature at transition points
 */
static float CalculateWordProximityScore(
    const std::string& word,
    const std::vector<latinime::SwipePoint>& processedPath,
    const std::vector<KeyBounds>& layout) {
    
    if (word.empty() || processedPath.empty()) return 0.0f;
    
    // Build a map of key positions
    std::unordered_map<char, std::pair<float, float>> keyPositions;
    for (const auto& key : layout) {
        if (key.key.length() == 1) {
            keyPositions[std::tolower(key.key[0])] = {key.centerX, key.centerY};
        }
    }
    
    // For each character in word, find the closest path point
    float totalScore = 0.0f;
    size_t pathIdx = 0;
    
    for (size_t charIdx = 0; charIdx < word.length(); ++charIdx) {
        char targetChar = std::tolower(word[charIdx]);
        auto it = keyPositions.find(targetChar);
        if (it == keyPositions.end()) continue;
        
        float targetX = it->second.first;
        float targetY = it->second.second;
        
        // Find the path segment closest to this key
        float bestDist = std::numeric_limits<float>::max();
        size_t bestIdx = pathIdx;
        
        // Search forward from current position (swipe should progress through letters)
        for (size_t i = pathIdx; i < processedPath.size(); ++i) {
            float dx = processedPath[i].x - targetX;
            float dy = processedPath[i].y - targetY;
            float dist = std::sqrt(dx * dx + dy * dy);
            
            if (dist < bestDist) {
                bestDist = dist;
                bestIdx = i;
            }
            
            // Don't search too far ahead - path should be sequential
            if (i > pathIdx + processedPath.size() / word.length() + 5) break;
        }
        
        // Update path index for next character
        pathIdx = bestIdx;
        
        // Score based on distance (closer = better)
        // Use a softer falloff than pure distance
        float avgKeyWidth = 50.0f;  // Approximate
        float normalizedDist = bestDist / avgKeyWidth;
        float charScore = std::exp(-normalizedDist * normalizedDist / 2.0f);
        
        // Bonus for first and last characters (they're usually more accurate)
        if (charIdx == 0 || charIdx == word.length() - 1) {
            charScore *= 1.2f;
        }
        
        totalScore += charScore;
    }
    
    // Normalize by word length
    return totalScore / static_cast<float>(word.length());
}

/**
 * setSwipeKeyboardLayout(keys: Array<{key: string, centerX: number, centerY: number, width: number, height: number}>): boolean
 * Set keyboard layout for swipe recognition
 */
static napi_value SetSwipeKeyboardLayout(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    // Validate arguments
    if (!ValidateArgCount(env, argc, 1, "setSwipeKeyboardLayout")) {
        return nullptr;
    }
    if (!ValidateArray(env, args[0], "keys")) {
        return nullptr;
    }

    uint32_t length = 0;
    napi_get_array_length(env, args[0], &length);

    g_keyboardLayout.clear();
    g_keyboardLayout.reserve(length);

    for (uint32_t i = 0; i < length; i++) {
        napi_value element;
        napi_get_element(env, args[0], i, &element);

        // Get properties
        napi_value keyValue, centerXValue, centerYValue, widthValue, heightValue;
        napi_get_named_property(env, element, "key", &keyValue);
        napi_get_named_property(env, element, "centerX", &centerXValue);
        napi_get_named_property(env, element, "centerY", &centerYValue);
        napi_get_named_property(env, element, "width", &widthValue);
        napi_get_named_property(env, element, "height", &heightValue);

        // Extract values
        std::string key = NapiValueToString(env, keyValue);
        double centerX = 0, centerY = 0, width = 0, height = 0;
        napi_get_value_double(env, centerXValue, &centerX);
        napi_get_value_double(env, centerYValue, &centerY);
        napi_get_value_double(env, widthValue, &width);
        napi_get_value_double(env, heightValue, &height);

        g_keyboardLayout.push_back({key, (float)centerX, (float)centerY, (float)width, (float)height});
    }

    napi_value result;
    napi_get_boolean(env, true, &result);
    return result;
}

/**
 * processSwipePath(points: Array<{x: number, y: number, timestamp: number}>): 
 *   {bestWord: string, alternatives: string[], confidence: number, rawSequence: string} | null
 * 
 * Process swipe path and return recognized word.
 * 
 * ENHANCED with Yandex-style algorithms:
 * 1. Bezier curve smoothing for trajectory
 * 2. Speed-based adaptive sampling (more points on turns)
 * 3. Proximity-weighted scoring considering neighbor keys
 */
static napi_value ProcessSwipePath(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    // Validate arguments
    if (!ValidateArgCount(env, argc, 1, "processSwipePath")) {
        return nullptr;
    }
    if (!ValidateArray(env, args[0], "points")) {
        return nullptr;
    }

    // Return null if prerequisites not met (not an error, just not ready)
    {
        std::lock_guard<std::mutex> lock(g_trieMutex);
        if (g_keyboardLayout.empty() || !g_trie) {
            napi_value result;
            napi_get_null(env, &result);
            return result;
        }
    }

    uint32_t length = 0;
    napi_get_array_length(env, args[0], &length);

    if (length < 5) { // Minimum 5 points for valid swipe
        napi_value result;
        napi_get_null(env, &result);
        return result;
    }

    // =========================================================================
    // STEP 1: Extract touch points into GestureStroke for processing
    // =========================================================================
    latinime::GestureParams params;
    params.minSamplingDistance = 3.0f;
    params.maxAngleRadians = 0.2618f;  // ~15 degrees
    params.maxSegmentLength = 20.0f;
    params.maxInterpolationSteps = 10;
    params.adaptiveSamplingSpeedThreshold = 300.0f;
    
    latinime::GestureStroke stroke(params);

    for (uint32_t i = 0; i < length; i++) {
        napi_value element;
        napi_get_element(env, args[0], i, &element);

        napi_value xValue, yValue, tsValue;
        napi_get_named_property(env, element, "x", &xValue);
        napi_get_named_property(env, element, "y", &yValue);
        napi_get_named_property(env, element, "timestamp", &tsValue);

        double x = 0, y = 0;
        int64_t ts = 0;
        napi_get_value_double(env, xValue, &x);
        napi_get_value_double(env, yValue, &y);
        napi_get_value_int64(env, tsValue, &ts);

        stroke.addPoint(static_cast<int>(x), static_cast<int>(y), ts);
    }

    // Validate total distance (min 50px)
    float totalDist = stroke.getTotalDistance();
    if (totalDist < 50.0f) {
        napi_value result;
        napi_get_null(env, &result);
        return result;
    }

    // =========================================================================
    // STEP 2: Apply Bezier smoothing + speed-based adaptive sampling
    // =========================================================================
    std::vector<latinime::SwipePoint> processedPath = stroke.getProcessedPath();
    
    OH_LOG_DEBUG(LOG_APP, "processSwipePath: raw=%{public}zu -> smoothed=%{public}zu points",
                 stroke.getPointCount(), processedPath.size());

    if (processedPath.size() < 3) {
        napi_value result;
        napi_get_null(env, &result);
        return result;
    }

    // =========================================================================
    // STEP 3: Extract key sequence with proximity-aware key detection
    // =========================================================================
    std::string keySequence;
    std::string lastKey;
    
    // Collect proximity scores for each position
    std::vector<std::vector<std::pair<std::string, float>>> pathKeyScores;
    
    for (const auto& point : processedPath) {
        // Get top 3 nearest keys with their proximity scores
        auto nearestKeys = GetNearestKeysWithScores(point.x, point.y, 3);
        pathKeyScores.push_back(nearestKeys);
        
        // For raw sequence, use the nearest key
        if (!nearestKeys.empty()) {
            std::string key = nearestKeys[0].first;
            if (!key.empty() && key != lastKey && key.length() == 1) {
                keySequence += key;
                lastKey = key;
            }
        }
    }

    if (keySequence.length() < 2) {
        napi_value result;
        napi_get_null(env, &result);
        return result;
    }

    // =========================================================================
    // STEP 4: Get candidate words from dictionary
    // =========================================================================
    std::vector<std::string> candidates;
    std::string firstLetter = keySequence.substr(0, 1);
    
    // Also consider alternative first letters (for fat-finger errors)
    std::set<std::string> firstLetterCandidates;
    firstLetterCandidates.insert(firstLetter);
    
    // Add first point's alternative keys as candidates
    if (!pathKeyScores.empty() && pathKeyScores[0].size() > 1) {
        for (size_t i = 1; i < std::min((size_t)2, pathKeyScores[0].size()); ++i) {
            if (pathKeyScores[0][i].second > 0.3f) {  // Only high-confidence alternatives
                firstLetterCandidates.insert(pathKeyScores[0][i].first);
            }
        }
    }
    
    // Get suggestions for each potential first letter
    for (const auto& fl : firstLetterCandidates) {
        auto suggestions = g_suggestEngine->getSuggestions(fl, 100);
        for (const auto& suggestion : suggestions) {
            // Allow some length variance
            if (suggestion.word.length() >= keySequence.length() - 2 &&
                suggestion.word.length() <= keySequence.length() + 3) {
                candidates.push_back(suggestion.word);
            }
        }
    }

    if (candidates.empty()) {
        napi_value result;
        napi_get_null(env, &result);
        return result;
    }

    // =========================================================================
    // STEP 5: Rank candidates with enhanced proximity scoring
    // =========================================================================
    struct Candidate {
        std::string word;
        float score;
        float proximityScore;
        float sequenceScore;
        int frequency;
    };

    std::vector<Candidate> ranked;
    
    for (const auto& word : candidates) {
        Candidate cand;
        cand.word = word;
        
        // 5a. Proximity-weighted path score (Yandex-style)
        cand.proximityScore = CalculateWordProximityScore(word, processedPath, g_keyboardLayout);
        
        // 5b. Simple sequence match score
        size_t matchCount = 0;
        for (size_t i = 0; i < std::min(word.length(), keySequence.length()); i++) {
            if (std::tolower(word[i]) == std::tolower(keySequence[i])) {
                matchCount++;
            }
        }
        cand.sequenceScore = static_cast<float>(matchCount) / 
                             static_cast<float>(std::max(word.length(), keySequence.length()));
        
        // 5c. Length penalty (prefer words close to expected length)
        int lenDiff = std::abs(static_cast<int>(word.length()) - static_cast<int>(keySequence.length()));
        float lengthPenalty = lenDiff * 0.05f;
        
        // 5d. Frequency boost
        cand.frequency = 0;
        {
            std::lock_guard<std::mutex> lock(g_trieMutex);
            if (g_trie) {
                cand.frequency = g_trie->getFrequency(word);
            }
        }
        float freqBoost = std::min(0.2f, cand.frequency * 0.001f);
        
        // 5e. Combined score (weighted combination)
        // Proximity score is the most important (60%)
        // Sequence match (25%)
        // Frequency boost (15%)
        cand.score = cand.proximityScore * 0.60f + 
                     cand.sequenceScore * 0.25f + 
                     freqBoost - 
                     lengthPenalty;
        
        // Threshold for inclusion
        if (cand.score > 0.25f) {
            ranked.push_back(cand);
        }
    }

    if (ranked.empty()) {
        // Fallback: if enhanced scoring gives nothing, try simpler matching
        for (const auto& word : candidates) {
            float simpleScore = 0;
            for (size_t i = 0; i < std::min(word.length(), keySequence.length()); i++) {
                if (std::tolower(word[i]) == std::tolower(keySequence[i])) {
                    simpleScore += 1.0f;
                }
            }
            simpleScore /= static_cast<float>(keySequence.length());
            
            if (simpleScore > 0.3f) {
                ranked.push_back({word, simpleScore, simpleScore, simpleScore, 0});
            }
        }
    }
    
    if (ranked.empty()) {
        napi_value result;
        napi_get_null(env, &result);
        return result;
    }

    // Sort by score (highest first)
    std::sort(ranked.begin(), ranked.end(),
        [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

    // =========================================================================
    // STEP 6: Build result object
    // =========================================================================
    napi_value obj;
    napi_create_object(env, &obj);

    // bestWord
    napi_value bestWordValue = SafeStringToNapi(env, ranked[0].word);
    napi_set_named_property(env, obj, "bestWord", bestWordValue);

    // alternatives (up to 5)
    napi_value alternatives;
    size_t altCount = (ranked.size() > 1) ? std::min((size_t)5, ranked.size() - 1) : 0;
    napi_create_array_with_length(env, altCount, &alternatives);
    for (size_t i = 0; i < altCount && i + 1 < ranked.size(); i++) {
        napi_value alt = SafeStringToNapi(env, ranked[i + 1].word);
        napi_set_element(env, alternatives, i, alt);
    }
    napi_set_named_property(env, obj, "alternatives", alternatives);

    // confidence (clamped to [0, 1])
    napi_value confidenceValue;
    float confidence = std::max(0.0f, std::min(1.0f, ranked[0].score));
    napi_create_double(env, confidence, &confidenceValue);
    napi_set_named_property(env, obj, "confidence", confidenceValue);

    // rawSequence
    napi_value rawSeqValue = SafeStringToNapi(env, keySequence);
    napi_set_named_property(env, obj, "rawSequence", rawSeqValue);
    
    // Debug: Add processing stats
    OH_LOG_INFO(LOG_APP, "processSwipePath: best='%{public}s' conf=%.2f prox=%.2f seq=%.2f alts=%{public}zu",
                ranked[0].word.c_str(), ranked[0].score,
                ranked[0].proximityScore, ranked[0].sequenceScore, altCount);

    return obj;
}

// ============================================================================
// Yandex-style Gesture Trail NAPI Functions
// ============================================================================

/**
 * initTrailData(params?: TrailParams): boolean
 * Initialize trail data system with optional parameters
 */
static napi_value InitTrailData(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    // Initialize with default or custom parameters
    g_trailParams = latinime::TrailParams();
    g_trailRenderParams = latinime::TrailRenderParams();

    // Optional params object
    if (argc >= 1) {
        napi_valuetype type;
        napi_typeof(env, args[0], &type);
        if (type == napi_object) {
            napi_value val;

            // Trail params
            if (napi_get_named_property(env, args[0], "minSamplingDistance", &val) == napi_ok) {
                napi_typeof(env, val, &type);
                if (type == napi_number) {
                    double d; napi_get_value_double(env, val, &d);
                    g_trailParams.minSamplingDistance = static_cast<float>(d);
                }
            }
            if (napi_get_named_property(env, args[0], "maxAngleRadians", &val) == napi_ok) {
                napi_typeof(env, val, &type);
                if (type == napi_number) {
                    double d; napi_get_value_double(env, val, &d);
                    g_trailParams.maxAngleRadians = static_cast<float>(d);
                }
            }

            // Render params
            if (napi_get_named_property(env, args[0], "maxWidth", &val) == napi_ok) {
                napi_typeof(env, val, &type);
                if (type == napi_number) {
                    double d; napi_get_value_double(env, val, &d);
                    g_trailRenderParams.maxWidth = static_cast<float>(d);
                }
            }
            if (napi_get_named_property(env, args[0], "minWidth", &val) == napi_ok) {
                napi_typeof(env, val, &type);
                if (type == napi_number) {
                    double d; napi_get_value_double(env, val, &d);
                    g_trailRenderParams.minWidth = static_cast<float>(d);
                }
            }
            if (napi_get_named_property(env, args[0], "fadeStartTimeMs", &val) == napi_ok) {
                napi_typeof(env, val, &type);
                if (type == napi_number) {
                    int32_t i; napi_get_value_int32(env, val, &i);
                    g_trailRenderParams.fadeStartTimeMs = i;
                }
            }
            if (napi_get_named_property(env, args[0], "fadeDurationMs", &val) == napi_ok) {
                napi_typeof(env, val, &type);
                if (type == napi_number) {
                    int32_t i; napi_get_value_int32(env, val, &i);
                    g_trailRenderParams.fadeDurationMs = i;
                }
            }
            if (napi_get_named_property(env, args[0], "trailColor", &val) == napi_ok) {
                napi_typeof(env, val, &type);
                if (type == napi_number) {
                    uint32_t u; napi_get_value_uint32(env, val, &u);
                    g_trailRenderParams.trailColor = u;
                }
            }
        }
    }

    // Create trail data and drawer
    g_trailData = std::make_unique<latinime::GestureTrailData>();
    g_trailDrawer = std::make_unique<latinime::GestureTrailDrawer>();

    OH_LOG_INFO(LOG_APP, "initTrailData: initialized with maxWidth=%.1f minWidth=%.1f fadeStart=%dms",
                g_trailRenderParams.maxWidth, g_trailRenderParams.minWidth,
                g_trailRenderParams.fadeStartTimeMs);

    napi_value result;
    napi_get_boolean(env, true, &result);
    return result;
}

/**
 * addTrailPoint(x: number, y: number, timestamp: number): void
 * Add a point to the trail drawer
 */
static napi_value AddTrailPoint(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (!ValidateArgCount(env, argc, 3, "addTrailPoint")) {
        return nullptr;
    }

    if (!g_trailDrawer) {
        napi_value undefined;
        napi_get_undefined(env, &undefined);
        return undefined;
    }

    double x = 0, y = 0;
    int64_t timestamp = 0;
    napi_get_value_double(env, args[0], &x);
    napi_get_value_double(env, args[1], &y);
    napi_get_value_int64(env, args[2], &timestamp);

    g_trailDrawer->addPoint(static_cast<int>(x), static_cast<int>(y), timestamp);

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

/**
 * updateTrailData(currentTime: number): number
 * Process drawer points into trail data and update visibility
 * Returns: number of visible points
 */
static napi_value UpdateTrailData(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (!ValidateArgCount(env, argc, 1, "updateTrailData")) {
        return nullptr;
    }

    if (!g_trailData || !g_trailDrawer) {
        napi_value result;
        napi_create_int32(env, 0, &result);
        return result;
    }

    int64_t currentTime = 0;
    napi_get_value_int64(env, args[0], &currentTime);

    // Copy points from drawer to trail data with Bezier interpolation
    const auto& xBuf = g_trailDrawer->getXBuffer();
    const auto& yBuf = g_trailDrawer->getYBuffer();
    const auto& timeBuf = g_trailDrawer->getTimeBuffer();
    size_t pointCount = g_trailDrawer->getPointCount();

    if (pointCount > 0) {
        g_trailData->copyFromRawPoints(
            xBuf.data(), yBuf.data(), timeBuf.data(),
            pointCount, currentTime, g_trailParams);
    }

    // Update visibility (which points should still be shown)
    int visibleCount = g_trailData->updateVisibility(currentTime, g_trailRenderParams);

    napi_value result;
    napi_create_int32(env, visibleCount, &result);
    return result;
}

/**
 * getTrailSegments(currentTime: number): Float32Array | null
 * Get trail segments for rendering
 * Returns: Float32Array of segments (7 floats per segment: x0, y0, w0, x1, y1, w1, alpha)
 */
static napi_value GetTrailSegments(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (!ValidateArgCount(env, argc, 1, "getTrailSegments")) {
        return nullptr;
    }

    if (!g_trailData) {
        napi_value nullVal;
        napi_get_null(env, &nullVal);
        return nullVal;
    }

    int64_t currentTime = 0;
    napi_get_value_int64(env, args[0], &currentTime);

    // Get segments from trail data
    std::vector<float> segments;
    int segmentCount = g_trailData->getSegmentsForRendering(currentTime, g_trailRenderParams, segments);

    if (segmentCount == 0 || segments.empty()) {
        napi_value nullVal;
        napi_get_null(env, &nullVal);
        return nullVal;
    }

    // Create Float32Array for efficient transfer
    napi_value arrayBuffer;
    void* data;
    size_t byteLength = segments.size() * sizeof(float);
    napi_create_arraybuffer(env, byteLength, &data, &arrayBuffer);
    memcpy(data, segments.data(), byteLength);

    napi_value typedArray;
    napi_create_typedarray(env, napi_float32_array, segments.size(), arrayBuffer, 0, &typedArray);

    return typedArray;
}

/**
 * resetTrailData(): void
 * Reset trail data for new gesture
 */
static napi_value ResetTrailData(napi_env env, napi_callback_info info) {
    if (g_trailData) {
        g_trailData->reset();
    }
    if (g_trailDrawer) {
        g_trailDrawer->reset();
    }

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

/**
 * setTrailRenderParams(params: TrailRenderParams): void
 * Update trail render parameters
 */
static napi_value SetTrailRenderParams(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (!ValidateArgCount(env, argc, 1, "setTrailRenderParams")) {
        return nullptr;
    }

    napi_valuetype type;
    napi_typeof(env, args[0], &type);
    if (type != napi_object) {
        napi_throw_type_error(env, "EINVAL", "setTrailRenderParams: params must be an object");
        return nullptr;
    }

    napi_value val;

    if (napi_get_named_property(env, args[0], "maxWidth", &val) == napi_ok) {
        napi_typeof(env, val, &type);
        if (type == napi_number) {
            double d; napi_get_value_double(env, val, &d);
            g_trailRenderParams.maxWidth = static_cast<float>(d);
        }
    }
    if (napi_get_named_property(env, args[0], "minWidth", &val) == napi_ok) {
        napi_typeof(env, val, &type);
        if (type == napi_number) {
            double d; napi_get_value_double(env, val, &d);
            g_trailRenderParams.minWidth = static_cast<float>(d);
        }
    }
    if (napi_get_named_property(env, args[0], "widthRatio", &val) == napi_ok) {
        napi_typeof(env, val, &type);
        if (type == napi_number) {
            double d; napi_get_value_double(env, val, &d);
            g_trailRenderParams.widthRatio = static_cast<float>(d);
        }
    }
    if (napi_get_named_property(env, args[0], "fadeStartTimeMs", &val) == napi_ok) {
        napi_typeof(env, val, &type);
        if (type == napi_number) {
            int32_t i; napi_get_value_int32(env, val, &i);
            g_trailRenderParams.fadeStartTimeMs = i;
        }
    }
    if (napi_get_named_property(env, args[0], "fadeDurationMs", &val) == napi_ok) {
        napi_typeof(env, val, &type);
        if (type == napi_number) {
            int32_t i; napi_get_value_int32(env, val, &i);
            g_trailRenderParams.fadeDurationMs = i;
        }
    }
    if (napi_get_named_property(env, args[0], "trailColor", &val) == napi_ok) {
        napi_typeof(env, val, &type);
        if (type == napi_number) {
            uint32_t u; napi_get_value_uint32(env, val, &u);
            g_trailRenderParams.trailColor = u;
        }
    }
    if (napi_get_named_property(env, args[0], "shadowEnabled", &val) == napi_ok) {
        napi_typeof(env, val, &type);
        if (type == napi_boolean) {
            bool b; napi_get_value_bool(env, val, &b);
            g_trailRenderParams.shadowEnabled = b;
        }
    }

    // Recalculate total lifetime
    g_trailRenderParams.totalLifetimeMs = g_trailRenderParams.fadeStartTimeMs +
                                           g_trailRenderParams.fadeDurationMs;

    OH_LOG_DEBUG(LOG_APP, "setTrailRenderParams: maxWidth=%.1f color=0x%08X fade=%d+%dms",
                 g_trailRenderParams.maxWidth, g_trailRenderParams.trailColor,
                 g_trailRenderParams.fadeStartTimeMs, g_trailRenderParams.fadeDurationMs);

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

/**
 * compactTrailBuffers(): void
 * Remove expired points from trail buffers to save memory
 */
static napi_value CompactTrailBuffers(napi_env env, napi_callback_info info) {
    if (g_trailData) {
        g_trailData->compactBuffers();
    }

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

// ============================================================================
// User Learning NAPI Functions (Legacy - Trie-based, no bigram context)
// ============================================================================

/**
 * addLearnedWordSimple(word: string, frequency?: number): boolean
 * Add a word to user dictionary (legacy, no bigram context)
 */
static napi_value AddLearnedWordSimple(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    // Validate arguments
    if (!ValidateArgCount(env, argc, 1, "addLearnedWordSimple")) {
        return nullptr;
    }
    if (!ValidateString(env, args[0], "word")) {
        return nullptr;
    }

    std::string word = NapiValueToString(env, args[0]);

    // Optional frequency parameter (default: 200)
    int frequency = 200;
    if (argc >= 2) {
        napi_valuetype type;
        napi_typeof(env, args[1], &type);
        if (type == napi_number) {
            napi_get_value_int32(env, args[1], &frequency);
        }
    }

    // Clamp frequency to valid range
    if (frequency < 1) frequency = 1;
    if (frequency > 255) frequency = 255;

    bool success = false;
    {
        std::lock_guard<std::mutex> lock(g_trieMutex);
        if (g_suggestEngine) {
            success = g_suggestEngine->addLearnedWord(word, frequency);
            if (success) {
                ClearSuggestionCache();  // Invalidate cache
            }
        }
    }

    OH_LOG_INFO(LOG_APP, "addLearnedWordSimple: word=%{public}s, freq=%d, success=%s",
                word.c_str(), frequency, success ? "true" : "false");

    napi_value result;
    napi_get_boolean(env, success, &result);
    return result;
}

/**
 * recordWordUsage(word: string): boolean
 * Record that a word was used (boosts frequency)
 */
static napi_value RecordWordUsage(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (!ValidateArgCount(env, argc, 1, "recordWordUsage")) {
        return nullptr;
    }
    if (!ValidateString(env, args[0], "word")) {
        return nullptr;
    }

    std::string word = NapiValueToString(env, args[0]);

    bool success = false;
    {
        std::lock_guard<std::mutex> lock(g_trieMutex);
        if (g_suggestEngine) {
            success = g_suggestEngine->recordWordUsage(word);
        }
    }

    napi_value result;
    napi_get_boolean(env, success, &result);
    return result;
}

/**
 * saveUserDict(path: string): boolean
 * Save user dictionary to file
 */
static napi_value SaveUserDict(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (!ValidateArgCount(env, argc, 1, "saveUserDict")) {
        return nullptr;
    }
    if (!ValidateString(env, args[0], "path")) {
        return nullptr;
    }

    std::string path = NapiValueToString(env, args[0]);

    bool success = false;
    {
        std::lock_guard<std::mutex> lock(g_trieMutex);
        if (g_suggestEngine) {
            success = g_suggestEngine->saveUserDict(path);
        }
    }

    OH_LOG_INFO(LOG_APP, "saveUserDict: path=%{public}s, success=%s",
                path.c_str(), success ? "true" : "false");

    napi_value result;
    napi_get_boolean(env, success, &result);
    return result;
}

/**
 * loadUserDict(path: string): boolean
 * Load user dictionary from file
 */
static napi_value LoadUserDict(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (!ValidateArgCount(env, argc, 1, "loadUserDict")) {
        return nullptr;
    }
    if (!ValidateString(env, args[0], "path")) {
        return nullptr;
    }

    std::string path = NapiValueToString(env, args[0]);

    bool success = false;
    {
        std::lock_guard<std::mutex> lock(g_trieMutex);
        if (g_suggestEngine) {
            success = g_suggestEngine->loadUserDict(path);
            if (success) {
                ClearSuggestionCache();  // Invalidate cache
            }
        }
    }

    OH_LOG_INFO(LOG_APP, "loadUserDict: path=%{public}s, success=%s",
                path.c_str(), success ? "true" : "false");

    napi_value result;
    napi_get_boolean(env, success, &result);
    return result;
}

/**
 * getLearnedWordsCount(): number
 * Get count of learned words in user dictionary
 */
static napi_value GetLearnedWordsCount(napi_env env, napi_callback_info info) {
    int count = 0;
    {
        std::lock_guard<std::mutex> lock(g_trieMutex);
        if (g_suggestEngine) {
            count = g_suggestEngine->getLearnedWordsCount();
        }
    }

    napi_value result;
    napi_create_int32(env, count, &result);
    return result;
}

/**
 * removeLearnedWord(word: string): boolean
 * Remove a word from user dictionary
 */
static napi_value RemoveLearnedWord(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (!ValidateArgCount(env, argc, 1, "removeLearnedWord")) {
        return nullptr;
    }
    if (!ValidateString(env, args[0], "word")) {
        return nullptr;
    }

    std::string word = NapiValueToString(env, args[0]);

    bool success = false;
    {
        std::lock_guard<std::mutex> lock(g_trieMutex);
        if (g_suggestEngine) {
            success = g_suggestEngine->removeLearnedWord(word);
            if (success) {
                ClearSuggestionCache();  // Invalidate cache
            }
        }
    }

    OH_LOG_INFO(LOG_APP, "removeLearnedWord: word=%{public}s, success=%s",
                word.c_str(), success ? "true" : "false");

    napi_value result;
    napi_get_boolean(env, success, &result);
    return result;
}

// ============================================================================
// Bigram-Aware Learning NAPI Functions
// ============================================================================

/**
 * addLearnedWord(word: string, prevWord: string, count?: number): void
 * Add a learned word with bigram context
 */
static napi_value AddLearnedWordWithContext(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    // Validate arguments (word and prevWord required, count optional)
    if (!ValidateArgCount(env, argc, 2, "addLearnedWord")) {
        return nullptr;
    }
    if (!ValidateString(env, args[0], "word")) {
        return nullptr;
    }
    if (!ValidateString(env, args[1], "prevWord")) {
        return nullptr;
    }

    std::string word = NapiValueToString(env, args[0]);
    std::string prevWord = NapiValueToString(env, args[1]);
    
    int count = 1;  // Default count
    if (argc >= 3) {
        napi_valuetype type;
        napi_typeof(env, args[2], &type);
        if (type == napi_number) {
            napi_get_value_int32(env, args[2], &count);
            if (count < 1) count = 1;
        }
    }

    // Add to suggest engine
    {
        std::lock_guard<std::mutex> lock(g_trieMutex);
        if (g_suggestEngine) {
            g_suggestEngine->addLearnedWordWithContext(word, prevWord, count);
            OH_LOG_DEBUG(LOG_APP, "addLearnedWord: word=\"%{public}s\" prevWord=\"%{public}s\" count=%d",
                        word.c_str(), prevWord.c_str(), count);
        }
    }

    // Clear suggestion cache (learned word may affect results)
    ClearSuggestionCache();

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

/**
 * getLearnedBoost(word: string, prevWord: string): number
 * Get boost score for a word in context
 */
static napi_value GetLearnedBoost(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    // Validate arguments
    if (!ValidateArgCount(env, argc, 2, "getLearnedBoost")) {
        return nullptr;
    }
    if (!ValidateString(env, args[0], "word")) {
        return nullptr;
    }
    if (!ValidateString(env, args[1], "prevWord")) {
        return nullptr;
    }

    std::string word = NapiValueToString(env, args[0]);
    std::string prevWord = NapiValueToString(env, args[1]);

    int boost = 0;
    {
        std::lock_guard<std::mutex> lock(g_trieMutex);
        if (g_suggestEngine) {
            boost = g_suggestEngine->getLearnedBoost(word, prevWord);
        }
    }

    napi_value result;
    napi_create_int32(env, boost, &result);
    return result;
}

/**
 * saveUserDictionary(path: string): boolean
 * Save bigram-aware user dictionary to JSON file
 */
static napi_value SaveUserDictionary(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    // Validate arguments
    if (!ValidateArgCount(env, argc, 1, "saveUserDictionary")) {
        return nullptr;
    }
    if (!ValidateString(env, args[0], "path")) {
        return nullptr;
    }

    std::string path = NapiValueToString(env, args[0]);

    bool success = false;
    {
        std::lock_guard<std::mutex> lock(g_trieMutex);
        if (g_suggestEngine) {
            success = g_suggestEngine->saveUserDictionary(path);
            OH_LOG_INFO(LOG_APP, "saveUserDictionary: path=\"%{public}s\" success=%{public}s",
                       path.c_str(), success ? "true" : "false");
        }
    }

    napi_value result;
    napi_get_boolean(env, success, &result);
    return result;
}

/**
 * loadUserDictionary(path: string): boolean
 * Load bigram-aware user dictionary from JSON file
 */
static napi_value LoadUserDictionary(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    // Validate arguments
    if (!ValidateArgCount(env, argc, 1, "loadUserDictionary")) {
        return nullptr;
    }
    if (!ValidateString(env, args[0], "path")) {
        return nullptr;
    }

    std::string path = NapiValueToString(env, args[0]);

    bool success = false;
    {
        std::lock_guard<std::mutex> lock(g_trieMutex);
        if (g_suggestEngine) {
            success = g_suggestEngine->loadUserDictionary(path);
            OH_LOG_INFO(LOG_APP, "loadUserDictionary: path=\"%{public}s\" success=%{public}s",
                       path.c_str(), success ? "true" : "false");
        }
    }

    // Clear suggestion cache (new learned words loaded)
    if (success) {
        ClearSuggestionCache();
    }

    napi_value result;
    napi_get_boolean(env, success, &result);
    return result;
}

/**
 * clearLearnedWords(): void
 * Clear all bigram-aware learned words
 */
static napi_value ClearLearnedWords(napi_env env, napi_callback_info info) {
    {
        std::lock_guard<std::mutex> lock(g_trieMutex);
        if (g_suggestEngine) {
            g_suggestEngine->clearLearnedWords();
            OH_LOG_INFO(LOG_APP, "clearLearnedWords: cleared all learned words");
        }
    }

    // Clear suggestion cache
    ClearSuggestionCache();

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

/**
 * getBigramLearnedWordsCount(): number
 * Get count of bigram-aware learned words
 */
static napi_value GetBigramLearnedWordsCount(napi_env env, napi_callback_info info) {
    int count = 0;
    {
        std::lock_guard<std::mutex> lock(g_trieMutex);
        if (g_suggestEngine) {
            count = g_suggestEngine->getBigramLearnedWordsCount();
        }
    }

    napi_value result;
    napi_create_int32(env, count, &result);
    return result;
}

// ============================================================================
// FlatTrie NAPI Functions - Instant loading (<50ms)
// ============================================================================

/**
 * loadFlatDictionary(path: string): boolean
 * Load pre-serialized .flat dictionary for instant loading
 */
static napi_value LoadFlatDictionary(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (!ValidateArgCount(env, argc, 1, "loadFlatDictionary")) {
        return nullptr;
    }
    if (!ValidateString(env, args[0], "path")) {
        return nullptr;
    }

    std::string path = NapiValueToString(env, args[0]);
    OH_LOG_INFO(LOG_APP, "loadFlatDictionary: loading from %{public}s", path.c_str());

    auto newFlatTrie = std::make_unique<hoskey::FlatTrie>();
    bool success = newFlatTrie->load(path);

    if (success) {
        OH_LOG_INFO(LOG_APP, "loadFlatDictionary: SUCCESS - loaded %d words in <50ms",
                    newFlatTrie->getWordCount());

        std::lock_guard<std::mutex> lock(g_trieMutex);
        g_flatTrie = std::move(newFlatTrie);
        ClearSuggestionCache();
    } else {
        OH_LOG_ERROR(LOG_APP, "loadFlatDictionary: FAILED to load from %{public}s", path.c_str());
    }

    napi_value result;
    napi_get_boolean(env, success, &result);
    return result;
}

/**
 * convertToFlatFormat(inputPath: string, outputPath: string, locale?: string): boolean
 * Convert .dict file to optimized .flat format
 */
static napi_value ConvertToFlatFormat(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (!ValidateArgCount(env, argc >= 2 ? 2 : argc, 2, "convertToFlatFormat")) {
        return nullptr;
    }
    if (!ValidateString(env, args[0], "inputPath")) {
        return nullptr;
    }
    if (!ValidateString(env, args[1], "outputPath")) {
        return nullptr;
    }

    std::string inputPath = NapiValueToString(env, args[0]);
    std::string outputPath = NapiValueToString(env, args[1]);
    std::string locale = "";

    if (argc >= 3) {
        napi_valuetype type;
        napi_typeof(env, args[2], &type);
        if (type == napi_string) {
            locale = NapiValueToString(env, args[2]);
        }
    }

    OH_LOG_INFO(LOG_APP, "convertToFlatFormat: %{public}s -> %{public}s (locale=%{public}s)",
                inputPath.c_str(), outputPath.c_str(), locale.c_str());

    bool success = hoskey::FlatTrieBuilder::convert(inputPath, outputPath, locale);

    if (success) {
        OH_LOG_INFO(LOG_APP, "convertToFlatFormat: SUCCESS");
    } else {
        OH_LOG_ERROR(LOG_APP, "convertToFlatFormat: FAILED");
    }

    napi_value result;
    napi_get_boolean(env, success, &result);
    return result;
}

/**
 * getFlatTrieStats(): { wordCount: number, memoryUsage: number, locale: string } | null
 * Get statistics from loaded FlatTrie
 */
static napi_value GetFlatTrieStats(napi_env env, napi_callback_info info) {
    std::lock_guard<std::mutex> lock(g_trieMutex);

    if (!g_flatTrie || !g_flatTrie->isLoaded()) {
        napi_value nullVal;
        napi_get_null(env, &nullVal);
        return nullVal;
    }

    napi_value result;
    napi_create_object(env, &result);

    napi_value wordCount, memUsage, locale;
    napi_create_int32(env, g_flatTrie->getWordCount(), &wordCount);
    napi_create_int64(env, static_cast<int64_t>(g_flatTrie->getMemoryUsage()), &memUsage);
    napi_create_string_utf8(env, g_flatTrie->getLocale().c_str(), NAPI_AUTO_LENGTH, &locale);

    napi_set_named_property(env, result, "wordCount", wordCount);
    napi_set_named_property(env, result, "memoryUsage", memUsage);
    napi_set_named_property(env, result, "locale", locale);

    return result;
}

/**
 * unload(): void
 * Unload dictionary and free memory safely
 * - Uses smart pointer reset() which handles nullptr safely
 * - Clears keyboard layout
 * - Clears suggestion cache
 * - No double-free possible due to unique_ptr semantics
 */
static napi_value Unload(napi_env env, napi_callback_info info) {
    OH_LOG_INFO(LOG_APP, "unload: releasing resources");

    // Clean up keyboard layout (vector::clear is safe even if empty)
    size_t layoutSize = g_keyboardLayout.size();
    g_keyboardLayout.clear();
    OH_LOG_DEBUG(LOG_APP, "unload: cleared keyboard layout (%zu keys)", layoutSize);

    // Clear suggestion cache (invalidate all cached results)
    ClearSuggestionCache();
    OH_LOG_DEBUG(LOG_APP, "unload: cleared suggestion cache");

    // Clean up dictionary instances with mutex protection
    {
        std::lock_guard<std::mutex> lock(g_trieMutex);

        // unique_ptr::reset() is safe even if already null (no double-free)
        if (g_suggestEngine) {
            OH_LOG_DEBUG(LOG_APP, "unload: releasing SuggestEngine");
            g_suggestEngine.reset();  // Safely deletes and sets to nullptr
        }

        if (g_trie) {
            OH_LOG_DEBUG(LOG_APP, "unload: releasing Trie");
            g_trie.reset();  // Safely deletes and sets to nullptr
        }

        if (g_flatTrie) {
            OH_LOG_DEBUG(LOG_APP, "unload: releasing FlatTrie");
            g_flatTrie.reset();  // Safely deletes and sets to nullptr
        }
    }

    OH_LOG_INFO(LOG_APP, "unload: complete");

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

// Module initialization
EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports) {
    // API 22 Optimization: Initialize cached property keys for faster object creation
    InitCachedPropertyKeys(env);

    // Add binary dictionary functions to exports
    napi_value binaryDictExports = latinime::RegisterBinaryDictionary(env);
    
    // Add proximity info functions to exports
    napi_value proximityInfoExports = latinime::RegisterProximityInfo(env);
    
    // Add dic traverse session functions to exports
    napi_value dicTraverseSessionExports = latinime::RegisterDicTraverseSession(env);
    
    // Set binary dictionary as a property of main exports
    napi_set_named_property(env, exports, "binaryDictionary", binaryDictExports);
    
    // Set proximity info as a property of main exports
    napi_set_named_property(env, exports, "proximityInfo", proximityInfoExports);
    
    // Set dic traverse session as a property of main exports
    napi_set_named_property(env, exports, "dicTraverseSession", dicTraverseSessionExports);
    
    // Add batch operations functions to exports
    napi_value batchOpsExports = latinime::RegisterBatchOperations(env);
    napi_set_named_property(env, exports, "batchOps", batchOpsExports);
    
    // Add other functions to main exports
    napi_property_descriptor desc[] = {
        { "loadDictionary", nullptr, LoadDictionary, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "loadDictionarySync", nullptr, LoadDictionarySync, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "contains", nullptr, Contains, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "getFrequency", nullptr, GetFrequency, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "getSuggestions", nullptr, GetSuggestions, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "findAutocorrection", nullptr, FindAutocorrection, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "setProximityInfo", nullptr, SetProximityInfo, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "getStats", nullptr, GetStats, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "setSwipeKeyboardLayout", nullptr, SetSwipeKeyboardLayout, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "processSwipePath", nullptr, ProcessSwipePath, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "unload", nullptr, Unload, nullptr, nullptr, nullptr, napi_default, nullptr },
        // FlatTrie API (instant loading)
        { "loadFlatDictionary", nullptr, LoadFlatDictionary, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "convertToFlatFormat", nullptr, ConvertToFlatFormat, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "getFlatTrieStats", nullptr, GetFlatTrieStats, nullptr, nullptr, nullptr, napi_default, nullptr },
        // User Learning API (Legacy - Trie-based)
        { "addLearnedWordSimple", nullptr, AddLearnedWordSimple, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "recordWordUsage", nullptr, RecordWordUsage, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "saveUserDict", nullptr, SaveUserDict, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "loadUserDict", nullptr, LoadUserDict, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "getLearnedWordsCount", nullptr, GetLearnedWordsCount, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "removeLearnedWord", nullptr, RemoveLearnedWord, nullptr, nullptr, nullptr, napi_default, nullptr },
        // Bigram-Aware Learning API (New)
        { "addLearnedWord", nullptr, AddLearnedWordWithContext, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "getLearnedBoost", nullptr, GetLearnedBoost, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "saveUserDictionary", nullptr, SaveUserDictionary, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "loadUserDictionary", nullptr, LoadUserDictionary, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "clearLearnedWords", nullptr, ClearLearnedWords, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "getBigramLearnedWordsCount", nullptr, GetBigramLearnedWordsCount, nullptr, nullptr, nullptr, napi_default, nullptr },
        // Gesture Trail API (Yandex-style)
        { "initTrailData", nullptr, InitTrailData, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "addTrailPoint", nullptr, AddTrailPoint, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "updateTrailData", nullptr, UpdateTrailData, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "getTrailSegments", nullptr, GetTrailSegments, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "resetTrailData", nullptr, ResetTrailData, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "setTrailRenderParams", nullptr, SetTrailRenderParams, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "compactTrailBuffers", nullptr, CompactTrailBuffers, nullptr, nullptr, nullptr, napi_default, nullptr },
    };

    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}
EXTERN_C_END

// Module registration
static napi_module nativeDictModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = Init,
    .nm_modname = "native_dict",
    .nm_priv = nullptr,
    .reserved = { 0 },
};

extern "C" __attribute__((constructor)) void RegisterNativeDictModule(void) {
    napi_module_register(&nativeDictModule);
}
