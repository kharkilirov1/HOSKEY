/**
 * HOSKEY Keyboard - Input Session Implementation
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#include "session.h"
#include <random>
#include <sstream>
#include <iomanip>

namespace keyboard {
namespace core {

Session::Session() {
    reset();
}

void Session::reset() {
    prevWord1_.clear();
    prevWord2_.clear();
    lastInput_.clear();
    predictionHistory_.clear();
    totalLatencyUs_ = 0;
    startTime_ = std::chrono::steady_clock::now();
    generateSessionId();
}

void Session::commitWord(const std::string& word) {
    // Shift context
    prevWord2_ = prevWord1_;
    prevWord1_ = word;
    lastInput_.clear();
}

void Session::recordPrediction(const std::string& input,
                                const std::vector<std::string>& candidates,
                                uint32_t latencyUs) {
    PredictionEntry entry;
    entry.input = input;
    entry.candidates = candidates;
    entry.timestamp = std::chrono::steady_clock::now();
    entry.latencyUs = latencyUs;

    predictionHistory_.push_back(entry);

    // Limit history size
    while (predictionHistory_.size() > MAX_HISTORY) {
        predictionHistory_.pop_front();
    }

    lastInput_ = input;
    totalLatencyUs_ += latencyUs;
}

void Session::recordSelection(const std::string& selected) {
    if (!predictionHistory_.empty()) {
        predictionHistory_.back().selected = selected;
    }
}

bool Session::isContinuation(const std::string& input) const {
    if (lastInput_.empty() || input.empty()) {
        return false;
    }

    // Check if new input starts with last input (user is typing more)
    if (input.length() > lastInput_.length()) {
        return input.substr(0, lastInput_.length()) == lastInput_;
    }

    // Or if new input is prefix of last (user deleted chars)
    if (input.length() < lastInput_.length()) {
        return lastInput_.substr(0, input.length()) == input;
    }

    return input == lastInput_;
}

std::chrono::milliseconds Session::getDuration() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime_);
}

uint32_t Session::getAverageLatencyUs() const {
    if (predictionHistory_.empty()) {
        return 0;
    }
    return static_cast<uint32_t>(totalLatencyUs_ / predictionHistory_.size());
}

void Session::generateSessionId() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);

    const char* hex = "0123456789abcdef";
    sessionId_.clear();
    sessionId_.reserve(16);

    for (int i = 0; i < 16; ++i) {
        sessionId_.push_back(hex[dis(gen)]);
    }
}

} // namespace core
} // namespace keyboard
