/**
 * Suggest Engine implementation
 * Weighted Levenshtein with proximity awareness
 */

#include "suggest_engine.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <sstream>
#include <chrono>

namespace hoskey {

// QWERTY keyboard proximity map
static const std::unordered_map<char, std::unordered_set<char>> kProximityMap = {
    {'q', {'w', 'a', 's'}},
    {'w', {'q', 'e', 'a', 's', 'd'}},
    {'e', {'w', 'r', 's', 'd', 'f'}},
    {'r', {'e', 't', 'd', 'f', 'g'}},
    {'t', {'r', 'y', 'f', 'g', 'h'}},
    {'y', {'t', 'u', 'g', 'h', 'j'}},
    {'u', {'y', 'i', 'h', 'j', 'k'}},
    {'i', {'u', 'o', 'j', 'k', 'l'}},
    {'o', {'i', 'p', 'k', 'l'}},
    {'p', {'o', 'l'}},
    {'a', {'q', 'w', 's', 'z', 'x'}},
    {'s', {'q', 'w', 'e', 'a', 'd', 'z', 'x', 'c'}},
    {'d', {'w', 'e', 'r', 's', 'f', 'x', 'c', 'v'}},
    {'f', {'e', 'r', 't', 'd', 'g', 'c', 'v', 'b'}},
    {'g', {'r', 't', 'y', 'f', 'h', 'v', 'b', 'n'}},
    {'h', {'t', 'y', 'u', 'g', 'j', 'b', 'n', 'm'}},
    {'j', {'y', 'u', 'i', 'h', 'k', 'n', 'm'}},
    {'k', {'u', 'i', 'o', 'j', 'l', 'm'}},
    {'l', {'i', 'o', 'p', 'k'}},
    {'z', {'a', 's', 'x'}},
    {'x', {'a', 's', 'd', 'z', 'c'}},
    {'c', {'s', 'd', 'f', 'x', 'v'}},
    {'v', {'d', 'f', 'g', 'c', 'b'}},
    {'b', {'f', 'g', 'h', 'v', 'n'}},
    {'n', {'g', 'h', 'j', 'b', 'm'}},
    {'m', {'h', 'j', 'k', 'n'}}
};

bool ProximityInfo::areProximate(char a, char b) const {
    auto it = kProximityMap.find(a);
    if (it != kProximityMap.end()) {
        return it->second.count(b) > 0;
    }
    return false;
}

SuggestEngine::SuggestEngine(TrieType* trie)
    : trie_(trie), proximityInfo_(nullptr), userTrie_(std::make_unique<Trie>()),
      userDictDirty_(false), bigramDictDirty_(false) {}

SuggestEngine::~SuggestEngine() = default;

void SuggestEngine::setProximityInfo(ProximityInfo* proximityInfo) {
    proximityInfo_ = proximityInfo;
}

std::vector<SuggestResult> SuggestEngine::getSuggestions(const std::string& input, int limit) {
    std::vector<SuggestResult> results;

    if (!trie_ || input.empty()) {
        return results;
    }

    // O(1) duplicate tracking instead of O(n²) nested loops
    std::unordered_set<std::string> addedWords;

    // Step 0: Get user dictionary matches first (highest priority)
    if (userTrie_ && userTrie_->getWordCount() > 0) {
        auto userMatches = userTrie_->findByPrefix(input, limit);
        for (const auto& entry : userMatches) {
            if (entry.frequency > 0) {  // Skip "deleted" words (freq=0)
                EditResult editResult;
                editResult.distance = 0;
                editResult.weightedCost = 0.0f;
                editResult.primaryError = ErrorType::NOT_AN_ERROR;

                // Boost user dictionary scores by 20%
                float score = calculateFinalScore(entry, editResult, input.length(), true) * 1.2f;
                results.emplace_back(entry.word, score, ErrorType::NOT_AN_ERROR, 0);
                addedWords.insert(entry.word);
            }
        }
    }

    // Step 1: Get exact prefix matches from main dictionary (fast path)
    // Reduced from limit*2 to limit+5 for better performance
    auto prefixMatches = trie_->findByPrefix(input, limit + 5);

    for (const auto& entry : prefixMatches) {
        // O(1) duplicate check
        if (addedWords.count(entry.word)) continue;

        EditResult editResult;
        editResult.distance = 0;
        editResult.weightedCost = 0.0f;
        editResult.primaryError = ErrorType::NOT_AN_ERROR;

        float score = calculateFinalScore(entry, editResult, input.length(), true);
        results.emplace_back(entry.word, score, ErrorType::NOT_AN_ERROR, 0);
        addedWords.insert(entry.word);
    }

    // Step 2: If not enough results, search with corrections
    if (results.size() < static_cast<size_t>(limit) && input.length() >= 2) {
        // Reduced from 500 to 100 candidates for performance
        char firstChar = input[0];
        auto candidates = trie_->getWordsByFirstLetter(firstChar, 100);

        int maxEditDistance = std::min(2, static_cast<int>(input.length()) / 3 + 1);

        for (const auto& candidate : candidates) {
            // O(1) duplicate check
            if (addedWords.count(candidate)) continue;

            // Skip if length difference too big
            int lengthDiff = std::abs(static_cast<int>(candidate.length()) -
                                     static_cast<int>(input.length()));
            if (lengthDiff > maxEditDistance) continue;

            // Calculate weighted distance
            EditResult editResult = calculateWeightedDistance(input, candidate);

            if (editResult.distance <= maxEditDistance) {
                int frequency = trie_->getFrequency(candidate);
                WordEntry entry(candidate, frequency);

                float score = calculateFinalScore(entry, editResult, input.length(), false);

                // Only add if score is reasonable
                if (score > 0.1f) {
                    results.emplace_back(candidate, score, editResult.primaryError,
                                        editResult.distance);
                    addedWords.insert(candidate);
                }
            }
        }
    }

    // Sort by score (descending)
    std::sort(results.begin(), results.end(),
              [](const SuggestResult& a, const SuggestResult& b) {
                  return a.score > b.score;
              });

    // Limit results
    if (results.size() > static_cast<size_t>(limit)) {
        results.resize(limit);
    }

    return results;
}

SuggestResult SuggestEngine::findAutocorrection(const std::string& word, float threshold) {
    SuggestResult best;

    if (!trie_ || word.empty() || word.length() < 2) {
        return best;
    }

    // Don't autocorrect if word is in dictionary
    if (trie_->contains(word)) {
        return best;
    }

    // Get suggestions
    auto suggestions = getSuggestions(word, 5);

    for (const auto& suggestion : suggestions) {
        // Skip exact match (shouldn't happen, but be safe)
        if (suggestion.word == word) continue;

        // Check if suggestion exceeds threshold
        if (suggestion.score >= threshold && suggestion.score > best.score) {
            best = suggestion;
        }
    }

    return best;
}

bool SuggestEngine::shouldAutocorrect(const std::string& input, const SuggestResult& suggestion) {
    // Don't autocorrect single character
    if (input.length() < 2) return false;

    // Don't autocorrect if input is already valid
    if (trie_ && trie_->contains(input)) return false;

    // Check score threshold
    if (suggestion.score < ScoringParams::AUTOCORRECTION_THRESHOLD) return false;

    // Check plausibility (secondary threshold)
    if (suggestion.score < ScoringParams::PLAUSIBILITY_THRESHOLD &&
        suggestion.editDistance > 1) {
        return false;
    }

    // Don't autocorrect short words with multiple errors
    if (input.length() <= ScoringParams::THRESHOLD_SHORT_WORD_LENGTH &&
        suggestion.editDistance > 1) {
        return false;
    }

    return true;
}

SuggestEngine::EditResult SuggestEngine::calculateWeightedDistance(
        const std::string& input, const std::string& word) {
    EditResult result;
    result.distance = levenshteinDistance(input, word, 3);
    result.primaryError = ErrorType::NOT_AN_ERROR;
    result.weightedCost = 0.0f;

    if (result.distance == 0) {
        return result;
    }

    // Determine primary error type based on analysis
    size_t minLen = std::min(input.length(), word.length());

    // Check for transposition
    if (input.length() == word.length() && result.distance == 2) {
        for (size_t i = 0; i + 1 < minLen; i++) {
            if (input[i] == word[i + 1] && input[i + 1] == word[i]) {
                result.primaryError = ErrorType::TRANSPOSITION_CORRECTION;
                result.weightedCost = ScoringParams::TRANSPOSITION_COST;
                return result;
            }
        }
    }

    // Check for proximity error
    if (result.distance == 1 && proximityInfo_) {
        for (size_t i = 0; i < minLen; i++) {
            if (input[i] != word[i]) {
                if (areProximate(input[i], word[i])) {
                    result.primaryError = ErrorType::PROXIMITY_CORRECTION;
                    result.weightedCost = (i == 0)
                        ? ScoringParams::FIRST_CHAR_PROXIMITY_COST
                        : ScoringParams::PROXIMITY_COST;
                    return result;
                }
            }
        }
    }

    // Determine insertion/omission/substitution
    if (input.length() > word.length()) {
        result.primaryError = ErrorType::INSERTION_CORRECTION;
        result.weightedCost = ScoringParams::INSERTION_COST * result.distance;
    } else if (input.length() < word.length()) {
        result.primaryError = ErrorType::OMISSION_CORRECTION;
        result.weightedCost = ScoringParams::OMISSION_COST * result.distance;
    } else {
        result.primaryError = ErrorType::SUBSTITUTION_CORRECTION;
        result.weightedCost = ScoringParams::SUBSTITUTION_COST * result.distance;
    }

    return result;
}

int SuggestEngine::levenshteinDistance(const std::string& a, const std::string& b, int maxDistance) {
    if (a == b) return 0;

    int lengthDiff = std::abs(static_cast<int>(a.length()) - static_cast<int>(b.length()));
    if (lengthDiff > maxDistance) return maxDistance + 1;

    if (a.empty()) return std::min(static_cast<int>(b.length()), maxDistance + 1);
    if (b.empty()) return std::min(static_cast<int>(a.length()), maxDistance + 1);

    // Use vector instead of 2D array for better cache performance
    std::vector<int> prev(b.length() + 1);
    std::vector<int> curr(b.length() + 1);

    // Initialize first row
    for (size_t j = 0; j <= b.length(); j++) {
        prev[j] = j;
    }

    // Fill matrix with early exit
    for (size_t i = 1; i <= a.length(); i++) {
        curr[0] = i;
        int minInRow = curr[0];

        for (size_t j = 1; j <= b.length(); j++) {
            if (a[i - 1] == b[j - 1]) {
                curr[j] = prev[j - 1];
            } else {
                curr[j] = 1 + std::min({prev[j - 1], prev[j], curr[j - 1]});
            }
            minInRow = std::min(minInRow, curr[j]);
        }

        // Early exit if minimum exceeds threshold
        if (minInRow > maxDistance) {
            return maxDistance + 1;
        }

        std::swap(prev, curr);
    }

    return prev[b.length()];
}

bool SuggestEngine::areProximate(char a, char b) {
    if (!proximityInfo_) return false;

    // Convert to lowercase for comparison
    char aLower = (a >= 'A' && a <= 'Z') ? a + 32 : a;
    char bLower = (b >= 'A' && b <= 'Z') ? b + 32 : b;

    return proximityInfo_->areProximate(aLower, bLower);
}

float SuggestEngine::calculateFinalScore(const WordEntry& entry, const EditResult& editResult,
                                         int inputLength, bool exactPrefix) {
    float score = 0.0f;

    // Base score from frequency (normalized to 0-1)
    float freqScore = static_cast<float>(entry.frequency) / 255.0f;

    if (exactPrefix) {
        // Exact prefix match gets high score
        score = freqScore * ScoringParams::EXACT_MATCH_PROMOTION;

        // Bonus for exact length match
        if (entry.word.length() == static_cast<size_t>(inputLength)) {
            score *= ScoringParams::PERFECT_MATCH_PROMOTION;
        }
    } else {
        // Correction case - apply penalties
        float penalty = editResult.weightedCost;

        // Distance penalty
        float distancePenalty = static_cast<float>(editResult.distance) *
                               ScoringParams::DISTANCE_WEIGHT_LENGTH;

        score = freqScore * (1.0f - penalty - distancePenalty);

        // Ensure non-negative
        score = std::max(0.0f, score);
    }

    // Length similarity bonus
    int wordLength = static_cast<int>(entry.word.length());
    float lengthRatio = static_cast<float>(std::min(inputLength, wordLength)) /
                       static_cast<float>(std::max(inputLength, wordLength));
    score *= (0.5f + 0.5f * lengthRatio);

    return score;
}

// =========================================================================
// User Learning Implementation
// =========================================================================

bool SuggestEngine::addLearnedWord(const std::string& word, int frequency) {
    if (word.empty() || word.length() < 2) {
        return false;
    }

    // Normalize to lowercase
    std::string normalized = word;
    for (char& c : normalized) {
        if (c >= 'A' && c <= 'Z') {
            c = c + 32;
        }
    }

    // Insert into user trie with high frequency
    userTrie_->insert(normalized, frequency);
    userDictDirty_ = true;
    return true;
}

bool SuggestEngine::recordWordUsage(const std::string& word) {
    if (word.empty()) {
        return false;
    }

    // Normalize to lowercase
    std::string normalized = word;
    for (char& c : normalized) {
        if (c >= 'A' && c <= 'Z') {
            c = c + 32;
        }
    }

    // Check if word exists in user dictionary
    int currentFreq = userTrie_->getFrequency(normalized);
    if (currentFreq > 0) {
        // Boost frequency (cap at 255)
        int newFreq = std::min(255, currentFreq + 10);
        userTrie_->insert(normalized, newFreq);
        userDictDirty_ = true;
        return true;
    }

    // Check if word exists in main dictionary
    if (trie_ && trie_->contains(normalized)) {
        // Add to user dict with boosted frequency
        int mainFreq = trie_->getFrequency(normalized);
        int userFreq = std::min(255, mainFreq + 50);
        userTrie_->insert(normalized, userFreq);
        userDictDirty_ = true;
        return true;
    }

    return false;
}

bool SuggestEngine::saveUserDict(const std::string& path) {
    if (!userTrie_) {
        return false;
    }

    // Use Trie's built-in serialization
    bool success = userTrie_->saveToFile(path);
    if (success) {
        userDictDirty_ = false;
    }
    return success;
}

bool SuggestEngine::loadUserDict(const std::string& path) {
    auto newUserTrie = std::make_unique<Trie>();
    bool success = newUserTrie->loadFromFile(path);

    if (success) {
        userTrie_ = std::move(newUserTrie);
        userDictDirty_ = false;
        return true;
    }

    return false;
}

int SuggestEngine::getLearnedWordsCount() const {
    return userTrie_ ? userTrie_->getWordCount() : 0;
}

bool SuggestEngine::removeLearnedWord(const std::string& word) {
    if (!userTrie_ || word.empty()) {
        return false;
    }

    // Normalize to lowercase
    std::string normalized = word;
    for (char& c : normalized) {
        if (c >= 'A' && c <= 'Z') {
            c = c + 32;
        }
    }

    // Check if exists
    if (!userTrie_->contains(normalized)) {
        return false;
    }

    // Set frequency to 0 (effective removal - Trie doesn't support real deletion)
    userTrie_->insert(normalized, 0);
    userDictDirty_ = true;
    return true;
}

// =========================================================================
// Bigram-Aware Learning Implementation
// =========================================================================

void SuggestEngine::addLearnedWordWithContext(const std::string& word, const std::string& prevWord, int count) {
    if (word.empty() || count <= 0) {
        return;
    }

    // Normalize to lowercase
    std::string normalizedWord = word;
    for (char& c : normalizedWord) {
        if (c >= 'A' && c <= 'Z') {
            c = c + 32;
        }
    }

    std::string normalizedPrev = prevWord;
    for (char& c : normalizedPrev) {
        if (c >= 'A' && c <= 'Z') {
            c = c + 32;
        }
    }

    std::lock_guard<std::mutex> lock(learnedWordsMutex_);

    // Find or create entry
    auto it = learnedWords_.find(normalizedWord);
    if (it == learnedWords_.end()) {
        LearnedWordEntry entry(normalizedWord);
        entry.totalCount = count;
        entry.lastUsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        if (!normalizedPrev.empty()) {
            entry.prevWordCounts[normalizedPrev] = count;
        }
        
        learnedWords_[normalizedWord] = std::move(entry);
    } else {
        it->second.totalCount += count;
        it->second.lastUsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        if (!normalizedPrev.empty()) {
            it->second.prevWordCounts[normalizedPrev] += count;
        }
    }

    bigramDictDirty_ = true;
}

int SuggestEngine::getLearnedBoost(const std::string& word, const std::string& prevWord) {
    if (word.empty()) {
        return 0;
    }

    // Normalize to lowercase
    std::string normalizedWord = word;
    for (char& c : normalizedWord) {
        if (c >= 'A' && c <= 'Z') {
            c = c + 32;
        }
    }

    std::string normalizedPrev = prevWord;
    for (char& c : normalizedPrev) {
        if (c >= 'A' && c <= 'Z') {
            c = c + 32;
        }
    }

    std::lock_guard<std::mutex> lock(learnedWordsMutex_);

    auto it = learnedWords_.find(normalizedWord);
    if (it == learnedWords_.end()) {
        return 0;
    }

    const LearnedWordEntry& entry = it->second;

    // Base boost from total count (logarithmic scaling, capped)
    int baseBoost = std::min(50, static_cast<int>(std::log2(entry.totalCount + 1) * 10));

    // Context boost if prevWord matches
    int contextBoost = 0;
    if (!normalizedPrev.empty()) {
        auto prevIt = entry.prevWordCounts.find(normalizedPrev);
        if (prevIt != entry.prevWordCounts.end()) {
            // Bigram match - significant boost
            contextBoost = std::min(100, prevIt->second * 20);
        }
    }

    // Recency boost (decay over time, max 30 days)
    int recencyBoost = 0;
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    int64_t ageMs = now - entry.lastUsed;
    int64_t maxAgeMs = 30LL * 24 * 60 * 60 * 1000;  // 30 days
    
    if (ageMs < maxAgeMs) {
        // Linear decay from 20 to 0 over 30 days
        recencyBoost = static_cast<int>(20.0 * (1.0 - static_cast<double>(ageMs) / maxAgeMs));
    }

    return baseBoost + contextBoost + recencyBoost;
}

// Simple JSON escape function
static std::string escapeJson(const std::string& str) {
    std::string result;
    result.reserve(str.length() + 10);
    for (char c : str) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n";  break;
            case '\r': result += "\\r";  break;
            case '\t': result += "\\t";  break;
            default:   result += c;      break;
        }
    }
    return result;
}

bool SuggestEngine::saveUserDictionary(const std::string& path) {
    std::lock_guard<std::mutex> lock(learnedWordsMutex_);

    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }

    // Write JSON format
    file << "{\n";
    file << "  \"version\": 1,\n";
    file << "  \"words\": {\n";

    bool firstWord = true;
    for (const auto& pair : learnedWords_) {
        const LearnedWordEntry& entry = pair.second;
        
        if (!firstWord) {
            file << ",\n";
        }
        firstWord = false;

        file << "    \"" << escapeJson(entry.word) << "\": {\n";
        file << "      \"totalCount\": " << entry.totalCount << ",\n";
        file << "      \"lastUsed\": " << entry.lastUsed << ",\n";
        file << "      \"prevWords\": {";

        bool firstPrev = true;
        for (const auto& prevPair : entry.prevWordCounts) {
            if (!firstPrev) {
                file << ", ";
            }
            firstPrev = false;
            file << "\"" << escapeJson(prevPair.first) << "\": " << prevPair.second;
        }

        file << "}\n";
        file << "    }";
    }

    file << "\n  }\n";
    file << "}\n";

    file.close();
    bigramDictDirty_ = false;
    return true;
}

// Simple JSON parsing helpers
static std::string readJsonString(const std::string& json, size_t& pos) {
    std::string result;
    if (pos >= json.length() || json[pos] != '"') return result;
    pos++;  // Skip opening quote
    
    while (pos < json.length() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.length()) {
            pos++;
            switch (json[pos]) {
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case '"': result += '"';  break;
                case '\\': result += '\\'; break;
                default: result += json[pos]; break;
            }
        } else {
            result += json[pos];
        }
        pos++;
    }
    if (pos < json.length()) pos++;  // Skip closing quote
    return result;
}

static int64_t readJsonNumber(const std::string& json, size_t& pos) {
    std::string numStr;
    while (pos < json.length() && (isdigit(json[pos]) || json[pos] == '-')) {
        numStr += json[pos];
        pos++;
    }
    return numStr.empty() ? 0 : std::stoll(numStr);
}

static void skipWhitespace(const std::string& json, size_t& pos) {
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\n' || 
           json[pos] == '\r' || json[pos] == '\t')) {
        pos++;
    }
}

bool SuggestEngine::loadUserDictionary(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    // Read entire file
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();
    file.close();

    std::lock_guard<std::mutex> lock(learnedWordsMutex_);
    learnedWords_.clear();

    // Simple state machine parser
    size_t pos = 0;
    
    // Find "words" section
    size_t wordsPos = json.find("\"words\"");
    if (wordsPos == std::string::npos) {
        return false;
    }
    
    pos = json.find('{', wordsPos + 7);  // Skip past "words":
    if (pos == std::string::npos) {
        return false;
    }
    pos++;  // Skip opening brace of words object

    // Parse each word entry
    while (pos < json.length()) {
        skipWhitespace(json, pos);
        
        if (json[pos] == '}') {
            break;  // End of words object
        }
        
        if (json[pos] == ',') {
            pos++;
            continue;
        }
        
        if (json[pos] != '"') {
            pos++;
            continue;
        }

        // Read word key
        std::string word = readJsonString(json, pos);
        if (word.empty()) continue;

        LearnedWordEntry entry(word);

        // Find entry object
        size_t entryStart = json.find('{', pos);
        if (entryStart == std::string::npos) break;
        pos = entryStart + 1;

        // Parse entry fields
        while (pos < json.length() && json[pos] != '}') {
            skipWhitespace(json, pos);
            
            if (json[pos] == ',') {
                pos++;
                continue;
            }
            
            if (json[pos] != '"') {
                pos++;
                continue;
            }

            std::string fieldName = readJsonString(json, pos);
            
            // Skip colon
            size_t colonPos = json.find(':', pos);
            if (colonPos == std::string::npos) break;
            pos = colonPos + 1;
            skipWhitespace(json, pos);

            if (fieldName == "totalCount") {
                entry.totalCount = static_cast<int>(readJsonNumber(json, pos));
            } else if (fieldName == "lastUsed") {
                entry.lastUsed = readJsonNumber(json, pos);
            } else if (fieldName == "prevWords") {
                // Parse prevWords object
                if (json[pos] == '{') {
                    pos++;
                    while (pos < json.length() && json[pos] != '}') {
                        skipWhitespace(json, pos);
                        if (json[pos] == ',') { pos++; continue; }
                        if (json[pos] != '"') { pos++; continue; }
                        
                        std::string prevWord = readJsonString(json, pos);
                        size_t prevColonPos = json.find(':', pos);
                        if (prevColonPos == std::string::npos) break;
                        pos = prevColonPos + 1;
                        skipWhitespace(json, pos);
                        int prevCount = static_cast<int>(readJsonNumber(json, pos));
                        
                        if (!prevWord.empty()) {
                            entry.prevWordCounts[prevWord] = prevCount;
                        }
                    }
                    if (pos < json.length()) pos++;  // Skip closing brace
                }
            }
        }
        
        if (pos < json.length() && json[pos] == '}') {
            pos++;  // Skip closing brace of entry
        }

        if (!word.empty()) {
            learnedWords_[word] = std::move(entry);
        }
    }

    bigramDictDirty_ = false;
    return true;
}

void SuggestEngine::clearLearnedWords() {
    std::lock_guard<std::mutex> lock(learnedWordsMutex_);
    learnedWords_.clear();
    bigramDictDirty_ = true;
}

int SuggestEngine::getBigramLearnedWordsCount() const {
    std::lock_guard<std::mutex> lock(learnedWordsMutex_);
    return static_cast<int>(learnedWords_.size());
}

} // namespace hoskey
