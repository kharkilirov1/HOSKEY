/**
 * HOSKEY Keyboard - Feature Builder Implementation
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#include "feature_builder.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace keyboard {
namespace nn {

FeatureBuilder::FeatureBuilder(const INeuralEngine* engine)
    : engine_(engine) {
}

NeuralFeatures FeatureBuilder::build(const KBPredictContext& context) const {
    NeuralFeatures features;

    // Extract input text
    std::string inputText;
    if (context.inputText && context.inputLength > 0) {
        inputText = std::string(context.inputText, context.inputLength);
    }

    // Normalize if configured
    if (config_.normalizeCase) {
        inputText = normalize(inputText);
    }

    // Tokenize input
    features.tokenIds = tokenize(inputText);
    features.inputLength = static_cast<int>(features.tokenIds.size());

    // Add context tokens (previous words)
    std::vector<int32_t> contextTokens;

    if (context.prevWord2) {
        std::string prev2 = config_.normalizeCase ?
            normalize(context.prevWord2) : context.prevWord2;
        auto tokens = tokenize(prev2);
        contextTokens.insert(contextTokens.end(), tokens.begin(), tokens.end());
    }

    if (context.prevWord1) {
        std::string prev1 = config_.normalizeCase ?
            normalize(context.prevWord1) : context.prevWord1;
        auto tokens = tokenize(prev1);
        contextTokens.insert(contextTokens.end(), tokens.begin(), tokens.end());
    }

    // Prepend context to input
    if (!contextTokens.empty()) {
        // Limit context length
        if (contextTokens.size() > static_cast<size_t>(config_.maxContextLength)) {
            contextTokens.erase(contextTokens.begin(),
                contextTokens.begin() + (contextTokens.size() - config_.maxContextLength));
        }

        features.tokenIds.insert(features.tokenIds.begin(),
            contextTokens.begin(), contextTokens.end());
    }

    features.contextLength = static_cast<int>(contextTokens.size());

    // Limit total length
    if (features.tokenIds.size() > static_cast<size_t>(
            config_.maxContextLength + config_.maxInputLength)) {
        features.tokenIds.resize(config_.maxContextLength + config_.maxInputLength);
    }

    // Process touch points if available
    if (config_.includeTouchFeatures &&
        context.touchPoints && context.touchPointCount > 0) {

        features.touchX.reserve(context.touchPointCount);
        features.touchY.reserve(context.touchPointCount);
        features.touchTime.reserve(context.touchPointCount);

        int64_t firstTimestamp = context.touchPoints[0].timestamp;

        for (uint32_t i = 0; i < context.touchPointCount; ++i) {
            const auto& point = context.touchPoints[i];

            features.touchX.push_back(normalizeX(point.x));
            features.touchY.push_back(normalizeY(point.y));

            // Normalize time relative to first point (0-1 range)
            float timeDelta = static_cast<float>(point.timestamp - firstTimestamp);
            features.touchTime.push_back(timeDelta / 1000.0f);  // Seconds
        }
    }

    features.isGesture = context.isGesture;

    return features;
}

NeuralFeatures FeatureBuilder::buildFromText(const std::string& input,
                                              const std::string& prevWord) const {
    NeuralFeatures features;

    std::string normalizedInput = config_.normalizeCase ? normalize(input) : input;
    std::string normalizedPrev = config_.normalizeCase ? normalize(prevWord) : prevWord;

    // Tokenize previous word as context
    if (!normalizedPrev.empty()) {
        auto contextTokens = tokenize(normalizedPrev);

        if (contextTokens.size() > static_cast<size_t>(config_.maxContextLength)) {
            contextTokens.erase(contextTokens.begin(),
                contextTokens.begin() + (contextTokens.size() - config_.maxContextLength));
        }

        features.tokenIds = std::move(contextTokens);
        features.contextLength = static_cast<int>(features.tokenIds.size());
    }

    // Tokenize input
    auto inputTokens = tokenize(normalizedInput);

    if (inputTokens.size() > static_cast<size_t>(config_.maxInputLength)) {
        inputTokens.resize(config_.maxInputLength);
    }

    features.inputLength = static_cast<int>(inputTokens.size());

    // Append input tokens
    features.tokenIds.insert(features.tokenIds.end(),
        inputTokens.begin(), inputTokens.end());

    features.isGesture = false;

    return features;
}

std::string FeatureBuilder::normalize(const std::string& text) {
    std::string result;
    result.reserve(text.size());

    // UTF-8 aware lowercase conversion
    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = static_cast<unsigned char>(text[i]);

        if ((c & 0x80) == 0) {
            // ASCII
            result.push_back(static_cast<char>(std::tolower(c)));
            i++;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
            // 2-byte UTF-8 (includes Cyrillic)
            unsigned char c1 = c;
            unsigned char c2 = static_cast<unsigned char>(text[i + 1]);

            // Cyrillic uppercase to lowercase
            // А-Я: D0 90-D0 AF -> D0 B0-D0 BF (а-п) and D1 80-D1 8F (р-я)
            if (c1 == 0xD0 && c2 >= 0x90 && c2 <= 0x9F) {
                // А-П -> а-п
                result.push_back(static_cast<char>(c1));
                result.push_back(static_cast<char>(c2 + 0x20));
            } else if (c1 == 0xD0 && c2 >= 0xA0 && c2 <= 0xAF) {
                // Р-Я -> р-я
                result.push_back(static_cast<char>(0xD1));
                result.push_back(static_cast<char>(c2 - 0x20));
            } else if (c1 == 0xD0 && c2 == 0x81) {
                // Ё -> ё
                result.push_back(static_cast<char>(0xD1));
                result.push_back(static_cast<char>(0x91));
            } else {
                // Keep as-is
                result.push_back(text[i]);
                result.push_back(text[i + 1]);
            }
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < text.size()) {
            // 3-byte UTF-8
            result.push_back(text[i]);
            result.push_back(text[i + 1]);
            result.push_back(text[i + 2]);
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < text.size()) {
            // 4-byte UTF-8
            result.push_back(text[i]);
            result.push_back(text[i + 1]);
            result.push_back(text[i + 2]);
            result.push_back(text[i + 3]);
            i += 4;
        } else {
            // Invalid UTF-8, skip
            i++;
        }
    }

    return result;
}

std::vector<int32_t> FeatureBuilder::toCodePoints(const std::string& text) {
    std::vector<int32_t> codePoints;
    codePoints.reserve(text.size());

    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        int32_t cp = 0;
        size_t len = 1;

        if ((c & 0x80) == 0) {
            cp = c;
            len = 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
            cp = (c & 0x1F) << 6;
            cp |= (static_cast<unsigned char>(text[i + 1]) & 0x3F);
            len = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < text.size()) {
            cp = (c & 0x0F) << 12;
            cp |= (static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6;
            cp |= (static_cast<unsigned char>(text[i + 2]) & 0x3F);
            len = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < text.size()) {
            cp = (c & 0x07) << 18;
            cp |= (static_cast<unsigned char>(text[i + 1]) & 0x3F) << 12;
            cp |= (static_cast<unsigned char>(text[i + 2]) & 0x3F) << 6;
            cp |= (static_cast<unsigned char>(text[i + 3]) & 0x3F);
            len = 4;
        } else {
            // Invalid UTF-8
            cp = 0xFFFD;  // Replacement character
            len = 1;
        }

        codePoints.push_back(cp);
        i += len;
    }

    return codePoints;
}

std::string FeatureBuilder::fromCodePoints(const std::vector<int32_t>& codePoints) {
    std::string result;
    result.reserve(codePoints.size() * 4);  // Worst case

    for (int32_t cp : codePoints) {
        if (cp < 0x80) {
            result.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            result.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            result.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x110000) {
            result.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            result.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    return result;
}

float FeatureBuilder::normalizeX(float x) const {
    return std::max(0.0f, std::min(1.0f, x / config_.keyboardWidth));
}

float FeatureBuilder::normalizeY(float y) const {
    return std::max(0.0f, std::min(1.0f, y / config_.keyboardHeight));
}

std::vector<int32_t> FeatureBuilder::tokenize(const std::string& text) const {
    if (engine_) {
        return engine_->tokenize(text);
    }

    // Fallback: character-level tokenization with simple mapping
    auto codePoints = toCodePoints(text);
    std::vector<int32_t> tokens;
    tokens.reserve(codePoints.size());

    for (int32_t cp : codePoints) {
        // Simple mapping: lowercase ASCII -> 4-29, Cyrillic -> 30-62
        if (cp >= 'a' && cp <= 'z') {
            tokens.push_back(4 + (cp - 'a'));
        } else if (cp >= 0x430 && cp <= 0x44F) {
            // Cyrillic lowercase а-я
            tokens.push_back(30 + (cp - 0x430));
        } else if (cp == 0x451) {
            // ё
            tokens.push_back(63);
        } else {
            // Unknown -> UNK token
            tokens.push_back(1);
        }
    }

    return tokens;
}

} // namespace nn
} // namespace keyboard
