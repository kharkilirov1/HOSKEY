# 1nc7 Decoder - Итоговый статус

## Что сделано

### 1. Обновлён SuccinctTrie класс
- `succinct_trie.h` / `succinct_trie.cpp` - placeholder реализация
- Компилируется без ошибок
- Предоставляет `word_count()` метод
- Graceful fallback к `extracted_words.txt`

### 2. Найдена структура файла
```
Magic: 0x37636E31 ("1nc7")
Version: 1
Item count: 15614
Payload: 999284

4 Bitvector sections:
  BV0 @ 130744: louds (main)     pop=369743
  BV1 @ 292744: has_child (main) pop=369743
  BV2 @ 448936: louds (sub)      pop=801071
  BV3 @ 605128: has_child (sub)  pop=402824

Labels @ 636424: byte array, size=362860
```

### 3. Ghidra анализ
- `FUN_00661926` - trie traversal с LOUDS
- `FUN_00661646` - select0
- `FUN_00661538` - rank1
- `+0x188` - sub-trie pointer

## Проблемы

### LOUDS навигация
Формула `first_child(node) = select0(louds, rank1(louds, node)) + 1` даёт мало узлов.

Возможные причины:
1. **Двухуровневая структура** - нужен правильный main→sub transition
2. **Другая формула LOUDS** - может Yandex использует модификацию
3. **Labels encoding** - может быть packed, не raw bytes

## Fallback работает
Клавиатура использует `extracted_words.txt` (108K слов) пока decoder в разработке.

## Следующие шаги
1. Детальный реверс `FUN_00661926` - понять exact формулу
2. Проверить двухуровневый переход (main→sub)
3. Исследовать labels encoding
