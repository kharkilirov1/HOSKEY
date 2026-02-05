/**
 * HOSKEY Keyboard - Neural Engine Interface
 *
 * Abstract interface for neural inference backends.
 * Implementations: MindSpore Lite, NNRt, Stub (fallback)
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#ifndef KEYBOARD_NATIVE_I_NEURAL_ENGINE_H
#define KEYBOARD_NATIVE_I_NEURAL_ENGINE_H

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <functional>

namespace keyboard {
namespace nn {

// ============================================================================
// Neural Engine Configuration
// ============================================================================

struct NeuralEngineConfig {
    std::string modelPath;          // Path to model file
    int numThreads = 2;             // Inference threads (0 = auto)
    bool useGpu = false;            // Use GPU/NPU if available
    bool enableFp16 = true;         // Use FP16 for faster inference
    int maxInferTimeMs = 20;        // Max inference time before timeout
    bool warmupOnLoad = true;       // Run warmup inference after load

    // Model-specific options
    int inputContextSize = 32;      // Max input context length
    int maxCandidates = 24;         // Max output candidates
};

// ============================================================================
// Input/Output Structures
// ============================================================================

/**
 * Feature vector for neural scoring
 */
struct NeuralFeatures {
    // Token IDs (vocabulary indices)
    std::vector<int32_t> tokenIds;

    // Touch coordinates (normalized 0.0-1.0)
    std::vector<float> touchX;
    std::vector<float> touchY;

    // Timing features (normalized)
    std::vector<float> touchTime;

    // Context embedding (from previous prediction)
    std::vector<float> contextEmbedding;

    // Meta features
    int contextLength = 0;
    int inputLength = 0;
    bool isGesture = false;
};

/**
 * Single scored candidate from neural model
 */
struct NeuralCandidate {
    std::string text;
    float score;            // Raw model score
    float confidence;       // Calibrated confidence [0, 1]
    int rank;               // Rank from model (0 = best)

    // Debug info
    std::vector<float> logits;  // Raw logits (optional)
};

/**
 * Neural inference result
 */
struct NeuralResult {
    std::vector<NeuralCandidate> candidates;
    float inferenceTimeMs;
    bool timedOut;
    bool usedFallback;
    std::string errorMessage;
};

// ============================================================================
// Neural Engine State
// ============================================================================

enum class NeuralEngineState {
    Unloaded,       // No model loaded
    Loading,        // Model is being loaded
    Ready,          // Model loaded and ready
    Inferring,      // Inference in progress
    Error           // Error state
};

// ============================================================================
// Neural Engine Interface
// ============================================================================

class INeuralEngine {
public:
    virtual ~INeuralEngine() = default;

    // ========================================================================
    // Lifecycle
    // ========================================================================

    /**
     * Load model from file
     * @param config Configuration with model path
     * @return true on success
     */
    virtual bool load(const NeuralEngineConfig& config) = 0;

    /**
     * Load model from memory buffer
     * @param data Model data
     * @param size Data size
     * @param config Configuration
     * @return true on success
     */
    virtual bool loadFromMemory(const void* data, size_t size,
                                const NeuralEngineConfig& config) = 0;

    /**
     * Unload model and free resources
     */
    virtual void unload() = 0;

    /**
     * Run warmup inference to initialize lazy resources
     * @return true on success
     */
    virtual bool warmup() = 0;

    // ========================================================================
    // Inference
    // ========================================================================

    /**
     * Score candidates based on input features
     * @param features Input features
     * @param candidates Candidate strings to score
     * @return Inference result
     */
    virtual NeuralResult infer(const NeuralFeatures& features,
                               const std::vector<std::string>& candidates) = 0;

    /**
     * Score candidates with timeout
     * @param features Input features
     * @param candidates Candidate strings to score
     * @param timeoutMs Maximum time in milliseconds
     * @return Inference result (may have timedOut=true)
     */
    virtual NeuralResult inferWithTimeout(const NeuralFeatures& features,
                                          const std::vector<std::string>& candidates,
                                          int timeoutMs) = 0;

    // ========================================================================
    // State & Info
    // ========================================================================

    /**
     * Check if model is loaded and ready
     */
    virtual bool isReady() const = 0;

    /**
     * Get current state
     */
    virtual NeuralEngineState getState() const = 0;

    /**
     * Get model info (name, version, etc.)
     */
    virtual std::string getModelInfo() const = 0;

    /**
     * Get memory usage in bytes
     */
    virtual size_t getMemoryUsage() const = 0;

    /**
     * Get last error message
     */
    virtual std::string getLastError() const = 0;

    // ========================================================================
    // Feature Building Helpers
    // ========================================================================

    /**
     * Convert word to token IDs using model's vocabulary
     * @param word Input word (UTF-8)
     * @return Token IDs
     */
    virtual std::vector<int32_t> tokenize(const std::string& word) const = 0;

    /**
     * Get vocabulary size
     */
    virtual int getVocabSize() const = 0;
};

// ============================================================================
// Factory
// ============================================================================

/**
 * Neural engine backend type
 */
enum class NeuralBackend {
    Auto,           // Auto-detect best available
    MindSpore,      // MindSpore Lite
    NNRt,           // HarmonyOS NNRt
    Stub            // Stub implementation (fallback)
};

/**
 * Create neural engine instance
 * @param backend Preferred backend
 * @return Engine instance (never null, may be stub)
 */
std::unique_ptr<INeuralEngine> createNeuralEngine(NeuralBackend backend = NeuralBackend::Auto);

/**
 * Check if backend is available on this platform
 */
bool isBackendAvailable(NeuralBackend backend);

/**
 * Get default backend for current platform
 */
NeuralBackend getDefaultBackend();

} // namespace nn
} // namespace keyboard

#endif // KEYBOARD_NATIVE_I_NEURAL_ENGINE_H
