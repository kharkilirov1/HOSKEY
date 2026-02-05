/**
 * HOSKEY Keyboard - NAPI Utilities Implementation
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#include "napi_utils.h"
#include <cstring>

namespace keyboard {
namespace napi {

// ============================================================================
// Argument Extraction
// ============================================================================

bool getStringArg(napi_env env, napi_value value, std::string& out) {
    size_t strLen = 0;
    if (napi_get_value_string_utf8(env, value, nullptr, 0, &strLen) != napi_ok) {
        return false;
    }

    out.resize(strLen);
    if (napi_get_value_string_utf8(env, value, &out[0], strLen + 1, &strLen) != napi_ok) {
        return false;
    }

    return true;
}

bool getNumberArg(napi_env env, napi_value value, double& out) {
    return napi_get_value_double(env, value, &out) == napi_ok;
}

bool getIntArg(napi_env env, napi_value value, int32_t& out) {
    return napi_get_value_int32(env, value, &out) == napi_ok;
}

bool getBoolArg(napi_env env, napi_value value, bool& out) {
    return napi_get_value_bool(env, value, &out) == napi_ok;
}

bool getObjectStringProp(napi_env env, napi_value obj,
                          const char* name, std::string& out) {
    napi_value prop;
    if (napi_get_named_property(env, obj, name, &prop) != napi_ok) {
        return false;
    }
    return getStringArg(env, prop, out);
}

bool getObjectNumberProp(napi_env env, napi_value obj,
                          const char* name, double& out) {
    napi_value prop;
    if (napi_get_named_property(env, obj, name, &prop) != napi_ok) {
        return false;
    }
    return getNumberArg(env, prop, out);
}

bool getObjectIntProp(napi_env env, napi_value obj,
                       const char* name, int32_t& out) {
    napi_value prop;
    if (napi_get_named_property(env, obj, name, &prop) != napi_ok) {
        return false;
    }
    return getIntArg(env, prop, out);
}

bool getObjectBoolProp(napi_env env, napi_value obj,
                        const char* name, bool& out) {
    napi_value prop;
    if (napi_get_named_property(env, obj, name, &prop) != napi_ok) {
        return false;
    }
    return getBoolArg(env, prop, out);
}

bool hasProperty(napi_env env, napi_value obj, const char* name) {
    bool hasProp = false;
    napi_has_named_property(env, obj, name, &hasProp);
    return hasProp;
}

// ============================================================================
// Result Creation
// ============================================================================

napi_value createString(napi_env env, const std::string& str) {
    napi_value result;
    napi_create_string_utf8(env, str.c_str(), str.length(), &result);
    return result;
}

napi_value createString(napi_env env, const char* str) {
    napi_value result;
    napi_create_string_utf8(env, str, NAPI_AUTO_LENGTH, &result);
    return result;
}

napi_value createNumber(napi_env env, double value) {
    napi_value result;
    napi_create_double(env, value, &result);
    return result;
}

napi_value createInt(napi_env env, int32_t value) {
    napi_value result;
    napi_create_int32(env, value, &result);
    return result;
}

napi_value createBool(napi_env env, bool value) {
    napi_value result;
    napi_get_boolean(env, value, &result);
    return result;
}

napi_value createUndefined(napi_env env) {
    napi_value result;
    napi_get_undefined(env, &result);
    return result;
}

napi_value createNull(napi_env env) {
    napi_value result;
    napi_get_null(env, &result);
    return result;
}

napi_value createObject(napi_env env) {
    napi_value result;
    napi_create_object(env, &result);
    return result;
}

napi_value createArray(napi_env env, size_t length) {
    napi_value result;
    napi_create_array_with_length(env, length, &result);
    return result;
}

void setProperty(napi_env env, napi_value obj,
                 const char* name, napi_value value) {
    napi_set_named_property(env, obj, name, value);
}

void setArrayElement(napi_env env, napi_value arr,
                     size_t index, napi_value value) {
    napi_set_element(env, arr, static_cast<uint32_t>(index), value);
}

// ============================================================================
// Error Handling
// ============================================================================

napi_value createError(napi_env env, KeyboardErrorCode code,
                        const char* message) {
    napi_value result = createObject(env);

    setProperty(env, result, "ok", createBool(env, false));
    setProperty(env, result, "code", createString(env, kb_error_string(code)));

    if (message) {
        setProperty(env, result, "message", createString(env, message));
    } else {
        setProperty(env, result, "message", createString(env, kb_error_string(code)));
    }

    setProperty(env, result, "recoverable", createBool(env, kb_is_recoverable(code)));

    return result;
}

napi_value createSuccess(napi_env env, napi_value data) {
    napi_value result = createObject(env);
    setProperty(env, result, "ok", createBool(env, true));

    if (data) {
        setProperty(env, result, "data", data);
    }

    return result;
}

void throwError(napi_env env, const char* message) {
    napi_throw_error(env, nullptr, message);
}

void throwTypeError(napi_env env, const char* message) {
    napi_throw_type_error(env, nullptr, message);
}

// ============================================================================
// Prediction Result Helpers
// ============================================================================

const char* sourceToString(KBCandidateSource source) {
    switch (source) {
        case KB_SOURCE_NEURAL: return "nn";
        case KB_SOURCE_NGRAM: return "ngram";
        case KB_SOURCE_TRIE: return "trie";
        case KB_SOURCE_RULE: return "rule";
        case KB_SOURCE_PERSONAL: return "personal";
        case KB_SOURCE_SHORTCUT: return "shortcut";
        case KB_SOURCE_EMOJI: return "emoji";
        case KB_SOURCE_MIXED: return "mixed";
        default: return "unknown";
    }
}

napi_value predictResultToNapi(napi_env env, const KBPredictResult& result) {
    napi_value obj = createObject(env);

    // Create candidates array
    napi_value candidates = createArray(env, result.candidateCount);

    for (uint32_t i = 0; i < result.candidateCount; ++i) {
        const auto& c = result.candidates[i];

        napi_value candidate = createObject(env);
        setProperty(env, candidate, "text", createString(env, c.text ? c.text : ""));
        setProperty(env, candidate, "score", createNumber(env, c.score));
        setProperty(env, candidate, "confidence", createNumber(env, c.confidence));
        setProperty(env, candidate, "source", createString(env, sourceToString(c.source)));

        if (c.isExactMatch) {
            setProperty(env, candidate, "isExactMatch", createBool(env, true));
        }
        if (c.isAutocorrect) {
            setProperty(env, candidate, "isAutocorrect", createBool(env, true));
        }

        setArrayElement(env, candidates, i, candidate);
    }

    setProperty(env, obj, "candidates", candidates);

    // Latency (convert microseconds to milliseconds)
    setProperty(env, obj, "latencyMs", createNumber(env, result.totalUs / 1000.0));

    // Source statistics
    napi_value sourceStats = createObject(env);
    setProperty(env, sourceStats, "neural", createInt(env, result.neuralCandidates));
    setProperty(env, sourceStats, "ngram", createInt(env, result.ngramCandidates));
    setProperty(env, sourceStats, "trie", createInt(env, result.trieCandidates));
    setProperty(env, sourceStats, "rule", createInt(env, result.ruleCandidates));
    setProperty(env, obj, "sourceStats", sourceStats);

    // Trace ID
    if (result.traceId) {
        setProperty(env, obj, "traceId", createString(env, result.traceId));
    }

    // Fallback flag
    if (result.usedFallback) {
        setProperty(env, obj, "usedFallback", createBool(env, true));
    }

    return obj;
}

bool parsePredictContext(napi_env env, napi_value obj, KBPredictContext& ctx,
                          std::string& inputBuffer, std::string& prevWord1Buffer) {
    // Reset context
    std::memset(&ctx, 0, sizeof(ctx));
    ctx.maxResults = 10;
    ctx.deadlineMs = 20;

    // Parse input
    if (hasProperty(env, obj, "input")) {
        if (!getObjectStringProp(env, obj, "input", inputBuffer)) {
            return false;
        }
        ctx.inputText = inputBuffer.c_str();
        ctx.inputLength = static_cast<uint32_t>(inputBuffer.length());
    }

    // Parse prevWord
    if (hasProperty(env, obj, "prevWord")) {
        if (getObjectStringProp(env, obj, "prevWord", prevWord1Buffer)) {
            ctx.prevWord1 = prevWord1Buffer.c_str();
        }
    }

    // Parse maxResults
    int32_t maxResults = 0;
    if (hasProperty(env, obj, "maxResults") &&
        getObjectIntProp(env, obj, "maxResults", maxResults)) {
        ctx.maxResults = static_cast<uint32_t>(maxResults);
    }

    // Parse deadlineMs
    int32_t deadlineMs = 0;
    if (hasProperty(env, obj, "deadlineMs") &&
        getObjectIntProp(env, obj, "deadlineMs", deadlineMs)) {
        ctx.deadlineMs = static_cast<uint32_t>(deadlineMs);
    }

    // Parse flags
    bool isGesture = false;
    if (hasProperty(env, obj, "isGesture") &&
        getObjectBoolProp(env, obj, "isGesture", isGesture)) {
        ctx.isGesture = isGesture ? 1 : 0;
    }

    return true;
}

bool parseLearnEvent(napi_env env, napi_value obj, KBLearnEvent& event,
                      std::string& wordBuffer, std::string& prevWordBuffer) {
    std::memset(&event, 0, sizeof(event));

    // Parse type
    std::string typeStr;
    if (hasProperty(env, obj, "type") &&
        getObjectStringProp(env, obj, "type", typeStr)) {
        if (typeStr == "word_selected") {
            event.type = KB_LEARN_WORD_SELECTED;
        } else if (typeStr == "word_typed") {
            event.type = KB_LEARN_WORD_TYPED;
        } else if (typeStr == "word_deleted") {
            event.type = KB_LEARN_WORD_DELETED;
        } else if (typeStr == "bigram") {
            event.type = KB_LEARN_BIGRAM;
        } else if (typeStr == "undo") {
            event.type = KB_LEARN_UNDO;
        } else {
            event.type = KB_LEARN_WORD_SELECTED;
        }
    }

    // Parse word
    if (hasProperty(env, obj, "word")) {
        if (!getObjectStringProp(env, obj, "word", wordBuffer)) {
            return false;
        }
        event.word = wordBuffer.c_str();
    }

    // Parse prevWord
    if (hasProperty(env, obj, "prevWord") &&
        getObjectStringProp(env, obj, "prevWord", prevWordBuffer)) {
        event.prevWord = prevWordBuffer.c_str();
    }

    return event.word != nullptr;
}

// ============================================================================
// Validation
// ============================================================================

bool validateArgCount(napi_env env, napi_callback_info info,
                      size_t minArgs, size_t maxArgs,
                      napi_value* args, size_t& actualCount) {
    actualCount = maxArgs;
    napi_get_cb_info(env, info, &actualCount, args, nullptr, nullptr);

    if (actualCount < minArgs) {
        throwTypeError(env, "Insufficient arguments");
        return false;
    }

    return true;
}

static napi_valuetype getValueType(napi_env env, napi_value value) {
    napi_valuetype type;
    napi_typeof(env, value, &type);
    return type;
}

bool isString(napi_env env, napi_value value) {
    return getValueType(env, value) == napi_string;
}

bool isNumber(napi_env env, napi_value value) {
    return getValueType(env, value) == napi_number;
}

bool isObject(napi_env env, napi_value value) {
    napi_valuetype type = getValueType(env, value);
    return type == napi_object;
}

bool isArray(napi_env env, napi_value value) {
    bool result = false;
    napi_is_array(env, value, &result);
    return result;
}

bool isFunction(napi_env env, napi_value value) {
    return getValueType(env, value) == napi_function;
}

bool isUndefined(napi_env env, napi_value value) {
    return getValueType(env, value) == napi_undefined;
}

bool isNull(napi_env env, napi_value value) {
    return getValueType(env, value) == napi_null;
}

} // namespace napi
} // namespace keyboard
