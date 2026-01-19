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

#include "dictionary/trie.h"
#include "dictionary/suggest_engine.h"

// OpenBoard suggest engine
#include "suggest/core/suggest.h"
#include "suggest/core/suggest_options.h"
#include "suggest/core/session/dic_traverse_session.h"
#include "suggest/core/layout/proximity_info.h"
#include "suggest/core/result/suggestion_results.h"
#include "suggest/policyimpl/gesture/gesture_suggest_policy_factory.h"
#include "dictionary_openboard/interface/dictionary_structure_with_buffer_policy.h"

// Global instances
static std::unique_ptr<hoskey::Trie> g_trie;
static std::unique_ptr<hoskey::SuggestEngine> g_suggestEngine;

// OpenBoard suggest engine instances
static std::unique_ptr<latinime::Suggest> g_openboardSuggest;
static std::unique_ptr<latinime::DicTraverseSession> g_traverseSession;
static std::unique_ptr<latinime::ProximityInfo> g_proximityInfo;

// Helper: Convert napi_value string to std::string
static std::string NapiValueToString(napi_env env, napi_value value) {
    size_t length = 0;
    napi_get_value_string_utf8(env, value, nullptr, 0, &length);

    std::string result(length, '\0');
    napi_get_value_string_utf8(env, value, &result[0], length + 1, &length);
    return result;
}

// Helper: Create napi_value string from std::string
static napi_value StringToNapiValue(napi_env env, const std::string& str) {
    napi_value result;
    napi_create_string_utf8(env, str.c_str(), str.length(), &result);
    return result;
}

/**
 * loadDictionary(path: string): boolean
 * Load binary dictionary from file path
 */
static napi_value LoadDictionary(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 1) {
        napi_value result;
        napi_get_boolean(env, false, &result);
        return result;
    }

    std::string path = NapiValueToString(env, args[0]);

    // Initialize trie if needed
    if (!g_trie) {
        g_trie = std::make_unique<hoskey::Trie>();
    }

    bool success = g_trie->loadFromFile(path);

    // Initialize suggest engine with loaded trie
    if (success && !g_suggestEngine) {
        g_suggestEngine = std::make_unique<hoskey::SuggestEngine>(g_trie.get());
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

    if (argc < 1 || !g_trie) {
        napi_value result;
        napi_get_boolean(env, false, &result);
        return result;
    }

    std::string word = NapiValueToString(env, args[0]);
    bool found = g_trie->contains(word);

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

    if (argc < 1 || !g_trie) {
        napi_value result;
        napi_create_int32(env, 0, &result);
        return result;
    }

    std::string word = NapiValueToString(env, args[0]);
    int frequency = g_trie->getFrequency(word);

    napi_value result;
    napi_create_int32(env, frequency, &result);
    return result;
}

/**
 * SuggestResult interface:
 * { word: string, score: number, errorType: number }
 */
static napi_value CreateSuggestResult(napi_env env, const hoskey::SuggestResult& sr) {
    napi_value obj;
    napi_create_object(env, &obj);

    napi_value word = StringToNapiValue(env, sr.word);
    napi_set_named_property(env, obj, "word", word);

    napi_value score;
    napi_create_double(env, sr.score, &score);
    napi_set_named_property(env, obj, "score", score);

    napi_value errorType;
    napi_create_int32(env, static_cast<int>(sr.errorType), &errorType);
    napi_set_named_property(env, obj, "errorType", errorType);

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

    napi_value result;
    napi_create_array(env, &result);

    if (argc < 2 || !g_suggestEngine) {
        return result;
    }

    std::string prefix = NapiValueToString(env, args[0]);

    int32_t limit = 10;
    napi_get_value_int32(env, args[1], &limit);

    auto suggestions = g_suggestEngine->getSuggestions(prefix, limit);

    for (size_t i = 0; i < suggestions.size(); i++) {
        napi_value item = CreateSuggestResult(env, suggestions[i]);
        napi_set_element(env, result, i, item);
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

    if (argc < 2 || !g_suggestEngine) {
        napi_value result;
        napi_get_null(env, &result);
        return result;
    }

    std::string word = NapiValueToString(env, args[0]);

    double threshold = 0.185; // Default OpenBoard threshold
    napi_get_value_double(env, args[1], &threshold);

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

    if (argc < 3) {
        napi_value result;
        napi_get_boolean(env, false, &result);
        return result;
    }

    std::string layout = NapiValueToString(env, args[0]);

    double keyWidth = 0, keyHeight = 0;
    napi_get_value_double(env, args[1], &keyWidth);
    napi_get_value_double(env, args[2], &keyHeight);

    g_proximityInfo = std::make_unique<hoskey::ProximityInfo>(layout, keyWidth, keyHeight);

    // Update suggest engine with proximity info
    if (g_suggestEngine) {
        g_suggestEngine->setProximityInfo(g_proximityInfo.get());
    }

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

    int wordCount = g_trie ? g_trie->getWordCount() : 0;
    size_t memoryUsage = g_trie ? g_trie->getMemoryUsage() : 0;

    napi_value wordCountVal;
    napi_create_int32(env, wordCount, &wordCountVal);
    napi_set_named_property(env, obj, "wordCount", wordCountVal);

    napi_value memoryVal;
    napi_create_int64(env, static_cast<int64_t>(memoryUsage), &memoryVal);
    napi_set_named_property(env, obj, "memoryUsage", memoryVal);

    return obj;
}

/**
 * setSwipeKeyboardLayout(keys: Array<{key: string, centerX: number, centerY: number, width: number, height: number}>): boolean
 * Set keyboard layout for swipe recognition
 *
 * TODO: Integrate with OpenBoard ProximityInfo
 */
static napi_value SetSwipeKeyboardLayout(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 1) {
        napi_value result;
        napi_get_boolean(env, false, &result);
        return result;
    }

    // TODO: Parse key layout and initialize OpenBoard ProximityInfo
    // For now, return success (layout parsing will be added later)

    napi_value result;
    napi_get_boolean(env, true, &result);
    return result;
}

/**
 * processSwipePath(points: Array<{x: number, y: number, timestamp: number}>): {bestWord: string, alternatives: string[], confidence: number, rawSequence: string} | null
 * Process swipe path and return recognized word
 *
 * TODO: Integrate with OpenBoard Suggest::getSuggestions()
 */
static napi_value ProcessSwipePath(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    // TODO: Parse touch points and call OpenBoard Suggest::getSuggestions()
    // For now, return null (swipe will use ETS fallback)

    napi_value result;
    napi_get_null(env, &result);
    return result;
}

/**
 * unload(): void
 * Unload dictionary and free memory
 */
static napi_value Unload(napi_env env, napi_callback_info info) {
    // Clean up OpenBoard instances
    g_openboardSuggest.reset();
    g_traverseSession.reset();
    g_proximityInfo.reset();

    // Clean up dictionary instances
    g_suggestEngine.reset();
    g_trie.reset();

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

// Module initialization
EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports) {
    napi_property_descriptor desc[] = {
        { "loadDictionary", nullptr, LoadDictionary, nullptr, nullptr, nullptr, napi_default, nullptr },
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
