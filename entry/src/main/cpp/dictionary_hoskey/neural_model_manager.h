/**
 * Neural Model Manager
 *
 * Manages all 15 MindSpore neural models for Yandex keyboard:
 *
 * TAP RANKING:
 *   - tap_model_ranker.ms    - Primary tap input ranking
 *   - tap_model_ranker_v2.ms - Improved tap ranking
 *   - ranker.ms              - General ranking
 *   - ranker_v2.ms           - Improved general ranking
 *
 * SWIPE RANKING:
 *   - ranker_swipe.ms        - Swipe-specific ranking
 *   - ranker_swipe_v2.ms     - Improved swipe ranking
 *   - swipe_blocker.ms       - Swipe vs tap detection
 *
 * LANGUAGE MODELS:
 *   - nnlm_model.ms          - Neural language model (context)
 *   - neural_model.ms        - Neural predictions
 *   - char_model.ms          - Character-level model
 *
 * AUTOCORRECT & MORPHOLOGY:
 *   - tree_autocorrect_model.ms - Autocorrection
 *   - lemmer_mhash.ms           - Lemmatization/morphology
 *
 * EMOJI:
 *   - emoji_suggest.ms       - Emoji suggestions
 *   - search_emoji_model.ms  - Emoji search
 *
 * EXPERIMENTAL:
 *   - ranker_exp.ms          - Experimental ranking
 */

#pragma once

#include "nnrt_scorer.h"
#include <string>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <vector>

namespace yandex {

// Model types enum
enum class ModelType {
    // Tap ranking
    TAP_RANKER,
    TAP_RANKER_V2,
    RANKER,
    RANKER_V2,
    RANKER_EXP,

    // Swipe ranking
    SWIPE_RANKER,
    SWIPE_RANKER_V2,
    SWIPE_BLOCKER,

    // Language models
    NNLM,
    NEURAL,
    CHAR_MODEL,

    // Autocorrect & morphology
    AUTOCORRECT,
    LEMMER,

    // Emoji
    EMOJI_SUGGEST,
    EMOJI_SEARCH,

    COUNT  // Number of model types
};

// Model file names
inline const char* GetModelFileName(ModelType type) {
    switch (type) {
        case ModelType::TAP_RANKER:      return "tap_model_ranker.ms";
        case ModelType::TAP_RANKER_V2:   return "tap_model_ranker_v2.ms";
        case ModelType::RANKER:          return "ranker.ms";
        case ModelType::RANKER_V2:       return "ranker_v2.ms";
        case ModelType::RANKER_EXP:      return "ranker_exp.ms";
        case ModelType::SWIPE_RANKER:    return "ranker_swipe.ms";
        case ModelType::SWIPE_RANKER_V2: return "ranker_swipe_v2.ms";
        case ModelType::SWIPE_BLOCKER:   return "swipe_blocker.ms";
        case ModelType::NNLM:            return "nnlm_model.ms";
        case ModelType::NEURAL:          return "neural_model.ms";
        case ModelType::CHAR_MODEL:      return "char_model.ms";
        case ModelType::AUTOCORRECT:     return "tree_autocorrect_model.ms";
        case ModelType::LEMMER:          return "lemmer_mhash.ms";
        case ModelType::EMOJI_SUGGEST:   return "emoji_suggest.ms";
        case ModelType::EMOJI_SEARCH:    return "search_emoji_model.ms";
        default: return "";
    }
}

/**
 * Neural Model Manager
 * Thread-safe manager for all neural models
 */
class NeuralModelManager {
public:
    NeuralModelManager();
    ~NeuralModelManager();

    /**
     * Set base directory for model files
     */
    void setModelsDirectory(const std::string& dir);

    /**
     * Load a specific model
     * @return true if loaded successfully
     */
    bool loadModel(ModelType type);

    /**
     * Load all models from the models directory
     * @return number of models loaded successfully
     */
    int loadAllModels();

    /**
     * Check if a model is loaded
     */
    bool isModelLoaded(ModelType type) const;

    /**
     * Get model count
     */
    int getLoadedModelCount() const;

    /**
     * Unload a specific model
     */
    void unloadModel(ModelType type);

    /**
     * Unload all models
     */
    void unloadAll();

    // ========================================================================
    // Scoring APIs for different use cases
    // ========================================================================

    /**
     * Score tap input suggestions
     * Uses: TAP_RANKER (primary), RANKER_V2 (fallback), NNLM (context)
     */
    std::vector<ScoredWord> scoreTapSuggestions(
        const std::vector<ScoringCandidate>& candidates,
        const std::string& context = ""
    );

    /**
     * Score swipe input suggestions
     * Uses: SWIPE_RANKER_V2 (primary), SWIPE_RANKER (fallback)
     */
    std::vector<ScoredWord> scoreSwipeSuggestions(
        const std::vector<ScoringCandidate>& candidates,
        const std::string& context = ""
    );

    /**
     * Check if input is swipe or tap
     * Uses: SWIPE_BLOCKER
     * @return probability that input is swipe (0.0-1.0)
     */
    float classifySwipeVsTap(
        const std::vector<float>& touchPoints,
        float duration
    );

    /**
     * Get autocorrection candidates
     * Uses: AUTOCORRECT, CHAR_MODEL
     */
    std::vector<ScoredWord> getAutocorrections(
        const std::string& word,
        const std::string& context = ""
    );

    /**
     * Get emoji suggestions for text
     * Uses: EMOJI_SUGGEST
     */
    std::vector<std::string> getEmojiSuggestions(
        const std::string& text,
        int limit = 5
    );

    /**
     * Search emoji by query
     * Uses: EMOJI_SEARCH
     */
    std::vector<std::string> searchEmoji(
        const std::string& query,
        int limit = 20
    );

    /**
     * Get lemma (base form) of a word
     * Uses: LEMMER
     */
    std::string getLemma(const std::string& word);

    /**
     * Get statistics about loaded models
     */
    struct ModelStats {
        int totalModels = 0;
        int loadedModels = 0;
        size_t totalMemory = 0;
        std::vector<std::string> loadedNames;
        std::vector<std::string> failedNames;
        std::string primaryDevice;
    };
    ModelStats getStats() const;

private:
    std::string modelsDir_;
    mutable std::mutex mutex_;

    // Model instances
    std::unique_ptr<NNRtScorer> models_[static_cast<int>(ModelType::COUNT)];
    bool loaded_[static_cast<int>(ModelType::COUNT)] = {false};

    // Helper to get model
    NNRtScorer* getModel(ModelType type) const;

    // Combine scores from multiple models
    std::vector<ScoredWord> combineScores(
        const std::vector<ScoredWord>& primary,
        const std::vector<ScoredWord>& secondary,
        float primaryWeight = 0.7f
    );
};

} // namespace yandex
