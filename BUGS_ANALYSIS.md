# HOSKEY Project Bug Analysis Report

**Date:** 2026-01-23
**Files analyzed:** 24 ETS/TS + 30+ C++ source files
**Total issues found:** 19

---

## Summary

| Severity | Count |
|----------|-------|
| Critical | 3 |
| High | 5 |
| Medium | 7 |
| Low | 4 |
| **Total** | **19** |

---

## Critical Issues

### 1. Wrong Argument Indices in GetSuggestions
**File:** `entry/src/main/cpp/binary_dictionary_napi.cpp`
**Lines:** 436

**Issue:** Uses `args[5]` instead of `args[8]` for pointerIds parameter.

```cpp
napiGetIntArrayRegion(env, args[5], 0, inputSize, pointerIds);  // Should be args[8]
```

**Impact:** Buffer overflow, memory corruption, crash during suggestion generation.

---

### 2. Unvalidated Array Access in GetSuggestions
**File:** `entry/src/main/cpp/binary_dictionary_napi.cpp`
**Lines:** 428-454

**Issue:** Memory allocated based on `inputSize` but actual array lengths not validated.

```cpp
int* xCoordinates = new int[inputSize];
int* yCoordinates = new int[inputSize];
// No validation that actual array lengths match inputSize
```

**Impact:** Memory leaks, null pointer dereference.

---

### 3. Unhandled Promise Rejection in onCreate
**File:** `entry/src/main/ets/InputMethodExtensionAbility/InputMethodService.ets`
**Lines:** 22-40

**Issue:** Dictionary loading is async but `onCreate` doesn't await.

```typescript
optimizedPredictionModel.load().then(() => {
  // ...
}).catch((err: Error) => {
  console.error('[HOSKEY] Failed to initialize prediction model', err);
});  // Promise not awaited
```

**Impact:** Keyboard appears with uninitialized dictionary, empty suggestions.

---

## High Severity Issues

### 4. Lifecycle Race Condition in KeyboardController
**File:** `entry/src/main/ets/InputMethodExtensionAbility/model/KeyboardController.ets`
**Lines:** 26-44, 333-420

**Issue:** `panelReady` flag can be false when `inputStart` event fires.

**Impact:** Keyboard flickers, doesn't respond to input.

---

### 5. queryTextFromIME Not Implemented
**File:** `entry/src/main/ets/InputMethodExtensionAbility/model/KeyboardController.ets`
**Lines:** 320-330

**Issue:** Method contains TODO and always returns empty string.

```typescript
// TODO: Implement proper text query when API is available
resolve('');
```

**Impact:** AutoCorrect and context-aware predictions don't work on first keystroke.

---

### 6. Null Pointer Risk in ProcessSwipePath
**File:** `entry/src/main/cpp/napi_init.cpp`
**Lines:** 862-888

**Issue:** `StringToNapiValue()` can return nullptr but result is used without check.

```cpp
napi_value bestWordValue = StringToNapiValue(env, ranked[0].word);
napi_set_named_property(env, obj, "bestWord", bestWordValue);  // Passes nullptr
```

**Impact:** Crash when accessing property in JavaScript.

---

### 7. Missing Exception Handling in LoadDictionaryExecute
**File:** `entry/src/main/cpp/napi_init.cpp`
**Lines:** 217-244

**Issue:** No try-catch around file operations in async worker.

**Impact:** Unhandled exception in worker thread, process abort.

---

### 8. Argument Index Mismatch in ProximityInfo Constructor
**File:** `entry/src/main/cpp/proximity_info_napi.cpp`
**Lines:** 78-94

**Issue:** Inconsistent parameter ordering between argument extraction and constructor call.

**Impact:** ProximityInfo initialized with wrong data, autocorrection fails.

---

## Medium Severity Issues

### 9. Unsafe Type Casting in NativeDictionary
**File:** `entry/src/main/ets/InputMethodExtensionAbility/model/NativeDictionary.ets`
**Lines:** 193, 271, 288, 312

**Issue:** Using `as` type assertions without runtime validation.

```typescript
const nativeStats: NativeDictStats = nativeDict.getStats() as NativeDictStats;
```

**Impact:** Runtime property access on wrong types.

---

### 10. Missing Dictionary File Validation
**File:** `entry/src/main/ets/InputMethodExtensionAbility/model/NativeDictionary.ets`
**Lines:** 164-171

**Issue:** Copied file not validated before loading.

**Impact:** Silent failure if file is corrupted.

---

### 11. Sound Feedback Not Implemented
**File:** `entry/src/main/ets/InputMethodExtensionAbility/model/FeedbackController.ets`
**Lines:** 59-67

**Issue:** Method is a no-op with TODO comment.

```typescript
async playSound(type: FeedbackType): Promise<void> {
    // TODO: Implement sound playback when sound resources are available
}
```

**Impact:** Incomplete user feedback.

---

### 12. Incomplete Dark Mode Resources
**File:** `entry/src/main/resources/dark/element/color.json`

**Issue:** Dark mode has 28 color definitions vs 269 in light mode.

**Impact:** Poor dark mode UX, potential unreadable text.

---

### 13. Promise.all() Masks Errors
**File:** `entry/src/main/ets/InputMethodExtensionAbility/model/FeedbackController.ets`
**Lines:** 70-76

**Issue:** No individual error handling in Promise.all().

**Impact:** Errors masked, feedback inconsistent.

---

### 14. Missing null check in altCount calculation
**File:** `entry/src/main/cpp/napi_init.cpp`
**Lines:** 872-876

**Issue:** If `ranked.size() == 0`, calculation causes unsigned underflow.

```cpp
size_t altCount = std::min((size_t)5, ranked.size() - 1);
```

**Impact:** Potential access violation.

---

### 15. Inconsistent Error Propagation
**File:** `entry/src/main/ets/InputMethodExtensionAbility/model/NativeDictionary.ets`

**Issue:** Errors only logged, not propagated to caller.

**Impact:** Silent failures, no user feedback.

---

## Low Severity Issues

### 16. Dead Code - Deferred Swipe Integration
**File:** `entry/src/main/ets/InputMethodExtensionAbility/model/input/InputPipeline.ets`
**Lines:** 218-241

**Issue:** Swipe handling code present but feature not integrated.

**Impact:** Code debt, maintenance burden.

---

### 17. Unused Logger
**File:** `entry/src/main/ets/InputMethodExtensionAbility/pages/Index.ets`
**Line:** 27

**Issue:** Logger created but never used.

```typescript
const logger = new Logger('Index');
```

**Impact:** Memory overhead.

---

### 18. Scattered Debug Flags
**Files:** Multiple ETS files

**Issue:** Inconsistent naming across files:
- `DEBUG_SELF_TEST` in InputMethodService.ets
- `DEBUG_MODE` in OptimizedPredictionModel.ets
- `DEBUG` in logger.ets
- `DEBUG_PREDICT`, `DEBUG_SWIPE` in Index.ets

**Impact:** Code maintainability issues.

---

### 19. Unused Imports
**Files:** Various

**Issue:** Some files have unused imports.

**Impact:** Code clutter.

---

## Recommendations

### Immediate (Critical)
1. Fix GetSuggestions parameter indices in `binary_dictionary_napi.cpp`
2. Make `onCreate` async and await dictionary loading
3. Add array bounds validation before allocation/access

### Short Term (High)
1. Add proper error handling to KeyboardController panel initialization
2. Implement `queryTextFromIME` or remove feature
3. Add null checks to NAPI return values
4. Add try-catch to LoadDictionaryExecute

### Medium Term
1. Complete dark mode color resources
2. Implement sound feedback or remove feature flag
3. Add runtime type validation for native calls
4. Consolidate debug flags to single location

### Long Term
1. Complete swipe integration or remove dead code
2. Add comprehensive error handling tests
3. Document NAPI contract with signatures
