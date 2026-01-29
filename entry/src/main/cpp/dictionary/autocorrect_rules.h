/*
 * Copyright (c) 2024 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 * 
 * Manual Autocorrect Rules (inspired by Yandex Keyboard)
 * 
 * Handles common misspellings that dictionary-based correction
 * might not catch, such as:
 * - "придти" → "прийти"
 * - "вообщем" → "в общем"
 * - "вкурсе" → "в курсе"
 */

#ifndef HOSKEY_AUTOCORRECT_RULES_H
#define HOSKEY_AUTOCORRECT_RULES_H

#include <string>
#include <unordered_map>
#include <vector>

namespace latinime {

/**
 * Single autocorrect rule
 */
struct AutocorrectRule {
    std::string wrongForm;      // Misspelled form
    std::string correctForm;    // Correct form
    bool caseSensitive;         // Whether to match case exactly
    bool wordBoundary;          // Only match at word boundaries
    int priority;               // Higher = applied first
    
    AutocorrectRule() : caseSensitive(false), wordBoundary(true), priority(0) {}
    
    AutocorrectRule(const std::string& wrong, const std::string& correct,
                   bool caseSens = false, bool boundary = true, int prio = 0)
        : wrongForm(wrong), correctForm(correct), caseSensitive(caseSens),
          wordBoundary(boundary), priority(prio) {}
};

/**
 * Manual Autocorrect Rules Engine
 * 
 * Applies rule-based corrections for common misspellings
 * that are language-specific and hard to catch with pure
 * dictionary-based approaches.
 */
class AutocorrectRules {
public:
    static constexpr int MAX_RULES = 1024;
    
    AutocorrectRules();
    ~AutocorrectRules();
    
    /**
     * Load rules from a file
     * File format: one rule per line, tab-separated
     * wrong_form\tcorrect_form\t[options]
     * 
     * Options: C=case-sensitive, W=word-boundary, P=priority
     * Example: придти\tприйти\tW\t100
     */
    bool loadRules(const char* path);
    
    /**
     * Load rules from JSON string
     * Format: [{"wrong": "...", "correct": "...", "priority": N}, ...]
     */
    bool loadRulesFromJson(const char* jsonStr);
    
    /**
     * Add a single rule
     */
    void addRule(const AutocorrectRule& rule);
    void addRule(const std::string& wrongForm, const std::string& correctForm,
                bool caseSensitive = false, bool wordBoundary = true, int priority = 0);
    
    /**
     * Remove a rule
     */
    void removeRule(const std::string& wrongForm);
    
    /**
     * Clear all rules
     */
    void clearRules();
    
    /**
     * Apply rules to a word
     * @param word Input word
     * @return Corrected word (or original if no rule applies)
     */
    std::string apply(const std::string& word) const;
    
    /**
     * Apply rules to code points
     * @param codePoints Input code points
     * @param length Length
     * @param outCodePoints Output buffer (must be large enough)
     * @param outLength Output length
     * @return true if a rule was applied
     */
    bool applyToCodePoints(const int* codePoints, int length,
                          int* outCodePoints, int& outLength) const;
    
    /**
     * Check if a rule exists for the given word
     */
    bool hasRule(const std::string& word) const;
    
    /**
     * Get number of loaded rules
     */
    size_t getRuleCount() const { return mRules.size(); }
    
    /**
     * Load default Russian rules
     */
    void loadDefaultRussianRules();
    
    /**
     * Load default English rules
     */
    void loadDefaultEnglishRules();
    
private:
    std::unordered_map<std::string, AutocorrectRule> mRules;
    std::vector<AutocorrectRule> mRuleList;  // For priority-ordered iteration
    bool mNeedsSorting;
    
    /**
     * Normalize word for lookup (lowercase, etc.)
     */
    static std::string normalizeForLookup(const std::string& word);
    
    /**
     * Convert code points to UTF-8 string
     */
    static std::string codePointsToString(const int* codePoints, int length);
    
    /**
     * Convert UTF-8 string to code points
     */
    static void stringToCodePoints(const std::string& str, 
                                   std::vector<int>& outCodePoints);
    
    /**
     * Sort rules by priority
     */
    void sortRulesByPriority();
};

} // namespace latinime

#endif // HOSKEY_AUTOCORRECT_RULES_H
