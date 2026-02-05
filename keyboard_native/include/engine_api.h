/**
 * HOSKEY Keyboard Native Engine - Public C API
 *
 * Stable C ABI for keyboard prediction engine.
 * This is the primary interface for NAPI bindings and external consumers.
 *
 * Thread Safety:
 * - All functions are thread-safe unless noted otherwise
 * - Engine instance can be used from multiple threads
 * - Results must be freed on the same thread or with proper synchronization
 *
 * Memory Management:
 * - All returned strings/results must be freed via kb_free_*() functions
 * - Input strings are borrowed (not owned) for the duration of the call
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#ifndef KEYBOARD_NATIVE_ENGINE_API_H
#define KEYBOARD_NATIVE_ENGINE_API_H

#include "types.h"
#include "error_codes.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Library Export Macros
// ============================================================================

#if defined(_WIN32) || defined(__CYGWIN__)
    #ifdef KB_BUILDING_DLL
        #define KB_API __declspec(dllexport)
    #else
        #define KB_API __declspec(dllimport)
    #endif
#else
    #if __GNUC__ >= 4
        #define KB_API __attribute__((visibility("default")))
    #else
        #define KB_API
    #endif
#endif

// ============================================================================
// Version & Info
// ============================================================================

/**
 * Get library version string
 * @return Static string, do not free (e.g., "1.0.0")
 */
KB_API const char* kb_get_version(void);

/**
 * Get ABI version for compatibility checking
 * @return ABI version number
 */
KB_API int kb_get_abi_version(void);

/**
 * Check if library was compiled with MindSpore support
 * @return 1 if MindSpore enabled, 0 otherwise
 */
KB_API int kb_has_mindspore(void);

// ============================================================================
// Engine Lifecycle
// ============================================================================

/**
 * Create engine from JSON configuration string
 *
 * @param config_json JSON configuration string (UTF-8, null-terminated)
 *                    If NULL, uses default configuration
 * @param out_handle  Output: engine handle on success
 * @return KB_OK on success, error code on failure
 *
 * JSON config fields (all optional):
 * {
 *   "dictPath": "/path/to/dict.bin",
 *   "modelPath": "/path/to/model.ms",
 *   "personalDictPath": "/path/to/personal.db",
 *   "useNeural": true,
 *   "neuralThreads": 2,
 *   "maxInferMs": 20,
 *   "cacheSize": 128,
 *   "fallbackPolicy": "ngram_trie_rule",
 *   "weights": {
 *     "neural": 0.30,
 *     "ngram": 0.15,
 *     "dict": 0.35,
 *     "personal": 0.20
 *   }
 * }
 */
KB_API KeyboardErrorCode kb_create_engine(
    const char* config_json,
    KBEngineHandle** out_handle
);

/**
 * Create engine from config struct
 *
 * @param config      Configuration struct (can be stack-allocated)
 * @param out_handle  Output: engine handle on success
 * @return KB_OK on success, error code on failure
 */
KB_API KeyboardErrorCode kb_create_engine_with_config(
    const KBEngineConfig* config,
    KBEngineHandle** out_handle
);

/**
 * Close and destroy engine
 * Safe to call multiple times (idempotent)
 *
 * @param handle Engine handle (can be NULL)
 */
KB_API void kb_close_engine(KBEngineHandle* handle);

/**
 * Get engine status
 *
 * @param handle     Engine handle
 * @param out_status Output: status struct
 * @return KB_OK on success, error code on failure
 */
KB_API KeyboardErrorCode kb_get_status(
    KBEngineHandle* handle,
    KBEngineStatus* out_status
);

/**
 * Get engine state
 *
 * @param handle Engine handle
 * @return Current state, or KB_STATE_ERROR if handle is invalid
 */
KB_API KBEngineState kb_get_state(KBEngineHandle* handle);

// ============================================================================
// Prediction API
// ============================================================================

/**
 * Get prediction candidates for input context (JSON interface)
 *
 * @param handle       Engine handle
 * @param context_json JSON context string:
 *                     {
 *                       "input": "hel",
 *                       "prevWord": "say",
 *                       "maxResults": 10,
 *                       "deadlineMs": 20
 *                     }
 * @return JSON result string (must free with kb_free_string):
 *         {
 *           "candidates": [
 *             {"text": "hello", "score": 0.95, "source": "nn"},
 *             {"text": "help", "score": 0.82, "source": "ngram"}
 *           ],
 *           "latencyMs": 5.2,
 *           "sourceStats": {"nn": 2, "ngram": 1},
 *           "traceId": "abc123"
 *         }
 *         On error: {"error": "ERR_...", "message": "..."}
 */
KB_API char* kb_predict_json(
    KBEngineHandle* handle,
    const char* context_json
);

/**
 * Get prediction candidates for input context (struct interface)
 *
 * @param handle     Engine handle
 * @param context    Input context
 * @param out_result Output: result (must free with kb_free_result)
 * @return KB_OK on success, error code on failure
 */
KB_API KeyboardErrorCode kb_predict(
    KBEngineHandle* handle,
    const KBPredictContext* context,
    KBPredictResult** out_result
);

/**
 * Allocate reusable result buffer
 * Use for high-frequency prediction to avoid allocation overhead
 *
 * @param max_candidates Maximum candidates capacity
 * @return Result buffer (must free with kb_free_result)
 */
KB_API KBPredictResult* kb_alloc_result(uint32_t max_candidates);

/**
 * Predict into pre-allocated result buffer
 *
 * @param handle     Engine handle
 * @param context    Input context
 * @param result     Pre-allocated result buffer
 * @return KB_OK on success, error code on failure
 */
KB_API KeyboardErrorCode kb_predict_into(
    KBEngineHandle* handle,
    const KBPredictContext* context,
    KBPredictResult* result
);

// ============================================================================
// Learning API
// ============================================================================

/**
 * Process learning event (JSON interface)
 *
 * @param handle     Engine handle
 * @param event_json JSON event:
 *                   {
 *                     "type": "word_selected",
 *                     "word": "hello",
 *                     "prevWord": "say"
 *                   }
 * @return KB_OK on success, error code on failure
 */
KB_API KeyboardErrorCode kb_learn_json(
    KBEngineHandle* handle,
    const char* event_json
);

/**
 * Process learning event (struct interface)
 *
 * @param handle Engine handle
 * @param event  Learning event
 * @return KB_OK on success, error code on failure
 */
KB_API KeyboardErrorCode kb_learn(
    KBEngineHandle* handle,
    const KBLearnEvent* event
);

/**
 * Flush pending learning updates to persistent storage
 *
 * @param handle Engine handle
 * @return KB_OK on success, error code on failure
 */
KB_API KeyboardErrorCode kb_flush_learning(KBEngineHandle* handle);

// ============================================================================
// Session Management
// ============================================================================

/**
 * Reset current input session
 * Call when user moves to a new text field or clears input
 *
 * @param handle Engine handle
 * @return KB_OK on success, error code on failure
 */
KB_API KeyboardErrorCode kb_reset_session(KBEngineHandle* handle);

/**
 * Clear all caches
 * Useful when memory pressure is detected
 *
 * @param handle Engine handle
 * @return KB_OK on success, error code on failure
 */
KB_API KeyboardErrorCode kb_clear_cache(KBEngineHandle* handle);

// ============================================================================
// Dictionary Management
// ============================================================================

/**
 * Load dictionary from file
 *
 * @param handle   Engine handle
 * @param path     Path to dictionary file
 * @param dict_type 0 = main dictionary, 1 = personal dictionary
 * @return KB_OK on success, error code on failure
 */
KB_API KeyboardErrorCode kb_load_dictionary(
    KBEngineHandle* handle,
    const char* path,
    int dict_type
);

/**
 * Load dictionary from file descriptor (for HarmonyOS rawfile)
 *
 * @param handle    Engine handle
 * @param fd        File descriptor
 * @param offset    Offset in file
 * @param length    Length of data
 * @param dict_type 0 = main dictionary, 1 = personal dictionary
 * @return KB_OK on success, error code on failure
 */
KB_API KeyboardErrorCode kb_load_dictionary_fd(
    KBEngineHandle* handle,
    int fd,
    size_t offset,
    size_t length,
    int dict_type
);

/**
 * Add word to personal dictionary
 *
 * @param handle    Engine handle
 * @param word      Word to add (UTF-8)
 * @param frequency Initial frequency (0-255)
 * @return KB_OK on success, error code on failure
 */
KB_API KeyboardErrorCode kb_add_word(
    KBEngineHandle* handle,
    const char* word,
    int frequency
);

/**
 * Remove word from personal dictionary
 *
 * @param handle Engine handle
 * @param word   Word to remove (UTF-8)
 * @return KB_OK on success, error code on failure
 */
KB_API KeyboardErrorCode kb_remove_word(
    KBEngineHandle* handle,
    const char* word
);

/**
 * Check if word exists in any dictionary
 *
 * @param handle Engine handle
 * @param word   Word to check (UTF-8)
 * @return 1 if exists, 0 if not, -1 on error
 */
KB_API int kb_word_exists(
    KBEngineHandle* handle,
    const char* word
);

// ============================================================================
// Neural Model Management
// ============================================================================

/**
 * Load neural model
 *
 * @param handle     Engine handle
 * @param model_path Path to .ms model file
 * @return KB_OK on success, error code on failure
 */
KB_API KeyboardErrorCode kb_load_model(
    KBEngineHandle* handle,
    const char* model_path
);

/**
 * Warmup neural model (run dummy inference)
 *
 * @param handle Engine handle
 * @return KB_OK on success, error code on failure
 */
KB_API KeyboardErrorCode kb_warmup_model(KBEngineHandle* handle);

/**
 * Unload neural model to free memory
 *
 * @param handle Engine handle
 * @return KB_OK on success, error code on failure
 */
KB_API KeyboardErrorCode kb_unload_model(KBEngineHandle* handle);

/**
 * Check if neural model is ready for inference
 *
 * @param handle Engine handle
 * @return 1 if ready, 0 if not, -1 on error
 */
KB_API int kb_model_ready(KBEngineHandle* handle);

// ============================================================================
// Memory Management
// ============================================================================

/**
 * Free string returned by kb_predict_json and similar functions
 *
 * @param str String to free (can be NULL)
 */
KB_API void kb_free_string(char* str);

/**
 * Free prediction result
 *
 * @param result Result to free (can be NULL)
 */
KB_API void kb_free_result(KBPredictResult* result);

// ============================================================================
// Logging & Debug
// ============================================================================

/**
 * Log callback function type
 */
typedef void (*KBLogCallback)(int level, const char* tag, const char* message);

/**
 * Set custom log callback
 * If not set, logs go to platform default (hilog on HarmonyOS)
 *
 * @param callback Log callback function (NULL to reset to default)
 */
KB_API void kb_set_log_callback(KBLogCallback callback);

/**
 * Set log level
 *
 * @param level 0=off, 1=error, 2=warn, 3=info, 4=debug, 5=trace
 */
KB_API void kb_set_log_level(int level);

/**
 * Get detailed timing breakdown for last prediction
 *
 * @param handle Engine handle
 * @return JSON string with timing info (must free with kb_free_string)
 */
KB_API char* kb_get_timing_info(KBEngineHandle* handle);

#ifdef __cplusplus
}
#endif

#endif // KEYBOARD_NATIVE_ENGINE_API_H
