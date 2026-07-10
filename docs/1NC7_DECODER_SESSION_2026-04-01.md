# 1nc7 Decoder Research Session — 2026-04-01

## Цель
Декодировать Yandex 1nc7 succinct trie (4.5MB, ~15614 записей, ~109K слов) для извлечения слов с частотами.

## Что подтверждено на устройстве

### Файловая структура
```
File: 4,498,484 bytes
Header: 24 bytes (magic=0x37636E31 "1nc7", version=1, payload_hint=999284, item_count=15614)

4 rank sections (сигнатура: u64=15614, u64=3904):
  Rank[0] at 130744: pop=369743
  Rank[1] at 292744: pop=369743
  Rank[2] at 448936: pop=801071
  Rank[3] at 605128: pop=402824

4 bits DataSections (сигнатура: f0≈999283-999284, count=15614):
  Bits[0] at 5776  (124992 bytes, заканчивается у Rank[0]+24=130768)
  Bits[1] at 167776 (заканчивается у Rank[1]+24=292768)
  Bits[2] at 323968 (заканчивается у Rank[2]+24=448960)
  Bits[3] at 480160 (заканчивается у Rank[3]+24=605152)

Metadata at 636384: [402824, 8, 255, 3222592, 50353] (5 x u64 = 40 bytes)
Label byte array at 636424: 362,860 bytes (до payload_hint=999284)
Tail data at 999284+
```

### Rank header format
```
[num_words: u64 = 15614]  — количество 64-bit слов в bitvector
[field1: u64 = 3904]      — config: 2 * floor(num_words/8) = 2*1952
[popcount: u64]           — total 1-bits
```
Rank entries начинаются СРАЗУ после header (offset +24), 16 bytes/entry (L1 u64 + L2 packed u64).

### Bits section formula
```
bits_start = rank_offset + 24 - 124992
```
Проверено: bits_start[0] = 130744+24-124992 = 5776 (совпадает с DECODER_SESSION_REPORT).

### Bitvector properties (подтверждено qpop)
```
BV0: bits@5776  pop=369743 qpop(first8words)=234 — РЕАЛЬНЫЕ данные
BV1: bits@167776 pop=369743 qpop=157 — РЕАЛЬНЫЕ данные
BV2: bits@323968 pop=801071 qpop=135 — РЕАЛЬНЫЕ данные
BV3: bits@480160 pop=402824 qpop=130 — РЕАЛЬНЫЕ данные
```

### First 30 bits каждого BV
```
BV0: 010010010110110111111110001110
BV1: 101101001010000010000010010101
BV2: 011011100100010110100000000000
BV3: 101001111010110000110000000000
```

### Label byte array analysis
```
Total: 362,860 bytes
Valid Cyrillic: 294,451 (81%)
Unknown (values 48-95): 68,409 (19%)

Top labels: В(78K), Б(62K), А(45K) — преимущественно UPPERCASE
label=0 → А(U+0410), label=1 → Б(U+0411), label=2 → В(U+0412)
label=32-47 → а-п (lowercase first half)
label=96-111 → р-я (lowercase second half)
label=112 → Ё, label=113 → ё
label=48-95 → НЕ ЗАМАПЛЕНЫ (box-drawing в стандартном CP866, но Яндекс может использовать иначе)
```

### Degree analysis (select0-based LOUDS interpretation)
```
BV0: max degree in first 200 nodes = 11 (node 11)
BV1: max degree = 3 (node 129)
BV2: max degree = 4 (node 88)
BV3: max degree = 6 (node 52)

НИ ОДИН BV не показывает node с degree ≈ 30 (алфавитный root).
→ Trie НЕ является стандартным character-per-edge LOUDS.
→ Вероятно бинарный/структурный trie с отдельным label mapping.
```

### BV0 first 10 nodes (select0-based)
```
node 0: degree=1, label=А
node 1: degree=0, label=А  (leaf)
node 2: degree=1, label=Б
node 3: degree=0, label=ъ  (leaf)
node 4: degree=1, label=Б
node 5: degree=2, label=Ч
node 6: degree=2, label=Б
node 7: degree=8, label=Н
node 8: degree=0, label=Б  (leaf)
node 9: degree=0, label=Б  (leaf)
```

## Ghidra Decompilation — Ключевые findings

### Traversal algorithm (FUN_00661926)
```c
// Navigation через SUB-TRIE bitvectors (NOT main level):
lVar6 = *(long *)(lVar3 + 0x188);  // sub-trie pointer

// Core formula:
lVar3 = FUN_00661538(lVar6 + 0xa0, *(lVar6 + 0x78), uVar1);  // rank1(sub_has_child, node_id)
uVar4 = FUN_00661646(lVar6 + 0x18, *(lVar6 + 0x10), lVar3 + -1);  // select1(sub_louds, rank-1)

// Terminal check:
if ((*(ulong *)(*(long *)(lVar6 + 0x120) + (uVar1 >> 6) * 8) >> (uVar1 & 0x3f) & 1) == 0) {
  // NOT terminal — continue
  *param_2 = *(undefined1 *)(*(long *)(lVar6 + 0x178) + uVar1);  // label at +0x178
} else {
  // IS terminal — extract word via packed IntVector
  uVar5 = FUN_00661538(lVar6 + 0x128, *(long *)(lVar6 + 0x120), uVar1);  // rank1 on terminal BV
  uVar5 = FUN_00661462(lVar6, uVar1, uVar5);  // packed label lookup
}
```

### Runtime struct offsets
```
+0x010: LOUDS bits pointer (sub-trie)
+0x018: LOUDS rank structure
+0x078: has_child bits pointer (sub-trie)
+0x0A0: has_child rank structure
+0x120: terminal_bits pointer (ОТДЕЛЬНЫЙ bitvector — не BV0-BV3!)
+0x128: terminal_rank structure
+0x160: packed_labels IntVector (variable bitwidth)
+0x168: label bitwidth (1 byte)
+0x170: label mask
+0x178: direct label byte array (indexed by node_id) — 362,860 bytes
+0x180: leaf-level label array
+0x188: sub-trie pointer (recursive)
```

### Label access (FUN_00661462)
```c
// НЕ простой labels[node_id]. Двухкомпонентный:
// 1. Packed IntVector at +0x160 (variable bitwidth at +0x168)
// 2. Direct byte at +0x178
// Результат: (packed_value << 8) | labels_byte[node_id]
ulong result = (packed_bits & mask) << 8 | *(byte*)(labels + node_id);
```

### Rank1 function (FUN_00661538)
```c
// EXCLUSIVE rank: counts bits [0, pos)
mask = ~(-1L << (pos & 0x3f));  // pos=0 → mask=0 → rank=0
// L1/L2 superblock (512 bits) + partial word popcount
```

### Multi-level state machine
```
- State: 8 x u32 positions (4 levels × 2 per level) + u64 level_index
- Valid levels: 1, 2, 3
- Level navigation: state[level * 2] = new_position
- Sub-trie pointer chain: trie_struct + 0x188 → sub_trie → sub_sub_trie
```

## Что пробовали (DFS strategies)

| Strategy | BV pair | Child formula | Result |
|----------|---------|---------------|--------|
| 1 | BV2+BV3 | rank1(louds, pos+1) | 2 words ("А","АА") |
| 2 | BV2+BV3 | select0-based, rank0 | 6 words (garbled) |
| 3 | BV0+BV1 | select0-based, rank0 | 1 word ("А") |
| 4 | All 12 combos | select1(rank1-1) | ≤2 words per combo |

Ни одна стратегия не даёт >6 слов из ожидаемых ~109K.

## Корневая причина неудач

1. **НЕ стандартный LOUDS**: max degree=11 в первых 200 nodes. Алфавитный trie с 30 буквами дал бы root degree ≈ 30. Формат использует БИНАРНЫЙ/структурный trie с отдельным label mapping.

2. **Terminal bitvector ОТСУТСТВУЕТ**: Ghidra показывает BV at +0x120 отдельный от 4 найденных BV. Его позиция в файле НЕ определена. Без terminal BV невозможно корректно идентифицировать слова.

3. **Labels — двухкомпонентные**: packed IntVector (variable bitwidth) + direct byte. Мы используем только direct byte. Upper bits (packed) теряются.

4. **Двухуровневая рекурсия**: main_trie → sub_trie → возможно sub_sub_trie. Навигация на каждом уровне использует РАЗНЫЕ bitvectors. Наш код пытался использовать один уровень.

## Что нужно для завершения

### Вариант A: Полный decoder (рекомендуется)
1. **Найти terminal bitvector в файле** — скорее всего между Rank[3] и Metadata (offset 636152-636384). Сигнатура: u64 с popcount ~109K (количество слов).
2. **Декодировать packed IntVector at +0x160** — определить bitwidth (offset +0x168), найти данные в файле.
3. **Реализовать двухуровневую навигацию** — main level определяет starting positions, sub-level навигирует по символам.
4. **Разобраться с бинарным trie** — вместо character-per-edge (degree=30), формат использует binary decisions (degree=1-4). Label mapping может кодировать character через цепочку бинарных решений.

### Вариант B: Frida dynamic hooking
Запустить Yandex Keyboard на Android эмуляторе, подцепиться Frida к FUN_00661926, дампить все слова во время runtime traversal.

### Вариант C: Внешние частоты
Использовать existing 108K word list + Russian National Corpus частоты. Быстрый результат, 80% качества.

## Ключевые файлы

### В проекте
- `entry/src/main/cpp/hosdict_engine/succinct_trie.h` — header с структурами
- `entry/src/main/cpp/hosdict_engine/succinct_trie.cpp` — текущий parser (507 строк, парсит BV и labels)
- `tools/decode_1nc7_trie.py` — Python decoder с 4 стратегиями
- `docs/1NC7_FORMAT_DECODED.md` — предыдущая документация формата

### Ghidra analysis
- `C:\Users\Kharki\clawd\v600_analysis\FULL_TRIE_DECOMPILED.c` — 272KB, 54 функции
- `C:\Users\Kharki\clawd\v600_analysis\FINAL_FORMAT_SPEC.md` — struct offsets, CP866
- `C:\Users\Kharki\clawd\v600_analysis\KEY_FUNCTIONS_ANALYSIS.md` — rank, select, label decompiled

### Reverse engineering notes
- `reverse/TRIE_FINDINGS_2026-03-26.md` — initial findings
- `reverse/TRIE_FORMAT_ANALYSIS_2026-03-29.md` — detailed format analysis
- `docs/DECODER_SESSION_REPORT_2026-03-29.md` — previous decoder session

## Python decoder output (reference)
```
BV0 first nodes (select0-based LOUDS):
  node 0: degree=1 label=А
  node 1: degree=0 label=А (leaf)
  node 2: degree=1 label=Б
  node 3: degree=0 label=ъ (leaf)
  node 7: degree=8 label=Н (highest degree in first 10)

Label distribution (362,860 total):
  81% valid Cyrillic (mostly UPPERCASE)
  19% unknown (values 48-95)
  Top: В(78K), Б(62K), А(45K), У(9.5K), Н(8.9K)
```
