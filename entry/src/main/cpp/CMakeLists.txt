# CMakeLists.txt for HOSKEY Native Dictionary Engine
#
# Architecture: Yandex-style dictionary (primary) with mmap-based CompTrie
#
# NOTE: OpenBoard sources are still compiled but NOT used at runtime.
# They are only needed for gesture_stroke.cpp and multi_predictor.cpp dependencies.
# All dictionary operations use YandexDict (g_yandexDict) exclusively.

cmake_minimum_required(VERSION 3.5.0)
project(native_dict)

# C++17 for modern features
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Root path
set(NATIVE_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR})

# Include directories
include_directories(
    ${NATIVE_ROOT_PATH}
    ${NATIVE_ROOT_PATH}/dictionary
    ${NATIVE_ROOT_PATH}/dictionary_hoskey
    ${NATIVE_ROOT_PATH}/suggest/core
    ${NATIVE_ROOT_PATH}/suggest/policyimpl
    ${NATIVE_ROOT_PATH}/utils
)

# Add the current directory for constants.h
include_directories(${NATIVE_ROOT_PATH})

# Source files - HOSKEY Dictionary Engine (LEGACY - not used at runtime)
# These files are compiled but globals (g_trie, g_suggestEngine, etc.) are not used.
# Kept for potential future reference or dependencies.
set(DICTIONARY_HOSKEY_SOURCES
    dictionary_hoskey/trie.cpp
    dictionary_hoskey/trie_node.cpp
    dictionary_hoskey/trie_pooled.cpp
    dictionary_hoskey/flat_trie.cpp
    dictionary_hoskey/binary_dict_reader.cpp
    dictionary_hoskey/suggest_engine.cpp
)

# Source files - Yandex Neural Dictionary (NNRt + CANNKit + CompTrie)
# All 15 neural models managed by NeuralModelManager
set(YANDEX_NEURAL_SOURCES
    dictionary_hoskey/yandex_trie.cpp
    dictionary_hoskey/comptrie_reader.cpp
    dictionary_hoskey/nnrt_scorer.cpp
    dictionary_hoskey/neural_model_manager.cpp
)

# Source files - OpenBoard Dictionary (LEGACY - only for gesture/multi_predictor deps)
# These files are NOT used for dictionary operations - YandexDict is primary.
set(DICTIONARY_OPENBOARD_SOURCES
    dictionary/property/ngram_context.cpp
    dictionary/utils/multi_bigram_map.cpp
    dictionary/utils/format_utils.cpp
    dictionary/utils/buffer_with_extendable_buffer.cpp
    dictionary/utils/byte_array_utils.cpp
    dictionary/utils/file_utils.cpp
    dictionary/utils/mmapped_buffer.cpp
    dictionary/utils/probability_utils.cpp
    dictionary/utils/sparse_table.cpp
    dictionary/utils/trie_map.cpp
    dictionary/utils/forgetting_curve_utils.cpp
    dictionary/utils/dict_file_writing_utils.cpp
    dictionary/header/header_policy.cpp
    dictionary/header/header_read_write_utils.cpp
    dictionary/structure/dictionary_structure_with_buffer_policy_factory.cpp
    dictionary/structure/pt_common/bigram/bigram_list_read_write_utils.cpp
    dictionary/structure/pt_common/dynamic_pt_gc_event_listeners.cpp
    dictionary/structure/pt_common/dynamic_pt_reading_helper.cpp
    dictionary/structure/pt_common/dynamic_pt_reading_utils.cpp
    dictionary/structure/pt_common/dynamic_pt_updating_helper.cpp
    dictionary/structure/pt_common/dynamic_pt_writing_utils.cpp
    dictionary/structure/pt_common/patricia_trie_reading_utils.cpp
    dictionary/structure/pt_common/shortcut/shortcut_list_reading_utils.cpp
    dictionary/structure/v2/patricia_trie_policy.cpp
    dictionary/structure/v2/ver2_patricia_trie_node_reader.cpp
    dictionary/structure/v2/ver2_pt_node_array_reader.cpp
    dictionary/structure/v4/content/dynamic_language_model_probability_utils.cpp
    dictionary/structure/v4/content/language_model_dict_content.cpp
    dictionary/structure/v4/content/language_model_dict_content_global_counters.cpp
    dictionary/structure/v4/content/shortcut_dict_content.cpp
    dictionary/structure/v4/content/sparse_table_dict_content.cpp
    dictionary/structure/v4/content/terminal_position_lookup_table.cpp
    dictionary/structure/v4/ver4_dict_buffers.cpp
    dictionary/structure/v4/ver4_dict_constants.cpp
    dictionary/structure/v4/ver4_patricia_trie_node_reader.cpp
    dictionary/structure/v4/ver4_patricia_trie_node_writer.cpp
    dictionary/structure/v4/ver4_patricia_trie_policy.cpp
    dictionary/structure/v4/ver4_patricia_trie_reading_utils.cpp
    dictionary/structure/v4/ver4_patricia_trie_writing_helper.cpp
    dictionary/structure/v4/ver4_pt_node_array_reader.cpp
    dictionary/structure/backward/v402/bigram/ver4_bigram_list_policy.cpp
    dictionary/structure/backward/v402/content/bigram_dict_content.cpp
    dictionary/structure/backward/v402/content/probability_dict_content.cpp
    dictionary/structure/backward/v402/content/shortcut_dict_content.cpp
    dictionary/structure/backward/v402/content/sparse_table_dict_content.cpp
    dictionary/structure/backward/v402/content/terminal_position_lookup_table.cpp
    dictionary/structure/backward/v402/ver4_dict_buffers.cpp
    dictionary/structure/backward/v402/ver4_dict_constants.cpp
    dictionary/structure/backward/v402/ver4_patricia_trie_node_reader.cpp
    dictionary/structure/backward/v402/ver4_patricia_trie_node_writer.cpp
    dictionary/structure/backward/v402/ver4_patricia_trie_policy.cpp
    dictionary/structure/backward/v402/ver4_patricia_trie_reading_utils.cpp
    dictionary/structure/backward/v402/ver4_patricia_trie_writing_helper.cpp
    dictionary/structure/backward/v402/ver4_pt_node_array_reader.cpp
)

# Source files - OpenBoard Suggest Engine (LEGACY - only for gesture/proximity deps)
# gesture_stroke.cpp and gesture_trail_data.cpp ARE used for swipe recognition.
# Other files are compiled but not used at runtime.
set(SUGGEST_SOURCES
    suggest/core/dicnode/dic_node.cpp
    suggest/core/dicnode/dic_node_utils.cpp
    suggest/core/dicnode/dic_nodes_cache.cpp
    suggest/core/dictionary/dictionary.cpp
    suggest/core/dictionary/dictionary_utils.cpp
    suggest/core/dictionary/digraph_utils.cpp
    suggest/core/dictionary/error_type_utils.cpp
    suggest/core/layout/additional_proximity_chars.cpp
    suggest/core/layout/proximity_info.cpp
    suggest/core/layout/proximity_info_params.cpp
    suggest/core/layout/proximity_info_state.cpp
    suggest/core/layout/proximity_info_state_utils.cpp
    suggest/core/policy/weighting.cpp
    suggest/core/result/suggestion_results.cpp
    suggest/core/result/suggestions_output_utils.cpp
    suggest/core/session/dic_traverse_session.cpp
    suggest/core/suggest.cpp
    suggest/policyimpl/gesture/gesture_suggest_policy_factory.cpp
    suggest/policyimpl/gesture/gesture_stroke.cpp
    suggest/policyimpl/gesture/gesture_trail_data.cpp
    suggest/policyimpl/typing/scoring_params.cpp
    suggest/policyimpl/typing/typing_scoring.cpp
    suggest/policyimpl/typing/typing_suggest_policy.cpp
    suggest/policyimpl/typing/typing_traversal.cpp
    suggest/policyimpl/typing/typing_weighting.cpp
)

# Source files - Multi-Predictor & Caching (Yandex-inspired)
set(MULTI_PREDICTOR_SOURCES
    suggest/multi_predictor.cpp
    suggest/suggestion_cache.cpp
)

# Source files - Autocorrect Rules
set(AUTOCORRECT_SOURCES
    dictionary/autocorrect_rules.cpp
)

# Source files - Utils
set(UTILS_SOURCES
    utils/autocorrection_threshold_utils.cpp
    utils/char_utils.cpp
    # utils/jni_data_utils.cpp  # REMOVED: Ported to NAPI helpers
    utils/log_utils.cpp
    utils/time_keeper.cpp
)

# Main NAPI entry
set(NAPI_SOURCES
    napi_init.cpp
)

# NAPI helpers (новые)
set(NAPI_HELPER_SOURCES
    napi_helpers.cpp
)

# Binary Dictionary NAPI
set(BINARY_DICTIONARY_NAPI_SOURCES
    binary_dictionary_napi.cpp
)

# Proximity Info NAPI
set(PROXIMITY_INFO_NAPI_SOURCES
    proximity_info_napi.cpp
)

# Dic Traverse Session NAPI
set(DIC_TRAVERSE_SESSION_NAPI_SOURCES
    dic_traverse_session_napi.cpp
)

# Batch Operations NAPI (Yandex-inspired)
set(BATCH_OPERATIONS_NAPI_SOURCES
    batch_operations_napi.cpp
)

# Create shared library
add_library(native_dict SHARED
    ${NAPI_SOURCES}
    ${NAPI_HELPER_SOURCES}
    ${BINARY_DICTIONARY_NAPI_SOURCES}
    ${PROXIMITY_INFO_NAPI_SOURCES}
    ${DIC_TRAVERSE_SESSION_NAPI_SOURCES}
    ${BATCH_OPERATIONS_NAPI_SOURCES}
    ${DICTIONARY_HOSKEY_SOURCES}
    ${YANDEX_NEURAL_SOURCES}
    ${DICTIONARY_OPENBOARD_SOURCES}
    ${SUGGEST_SOURCES}
    ${MULTI_PREDICTOR_SOURCES}
    ${AUTOCORRECT_SOURCES}
    ${UTILS_SOURCES}
)

# MindSpore Lite for neural scoring (HarmonyOS 5.0+)
# Documentation: https://developer.huawei.com/consumer/cn/doc/harmonyos-guides-V5/mindspore-lite-guidelines-V5

# Debug: print available variables
message(STATUS "OHOS_SDK: ${OHOS_SDK}")
message(STATUS "OHOS_SDK_NATIVE: ${OHOS_SDK_NATIVE}")
message(STATUS "OHOS_ARCH: ${OHOS_ARCH}")
message(STATUS "CMAKE_OHOS_ARCH_ABI: ${CMAKE_OHOS_ARCH_ABI}")

# Determine SDK path (try multiple sources)
if(DEFINED OHOS_SDK_NATIVE)
    set(NATIVE_SDK_PATH ${OHOS_SDK_NATIVE})
elseif(DEFINED OHOS_SDK)
    set(NATIVE_SDK_PATH ${OHOS_SDK}/native)
else()
    # Fallback: try standard DevEco path
    set(NATIVE_SDK_PATH "C:/Program Files/Huawei/DevEco Studio/sdk/default/openharmony/native")
endif()

# Determine arch - HarmonyOS uses different naming than Android
# CMAKE_OHOS_ARCH_ABI might be arm64-v8a but library path uses aarch64-linux-ohos
if(DEFINED CMAKE_OHOS_ARCH_ABI)
    if(CMAKE_OHOS_ARCH_ABI STREQUAL "arm64-v8a")
        set(OHOS_ARCH "aarch64-linux-ohos")
    elseif(CMAKE_OHOS_ARCH_ABI STREQUAL "x86_64")
        set(OHOS_ARCH "x86_64-linux-ohos")
    elseif(CMAKE_OHOS_ARCH_ABI STREQUAL "armeabi-v7a")
        set(OHOS_ARCH "arm-linux-ohos")
    else()
        set(OHOS_ARCH ${CMAKE_OHOS_ARCH_ABI})
    endif()
elseif(NOT DEFINED OHOS_ARCH)
    set(OHOS_ARCH "aarch64-linux-ohos")  # Default for ARM64 devices
endif()

message(STATUS "Using NATIVE_SDK_PATH: ${NATIVE_SDK_PATH}")
message(STATUS "Using OHOS_ARCH: ${OHOS_ARCH}")

# Find MindSpore Lite library
find_library(MINDSPORE_LITE_LIB 
    NAMES mindspore_lite_ndk libmindspore_lite_ndk
    PATHS 
        ${NATIVE_SDK_PATH}/sysroot/usr/lib/${OHOS_ARCH}
        ${NATIVE_SDK_PATH}/llvm/lib
    NO_DEFAULT_PATH
)

# MindSpore headers use <> includes which search in sysroot
# sysroot is set to openharmony/native/sysroot by toolchain

# Force enable MindSpore for model support
set(FORCE_MINDSPORE ON)

if(MINDSPORE_LITE_LIB OR FORCE_MINDSPORE)
    message(STATUS "MindSpore Lite ENABLED (force=${FORCE_MINDSPORE}, lib=${MINDSPORE_LITE_LIB})")
    target_compile_definitions(native_dict PRIVATE MINDSPORE_AVAILABLE=1)
    
    # Add MindSpore include path (openharmony, not hms!)
    target_include_directories(native_dict PRIVATE
        ${NATIVE_SDK_PATH}/sysroot/usr/include
    )
else()
    message(STATUS "MindSpore Lite disabled, using heuristic scoring fallback")
    target_compile_definitions(native_dict PRIVATE MINDSPORE_AVAILABLE=0)
endif()

# Link with HarmonyOS libraries
target_link_libraries(native_dict PUBLIC
    libace_napi.z.so
    libhilog_ndk.z.so
)

# Link MindSpore Lite if available (legacy fallback)
if(MINDSPORE_LITE_LIB)
    target_link_libraries(native_dict PUBLIC ${MINDSPORE_LITE_LIB})
endif()

# ============================================================================
# NPU Acceleration via MindSpore Lite
# 
# MindSpore Lite C API supports:
#   - OH_AI_DEVICETYPE_KIRIN_NPU for Kirin NPU
#   - OH_AI_DEVICETYPE_NNRT for NNRT backend
#   - OH_AI_DEVICETYPE_CPU with FP16 as fallback
#
# Documentation:
#   - https://developer.huawei.com/consumer/cn/doc/harmonyos-guides-V5/mindspore-lite-guidelines-V5
# ============================================================================
# NPU support is built into MindSpore Lite - no additional libraries needed
