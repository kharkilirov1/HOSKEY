/**
 * HOSKEY Keyboard - MindSpore Lite Neural Engine Implementation
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#include "neural_engine_ms.h"
#include "../platform/harmony_log.h"

#include <chrono>
#include <fstream>
#include <cstring>
#include <algorithm>

#ifdef USE_MINDSPORE
#include "mindspore/lite/include/model.h"
#include "mindspore/lite/include/context.h"
#include "mindspore/lite/include/ms_tensor.h"
#endif

namespace keyboard {
namespace nn {

static constexpr const char* TAG = "NeuralEngineMS";

// ============================================================================
// Factory Implementation
// ============================================================================

std::unique_ptr<INeuralEngine> createNeuralEngine(NeuralBackend backend) {
    switch (backend) {
        case NeuralBackend::MindSpore:
#ifdef USE_MINDSPORE
            return std::make_unique<NeuralEngineMindSpore>();
#else
            platform::logWarn(TAG, "MindSpore not available, using stub");
            return std::make_unique<NeuralEngineStub>();
#endif

        case NeuralBackend::NNRt:
            // NNRt implementation would go here
            platform::logWarn(TAG, "NNRt not implemented, using stub");
            return std::make_unique<NeuralEngineStub>();

        case NeuralBackend::Stub:
            return std::make_unique<NeuralEngineStub>();

        case NeuralBackend::Auto:
        default:
#ifdef USE_MINDSPORE
            return std::make_unique<NeuralEngineMindSpore>();
#else
            return std::make_unique<NeuralEngineStub>();
#endif
    }
}

bool isBackendAvailable(NeuralBackend backend) {
    switch (backend) {
        case NeuralBackend::MindSpore:
#ifdef USE_MINDSPORE
            return true;
#else
            return false;
#endif
        case NeuralBackend::NNRt:
            return false;  // Not yet implemented
        case NeuralBackend::Stub:
            return true;
        case NeuralBackend::Auto:
            return true;
        default:
            return false;
    }
}

NeuralBackend getDefaultBackend() {
#ifdef USE_MINDSPORE
    return NeuralBackend::MindSpore;
#else
    return NeuralBackend::Stub;
#endif
}

// ============================================================================
// NeuralEngineMindSpore Implementation
// ============================================================================

NeuralEngineMindSpore::NeuralEngineMindSpore() {
    platform::logInfo(TAG, "NeuralEngineMindSpore created");
}

NeuralEngineMindSpore::~NeuralEngineMindSpore() {
    unload();
    platform::logInfo(TAG, "NeuralEngineMindSpore destroyed");
}

bool NeuralEngineMindSpore::load(const NeuralEngineConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ == NeuralEngineState::Ready) {
        platform::logWarn(TAG, "Model already loaded, unloading first");
        unload();
    }

    state_ = NeuralEngineState::Loading;
    config_ = config;

    // Read model file
    std::ifstream file(config.modelPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        lastError_ = "Failed to open model file: " + config.modelPath;
        platform::logError(TAG, lastError_.c_str());
        state_ = NeuralEngineState::Error;
        return false;
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(fileSize);
    if (!file.read(buffer.data(), fileSize)) {
        lastError_ = "Failed to read model file";
        platform::logError(TAG, lastError_.c_str());
        state_ = NeuralEngineState::Error;
        return false;
    }
    file.close();

    return loadModelInternal(buffer.data(), fileSize);
}

bool NeuralEngineMindSpore::loadFromMemory(const void* data, size_t size,
                                           const NeuralEngineConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ == NeuralEngineState::Ready) {
        unload();
    }

    state_ = NeuralEngineState::Loading;
    config_ = config;

    return loadModelInternal(data, size);
}

bool NeuralEngineMindSpore::loadModelInternal(const void* data, size_t size) {
#ifdef USE_MINDSPORE
    // Initialize context
    if (!initContext(config_)) {
        return false;
    }

    // Create model
    model_ = std::make_unique<mindspore::Model>();

    // Build model from buffer
    auto status = model_->Build(data, size, mindspore::kMindIR, context_.get());
    if (status != mindspore::kSuccess) {
        lastError_ = "Failed to build model: " + std::to_string(static_cast<int>(status));
        platform::logError(TAG, lastError_.c_str());
        state_ = NeuralEngineState::Error;
        return false;
    }

    // Validate model structure
    if (!validateModel()) {
        return false;
    }

    // Build vocabulary if embedded in model
    buildVocabulary();

    // Get memory usage
    modelMemoryUsage_ = size;  // Approximate

    // Warmup if requested
    if (config_.warmupOnLoad) {
        state_ = NeuralEngineState::Ready;
        if (!warmup()) {
            platform::logWarn(TAG, "Warmup failed, but model is loaded");
        }
    }

    state_ = NeuralEngineState::Ready;
    platform::logInfo(TAG, "Model loaded successfully, size=%zu bytes", size);
    return true;

#else
    // MindSpore not available
    lastError_ = "MindSpore Lite not compiled in";
    platform::logError(TAG, lastError_.c_str());
    state_ = NeuralEngineState::Error;
    return false;
#endif
}

bool NeuralEngineMindSpore::initContext(const NeuralEngineConfig& config) {
#ifdef USE_MINDSPORE
    context_ = std::make_unique<mindspore::Context>();

    // Set thread count
    int threads = config.numThreads;
    if (threads <= 0) {
        threads = std::min(4, static_cast<int>(std::thread::hardware_concurrency()));
    }
    context_->SetThreadNum(threads);

    // Set thread affinity (bind to big cores for performance)
    context_->SetThreadAffinity(1);  // 1 = big cores

    // Enable FP16 if requested
    if (config.enableFp16) {
        context_->SetEnableParallel(true);
    }

    // Add CPU device
    auto& deviceList = context_->MutableDeviceInfo();

    auto cpuDevice = std::make_shared<mindspore::CPUDeviceInfo>();
    cpuDevice->SetEnableFP16(config.enableFp16);
    deviceList.push_back(cpuDevice);

    // Add NPU device if GPU/NPU requested
    if (config.useGpu) {
        // NPU support for HiSilicon chips
        auto npuDevice = std::make_shared<mindspore::KirinNPUDeviceInfo>();
        npuDevice->SetFrequency(3);  // High frequency
        deviceList.push_back(npuDevice);
    }

    platform::logInfo(TAG, "Context initialized: threads=%d, fp16=%d, npu=%d",
                     threads, config.enableFp16 ? 1 : 0, config.useGpu ? 1 : 0);
    return true;

#else
    return false;
#endif
}

bool NeuralEngineMindSpore::validateModel() {
#ifdef USE_MINDSPORE
    // Get input tensor info
    auto inputs = model_->GetInputs();
    if (inputs.empty()) {
        lastError_ = "Model has no inputs";
        platform::logError(TAG, lastError_.c_str());
        state_ = NeuralEngineState::Error;
        return false;
    }

    inputInfo_.clear();
    for (const auto& tensor : inputs) {
        TensorInfo info;
        info.name = tensor.Name();
        info.shape = tensor.Shape();
        info.dataType = static_cast<int>(tensor.DataType());
        inputInfo_.push_back(info);

        platform::logInfo(TAG, "Input tensor: %s, shape=[%s]",
                         info.name.c_str(),
                         info.shape.empty() ? "dynamic" :
                         std::to_string(info.shape[0]).c_str());
    }

    // Get output tensor info
    auto outputs = model_->GetOutputs();
    if (outputs.empty()) {
        lastError_ = "Model has no outputs";
        platform::logError(TAG, lastError_.c_str());
        state_ = NeuralEngineState::Error;
        return false;
    }

    outputInfo_.clear();
    for (const auto& tensor : outputs) {
        TensorInfo info;
        info.name = tensor.Name();
        info.shape = tensor.Shape();
        info.dataType = static_cast<int>(tensor.DataType());
        outputInfo_.push_back(info);
    }

    return true;

#else
    return false;
#endif
}

bool NeuralEngineMindSpore::buildVocabulary() {
    // Vocabulary would typically be embedded in model or loaded from file
    // For now, use basic character-level tokenization

    // Common vocabulary for keyboard prediction
    // This would be loaded from model metadata in production
    vocab_.clear();
    reverseVocab_.clear();

    // Special tokens
    vocab_["<PAD>"] = 0;
    vocab_["<UNK>"] = 1;
    vocab_["<BOS>"] = 2;
    vocab_["<EOS>"] = 3;

    padTokenId_ = 0;
    unkTokenId_ = 1;

    reverseVocab_ = {"<PAD>", "<UNK>", "<BOS>", "<EOS>"};

    // Add basic ASCII and Cyrillic characters
    int id = 4;

    // ASCII lowercase
    for (char c = 'a'; c <= 'z'; ++c) {
        std::string s(1, c);
        vocab_[s] = id;
        reverseVocab_.push_back(s);
        id++;
    }

    // Cyrillic lowercase (UTF-8)
    const char* cyrillic[] = {
        "а", "б", "в", "г", "д", "е", "ё", "ж", "з", "и", "й", "к", "л", "м",
        "н", "о", "п", "р", "с", "т", "у", "ф", "х", "ц", "ч", "ш", "щ", "ъ",
        "ы", "ь", "э", "ю", "я"
    };
    for (const char* c : cyrillic) {
        vocab_[c] = id;
        reverseVocab_.push_back(c);
        id++;
    }

    platform::logInfo(TAG, "Built vocabulary with %d tokens", id);
    return true;
}

void NeuralEngineMindSpore::unload() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ == NeuralEngineState::Unloaded) {
        return;
    }

    state_ = NeuralEngineState::Unloaded;

#ifdef USE_MINDSPORE
    model_.reset();
    context_.reset();
    inputTensors_.clear();
    outputTensors_.clear();
#endif

    inputBuffer_.clear();
    outputBuffer_.clear();
    vocab_.clear();
    reverseVocab_.clear();
    modelMemoryUsage_ = 0;

    platform::logInfo(TAG, "Model unloaded");
}

bool NeuralEngineMindSpore::warmup() {
    if (state_ != NeuralEngineState::Ready) {
        return false;
    }

    platform::logInfo(TAG, "Running warmup inference...");

    // Create dummy features
    NeuralFeatures features;
    features.tokenIds = {1, 2, 3};  // UNK, BOS, EOS
    features.inputLength = 3;
    features.contextLength = 0;

    // Dummy candidates
    std::vector<std::string> candidates = {"test", "warm", "up"};

    // Run inference
    auto result = infer(features, candidates);

    if (!result.errorMessage.empty()) {
        platform::logWarn(TAG, "Warmup inference failed: %s", result.errorMessage.c_str());
        return false;
    }

    platform::logInfo(TAG, "Warmup complete, latency=%.2f ms", result.inferenceTimeMs);
    return true;
}

NeuralResult NeuralEngineMindSpore::infer(const NeuralFeatures& features,
                                          const std::vector<std::string>& candidates) {
    NeuralResult result;
    result.timedOut = false;
    result.usedFallback = false;

    if (state_ != NeuralEngineState::Ready) {
        result.errorMessage = "Model not ready";
        result.usedFallback = true;
        return result;
    }

#ifdef USE_MINDSPORE
    std::lock_guard<std::mutex> lock(mutex_);

    auto startTime = std::chrono::high_resolution_clock::now();

    state_ = NeuralEngineState::Inferring;

    // Prepare inputs
    if (!prepareInputs(features, candidates)) {
        state_ = NeuralEngineState::Ready;
        result.errorMessage = "Failed to prepare inputs";
        result.usedFallback = true;
        return result;
    }

    // Run inference
    if (!runInference()) {
        state_ = NeuralEngineState::Ready;
        result.errorMessage = "Inference failed: " + lastError_;
        result.usedFallback = true;
        return result;
    }

    // Extract results
    result = extractResults(candidates);

    state_ = NeuralEngineState::Ready;

    auto endTime = std::chrono::high_resolution_clock::now();
    result.inferenceTimeMs = std::chrono::duration<float, std::milli>(
        endTime - startTime).count();

    return result;

#else
    // MindSpore not available, return stub result
    result.usedFallback = true;
    result.inferenceTimeMs = 0.1f;

    float score = 1.0f;
    for (const auto& candidate : candidates) {
        NeuralCandidate nc;
        nc.text = candidate;
        nc.score = score;
        nc.confidence = score;
        nc.rank = static_cast<int>(result.candidates.size());
        result.candidates.push_back(nc);
        score *= 0.9f;
    }

    return result;
#endif
}

NeuralResult NeuralEngineMindSpore::inferWithTimeout(
    const NeuralFeatures& features,
    const std::vector<std::string>& candidates,
    int timeoutMs) {

    // Use std::async with timeout for deadline enforcement
    auto future = std::async(std::launch::async, [this, &features, &candidates]() {
        return infer(features, candidates);
    });

    auto status = future.wait_for(std::chrono::milliseconds(timeoutMs));

    if (status == std::future_status::timeout) {
        NeuralResult result;
        result.timedOut = true;
        result.usedFallback = true;
        result.inferenceTimeMs = static_cast<float>(timeoutMs);
        result.errorMessage = "Inference timed out";

        platform::logWarn(TAG, "Inference timed out after %d ms", timeoutMs);

        // Return stub results
        float score = 1.0f;
        for (const auto& candidate : candidates) {
            NeuralCandidate nc;
            nc.text = candidate;
            nc.score = score;
            nc.confidence = 0.5f;  // Low confidence due to timeout
            nc.rank = static_cast<int>(result.candidates.size());
            result.candidates.push_back(nc);
            score *= 0.9f;
        }

        return result;
    }

    return future.get();
}

bool NeuralEngineMindSpore::prepareInputs(const NeuralFeatures& features,
                                          const std::vector<std::string>& /*candidates*/) {
#ifdef USE_MINDSPORE
    auto inputs = model_->GetInputs();
    if (inputs.empty()) {
        lastError_ = "No input tensors";
        return false;
    }

    // Prepare token IDs input
    auto& inputTensor = inputs[0];
    auto inputSize = inputTensor.ElementNum();

    inputBuffer_.resize(inputSize);
    std::fill(inputBuffer_.begin(), inputBuffer_.end(), static_cast<float>(padTokenId_));

    // Copy token IDs (convert to float if needed)
    size_t copySize = std::min(features.tokenIds.size(), static_cast<size_t>(inputSize));
    for (size_t i = 0; i < copySize; ++i) {
        inputBuffer_[i] = static_cast<float>(features.tokenIds[i]);
    }

    // Copy to tensor
    std::memcpy(inputTensor.MutableData(), inputBuffer_.data(),
                inputBuffer_.size() * sizeof(float));

    return true;

#else
    (void)features;
    return true;
#endif
}

NeuralResult NeuralEngineMindSpore::extractResults(
    const std::vector<std::string>& candidates) {

    NeuralResult result;

#ifdef USE_MINDSPORE
    auto outputs = model_->GetOutputs();
    if (outputs.empty()) {
        result.errorMessage = "No output tensors";
        return result;
    }

    auto& outputTensor = outputs[0];
    auto outputSize = outputTensor.ElementNum();
    const float* outputData = static_cast<const float*>(outputTensor.Data());

    // Extract scores for each candidate
    for (size_t i = 0; i < candidates.size() && i < static_cast<size_t>(outputSize); ++i) {
        NeuralCandidate nc;
        nc.text = candidates[i];
        nc.score = outputData[i];

        // Apply softmax for confidence
        nc.confidence = 1.0f / (1.0f + std::exp(-nc.score));  // Sigmoid

        nc.rank = static_cast<int>(i);
        result.candidates.push_back(nc);
    }

    // Sort by score
    std::sort(result.candidates.begin(), result.candidates.end(),
              [](const NeuralCandidate& a, const NeuralCandidate& b) {
                  return a.score > b.score;
              });

    // Update ranks
    for (size_t i = 0; i < result.candidates.size(); ++i) {
        result.candidates[i].rank = static_cast<int>(i);
    }

#else
    // Stub: return candidates with decaying scores
    float score = 1.0f;
    for (const auto& candidate : candidates) {
        NeuralCandidate nc;
        nc.text = candidate;
        nc.score = score;
        nc.confidence = score;
        nc.rank = static_cast<int>(result.candidates.size());
        result.candidates.push_back(nc);
        score *= 0.9f;
    }
#endif

    return result;
}

bool NeuralEngineMindSpore::runInference() {
#ifdef USE_MINDSPORE
    auto status = model_->Predict(model_->GetInputs(), &outputTensors_);
    if (status != mindspore::kSuccess) {
        lastError_ = "Predict failed: " + std::to_string(static_cast<int>(status));
        return false;
    }
    return true;
#else
    return true;
#endif
}

bool NeuralEngineMindSpore::isReady() const {
    return state_ == NeuralEngineState::Ready;
}

NeuralEngineState NeuralEngineMindSpore::getState() const {
    return state_;
}

std::string NeuralEngineMindSpore::getModelInfo() const {
    std::string info = "MindSpore Lite Engine";
    info += "\n  Model: " + (modelName_.empty() ? "unknown" : modelName_);
    info += "\n  Version: " + (modelVersion_.empty() ? "unknown" : modelVersion_);
    info += "\n  State: " + std::to_string(static_cast<int>(state_.load()));
    info += "\n  Vocab size: " + std::to_string(vocab_.size());
    return info;
}

size_t NeuralEngineMindSpore::getMemoryUsage() const {
    return modelMemoryUsage_ + inputBuffer_.capacity() * sizeof(float) +
           outputBuffer_.capacity() * sizeof(float);
}

std::string NeuralEngineMindSpore::getLastError() const {
    return lastError_;
}

std::vector<int32_t> NeuralEngineMindSpore::tokenize(const std::string& word) const {
    std::vector<int32_t> tokens;

    // Character-level tokenization
    size_t i = 0;
    while (i < word.size()) {
        // Handle UTF-8 multi-byte characters
        size_t charLen = 1;
        unsigned char c = static_cast<unsigned char>(word[i]);

        if ((c & 0x80) == 0) {
            charLen = 1;
        } else if ((c & 0xE0) == 0xC0) {
            charLen = 2;
        } else if ((c & 0xF0) == 0xE0) {
            charLen = 3;
        } else if ((c & 0xF8) == 0xF0) {
            charLen = 4;
        }

        std::string ch = word.substr(i, charLen);

        auto it = vocab_.find(ch);
        if (it != vocab_.end()) {
            tokens.push_back(it->second);
        } else {
            tokens.push_back(unkTokenId_);
        }

        i += charLen;
    }

    return tokens;
}

int NeuralEngineMindSpore::getVocabSize() const {
    return static_cast<int>(vocab_.size());
}

} // namespace nn
} // namespace keyboard
