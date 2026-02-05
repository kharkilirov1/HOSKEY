/**
 * HOSKEY Keyboard - Core Prediction Engine
 *
 * Main engine class that coordinates all prediction subsystems:
 * - Neural inference (MindSpore)
 * - Dictionary/Trie lookup
 * - N-gram model
 * - Rule-based fallback
 *
 * Implements fallback policy and score fusion.
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#ifndef KEYBOARD_NATIVE_ENGINE_H
#define KEYBOARD_NATIVE_ENGINE_H

#include "../../include/types.h"
#include "../../include/error_codes.h"
#include "../nn/i_neural_engine.h"
#include "../nn/feature_builder.h"
#include "fallback_policy.h"
#include "ranking.h"
#include "session.h"

#include <memory>
#include <mutex>
#include <atomic>
#include <string>
#include <vector>
#include <unordered_map>
#include <list>
#include <chrono>

namespace keyboard {
namespace core {

// Forward declarations
class DictEngine;
class NgramEngine;
class RuleEngine;

/**
 * Core prediction engine
 */
class Engine {
public:
    Engine();
    ~Engine();

    // Disable copy
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // ========================================================================
    // Lifecycle
    // ========================================================================

    /**
     * Initialize engine with configuration
     */
    KeyboardErrorCode init(const KBEngineConfig& config);

    /**
     * Initialize engine from JSON configuration
     */
    KeyboardErrorCode initFromJson(const char* configJson);

    /**
     * Shutdown engine and release resources
     */
    void shutdown();

    /**
     * Get current state
     */
    KBEngineState getState() const { return state_; }

    /**
     * Get engine status
     */
    KeyboardErrorCode getStatus(KBEngineStatus& status) const;

    // ========================================================================
    // Prediction
    // ========================================================================

    /**
     * Get prediction candidates
     */
    KeyboardErrorCode predict(const KBPredictContext& context,
                              KBPredictResult& result);

    /**
     * Get prediction as JSON string (caller must free with kb_free_string)
     */
    char* predictJson(const char* contextJson);

    // ========================================================================
    // Learning
    // ========================================================================

    /**
     * Process learning event
     */
    KeyboardErrorCode learn(const KBLearnEvent& event);

    /**
     * Process learning event from JSON
     */
    KeyboardErrorCode learnJson(const char* eventJson);

    /**
     * Flush learning data to persistent storage
     */
    KeyboardErrorCode flushLearning();

    // ========================================================================
    // Session
    // ========================================================================

    /**
     * Reset current input session
     */
    KeyboardErrorCode resetSession();

    /**
     * Clear all caches
     */
    KeyboardErrorCode clearCache();

    // ========================================================================
    // Dictionary Management
    // ========================================================================

    /**
     * Load dictionary from file
     */
    KeyboardErrorCode loadDictionary(const char* path, int dictType);

    /**
     * Load dictionary from file descriptor
     */
    KeyboardErrorCode loadDictionaryFd(int fd, size_t offset, size_t length, int dictType);

    /**
     * Add word to personal dictionary
     */
    KeyboardErrorCode addWord(const char* word, int frequency);

    /**
     * Remove word from personal dictionary
     */
    KeyboardErrorCode removeWord(const char* word);

    /**
     * Check if word exists
     */
    bool wordExists(const char* word) const;

    // ========================================================================
    // Neural Model Management
    // ========================================================================

    /**
     * Load neural model
     */
    KeyboardErrorCode loadModel(const char* modelPath);

    /**
     * Warmup neural model
     */
    KeyboardErrorCode warmupModel();

    /**
     * Unload neural model
     */
    KeyboardErrorCode unloadModel();

    /**
     * Check if model is ready
     */
    bool modelReady() const;

    // ========================================================================
    // Timing & Debug
    // ========================================================================

    /**
     * Get timing info for last prediction (JSON)
     */
    char* getTimingInfo() const;

private:
    // ========================================================================
    // Internal Prediction Pipeline
    // ========================================================================

    struct PredictionPipeline {
        std::vector<std::string> trieResults;
        std::vector<std::string> ngramResults;
        nn::NeuralResult neuralResult;
        std::vector<std::string> ruleResults;

        // Timing
        uint32_t trieTimeUs = 0;
        uint32_t ngramTimeUs = 0;
        uint32_t neuralTimeUs = 0;
        uint32_t ruleTimeUs = 0;
        uint32_t rankTimeUs = 0;

        bool usedFallback = false;
        FallbackReason fallbackReason = FallbackReason::None;
    };

    /**
     * Run full prediction pipeline
     */
    KeyboardErrorCode runPipeline(const KBPredictContext& context,
                                  PredictionPipeline& pipeline);

    /**
     * Get candidates from trie/dictionary
     */
    void getTrieCandidates(const std::string& prefix, int maxResults,
                           std::vector<std::string>& results);

    /**
     * Get candidates from n-gram model
     */
    void getNgramCandidates(const std::string& prevWord,
                            const std::string& prefix,
                            int maxResults,
                            std::vector<std::string>& results);

    /**
     * Get candidates from neural model
     */
    nn::NeuralResult getNeuralScores(const nn::NeuralFeatures& features,
                                     const std::vector<std::string>& candidates,
                                     int deadlineMs);

    /**
     * Get rule-based candidates (ultimate fallback)
     */
    void getRuleCandidates(const std::string& prefix, int maxResults,
                           std::vector<std::string>& results);

    /**
     * Build final result from pipeline
     */
    void buildResult(const PredictionPipeline& pipeline,
                     KBPredictResult& result);

    // ========================================================================
    // LRU Cache
    // ========================================================================

    struct CacheEntry {
        std::string key;
        std::vector<KBCandidate> candidates;
        std::chrono::steady_clock::time_point timestamp;
    };

    mutable std::mutex cacheMutex_;
    std::list<CacheEntry> cacheList_;
    std::unordered_map<std::string, std::list<CacheEntry>::iterator> cacheMap_;
    size_t maxCacheSize_ = 128;

    const std::vector<KBCandidate>* getCached(const std::string& key) const;
    void addToCache(const std::string& key, const std::vector<KBCandidate>& candidates);
    void clearCacheInternal();

    // ========================================================================
    // Statistics
    // ========================================================================

    mutable std::mutex statsMutex_;
    uint64_t totalPredictions_ = 0;
    uint64_t cacheHits_ = 0;
    uint64_t cacheMisses_ = 0;
    uint64_t fallbackCount_ = 0;
    std::vector<uint32_t> latencyHistory_;  // Recent latencies for percentiles
    static constexpr size_t LATENCY_HISTORY_SIZE = 1000;

    void recordLatency(uint32_t latencyUs);
    uint32_t getPercentileLatency(int percentile) const;

    // ========================================================================
    // Subsystems
    // ========================================================================

    std::unique_ptr<nn::INeuralEngine> neuralEngine_;
    std::unique_ptr<nn::FeatureBuilder> featureBuilder_;
    std::unique_ptr<DictEngine> dictEngine_;
    std::unique_ptr<NgramEngine> ngramEngine_;
    std::unique_ptr<RuleEngine> ruleEngine_;
    std::unique_ptr<FallbackPolicy> fallbackPolicy_;
    std::unique_ptr<Ranker> ranker_;
    std::unique_ptr<Session> session_;

    // ========================================================================
    // State
    // ========================================================================

    std::atomic<KBEngineState> state_{KB_STATE_UNINITIALIZED};
    mutable std::mutex engineMutex_;
    KBEngineConfig config_;
    std::string lastError_;

    // Last prediction timing (for debug)
    mutable std::mutex timingMutex_;
    struct TimingInfo {
        uint32_t preprocessUs = 0;
        uint32_t trieUs = 0;
        uint32_t ngramUs = 0;
        uint32_t neuralUs = 0;
        uint32_t ruleUs = 0;
        uint32_t rankUs = 0;
        uint32_t totalUs = 0;
        bool usedFallback = false;
        std::string traceId;
    } lastTiming_;

    // ========================================================================
    // Helpers
    // ========================================================================

    std::string generateTraceId() const;
    void setError(const std::string& error);
    static std::string buildCacheKey(const KBPredictContext& context);
};

} // namespace core
} // namespace keyboard

#endif // KEYBOARD_NATIVE_ENGINE_H
