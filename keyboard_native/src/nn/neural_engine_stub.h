/**
 * HOSKEY Keyboard - Neural Engine Stub
 *
 * Fallback implementation when no neural backend is available.
 * Returns pass-through results with dummy scores.
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#ifndef KEYBOARD_NATIVE_NEURAL_ENGINE_STUB_H
#define KEYBOARD_NATIVE_NEURAL_ENGINE_STUB_H

#include "i_neural_engine.h"

namespace keyboard {
namespace nn {

/**
 * Stub neural engine - returns candidates as-is with default scores
 */
class NeuralEngineStub : public INeuralEngine {
public:
    NeuralEngineStub() = default;
    ~NeuralEngineStub() override = default;

    bool load(const NeuralEngineConfig& /*config*/) override {
        ready_ = true;
        return true;
    }

    bool loadFromMemory(const void* /*data*/, size_t /*size*/,
                        const NeuralEngineConfig& /*config*/) override {
        ready_ = true;
        return true;
    }

    void unload() override {
        ready_ = false;
    }

    bool warmup() override {
        return ready_;
    }

    NeuralResult infer(const NeuralFeatures& /*features*/,
                       const std::vector<std::string>& candidates) override {
        NeuralResult result;
        result.inferenceTimeMs = 0.0f;
        result.timedOut = false;
        result.usedFallback = true;

        float score = 1.0f;
        int rank = 0;
        for (const auto& c : candidates) {
            NeuralCandidate nc;
            nc.text = c;
            nc.score = score;
            nc.confidence = score;
            nc.rank = rank++;
            result.candidates.push_back(nc);
            score *= 0.9f;
        }

        return result;
    }

    NeuralResult inferWithTimeout(const NeuralFeatures& features,
                                  const std::vector<std::string>& candidates,
                                  int /*timeoutMs*/) override {
        return infer(features, candidates);
    }

    bool isReady() const override { return ready_; }

    NeuralEngineState getState() const override {
        return ready_ ? NeuralEngineState::Ready : NeuralEngineState::Unloaded;
    }

    std::string getModelInfo() const override {
        return "Stub Neural Engine (no-op)";
    }

    size_t getMemoryUsage() const override { return 0; }

    std::string getLastError() const override { return ""; }

    std::vector<int32_t> tokenize(const std::string& word) const override {
        std::vector<int32_t> tokens;
        for (char c : word) {
            tokens.push_back(static_cast<int32_t>(static_cast<unsigned char>(c)));
        }
        return tokens;
    }

    int getVocabSize() const override { return 256; }

private:
    bool ready_ = false;
};

} // namespace nn
} // namespace keyboard

#endif // KEYBOARD_NATIVE_NEURAL_ENGINE_STUB_H
