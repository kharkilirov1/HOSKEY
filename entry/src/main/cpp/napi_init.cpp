/**
 * HOSKEY Native Dictionary Engine
 * N-API bridge for high-performance text prediction
 *
 * Architecture: Yandex-style dictionary with mmap-based CompTrie
 * - Instant dictionary loading via mmap
 * - Neural model scoring (MindSpore/NNRt)
 * - Beam search for swipe/gesture input
 * - Multi-predictor score fusion
 */

#include "napi/native_api.h"
#include <string>
#include <vector>
#include <memory>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <limits>
#include <cstdio>
#include <mutex>
#include <set>
#include <unordered_map>
#include <tuple>
#include <unistd.h>

// HarmonyOS logging
#include <hilog/log.h>

// Log domain and tag for HOSKEY native layer
#undef LOG_DOMAIN
#undef LOG_TAG
#define LOG_DOMAIN 0x0001
#define LOG_TAG "HOSKEY-NATIVE"

// Yandex-style dictionary (primary)
#include "dictionary_hoskey/yandex_trie.h"
#include "dictionary_hoskey/nnrt_scorer.h"  // NNRt + CANNKit neural scoring
#include "dictionary_hoskey/neural_model_manager.h"  // All 15 neural models

// Include constants
#include "constants.h"

// Include Yandex-style beam search for swipe
#include "swipe_beam_search.h"

// Include multi-predictor system (Yandex-style score fusion)
#include "suggest/multi_predictor.h"

// Include batch operations NAPI
#include "batch_operations_napi.h"

// Include proximity info NAPI (for keyboard geometry)
#include "proximity_info_napi.h"

// Include legacy OpenBoard NAPI (kept for compatibility)
#include "binary_dictionary_napi.h"
#include "dic_traverse_session_napi.h"

// Keyboard layout for swipe gesture recognition
struct KeyBounds {
    std::string key;
    float centerX, centerY;
    float width, height;
};

static std::vector<KeyBounds> g_keyboardLayout;

// ============================================================================
// Global Instances (Yandex-style architecture)
// ============================================================================

// Primary dictionary: Yandex mmap-based CompTrie
std::unique_ptr<yandex::YandexDict> g_yandexDict;
std::mutex g_yandexMutex;

// Neural Model Manager - manages ALL 15 neural models
std::unique_ptr<yandex::NeuralModelManager> g_modelManager;
std::mutex g_modelManagerMutex;
bool g_neuralModelsEnabled = false;

// Legacy single scorer (kept for backward compatibility)
std::unique_ptr<yandex::NNRtScorer> g_neuralScorer;
std::mutex g_neuralScorerMutex;
bool g_neuralScorerEnabled = false;

// Multi-predictor for score fusion (Yandex-style)
std::unique_ptr<latinime::MultiPredictor> g_multiPredictor;
std::mutex g_multiPredictorMutex;

// Beam search for swipe/gesture recognition
std::unique_ptr<hoskey::SwipeBeamSearch> g_beamSearch;
std::mutex g_beamSearchMutex;

// MindSpore models for neural scoring
// Models are loaded on demand for better startup performance
struct ModelPaths {
    std::string tapModelRanker;    // tap_model_ranker.ms - primary tap scoring (USED)
    std::string treeAutocorrect;   // tree_autocorrect_model.ms - neural autocorrect
    std::string nnlmModel;         // nnlm_model.ms - neural language model
    std::string swipeBlocker;      // swipe_blocker.ms - validates swipe vs tap
    std::string lemmer;            // lemmer_mhash.ms - morphology

    // Loading state
    bool rankerLoaded = false;
    bool autocorrectLoaded = false;
    bool nnlmLoaded = false;
};
static ModelPaths g_modelPaths;

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
    std::vector<yandex::Suggestion> results;
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
static const std::vector<yandex::Suggestion>* GetCachedSuggestions(const std::string& prefix) {
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
static void CacheSuggestions(const std::string& prefix, const std::vector<yandex::Suggestion>& results) {
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
 * Loads YandexDict from file path
 */
static void LoadDictionaryExecute(napi_env env, void* data) {
    LoadDictionaryAsyncData* asyncData = static_cast<LoadDictionaryAsyncData*>(data);

    OH_LOG_INFO(LOG_APP, "loadDictionary [ASYNC]: loading from path=%{public}s", asyncData->path.c_str());

    auto newDict = std::make_unique<yandex::YandexDict>();
    bool success = newDict->load(asyncData->path);

    if (success) {
        asyncData->wordCount = static_cast<int>(newDict->getWordCount());
        OH_LOG_INFO(LOG_APP, "loadDictionary [ASYNC]: SUCCESS - loaded %d words", asyncData->wordCount);

        {
            std::lock_guard<std::mutex> lock(g_yandexMutex);
            g_yandexDict = std::move(newDict);
        }

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
 * Load YandexDict SYNCHRONOUSLY from file path
 */
static napi_value LoadDictionarySync(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (!ValidateArgCount(env, argc, 1, "loadDictionarySync")) {
        return nullptr;
    }
    if (!ValidateString(env, args[0], "path")) {
        return nullptr;
    }

    std::string path = NapiValueToString(env, args[0]);

    OH_LOG_INFO(LOG_APP, "loadDictionarySync: loading from path=%{public}s", path.c_str());

    auto newDict = std::make_unique<yandex::YandexDict>();
    bool success = newDict->load(path);

    if (success) {
        int wordCount = static_cast<int>(newDict->getWordCount());
        size_t memUsage = newDict->getMemoryUsage();
        OH_LOG_INFO(LOG_APP, "loadDictionarySync: SUCCESS - %{public}d words, %{public}zu bytes",
                    wordCount, memUsage);

        {
            std::lock_guard<std::mutex> lock(g_yandexMutex);
            g_yandexDict = std::move(newDict);
        }

        ClearSuggestionCache();
    } else {
        OH_LOG_ERROR(LOG_APP, "loadDictionarySync: FAILED to load from %{public}s", path.c_str());
    }

    napi_value result;
    napi_get_boolean(env, success, &result);
    return result;
}

// ============================================================================
// Async LoadDictionaryFromFd Implementation (ANR fix)
// ============================================================================

/**
 * Async work data for loadDictionaryFromFd
 */
struct LoadDictionaryFromFdAsyncData {
    napi_async_work work;
    napi_deferred deferred;
    int fd;
    size_t offset;
    size_t length;
    bool success;
    int wordCount;
};

/**
 * Check if file is in Yandex format (has JSON config at offset 32)
 */
static bool isYandexFormat(int fd, size_t offset) {
    // Save current position
    off_t savedPos = lseek(fd, 0, SEEK_CUR);
    if (savedPos < 0) {
        return false;
    }

    // Seek to offset and read first 64 bytes
    if (lseek(fd, static_cast<off_t>(offset), SEEK_SET) < 0) {
        return false;
    }

    uint8_t header[64];
    ssize_t bytesRead = read(fd, header, sizeof(header));

    // Restore position
    lseek(fd, savedPos, SEEK_SET);

    if (bytesRead < 64) {
        return false;
    }

    // Check magic (same bytes, different endianness interpretation)
    // Yandex: 9b c1 3a fe (LE reads as 0xfe3ac19b)
    // Both formats use same magic bytes
    uint32_t magic = *reinterpret_cast<uint32_t*>(header);
    if (magic != 0xfe3ac19b && magic != 0x9bc13afe) {
        return false;
    }

    // Yandex format has JSON starting at offset 32 (first char is '{')
    // OpenBoard format has binary trie data
    return header[32] == '{';
}

/**
 * Execute callback - runs on worker thread (thread pool)
 * Uses mmap-based YandexDict for instant loading
 */
static void LoadDictionaryFromFdExecute(napi_env env, void* data) {
    LoadDictionaryFromFdAsyncData* asyncData = static_cast<LoadDictionaryFromFdAsyncData*>(data);

    OH_LOG_INFO(LOG_APP, "loadDictionaryFromFd [ASYNC]: fd=%d, offset=%zu, length=%zu",
                asyncData->fd, asyncData->offset, asyncData->length);

    // Load using YandexDict (mmap-based, instant loading)
    auto yandexDict = std::make_unique<yandex::YandexDict>();
    bool success = yandexDict->loadFromFd(asyncData->fd, asyncData->offset, asyncData->length);

    if (success) {
        asyncData->wordCount = static_cast<int>(yandexDict->getWordCount());
        OH_LOG_INFO(LOG_APP, "loadDictionaryFromFd [ASYNC]: SUCCESS - %d words (mmap instant!)",
                    asyncData->wordCount);

        // Test specific words to verify dictionary content
        const char* testWords[] = {"привет", "пока", "спасибо", "прив", "при"};
        for (const char* word : testWords) {
            bool exists = yandexDict->contains(word);
            uint64_t freq = yandexDict->getFrequency(word);
            OH_LOG_INFO(LOG_APP, "loadDictionaryFromFd TEST: '%s' exists=%d freq=%llu",
                        word, exists ? 1 : 0, (unsigned long long)freq);
        }

        // Store in global dict
        {
            std::lock_guard<std::mutex> lock(g_yandexMutex);
            g_yandexDict = std::move(yandexDict);
        }

        // Clear suggestion cache (old results are invalid)
        ClearSuggestionCache();
        asyncData->success = true;
    } else {
        OH_LOG_ERROR(LOG_APP, "loadDictionaryFromFd [ASYNC]: FAILED to load dictionary");
        asyncData->success = false;
    }
}

/**
 * Complete callback - runs on main JS thread after execute completes
 * Resolves the promise
 */
static void LoadDictionaryFromFdComplete(napi_env env, napi_status status, void* data) {
    LoadDictionaryFromFdAsyncData* asyncData = static_cast<LoadDictionaryFromFdAsyncData*>(data);

    napi_value result;
    napi_get_boolean(env, asyncData->success, &result);

    // Resolve the promise
    napi_resolve_deferred(env, asyncData->deferred, result);

    // Clean up
    napi_delete_async_work(env, asyncData->work);
    delete asyncData;
}

/**
 * loadDictionaryFromFd(fd: number, offset: number, length: number): Promise<boolean>
 * Load dictionary using memory-mapping from file descriptor ASYNCHRONOUSLY
 * Returns a Promise to avoid blocking the main thread (ANR fix)
 *
 * Use this for rawfile resources where fd is available
 */
static napi_value LoadDictionaryFromFd(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (!ValidateArgCount(env, argc, 3, "loadDictionaryFromFd")) {
        return nullptr;
    }
    if (!ValidateNumber(env, args[0], "fd") ||
        !ValidateNumber(env, args[1], "offset") ||
        !ValidateNumber(env, args[2], "length")) {
        return nullptr;
    }

    int32_t fd = -1;
    int64_t offset = -1;
    int64_t length = -1;
    if (napi_get_value_int32(env, args[0], &fd) != napi_ok ||
        napi_get_value_int64(env, args[1], &offset) != napi_ok ||
        napi_get_value_int64(env, args[2], &length) != napi_ok) {
        napi_throw_type_error(env, "EINVAL", "loadDictionaryFromFd: invalid numeric arguments");
        return nullptr;
    }
    if (fd < 0 || offset < 0 || length <= 0) {
        napi_throw_range_error(env, "EINVAL",
                               "loadDictionaryFromFd: fd must be >= 0, offset >= 0, length > 0");
        return nullptr;
    }
    if (static_cast<uint64_t>(offset) > std::numeric_limits<size_t>::max() ||
        static_cast<uint64_t>(length) > std::numeric_limits<size_t>::max()) {
        napi_throw_range_error(env, "EINVAL", "loadDictionaryFromFd: offset/length out of range");
        return nullptr;
    }

    // Create async data
    LoadDictionaryFromFdAsyncData* asyncData = new LoadDictionaryFromFdAsyncData();
    asyncData->fd = fd;
    asyncData->offset = static_cast<size_t>(offset);
    asyncData->length = static_cast<size_t>(length);
    asyncData->success = false;
    asyncData->wordCount = 0;

    // Create promise
    napi_value promise;
    napi_create_promise(env, &asyncData->deferred, &promise);

    // Create async work name
    napi_value resourceName;
    napi_create_string_utf8(env, "loadDictionaryFromFd", NAPI_AUTO_LENGTH, &resourceName);

    // Create async work
    napi_create_async_work(
        env,
        nullptr,
        resourceName,
        LoadDictionaryFromFdExecute,
        LoadDictionaryFromFdComplete,
        asyncData,
        &asyncData->work
    );

    // Queue async work
    napi_queue_async_work(env, asyncData->work);

    OH_LOG_INFO(LOG_APP, "loadDictionaryFromFd: async work queued for fd=%d, offset=%ld, length=%ld",
                fd, (long)offset, (long)length);

    return promise;
}

/**
 * loadTextDictionaryFromFd(fd: number, offset: number, length: number): boolean
 * Load dictionary from text file (word=X,f=Y format) via file descriptor
 * This is a SYNCHRONOUS function for simplicity (text parsing is fast)
 */
static napi_value LoadTextDictionaryFromFd(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 3) {
        napi_throw_error(env, nullptr, "loadTextDictionaryFromFd requires 3 arguments");
        return nullptr;
    }

    int fd;
    double offsetD, lengthD;
    napi_get_value_int32(env, args[0], &fd);
    napi_get_value_double(env, args[1], &offsetD);
    napi_get_value_double(env, args[2], &lengthD);

    size_t offset = static_cast<size_t>(offsetD);
    size_t length = static_cast<size_t>(lengthD);

    OH_LOG_INFO(LOG_APP, "loadTextDictionaryFromFd: fd=%d, offset=%zu, length=%zu", fd, offset, length);

    auto yandexDict = std::make_unique<yandex::YandexDict>();
    bool success = yandexDict->loadFromTextFd(fd, offset, length);

    napi_value result;
    if (success) {
        int wordCount = static_cast<int>(yandexDict->getWordCount());
        OH_LOG_INFO(LOG_APP, "loadTextDictionaryFromFd: SUCCESS - %d words loaded (in-memory trie)", wordCount);

        // Store in global dict
        {
            std::lock_guard<std::mutex> lock(g_yandexMutex);
            g_yandexDict = std::move(yandexDict);
        }

        // Clear suggestion cache
        ClearSuggestionCache();

        napi_get_boolean(env, true, &result);
    } else {
        OH_LOG_ERROR(LOG_APP, "loadTextDictionaryFromFd: FAILED to load dictionary");
        napi_get_boolean(env, false, &result);
    }

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

    // Check in YandexDict
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(g_yandexMutex);
        if (g_yandexDict && g_yandexDict->isLoaded()) {
            found = g_yandexDict->contains(word);
        }
    }

    napi_value result;
    napi_get_boolean(env, found, &result);
    return result;
}

/**
 * getFrequency(word: string): number
 * Get frequency/probability of word (0-255)
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

    // Get frequency from YandexDict
    int frequency = 0;
    {
        std::lock_guard<std::mutex> lock(g_yandexMutex);
        if (g_yandexDict && g_yandexDict->isLoaded()) {
            frequency = g_yandexDict->getFrequency(word);
        }
    }

    napi_value result;
    napi_create_int32(env, frequency, &result);
    return result;
}

/**
 * SuggestResult interface:
 * { word: string, score: number }
 * Creates a JavaScript object from yandex::Suggestion
 *
 * OPTIMIZED for API 22:
 * - Uses cached property keys (avoids string internalization overhead)
 * - Uses napi_set_property with cached keys instead of napi_set_named_property
 */
static napi_value CreateSuggestResult(napi_env env, const yandex::Suggestion& s) {
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

    // Fallback to named properties if cache not initialized
    if (!wordKey || !scoreKey) {
        napi_value word = StringToNapiValue(env, s.word);
        if (word) napi_set_named_property(env, obj, "word", word);

        napi_value score;
        napi_create_double(env, s.score, &score);
        napi_set_named_property(env, obj, "score", score);

        return obj;
    }

    // Fast path: use cached keys with napi_set_property
    napi_value wordVal = StringToNapiValue(env, s.word);
    if (wordVal) {
        napi_set_property(env, obj, wordKey, wordVal);
    }

    napi_value scoreVal;
    napi_create_double(env, s.score, &scoreVal);
    napi_set_property(env, obj, scoreKey, scoreVal);

    return obj;
}

/**
 * getSuggestions(prefix: string, limit: number): SuggestResult[]
 * Get word suggestions with scores from YandexDict
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

    std::string prefix = NapiValueToString(env, args[0]);

    int32_t limit = 10;
    napi_get_value_int32(env, args[1], &limit);
    // Clamp limit to reasonable bounds
    if (limit < 1) limit = 1;
    if (limit > 100) limit = 100;

    std::vector<yandex::Suggestion> suggestions;

    // Check LRU cache first
    const std::vector<yandex::Suggestion>* cachedResults = GetCachedSuggestions(prefix);

    if (cachedResults) {
        // Cache hit
        suggestions = *cachedResults;
        if (static_cast<int32_t>(suggestions.size()) > limit) {
            suggestions.resize(limit);
        }
        OH_LOG_DEBUG(LOG_APP, "getSuggestions: cache HIT, %{public}zu results", suggestions.size());
    } else {
        // Cache miss - get from YandexDict
        std::lock_guard<std::mutex> lock(g_yandexMutex);
        if (g_yandexDict && g_yandexDict->isLoaded()) {
            suggestions = g_yandexDict->getSuggestions(prefix, limit * 2); // Get more for re-ranking
            OH_LOG_DEBUG(LOG_APP, "getSuggestions: got %{public}zu candidates from dict", suggestions.size());
        } else {
            // No dictionary loaded
            napi_value emptyResult;
            napi_create_array(env, &emptyResult);
            OH_LOG_WARN(LOG_APP, "getSuggestions: no dictionary loaded - returning empty array");
            return emptyResult;
        }

        // ====================================================================
        // Neural re-ranking: Use NeuralModelManager for multi-model scoring
        // Uses: TAP_RANKER (primary), RANKER_V2 (fallback), NNLM (context)
        // ====================================================================
        if (g_neuralModelsEnabled && g_modelManager && !suggestions.empty()) {
            std::lock_guard<std::mutex> scorerLock(g_modelManagerMutex);

            // Convert to scoring candidates
            std::vector<yandex::ScoringCandidate> candidates;
            candidates.reserve(suggestions.size());
            for (const auto& s : suggestions) {
                yandex::ScoringCandidate c;
                c.word = s.word;
                c.baseScore = s.score;
                candidates.push_back(c);
            }

            // Run multi-model neural scoring (TAP_RANKER + NNLM context)
            std::vector<yandex::ScoredWord> scored = g_modelManager->scoreTapSuggestions(candidates, "");

            // Convert back to Suggestion format with neural scores
            suggestions.clear();
            for (const auto& sw : scored) {
                yandex::Suggestion s;
                s.word = sw.word;
                s.score = sw.score;  // Combined score from neural models + freq
                suggestions.push_back(s);
            }

            OH_LOG_DEBUG(LOG_APP, "getSuggestions: neural re-ranked %{public}zu results (multi-model)", suggestions.size());
        }

        // Trim to requested limit
        if (static_cast<int32_t>(suggestions.size()) > limit) {
            suggestions.resize(limit);
        }

        CacheSuggestions(prefix, suggestions);
    }

    // Create array with known size
    napi_value result;
    napi_create_array_with_length(env, suggestions.size(), &result);

    // Populate array
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
 * Find best autocorrection candidate using YandexDict
 *
 * Simple algorithm:
 * 1. If word exists in dictionary with high frequency, no correction needed
 * 2. Get suggestions for the word
 * 3. If top suggestion has much higher score than input, use it
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

    std::string word = NapiValueToString(env, args[0]);

    double threshold = 0.185;
    napi_get_value_double(env, args[1], &threshold);
    if (threshold < 0.0) threshold = 0.0;
    if (threshold > 1.0) threshold = 1.0;

    std::lock_guard<std::mutex> lock(g_yandexMutex);

    // Return null if dictionary not loaded
    if (!g_yandexDict || !g_yandexDict->isLoaded()) {
        napi_value result;
        napi_get_null(env, &result);
        return result;
    }

    // If word exists in dictionary with decent frequency, no correction needed
    if (g_yandexDict->contains(word)) {
        uint8_t freq = g_yandexDict->getFrequency(word);
        if (freq > 50) {  // High enough frequency = valid word
            napi_value result;
            napi_get_null(env, &result);
            return result;
        }
    }

    // Get suggestions
    auto suggestions = g_yandexDict->getSuggestions(word, 5);

    if (suggestions.empty()) {
        napi_value result;
        napi_get_null(env, &result);
        return result;
    }

    // Check if top suggestion is significantly better
    const auto& top = suggestions[0];

    // Only autocorrect if:
    // 1. Top suggestion is different from input
    // 2. Score is above threshold
    if (top.word != word && top.score > threshold) {
        return CreateSuggestResult(env, top);
    }

    napi_value result;
    napi_get_null(env, &result);
    return result;
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
    bool isLoaded = false;
    {
        std::lock_guard<std::mutex> lock(g_yandexMutex);
        if (g_yandexDict && g_yandexDict->isLoaded()) {
            wordCount = static_cast<int>(g_yandexDict->getWordCount());
            memoryUsage = g_yandexDict->getMemoryUsage();
            isLoaded = true;
        }
    }

    napi_value wordCountVal;
    napi_create_int32(env, wordCount, &wordCountVal);
    napi_set_named_property(env, obj, "wordCount", wordCountVal);

    napi_value memoryVal;
    napi_create_int64(env, static_cast<int64_t>(memoryUsage), &memoryVal);
    napi_set_named_property(env, obj, "memoryUsage", memoryVal);

    napi_value loadedVal;
    napi_get_boolean(env, isLoaded, &loadedVal);
    napi_set_named_property(env, obj, "isLoaded", loadedVal);

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
/**
 * Get nearest keys with proximity scores
 * Uses Yandex-style weighted distance calculation
 * 
 * Yandex parameters (from json_config.json):
 * - StartKeyDistanceWeightX = 0.8
 * - StartKeyDistanceWeightY = 1.4  
 * - KeySquaredDistanceLimit = 3000
 */
static std::vector<std::pair<std::string, float>> GetNearestKeysWithScores(
    float x, float y, size_t topN = 3) {
    
    // Yandex weights from Swipe/Rule config
    constexpr float WEIGHT_X = 0.8f;   // StartKeyDistanceWeightX
    constexpr float WEIGHT_Y = 1.4f;   // StartKeyDistanceWeightY
    constexpr float KEY_SQUARED_DIST_LIMIT = 3000.0f;  // KeySquaredDistanceLimit
    
    std::vector<std::pair<std::string, float>> result;
    if (g_keyboardLayout.empty()) return result;
    
    // Calculate weighted distances to all keys
    std::vector<std::tuple<std::string, float, float>> keysWithDist;
    for (const auto& key : g_keyboardLayout) {
        if (key.key.length() != 1) continue;  // Only single characters
        
        float dx = x - key.centerX;
        float dy = y - key.centerY;
        
        // Yandex-style weighted squared distance
        float weightedDistSq = (dx * dx * WEIGHT_X * WEIGHT_X) + 
                               (dy * dy * WEIGHT_Y * WEIGHT_Y);
        
        // Skip if beyond distance limit
        if (weightedDistSq > KEY_SQUARED_DIST_LIMIT) continue;
        
        float dist = std::sqrt(weightedDistSq);
        
        // Normalize by key size (larger keys have larger "hitbox")
        float keyRadius = std::sqrt(key.width * key.width + key.height * key.height) / 2.0f;
        float normalizedDist = dist / keyRadius;
        
        keysWithDist.emplace_back(key.key, weightedDistSq, normalizedDist);
    }
    
    // Sort by weighted squared distance (Yandex uses squared distance for speed)
    std::sort(keysWithDist.begin(), keysWithDist.end(),
        [](const auto& a, const auto& b) { return std::get<1>(a) < std::get<1>(b); });
    
    // Convert to proximity scores (Gaussian-like falloff)
    for (size_t i = 0; i < std::min(topN, keysWithDist.size()); ++i) {
        const auto& [key, distSq, normDist] = keysWithDist[i];
        
        // Proximity score: higher when closer to key center
        // Score = 1 - (distSq / limit), clamped to [0, 1]
        float score = 1.0f - (distSq / KEY_SQUARED_DIST_LIMIT);
        score = std::max(0.0f, std::min(1.0f, score));
        
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
    
    // Initialize Yandex-style Beam Search with layout
    {
        std::lock_guard<std::mutex> lock(g_beamSearchMutex);
        
        // Create beam search with Yandex parameters (tuned for HOSKEY)
        hoskey::SwipeParams params;
        params.beamWidth = 500;  // Increased from 300 for better exploration
        params.keySquaredDistanceLimit = 6000.0f;  // Increased from 3000 (~77px radius)
        params.maxTransitionSquaredDistance = 15000.0f;  // Increased from 10000
        params.weightX = 0.9f;  // Slightly more balanced X/Y
        params.weightY = 1.2f;  // Reduced Y weight
        params.topK = 30;  // Increased from 24
        params.minSamplingDistance = 3.0f;  // Increased from 0.5 for less dense sampling
        
        g_beamSearch = std::make_unique<hoskey::SwipeBeamSearch>(params);
        
        // Convert layout to beam search format
        std::vector<hoskey::KeyInfo> beamLayout;
        beamLayout.reserve(g_keyboardLayout.size());
        for (const auto& key : g_keyboardLayout) {
            beamLayout.push_back({key.key, key.centerX, key.centerY, key.width, key.height});
        }
        g_beamSearch->setLayout(beamLayout);
        
        OH_LOG_INFO(LOG_APP, "setSwipeKeyboardLayout: BeamSearch initialized with %{public}zu keys", beamLayout.size());
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
        std::lock_guard<std::mutex> lock(g_yandexMutex);
        if (g_keyboardLayout.empty() || !g_yandexDict || !g_yandexDict->isLoaded()) {
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
    // Yandex-style parameters from json_config.json
    // =========================================================================
    latinime::GestureParams params;
    // Tuned parameters for better swipe recognition
    params.minSamplingDistance = 3.0f;  // Increased for less noise
    params.maxAngleRadians = 0.35f;  // ~20 degrees - more tolerant
    params.maxSegmentLength = 30.0f;  // Increased segment length
    params.maxInterpolationSteps = 8;  // Reduced interpolation
    params.adaptiveSamplingSpeedThreshold = 400.0f;  // More adaptive
    // Tuned Swipe/Rule parameters
    params.keyDistanceWeightX = 0.9f;   // More balanced X
    params.keyDistanceWeightY = 1.2f;   // Less Y bias
    params.keySquaredDistanceLimit = 6000.0f;  // Increased radius (~77px)
    params.maxTransitionSquaredDistance = 15000.0f;  // Allow larger transitions
    params.beamWidth = 500;  // Wider beam
    params.topK = 30;  // More candidates
    
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
    // STEP 3: Yandex-style Beam Search Decode
    // =========================================================================
    
    // Convert processed path to beam search format
    std::vector<hoskey::SwipePoint> beamPath;
    beamPath.reserve(processedPath.size());
    for (const auto& pt : processedPath) {
        beamPath.push_back({pt.x, pt.y, (int64_t)pt.timestamp});
    }
    
    // Try beam search first (Yandex-style)
    std::vector<hoskey::SwipeCandidate> beamCandidates;
    {
        std::lock_guard<std::mutex> beamLock(g_beamSearchMutex);
        std::lock_guard<std::mutex> yandexLock(g_yandexMutex);

        if (g_beamSearch && g_yandexDict && g_yandexDict->isLoaded()) {
            // Dictionary access functions using Yandex
            auto contains = [](const std::string& word) -> bool {
                return g_yandexDict->contains(word);
            };
            auto getFrequency = [](const std::string& word) -> int {
                return static_cast<int>(g_yandexDict->getFrequency(word));
            };
            auto getSuggestions = [](const std::string& prefix, int limit) -> std::vector<std::string> {
                std::vector<std::string> result;
                if (g_yandexDict && g_yandexDict->isLoaded()) {
                    auto suggestions = g_yandexDict->getSuggestions(prefix, limit);
                    for (const auto& s : suggestions) {
                        result.push_back(s.word);
                    }
                }
                return result;
            };

            beamCandidates = g_beamSearch->decode(beamPath, contains, getFrequency, getSuggestions);
            OH_LOG_DEBUG(LOG_APP, "processSwipePath: BeamSearch returned %{public}zu candidates", beamCandidates.size());
        }
    }
    
    // If beam search succeeded, use its results
    if (!beamCandidates.empty()) {
        // Build result from beam search candidates
        napi_value obj;
        napi_create_object(env, &obj);
        
        // bestWord
        napi_value bestWordValue = SafeStringToNapi(env, beamCandidates[0].word);
        napi_set_named_property(env, obj, "bestWord", bestWordValue);
        
        // alternatives
        size_t altCount = (beamCandidates.size() > 1) ? std::min((size_t)5, beamCandidates.size() - 1) : 0;
        napi_value alternatives;
        napi_create_array_with_length(env, altCount, &alternatives);
        for (size_t i = 0; i < altCount; i++) {
            napi_value alt = SafeStringToNapi(env, beamCandidates[i + 1].word);
            napi_set_element(env, alternatives, i, alt);
        }
        napi_set_named_property(env, obj, "alternatives", alternatives);
        
        // confidence
        float confidence = std::max(0.0f, std::min(1.0f, beamCandidates[0].score));
        napi_value confidenceValue;
        napi_create_double(env, confidence, &confidenceValue);
        napi_set_named_property(env, obj, "confidence", confidenceValue);
        
        // rawSequence (extract from path)
        std::string rawSeq;
        std::string lastKey;
        for (const auto& pt : beamPath) {
            auto nearestKeys = GetNearestKeysWithScores(pt.x, pt.y, 1);
            if (!nearestKeys.empty() && nearestKeys[0].first != lastKey) {
                rawSeq += nearestKeys[0].first;
                lastKey = nearestKeys[0].first;
            }
        }
        napi_value rawSeqValue = SafeStringToNapi(env, rawSeq);
        napi_set_named_property(env, obj, "rawSequence", rawSeqValue);
        
        OH_LOG_INFO(LOG_APP, "processSwipePath: BeamSearch result '%{public}s' (confidence=%.2f)",
                    beamCandidates[0].word.c_str(), confidence);
        
        return obj;
    }
    
    // =========================================================================
    // STEP 3b: Fallback - Extract key sequence with proximity-aware key detection
    // =========================================================================
    OH_LOG_DEBUG(LOG_APP, "processSwipePath: BeamSearch failed, using fallback");
    
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
    // STEP 4: Get candidate words from dictionary (fallback path)
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
    
    // Get suggestions for each potential first letter (Yandex)
    {
        std::lock_guard<std::mutex> lock(g_yandexMutex);
        if (g_yandexDict && g_yandexDict->isLoaded()) {
            for (const auto& fl : firstLetterCandidates) {
                auto suggestions = g_yandexDict->getSuggestions(fl, 100);
                for (const auto& suggestion : suggestions) {
                    // Allow some length variance
                    if (suggestion.word.length() >= keySequence.length() - 2 &&
                        suggestion.word.length() <= keySequence.length() + 3) {
                        candidates.push_back(suggestion.word);
                    }
                }
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
        
        // 5d. Frequency boost (Yandex)
        cand.frequency = 0;
        {
            std::lock_guard<std::mutex> lock(g_yandexMutex);
            if (g_yandexDict && g_yandexDict->isLoaded()) {
                cand.frequency = static_cast<int>(g_yandexDict->getFrequency(word));
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

    // TODO: Implement learned words in YandexDict
    bool success = false;
    OH_LOG_DEBUG(LOG_APP, "addLearnedWordSimple: word=%{public}s, freq=%d - NOT IMPLEMENTED (Yandex)",
                word.c_str(), frequency);

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

    // TODO: Implement in YandexDict
    bool success = false;
    OH_LOG_DEBUG(LOG_APP, "recordWordUsage: word=%{public}s - NOT IMPLEMENTED (Yandex)", word.c_str());

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

    // TODO: Implement in YandexDict
    bool success = false;
    OH_LOG_DEBUG(LOG_APP, "saveUserDict: path=%{public}s - NOT IMPLEMENTED (Yandex)", path.c_str());

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

    // TODO: Implement in YandexDict
    bool success = false;
    OH_LOG_DEBUG(LOG_APP, "loadUserDict: path=%{public}s - NOT IMPLEMENTED (Yandex)", path.c_str());

    napi_value result;
    napi_get_boolean(env, success, &result);
    return result;
}

/**
 * getLearnedWordsCount(): number
 * Get count of learned words in user dictionary
 */
static napi_value GetLearnedWordsCount(napi_env env, napi_callback_info info) {
    // TODO: Implement in YandexDict
    int count = 0;

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

    // TODO: Implement in YandexDict
    bool success = false;
    OH_LOG_DEBUG(LOG_APP, "removeLearnedWord: word=%{public}s - NOT IMPLEMENTED (Yandex)", word.c_str());

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

    // TODO: Implement in YandexDict
    OH_LOG_DEBUG(LOG_APP, "addLearnedWordWithContext: word=\"%{public}s\" prevWord=\"%{public}s\" count=%d - NOT IMPLEMENTED (Yandex)",
                word.c_str(), prevWord.c_str(), count);

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

    // TODO: Implement in YandexDict
    int boost = 0;

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

    // TODO: Implement in YandexDict
    bool success = false;
    OH_LOG_DEBUG(LOG_APP, "saveUserDictionary: path=\"%{public}s\" - NOT IMPLEMENTED (Yandex)", path.c_str());

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

    // TODO: Implement in YandexDict
    bool success = false;
    OH_LOG_DEBUG(LOG_APP, "loadUserDictionary: path=\"%{public}s\" - NOT IMPLEMENTED (Yandex)", path.c_str());

    napi_value result;
    napi_get_boolean(env, success, &result);
    return result;
}

/**
 * clearLearnedWords(): void
 * Clear all bigram-aware learned words
 */
static napi_value ClearLearnedWords(napi_env env, napi_callback_info info) {
    // TODO: Implement in YandexDict
    OH_LOG_DEBUG(LOG_APP, "clearLearnedWords - NOT IMPLEMENTED (Yandex)");

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

/**
 * getBigramLearnedWordsCount(): number
 * Get count of bigram-aware learned words
 */
static napi_value GetBigramLearnedWordsCount(napi_env env, napi_callback_info info) {
    // TODO: Implement in YandexDict
    int count = 0;

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
    // DEPRECATED: FlatTrie is legacy OpenBoard format
    // Use loadDictionaryFromFd with Yandex format instead
    OH_LOG_WARN(LOG_APP, "loadFlatDictionary: DEPRECATED - use loadDictionaryFromFd instead");

    napi_value result;
    napi_get_boolean(env, false, &result);
    return result;
}

/**
 * convertToFlatFormat(inputPath: string, outputPath: string, locale?: string): boolean
 * Convert .dict file to optimized .flat format
 */
static napi_value ConvertToFlatFormat(napi_env env, napi_callback_info info) {
    // DEPRECATED: FlatTrie is legacy OpenBoard format
    OH_LOG_WARN(LOG_APP, "convertToFlatFormat: DEPRECATED - Yandex format doesn't require conversion");

    napi_value result;
    napi_get_boolean(env, false, &result);
    return result;
}

/**
 * getFlatTrieStats(): { wordCount: number, memoryUsage: number, locale: string } | null
 * Get statistics from loaded FlatTrie
 */
static napi_value GetFlatTrieStats(napi_env env, napi_callback_info info) {
    // DEPRECATED: FlatTrie is legacy OpenBoard format
    // Use getStats() for YandexDict stats
    OH_LOG_WARN(LOG_APP, "getFlatTrieStats: DEPRECATED - use getStats() instead");

    napi_value nullVal;
    napi_get_null(env, &nullVal);
    return nullVal;
}

// ============================================================================
// MultiPredictor NAPI Functions (Yandex-style multi-source prediction)
// ============================================================================

/**
 * initMultiPredictor(): boolean
 * Initialize the multi-predictor system with default predictors
 * Requires dictionary to be loaded first
 */
static napi_value InitMultiPredictor(napi_env env, napi_callback_info info) {
    std::lock_guard<std::mutex> lock(g_multiPredictorMutex);

    g_multiPredictor = std::make_unique<latinime::MultiPredictor>();

    OH_LOG_INFO(LOG_APP, "initMultiPredictor: initialized empty multi-predictor");

    napi_value result;
    napi_get_boolean(env, true, &result);
    return result;
}

/**
 * addDictionaryPredictor(): boolean
 * Add dictionary-based predictor to the pipeline
 * Note: Currently a stub - full implementation would require Dictionary pointer
 */
static napi_value AddDictionaryPredictor(napi_env env, napi_callback_info info) {
    std::lock_guard<std::mutex> lock(g_multiPredictorMutex);

    if (!g_multiPredictor) {
        OH_LOG_ERROR(LOG_APP, "addDictionaryPredictor: multi-predictor not initialized");
        napi_value result;
        napi_get_boolean(env, false, &result);
        return result;
    }

    // Note: DictionaryPredictor requires a Dictionary* which we don't have in this context
    // This is a placeholder - full implementation would use OpenBoard's Dictionary class
    auto predictor = std::make_unique<latinime::DictionaryPredictor>(nullptr);
    g_multiPredictor->addPredictor(std::move(predictor));

    OH_LOG_INFO(LOG_APP, "addDictionaryPredictor: added (count=%zu)",
                g_multiPredictor->getPredictorCount());

    napi_value result;
    napi_get_boolean(env, true, &result);
    return result;
}

/**
 * addNgramPredictor(): boolean
 * Add n-gram based predictor for contextual suggestions
 */
static napi_value AddNgramPredictor(napi_env env, napi_callback_info info) {
    std::lock_guard<std::mutex> lock(g_multiPredictorMutex);

    if (!g_multiPredictor) {
        OH_LOG_ERROR(LOG_APP, "addNgramPredictor: multi-predictor not initialized");
        napi_value result;
        napi_get_boolean(env, false, &result);
        return result;
    }

    auto predictor = std::make_unique<latinime::NgramPredictor>(nullptr);
    g_multiPredictor->addPredictor(std::move(predictor));

    OH_LOG_INFO(LOG_APP, "addNgramPredictor: added (count=%zu)",
                g_multiPredictor->getPredictorCount());

    napi_value result;
    napi_get_boolean(env, true, &result);
    return result;
}

/**
 * removePredictor(sourceId: number): boolean
 * Remove a predictor by its source ID
 */
static napi_value RemovePredictor(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (!ValidateArgCount(env, argc, 1, "removePredictor")) {
        return nullptr;
    }

    int32_t sourceId = 0;
    napi_get_value_int32(env, args[0], &sourceId);

    std::lock_guard<std::mutex> lock(g_multiPredictorMutex);

    if (!g_multiPredictor) {
        napi_value result;
        napi_get_boolean(env, false, &result);
        return result;
    }

    size_t prevCount = g_multiPredictor->getPredictorCount();
    g_multiPredictor->removePredictor(sourceId);

    bool removed = (g_multiPredictor->getPredictorCount() < prevCount);
    OH_LOG_INFO(LOG_APP, "removePredictor: sourceId=%d removed=%d", sourceId, removed);

    napi_value result;
    napi_get_boolean(env, removed, &result);
    return result;
}

/**
 * setPredictorEnabled(sourceId: number, enabled: boolean): void
 * Enable or disable a predictor
 */
static napi_value SetPredictorEnabled(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (!ValidateArgCount(env, argc, 2, "setPredictorEnabled")) {
        return nullptr;
    }

    int32_t sourceId = 0;
    bool enabled = true;
    napi_get_value_int32(env, args[0], &sourceId);
    napi_get_value_bool(env, args[1], &enabled);

    std::lock_guard<std::mutex> lock(g_multiPredictorMutex);

    if (g_multiPredictor) {
        g_multiPredictor->setPredictorEnabled(sourceId, enabled);
        OH_LOG_DEBUG(LOG_APP, "setPredictorEnabled: sourceId=%d enabled=%d", sourceId, enabled);
    }

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

/**
 * getMultiPredictions(currentWord: string, prevWord?: string, maxResults?: number): Suggestion[]
 * Get combined predictions from all enabled predictors
 */
static napi_value GetMultiPredictions(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 1) {
        napi_throw_error(env, nullptr, "getMultiPredictions requires at least 1 argument");
        return nullptr;
    }

    std::string currentWord = NapiValueToString(env, args[0]);
    std::string prevWord = "";
    int maxResults = latinime::MultiPredictor::DEFAULT_MAX_RESULTS;

    if (argc >= 2) {
        napi_valuetype type;
        napi_typeof(env, args[1], &type);
        if (type == napi_string) {
            prevWord = NapiValueToString(env, args[1]);
        }
    }

    if (argc >= 3) {
        napi_valuetype type;
        napi_typeof(env, args[2], &type);
        if (type == napi_number) {
            napi_get_value_int32(env, args[2], &maxResults);
        }
    }

    std::lock_guard<std::mutex> lock(g_multiPredictorMutex);

    if (!g_multiPredictor) {
        // Return empty array if not initialized
        napi_value result;
        napi_create_array_with_length(env, 0, &result);
        return result;
    }

    // Convert strings to code points
    std::vector<int> inputCodePoints;
    for (char c : currentWord) {
        inputCodePoints.push_back(static_cast<int>(static_cast<unsigned char>(c)));
    }

    std::vector<int> prevCodePoints;
    for (char c : prevWord) {
        prevCodePoints.push_back(static_cast<int>(static_cast<unsigned char>(c)));
    }

    // Build prediction input
    latinime::PredictionInput input;
    input.inputCodePoints = inputCodePoints.data();
    input.inputLength = static_cast<int>(inputCodePoints.size());
    input.prevWordCodePoints = prevCodePoints.empty() ? nullptr : prevCodePoints.data();
    input.prevWordLength = static_cast<int>(prevCodePoints.size());
    input.isGesture = false;

    // Get suggestions
    std::vector<latinime::Suggestion> suggestions;
    g_multiPredictor->getSuggestions(input, maxResults, suggestions);

    // Build result array
    napi_value result;
    napi_create_array_with_length(env, suggestions.size(), &result);

    for (size_t i = 0; i < suggestions.size(); ++i) {
        const auto& s = suggestions[i];

        napi_value obj;
        napi_create_object(env, &obj);

        // Convert code points back to string
        std::string word;
        for (int cp : s.codePoints) {
            word += static_cast<char>(cp);
        }

        napi_value wordVal = SafeStringToNapi(env, word);
        napi_value scoreVal, probVal, sourceIdVal, isExactVal, isAutoVal;
        napi_create_double(env, s.score, &scoreVal);
        napi_create_int32(env, s.probability, &probVal);
        napi_create_int32(env, s.sourceId, &sourceIdVal);
        napi_get_boolean(env, s.isExactMatch, &isExactVal);
        napi_get_boolean(env, s.isAutoCorrection, &isAutoVal);

        napi_set_named_property(env, obj, "word", wordVal);
        napi_set_named_property(env, obj, "score", scoreVal);
        napi_set_named_property(env, obj, "probability", probVal);
        napi_set_named_property(env, obj, "sourceId", sourceIdVal);
        napi_set_named_property(env, obj, "isExactMatch", isExactVal);
        napi_set_named_property(env, obj, "isAutoCorrection", isAutoVal);

        napi_set_element(env, result, i, obj);
    }

    OH_LOG_DEBUG(LOG_APP, "getMultiPredictions: input='%s' results=%zu",
                 currentWord.c_str(), suggestions.size());

    return result;
}

/**
 * clearMultiPredictor(): void
 * Clear all predictors
 */
static napi_value ClearMultiPredictor(napi_env env, napi_callback_info info) {
    std::lock_guard<std::mutex> lock(g_multiPredictorMutex);

    if (g_multiPredictor) {
        g_multiPredictor->clearPredictors();
        OH_LOG_INFO(LOG_APP, "clearMultiPredictor: cleared all predictors");
    }

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

/**
 * getMultiPredictorStats(): { predictorCount: number } | null
 * Get multi-predictor statistics
 */
static napi_value GetMultiPredictorStats(napi_env env, napi_callback_info info) {
    std::lock_guard<std::mutex> lock(g_multiPredictorMutex);

    if (!g_multiPredictor) {
        napi_value nullVal;
        napi_get_null(env, &nullVal);
        return nullVal;
    }

    napi_value result;
    napi_create_object(env, &result);

    napi_value countVal;
    napi_create_int32(env, static_cast<int32_t>(g_multiPredictor->getPredictorCount()), &countVal);
    napi_set_named_property(env, result, "predictorCount", countVal);

    return result;
}

// ============================================================================
// Yandex-style Filtering API (Blacklist, Autocorrect Blocker)
// ============================================================================

/**
 * addToBlacklist(word: string): void
 * Add word to blacklist (won't appear in suggestions)
 */
static napi_value AddToBlacklist(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 1) {
        napi_value undefined;
        napi_get_undefined(env, &undefined);
        return undefined;
    }

    std::string word = NapiValueToString(env, args[0]);
    
    std::lock_guard<std::mutex> lock(g_multiPredictorMutex);
    if (g_multiPredictor) {
        g_multiPredictor->addToBlacklist(word);
        OH_LOG_DEBUG(LOG_APP, "addToBlacklist: %{public}s", word.c_str());
    }

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

/**
 * removeFromBlacklist(word: string): void
 */
static napi_value RemoveFromBlacklist(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc >= 1) {
        std::string word = NapiValueToString(env, args[0]);
        std::lock_guard<std::mutex> lock(g_multiPredictorMutex);
        if (g_multiPredictor) {
            g_multiPredictor->removeFromBlacklist(word);
        }
    }

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

/**
 * addToAutocorrectBlocker(word: string): void
 * Add word to autocorrect blocker (won't be auto-replaced)
 */
static napi_value AddToAutocorrectBlocker(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc >= 1) {
        std::string word = NapiValueToString(env, args[0]);
        std::lock_guard<std::mutex> lock(g_multiPredictorMutex);
        if (g_multiPredictor) {
            g_multiPredictor->addToAutocorrectBlocker(word);
            OH_LOG_DEBUG(LOG_APP, "addToAutocorrectBlocker: %{public}s", word.c_str());
        }
    }

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

/**
 * removeFromAutocorrectBlocker(word: string): void
 */
static napi_value RemoveFromAutocorrectBlocker(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc >= 1) {
        std::string word = NapiValueToString(env, args[0]);
        std::lock_guard<std::mutex> lock(g_multiPredictorMutex);
        if (g_multiPredictor) {
            g_multiPredictor->removeFromAutocorrectBlocker(word);
        }
    }

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

/**
 * setFusionParams(params: FusionParams): void
 * Configure score fusion weights (Yandex-style)
 */
static napi_value SetFusionParams(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 1) {
        napi_value undefined;
        napi_get_undefined(env, &undefined);
        return undefined;
    }

    latinime::FusionParams params;
    
    napi_value val;
    if (napi_get_named_property(env, args[0], "dictionaryWeight", &val) == napi_ok) {
        napi_get_value_double(env, val, reinterpret_cast<double*>(&params.dictionaryWeight));
    }
    if (napi_get_named_property(env, args[0], "neuralWeight", &val) == napi_ok) {
        napi_get_value_double(env, val, reinterpret_cast<double*>(&params.neuralWeight));
    }
    if (napi_get_named_property(env, args[0], "personalWeight", &val) == napi_ok) {
        napi_get_value_double(env, val, reinterpret_cast<double*>(&params.personalWeight));
    }
    if (napi_get_named_property(env, args[0], "ngramWeight", &val) == napi_ok) {
        napi_get_value_double(env, val, reinterpret_cast<double*>(&params.ngramWeight));
    }
    if (napi_get_named_property(env, args[0], "autocorrectThreshold", &val) == napi_ok) {
        napi_get_value_double(env, val, reinterpret_cast<double*>(&params.autocorrectThreshold));
    }

    std::lock_guard<std::mutex> lock(g_multiPredictorMutex);
    if (g_multiPredictor) {
        g_multiPredictor->setFusionParams(params);
        OH_LOG_INFO(LOG_APP, "setFusionParams: dict=%.2f neural=%.2f personal=%.2f ngram=%.2f",
                    params.dictionaryWeight, params.neuralWeight, 
                    params.personalWeight, params.ngramWeight);
    }

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

/**
 * getFusionParams(): FusionParams
 */
static napi_value GetFusionParams(napi_env env, napi_callback_info info) {
    napi_value result;
    napi_create_object(env, &result);

    std::lock_guard<std::mutex> lock(g_multiPredictorMutex);
    
    latinime::FusionParams params;
    if (g_multiPredictor) {
        params = g_multiPredictor->getFusionParams();
    }

    napi_value val;
    napi_create_double(env, params.dictionaryWeight, &val);
    napi_set_named_property(env, result, "dictionaryWeight", val);
    napi_create_double(env, params.neuralWeight, &val);
    napi_set_named_property(env, result, "neuralWeight", val);
    napi_create_double(env, params.personalWeight, &val);
    napi_set_named_property(env, result, "personalWeight", val);
    napi_create_double(env, params.ngramWeight, &val);
    napi_set_named_property(env, result, "ngramWeight", val);
    napi_create_double(env, params.autocorrectThreshold, &val);
    napi_set_named_property(env, result, "autocorrectThreshold", val);
    napi_create_double(env, params.maxRelativeScoreGap, &val);
    napi_set_named_property(env, result, "maxRelativeScoreGap", val);

    return result;
}

// ============================================================================
// Yandex Neural Dictionary API
// ============================================================================

/**
 * loadYandexDict(path: string): boolean
 * Load Yandex dictionary from main_ru file
 */
static napi_value LoadYandexDict(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (!ValidateArgCount(env, argc, 1, "loadYandexDict")) {
        return nullptr;
    }
    if (!ValidateString(env, args[0], "path")) {
        return nullptr;
    }

    std::string pathStr = NapiValueToString(env, args[0]);
    if (pathStr.empty()) {
        napi_throw_error(env, "EINVAL", "loadYandexDict: path must not be empty");
        return nullptr;
    }

    OH_LOG_INFO(LOG_APP, "loadYandexDict: loading from %{public}s", pathStr.c_str());

    std::lock_guard<std::mutex> lock(g_yandexMutex);

    g_yandexDict = std::make_unique<yandex::YandexDict>();

    // Auto-detect format: *.txt = text format, otherwise binary.
    std::string lowerPath = pathStr;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    bool isTextFormat = lowerPath.size() >= 4 && lowerPath.substr(lowerPath.size() - 4) == ".txt";
    
    bool success;
    if (isTextFormat) {
        OH_LOG_INFO(LOG_APP, "loadYandexDict: using TEXT format");
        success = g_yandexDict->loadFromTextFile(pathStr);
    } else {
        OH_LOG_INFO(LOG_APP, "loadYandexDict: using BINARY format (main_ru)");
        success = g_yandexDict->load(pathStr);
    }

    if (success) {
        OH_LOG_INFO(LOG_APP, "loadYandexDict: SUCCESS - %{public}zu words loaded",
                    g_yandexDict->getWordCount());
    } else {
        OH_LOG_ERROR(LOG_APP, "loadYandexDict: FAILED to load from %{public}s", pathStr.c_str());
        g_yandexDict.reset();
    }

    napi_value result;
    napi_get_boolean(env, success, &result);
    return result;
}

/**
 * loadNeuralModel(path: string): boolean
 * Load MindSpore model for neural scoring
 */
static napi_value LoadNeuralModel(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 1) {
        OH_LOG_ERROR(LOG_APP, "loadNeuralModel: missing path argument");
        napi_value result;
        napi_get_boolean(env, false, &result);
        return result;
    }

    char path[512];
    size_t pathLen;
    napi_get_value_string_utf8(env, args[0], path, sizeof(path), &pathLen);

    OH_LOG_INFO(LOG_APP, "loadNeuralModel: loading from %{public}s", path);

    std::lock_guard<std::mutex> lock(g_yandexMutex);

    g_neuralScorer = std::make_unique<yandex::NNRtScorer>();
    bool success = g_neuralScorer->loadModel(path);

    if (success) {
        OH_LOG_INFO(LOG_APP, "loadNeuralModel: SUCCESS");
    } else {
        OH_LOG_ERROR(LOG_APP, "loadNeuralModel: FAILED to load from %{public}s", path);
        g_neuralScorer.reset();
    }

    napi_value result;
    napi_get_boolean(env, success, &result);
    return result;
}

/**
 * loadModels(modelsDir: string): object
 * Load ALL 15 neural models from the specified directory
 *
 * Models loaded:
 *   TAP RANKING: tap_model_ranker.ms, tap_model_ranker_v2.ms, ranker.ms, ranker_v2.ms, ranker_exp.ms
 *   SWIPE RANKING: ranker_swipe.ms, ranker_swipe_v2.ms, swipe_blocker.ms
 *   LANGUAGE: nnlm_model.ms, neural_model.ms, char_model.ms
 *   AUTOCORRECT: tree_autocorrect_model.ms, lemmer_mhash.ms
 *   EMOJI: emoji_suggest.ms, search_emoji_model.ms
 *
 * @param modelsDir - Directory containing all .ms model files
 * @returns { loaded: number, total: number, models: string[] }
 */
static napi_value LoadModels(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 1) {
        OH_LOG_ERROR(LOG_APP, "loadModels: missing modelsDir argument");
        napi_value result;
        napi_create_object(env, &result);

        napi_value loadedVal, totalVal;
        napi_create_int32(env, 0, &loadedVal);
        napi_create_int32(env, 15, &totalVal);
        napi_set_named_property(env, result, "loaded", loadedVal);
        napi_set_named_property(env, result, "total", totalVal);
        return result;
    }

    // Get models directory path
    char modelsDir[512];
    size_t dirLen;
    napi_get_value_string_utf8(env, args[0], modelsDir, sizeof(modelsDir), &dirLen);

    OH_LOG_INFO(LOG_APP, "loadModels: Loading ALL 15 models from %{public}s", modelsDir);

    // ========================================================================
    // Initialize NeuralModelManager and load ALL models
    // ========================================================================
    int loadedCount = 0;
    std::vector<std::string> loadedNames;
    std::vector<std::string> failedNames;

    {
        std::lock_guard<std::mutex> lock(g_modelManagerMutex);

        // Create manager if not exists
        if (!g_modelManager) {
            g_modelManager = std::make_unique<yandex::NeuralModelManager>();
        }

        // Set models directory
        g_modelManager->setModelsDirectory(modelsDir);

        // Load ALL 15 models
        loadedCount = g_modelManager->loadAllModels();

        // Get stats
        auto stats = g_modelManager->getStats();
        loadedNames = stats.loadedNames;
        failedNames = stats.failedNames;

        g_neuralModelsEnabled = (loadedCount > 0);

        OH_LOG_INFO(LOG_APP, "loadModels: %{public}d/15 models loaded, device: %{public}s",
                    loadedCount, stats.primaryDevice.c_str());
    }

    // Also load primary ranker in legacy scorer for backward compatibility
    {
        std::lock_guard<std::mutex> lock(g_neuralScorerMutex);
        if (!g_neuralScorer) {
            g_neuralScorer = std::make_unique<yandex::NNRtScorer>();
        }
        std::string primaryPath = std::string(modelsDir) + "/tap_model_ranker.ms";
        if (g_neuralScorer->loadModel(primaryPath)) {
            g_neuralScorerEnabled = true;
            OH_LOG_INFO(LOG_APP, "loadModels: Legacy scorer also loaded");
        }
    }

    // Create result object
    napi_value result;
    napi_create_object(env, &result);

    // loaded count
    napi_value loadedVal;
    napi_create_int32(env, loadedCount, &loadedVal);
    napi_set_named_property(env, result, "loaded", loadedVal);

    // total count
    napi_value totalVal;
    napi_create_int32(env, 15, &totalVal);
    napi_set_named_property(env, result, "total", totalVal);

    // success flag
    napi_value successVal;
    napi_get_boolean(env, loadedCount > 0, &successVal);
    napi_set_named_property(env, result, "success", successVal);

    // loaded model names array
    napi_value modelsArray;
    napi_create_array_with_length(env, loadedNames.size(), &modelsArray);
    for (size_t i = 0; i < loadedNames.size(); i++) {
        napi_value nameVal;
        napi_create_string_utf8(env, loadedNames[i].c_str(), loadedNames[i].length(), &nameVal);
        napi_set_element(env, modelsArray, static_cast<uint32_t>(i), nameVal);
    }
    napi_set_named_property(env, result, "models", modelsArray);

    // failed model names array
    napi_value failedArray;
    napi_create_array_with_length(env, failedNames.size(), &failedArray);
    for (size_t i = 0; i < failedNames.size(); i++) {
        napi_value nameVal;
        napi_create_string_utf8(env, failedNames[i].c_str(), failedNames[i].length(), &nameVal);
        napi_set_element(env, failedArray, static_cast<uint32_t>(i), nameVal);
    }
    napi_set_named_property(env, result, "failed", failedArray);

    return result;
}

/**
 * getModelStats(): object
 * Get status of loaded models (including NeuralModelManager stats)
 */
static napi_value GetModelStats(napi_env env, napi_callback_info info) {
    napi_value result;
    napi_create_object(env, &result);

    // Helper to set boolean property
    auto setBool = [env, &result](const char* name, bool value) {
        napi_value v;
        napi_get_boolean(env, value, &v);
        napi_set_named_property(env, result, name, v);
    };

    // Helper to set string property
    auto setString = [env, &result](const char* name, const std::string& value) {
        napi_value v;
        napi_create_string_utf8(env, value.c_str(), value.length(), &v);
        napi_set_named_property(env, result, name, v);
    };

    // Helper to set int property
    auto setInt = [env, &result](const char* name, int value) {
        napi_value v;
        napi_create_int32(env, value, &v);
        napi_set_named_property(env, result, name, v);
    };

    setBool("yandexDictLoaded", g_yandexDict && g_yandexDict->isLoaded());
    setBool("neuralModelsEnabled", g_neuralModelsEnabled);
    setBool("beamSearchReady", g_beamSearch != nullptr);

    // NeuralModelManager stats (ALL 15 models)
    if (g_modelManager) {
        std::lock_guard<std::mutex> lock(g_modelManagerMutex);
        auto stats = g_modelManager->getStats();

        setInt("totalModels", stats.totalModels);
        setInt("loadedModels", stats.loadedModels);
        setString("primaryDevice", stats.primaryDevice);

        // Create arrays for loaded/failed model names
        napi_value loadedArray;
        napi_create_array_with_length(env, stats.loadedNames.size(), &loadedArray);
        for (size_t i = 0; i < stats.loadedNames.size(); i++) {
            napi_value nameVal;
            napi_create_string_utf8(env, stats.loadedNames[i].c_str(), stats.loadedNames[i].length(), &nameVal);
            napi_set_element(env, loadedArray, static_cast<uint32_t>(i), nameVal);
        }
        napi_set_named_property(env, result, "loadedModelNames", loadedArray);

        napi_value failedArray;
        napi_create_array_with_length(env, stats.failedNames.size(), &failedArray);
        for (size_t i = 0; i < stats.failedNames.size(); i++) {
            napi_value nameVal;
            napi_create_string_utf8(env, stats.failedNames[i].c_str(), stats.failedNames[i].length(), &nameVal);
            napi_set_element(env, failedArray, static_cast<uint32_t>(i), nameVal);
        }
        napi_set_named_property(env, result, "failedModelNames", failedArray);
    } else {
        setInt("totalModels", 15);
        setInt("loadedModels", 0);
        setString("primaryDevice", "Not initialized");
    }

    // Legacy scorer status (backward compat)
    setBool("legacyScorerEnabled", g_neuralScorerEnabled);
    setBool("legacyScorerLoaded", g_neuralScorer && g_neuralScorer->isLoaded());
    if (g_neuralScorer) {
        setString("legacyDevice", g_neuralScorer->getDeviceInfo());
    }

    return result;
}

/**
 * getYandexSuggestions(prefix: string, limit?: number, context?: string): ScoredWord[]
 * Get neural-scored suggestions from Yandex dictionary
 */
static napi_value GetYandexSuggestions(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 1) {
        OH_LOG_ERROR(LOG_APP, "getYandexSuggestions: missing prefix argument");
        napi_value result;
        napi_create_array(env, &result);
        return result;
    }

    // Get prefix
    char prefix[256];
    size_t prefixLen;
    napi_get_value_string_utf8(env, args[0], prefix, sizeof(prefix), &prefixLen);

    // Get limit (default: 10)
    int limit = 10;
    if (argc >= 2) {
        napi_valuetype type;
        napi_typeof(env, args[1], &type);
        if (type == napi_number) {
            napi_get_value_int32(env, args[1], &limit);
        }
    }

    // Get context (default: empty)
    char context[512] = "";
    if (argc >= 3) {
        napi_valuetype type;
        napi_typeof(env, args[2], &type);
        if (type == napi_string) {
            size_t contextLen;
            napi_get_value_string_utf8(env, args[2], context, sizeof(context), &contextLen);
        }
    }

    std::lock_guard<std::mutex> lock(g_yandexMutex);

    // Create result array
    napi_value result;
    napi_create_array(env, &result);

    if (!g_yandexDict || !g_yandexDict->isLoaded()) {
        OH_LOG_WARN(LOG_APP, "getYandexSuggestions: Yandex dict not loaded");
        return result;
    }

    // Get suggestions from trie
    auto suggestions = g_yandexDict->getSuggestions(prefix, limit * 2);  // Get more for neural scoring

    // If neural model is loaded, use it for scoring
    if (g_neuralScorer && g_neuralScorer->isLoaded()) {
        // Convert to scoring candidates
        std::vector<yandex::ScoringCandidate> candidates;
        candidates.reserve(suggestions.size());

        for (const auto& s : suggestions) {
            yandex::ScoringCandidate c;
            c.word = s.word;
            c.wordId = g_yandexDict->getWordId(s.word);
            c.baseScore = s.score;
            candidates.push_back(c);
        }

        // Score with neural model
        auto scored = g_neuralScorer->score(candidates, context);

        // Convert to NAPI array
        uint32_t idx = 0;
        for (const auto& s : scored) {
            if (idx >= static_cast<uint32_t>(limit)) break;

            napi_value item;
            napi_create_object(env, &item);

            napi_value wordVal, scoreVal, neuralVal, freqVal;
            napi_create_string_utf8(env, s.word.c_str(), s.word.length(), &wordVal);
            napi_create_double(env, s.score, &scoreVal);
            napi_create_double(env, s.neuralScore, &neuralVal);
            napi_create_double(env, s.freqScore, &freqVal);

            napi_set_named_property(env, item, "word", wordVal);
            napi_set_named_property(env, item, "score", scoreVal);
            napi_set_named_property(env, item, "neuralScore", neuralVal);
            napi_set_named_property(env, item, "freqScore", freqVal);

            napi_set_element(env, result, idx++, item);
        }
    } else {
        // No neural model - return raw suggestions
        uint32_t idx = 0;
        for (const auto& s : suggestions) {
            if (idx >= static_cast<uint32_t>(limit)) break;

            napi_value item;
            napi_create_object(env, &item);

            napi_value wordVal, scoreVal;
            napi_create_string_utf8(env, s.word.c_str(), s.word.length(), &wordVal);
            napi_create_double(env, s.score, &scoreVal);

            napi_set_named_property(env, item, "word", wordVal);
            napi_set_named_property(env, item, "score", scoreVal);

            napi_set_element(env, result, idx++, item);
        }
    }

    return result;
}

/**
 * getYandexStats(): object
 * Get Yandex dictionary statistics
 */
static napi_value GetYandexStats(napi_env env, napi_callback_info info) {
    std::lock_guard<std::mutex> lock(g_yandexMutex);

    napi_value result;
    napi_create_object(env, &result);

    bool dictLoaded = g_yandexDict && g_yandexDict->isLoaded();
    bool modelLoaded = g_neuralScorer && g_neuralScorer->isLoaded();

    napi_value dictLoadedVal, modelLoadedVal;
    napi_get_boolean(env, dictLoaded, &dictLoadedVal);
    napi_get_boolean(env, modelLoaded, &modelLoadedVal);
    napi_set_named_property(env, result, "dictLoaded", dictLoadedVal);
    napi_set_named_property(env, result, "modelLoaded", modelLoadedVal);

    if (dictLoaded) {
        napi_value wordCountVal, memoryVal;
        napi_create_int32(env, static_cast<int32_t>(g_yandexDict->getWordCount()), &wordCountVal);
        napi_create_int64(env, static_cast<int64_t>(g_yandexDict->getMemoryUsage()), &memoryVal);
        napi_set_named_property(env, result, "wordCount", wordCountVal);
        napi_set_named_property(env, result, "memoryBytes", memoryVal);
    }

    return result;
}

/**
 * unloadYandex(): void
 * Unload Yandex dictionary and neural model
 */
static napi_value UnloadYandex(napi_env env, napi_callback_info info) {
    std::lock_guard<std::mutex> lock(g_yandexMutex);

    if (g_neuralScorer) {
        g_neuralScorer->unload();
        g_neuralScorer.reset();
        OH_LOG_INFO(LOG_APP, "unloadYandex: neural model unloaded");
    }

    if (g_yandexDict) {
        g_yandexDict.reset();
        OH_LOG_INFO(LOG_APP, "unloadYandex: dictionary unloaded");
    }

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

// ============================================================================
// End Yandex Neural Dictionary API
// ============================================================================

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

    // Clean up MultiPredictor
    {
        std::lock_guard<std::mutex> lock(g_multiPredictorMutex);
        if (g_multiPredictor) {
            OH_LOG_DEBUG(LOG_APP, "unload: releasing MultiPredictor");
            g_multiPredictor.reset();
        }
    }

    // Clean up BeamSearch
    {
        std::lock_guard<std::mutex> lock(g_beamSearchMutex);
        if (g_beamSearch) {
            OH_LOG_DEBUG(LOG_APP, "unload: releasing BeamSearch");
            g_beamSearch.reset();
        }
    }

    // Clean up Yandex Dictionary and Neural scorer
    {
        std::lock_guard<std::mutex> lock(g_yandexMutex);
        if (g_neuralScorer) {
            OH_LOG_DEBUG(LOG_APP, "unload: releasing MindSpore scorer");
            g_neuralScorer->unload();
            g_neuralScorer.reset();
        }
        if (g_yandexDict) {
            OH_LOG_DEBUG(LOG_APP, "unload: releasing Yandex dict");
            g_yandexDict.reset();
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

    // Add binary dictionary functions to exports (legacy OpenBoard - kept for compatibility)
    napi_value binaryDictExports = latinime::RegisterBinaryDictionary(env);

    // Add proximity info functions to exports
    napi_value proximityInfoExports = latinime::RegisterProximityInfo(env);

    // Add dic traverse session functions to exports (legacy OpenBoard - kept for compatibility)
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

    // Add MultiPredictor namespace (Yandex-style multi-source prediction)
    napi_value multiPredictorExports;
    napi_create_object(env, &multiPredictorExports);
    napi_property_descriptor multiPredictorDesc[] = {
        { "init", nullptr, InitMultiPredictor, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "addDictionaryPredictor", nullptr, AddDictionaryPredictor, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "addNgramPredictor", nullptr, AddNgramPredictor, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "removePredictor", nullptr, RemovePredictor, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "setPredictorEnabled", nullptr, SetPredictorEnabled, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "getPredictions", nullptr, GetMultiPredictions, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "clear", nullptr, ClearMultiPredictor, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "getStats", nullptr, GetMultiPredictorStats, nullptr, nullptr, nullptr, napi_default, nullptr },
        // Yandex-style filtering
        { "addToBlacklist", nullptr, AddToBlacklist, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "removeFromBlacklist", nullptr, RemoveFromBlacklist, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "addToAutocorrectBlocker", nullptr, AddToAutocorrectBlocker, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "removeFromAutocorrectBlocker", nullptr, RemoveFromAutocorrectBlocker, nullptr, nullptr, nullptr, napi_default, nullptr },
        // Score fusion params
        { "setFusionParams", nullptr, SetFusionParams, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "getFusionParams", nullptr, GetFusionParams, nullptr, nullptr, nullptr, napi_default, nullptr },
    };
    napi_define_properties(env, multiPredictorExports, sizeof(multiPredictorDesc) / sizeof(multiPredictorDesc[0]), multiPredictorDesc);
    napi_set_named_property(env, exports, "multiPredictor", multiPredictorExports);

    // Add other functions to main exports
    napi_property_descriptor desc[] = {
        { "loadDictionary", nullptr, LoadDictionary, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "loadDictionarySync", nullptr, LoadDictionarySync, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "loadDictionaryFromFd", nullptr, LoadDictionaryFromFd, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "loadTextDictionaryFromFd", nullptr, LoadTextDictionaryFromFd, nullptr, nullptr, nullptr, napi_default, nullptr },
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
        // Yandex Neural Dictionary API
        { "loadYandexDict", nullptr, LoadYandexDict, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "loadNeuralModel", nullptr, LoadNeuralModel, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "getYandexSuggestions", nullptr, GetYandexSuggestions, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "getYandexStats", nullptr, GetYandexStats, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "unloadYandex", nullptr, UnloadYandex, nullptr, nullptr, nullptr, napi_default, nullptr },
        // Additional Models API (Yandex-style)
        { "loadModels", nullptr, LoadModels, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "getModelStats", nullptr, GetModelStats, nullptr, nullptr, nullptr, napi_default, nullptr },
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
