/**
 * Error Type Utilities
 * Helper functions for error classification
 */

#include "scoring_params.h"

namespace hoskey {

/**
 * Determine if an error is "acceptable" for autocorrection
 */
bool isAcceptableError(ErrorType type, int editDistance, int wordLength) {
    // Single character errors are always acceptable
    if (editDistance <= 1) {
        return true;
    }

    // For longer words, allow more errors
    int maxAllowedErrors = wordLength / 4 + 1;
    if (editDistance > maxAllowedErrors) {
        return false;
    }

    // Transposition is relatively acceptable
    if (type == ErrorType::TRANSPOSITION_CORRECTION) {
        return true;
    }

    // Proximity errors are acceptable
    if (type == ErrorType::PROXIMITY_CORRECTION) {
        return true;
    }

    // Case errors are always acceptable
    if (type == ErrorType::MATCH_WITH_WRONG_CASE) {
        return true;
    }

    // Multiple substitutions are less acceptable
    if (type == ErrorType::SUBSTITUTION_CORRECTION && editDistance > 1) {
        return false;
    }

    return editDistance <= 2;
}

/**
 * Get human-readable name for error type
 */
const char* getErrorTypeName(ErrorType type) {
    switch (type) {
        case ErrorType::NOT_AN_ERROR:
            return "exact_match";
        case ErrorType::MATCH_WITH_WRONG_CASE:
            return "case_error";
        case ErrorType::MATCH_WITH_ACCENT_ERROR:
            return "accent_error";
        case ErrorType::PROXIMITY_CORRECTION:
            return "proximity";
        case ErrorType::INSERTION_CORRECTION:
            return "insertion";
        case ErrorType::OMISSION_CORRECTION:
            return "omission";
        case ErrorType::TRANSPOSITION_CORRECTION:
            return "transposition";
        case ErrorType::SUBSTITUTION_CORRECTION:
            return "substitution";
        default:
            return "unknown";
    }
}

} // namespace hoskey
