# HOSKEY Keyboard Native Engine - CMake Build
#
# Build configurations:
#   - HarmonyOS: cmake -DPLATFORM=ohos -DUSE_MINDSPORE=ON ..
#   - Desktop (test): cmake -DPLATFORM=desktop -DUSE_MINDSPORE=OFF ..
#
# Copyright (c) 2024-2026 HOSKEY Project
# Licensed under the Apache License, Version 2.0

cmake_minimum_required(VERSION 3.16)

project(keyboard_native
    VERSION 1.0.0
    DESCRIPTION "HOSKEY Keyboard Native Prediction Engine"
    LANGUAGES CXX C
)

# ============================================================================
# Options
# ============================================================================

option(USE_MINDSPORE "Enable MindSpore Lite neural backend" OFF)
option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_BENCHMARKS "Build benchmarks" ON)
option(PLATFORM "Target platform: ohos, desktop" "desktop")

# ============================================================================
# C++ Standard
# ============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# Compiler Flags
# ============================================================================

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
        -fvisibility=hidden
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -DNDEBUG)
    else()
        add_compile_options(-g -O0)
    endif()
endif()

# ============================================================================
# Platform Configuration
# ============================================================================

if(PLATFORM STREQUAL "ohos")
    message(STATUS "Building for HarmonyOS")
    add_definitions(-D__OHOS__)

    # HarmonyOS SDK paths
    if(DEFINED ENV{OHOS_SDK_HOME})
        set(OHOS_SDK_HOME $ENV{OHOS_SDK_HOME})
    endif()

    # NAPI headers
    if(OHOS_SDK_HOME)
        include_directories(${OHOS_SDK_HOME}/native/sysroot/usr/include)
    endif()
else()
    message(STATUS "Building for desktop (test mode)")
    add_definitions(-DDESKTOP_BUILD)
    # Include stub headers for HarmonyOS APIs
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/stubs)
endif()

# ============================================================================
# MindSpore Configuration
# ============================================================================

if(USE_MINDSPORE)
    message(STATUS "MindSpore Lite enabled")
    add_definitions(-DUSE_MINDSPORE)

    # Find MindSpore Lite
    if(DEFINED ENV{MINDSPORE_HOME})
        set(MINDSPORE_HOME $ENV{MINDSPORE_HOME})
        include_directories(${MINDSPORE_HOME}/include)
        link_directories(${MINDSPORE_HOME}/lib)
    endif()
else()
    message(STATUS "MindSpore Lite disabled (using stub)")
endif()

# ============================================================================
# Include Directories
# ============================================================================

# Existing HOSKEY sources path
set(HOSKEY_CPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../entry/src/main/cpp")

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    # Include existing HOSKEY infrastructure
    ${HOSKEY_CPP_DIR}
)

# ============================================================================
# Source Files
# ============================================================================

set(CORE_SOURCES
    src/core/engine.cpp
    src/core/fallback_policy.cpp
    src/core/ranking.cpp
    src/core/session.cpp
)

set(NN_SOURCES
    src/nn/neural_engine_ms.cpp
    src/nn/feature_builder.cpp
    src/nn/neural_engine_nnrt_adapter.cpp
)

set(INTEGRATION_SOURCES
    src/integration/hoskey_integration.cpp
)

set(DICT_SOURCES
    src/dict/dict_engine.cpp
)

set(PLATFORM_SOURCES
    src/platform/harmony_log.cpp
    src/error_codes.cpp
)

set(NAPI_SOURCES
    src/bridge_napi/napi_module.cpp
    src/bridge_napi/napi_utils.cpp
)

# ============================================================================
# Existing HOSKEY Sources (for integration)
# ============================================================================

set(HOSKEY_DICT_SOURCES
    ${HOSKEY_CPP_DIR}/dictionary_hoskey/yandex_trie.cpp
    ${HOSKEY_CPP_DIR}/dictionary_hoskey/neural_model_manager.cpp
    ${HOSKEY_CPP_DIR}/dictionary_hoskey/nnrt_scorer.cpp
    ${HOSKEY_CPP_DIR}/dictionary_hoskey/comptrie_reader.cpp
    ${HOSKEY_CPP_DIR}/dictionary_hoskey/trie_pooled.cpp
)

# ============================================================================
# Core Library (static)
# ============================================================================

add_library(keyboard_core STATIC
    ${CORE_SOURCES}
    ${NN_SOURCES}
    ${DICT_SOURCES}
    ${PLATFORM_SOURCES}
    ${INTEGRATION_SOURCES}
    ${HOSKEY_DICT_SOURCES}
)

target_include_directories(keyboard_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(USE_MINDSPORE)
    target_link_libraries(keyboard_core PRIVATE mindspore-lite)
endif()

# ============================================================================
# NAPI Module (shared library for HarmonyOS)
# ============================================================================

if(PLATFORM STREQUAL "ohos")
    add_library(keyboard_native SHARED
        ${NAPI_SOURCES}
    )

    target_link_libraries(keyboard_native PRIVATE
        keyboard_core
    )

    # HarmonyOS specific
    target_link_libraries(keyboard_native PRIVATE
        ace_napi.z
        hilog_ndk.z
    )

    set_target_properties(keyboard_native PROPERTIES
        OUTPUT_NAME "keyboard_native"
        PREFIX ""
        SUFFIX ".so"
    )
endif()

# ============================================================================
# Tests
# ============================================================================

if(BUILD_TESTS)
    enable_testing()

    add_executable(test_engine
        tests/test_engine.cpp
    )

    target_link_libraries(test_engine PRIVATE
        keyboard_core
    )

    add_test(NAME engine_tests COMMAND test_engine)
endif()

# ============================================================================
# Benchmarks
# ============================================================================

if(BUILD_BENCHMARKS)
    add_executable(bench_predict
        benchmarks/bench_predict.cpp
    )

    target_link_libraries(bench_predict PRIVATE
        keyboard_core
    )
endif()

# ============================================================================
# Installation
# ============================================================================

install(FILES
    include/engine_api.h
    include/types.h
    include/error_codes.h
    DESTINATION include/keyboard_native
)

install(TARGETS keyboard_core
    ARCHIVE DESTINATION lib
)

if(PLATFORM STREQUAL "ohos")
    install(TARGETS keyboard_native
        LIBRARY DESTINATION lib
    )
endif()

# ============================================================================
# Summary
# ============================================================================

message(STATUS "")
message(STATUS "=== HOSKEY Keyboard Native Configuration ===")
message(STATUS "Version:      ${PROJECT_VERSION}")
message(STATUS "Platform:     ${PLATFORM}")
message(STATUS "MindSpore:    ${USE_MINDSPORE}")
message(STATUS "Build tests:  ${BUILD_TESTS}")
message(STATUS "Build bench:  ${BUILD_BENCHMARKS}")
message(STATUS "Build type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "")
