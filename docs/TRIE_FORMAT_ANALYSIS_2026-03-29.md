# Trie Format Analysis — 2026-03-29

## File Structure

```
Offset 0:        Header (24 bytes, 6 x u32 LE)
Offset 24:       Body (999,260 bytes)
Offset 999284:   Tail (3,499,200 bytes)
EOF:             4,498,484 bytes
```

### Header
```
magic:       0x37636E31 ("1nc7" LE)
version:     1
payload_hint: 999284     <- body/tail boundary
reserved:    0
item_count:  15614       <- matches body_rows (15613)
reserved:    0
```

### Tail Footer Metadata
Last ~130 bytes of tail. Format: `[entry_count:u32]` then per entry: `[key_len:u32][key:ascii][value:u64]`.

```
_VERSION     = 1
_MAX_FREQ    = 0xFFFFFFFF
_ORDER       = 3          <- trigram model
_MIN_FREQ    = 3
_TOTAL_WORDS = (large value, possibly corpus count)
_LANG_ID     = 1          <- Russian
```

**`_ORDER=3` is critical.** This is a 3-level trie (uni/bi/trigrams).

---

## Body Structure

The body is NOT a simple array of records or a single bitvector. It is a **densely interleaved multi-section structure** with 880+ micro-sections at 256-byte granularity:

| Zone Type | Size | Percentage | Description |
|-----------|------|------------|-------------|
| MIXED | 423,424 B | 42.4% | Ambiguous packed data |
| BITVEC | 307,200 B | 30.7% | Bitvector-like (bit density 0.25-0.75) |
| SPARSE | 139,264 B | 13.9% | High zero ratio (>30%) |
| LABEL | 129,280 B | 12.9% | cp1251 label data (>40% cp1251 bytes) |

The LABEL and BITVEC zones are interleaved at a very fine grain (256B-1KB blocks), suggesting the body stores trie levels contiguously — each level's bitvector, labels, and packed data are grouped together.

### No Section Table Found
- No yarchive-style section table at end of body or file
- No structured index in body or tail preamble
- The body is one continuous packed structure

---

## Tail Structure

### Preamble (first ~40K bytes)
Mostly bytes 0x01 (13.2%) and 0x02 (20.3%), with some control codes. This is likely:
- Variable-length encoded degree sequence, OR
- Packed child count / node metadata, OR
- Continuation of body's packed structure

### Label Arena (offset ~40K+)
Contains **label arrays** — alphabetically sorted subsets of the Russian alphabet:
- `абвгдежзийклмнопрстуфхцчшщыэюя` (full alphabet — root node children)
- `еик` (3 children: е, и, к)
- etc.

Total: 516,888 cp1251 runs, mean length 4.1 chars.

These are NOT words. Each run represents the set of valid child transitions from a trie node. The ordering is alphabetical, consistent with a sorted child list in a succinct trie.

### After cp1251 runs, most common following bytes are operators:
- 0x89 (9,561 times)
- 0x20 (7,542)
- 0xAE (7,237)
- 0xB3 (6,273)
- 0x0A (6,066)

These "operator" bytes likely encode:
- Child group boundaries
- Level transitions
- Weight/frequency delimiters
- Backtrack markers

---

## Revised Understanding

### What the file IS
A **3-level succinct trie** (`_ORDER=3`) for a Russian trigram language model with:
- 15,614 nodes (item_count)
- Interleaved bitvectors (has_child, terminal, louds) in the body
- Label arrays stored separately (in tail label arena)
- Degree/structure metadata in tail preamble
- Key-value metadata in tail footer

### What the file is NOT
- NOT a simple record-based index + tail payload
- NOT a yarchive with a section table
- NOT plain text words separated by operators

### Why current ParseTrieBlob() fails
1. Heuristic block scanning cannot find section boundaries in a densely interleaved format
2. Splitting bitvector region in half for has_child/terminal is wrong
3. Label bytes in the body are misidentified as bitvector data
4. The tail preamble structure is completely ignored
5. DFS uses wrong child navigation because bitvectors are wrong

---

## Reference Trie Comparison (from extracted_words.txt, 109K words)

```
Reference Trie:
  total_nodes:    211,917
  terminal_nodes: 108,995
  internal_nodes: 129,840
  leaf_nodes:      82,077
  total_edges:    211,916
  root_degree:         30
  max_depth:           18
  unique_labels:       32

Estimated LOUDS sizes:
  bitvectors:     106 KB (has_child + louds + terminal)
  rank structures: 26 KB
  labels:         207 KB
  TOTAL:          344 KB

Actual file: 4,498 KB = 13x larger
```

The 13x size ratio means the real trie has MUCH more data than our 109K word list:
- More words/word-forms
- N-gram frequency weights per edge/node
- Additional packed structures (language model data)
- Possibly multiple sub-tries

### LOUDS Pattern Match
Body first bytes `fb ff ff ff ff ff ff ff` are very close to expected LOUDS for root degree ~30 (`ff ff ff bf...`). The difference indicates the real trie's root has a slightly different degree (maybe 5 children for super-root, or 33 with extra chars like hyphen/apostrophe).

### Body = Bitvector Storage (confirmed)
The body IS bitvector data, NOT records or labels:
- First ~5000 bytes: nearly all 1s (has_child for upper trie levels where most nodes have children)
- Gradually decreasing density deeper in body (more leaf nodes)
- No fixed-position fields within 64-byte rows
- The cp1251 "LABEL" classification was incorrect — high bitvector bytes (0xE0-0xFF) coincidentally fall in cp1251 range

### Tail = Label Arrays + Metadata (confirmed)
- Preamble (~40KB): structural data (possibly LOUDS degree sequence or node metadata)
- Label arena (~2.1MB cp1251 data): child label arrays, one per parent node
- Footer: 6-entry key-value metadata section

---

## Path to Real Decoder

### Phase 1: Metadata + Level Discovery
1. Parse tail footer metadata (done — `_ORDER=3`, `_LANG_ID=1`)
2. Find level boundaries in body:
   - Look for 3 repeating patterns of (bitvector + label + packed data)
   - Use item_count and _ORDER to estimate level sizes
3. Identify which body section is which bitvector

### Phase 2: Bitvector Identification
4. For each candidate bitvector section:
   - Compute one_count (popcount)
   - Check if it has a rank structure nearby
   - Test has_child vs terminal vs louds semantics
5. For each label section:
   - Verify cp1251 character ranges
   - Check if label count matches bitvector one_count

### Phase 3: Navigation
6. Implement proper LOUDS child navigation using identified bitvectors
7. Map labels from the label section (body + tail arena)
8. DFS to extract words
9. Validate against item_count and _TOTAL_WORDS

### Estimated Effort
This is a multi-session reverse-engineering task. Each phase requires hypothesis testing and validation.

### Session 2 Findings: Bitvector Sections Identified

**4 rank structures found** (L1 monotonic u64 tables at 16-byte stride):

| # | Rank offset | BV bits offset | BV size | Popcount | Density |
|---|-----------|---------------|---------|----------|---------|
| 1 | body+130744 | body+161992 | 125KB | 369,743 | 0.370 |
| 2 | body+292744 | body+167752 | 125KB | 369,743 | 0.370 |
| 3 | body+448936 | body+323944 | 125KB | 801,071 | 0.801 |
| 4 | body+605128 | body+480136 | 125KB | 402,824 | 0.403 |

All bitvectors have exactly 999,936 bits (1953 superblocks x 512 bits).

**LOUDS hypothesis tested and FAILED:**
- Tried LSB-first, MSB-first, and inverted encoding at multiple offsets
- Best match with tail label degree sequence: ~13% (essentially random)
- Root degree pattern (28-34 consecutive 1-bits) found at ~32 body positions
- But post-root degree sequences do NOT match tail label group sizes

**Conclusion:** Format is NOT standard LOUDS. The trie uses a custom Yandex encoding, possibly:
- DFUDS (Depth-First Unary Degree Sequence)
- Level-wise encoding with separate per-level structures
- Compressed trie with multi-character labels
- Proprietary encoding matching decompiled FUN_00661538/FUN_00661462

### Alternative Approach: Binary Instrumentation
Instead of guessing the format, use the **original Yandex library** (`libjni_ykeyboard3.so` from reverse/) as a black box:
1. Write a JNI harness that loads the real library
2. Call its getSuggestions/loadTrie functions
3. Dump the internal trie state after loading
4. Use this as ground truth for our decoder

This approach bypasses the format reverse-engineering entirely but requires Android JNI environment setup.

---

## Current Workaround Status
The extracted UTF-8 word list (`extracted_words.txt`, ~109K words from `dict_ru_full`) continues to serve as the active lexicon via `lexicon.default_payload`. This provides full keyboard usability while the decoder is in progress.
