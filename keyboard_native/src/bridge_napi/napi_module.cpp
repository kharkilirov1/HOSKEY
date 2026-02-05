/**
 * HOSKEY Keyboard - NAPI Module
 *
 * N-API bindings for keyboard prediction engine.
 * Exports: init, predict, learn, resetSession, close, getVersion
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#include "napi/native_api.h"
#include "napi_utils.h"
#include "../core/engine.h"
#include "../../include/engine_api.h"

#include <memory>
#include <mutex>

namespace keyboard {
namespace napi {

// ============================================================================
// Global Engine Instance
// ============================================================================

static std::unique_ptr<core::Engine> g_engine;
static std::mutex g_engineMutex;

// ============================================================================
// Helper: Get Engine
// ============================================================================

static core::Engine* getEngine(napi_env env) {
    if (!g_engine) {
        throwError(env, "Engine not initialized");
        return nullptr;
    }
    return g_engine.get();
}

// ============================================================================
// API: init(config: string | object): { ok: boolean, version: string }
// ============================================================================

static napi_value Init(napi_env env, napi_callback_info info) {
    std::lock_guard<std::mutex> lock(g_engineMutex);

    // Get arguments
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    // Create engine if not exists
    if (!g_engine) {
        g_engine = std::make_unique<core::Engine>();
    }

    KeyboardErrorCode err = KB_OK;

    if (argc > 0) {
        if (isString(env, args[0])) {
            // JSON config string
            std::string configJson;
            if (getStringArg(env, args[0], configJson)) {
                err = g_engine->initFromJson(configJson.c_str());
            }
        } else if (isObject(env, args[0])) {
            // Config object - convert to struct
            KBEngineConfig config;
            kb_config_init_default(&config);

            std::string dictPath, modelPath, personalPath;

            if (hasProperty(env, args[0], "dictPath") &&
                getObjectStringProp(env, args[0], "dictPath", dictPath)) {
                config.dictPath = dictPath.c_str();
            }

            if (hasProperty(env, args[0], "modelPath") &&
                getObjectStringProp(env, args[0], "modelPath", modelPath)) {
                config.modelPath = modelPath.c_str();
            }

            if (hasProperty(env, args[0], "personalDictPath") &&
                getObjectStringProp(env, args[0], "personalDictPath", personalPath)) {
                config.personalDictPath = personalPath.c_str();
            }

            bool useNeural = true;
            if (hasProperty(env, args[0], "useNeural") &&
                getObjectBoolProp(env, args[0], "useNeural", useNeural)) {
                config.useNeural = useNeural ? 1 : 0;
            }

            int32_t maxInferMs = 0;
            if (hasProperty(env, args[0], "maxInferMs") &&
                getObjectIntProp(env, args[0], "maxInferMs", maxInferMs)) {
                config.maxInferMs = static_cast<uint32_t>(maxInferMs);
            }

            int32_t cacheSize = 0;
            if (hasProperty(env, args[0], "cacheSize") &&
                getObjectIntProp(env, args[0], "cacheSize", cacheSize)) {
                config.cacheSize = static_cast<uint32_t>(cacheSize);
            }

            err = g_engine->init(config);
        }
    } else {
        // Default config
        KBEngineConfig config;
        kb_config_init_default(&config);
        err = g_engine->init(config);
    }

    // Build result
    napi_value result = createObject(env);
    setProperty(env, result, "ok", createBool(env, err == KB_OK));
    setProperty(env, result, "version", createString(env, KB_VERSION_STRING));

    if (err != KB_OK) {
        setProperty(env, result, "error", createString(env, kb_error_string(err)));
    }

    return result;
}

// ============================================================================
// API: predict(context: object): { candidates, latencyMs, sourceStats }
// ============================================================================

static napi_value Predict(napi_env env, napi_callback_info info) {
    auto* engine = getEngine(env);
    if (!engine) {
        return createError(env, KB_ERR_NOT_INITIALIZED);
    }

    // Get arguments
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 1 || !isObject(env, args[0])) {
        return createError(env, KB_ERR_INVALID_ARG, "Expected context object");
    }

    // Parse context
    KBPredictContext ctx;
    std::string inputBuffer, prevWord1Buffer;

    if (!parsePredictContext(env, args[0], ctx, inputBuffer, prevWord1Buffer)) {
        return createError(env, KB_ERR_INVALID_ARG, "Invalid context object");
    }

    // Run prediction
    KBPredictResult result = {};
    KeyboardErrorCode err = engine->predict(ctx, result);

    if (err != KB_OK) {
        return createError(env, err);
    }

    // Convert to NAPI
    napi_value output = predictResultToNapi(env, result);

    // Free result
    if (result.candidates) {
        for (uint32_t i = 0; i < result.candidateCount; ++i) {
            free(result.candidates[i].text);
        }
        delete[] result.candidates;
    }
    if (result.traceId) {
        free(result.traceId);
    }

    return output;
}

// ============================================================================
// API: learn(event: object): { ok: boolean }
// ============================================================================

static napi_value Learn(napi_env env, napi_callback_info info) {
    auto* engine = getEngine(env);
    if (!engine) {
        return createError(env, KB_ERR_NOT_INITIALIZED);
    }

    // Get arguments
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 1 || !isObject(env, args[0])) {
        return createError(env, KB_ERR_INVALID_ARG, "Expected event object");
    }

    // Parse event
    KBLearnEvent event;
    std::string wordBuffer, prevWordBuffer;

    if (!parseLearnEvent(env, args[0], event, wordBuffer, prevWordBuffer)) {
        return createError(env, KB_ERR_INVALID_ARG, "Invalid event object");
    }

    // Process learning
    KeyboardErrorCode err = engine->learn(event);

    // Build result
    napi_value result = createObject(env);
    setProperty(env, result, "ok", createBool(env, err == KB_OK));

    if (err != KB_OK) {
        setProperty(env, result, "error", createString(env, kb_error_string(err)));
    }

    return result;
}

// ============================================================================
// API: resetSession(): { ok: boolean }
// ============================================================================

static napi_value ResetSession(napi_env env, napi_callback_info /*info*/) {
    auto* engine = getEngine(env);
    if (!engine) {
        return createError(env, KB_ERR_NOT_INITIALIZED);
    }

    KeyboardErrorCode err = engine->resetSession();

    napi_value result = createObject(env);
    setProperty(env, result, "ok", createBool(env, err == KB_OK));

    return result;
}

// ============================================================================
// API: close(): { ok: boolean }
// ============================================================================

static napi_value Close(napi_env env, napi_callback_info /*info*/) {
    std::lock_guard<std::mutex> lock(g_engineMutex);

    if (g_engine) {
        g_engine->shutdown();
        g_engine.reset();
    }

    napi_value result = createObject(env);
    setProperty(env, result, "ok", createBool(env, true));

    return result;
}

// ============================================================================
// API: getVersion(): string
// ============================================================================

static napi_value GetVersion(napi_env env, napi_callback_info /*info*/) {
    return createString(env, KB_VERSION_STRING);
}

// ============================================================================
// API: getStatus(): object
// ============================================================================

static napi_value GetStatus(napi_env env, napi_callback_info /*info*/) {
    auto* engine = getEngine(env);
    if (!engine) {
        return createError(env, KB_ERR_NOT_INITIALIZED);
    }

    KBEngineStatus status;
    KeyboardErrorCode err = engine->getStatus(status);

    if (err != KB_OK) {
        return createError(env, err);
    }

    napi_value result = createObject(env);

    const char* stateStr = "unknown";
    switch (status.state) {
        case KB_STATE_UNINITIALIZED: stateStr = "uninitialized"; break;
        case KB_STATE_INITIALIZING: stateStr = "initializing"; break;
        case KB_STATE_READY: stateStr = "ready"; break;
        case KB_STATE_CLOSING: stateStr = "closing"; break;
        case KB_STATE_CLOSED: stateStr = "closed"; break;
        case KB_STATE_ERROR: stateStr = "error"; break;
    }

    setProperty(env, result, "state", createString(env, stateStr));
    setProperty(env, result, "neuralReady", createBool(env, status.neuralReady));
    setProperty(env, result, "dictReady", createBool(env, status.dictReady));
    setProperty(env, result, "personalDictReady", createBool(env, status.personalDictReady));
    setProperty(env, result, "dictWordCount", createInt(env, status.dictWordCount));
    setProperty(env, result, "personalWordCount", createInt(env, status.personalWordCount));
    setProperty(env, result, "totalPredictions",
                createNumber(env, static_cast<double>(status.totalPredictions)));
    setProperty(env, result, "cacheHits",
                createNumber(env, static_cast<double>(status.cacheHits)));
    setProperty(env, result, "cacheMisses",
                createNumber(env, static_cast<double>(status.cacheMisses)));
    setProperty(env, result, "fallbackCount",
                createNumber(env, static_cast<double>(status.fallbackCount)));

    // Latency stats
    napi_value latency = createObject(env);
    setProperty(env, latency, "avgUs", createInt(env, status.avgLatencyUs));
    setProperty(env, latency, "p50Us", createInt(env, status.p50LatencyUs));
    setProperty(env, latency, "p95Us", createInt(env, status.p95LatencyUs));
    setProperty(env, latency, "p99Us", createInt(env, status.p99LatencyUs));
    setProperty(env, result, "latency", latency);

    setProperty(env, result, "memoryUsage",
                createNumber(env, static_cast<double>(status.memoryUsage)));

    return result;
}

// ============================================================================
// API: loadDictionary(path: string, type?: number): { ok: boolean }
// ============================================================================

static napi_value LoadDictionary(napi_env env, napi_callback_info info) {
    auto* engine = getEngine(env);
    if (!engine) {
        return createError(env, KB_ERR_NOT_INITIALIZED);
    }

    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 1 || !isString(env, args[0])) {
        return createError(env, KB_ERR_INVALID_ARG, "Expected path string");
    }

    std::string path;
    if (!getStringArg(env, args[0], path)) {
        return createError(env, KB_ERR_INVALID_ARG);
    }

    int32_t dictType = 0;
    if (argc > 1) {
        getIntArg(env, args[1], dictType);
    }

    KeyboardErrorCode err = engine->loadDictionary(path.c_str(), dictType);

    napi_value result = createObject(env);
    setProperty(env, result, "ok", createBool(env, err == KB_OK));

    if (err != KB_OK) {
        setProperty(env, result, "error", createString(env, kb_error_string(err)));
    }

    return result;
}

// ============================================================================
// API: loadModel(path: string): { ok: boolean }
// ============================================================================

static napi_value LoadModel(napi_env env, napi_callback_info info) {
    auto* engine = getEngine(env);
    if (!engine) {
        return createError(env, KB_ERR_NOT_INITIALIZED);
    }

    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 1 || !isString(env, args[0])) {
        return createError(env, KB_ERR_INVALID_ARG, "Expected path string");
    }

    std::string path;
    if (!getStringArg(env, args[0], path)) {
        return createError(env, KB_ERR_INVALID_ARG);
    }

    KeyboardErrorCode err = engine->loadModel(path.c_str());

    napi_value result = createObject(env);
    setProperty(env, result, "ok", createBool(env, err == KB_OK));

    if (err != KB_OK) {
        setProperty(env, result, "error", createString(env, kb_error_string(err)));
    }

    return result;
}

// ============================================================================
// Module Registration
// ============================================================================

static napi_value RegisterModule(napi_env env, napi_value exports) {
    napi_property_descriptor desc[] = {
        {"init", nullptr, Init, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"predict", nullptr, Predict, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"learn", nullptr, Learn, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"resetSession", nullptr, ResetSession, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"close", nullptr, Close, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"getVersion", nullptr, GetVersion, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"getStatus", nullptr, GetStatus, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"loadDictionary", nullptr, LoadDictionary, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"loadModel", nullptr, LoadModel, nullptr, nullptr, nullptr, napi_default, nullptr},
    };

    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);

    return exports;
}

NAPI_MODULE(keyboard_native, RegisterModule)

} // namespace napi
} // namespace keyboard

// ============================================================================
// C API Implementation (for engine_api.h)
// ============================================================================

extern "C" {

const char* kb_error_string(KeyboardErrorCode code) {
    switch (code) {
        case KB_OK: return "OK";
        case KB_ERR_UNKNOWN: return "ERR_UNKNOWN";
        case KB_ERR_INVALID_ARG: return "ERR_INVALID_ARG";
        case KB_ERR_NULL_POINTER: return "ERR_NULL_POINTER";
        case KB_ERR_OUT_OF_MEMORY: return "ERR_OUT_OF_MEMORY";
        case KB_ERR_BUFFER_TOO_SMALL: return "ERR_BUFFER_TOO_SMALL";
        case KB_ERR_INVALID_STATE: return "ERR_INVALID_STATE";
        case KB_ERR_TIMEOUT: return "ERR_TIMEOUT";
        case KB_ERR_CANCELLED: return "ERR_CANCELLED";
        case KB_ERR_NOT_INITIALIZED: return "ERR_NOT_INITIALIZED";
        case KB_ERR_ALREADY_INITIALIZED: return "ERR_ALREADY_INITIALIZED";
        case KB_ERR_INIT_FAILED: return "ERR_INIT_FAILED";
        case KB_ERR_CONFIG_INVALID: return "ERR_CONFIG_INVALID";
        case KB_ERR_CONFIG_PARSE_FAILED: return "ERR_CONFIG_PARSE_FAILED";
        case KB_ERR_MODEL_NOT_FOUND: return "ERR_MODEL_NOT_FOUND";
        case KB_ERR_MODEL_LOAD_FAILED: return "ERR_MODEL_LOAD_FAILED";
        case KB_ERR_MODEL_INVALID_FORMAT: return "ERR_MODEL_INVALID_FORMAT";
        case KB_ERR_MODEL_VERSION_MISMATCH: return "ERR_MODEL_VERSION_MISMATCH";
        case KB_ERR_MODEL_CORRUPTED: return "ERR_MODEL_CORRUPTED";
        case KB_ERR_INFER_FAILED: return "ERR_INFER_FAILED";
        case KB_ERR_INFER_TIMEOUT: return "ERR_INFER_TIMEOUT";
        case KB_ERR_MODEL_NOT_READY: return "ERR_MODEL_NOT_READY";
        case KB_ERR_DICT_NOT_FOUND: return "ERR_DICT_NOT_FOUND";
        case KB_ERR_DICT_LOAD_FAILED: return "ERR_DICT_LOAD_FAILED";
        case KB_ERR_DICT_INVALID_FORMAT: return "ERR_DICT_INVALID_FORMAT";
        case KB_ERR_DICT_CORRUPTED: return "ERR_DICT_CORRUPTED";
        case KB_ERR_DICT_TOO_LARGE: return "ERR_DICT_TOO_LARGE";
        case KB_ERR_DICT_EMPTY: return "ERR_DICT_EMPTY";
        case KB_ERR_SESSION_INVALID: return "ERR_SESSION_INVALID";
        case KB_ERR_SESSION_EXPIRED: return "ERR_SESSION_EXPIRED";
        case KB_ERR_SESSION_LIMIT_REACHED: return "ERR_SESSION_LIMIT_REACHED";
        case KB_ERR_FILE_NOT_FOUND: return "ERR_FILE_NOT_FOUND";
        case KB_ERR_FILE_READ_FAILED: return "ERR_FILE_READ_FAILED";
        case KB_ERR_FILE_WRITE_FAILED: return "ERR_FILE_WRITE_FAILED";
        case KB_ERR_PERMISSION_DENIED: return "ERR_PERMISSION_DENIED";
        case KB_ERR_IO_ERROR: return "ERR_IO_ERROR";
        case KB_ERR_THREAD_CREATE_FAILED: return "ERR_THREAD_CREATE_FAILED";
        case KB_ERR_MUTEX_LOCK_FAILED: return "ERR_MUTEX_LOCK_FAILED";
        case KB_ERR_DEADLOCK_DETECTED: return "ERR_DEADLOCK_DETECTED";
        default: return "ERR_UNKNOWN";
    }
}

const char* kb_get_version(void) {
    return KB_VERSION_STRING;
}

int kb_get_abi_version(void) {
    return KB_ABI_VERSION;
}

int kb_has_mindspore(void) {
#ifdef USE_MINDSPORE
    return 1;
#else
    return 0;
#endif
}

void kb_free_string(char* str) {
    free(str);
}

void kb_free_result(KBPredictResult* result) {
    if (!result) return;

    if (result->candidates) {
        for (uint32_t i = 0; i < result->candidateCount; ++i) {
            free(result->candidates[i].text);
        }
        delete[] result->candidates;
    }

    if (result->traceId) {
        free(result->traceId);
    }

    delete result;
}

} // extern "C"
