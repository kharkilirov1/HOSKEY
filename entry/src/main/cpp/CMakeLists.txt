# CMakeLists.txt for HOSKEY Native Dictionary Engine
# Based on OpenBoard architecture for high-performance text prediction

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
    ${NATIVE_ROOT_PATH}/dictionary_openboard
    ${NATIVE_ROOT_PATH}/suggest/core
    ${NATIVE_ROOT_PATH}/suggest/policyimpl
    ${NATIVE_ROOT_PATH}/utils
)

# Source files - Dictionary Engine
set(DICTIONARY_SOURCES
    dictionary/trie.cpp
    dictionary/trie_node.cpp
    dictionary/binary_dict_reader.cpp
    dictionary/suggest_engine.cpp
)

# Source files - OpenBoard Suggest Engine
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
    suggest/policyimpl/typing/scoring_params.cpp
    suggest/policyimpl/typing/typing_scoring.cpp
    suggest/policyimpl/typing/typing_suggest_policy.cpp
    suggest/policyimpl/typing/typing_traversal.cpp
    suggest/policyimpl/typing/typing_weighting.cpp
)

# Source files - Utils
set(UTILS_SOURCES
    utils/autocorrection_threshold_utils.cpp
    utils/char_utils.cpp
    utils/jni_data_utils.cpp
    utils/log_utils.cpp
    utils/time_keeper.cpp
)

# Main NAPI entry
set(NAPI_SOURCES
    napi_init.cpp
)

# Create shared library
add_library(native_dict SHARED
    ${NAPI_SOURCES}
    ${DICTIONARY_SOURCES}
    ${SUGGEST_SOURCES}
    ${UTILS_SOURCES}
)

# Link with HarmonyOS NAPI - using the correct library name
find_library(NAPI_LIB ace napi HINTS ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../*/native/lib/)
if(NAPI_LIB)
    target_link_libraries(native_dict PUBLIC ${NAPI_LIB})
else()
    # Fallback to the common HarmonyOS NAPI library name
    target_link_libraries(native_dict PUBLIC libace_napi.z.so)
endif()