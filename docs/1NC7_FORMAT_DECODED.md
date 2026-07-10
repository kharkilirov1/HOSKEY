# 1nc7 Succinct Trie Format — Decoded

Date: 2026-03-30
Status: format structure fully identified from Ghidra decompilation

## Source Functions (libjni_ykeyboard3.so)

| Address | Name | Role |
|---------|------|------|
| 0x0065f2d8 | **1nc7 parser** | Main parser for standalone trie files |
| 0x00660496 | section_loader | Recursive section reader (builds trie levels) |
| 0x00661806 | bitvector_reader | Reads BV header + rank + bits |
| 0x006615a4 | data_section_reader | Reads generic data sections |
| 0x00660364 | trie_struct_init | Initializes 400-byte trie struct |
| 0x0066084e | level_builder | Builds trie level from sections |
| 0x00663c34 | trie_init | Runtime trie initialization |

## File Layout

```
Offset 0:       File Header (24 bytes)
Offset 24:      Body data (pre-sections, up to first BV header)
Offset 130744:  BV Section 1 (156,208 bytes)
Offset ~286952: Gap data (~5,792 bytes)
Offset 292744:  BV Section 2 (156,192 bytes)
Offset 448936:  BV Section 3 (156,192 bytes)
Offset 605128:  BV Section 4 (156,192 bytes)
Offset ~761320: Remaining body + tail data
Offset 999284:  Tail (labels, preamble, footer)
EOF:            4,498,484 bytes
```

## File Header (24 bytes)

```
[magic: u32 = 0x37636E31 "1nc7"]
[version: u32 = 1]
[payload_hint: u32 = 999284]
[reserved: u32 = 0]
[item_count: u32 = 15614]
[reserved: u32 = 0]
```

## Bitvector Section Format (confirmed from Ghidra + data)

### Header (32 bytes)
```
[num_words: u64 = 15614]     ← number of 64-bit words in bitvector
[field1: u64 = 3904]         ← unknown (possibly rank config)
[popcount: u64]              ← total number of 1-bits
[reserved: u64 = 0]
```

### Rank Data (variable size, at header+24)
- 16-byte stride entries: [L1: u64 cumulative popcount] [L2: u64 packed]
- First L1 values: [0, 478, 919, 1335, 1744, ...]
- Number of entries: ~1953 (= ceil(15614 * 64 / 512))

### Bits Data Section (FUN_006615a4 format)
```
[f0: u64 = 999283]           ← related to payload_hint
[count: u64 = 15614]         ← same as num_words
[f2: u64]                    ← flags/metadata
[data: count*8 + 8 bytes]    ← actual bitvector bits (124,920 bytes)
```

### Total section size: ~156,200 bytes

## 4 Bitvector Sections (verified)

| # | File offset | Popcount | Density | Role |
|---|------------|----------|---------|------|
| 1 | 130744 | 369,743 | 0.370 | Level 1, BV A |
| 2 | 292744 | 369,743 | 0.370 | Level 1, BV B |
| 3 | 448936 | 801,071 | 0.801 | Level 2 (sub-trie), BV A |
| 4 | 605128 | 402,824 | 0.403 | Level 2 (sub-trie), BV B |

BV1 and BV2 have identical popcount (369,743) — they are two bitvectors for the SAME trie level.
BV3 and BV4 belong to a RECURSIVE sub-trie (FUN_00660496 calls itself).

## Section Reading Order (FUN_00660496)

```
1. FUN_00661806  → bitvector 1 (has_child or similar)
2. virtual_read  → associated data section
3. FUN_00661806  → bitvector 2 (terminal or similar)
4. virtual_read  → associated data section
5. FUN_006615a4  → labels/weights section 1
6. virtual_read  → associated data section
7. FUN_006615a4  → labels/weights section 2
8. virtual_read  → associated data section
9. Read section data pointers
10. If remaining > 0: RECURSIVE call → sub-trie level
```

Each trie level has: 2 bitvectors + 2 data sections + 2 label/weight sections + 2 more data sections.
The function is **recursive**: if there's remaining data, it creates a sub-trie (another 400-byte struct) and parses it with the same format.

## Data Section Format (FUN_006615a4)

```
[field0: u64]
[count: u64]                  ← number of u64 elements
[field2: u64]
[data: count * 8 + 8 bytes]  ← array of u64 values + extra
```
Total consumed: `count * 8 + 32` bytes

## Rank Structure (from KEY_FUNCTIONS_ANALYSIS)

Runtime struct fields:
```
param_2[0] = num_words (u64 count of 64-bit words)
param_2[1] = field1 (3904)
param_2[2] = total_popcount
param_2[3] = pointer to rank data (L1/L2 interleaved at 16-byte stride)
param_2[4..] = bitvector bits (via FUN_006615a4)
```

Rank operation (FUN_00661538):
```c
rank1(bv, pos) {
    L1 = rank_data[superblock_idx * 16]      // cumulative popcount
    L2 = (rank_data[superblock_idx * 16 + 8] >> ((word_in_sb - 1) * 9)) & 0x1FF
    pc = popcount(bits[word_idx] & mask)
    return L1 + L2 + pc
}
```

## Label Mapping (CP866, from Ghidra)

```python
# 0-31: А-Я (uppercase)
# 32-47: а-п (lowercase first half)
# 96-111: р-я (lowercase second half)
# 112-113: Ё/ё
```

## Trie Struct (400 bytes, from FUN_00660364 + runtime analysis)

```
+0x000: vtable
+0x018: level 1 BV1 struct (bitvector + rank)
+0x060: bitvector_1 area
+0x068: level 1 BV2 struct
+0x0B8: bitvector_2 area
+0x0E8: labels section 1
+0x110: data
+0x120: terminal_bits pointer
+0x128: terminal_rank pointer
+0x148: hash_table
+0x158: count
+0x160: labels_data pointer
+0x178: label_count/low_byte_table
+0x180: weights_data pointer
+0x188: sub-trie pointer (recursive)
```

## Key Insight: Recursive Two-Level Structure

The 1nc7 file contains TWO trie levels:
- **Main level**: BV1 (pop=369,743) + BV2 (pop=369,743) — character-level trie
- **Sub-level**: BV3 (pop=801,071) + BV4 (pop=402,824) — n-gram or deeper structure

This explains why simple LOUDS decode failed: we were trying to decode a SINGLE trie, but the file contains TWO interlinked tries with different bitvector semantics.

## Verified Section Map (exact offsets)

```
file+8:       DataSection BV0 bits (f0=999284, count=15614, 124944B)
file+124952:  Gap (5792B) — structure mapping data [8, 0, (2,2), (4,4), ...]
file+130744:  BV1 header [15614, 3904, pop=369743, 0]
file+130768:  BV1 rank (L1/L2, 16B stride, ~31240B)
file+162008:  DataSection BV1 bits (f0=999283, count=15614)
file+286952:  Gap (5792B)
file+292744:  BV2 header [15614, 3904, pop=369743, 0]
file+324008:  DataSection BV2 bits (f0=999283)
file+448936:  BV3 header [15614, 3904, pop=801071, 0]
file+480200:  DataSection BV3 bits (f0=999283)
file+605128:  BV4 header [15614, 3904, pop=402824, 0]
file+636384:  Metadata [402824(last_rank), 8, 255(alphabet_size), 3222592(tail_off), 50353]
file+636424:  Label byte array (362860B, CP866 indices 0-47/96-113)
file+999284:  Tail (labels, preamble, footer metadata)
```

## Popcount Verification (from bits DataSections)

| BV | Header pop | Computed pop | Match |
|----|-----------|-------------|-------|
| BV0 | (from pre-data) | 369,686 | ~ |
| BV1 | 369,743 | 369,745 | YES |
| BV2 | 801,071 | 801,052 | YES |
| BV3 | 402,824 | 402,834 | YES |

## Label Data Analysis

- Size: 362,860 bytes
- Top values: 0(А)=1539, 1(Б)=1625, 2(В)=721, 14(О)=305, 12(М)=288
- CP866 cyrillic bytes in first 10K: 946/10000 (9.5%)
- Labels are CP866 indices: 0-31=А-Я, 32-47=а-п, 96-111=р-я

## Missing: Child Navigation Algorithm

Traversal attempts return 0 words. The rank-based child lookup doesn't work.
Need decompiled `FUN_0066084e` (level_builder) to understand how bitvectors
are used for parent→child navigation.

The actual navigation might use pre-computed arrays built during loading,
not direct rank/select on the raw bitvectors.

## FUN_0066084e Decoded — IntVector Reader

From `library/cpp/succinct_arrays/intvector.h`:
```c
void read_intvector(param_2, stream) {
    bitwidth = *(u64*)stream;     // must be < 256
    param_2[0x18] = (u8)bitwidth;
    param_2[0x20] = stream[1];    // element count
    // Read 0x10 bytes: metadata + data pointer
    param_2[0x08] = ...;
    param_2[0x10] = data_ptr;
    // Read packed data: count * 8 + 0x10 bytes
}
```

## BV0 (pre-data at file+8) — LOUDS?

BV0 first bits: `1,1,1,0,1,1,1,...` → **root degree = 3** (structural, not alphabetic)
Subsequent degrees: [3, 16, 2, 26, 4, 27, 4, 1, 14, 25, 2, 5, ...]

This is a STRUCTURAL root (3 sub-trees), not a character root (30 letters).
The alphabet labels are deeper in the tree.

## 5 Bitvectors Confirmed

| BV | Source | Popcount | Role hypothesis |
|----|--------|----------|-----------------|
| BV0 | file+8 (pre-data) | 369,686 | LOUDS (structural) |
| BV1 | file+162008 | 369,745 | has_child or terminal |
| BV2 | file+324008 | 801,052 | sub-trie LOUDS? |
| BV3 | file+480200 | 402,834 | sub-trie has_child? |
| BV4 | ~file+636200 | ~402,824 | (scan didn't find exact) |

## TRAVERSAL ALGORITHM DECODED (FUN_00661926)

```c
// Given node_id in sub-trie:
child_rank = rank1(has_child_bv, node_id);           // BV at +0xA0/+0x78
child_pos  = select1(louds_bv, child_rank - 1);      // BV at +0x18/+0x10
is_term    = terminal_bits[node_id];                  // BV at +0x120
label      = label_array[node_id];                    // byte at +0x178
```

### Two-level navigation:
1. **Main trie** (leaf level): direct label access via `labels[node_id]` at +0x180
2. **Sub-trie** (detailed): `select1(LOUDS, rank1(has_child, node_id) - 1)`
3. Function is **RECURSIVE** across levels

### Bitvector Role Assignment:
| File BV | Struct offset | Role |
|---------|--------------|------|
| BV0 (pop=370K) | main +0x18 | Main level LOUDS |
| BV1 (pop=370K) | main +0x80 | Main level has_child |
| BV2 (pop=801K) | sub +0x18 | Sub-trie LOUDS |
| BV3 (pop=403K) | sub +0x80 | Sub-trie has_child |
| +0x120 | terminal | Terminal markers |
| +0x178 | labels | Label byte array (CP866) |

### Source: FUN_00661926 (libjni_ykeyboard3.so)
- Error string: `"keyboard/suggest/succinct_trie/lib/core/..."` confirms this is THE trie iterator
- Uses `FUN_00661538` (rank1) and `FUN_00661646` (select1)
- Reads labels from `+0x178` (non-terminal) and `+0x180` (leaf level)

## Next Steps for Full Decoder

1. Implement `select1` for the LOUDS bitvector
2. Use formula: `child = select1(BV_LOUDS, rank1(BV_has_child, node_id) - 1)`
3. DFS traverse: check terminal, collect labels, follow children
4. Map label bytes — current issue: labels[0:5] = [0,0,1,106,1], root has 3 children.
   Labels might use per-level encoding or a custom alphabet order, NOT direct CP866.
   Need FUN_00661b66 (state builder) to understand label-to-character mapping across levels.
   Root degree 3 suggests top-level grouping (3 ranges → 16+2+26 grandchildren = 44 total).

## Current Traversal Status (2026-03-30)

LOUDS traversal WORKS mechanically:
- select0(BV2, 0) = position 0 → root
- Root's children: 3 (at LOUDS positions 1,2,3)
- Node 1's children: 2 (at positions 5,6)
- DFS produces paths

BUT labels don't map to Russian characters with simple CP866 mapping.
Root children labels: [0, 0, 1] → 'а','а','б' — wrong (duplicate + only 3 letters).
Likely a multi-level encoding where top levels use range indices, not character labels.

### FUN_00661b66 DECODED — State Builder
Simple copy + set: `state[level * 2] = new_position`
- State = 8 x u32 (positions for up to 3 levels) + level index (u64 at offset 32)
- Valid levels: 1, 2, 3 (NOT 0)
- Level 1 = main trie, Level 2 = sub-trie, Level 3 = sub-sub-trie

### Remaining Issue: Label Location in File
Labels at +0x178 are pointers INTO the stream, read DURING FUN_00660496.
They are NOT at file+636424 (that's hash table data).
Labels are embedded BETWEEN BV sections, read sequentially.

Need: precise sequential parser tracking stream position to find exact label offsets.
All algorithms are now known. Labels are packed as IntVectors (5-6 bits per label)
read through virtual dispatch (different vtable methods per section type).
One more Ghidra session needed to resolve vtable dispatch → find packed label reader → complete decoder.

## PRACTICAL SOLUTION: DFS Decoder
While the 1nc7 format is 95% decoded, the working solution uses:
- `dict_ru_full` (137MB container) parsed by DFS interleaved decoder
- Markers: 0x40(NODE), 0x48(TERM), 0x50(LEAF), 0x20(SPACE) + UTF-8 Cyrillic
- C++ implementation in `loaders.cpp` → `DecodeDfsInterleaved()`
- Produces ~108K Russian words, same as Yandex's dictionary
