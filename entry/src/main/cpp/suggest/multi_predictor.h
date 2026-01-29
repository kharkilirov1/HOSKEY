/*
 * Copyright (c) 2024 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 * 
 * Multi-Predictor Architecture (inspired by Yandex Keyboard)
 * Combines multiple prediction sources for better suggestions.
 */

#ifndef HOSKEY_MULTI_PREDICTOR_H
#define HOSKEY_MULTI_PREDICTOR_H

#include <vector>
#include <memory>
#include <string>
#include <algorithm>

#include "defines.h"

namespace latinime {

// Forward declarations
class Dictionary;
class SuggestionResults;
class ProximityInfo;

/**
 * Single suggestion with metadata
 */
struct Suggestion {
    std::vector<int> codePoints;      // Word as code points
    float score;                       // Combined score (0.0 - 1.0)
    int probability;                   // Dictionary probability
    int sourceId;                      // Which predictor generated this
    bool isExactMatch;
    bool isAutoCorrection;
    
    Suggestion() : score(0.0f), probability(0), sourceId(0), 
                   isExactMatch(false), isAutoCorrection(false) {}
    
    // For sorting (higher score first)
    bool operator<(const Suggestion& other) const {
        return score > other.score;
    }
};

/**
 * Input context for predictors
 */
struct PredictionInput {
    const int* inputCodePoints;
    int inputLength;
    const int* prevWordCodePoints;
    int prevWordLength;
    const ProximityInfo* proximityInfo;
    const int* xCoordinates;
    const int* yCoordinates;
    int pointCount;
    bool isGesture;
    
    PredictionInput() 
        : inputCodePoints(nullptr), inputLength(0),
          prevWordCodePoints(nullptr), prevWordLength(0),
          proximityInfo(nullptr), xCoordinates(nullptr), 
          yCoordinates(nullptr), pointCount(0), isGesture(false) {}
};

/**
 * Base class for all predictors
 */
class Predictor {
public:
    virtual ~Predictor() = default;
    
    /**
     * Get predictions for the given input
     * @param input The input context
     * @param maxResults Maximum number of results to return
     * @param outSuggestions Output vector for suggestions
     */
    virtual void predict(const PredictionInput& input, 
                        int maxResults,
                        std::vector<Suggestion>& outSuggestions) = 0;
    
    /**
     * Get predictor priority (higher = more important)
     */
    virtual int getPriority() const { return 0; }
    
    /**
     * Get predictor source ID (for tracking)
     */
    virtual int getSourceId() const = 0;
    
    /**
     * Check if predictor is enabled
     */
    virtual bool isEnabled() const { return true; }
    
    /**
     * Get predictor name for debugging
     */
    virtual const char* getName() const = 0;
};

/**
 * Dictionary-based predictor (wraps existing OpenBoard logic)
 */
class DictionaryPredictor : public Predictor {
public:
    static constexpr int SOURCE_ID = 1;
    
    explicit DictionaryPredictor(Dictionary* dict) : mDictionary(dict) {}
    
    void predict(const PredictionInput& input, 
                int maxResults,
                std::vector<Suggestion>& outSuggestions) override;
    
    int getSourceId() const override { return SOURCE_ID; }
    const char* getName() const override { return "DictionaryPredictor"; }
    int getPriority() const override { return 100; }
    
private:
    Dictionary* mDictionary;
};

/**
 * N-gram based predictor for contextual suggestions
 */
class NgramPredictor : public Predictor {
public:
    static constexpr int SOURCE_ID = 2;
    
    explicit NgramPredictor(Dictionary* dict) : mDictionary(dict), mEnabled(true) {}
    
    void predict(const PredictionInput& input, 
                int maxResults,
                std::vector<Suggestion>& outSuggestions) override;
    
    int getSourceId() const override { return SOURCE_ID; }
    const char* getName() const override { return "NgramPredictor"; }
    int getPriority() const override { return 90; }
    bool isEnabled() const override { return mEnabled; }
    
    void setEnabled(bool enabled) { mEnabled = enabled; }
    
private:
    Dictionary* mDictionary;
    bool mEnabled;
};

/**
 * Multi-Predictor: Combines multiple prediction sources
 * 
 * Architecture inspired by Yandex Keyboard's multi-predictor system.
 * Allows plugging in different prediction engines and merging their results.
 */
class MultiPredictor {
public:
    static constexpr int DEFAULT_MAX_RESULTS = 24;
    static constexpr int MAX_PREDICTORS = 8;
    
    MultiPredictor();
    ~MultiPredictor();
    
    /**
     * Add a predictor to the pipeline
     * @param predictor Unique pointer to predictor (ownership transferred)
     */
    void addPredictor(std::unique_ptr<Predictor> predictor);
    
    /**
     * Remove a predictor by source ID
     */
    void removePredictor(int sourceId);
    
    /**
     * Clear all predictors
     */
    void clearPredictors();
    
    /**
     * Get suggestions from all predictors
     * @param input The input context
     * @param maxResults Maximum total results
     * @param outSuggestions Output vector (sorted by score)
     */
    void getSuggestions(const PredictionInput& input,
                       int maxResults,
                       std::vector<Suggestion>& outSuggestions);
    
    /**
     * Get number of active predictors
     */
    size_t getPredictorCount() const { return mPredictors.size(); }
    
    /**
     * Enable/disable a predictor by source ID
     */
    void setPredictorEnabled(int sourceId, bool enabled);
    
private:
    std::vector<std::unique_ptr<Predictor>> mPredictors;
    
    /**
     * Merge suggestions from multiple sources
     * - Removes duplicates (by code points)
     * - Re-scores based on multiple appearances
     * - Sorts by final score
     */
    void mergeSuggestions(std::vector<std::vector<Suggestion>>& allResults,
                         int maxResults,
                         std::vector<Suggestion>& outMerged);
    
    /**
     * Check if two suggestions are duplicates
     */
    static bool isDuplicate(const Suggestion& a, const Suggestion& b);
    
    /**
     * Calculate final score for merged suggestion
     */
    static float calculateMergedScore(const std::vector<const Suggestion*>& duplicates);
};

} // namespace latinime

#endif // HOSKEY_MULTI_PREDICTOR_H
