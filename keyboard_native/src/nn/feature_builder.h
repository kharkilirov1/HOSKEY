/**
 * HOSKEY Keyboard - Feature Builder
 *
 * Builds feature vectors for neural inference from input context.
 * Handles tokenization, normalization, and feature extraction.
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#ifndef KEYBOARD_NATIVE_FEATURE_BUILDER_H
#define KEYBOARD_NATIVE_FEATURE_BUILDER_H

#include "i_neural_engine.h"
#include "../../include/types.h"
#include <string>
#include <vector>

namespace keyboard {
namespace nn {

/**
 * Feature builder configuration
 */
struct FeatureBuilderConfig {
    int maxContextLength = 32;      // Max context tokens
    int maxInputLength = 16;        // Max input tokens
    float keyboardWidth = 1080.0f;  // For touch normalization
    float keyboardHeight = 400.0f;
    bool normalizeCase = true;      // Lowercase input
    bool includeTouchFeatures = true;
};

/**
 * Builds neural features from prediction context
 */
class FeatureBuilder {
public:
    explicit FeatureBuilder(const INeuralEngine* engine = nullptr);

    /**
     * Set configuration
     */
    void setConfig(const FeatureBuilderConfig& config) { config_ = config; }

    /**
     * Set neural engine (for tokenization)
     */
    void setEngine(const INeuralEngine* engine) { engine_ = engine; }

    /**
     * Build features from prediction context
     */
    NeuralFeatures build(const KBPredictContext& context) const;

    /**
     * Build features from simple string input
     */
    NeuralFeatures buildFromText(const std::string& input,
                                  const std::string& prevWord = "") const;

    /**
     * Normalize text (lowercase, remove diacritics, etc.)
     */
    static std::string normalize(const std::string& text);

    /**
     * Convert UTF-8 string to code points
     */
    static std::vector<int32_t> toCodePoints(const std::string& text);

    /**
     * Convert code points to UTF-8 string
     */
    static std::string fromCodePoints(const std::vector<int32_t>& codePoints);

private:
    const INeuralEngine* engine_ = nullptr;
    FeatureBuilderConfig config_;

    // Normalize touch coordinates to [0, 1]
    float normalizeX(float x) const;
    float normalizeY(float y) const;

    // Tokenize with fallback if engine not available
    std::vector<int32_t> tokenize(const std::string& text) const;
};

} // namespace nn
} // namespace keyboard

#endif // KEYBOARD_NATIVE_FEATURE_BUILDER_H
