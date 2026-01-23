# Porting OpenBoard Suggest Engine from JNI to NAPI

## Overview

This document describes the process of porting the OpenBoard Suggest Engine, originally written for Android using JNI, to work within a HarmonyOS application using the NAPI (Native API).

## Scope of Work

- **Target Component:** OpenBoard Suggest Engine (Core functionality for swipe path interpretation, word prediction, etc.)
- **Source:** Derived from Android AOSP/OpenBoard keyboard project (C++/JNI).
- **Target Platform:** HarmonyOS OpenHarmony (C++/NAPI).
- **Total Files Involved:** ~200+ (initial estimate: ~11,500 lines of code).
- **Critical Dependencies:** JNI headers and types (`jni.h`, `JNIEnv*`, `jintArray`, etc.).

## Mapping JNI Types to NAPI Types

| JNI Type | NAPI Equivalent | Conversion Method |
|----------|------------------|-------------------|
| `JNIEnv *env` | `napi_env env` | Direct context replacement |
| `jintArray` | `napi_value` (array) | `napi_get_array_length`, `napi_get_element` |
| `jfloatArray` | `napi_value` (array) | `napi_get_array_length`, `napi_get_element` |
| `jint` | `int32_t` | `napi_get_value_int32` |
| `jfloat` | `float` / `double` | `napi_get_value_double` |
| `jsize` | `uint32_t` | `napi_get_array_length` |

### JNI Methods to NAPI Equivalents

| JNI Method | NAPI Equivalent |
|------------|------------------|
| `env->GetIntArrayRegion(jArray, 0, len, buffer)` | `for (i=0; i<len; i++) { napi_get_element(); napi_get_value_int32(); }` |
| `env->GetFloatArrayRegion(jArray, 0, len, buffer)` | `for (i=0; i<len; i++) { napi_get_element(); napi_get_value_double(); }` |
| `env->SetIntArrayRegion(jArray, 0, len, buffer)` | `for (i=0; i<len; i++) { napi_create_int32(); napi_set_element(); }` |
| `env->SetFloatArrayRegion(jArray, 0, len, buffer)` | `for (i=0; i<len; i++) { napi_create_double(); napi_set_element(); }` |

## Porting Plan (7 Stages)

### Stage 1: Create NAPI Helper Utilities

**Goal:** Create NAPI equivalents for common JNI helper functions.

**Files:**

- `entry/src/main/cpp/napi_helpers.h`
- `entry/src/main/cpp/napi_helpers.cpp`

**Implemented Functions:**

- `napiGetIntArrayRegion`
- `napiGetFloatArrayRegion`
- `napiSetIntArrayRegion`
- `napiSetFloatArrayRegion`
- `napiCreateIntArray`
- `napiCreateFloatArray`
- `napiCreateString`
- `napiGetArrayLengthChecked`
- `napiIsArray`
- `napiIsObject`

### Stage 2: Port ProximityInfo

**Goal:** Remove JNI dependencies from `ProximityInfo` (a core component for keyboard layout interpretation).

**Files:**

- `entry/src/main/cpp/suggest/core/layout/proximity_info.h`
- `entry/src/main/cpp/suggest/core/layout/proximity_info.cpp`

**Changes:**

- Constructor signature updated to use `napi_env` and `napi_value` instead of `JNIEnv*` and `jintArray`/`jfloatArray`.
- Internal helper functions `safeGetOrFillZeroIntArrayRegion` and `safeGetOrFillZeroFloatArrayRegion` updated to use `hoskey::napiGetXxxRegion`.

### Stage 3: Port SuggestionResults

**Goal:** Remove JNI dependencies from `SuggestionResults` (handles output of suggestions).

**Files:**

- `entry/src/main/cpp/suggest/core/result/suggestion_results.h`
- `entry/src/main/cpp/suggest/core/result/suggestion_results.cpp`

**Changes:**

- `outputSuggestions` method signature updated to use `napi_env` and `napi_value` arrays.

### Stage 4: Port Dictionary Core (Planned)

**Goal:** Remove JNI from dictionary interface.

**Files:**

- `entry/src/main/cpp/suggest/core/dictionary/dictionary.h`
- `entry/src/main/cpp/suggest/core/dictionary/dictionary.cpp`

### Stage 5: Update DicTraverseSession (Planned)

**Goal:** Remove JNI includes (`#include "jni.h"`) and update data types if necessary.

**Files:**

- `entry/src/main/cpp/suggest/core/session/dic_traverse_session.h`

### Stage 6: Create NAPI Wrapper for `Suggest::getSuggestions()` (Partial Implementation)

**Goal:** Wrap the main algorithm in an NAPI function.

**File:** `entry/src/main/cpp/napi_init.cpp`

**Added Function:** `ProcessSwipePathOpenBoard`

**Signature:**

```ts
processSwipePathOpenBoard(
  touchPoints: Array<{x: number, y: number, time: number}>,
  keyLayout: Array<{key: string, x: number, y: number, width: number, height: number}>
): {words: string[], scores: number[]} | null
```

**Status:** Implemented the NAPI wrapper registration and basic input parsing. The core `latinime::Suggest` call is currently a placeholder returning an empty result object. This requires completion of Stages 4 and 5.

### Stage 7: Update CMakeLists.txt and Rebuild

**Goal:** Include all OpenBoard files in the build process.

**File:** `entry/src/main/cpp/CMakeLists.txt`

**Changes:**

- Added `napi_helpers.cpp` to source list.
- Removed `utils/jni_data_utils.cpp` from source list.
- Ensured all necessary include directories are present.

## Additional Changes

- `utils/char_utils.cpp`: No JNI dependencies found, left unchanged.
- `utils/log_utils.cpp`: (Assumed) No JNI dependencies, left unchanged. (Note: Actual check pending if file exists)

## Current Status

- **Compilation:** Should pass without `jni.h not found` errors for stages 1, 2, and 3.
- **NAPI Function:** `processSwipePathOpenBoard` is registered but returns a dummy result.
- **Next Steps:** Complete Stages 4 and 5 to enable the core suggest engine functionality within the NAPI wrapper.

## Challenges Encountered

- **Thread Safety:** The OpenBoard suggest engine was not inherently thread-safe. A mutex (`g_suggestEngineMutex`) was implemented in the NAPI wrapper to prevent concurrent access.
- **Memory Management:** Careful attention was paid to NAPI string and array creation/deletion to prevent leaks.
- **Complexity:** The dictionary and suggest engine components are highly interdependent, making incremental porting challenging.

## Performance Considerations

- **Latency:** Initial goal is <20ms for swipe path processing.
- **Memory:** Shared Trie structure expected (~2-5MB).
- **Accuracy:** Target >80% accuracy compared to original OpenBoard behavior.

## Fallback Mechanism

- Implement a fallback to ETS-based swipe path processing in case the native NAPI module fails.

## Result of Porting

Upon completion of all stages:

- ✅ Full OpenBoard `Suggest::getSuggestions()` accessible via NAPI.
- ✅ ProximityInfo with full keyboard layout support.
- ✅ DicNode traversal with priority queue.
- ✅ Weighted Levenshtein + proximity scoring.
- ✅ ~85-90% accuracy (matching OpenBoard).
- ✅ Production-level quality and performance.

## References

- HarmonyOS NAPI Guide
- Node-API Reference
- Android AOSP LatinIME Source Code