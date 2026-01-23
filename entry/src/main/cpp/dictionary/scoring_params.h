/**
 * Scoring Parameters for HOSKEY Suggest Engine
 * Error type definitions and scoring weights
 */

#ifndef HOSKEY_SCORING_PARAMS_H
#define HOSKEY_SCORING_PARAMS_H

namespace hoskey {

/**
 * Error types for autocorrection
 */
enum class ErrorType {
    NOT_AN_ERROR = 0,
    PROXIMITY_CORRECTION = 1,      // Wrong key press (near correct key)
    OMISSION_CORRECTION = 2,       // Missing character
    INSERTION_CORRECTION = 3,      // Extra character
    SUBSTITUTION_CORRECTION = 4,   // Wrong character (not proximity)
    TRANSPOSITION_CORRECTION = 5,  // Swapped adjacent characters
};

} // namespace hoskey

#endif // HOSKEY_SCORING_PARAMS_H
