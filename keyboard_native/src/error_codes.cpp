/**
 * HOSKEY Keyboard Native Engine - Error Codes Implementation
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#include "../include/error_codes.h"

const char* kb_error_string(KeyboardErrorCode code) {
    switch (code) {
        // Success
        case KB_OK: return "Success";

        // General errors
        case KB_ERR_UNKNOWN: return "Unknown error";
        case KB_ERR_INVALID_ARG: return "Invalid argument";
        case KB_ERR_NULL_POINTER: return "Null pointer";
        case KB_ERR_OUT_OF_MEMORY: return "Out of memory";
        case KB_ERR_BUFFER_TOO_SMALL: return "Buffer too small";
        case KB_ERR_INVALID_STATE: return "Invalid state";
        case KB_ERR_TIMEOUT: return "Operation timed out";
        case KB_ERR_CANCELLED: return "Operation cancelled";

        // Initialization errors
        case KB_ERR_NOT_INITIALIZED: return "Engine not initialized";
        case KB_ERR_ALREADY_INITIALIZED: return "Engine already initialized";
        case KB_ERR_INIT_FAILED: return "Initialization failed";
        case KB_ERR_CONFIG_INVALID: return "Invalid configuration";
        case KB_ERR_CONFIG_PARSE_FAILED: return "Configuration parse failed";

        // Model errors
        case KB_ERR_MODEL_NOT_FOUND: return "Model not found";
        case KB_ERR_MODEL_LOAD_FAILED: return "Model load failed";
        case KB_ERR_MODEL_INVALID_FORMAT: return "Invalid model format";
        case KB_ERR_MODEL_VERSION_MISMATCH: return "Model version mismatch";
        case KB_ERR_MODEL_CORRUPTED: return "Model corrupted";
        case KB_ERR_INFER_FAILED: return "Inference failed";
        case KB_ERR_INFER_TIMEOUT: return "Inference timed out";
        case KB_ERR_MODEL_NOT_READY: return "Model not ready";

        // Dictionary errors
        case KB_ERR_DICT_NOT_FOUND: return "Dictionary not found";
        case KB_ERR_DICT_LOAD_FAILED: return "Dictionary load failed";
        case KB_ERR_DICT_INVALID_FORMAT: return "Invalid dictionary format";
        case KB_ERR_DICT_CORRUPTED: return "Dictionary corrupted";
        case KB_ERR_DICT_TOO_LARGE: return "Dictionary too large";
        case KB_ERR_DICT_EMPTY: return "Dictionary empty";

        // Session errors
        case KB_ERR_SESSION_INVALID: return "Invalid session";
        case KB_ERR_SESSION_EXPIRED: return "Session expired";
        case KB_ERR_SESSION_LIMIT_REACHED: return "Session limit reached";

        // Platform errors
        case KB_ERR_FILE_NOT_FOUND: return "File not found";
        case KB_ERR_FILE_READ_FAILED: return "File read failed";
        case KB_ERR_FILE_WRITE_FAILED: return "File write failed";
        case KB_ERR_PERMISSION_DENIED: return "Permission denied";
        case KB_ERR_IO_ERROR: return "I/O error";

        // Threading errors
        case KB_ERR_THREAD_CREATE_FAILED: return "Thread creation failed";
        case KB_ERR_MUTEX_LOCK_FAILED: return "Mutex lock failed";
        case KB_ERR_DEADLOCK_DETECTED: return "Deadlock detected";

        default: return "Unknown error code";
    }
}
