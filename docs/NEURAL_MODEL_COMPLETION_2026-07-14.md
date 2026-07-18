# Neural/model completion ledger — 2026-07-14

This ledger supersedes the 2026-07-13 snapshot for the current checkout.
Claims below are limited to witnesses produced after the last production-code
edit.

**Overall outcome: PARTIAL.** Word/subword NNLM, RNNT swipe and model-backed
emoji search have current device intended-path witnesses. Context emoji,
Original TapModel and neural autocorrect remain host/reverse slices only and
are not presented as production-complete.

## Checkout and exact artifacts

- Branch: `codex/neural-matrixnet-ranking`.
- The checkout was already dirty. Existing modified/untracked work was
  preserved; no reset, clean, commit or push was performed.
- Production `.hosdict`: 43 assets, 112,320,208 bytes, SHA-256
  `A18B87F1A652FD9BEB30B64868FCD55A51FFB2FECA6A4614CEF12C926345DB86`.
- Production HAP:
  `entry/build/default/outputs/default/app/entry-default.hap`, 130,300,175
  bytes, SHA-256
  `C0C66E23A4BE0C752BC369AF590171AD0F6F32623ECD7966CEF0496D98B65774`.
- The production HAP above was passed directly to `hdc install -r`; installation
  returned `install bundle successfully`. Device policy prevents shell readback
  of `/data/app/.../entry.hap`, so an independently computed installed-file
  SHA is unavailable rather than inferred.
- Test HAP:
  `entry/build/default/outputs/ohosTest/entry-ohosTest-unsigned.hap`,
  113,720,190 bytes, SHA-256
  `556ECF3C0C848BD283CB10246243B145FA78CCC22EAE97AA0D6543CCAB9DF847`.
  It has 26 ZIP entries and zero native `.so` entries; native inference stays
  in the separately installed production IME module.

## Branch status

| Branch | Current state | Strongest witness | Remaining gap |
|---|---|---|---|
| Word/subword NNLM | verified-device | ON uses runtime encoder/app embedding and reaches `invoke_count=7`, `scored_count=320`; OFF keeps counters fixed, reports `fallback=disabled:debug_switch` and changes top order | missing/corrupt/wrong-shape graph is host-negative-tested, not device-tested |
| RNNT swipe | verified-device | real continuous Russian gesture: 40 points, 7 frames, 2 RNNT-scored candidates, `fallback=none`, commits `привет `; Hypium 1/1 pass | per-component removal/malformed/NaN device matrix and broader gesture corpus |
| Emoji search + panel | verified-device | local query `кот` invokes native compact-trie model exactly once per character, returns Android-oracle top-10 and commits `😻` to the outer editor | full morphology/glyph-filter parity is not proven for the complete corpus |
| Context emoji | verified-host wrapper slice | strict `ynnlm_v1` parser, 1,752-entry catalog, decoder offset 44,480, exact 4,000+500 to 2,500 state composer and postprocessor | no exact raw-text/tag frontend, encoder-state route or verified Harmony decoder execution |
| Original TapModel | partial-host | Default bundle schema/layout goldens, source-slice hashes and common oblivious-tree parser/evaluator tests | original feature primitives, coordinate/layout fallbacks, ranker/classifier call order and calibration are incomplete; no production hook |
| Neural autocorrect | partial-host | Default/K13321/K13421 forest structure and 79-feature order are recoverable by the common tree tooling | feature primitives, output transform/threshold, blocker ordering and commit/undo semantics lack an original-runtime oracle; no production hook |

## Final device evidence

### Emoji search

The final production HAP was installed before this run. The launcher search is
a real external editor and the HOSKEY emoji query is an internal `TextInput`.

- Before commit: outer `SearchField` text is empty; internal `TextInput` is
  focused and contains `кот`.
- Native route records after typing three characters: exactly 3, with counters
  `invoke_count=1,2,3`; all report
  `asset=present,contract=valid,variant=default,load=loaded,fallback=none` and
  `route=utf16be_comptrie_prefix`.
- Per-call latency: 142 us, 866 us and 166 us. The previous duplicated
  programmatic `TextInput.onChange` invocation is no longer present.
- Top-10 for `кот`:
  `😻 😹 😽 🐈 😿 😼 🙀 😸 😾 🐱`.
- After selecting the first result: outer editor text is `😻`; internal query
  remains `кот`.
- Condensed witness:
  `build/device_e2e/final-emoji-e2e-witness-20260714.txt`.

### NNLM A/B

- ON top six: `просто, привет, про, прям, правда, при`.
- ON diagnostics: `ynnlm_encoder=runtime`,
  `ynnlm_app_embedding=runtime`, context length 9,
  `invoke_count=7`, `scored_count=320`, `fallback=none`, 175 ms.
- OFF top six: `приду, прошу, прямо, права, проще, прост`.
- OFF leaves counters at 7/320 and reports
  `fallback=disabled:debug_switch`, latency 0.

### RNNT A/B and real editor gesture

- Synthetic ON: all 11 models loaded, sinusoidal position path, shared
  `char_dec_750` predictor, 7 frames, one scored candidate,
  `invoke_count=1`, `scored_count=1`, `fallback=none`.
- Synthetic OFF: MatrixNet backoff, zero RNNT frames/work, counters remain 1/1,
  `fallback=disabled:debug_switch`.
- Real editor gesture: `swipe=rnnt_matrixnet_v1`, 40 points, 12 candidates,
  1,060-word pool, 7 frames, 2 RNNT-scored candidates, 260 ms RNNT / 289 ms
  total, `fallback=none`.
- Editor committed length 7 (`привет` plus delimiter).
- `aa test` result: `Tests run: 1, Failure: 0, Error: 0, Pass: 1`, 51.713 s.
- Condensed witness:
  `build/device_e2e/final-neural-device-witness-20260714.txt`.

## Asset provenance and packaging discipline

### Production

- `emoji.search_model.raw` is included as
  `emoji_search_comptrie_v1`; source size 1,133,908, SHA-256
  `8E6BB06514229A24B53C18E393BFE27F21A7E8059A295322C709F0EEC4205D30`.
- Context emoji, TapModel forests/bundle and autocorrect forests are excluded
  from the production package. Their small feature/config files remain; their
  presence is not treated as activation of those forests.

### Opt-in audit pack

- `--full-models` was built to ignored
  `build/audit/ru.full-models.hosdict`: 83 assets, 293,695,744 bytes,
  SHA-256
  `751ECFC1DA165ACFE4D23FE21591E0344C5DD9C4D847119C46E8CAB2A2B3C49C`;
  package gate passed.
- Default TapModel chain is hash-pinned:
  - bundle offset `0x369c1c0`, SHA-256 `70C2FC2E...15225010`;
  - ranker offset `0x36bd780`, SHA-256 `70E623AD...D0BDFDB`;
  - classifier offset `0x38c38d0`, SHA-256 `69B9917D...198A6D5`.
- Experimental TapModel variants are excluded from the audit pack because the
  named K13321 classifier and K13421 feature/ranker/classifier files contain
  known wrong slices, and no coherent experimental runtime route requires any
  of those variants.
- TFLite aliases for emoji search/context, TapModel and autocorrect are also
  excluded: a `TFL3` identifier proves file format, not graph role. Remaining
  aliases are now rejected if the identifier is absent.

## Verification commands with current results

- `tools/build_final.ps1`: `BUILD SUCCESSFUL`; package and strict structural
  gates passed with the explicit HSKD-v1 missing-variant-manifest waiver.
- `hvigorw test ...`: 18 tests, 18 pass.
- `tools/run_build_hosdict_pins_test.ps1`: 6 tests, 6 pass, including the RED
  regressions for wrong Tap hashes, unsafe aliases and non-TFL3 files.
- `hvigorw assembleHap ... entry@ohosTest`: `BUILD SUCCESSFUL`.
- `aa test ... SwipeGestureE2ETest`: 1/1 pass on device.

## Exact remaining blockers

1. **Context emoji:** the parser/composer/postprocessor is not a text-to-emoji
   runner. The exact tokenizer/tag inputs (`app`, sex/age/defaults), matching
   encoder state and verified TFLite-to-Harmony execution are still absent.
2. **Original TapModel:** source provenance and tree/bundle structure do not
   determine all original feature values or classifier calibration. Activating
   it now could silently rerank taps with invented semantics.
3. **Neural autocorrect:** the 79 slots are named/ordered, but many primitive
   LM/transfeme/neural/geometry values and the original final decision edge are
   not independently reproduced. Heuristic/TfmTrie remains the safe fallback.
4. **Device negative matrix:** NNLM missing/corrupt graph and RNNT individual
   component removal, malformed path, NaN/empty coordinates and unsupported
   layout still need disposable diagnostic-package runs.
5. **Release/provenance:** HSKD v1 has no versioned per-asset variant/hash
   manifest. The HAP/APP are unsigned and are debug artifacts, not release
   deliverables.
6. **Performance:** duplicate emoji inference is fixed and current model-route
   latencies are recorded above, but cold 112 MB container loading still needs
   an mmap/lazy-loading design plus physical-device memory/latency regression
   before claiming broad startup tuning complete.
