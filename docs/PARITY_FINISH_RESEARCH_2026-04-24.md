# HOSKEY parity finish research

Дата: 2026-04-24  
Цель: понять, как довести движок до настоящего parity, а не до имитации через `txt`/flat word list.

## 1. Короткий вердикт

Мы идём в правильную сторону по упаковке и native integration, но **неправильно считать текущий `lexicon.words` решением**.

Что уже исправлено правильно:

- внешний `resources/rawfile/dict/words.txt` больше не нужен runtime;
- `.hap` содержит контейнер `ru.hoskey.hosdict`, а не loose `words.txt`;
- acceptance сейчас проходит: `ACCEPTANCE OK`;
- движок видит 1nc7 asset (`succinct=ok`), `tfmtrie`, `lemmer`, `normdict`, policy assets.

Что ещё не является parity:

- внутри `.hosdict` всё ещё лежит `lexicon.words` (`weighted_wordlist_v1`) и именно он строит основной `PredictorEngine`;
- текущий `SuccinctTrie` не реализует реальную 1nc7 state machine;
- `SuccinctTrie::Tokenize()` и `SuccinctTrie::GetTokenString()` пока stub;
- `IsKnownWord()` считает prefix валидным словом через `IsValidPrefix()`, это не Yandex semantics;
- `ranker` и `nnlm` сейчас видны как assets, но runtime уходит в fallback: `ranker=ms-present/fallback`, `nnlm=ms-present/fallback`.

Главный вывод: **следующий финишный шаг — заменить `lexicon.words` на реальный 1nc7 candidate provider с active-state traversal и terminal epsilon-handoff.**

## 2. Проверенное текущее состояние

Текущий контейнер:

```text
entry/src/main/resources/rawfile/dict/ru.hoskey.hosdict
size=32,154,576 bytes
assets=18
```

Ключевые assets:

```text
lexicon.words                                      weighted_wordlist_v1       2,391,030
lexicon.default                                    succinct_lexicon_v1        4,498,484
lexicon.experiments.KEYBOARD-13421_v2              succinct_lexicon_k13421   11,049,724
rules.transfeme                                    tfmtrie_v1                 1,784,112
rules.transfeme_experiments.KEYBOARD-13421_v2      tfmtrie_k13421             1,907,056
normdict.default                                   normdict_v2                1,159,136
lemma.hash                                         chd_hash                   1,804,698
lemma.rules                                        lemma_rules_program          539,252
policy.blacklist                                   policy_blob                  374,455
policy.autocorrect_blocker                         policy_blob                    6,389
features.autocc                                    feature_json                   4,718
features.ranker                                    feature_json                   2,734
features.tap_model                                 feature_json                   1,270
ranking.ranker                                     mindspore_model            1,507,040
ranking.neural_model                               mindspore_model            5,112,360
```

Acceptance на момент исследования:

```text
probe.pr=про|прямо|прошу|просто|привет|правда
diag=engine=hoskey, trie=118141w/230007n, succinct=ok, tfm=31cls,
     lemmer=ok, normdict=ru, ranker=ms-present/fallback,
     nnlm=ms-present/fallback
ACCEPTANCE OK
```

Это хороший smoke gate, но он подтверждает только baseline, не parity.

## 3. Reverse finding: почему простой `prefix -> node_id` неверен

Самый важный результат R3 reverse:

```text
reverse/R3_state_machine_prototype_2026-04-09.txt
Model idea: visible prefix maps to a SET of node states, not a single node_id.
Total prefixes in graph: 22973
Ambiguous visible prefixes: 260
```

Пример `"ла"`:

```text
reverse/R3_case_study_la_2026-04-09.txt

л  -> node 14
ла -> node 308 (term=0, has_child=1)
ла -> node 325 (term=1, has_child=1)
```

Reverse semantics:

- `FUN_0066242c` на non-terminal node пишет `label|0x100` и возвращает label;
- `FUN_0066242c` на terminal node не выдаёт label, а считает terminal rank, вызывает `FUN_00661462`, затем `FUN_00661baa`;
- `FUN_0066268c` видит empty emitted char и рекурсирует с тем же input character;
- значит terminal nodes работают как **epsilon-transitions** между prefix states.

Практический вывод для engine:

```cpp
ActivePrefixState = set<NodeState>

struct NodeState {
  uint32_t node_id;
  bool terminal_here;
  bool has_child;
  // later: level/subtrie/handoff context
};
```

Candidate generation должен объединять:

- direct continuation from non-terminal states;
- handoff continuation from terminal states;
- exact terminal candidates отдельно от plain prefixes.

Текущая модель `FindPrefixNode(prefix) -> optional<uint64_t>` архитектурно недостаточна.

## 4. 1nc7 layout: что уже известно

Рабочие R3 scripts для default `trie` используют такие участки:

```text
has_child/raw0    0x000008
louds/raw1        0x0278D8
terminal_raw      0x0753C8
terminal_ds       0x093BC8
intvector         0x09B5E8
labels            0x0FDB98
```

Проверенная root-phase navigation из latest probes:

```text
first_child = select1(rank1(has_child, node_id)) + 1
sibling stepping via louds/raw1 bit
label = cp1251(labels[node_id]) for root-phase visible labels
terminal = terminal_raw.bit(node_id)
```

Контрольные reverse facts:

```text
Clean root letters: 30
абвгдежзийклмнопрстуфхцчшщыэюя

Clean bigrams count: 624
Clean trigrams count: 4875

First terminal boundary length: 2
First boundary examples: дш, ла, рь, уё, шк, юх, яу
```

Но это только root-phase. Для полного parity нужно воспроизвести:

- terminal-rank -> `FUN_00661462`;
- `FUN_00661baa` handoff;
- multi-level / sub-trie transitions;
- packed label upper bits / IntVector;
- exact terminal vs prefix state semantics.

## 5. Почему текущий `SuccinctTrie` не финальный

Файл:

```text
entry/src/main/cpp/hosdict_engine/succinct_trie.cpp
```

Проблемы:

1. Парсинг bitvectors эвристический, через scan headers, не через подтверждённый 1nc7 loader.
2. `main_level_.has_child = load_bv(h[0])` фактически fallback.
3. Label mapping использует искусственный `Utf8ToLabelIndex()`, не реальный cp1251/packed label/handoff.
4. `FindPrefixNode()` возвращает один node, хотя reverse доказал `set[node_state]`.
5. `Tokenize()` и `GetTokenString()` пустые, поэтому neural path не может быть корректным.
6. `GetCompletions()` не учитывает terminal epsilon transitions.
7. `IsKnownWord()` сейчас принимает valid prefix как known word.

Финиш: `SuccinctTrie` надо либо переписать, либо заменить на новый `OneNc7Trie` / `SuccinctCandidateProvider`.

## 6. Model/assets gap

Yandex config (`extracted_sections/config`) указывает не только на `trie`, а на весь predictor bundle:

```text
Default.LM                         trie
Default.Transfemes                 rules.bin
Default.NeuralModel.Path           neural_model
Default.CharModel                  char_model
Default.RnntModel                  rnnt_model
Default.Ranker/Catboost.Path       ranker
Default.TapModel.Ranker.Path       tap_model_ranker
Default.TapModel.Classifier.Path   tap_model_classifier
Default.Autocc.TreeAutocorrectModel.Path tree_autocorrect_model
Default.Predictor/EmojiSearch.Path search_emoji_model
Default.Predictor/EmojiSuggest.Path emoji_model

Exp/KEYBOARD-13421_v2.LM           KEYBOARD-13421_trie
Exp/KEYBOARD-13421_v2.Transfemes   KEYBOARD-13421_rules.bin
Exp/KEYBOARD-13421_v2.NeuralModel.Path KEYBOARD-13321_nnlm_model
```

Текущий `.hosdict` не пакует большинство этих raw assets. Он пакует только два `.ms` файла:

- `ranking.ranker`;
- `ranking.neural_model`.

Но runtime shape expectations не совпадают:

- `MLRanker` ждёт один input на `RankerFeatures::kFeatureCount == 39`;
- текущий `ranker.ms` по reverse/model analysis соответствует не scalar ranker, а state transform (`[1x500] -> [1x1500]`);
- `NeuralLM` ждёт NNLM signature с `2 inputs / 3 outputs`;
- текущий `ranking.neural_model` не закрывает live NNLM path;
- поэтому diagnostics честно показывает fallback.

Отдельно важно: `ynnlm_v1` wrapper содержит несколько embedded TFLite blobs. Старый `rip_model.py` вырезает от первого `TFL3` до EOF, это даёт не полноценную модель, а concatenated tail. Для parity нужен extractor, который извлекает каждый FlatBuffer по реальной длине и связывает его с ролью из config.

## 7. Финишный план

### Stage 0 — container-only baseline

Статус: почти закрыто.

Done gate:

- в HAP нет `resources/rawfile/dict/words.txt`;
- runtime не читает loose `words.txt`;
- acceptance проходит.

Открытый хвост:

- `lexicon.words` ещё внутри `.hosdict`; это временный bridge, не parity.

### Stage 1 — parity corpus before rewrite

Нельзя переписывать 1nc7 вслепую. Сначала нужен corpus/golden runner.

Сделать:

1. `tools/parity_corpus.jsonl`:
   - prefixes из R3: `ла`, `лаб`, `алв`, `бое`, `ст`, `пр`, `не`, `она`, `что`, `яу`, split cases;
   - common prefixes: `хор`, `при`, `про`, `как`, `это`, `что`;
   - autocorrect: `вообщем`, keyboard-near typos, blocker cases;
   - context cases from `previousTokens/fullContext`;
   - geometry cases.
2. Runner:
   - current HOSKEY;
   - reverse oracle from R3 scripts;
   - captured Yandex suggestions where available.
3. Metrics:
   - top-1 agreement;
   - top-3 overlap;
   - `willAutocorrect`;
   - known/exact/prefix separation.

### Stage 2 — real 1nc7 parser/provider

Implement new C++ component:

```text
entry/src/main/cpp/hosdict_engine/one_nc7_trie.{h,cpp}
```

Responsibilities:

- parse 1nc7 by confirmed structure, not by generic scan;
- support current `trie` and `KEYBOARD-13421_trie`;
- expose rank/select bitvectors;
- expose labels with actual Yandex label semantics;
- model prefix as `ActivePrefixState`;
- implement terminal epsilon-handoff;
- implement exact terminal check separately from prefix check;
- implement `Tokenize()` and `GetTokenString()` for neural paths;
- expose completion iterator with weights/frequencies later.

Minimum API:

```cpp
class OneNc7Trie {
 public:
  Status Parse(const uint8_t* data, size_t size);
  ActivePrefixState WalkPrefix(std::string_view utf8) const;
  bool ContainsExact(std::string_view utf8) const;
  bool HasPrefix(std::string_view utf8) const;
  std::vector<Candidate> Complete(std::string_view prefix, size_t limit) const;
  std::vector<uint32_t> Tokenize(std::string_view utf8) const;
  std::string TokenString(uint32_t token_id) const;
};
```

Validation gates:

- root letters exactly match R3 list;
- bigrams count = 624 for root-phase probe;
- `ла` maps to both nodes `308` and `325`;
- ambiguous visible prefix count = 260 for same probe depth;
- all prototype transition records match R3 `direct/handoff` views;
- `ContainsExact("она") == true`, `HasPrefix("он") == true`, but prefix-only forms are not known words;
- full vocabulary dump overlaps with extracted/reference oracle at high threshold before deleting bridge.

### Stage 3 — remove `lexicon.words` bridge

Only after Stage 2 passes:

1. Stop packing `lexicon.words` in `tools/build_hosdict.py`.
2. Remove `package_.store().FindWordList("lexicon.words")` from primary path.
3. Replace `PredictorEngine` primary source with `OneNc7TrieCandidateProvider`.
4. Keep old `PredictorEngine` only as debug/fallback behind explicit flag.
5. Update acceptance:

```cpp
Require(!container.HasAsset("lexicon.words"));
Require(!fs::exists(package_dir / "words.txt"));
Require(diagnostics contains "one_nc7=ok");
Require(diagnostics does not contain "trie=118141w");
```

This is the point where we stop being “flat word list keyboard”.

### Stage 4 — n-gram / weight tail

R4 already decoded key value encodings:

- `LEB128 + ZigZag`;
- unsigned `LEB128`;
- prefix-varint + ZigZag;
- unsigned prefix-varint;
- record skippers `FUN_00609a96` / `FUN_006092f6`.

Implement:

```text
entry/src/main/cpp/hosdict_engine/ngram_freqs.{h,cpp}
```

Use it for:

- unigram weights from native trie tail;
- bigram/context weights;
- `lm_1gr_static`, `lm_2gr_static`, `lm_ngr_static`;
- next-word prediction.

Done gate:

- no TSV/flat weights needed for ranking;
- context changes suggestion order;
- `features_ranker.json` static LM slots get real values.

### Stage 5 — ML/ranker/autocorrect models

Do not force all assets through one fake `MLRanker`.

Needed split:

```text
CharLMRunner
SubwordNNLMRunner
WordNNLMRunner
CatboostOrTreeRankerRunner
TapModelRunner
AutoccModelRunner
EmojiModelRunner
```

Work:

1. Pack all config-referenced model assets into `.hosdict` with semantic keys and original raw key aliases.
2. Write model inventory tool:
   - extract every embedded `TFL3` correctly;
   - detect exact input/output signatures;
   - map signature to runner role.
3. Make runtime diagnostics strict:

```text
ranker=loaded:<role>
nnlm=loaded:<role>
char_lm=loaded
autocc_model=loaded
```

No more `ms-present/fallback` pretending to be model integration.

Done gate:

- if model asset is present but signature is unsupported, diagnostics says unsupported with shape;
- if loaded, it contributes non-zero feature values;
- ranker output changes ordering on controlled corpus.

### Stage 6 — autocorrect parity

Current autocorrect path is baseline:

- manual rules;
- fuzzy;
- tfmtrie candidates;
- simple safety guard.

Need:

- exact `features_autocc.json` feature computation;
- blocker policy parity;
- `tree_autocorrect_model` / `KEYBOARD-13421_autocc`;
- separation of suggest vs autocorrect decision;
- threshold calibration against corpus.

Done gate:

- common typos autocorrect correctly;
- exact known words never autocorrect;
- blocker cases stay blocked;
- `willAutocorrect` matches corpus.

### Stage 7 — live IME/personal/emoji parity

After core engine:

- real app/package context from IME API;
- extended previous context;
- personalization state;
- personal dictionary/blacklist;
- emoji candidate branch;
- swipe/gesture branch;
- remote predictor compatibility hooks.

This is important, but not the current blocker. The current blocker is still `lexicon.words`/1nc7.

## 8. Immediate implementation tickets

Recommended order:

1. Add `tools/r3_dump_active_trie.py` that emits machine-readable JSON oracle from current R3 scripts:

```json
{"prefix":"ла","states":[{"node":308,"term":0},{"node":325,"term":1}],"direct":"...","handoff":"..."}
```

2. Add C++ unit/acceptance runner for `OneNc7Trie` against that JSON.
3. Implement `OneNc7Trie` root-phase exactly.
4. Implement terminal epsilon-handoff exactly enough for all R3 prototype records.
5. Replace `SuccinctTrie::FindPrefixNode()` single-node API with active-state API.
6. Wire `KeyboardEngine` to use `OneNc7Trie` for:
   - exact;
   - prefix;
   - completion;
   - `CheckWord`.
7. Only then remove `lexicon.words` from `.hosdict`.
8. Then implement R4 native frequency reader.
9. Then fix ML/ranker assets and runners.

## 9. What “done” means now

Minimum real parity gate:

```text
Package:
  no words.txt
  no lexicon.words
  has lexicon.default / experiment trie

Trie:
  one_nc7=ok
  active-state traversal passes R3 corpus
  exact != prefix
  terminal epsilon-handoff works

Predictor:
  top-k corpus passes without flat word list
  context weights affect order
  autocorrect corpus passes

ML:
  no ms-present/fallback on required model path
  unsupported shapes are explicit
  loaded runners produce non-zero features

IME:
  live typing uses same engine path as tests
  no UI freeze
```

## 10. Final answer to “правильно ли идём?”

Да — по направлению к container/native engine.

Нет — если оставить `lexicon.words` как основу. Это будет не Yandex-like engine, а быстрый bridge.

Правильный finish path:

```text
1nc7 active-state candidate provider
-> native ngram/frequency tail
-> real model runners by signature
-> corpus-gated removal of lexicon.words
-> autocorrect/context/IME parity
```

Самый важный следующий кодовый шаг: **OneNc7Trie + ActivePrefixState + terminal epsilon-handoff**, затем удаление `lexicon.words`.

## 11. Implementation progress — 2026-04-25

Выполнен первый кодовый шаг по этому плану.

Добавлено:

- `tools/r3_dump_active_trie.py`
  - генерирует machine-readable oracle:
    `reverse/R3_active_trie_oracle_2026-04-25.json`;
  - подтверждённые totals:
    - `root_letters=абвгдежзийклмнопрстуфхцчшщыэюя`;
    - `total_prefixes=22973`;
    - `ambiguous_prefixes=260`.
- `entry/src/main/cpp/hosdict_engine/one_nc7_trie.h`
- `entry/src/main/cpp/hosdict_engine/one_nc7_trie.cpp`
  - native active-state root-phase decoder;
  - `WalkPrefix(prefix) -> vector<OneNc7NodeState>`;
  - `NextLetters(prefix)` с разделением `nonterminal` / `terminal`;
  - terminal-boundary visibility for handoff cases.
- `tools/one_nc7_trie_acceptance.cpp`
- `tools/run_one_nc7_trie_acceptance.ps1`
  - acceptance по R3 anchors:
    - `ла -> 308(term=0), 325(term=1)`;
    - `алв -> 964(term=0), 982(term=1)`;
    - `бое -> 1340(term=1), 1346(term=0), 1355(term=1)`;
    - `яу -> 720(term=1), 739(term=0)`.
- `KeyboardEngine` теперь загружает `OneNc7Trie` параллельно с legacy `SuccinctTrie`;
  diagnostics показывает `one_nc7=ok`.

Проверено:

```text
tools/run_one_nc7_trie_acceptance.ps1 -> ONE_NC7 ACCEPTANCE OK
tools/run_engine_acceptance.ps1       -> ACCEPTANCE OK
tools/run_keyboard_engine_diag.ps1    -> success_cases=96, one_nc7=ok
tools/build_final.ps1                 -> BUILD SUCCESSFUL
HAP check                             -> no words.txt, only ru.hoskey.hosdict
```

Важно: это ещё не удаляет `lexicon.words`. Это первая безопасная интеграция правильной 1nc7 active-state модели рядом с текущим bridge. Следующий шаг — использовать `OneNc7Trie` как candidate provider и расширить traversal за root-phase / terminal handoff до полного vocabulary + ngram path.

## 12. Implementation progress — 2026-04-25 / binary predictor bridge removed

Следующий слой доведён: runtime больше не пакует и не читает `lexicon.words`
по умолчанию.

Изменено:

- `tools/build_hosdict.py`
  - вместо `lexicon.words weighted_wordlist_v1` генерирует
    `lexicon.predictor predictor_trie_v1`;
  - `lexicon.words` остаётся только как dev fallback при явном
    `HOSKEY_PACK_LEGACY_WORDLIST=1`;
  - `predictor_trie_v1` — бинарный trie:
    `HPT1 + nodes(first_edge, edge_count, weight, max_subtree_weight) + edges(label, child)`.
- `PredictorEngine`
  - добавлен `LoadBinary(data, size)`;
  - загружает prebuilt trie без парсинга текстового списка слов.
- `KeyboardEngine`
  - primary path: `lexicon.predictor`;
  - legacy `lexicon.words` используется только если binary predictor отсутствует;
  - diagnostics теперь показывает `trie_src=bin`;
  - `IsKnownWord()` больше не считает любой 1nc7 prefix точным словом через
    `SuccinctTrie::IsValidPrefix()`;
  - добавлен безопасный experimental gate
    `HOSKEY_EXPERIMENTAL_ONE_NC7_FILTER=1`, который может фильтровать
    predictor prefix-кандидаты через `OneNc7Trie::NextLetters()`.
- `tools/build_final.ps1`
  - перед hvigor теперь всегда пересобирает `.hosdict`, чтобы HAP не уносил
    устаревший словарный контейнер.
- `tools/engine_acceptance.cpp`
  - проверяет, что в контейнере нет `lexicon.words`;
  - проверяет, что есть `lexicon.predictor`;
  - проверяет runtime `trie_src=bin`.
  - проверяет, что prefix-only `прив` не проходит как exact word.

Проверено:

```text
python tools/build_hosdict.py
  -> PACK lexicon.predictor ... (5.3MB)
  -> no lexicon.words

tools/run_engine_acceptance.ps1
  -> ACCEPTANCE OK
  -> diag includes: trie=118141w/230007n, trie_src=bin, one_nc7=ok

HOSKEY_EXPERIMENTAL_ONE_NC7_FILTER=1 tools/run_engine_acceptance.ps1
  -> ACCEPTANCE OK
  -> diag includes: one_nc7_filter=1

tools/run_one_nc7_trie_acceptance.ps1
  -> ONE_NC7 ACCEPTANCE OK

tools/run_keyboard_engine_diag.ps1
  -> success_cases=96
  -> predictor_cases=96
  -> succinct_cases=96

tools/build_final.ps1
  -> BUILD SUCCESSFUL

HAP check
  -> words.txt entries: []
  -> lexicon.words: []
  -> lexicon.predictor: predictor_trie_v1, 5,520,184 bytes
```

Текущий статус точнее:

- loose `words.txt` удалён;
- container text bridge `lexicon.words` удалён по умолчанию;
- основной predictor runtime теперь бинарный, не текстовый;
- `OneNc7Trie` уже подключён и может быть включён как transition-filter;
- полный Yandex parity всё ещё требует добить native 1nc7 candidate
  materialization за root-phase/terminal handoff и заменить binary bridge на
  прямой `lexicon.default` provider.

## 13. Implementation progress — 2026-04-25 / prebuilt binary asset gate

Дожат ещё один слой против возврата к text bridge:

- добавлен `tools/lexicon.predictor.hpt`;
  - это prebuilt `HPT1` binary trie;
  - сборка по умолчанию берёт именно его, а не TSV;
  - TSV используется только при явном
    `HOSKEY_REGENERATE_PREDICTOR=1`.
- `tools/build_hosdict.py`
  - больше не генерирует predictor из `words_with_weights.tsv` по умолчанию;
  - при отсутствии `tools/lexicon.predictor.hpt` падает с явной ошибкой;
  - `lexicon.words` по-прежнему пакуется только при
    `HOSKEY_PACK_LEGACY_WORDLIST=1`.
- добавлен `tools/package_asset_gate.py`;
  - проверяет HAP на отсутствие loose assets:
    `words.txt`, `words_with_weights.tsv`, `lexicon.predictor.hpt`;
  - проверяет `.hosdict`:
    - нет `lexicon.words`;
    - есть `lexicon.predictor predictor_trie_v1`;
    - есть `lexicon.default`;
  - проверяет magic/counts prebuilt `HPT1`.
- `tools/build_final.ps1`
  - после `assembleApp` запускает `package_asset_gate.py`.

Проверено:

```text
python tools/build_hosdict.py
  -> PACK lexicon.predictor: lexicon.predictor.hpt
  -> без обращения к words_with_weights.tsv в default mode

tools/run_engine_acceptance.ps1
  -> ACCEPTANCE OK

tools/run_one_nc7_trie_acceptance.ps1
  -> ONE_NC7 ACCEPTANCE OK

tools/build_final.ps1
  -> BUILD SUCCESSFUL
  -> PACKAGE ASSET GATE OK
```

Итог текущего этапа:

- runtime: нет txt/wordlist bridge;
- package: нет `lexicon.words`;
- build default: нет TSV-generation bridge;
- HAP gate: запрещает loose dictionary artifacts.

Оставшийся финишный parity-блок теперь стал уже не packaging, а именно
native `lexicon.default` candidate materializer:

```text
OneNc7Trie root-phase OK
-> нужно расширить traversal за root-phase/subtrie
-> восстановить terminal handoff candidate materialization
-> подключить native frequency/ngram tail
-> после этого убрать временный binary predictor.hpt bridge
```

## 14. Implementation progress — 2026-04-25 / recursive 1nc7 skeleton in runtime

Добавлен следующий native слой 1nc7 поверх root-phase traversal:

- `OneNc7Trie` теперь парсит рекурсивную структуру `FUN_00660496`:
  - raw -> bv -> raw -> bv -> raw -> ds -> raw -> ds -> intvector-tail;
  - tail blobs;
  - recursive child trie start.
- Новый runtime API:
  - `OneNc7Trie::levels()`;
  - `OneNc7Trie::FormatLevelSummary()`.
- `KeyboardEngine` diagnostics теперь показывает:
  - `one_nc7_levels=3`.
- `tools/one_nc7_trie_acceptance.cpp` теперь проверяет не только root active
  states, но и точные offsets рекурсивных уровней:

```text
level0 start=0x8      labels=0xFDB98  len=999288 child=1
level1 start=0x1F1B20 labels=0x1FA530 len=42248  child=1
level2 start=0x204A48 labels=0x205A20 len=4752   suffix=0x206CB8 len=3208 child=0
```

Проверено:

```text
tools/run_one_nc7_trie_acceptance.ps1
  -> one_nc7.levels=3
  -> ONE_NC7 ACCEPTANCE OK

tools/run_engine_acceptance.ps1
  -> diag includes: one_nc7=ok, one_nc7_levels=3
  -> ACCEPTANCE OK

tools/run_keyboard_engine_diag.ps1
  -> success_cases=96

tools/build_final.ps1
  -> BUILD SUCCESSFUL
  -> PACKAGE ASSET GATE OK
```

Значение этого шага: runtime уже знает не только root-level graph, но и
официальную рекурсивную раскладку label/suffix blobs. Следующий кодовый шаг —
использовать level2 suffix blob (`+0x180`) для native candidate materialization,
а не только для диагностики.

## 15. Implementation progress — 2026-04-25 / native suffix pool surfaced

Дожат следующий безопасный слой materialization path:

- `OneNc7Trie` теперь декодирует `+0x180` suffix/continuation pool:
  - `SuffixEntries(level_index, max_entries)`;
  - `SuffixEntryCount(level_index)`;
  - `HasSuffixEntry(level_index, text)`.
- Для level2 подтверждено:

```text
suffix_blob=0x206CB8
suffix_size=3208
suffix_entries=770
head:
  1: " а"
  4: " ал"
  8: " ар"
  12: " б"
  15: " бе-"
  20: " благ"
  26: " бог"
  31: " бол"
  36: " в"
  39: " воскрес"
```

- Acceptance теперь проверяет конкретные suffix entries:
  - `"про"`;
  - `" собач"`;
  - `"кого-нельзя"`.
- `KeyboardEngine` diagnostics теперь показывает:
  - `one_nc7_suffix=770`.

Проверено:

```text
tools/run_one_nc7_trie_acceptance.ps1
  -> suffix_count=770
  -> ONE_NC7 ACCEPTANCE OK

tools/run_engine_acceptance.ps1
  -> diag includes: one_nc7_suffix=770
  -> ACCEPTANCE OK

HOSKEY_EXPERIMENTAL_ONE_NC7_FILTER=1 tools/run_engine_acceptance.ps1
  -> ACCEPTANCE OK

tools/run_keyboard_engine_diag.ps1
  -> success_cases=96

tools/build_final.ps1
  -> BUILD SUCCESSFUL
  -> PACKAGE ASSET GATE OK
```

Это ещё не полный candidate provider, но это уже runtime-доступ к реальному
native continuation pool, из которого Яндекс добирает хвосты кандидатов. Для
финального снятия `lexicon.predictor.hpt` остаётся связать iterator node/key ->
suffix offset точно по `FUN_00661926/FUN_00661462`.

## 16. Implementation progress — 2026-04-25 / FUN_00661462 terminal key layer

Дожат следующий reverse-backed слой 1nc7 materialization path.

Добавлено в `OneNc7Trie`:

- полная runtime-метадата по рекурсивным уровням:
  - raw offsets для `has_child`, `louds`, `terminal`;
  - `terminal_rank_ds_offset`;
  - `intvector_offset`, `intvector_data_offset`, `bitwidth`, `mask`, `total_bits`, `word_count`.
- API для packed upper bits / terminal key:
  - `PackedUpper(level, rank)`;
  - `TerminalKey(level, node_id, rank)`;
  - `RootTerminalRank(node_id)`;
  - `RootTerminalKey(node_id)`.
- Реализована C++-эмуляция `FUN_00661462`:

```text
key = (packed_intvector[terminal_rank] & mask) << 8 | label_blob[node_id]
```

Acceptance теперь проверяет конкретный reverse anchor из `R3_case_study_la`:

```text
ла terminal node=325
terminal_rank=29
packed_upper=14
terminal_key=0xEE0
```

Также добавлен первый безопасный native root-phase completion iterator:

- `TerminalCompletions(prefix, max_extra_letters, max_results)`;
- optional runtime gate `HOSKEY_EXPERIMENTAL_ONE_NC7_NATIVE=1` добавляет такие
  кандидаты в `KeyboardEngine` с tag `one_nc7`;
- по умолчанию выключено, чтобы не ломать ranking до полного subtrie/handoff.

Проверено:

```text
tools/run_one_nc7_trie_acceptance.ps1
  -> root_terminal.la.rank=29
  -> root_terminal.la.key=0xee0
  -> completions.л includes ла@325
  -> ONE_NC7 ACCEPTANCE OK

tools/run_engine_acceptance.ps1
  -> ACCEPTANCE OK

HOSKEY_EXPERIMENTAL_ONE_NC7_NATIVE=1 tools/run_keyboard_engine_diag.ps1
  -> success_cases=96
  -> diagnostics include one_nc7_native=N
```

Текущий честный статус: мы уже исполняем root active-state traversal, suffix pool
и `FUN_00661462` terminal-key слой нативно из `lexicon.default`. Оставшийся
блок до полного снятия `lexicon.predictor.hpt` — точное продолжение
`FUN_00661baa -> FUN_00661926` через child trie и suffix offsets.
