# 1nc7 Decoder - Полная спецификация из Ghidra

## Ключевые находки из FULL_TRIE_DECOMPILED.c

### 1. FUN_00661926 (Trie Traversal) - Главная функция

```c
// На входе:
// param_1 = state array (positions for each level)
// param_2 = buffer for result
// param_3 = context
// param_4 = trie struct pointer

ulong FUN_00661926(undefined4 *param_1, undefined1 *param_2, undefined8 *param_3, long *param_4)
{
    lVar3 = *param_4;           // trie struct base
    FUN_00660c82(lVar3);
    
    uVar7 = *(ulong *)(param_1 + 8);    // current level (1, 2, or 3)
    
    // Navigate to appropriate sub-trie level
    if (1 < uVar7) {
        lVar6 = uVar7 - 1;
        do {
            lVar3 = *(long *)(lVar3 + 0x188);  // FOLLOW sub-trie pointer!
            lVar6 = lVar6 + -1;
        } while (lVar6 != 0);
    }
    
    // Get position from state
    uVar1 = *(ulong *)(param_1 + uVar7 * 2);  // state[level * 2] = position
    
    // Check if terminal using terminal bitvector at +0x120
    lVar6 = *(long *)(lVar3 + 0x120);  // terminal_bits pointer
    if ((*(ulong *)(lVar6 + (uVar1 >> 6) * 8) >> (uVar1 & 0x3f) & 1) != 0) {
        // TERMINAL NODE - extract word
        
        // Get rank in terminal bitvector
        uVar5 = FUN_00661538(lVar3 + 0x128, *(long *)(lVar3 + 0x120), uVar1);
        
        // Get label using packed IntVector + byte array
        uVar5 = FUN_00661462(lVar6, uVar1, uVar5);
        
        // Build result
        FUN_00661baa(&local_58, param_1, uVar4, uVar5);
        return 1;  // Found word
    }
    
    // NOT terminal - get label for current node
    // labels array at +0x178
    *param_3 = *(byte *)(*(long *)(lVar3 + 0x178) + uVar2) | 0x100;
}
```

**Ключевые offset'ы в runtime struct:**
| Offset | Поле | Назначение |
|--------|------|-----------|
| `+0x120` | `terminal_bits` | Bitvector: 1 = terminal node |
| `+0x128` | `terminal_rank` | Rank structure for terminal BV |
| `+0x160` | `labels_packed` | Packed IntVector (high bits) |
| `+0x168` | `label_bitwidth` | Bits per label element |
| `+0x170` | `label_mask` | Mask for packed labels |
| `+0x178` | `labels_bytes` | Direct byte array (low bits) |
| `+0x188` | `sub_trie` | Pointer to sub-trie level |

### 2. FUN_00661462 (Label Lookup)

```c
// Returns: (packed_bits << 8) | byte_value

ulong FUN_00661462(long trie, long node_id, long rank)
{
    // Get bitwidth from +0x168
    bitwidth = *(byte *)(trie + 0x168);
    
    if (bitwidth == 0) {
        packed = 0;
    } else {
        // Read from packed array at +0x160
        word_idx = (rank * bitwidth) >> 6;      // / 64
        bit_offset = (rank * bitwidth) & 0x3f;  // % 64
        
        // Read up to 2 words and combine
        word1 = *(ulong *)(*(long *)(trie + 0x160) + word_idx * 8) >> bit_offset;
        if ((64 - bit_offset) < bitwidth) {
            word2 = *(long *)(*(long *)(trie + 0x160) + (word_idx + 1) * 8);
            word1 |= word2 << (64 - bit_offset);
        }
        packed = (word1 & *(ulong *)(trie + 0x170)) << 8;
    }
    
    // Combine with direct byte from +0x178
    return packed | *(byte *)(*(long *)(trie + 0x178) + node_id);
}
```

### 3. Алгоритм декодирования

```python
def decode_1nc7(data):
    # 1. Parse header
    magic = data[0:4]  # "1nc7"
    version = struct.unpack('<I', data[4:8])[0]  # 1
    payload_hint = struct.unpack('<I', data[8:12])[0]  # 999284
    item_count = struct.unpack('<I', data[16:20])[0]  # 15614
    
    # 2. Find 4 bitvector sections
    bv_offsets = scan_for_bv_headers(data, item_count=15614, field1=3904)
    # Returns: [130744, 292744, 448936, 605128]
    
    # 3. Parse bitvectors
    # BV0: louds (main)
    # BV1: has_child (main)
    # BV2: louds (sub)
    # BV3: has_child (sub)
    
    # 4. Terminal bitvector - where is it?
    # Runtime struct has pointer at +0x120
    # Must be loaded from somewhere in file...
    
    # 5. Labels array at +0x178 equivalent
    # Found after BV4 metadata: offset 636424
    
    # 6. DFS traversal
    words = []
    def dfs(node_id, path, level):
        # Check terminal
        if is_terminal(node_id):
            words.append(''.join(path))
        
        # Get children using LOUDS
        child_rank = rank1(level.has_child, node_id)
        child_pos = select1(level.louds, child_rank - 1)
        
        for child_node in enumerate_children(level.louds, child_pos):
            label = get_label(child_node)
            path.append(label_to_char(label))
            dfs(child_node, path, level)
            path.pop()
    
    dfs(0, [], sub_level)  # Start at root of sub-trie
    return words
```

## Проблема: Где в файле terminal bitvector?

Из runtime struct (+0x120) - это указатель, который устанавливается при загрузке.
В файле 1nc7 он может быть:

1. **Между BV3 и labels** (offset 605128 - 636424)
2. **В tail section** (offset 999284+)
3. **Внутри BV3 как флаги** (но BV3 popcount = 402824, а words = 109000)

## Гипотеза: Terminal bitvector внутри других BV

BV3 popcount = 402824, а words = ~109000.
Возможно terminal markers размазаны по структуре?

Или терминальные узлы определяются по другому признаку:
- has_child == 0 (лист)
- Специальное значение label
- Отсутствие детей в LOUDS

## Решение

Использовать **fallback**: считать терминальными узлы где:
1. has_child == 0 (нет детей)
2. ИЛИ reached max depth
3. ИЛИ специальная проверка по LOUDS

Но с правильной навигацией по sub-trie (BV2/BV3) и корректным label lookup.
