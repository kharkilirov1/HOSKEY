# 1nc7 Decoder - Найденное и проблемы

## Структура файла (подтверждено)

```
[0:4]       magic = "1nc7" (0x37636E31)
[4:8]       version = 1
[8:12]      payload_hint = 999284
[16:20]     item_count = 15614

BV Headers (4 штуки):
  BV0 @ 130744: louds (main)    popcount=369743
  BV1 @ 292744: has_child (main) popcount=369743
  BV2 @ 448936: louds (sub)     popcount=801071
  BV3 @ 605128: has_child (sub) popcount=402824

Labels @ 636424: byte array, size=362860
```

## Двухуровневая структура

- **Main trie** (BV0/BV1): верхний уровень
- **Sub trie** (BV2/BV3): нижний уровень (через +0x188 pointer)

## Проблемы

### 1. LOUDS навигация
Формулы из Ghidra:
```c
rank = rank1(louds, node_id);
child_start = select0(louds, rank - 1) + 1;
```

Но обход даёт только ~3-7 узлов вместо 109K.

### 2. Label mapping
Labels как byte array, но mapping непонятен:
- 0 = 'а'
- 1 = 'б'
- 106 = ???

### 3. Python decoder тоже не работает
Возвращает 0 слов на этом файле.

## Гипотезы

1. **Формат LOUDS другой** - может не стандартный LOUDS а модификация
2. **Нужен main→sub transition** - когда has_child=1 в main, переходить в sub
3. **Labels packed/encoded** - может быть не raw bytes

## Следующие шаги

1. Детальный реверс `FUN_00661926` - понять точную формулу
2. Проверить `words_final.csv` - откуда взялись слова ранее
3. Возможно формат изменился в новых версиях

## Fallback

Использовать `extracted_words.txt` (108K слов) пока decoder не готов.
