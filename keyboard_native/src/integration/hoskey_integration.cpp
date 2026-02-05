/**
 * HOSKEY Integration Implementation
 *
 * Fixed bugs:
 * - Added async initialization to prevent ANR
 * - Made ready_ atomic for thread safety
 * - Added proper result initialization
 * - Added component mutex for thread-safe access
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#include "hoskey_integration.h"
#include "../platform/harmony_log.h"

#include <mutex>
#include <cstring>
#include <chrono>

namespace keyboard {
namespace integration {

static constexpr const char* TAG = "HoskeyIntegration";

// Global singleton
static std::unique_ptr<HoskeyIntegratedEngine> g_globalEngine;
static std::mutex g_globalMutex;

HoskeyIntegratedEngine::HoskeyIntegratedEngine() {
    platform::logInfo(TAG, "HoskeyIntegratedEngine created");
}

HoskeyIntegratedEngine::~HoskeyIntegratedEngine() {
    shutdown();
    platform::logInfo(TAG, "HoskeyIntegratedEngine destroyed");
}

// ============================================================================
// Synchronous init (blocking - use initAsync for UI thread!)
// ============================================================================

bool HoskeyIntegratedEngine::init(const HoskeyConfig& config) {
    return initInternal(config);
}

// ============================================================================
// Asynchronous init (recommended for UI thread)
// ============================================================================

void HoskeyIntegratedEngine::initAsync(const HoskeyConfig& config,
                                        std::function<void(bool success)> onComplete) {
    if (initializing_.load()) {
        platform::logWarn(TAG, "initAsync: already initializing");
        if (onComplete) onComplete(false);
        return;
    }

    initializing_ = true;

    // Launch async init
    initFuture_ = std::async(std::launch::async, [this, config, onComplete]() {
        bool success = initInternal(config);
        initializing_ = false;

        if (onComplete) {
            onComplete(success);
        }

        return success;
    });

    platform::logInfo(TAG, "initAsync: started background initialization");
}

bool HoskeyIntegratedEngine::waitForInit(int timeoutMs) {
    if (!initFuture_.valid()) {
        return ready_.load();
    }

    if (timeoutMs <= 0) {
        initFuture_.wait();
        return initFuture_.get();
    } else {
        auto status = initFuture_.wait_for(std::chrono::milliseconds(timeoutMs));
        if (status == std::future_status::ready) {
            return initFuture_.get();
        }
        return false;
    }
}

// ============================================================================
// Internal init (runs on worker thread for async)
// ============================================================================

bool HoskeyIntegratedEngine::initInternal(const HoskeyConfig& config) {
    config_ = config;

    platform::logInfo(TAG, "Initializing HOSKEY integrated engine...");

    // 1. Initialize YandexDict (uses mmap - fast!)
    if (!config.dictPath.empty()) {
        std::lock_guard<std::mutex> lock(componentMutex_);
        yandexDict_ = std::make_unique<yandex::YandexDict>();

        if (!yandexDict_->load(config.dictPath)) {
            platform::logError(TAG, "Failed to load YandexDict from: %s",
                              config.dictPath.c_str());
        } else {
            dictLoaded_ = true;
            platform::logInfo(TAG, "YandexDict loaded: %zu words (mmap)",
                             yandexDict_->getWordCount());
        }
    }

    // 2. Initialize NeuralModelManager (can be slow!)
    if (config.useNeural && !config.modelsDir.empty()) {
        std::lock_guard<std::mutex> lock(componentMutex_);
        modelManager_ = std::make_unique<yandex::NeuralModelManager>();
        modelManager_->setModelsDirectory(config.modelsDir);

        int loadedCount = loadNeuralModels(config.modelsToLoad);
        if (loadedCount > 0) {
            neuralLoaded_ = true;
        }
        platform::logInfo(TAG, "Loaded %d neural models", loadedCount);
    }

    // 3. Initialize keyboard_native engine
    {
        std::lock_guard<std::mutex> lock(componentMutex_);
        engine_ = std::make_unique<core::Engine>();

        KBEngineConfig kbConfig;
        kb_config_init_default(&kbConfig);

        kbConfig.useNeural = 0;
        kbConfig.cacheSize = static_cast<uint32_t>(config.cacheSize);
        kbConfig.maxInferMs = static_cast<uint32_t>(config.maxInferMs);

        auto err = engine_->init(kbConfig);
        if (err != KB_OK) {
            platform::logError(TAG, "Failed to init keyboard_native engine");
            return false;
        }
    }

    ready_ = true;
    platform::logInfo(TAG, "HOSKEY integrated engine ready");

    return true;
}

void HoskeyIntegratedEngine::shutdown() {
    if (initFuture_.valid()) {
        initFuture_.wait();
    }

    ready_ = false;
    initializing_ = false;
    dictLoaded_ = false;
    neuralLoaded_ = false;

    std::lock_guard<std::mutex> lock(componentMutex_);

    if (engine_) {
        engine_->shutdown();
        engine_.reset();
    }

    if (modelManager_) {
        modelManager_->unloadAll();
        modelManager_.reset();
    }

    yandexDict_.reset();

    platform::logInfo(TAG, "HOSKEY integrated engine shutdown");
}

int HoskeyIntegratedEngine::loadNeuralModels(int flags) {
    if (!modelManager_) return 0;

    int loaded = 0;

    if (flags & HoskeyConfig::MODEL_TAP_RANKER) {
        if (modelManager_->loadModel(yandex::ModelType::TAP_RANKER)) loaded++;
        if (modelManager_->loadModel(yandex::ModelType::RANKER_V2)) loaded++;
    }

    if (flags & HoskeyConfig::MODEL_SWIPE_RANKER) {
        if (modelManager_->loadModel(yandex::ModelType::SWIPE_RANKER_V2)) loaded++;
        if (modelManager_->loadModel(yandex::ModelType::SWIPE_BLOCKER)) loaded++;
    }

    if (flags & HoskeyConfig::MODEL_NNLM) {
        if (modelManager_->loadModel(yandex::ModelType::NNLM)) loaded++;
    }

    if (flags & HoskeyConfig::MODEL_AUTOCORRECT) {
        if (modelManager_->loadModel(yandex::ModelType::AUTOCORRECT)) loaded++;
        if (modelManager_->loadModel(yandex::ModelType::CHAR_MODEL)) loaded++;
    }

    if (flags & HoskeyConfig::MODEL_EMOJI) {
        if (modelManager_->loadModel(yandex::ModelType::EMOJI_SUGGEST)) loaded++;
    }

    return loaded;
}

KeyboardErrorCode HoskeyIntegratedEngine::predict(const KBPredictContext& ctx,
                                                   KBPredictResult& result) {
    if (!ready_.load()) {
        return KB_ERR_NOT_INITIALIZED;
    }

    // Initialize result to zero (BUG FIX)
    std::memset(&result, 0, sizeof(result));

    std::string prefix;
    if (ctx.inputText && ctx.inputLength > 0) {
        prefix = std::string(ctx.inputText, ctx.inputLength);
    }

    // Thread-safe access to components
    std::lock_guard<std::mutex> lock(componentMutex_);

    // 1. Get candidates from YandexDict
    std::vector<yandex::Suggestion> suggestions;
    if (yandexDict_ && dictLoaded_.load()) {
        suggestions = yandexDict_->getSuggestions(prefix,
            static_cast<int>(ctx.maxResults > 0 ? ctx.maxResults * 2 : 20));
    }

    // 2. Score with neural models if available
    std::vector<yandex::ScoredWord> scored;
    if (modelManager_ && neuralLoaded_.load() && !suggestions.empty()) {
        std::vector<yandex::ScoringCandidate> candidates;
        candidates.reserve(suggestions.size());

        for (const auto& s : suggestions) {
            yandex::ScoringCandidate sc;
            sc.word = s.word;
            sc.wordId = 0;
            sc.baseScore = s.score;
            candidates.push_back(sc);
        }

        std::string context;
        if (ctx.prevWord1) {
            context = ctx.prevWord1;
        }

        scored = modelManager_->scoreTapSuggestions(candidates, context);
    }

    // 3. Build result
    size_t count = scored.empty() ? suggestions.size() : scored.size();
    if (ctx.maxResults > 0 && count > ctx.maxResults) {
        count = ctx.maxResults;
    }

    // Handle empty results (BUG FIX)
    if (count == 0) {
        result.candidateCount = 0;
        result.candidates = nullptr;
        return KB_OK;
    }

    result.candidateCount = static_cast<uint32_t>(count);
    result.candidates = new KBCandidate[count];

    // Zero-initialize all candidates (BUG FIX)
    std::memset(result.candidates, 0, sizeof(KBCandidate) * count);

    if (!scored.empty()) {
        for (size_t i = 0; i < count; ++i) {
            auto& out = result.candidates[i];
            out.text = strdup(scored[i].word.c_str());
            out.score = scored[i].score;
            out.confidence = scored[i].neuralScore;
            out.source = KB_SOURCE_NEURAL;
            out.sourceMask = KB_SOURCE_NEURAL | KB_SOURCE_TRIE;
            out.isExactMatch = (scored[i].word == prefix) ? 1 : 0;
            out.isAutocorrect = 0;
        }
        result.neuralCandidates = static_cast<uint32_t>(count);
    } else {
        for (size_t i = 0; i < count; ++i) {
            auto& out = result.candidates[i];
            out.text = strdup(suggestions[i].word.c_str());
            out.score = suggestions[i].score;
            out.confidence = suggestions[i].score;
            out.source = KB_SOURCE_TRIE;
            out.sourceMask = KB_SOURCE_TRIE;
            out.isExactMatch = (suggestions[i].word == prefix) ? 1 : 0;
            out.isAutocorrect = 0;
        }
        result.trieCandidates = static_cast<uint32_t>(count);
    }

    return KB_OK;
}

KeyboardErrorCode HoskeyIntegratedEngine::learn(const KBLearnEvent& event) {
    if (!ready_.load()) {
        return KB_ERR_NOT_INITIALIZED;
    }

    std::lock_guard<std::mutex> lock(componentMutex_);
    if (!engine_) {
        return KB_ERR_NOT_INITIALIZED;
    }

    return engine_->learn(event);
}

KeyboardErrorCode HoskeyIntegratedEngine::resetSession() {
    if (!ready_.load()) {
        return KB_ERR_NOT_INITIALIZED;
    }

    std::lock_guard<std::mutex> lock(componentMutex_);
    if (!engine_) {
        return KB_ERR_NOT_INITIALIZED;
    }

    return engine_->resetSession();
}

std::vector<yandex::Suggestion> HoskeyIntegratedEngine::getYandexSuggestions(
    const std::string& prefix, int limit) {

    std::lock_guard<std::mutex> lock(componentMutex_);
    if (!yandexDict_ || !dictLoaded_.load()) {
        return {};
    }

    return yandexDict_->getSuggestions(prefix, limit);
}

std::vector<yandex::ScoredWord> HoskeyIntegratedEngine::scoreWithNeural(
    const std::vector<std::string>& candidates,
    const std::string& context) {

    std::lock_guard<std::mutex> lock(componentMutex_);
    if (!modelManager_ || !neuralLoaded_.load()) {
        return {};
    }

    std::vector<yandex::ScoringCandidate> scoringCandidates;
    scoringCandidates.reserve(candidates.size());

    for (const auto& word : candidates) {
        yandex::ScoringCandidate sc;
        sc.word = word;
        sc.wordId = 0;
        sc.baseScore = 0.5f;
        scoringCandidates.push_back(sc);
    }

    return modelManager_->scoreTapSuggestions(scoringCandidates, context);
}

HoskeyIntegratedEngine::Stats HoskeyIntegratedEngine::getStats() const {
    Stats stats;

    std::lock_guard<std::mutex> lock(componentMutex_);

    if (yandexDict_ && dictLoaded_.load()) {
        stats.dictWordCount = yandexDict_->getWordCount();
    }

    if (modelManager_ && neuralLoaded_.load()) {
        auto mstats = modelManager_->getStats();
        stats.loadedModels = mstats.loadedModels;
        stats.primaryDevice = mstats.primaryDevice;
    }

    if (engine_) {
        KBEngineStatus estatus;
        engine_->getStatus(estatus);
        stats.totalPredictions = estatus.totalPredictions;
        stats.cacheHits = estatus.cacheHits;
    }

    return stats;
}

// ============================================================================
// Global Engine Functions
// ============================================================================

HoskeyIntegratedEngine& getGlobalEngine() {
    std::lock_guard<std::mutex> lock(g_globalMutex);

    if (!g_globalEngine) {
        g_globalEngine = std::make_unique<HoskeyIntegratedEngine>();
    }

    return *g_globalEngine;
}

bool initGlobalEngine(const HoskeyConfig& config) {
    std::lock_guard<std::mutex> lock(g_globalMutex);

    if (!g_globalEngine) {
        g_globalEngine = std::make_unique<HoskeyIntegratedEngine>();
    }

    return g_globalEngine->init(config);
}

void initGlobalEngineAsync(const HoskeyConfig& config,
                           std::function<void(bool success)> onComplete) {
    std::lock_guard<std::mutex> lock(g_globalMutex);

    if (!g_globalEngine) {
        g_globalEngine = std::make_unique<HoskeyIntegratedEngine>();
    }

    g_globalEngine->initAsync(config, onComplete);
}

void shutdownGlobalEngine() {
    std::lock_guard<std::mutex> lock(g_globalMutex);

    if (g_globalEngine) {
        g_globalEngine->shutdown();
        g_globalEngine.reset();
    }
}

} // namespace integration
} // namespace keyboard
