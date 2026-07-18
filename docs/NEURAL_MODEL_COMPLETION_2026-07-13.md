# Neural/model completion ledger — 2026-07-13

This file records fresh evidence for the current checkout. `reported` facts from
older documents are not treated as verification.

**Overall outcome: PARTIAL.** Word/subword NNLM and real-editor RNNT swipe now
have device-level intended-path witnesses. Emoji search/context, Original
TapModel and neural autocorrect remain deliberately unhooked because their
stripped-native decision contracts are still incomplete.

## Baseline

- Branch: `codex/neural-matrixnet-ranking`.
- The working tree was already dirty before this task (28 modified and 19
  untracked entries); those changes are preserved as user work.
- Baseline package was 111185984 bytes, SHA-256
  `2FCB1B0C27B1CACA4195781360422C833D3DC6D2CCC0DBFFA9E1B698B6F06B67`.
- Current package is 111186208 bytes, SHA-256
  `7D69FD779141531E782F853BA8576C17DE6576BFC36061641684EA9ECA34E8B5`;
  the only model-route change is a provenance-pinned Default
  `ranking.matrixnet` replacing the incoherent K13421 forest.
- Fresh package gate: 42 assets, passed.
- Fresh host acceptance: passed.
- Fresh HAP: `entry/build/default/outputs/default/app/entry-default.hap`,
  128898478 bytes, SHA-256
  `5F0780E46E6A54BE8E64728B88BD53D1832282F1222B02F484A42990ADC9FAF9`.
- Fresh test HAP:
  `entry/build/default/outputs/ohosTest/entry-ohosTest-unsigned.hap`,
  112586190 bytes, SHA-256
  `5EF01B2A4ED69E7555AAC69E77018BA6ADA0BD2314C75CFB80820248CF83DA72`.
  Its ZIP has zero native `.so` entries, so the x86 emulator no longer tries to
  load the ARM-only production bridge from the test process.
- The exact production HAP above was installed on the HarmonyOS emulator and
  passed the real-editor continuous-gesture test. Production source files are
  older than the HAP; the only later code edit is the strict-gate test wrapper.

## Route registry

| family | hypothesis | evidence | counterevidence | status | blocker | next discriminating experiment |
|---|---|---|---|---|---|---|
| NNLM | packaged word/subword graphs execute on device and causally rerank | exact-HAP diagnostics show runtime encoder, finite scores, cache miss/hit and context-9 inference; ON reached `invoke_count=7/scored_count=320`, while OFF preserved counters and reported `fallback=disabled:debug_switch` with a different order | corrupt/wrong-shape graph was negative-tested on host, not on device | verified-device | device corruption/missing-graph case is not automated | install an intentionally corrupted diagnostic package on a disposable target and prove safe fallback |
| RNNT swipe | the recovered sampling/pre/post/joint/predictor chain executes for a real Russian gesture and commits its result | real editor E2E produced pan + touch-up telemetry, 41 points, 7 RNNT frames, one scored candidate, `fallback=none`, and committed `привет `; synthetic ON/OFF switched `rnnt_matrixnet_v1` to `matrixnet_backoff_v1` without incrementing counters | the 350 ms cold route is budget-limited and only one real word is covered | verified-device | no multi-word device corpus or component-removal device matrix | run a physical-device golden gesture corpus and per-component missing/corrupt cases |
| emoji search | `search_emoji_model` is a neural 525-class graph | none | the asset is a FlatBuffer mapping/trie plus 6450 catalog groups and 1898 unique emoji; `[1,1,525]` is the NNLM `emb_app` embedding | refuted | exact stripped-native query-to-leaf/ranking algorithm is unavailable | obtain a native oracle or recover `getEmojis` internals before implementing production search |
| emoji context | `emoji_model` can consume raw text by itself | the embedded decoder is `state[1,2500] -> softmax[1,1752]`; its 1752-entry rank catalog and exact 2500-state composer are recovered | HOSKEY does not export the required encoder `state[4000] + hidden[500]`; the matching K13321 chain is not packaged | blocked | no coherent text-to-state route | first implement and verify the matching K13321 NNLM state path, then connect the decoder |
| TapModel | default ranker/classifier are oblivious-tree ensembles and `tap_models_default` supplies per-layout touch distributions | ranker is 1000 depth-8/37-feature trees; classifier is 2000 depth-6/76-feature trees; the six-layout protobuf schema and the complete 37 ranker feature order are recovered | exact classifier `2N+2` feature semantics and raw-vs-calibrated threshold behavior are not recovered | partial | no trustworthy classifier/original-runtime oracle | keep the generic tree backend structural-only; obtain classifier vectors/decisions before a production hook |
| autocorrect | all three variants share one 79-feature depth-6 oblivious-tree contract | default has 2000 trees; K13321/K13421 have 1999; forest structure, 79-feature catalog, neural splits and config threshold `0.65` are proven | original comparator/leaf-bit convention, raw-vs-sigmoid transform, blocker ordering and final commit edge remain unproven | partial | no original feature-vector/decision oracle | capture all 79 native features plus raw decision, then run single-split and threshold sweeps |
| package provenance | selective semantic packaging is sufficient | package has 42 non-overlapping aligned entries; strict negative-tested gate now rejects bad magic/version/JSON/OOB/overlap/duplicate names/alignment and binds an optional whole-package SHA | the HSKD v1/v2 manifest still has no defined per-asset variant/hash-binding schema | partial | variant manifest remains absent | define and consume a versioned route manifest before claiming strict provenance complete |
| privacy gate | model routes are safe in sensitive editors | `PrivacyGate` blocks PASSWORD/PIN/NUMBER_PASSWORD/visible-password patterns, BASIC capability and unknown editor state before context/model/learning calls; host assertions pass and a normal editor transitioned from `editor_unknown` to `allowed` on device | no password-field device test; HarmonyOS incognito platform signal is still unproven | verified-host/partial-device | password-device and incognito signal witnesses are missing | add a disposable password editor E2E and identify a platform incognito signal or retain fail-closed behavior |

## Evidence ledger

| claim | state before | change | intended-path witness | negative witness | device witness | status | residual risk |
|---|---|---|---|---|---|---|---|
| word/subword NNLM invokes MindSpore and affects ordering | historical logs only | diagnostic `disable_ynnlm` switch added through C++/NAPI/ArkTS | ON: `ynnlm_encoder=runtime`, context 9, `invoke_count=7`, `scored_count=320`, finite scores, 251 ms | OFF: counters stay 7/320, `fallback=disabled:debug_switch`, latency 0 and candidate order changes | exact installed HAP, SetupGuide ON/OFF probe | verified-device | missing/corrupt graph was not exercised on device |
| continuous gesture reaches RNNT and commits | per-key raw touch produced only geometry telemetry | first RNNT candidate is always scored; `PanGesture` forwards global coordinates through the existing route; handled state is non-reactive | `swipe=rnnt_matrixnet_v1`, 41 points, 7 frames, one scored candidate, first score `-17.074379`, `fallback=none` | pre-fix run committed only `п` with zero route logs; RNNT OFF uses MatrixNet backoff, preserves counters and changes top MatrixNet score | exact installed HAP, real editor, `привет ` committed, Hypium 1/1 pass | verified-device | one golden word; 304 ms RNNT / 351 ms total is budget-limited |
| ohosTest can drive the production IME on an x86 emulator | test HAP attempted to load ARM-only `libjni_ykeyboard3.so` | exclude the production bridge from the `ohosTest` target | final test HAP ZIP has `TEST_NATIVE_ENTRY_COUNT=0` while the separately installed production HAP owns native inference | stale test package with the bridge failed at ABI load | final 51.028 s editor E2E pass | verified-device | target-specific packaging must be preserved |
| sensitive editors are fail-closed before model/context/learning | no central gate | added `PrivacyGate` and propagated decisions through controller/model/page | password patterns, BASIC capability and unknown editor state are blocked in host assertions | normal editor is initially blocked as unknown, then explicitly allowed after metadata arrives | normal-editor transition observed; no password editor E2E | verified-host/partial-device | incognito platform signal and password-device witness remain missing |
| emulator disconnect is an app/model crash | unknown | none | none | two crash reports identify host `GPU Thread is hung`, error 802003; second has a `glFenceSync` timeout | emulator process was killed by its crash server | refuted | model load increases pressure but is not the recorded crash type |
| autocorrect raw variants are structurally decodable | raw blobs were only named assets | temporary conversion probe | exact source slices and forest invariants: 6225/6225, 6218/6218, 6264/6264; neural features 72..78 occur in 1435--1473 trees | zero-border feature IDs are unused rather than silently invented | not yet | verified-host provenance/structure | comparator, bit order, score transform and runtime features remain blocked |
| TapModel bundle is opaque random data | unknown | generic variable-depth `HOBL` backend and provenance-pinned converter added | exact protobuf descriptor yields six layouts; converter includes the compact first tree through the model's relative-pointer vector and recovers 1000/37/3316; C++ loads and scores the result | the old contiguous scan saw only 999 trees, and earlier K13321/K13421 experiment slices were shown to be corrupt | not yet | verified-host structural backend only | classifier semantics and original-runtime oracle remain blocked |
| emoji-search output is 525 neural classes | active hypothesis | none | decoded production asset contains 6450 groups/44165 references/1898 unique emoji | the only 525-wide tensor is NNLM application embedding `emb_app` | not yet | refuted | exact trie leaf/ranking remains blocked |
| contextual emoji can run from zero/random state | tempting fallback | none | exact state composer requires selected slices of encoder state 4000 plus hidden 500 | zero/random state bypasses the intended path and cannot prove contextual inference | not yet | rejected | matching K13321 encoder chain is required |
| same-basename `.tflite` proves raw/MS model role | old inventory behavior | inventory now analyzes only bytes that are themselves `TFL3` | `search_emoji_model`, `search_emoji_model.ms` and `emoji_model` no longer inherit plausible signatures from unrelated aliases | direct TFLite files are still independently analyzed | not yet | verified-host provenance guard | explicit source-slice alias manifest is still absent |
| Default ranker can atomically replace mixed K13421 route | package used K13421 HRNK SHA `90475f...` | pointer-aware converter emitted Default 1000-tree HRNK SHA `0a39e2...`; builder pins this hash and uses a variant-specific filename | fresh package passes structural/semantic gates and host engine acceptance; captured 44-feature vectors changed K13421→Default scores (`4.45350→4.61067`) | corrupt pinned bytes fail the build test | final HAP loads 1000-tree/44-feature ranker and swipe route reports finite MatrixNet score | verified-host and packaged-device | HSKD v1 still cannot bind variant provenance in-container |

## Final exact-HAP device witnesses

The device logs below are tied to the production HAP hash recorded in the
baseline section:

- `build/device_swipe_e2e_hilog_20260713_final.log` contains the SetupGuide
  diagnostic A/B probes and the real IME process route.
- NNLM ON top six: `просто, привет, про, прям, правда, при`; route counters are
  `invoke_count=7`, `scored_count=320`, `fallback=none`, latency 251 ms.
- NNLM OFF top six: `приду, прошу, прямо, права, проще, прост`; counters remain
  7/320 and diagnostics report `fallback=disabled:debug_switch`, latency 0.
- Synthetic RNNT ON: 7 frames, one scored candidate, 154 compute units,
  `fallback=none`, 218 ms and top MatrixNet score `2.37475824`.
- Synthetic RNNT OFF: zero frames/scored work, counters remain 1/1,
  `fallback=disabled:debug_switch`, 26 ms total backoff and top MatrixNet score
  `6.17879772`. The candidate text order happens to remain identical, so the
  claim is limited to model engagement and score/route delta, not rank delta.
- The real editor test injects a 21-point path through the centers of
  `п-р-и-в-е-т`; native telemetry records 41 points. The route is
  `swipe=rnnt_matrixnet_v1`, 12 candidates, 1060-word pool, 7 frames,
  `rnnt_scored=1`, `rnnt_budget_limited=1`, encode 185 ms, score 118 ms,
  RNNT 304 ms, total 351 ms and `fallback=none`.
- The editor then reports `committed_length=7` and contains `привет` (word plus
  delimiter). Hypium reports `Tests run: 1, Failure: 0, Error: 0, Pass: 1` in
  51.028 s. Condensed witness:
  `build/device_swipe_e2e_witness_20260713_final.log`.
- Final-audit rerun command:
  `aa test -b com.opentech.openboard -m entry_test -s unittest OpenHarmonyTestRunner -s class SwipeGestureE2ETest -s timeout 120000 -w 180000`.
  It passed 1/1 in 53.614 s. Its independent route sampled 38 native points,
  produced 6 RNNT frames and 3 scored candidates with `fallback=none`, RNNT
  227 ms / total 245 ms, then committed length 7. Witness:
  `build/device_swipe_e2e_witness_20260713_rerun_final.log`.
- A control rerun without `-s timeout 120000` stopped at the runner's default
  5-second per-case timeout before the intentional cold-load wait. The corrected
  command above distinguishes harness timeout from a model/runtime failure.
- The causal negative baseline is
  `build/device_swipe_e2e_hilog_20260713_run4.log`: the same injected path
  committed only `п`, emitted zero `[HOSKEY_SWIPE_ROUTE]` records and failed the
  assertion. Per-key raw `onTouch` alone therefore did not provide a continuous
  gesture route; the subsequent `PanGesture` path did.
- The 20-second wait in the E2E is intentional cold-start allowance for the
  106 MB container in the isolated IME process plus the SetupGuide diagnostics
  instance. It is not a production latency claim.

## Final host/package verification

- `build/final_focused_matrix_20260713.log`: 11/11 commands exited zero:
  oblivious-tree C++, converter, inventory, emoji validator, Tap bundle
  validator, package hash pin negatives, 23 strict-gate tests, neural ranker
  features, engine acceptance, privacy integration and `git diff --check`.
- Strict mode intentionally exits 2 when the HSKD v1 variant manifest is absent;
  the build uses the explicit `--allow-missing-variant-manifest` waiver after
  all structural checks. The test wrapper separately proves both outcomes.
- Final `.hosdict`: HSKD v1, 42 assets, 111186208 bytes, SHA-256
  `7D69FD779141531E782F853BA8576C17DE6576BFC36061641684EA9ECA34E8B5`.
  The sole `.hosdict` member in the production HAP is byte-identical to source.
- `build/final_asset_inventory_20260713.tsv` independently binds all 42 index
  entries to their whole-file source bytes and packaged SHA-256; every pair is
  equal. It contains no `emoji.*`, `tap.*` or `autocc.tree_model*` key.
- The last production build used `tools/build_final.ps1`/`assembleApp`; this is
  required because `assembleHap` refreshes only the unsigned module HAP, not
  `outputs/default/app/entry-default.hap`.
- Remaining build hygiene: output is unsigned; warnings remain for
  `READ_PASTEBOARD` and a SetupGuide API-26 `fill` call while compatibility is
  API 23. These warnings were not suppressed.

## Emulator crash diagnosis (historical, not the final device blocker)

The emulator crashed twice with the same host-side graphics failure, not a
guest app exception:

- `Emulator.log`: `GPU Thread is hung!`, `ERROR_CODE=802003`,
  `ERROR_TYPE=graphic hung`.
- `qemu.log`: `glFenceSync` timeout immediately before the second crash.
- Crash reports:
  - `crash_report-2026-07-13T113200.zip`
  - `crash_report-2026-07-13T115552.zip`

`-noWindow` was also tested. This emulator build still created the OpenGL
renderer and did not expose an HDC target, so it is not a valid device-test
fallback. After a clean supported launch and removal of the ARM bridge from the
test HAP, the final editor E2E completed; therefore the earlier crash cannot be
summarized as simply “the PC cannot run all model modules.”

## Structural-only tree artifacts

These artifacts are deliberately kept under ignored `build/` and are **not
production eligible**.  Their sidecars pin source/feature hashes and record the
unproven `gt` comparator and LSB leaf-order assumptions.

| artifact | source contract | HOBL SHA-256 | host zero-vector witness |
|---|---|---|---|
| `tap.default.ranker.hobl` | 1000 trees, depth 8, 37 features, 3316 borders | `c8f45f3f47bc233685306bb71a2b1fb099438b6f0be743a41e8cb6d5abe052c6` | finite `3.526`, strict diagnostics, 436 us |
| `autocorrect.default.hobl` | 2000 trees, depth 6, 79 features, 6225 borders | `34ca6b9639e6f5fd41449ab2f86ee1b26262a24bb6f6f5cf01c2a4db5264f74f` | finite `2.93328`, strict diagnostics, 273 us |
| `autocorrect.k13321.hobl` | 1999 trees, depth 6, 79 features, 6218 borders | `783b16abc81d96fe18910c937361d5b4605934e07488402e410b2462c190d9a2` | finite `-3.10007`, strict diagnostics, 435 us |
| `autocorrect.k13421.hobl` | 1999 trees, depth 6, 79 features, 6264 borders | `b3ac59e709b3cff01e04a76e7c272e80107cef576325797238543f81612d9cac` | finite `-2.65759`, strict diagnostics, 363 us |

The finite scores prove parser/backend engagement only.  They do not prove the
original Yandex decision semantics, feature extraction, calibration, threshold
or automatic-commit behavior.

## Recovered emoji byte contracts

`tools/validate_emoji_assets.py` now validates both assets without aliasing one
to the other:

- `search_emoji_model`: opaque mapping payload 256949 bytes, 6450 groups,
  44165 references, 1898 unique emoji, canonical decoded catalog SHA-256
  `aa81eef43483fa310d73ee22b58442c57a363f71eecb582fa7bc6abf2922aea9`.
  It remains non-runnable because query-to-leaf/ranking is stripped-native.
- `emoji_model`: `ynnlm_v1`, order 10, catalog at 352, decoder at 44480,
  1752 unique strings and inverse rank/lex maps.  Rank 0..9 is
  `<unk>, 👋, 👍, 👌, ❤️, 👏, 🙏, 😘, 🤣, 🎉`.  It remains non-runnable from
  text because the matching encoder state/hidden chain is absent.

## Tap bundle contract

`tools/validate_tap_bundle.py` strictly parses the outer varint frame and exact
protobuf descriptor.  The Default bundle contains six UTF-8 layout maps and
2684 key records.  Golden `android_ru_east_slavic_separate_comma` is
1080x631 with 755 records; key `а` is x=295, y=169, width=99, height=154 with
the recovered finite Gaussian parameters.  This validates the bundle, not the
missing classifier feature semantics/calibration.

## Exact remaining completion blockers

1. **Emoji search/UI:** `search_emoji_model` is not a 525-class neural model.
   The validated raw asset SHA-256 is
   `8E6BB06514229A24B53C18E393BFE27F21A7E8059A295322C709F0EEC4205D30`;
   it is a mapping/trie plus catalog. The exact query preprocessing,
   query-to-leaf traversal and ranking logic live in stripped native code.
   `search_emoji_model.ms` is an unrelated/unsupported role and is not
   packaged. Static substring search was rejected because it would falsely
   satisfy the UI while bypassing the intended path.
2. **Context emoji:** wrapper SHA-256
   `73065833C4CFAA6E7712C6FE6A288252B72F7028E9EE20D099C7228404D8AF70`
   proves the 1752-output decoder at offset 44480, but raw text cannot supply
   its exact 2500-state input without the matching K13321 encoder
   `state[4000] + hidden[500]` chain. Zero/random state was explicitly rejected.
3. **Original TapModel:** source ranker/classifier/bundle are structurally
   decoded, but the classifier's exact `2N+2` feature semantics, call order,
   calibration and decision threshold remain absent. No Tap tree/bundle is
   packaged or production-hooked; only the proven feature JSON remains.
4. **Neural autocorrect:** all three forests and the 79-feature catalog are
   structural-only. Comparator, leaf-bit convention, exact runtime feature
   vector, output transform/threshold, blocker ordering and one-shot
   commit/undo edge still lack an original-runtime oracle. No autocorrect tree
   is packaged; the existing heuristic/TfmTrie path remains the safe fallback.
5. **Privacy/device negatives:** password patterns and unknown state are
   fail-closed on host, but there is no final password-field device witness and
   no proven HarmonyOS incognito signal.
6. **Provenance/signing:** HSKD v1 has no versioned per-asset
   variant/hash-binding manifest. The final HAP is unsigned and therefore not a
   release artifact.
7. **Regression/performance:** the exact final HAP has NNLM A/B and one real
   RNNT editor E2E, not a full physical-device corpus covering ordinary taps,
   every language/layout, static emoji panel, secure fields, missing components
   and long-run memory. The measured real-emulator swipe is 351 ms and
   budget-limited; further latency work needs a physical-device corpus rather
   than relaxed correctness gates.
