/**
 * HOSKEY Native Dictionary Engine
 * N-API bridge for high-performance text prediction
 *
 * Based on OpenBoard architecture:
 * - Patricia Trie for fast prefix search
 * - Weighted Levenshtein for autocorrection
 * - Proximity-aware scoring
 */

#include "napi/native_api.h"
#include <string>
#include <vector>
#include <memory>

#include "dictionary/trie.h"
#include "dictionary/suggest_engine.h"
#include "scoring/scoring_params.h"
#include "proximity/proximity_info.h"
#include "swipe/swipe_engine.h"

// Global instances
static std::unique_ptr<hoskey::Trie> g_trie;
static std::unique_ptr<hoskey::SuggestEngine> g_suggestEngine;
static std::unique_ptr<hoskey::ProximityInfo> g_proximityInfo;
static std::unique_ptr<hoskey::SwipeEngine> g_swipeEngine;

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

    // Initialize swipe engine if needed
    if (!g_swipeEngine) {
        g_swipeEngine = std::make_unique<hoskey::SwipeEngine>();
        if (g_trie) {
            g_swipeEngine->setDictionary(g_trie.get());
        }
        if (g_proximityInfo) {
            g_swipeEngine->setProximityInfo(g_proximityInfo.get());
        }
    }

    // Parse array of key bounds
    bool isArray = false;
    napi_is_array(env, args[0], &isArray);

    if (!isArray) {
        napi_value result;
        napi_get_boolean(env, false, &result);
        return result;
    }

    uint32_t length = 0;
    napi_get_array_length(env, args[0], &length);

    std::vector<hoskey::KeyBounds> layout;
    layout.reserve(length);

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

        if (!key.empty()) {
            hoskey::KeyBounds bounds(key[0], centerX, centerY, width, height);
            layout.push_back(bounds);
        }
    }

    g_swipeEngine->setKeyboardLayout(layout);

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

    if (argc < 1 || !g_swipeEngine) {
        napi_value result;
        napi_get_null(env, &result);
        return result;
    }

    // Parse array of points
    bool isArray = false;
    napi_is_array(env, args[0], &isArray);

    if (!isArray) {
        napi_value result;
        napi_get_null(env, &result);
        return result;
    }

    uint32_t length = 0;
    napi_get_array_length(env, args[0], &length);

    std::vector<hoskey::SwipePoint> path;
    path.reserve(length);

    for (uint32_t i = 0; i < length; i++) {
        napi_value element;
        napi_get_element(env, args[0], i, &element);

        // Get properties
        napi_value xValue, yValue, tsValue;

        napi_get_named_property(env, element, "x", &xValue);
        napi_get_named_property(env, element, "y", &yValue);
        napi_get_named_property(env, element, "timestamp", &tsValue);

        // Extract values
        double x = 0, y = 0;
        int64_t ts = 0;

        napi_get_value_double(env, xValue, &x);
        napi_get_value_double(env, yValue, &y);
        napi_get_value_int64(env, tsValue, &ts);

        path.emplace_back(x, y, ts);
    }

    // Process swipe
    hoskey::SwipeResult result = g_swipeEngine->processSwipe(path);

    if (result.bestWord.empty()) {
        napi_value nullResult;
        napi_get_null(env, &nullResult);
        return nullResult;
    }

    // Build result object
    napi_value obj;
    napi_create_object(env, &obj);

    // bestWord
    napi_value bestWordValue = StringToNapiValue(env, result.bestWord);
    napi_set_named_property(env, obj, "bestWord", bestWordValue);

    // alternatives
    napi_value alternatives;
    napi_create_array_with_length(env, result.alternatives.size(), &alternatives);
    for (size_t i = 0; i < result.alternatives.size(); i++) {
        napi_value alt = StringToNapiValue(env, result.alternatives[i]);
        napi_set_element(env, alternatives, i, alt);
    }
    napi_set_named_property(env, obj, "alternatives", alternatives);

    // confidence
    napi_value confidenceValue;
    napi_create_double(env, result.confidence, &confidenceValue);
    napi_set_named_property(env, obj, "confidence", confidenceValue);

    // rawSequence
    napi_value rawSeqValue = StringToNapiValue(env, result.rawSequence);
    napi_set_named_property(env, obj, "rawSequence", rawSeqValue);

    return obj;
}

/**
 * unload(): void
 * Unload dictionary and free memory
 */
static napi_value Unload(napi_env env, napi_callback_info info) {
    g_swipeEngine.reset();
    g_suggestEngine.reset();
    g_trie.reset();
    g_proximityInfo.reset();

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
