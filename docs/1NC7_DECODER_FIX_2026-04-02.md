# Исправление 1nc7 Decoder — Priority 1

Дата: 2026-04-02
Статус: Implementation complete, testing required

## Что было сделано

### 1. Добавлен Terminal Bitvector support

**Проблема:** Предыдущая реализация использовала `has_child == 0` для определения листьев, но по данным Ghidra (FUN_00661926) terminal nodes определяются отдельным bitvector по offset +0x120.

**Решение:**
- Добавлен `RankedBitvector terminal_bv_` в класс `SuccinctTrie`
- Добавлен поиск terminal BV между BV3 и metadata (popcount должен быть ~109K)
- Обновлён DFS для использования `terminal_bv_.GetBit(node_id)` если он доступен

### 2. Улучшена двухуровневая навигация

**Проблема:** Навигация через sub-trie (BV2/BV3) была неполной.

**Решение:**
- Sub-trie теперь правильно использует BV2=louds, BV3=has_child
- Terminal BV пробрасывается в sub_level_
- Улучшены safety checks в DFS

### 3. Улучшена диагностика

Добавлено логирование:
- Поиск terminal BV с выводом popcount
- Диагностика root node (hc, terminal)
- Проверка количества найденных слов

## Ключевые изменения в коде

### succinct_trie.h
```cpp
// Добавлено:
RankedBitvector terminal_bv_;
bool has_terminal_bv_ = false;
RankedBitvector terminal;  // в TrieLevel
```

### succinct_trie.cpp — Parse()
```cpp
// Поиск terminal BV (popcount ~109K)
if (num_bvs >= 5) { check bvs[4] }
// Или сканирование между BV3 и metadata
for (scan p between last BV and label_start) {
  if (v0 == item_count && v1 == 3904 && v2 ~109K) {
    terminal_bv_ = ...
  }
}
```

### succinct_trie.cpp — DFS()
```cpp
// Terminal check с использованием BV
bool is_terminal = false;
if (has_terminal_bv_ && node_id < terminal_bv_.num_words * 64) {
  is_terminal = terminal_bv_.GetBit(node_id);
} else {
  is_terminal = !level.has_child.GetBit(node_id) && !path.empty();
}

// Улучшенная LOUDS навигация
uint64_t child_rank = level.has_child.Rank1(node_id);
uint64_t select_idx = (child_rank == 0) ? 0 : (child_rank - 1);
uint64_t child_start = level.louds.Select1(select_idx);

// Конвертация LOUDS pos → node_id
uint64_t child_node = level.louds.Rank1(pos);
if (child_node > 0) child_node--;
```

## Как тестировать

### 1. Сборка
```bash
cd entry/src/main/cpp
mkdir -p build && cd build
cmake .. -DCMAKE_CXX_STANDARD=17
make -j4
```

### 2. DevEco Studio
- Открыть проект в DevEco Studio
- Build → Build Hap
- Запустить на устройстве/эмуляторе

### 3. Проверка диагностики
В логах (Logcat) искать:
```
Terminal BV: found (popcount=108995)
Total words: 109000
w[0]="..."
```

### 4. Ожидаемые результаты

**Если terminal BV найден и работает:**
- Должно найтись ~109K слов
- Первые 5 слов должны быть валидными русскими словами

**Если terminal BV не найден:**
- Fallback на has_child == 0 (старое поведение)
- Возможно 0-11 слов (как раньше)

**Если навигация работает неправильно:**
- Много garbage words
- Мало слов

## Следующие шаги если не работает

### Вариант A: Terminal BV всё ещё не найден
- Проверить offset вручную через hex editor
- Попробовать сканировать всё пространство между BV3 и metadata
- Проверить что popcount действительно ~109K

### Вариант B: Навигация неправильная
- Проверить формулу LOUDS (возможно нужна другая интерпретация)
- Попробовать select0 вместо select1
- Проверить порядок bits (LSB vs MSB)

### Вариант C: Labels неправильные
- Проверить CP866 mapping
- Проверить что label_start указывает на правильное место
- Проверить packed IntVector (двухкомпонентные labels)

## Альтернатива (fallback)

Если 1nc7 decoder не заработает, использовать:
- `extracted_words.txt` (108,995 слов)
- Russian National Corpus для частот
- Без 1nc7 частот (влияет на качество ранжирования)

---

**Статус:** Код готов к тестированию. Требуется сборка и запуск на устройстве.
