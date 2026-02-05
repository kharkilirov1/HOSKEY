/**
 * HOSKEY Keyboard Native Engine - Core Types
 *
 * Stable C ABI types for cross-boundary communication.
 * No STL types exposed - only C-compatible structs.
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#ifndef KEYBOARD_NATIVE_TYPES_H
#define KEYBOARD_NATIVE_TYPES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Version Information
// ============================================================================

#define KB_VERSION_MAJOR 1
#define KB_VERSION_MINOR 0
#define KB_VERSION_PATCH 0
#define KB_VERSION_STRING "1.0.0"

// ABI version for binary compatibility checking
#define KB_ABI_VERSION 1

// ============================================================================
// Forward Declarations (opaque handles)
// ============================================================================

/** Opaque handle to keyboard engine instance */
typedef struct KBEngineHandle KBEngineHandle;

/** Opaque handle to prediction session */
typedef struct KBSessionHandle KBSessionHandle;

// ============================================================================
// Candidate Source Types
// ============================================================================

/**
 * Source of prediction candidate
 */
typedef enum KBCandidateSource {
    KB_SOURCE_UNKNOWN = 0,
    KB_SOURCE_NEURAL = 1,        // Neural network (MindSpore)
    KB_SOURCE_NGRAM = 2,         // N-gram language model
    KB_SOURCE_TRIE = 4,          // Dictionary trie lookup
    KB_SOURCE_RULE = 8,          // Rule-based (fallback)
    KB_SOURCE_PERSONAL = 16,     // User's personal dictionary
    KB_SOURCE_SHORTCUT = 32,     // User shortcuts/abbreviations
    KB_SOURCE_EMOJI = 64,        // Emoji suggestions
    KB_SOURCE_MIXED = 128        // Multiple sources combined
} KBCandidateSource;

// ============================================================================
// Prediction Candidate
// ============================================================================

/**
 * Single prediction candidate
 * Memory: text is owned by the result, freed with kb_free_result()
 */
typedef struct KBCandidate {
    char* text;                  // UTF-8 word text (null-terminated)
    float score;                 // Combined score [0.0, 1.0]
    float confidence;            // Confidence level [0.0, 1.0]
    KBCandidateSource source;    // Which predictor generated this
    uint32_t sourceMask;         // Bitmask of all contributing sources
    uint8_t isExactMatch;        // 1 if exact prefix match
    uint8_t isAutocorrect;       // 1 if this is an autocorrection
    uint8_t reserved[2];         // Padding for alignment
} KBCandidate;

// ============================================================================
// Prediction Result
// ============================================================================

/**
 * Prediction result container
 * Memory: entire struct and all candidates freed with kb_free_result()
 */
typedef struct KBPredictResult {
    KBCandidate* candidates;     // Array of candidates
    uint32_t candidateCount;     // Number of candidates
    uint32_t maxCandidates;      // Capacity (for reuse)

    // Timing information (microseconds)
    uint32_t preprocessUs;       // Preprocessing time
    uint32_t inferUs;            // Neural inference time
    uint32_t rankUs;             // Ranking time
    uint32_t totalUs;            // Total latency

    // Source statistics
    uint32_t neuralCandidates;   // Count from neural
    uint32_t ngramCandidates;    // Count from ngram
    uint32_t trieCandidates;     // Count from trie
    uint32_t ruleCandidates;     // Count from rules

    // Debug info
    char* traceId;               // Optional trace ID for logging
    uint8_t usedFallback;        // 1 if fallback was triggered
    uint8_t reserved[3];         // Padding
} KBPredictResult;

// ============================================================================
// Input Context
// ============================================================================

/**
 * Touch/tap coordinate
 */
typedef struct KBPoint {
    float x;
    float y;
    int64_t timestamp;           // Milliseconds since epoch
} KBPoint;

/**
 * Input context for prediction
 */
typedef struct KBPredictContext {
    // Current input
    const char* inputText;       // Current typed text (UTF-8)
    uint32_t inputLength;        // Length in bytes
    uint32_t cursorPosition;     // Cursor position in code points

    // Previous words context (for n-gram)
    const char* prevWord1;       // Previous word (null if none)
    const char* prevWord2;       // Word before previous (null if none)

    // Touch coordinates (optional, for neural scoring)
    const KBPoint* touchPoints;  // Array of touch points
    uint32_t touchPointCount;    // Number of touch points

    // Gesture/swipe mode
    uint8_t isGesture;           // 1 if swipe input
    uint8_t isComposing;         // 1 if in composition mode

    // Limits
    uint32_t maxResults;         // Max candidates to return (0 = default)
    uint32_t deadlineMs;         // Max time to spend (0 = default)

    // Filters
    uint8_t includeEmoji;        // 1 to include emoji suggestions
    uint8_t includeShortcuts;    // 1 to include user shortcuts
    uint8_t reserved[2];         // Padding
} KBPredictContext;

// ============================================================================
// Learning Event
// ============================================================================

/**
 * Type of learning event
 */
typedef enum KBLearnEventType {
    KB_LEARN_WORD_SELECTED = 1,  // User selected a suggestion
    KB_LEARN_WORD_TYPED = 2,     // User typed word manually
    KB_LEARN_WORD_DELETED = 3,   // User deleted a word
    KB_LEARN_BIGRAM = 4,         // Word pair context
    KB_LEARN_SHORTCUT = 5,       // User created shortcut
    KB_LEARN_UNDO = 6            // User undid an autocorrection
} KBLearnEventType;

/**
 * Learning event for personalization
 */
typedef struct KBLearnEvent {
    KBLearnEventType type;
    const char* word;            // The word
    const char* prevWord;        // Previous word (for bigram)
    const char* replacement;     // What it was replaced with (for undo)
    int32_t frequency;           // Frequency boost (-1 to decrement)
    int64_t timestamp;           // Event timestamp
} KBLearnEvent;

// ============================================================================
// Engine Configuration
// ============================================================================

/**
 * Fallback policy when neural inference fails or times out
 */
typedef enum KBFallbackPolicy {
    KB_FALLBACK_NGRAM_TRIE_RULE = 0,  // Default: ngram -> trie -> rule
    KB_FALLBACK_TRIE_ONLY = 1,         // Skip ngram, use trie only
    KB_FALLBACK_RULE_ONLY = 2,         // Use rule-based only
    KB_FALLBACK_NONE = 3               // Return error if NN fails
} KBFallbackPolicy;

/**
 * Engine configuration (JSON-compatible fields)
 */
typedef struct KBEngineConfig {
    // Paths
    const char* dictPath;        // Path to dictionary file
    const char* modelPath;       // Path to neural model (.ms)
    const char* personalDictPath;// Path to personal dictionary
    const char* cachePath;       // Path for cache files

    // Neural settings
    uint8_t useNeural;           // 1 to enable neural inference
    uint8_t useGpu;              // 1 to prefer GPU/NPU
    uint8_t warmupOnInit;        // 1 to warmup model on init
    uint8_t reserved1;
    uint32_t neuralThreads;      // Thread count for inference (0 = auto)
    uint32_t maxInferMs;         // Max inference time (default: 20ms)

    // Fallback settings
    KBFallbackPolicy fallbackPolicy;

    // Cache settings
    uint32_t cacheSize;          // LRU cache size (default: 128)
    uint8_t enableCache;         // 1 to enable suggestion cache
    uint8_t reserved2[3];

    // Latency budgets (microseconds, 0 = no limit)
    uint32_t p50TargetUs;        // p50 target (default: 8000)
    uint32_t p95TargetUs;        // p95 target (default: 20000)
    uint32_t p99TargetUs;        // p99 target (default: 35000)

    // Scoring weights [0.0, 1.0]
    float neuralWeight;          // Neural score weight (default: 0.30)
    float ngramWeight;           // N-gram score weight (default: 0.15)
    float dictWeight;            // Dictionary score weight (default: 0.35)
    float personalWeight;        // Personal dict weight (default: 0.20)

    // Debug
    uint8_t enableTracing;       // 1 to enable trace IDs
    uint8_t logLevel;            // 0=off, 1=error, 2=warn, 3=info, 4=debug
    uint8_t reserved3[2];
} KBEngineConfig;

/**
 * Initialize config with default values
 */
static inline void kb_config_init_default(KBEngineConfig* config) {
    if (!config) return;

    config->dictPath = NULL;
    config->modelPath = NULL;
    config->personalDictPath = NULL;
    config->cachePath = NULL;

    config->useNeural = 1;
    config->useGpu = 0;
    config->warmupOnInit = 1;
    config->neuralThreads = 0;
    config->maxInferMs = 20;

    config->fallbackPolicy = KB_FALLBACK_NGRAM_TRIE_RULE;

    config->cacheSize = 128;
    config->enableCache = 1;

    config->p50TargetUs = 8000;
    config->p95TargetUs = 20000;
    config->p99TargetUs = 35000;

    config->neuralWeight = 0.30f;
    config->ngramWeight = 0.15f;
    config->dictWeight = 0.35f;
    config->personalWeight = 0.20f;

    config->enableTracing = 0;
    config->logLevel = 2; // warn
}

// ============================================================================
// Engine State
// ============================================================================

/**
 * Engine state machine states
 */
typedef enum KBEngineState {
    KB_STATE_UNINITIALIZED = 0,
    KB_STATE_INITIALIZING = 1,
    KB_STATE_READY = 2,
    KB_STATE_CLOSING = 3,
    KB_STATE_CLOSED = 4,
    KB_STATE_ERROR = 5
} KBEngineState;

/**
 * Engine status information
 */
typedef struct KBEngineStatus {
    KBEngineState state;
    uint8_t neuralReady;         // 1 if neural model loaded
    uint8_t dictReady;           // 1 if dictionary loaded
    uint8_t personalDictReady;   // 1 if personal dict loaded
    uint8_t reserved;

    uint32_t dictWordCount;      // Words in main dictionary
    uint32_t personalWordCount;  // Words in personal dictionary

    // Latency statistics (microseconds)
    uint32_t avgLatencyUs;
    uint32_t p50LatencyUs;
    uint32_t p95LatencyUs;
    uint32_t p99LatencyUs;

    // Counters
    uint64_t totalPredictions;
    uint64_t cacheHits;
    uint64_t cacheMisses;
    uint64_t fallbackCount;

    // Memory usage (bytes)
    size_t memoryUsage;
} KBEngineStatus;

#ifdef __cplusplus
}
#endif

#endif // KEYBOARD_NATIVE_TYPES_H
