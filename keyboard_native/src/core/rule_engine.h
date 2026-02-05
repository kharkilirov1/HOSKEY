/**
 * HOSKEY Keyboard - Rule Engine
 *
 * Rule-based predictions as ultimate fallback.
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#ifndef KEYBOARD_NATIVE_RULE_ENGINE_H
#define KEYBOARD_NATIVE_RULE_ENGINE_H

#include <string>
#include <vector>

namespace keyboard {
namespace core {

/**
 * Rule-based prediction engine
 * Ultimate fallback when all else fails
 */
class RuleEngine {
public:
    RuleEngine() = default;
    ~RuleEngine() = default;

    /**
     * Get completions based on rules
     */
    std::vector<std::string> complete(const std::string& prefix, int maxResults) {
        std::vector<std::string> results;

        // Very simple completions - just add common endings
        if (prefix.empty() || maxResults <= 0) {
            return results;
        }

        // Common word endings for Russian
        static const std::vector<std::string> ruEndings = {
            "а", "о", "е", "и", "ы", "у", "ть", "ся", "ет", "ит"
        };

        // Common word endings for English
        static const std::vector<std::string> enEndings = {
            "s", "ed", "ing", "er", "ly", "tion", "ness"
        };

        // Detect language (very simple - check if first char is Cyrillic)
        unsigned char firstByte = static_cast<unsigned char>(prefix[0]);
        bool isCyrillic = (firstByte >= 0xD0 && firstByte <= 0xD3);

        const auto& endings = isCyrillic ? ruEndings : enEndings;

        for (const auto& ending : endings) {
            if (static_cast<int>(results.size()) >= maxResults) break;
            results.push_back(prefix + ending);
        }

        return results;
    }

    /**
     * Check if loaded (always true for rule engine)
     */
    bool isLoaded() const { return true; }
};

} // namespace core
} // namespace keyboard

#endif // KEYBOARD_NATIVE_RULE_ENGINE_H
