# Yandex Quality Parity — Design Spec

Date: 2026-03-31
Status: approved
Scope: full 5-layer suggestion quality parity with Yandex Keyboard (tap only, swipe deferred)

## Decision Log

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Quality level | Full parity (all 5 layers) | User requirement |
| Dictionary source | Decode 1nc7 trie natively | Get words + frequencies from original format |
| Swipe input | Deferred | Separate track, focus on tap quality first |
| ML models | Convert TFLite → MindSpore Lite | Native HarmonyOS support, no extra runtime |
| Architecture | Hybrid (3 parallel streams) | 1nc7 decoder doesn't block other decoders |

## Current State

### What works
- `.hosdict` container installs and opens on device
- NAPI bridge connects C++ engine to ArkTS
- `getSuggestions()` / `checkWord()` API works end-to-end
- Keyboard UI renders, handles input, displays suggestions
- Workaround: `extracted_words.txt` (108,995 words, no frequencies) as fallback lexicon

### What doesn't work
- `trie` (1nc7) decoder: outputs 11 garbage words instead of ~15,614
- `tfmtrie`: parses header but candidate generation unverified
- `normdict`: metadata parsed, trie body not decoded, `GetSuggestionFrequency()` is stub
- `lemmer`: loads hash + rules headers, `Lemmatize()` unverified
- `MLRanker`: heuristic fallback only, no real neural inference
- No `.ms` model files in project

## Architecture: 3-Stream Hybrid

```
Stream A (critical path):     1nc7 Trie Decoder
Stream B (parallel):          TfmTrie + NormDict + Lemmer
Stream C (after A+B):         ML Model Conversion + Integration
Final:                        Pipeline Assembly + Verification
```

Streams A and B are independent and can proceed in parallel.
Stream C depends on A (trie frequencies needed for ML features) and B (lemmer/normdict features).

---

## Stream A: 1nc7 Trie Decoder

### Goal
Fully decode the `trie` file (4.5MB, format `1nc7`) — extract all words with frequency weights.

### Background
Format is 95% reverse-engineered from Ghidra decompilation of `libjni_ykeyboard3.so`:
- File header: 24 bytes (magic "1nc7", version, payload_hint=999284, item_count=15614)
- 5 bitvectors with rank/select acceleration
- Two-level recursive structure (main trie + sub-trie)
- Traversal: `child = select1(LOUDS, rank1(has_child, node_id) - 1)`
- Labels encoded as CP866 indices
- Exact section offsets confirmed

### Blocking Issue
Label data is packed as IntVectors (5-6 bits per label), read through virtual dispatch during sequential parse. The exact packed label reader function has not been decompiled.

### Plan
1. **Ghidra session**: decompile `FUN_0066084e` (IntVector reader) and vtable dispatch for label sections
2. **Identify label offsets**: labels are embedded between BV sections, read during sequential parse — track stream position
3. **Create new files** `succinct_trie.h` and `succinct_trie.cpp` in `hosdict_engine/`
4. **Update CMakeLists.txt**: add `hosdict_engine/succinct_trie.cpp` to sources
5. **Implement sequential parser** in `succinct_trie.cpp`:
   - Parse 24-byte header
   - For each level: read 2 BV + rank tables + 2 data sections + 2 IntVector label/weight sections
   - Recursive call for sub-trie
6. **Implement rank1/select1** on bitvectors using existing rank table format (L1/L2 at 16-byte stride)
7. **DFS traversal**: has_child → LOUDS navigation → label decode → terminal check → weight extraction
8. **Verification**: compare output against 108,995 words from `extracted_words.txt`

### Integration into KeyboardEngine
- Add `AssetKind::kSuccinctTrie` to `asset_manifest.h`
- `SuccinctTrie` used at **load time only**: `Parse()` produces word list + weights → fed into existing `PredictorEngine::Build(words, weights)`. This avoids duplicating trie structures in memory.
- `GetDefaultWordSource()` priority: `lexicon.default` (succinct trie) > `lexicon.default_payload` > `lexicon.dfs_container`
- Remove `lexicon.default_payload` fallback once trie decoder is verified

### Key Files
- `entry/src/main/cpp/hosdict_engine/succinct_trie.cpp` — **CREATE NEW**
- `entry/src/main/cpp/hosdict_engine/succinct_trie.h` — **CREATE NEW**
- `entry/src/main/cpp/CMakeLists.txt` — add to build
- `entry/src/main/cpp/hosdict_engine/asset_manifest.{h,cpp}` — add kSuccinctTrie
- `entry/src/main/cpp/hosdict_engine/keyboard_engine.cpp` — update GetDefaultWordSource()
- `docs/1NC7_FORMAT_DECODED.md` — format reference
- `C:\Users\Kharki\clawd\v600_analysis\FULL_TRIE_DECOMPILED.c` — Ghidra decompilation (external, not in repo)

### Exit Criteria
- `SuccinctTrie::Parse()` returns word list with weights
- Word count ≥ 15,000 (item_count from header = 15,614)
- Words match known Russian vocabulary
- `lexicon.default` replaces `lexicon.default_payload` as primary source

---

## Stream B: TfmTrie + NormDict + Lemmer

### B.1: TfmTrie (Keyboard Typo Correction)

**Goal:** Generate correction candidates based on keyboard key proximity.

**Current state:** `TfmTrieDecoder` parses header (magic "TfmTrie\0"), extracts SubstitutionClass structures. `GenerateCandidates()` exists but is unverified.

**Plan:**
1. Verify parse: count substitution classes, verify companions match JCUKEN layout adjacency
2. Verify companions for Russian keys (e.g., "а" → "п","р","о","в")
3. Test `GenerateCandidates()` end-to-end
4. Integrate: tfmtrie candidates → PredictorEngine lookup → scoring with `tfmtrie_score` feature
5. Verification: "поивет" → candidate "привет" with high score

**Exit criteria:** `GenerateCandidates("поивет")` returns `[("привет", 0.85), ...]`

### B.2: NormDict (Word Normalization)

**Goal:** Normalize words for canonical lookup, provide suggestion frequencies.

**Current state:** `NormDictReader` parses DATA/META sections, extracts key=value config. `GetSuggestionFrequency()` is a stub — trie body not decoded.

**Risk:** NormDict trie body (SuggestionTrieFormatVersion=2) may be a different format than 1nc7. This is a potential blocker.

**Plan:**
1. Inspect NormDict trie body: check for known magic bytes (1nc7, LOUDS, DFUDS)
2. If 1nc7 format — reuse decoder from Stream A
3. If standard LOUDS/DFUDS — implement lightweight decoder (format is well-documented)
4. **Contingency:** If format is unknown and requires separate RE work — defer `GetSuggestionFrequency()` and use trie weights from Stream A as frequency proxy. NormDict normalization (lowercase, ё→е) still works without trie body.
5. Implement `GetSuggestionFrequency(word)` — lookup normalized form frequency
6. Add `NormDictReader normdict_` member to `KeyboardEngine` and wire into `GetSuggestions()` pipeline
7. Verification: `Normalize("Привет")` → "привет", `GetSuggestionFrequency("привет")` > 0

**Exit criteria:** NormDict normalization works. Frequency lookup works OR is deferred with contingency documented.

### B.3: Lemmer (Morphological Analysis)

**Goal:** Lemmatize words ("бежал" → "бежать") and expand candidates with morphological variants.

**Current state:** `Lemmer` loads `lemmer_mhash` (CHD perfect hash, 103,803 keys) and `lemmer_rules` (3,229 rules). Headers parsed, `Lemmatize()` unverified.

**Plan:**
1. Implement CHD lookup: word → hash → bucket → displacement → rule index
2. Apply rule to word suffix → lemma (base form)
3. Reverse expansion: lemma → known word forms (for suggestion enrichment)
4. Verification: `Lemmatize("бежал")` → "бежать", `Lemmatize("домов")` → "дом"

**Exit criteria:** Lemmatize returns correct lemmas for common Russian word forms.

### Key Files (Stream B)
- `entry/src/main/cpp/hosdict_engine/tfmtrie_decoder.{h,cpp}`
- `entry/src/main/cpp/hosdict_engine/normdict_reader.{h,cpp}`
- `entry/src/main/cpp/hosdict_engine/lemmer.{h,cpp}`
- `reverse/TFMTRIE_WORKING_SPEC_2026-03-25.md`
- `reverse/EXPERIMENTAL_RULES_AND_LEMMER_WORKING_SPEC_2026-03-25.md`

---

## Stream C: ML Model Conversion + Integration

### Goal
Replace heuristic scoring with real neural ranking using MindSpore Lite.

### Models (tap-quality scope)

| Model | Purpose | Source format | Size |
|-------|---------|--------------|------|
| `ranker` | Main candidate ranking | TFLite 374KB | Primary |
| `tap_model_ranker` | Tap-specific ranking | TFLite 8.6MB | Primary |
| `tree_autocorrect_model` | Autocorrect confidence | TFLite 256KB | Critical |
| `char_model` | Character-level LM | TFLite 450KB | Secondary |
| `neural_model` | N-gram context LM | .ms 4.9MB (ready) | Critical |

### Model Source Locations
- `.ms` files: `C:\Users\Kharki\Desktop\yandex_extracted\ms_models\`
- TFLite files: `C:\Users\Kharki\clawd\yandex_extracted\yandex_extracted\`
- Raw extracted: `C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\`

### Conversion Plan
1. Install MindSpore Lite converter tool (`converter_lite`)
2. **Operator coverage check** (before any integration work): run `converter_lite` in dry-run/validation mode on each TFLite model to confirm all operators are supported. If any model fails, document unsupported ops and evaluate alternatives (custom op registration, model simplification, or skip that model).
3. Convert each verified model: `converter_lite --fmk=TFLITE --modelFile=X.tflite --outputFile=X.ms`
4. Validate: compare input/output tensor shapes, run test inference with dummy data
5. `neural_model.ms` already exists — use directly

### Feature Vector Compatibility (CRITICAL)

Yandex models were trained on Yandex's internal feature set. We cannot feed arbitrary features and expect meaningful output. Strategy:

1. **Reverse-engineer exact feature vectors** by inspecting converted models:
   - Load each `.ms` model, inspect input tensor shape (number of features)
   - Cross-reference with `features_autocc.json`, `features_ranker.json`, `features_tap_model.json` from the extracted assets — these JSON files define the exact feature catalog Yandex uses
   - Map each Yandex feature name to our engine's equivalent computation
2. **Build feature adapters**: for each model, create a feature extraction function that produces exactly the tensor the model expects, in the exact order
3. **Fallback**: if any Yandex feature cannot be computed from our engine state (e.g., features requiring proprietary data), substitute with closest approximation and document the deviation
4. If feature catalogs (`features_*.json`) are not parseable or don't exist — fall back to input tensor shape inspection + heuristic feature mapping based on model purpose

### Integration Plan
1. Parse `features_ranker.json`, `features_tap_model.json`, `features_autocc.json` to determine exact feature sets
2. Pack `.ms` files into `.hosdict` container (new asset kinds: `kTapRankerModel`, `kAutocorrectModel`, `kContextModel`, `kCharModel`)
3. Add asset kinds to `AssetKind` enum in `asset_manifest.h`
4. `MLRanker::Load()` — load MindSpore Lite model from container asset
5. Implement per-model feature extractors matching Yandex's feature catalogs
6. Connect `tree_autocorrect_model` to `AutocorrectEngine` — replace Levenshtein heuristic
7. Connect `neural_model` + `char_model` to `ContextScorer` — context-aware ranking

### Latency Budget
Target: < 50ms total per getSuggestions() call.

| Stage | Budget | Strategy |
|-------|--------|----------|
| Trie lookup + candidates | 10ms | Binary search + prefix scan |
| TfmTrie + Lemmer expansion | 5ms | Precomputed tables |
| Feature extraction | 5ms | Computed from existing data |
| ranker.ms inference | 10ms | Batch all candidates in single call |
| tap_model_ranker.ms | 10ms | Run only on top-K from ranker |
| neural_model context | 5ms | Cache context embedding per request |
| autocorrect decision | 5ms | Single candidate inference |

If latency exceeds budget: cascade strategy — run fast ranker.ms first, expensive tap_model_ranker only on top-5 candidates.

### Key Files (Stream C)
- `entry/src/main/cpp/hosdict_engine/ml_ranker.{h,cpp}`
- `entry/src/main/cpp/hosdict_engine/keyboard_engine.cpp`
- `entry/src/main/cpp/hosdict_engine/loaders.{h,cpp}`
- `entry/src/main/cpp/hosdict_engine/asset_manifest.{h,cpp}`
- `tools/convert_models.py` — conversion script

### Exit Criteria
- All 5 models converted and loading without crash
- `MLRanker::Score()` uses real MindSpore Lite inference
- `AutocorrectEngine` uses tree_autocorrect_model
- Heuristic fallback remains as `#ifndef MINDSPORE_LITE_AVAILABLE` path

---

## Final: Pipeline Assembly + Verification

### Full Pipeline

```
Input: token + previous_tokens + locale
  │
  ├─ NormDict::Normalize(token)
  ├─ PolicyStore::IsBlacklisted(token) → early return if blocked
  │
  ├─ Candidate Generation:
  │   ├─ SuccinctTrie: exact + prefix + Levenshtein-1 fuzzy (with weights)
  │   ├─ TfmTrie: keyboard proximity candidates (with proximity scores)
  │   └─ Lemmer: morphological variants (lemma → related forms)
  │
  ├─ Feature Extraction (20+ features per candidate)
  │
  ├─ ML Ranking:
  │   ├─ ranker.ms — primary score
  │   ├─ tap_model_ranker.ms — tap-specific adjustment
  │   ├─ neural_model.ms — context score (previous words)
  │   └─ char_model.ms — character-level plausibility
  │
  ├─ Autocorrect Decision:
  │   └─ tree_autocorrect_model.ms — confidence threshold
  │
  └─ Output: sorted suggestions + autocorrect flag
```

### Verification Benchmark
1. Test set: 200 queries (correct words, typos, contextual predictions)
2. Reference: expected top-3 from Yandex Keyboard (manual collection)
3. Metrics:
   - Precision@3 ≥ 0.8
   - Autocorrect accuracy ≥ 0.9
   - MRR (Mean Reciprocal Rank) — track but no hard threshold
4. Latency: `getSuggestions()` < 50ms on device
5. Memory: RSS < 100MB with all models loaded

### Device Testing
1. Build `.hap`, sign, install
2. Latency and memory profiling
3. Real typing session — subjective quality assessment

---

## Risks

| Risk | Impact | Mitigation |
|------|--------|-----------|
| 1nc7 label mapping requires more Ghidra work than expected | Stream A delayed | Fallback: continue using extracted_words.txt with external frequency corpus |
| NormDict trie body is unknown format | Stream B.2 partially blocked | Use trie weights as frequency proxy, normalization still works |
| TFLite → MindSpore conversion fails (unsupported ops) | Stream C blocked for that model | Try alternative: ONNX intermediate format, or use heuristic for that scoring layer |
| Yandex feature catalogs (JSON) missing or unparseable | ML models produce garbage | Inspect input tensor shapes + reverse-engineer from model structure |
| Latency exceeds 50ms with all models | UX degradation | Cascade: fast model first, expensive model on top-K only |
| Memory exceeds 100MB | OOM on low-end devices | Lazy model loading, mmap where possible |

## Memory Budget

| Component | Estimated | Notes |
|-----------|----------|-------|
| Succinct trie (mmap) | ~5 MB | File-backed, minimal RSS |
| PredictorEngine in-memory trie | ~25 MB | 15K words + edges + weights |
| TfmTrie substitution tables | ~1 MB | Parsed from tfmtrie asset |
| Lemmer hash + rules | ~2.5 MB | CHD hash 1.8MB + rules 539KB |
| NormDict | ~3 MB | Metadata + trie body |
| PolicyStore (blacklist) | ~0.5 MB | Hash set of blocked words |
| ranker.ms | ~0.5 MB | Small ranking model |
| tap_model_ranker.ms | ~9 MB | Largest model, consider lazy load |
| neural_model.ms | ~5 MB | Context scoring |
| char_model.ms | ~0.5 MB | Character LM |
| tree_autocorrect_model.ms | ~0.3 MB | Autocorrect decision |
| **Total** | **~52 MB** | Within 100MB budget |

Note: `tap_model_ranker` is the largest at ~9MB. If memory is tight, load lazily on first inference call.

## Testing Strategy

Each stream must produce unit tests:
- **Stream A**: parse trie file → assert word_count >= 15000, spot-check known words with weights
- **Stream B**: TfmTrie candidate generation tests, NormDict lookup tests, Lemmer correctness tests
- **Stream C**: model loading + inference with dummy input (no crash, output shape matches)
- **Final**: integration test — full getSuggestions() pipeline with benchmark queries

## Out of Scope

- Swipe input (deferred to separate track)
- Emoji suggestions (deferred)
- Voice input (deferred)
- Experimental model variants (KEYBOARD-13321_*, KEYBOARD-13421_*)
- ranker_v2, ranker_exp (v2 optimization track)
- nnlm_model (covered by neural_model)
