# Neural/model completion ledger — 2026-07-16

This ledger supersedes the 2026-07-13 and 2026-07-14 snapshots for the current
checkout. Claims below are tied to witnesses produced after the final
production-code edit.

**Outcome: COMPLETE for the requested neural/model branch scope.** This is not
a release-readiness claim: the final emulator memory footprint remains high,
the HAP is unsigned, and no physical HarmonyOS device was available.

## Checkout and exact artifacts

- Branch: `codex/neural-matrixnet-ranking`.
- The checkout was already dirty. Existing modified/untracked work was
  preserved; no reset, clean, commit, push, or SwiftKey operation was made.
- Production `.hosdict`: 59 assets, 132,370,752 bytes, SHA-256
  `840368E9D866FBB3587DB0B09F10230A75EF9F87016A191D76EA3853143C3835`.
- Production HAP:
  `entry/build/default/outputs/default/entry-default-unsigned.hap`,
  151,289,519 bytes, SHA-256
  `078D5ED0059CA17CB6DC3EF1F51BB60D4109C264F410E65B357C9542B0592085`.
- `entry/build/default/outputs/default/app/entry-default.hap` was byte-identical
  to the production HAP above.
- Test HAP:
  `entry/build/default/outputs/ohosTest/entry-ohosTest-unsigned.hap`,
  133,925,935 bytes, SHA-256
  `7CAD5B790D4EE33D0ECECE9BBB5694C6F13DC859120D8E325BD4A6719644B9B0`.
- APP: `build/outputs/default/HOSKEY-default-unsigned.app`, 151,291,126
  bytes, SHA-256
  `64931387FE5786BB8F353A7B2FD9C8B689B0F8A2FB120325AB3D685583E9BF06`.
- Both HAP paths were passed directly to `hdc install -r`; each returned
  `install bundle successfully`. Device policy still prevents independent
  shell hashing of the installed `/data/app/...` HAP, so this limitation is
  recorded rather than inferred away.

Artifact hashes are recorded in
`build/final-audit-20260716/artifact-hashes-final.json`.

## Defect found in the final audit

The first final device run exposed `word_chain=invalid:subword_dec_contract`.
The new validator had incorrectly assumed two `subword_dec.ms` outputs. A
direct MindIR dump proved the actual graph contract:

- inputs: token `1`, state `2500`;
- outputs: logits `7000`, hidden `500`, state `2500`.

`YnnlmWordChain` now validates all three outputs. This restored the complete
YNNLM chain and changed Autocorrect from `producer_unavailable` to the intended
`tree_policy` path. The device then produced `превет -> привет`, one undo, and
suppressed the repeated replacement.

## Evidence by branch

### 1. Word/subword NNLM and RNNT swipe

- Controlled NNLM device A/B on the exact production HAP:
  - ON: `[привет, просто, про, прям, прости, правильно]`;
  - OFF: `[приду, прошу, прямо, права, проще, прост]`;
  - ON diagnostics: `contract=valid`, `load=loaded`, non-zero
    `invoke_count/scored_count`, `fallback=none`, 131 ms.
- Word chain diagnostics after the contract fix:
  `loaded:words=107989,subwords=7000,apps=1000`, all optional MindIR build
  statuses `0`, and exact native `char_dec` loaded.
- Controlled RNNT ON route: `rnnt_matrixnet_v1`, 7 frames, 3 scored
  candidates, `fallback=none`, 236 ms. OFF disabled RNNT and used the tagged
  MatrixNet backoff; the ranking score changed even where the top list stayed
  stable.
- Real continuous-gesture E2E: 41 touch points, `swipe_input=valid`, 7 RNNT
  frames, 2 RNNT-scored candidates, 24 MatrixNet-scored candidates, 205 ms,
  and `handled=1` in the real editor.
- Negative device audit covers every one of 11 missing RNNT components, all 3
  required NNLM components, corrupt/wrong-shape graphs, malformed path,
  empty/NaN coordinates, and unsupported layout: 26/26 passed.

### 2. Neural emoji search and panel integration

- Device UI E2E typed a query, obtained model-only results, cleared back to
  categories, and inserted the chosen emoji.
- Intended route: `utf16be_comptrie_prefix`, `contract=valid`, `fallback=none`;
  observed non-zero invoke/scored counters and sub-millisecond inference
  (160–526 us in the final E2E).
- Package gate binds search mapping to the real catalog and rejects the old
  525-class assumption.

### 3. Context emoji suggestions

- Controlled device A/B:
  - `я люблю` ON -> `[😘, ❤️]`, OFF -> `[]`;
  - another context -> `[❣️, ❤️, ♥️]`.
- Real typing E2E exercised native -> NAPI -> ArkTS -> suggestion bar -> editor.
- Intended route: `k13321_context_emoji_runtime`, `contract=valid`, order 10,
  catalog 1752, non-zero invoke/scored counters, `fallback=none`.

### 4. Original TapModel

- Coherent default 122.5 bundle, ranker, classifier, and exact feature schema
  are packaged as one pinned chain.
- Controlled ON/OFF changed candidate order.
- Real-touch E2E exercised centered taps and a boundary tap between adjacent
  keys. Both used `path=ranker_classifier`; the score changed from
  `0.813805521` to `0.190550342`, with 6 touch events and real editor text.
- Missing/invalid geometry remains explicitly tagged and falls back safely.

### 5. Neural autocorrect variants

- Default, `KEYBOARD-13321`, and `KEYBOARD-13421` all executed the recovered
  79-feature HOBL forest on device with non-zero invoke/scored counters.
- Exact producer contract:
  `autocc_producer_contract=ynnlm1225_char_eos_empty_v1`,
  `autocc_char_score_available=1`.
- Observed model probabilities: default/K13421 `0.967416942`, K13321
  `0.998952866`; all were above the recovered 0.65 threshold for the probe.
- Real editor E2E: `превет -> привет `, backspace restored `превет`, and the
  next delimiter logged `suppressed_after_undo` instead of replacing again.
- Blocker, blacklist, protected-field, disabled-model, and heuristic/TfmTrie
  fallback paths remain separate and tagged.

## Final verification

- `build_hosdict.py`: 59 assets, success.
- Raw and HAP `package_asset_gate.py`: success.
- Raw and HAP `strict_hosdict_gate.py`: structural success with the explicit
  HSKD-v1 missing-variant-manifest waiver.
- `assembleApp`, production `assembleHap`, and ohosTest `assembleHap`:
  `BUILD SUCCESSFUL`.
- Focused host matrix: 17/17 scripts exited 0, including engine acceptance,
  every model runner, converters, package pins, and negative validators.
- Python: 64 tests, 64 pass.
- ArkTS unit tests: 19 tests, 19 pass.
- `git diff --check`: pass.
- Final uninterrupted device regression on emulator `127.0.0.1:5555`: 8/8:
  PredictionBackspace, SwipeGesture, ContextEmoji, EmojiSearch,
  AutocorrectUndo, NeuralModelNegative, TapModel, PrivacyPassword.
- Device result ledger:
  `build/device_e2e/final-green-testhap-7cad5b79-20260716/summary.json`.
- Exact-HAP controlled model A/B log:
  `build/device_e2e/final-exact-hap-078d5ed0-20260716/setupguide-model-ab-hilog.log`.

## Performance and residual risks

- Controlled device latencies on the final production HAP:
  NNLM 131 ms, RNNT 236 ms, context emoji 30.565 ms, Autocorrect 19.716 ms.
  Ordinary warm NNLM samples were usually 26–48 ms; the collected mixed
  cold/warm sample included a 584 ms cold outlier.
- Final all-model SetupGuide process memory:
  PSS 526,226 kB, RSS 628,540 kB, USS 504,172 kB. This is not release-grade and
  remains the largest unresolved performance risk; a lazy/mmap model store is
  still needed for a low-memory target.
- Verification used the HarmonyOS emulator, not a physical device.
- The current HSKD v1 format has no variant-manifest schema. Build-time SHA-256
  pins and atomic package gates prevent the known mixing cases, but the strict
  gate correctly keeps the metadata provenance waiver visible.
- The HAP is unsigned debug output.
- Independent installed-HAP readback is blocked by device permissions.
- Password focus is a HarmonyOS platform secure takeover; HOSKEY is hidden as
  required. No SwiftKey selection, hook, restore, or switch is part of this
  implementation or verification.
