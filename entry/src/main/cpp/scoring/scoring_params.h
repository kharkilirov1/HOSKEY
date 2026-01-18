/**
 * Scoring Parameters for HOSKEY
 * Ported from OpenBoard (Apache 2.0 License)
 *
 * These numerically optimized parameters are crucial for high-quality autocorrection.
 * Based on extensive A/B testing by Android/OpenBoard team.
 */

#ifndef HOSKEY_SCORING_PARAMS_H
#define HOSKEY_SCORING_PARAMS_H

namespace hoskey {

/**
 * Error types for weighted correction
 * More granular than simple Levenshtein distance
 */
enum class ErrorType {
    NOT_AN_ERROR = 0,
    MATCH_WITH_WRONG_CASE = 1,
    MATCH_WITH_ACCENT_ERROR = 2,
    PROXIMITY_CORRECTION = 3,      // Nearby key pressed
    INSERTION_CORRECTION = 4,      // Extra character typed
    OMISSION_CORRECTION = 5,       // Character missing
    TRANSPOSITION_CORRECTION = 6,  // Two chars swapped
    SUBSTITUTION_CORRECTION = 7    // Wrong character
};

/**
 * Scoring parameters from OpenBoard
 * All values are carefully tuned through machine learning and user studies
 */
class ScoringParams {
public:
    // === MATCH PROMOTIONS ===
    static constexpr float EXACT_MATCH_PROMOTION = 1.1f;
    static constexpr float PERFECT_MATCH_PROMOTION = 1.1f;

    // === CASE/ACCENT PENALTIES ===
    static constexpr float CASE_ERROR_PENALTY_FOR_EXACT_MATCH = 0.01f;
    static constexpr float ACCENT_ERROR_PENALTY_FOR_EXACT_MATCH = 0.02f;
    static constexpr float DIGRAPH_PENALTY_FOR_EXACT_MATCH = 0.03f;

    // === PROXIMITY COSTS (nearby key pressed) ===
    static constexpr float PROXIMITY_COST = 0.0694f;
    static constexpr float FIRST_CHAR_PROXIMITY_COST = 0.072f;
    static constexpr float FIRST_PROXIMITY_COST = 0.07788f;
    static constexpr float ADDITIONAL_PROXIMITY_COST = 0.37972f;

    // === OMISSION COSTS (character missing) ===
    static constexpr float OMISSION_COST = 0.467f;
    static constexpr float OMISSION_COST_SAME_CHAR = 0.345f;
    static constexpr float OMISSION_COST_FIRST_CHAR = 0.5256f;
    static constexpr float INTENTIONAL_OMISSION_COST = 0.1f;

    // === INSERTION COSTS (extra character typed) ===
    static constexpr float INSERTION_COST = 0.7248f;
    static constexpr float TERMINAL_INSERTION_COST = 0.8128f;
    static constexpr float INSERTION_COST_SAME_CHAR = 0.5508f;
    static constexpr float INSERTION_COST_PROXIMITY_CHAR = 0.674f;
    static constexpr float INSERTION_COST_FIRST_CHAR = 0.639f;

    // === TRANSPOSITION COST (two chars swapped) ===
    static constexpr float TRANSPOSITION_COST = 0.5608f;

    // === SUBSTITUTION COST (wrong character) ===
    static constexpr float SUBSTITUTION_COST = 0.3806f;

    // === SPACE HANDLING ===
    static constexpr float SPACE_SUBSTITUTION_COST = 0.33f;
    static constexpr float SPACE_OMISSION_COST = 0.1f;

    // === COMPLETION COSTS ===
    static constexpr float COST_FIRST_COMPLETION = 0.4836f;
    static constexpr float COST_COMPLETION = 0.00624f;

    // === TERMINAL COSTS ===
    static constexpr float HAS_PROXIMITY_TERMINAL_COST = 0.0683f;
    static constexpr float HAS_EDIT_CORRECTION_TERMINAL_COST = 0.0362f;
    static constexpr float HAS_MULTI_WORD_TERMINAL_COST = 0.3482f;

    // === DISTANCE WEIGHTS ===
    static constexpr float DISTANCE_WEIGHT_LENGTH = 0.1524f;
    static constexpr float DISTANCE_WEIGHT_LANGUAGE = 1.1214f;

    // === THRESHOLDS ===
    static constexpr float MAX_SPATIAL_DISTANCE = 1.0f;
    static constexpr float AUTOCORRECT_OUTPUT_THRESHOLD = 1.0f;
    static constexpr int THRESHOLD_NEXT_WORD_PROBABILITY = 40;
    static constexpr int THRESHOLD_NEXT_WORD_PROBABILITY_FOR_CAPPED = 120;
    static constexpr int THRESHOLD_SHORT_WORD_LENGTH = 4;
    static constexpr float NORMALIZED_SPATIAL_DISTANCE_THRESHOLD_FOR_EDIT = 0.095f;

    // === TYPING SCORES ===
    static constexpr float TYPING_BASE_OUTPUT_SCORE = 1.0f;
    static constexpr float TYPING_MAX_OUTPUT_SCORE_PER_INPUT = 0.1f;

    // === CACHE LIMITS ===
    static constexpr int MAX_CACHE_DIC_NODE_SIZE = 170;
    static constexpr int MAX_CACHE_DIC_NODE_SIZE_FOR_SINGLE_POINT = 310;

    // === AUTOCORRECTION THRESHOLDS ===
    // These are the key values for deciding when to autocorrect
    static constexpr float AUTOCORRECTION_THRESHOLD = 0.185f;  // Main threshold
    static constexpr float PLAUSIBILITY_THRESHOLD = 0.5f;      // Secondary check

    // === EDIT DISTANCE MULTIPLIERS ===
    static constexpr int MAX_INITIAL_SCORE = 255;
    static constexpr int TYPED_LETTER_MULTIPLIER = 2;
    static constexpr int FULL_WORD_MULTIPLIER = 2;

    /**
     * Get cost for specific error type
     */
    static float getCostForErrorType(ErrorType type, bool isFirstChar = false, bool isSameChar = false);

    /**
     * Calculate normalized score (for autocorrection decision)
     */
    static float calcNormalizedScore(int editDistance, int inputLength, int wordLength, int rawScore);

private:
    ScoringParams() = delete;
};

} // namespace hoskey

#endif // HOSKEY_SCORING_PARAMS_H
