# 1nc7 Decoder - Текущее состояние

## Реализовано (v2)

### Алгоритм на основе Ghidra
- **Использует sub-trie (BV2/BV3)** вместо main-trie, согласно FUN_00661926
- **LOUDS навигация**: `Rank1` + `Select1` для поиска дочерних узлов
- **Терминальные узлы**: `has_child == 0` как индикатор листа
- **Label lookup**: CP866 → UTF-8 декодирование

### Код
- `succinct_trie.cpp` / `succinct_trie.h` - обновлённый decoder
- `tools/test_decoder_v2.cpp` - standalone тест

## Проблема: Отсутствует оригинальный файл

Для тестирования нужен оригинальный Yandex Keyboard trie файл (1nc7 формат, ~4.5MB).

### Где найти:
1. Извлечь из Yandex Keyboard APK: `assets/trie` или `assets/dicts/russian/trie`
2. Или использовать предоставленный файл `trie` (без расширения)

### Как извлечь из APK:
```bash
# 1. Скачать Yandex Keyboard APK
# 2. Распаковать
unzip yandex-keyboard.apk -d yandex_apk

# 3. Найти trie файл
find yandex_apk/assets -name "trie" -type f

# 4. Скопировать в проект
cp yandex_apk/assets/dicts/russian/trie entry/src/main/resources/rawfile/dict/trie
```

## Ожидаемые результаты

При правильной работе decoder должен извлечь ~109,000 русских слов.

### Формат вывода:
```
1nc7: version=1 items=15614 payload=999284
Found 4 BVs
BV pop=369743
BV pop=369743
BV pop=801071
BV pop=402824
Using sub-level BVs 2,3
Labels: count=...
word[0]: аав
word[1]: аал
...
Total words: 108995
```

## Fallback

Если 1nc7 decoder не работает, используется `extracted_words.txt` (108K слов) как fallback.

## Следующие шаги

1. Получить оригинальный trie файл
2. Протестировать decoder
3. Если нужно - отладить навигацию (возможно формула Rank/Select другая)
4. Интегрировать с HOSKEY engine
