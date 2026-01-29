/*
 * Copyright (c) 2024 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#include "multi_predictor.h"
#include "suggest/core/dictionary/dictionary.h"
#include "suggest/core/result/suggestion_results.h"

namespace latinime {

// ==================== MultiPredictor ====================

MultiPredictor::MultiPredictor() {
    mPredictors.reserve(MAX_PREDICTORS);
}

MultiPredictor::~MultiPredictor() = default;

void MultiPredictor::addPredictor(std::unique_ptr<Predictor> predictor) {
    if (mPredictors.size() >= MAX_PREDICTORS) {
        return;
    }
    mPredictors.push_back(std::move(predictor));
    
    // Sort by priority (higher first)
    std::sort(mPredictors.begin(), mPredictors.end(),
        [](const std::unique_ptr<Predictor>& a, const std::unique_ptr<Predictor>& b) {
            return a->getPriority() > b->getPriority();
        });
}

void MultiPredictor::removePredictor(int sourceId) {
    mPredictors.erase(
        std::remove_if(mPredictors.begin(), mPredictors.end(),
            [sourceId](const std::unique_ptr<Predictor>& p) {
                return p->getSourceId() == sourceId;
            }),
        mPredictors.end());
}

void MultiPredictor::clearPredictors() {
    mPredictors.clear();
}

void MultiPredictor::getSuggestions(const PredictionInput& input,
                                    int maxResults,
                                    std::vector<Suggestion>& outSuggestions) {
    outSuggestions.clear();
    
    if (mPredictors.empty() || maxResults <= 0) {
        return;
    }
    
    // Collect results from all enabled predictors
    std::vector<std::vector<Suggestion>> allResults;
    allResults.reserve(mPredictors.size());
    
    for (auto& predictor : mPredictors) {
        if (!predictor->isEnabled()) {
            continue;
        }
        
        std::vector<Suggestion> predictorResults;
        predictorResults.reserve(maxResults);
        
        predictor->predict(input, maxResults, predictorResults);
        
        if (!predictorResults.empty()) {
            allResults.push_back(std::move(predictorResults));
        }
    }
    
    // Merge all results
    mergeSuggestions(allResults, maxResults, outSuggestions);
}

void MultiPredictor::setPredictorEnabled(int sourceId, bool enabled) {
    for (auto& predictor : mPredictors) {
        if (predictor->getSourceId() == sourceId) {
            // Note: Would need to add setEnabled to base class or cast
            // For now, predictors manage their own enabled state
            break;
        }
    }
}

void MultiPredictor::mergeSuggestions(std::vector<std::vector<Suggestion>>& allResults,
                                      int maxResults,
                                      std::vector<Suggestion>& outMerged) {
    if (allResults.empty()) {
        return;
    }
    
    // If only one source, just return its results
    if (allResults.size() == 1) {
        outMerged = std::move(allResults[0]);
        if (outMerged.size() > static_cast<size_t>(maxResults)) {
            outMerged.resize(maxResults);
        }
        return;
    }
    
    // Group duplicates by code points
    std::vector<std::vector<const Suggestion*>> groups;
    std::vector<bool> processed;
    
    // Flatten all results with tracking
    std::vector<const Suggestion*> flatList;
    for (const auto& resultSet : allResults) {
        for (const auto& suggestion : resultSet) {
            flatList.push_back(&suggestion);
        }
    }
    processed.resize(flatList.size(), false);
    
    // Find duplicates and group them
    for (size_t i = 0; i < flatList.size(); ++i) {
        if (processed[i]) continue;
        
        std::vector<const Suggestion*> group;
        group.push_back(flatList[i]);
        processed[i] = true;
        
        for (size_t j = i + 1; j < flatList.size(); ++j) {
            if (processed[j]) continue;
            
            if (isDuplicate(*flatList[i], *flatList[j])) {
                group.push_back(flatList[j]);
                processed[j] = true;
            }
        }
        
        groups.push_back(std::move(group));
    }
    
    // Create merged suggestions
    for (const auto& group : groups) {
        Suggestion merged = *group[0]; // Copy first
        merged.score = calculateMergedScore(group);
        
        // Boost if appeared in multiple predictors
        if (group.size() > 1) {
            merged.score *= (1.0f + 0.1f * (group.size() - 1));
        }
        
        outMerged.push_back(std::move(merged));
    }
    
    // Sort by score
    std::sort(outMerged.begin(), outMerged.end());
    
    // Limit results
    if (outMerged.size() > static_cast<size_t>(maxResults)) {
        outMerged.resize(maxResults);
    }
}

bool MultiPredictor::isDuplicate(const Suggestion& a, const Suggestion& b) {
    if (a.codePoints.size() != b.codePoints.size()) {
        return false;
    }
    
    for (size_t i = 0; i < a.codePoints.size(); ++i) {
        if (a.codePoints[i] != b.codePoints[i]) {
            return false;
        }
    }
    
    return true;
}

float MultiPredictor::calculateMergedScore(const std::vector<const Suggestion*>& duplicates) {
    if (duplicates.empty()) {
        return 0.0f;
    }
    
    // Use weighted average based on predictor priority
    float totalScore = 0.0f;
    float totalWeight = 0.0f;
    
    for (const auto* suggestion : duplicates) {
        // Use source ID as weight approximation (can be improved)
        float weight = 1.0f;
        totalScore += suggestion->score * weight;
        totalWeight += weight;
    }
    
    return totalWeight > 0.0f ? totalScore / totalWeight : 0.0f;
}

// ==================== DictionaryPredictor ====================

void DictionaryPredictor::predict(const PredictionInput& input, 
                                  int maxResults,
                                  std::vector<Suggestion>& outSuggestions) {
    if (!mDictionary || !input.inputCodePoints || input.inputLength <= 0) {
        return;
    }
    
    // Use dictionary's getProbability for basic scoring
    // This is a simplified implementation - full implementation would use
    // the complete suggestion pipeline
    
    CodePointArrayView inputView(input.inputCodePoints, input.inputLength);
    int probability = mDictionary->getProbability(inputView);
    
    if (probability > 0) {
        Suggestion s;
        s.codePoints.assign(input.inputCodePoints, 
                           input.inputCodePoints + input.inputLength);
        s.probability = probability;
        s.score = static_cast<float>(probability) / MAX_PROBABILITY;
        s.sourceId = SOURCE_ID;
        s.isExactMatch = true;
        outSuggestions.push_back(std::move(s));
    }
}

// ==================== NgramPredictor ====================

void NgramPredictor::predict(const PredictionInput& input, 
                            int maxResults,
                            std::vector<Suggestion>& outSuggestions) {
    if (!mDictionary || !mEnabled) {
        return;
    }
    
    // N-gram prediction requires previous word context
    if (!input.prevWordCodePoints || input.prevWordLength <= 0) {
        return;
    }
    
    // Get n-gram predictions using dictionary's bigram capabilities
    // This is a placeholder - full implementation would query the bigram/trigram data
    
    // For now, just indicate that n-gram predictor was consulted
    // Real implementation would iterate through ngram candidates
}

} // namespace latinime
