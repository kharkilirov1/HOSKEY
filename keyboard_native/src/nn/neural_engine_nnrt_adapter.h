/**
 * HOSKEY Keyboard - Neural Engine Adapter for NNRtScorer
 *
 * Adapter that wraps existing NNRtScorer/NeuralModelManager
 * to work with keyboard_native INeuralEngine interface.
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#ifndef KEYBOARD_NATIVE_NEURAL_ENGINE_NNRT_ADAPTER_H
#define KEYBOARD_NATIVE_NEURAL_ENGINE_NNRT_ADAPTER_H

#include "i_neural_engine.h"

// Include existing HOSKEY neural infrastructure
#include "dictionary_hoskey/nnrt_scorer.h"
#include "dictionary_hoskey/neural_model_manager.h"

#include <memory>
#include <mutex>
#include <atomic>

namespace keyboard {
namespace nn {

/**
 * Adapter for existing NNRtScorer/NeuralModelManager
 *
 * Delegates to existing MindSpore infrastructure instead of
 * reimplementing everything. This allows keyboard_native to
 * use the battle-tested code in nnrt_scorer.cpp.
 */
class NeuralEngineNNRtAdapter : public INeuralEngine {
public:
    /**
     * Create adapter with existing model manager
     * @param manager Existing NeuralModelManager (owned externally)
     */
    explicit NeuralEngineNNRtAdapter(yandex::NeuralModelManager* manager = nullptr);

    ~NeuralEngineNNRtAdapter() override;

    // ========================================================================
    // INeuralEngine Interface
    // ========================================================================

    bool load(const NeuralEngineConfig& config) override;
    bool loadFromMemory(const void* data, size_t size,
                        const NeuralEngineConfig& config) override;
    void unload() override;
    bool warmup() override;

    NeuralResult infer(const NeuralFeatures& features,
                       const std::vector<std::string>& candidates) override;

    NeuralResult inferWithTimeout(const NeuralFeatures& features,
                                  const std::vector<std::string>& candidates,
                                  int timeoutMs) override;

    bool isReady() const override;
    NeuralEngineState getState() const override;
    std::string getModelInfo() const override;
    size_t getMemoryUsage() const override;
    std::string getLastError() const override;

    std::vector<int32_t> tokenize(const std::string& word) const override;
    int getVocabSize() const override;

    // ========================================================================
    // Adapter-specific
    // ========================================================================

    /**
     * Set external model manager
     */
    void setModelManager(yandex::NeuralModelManager* manager);

    /**
     * Get direct access to model manager
     */
    yandex::NeuralModelManager* getModelManager() const { return modelManager_; }

    /**
     * Use specific model type for scoring
     */
    void setModelType(yandex::ModelType type) { modelType_ = type; }

private:
    // External model manager (not owned)
    yandex::NeuralModelManager* modelManager_ = nullptr;

    // Own scorer for standalone use
    std::unique_ptr<yandex::NNRtScorer> ownScorer_;

    // Which model to use
    yandex::ModelType modelType_ = yandex::ModelType::TAP_RANKER;

    // State
    mutable std::mutex mutex_;
    std::atomic<NeuralEngineState> state_{NeuralEngineState::Unloaded};
    std::string lastError_;
    std::string modelsDir_;

    // Convert between formats
    std::vector<yandex::ScoringCandidate> toScoringCandidates(
        const NeuralFeatures& features,
        const std::vector<std::string>& candidates) const;

    NeuralResult fromScoredWords(
        const std::vector<yandex::ScoredWord>& scored,
        float inferTimeMs) const;
};

} // namespace nn
} // namespace keyboard

#endif // KEYBOARD_NATIVE_NEURAL_ENGINE_NNRT_ADAPTER_H
