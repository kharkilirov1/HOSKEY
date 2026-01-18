/**
 * Weighting functions for scoring
 * Based on OpenBoard's typing_weighting.cpp
 */

#include "scoring_params.h"
#include <cmath>
#include <algorithm>

namespace hoskey {

/**
 * Calculate combined weight for a correction
 * Takes into account multiple error types and their positions
 */
float calculateCombinedWeight(
        int editDistance,
        float proximityCost,
        float frequencyScore,
        int wordLength,
        bool hasProximityError) {

    // Base weight from edit distance
    float distanceWeight = 1.0f - (static_cast<float>(editDistance) *
                                   ScoringParams::DISTANCE_WEIGHT_LENGTH);

    // Proximity bonus (if error is nearby key, less penalty)
    float proximityWeight = 1.0f;
    if (hasProximityError && proximityCost > 0) {
        proximityWeight = 1.0f - proximityCost;
    }

    // Length bonus (longer words get more tolerance)
    float lengthBonus = 1.0f;
    if (wordLength > ScoringParams::THRESHOLD_SHORT_WORD_LENGTH) {
        lengthBonus = 1.0f + (wordLength - ScoringParams::THRESHOLD_SHORT_WORD_LENGTH) * 0.02f;
    }

    // Frequency weight from dictionary
    float freqWeight = frequencyScore * ScoringParams::DISTANCE_WEIGHT_LANGUAGE;

    // Combine all weights
    return distanceWeight * proximityWeight * lengthBonus * freqWeight;
}

/**
 * Apply terminal cost based on correction type
 */
float applyTerminalCost(float score, bool hasProximityError, bool hasEditError, bool isMultiWord) {
    if (hasProximityError) {
        score -= ScoringParams::HAS_PROXIMITY_TERMINAL_COST;
    }
    if (hasEditError) {
        score -= ScoringParams::HAS_EDIT_CORRECTION_TERMINAL_COST;
    }
    if (isMultiWord) {
        score -= ScoringParams::HAS_MULTI_WORD_TERMINAL_COST;
    }
    return std::max(0.0f, score);
}

} // namespace hoskey
