cmake_minimum_required(VERSION 3.18.1)
project(hoskey_bridge)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(hoskey_bridge SHARED
    napi_bridge/hosdict_bridge.cpp
    hosdict_engine/asset_manifest.cpp
    hosdict_engine/loaders.cpp
    hosdict_engine/hosdict_container.cpp
    hosdict_engine/hosdict_package.cpp
    hosdict_engine/char_trigram_model.cpp
    hosdict_engine/context_emoji_model.cpp
    hosdict_engine/emoji_suggest_runner.cpp
    hosdict_engine/emoji_search_model.cpp
    hosdict_engine/keyboard_engine.cpp
    hosdict_engine/predictor_engine.cpp
    hosdict_engine/text_utils.cpp
    hosdict_engine/policy_store.cpp
    hosdict_engine/normdict_reader.cpp
    hosdict_engine/lemmer.cpp
    hosdict_engine/tfmtrie_decoder.cpp
    hosdict_engine/one_nc7_trie.cpp
    hosdict_engine/oblivious_tree_model.cpp
    hosdict_engine/autocc_model_runner.cpp
    hosdict_engine/tap_model_bundle.cpp
    hosdict_engine/tap_model_runner.cpp
    hosdict_engine/ml_ranker.cpp
    hosdict_engine/neural_lm.cpp
    hosdict_engine/rnnt_swipe_model.cpp
    hosdict_engine/swipe_decoder.cpp
    hosdict_engine/ynnlm_bundle.cpp
    hosdict_engine/rwkv_encoder.cpp
    hosdict_engine/ynnlm_char_decoder.cpp
    hosdict_engine/ynnlm_word_chain.cpp
    hosdict_engine/succinct_trie.cpp
)

# The exact char decoder is dominated by ~2.4M int8 MACs per character. Debug
# builds otherwise force -O0 and make synchronous autocorrect unusably slow;
# keep this pure, test-covered kernel optimized without enabling fast-math.
set_source_files_properties(
    hosdict_engine/ynnlm_char_decoder.cpp
    PROPERTIES COMPILE_OPTIONS "-O3"
)

target_compile_definitions(hoskey_bridge PRIVATE
    HARMONY_BUILD=1
)

if(CMAKE_SYSTEM_NAME STREQUAL "OHOS")
    target_compile_definitions(hoskey_bridge PRIVATE
        MINDSPORE_LITE_AVAILABLE=1
    )
endif()

target_include_directories(hoskey_bridge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/hosdict_engine
    ${CMAKE_CURRENT_SOURCE_DIR}/napi_bridge
)

target_link_libraries(hoskey_bridge
    libace_napi.z.so
    libhilog_ndk.z.so
    libc.so
    libm.so
    libdl.so
)

if(CMAKE_SYSTEM_NAME STREQUAL "OHOS")
    target_link_libraries(hoskey_bridge
        libmindspore_lite_ndk.so
    )
endif()
