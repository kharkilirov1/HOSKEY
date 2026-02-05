/**
 * HOSKEY Keyboard - Engine Tests
 *
 * Unit and integration tests for core engine.
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#include <cassert>
#include <iostream>
#include <cstring>

#include "../include/types.h"
#include "../include/error_codes.h"
#include "../src/core/engine.h"

using namespace keyboard::core;

// ============================================================================
// Test Utilities
// ============================================================================

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "FAIL: " << message << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
            return false; \
        } \
    } while (0)

#define RUN_TEST(test) \
    do { \
        std::cout << "Running " << #test << "... "; \
        if (test()) { \
            std::cout << "PASS" << std::endl; \
            passed++; \
        } else { \
            std::cout << "FAIL" << std::endl; \
            failed++; \
        } \
    } while (0)

// ============================================================================
// Unit Tests
// ============================================================================

bool test_engine_create_destroy() {
    Engine engine;
    TEST_ASSERT(engine.getState() == KB_STATE_UNINITIALIZED,
                "Initial state should be uninitialized");
    return true;
}

bool test_engine_init_default() {
    Engine engine;

    KBEngineConfig config;
    kb_config_init_default(&config);

    auto err = engine.init(config);
    TEST_ASSERT(err == KB_OK, "Init should succeed with default config");
    TEST_ASSERT(engine.getState() == KB_STATE_READY, "State should be ready after init");

    engine.shutdown();
    TEST_ASSERT(engine.getState() == KB_STATE_CLOSED, "State should be closed after shutdown");

    return true;
}

bool test_engine_double_init() {
    Engine engine;

    KBEngineConfig config;
    kb_config_init_default(&config);

    auto err1 = engine.init(config);
    TEST_ASSERT(err1 == KB_OK, "First init should succeed");

    auto err2 = engine.init(config);
    TEST_ASSERT(err2 == KB_ERR_ALREADY_INITIALIZED, "Second init should fail");

    engine.shutdown();
    return true;
}

bool test_engine_predict_without_init() {
    Engine engine;

    KBPredictContext ctx = {};
    ctx.inputText = "hel";
    ctx.inputLength = 3;
    ctx.maxResults = 10;

    KBPredictResult result = {};
    auto err = engine.predict(ctx, result);

    TEST_ASSERT(err == KB_ERR_NOT_INITIALIZED, "Predict without init should fail");

    return true;
}

bool test_engine_predict_basic() {
    Engine engine;

    KBEngineConfig config;
    kb_config_init_default(&config);
    config.useNeural = 0;  // Disable neural for fast test

    auto err = engine.init(config);
    TEST_ASSERT(err == KB_OK, "Init should succeed");

    KBPredictContext ctx = {};
    std::string input = "hel";
    ctx.inputText = input.c_str();
    ctx.inputLength = static_cast<uint32_t>(input.length());
    ctx.maxResults = 10;

    KBPredictResult result = {};
    err = engine.predict(ctx, result);

    TEST_ASSERT(err == KB_OK, "Predict should succeed");
    // Note: without dictionary, we may not get candidates
    // But the call should not crash

    // Clean up
    if (result.candidates) {
        for (uint32_t i = 0; i < result.candidateCount; ++i) {
            free(result.candidates[i].text);
        }
        delete[] result.candidates;
    }
    if (result.traceId) {
        free(result.traceId);
    }

    engine.shutdown();
    return true;
}

bool test_engine_reset_session() {
    Engine engine;

    KBEngineConfig config;
    kb_config_init_default(&config);

    auto err = engine.init(config);
    TEST_ASSERT(err == KB_OK, "Init should succeed");

    err = engine.resetSession();
    TEST_ASSERT(err == KB_OK, "Reset session should succeed");

    engine.shutdown();
    return true;
}

bool test_engine_learn() {
    Engine engine;

    KBEngineConfig config;
    kb_config_init_default(&config);

    auto err = engine.init(config);
    TEST_ASSERT(err == KB_OK, "Init should succeed");

    KBLearnEvent event = {};
    std::string word = "test";
    event.type = KB_LEARN_WORD_SELECTED;
    event.word = word.c_str();
    event.frequency = 1;

    err = engine.learn(event);
    TEST_ASSERT(err == KB_OK, "Learn should succeed");

    engine.shutdown();
    return true;
}

bool test_engine_status() {
    Engine engine;

    KBEngineConfig config;
    kb_config_init_default(&config);

    auto err = engine.init(config);
    TEST_ASSERT(err == KB_OK, "Init should succeed");

    KBEngineStatus status;
    err = engine.getStatus(status);

    TEST_ASSERT(err == KB_OK, "Get status should succeed");
    TEST_ASSERT(status.state == KB_STATE_READY, "Status state should be ready");

    engine.shutdown();
    return true;
}

bool test_engine_repeated_init_close() {
    // Test for memory leaks and crashes
    for (int i = 0; i < 5; ++i) {
        Engine engine;

        KBEngineConfig config;
        kb_config_init_default(&config);

        auto err = engine.init(config);
        TEST_ASSERT(err == KB_OK, "Init should succeed");

        // Do some predictions
        KBPredictContext ctx = {};
        std::string input = "test";
        ctx.inputText = input.c_str();
        ctx.inputLength = static_cast<uint32_t>(input.length());
        ctx.maxResults = 5;

        KBPredictResult result = {};
        engine.predict(ctx, result);

        // Clean up result
        if (result.candidates) {
            for (uint32_t j = 0; j < result.candidateCount; ++j) {
                free(result.candidates[j].text);
            }
            delete[] result.candidates;
        }
        if (result.traceId) {
            free(result.traceId);
        }

        engine.shutdown();
    }

    return true;
}

// ============================================================================
// Integration Tests
// ============================================================================

bool test_integration_full_cycle() {
    Engine engine;

    // 1. Init
    KBEngineConfig config;
    kb_config_init_default(&config);
    config.useNeural = 0;

    auto err = engine.init(config);
    TEST_ASSERT(err == KB_OK, "Init should succeed");

    // 2. Predict
    KBPredictContext ctx = {};
    std::string input = "hel";
    ctx.inputText = input.c_str();
    ctx.inputLength = static_cast<uint32_t>(input.length());
    ctx.maxResults = 10;

    KBPredictResult result = {};
    err = engine.predict(ctx, result);
    TEST_ASSERT(err == KB_OK, "Predict should succeed");

    // 3. Learn
    KBLearnEvent event = {};
    std::string word = "hello";
    event.type = KB_LEARN_WORD_SELECTED;
    event.word = word.c_str();

    err = engine.learn(event);
    TEST_ASSERT(err == KB_OK, "Learn should succeed");

    // 4. Reset session
    err = engine.resetSession();
    TEST_ASSERT(err == KB_OK, "Reset session should succeed");

    // 5. Clean up
    if (result.candidates) {
        for (uint32_t i = 0; i < result.candidateCount; ++i) {
            free(result.candidates[i].text);
        }
        delete[] result.candidates;
    }
    if (result.traceId) {
        free(result.traceId);
    }

    // 6. Close
    engine.shutdown();
    TEST_ASSERT(engine.getState() == KB_STATE_CLOSED, "State should be closed");

    return true;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "=== HOSKEY Keyboard Engine Tests ===" << std::endl;
    std::cout << std::endl;

    int passed = 0;
    int failed = 0;

    // Unit tests
    std::cout << "Unit Tests:" << std::endl;
    RUN_TEST(test_engine_create_destroy);
    RUN_TEST(test_engine_init_default);
    RUN_TEST(test_engine_double_init);
    RUN_TEST(test_engine_predict_without_init);
    RUN_TEST(test_engine_predict_basic);
    RUN_TEST(test_engine_reset_session);
    RUN_TEST(test_engine_learn);
    RUN_TEST(test_engine_status);
    RUN_TEST(test_engine_repeated_init_close);

    std::cout << std::endl;

    // Integration tests
    std::cout << "Integration Tests:" << std::endl;
    RUN_TEST(test_integration_full_cycle);

    std::cout << std::endl;
    std::cout << "=== Results ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;

    return failed > 0 ? 1 : 0;
}
