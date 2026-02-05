/*
 * Copyright (c) 2024 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 * 
 * Multi-Predictor Architecture (Yandex-style)
 * Combines multiple prediction sources with score fusion.
 */

#ifndef HOSKEY_MULTI_PREDICTOR_H
#define HOSKEY_MULTI_PREDICTOR_H

#include <vector>
#include <memory>
#include <string>
#include <algorithm>
#include <unordered_set>
#include <functional>

#include "defines.h"

namespace latinime {

// Forward declarations
class Dictionary;
class SuggestionResults;
class ProximityInfo;

/**
 * Score Fusion Parameters (Yandex-style)
 * Controls how scores from different predictors are combined.
 */
struct FusionParams {
    float dictionaryWeight = 0.35f;    // Base dictionary score weight
    float neuralWeight = 0.30f;        // Neural model (TapModel) weight
    float personalWeight = 0.20f;      // Personalization weight
    float ngramWeight = 0.15f;         // N-gram context weight
    
    // Autocorrect thresholds
    float autocorrectThreshold = 0.75f;  // Min score diff for autocorrect
    float maxRelativeScoreGap = 0.3f;    // Max gap between top-1 and top-2
    
    // Boost factors
    float exactMatchBoost = 1.2f;        // Boost for exact prefix match
    float multiSourceBoost = 0.1f;       // Boost per additional source
    float frequencyBoost = 0.05f;        // Boost per learned usage
};

/**
 * Single suggestion with metadata
 */
struct Suggestion {
    std::vector<int> codePoints;      // Word as code points
    std::string word;                 // Word as UTF-8 string
    float score;                      // Combined score (0.0 - 1.0)
    float neuralScore;                // Neural model score
    float dictScore;                  // Dictionary score
    float personalScore;              // Personalization score
    int probability;                  // Dictionary probability
    int sourceId;                     // Which predictor generated this
    int sourceMask;                   // Bitmask of all sources
    bool isExactMatch;
    bool isAutoCorrection;
    
    Suggestion() : score(0.0f), neuralScore(0.0f), dictScore(0.0f),
                   personalScore(0.0f), probability(0), sourceId(0), 
                   sourceMask(0), isExactMatch(false), isAutoCorrection(false) {}
    
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
    std::string inputWord;            // UTF-8 version
    const int* prevWordCodePoints;
    int prevWordLength;
    std::string prevWord;             // UTF-8 previous word for context
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
 * Predictor Source IDs
 */
enum class PredictorSource : int {
    DICTIONARY = 1,
    NGRAM = 2,
    NEURAL = 4,
    PERSONAL = 8,
    REMOTE = 16
};

/**
 * Base class for all predictors
 */
class Predictor {
public:
    virtual ~Predictor() = default;
    
    virtual void predict(const PredictionInput& input, 
                        int maxResults,
                        std::vector<Suggestion>& outSuggestions) = 0;
    
    virtual int getPriority() const { return 0; }
    virtual int getSourceId() const = 0;
    virtual bool isEnabled() const { return enabled_; }
    virtual void setEnabled(bool enabled) { enabled_ = enabled; }
    virtual const char* getName() const = 0;
    
    // Weight for score fusion
    virtual float getWeight() const { return 1.0f; }
    virtual void setWeight(float w) { weight_ = w; }

protected:
    bool enabled_ = true;
    float weight_ = 1.0f;
};

/**
 * Dictionary-based predictor
 */
class DictionaryPredictor : public Predictor {
public:
    static constexpr int SOURCE_ID = static_cast<int>(PredictorSource::DICTIONARY);
    
    explicit DictionaryPredictor(Dictionary* dict) : mDictionary(dict) {
        weight_ = 0.35f;
    }
    
    void predict(const PredictionInput& input, 
                int maxResults,
                std::vector<Suggestion>& outSuggestions) override;
    
    int getSourceId() const override { return SOURCE_ID; }
    const char* getName() const override { return "DictionaryPredictor"; }
    int getPriority() const override { return 100; }
    float getWeight() const override { return weight_; }
    
private:
    Dictionary* mDictionary;
};

/**
 * N-gram based predictor for contextual suggestions
 */
class NgramPredictor : public Predictor {
public:
    static constexpr int SOURCE_ID = static_cast<int>(PredictorSource::NGRAM);
    
    explicit NgramPredictor(Dictionary* dict) : mDictionary(dict) {
        weight_ = 0.15f;
    }
    
    void predict(const PredictionInput& input, 
                int maxResults,
                std::vector<Suggestion>& outSuggestions) override;
    
    int getSourceId() const override { return SOURCE_ID; }
    const char* getName() const override { return "NgramPredictor"; }
    int getPriority() const override { return 90; }
    float getWeight() const override { return weight_; }
    
private:
    Dictionary* mDictionary;
};

/**
 * Neural predictor interface (for MindSpore/TapModel)
 */
class NeuralPredictor : public Predictor {
public:
    static constexpr int SOURCE_ID = static_cast<int>(PredictorSource::NEURAL);
    
    using ScorerFunc = std::function<std::vector<std::pair<std::string, float>>(
        const std::vector<std::string>& candidates,
        const std::string& context
    )>;
    
    NeuralPredictor() {
        weight_ = 0.30f;
    }
    
    void setScorer(ScorerFunc scorer) { scorer_ = scorer; }
    
    void predict(const PredictionInput& input, 
                int maxResults,
                std::vector<Suggestion>& outSuggestions) override;
    
    int getSourceId() const override { return SOURCE_ID; }
    const char* getName() const override { return "NeuralPredictor"; }
    int getPriority() const override { return 95; }
    float getWeight() const override { return weight_; }
    
private:
    ScorerFunc scorer_;
};

/**
 * Personal dictionary predictor (learned words with bigram context)
 */
class PersonalPredictor : public Predictor {
public:
    static constexpr int SOURCE_ID = static_cast<int>(PredictorSource::PERSONAL);
    
    using LookupFunc = std::function<std::vector<std::pair<std::string, int>>(
        const std::string& prefix,
        const std::string& prevWord,
        int limit
    )>;
    
    PersonalPredictor() {
        weight_ = 0.20f;
    }
    
    void setLookup(LookupFunc lookup) { lookup_ = lookup; }
    
    void predict(const PredictionInput& input, 
                int maxResults,
                std::vector<Suggestion>& outSuggestions) override;
    
    int getSourceId() const override { return SOURCE_ID; }
    const char* getName() const override { return "PersonalPredictor"; }
    int getPriority() const override { return 110; }  // Highest priority
    float getWeight() const override { return weight_; }
    
private:
    LookupFunc lookup_;
};

/**
 * Multi-Predictor: Combines multiple prediction sources (Yandex-style)
 */
class MultiPredictor {
public:
    static constexpr int DEFAULT_MAX_RESULTS = 24;
    static constexpr int MAX_PREDICTORS = 8;
    
    MultiPredictor();
    ~MultiPredictor();
    
    // Predictor management
    void addPredictor(std::unique_ptr<Predictor> predictor);
    void removePredictor(int sourceId);
    void clearPredictors();
    void setPredictorEnabled(int sourceId, bool enabled);
    void setPredictorWeight(int sourceId, float weight);
    
    // Fusion parameters
    void setFusionParams(const FusionParams& params) { fusionParams_ = params; }
    const FusionParams& getFusionParams() const { return fusionParams_; }
    
    // Blacklist management (Yandex-style)
    void addToBlacklist(const std::string& word);
    void removeFromBlacklist(const std::string& word);
    void clearBlacklist();
    void initDefaultBlacklist();  // Initialize with profanity filter
    bool isBlacklisted(const std::string& word) const;
    
    // Autocorrect blocker (Yandex-style)
    void addToAutocorrectBlocker(const std::string& word);
    void removeFromAutocorrectBlocker(const std::string& word);
    void clearAutocorrectBlocker();
    bool isAutocorrectBlocked(const std::string& word) const;
    
    // Main API
    void getSuggestions(const PredictionInput& input,
                       int maxResults,
                       std::vector<Suggestion>& outSuggestions);
    
    // Check if should autocorrect (Yandex-style decision)
    bool shouldAutocorrect(const std::string& input, 
                          const std::vector<Suggestion>& suggestions) const;
    
    size_t getPredictorCount() const { return mPredictors.size(); }
    
private:
    std::vector<std::unique_ptr<Predictor>> mPredictors;
    FusionParams fusionParams_;
    
    // Yandex-style filters
    std::unordered_set<std::string> blacklist_;
    std::unordered_set<std::string> autocorrectBlocker_;
    
    void mergeSuggestions(std::vector<std::vector<Suggestion>>& allResults,
                         int maxResults,
                         std::vector<Suggestion>& outMerged);
    
    static bool isDuplicate(const Suggestion& a, const Suggestion& b);
    
    float calculateFusedScore(const std::vector<const Suggestion*>& duplicates);
    
    void applyFilters(std::vector<Suggestion>& suggestions);
    
    // Convert code points to UTF-8 string
    static std::string codePointsToString(const std::vector<int>& codePoints);
};

} // namespace latinime

#endif // HOSKEY_MULTI_PREDICTOR_H
