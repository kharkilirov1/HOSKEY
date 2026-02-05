/**
 * HOSKEY Keyboard - Input Session
 *
 * Manages state for a single input session (text field).
 * Tracks context, predictions, and user interactions.
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#ifndef KEYBOARD_NATIVE_SESSION_H
#define KEYBOARD_NATIVE_SESSION_H

#include <string>
#include <vector>
#include <deque>
#include <chrono>

namespace keyboard {
namespace core {

/**
 * Prediction entry for history
 */
struct PredictionEntry {
    std::string input;
    std::vector<std::string> candidates;
    std::string selected;           // What user selected (empty if none)
    std::chrono::steady_clock::time_point timestamp;
    uint32_t latencyUs;
};

/**
 * Input session state
 */
class Session {
public:
    Session();

    /**
     * Reset session state (called when moving to new text field)
     */
    void reset();

    /**
     * Update context with committed word
     */
    void commitWord(const std::string& word);

    /**
     * Record prediction for history
     */
    void recordPrediction(const std::string& input,
                          const std::vector<std::string>& candidates,
                          uint32_t latencyUs);

    /**
     * Record user selection
     */
    void recordSelection(const std::string& selected);

    /**
     * Get previous words (for n-gram context)
     */
    const std::string& getPrevWord1() const { return prevWord1_; }
    const std::string& getPrevWord2() const { return prevWord2_; }

    /**
     * Get last input (for cache optimization)
     */
    const std::string& getLastInput() const { return lastInput_; }

    /**
     * Check if input is continuation of last input (for prefix cache)
     */
    bool isContinuation(const std::string& input) const;

    /**
     * Get session duration
     */
    std::chrono::milliseconds getDuration() const;

    /**
     * Get prediction count in session
     */
    size_t getPredictionCount() const { return predictionHistory_.size(); }

    /**
     * Get average latency in session
     */
    uint32_t getAverageLatencyUs() const;

    /**
     * Get session ID (for tracing)
     */
    const std::string& getSessionId() const { return sessionId_; }

private:
    // Context
    std::string prevWord1_;
    std::string prevWord2_;
    std::string lastInput_;

    // History
    std::deque<PredictionEntry> predictionHistory_;
    static constexpr size_t MAX_HISTORY = 50;

    // Session info
    std::string sessionId_;
    std::chrono::steady_clock::time_point startTime_;
    uint64_t totalLatencyUs_ = 0;

    void generateSessionId();
};

} // namespace core
} // namespace keyboard

#endif // KEYBOARD_NATIVE_SESSION_H
