/**
 * HOSKEY Keyboard Native Engine - Error Codes
 *
 * Stable C ABI error codes for cross-boundary communication.
 * All error codes are negative, success is 0.
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#ifndef KEYBOARD_NATIVE_ERROR_CODES_H
#define KEYBOARD_NATIVE_ERROR_CODES_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Error codes enumeration
 * Negative values indicate errors, 0 = success
 */
typedef enum KeyboardErrorCode {
    // Success
    KB_OK = 0,

    // General errors (1-99)
    KB_ERR_UNKNOWN = -1,
    KB_ERR_INVALID_ARG = -2,
    KB_ERR_NULL_POINTER = -3,
    KB_ERR_OUT_OF_MEMORY = -4,
    KB_ERR_BUFFER_TOO_SMALL = -5,
    KB_ERR_INVALID_STATE = -6,
    KB_ERR_TIMEOUT = -7,
    KB_ERR_CANCELLED = -8,

    // Initialization errors (100-199)
    KB_ERR_NOT_INITIALIZED = -100,
    KB_ERR_ALREADY_INITIALIZED = -101,
    KB_ERR_INIT_FAILED = -102,
    KB_ERR_CONFIG_INVALID = -103,
    KB_ERR_CONFIG_PARSE_FAILED = -104,

    // Model errors (200-299)
    KB_ERR_MODEL_NOT_FOUND = -200,
    KB_ERR_MODEL_LOAD_FAILED = -201,
    KB_ERR_MODEL_INVALID_FORMAT = -202,
    KB_ERR_MODEL_VERSION_MISMATCH = -203,
    KB_ERR_MODEL_CORRUPTED = -204,
    KB_ERR_INFER_FAILED = -205,
    KB_ERR_INFER_TIMEOUT = -206,
    KB_ERR_MODEL_NOT_READY = -207,

    // Dictionary errors (300-399)
    KB_ERR_DICT_NOT_FOUND = -300,
    KB_ERR_DICT_LOAD_FAILED = -301,
    KB_ERR_DICT_INVALID_FORMAT = -302,
    KB_ERR_DICT_CORRUPTED = -303,
    KB_ERR_DICT_TOO_LARGE = -304,
    KB_ERR_DICT_EMPTY = -305,

    // Session errors (400-499)
    KB_ERR_SESSION_INVALID = -400,
    KB_ERR_SESSION_EXPIRED = -401,
    KB_ERR_SESSION_LIMIT_REACHED = -402,

    // Platform errors (500-599)
    KB_ERR_FILE_NOT_FOUND = -500,
    KB_ERR_FILE_READ_FAILED = -501,
    KB_ERR_FILE_WRITE_FAILED = -502,
    KB_ERR_PERMISSION_DENIED = -503,
    KB_ERR_IO_ERROR = -504,

    // Threading errors (600-699)
    KB_ERR_THREAD_CREATE_FAILED = -600,
    KB_ERR_MUTEX_LOCK_FAILED = -601,
    KB_ERR_DEADLOCK_DETECTED = -602

} KeyboardErrorCode;

/**
 * Check if error code indicates success
 */
static inline int kb_is_success(KeyboardErrorCode code) {
    return code == KB_OK;
}

/**
 * Check if error is recoverable (can retry or fallback)
 */
static inline int kb_is_recoverable(KeyboardErrorCode code) {
    switch (code) {
        case KB_ERR_TIMEOUT:
        case KB_ERR_INFER_TIMEOUT:
        case KB_ERR_MODEL_NOT_READY:
        case KB_ERR_THREAD_CREATE_FAILED:
        case KB_ERR_MUTEX_LOCK_FAILED:
            return 1;
        default:
            return 0;
    }
}

/**
 * Get human-readable error message
 * Returns pointer to static string (do not free)
 */
const char* kb_error_string(KeyboardErrorCode code);

#ifdef __cplusplus
}
#endif

#endif // KEYBOARD_NATIVE_ERROR_CODES_H
