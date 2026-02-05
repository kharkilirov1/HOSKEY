/**
 * HOSKEY Keyboard - Fallback Policy
 *
 * Manages fallback decisions when neural inference fails or times out.
 * Ensures prediction always returns results within latency budget.
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#ifndef KEYBOARD_NATIVE_FALLBACK_POLICY_H
#define KEYBOARD_NATIVE_FALLBACK_POLICY_H

#include "../../include/types.h"
#include <string>
#include <chrono>

namespace keyboard {
namespace core {

/**
 * Reason for triggering fallback
 */
enum class FallbackReason {
    None,               // No fallback needed
    NeuralNotReady,     // Neural engine not loaded/ready
    NeuralTimeout,      // Neural inference timed out
    NeuralError,        // Neural inference failed
    DeadlineExceeded,   // Would exceed total deadline
    EmptyNeuralResults, // Neural returned no results
    ConfigDisabled      // Neural disabled in config
};

/**
 * Fallback decision result
 */
struct FallbackDecision {
    bool shouldUseNeural;       // Attempt neural inference
    bool shouldUseNgram;        // Use n-gram model
    bool shouldUseTrie;         // Use trie/dictionary
    bool shouldUseRule;         // Use rule-based (last resort)

    int neuralDeadlineMs;       // Time budget for neural
    int remainingDeadlineMs;    // Time for fallback sources

    FallbackReason reason;      // Why fallback was triggered
};

/**
 * Runtime context for fallback decisions
 */
struct FallbackContext {
    bool neuralReady;           // Is neural engine ready?
    int totalDeadlineMs;        // Total time budget
    int elapsedMs;              // Time already spent
    int recentNeuralLatencyMs;  // Recent neural latency (p95)
    int recentFallbackRate;     // Percentage of recent fallbacks (0-100)
};

/**
 * Fallback policy implementation
 */
class FallbackPolicy {
public:
    FallbackPolicy();

    /**
     * Set policy from config
     */
    void setPolicy(KBFallbackPolicy policy) { policy_ = policy; }

    /**
     * Set latency budgets
     */
    void setLatencyBudget(int p50Ms, int p95Ms, int p99Ms) {
        p50BudgetMs_ = p50Ms;
        p95BudgetMs_ = p95Ms;
        p99BudgetMs_ = p99Ms;
    }

    /**
     * Make fallback decision based on context
     */
    FallbackDecision decide(const FallbackContext& ctx) const;

    /**
     * Update after prediction (for adaptive policy)
     */
    void recordOutcome(bool usedNeural, bool neuralSucceeded,
                       int neuralLatencyMs, int totalLatencyMs);

    /**
     * Get fallback reason as string
     */
    static const char* reasonToString(FallbackReason reason);

private:
    KBFallbackPolicy policy_ = KB_FALLBACK_NGRAM_TRIE_RULE;

    // Latency budgets
    int p50BudgetMs_ = 8;
    int p95BudgetMs_ = 20;
    int p99BudgetMs_ = 35;

    // Adaptive thresholds (updated based on history)
    mutable int adaptiveNeuralBudgetMs_ = 15;
    mutable int consecutiveTimeouts_ = 0;

    // Safety margin for non-neural work
    static constexpr int SAFETY_MARGIN_MS = 5;
};

/**
 * Reason to string conversion
 */
inline const char* FallbackPolicy::reasonToString(FallbackReason reason) {
    switch (reason) {
        case FallbackReason::None:
            return "none";
        case FallbackReason::NeuralNotReady:
            return "neural_not_ready";
        case FallbackReason::NeuralTimeout:
            return "neural_timeout";
        case FallbackReason::NeuralError:
            return "neural_error";
        case FallbackReason::DeadlineExceeded:
            return "deadline_exceeded";
        case FallbackReason::EmptyNeuralResults:
            return "empty_neural_results";
        case FallbackReason::ConfigDisabled:
            return "config_disabled";
        default:
            return "unknown";
    }
}

} // namespace core
} // namespace keyboard

#endif // KEYBOARD_NATIVE_FALLBACK_POLICY_H
