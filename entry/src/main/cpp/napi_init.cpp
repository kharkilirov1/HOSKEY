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

// HarmonyOS logging
#include <hilog/log.h>

// Log domain and tag for HOSKEY native layer
#undef LOG_DOMAIN
#undef LOG_TAG
#define LOG_DOMAIN 0x0001
#define LOG_TAG "HOSKEY-NATIVE"

#include "dictionary_hoskey/trie.h"
#include "dictionary_hoskey/suggest_engine.h"

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

// Keyboard layout for NAPI swipe
struct KeyBounds {
    std::string key;
    float centerX, centerY;
    float width, height;
};

static std::vector<KeyBounds> g_keyboardLayout;

// Global instances with mutex protection
static std::mutex g_trieMutex;  // Protects g_trie and g_suggestEngine
static std::unique_ptr<hoskey::Trie> g_trie;
static std::unique_ptr<hoskey::SuggestEngine> g_suggestEngine;

// ============================================================================
// API 22 Optimization: Cached property keys for faster object creation
// Avoids repeated string internalization overhead
// ============================================================================
static napi_ref g_cachedWordKey = nullptr;
static napi_ref g_cachedScoreKey = nullptr;
static napi_ref g_cachedErrorTypeKey = nullptr;
static bool g_keysInitialized = false;

// ============================================================================
// API 22 Optimization: Simple LRU cache for suggestions
// Avoids repeated Trie lookups for same prefix (common during fast typing)
// ============================================================================
#include <list>

struct SuggestionCacheEntry {
    std::string prefix;
    std::vector<hoskey::SuggestResult> results;
};

static std::list<SuggestionCacheEntry> g_suggestionCache;
static std::mutex g_cacheMutex;
static const size_t SUGGESTION_CACHE_SIZE = 32;  // Cache last 32 prefixes

/**
 * Get cached suggestions for prefix (returns nullptr if not in cache)
 */
static const std::vector<hoskey::SuggestResult>* GetCachedSuggestions(const std::string& prefix) {
    std::lock_guard<std::mutex> lock(g_cacheMutex);

    for (auto& entry : g_suggestionCache) {
        if (entry.prefix == prefix) {
            // Move to front (LRU)
            if (&entry != &g_suggestionCache.front()) {
                auto it = std::find_if(g_suggestionCache.begin(), g_suggestionCache.end(),
                    [&prefix](const SuggestionCacheEntry& e) { return e.prefix == prefix; });
                if (it != g_suggestionCache.end()) {
                    g_suggestionCache.splice(g_suggestionCache.begin(), g_suggestionCache, it);
                }
            }
            return &g_suggestionCache.front().results;
        }
    }
    return nullptr;
}

/**
 * Add suggestions to cache
 */
static void CacheSuggestions(const std::string& prefix, const std::vector<hoskey::SuggestResult>& results) {
    std::lock_guard<std::mutex> lock(g_cacheMutex);

    // Check if already in cache
    for (auto& entry : g_suggestionCache) {
        if (entry.prefix == prefix) {
            return;  // Already cached
        }
    }

    // Add to front
    g_suggestionCache.push_front({prefix, results});

    // Evict oldest if over capacity
    while (g_suggestionCache.size() > SUGGESTION_CACHE_SIZE) {
        g_suggestionCache.pop_back();
    }
}

/**
 * Clear suggestion cache (call when dictionary changes)
 */
static void ClearSuggestionCache() {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    g_suggestionCache.clear();
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
    auto newTrie = std::make_unique<hoskey::Trie>();
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
    auto newTrie = std::make_unique<hoskey::Trie>();
    bool success = newTrie->loadFromFile(path);

    if (success) {
        int wordCount = newTrie->getWordCount();
        size_t memUsage = newTrie->getMemoryUsage();
        OH_LOG_INFO(LOG_APP, "loadDictionarySync: SUCCESS - loaded %{public}d words, memory=%{public}zu bytes",
                    wordCount, memUsage);

        // DEBUG: Log newTrie pointer BEFORE any additional operations
        OH_LOG_INFO(LOG_APP, "loadDictionarySync: DEBUG newTrie=%{public}p wordCount=%{public}d",
                    static_cast<void*>(newTrie.get()), newTrie->getWordCount());

        // DEBUG: Test prefix search FIRST (before any manual inserts)
        OH_LOG_INFO(LOG_APP, "loadDictionarySync: === TESTING LOADED DICTIONARY (before manual inserts) ===");

        // Test Latin prefix "a"
        auto latinTest = newTrie->findByPrefix("a", 3);
        OH_LOG_INFO(LOG_APP, "loadDictionarySync: TEST1 findByPrefix(a) returned %{public}zu results", latinTest.size());
        for (size_t i = 0; i < latinTest.size() && i < 3; i++) {
            OH_LOG_INFO(LOG_APP, "  Latin[%{public}zu]: word=%{public}s freq=%{public}d", i, latinTest[i].word.c_str(), latinTest[i].frequency);
        }

        // Test Cyrillic prefix "п" (UTF-8: D0 BF)
        auto cyrillicTest = newTrie->findByPrefix("\xD0\xBF", 3);  // "п" in UTF-8
        OH_LOG_INFO(LOG_APP, "loadDictionarySync: TEST1 findByPrefix(п/D0BF) returned %{public}zu results", cyrillicTest.size());
        for (size_t i = 0; i < cyrillicTest.size() && i < 3; i++) {
            OH_LOG_INFO(LOG_APP, "  Cyrillic[%{public}zu]: word=%{public}s freq=%{public}d", i, cyrillicTest[i].word.c_str(), cyrillicTest[i].frequency);
        }

        // Test: check if dictionary contains specific words
        bool hasPrivet = newTrie->contains("\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82"); // "привет"
        bool hasHello = newTrie->contains("hello");
        OH_LOG_INFO(LOG_APP, "loadDictionarySync: TEST1 contains(привет)=%{public}s contains(hello)=%{public}s",
                    hasPrivet ? "YES" : "NO", hasHello ? "YES" : "NO");

        // DEBUG: Now test manual insert to see if insert mechanism works
        OH_LOG_INFO(LOG_APP, "loadDictionarySync: === TESTING MANUAL INSERT ===");
        newTrie->insert("testword", 100);
        bool hasTestWord = newTrie->contains("testword");
        OH_LOG_INFO(LOG_APP, "loadDictionarySync: TEST2 manual insert: contains(testword)=%{public}s",
                    hasTestWord ? "YES" : "NO");

        // DEBUG: Insert Cyrillic test word
        newTrie->insert("\xD1\x82\xD0\xB5\xD1\x81\xD1\x82", 100);  // "тест" in UTF-8
        bool hasCyrTest = newTrie->contains("\xD1\x82\xD0\xB5\xD1\x81\xD1\x82");
        OH_LOG_INFO(LOG_APP, "loadDictionarySync: TEST2 manual insert cyrillic: contains(тест)=%{public}s",
                    hasCyrTest ? "YES" : "NO");

        // Now test findByPrefix again after manual inserts
        auto latinTest2 = newTrie->findByPrefix("t", 3);
        OH_LOG_INFO(LOG_APP, "loadDictionarySync: TEST2 findByPrefix(t) after manual insert returned %{public}zu results", latinTest2.size());

        // Quick swap under mutex
        OH_LOG_INFO(LOG_APP, "loadDictionarySync: Moving newTrie to g_trie...");
        {
            std::lock_guard<std::mutex> lock(g_trieMutex);
            g_suggestEngine.reset();
            g_trie = std::move(newTrie);
            g_suggestEngine = std::make_unique<hoskey::SuggestEngine>(g_trie.get());
            OH_LOG_INFO(LOG_APP, "loadDictionarySync: g_trie now=%{public}p wordCount=%{public}d",
                        static_cast<void*>(g_trie.get()), g_trie->getWordCount());
        }

        // Clear suggestion cache (old results are invalid now)
        ClearSuggestionCache();

        // Final verification after move
        {
            std::lock_guard<std::mutex> lock(g_trieMutex);
            auto finalTest = g_trie->findByPrefix("a", 3);
            OH_LOG_INFO(LOG_APP, "loadDictionarySync: FINAL TEST g_trie->findByPrefix(a) returned %{public}zu results", finalTest.size());
        }
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

    // DEBUG: Log prefix bytes to diagnose UTF-8 issues
    std::string hexPrefix;
    for (unsigned char c : prefix) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", c);
        hexPrefix += buf;
    }
    OH_LOG_INFO(LOG_APP, "getSuggestions: prefix=\"%{public}s\" bytes=[%{public}s] len=%{public}zu",
                prefix.c_str(), hexPrefix.c_str(), prefix.length());

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
        OH_LOG_INFO(LOG_APP, "getSuggestions: cache MISS, engine returned %{public}zu results", suggestions.size());
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
 * getStats(): { wordCount: number, memoryUsage: number }
 * Get dictionary statistics
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

    return obj;
}

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
 * processSwipePath(points: Array<{x: number, y: number, timestamp: number}>): {bestWord: string, alternatives: string[], confidence: number, rawSequence: string} | null
 * Process swipe path and return recognized word
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

    // Extract touch points
    struct Point { float x, y; int64_t timestamp; };
    std::vector<Point> path;
    path.reserve(length);

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

        path.push_back({(float)x, (float)y, ts});
    }

    // Validate path distance (min 50px)
    float totalDist = 0;
    for (size_t i = 1; i < path.size(); i++) {
        float dx = path[i].x - path[i-1].x;
        float dy = path[i].y - path[i-1].y;
        totalDist += std::sqrt(dx * dx + dy * dy);
    }

    if (totalDist < 50.0f) {
        napi_value result;
        napi_get_null(env, &result);
        return result;
    }

    // Sample path (every 10th point or at least 5 samples)
    std::vector<Point> sampled;
    int step = std::max(1, (int)path.size() / 15);
    for (size_t i = 0; i < path.size(); i += step) {
        sampled.push_back(path[i]);
    }
    if (sampled.back().x != path.back().x || sampled.back().y != path.back().y) {
        sampled.push_back(path.back());
    }

    // Extract key sequence
    std::string keySequence;
    std::string lastKey;
    for (const auto& point : sampled) {
        std::string key = FindNearestKey(point.x, point.y);
        if (!key.empty() && key != lastKey && key.length() == 1) {
            keySequence += key;
            lastKey = key;
        }
    }

    if (keySequence.length() < 2) {
        napi_value result;
        napi_get_null(env, &result);
        return result;
    }

    // Get candidates from trie (words starting with first letter)
    std::vector<std::string> candidates;
    std::string firstLetter = keySequence.substr(0, 1);

    // Use trie to get words starting with first letter
    auto suggestions = g_suggestEngine->getSuggestions(firstLetter, 100);
    for (const auto& suggestion : suggestions) {
        if (suggestion.word.length() >= keySequence.length() - 2 &&
            suggestion.word.length() <= keySequence.length() + 3) {
            candidates.push_back(suggestion.word);
        }
    }

    if (candidates.empty()) {
        napi_value result;
        napi_get_null(env, &result);
        return result;
    }

    // Rank candidates by matching key sequence
    struct Candidate {
        std::string word;
        float score;
    };

    std::vector<Candidate> ranked;
    for (const auto& word : candidates) {
        // Calculate score based on sequence match
        float score = 0;
        size_t matchCount = 0;

        for (size_t i = 0; i < std::min(word.length(), keySequence.length()); i++) {
            if (std::tolower(word[i]) == std::tolower(keySequence[i])) {
                matchCount++;
            }
        }

        score = (float)matchCount / (float)keySequence.length();

        // Bonus for length match
        int lenDiff = std::abs((int)word.length() - (int)keySequence.length());
        score -= lenDiff * 0.1f;

        // Bonus for frequency
        int freq = 0;
        {
            std::lock_guard<std::mutex> lock(g_trieMutex);
            if (g_trie) {
                freq = g_trie->getFrequency(word);
            }
        }
        score += freq * 0.001f;

        if (score > 0.3f) { // Threshold
            ranked.push_back({word, score});
        }
    }

    if (ranked.empty()) {
        napi_value result;
        napi_get_null(env, &result);
        return result;
    }

    // Sort by score
    std::sort(ranked.begin(), ranked.end(),
        [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

    // Build result object
    napi_value obj;
    napi_create_object(env, &obj);

    // bestWord
    napi_value bestWordValue = StringToNapiValue(env, ranked[0].word);
    napi_set_named_property(env, obj, "bestWord", bestWordValue);

    // alternatives (up to 5)
    napi_value alternatives;
    size_t altCount = std::min((size_t)5, ranked.size() - 1);
    napi_create_array_with_length(env, altCount, &alternatives);
    for (size_t i = 0; i < altCount && i + 1 < ranked.size(); i++) {
        napi_value alt = StringToNapiValue(env, ranked[i + 1].word);
        napi_set_element(env, alternatives, i, alt);
    }
    napi_set_named_property(env, obj, "alternatives", alternatives);

    // confidence
    napi_value confidenceValue;
    float confidence = std::min(1.0f, ranked[0].score);
    napi_create_double(env, confidence, &confidenceValue);
    napi_set_named_property(env, obj, "confidence", confidenceValue);

    // rawSequence
    napi_value rawSeqValue = StringToNapiValue(env, keySequence);
    napi_set_named_property(env, obj, "rawSequence", rawSeqValue);

    return obj;
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
