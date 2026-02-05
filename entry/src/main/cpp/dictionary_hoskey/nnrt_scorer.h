/**
 * NNRt Scorer - Neural ranking using Neural Network Runtime + CANNKit
 * 
 * Uses OH_NN* APIs for inference with NPU acceleration on Huawei Kirin.
 * Models: .ms or .om format
 * 
 * CANNKit provides NPU-specific optimizations:
 *   - HMS_HiAIOptions_SetDeviceType (NPU/CPU)
 *   - HMS_HiAIOptions_SetFormatMode (NCHW/NHWC)
 *   - Dynamic shape support
 */

#ifndef NNRT_SCORER_H
#define NNRT_SCORER_H

#include <string>
#include <vector>
#include <cstdint>

namespace yandex {

/**
 * Candidate for scoring
 */
struct ScoringCandidate {
    std::string word;
    uint32_t wordId;
    float baseScore;  // From trie (frequency-based)
};

/**
 * Scored result
 */
struct ScoredWord {
    std::string word;
    float score;       // Combined score [0-1]
    float neuralScore; // From neural model [0-1]
    float freqScore;   // From frequency [0-1]
};

/**
 * Device type for inference
 */
enum class InferenceDevice {
    Auto,   // NPU if available, else CPU
    NPU,    // Force NPU (fails if unavailable)
    CPU     // Force CPU
};

/**
 * NNRt Scorer
 * 
 * Usage:
 *   NNRtScorer scorer;
 *   scorer.setDevice(InferenceDevice::Auto);  // optional
 *   scorer.loadModel("/data/storage/.../rawfile/ranker.ms");
 *   auto scored = scorer.score(candidates, "привет как");
 */
class NNRtScorer {
public:
    NNRtScorer();
    ~NNRtScorer();
    
    // Non-copyable
    NNRtScorer(const NNRtScorer&) = delete;
    NNRtScorer& operator=(const NNRtScorer&) = delete;
    
    /**
     * Set preferred device before loading model
     * Default: Auto (NPU if available)
     */
    void setDevice(InferenceDevice device);
    
    /**
     * Load model from file
     * Supports .ms (MindSpore) and .om (Offline Model) formats
     * @param modelPath Path to model file (from rawfile)
     * @return true if loaded successfully
     */
    bool loadModel(const std::string& modelPath);
    
    /**
     * Check if model is loaded
     */
    bool isLoaded() const { return loaded_; }
    
    /**
     * Check if running on NPU
     */
    bool isNpuActive() const { return npuActive_; }
    
    /**
     * Score candidates given context
     * @param candidates Word candidates from trie
     * @param context Previous words for language model
     * @return Scored and sorted results
     */
    std::vector<ScoredWord> score(
        const std::vector<ScoringCandidate>& candidates,
        const std::string& context = ""
    ) const;
    
    /**
     * Batch score for better NPU utilization
     * @param batchSize Number of candidates per batch (default: 16)
     */
    std::vector<ScoredWord> scoreBatch(
        const std::vector<ScoringCandidate>& candidates,
        const std::string& context = "",
        int batchSize = 16
    ) const;
    
    /**
     * Get app ID from package name (for context features)
     */
    static int getAppId(const std::string& packageName);
    
    /**
     * Unload model and release resources
     */
    void unload();
    
    /**
     * Get device info string
     */
    std::string getDeviceInfo() const;
    
private:
    bool loaded_ = false;
    bool npuActive_ = false;
    std::string modelPath_;
    InferenceDevice preferredDevice_ = InferenceDevice::Auto;
    
    // pImpl for NNRt internals
    class Impl;
    Impl* impl_;
};

// Backward compatibility alias
using MindSporeScorer = NNRtScorer;

} // namespace yandex

#endif // NNRT_SCORER_H
