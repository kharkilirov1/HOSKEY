/**
 * HOSKEY Keyboard - Prediction Benchmark
 *
 * Measures p50/p95/p99 latency for prediction.
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <numeric>
#include <cstring>

#include "../include/types.h"
#include "../src/core/engine.h"

using namespace keyboard::core;

// ============================================================================
// Benchmark Configuration
// ============================================================================

constexpr int WARMUP_ITERATIONS = 100;
constexpr int BENCHMARK_ITERATIONS = 1000;

// Test inputs
const std::vector<std::string> TEST_INPUTS = {
    "h", "he", "hel", "hell", "hello",
    "п", "пр", "при", "прив", "приве", "привет",
    "a", "ab", "abc", "abcd",
    "test", "testing", "example"
};

// ============================================================================
// Percentile Calculation
// ============================================================================

template<typename T>
T percentile(std::vector<T>& data, int p) {
    if (data.empty()) return T();

    std::sort(data.begin(), data.end());
    size_t index = (data.size() * p) / 100;
    if (index >= data.size()) index = data.size() - 1;
    return data[index];
}

// ============================================================================
// Benchmark
// ============================================================================

void runBenchmark() {
    std::cout << "=== HOSKEY Keyboard Prediction Benchmark ===" << std::endl;
    std::cout << std::endl;

    // Initialize engine
    Engine engine;
    KBEngineConfig config;
    kb_config_init_default(&config);
    config.useNeural = 0;  // Disable neural for baseline benchmark
    config.enableCache = 0;  // Disable cache for accurate measurement

    auto err = engine.init(config);
    if (err != KB_OK) {
        std::cerr << "Failed to initialize engine" << std::endl;
        return;
    }

    std::cout << "Configuration:" << std::endl;
    std::cout << "  Warmup iterations:    " << WARMUP_ITERATIONS << std::endl;
    std::cout << "  Benchmark iterations: " << BENCHMARK_ITERATIONS << std::endl;
    std::cout << "  Test inputs:          " << TEST_INPUTS.size() << std::endl;
    std::cout << "  Neural enabled:       " << (config.useNeural ? "yes" : "no") << std::endl;
    std::cout << "  Cache enabled:        " << (config.enableCache ? "yes" : "no") << std::endl;
    std::cout << std::endl;

    // Warmup
    std::cout << "Warming up..." << std::endl;
    for (int i = 0; i < WARMUP_ITERATIONS; ++i) {
        for (const auto& input : TEST_INPUTS) {
            KBPredictContext ctx = {};
            ctx.inputText = input.c_str();
            ctx.inputLength = static_cast<uint32_t>(input.length());
            ctx.maxResults = 10;

            KBPredictResult result = {};
            engine.predict(ctx, result);

            // Free result
            if (result.candidates) {
                for (uint32_t j = 0; j < result.candidateCount; ++j) {
                    free(result.candidates[j].text);
                }
                delete[] result.candidates;
            }
            if (result.traceId) free(result.traceId);
        }
    }

    // Benchmark
    std::cout << "Running benchmark..." << std::endl;
    std::vector<uint64_t> latencies;
    latencies.reserve(BENCHMARK_ITERATIONS * TEST_INPUTS.size());

    uint64_t totalCandidates = 0;

    for (int i = 0; i < BENCHMARK_ITERATIONS; ++i) {
        for (const auto& input : TEST_INPUTS) {
            KBPredictContext ctx = {};
            ctx.inputText = input.c_str();
            ctx.inputLength = static_cast<uint32_t>(input.length());
            ctx.maxResults = 10;

            auto start = std::chrono::high_resolution_clock::now();

            KBPredictResult result = {};
            engine.predict(ctx, result);

            auto end = std::chrono::high_resolution_clock::now();

            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                end - start).count();

            latencies.push_back(static_cast<uint64_t>(duration));
            totalCandidates += result.candidateCount;

            // Free result
            if (result.candidates) {
                for (uint32_t j = 0; j < result.candidateCount; ++j) {
                    free(result.candidates[j].text);
                }
                delete[] result.candidates;
            }
            if (result.traceId) free(result.traceId);
        }
    }

    // Calculate statistics
    uint64_t sum = std::accumulate(latencies.begin(), latencies.end(), uint64_t(0));
    double avgUs = static_cast<double>(sum) / latencies.size();

    uint64_t minUs = *std::min_element(latencies.begin(), latencies.end());
    uint64_t maxUs = *std::max_element(latencies.begin(), latencies.end());

    std::vector<uint64_t> sorted = latencies;
    uint64_t p50Us = percentile(sorted, 50);
    uint64_t p95Us = percentile(sorted, 95);
    uint64_t p99Us = percentile(sorted, 99);

    double throughput = latencies.size() / (sum / 1e6);  // predictions per second

    // Print results
    std::cout << std::endl;
    std::cout << "=== Results ===" << std::endl;
    std::cout << std::endl;
    std::cout << "Latency (microseconds):" << std::endl;
    std::cout << "  Min:    " << minUs << " µs" << std::endl;
    std::cout << "  Avg:    " << static_cast<uint64_t>(avgUs) << " µs" << std::endl;
    std::cout << "  p50:    " << p50Us << " µs" << std::endl;
    std::cout << "  p95:    " << p95Us << " µs" << std::endl;
    std::cout << "  p99:    " << p99Us << " µs" << std::endl;
    std::cout << "  Max:    " << maxUs << " µs" << std::endl;
    std::cout << std::endl;
    std::cout << "Latency (milliseconds):" << std::endl;
    std::cout << "  p50:    " << (p50Us / 1000.0) << " ms" << std::endl;
    std::cout << "  p95:    " << (p95Us / 1000.0) << " ms" << std::endl;
    std::cout << "  p99:    " << (p99Us / 1000.0) << " ms" << std::endl;
    std::cout << std::endl;
    std::cout << "Performance:" << std::endl;
    std::cout << "  Throughput:     " << static_cast<uint64_t>(throughput)
              << " predictions/sec" << std::endl;
    std::cout << "  Total calls:    " << latencies.size() << std::endl;
    std::cout << "  Avg candidates: " << (totalCandidates / latencies.size()) << std::endl;
    std::cout << std::endl;

    // Check against targets
    std::cout << "=== Target Compliance ===" << std::endl;
    bool p50Pass = (p50Us / 1000.0) <= 8.0;
    bool p95Pass = (p95Us / 1000.0) <= 20.0;
    bool p99Pass = (p99Us / 1000.0) <= 35.0;

    std::cout << "  p50 <= 8ms:   " << (p50Pass ? "PASS" : "FAIL")
              << " (" << (p50Us / 1000.0) << " ms)" << std::endl;
    std::cout << "  p95 <= 20ms:  " << (p95Pass ? "PASS" : "FAIL")
              << " (" << (p95Us / 1000.0) << " ms)" << std::endl;
    std::cout << "  p99 <= 35ms:  " << (p99Pass ? "PASS" : "FAIL")
              << " (" << (p99Us / 1000.0) << " ms)" << std::endl;
    std::cout << std::endl;

    engine.shutdown();
}

int main() {
    runBenchmark();
    return 0;
}
