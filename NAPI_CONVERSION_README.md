# OpenBoard to HarmonyOS NAPI Conversion

This document describes the conversion of OpenBoard C++ JNI code to HarmonyOS NAPI.

## Overview

The project converts Android JNI code from OpenBoard keyboard to HarmonyOS NAPI for better performance and HarmonyOS compatibility.

## Converted Modules

### 1. Binary Dictionary NAPI
- **Files**: `binary_dictionary_napi.cpp`, `binary_dictionary_napi.h`
- **Functions**:
  - `open()` - Opens a dictionary from file
  - `createOnMemory()` - Creates an in-memory dictionary
  - `flush()` - Flushes dictionary changes to file
  - `needsToRunGC()` - Checks if garbage collection is needed
  - `flushWithGC()` - Flushes with garbage collection
  - `close()` - Closes and cleans up dictionary
  - `getFormatVersion()` - Gets the dictionary format version
  - `getProbability()` - Gets word probability
  - `getMaxProbabilityOfExactMatches()` - Gets max probability for exact matches
  - `getSuggestions()` - Gets word suggestions

### 2. Proximity Info NAPI
- **Files**: `proximity_info_napi.cpp`, `proximity_info_napi.h`
- **Functions**:
  - `setProximityInfo()` - Sets keyboard proximity information
  - `release()` - Releases proximity info resources

### 3. Dic Traverse Session NAPI
- **Files**: `dic_traverse_session_napi.cpp`, `dic_traverse_session_napi.h`
- **Functions**:
  - `newSession()` - Creates a new traversal session
  - `release()` - Releases traversal session
  - `getPrevWord()` - Gets previous word information

## Conversion Rules Applied

- ✅ Removed all `#include "jni.h"` references
- ✅ Replaced `JNIEnv*` with `napi_env`
- ✅ Adapted `jstring` to use NAPI string functions
- ✅ Adapted `jintArray` to use NAPI array functions
- ✅ Removed `JNIEXPORT`, `JNICALL` macros
- ✅ Updated logging from Android log to HarmonyOS hilog

## Architecture

- Uses NAPI external objects to wrap C++ class instances
- Implements proper memory management with destructors
- Utilizes NAPI helpers for array handling
- Maintains functional interfaces adapted to NAPI conventions

## Constants

Defined in `constants.h`:
- `NOT_A_PROBABILITY`
- `MAX_RESULTS`
- `MAX_WORD_LENGTH`
- `CODE_POINT_BEGINNING_OF_SENTENCE`
- Other required constants

## Build Configuration

Updated `CMakeLists.txt` to include all NAPI source files and dependencies.

## Benefits

- HarmonyOS compatibility
- Better performance without JNI overhead
- Memory safety with NAPI external objects
- Modular design for easy maintenance
- Extensible architecture for future enhancements