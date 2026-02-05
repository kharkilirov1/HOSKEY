/**
 * HOSKEY Integration Layer
 *
 * Integrates keyboard_native with existing HOSKEY infrastructure:
 * - YandexDict (dictionary)
 * - NeuralModelManager (neural models)
 * - NNRtScorer (MindSpore inference)
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#ifndef KEYBOARD_NATIVE_HOSKEY_INTEGRATION_H
#define KEYBOARD_NATIVE_HOSKEY_INTEGRATION_H

#include "../core/engine.h"
#include "../nn/neural_engine_nnrt_adapter.h"

// Existing HOSKEY components
#include "dictionary_hoskey/yandex_trie.h"
#include "dictionary_hoskey/neural_model_manager.h"

#include <memory>
#include <string>
#include <atomic>
#include <functional>
#include <thread>
#include <future>

namespace keyboard {
namespace integration {

/**
 * HOSKEY-integrated engine configuration
 */
struct HoskeyConfig {
    // Paths
    std::string dictPath;           // YandexDict (.dict file)
    std::string modelsDir;          // Directory with .ms models
    std::string personalDictPath;   // Personal dictionary

    // Neural options
    bool useNeural = true;
    int neuralThreads = 2;
    int maxInferMs = 20;

    // Cache
    int cacheSize = 128;

    // Models to load (bitmask)
    enum ModelFlags {
        MODEL_TAP_RANKER    = 1 << 0,
        MODEL_SWIPE_RANKER  = 1 << 1,
        MODEL_NNLM          = 1 << 2,
        MODEL_AUTOCORRECT   = 1 << 3,
        MODEL_EMOJI         = 1 << 4,
        MODEL_ALL           = 0xFFFF
    };
    int modelsToLoad = MODEL_TAP_RANKER | MODEL_NNLM;
};

/**
 * Integrated engine that uses existing HOSKEY components
 */
class HoskeyIntegratedEngine {
public:
    HoskeyIntegratedEngine();
    ~HoskeyIntegratedEngine();

    /**
     * Initialize with HOSKEY config (SYNCHRONOUS - use initAsync for UI thread!)
     */
    bool init(const HoskeyConfig& config);

    /**
     * Initialize asynchronously (RECOMMENDED for UI thread)
     * @param config Configuration
     * @param onComplete Callback called when init completes (may be on different thread)
     */
    void initAsync(const HoskeyConfig& config,
                   std::function<void(bool success)> onComplete = nullptr);

    /**
     * Check if async init is still in progress
     */
    bool isInitializing() const { return initializing_.load(); }

    /**
     * Wait for async init to complete
     * @param timeoutMs Max time to wait (0 = infinite)
     * @return true if init completed, false if timeout
     */
    bool waitForInit(int timeoutMs = 0);

    /**
     * Shutdown
     */
    void shutdown();

    /**
     * Check if ready (thread-safe)
     */
    bool isReady() const { return ready_.load(); }

    /**
     * Check if dictionary is loaded
     */
    bool isDictLoaded() const { return dictLoaded_.load(); }

    /**
     * Check if neural models are loaded
     */
    bool isNeuralLoaded() const { return neuralLoaded_.load(); }

    // ========================================================================
    // Access to components
    // ========================================================================

    /**
     * Get keyboard_native engine
     */
    core::Engine* getEngine() { return engine_.get(); }

    /**
     * Get YandexDict (for direct trie access)
     */
    yandex::YandexDict* getYandexDict() { return yandexDict_.get(); }

    /**
     * Get NeuralModelManager (for direct model access)
     */
    yandex::NeuralModelManager* getModelManager() { return modelManager_.get(); }

    // ========================================================================
    // High-level API (delegates to engine)
    // ========================================================================

    /**
     * Get predictions
     */
    KeyboardErrorCode predict(const KBPredictContext& ctx, KBPredictResult& result);

    /**
     * Learn from user
     */
    KeyboardErrorCode learn(const KBLearnEvent& event);

    /**
     * Reset session
     */
    KeyboardErrorCode resetSession();

    /**
     * Get suggestions using YandexDict directly
     */
    std::vector<yandex::Suggestion> getYandexSuggestions(
        const std::string& prefix, int limit = 10);

    /**
     * Score suggestions using neural models
     */
    std::vector<yandex::ScoredWord> scoreWithNeural(
        const std::vector<std::string>& candidates,
        const std::string& context = "");

    // ========================================================================
    // Stats
    // ========================================================================

    struct Stats {
        size_t dictWordCount = 0;
        int loadedModels = 0;
        uint64_t totalPredictions = 0;
        uint64_t cacheHits = 0;
        std::string primaryDevice;
    };

    Stats getStats() const;

private:
    std::unique_ptr<core::Engine> engine_;
    std::unique_ptr<yandex::YandexDict> yandexDict_;
    std::unique_ptr<yandex::NeuralModelManager> modelManager_;

    HoskeyConfig config_;

    // Thread-safe state flags
    std::atomic<bool> ready_{false};
    std::atomic<bool> initializing_{false};
    std::atomic<bool> dictLoaded_{false};
    std::atomic<bool> neuralLoaded_{false};

    // Async init support
    std::future<bool> initFuture_;
    mutable std::mutex componentMutex_;  // Protects component access

    // Load neural models based on flags
    int loadNeuralModels(int flags);

    // Internal init (runs on worker thread for async)
    bool initInternal(const HoskeyConfig& config);
};

/**
 * Global singleton for easy access
 */
HoskeyIntegratedEngine& getGlobalEngine();

/**
 * Initialize global engine (SYNCHRONOUS - blocks until complete)
 */
bool initGlobalEngine(const HoskeyConfig& config);

/**
 * Initialize global engine ASYNCHRONOUSLY (recommended for UI thread)
 * @param config Configuration
 * @param onComplete Callback when init completes (may be on different thread!)
 */
void initGlobalEngineAsync(const HoskeyConfig& config,
                           std::function<void(bool success)> onComplete = nullptr);

/**
 * Shutdown global engine
 */
void shutdownGlobalEngine();

} // namespace integration
} // namespace keyboard

#endif // KEYBOARD_NATIVE_HOSKEY_INTEGRATION_H
