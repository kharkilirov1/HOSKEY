/**
 * Suggest Engine for HOSKEY
 * Combines Trie lookup with weighted error correction
 *
 * Features:
 * - Proximity-aware correction
 * - Multiple error type handling
 * - Optimized for <5ms response time
 */

#ifndef HOSKEY_SUGGEST_ENGINE_H
#define HOSKEY_SUGGEST_ENGINE_H

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include "trie.h"

// Feature flag: Use optimized pooled trie
#ifndef USE_POOLED_TRIE
#define USE_POOLED_TRIE 1
#endif

#if USE_POOLED_TRIE
#include "trie_pooled.h"
#endif

namespace hoskey {

// Type alias for trie based on feature flag
#if USE_POOLED_TRIE
using TrieType = TriePooled;
#else
using TrieType = Trie;
#endif

/**
 * Error types for correction detection
 */
enum class ErrorType {
    NOT_AN_ERROR,
    PROXIMITY_CORRECTION,      // Key near the intended key
    SUBSTITUTION_CORRECTION,   // Wrong character
    OMISSION_CORRECTION,       // Missing character
    INSERTION_CORRECTION,      // Extra character
    TRANSPOSITION_CORRECTION   // Swapped characters
};

/**
 * Scoring parameters for suggestion ranking
 */
struct ScoringParams {
    static constexpr float AUTOCORRECTION_THRESHOLD = 0.185f;
    static constexpr float PLAUSIBILITY_THRESHOLD = 0.15f;
    static constexpr int THRESHOLD_SHORT_WORD_LENGTH = 3;
    static constexpr float TRANSPOSITION_COST = 0.1f;
    static constexpr float FIRST_CHAR_PROXIMITY_COST = 0.2f;
    static constexpr float PROXIMITY_COST = 0.1f;
    static constexpr float INSERTION_COST = 0.15f;
    static constexpr float OMISSION_COST = 0.15f;
    static constexpr float SUBSTITUTION_COST = 0.2f;
    static constexpr float EXACT_MATCH_PROMOTION = 1.2f;
    static constexpr float PERFECT_MATCH_PROMOTION = 1.1f;
    static constexpr float DISTANCE_WEIGHT_LENGTH = 0.1f;
};

/**
 * Simple proximity info for keyboard-aware correction
 */
class ProximityInfo {
public:
    bool areProximate(char a, char b) const;
};

/**
 * Result from suggestion engine
 */
struct SuggestResult {
    std::string word;
    float score;           // Combined score (0-1, higher = better)
    ErrorType errorType;   // Primary error type detected
    int editDistance;      // Raw edit distance

    SuggestResult() : score(0.0f), errorType(ErrorType::NOT_AN_ERROR), editDistance(0) {}
    SuggestResult(const std::string& w, float s, ErrorType e, int d)
        : word(w), score(s), errorType(e), editDistance(d) {}
};

/**
 * Learned word entry with bigram context support
 * Tracks word usage with previous word context for smarter predictions
 */
struct LearnedWordEntry {
    std::string word;
    std::unordered_map<std::string, int> prevWordCounts;  // prevWord → usage count
    int totalCount;      // Total usage count across all contexts
    int64_t lastUsed;    // Unix timestamp of last use (milliseconds)

    LearnedWordEntry() : totalCount(0), lastUsed(0) {}
    explicit LearnedWordEntry(const std::string& w) 
        : word(w), totalCount(0), lastUsed(0) {}
};

/**
 * Suggest Engine with weighted Levenshtein distance
 */
class SuggestEngine {
public:
    explicit SuggestEngine(TrieType* trie);
    ~SuggestEngine();

    // Set proximity info for keyboard-aware correction
    void setProximityInfo(ProximityInfo* proximityInfo);

    // Get suggestions for prefix
    std::vector<SuggestResult> getSuggestions(const std::string& input, int limit = 10);

    // Find best autocorrection candidate
    SuggestResult findAutocorrection(const std::string& word, float threshold = 0.185f);

    // Check if word should be autocorrected
    bool shouldAutocorrect(const std::string& input, const SuggestResult& suggestion);

    // =========================================================================
    // User Learning API (Legacy - Trie-based)
    // =========================================================================

    /**
     * Add a learned word to user dictionary (legacy)
     * @param word - Word to learn
     * @param frequency - Initial frequency (default: 200 = high priority)
     * @returns true if word was added or updated
     */
    bool addLearnedWord(const std::string& word, int frequency = 200);

    /**
     * Record word usage (boost frequency for existing word)
     * @param word - Word that was used
     * @returns true if word exists and was boosted
     */
    bool recordWordUsage(const std::string& word);

    /**
     * Save user dictionary to file (legacy Trie format)
     * @param path - File path to save to
     * @returns true on success
     */
    bool saveUserDict(const std::string& path);

    /**
     * Load user dictionary from file (legacy Trie format)
     * @param path - File path to load from
     * @returns true on success, false if file doesn't exist or is invalid
     */
    bool loadUserDict(const std::string& path);

    /**
     * Get learned words count
     */
    int getLearnedWordsCount() const;

    /**
     * Remove a word from user dictionary
     * @param word - Word to remove
     * @returns true if word was removed
     */
    bool removeLearnedWord(const std::string& word);

    // =========================================================================
    // Bigram-Aware Learning API (New)
    // =========================================================================

    /**
     * Add a learned word with bigram context
     * @param word - Word to learn
     * @param prevWord - Previous word (context), empty string for no context
     * @param count - Usage count to add (default: 1)
     */
    void addLearnedWordWithContext(const std::string& word, const std::string& prevWord, int count = 1);

    /**
     * Get learned boost for a word in context
     * Returns a score boost based on how often this word follows prevWord
     * @param word - Word to check
     * @param prevWord - Previous word context, empty for no context
     * @returns Boost value (0 = no boost, higher = more relevant)
     */
    int getLearnedBoost(const std::string& word, const std::string& prevWord);

    /**
     * Save user dictionary to JSON file (bigram-aware)
     * @param path - File path to save to
     * @returns true on success
     */
    bool saveUserDictionary(const std::string& path);

    /**
     * Load user dictionary from JSON file (bigram-aware)
     * @param path - File path to load from
     * @returns true on success
     */
    bool loadUserDictionary(const std::string& path);

    /**
     * Clear all learned words (bigram-aware)
     */
    void clearLearnedWords();

    /**
     * Get count of bigram-aware learned words
     */
    int getBigramLearnedWordsCount() const;

private:
    TrieType* trie_;
    ProximityInfo* proximityInfo_;

    // User dictionary (learned words) - Legacy Trie-based
    // Note: userTrie_ always uses original Trie (not TriePooled) because:
    // 1. User dictionary is small (~1000 words max)
    // 2. Trie has saveToFile() which TriePooled doesn't
    std::unique_ptr<Trie> userTrie_;
    bool userDictDirty_;  // True if changes need to be saved

    // Bigram-aware learned words storage
    std::unordered_map<std::string, LearnedWordEntry> learnedWords_;
    mutable std::mutex learnedWordsMutex_;  // Thread safety for learned words
    bool bigramDictDirty_;  // True if bigram dict needs saving

    // Weighted Levenshtein with error type detection
    struct EditResult {
        int distance;
        float weightedCost;
        ErrorType primaryError;
    };

    EditResult calculateWeightedDistance(const std::string& input, const std::string& word);

    // Simple Levenshtein for fallback
    int levenshteinDistance(const std::string& a, const std::string& b, int maxDistance = 3);

    // Check if two characters are proximate on keyboard
    bool areProximate(char a, char b);

    // Calculate final score combining all factors
    float calculateFinalScore(const WordEntry& entry, const EditResult& editResult,
                             int inputLength, bool exactPrefix);
};

} // namespace hoskey

#endif // HOSKEY_SUGGEST_ENGINE_H
