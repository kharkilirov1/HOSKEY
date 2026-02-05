/**
 * HOSKEY Keyboard - Fallback Policy Implementation
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#include "fallback_policy.h"
#include <algorithm>

namespace keyboard {
namespace core {

FallbackPolicy::FallbackPolicy() = default;

FallbackDecision FallbackPolicy::decide(const FallbackContext& ctx) const {
    FallbackDecision decision;
    decision.shouldUseNeural = false;
    decision.shouldUseNgram = true;
    decision.shouldUseTrie = true;
    decision.shouldUseRule = false;
    decision.neuralDeadlineMs = 0;
    decision.remainingDeadlineMs = ctx.totalDeadlineMs - ctx.elapsedMs;
    decision.reason = FallbackReason::None;

    // Check policy
    if (policy_ == KB_FALLBACK_NONE) {
        // Must use neural or fail
        if (!ctx.neuralReady) {
            decision.reason = FallbackReason::NeuralNotReady;
            return decision;
        }
        decision.shouldUseNeural = true;
        decision.neuralDeadlineMs = decision.remainingDeadlineMs;
        return decision;
    }

    if (policy_ == KB_FALLBACK_RULE_ONLY) {
        decision.shouldUseNeural = false;
        decision.shouldUseNgram = false;
        decision.shouldUseTrie = false;
        decision.shouldUseRule = true;
        decision.reason = FallbackReason::ConfigDisabled;
        return decision;
    }

    if (policy_ == KB_FALLBACK_TRIE_ONLY) {
        decision.shouldUseNeural = false;
        decision.shouldUseNgram = false;
        decision.shouldUseTrie = true;
        decision.shouldUseRule = true;
        decision.reason = FallbackReason::ConfigDisabled;
        return decision;
    }

    // Default policy: KB_FALLBACK_NGRAM_TRIE_RULE
    // Use neural if ready and we have time budget

    if (!ctx.neuralReady) {
        decision.reason = FallbackReason::NeuralNotReady;
        decision.shouldUseRule = true;
        return decision;
    }

    // Calculate if we have time for neural
    int availableTime = decision.remainingDeadlineMs - SAFETY_MARGIN_MS;

    if (availableTime <= 0) {
        decision.reason = FallbackReason::DeadlineExceeded;
        decision.shouldUseRule = true;
        return decision;
    }

    // Adaptive budget based on recent neural latency
    int neuralBudget = adaptiveNeuralBudgetMs_;

    // If recent latency is high, reduce budget
    if (ctx.recentNeuralLatencyMs > 0) {
        // Use recent p95 as guide, with some headroom
        neuralBudget = std::min(neuralBudget,
            static_cast<int>(ctx.recentNeuralLatencyMs * 1.2f));
    }

    // If recent fallback rate is high, be more aggressive with timeout
    if (ctx.recentFallbackRate > 30) {
        neuralBudget = std::max(5, neuralBudget - 5);
    }

    // Cap to available time
    neuralBudget = std::min(neuralBudget, availableTime);

    if (neuralBudget < 3) {
        // Not enough time for neural
        decision.reason = FallbackReason::DeadlineExceeded;
        decision.shouldUseRule = true;
        return decision;
    }

    decision.shouldUseNeural = true;
    decision.neuralDeadlineMs = neuralBudget;
    decision.shouldUseRule = true;  // Always have rule as backup

    return decision;
}

void FallbackPolicy::recordOutcome(bool usedNeural, bool neuralSucceeded,
                                   int neuralLatencyMs, int /*totalLatencyMs*/) {
    if (!usedNeural) {
        return;
    }

    if (neuralSucceeded) {
        consecutiveTimeouts_ = 0;

        // Slowly adapt budget based on actual latency
        if (neuralLatencyMs > 0 && neuralLatencyMs < 100) {
            // Exponential moving average
            adaptiveNeuralBudgetMs_ = static_cast<int>(
                adaptiveNeuralBudgetMs_ * 0.9 + neuralLatencyMs * 1.2 * 0.1);
            adaptiveNeuralBudgetMs_ = std::max(5, std::min(30, adaptiveNeuralBudgetMs_));
        }
    } else {
        consecutiveTimeouts_++;

        // If too many consecutive timeouts, increase budget temporarily
        if (consecutiveTimeouts_ >= 3) {
            adaptiveNeuralBudgetMs_ = std::min(30, adaptiveNeuralBudgetMs_ + 5);
        }
    }
}

} // namespace core
} // namespace keyboard
