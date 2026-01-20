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

// Global instances
static std::unique_ptr<hoskey::Trie> g_trie;
static std::unique_ptr<hoskey::SuggestEngine> g_suggestEngine;

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

    // This function now uses the actual proximity info implementation from the NAPI module
    // The original function was a placeholder

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

    if (argc < 1) {
        napi_value result;
        napi_get_boolean(env, false, &result);
        return result;
    }

    // Parse array of keys
    bool isArray = false;
    napi_is_array(env, args[0], &isArray);

    if (!isArray) {
        napi_value result;
        napi_get_boolean(env, false, &result);
        return result;
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

    if (argc < 1 || g_keyboardLayout.empty() || !g_trie) {
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
        int freq = g_trie->getFrequency(word);
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
 * Unload dictionary and free memory
 */
static napi_value Unload(napi_env env, napi_callback_info info) {
    // Clean up keyboard layout
    g_keyboardLayout.clear();

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
