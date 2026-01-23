/**
 * Suggest Engine for HOSKEY
 * Combines Trie lookup with weighted error correction
 *
 * Features:
 * - Proximity-aware correction
 * - Multiple error type handling
 * - Optimized for <5ms response time
 */

#ifndef HOSKEY_SUGGEST_ENGINE_H
#define HOSKEY_SUGGEST_ENGINE_H

#include <string>
#include <vector>
#include "trie.h"

namespace hoskey {

/**
 * Error types for correction detection
 */
enum class ErrorType {
    NOT_AN_ERROR,
    PROXIMITY_CORRECTION,      // Key near the intended key
    SUBSTITUTION_CORRECTION,   // Wrong character
    OMISSION_CORRECTION,       // Missing character
    INSERTION_CORRECTION,      // Extra character
    TRANSPOSITION_CORRECTION   // Swapped characters
};

/**
 * Scoring parameters for suggestion ranking
 */
struct ScoringParams {
    static constexpr float AUTOCORRECTION_THRESHOLD = 0.185f;
    static constexpr float PLAUSIBILITY_THRESHOLD = 0.15f;
    static constexpr int THRESHOLD_SHORT_WORD_LENGTH = 3;
    static constexpr float TRANSPOSITION_COST = 0.1f;
    static constexpr float FIRST_CHAR_PROXIMITY_COST = 0.2f;
    static constexpr float PROXIMITY_COST = 0.1f;
    static constexpr float INSERTION_COST = 0.15f;
    static constexpr float OMISSION_COST = 0.15f;
    static constexpr float SUBSTITUTION_COST = 0.2f;
    static constexpr float EXACT_MATCH_PROMOTION = 1.2f;
    static constexpr float PERFECT_MATCH_PROMOTION = 1.1f;
    static constexpr float DISTANCE_WEIGHT_LENGTH = 0.1f;
};

/**
 * Simple proximity info for keyboard-aware correction
 */
class ProximityInfo {
public:
    bool areProximate(char a, char b) const;
};

/**
 * Result from suggestion engine
 */
struct SuggestResult {
    std::string word;
    float score;           // Combined score (0-1, higher = better)
    ErrorType errorType;   // Primary error type detected
    int editDistance;      // Raw edit distance

    SuggestResult() : score(0.0f), errorType(ErrorType::NOT_AN_ERROR), editDistance(0) {}
    SuggestResult(const std::string& w, float s, ErrorType e, int d)
        : word(w), score(s), errorType(e), editDistance(d) {}
};

/**
 * Suggest Engine with weighted Levenshtein distance
 */
class SuggestEngine {
public:
    explicit SuggestEngine(Trie* trie);
    ~SuggestEngine();

    // Set proximity info for keyboard-aware correction
    void setProximityInfo(ProximityInfo* proximityInfo);

    // Get suggestions for prefix
    std::vector<SuggestResult> getSuggestions(const std::string& input, int limit = 10);

    // Find best autocorrection candidate
    SuggestResult findAutocorrection(const std::string& word, float threshold = 0.185f);

    // Check if word should be autocorrected
    bool shouldAutocorrect(const std::string& input, const SuggestResult& suggestion);

private:
    Trie* trie_;
    ProximityInfo* proximityInfo_;

    // Weighted Levenshtein with error type detection
    struct EditResult {
        int distance;
        float weightedCost;
        ErrorType primaryError;
    };

    EditResult calculateWeightedDistance(const std::string& input, const std::string& word);

    // Simple Levenshtein for fallback
    int levenshteinDistance(const std::string& a, const std::string& b, int maxDistance = 3);

    // Check if two characters are proximate on keyboard
    bool areProximate(char a, char b);

    // Calculate final score combining all factors
    float calculateFinalScore(const WordEntry& entry, const EditResult& editResult,
                             int inputLength, bool exactPrefix);
};

} // namespace hoskey

#endif // HOSKEY_SUGGEST_ENGINE_H
