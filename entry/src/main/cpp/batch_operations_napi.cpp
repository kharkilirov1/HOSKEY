/*
 * Copyright (c) 2024 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#include "batch_operations_napi.h"
#include "napi_helpers.h"
#include "dictionary/autocorrect_rules.h"
#include "dictionary_hoskey/suggest_engine.h"
#include "dictionary_hoskey/trie.h"

// Feature flag: Use optimized pooled trie
#ifndef USE_POOLED_TRIE
#define USE_POOLED_TRIE 1
#endif

#if USE_POOLED_TRIE
#include "dictionary_hoskey/trie_pooled.h"
#endif

#include <hilog/log.h>
#include <vector>
#include <string>
#include <mutex>
#include <memory>

#undef LOG_DOMAIN
#undef LOG_TAG
#define LOG_DOMAIN 0x0001
#define LOG_TAG "HOSKEY-BATCH"

// External global instances (defined in napi_init.cpp)
extern std::mutex g_trieMutex;
#if USE_POOLED_TRIE
extern std::unique_ptr<hoskey::TriePooled> g_trie;
#else
extern std::unique_ptr<hoskey::Trie> g_trie;
#endif
extern std::unique_ptr<hoskey::SuggestEngine> g_suggestEngine;

// Global autocorrect rules instance
static std::unique_ptr<latinime::AutocorrectRules> g_autocorrectRules;
static std::mutex g_rulesMutex;

namespace latinime {

// Helper: Get string from NAPI value
static std::string GetString(napi_env env, napi_value value) {
    size_t len = 0;
    napi_get_value_string_utf8(env, value, nullptr, 0, &len);
    if (len == 0) return "";
    
    std::vector<char> buf(len + 1);
    napi_get_value_string_utf8(env, value, buf.data(), buf.size(), &len);
    return std::string(buf.data(), len);
}

// Helper: Create NAPI string
static napi_value CreateString(napi_env env, const std::string& str) {
    napi_value result;
    napi_create_string_utf8(env, str.c_str(), str.length(), &result);
    return result;
}

napi_value BatchContains(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    
    if (argc < 1) {
        napi_throw_error(env, nullptr, "batchContains requires array of words");
        return nullptr;
    }
    
    // Check if array
    bool isArray = false;
    napi_is_array(env, args[0], &isArray);
    if (!isArray) {
        napi_throw_type_error(env, nullptr, "Argument must be an array");
        return nullptr;
    }
    
    uint32_t length = 0;
    napi_get_array_length(env, args[0], &length);
    
    // Create result array
    napi_value result;
    napi_create_array_with_length(env, length, &result);
    
    std::lock_guard<std::mutex> lock(g_trieMutex);
    
    if (!g_trie) {
        // Return all false if no dictionary
        for (uint32_t i = 0; i < length; ++i) {
            napi_value falseVal;
            napi_get_boolean(env, false, &falseVal);
            napi_set_element(env, result, i, falseVal);
        }
        return result;
    }
    
    for (uint32_t i = 0; i < length; ++i) {
        napi_value element;
        napi_get_element(env, args[0], i, &element);
        
        std::string word = GetString(env, element);
        bool exists = g_trie->contains(word);
        
        napi_value boolVal;
        napi_get_boolean(env, exists, &boolVal);
        napi_set_element(env, result, i, boolVal);
    }
    
    return result;
}

napi_value BatchGetFrequency(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    
    if (argc < 1) {
        napi_throw_error(env, nullptr, "batchGetFrequency requires array of words");
        return nullptr;
    }
    
    bool isArray = false;
    napi_is_array(env, args[0], &isArray);
    if (!isArray) {
        napi_throw_type_error(env, nullptr, "Argument must be an array");
        return nullptr;
    }
    
    uint32_t length = 0;
    napi_get_array_length(env, args[0], &length);
    
    napi_value result;
    napi_create_array_with_length(env, length, &result);
    
    std::lock_guard<std::mutex> lock(g_trieMutex);
    
    if (!g_trie) {
        // Return all 0 if no dictionary
        for (uint32_t i = 0; i < length; ++i) {
            napi_value zeroVal;
            napi_create_int32(env, 0, &zeroVal);
            napi_set_element(env, result, i, zeroVal);
        }
        return result;
    }
    
    for (uint32_t i = 0; i < length; ++i) {
        napi_value element;
        napi_get_element(env, args[0], i, &element);
        
        std::string word = GetString(env, element);
        int freq = g_trie->getFrequency(word);
        
        napi_value freqVal;
        napi_create_int32(env, freq, &freqVal);
        napi_set_element(env, result, i, freqVal);
    }
    
    return result;
}

napi_value BatchGetSuggestions(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    
    if (argc < 2) {
        napi_throw_error(env, nullptr, "batchGetSuggestions requires (prefixes, limit)");
        return nullptr;
    }
    
    bool isArray = false;
    napi_is_array(env, args[0], &isArray);
    if (!isArray) {
        napi_throw_type_error(env, nullptr, "First argument must be an array");
        return nullptr;
    }
    
    uint32_t length = 0;
    napi_get_array_length(env, args[0], &length);
    
    int32_t limit = 10;
    napi_get_value_int32(env, args[1], &limit);
    if (limit < 1) limit = 1;
    if (limit > 50) limit = 50;
    
    napi_value result;
    napi_create_array_with_length(env, length, &result);
    
    std::lock_guard<std::mutex> lock(g_trieMutex);
    
    if (!g_suggestEngine) {
        // Return empty arrays if no engine
        for (uint32_t i = 0; i < length; ++i) {
            napi_value emptyArr;
            napi_create_array(env, &emptyArr);
            napi_set_element(env, result, i, emptyArr);
        }
        return result;
    }
    
    for (uint32_t i = 0; i < length; ++i) {
        napi_value element;
        napi_get_element(env, args[0], i, &element);
        
        std::string prefix = GetString(env, element);
        auto suggestions = g_suggestEngine->getSuggestions(prefix, limit);
        
        napi_value suggestArr;
        napi_create_array_with_length(env, suggestions.size(), &suggestArr);
        
        for (size_t j = 0; j < suggestions.size(); ++j) {
            napi_value obj;
            napi_create_object(env, &obj);
            
            napi_set_named_property(env, obj, "word", CreateString(env, suggestions[j].word));
            
            napi_value scoreVal;
            napi_create_double(env, suggestions[j].score, &scoreVal);
            napi_set_named_property(env, obj, "score", scoreVal);
            
            napi_value errorTypeVal;
            napi_create_int32(env, static_cast<int>(suggestions[j].errorType), &errorTypeVal);
            napi_set_named_property(env, obj, "errorType", errorTypeVal);
            
            napi_set_element(env, suggestArr, j, obj);
        }
        
        napi_set_element(env, result, i, suggestArr);
    }
    
    return result;
}

napi_value ProcessInputBatch(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    
    if (argc < 1) {
        napi_throw_error(env, nullptr, "processInputBatch requires request object");
        return nullptr;
    }
    
    // Extract request properties
    napi_value currentWordVal, prevWordVal, limitVal, checkAutocorrectVal, applyRulesVal;
    napi_get_named_property(env, args[0], "currentWord", &currentWordVal);
    napi_get_named_property(env, args[0], "prevWord", &prevWordVal);
    napi_get_named_property(env, args[0], "suggestionLimit", &limitVal);
    napi_get_named_property(env, args[0], "checkAutocorrect", &checkAutocorrectVal);
    napi_get_named_property(env, args[0], "applyRules", &applyRulesVal);
    
    std::string currentWord = GetString(env, currentWordVal);
    std::string prevWord;
    
    napi_valuetype prevWordType;
    napi_typeof(env, prevWordVal, &prevWordType);
    if (prevWordType == napi_string) {
        prevWord = GetString(env, prevWordVal);
    }
    
    int32_t limit = 10;
    napi_valuetype limitType;
    napi_typeof(env, limitVal, &limitType);
    if (limitType == napi_number) {
        napi_get_value_int32(env, limitVal, &limit);
    }
    
    bool checkAutocorrect = true;
    napi_valuetype checkType;
    napi_typeof(env, checkAutocorrectVal, &checkType);
    if (checkType == napi_boolean) {
        napi_get_value_bool(env, checkAutocorrectVal, &checkAutocorrect);
    }
    
    bool applyRules = true;
    napi_valuetype applyType;
    napi_typeof(env, applyRulesVal, &applyType);
    if (applyType == napi_boolean) {
        napi_get_value_bool(env, applyRulesVal, &applyRules);
    }
    
    // Create response object
    napi_value response;
    napi_create_object(env, &response);
    
    // Lock and process
    std::lock_guard<std::mutex> lock(g_trieMutex);
    
    // 1. Check existence
    bool exists = false;
    int frequency = 0;
    if (g_trie) {
        exists = g_trie->contains(currentWord);
        if (exists) {
            frequency = g_trie->getFrequency(currentWord);
        }
    }
    
    napi_value existsVal, freqVal;
    napi_get_boolean(env, exists, &existsVal);
    napi_create_int32(env, frequency, &freqVal);
    napi_set_named_property(env, response, "exists", existsVal);
    napi_set_named_property(env, response, "frequency", freqVal);
    
    // 2. Get suggestions
    napi_value suggestArr;
    if (g_suggestEngine) {
        auto suggestions = g_suggestEngine->getSuggestions(currentWord, limit);
        napi_create_array_with_length(env, suggestions.size(), &suggestArr);
        
        for (size_t i = 0; i < suggestions.size(); ++i) {
            napi_value obj;
            napi_create_object(env, &obj);
            napi_set_named_property(env, obj, "word", CreateString(env, suggestions[i].word));
            
            napi_value scoreVal;
            napi_create_double(env, suggestions[i].score, &scoreVal);
            napi_set_named_property(env, obj, "score", scoreVal);
            
            napi_set_element(env, suggestArr, i, obj);
        }
    } else {
        napi_create_array(env, &suggestArr);
    }
    napi_set_named_property(env, response, "suggestions", suggestArr);
    
    // 3. Check autocorrection
    if (checkAutocorrect && g_suggestEngine && !exists) {
        auto correction = g_suggestEngine->findAutocorrection(currentWord, 0.185);
        if (!correction.word.empty()) {
            napi_value corrObj;
            napi_create_object(env, &corrObj);
            napi_set_named_property(env, corrObj, "word", CreateString(env, correction.word));
            
            napi_value corrScore;
            napi_create_double(env, correction.score, &corrScore);
            napi_set_named_property(env, corrObj, "score", corrScore);
            
            napi_set_named_property(env, response, "autocorrection", corrObj);
        }
    }
    
    // 4. Apply manual autocorrect rules
    if (applyRules) {
        std::lock_guard<std::mutex> rulesLock(g_rulesMutex);
        if (g_autocorrectRules) {
            std::string corrected = g_autocorrectRules->apply(currentWord);
            if (corrected != currentWord) {
                napi_set_named_property(env, response, "ruleApplied", 
                                       CreateString(env, corrected));
            }
        }
    }
    
    return response;
}

/**
 * loadAutocorrectRules(language: string): boolean
 * Load default autocorrect rules for language
 */
static napi_value LoadAutocorrectRules(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    
    std::string language = "ru";  // Default to Russian
    if (argc >= 1) {
        language = GetString(env, args[0]);
    }
    
    std::lock_guard<std::mutex> lock(g_rulesMutex);
    
    if (!g_autocorrectRules) {
        g_autocorrectRules = std::make_unique<AutocorrectRules>();
    }
    
    g_autocorrectRules->clearRules();
    
    if (language == "ru" || language == "rus" || language == "russian") {
        g_autocorrectRules->loadDefaultRussianRules();
        OH_LOG_INFO(LOG_APP, "Loaded %zu Russian autocorrect rules", 
                   g_autocorrectRules->getRuleCount());
    } else if (language == "en" || language == "eng" || language == "english") {
        g_autocorrectRules->loadDefaultEnglishRules();
        OH_LOG_INFO(LOG_APP, "Loaded %zu English autocorrect rules", 
                   g_autocorrectRules->getRuleCount());
    }
    
    napi_value result;
    napi_get_boolean(env, g_autocorrectRules->getRuleCount() > 0, &result);
    return result;
}

/**
 * addAutocorrectRule(wrong: string, correct: string): void
 * Add a custom autocorrect rule
 */
static napi_value AddAutocorrectRule(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    
    if (argc < 2) {
        napi_throw_error(env, nullptr, "addAutocorrectRule requires (wrong, correct)");
        return nullptr;
    }
    
    std::string wrong = GetString(env, args[0]);
    std::string correct = GetString(env, args[1]);
    
    std::lock_guard<std::mutex> lock(g_rulesMutex);
    
    if (!g_autocorrectRules) {
        g_autocorrectRules = std::make_unique<AutocorrectRules>();
    }
    
    g_autocorrectRules->addRule(wrong, correct);
    
    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

/**
 * applyAutocorrectRule(word: string): string
 * Apply autocorrect rules to a word
 */
static napi_value ApplyAutocorrectRule(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    
    if (argc < 1) {
        napi_throw_error(env, nullptr, "applyAutocorrectRule requires word");
        return nullptr;
    }
    
    std::string word = GetString(env, args[0]);
    std::string result = word;
    
    {
        std::lock_guard<std::mutex> lock(g_rulesMutex);
        if (g_autocorrectRules) {
            result = g_autocorrectRules->apply(word);
        }
    }
    
    return CreateString(env, result);
}

napi_value RegisterBatchOperations(napi_env env) {
    napi_value exports;
    napi_create_object(env, &exports);
    
    napi_property_descriptor descriptors[] = {
        { "batchContains", nullptr, BatchContains, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "batchGetFrequency", nullptr, BatchGetFrequency, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "batchGetSuggestions", nullptr, BatchGetSuggestions, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "processInputBatch", nullptr, ProcessInputBatch, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "loadAutocorrectRules", nullptr, LoadAutocorrectRules, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "addAutocorrectRule", nullptr, AddAutocorrectRule, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "applyAutocorrectRule", nullptr, ApplyAutocorrectRule, nullptr, nullptr, nullptr, napi_default, nullptr },
    };
    
    napi_define_properties(env, exports, sizeof(descriptors) / sizeof(descriptors[0]), descriptors);
    return exports;
}

} // namespace latinime
