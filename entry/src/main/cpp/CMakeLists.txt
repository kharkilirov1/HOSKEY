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
    ${NATIVE_ROOT_PATH}/dictionary_hoskey
    ${NATIVE_ROOT_PATH}/suggest/core
    ${NATIVE_ROOT_PATH}/suggest/policyimpl
    ${NATIVE_ROOT_PATH}/utils
)

# Add the current directory for constants.h
include_directories(${NATIVE_ROOT_PATH})

# Source files - HOSKEY Dictionary Engine
set(DICTIONARY_HOSKEY_SOURCES
    dictionary_hoskey/trie.cpp
    dictionary_hoskey/trie_node.cpp
    dictionary_hoskey/binary_dict_reader.cpp
    dictionary_hoskey/suggest_engine.cpp
)

# Source files - OpenBoard Dictionary (required for suggest engine)
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

# Create shared library
add_library(native_dict SHARED
    ${NAPI_SOURCES}
    ${NAPI_HELPER_SOURCES}
    ${BINARY_DICTIONARY_NAPI_SOURCES}
    ${PROXIMITY_INFO_NAPI_SOURCES}
    ${DIC_TRAVERSE_SESSION_NAPI_SOURCES}
    ${DICTIONARY_HOSKEY_SOURCES}
    ${DICTIONARY_OPENBOARD_SOURCES}
    ${SUGGEST_SOURCES}
    ${UTILS_SOURCES}
)

# Link with HarmonyOS libraries
target_link_libraries(native_dict PUBLIC
    libace_napi.z.so
    libhilog_ndk.z.so
)
