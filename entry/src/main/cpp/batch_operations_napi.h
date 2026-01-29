/*
 * Copyright (c) 2024 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 * 
 * Batch Operations NAPI - High-performance batch processing
 * 
 * Reduces NAPI call overhead by batching multiple operations into single calls.
 * Inspired by Yandex Keyboard's protobuf request/response pattern.
 */

#ifndef HOSKEY_BATCH_OPERATIONS_NAPI_H
#define HOSKEY_BATCH_OPERATIONS_NAPI_H

#include <napi/native_api.h>

namespace latinime {

/**
 * Register batch operations module
 */
napi_value RegisterBatchOperations(napi_env env);

/**
 * batchContains(words: string[]): boolean[]
 * Check if multiple words exist in dictionary in a single call
 */
napi_value BatchContains(napi_env env, napi_callback_info info);

/**
 * batchGetFrequency(words: string[]): number[]
 * Get frequencies of multiple words in a single call
 */
napi_value BatchGetFrequency(napi_env env, napi_callback_info info);

/**
 * batchGetSuggestions(prefixes: string[], limit: number): SuggestResult[][]
 * Get suggestions for multiple prefixes in a single call
 */
napi_value BatchGetSuggestions(napi_env env, napi_callback_info info);

/**
 * processInputBatch(request: InputBatchRequest): InputBatchResponse
 * 
 * Combined batch operation for typical input processing:
 * - Check word existence
 * - Get suggestions
 * - Find autocorrection
 * - Apply autocorrect rules
 * 
 * InputBatchRequest:
 * {
 *   currentWord: string,
 *   prevWord?: string,
 *   suggestionLimit?: number,
 *   checkAutocorrect?: boolean,
 *   applyRules?: boolean
 * }
 * 
 * InputBatchResponse:
 * {
 *   exists: boolean,
 *   frequency: number,
 *   suggestions: SuggestResult[],
 *   autocorrection?: SuggestResult,
 *   ruleApplied?: string
 * }
 */
napi_value ProcessInputBatch(napi_env env, napi_callback_info info);

} // namespace latinime

#endif // HOSKEY_BATCH_OPERATIONS_NAPI_H
