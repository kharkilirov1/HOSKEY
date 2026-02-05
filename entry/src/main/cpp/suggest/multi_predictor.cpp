/*
 * Copyright (c) 2024 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 * 
 * Multi-Predictor with Yandex-style Score Fusion
 */

#include "multi_predictor.h"
#include "suggest/core/dictionary/dictionary.h"
#include "suggest/core/result/suggestion_results.h"
#include <cmath>
#include <codecvt>
#include <locale>

namespace latinime {

// ==================== Utility Functions ====================

std::string MultiPredictor::codePointsToString(const std::vector<int>& codePoints) {
    std::string result;
    result.reserve(codePoints.size() * 2);  // Estimate for UTF-8
    
    for (int cp : codePoints) {
        if (cp < 0x80) {
            result += static_cast<char>(cp);
        } else if (cp < 0x800) {
            result += static_cast<char>(0xC0 | (cp >> 6));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            result += static_cast<char>(0xE0 | (cp >> 12));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            result += static_cast<char>(0xF0 | (cp >> 18));
            result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return result;
}

// ==================== MultiPredictor ====================

MultiPredictor::MultiPredictor() {
    mPredictors.reserve(MAX_PREDICTORS);
    initDefaultBlacklist();
}

void MultiPredictor::initDefaultBlacklist() {
    // Russian profanity filter (common swear word roots)
    // These will not appear in suggestions
    static const char* profanityWords[] = {
        // Core obscenities
        "хуй", "хуя", "хуе", "хуи", "хую", "хуём", "хуёв",
        "пизд", "пизда", "пиздец", "пизды", "пизду",
        "блядь", "бляди", "блядей", "блядям", "блядина",
        "ебать", "ебал", "ебан", "ебу", "ебёт", "ебут", "ёб", "ёбан",
        "сука", "суки", "сукам", "сучка", "сучки",
        // Derivatives
        "мудак", "мудаки", "мудила", "мудило",
        "пидор", "пидар", "пидорас", "пидары",
        "залупа", "залупы",
        "жопа", "жопу", "жопы", "жопой",
        "говно", "говна", "говну", "говном",
        "дерьмо", "дерьма",
        "хер", "херня", "херов",
        "срать", "срал", "срёт",
        "ссать", "ссал", "ссыт",
        "шлюха", "шлюхи",
        "бля", "блять", "блин",  // Common expletives
        "нахуй", "нахуя", "нахер",
        "похуй", "похер",
        "охуеть", "охуел", "охуенн",
        "заебал", "заебись", "заёб",
        "ебанут", "ебанат", "ебанько",
        "пиздец", "пиздат", "пиздюк",
        "мудозвон", "долбоёб", "долбоеб",
        "уёб", "уебок", "уебан",
        // Compound forms
        "хуесос", "хуеплёт", "хуйня",
        "пиздобол", "пиздострадал",
        "ёбаный", "ебаный", "ёбанный"
    };
    
    for (const char* word : profanityWords) {
        blacklist_.insert(word);
    }
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

void MultiPredictor::setPredictorEnabled(int sourceId, bool enabled) {
    for (auto& predictor : mPredictors) {
        if (predictor->getSourceId() == sourceId) {
            predictor->setEnabled(enabled);
            break;
        }
    }
}

void MultiPredictor::setPredictorWeight(int sourceId, float weight) {
    for (auto& predictor : mPredictors) {
        if (predictor->getSourceId() == sourceId) {
            predictor->setWeight(weight);
            break;
        }
    }
}

// ==================== Blacklist/Blocker ====================

void MultiPredictor::addToBlacklist(const std::string& word) {
    blacklist_.insert(word);
}

void MultiPredictor::removeFromBlacklist(const std::string& word) {
    blacklist_.erase(word);
}

void MultiPredictor::clearBlacklist() {
    blacklist_.clear();
}

bool MultiPredictor::isBlacklisted(const std::string& word) const {
    return blacklist_.find(word) != blacklist_.end();
}

void MultiPredictor::addToAutocorrectBlocker(const std::string& word) {
    autocorrectBlocker_.insert(word);
}

void MultiPredictor::removeFromAutocorrectBlocker(const std::string& word) {
    autocorrectBlocker_.erase(word);
}

void MultiPredictor::clearAutocorrectBlocker() {
    autocorrectBlocker_.clear();
}

bool MultiPredictor::isAutocorrectBlocked(const std::string& word) const {
    return autocorrectBlocker_.find(word) != autocorrectBlocker_.end();
}

// ==================== Main Prediction Logic ====================

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
        predictorResults.reserve(maxResults * 2);  // Get more for merging
        
        predictor->predict(input, maxResults * 2, predictorResults);
        
        // Tag each result with source
        for (auto& s : predictorResults) {
            s.sourceId = predictor->getSourceId();
            s.sourceMask = predictor->getSourceId();
            
            // Ensure word string is set
            if (s.word.empty() && !s.codePoints.empty()) {
                s.word = codePointsToString(s.codePoints);
            }
        }
        
        if (!predictorResults.empty()) {
            allResults.push_back(std::move(predictorResults));
        }
    }
    
    // Merge all results with score fusion
    mergeSuggestions(allResults, maxResults, outSuggestions);
    
    // Apply blacklist filter
    applyFilters(outSuggestions);
}

void MultiPredictor::applyFilters(std::vector<Suggestion>& suggestions) {
    if (blacklist_.empty()) {
        return;
    }
    
    suggestions.erase(
        std::remove_if(suggestions.begin(), suggestions.end(),
            [this](const Suggestion& s) {
                return isBlacklisted(s.word);
            }),
        suggestions.end());
}

bool MultiPredictor::shouldAutocorrect(const std::string& input, 
                                       const std::vector<Suggestion>& suggestions) const {
    if (suggestions.empty()) {
        return false;
    }
    
    // Check autocorrect blocker
    if (isAutocorrectBlocked(input)) {
        return false;
    }
    
    const auto& top = suggestions[0];
    
    // Don't autocorrect to the same word
    if (top.word == input) {
        return false;
    }
    
    // Check score threshold
    if (top.score < fusionParams_.autocorrectThreshold) {
        return false;
    }
    
    // Check score gap (Yandex-style)
    if (suggestions.size() > 1) {
        float gap = top.score - suggestions[1].score;
        if (gap < fusionParams_.maxRelativeScoreGap) {
            return false;  // Not confident enough
        }
    }
    
    // Check if top result is from multiple sources (higher confidence)
    int sourceCount = __builtin_popcount(top.sourceMask);
    if (sourceCount >= 2) {
        return true;  // Multiple predictors agree
    }
    
    return top.score >= fusionParams_.autocorrectThreshold * 1.1f;
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
    
    // Group duplicates by word
    std::unordered_map<std::string, std::vector<const Suggestion*>> groups;
    
    for (const auto& resultSet : allResults) {
        for (const auto& suggestion : resultSet) {
            groups[suggestion.word].push_back(&suggestion);
        }
    }
    
    // Create merged suggestions with score fusion
    for (const auto& [word, group] : groups) {
        Suggestion merged = *group[0];  // Copy first
        
        // Combine source masks
        for (const auto* s : group) {
            merged.sourceMask |= s->sourceId;
        }
        
        // Apply Yandex-style score fusion
        merged.score = calculateFusedScore(group);
        
        // Boost if appeared in multiple predictors
        int sourceCount = static_cast<int>(group.size());
        if (sourceCount > 1) {
            merged.score *= (1.0f + fusionParams_.multiSourceBoost * (sourceCount - 1));
        }
        
        // Exact match boost
        if (merged.isExactMatch) {
            merged.score *= fusionParams_.exactMatchBoost;
        }
        
        // Clamp to [0, 1]
        merged.score = std::min(1.0f, std::max(0.0f, merged.score));
        
        outMerged.push_back(std::move(merged));
    }
    
    // Sort by score (descending)
    std::sort(outMerged.begin(), outMerged.end());
    
    // Limit results
    if (outMerged.size() > static_cast<size_t>(maxResults)) {
        outMerged.resize(maxResults);
    }
}

bool MultiPredictor::isDuplicate(const Suggestion& a, const Suggestion& b) {
    return a.word == b.word;
}

float MultiPredictor::calculateFusedScore(const std::vector<const Suggestion*>& duplicates) {
    if (duplicates.empty()) {
        return 0.0f;
    }
    
    /*
     * Yandex-style Score Fusion:
     * 
     * finalScore = dictWeight * dictScore
     *            + neuralWeight * neuralScore  
     *            + personalWeight * personalScore
     *            + ngramWeight * ngramScore
     */
    
    float dictScore = 0.0f;
    float neuralScore = 0.0f;
    float personalScore = 0.0f;
    float ngramScore = 0.0f;
    
    float dictWeight = 0.0f;
    float neuralWeight = 0.0f;
    float personalWeight = 0.0f;
    float ngramWeight = 0.0f;
    
    for (const auto* s : duplicates) {
        switch (s->sourceId) {
            case static_cast<int>(PredictorSource::DICTIONARY):
                dictScore = std::max(dictScore, s->score);
                dictWeight = fusionParams_.dictionaryWeight;
                break;
            case static_cast<int>(PredictorSource::NEURAL):
                neuralScore = std::max(neuralScore, s->neuralScore > 0 ? s->neuralScore : s->score);
                neuralWeight = fusionParams_.neuralWeight;
                break;
            case static_cast<int>(PredictorSource::PERSONAL):
                personalScore = std::max(personalScore, s->personalScore > 0 ? s->personalScore : s->score);
                personalWeight = fusionParams_.personalWeight;
                break;
            case static_cast<int>(PredictorSource::NGRAM):
                ngramScore = std::max(ngramScore, s->score);
                ngramWeight = fusionParams_.ngramWeight;
                break;
        }
    }
    
    float totalWeight = dictWeight + neuralWeight + personalWeight + ngramWeight;
    if (totalWeight < 0.01f) {
        // Fallback: use simple average
        float sum = 0.0f;
        for (const auto* s : duplicates) {
            sum += s->score;
        }
        return sum / duplicates.size();
    }
    
    // Weighted sum
    float fusedScore = (dictWeight * dictScore +
                        neuralWeight * neuralScore +
                        personalWeight * personalScore +
                        ngramWeight * ngramScore) / totalWeight;
    
    return fusedScore;
}

// ==================== DictionaryPredictor ====================

void DictionaryPredictor::predict(const PredictionInput& input, 
                                  int maxResults,
                                  std::vector<Suggestion>& outSuggestions) {
    if (!mDictionary || !input.inputCodePoints || input.inputLength <= 0) {
        return;
    }
    
    CodePointArrayView inputView(input.inputCodePoints, input.inputLength);
    int probability = mDictionary->getProbability(inputView);
    
    if (probability > 0) {
        Suggestion s;
        s.codePoints.assign(input.inputCodePoints, 
                           input.inputCodePoints + input.inputLength);
        s.probability = probability;
        s.score = static_cast<float>(probability) / MAX_PROBABILITY;
        s.dictScore = s.score;
        s.sourceId = SOURCE_ID;
        s.isExactMatch = true;
        outSuggestions.push_back(std::move(s));
    }
}

// ==================== NgramPredictor ====================

void NgramPredictor::predict(const PredictionInput& input, 
                            int maxResults,
                            std::vector<Suggestion>& outSuggestions) {
    if (!mDictionary || !enabled_) {
        return;
    }
    
    if (!input.prevWordCodePoints || input.prevWordLength <= 0) {
        return;
    }
    
    // Placeholder - full implementation would query bigram/trigram data
}

// ==================== NeuralPredictor ====================

void NeuralPredictor::predict(const PredictionInput& input, 
                             int maxResults,
                             std::vector<Suggestion>& outSuggestions) {
    if (!enabled_ || !scorer_) {
        return;
    }
    
    // This predictor re-scores existing candidates from other predictors
    // It needs candidates to be passed in separately
    // For now, it's a placeholder that will be connected to MindSporeScorer
}

// ==================== PersonalPredictor ====================

void PersonalPredictor::predict(const PredictionInput& input, 
                               int maxResults,
                               std::vector<Suggestion>& outSuggestions) {
    if (!enabled_ || !lookup_) {
        return;
    }
    
    auto results = lookup_(input.inputWord, input.prevWord, maxResults);
    
    for (const auto& [word, boost] : results) {
        Suggestion s;
        s.word = word;
        s.score = std::min(1.0f, 0.5f + boost * 0.01f);  // Convert boost to score
        s.personalScore = s.score;
        s.sourceId = SOURCE_ID;
        outSuggestions.push_back(std::move(s));
    }
}

} // namespace latinime
