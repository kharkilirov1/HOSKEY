/**
 * HOSKEY Keyboard - Candidate Ranking Implementation
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#include "ranking.h"
#include <algorithm>
#include <unordered_map>
#include <cstring>
#include <cctype>

namespace keyboard {
namespace core {

Ranker::Ranker() = default;

void Ranker::setWeights(float neural, float ngram, float trie, float personal) {
    config_.neuralWeight = neural;
    config_.ngramWeight = ngram;
    config_.trieWeight = trie;
    config_.personalWeight = personal;
}

std::vector<RankCandidate> Ranker::rank(
    const std::vector<std::pair<std::string, float>>& trieCandidates,
    const std::vector<std::pair<std::string, float>>& ngramCandidates,
    const nn::NeuralResult& neuralResult,
    const std::vector<std::string>& ruleCandidates,
    const std::string& inputText,
    int maxResults) {

    // Merge all candidates
    auto merged = mergeCandidates(trieCandidates, ngramCandidates,
                                   neuralResult, ruleCandidates);

    // Calculate fused scores
    for (auto& candidate : merged) {
        calculateFusedScore(candidate, inputText);
    }

    // Deduplicate
    deduplicate(merged);

    // Sort by score
    sortByScore(merged);

    // Limit results
    if (static_cast<int>(merged.size()) > maxResults) {
        merged.resize(maxResults);
    }

    return merged;
}

std::vector<RankCandidate> Ranker::rankSimple(
    const std::vector<std::pair<std::string, float>>& trieCandidates,
    const std::vector<std::pair<std::string, float>>& ngramCandidates,
    const std::string& inputText,
    int maxResults) {

    nn::NeuralResult emptyNeural;
    std::vector<std::string> emptyRules;

    return rank(trieCandidates, ngramCandidates, emptyNeural, emptyRules,
                inputText, maxResults);
}

std::vector<RankCandidate> Ranker::mergeCandidates(
    const std::vector<std::pair<std::string, float>>& trieCandidates,
    const std::vector<std::pair<std::string, float>>& ngramCandidates,
    const nn::NeuralResult& neuralResult,
    const std::vector<std::string>& ruleCandidates) {

    std::unordered_map<std::string, RankCandidate> candidateMap;

    // Add trie candidates
    for (const auto& [word, score] : trieCandidates) {
        std::string normalized = normalizeForComparison(word);
        auto& candidate = candidateMap[normalized];
        candidate.text = word;
        candidate.trieScore = score;
        candidate.sourceMask |= KB_SOURCE_TRIE;
    }

    // Add ngram candidates
    for (const auto& [word, score] : ngramCandidates) {
        std::string normalized = normalizeForComparison(word);
        auto& candidate = candidateMap[normalized];
        if (candidate.text.empty()) {
            candidate.text = word;
        }
        candidate.ngramScore = score;
        candidate.sourceMask |= KB_SOURCE_NGRAM;
    }

    // Add neural scores
    for (const auto& nc : neuralResult.candidates) {
        std::string normalized = normalizeForComparison(nc.text);
        auto& candidate = candidateMap[normalized];
        if (candidate.text.empty()) {
            candidate.text = nc.text;
        }
        candidate.neuralScore = nc.score;
        candidate.confidence = nc.confidence;
        candidate.sourceMask |= KB_SOURCE_NEURAL;
    }

    // Add rule candidates
    float ruleScore = 0.5f;
    for (const auto& word : ruleCandidates) {
        std::string normalized = normalizeForComparison(word);
        auto& candidate = candidateMap[normalized];
        if (candidate.text.empty()) {
            candidate.text = word;
        }
        candidate.ruleScore = ruleScore;
        candidate.sourceMask |= KB_SOURCE_RULE;
        ruleScore *= 0.9f;
    }

    // Convert to vector
    std::vector<RankCandidate> result;
    result.reserve(candidateMap.size());
    for (auto& [key, candidate] : candidateMap) {
        result.push_back(std::move(candidate));
    }

    return result;
}

void Ranker::calculateFusedScore(RankCandidate& candidate,
                                  const std::string& inputText) {
    float fusedScore = 0.0f;
    float totalWeight = 0.0f;

    // Neural component
    if (candidate.neuralScore > 0.0f) {
        fusedScore += candidate.neuralScore * config_.neuralWeight;
        totalWeight += config_.neuralWeight;
    }

    // N-gram component
    if (candidate.ngramScore > 0.0f) {
        fusedScore += candidate.ngramScore * config_.ngramWeight;
        totalWeight += config_.ngramWeight;
    }

    // Trie component
    if (candidate.trieScore > 0.0f) {
        fusedScore += candidate.trieScore * config_.trieWeight;
        totalWeight += config_.trieWeight;
    }

    // Rule component
    if (candidate.ruleScore > 0.0f) {
        fusedScore += candidate.ruleScore * config_.ruleWeight;
        totalWeight += config_.ruleWeight;
    }

    // Personal component
    if (candidate.personalScore > 0.0f) {
        fusedScore += candidate.personalScore * config_.personalWeight;
        totalWeight += config_.personalWeight;
    }

    // Normalize by total weight
    if (totalWeight > 0.0f) {
        fusedScore /= totalWeight;
    }

    // Apply boosts
    candidate.isExactMatch = isExactMatch(candidate.text, inputText);
    if (candidate.isExactMatch) {
        fusedScore *= config_.exactMatchBoost;
    }

    // Multi-source boost
    int sourceCount = countSources(candidate.sourceMask);
    if (sourceCount > 1) {
        fusedScore *= (1.0f + config_.multiSourceBoost * (sourceCount - 1));
    }

    // Autocorrect penalty (if not exact match and is autocorrect)
    if (candidate.isAutocorrect && !candidate.isExactMatch) {
        fusedScore *= config_.autocorrectPenalty;
    }

    candidate.fusedScore = std::min(1.0f, std::max(0.0f, fusedScore));

    // Set confidence
    if (candidate.confidence <= 0.0f) {
        candidate.confidence = fusedScore;
    }
}

void Ranker::deduplicate(std::vector<RankCandidate>& candidates) {
    // Already deduplicated during merge, but check for case variations
    std::unordered_map<std::string, size_t> seen;

    for (size_t i = 0; i < candidates.size(); ++i) {
        std::string normalized = normalizeForComparison(candidates[i].text);

        auto it = seen.find(normalized);
        if (it != seen.end()) {
            // Keep higher scored one
            if (candidates[i].fusedScore > candidates[it->second].fusedScore) {
                candidates[it->second].isDuplicate = true;
                seen[normalized] = i;
            } else {
                candidates[i].isDuplicate = true;
            }
        } else {
            seen[normalized] = i;
        }
    }

    // Remove duplicates
    candidates.erase(
        std::remove_if(candidates.begin(), candidates.end(),
            [](const RankCandidate& c) { return c.isDuplicate; }),
        candidates.end());
}

void Ranker::sortByScore(std::vector<RankCandidate>& candidates) {
    std::sort(candidates.begin(), candidates.end(),
        [](const RankCandidate& a, const RankCandidate& b) {
            return a.fusedScore > b.fusedScore;
        });
}

bool Ranker::shouldAutocorrect(const std::vector<RankCandidate>& candidates,
                                const std::string& inputText) const {
    if (candidates.empty()) {
        return false;
    }

    const auto& top = candidates[0];

    // Don't autocorrect if top candidate is exact match
    if (top.isExactMatch) {
        return false;
    }

    // Need minimum score
    if (top.fusedScore < config_.autocorrectMinScore) {
        return false;
    }

    // Check if input is in candidates (user typed a valid word)
    for (const auto& c : candidates) {
        if (isExactMatch(c.text, inputText)) {
            // Input is valid, check score gap
            if (top.fusedScore - c.fusedScore < config_.autocorrectScoreGap) {
                return false;
            }
            break;
        }
    }

    return true;
}

void Ranker::toOutputCandidates(const std::vector<RankCandidate>& ranked,
                                 KBPredictResult& result) {
    result.candidateCount = static_cast<uint32_t>(ranked.size());
    result.candidates = new KBCandidate[result.candidateCount];

    result.neuralCandidates = 0;
    result.ngramCandidates = 0;
    result.trieCandidates = 0;
    result.ruleCandidates = 0;

    for (size_t i = 0; i < ranked.size(); ++i) {
        const auto& rc = ranked[i];
        auto& out = result.candidates[i];

        out.text = strdup(rc.text.c_str());
        out.score = rc.fusedScore;
        out.confidence = rc.confidence;
        out.sourceMask = rc.sourceMask;
        out.isExactMatch = rc.isExactMatch ? 1 : 0;
        out.isAutocorrect = rc.isAutocorrect ? 1 : 0;

        // Determine primary source
        if (rc.sourceMask & KB_SOURCE_NEURAL) {
            out.source = KB_SOURCE_NEURAL;
            result.neuralCandidates++;
        } else if (rc.sourceMask & KB_SOURCE_NGRAM) {
            out.source = KB_SOURCE_NGRAM;
            result.ngramCandidates++;
        } else if (rc.sourceMask & KB_SOURCE_TRIE) {
            out.source = KB_SOURCE_TRIE;
            result.trieCandidates++;
        } else {
            out.source = KB_SOURCE_RULE;
            result.ruleCandidates++;
        }
    }
}

bool Ranker::isExactMatch(const std::string& candidate, const std::string& input) {
    return normalizeForComparison(candidate) == normalizeForComparison(input);
}

std::string Ranker::normalizeForComparison(const std::string& text) {
    std::string result;
    result.reserve(text.size());

    for (unsigned char c : text) {
        if (c < 0x80) {
            result.push_back(static_cast<char>(std::tolower(c)));
        } else {
            result.push_back(c);
        }
    }

    return result;
}

int Ranker::countSources(uint32_t sourceMask) {
    int count = 0;
    while (sourceMask) {
        count += sourceMask & 1;
        sourceMask >>= 1;
    }
    return count;
}

} // namespace core
} // namespace keyboard
