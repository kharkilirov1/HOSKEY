 #Ghidra GUI: найти 1nc7 parser

## Проект
`C:\Users\Kharki\ghidra_yandex\YKB.gpr`

## Задача
Найти функцию, которая парсит файл `trie` (magic `1nc7`, 4.5MB).

## Шаги в Ghidra

### 1. Найти регистрацию handler'а
- Перейти к адресу **0x005513b8**
- Это функция, которая регистрирует "1nc7" формат
- Строка "1nc7" находится в rodata at **0x00040b86**
- Она вызывает `format_register` (0x22cda4) и PLT handler register (0x818140)

### 2. Найти init_array constructor
- Перейти в секцию `.init_array` (addr 0x920268, size 1984)
- Один из конструкторов вызывает 0x005513b8
- Найти его через XREF к 0x005513b8

### 3. Trace handler registration
- Глобальная переменная в BSS: **0x943f60** хранит format name
- Через XREF к 0x943f60 найти где handler сохраняется
- Handler имеет vtable с методом `parse` (обычно vtable[4] или vtable[5])

### 4. Декомпилировать parse method
- Это основная функция — она читает `1nc7` header, затем парсит секции
- Сохранить decompiled C код в: `C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\tools\1nc7_parser_decompiled.c`

## Что уже известно

### Адреса ключевых функций
| Адрес | Назначение |
|-------|-----------|
| 0x005513b8 | 1nc7 format registration |
| 0x00663c34 | trie_init (заполняет runtime struct) |
| 0x006ee9d8 | yarchive blob parser |
| 0x00661538 | rank operation |
| 0x006622a7 | label_to_unicode |

### Runtime struct (из предыдущего Ghidra анализа)
```
+0x060: bitvector_1
+0x0B8: bitvector_2
+0x120: terminal_bits
+0x128: terminal_rank
+0x148: hash_table
+0x160: labels_data
+0x178: label_count/size
+0x180: weights_data
```

### Файл trie (4.5MB)
```
[0, 24):    Header (magic=1nc7, version=1, payload_hint=999284, item_count=15614)
[24, 5776): Body prefix (5752B, dense bitvector data)
[5776, 636376): 4 x (bitvector + rank) sections
[636376, 999284): Remaining data (362KB, small byte indices)
[999284, EOF-128): Tail (labels + preamble)
[EOF-128, EOF): Footer metadata (_VERSION=1, _ORDER=3, _LANG_ID=1)
```

### 4 bitvector sections (найдены rank L1 tables)
| Rank offset | BV offset | Popcount | Entries |
|------------|-----------|----------|---------|
| body+130744 | body+5752 | 369,743 | 1953 |
| body+292744 | body+167752 | 369,743 | 1953 |
| body+448936 | body+~324000 | 801,071 | 1953 |
| body+605128 | body+~480000 | 402,824 | 1953 |
