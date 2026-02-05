/**
 * HOSKEY Keyboard - NAPI Utilities
 *
 * Helper functions for N-API bindings.
 * Handles argument parsing, error handling, and result serialization.
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#ifndef KEYBOARD_NATIVE_NAPI_UTILS_H
#define KEYBOARD_NATIVE_NAPI_UTILS_H

#include "napi/native_api.h"
#include "../../include/types.h"
#include "../../include/error_codes.h"

#include <string>
#include <vector>

namespace keyboard {
namespace napi {

// ============================================================================
// Argument Extraction
// ============================================================================

/**
 * Get string argument
 */
bool getStringArg(napi_env env, napi_value value, std::string& out);

/**
 * Get number argument
 */
bool getNumberArg(napi_env env, napi_value value, double& out);

/**
 * Get integer argument
 */
bool getIntArg(napi_env env, napi_value value, int32_t& out);

/**
 * Get boolean argument
 */
bool getBoolArg(napi_env env, napi_value value, bool& out);

/**
 * Get object property as string
 */
bool getObjectStringProp(napi_env env, napi_value obj,
                          const char* name, std::string& out);

/**
 * Get object property as number
 */
bool getObjectNumberProp(napi_env env, napi_value obj,
                          const char* name, double& out);

/**
 * Get object property as int
 */
bool getObjectIntProp(napi_env env, napi_value obj,
                       const char* name, int32_t& out);

/**
 * Get object property as bool
 */
bool getObjectBoolProp(napi_env env, napi_value obj,
                        const char* name, bool& out);

/**
 * Check if object has property
 */
bool hasProperty(napi_env env, napi_value obj, const char* name);

// ============================================================================
// Result Creation
// ============================================================================

/**
 * Create string value
 */
napi_value createString(napi_env env, const std::string& str);

/**
 * Create string value from C string
 */
napi_value createString(napi_env env, const char* str);

/**
 * Create number value
 */
napi_value createNumber(napi_env env, double value);

/**
 * Create integer value
 */
napi_value createInt(napi_env env, int32_t value);

/**
 * Create boolean value
 */
napi_value createBool(napi_env env, bool value);

/**
 * Create undefined value
 */
napi_value createUndefined(napi_env env);

/**
 * Create null value
 */
napi_value createNull(napi_env env);

/**
 * Create empty object
 */
napi_value createObject(napi_env env);

/**
 * Create empty array
 */
napi_value createArray(napi_env env, size_t length = 0);

/**
 * Set object property
 */
void setProperty(napi_env env, napi_value obj,
                 const char* name, napi_value value);

/**
 * Set array element
 */
void setArrayElement(napi_env env, napi_value arr,
                     size_t index, napi_value value);

// ============================================================================
// Error Handling
// ============================================================================

/**
 * Create error result object
 */
napi_value createError(napi_env env, KeyboardErrorCode code,
                        const char* message = nullptr);

/**
 * Create success result with data
 */
napi_value createSuccess(napi_env env, napi_value data = nullptr);

/**
 * Throw JavaScript error
 */
void throwError(napi_env env, const char* message);

/**
 * Throw type error
 */
void throwTypeError(napi_env env, const char* message);

// ============================================================================
// Prediction Result Helpers
// ============================================================================

/**
 * Convert KBPredictResult to NAPI object
 */
napi_value predictResultToNapi(napi_env env, const KBPredictResult& result);

/**
 * Parse KBPredictContext from NAPI object
 */
bool parsePredictContext(napi_env env, napi_value obj, KBPredictContext& ctx,
                          std::string& inputBuffer, std::string& prevWord1Buffer);

/**
 * Parse KBLearnEvent from NAPI object
 */
bool parseLearnEvent(napi_env env, napi_value obj, KBLearnEvent& event,
                      std::string& wordBuffer, std::string& prevWordBuffer);

/**
 * Convert candidate source to string
 */
const char* sourceToString(KBCandidateSource source);

// ============================================================================
// Validation
// ============================================================================

/**
 * Validate argument count
 */
bool validateArgCount(napi_env env, napi_callback_info info,
                      size_t minArgs, size_t maxArgs,
                      napi_value* args, size_t& actualCount);

/**
 * Validate argument type
 */
bool isString(napi_env env, napi_value value);
bool isNumber(napi_env env, napi_value value);
bool isObject(napi_env env, napi_value value);
bool isArray(napi_env env, napi_value value);
bool isFunction(napi_env env, napi_value value);
bool isUndefined(napi_env env, napi_value value);
bool isNull(napi_env env, napi_value value);

} // namespace napi
} // namespace keyboard

#endif // KEYBOARD_NATIVE_NAPI_UTILS_H
