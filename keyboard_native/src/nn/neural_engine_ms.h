/**
 * HOSKEY Keyboard - MindSpore Lite Neural Engine
 *
 * Implementation of INeuralEngine using MindSpore Lite runtime.
 * Supports CPU and NPU inference on HarmonyOS.
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#ifndef KEYBOARD_NATIVE_NEURAL_ENGINE_MS_H
#define KEYBOARD_NATIVE_NEURAL_ENGINE_MS_H

#include "i_neural_engine.h"
#include <mutex>
#include <atomic>
#include <thread>
#include <future>
#include <unordered_map>

// Forward declarations for MindSpore types
// Avoids including heavy MindSpore headers in this header
namespace mindspore {
class Model;
class Context;
namespace tensor {
class MSTensor;
}
}

namespace keyboard {
namespace nn {

/**
 * MindSpore Lite implementation of neural engine
 */
class NeuralEngineMindSpore : public INeuralEngine {
public:
    NeuralEngineMindSpore();
    ~NeuralEngineMindSpore() override;

    // Disable copy
    NeuralEngineMindSpore(const NeuralEngineMindSpore&) = delete;
    NeuralEngineMindSpore& operator=(const NeuralEngineMindSpore&) = delete;

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

private:
    // ========================================================================
    // Internal Methods
    // ========================================================================

    bool initContext(const NeuralEngineConfig& config);
    bool loadModelInternal(const void* data, size_t size);
    bool validateModel();
    bool buildVocabulary();

    // Prepare input tensors from features
    bool prepareInputs(const NeuralFeatures& features,
                       const std::vector<std::string>& candidates);

    // Extract results from output tensors
    NeuralResult extractResults(const std::vector<std::string>& candidates);

    // Run inference (no timeout)
    bool runInference();

    // ========================================================================
    // MindSpore Runtime
    // ========================================================================

#ifdef USE_MINDSPORE
    std::unique_ptr<mindspore::Model> model_;
    std::unique_ptr<mindspore::Context> context_;
    std::vector<mindspore::tensor::MSTensor> inputTensors_;
    std::vector<mindspore::tensor::MSTensor> outputTensors_;
#else
    void* model_ = nullptr;  // Placeholder when MindSpore disabled
    void* context_ = nullptr;
#endif

    // ========================================================================
    // Vocabulary
    // ========================================================================

    std::unordered_map<std::string, int32_t> vocab_;  // word -> id
    std::vector<std::string> reverseVocab_;           // id -> word
    int32_t unkTokenId_ = 0;
    int32_t padTokenId_ = 0;

    // ========================================================================
    // State
    // ========================================================================

    std::atomic<NeuralEngineState> state_{NeuralEngineState::Unloaded};
    mutable std::mutex mutex_;
    std::string lastError_;
    NeuralEngineConfig config_;

    // Model info
    std::string modelName_;
    std::string modelVersion_;
    size_t modelMemoryUsage_ = 0;

    // Input/output tensor info
    struct TensorInfo {
        std::string name;
        std::vector<int64_t> shape;
        int dataType;  // 0=float, 1=int32, etc.
    };
    std::vector<TensorInfo> inputInfo_;
    std::vector<TensorInfo> outputInfo_;

    // Pre-allocated buffers
    std::vector<float> inputBuffer_;
    std::vector<float> outputBuffer_;
};

/**
 * Stub implementation when MindSpore is not available
 * Returns empty results, never fails
 */
class NeuralEngineStub : public INeuralEngine {
public:
    NeuralEngineStub() = default;
    ~NeuralEngineStub() override = default;

    bool load(const NeuralEngineConfig& /*config*/) override { return true; }
    bool loadFromMemory(const void* /*data*/, size_t /*size*/,
                        const NeuralEngineConfig& /*config*/) override { return true; }
    void unload() override {}
    bool warmup() override { return true; }

    NeuralResult infer(const NeuralFeatures& /*features*/,
                       const std::vector<std::string>& candidates) override {
        NeuralResult result;
        result.inferenceTimeMs = 0.1f;
        result.timedOut = false;
        result.usedFallback = true;

        // Return candidates with uniform scores
        float score = 1.0f;
        for (const auto& candidate : candidates) {
            NeuralCandidate nc;
            nc.text = candidate;
            nc.score = score;
            nc.confidence = score;
            nc.rank = static_cast<int>(result.candidates.size());
            result.candidates.push_back(nc);
            score *= 0.9f;  // Decay for ranking
        }
        return result;
    }

    NeuralResult inferWithTimeout(const NeuralFeatures& features,
                                  const std::vector<std::string>& candidates,
                                  int /*timeoutMs*/) override {
        return infer(features, candidates);
    }

    bool isReady() const override { return true; }
    NeuralEngineState getState() const override { return NeuralEngineState::Ready; }
    std::string getModelInfo() const override { return "Stub Neural Engine v1.0"; }
    size_t getMemoryUsage() const override { return 0; }
    std::string getLastError() const override { return ""; }

    std::vector<int32_t> tokenize(const std::string& /*word*/) const override {
        return {0};  // Return UNK token
    }
    int getVocabSize() const override { return 1; }
};

} // namespace nn
} // namespace keyboard

#endif // KEYBOARD_NATIVE_NEURAL_ENGINE_MS_H
