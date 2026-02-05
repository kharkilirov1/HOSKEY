/**
 * HOSKEY Keyboard - Candidate Ranking
 *
 * Score fusion and ranking for prediction candidates.
 * Combines scores from multiple sources (neural, n-gram, trie, rule).
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#ifndef KEYBOARD_NATIVE_RANKING_H
#define KEYBOARD_NATIVE_RANKING_H

#include "../../include/types.h"
#include "../nn/i_neural_engine.h"
#include <string>
#include <vector>

namespace keyboard {
namespace core {

/**
 * Internal candidate representation during ranking
 */
struct RankCandidate {
    std::string text;

    // Scores from each source (0.0 = not present)
    float neuralScore = 0.0f;
    float ngramScore = 0.0f;
    float trieScore = 0.0f;
    float ruleScore = 0.0f;
    float personalScore = 0.0f;

    // Combined scores
    float fusedScore = 0.0f;
    float confidence = 0.0f;

    // Source tracking
    uint32_t sourceMask = 0;

    // Flags
    bool isExactMatch = false;
    bool isAutocorrect = false;

    // For deduplication
    bool isDuplicate = false;
};

/**
 * Ranking configuration
 */
struct RankingConfig {
    // Source weights (should sum to ~1.0)
    float neuralWeight = 0.30f;
    float ngramWeight = 0.15f;
    float trieWeight = 0.35f;
    float ruleWeight = 0.05f;
    float personalWeight = 0.15f;

    // Boost factors
    float exactMatchBoost = 1.2f;       // Boost for exact prefix match
    float multiSourceBoost = 0.05f;     // Boost per additional source
    float autocorrectPenalty = 0.9f;    // Penalty for autocorrect candidates

    // Autocorrect thresholds
    float autocorrectMinScore = 0.75f;  // Min score for autocorrect
    float autocorrectScoreGap = 0.15f;  // Min gap above typed word

    // Limits
    int maxCandidates = 24;
};

/**
 * Candidate ranker
 */
class Ranker {
public:
    Ranker();

    /**
     * Set ranking configuration
     */
    void setConfig(const RankingConfig& config) { config_ = config; }

    /**
     * Set source weights
     */
    void setWeights(float neural, float ngram, float trie, float personal);

    /**
     * Rank and merge candidates from multiple sources
     *
     * @param trieCandidates Candidates from trie with scores
     * @param ngramCandidates Candidates from n-gram with scores
     * @param neuralResult Neural scoring result
     * @param ruleCandidates Rule-based candidates
     * @param inputText Original input (for exact match detection)
     * @param maxResults Maximum results to return
     * @return Ranked candidates
     */
    std::vector<RankCandidate> rank(
        const std::vector<std::pair<std::string, float>>& trieCandidates,
        const std::vector<std::pair<std::string, float>>& ngramCandidates,
        const nn::NeuralResult& neuralResult,
        const std::vector<std::string>& ruleCandidates,
        const std::string& inputText,
        int maxResults);

    /**
     * Simple ranking without neural (for fallback)
     */
    std::vector<RankCandidate> rankSimple(
        const std::vector<std::pair<std::string, float>>& trieCandidates,
        const std::vector<std::pair<std::string, float>>& ngramCandidates,
        const std::string& inputText,
        int maxResults);

    /**
     * Decide if should autocorrect
     */
    bool shouldAutocorrect(const std::vector<RankCandidate>& candidates,
                           const std::string& inputText) const;

    /**
     * Convert ranked candidates to output format
     */
    static void toOutputCandidates(const std::vector<RankCandidate>& ranked,
                                   KBPredictResult& result);

private:
    RankingConfig config_;

    /**
     * Merge all candidates into unified list
     */
    std::vector<RankCandidate> mergeCandidates(
        const std::vector<std::pair<std::string, float>>& trieCandidates,
        const std::vector<std::pair<std::string, float>>& ngramCandidates,
        const nn::NeuralResult& neuralResult,
        const std::vector<std::string>& ruleCandidates);

    /**
     * Calculate fused score for candidate
     */
    void calculateFusedScore(RankCandidate& candidate, const std::string& inputText);

    /**
     * Remove duplicate candidates (keep highest scored)
     */
    void deduplicate(std::vector<RankCandidate>& candidates);

    /**
     * Sort candidates by fused score
     */
    static void sortByScore(std::vector<RankCandidate>& candidates);

    /**
     * Check if candidate is exact match for input
     */
    static bool isExactMatch(const std::string& candidate, const std::string& input);

    /**
     * Normalize text for comparison
     */
    static std::string normalizeForComparison(const std::string& text);

    /**
     * Count sources contributing to candidate
     */
    static int countSources(uint32_t sourceMask);
};

} // namespace core
} // namespace keyboard

#endif // KEYBOARD_NATIVE_RANKING_H
