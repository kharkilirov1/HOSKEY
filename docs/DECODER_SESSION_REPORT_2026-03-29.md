# Decoder Research Session Report — 2026-03-29

## Objective
Crack the Yandex `1nc7` succinct trie format to extract Russian words directly, replacing the temporary UTF-8 word list workaround.

## Summary
The session produced deep structural understanding of the file but did NOT achieve word extraction. The format is proprietary Yandex `NSuccinctTrie::TTrie` and resists standard LOUDS/DFUDS interpretations.

## Confirmed Findings

### File Layout
```
[0, 24)        : Header (6 x u32 LE)
[24, 5776)     : Body header / BV0 (5752 bytes, dense bitvector, nearly all 1s)
[5776, 130768) : BV1 bits (124992 bytes, pop=345,517, density=0.346)
[130768, 162016): R1 rank (31248 bytes, 1953 entries, L1 final=369,743)
[162016, 167776): Gap (5760 bytes)
[167776, 292768): BV2 bits (124992 bytes, pop=370,099, density=0.370)
[292768, 324016): R2 rank (31248 bytes, 1953 entries, L1 final=369,743)
[~324016, ~448968): BV3 bits (~125KB, pop=801,203, density=0.801)
[~448968, ~480216): R3 rank (31248 bytes, L1 final=801,071)
[~480168, ~605160): BV4 bits (~125KB, pop=402,976, density=0.403)
[~605160, ~636408): R4 rank (31248 bytes, L1 final=402,824)
[636408, 999284): Remaining body (362,876 bytes) — structured metadata
[999284, 4498352): Tail preamble + label arena + footer metadata
[~4498352, 4498484): Footer (6-entry KV: _VERSION=1, _ORDER=3, _LANG_ID=1, etc.)
```

### Rank Structure Format (confirmed)
- 16 bytes per entry: [L1:u64 cumulative popcount] [L2_packed:u64]
- 1953 entries per bitvector = 1952 superblocks of 512 bits
- 4 rank tables found at body offsets: 130,744 / 292,744 / 448,936 / 605,128

### Bitvector Properties
| BV | Popcount | Density | Possible Role |
|----|----------|---------|---------------|
| BV0 (header) | ~5,200 | ~0.90 | Unknown (very dense) |
| BV1 | 345,517 | 0.346 | Unknown |
| BV2 | 370,099 | 0.370 | Unknown (same pop as R1) |
| BV3 | 801,203 | 0.801 | Highest density |
| BV4 | 402,976 | 0.403 | Unknown |

### Tail Structure
- Preamble (0..39,936): mostly bytes 0x01/0x02 — structural control data
- Label arena (39,936+): sorted cp1251 alphabet subsets per trie node
  - First group (30 chars): `абвгдежзийклмнопрстуфхцчшщыэюя`
  - Total: 500K+ groups, 1.1M+ label bytes
- Footer (last ~130 bytes): KV metadata

### Reference Trie (from extracted_words.txt)
- 211,917 nodes, 211,916 edges, 108,995 terminal
- Root degree: 30
- Estimated LOUDS size: 344 KB — file is 13x larger

## What Was Tried and Failed

### LOUDS Decode (all variants)
- LSB-first at 32+ body offsets: best match 13.5% (random)
- MSB-first: no improvement
- Inverted (0=child, 1=end): no match
- FST S-LOUDS (1=first child): no match
- Root pattern search: found degree 28-34 at many positions but post-root degrees never match tail label groups

### DFS Tail Decode
- Strategy A (0x00=pop, cp1251=push): 34 garbage words
- Strategy B (null-terminated): 175K "words" but all alphabet runs (not words)
- Strategy C (level-wise): confirms label groups are sorted character sets

### Label Search
- First label group `абвгдежзийклмнопрстуфхцчшщыэюя` found ONLY in tail+39975, NOT in body
- Body "LABEL" classification was incorrect — dense bitvector bytes coincidentally fall in cp1251 range

## Why Standard Approaches Fail
The Yandex `NSuccinctTrie` format does NOT use standard LOUDS or DFUDS encoding. Evidence:
1. Degree sequences from all 4 bitvectors (under all interpretations) never match the tail label group sizes
2. The format uses 4 co-indexed bitvectors of identical size — this is unusual
3. Two bitvectors (BV1/BV2) have nearly identical popcounts (~370K) but different bit distributions
4. The preamble (40KB of small control bytes) has no equivalent in standard succinct trie formats

## Recommended Next Steps

### Option 1: Ghidra Deep Decompilation (highest value)
Decompile functions in address range 0x0066xxxx from `libjni_ykeyboard3.so`:
- `FUN_00661538` — rank operation
- `FUN_00661462` — label lookup
- `FUN_00660dca` — weight reading
These were referenced in the C++ code but are NOT in the existing callgraph decomp.

### Option 2: Binary Instrumentation
Load `libjni_ykeyboard3.so` in an Android emulator, hook the trie loading function, dump internal state after parsing. This bypasses format reverse entirely.

### Option 3: Community Research
Search for other reverse-engineering efforts of Yandex Keyboard or similar `NSuccinctTrie` formats. The mangled symbols suggest this is from Yandex's internal `arcadia` codebase.

## Tools Created
| File | Purpose |
|------|---------|
| `tools/validate_record_hypothesis.py` | Tested (disproved) record-based body hypothesis |
| `tools/validate_tail_words.py` | Analyzed tail byte structure |
| `tools/extract_tail_to_file.py` | Extracted tail fragments to UTF-8 |
| `tools/decode_tail_footer.py` | Decoded footer metadata |
| `tools/find_section_table.py` | Searched for section tables, zone analysis |
| `tools/find_trie_levels.py` | Row-level classification, BV-label pairing |
| `tools/inspect_body_rows.py` | Byte-level row inspection |
| `tools/build_reference_trie.py` | Built reference trie from extracted words |
| `tools/find_bitvector_sections.py` | **Found 4 rank structures** (key breakthrough) |
| `tools/try_louds_decode.py` | Tested LOUDS decode with label matching |
| `tools/find_louds_root.py` | Searched for root degree pattern |
| `tools/inspect_bitvectors.py` | Detailed BV byte inspection + layout |
| `tools/try_dfs_tail_decode.py` | Tested DFS and null-terminated strategies |

## Session 2: Disassembly Findings

### Binary: libjni_ykeyboard3.so (AArch64, stripped, 9.6MB)
- .text at 0x1ED4B0 (6.2MB), .rodata at 0x34E40 (718KB)
- 52 JNI exports, 259 dynamic symbols, 0 static symbols
- `"1nc7"` string found at rodata 0x40B86 (format registration, not parser)

### Core Trie Functions Identified
4 function prologues at 0x660AF8, 0x66131C, 0x661870, 0x661C6C.

**func_0x661528 (label reader)** — fully disassembled:
- Loads byte from data array at given index
- Bit 3 (0x08): back-reference (return previous byte)
- Bit 7 (0x80): extended decode via vtable call
- Bit 6 (0x40): skip table path
- Returns byte value (0-255)

**func_0x66131C (child iterator)** — partially disassembled:
- Iterates 16-byte entries (`add x27, x27, #0x10`)
- Reads second qword of each entry as label reader parameter
- Finds max label value across children, stores at object+0x88
- Calls func_0x661528 for each child entry

### Remaining Body Structure (363KB after bitvectors)
Header: u64[0]=255 (max_label_id), u64[1]=3222592 (tail offset), u64[2]=50353
- 22,680 potential 16-byte records
- Byte values mostly 0x00-0x19 (small indices), NOT cp1251 characters
- Flag distribution: 60.5% plain, 22.5% back-ref (bit 3), 6.6% skip+backref
- The data uses the byte-flag encoding discovered in the label reader function

### What tail[3222592:] Contains
276,608 bytes of sparse bitmap data (mostly zeros with scattered set bits). NOT a label lookup table.

## Conclusion
The trie format uses a compact byte-per-node encoding where each byte combines label index + navigation flags. The 4 bitvector sections provide rank/select acceleration. The label indices map to characters via the sorted label arrays in the tail arena. But the exact traversal algorithm requires full Ghidra decompilation of the trie loading and iteration code.

## Recommended Next Steps (prioritized)
1. **Full Ghidra project** on libjni_ykeyboard3.so — decompile 0x660AF8-0x661C6C range
2. **Frida dynamic hooking** — attach to Yandex Keyboard on Android, hook trie traversal, dump words
3. **Alternative dict source** — investigate dict_ru_full format (already extracted 109K words)

## Session 3: Deep Ghidra + vtable Analysis

### Previous Ghidra Work Found
Directory `C:\Users\Kharki\clawd\v600_analysis\` contains complete prior Ghidra analysis:
- `FULL_TRIE_DECOMPILED.c` (272KB, 54 functions)
- `FINAL_FORMAT_SPEC.md` — complete runtime struct and CP866 mapping
- `ONDISK_LAYOUT.md` — DFS interleaved encoding (0x40/0x48/0x20 markers)
- `KEY_FUNCTIONS_ANALYSIS.md` — rank, select, label_to_unicode, word_builder
- `yandex_trie_decoder.py` — working DFS decoder for container format
- `words_final.csv` — 108,848 words already extracted

### Two Separate File Formats Confirmed
1. **Container** (`dict_ru_full`, 137MB, magic `0xfe3ac19b`): DFS interleaved, decoded by v600_parser
2. **Standalone trie** (`trie`, 4.5MB, magic `1nc7`): succinct bitvectors, separate unknown parser

### v600_parser Analysis (FUN_00680864)
- Calls yarchive parser TWICE (nested blobs)
- Section lookup by string keys ("config", "conf.json")
- Iterates registered section handlers via global list (DAT_00a6cd10)
- Handler vtable+0x30 is called for each section
- Handlers map sections to trie struct fields

### 1nc7 Parser Registration
- Found at 0x5513d0: registers "1nc7" magic with a handler
- Handler vtable at 0x81ce00 — all zeros (needs runtime relocation)
- Registration calls: 0x22cda4 (format register) + 0x818140 (PLT handler register)

### File Format Conclusion: NOT Standard LOUDS
- Tested ALL 4 bitvectors as LOUDS (both conventions, all offsets): NO root degree 28-34 found
- Format is proprietary, not LOUDS/DFUDS/BP
- Bitvectors serve unknown purpose in non-standard encoding
- Full decode requires the actual 1nc7 parser function (not yet decompiled)

### Ghidra Headless Run
Launched `DecompileTrieParser.java` to decompile:
- 1nc7 registration functions
- Section handler xrefs (DAT_00a6cd10)
- Missing trie functions (0x6611ac, 0x66131c, etc.)
- Bitvector init functions

## Workaround Status
`extracted_words.txt` (108,995 words from `dict_ru_full`) remains active as `lexicon.default_payload`. Keyboard usability is not blocked.
