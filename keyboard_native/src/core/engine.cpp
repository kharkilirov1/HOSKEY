/**
 * HOSKEY Keyboard - Core Prediction Engine Implementation
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#include "engine.h"
#include "../platform/harmony_log.h"
#include "../dict/dict_engine.h"

#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <algorithm>

namespace keyboard {
namespace core {

static constexpr const char* TAG = "KeyboardEngine";

// ============================================================================
// Constructor / Destructor
// ============================================================================

Engine::Engine() {
    platform::logInfo(TAG, "Engine created");
}

Engine::~Engine() {
    shutdown();
    platform::logInfo(TAG, "Engine destroyed");
}

// ============================================================================
// Lifecycle
// ============================================================================

KeyboardErrorCode Engine::init(const KBEngineConfig& config) {
    std::lock_guard<std::mutex> lock(engineMutex_);

    if (state_ == KB_STATE_READY) {
        platform::logWarn(TAG, "Engine already initialized");
        return KB_ERR_ALREADY_INITIALIZED;
    }

    state_ = KB_STATE_INITIALIZING;
    config_ = config;

    platform::logInfo(TAG, "Initializing engine...");

    // Initialize neural engine
    if (config.useNeural) {
        auto backend = nn::getDefaultBackend();
        neuralEngine_ = nn::createNeuralEngine(backend);

        if (config.modelPath && std::strlen(config.modelPath) > 0) {
            nn::NeuralEngineConfig nnConfig;
            nnConfig.modelPath = config.modelPath;
            nnConfig.numThreads = static_cast<int>(config.neuralThreads);
            nnConfig.maxInferTimeMs = static_cast<int>(config.maxInferMs);
            nnConfig.warmupOnLoad = config.warmupOnInit;

            if (!neuralEngine_->load(nnConfig)) {
                platform::logWarn(TAG, "Failed to load neural model: %s",
                                 neuralEngine_->getLastError().c_str());
                // Continue without neural - will use fallback
            }
        }
    } else {
        // Create stub engine
        neuralEngine_ = nn::createNeuralEngine(nn::NeuralBackend::Stub);
    }

    // Initialize feature builder
    featureBuilder_ = std::make_unique<nn::FeatureBuilder>(neuralEngine_.get());

    // Initialize dictionary engine
    dictEngine_ = std::make_unique<DictEngine>();
    if (config.dictPath && std::strlen(config.dictPath) > 0) {
        if (!dictEngine_->loadMainDict(config.dictPath)) {
            platform::logWarn(TAG, "Failed to load main dictionary");
            // Continue - can still work with neural/rules
        }
    }

    if (config.personalDictPath && std::strlen(config.personalDictPath) > 0) {
        if (!dictEngine_->loadPersonalDict(config.personalDictPath)) {
            platform::logWarn(TAG, "Failed to load personal dictionary");
        }
    }

    // Initialize fallback policy
    fallbackPolicy_ = std::make_unique<FallbackPolicy>();
    fallbackPolicy_->setPolicy(config.fallbackPolicy);
    fallbackPolicy_->setLatencyBudget(
        static_cast<int>(config.p50TargetUs / 1000),
        static_cast<int>(config.p95TargetUs / 1000),
        static_cast<int>(config.p99TargetUs / 1000));

    // Initialize ranker
    ranker_ = std::make_unique<Ranker>();
    RankingConfig rankConfig;
    rankConfig.neuralWeight = config.neuralWeight;
    rankConfig.ngramWeight = config.ngramWeight;
    rankConfig.trieWeight = config.dictWeight;
    rankConfig.personalWeight = config.personalWeight;
    ranker_->setConfig(rankConfig);

    // Initialize session
    session_ = std::make_unique<Session>();

    // Initialize cache
    maxCacheSize_ = config.cacheSize > 0 ? config.cacheSize : 128;

    state_ = KB_STATE_READY;
    platform::logInfo(TAG, "Engine initialized successfully");

    return KB_OK;
}

KeyboardErrorCode Engine::initFromJson(const char* configJson) {
    // Parse JSON and call init with config struct
    KBEngineConfig config;
    kb_config_init_default(&config);

    if (configJson && std::strlen(configJson) > 0) {
        // Simple JSON parsing (in production, use a proper JSON library)
        // For now, just use defaults
        platform::logInfo(TAG, "JSON config parsing not fully implemented, using defaults");
    }

    return init(config);
}

void Engine::shutdown() {
    std::lock_guard<std::mutex> lock(engineMutex_);

    if (state_ == KB_STATE_UNINITIALIZED || state_ == KB_STATE_CLOSED) {
        return;
    }

    state_ = KB_STATE_CLOSING;
    platform::logInfo(TAG, "Shutting down engine...");

    // Clear cache
    clearCacheInternal();

    // Unload neural model
    if (neuralEngine_) {
        neuralEngine_->unload();
    }

    // Release resources
    neuralEngine_.reset();
    featureBuilder_.reset();
    dictEngine_.reset();
    ngramEngine_.reset();
    ruleEngine_.reset();
    fallbackPolicy_.reset();
    ranker_.reset();
    session_.reset();

    state_ = KB_STATE_CLOSED;
    platform::logInfo(TAG, "Engine shutdown complete");
}

KeyboardErrorCode Engine::getStatus(KBEngineStatus& status) const {
    std::lock_guard<std::mutex> lock(engineMutex_);

    status.state = state_;
    status.neuralReady = neuralEngine_ && neuralEngine_->isReady() ? 1 : 0;
    status.dictReady = dictEngine_ && dictEngine_->isLoaded() ? 1 : 0;
    status.personalDictReady = dictEngine_ && dictEngine_->hasPersonalDict() ? 1 : 0;

    status.dictWordCount = dictEngine_ ? static_cast<uint32_t>(dictEngine_->getWordCount()) : 0;
    status.personalWordCount = dictEngine_ ?
        static_cast<uint32_t>(dictEngine_->getPersonalWordCount()) : 0;

    {
        std::lock_guard<std::mutex> statsLock(statsMutex_);
        status.totalPredictions = totalPredictions_;
        status.cacheHits = cacheHits_;
        status.cacheMisses = cacheMisses_;
        status.fallbackCount = fallbackCount_;

        status.avgLatencyUs = getPercentileLatency(50);
        status.p50LatencyUs = getPercentileLatency(50);
        status.p95LatencyUs = getPercentileLatency(95);
        status.p99LatencyUs = getPercentileLatency(99);
    }

    status.memoryUsage = 0;
    if (neuralEngine_) {
        status.memoryUsage += neuralEngine_->getMemoryUsage();
    }
    if (dictEngine_) {
        status.memoryUsage += dictEngine_->getMemoryUsage();
    }

    return KB_OK;
}

// ============================================================================
// Prediction
// ============================================================================

KeyboardErrorCode Engine::predict(const KBPredictContext& context,
                                   KBPredictResult& result) {
    auto startTime = std::chrono::high_resolution_clock::now();

    if (state_ != KB_STATE_READY) {
        return KB_ERR_NOT_INITIALIZED;
    }

    // Check cache
    std::string cacheKey = buildCacheKey(context);
    if (config_.enableCache) {
        const auto* cached = getCached(cacheKey);
        if (cached) {
            // Copy cached results
            result.candidateCount = static_cast<uint32_t>(cached->size());
            result.candidates = new KBCandidate[result.candidateCount];
            for (size_t i = 0; i < cached->size(); ++i) {
                result.candidates[i] = (*cached)[i];
                // Deep copy text
                result.candidates[i].text = strdup((*cached)[i].text);
            }
            result.totalUs = 100;  // Cache hit is fast
            return KB_OK;
        }
    }

    // Run prediction pipeline
    PredictionPipeline pipeline;
    auto err = runPipeline(context, pipeline);
    if (err != KB_OK) {
        return err;
    }

    // Build result
    buildResult(pipeline, result);

    // Record timing
    auto endTime = std::chrono::high_resolution_clock::now();
    auto totalUs = std::chrono::duration_cast<std::chrono::microseconds>(
        endTime - startTime).count();

    result.totalUs = static_cast<uint32_t>(totalUs);
    result.preprocessUs = 0;  // TODO: measure
    result.inferUs = pipeline.neuralTimeUs;
    result.rankUs = pipeline.rankTimeUs;
    result.usedFallback = pipeline.usedFallback ? 1 : 0;

    // Update stats
    recordLatency(static_cast<uint32_t>(totalUs));
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        totalPredictions_++;
        if (pipeline.usedFallback) {
            fallbackCount_++;
        }
    }

    // Add to cache
    if (config_.enableCache && result.candidateCount > 0) {
        std::vector<KBCandidate> candidates(
            result.candidates, result.candidates + result.candidateCount);
        addToCache(cacheKey, candidates);
    }

    // Generate trace ID if enabled
    if (config_.enableTracing) {
        result.traceId = strdup(generateTraceId().c_str());
    }

    return KB_OK;
}

char* Engine::predictJson(const char* contextJson) {
    // Parse context JSON
    KBPredictContext context = {};
    context.maxResults = 10;
    context.deadlineMs = 20;

    // Simple parsing - in production use proper JSON library
    if (contextJson) {
        // Extract input field (simplified)
        std::string json(contextJson);
        auto inputPos = json.find("\"input\"");
        if (inputPos != std::string::npos) {
            auto colonPos = json.find(":", inputPos);
            auto quoteStart = json.find("\"", colonPos + 1);
            auto quoteEnd = json.find("\"", quoteStart + 1);
            if (quoteStart != std::string::npos && quoteEnd != std::string::npos) {
                std::string input = json.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                context.inputText = input.c_str();
                context.inputLength = static_cast<uint32_t>(input.length());
            }
        }
    }

    KBPredictResult result = {};
    auto err = predict(context, result);

    // Build JSON response
    std::ostringstream oss;
    oss << "{";

    if (err == KB_OK) {
        oss << "\"candidates\":[";
        for (uint32_t i = 0; i < result.candidateCount; ++i) {
            if (i > 0) oss << ",";
            oss << "{\"text\":\"" << result.candidates[i].text << "\"";
            oss << ",\"score\":" << std::fixed << std::setprecision(3)
                << result.candidates[i].score;

            const char* source = "trie";
            if (result.candidates[i].source == KB_SOURCE_NEURAL) source = "nn";
            else if (result.candidates[i].source == KB_SOURCE_NGRAM) source = "ngram";
            else if (result.candidates[i].source == KB_SOURCE_RULE) source = "rule";

            oss << ",\"source\":\"" << source << "\"}";
        }
        oss << "],";
        oss << "\"latencyMs\":" << std::fixed << std::setprecision(2)
            << (result.totalUs / 1000.0);
        if (result.traceId) {
            oss << ",\"traceId\":\"" << result.traceId << "\"";
        }
    } else {
        oss << "\"error\":\"" << kb_error_string(err) << "\"";
    }

    oss << "}";

    // Free result
    if (result.candidates) {
        for (uint32_t i = 0; i < result.candidateCount; ++i) {
            free(result.candidates[i].text);
        }
        delete[] result.candidates;
    }
    if (result.traceId) free(result.traceId);

    return strdup(oss.str().c_str());
}

KeyboardErrorCode Engine::runPipeline(const KBPredictContext& context,
                                       PredictionPipeline& pipeline) {
    // Extract input
    std::string inputText;
    if (context.inputText && context.inputLength > 0) {
        inputText = std::string(context.inputText, context.inputLength);
    }

    std::string prevWord1;
    if (context.prevWord1) {
        prevWord1 = context.prevWord1;
    } else if (session_) {
        prevWord1 = session_->getPrevWord1();
    }

    int maxResults = context.maxResults > 0 ?
        static_cast<int>(context.maxResults) : 10;
    int deadlineMs = context.deadlineMs > 0 ?
        static_cast<int>(context.deadlineMs) :
        static_cast<int>(config_.maxInferMs);

    // Get trie candidates
    auto trieStart = std::chrono::high_resolution_clock::now();
    getTrieCandidates(inputText, maxResults * 2, pipeline.trieResults);
    auto trieEnd = std::chrono::high_resolution_clock::now();
    pipeline.trieTimeUs = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(trieEnd - trieStart).count());

    // Get ngram candidates
    auto ngramStart = std::chrono::high_resolution_clock::now();
    getNgramCandidates(prevWord1, inputText, maxResults, pipeline.ngramResults);
    auto ngramEnd = std::chrono::high_resolution_clock::now();
    pipeline.ngramTimeUs = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(ngramEnd - ngramStart).count());

    // Decide on neural usage
    FallbackContext fbCtx;
    fbCtx.neuralReady = neuralEngine_ && neuralEngine_->isReady();
    fbCtx.totalDeadlineMs = deadlineMs;
    fbCtx.elapsedMs = static_cast<int>((pipeline.trieTimeUs + pipeline.ngramTimeUs) / 1000);
    fbCtx.recentNeuralLatencyMs = getPercentileLatency(95) / 1000;
    fbCtx.recentFallbackRate = totalPredictions_ > 0 ?
        static_cast<int>(fallbackCount_ * 100 / totalPredictions_) : 0;

    auto decision = fallbackPolicy_->decide(fbCtx);

    if (decision.shouldUseNeural && fbCtx.neuralReady) {
        // Build neural features
        nn::NeuralFeatures features = featureBuilder_->buildFromText(inputText, prevWord1);

        // Collect candidates to score
        std::vector<std::string> candidates;
        candidates.insert(candidates.end(),
            pipeline.trieResults.begin(), pipeline.trieResults.end());
        candidates.insert(candidates.end(),
            pipeline.ngramResults.begin(), pipeline.ngramResults.end());

        // Remove duplicates
        std::sort(candidates.begin(), candidates.end());
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

        // Run neural inference
        auto neuralStart = std::chrono::high_resolution_clock::now();
        pipeline.neuralResult = getNeuralScores(features, candidates, decision.neuralDeadlineMs);
        auto neuralEnd = std::chrono::high_resolution_clock::now();
        pipeline.neuralTimeUs = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(neuralEnd - neuralStart).count());

        if (pipeline.neuralResult.timedOut || pipeline.neuralResult.usedFallback) {
            pipeline.usedFallback = true;
            pipeline.fallbackReason = FallbackReason::NeuralTimeout;
        }
    } else {
        pipeline.usedFallback = true;
        pipeline.fallbackReason = decision.reason;
    }

    // Get rule-based candidates if needed
    if (decision.shouldUseRule || pipeline.usedFallback) {
        auto ruleStart = std::chrono::high_resolution_clock::now();
        getRuleCandidates(inputText, maxResults, pipeline.ruleResults);
        auto ruleEnd = std::chrono::high_resolution_clock::now();
        pipeline.ruleTimeUs = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(ruleEnd - ruleStart).count());
    }

    return KB_OK;
}

void Engine::getTrieCandidates(const std::string& prefix, int maxResults,
                                std::vector<std::string>& results) {
    if (!dictEngine_) return;

    auto suggestions = dictEngine_->getSuggestions(prefix, maxResults);
    results.reserve(suggestions.size());
    for (const auto& s : suggestions) {
        results.push_back(s.word);
    }
}

void Engine::getNgramCandidates(const std::string& /*prevWord*/,
                                 const std::string& /*prefix*/,
                                 int /*maxResults*/,
                                 std::vector<std::string>& /*results*/) {
    // N-gram engine not yet implemented
    // Would query n-gram model for likely continuations
}

nn::NeuralResult Engine::getNeuralScores(const nn::NeuralFeatures& features,
                                          const std::vector<std::string>& candidates,
                                          int deadlineMs) {
    if (!neuralEngine_ || !neuralEngine_->isReady()) {
        nn::NeuralResult result;
        result.usedFallback = true;
        return result;
    }

    return neuralEngine_->inferWithTimeout(features, candidates, deadlineMs);
}

void Engine::getRuleCandidates(const std::string& prefix, int maxResults,
                                std::vector<std::string>& results) {
    // Simple rule-based: just return the input as-is if valid
    if (!prefix.empty()) {
        results.push_back(prefix);
    }

    // Add some common completions (hardcoded fallback)
    // In production, this would be more sophisticated
    if (prefix.length() >= 2) {
        // This is a minimal fallback - real implementation would use patterns
        (void)maxResults;  // Silence unused warning
    }
}

void Engine::buildResult(const PredictionPipeline& pipeline,
                          KBPredictResult& result) {
    if (!ranker_) return;

    // Convert to ranked format
    std::vector<std::pair<std::string, float>> triePairs;
    float score = 1.0f;
    for (const auto& word : pipeline.trieResults) {
        triePairs.emplace_back(word, score);
        score *= 0.95f;
    }

    std::vector<std::pair<std::string, float>> ngramPairs;
    score = 1.0f;
    for (const auto& word : pipeline.ngramResults) {
        ngramPairs.emplace_back(word, score);
        score *= 0.95f;
    }

    std::string inputText;  // Extract from context if needed

    // Rank candidates
    auto ranked = ranker_->rank(triePairs, ngramPairs, pipeline.neuralResult,
                                pipeline.ruleResults, inputText, 10);

    // Convert to output format
    Ranker::toOutputCandidates(ranked, result);
}

// ============================================================================
// Learning
// ============================================================================

KeyboardErrorCode Engine::learn(const KBLearnEvent& event) {
    if (state_ != KB_STATE_READY) {
        return KB_ERR_NOT_INITIALIZED;
    }

    if (!event.word) {
        return KB_ERR_INVALID_ARG;
    }

    // Update session context
    if (session_ && (event.type == KB_LEARN_WORD_SELECTED ||
                     event.type == KB_LEARN_WORD_TYPED)) {
        session_->commitWord(event.word);
        if (event.type == KB_LEARN_WORD_SELECTED) {
            session_->recordSelection(event.word);
        }
    }

    // Update personal dictionary
    if (dictEngine_) {
        if (event.type == KB_LEARN_WORD_SELECTED ||
            event.type == KB_LEARN_WORD_TYPED) {
            dictEngine_->addToPersonalDict(event.word,
                event.frequency > 0 ? event.frequency : 1);
        } else if (event.type == KB_LEARN_WORD_DELETED) {
            dictEngine_->removeFromPersonalDict(event.word);
        }
    }

    // Clear relevant cache entries
    clearCacheInternal();

    return KB_OK;
}

KeyboardErrorCode Engine::learnJson(const char* /*eventJson*/) {
    // Parse JSON and call learn
    // Simplified implementation
    return KB_OK;
}

KeyboardErrorCode Engine::flushLearning() {
    if (dictEngine_) {
        dictEngine_->savePersonalDict();
    }
    return KB_OK;
}

// ============================================================================
// Session
// ============================================================================

KeyboardErrorCode Engine::resetSession() {
    if (session_) {
        session_->reset();
    }
    clearCacheInternal();
    return KB_OK;
}

KeyboardErrorCode Engine::clearCache() {
    clearCacheInternal();
    return KB_OK;
}

// ============================================================================
// Dictionary Management
// ============================================================================

KeyboardErrorCode Engine::loadDictionary(const char* path, int dictType) {
    if (!dictEngine_) {
        dictEngine_ = std::make_unique<DictEngine>();
    }

    bool success = false;
    if (dictType == 0) {
        success = dictEngine_->loadMainDict(path);
    } else {
        success = dictEngine_->loadPersonalDict(path);
    }

    return success ? KB_OK : KB_ERR_DICT_LOAD_FAILED;
}

KeyboardErrorCode Engine::loadDictionaryFd(int fd, size_t offset, size_t length, int dictType) {
    if (!dictEngine_) {
        dictEngine_ = std::make_unique<DictEngine>();
    }

    bool success = false;
    if (dictType == 0) {
        success = dictEngine_->loadMainDictFd(fd, offset, length);
    } else {
        success = dictEngine_->loadPersonalDictFd(fd, offset, length);
    }

    return success ? KB_OK : KB_ERR_DICT_LOAD_FAILED;
}

KeyboardErrorCode Engine::addWord(const char* word, int frequency) {
    if (!dictEngine_) return KB_ERR_NOT_INITIALIZED;
    if (!word) return KB_ERR_INVALID_ARG;

    dictEngine_->addToPersonalDict(word, frequency);
    return KB_OK;
}

KeyboardErrorCode Engine::removeWord(const char* word) {
    if (!dictEngine_) return KB_ERR_NOT_INITIALIZED;
    if (!word) return KB_ERR_INVALID_ARG;

    dictEngine_->removeFromPersonalDict(word);
    return KB_OK;
}

bool Engine::wordExists(const char* word) const {
    if (!dictEngine_ || !word) return false;
    return dictEngine_->contains(word);
}

// ============================================================================
// Neural Model Management
// ============================================================================

KeyboardErrorCode Engine::loadModel(const char* modelPath) {
    if (!neuralEngine_) {
        neuralEngine_ = nn::createNeuralEngine(nn::NeuralBackend::Auto);
    }

    nn::NeuralEngineConfig nnConfig;
    nnConfig.modelPath = modelPath;
    nnConfig.numThreads = static_cast<int>(config_.neuralThreads);
    nnConfig.maxInferTimeMs = static_cast<int>(config_.maxInferMs);

    if (!neuralEngine_->load(nnConfig)) {
        return KB_ERR_MODEL_LOAD_FAILED;
    }

    return KB_OK;
}

KeyboardErrorCode Engine::warmupModel() {
    if (!neuralEngine_) return KB_ERR_NOT_INITIALIZED;
    return neuralEngine_->warmup() ? KB_OK : KB_ERR_INIT_FAILED;
}

KeyboardErrorCode Engine::unloadModel() {
    if (neuralEngine_) {
        neuralEngine_->unload();
    }
    return KB_OK;
}

bool Engine::modelReady() const {
    return neuralEngine_ && neuralEngine_->isReady();
}

// ============================================================================
// Debug
// ============================================================================

char* Engine::getTimingInfo() const {
    std::lock_guard<std::mutex> lock(timingMutex_);

    std::ostringstream oss;
    oss << "{";
    oss << "\"preprocessUs\":" << lastTiming_.preprocessUs;
    oss << ",\"trieUs\":" << lastTiming_.trieUs;
    oss << ",\"ngramUs\":" << lastTiming_.ngramUs;
    oss << ",\"neuralUs\":" << lastTiming_.neuralUs;
    oss << ",\"ruleUs\":" << lastTiming_.ruleUs;
    oss << ",\"rankUs\":" << lastTiming_.rankUs;
    oss << ",\"totalUs\":" << lastTiming_.totalUs;
    oss << ",\"usedFallback\":" << (lastTiming_.usedFallback ? "true" : "false");
    if (!lastTiming_.traceId.empty()) {
        oss << ",\"traceId\":\"" << lastTiming_.traceId << "\"";
    }
    oss << "}";

    return strdup(oss.str().c_str());
}

// ============================================================================
// Cache
// ============================================================================

const std::vector<KBCandidate>* Engine::getCached(const std::string& key) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);

    auto it = cacheMap_.find(key);
    if (it == cacheMap_.end()) {
        return nullptr;
    }

    // Move to front (LRU)
    auto listIt = it->second;
    if (listIt != cacheList_.begin()) {
        const_cast<std::list<CacheEntry>&>(cacheList_).splice(
            cacheList_.begin(), cacheList_, listIt);
    }

    {
        std::lock_guard<std::mutex> statsLock(statsMutex_);
        const_cast<uint64_t&>(cacheHits_)++;
    }

    return &cacheList_.front().candidates;
}

void Engine::addToCache(const std::string& key,
                        const std::vector<KBCandidate>& candidates) {
    std::lock_guard<std::mutex> lock(cacheMutex_);

    // Already in cache?
    if (cacheMap_.find(key) != cacheMap_.end()) {
        return;
    }

    // Add to front
    CacheEntry entry;
    entry.key = key;
    entry.candidates = candidates;
    entry.timestamp = std::chrono::steady_clock::now();

    cacheList_.push_front(entry);
    cacheMap_[key] = cacheList_.begin();

    // Evict if over capacity
    while (cacheList_.size() > maxCacheSize_) {
        auto& oldest = cacheList_.back();
        cacheMap_.erase(oldest.key);
        cacheList_.pop_back();
    }

    {
        std::lock_guard<std::mutex> statsLock(statsMutex_);
        cacheMisses_++;
    }
}

void Engine::clearCacheInternal() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    cacheList_.clear();
    cacheMap_.clear();
}

// ============================================================================
// Statistics
// ============================================================================

void Engine::recordLatency(uint32_t latencyUs) {
    std::lock_guard<std::mutex> lock(statsMutex_);

    latencyHistory_.push_back(latencyUs);
    if (latencyHistory_.size() > LATENCY_HISTORY_SIZE) {
        latencyHistory_.erase(latencyHistory_.begin());
    }
}

uint32_t Engine::getPercentileLatency(int percentile) const {
    if (latencyHistory_.empty()) {
        return 0;
    }

    std::vector<uint32_t> sorted = latencyHistory_;
    std::sort(sorted.begin(), sorted.end());

    size_t index = (sorted.size() * percentile) / 100;
    if (index >= sorted.size()) {
        index = sorted.size() - 1;
    }

    return sorted[index];
}

// ============================================================================
// Helpers
// ============================================================================

std::string Engine::generateTraceId() const {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);

    const char* hex = "0123456789abcdef";
    std::string id;
    id.reserve(12);

    for (int i = 0; i < 12; ++i) {
        id.push_back(hex[dis(gen)]);
    }

    return id;
}

void Engine::setError(const std::string& error) {
    lastError_ = error;
    platform::logError(TAG, "%s", error.c_str());
}

std::string Engine::buildCacheKey(const KBPredictContext& context) {
    std::string key;
    if (context.inputText && context.inputLength > 0) {
        key = std::string(context.inputText, context.inputLength);
    }
    if (context.prevWord1) {
        key += "|";
        key += context.prevWord1;
    }
    return key;
}

} // namespace core
} // namespace keyboard
