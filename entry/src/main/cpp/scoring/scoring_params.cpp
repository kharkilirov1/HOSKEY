/**
 * Scoring Parameters implementation
 * Ported from OpenBoard (Apache 2.0 License)
 */

#include "scoring_params.h"
#include <cmath>
#include <algorithm>

namespace hoskey {

float ScoringParams::getCostForErrorType(ErrorType type, bool isFirstChar, bool isSameChar) {
    switch (type) {
        case ErrorType::NOT_AN_ERROR:
            return 0.0f;

        case ErrorType::MATCH_WITH_WRONG_CASE:
            return CASE_ERROR_PENALTY_FOR_EXACT_MATCH;

        case ErrorType::MATCH_WITH_ACCENT_ERROR:
            return ACCENT_ERROR_PENALTY_FOR_EXACT_MATCH;

        case ErrorType::PROXIMITY_CORRECTION:
            if (isFirstChar) {
                return FIRST_CHAR_PROXIMITY_COST;
            }
            return PROXIMITY_COST;

        case ErrorType::INSERTION_CORRECTION:
            if (isFirstChar) {
                return INSERTION_COST_FIRST_CHAR;
            }
            if (isSameChar) {
                return INSERTION_COST_SAME_CHAR;
            }
            return INSERTION_COST;

        case ErrorType::OMISSION_CORRECTION:
            if (isFirstChar) {
                return OMISSION_COST_FIRST_CHAR;
            }
            if (isSameChar) {
                return OMISSION_COST_SAME_CHAR;
            }
            return OMISSION_COST;

        case ErrorType::TRANSPOSITION_CORRECTION:
            return TRANSPOSITION_COST;

        case ErrorType::SUBSTITUTION_CORRECTION:
            return SUBSTITUTION_COST;

        default:
            return SUBSTITUTION_COST; // Fallback
    }
}

float ScoringParams::calcNormalizedScore(int editDistance, int inputLength, int wordLength, int rawScore) {
    if (inputLength == 0 || wordLength == 0) {
        return 0.0f;
    }

    if (rawScore <= 0 || editDistance >= wordLength) {
        return 0.0f;
    }

    // Weight based on edit distance relative to word length
    float weight = 1.0f - static_cast<float>(editDistance) / static_cast<float>(wordLength);

    // Maximum possible score calculation
    float maxScore = static_cast<float>(MAX_INITIAL_SCORE)
                   * powf(static_cast<float>(TYPED_LETTER_MULTIPLIER),
                         static_cast<float>(std::min(inputLength, wordLength)))
                   * static_cast<float>(FULL_WORD_MULTIPLIER);

    return (static_cast<float>(rawScore) / maxScore) * weight;
}

} // namespace hoskey
