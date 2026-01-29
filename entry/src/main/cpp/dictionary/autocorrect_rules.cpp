/*
 * Copyright (c) 2024 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#include "autocorrect_rules.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace latinime {

AutocorrectRules::AutocorrectRules() : mNeedsSorting(false) {
    mRules.reserve(256);
    mRuleList.reserve(256);
}

AutocorrectRules::~AutocorrectRules() = default;

bool AutocorrectRules::loadRules(const char* path) {
    if (!path) return false;
    
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // Parse tab-separated format: wrong\tcorrect\t[options]
        std::istringstream iss(line);
        std::string wrongForm, correctForm, options;
        
        if (!std::getline(iss, wrongForm, '\t')) continue;
        if (!std::getline(iss, correctForm, '\t')) continue;
        std::getline(iss, options, '\t');  // Optional
        
        bool caseSensitive = false;
        bool wordBoundary = true;
        int priority = 0;
        
        // Parse options
        for (char c : options) {
            if (c == 'C' || c == 'c') caseSensitive = true;
            if (c == 'W' || c == 'w') wordBoundary = true;
            if (c == 'N' || c == 'n') wordBoundary = false;
        }
        
        // Parse priority if present
        size_t pPos = options.find('P');
        if (pPos == std::string::npos) pPos = options.find('p');
        if (pPos != std::string::npos && pPos + 1 < options.size()) {
            priority = std::atoi(options.c_str() + pPos + 1);
        }
        
        addRule(wrongForm, correctForm, caseSensitive, wordBoundary, priority);
    }
    
    return true;
}

bool AutocorrectRules::loadRulesFromJson(const char* jsonStr) {
    // Simplified JSON parsing (for full support, use a JSON library)
    // Expected format: [{"wrong":"...", "correct":"...", "priority":N}, ...]
    
    if (!jsonStr) return false;
    
    // This is a simplified parser - production code should use proper JSON parsing
    std::string json(jsonStr);
    
    // Find all rule objects
    size_t pos = 0;
    while ((pos = json.find("\"wrong\"", pos)) != std::string::npos) {
        // Extract wrong value
        size_t valueStart = json.find("\"", pos + 7);
        if (valueStart == std::string::npos) break;
        size_t valueEnd = json.find("\"", valueStart + 1);
        if (valueEnd == std::string::npos) break;
        std::string wrongForm = json.substr(valueStart + 1, valueEnd - valueStart - 1);
        
        // Find correct value
        size_t correctPos = json.find("\"correct\"", valueEnd);
        if (correctPos == std::string::npos) break;
        valueStart = json.find("\"", correctPos + 9);
        if (valueStart == std::string::npos) break;
        valueEnd = json.find("\"", valueStart + 1);
        if (valueEnd == std::string::npos) break;
        std::string correctForm = json.substr(valueStart + 1, valueEnd - valueStart - 1);
        
        // Find priority (optional)
        int priority = 0;
        size_t priorityPos = json.find("\"priority\"", valueEnd);
        if (priorityPos != std::string::npos && priorityPos < json.find("}", valueEnd)) {
            size_t numStart = json.find_first_of("0123456789-", priorityPos + 10);
            if (numStart != std::string::npos) {
                priority = std::atoi(json.c_str() + numStart);
            }
        }
        
        addRule(wrongForm, correctForm, false, true, priority);
        pos = valueEnd + 1;
    }
    
    return true;
}

void AutocorrectRules::addRule(const AutocorrectRule& rule) {
    std::string key = normalizeForLookup(rule.wrongForm);
    mRules[key] = rule;
    mRuleList.push_back(rule);
    mNeedsSorting = true;
}

void AutocorrectRules::addRule(const std::string& wrongForm, const std::string& correctForm,
                               bool caseSensitive, bool wordBoundary, int priority) {
    addRule(AutocorrectRule(wrongForm, correctForm, caseSensitive, wordBoundary, priority));
}

void AutocorrectRules::removeRule(const std::string& wrongForm) {
    std::string key = normalizeForLookup(wrongForm);
    auto it = mRules.find(key);
    if (it != mRules.end()) {
        // Remove from list too
        mRuleList.erase(
            std::remove_if(mRuleList.begin(), mRuleList.end(),
                [&key](const AutocorrectRule& r) {
                    return normalizeForLookup(r.wrongForm) == key;
                }),
            mRuleList.end());
        mRules.erase(it);
    }
}

void AutocorrectRules::clearRules() {
    mRules.clear();
    mRuleList.clear();
    mNeedsSorting = false;
}

std::string AutocorrectRules::apply(const std::string& word) const {
    if (word.empty() || mRules.empty()) {
        return word;
    }
    
    std::string key = normalizeForLookup(word);
    auto it = mRules.find(key);
    
    if (it != mRules.end()) {
        const AutocorrectRule& rule = it->second;
        
        // Check case sensitivity
        if (rule.caseSensitive && word != rule.wrongForm) {
            return word;
        }
        
        return rule.correctForm;
    }
    
    return word;
}

bool AutocorrectRules::applyToCodePoints(const int* codePoints, int length,
                                         int* outCodePoints, int& outLength) const {
    if (!codePoints || length <= 0 || !outCodePoints) {
        return false;
    }
    
    std::string word = codePointsToString(codePoints, length);
    std::string result = apply(word);
    
    if (result == word) {
        // No change - copy input to output
        std::copy(codePoints, codePoints + length, outCodePoints);
        outLength = length;
        return false;
    }
    
    // Convert result back to code points
    std::vector<int> resultCodePoints;
    stringToCodePoints(result, resultCodePoints);
    
    outLength = static_cast<int>(resultCodePoints.size());
    std::copy(resultCodePoints.begin(), resultCodePoints.end(), outCodePoints);
    
    return true;
}

bool AutocorrectRules::hasRule(const std::string& word) const {
    std::string key = normalizeForLookup(word);
    return mRules.find(key) != mRules.end();
}

void AutocorrectRules::loadDefaultRussianRules() {
    // Common Russian misspellings
    addRule("придти", "прийти", false, true, 100);
    addRule("прийдти", "прийти", false, true, 100);
    addRule("вообщем", "в общем", false, true, 100);
    addRule("вобщем", "в общем", false, true, 100);
    addRule("вкурсе", "в курсе", false, true, 90);
    addRule("впринципе", "в принципе", false, true, 90);
    addRule("вследствии", "вследствие", false, true, 80);
    addRule("врядли", "вряд ли", false, true, 90);
    addRule("врятли", "вряд ли", false, true, 90);
    addRule("вкратце", "вкратце", false, true, 80);  // Already correct, for validation
    addRule("насчет", "насчёт", false, true, 70);
    addRule("ихний", "их", false, true, 85);
    addRule("ихняя", "их", false, true, 85);
    addRule("ложить", "класть", false, true, 80);
    addRule("одеть", "надеть", false, true, 75);  // Context-dependent, lower priority
    addRule("договора", "договоры", false, true, 70);
    addRule("крема", "кремы", false, true, 70);
    addRule("шофера", "шофёры", false, true, 70);
    addRule("тортЫ", "тОрты", false, true, 60);
    addRule("звОнит", "звонИт", false, true, 80);
    addRule("понЯл", "пОнял", false, true, 70);
    addRule("щас", "сейчас", false, true, 50);
    addRule("ваще", "вообще", false, true, 50);
    addRule("чо", "что", false, true, 40);
    addRule("чё", "что", false, true, 40);
    addRule("типо", "типа", false, true, 60);
    addRule("скока", "сколько", false, true, 50);
    addRule("ща", "сейчас", false, true, 40);
}

void AutocorrectRules::loadDefaultEnglishRules() {
    // Common English misspellings
    addRule("teh", "the", false, true, 100);
    addRule("thier", "their", false, true, 90);
    addRule("recieve", "receive", false, true, 90);
    addRule("seperate", "separate", false, true, 90);
    addRule("definately", "definitely", false, true, 90);
    addRule("occured", "occurred", false, true, 85);
    addRule("occurence", "occurrence", false, true, 85);
    addRule("accomodate", "accommodate", false, true, 85);
    addRule("acheive", "achieve", false, true, 85);
    addRule("aquire", "acquire", false, true, 85);
    addRule("apparant", "apparent", false, true, 80);
    addRule("arguement", "argument", false, true, 80);
    addRule("basicly", "basically", false, true, 80);
    addRule("beleive", "believe", false, true, 85);
    addRule("calender", "calendar", false, true, 80);
    addRule("collegue", "colleague", false, true, 80);
    addRule("concious", "conscious", false, true, 80);
    addRule("dissapear", "disappear", false, true, 80);
    addRule("embarass", "embarrass", false, true, 80);
    addRule("enviroment", "environment", false, true, 85);
    addRule("goverment", "government", false, true, 85);
    addRule("happend", "happened", false, true, 80);
    addRule("immediatlely", "immediately", false, true, 80);
    addRule("independant", "independent", false, true, 80);
    addRule("knowlege", "knowledge", false, true, 85);
    addRule("neccessary", "necessary", false, true, 85);
    addRule("noticable", "noticeable", false, true, 80);
    addRule("occassion", "occasion", false, true, 80);
    addRule("posession", "possession", false, true, 80);
    addRule("priviledge", "privilege", false, true, 80);
    addRule("publically", "publicly", false, true, 75);
    addRule("recomend", "recommend", false, true, 85);
    addRule("refered", "referred", false, true, 80);
    addRule("relevent", "relevant", false, true, 80);
    addRule("religous", "religious", false, true, 80);
    addRule("sucessful", "successful", false, true, 85);
    addRule("suprise", "surprise", false, true, 85);
    addRule("tommorow", "tomorrow", false, true, 90);
    addRule("truely", "truly", false, true, 80);
    addRule("untill", "until", false, true, 85);
    addRule("wierd", "weird", false, true, 85);
    
    // Common texting shortcuts (low priority)
    addRule("u", "you", false, true, 30);
    addRule("ur", "your", false, true, 30);
    addRule("r", "are", false, true, 25);
    addRule("y", "why", false, true, 25);
    addRule("b4", "before", false, true, 30);
    addRule("2day", "today", false, true, 30);
    addRule("2morrow", "tomorrow", false, true, 30);
}

std::string AutocorrectRules::normalizeForLookup(const std::string& word) {
    std::string result;
    result.reserve(word.size());
    
    for (unsigned char c : word) {
        // Convert to lowercase for case-insensitive lookup
        if (c >= 'A' && c <= 'Z') {
            result.push_back(c + 32);
        } else {
            result.push_back(c);
        }
    }
    
    return result;
}

std::string AutocorrectRules::codePointsToString(const int* codePoints, int length) {
    std::string result;
    result.reserve(length * 4);
    
    for (int i = 0; i < length; ++i) {
        int cp = codePoints[i];
        if (cp < 0x80) {
            result.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            result.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            result.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            result.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            result.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    
    return result;
}

void AutocorrectRules::stringToCodePoints(const std::string& str, 
                                          std::vector<int>& outCodePoints) {
    outCodePoints.clear();
    outCodePoints.reserve(str.size());
    
    size_t i = 0;
    while (i < str.size()) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        int cp;
        
        if ((c & 0x80) == 0) {
            cp = c;
            i += 1;
        } else if ((c & 0xE0) == 0xC0) {
            cp = (c & 0x1F) << 6;
            if (i + 1 < str.size()) {
                cp |= (static_cast<unsigned char>(str[i + 1]) & 0x3F);
            }
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            cp = (c & 0x0F) << 12;
            if (i + 1 < str.size()) {
                cp |= (static_cast<unsigned char>(str[i + 1]) & 0x3F) << 6;
            }
            if (i + 2 < str.size()) {
                cp |= (static_cast<unsigned char>(str[i + 2]) & 0x3F);
            }
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            cp = (c & 0x07) << 18;
            if (i + 1 < str.size()) {
                cp |= (static_cast<unsigned char>(str[i + 1]) & 0x3F) << 12;
            }
            if (i + 2 < str.size()) {
                cp |= (static_cast<unsigned char>(str[i + 2]) & 0x3F) << 6;
            }
            if (i + 3 < str.size()) {
                cp |= (static_cast<unsigned char>(str[i + 3]) & 0x3F);
            }
            i += 4;
        } else {
            // Invalid UTF-8, skip
            i += 1;
            continue;
        }
        
        outCodePoints.push_back(cp);
    }
}

void AutocorrectRules::sortRulesByPriority() {
    if (!mNeedsSorting) return;
    
    std::sort(mRuleList.begin(), mRuleList.end(),
        [](const AutocorrectRule& a, const AutocorrectRule& b) {
            return a.priority > b.priority;
        });
    
    mNeedsSorting = false;
}

} // namespace latinime
