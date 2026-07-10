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
    hosdict_engine/keyboard_engine.cpp
    hosdict_engine/predictor_engine.cpp
    hosdict_engine/text_utils.cpp
    hosdict_engine/policy_store.cpp
    hosdict_engine/normdict_reader.cpp
    hosdict_engine/lemmer.cpp
    hosdict_engine/tfmtrie_decoder.cpp
    hosdict_engine/one_nc7_trie.cpp
    hosdict_engine/ml_ranker.cpp
    hosdict_engine/neural_lm.cpp
    hosdict_engine/ynnlm_bundle.cpp
    hosdict_engine/rwkv_encoder.cpp
    hosdict_engine/ynnlm_word_chain.cpp
    hosdict_engine/succinct_trie.cpp
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
