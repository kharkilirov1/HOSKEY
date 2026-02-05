/**
 * Neural Model Manager Implementation
 */

#include "neural_model_manager.h"
#include <hilog/log.h>
#include <algorithm>
#include <cmath>

#undef LOG_DOMAIN
#undef LOG_TAG
#define LOG_DOMAIN 0x0001
#define LOG_TAG "HOSKEY-NEURAL"

namespace yandex {

NeuralModelManager::NeuralModelManager() {
    // Initialize all model pointers to null
    for (int i = 0; i < static_cast<int>(ModelType::COUNT); i++) {
        models_[i] = nullptr;
        loaded_[i] = false;
    }
}

NeuralModelManager::~NeuralModelManager() {
    unloadAll();
}

void NeuralModelManager::setModelsDirectory(const std::string& dir) {
    std::lock_guard<std::mutex> lock(mutex_);
    modelsDir_ = dir;
    // Ensure trailing slash
    if (!modelsDir_.empty() && modelsDir_.back() != '/') {
        modelsDir_ += '/';
    }
    OH_LOG_INFO(LOG_APP, "NeuralModelManager: models directory set to %{public}s", modelsDir_.c_str());
}

bool NeuralModelManager::loadModel(ModelType type) {
    std::lock_guard<std::mutex> lock(mutex_);

    int idx = static_cast<int>(type);
    if (idx < 0 || idx >= static_cast<int>(ModelType::COUNT)) {
        return false;
    }

    // Already loaded
    if (loaded_[idx] && models_[idx]) {
        return true;
    }

    // Get file path
    const char* fileName = GetModelFileName(type);
    if (!fileName || fileName[0] == '\0') {
        OH_LOG_ERROR(LOG_APP, "NeuralModelManager: unknown model type %{public}d", idx);
        return false;
    }

    std::string fullPath = modelsDir_ + fileName;

    // Create scorer instance
    if (!models_[idx]) {
        models_[idx] = std::make_unique<NNRtScorer>();
    }

    // Try to load
    OH_LOG_INFO(LOG_APP, "NeuralModelManager: loading %{public}s", fileName);

    if (models_[idx]->loadModel(fullPath)) {
        loaded_[idx] = true;
        std::string device = models_[idx]->getDeviceInfo();
        OH_LOG_INFO(LOG_APP, "NeuralModelManager: %{public}s loaded on %{public}s",
                    fileName, device.c_str());
        return true;
    } else {
        OH_LOG_WARN(LOG_APP, "NeuralModelManager: failed to load %{public}s", fileName);
        models_[idx].reset();
        loaded_[idx] = false;
        return false;
    }
}

int NeuralModelManager::loadAllModels() {
    int count = 0;

    // Load in priority order - most important models first
    // This ensures they get NPU if available

    // Primary ranking models
    if (loadModel(ModelType::TAP_RANKER)) count++;
    if (loadModel(ModelType::RANKER_V2)) count++;
    if (loadModel(ModelType::SWIPE_RANKER_V2)) count++;

    // Language models
    if (loadModel(ModelType::NNLM)) count++;
    if (loadModel(ModelType::NEURAL)) count++;

    // Autocorrect & morphology
    if (loadModel(ModelType::AUTOCORRECT)) count++;
    if (loadModel(ModelType::LEMMER)) count++;

    // Swipe detection
    if (loadModel(ModelType::SWIPE_BLOCKER)) count++;

    // Secondary ranking models
    if (loadModel(ModelType::TAP_RANKER_V2)) count++;
    if (loadModel(ModelType::RANKER)) count++;
    if (loadModel(ModelType::SWIPE_RANKER)) count++;
    if (loadModel(ModelType::RANKER_EXP)) count++;

    // Character model
    if (loadModel(ModelType::CHAR_MODEL)) count++;

    // Emoji models
    if (loadModel(ModelType::EMOJI_SUGGEST)) count++;
    if (loadModel(ModelType::EMOJI_SEARCH)) count++;

    OH_LOG_INFO(LOG_APP, "NeuralModelManager: loaded %{public}d/%{public}d models",
                count, static_cast<int>(ModelType::COUNT));

    return count;
}

bool NeuralModelManager::isModelLoaded(ModelType type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    int idx = static_cast<int>(type);
    if (idx < 0 || idx >= static_cast<int>(ModelType::COUNT)) {
        return false;
    }
    return loaded_[idx];
}

int NeuralModelManager::getLoadedModelCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    int count = 0;
    for (int i = 0; i < static_cast<int>(ModelType::COUNT); i++) {
        if (loaded_[i]) count++;
    }
    return count;
}

void NeuralModelManager::unloadModel(ModelType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    int idx = static_cast<int>(type);
    if (idx >= 0 && idx < static_cast<int>(ModelType::COUNT)) {
        if (models_[idx]) {
            models_[idx]->unload();
            models_[idx].reset();
        }
        loaded_[idx] = false;
    }
}

void NeuralModelManager::unloadAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < static_cast<int>(ModelType::COUNT); i++) {
        if (models_[i]) {
            models_[i]->unload();
            models_[i].reset();
        }
        loaded_[i] = false;
    }
    OH_LOG_INFO(LOG_APP, "NeuralModelManager: all models unloaded");
}

NNRtScorer* NeuralModelManager::getModel(ModelType type) const {
    int idx = static_cast<int>(type);
    if (idx >= 0 && idx < static_cast<int>(ModelType::COUNT) && loaded_[idx]) {
        return models_[idx].get();
    }
    return nullptr;
}

// ============================================================================
// Scoring APIs
// ============================================================================

std::vector<ScoredWord> NeuralModelManager::scoreTapSuggestions(
    const std::vector<ScoringCandidate>& candidates,
    const std::string& context
) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (candidates.empty()) {
        return {};
    }

    // Try primary model first
    NNRtScorer* primary = getModel(ModelType::TAP_RANKER);
    if (!primary) {
        primary = getModel(ModelType::RANKER_V2);
    }
    if (!primary) {
        primary = getModel(ModelType::RANKER);
    }

    std::vector<ScoredWord> results;

    if (primary) {
        results = primary->score(candidates, context);

        // Boost with NNLM if available (context-aware scoring)
        NNRtScorer* nnlm = getModel(ModelType::NNLM);
        if (nnlm && !context.empty()) {
            auto contextScores = nnlm->score(candidates, context);
            results = combineScores(results, contextScores, 0.7f);
        }
    } else {
        // Fallback: use base scores
        for (const auto& c : candidates) {
            ScoredWord sw;
            sw.word = c.word;
            sw.freqScore = c.baseScore;
            sw.neuralScore = 0.5f;
            sw.score = c.baseScore;
            results.push_back(sw);
        }
    }

    // Sort by score
    std::sort(results.begin(), results.end(),
              [](const ScoredWord& a, const ScoredWord& b) {
                  return a.score > b.score;
              });

    return results;
}

std::vector<ScoredWord> NeuralModelManager::scoreSwipeSuggestions(
    const std::vector<ScoringCandidate>& candidates,
    const std::string& context
) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (candidates.empty()) {
        return {};
    }

    // Try swipe-specific models first
    NNRtScorer* primary = getModel(ModelType::SWIPE_RANKER_V2);
    if (!primary) {
        primary = getModel(ModelType::SWIPE_RANKER);
    }
    if (!primary) {
        // Fallback to general ranker
        primary = getModel(ModelType::TAP_RANKER);
    }

    std::vector<ScoredWord> results;

    if (primary) {
        results = primary->score(candidates, context);
    } else {
        // Fallback: use base scores
        for (const auto& c : candidates) {
            ScoredWord sw;
            sw.word = c.word;
            sw.freqScore = c.baseScore;
            sw.neuralScore = 0.5f;
            sw.score = c.baseScore;
            results.push_back(sw);
        }
    }

    // Sort by score
    std::sort(results.begin(), results.end(),
              [](const ScoredWord& a, const ScoredWord& b) {
                  return a.score > b.score;
              });

    return results;
}

float NeuralModelManager::classifySwipeVsTap(
    const std::vector<float>& touchPoints,
    float duration
) {
    std::lock_guard<std::mutex> lock(mutex_);

    NNRtScorer* blocker = getModel(ModelType::SWIPE_BLOCKER);
    if (!blocker) {
        // Heuristic fallback: if many points and long duration, likely swipe
        if (touchPoints.size() > 10 && duration > 150.0f) {
            return 0.8f;  // Likely swipe
        }
        return 0.2f;  // Likely tap
    }

    // TODO: Implement proper feature extraction for swipe_blocker model
    // For now, use heuristic
    float totalDistance = 0.0f;
    for (size_t i = 2; i < touchPoints.size(); i += 2) {
        float dx = touchPoints[i] - touchPoints[i-2];
        float dy = touchPoints[i+1] - touchPoints[i-1];
        totalDistance += std::sqrt(dx*dx + dy*dy);
    }

    // Simple heuristic: distance / duration ratio
    float speed = duration > 0 ? totalDistance / duration : 0;
    if (speed > 0.5f && touchPoints.size() > 10) {
        return 0.9f;  // High confidence swipe
    } else if (speed > 0.2f) {
        return 0.6f;  // Moderate confidence swipe
    }
    return 0.2f;  // Likely tap
}

std::vector<ScoredWord> NeuralModelManager::getAutocorrections(
    const std::string& word,
    const std::string& context
) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<ScoredWord> results;

    NNRtScorer* autocorrect = getModel(ModelType::AUTOCORRECT);
    NNRtScorer* charModel = getModel(ModelType::CHAR_MODEL);

    if (!autocorrect && !charModel) {
        // No autocorrect models available
        return results;
    }

    // Create candidate from input word
    ScoringCandidate c;
    c.word = word;
    c.baseScore = 0.5f;

    std::vector<ScoringCandidate> candidates = {c};

    if (autocorrect) {
        results = autocorrect->score(candidates, context);
    }

    return results;
}

std::vector<std::string> NeuralModelManager::getEmojiSuggestions(
    const std::string& text,
    int limit
) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> results;

    NNRtScorer* emojiModel = getModel(ModelType::EMOJI_SUGGEST);
    if (!emojiModel) {
        // No emoji model available
        return results;
    }

    // TODO: Implement proper emoji suggestion using model
    // This requires understanding the model's input/output format

    return results;
}

std::vector<std::string> NeuralModelManager::searchEmoji(
    const std::string& query,
    int limit
) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> results;

    NNRtScorer* searchModel = getModel(ModelType::EMOJI_SEARCH);
    if (!searchModel) {
        return results;
    }

    // TODO: Implement proper emoji search using model

    return results;
}

std::string NeuralModelManager::getLemma(const std::string& word) {
    std::lock_guard<std::mutex> lock(mutex_);

    NNRtScorer* lemmer = getModel(ModelType::LEMMER);
    if (!lemmer) {
        return word;  // Return original word if no lemmer
    }

    // TODO: Implement lemmatization using model

    return word;
}

NeuralModelManager::ModelStats NeuralModelManager::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);

    ModelStats stats;
    stats.totalModels = static_cast<int>(ModelType::COUNT);

    for (int i = 0; i < static_cast<int>(ModelType::COUNT); i++) {
        const char* name = GetModelFileName(static_cast<ModelType>(i));
        if (loaded_[i] && models_[i]) {
            stats.loadedModels++;
            stats.loadedNames.push_back(name);
            if (stats.primaryDevice.empty()) {
                stats.primaryDevice = models_[i]->getDeviceInfo();
            }
        } else {
            stats.failedNames.push_back(name);
        }
    }

    return stats;
}

std::vector<ScoredWord> NeuralModelManager::combineScores(
    const std::vector<ScoredWord>& primary,
    const std::vector<ScoredWord>& secondary,
    float primaryWeight
) {
    if (secondary.empty()) {
        return primary;
    }

    // Build map for secondary scores
    std::unordered_map<std::string, float> secondaryScores;
    for (const auto& s : secondary) {
        secondaryScores[s.word] = s.score;
    }

    // Combine
    std::vector<ScoredWord> combined;
    combined.reserve(primary.size());

    float secondaryWeight = 1.0f - primaryWeight;

    for (const auto& p : primary) {
        ScoredWord c = p;
        auto it = secondaryScores.find(p.word);
        if (it != secondaryScores.end()) {
            c.score = p.score * primaryWeight + it->second * secondaryWeight;
        }
        combined.push_back(c);
    }

    return combined;
}

} // namespace yandex
