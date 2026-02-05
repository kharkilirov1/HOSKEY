/**
 * HOSKEY Keyboard - Neural Engine Adapter Implementation
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#include "neural_engine_nnrt_adapter.h"
#include "neural_engine_stub.h"
#include "../platform/harmony_log.h"

#include <chrono>
#include <algorithm>

namespace keyboard {
namespace nn {

static constexpr const char* TAG = "NeuralEngineAdapter";

NeuralEngineNNRtAdapter::NeuralEngineNNRtAdapter(yandex::NeuralModelManager* manager)
    : modelManager_(manager) {
    if (manager && manager->getLoadedModelCount() > 0) {
        state_ = NeuralEngineState::Ready;
    }
    platform::logInfo(TAG, "NeuralEngineNNRtAdapter created");
}

NeuralEngineNNRtAdapter::~NeuralEngineNNRtAdapter() {
    unload();
    platform::logInfo(TAG, "NeuralEngineNNRtAdapter destroyed");
}

void NeuralEngineNNRtAdapter::setModelManager(yandex::NeuralModelManager* manager) {
    std::lock_guard<std::mutex> lock(mutex_);
    modelManager_ = manager;
    if (manager && manager->getLoadedModelCount() > 0) {
        state_ = NeuralEngineState::Ready;
    }
}

bool NeuralEngineNNRtAdapter::load(const NeuralEngineConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    state_ = NeuralEngineState::Loading;
    modelsDir_ = config.modelPath;

    // If we have external model manager, use it
    if (modelManager_) {
        if (!modelsDir_.empty()) {
            modelManager_->setModelsDirectory(modelsDir_);
        }

        // Load primary tap ranker model
        if (modelManager_->loadModel(modelType_)) {
            state_ = NeuralEngineState::Ready;
            platform::logInfo(TAG, "Using external ModelManager, loaded model type %d",
                             static_cast<int>(modelType_));
            return true;
        }
    }

    // Fallback: create own scorer
    ownScorer_ = std::make_unique<yandex::NNRtScorer>();
    ownScorer_->setDevice(yandex::InferenceDevice::Auto);

    std::string modelPath = config.modelPath;
    if (modelPath.empty()) {
        lastError_ = "No model path provided";
        state_ = NeuralEngineState::Error;
        return false;
    }

    // If path is directory, append default model name
    if (modelPath.back() == '/') {
        modelPath += yandex::GetModelFileName(modelType_);
    }

    if (!ownScorer_->loadModel(modelPath)) {
        lastError_ = "Failed to load model: " + modelPath;
        platform::logError(TAG, lastError_.c_str());
        state_ = NeuralEngineState::Error;
        return false;
    }

    state_ = NeuralEngineState::Ready;
    platform::logInfo(TAG, "Loaded model: %s, NPU=%d",
                     modelPath.c_str(), ownScorer_->isNpuActive() ? 1 : 0);

    if (config.warmupOnLoad) {
        warmup();
    }

    return true;
}

bool NeuralEngineNNRtAdapter::loadFromMemory(const void* /*data*/, size_t /*size*/,
                                              const NeuralEngineConfig& config) {
    // NNRtScorer doesn't support memory loading directly
    // Fall back to file-based loading
    return load(config);
}

void NeuralEngineNNRtAdapter::unload() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (ownScorer_) {
        ownScorer_->unload();
        ownScorer_.reset();
    }

    // Don't unload external model manager - it's shared
    state_ = NeuralEngineState::Unloaded;
    platform::logInfo(TAG, "Adapter unloaded");
}

bool NeuralEngineNNRtAdapter::warmup() {
    if (state_ != NeuralEngineState::Ready) {
        return false;
    }

    platform::logInfo(TAG, "Running warmup...");

    // Create dummy candidates
    std::vector<std::string> dummyCandidates = {"test", "warm", "up"};
    NeuralFeatures dummyFeatures;
    dummyFeatures.tokenIds = {1, 2, 3};

    auto result = infer(dummyFeatures, dummyCandidates);

    platform::logInfo(TAG, "Warmup complete, latency=%.2f ms", result.inferenceTimeMs);
    return !result.timedOut;
}

NeuralResult NeuralEngineNNRtAdapter::infer(const NeuralFeatures& features,
                                            const std::vector<std::string>& candidates) {
    NeuralResult result;
    result.timedOut = false;
    result.usedFallback = false;

    if (state_ != NeuralEngineState::Ready) {
        result.errorMessage = "Engine not ready";
        result.usedFallback = true;

        // Return stub results
        float score = 1.0f;
        for (const auto& c : candidates) {
            NeuralCandidate nc;
            nc.text = c;
            nc.score = score;
            nc.confidence = score;
            nc.rank = static_cast<int>(result.candidates.size());
            result.candidates.push_back(nc);
            score *= 0.9f;
        }
        return result;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    // Convert to NNRtScorer format
    auto scoringCandidates = toScoringCandidates(features, candidates);

    // Build context string from previous tokens
    std::string context;
    // In a real implementation, decode tokens to context

    std::vector<yandex::ScoredWord> scored;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (modelManager_) {
            // Use external model manager
            scored = modelManager_->scoreTapSuggestions(scoringCandidates, context);
        } else if (ownScorer_ && ownScorer_->isLoaded()) {
            // Use own scorer
            scored = ownScorer_->score(scoringCandidates, context);
        } else {
            result.errorMessage = "No scorer available";
            result.usedFallback = true;
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    float inferTimeMs = std::chrono::duration<float, std::milli>(
        endTime - startTime).count();

    // Convert result
    result = fromScoredWords(scored, inferTimeMs);

    return result;
}

NeuralResult NeuralEngineNNRtAdapter::inferWithTimeout(
    const NeuralFeatures& features,
    const std::vector<std::string>& candidates,
    int timeoutMs) {

    // NNRtScorer doesn't have built-in timeout, so we just call infer
    // In a production system, you'd use async execution with timeout

    auto start = std::chrono::steady_clock::now();
    auto result = infer(features, candidates);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    if (elapsed > timeoutMs) {
        result.timedOut = true;
        result.usedFallback = true;
        platform::logWarn(TAG, "Inference exceeded timeout: %lld > %d ms",
                         elapsed, timeoutMs);
    }

    return result;
}

bool NeuralEngineNNRtAdapter::isReady() const {
    if (modelManager_ && modelManager_->getLoadedModelCount() > 0) {
        return true;
    }
    if (ownScorer_ && ownScorer_->isLoaded()) {
        return true;
    }
    return false;
}

NeuralEngineState NeuralEngineNNRtAdapter::getState() const {
    return state_;
}

std::string NeuralEngineNNRtAdapter::getModelInfo() const {
    std::string info = "NNRtScorer Adapter\n";

    if (modelManager_) {
        auto stats = modelManager_->getStats();
        info += "  ModelManager: " + std::to_string(stats.loadedModels) + "/" +
                std::to_string(stats.totalModels) + " models loaded\n";
        info += "  Device: " + stats.primaryDevice + "\n";
        for (const auto& name : stats.loadedNames) {
            info += "    - " + name + "\n";
        }
    } else if (ownScorer_) {
        info += "  OwnScorer: " + ownScorer_->getDeviceInfo() + "\n";
        info += "  NPU Active: " + std::string(ownScorer_->isNpuActive() ? "yes" : "no") + "\n";
    }

    return info;
}

size_t NeuralEngineNNRtAdapter::getMemoryUsage() const {
    if (modelManager_) {
        return modelManager_->getStats().totalMemory;
    }
    // Estimate for single model
    return 10 * 1024 * 1024;  // ~10MB per model
}

std::string NeuralEngineNNRtAdapter::getLastError() const {
    return lastError_;
}

std::vector<int32_t> NeuralEngineNNRtAdapter::tokenize(const std::string& word) const {
    // Simple character-level tokenization
    std::vector<int32_t> tokens;

    size_t i = 0;
    while (i < word.size()) {
        unsigned char c = static_cast<unsigned char>(word[i]);
        int32_t cp = 0;
        size_t len = 1;

        if ((c & 0x80) == 0) {
            cp = c;
            len = 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < word.size()) {
            cp = (c & 0x1F) << 6;
            cp |= (static_cast<unsigned char>(word[i + 1]) & 0x3F);
            len = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < word.size()) {
            cp = (c & 0x0F) << 12;
            cp |= (static_cast<unsigned char>(word[i + 1]) & 0x3F) << 6;
            cp |= (static_cast<unsigned char>(word[i + 2]) & 0x3F);
            len = 3;
        } else {
            i++;
            continue;
        }

        // Map to token ID
        if (cp >= 'a' && cp <= 'z') {
            tokens.push_back(4 + (cp - 'a'));
        } else if (cp >= 0x430 && cp <= 0x44F) {
            tokens.push_back(30 + (cp - 0x430));
        } else {
            tokens.push_back(1);  // UNK
        }

        i += len;
    }

    return tokens;
}

int NeuralEngineNNRtAdapter::getVocabSize() const {
    return 64;  // Basic charset
}

std::vector<yandex::ScoringCandidate> NeuralEngineNNRtAdapter::toScoringCandidates(
    const NeuralFeatures& /*features*/,
    const std::vector<std::string>& candidates) const {

    std::vector<yandex::ScoringCandidate> result;
    result.reserve(candidates.size());

    uint32_t id = 0;
    for (const auto& word : candidates) {
        yandex::ScoringCandidate sc;
        sc.word = word;
        sc.wordId = id++;
        sc.baseScore = 0.5f;  // Default score
        result.push_back(sc);
    }

    return result;
}

NeuralResult NeuralEngineNNRtAdapter::fromScoredWords(
    const std::vector<yandex::ScoredWord>& scored,
    float inferTimeMs) const {

    NeuralResult result;
    result.inferenceTimeMs = inferTimeMs;
    result.timedOut = false;
    result.usedFallback = scored.empty();

    result.candidates.reserve(scored.size());

    int rank = 0;
    for (const auto& sw : scored) {
        NeuralCandidate nc;
        nc.text = sw.word;
        nc.score = sw.neuralScore;
        nc.confidence = sw.score;  // Combined score as confidence
        nc.rank = rank++;
        result.candidates.push_back(nc);
    }

    return result;
}

// Factory function update to support adapter
std::unique_ptr<INeuralEngine> createNeuralEngine(NeuralBackend backend) {
    switch (backend) {
        case NeuralBackend::NNRt:
            return std::make_unique<NeuralEngineNNRtAdapter>();

        case NeuralBackend::MindSpore:
#ifdef USE_MINDSPORE
            return std::make_unique<NeuralEngineMindSpore>();
#else
            // Fall through to NNRt adapter which uses MindSpore internally
            return std::make_unique<NeuralEngineNNRtAdapter>();
#endif

        case NeuralBackend::Auto:
        default:
            // Prefer NNRt adapter as it uses the battle-tested code
            return std::make_unique<NeuralEngineNNRtAdapter>();

        case NeuralBackend::Stub:
            return std::make_unique<NeuralEngineStub>();
    }
}

} // namespace nn
} // namespace keyboard
