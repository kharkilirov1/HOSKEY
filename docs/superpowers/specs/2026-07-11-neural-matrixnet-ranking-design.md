# Neural MatrixNet Ranking Completion — Design

**Date:** 2026-07-11  
**Scope:** close the remaining tap-suggestion ranking gap in the current Russian neural completion path. Swipe, voice, emoji, and multilingual prediction remain separate tracks.

## Current verified state

- The 1000-tree, 44-feature MatrixNet evaluator is bit-exact and loaded from `ranking.matrixnet`.
- Neural word-chain candidates are currently re-ranked by vocabulary frequency because `kMatrixNetForNeural` is `false`.
- MatrixNet regressed on-device when enabled with incomplete features: `lm_ngr_main` and `distance_transfeme` were zero.
- The current eight-query reference has seven acceptable cases and one clear divergence: `кот` surfaces the `который` family instead of `котик/котя/котёнок`.
- The exact DevEco SDK root is `C:/Program Files/Huawei/DevEco Studio/sdk`; a fresh `assembleHap` succeeds with that root.

## Decision

Use the existing word-chain logits and parsed transfeme table to feed MatrixNet features 1, 8, 16, 17 and their combo features. Capture the corresponding values from the installed Android Yandex Keyboard with Frida, reproduce the exact feature semantics when the hook identifies them, and use deterministic calibration only where the original native value cannot be reproduced directly.

Rejected alternatives:

1. **Calibration without Android ground truth:** fast but leaves avoidable uncertainty now that the rooted Android device and Frida server are available.
2. **Prefix-specific suppression rules:** could fix `кот` quickly but would preserve the underlying incomplete-ranker defect.

## Architecture

### 0. Android ground-truth capture

The connected MI 9 already runs `ru.yandex.androidkeyboard` as the current IME and exposes the process through Frida. A dedicated hook will attach to `libjni_ykeyboard3.so`, locate the ranker feature-vector construction/evaluation path, and capture only the 44-float ranker vector plus the visible query/candidate identifiers needed to correlate a record.

The reverse proceeds from the known exact HRNK model rather than blind tracing:

1. identify native code that reads the `features_ranker.json` catalog or passes 44 contiguous floats into the CatBoost/MatrixNet evaluator;
2. hook candidate call sites and validate the vector by checking feature 0/7 against word lengths and features 2/9 against static weights;
3. record features 1, 8, 16, 17, 28, and 30 for the eight reference prefixes;
4. verify combo identities `f28 == f16 + f8` and `f30 == f1 - f8` before accepting a hook;
5. preserve the hook script and a sanitized JSON trace as reproducible evidence.

The hook must not dump arbitrary process memory, surrounding editor text, credentials, or unrelated application data. Captures are limited to the Yandex Keyboard process and explicitly entered test prefixes.

### 1. Transfeme proximity

`TfmTrieDecoder` will expose a read-only directional proximity API:

```cpp
float Proximity(std::string_view original,
                std::string_view correction) const;
```

The score is the sum of parsed per-position transition weights for the aligned portion of the two strings. Exact characters use the self-weight already decoded into `SubstitutionClass`; confusable substitutions use their directional table weight; unrelated pairs and suffix characters contribute zero. This preserves the source table's `0..16.4` scale and naturally gives prefix completions a non-zero score. `distance_per_letter_transfeme` is `score / max(correction_codepoints, 1)`.

The implementation must not call `GenerateCandidates()` per candidate: ranking can evaluate many candidates on every keypress, so proximity must remain `O(min(original_length, correction_length))`.

### 2. Neural score transport

Replace the anonymous `pair<string, float>` result with a named prediction result that retains:

- top candidate word and raw logit;
- raw logit for the exact normalized input when that word exists in the output vocabulary;
- an explicit `has_original_logit` flag.

No additional MindSpore inference is allowed. Both candidate and original values come from the logits vector already produced by `YnnlmWordChain::Predict`.

### 3. Exact mapping with calibrated fallback

Add a pure calibration function:

```cpp
float CalibrateNgramLogit(float raw_logit,
                          const NgramCalibration& calibration);
```

If the Android trace proves an exact affine or otherwise simple mapping from the HOSKEY word-chain logit to `lm_ngr_main`, encode that mapping directly. If exact reproduction requires unavailable hidden state, use an affine monotonic fallback with finite-value handling and clamping:

```text
calibrated = clamp(scale * raw_logit + bias, low, high)
```

`low/high` must cover the MatrixNet feature-8 border range (`2.79857..34.73030`). `scale` and `bias` are fitted to the captured Android feature values first; reference-order grid search is only the fallback objective when exact feature pairs are unavailable. The transform must be monotonic (`scale > 0`) so it cannot reverse the neural model's own order before MatrixNet feature fusion.

If the original logit is absent, `original_lm_ngr_main` is `low`; `neural_oov` records the missing original score. Invalid/non-finite candidate logits disable MatrixNet for that candidate and preserve the current frequency fallback.

### 4. Feature construction and fallback

Build neural `RankerFeatures` in one testable helper. It must populate:

- feature 1: calibrated original `lm_ngr_main`;
- feature 8: calibrated candidate `lm_ngr_main`;
- feature 16: transfeme proximity;
- feature 17: proximity per candidate letter;
- features 28 and 30 through the existing exact `ToVector44()` combo formulas;
- all currently available static, dictionary, edit, BLEU, precision, and permutation features.

MatrixNet is used only when the model, vocabulary, transfeme data, and calibration are valid. Otherwise the code must execute the existing frequency-dominant fallback without changing next-word behavior.

### 5. Calibration trace

In debug builds, emit one bounded diagnostic record per candidate containing query, candidate, raw candidate/original logits, transfeme proximity, static weight, and MatrixNet score. The trace must not log surrounding editor text and must be disabled in release behavior.

A host calibration script consumes these records plus the checked reference cases, evaluates candidate order with the real HRNK model, and writes a small generated calibration header or JSON consumed by the engine. The selected constants and reference score become deterministic regression-test inputs.

## Test strategy

Follow red-green-refactor for every behavior:

1. `TfmTrieDecoder::Proximity` properties using the real `rules.transfeme` asset: exact prefix score is non-zero; confusable substitution scores above unrelated substitution; longer unrelated suffix lowers the per-letter value.
2. Calibration unit tests: monotonicity, clamp endpoints, NaN/Inf rejection, absent-original fallback.
3. Feature-vector unit test: indices 1, 8, 16, 17, 28, and 30 contain the expected values.
4. Existing native hardening, bundle, smoke, and 4491-query invariant suites remain green.
5. Frida hook validation: captured 44-float vectors satisfy the known feature identities and contain plausible feature ranges.
6. Fresh HAP build and emulator A/B on the eight reference prefixes.

## Acceptance criteria

- `kMatrixNetForNeural` is no longer a permanently false compile-time flag.
- The Android hook target is proven by feature-length and combo-identity witnesses, and the reusable hook plus sanitized reference trace are stored with the project tooling.
- All neural MatrixNet candidates carry valid features 1/8/16/17; invalid data takes the existing fallback path.
- The seven currently acceptable reference queries do not regress in top-3 overlap.
- `кот` no longer ranks the `который` family ahead of all `кот*` content-word completions; at least one of `котик`, `котя`, `котёнок` is in top 3.
- `engine_invariants` reports 4491 queries with zero empty, duplicate, unsorted, and oversize violations.
- Suggestion latency is measured before and after on the same emulator; the added CPU-only feature work must not add more than 5 ms to median ranking latency.
- A fresh HAP is built from the modified checkout and the tested emulator artifact hash is recorded.

## Rollback

The current frequency re-rank remains an explicit runtime fallback. If calibrated MatrixNet fails the acceptance gate, the new feature code stays testable but neural MatrixNet remains disabled until the calibration data is corrected; no hard-coded word suppression is permitted.
