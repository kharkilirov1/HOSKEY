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
)

# Source files - Dictionary Engine
set(DICTIONARY_SOURCES
    dictionary/trie.cpp
    dictionary/trie_node.cpp
    dictionary/binary_dict_reader.cpp
    dictionary/suggest_engine.cpp
)

# OpenBoard Suggest Engine (excluded - requires JNI, not compatible with NAPI)
# Future: Create JNI-free adapter or use NAPI-native swipe implementation

# Main NAPI entry
set(NAPI_SOURCES
    napi_init.cpp
)

# Create shared library
add_library(native_dict SHARED
    ${NAPI_SOURCES}
    ${DICTIONARY_SOURCES}
)

# Link with HarmonyOS NAPI - using the correct library name
find_library(NAPI_LIB ace napi HINTS ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../*/native/lib/)
if(NAPI_LIB)
    target_link_libraries(native_dict PUBLIC ${NAPI_LIB})
else()
    # Fallback to the common HarmonyOS NAPI library name
    target_link_libraries(native_dict PUBLIC libace_napi.z.so)
endif()