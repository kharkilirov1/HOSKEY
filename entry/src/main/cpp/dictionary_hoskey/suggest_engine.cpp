/**
 * Suggest Engine implementation
 * Weighted Levenshtein with proximity awareness
 */

#include "suggest_engine.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace hoskey {

// QWERTY keyboard proximity map
static const std::unordered_map<char, std::unordered_set<char>> kProximityMap = {
    {'q', {'w', 'a', 's'}},
    {'w', {'q', 'e', 'a', 's', 'd'}},
    {'e', {'w', 'r', 's', 'd', 'f'}},
    {'r', {'e', 't', 'd', 'f', 'g'}},
    {'t', {'r', 'y', 'f', 'g', 'h'}},
    {'y', {'t', 'u', 'g', 'h', 'j'}},
    {'u', {'y', 'i', 'h', 'j', 'k'}},
    {'i', {'u', 'o', 'j', 'k', 'l'}},
    {'o', {'i', 'p', 'k', 'l'}},
    {'p', {'o', 'l'}},
    {'a', {'q', 'w', 's', 'z', 'x'}},
    {'s', {'q', 'w', 'e', 'a', 'd', 'z', 'x', 'c'}},
    {'d', {'w', 'e', 'r', 's', 'f', 'x', 'c', 'v'}},
    {'f', {'e', 'r', 't', 'd', 'g', 'c', 'v', 'b'}},
    {'g', {'r', 't', 'y', 'f', 'h', 'v', 'b', 'n'}},
    {'h', {'t', 'y', 'u', 'g', 'j', 'b', 'n', 'm'}},
    {'j', {'y', 'u', 'i', 'h', 'k', 'n', 'm'}},
    {'k', {'u', 'i', 'o', 'j', 'l', 'm'}},
    {'l', {'i', 'o', 'p', 'k'}},
    {'z', {'a', 's', 'x'}},
    {'x', {'a', 's', 'd', 'z', 'c'}},
    {'c', {'s', 'd', 'f', 'x', 'v'}},
    {'v', {'d', 'f', 'g', 'c', 'b'}},
    {'b', {'f', 'g', 'h', 'v', 'n'}},
    {'n', {'g', 'h', 'j', 'b', 'm'}},
    {'m', {'h', 'j', 'k', 'n'}}
};

bool ProximityInfo::areProximate(char a, char b) const {
    auto it = kProximityMap.find(a);
    if (it != kProximityMap.end()) {
        return it->second.count(b) > 0;
    }
    return false;
}

SuggestEngine::SuggestEngine(Trie* trie) : trie_(trie), proximityInfo_(nullptr) {}

SuggestEngine::~SuggestEngine() = default;

void SuggestEngine::setProximityInfo(ProximityInfo* proximityInfo) {
    proximityInfo_ = proximityInfo;
}

std::vector<SuggestResult> SuggestEngine::getSuggestions(const std::string& input, int limit) {
    std::vector<SuggestResult> results;

    if (!trie_ || input.empty()) {
        return results;
    }

    // Step 1: Get exact prefix matches (fast path)
    auto prefixMatches = trie_->findByPrefix(input, limit * 2);

    for (const auto& entry : prefixMatches) {
        EditResult editResult;
        editResult.distance = 0;
        editResult.weightedCost = 0.0f;
        editResult.primaryError = ErrorType::NOT_AN_ERROR;

        float score = calculateFinalScore(entry, editResult, input.length(), true);
        results.emplace_back(entry.word, score, ErrorType::NOT_AN_ERROR, 0);
    }

    // Step 2: If not enough results, search with corrections
    if (results.size() < static_cast<size_t>(limit) && input.length() >= 2) {
        // Get candidates by first letter (for correction search)
        char firstChar = input[0];
        auto candidates = trie_->getWordsByFirstLetter(firstChar, 500);

        // Also check nearby first letters if proximity info available
        if (proximityInfo_) {
            // TODO: Add nearby first letter candidates
        }

        int maxEditDistance = std::min(2, static_cast<int>(input.length()) / 3 + 1);

        for (const auto& candidate : candidates) {
            // Skip if already in results
            bool alreadyAdded = false;
            for (const auto& r : results) {
                if (r.word == candidate) {
                    alreadyAdded = true;
                    break;
                }
            }
            if (alreadyAdded) continue;

            // Skip if length difference too big
            int lengthDiff = std::abs(static_cast<int>(candidate.length()) -
                                     static_cast<int>(input.length()));
            if (lengthDiff > maxEditDistance) continue;

            // Calculate weighted distance
            EditResult editResult = calculateWeightedDistance(input, candidate);

            if (editResult.distance <= maxEditDistance) {
                int frequency = trie_->getFrequency(candidate);
                WordEntry entry(candidate, frequency);

                float score = calculateFinalScore(entry, editResult, input.length(), false);

                // Only add if score is reasonable
                if (score > 0.1f) {
                    results.emplace_back(candidate, score, editResult.primaryError,
                                        editResult.distance);
                }
            }
        }
    }

    // Sort by score (descending)
    std::sort(results.begin(), results.end(),
              [](const SuggestResult& a, const SuggestResult& b) {
                  return a.score > b.score;
              });

    // Limit results
    if (results.size() > static_cast<size_t>(limit)) {
        results.resize(limit);
    }

    return results;
}

SuggestResult SuggestEngine::findAutocorrection(const std::string& word, float threshold) {
    SuggestResult best;

    if (!trie_ || word.empty() || word.length() < 2) {
        return best;
    }

    // Don't autocorrect if word is in dictionary
    if (trie_->contains(word)) {
        return best;
    }

    // Get suggestions
    auto suggestions = getSuggestions(word, 5);

    for (const auto& suggestion : suggestions) {
        // Skip exact match (shouldn't happen, but be safe)
        if (suggestion.word == word) continue;

        // Check if suggestion exceeds threshold
        if (suggestion.score >= threshold && suggestion.score > best.score) {
            best = suggestion;
        }
    }

    return best;
}

bool SuggestEngine::shouldAutocorrect(const std::string& input, const SuggestResult& suggestion) {
    // Don't autocorrect single character
    if (input.length() < 2) return false;

    // Don't autocorrect if input is already valid
    if (trie_ && trie_->contains(input)) return false;

    // Check score threshold
    if (suggestion.score < ScoringParams::AUTOCORRECTION_THRESHOLD) return false;

    // Check plausibility (secondary threshold)
    if (suggestion.score < ScoringParams::PLAUSIBILITY_THRESHOLD &&
        suggestion.editDistance > 1) {
        return false;
    }

    // Don't autocorrect short words with multiple errors
    if (input.length() <= ScoringParams::THRESHOLD_SHORT_WORD_LENGTH &&
        suggestion.editDistance > 1) {
        return false;
    }

    return true;
}

SuggestEngine::EditResult SuggestEngine::calculateWeightedDistance(
        const std::string& input, const std::string& word) {
    EditResult result;
    result.distance = levenshteinDistance(input, word, 3);
    result.primaryError = ErrorType::NOT_AN_ERROR;
    result.weightedCost = 0.0f;

    if (result.distance == 0) {
        return result;
    }

    // Determine primary error type based on analysis
    size_t minLen = std::min(input.length(), word.length());

    // Check for transposition
    if (input.length() == word.length() && result.distance == 2) {
        for (size_t i = 0; i + 1 < minLen; i++) {
            if (input[i] == word[i + 1] && input[i + 1] == word[i]) {
                result.primaryError = ErrorType::TRANSPOSITION_CORRECTION;
                result.weightedCost = ScoringParams::TRANSPOSITION_COST;
                return result;
            }
        }
    }

    // Check for proximity error
    if (result.distance == 1 && proximityInfo_) {
        for (size_t i = 0; i < minLen; i++) {
            if (input[i] != word[i]) {
                if (areProximate(input[i], word[i])) {
                    result.primaryError = ErrorType::PROXIMITY_CORRECTION;
                    result.weightedCost = (i == 0)
                        ? ScoringParams::FIRST_CHAR_PROXIMITY_COST
                        : ScoringParams::PROXIMITY_COST;
                    return result;
                }
            }
        }
    }

    // Determine insertion/omission/substitution
    if (input.length() > word.length()) {
        result.primaryError = ErrorType::INSERTION_CORRECTION;
        result.weightedCost = ScoringParams::INSERTION_COST * result.distance;
    } else if (input.length() < word.length()) {
        result.primaryError = ErrorType::OMISSION_CORRECTION;
        result.weightedCost = ScoringParams::OMISSION_COST * result.distance;
    } else {
        result.primaryError = ErrorType::SUBSTITUTION_CORRECTION;
        result.weightedCost = ScoringParams::SUBSTITUTION_COST * result.distance;
    }

    return result;
}

int SuggestEngine::levenshteinDistance(const std::string& a, const std::string& b, int maxDistance) {
    if (a == b) return 0;

    int lengthDiff = std::abs(static_cast<int>(a.length()) - static_cast<int>(b.length()));
    if (lengthDiff > maxDistance) return maxDistance + 1;

    if (a.empty()) return std::min(static_cast<int>(b.length()), maxDistance + 1);
    if (b.empty()) return std::min(static_cast<int>(a.length()), maxDistance + 1);

    // Use vector instead of 2D array for better cache performance
    std::vector<int> prev(b.length() + 1);
    std::vector<int> curr(b.length() + 1);

    // Initialize first row
    for (size_t j = 0; j <= b.length(); j++) {
        prev[j] = j;
    }

    // Fill matrix with early exit
    for (size_t i = 1; i <= a.length(); i++) {
        curr[0] = i;
        int minInRow = curr[0];

        for (size_t j = 1; j <= b.length(); j++) {
            if (a[i - 1] == b[j - 1]) {
                curr[j] = prev[j - 1];
            } else {
                curr[j] = 1 + std::min({prev[j - 1], prev[j], curr[j - 1]});
            }
            minInRow = std::min(minInRow, curr[j]);
        }

        // Early exit if minimum exceeds threshold
        if (minInRow > maxDistance) {
            return maxDistance + 1;
        }

        std::swap(prev, curr);
    }

    return prev[b.length()];
}

bool SuggestEngine::areProximate(char a, char b) {
    if (!proximityInfo_) return false;

    // Convert to lowercase for comparison
    char aLower = (a >= 'A' && a <= 'Z') ? a + 32 : a;
    char bLower = (b >= 'A' && b <= 'Z') ? b + 32 : b;

    return proximityInfo_->areProximate(aLower, bLower);
}

float SuggestEngine::calculateFinalScore(const WordEntry& entry, const EditResult& editResult,
                                         int inputLength, bool exactPrefix) {
    float score = 0.0f;

    // Base score from frequency (normalized to 0-1)
    float freqScore = static_cast<float>(entry.frequency) / 255.0f;

    if (exactPrefix) {
        // Exact prefix match gets high score
        score = freqScore * ScoringParams::EXACT_MATCH_PROMOTION;

        // Bonus for exact length match
        if (entry.word.length() == static_cast<size_t>(inputLength)) {
            score *= ScoringParams::PERFECT_MATCH_PROMOTION;
        }
    } else {
        // Correction case - apply penalties
        float penalty = editResult.weightedCost;

        // Distance penalty
        float distancePenalty = static_cast<float>(editResult.distance) *
                               ScoringParams::DISTANCE_WEIGHT_LENGTH;

        score = freqScore * (1.0f - penalty - distancePenalty);

        // Ensure non-negative
        score = std::max(0.0f, score);
    }

    // Length similarity bonus
    int wordLength = static_cast<int>(entry.word.length());
    float lengthRatio = static_cast<float>(std::min(inputLength, wordLength)) /
                       static_cast<float>(std::max(inputLength, wordLength));
    score *= (0.5f + 0.5f * lengthRatio);

    return score;
}

} // namespace hoskey
