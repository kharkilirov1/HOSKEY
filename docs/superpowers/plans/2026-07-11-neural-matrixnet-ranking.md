# Neural MatrixNet Ranking Completion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans`. This checkout uses inline execution on `codex/neural-matrixnet-ranking`; do not dispatch subagents.

**Goal:** recover the real Yandex ranker features on Android, feed MatrixNet features 1/8/16/17 in HOSKEY, and remove the remaining Russian tap-suggestion ranking divergence without regressing the existing seven reference cases.

**Architecture:** use Frida call discovery plus 44-float combo invariants to locate the Android feature vector. Implement pure, host-tested transfeme and neural-score mapping helpers, retain the existing frequency fallback, then enable MatrixNet only for valid feature records and verify the exact modified HAP on the emulator.

**Tech Stack:** C++17, PowerShell, Python 3.12, Frida 16/17, Android `adb`, HarmonyOS Hvigor/CMake, MindSpore Lite NDK.

## Global Constraints

- Never log surrounding editor text, passwords, or unrelated Android process memory.
- Android capture is limited to `ru.yandex.androidkeyboard`, explicit test prefixes, and 44-float candidate feature records.
- No extra MindSpore inference per candidate; reuse the existing decoder logits vector.
- Next-word prediction keeps raw context-logit ordering.
- Any invalid/non-finite feature record takes the existing frequency fallback.
- No hard-coded word-specific suppression.
- No git commit, push, install replacement, or external publication without explicit user approval; local build/install to test devices is allowed by the task.

---

### Task 1: Android ranker feature discovery and capture

**Files:**
- Create: `tools/frida_ranker_features.js`
- Create: `tools/capture_ranker_features.py`
- Create: `tools/ranker_feature_reference.json`

**Interfaces:**
- `frida_ranker_features.js` sends `call-summary`, `feature-vector`, and `suggestion-text` messages.
- `capture_ranker_features.py discover` records module-relative call targets during `getSuggestions`.
- `capture_ranker_features.py capture --targets <offsets>` validates pointer arguments as 44-float vectors.
- A vector is accepted only if all values are finite and:
  - features 0 and 7 are integer-like lengths in `[0, 32]`;
  - features 14, 15, and 43 are boolean-like;
  - `abs(f28 - (f16 + f8)) <= 1e-3`;
  - `abs(f30 - (f1 - f8)) <= 1e-3`.

- [ ] **Step 1: Implement discovery mode**

Hook the exported `DictionaryFacilitator_getSuggestions`, start `Stalker.follow()` for its thread on entry, collect internal `libjni_ykeyboard3.so` call targets, and emit sorted module-relative offsets on leave.

- [ ] **Step 2: Verify discovery against a live call**

Run:

```powershell
python tools/capture_ranker_features.py discover --package ru.yandex.androidkeyboard --seconds 30
```

Trigger the explicit prefix `кот` with `adb shell input text`. Expected: at least one `getSuggestions` interval and a non-empty internal call summary.

- [ ] **Step 3: Implement validated vector scanning**

For every discovered target, attach an interceptor. On entry, examine direct pointer arguments `x0..x7`, then one pointer indirection within the first 96 bytes of object-like arguments. Read exactly 44 little-endian floats only from readable mapped ranges. Emit nothing unless the invariant validator passes.

- [ ] **Step 4: Capture the eight reference prefixes**

Capture `прог`, `мага`, `привет`, `спаси`, `здрав`, `погод`, `дом`, and `кот`. Store only candidate text, query prefix, module-relative hook offset, and the 44 floats in `tools/ranker_feature_reference.json`.

- [ ] **Step 5: Verify the hook witness**

Run:

```powershell
python tools/capture_ranker_features.py verify tools/ranker_feature_reference.json
```

Expected: every stored record has 44 finite floats, combo identities pass, and the same hook offset is observed for multiple prefixes.

---

### Task 2: Transfeme proximity test and implementation

**Files:**
- Modify: `entry/src/main/cpp/hosdict_engine/tfmtrie_decoder.h`
- Modify: `entry/src/main/cpp/hosdict_engine/tfmtrie_decoder.cpp`
- Create: `entry/src/main/cpp/build_test/test_neural_ranker_features.cpp`
- Create: `tools/run_neural_ranker_features_test.ps1`

**Interfaces:**

```cpp
float TfmTrieDecoder::Proximity(std::string_view original,
                                std::string_view correction) const;
```

- [ ] **Step 1: Write the failing proximity test**

Load `rules.transfeme` from the real `.hosdict`. Assert:

```cpp
const float exact = tfm.Proximity("кот", "кот");
const float completion = tfm.Proximity("кот", "котик");
const float confusable = tfm.Proximity("кот", "кат");
const float unrelated = tfm.Proximity("кот", "южя");
Check(exact > 0.0f, "exact transfeme proximity is non-zero");
Check(completion == exact, "suffix continuation preserves prefix proximity");
Check(confusable > unrelated, "confusable substitution beats unrelated letters");
Check(completion / 5.0f < exact / 3.0f,
      "per-letter proximity penalizes long continuation");
```

- [ ] **Step 2: Run RED**

```powershell
powershell -ExecutionPolicy Bypass -File tools/run_neural_ranker_features_test.ps1
```

Expected: compile failure because `Proximity` does not exist.

- [ ] **Step 3: Implement minimal proximity**

Decode UTF-8, iterate aligned positions, and add the directional parsed weight for `(original[i], correction[i])`. Use the stored self-weight for equal characters, direct class weight first, reverse class weight only when the direct edge is absent, and zero for unrelated pairs or suffix positions. Complexity must be `O(min(n, m) * class_width)` with no allocations beyond UTF-8 decoding.

- [ ] **Step 4: Run GREEN**

Expected: all four proximity assertions pass using the real asset.

---

### Task 3: Neural feature mapping test and implementation

**Files:**
- Modify: `entry/src/main/cpp/hosdict_engine/ml_ranker.h`
- Modify: `entry/src/main/cpp/hosdict_engine/ml_ranker.cpp`
- Modify: `entry/src/main/cpp/build_test/test_neural_ranker_features.cpp`

**Interfaces:**

```cpp
struct NgramCalibration {
  float scale;
  float bias;
  float low;
  float high;
};

struct NeuralRankerFeatureInput {
  std::string original;
  std::string correction;
  float raw_original_logit;
  float raw_correction_logit;
  bool has_original_logit;
  float original_static;
  float correction_static;
  float transfeme_proximity;
  bool original_known;
};

bool CalibrateNgramLogit(float raw, const NgramCalibration& c, float* out);
RankerFeatures BuildNeuralRankerFeatures(const NeuralRankerFeatureInput& input,
                                         const NgramCalibration& calibration);
```

- [ ] **Step 1: Add failing calibration and vector tests**

Assert monotonic affine mapping, clamping, NaN/Inf rejection, absent-original fallback to `low`, and exact `ToVector44()` values at indices 1, 8, 16, 17, 28, and 30.

- [ ] **Step 2: Run RED**

Expected: compile failure because the calibration and feature-builder interfaces do not exist.

- [ ] **Step 3: Implement calibration and the feature builder**

Reuse the existing UTF-8 edit/BLEU/permutation helpers by moving only the minimum pure helpers needed into `ml_ranker.cpp`. Do not change `EvalMatrixNet` or the 44-feature order.

- [ ] **Step 4: Run GREEN and existing MatrixNet tests**

Run the new focused test and `test_hardening`; expected: zero failures.

---

### Task 4: Preserve candidate and original logits without extra inference

**Files:**
- Modify: `entry/src/main/cpp/hosdict_engine/ynnlm_word_chain.h`
- Modify: `entry/src/main/cpp/hosdict_engine/ynnlm_word_chain.cpp`
- Modify: `entry/src/main/cpp/build_test/test_neural_ranker_features.cpp`

**Interfaces:**

```cpp
struct YnnlmPrediction { std::string word; float raw_logit; };
struct YnnlmPredictionBatch {
  std::vector<YnnlmPrediction> candidates;
  bool has_original_logit = false;
  float original_raw_logit = 0.0f;
};

YnnlmPredictionBatch SelectYnnlmPredictions(
    std::string_view prefix,
    const std::vector<std::string>& rank_words,
    const std::vector<float>& logits,
    size_t topk);
```

- [ ] **Step 1: Add a failing selector test**

Use a synthetic vocabulary/logit vector to prove prefix filtering, descending top-k, exact-prefix score retention even when it is outside top-k, non-finite exclusion, and deterministic tie order.

- [ ] **Step 2: Run RED**

Expected: missing selector API.

- [ ] **Step 3: Implement selector and change `Predict` return type**

Call the selector on the already produced `logits`. Do not execute any additional model stage.

- [ ] **Step 4: Run GREEN and compile all native sources**

Expected: focused test passes and all callers compile after Task 5 wiring.

---

### Task 5: Wire exact/captured mapping into `KeyboardEngine`

**Files:**
- Modify: `entry/src/main/cpp/hosdict_engine/keyboard_engine.cpp`
- Modify: `entry/src/main/cpp/hosdict_engine/keyboard_engine.h`
- Modify: `entry/src/main/cpp/CMakeLists.txt` only if a new source unit is introduced

**Interfaces:**
- Consume `YnnlmPredictionBatch`, `TfmTrieDecoder::Proximity`, and `BuildNeuralRankerFeatures`.
- Preserve frequency fallback for invalid calibration/features and for next-word prediction.

- [ ] **Step 1: Add failing integration assertions**

Extend the focused host test with a synthetic candidate batch and real HRNK asset to show that all candidates reach MatrixNet with populated feature indices and invalid logits select the fallback marker.

- [ ] **Step 2: Run RED**

Expected: current compile-time `kMatrixNetForNeural=false` path cannot satisfy the assertions.

- [ ] **Step 3: Replace the compile-time flag with runtime validity**

Populate features 1/8/16/17 from the new helpers, call `ScoreOne`, stable-sort MatrixNet-valid candidates, and frequency-sort only fallback candidates. Preserve manual/exact lead entries and deduplication.

- [ ] **Step 4: Run native verification**

Run focused features, hardening, ynnlm bundle, engine smoke, and 4491-query invariants. Expected: all pass with zero invariant violations.

---

### Task 6: Fit or encode the Android mapping

**Files:**
- Create: `tools/fit_neural_ranker_calibration.py`
- Modify: `entry/src/main/cpp/hosdict_engine/ml_ranker.h`
- Modify: `entry/src/main/cpp/build_test/test_neural_ranker_features.cpp`

- [ ] **Step 1: Compare Android features to HOSKEY raw traces**

For matched query/candidate pairs, fit `android_feature = scale * raw_logit + bias`. Report sample count, MAE, max error, and monotonicity.

- [ ] **Step 2: Select exact mapping or bounded fallback**

If max error is within `1e-3`, encode the exact affine mapping. Otherwise grid-search positive `scale` and `bias` against Android feature MAE and the eight-query ordering, clamp to the observed Android range, and record the selected constants plus metrics in `tools/ranker_feature_reference.json`.

- [ ] **Step 3: Freeze constants in regression tests**

Add real captured raw→expected pairs. Expected: the calibration test reproduces Android feature 8 within the recorded tolerance.

---

### Task 7: Build, install, and device A/B

**Files:**
- Modify: `RELEASE_QUALITY.hermes.md`
- Modify: `SESSION_HANDOFF.md`

- [ ] **Step 1: Build a fresh HAP**

```powershell
$env:JAVA_HOME='C:\Program Files\Huawei\DevEco Studio\jbr'
$env:DEVECO_SDK_HOME='C:\Program Files\Huawei\DevEco Studio\sdk'
& 'C:\Program Files\Huawei\DevEco Studio\tools\hvigor\bin\hvigorw.bat' `
  --mode module -p product=default -p buildMode=debug assembleHap --no-daemon
```

Expected: `BUILD SUCCESSFUL`.

- [ ] **Step 2: Record artifact identity and install**

Record SHA-256 of the HAP, install with `hdc install -r`, force-stop/restart, and verify the new process loaded `ranking.matrixnet` and the word-chain models.

- [ ] **Step 3: Capture the eight HOSKEY reference queries**

Compare top-3 results against the Android reference. Expected: the seven accepted queries do not regress and `кот` has at least one of `котик`, `котя`, `котёнок` in top 3 ahead of the `который` family.

- [ ] **Step 4: Measure latency and run final invariants**

Median added ranking latency must be at most 5 ms on the same emulator. Re-run the 4491-query invariant suite after the final edit.

- [ ] **Step 5: Update handoff documentation truthfully**

Record the Frida hook witness, exact/fallback mapping metrics, test outputs, HAP hash, Android/Harmony result tables, remaining risks, and rollback path. Do not claim full suggestion closure unless every acceptance criterion is witnessed.

