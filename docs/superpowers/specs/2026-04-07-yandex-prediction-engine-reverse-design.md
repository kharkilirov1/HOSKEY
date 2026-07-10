# Reverse Engineering: Yandex Prediction Engine (1:1 Reproduction)

Date: 2026-04-07
Status: Design approved, pending implementation

## Goal

Reproduce the Yandex keyboard's `getSuggestions` pipeline exactly as it works in `libjni_ykeyboard3.so`, including n-gram LM, neural ranking, and score fusion.

## Current State

### What Works
- HSKD container parser (17 assets loaded)
- BV structure: 4 bitvectors parsed (rank/select verified)
- LOUDS navigation formula: `child = select1(louds, rank1(has_child, node_id) - 1)`
- NAPI bridge: ArkTS <-> C++ communication
- DictInstaller: copies assets to device filesDir

### What's Broken
- **SuccinctTrie labels**: parser reads hash table data at offset 636424 instead of IntVector-packed labels embedded between BV sections
- **N-gram LM**: 3.5MB tail data (`_ORDER=3`, `_TOTAL_WORDS=22B`) not parsed
- **Neural ranker**: MindSpore models loaded but I/O format unknown
- **Score fusion**: weights and thresholds unknown
- **Result**: `GetSuggestions()` always returns empty

### Key Artifacts Available
- `libjni_ykeyboard3.so` — Yandex native library (in Ghidra project)
- `ru.hoskey.hosdict` (29MB) — repacked Yandex dictionary container
- `russian_words.txt` (109,829 words) — extracted via DFS decoder for verification
- `captured_suggestions.json` — captured real Yandex suggestions for comparison
- Ghidra project at `C:\Users\Kharki\ghidra_yandex\YKB.rep`
- PyGhidra scripts at `C:\Users\Kharki\ghidra_scripts\`
- Previous reverse docs at `HOSKEY/docs/` and `HOSKEY/reverse/`

### Key Addresses in libjni_ykeyboard3.so
| Address | Function | Status |
|---------|----------|--------|
| JNI entry | `getSuggestions` | Known, partially decompiled |
| 0x0065f2d8 | 1nc7 parser (main) | Partially decoded |
| 0x00660496 | Section reader (recursive) | **CRITICAL: vtable dispatch for labels not decoded** |
| 0x00661806 | Bitvector reader | Decoded |
| 0x006615a4 | Data section reader | Decoded |
| 0x0066084e | IntVector reader | Decoded (format known) |
| 0x00661926 | Trie iterator | Decoded (navigation formula) |
| 0x00661b66 | State builder | Decoded |
| 0x00661538 | rank1 | Decoded |
| 0x00661646 | select1 | Decoded |

## Architecture (from Reverse Engineering)

```
User types "при" after "я написал"
          |
          v
  getSuggestions(SuggestRequest)
          |
          v
  +------------------+
  | Token Normalizer  | -> "при" (lowercase, ё->е)
  +------------------+
          |
          v
  +------------------+
  | Trie Prefix Walk  | -> Find node for "при" in succinct trie
  +------------------+
          |
          v
  +------------------+
  | N-gram LM Query   | -> P("привет"|"я","написал") = 0.12
  | (_ORDER=3)        |    P("приду"|"я","написал") = 0.08
  +------------------+    P("причём"|"я","написал") = 0.03
          |
          v
  +------------------+
  | Neural Ranker     | -> Re-rank candidates using ML features
  | (MindSpore .ms)   |
  +------------------+
          |
          v
  +------------------+
  | Score Fusion      | -> Combine n-gram + neural + autocorrect scores
  +------------------+
          |
          v
  SuggestResponse { suggestions: ["привет","приду","причём"] }
```

## Implementation Plan (8 Phases)

### R1: Decompile getSuggestions pipeline (top-level)
- **Input**: JNI entry point address
- **Tool**: PyGhidra
- **Output**: `GETSUGGESTIONS_FULL_PIPELINE.md` with annotated callgraph
- **Key question**: What functions are called between receiving SuggestRequest and returning SuggestResponse?

### R2: Decode label reader (FUN_00660496 vtable dispatch)
- **Blocked by**: R1
- **Input**: FUN_00660496 decompilation
- **Output**: Python script extracting all labels from 1nc7 file
- **Key question**: Where in the byte stream are labels stored, what's the IntVector packing, and what's the label-to-character mapping?
- **Verification**: Decoded labels produce valid Russian characters

### R3: Verify trie traversal + word extraction
- **Blocked by**: R2
- **Input**: Decoded labels + BV navigation
- **Output**: Full word list extracted from 1nc7 trie
- **Verification**: Extracted words match `russian_words.txt` (109,829 words)

### R4: Decode N-gram LM (tail 3.5MB)
- **Blocked by**: R1, R3
- **Input**: Tail data at offset 999284+, pipeline callgraph
- **Output**: Python script for P(word|context) query
- **Key questions**: How are probabilities mapped to trie nodes? Storage format? Backoff strategy?
- **Metadata**: `_ORDER=3`, `_TOTAL_WORDS=22B`, `_MIN_FREQ=3`

### R5: Decode Neural Ranker I/O
- **Blocked by**: R1
- **Input**: Pipeline callgraph, `features.ranker` JSON, MindSpore models
- **Output**: `NEURAL_RANKER_IO.md` with tensor specs
- **Key questions**: Input features? Output format? MindSpore Lite API for HarmonyOS?

### R6: Decode Score Fusion
- **Blocked by**: R1
- **Input**: Pipeline callgraph
- **Output**: `SCORE_FUSION.md` with formulas
- **Key questions**: Predictor weights? Autocorrect threshold? How Ngram/Neural/Autocorrect scores combine?

### R7: Implement full prediction engine in C++
- **Blocked by**: R3, R4, R5, R6
- **Changes**: Rewrite `SuccinctTrie::Parse()`, `KeyboardEngine::GetSuggestions()`, add n-gram LM query, wire MindSpore inference
- **Verification**: Compare output with `captured_suggestions.json`

### R8: End-to-end device test
- **Blocked by**: R7
- **Build HAP, test on device**
- **Criteria**: Suggestions appear, context-aware, autocorrect works, <50ms latency

## Ghidra Session Plan for R1+R2

### Session 1: getSuggestions callgraph (R1)
1. Open `libjni_ykeyboard3.so` in Ghidra
2. Navigate to `Java_...DictionaryFacilitator_getSuggestions`
3. Trace calls into C++ `getSuggestions` implementation
4. For each called function: note address, purpose, data flow
5. Identify: trie lookup call, LM query call, neural call, fusion call
6. Document in `GETSUGGESTIONS_FULL_PIPELINE.md`

### Session 2: Label reader (R2)
1. Navigate to `FUN_00660496` (section reader)
2. Find the vtable dispatch that reads labels
3. Follow the virtual call to the actual IntVector reader
4. Document: stream offset, bitwidth, element count, packing format
5. Write Python extraction script
6. Verify: extracted labels decode to Cyrillic

### What to Record
For each function:
- Address
- Input parameters (types, semantics)
- Output (return value, side effects)
- Called functions (addresses)
- Data structures accessed (offsets into trie struct)
- String literals (error messages, log tags)

## Risk Assessment

| Risk | Mitigation |
|------|------------|
| getSuggestions heavily obfuscated | Use string refs ("Predictor/Ngram" etc.) as anchors |
| Label IntVector uses unknown packing | FUN_0066084e already decoded (bitwidth + count + packed data) |
| N-gram LM format completely custom | Tail metadata gives structure hints (_ORDER, _TOTAL_WORDS) |
| MindSpore models incompatible with HarmonyOS | Test with MindSpore Lite HarmonyOS SDK first |
| Score fusion uses dynamic weights from server | `features.ranker` JSON likely contains static defaults |

## File Locations

```
Ghidra project:     C:\Users\Kharki\ghidra_yandex\YKB.rep
PyGhidra scripts:   C:\Users\Kharki\ghidra_scripts\
Yandex SO:          C:\Users\Kharki\Desktop\yandex_extracted\native_libs\
Dictionary:         HOSKEY\entry\src\main\resources\rawfile\dict\ru.hoskey.hosdict
Verification data:  C:\Users\Kharki\Desktop\yandex_extracted\russian_words.txt
Captured suggests:  C:\Users\Kharki\Desktop\yandex_extracted\captured_suggestions.json
Previous docs:      HOSKEY\docs\  and  HOSKEY\reverse\
Engine source:      HOSKEY\entry\src\main\cpp\hosdict_engine\
```
