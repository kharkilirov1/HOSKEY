# HOSKEY agent handoff — 2026-03-29 (updated)

## Коротко
Состояние проекта двоякое:
- инфраструктура загрузки контейнера, NAPI, ArkTS и diagnostics работает end-to-end;
- настоящий native decoder для `trie` **не доведён**: `lexicon.default` всё ещё даёт `word count: 11` и мусорные слова (`А`, `г`, `Р`, ...).

Чтобы не блокировать usability, добавлен **временный fallback**:
- из `dict_ru_full` извлечён UTF-8 word list;
- он подключён как `lexicon.default_payload`;
- runtime теперь предпочитает его как активный словарь.

Это workaround, а не решение корневой задачи decoder parity.

---

## Что уже работает

### Runtime / app path
- `.hosdict` контейнер ставится и открывается на устройстве.
- `getLoadReports()` работает.
- `SetupGuidePage` логирует structured reports и native notes.
- `getSuggestions("вообщем", "", "ru-RU") -> [в общем]` уже подтверждалось runtime-логами.

### Suggestion pipeline (keyboard_engine.cpp)
- `GetDefaultWordSource()` — приоритет: `lexicon.default_payload` > `lexicon.default` > `lexicon.experiments.KEYBOARD-13421_v2`
- `CandidateGenerator::Generate()` — exact match (binary search), prefix match, Levenshtein-1 fuzzy fallback
- `ContextScorer::Score()` — frequency-based scoring (manual_rule > exact > prefix > fuzzy > identity)
- `AutocorrectEngine::Apply()` — manual rules + Levenshtein-based autocorrect с blocker policy
- Sorted word index кэшируется per word list pointer (thread-safe)

### Native diagnostics
В `SuccinctTrie::AnalyzeBasinIR()` уже выведены:
- `basin seq` / `basin actions` / `basin edges`
- `basin roots` / `basin records` / `record roots` / `record hotspot`
- `basin sig` / `basin sample` / `record sig` / `record sample`
- `focus record` / `focus roots` / `focus edges` / `focus cont`
- `focus motifs` / `focus motif sample`
- `focus semantic` / `focus semantic sample`
- `focus chains` / `focus chain sample`
- `focus seeds` / `focus seed sample`
- `word sample`

### Reverse progress
Reverse-заметки зафиксированы в `reverse/`, ключевые файлы:
- `reverse/TRIE_FINDINGS_2026-03-26.md`
- `reverse/TRIE_OPERATOR_0x89_FINDINGS_2026-03-27.md`
- `reverse/TRIE_OPERATOR_0x0A_FINDINGS_2026-03-27.md`
- `reverse/TRIE_OPERATOR_0xAE_FINDINGS_2026-03-27.md`
- `reverse/TRIE_CP_LEADING_IR_FINDINGS_2026-03-27.md`
- `reverse/TRIE_WEIGHTED_OPERATOR_GRAPH_2026-03-27.md`
- `reverse/TRIE_PATH_BASED_IR_2026-03-27.md`

Рабочая IR-модель:
- `B_EY = OP_89 -> EY`
- `B_U  = OP_0A -> U`
- `B_YU = OP_AE -> YU`

Hotspot: `R3` — главный record family.

---

## Диагноз decoder'а (deep inspection)

### Структура файла `trie`
Файл имеет формат `1nc7` с 6-dword заголовком:
```
magic:         0x37636E31 ("1nc7" LE)
version:       1
payload_hint:  999284    <-- граница body/tail
reserved:      0
item_count:    15614     <-- предположительно число слов
reserved:      0
```

### Что файл НЕ является
Файл **не является** стандартным LOUDS succinct trie. Текущий код пытается:
1. `ParseYArchive()` — ищет yarchive section table в конце файла. Терпит неудачу.
2. `ParseTrieBlob()` — сканирует 4KB блоки по bit density / byte mean, пытается найти bitvector и label regions. **Это главная проблема**: heuristic splitting bitvector region пополам для has_child/terminal — почти наверняка неверно.
3. `ParseDFSFallback()` — ищет marker bytes (0x40, 0x48, 0x50, 0x58, 0x20) + UTF-8 Cyrillic. Из-за неправильных markers даёт 11 мусорных слов.

### Реальная структура (из reverse-анализа)
- **Body** (offset 24..999284): ~15,600 x 64-byte rows. Каждая row содержит 4 x 16-byte records. Каждый record имеет offset (dword at +8) указывающий в tail region.
- **Tail** (offset 999284+): cp1251-encoded text fragments, перемежаемые operator bytes (0x89, 0x0A, 0xAE, 0x94, 0x20, etc.). Это НЕ bitvector и НЕ byte array labels в привычном смысле.
- Operators работают как routing:
  - `OP_89` → route to `EY` class
  - `OP_0A` → route to `U` class
  - `OP_AE` → route to `YU` class
- Records в body формируют index, tail содержит actual word fragments.

### Почему DFS/LOUDS не работает
- `FirstChild()` / `ChildrenEnd()` возвращают raw rank values, не делают proper Select0 на LOUDS BV
- Даже если бы Select0 работал, он применяется к неправильно идентифицированным bitvectors
- Label table (`label_low`) назначена к случайному byte-array region
- В результате DFS генерирует бессмысленные 1-символьные "слова"

### Что нужно для настоящего fix
Перестать пытаться парсить это как LOUDS. Вместо этого:
1. Читать body как массив 64-byte rows с 4 records по 16 bytes каждый
2. Для каждого record извлекать tail offset
3. В tail region декодировать cp1251 text fragments, распознавая operators как delimiters/routing
4. Реконструировать слова из цепочек tail fragments

---

## Что было добавлено как временный workaround

### Новый fallback-лексикон
Сгенерирован файл:
- `entry/src/main/resources/rawfile/dict/extracted_words.txt`

Источник:
- извлечён из `entry/src/main/resources/rawfile/dict/dict_ru_full`
- объём после фильтрации: **108,995** слов

### Изменённые файлы
- `entry/src/main/cpp/hosdict_engine/asset_manifest.cpp`
  - добавлен asset `lexicon.default_payload` (format: `utf8_wordlist_v1`, file: `extracted_words.txt`)
- `entry/src/main/cpp/hosdict_engine/loaders.cpp`
  - добавлен формат `utf8_wordlist_v1`
  - загрузка UTF-8 word list в `LoadedWordListAsset`
  - фильтрация: `IsAcceptedExtractedWord()` — только кириллица, 2-32 символа
  - дедупликация через `unordered_set<string> seen`
- `entry/src/main/cpp/hosdict_engine/keyboard_engine.cpp`
  - `GetDefaultWordSource()` теперь предпочитает `lexicon.default_payload`, если он есть

### Важно
Это **не** настоящий decoder fix.
Это временный путь, чтобы клава могла начать опираться на большой словарь до завершения reverse.

---

## Что подтвердили последние device logs
Последний лог до fallback показал:
- `focus record: R3=496`
- `focus roots: OP_89->EY=273, OP_0A->U=270, OP_AE->YU=133`
- `focus semantic: SEM_U_LOCAL=130, SEM_EY_LOCAL=92, SEM_YU_LOCAL=63, SEM_EY_LOOP=39, SEM_EY_REFINE=26, SEM_U_LOOP=26, SEM_EY_TO_U=23, SEM_U_TRANSIT=18`
- `focus chains`: уже видны, но пока шумные
- `focus seeds`: уже видны, но пока шумные
- `word sample`: мусорные одиночные буквы

Вывод:
- diagnostics path богатый и полезный;
- traversal всё ещё не годится.

---

## Архитектура кода (reference)

### C++ engine pipeline
```
KeyboardEngine::GetSuggestions(request)
  -> RuleEngine::Normalize(request)
  -> CandidateGenerator::Generate(normalized, package)
       -> GetDefaultWordSource(package)  // выбирает активный лексикон
       -> manual rules check
       -> exact match (binary search on SortedWordIndex)
       -> prefix matches (lower_bound scan)
       -> Levenshtein-1 fuzzy (nearby codepoints scan)
  -> ContextScorer::Score(suggestions)
  -> ResultMaterializer::Materialize(request, suggestions)
  -> AutocorrectEngine::Apply(normalized, package, response)
```

### Asset loading pipeline
```
BuildKnownRuLayoutManifest()
  -> LexiconLoader::Load()
       -> "utf8_wordlist_v1": ReadFileText + AppendAcceptedWordsFromUtf8List -> PutWordList
       -> "succinct_lexicon_v1": ReadFileBytes + SuccinctTrie::Parse() -> PutWordList (или PutBinary при failure)
  -> PolicyLoader, FeatureConfigLoader, NormDictLoader, LemmerLoader, TransfemeRuleLoader
```

### SuccinctTrie::Parse() flow
```
Parse(data, size)
  -> BuildCP866Table()
  -> detect magic
  -> if "1nc7": ParseTrieBlob(data+24, size-24)
       -> ParseYArchive() [fails]
       -> heuristic block scan [wrong structure detection]
  -> EnumerateAllWords()
       -> DFSCollect(0, 0, ...) [produces garbage]
  -> if empty: ParseDFSFallback() [produces 11 garbage words]
  -> AnalyzeBasinIR() [diagnostics only, не влияет на слова]
```

---

## Следующий агент: что делать дальше

### Ветка A — pragmatic usability
1. Запустить app на устройстве.
2. Проверить, что загружается `lexicon.default_payload`.
3. Проверить, что активный lexicon переключился на payload.
4. Оценить качество suggestions/contains/prefix search уже на fallback-словаре.

### Ветка B — настоящий fix (рекомендуется)
Идти в: `entry/src/main/cpp/hosdict_engine/succinct_trie.cpp`

**Стратегия:**
1. **Перестать расширять diagnostics.** AnalyzeBasinIR() уже даёт достаточно данных.
2. **Отказаться от LOUDS-парсинга.** Файл не LOUDS trie.
3. **Написать record-based parser:**
   - Читать body (offset 24..payload_hint) как массив 64-byte rows
   - Каждая row = 4 records x 16 bytes
   - Из каждого record: извлечь tail offset (LE32 at record+8)
   - В tail: декодировать cp1251 runs как word fragments
   - Operators (0x89, 0x0A, 0xAE) — delimiters между fragments
4. **Валидация:** word sample должен стать похож на реальные русские слова.
5. **Только после этого** возвращать `lexicon.default` как основной источник.

### Самый точный immediate target
Не reverse "в целом", а именно:
- Найти границы 3 уровней trie в body (соответствует `_ORDER=3`)
- Идентифицировать bitvectors (has_child, terminal, louds) для каждого уровня
- Сопоставить label sections в body/tail с bitvector one_counts
- Детальный анализ формата: `docs/TRIE_FORMAT_ANALYSIS_2026-03-29.md`
- **Найдены 4 rank-table + bitvector секции** в body (каждая 125KB + 31KB rank)
- `1nc7` trie — succinct bitvector формат для runtime, декодирование сложное
- **КЛЮЧЕВОЕ ОТКРЫТИЕ:** `dict_ru_full` — контейнер с DFS-encoded trie (magic `0xfe3ac19b`)
- Полный Ghidra reverse уже выполнен ранее: `C:\Users\Kharki\clawd\v600_analysis\`
- **Decoder существует:** `yandex_trie_decoder.py` + `FULL_TRIE_DECOMPILED.c` (272KB)
- **108,848 слов уже извлечены** в `words_final.csv`
- DFS формат: markers 0x40(NODE)/0x48(TERM)/0x20(SPACE) + UTF-8 кириллица
- Спецификация: `FINAL_FORMAT_SPEC.md`, `ONDISK_LAYOUT.md`, `KEY_FUNCTIONS_ANALYSIS.md`

### Что сделано в текущей сессии (C++ code changes)
1. **`loaders.cpp`**: добавлен `DecodeDfsInterleaved()` — нативный DFS decoder
   - Markers: 0x40(NODE)/0x48(TERM)/0x50(LEAF)/0x58(XTND)/0x20(SPACE) + UTF-8
   - Container magic check: `0xFE3AC19B`, blob at offset `0x2720`
   - Формат: `dfs_interleaved_v1`
2. **`asset_manifest.cpp`**: добавлен asset `lexicon.dfs_container` → `dict_ru_full`
3. **`keyboard_engine.cpp`**: DFS container — первый приоритет в `GetDefaultWordSource()`

### 1nc7 succinct trie — ФОРМАТ РАСКРЫТ (2026-03-30)
- **Parser найден**: `FUN_0065f2d8` — main 1nc7 parser (декомпилирован из Ghidra GUI)
- **Section loader**: `FUN_00660496` — РЕКУРСИВНЫЙ, строит два trie level'а
- **Bitvector reader**: `FUN_00661806` — header(32B) + rank(L1/L2 16B stride) + bits
- **Data reader**: `FUN_006615a4` — header(24B) + data(count*8+8)
- **Точные offset'ы подтверждены**: 4 BV sections at file+130744/292744/448936/605128
- **Двухуровневая структура**: main trie (BV1+BV2) + recursive sub-trie (BV3+BV4)
- **Полная документация**: `docs/1NC7_FORMAT_DECODED.md`
- Следующий шаг: написать sequential parser используя exact section offsets

---

## Полезные файлы для нового агента

### C++ runtime
- `entry/src/main/cpp/hosdict_engine/succinct_trie.cpp` — **главный blocker**
- `entry/src/main/cpp/hosdict_engine/succinct_trie.h`
- `entry/src/main/cpp/hosdict_engine/trie_basin_ir.cpp` / `.h`
- `entry/src/main/cpp/hosdict_engine/loaders.cpp`
- `entry/src/main/cpp/hosdict_engine/keyboard_engine.cpp`
- `entry/src/main/cpp/hosdict_engine/asset_manifest.cpp`
- `entry/src/main/cpp/napi_bridge/hosdict_bridge.cpp`

### ArkTS/UI
- `entry/src/main/ets/InputMethodExtensionAbility/model/HoskeyEngine.ets`
- `entry/src/main/ets/pages/SetupGuidePage.ets`

### Tooling / reverse
- `tools/analyze_trie.py` — Python-анализатор структуры trie файла
- `tools/decode_trie_basin_ir.py` — декодер basin IR
- `tools/extract_trie_words.py` — извлечение слов
- `reverse/*.md` — 67 reverse-engineering заметок

---

## Честный итог
- Research **зафиксирован**.
- Reverse notes **есть**.
- Memory **сохранена**.
- Diagnostics **избыточны** — больше расширять не нужно.
- Workaround **активен** и даёт ~109K слов для usability.
- **Корневая причина** теперь понятна: файл не LOUDS, а record-based index + tail payload.
- Decoder parity **ещё нет**. Нужен record-based parser вместо LOUDS heuristics.
