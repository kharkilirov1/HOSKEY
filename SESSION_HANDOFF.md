# HOSKEY — session handoff (2026-07-11)

> Заметка для следующего ИИ, которому передают проект. Кратко: что сделано в этой
> сессии, текущее состояние кода, что осталось, как собирать/ставить/тестировать,
> и где грабли. Глубокий журнал по шагам — в `RELEASE_QUALITY.hermes.md` (тот же
> каталог), здесь — навигационная сводка.

## 0. Что за проект и цель сессии
HOSKEY — офлайновая HarmonyOS-клавиатура (input-method extension). Нативный C++
движок в `entry/src/main/cpp/hosdict_engine` читает реверс-инженеренный бинарный
формат словаря / нейро-LM Яндекс-клавы, чтобы давать **офлайн** дополнение слов,
автокоррект, предсказание. ArkTS-слой (`entry/src/main/ets/InputMethodExtensionAbility`)
рулит UI, обучением (`PredictionModel`) и NAPI-мостом. Реверс — это interop с
собственными ассетами (у пользователя извлечён APK в `C:/Users/Kharki/Desktop/yandex_extracted`).
**Цель сессии:** довести выдачу подсказок до похожести на настоящую Яндекс-клаву
(«ру раскладка работает, но выдача не как на телефоне»), затем — соседние фичи
(next-word, autocc, свайп).

## 1. Что сделано в этой сессии (всё собрано и по большей части провалидировано на устройстве)

### 1.1 Реверс MatrixNet-ранкера Яндекса — ПОЛНОСТЬЮ (витнесс bit-exact)
- Исходник: `C:/Users/Kharki/Desktop/yandex_extracted/extracted_sections/KEYBOARD-13421_ranker`
  (574016 Б). Это **MatrixNet: 1000 oblivious (симметричных) деревьев глубины 6**.
- Формат вскрыт: файл = `[984×u32 header] [1000 записей ×560Б] [таблица порогов 10080Б]`.
  Запись 560Б = `[16Б шапка][64 листа float64=512Б][6 условий ×… = сплиты]`. Хвост =
  per-feature border table; условие `c` = «фича f > порог t». Инвариант корректности:
  плоская таблица условий = 2274 == max(cond)+1 == 2274 (СХОДИТСЯ).
- Спека 44 фич: `extracted_sections/features_ranker.json` (28 Base + 5 Combos + 11
  KeyboardSpecific). Порядок фич = порядок в JSON = feature id 0..43.
- Генератор модели (ПЕРСИСТЕНТНЫЙ, т.к. scratchpad вытирается между ходами):
  `tools/build_ranker_model.py` → пишет `ms_models/ranker_model.bin` (формат 'HRNK',
  295196 Б: magic, u32 n_trees=1000, u32 44, per-feat [u16 nb][nb×f32], per-tree
  6×[u8 feat,f32 thr]+64×f32 leaf).
- Оценщик в C++: `ml_ranker.{h,cpp}` — `MLRanker::LoadMatrixNet` + `EvalMatrixNet`
  + `RankerFeatures::ToVector44()`. Чистый C++ (без MindSpore), работает и на хосте.
  **Витнесс:** воспроизводит Python-эталон бит-в-бит по 6 константным векторам
  (0→7.7881, 5→7.2551, 50→5.0735, …). Конвенция листа: `idx = OR_i (x[feat_i] > thr_i) << i`.

### 1.2 Диагноз выдачи и step-1 фикс (ЖИВОЙ, провалидирован)
- Диагноз: движок генерирует правильных кандидатов, но нейро-цепочка добавлялась в
  порядке **сырого логита** → редкие основы («прогами/прогрыз») вместо частотных
  слов. Телефон тянет частотные инфлектированные слова.
- Фикс step-1 (`keyboard_engine.cpp`, блок нейро-препенда ~строка 906): пере-ранк
  нейро-кандидатов по **частоте** (`nnlm_.VocabularyWeight` ∈ [100..1100]) + логит
  как tie-break через `stable_sort`.
- **A/B на устройстве (эмулятор) vs эталон телефона: 6/8 совпадают** (3 точно:
  привет/спаси/здрав; 3 сильно: прог/мага/погод; 1 частично: дом; 1 расхождение: кот).

### 1.3 MatrixNet ИНТЕГРИРОВАН, но ВЫКЛЮЧЕН для нейро-кандидатов
- Ассет `ranking.matrixnet` пакуется в контейнер (`build_hosdict.py`), грузится в
  `keyboard_engine.cpp` (~строка 340, `ranker_.LoadMatrixNet`), MatrixNet приоритетен
  в `ranker_.ScoreOne`.
- НО: MatrixNet-переранк нейро-кандидатов **просел vs step-1** (всплывал «прогрел»
  вместо «программа») — т.к. 2 его топ-фичи скормлены нулями (см. §3). Поэтому в
  `keyboard_engine.cpp` стоит `constexpr bool kMatrixNetForNeural = false;` — нейро
  использует step-1, MatrixNet остаётся активен только для эвристического пула
  (`ranker_.Score`). **Флаг = точка повторного включения.**

### 1.4 Next-word (предсказание следующего слова) — ДОБАВЛЕНО, собрано
- `ynnlm_word_chain.cpp Predict`: теперь принимает пустой префикс — пропускает шаг
  префикса, декодирует продолжение из hidden последнего контекстного слова
  (`StartsWith("")` пропускает все слова). Гейт: пустой префикс И пустой контекст → пусто.
- `keyboard_engine.cpp`: гейт нейро-блока расширен на `(size>=4 || empty)`; для
  next-word логит-порядок сохранён (частотный ре-ранк применяется только при
  непустом префиксе); флаг `kSuggestionFlagPrediction`. Срабатывает на границе слова.

### 1.5 Autocc (авто-заглавная / авто-пробел) — ДОБАВЛЕНО, собрано
- `entry/src/main/ets/.../pages/Index.ets`: авто-заглавная после `. ! ?`
  (в `onBoundaryTyped`) и при пустом поле (`onBackspaceTyped isBufferEmpty`);
  helper `endsSentence()`. Авто-пробел после выбора подсказки уже был (`pickSuggestion`).
  Провалидировано визуально (все слова с заглавной).

### 1.6 Инфраструктура сборки/установки — ПОЧИНЕНА
- CLI HAP-сборка падала («SDK component missing»): нужно
  `export DEVECO_SDK_HOME="C:/Program Files/Huawei/DevEco Studio/sdk"` (корень SDK, не
  подпапка openharmony). Сделан `setx` (новые шеллы).
- Установка на эмулятор: **неподписанный HAP ставится напрямую** —
  `hdc -t 127.0.0.1:5555 install -r <hap>` («install bundle successfully»). IDE не нужна.

## 2. Изменённые/новые файлы
- `entry/src/main/cpp/hosdict_engine/ml_ranker.h` / `ml_ranker.cpp` — MatrixNet (NEW методы).
- `entry/src/main/cpp/hosdict_engine/keyboard_engine.cpp` — MatrixNet load, step-1 re-rank,
  `kMatrixNetForNeural=false`, next-word гейт.
- `entry/src/main/cpp/hosdict_engine/ynnlm_word_chain.cpp` — пустой префикс (next-word).
- `entry/src/main/ets/InputMethodExtensionAbility/pages/Index.ets` — autocc.
- `tools/build_hosdict.py` — ассет `ranking.matrixnet`.
- `tools/build_ranker_model.py` — NEW, генератор HRNK-модели.
- `RELEASE_QUALITY.hermes.md` — подробный журнал (дополнен).

## 3. Что осталось (roadmap, по приоритету)

### 3.1 Дореализовать MatrixNet (чтобы побил step-1, напр. кейс «кот»)
Не хватает 2 топ-фич (по частоте сплитов деревьев):
- **transfeme-дистанция** (фичи 16/17, топ-1): ДАННЫЕ уже в приложении
  (`rules.transfeme`, `tfmtrie_` грузится). Нужна только ПРОВОДКА — считать дистанцию
  original↔correction. Не реверс. Для чистых дополнений она ≈0 (низкий приоритет).
- **lm_ngr_main** (фича 8, топ-3, ~340 сплитов + комбо 28/30): нейро-LM «естественность
  в контексте». Сигнал ЕСТЬ (логит word_chain), но **масштаб не тот** (модель ждёт
  ~[3..35], логит ~[−10..90]). Два пути:
  - **(A) Калибровка** — подогнать монотонное преобразование логита под уже снятый
    эталон телефона. Не реверс, часы. Рекомендуется первым.
  - **(B) Frida-реверс** точных значений с Android-телефона (хук нативного экстрактора
    фич) → байт-в-байт. Многодневно, нужен телефон.
  После этого: `kMatrixNetForNeural = true`, пере-A/B, оставить если ≥ step-1.

### 3.2 Свайп-ввод (glide) — БОЛЬШОЕ, не начато
Ассеты есть (`ms_models/ranker_swipe.ms`, `extracted_sections/features_ranker_swipe.json`,
`KEYBOARD-13421_ranker_swipe`, swipe-trie), но НЕТ декодера. Нужно: UI-захват пути
пальца (x,y,t) + отличие свайпа от тапа + path→последовательность клавиш + декодер
(сопоставление со словарём) + свайп-ранкер + коммит на конце жеста. Требует ЖИВОГО
устройства для UX-отладки. Алгоритм декодера Яндекса НЕ реверсён (либо реверсить,
либо стандартная path-to-word эвристика).

### 3.3 Прочее
- tap-модель (geometry_* фичи, коррекция по касанию; `tap_model_classifier/ranker.ms`).
- emoji-подсказки (`search_emoji_model.ms`).
- голос/ASR (`rnnt_model.ms`).
- другие языки (пользователь отложил).
- Мелочь: авто-заглавная при ПЕРВОМ открытии пустого поля не подключена (нет
  onStartInput-хука; сейчас — только после `. ! ?` и опустошения поля).

## 4. Как собирать / ставить / тестировать
```bash
# СБОРКА HAP (обязательно DEVECO_SDK_HOME + --no-daemon чтобы не лочить выходной файл):
cd C:/Users/Kharki/DevEcoStudioProjects/HOSKEY
export DEVECO_SDK_HOME="C:/Program Files/Huawei/DevEco Studio/sdk"
node "C:/Program Files/Huawei/DevEco Studio/tools/hvigor/bin/hvigorw.js" \
     --mode module -p product=default assembleHap --no-daemon
# → entry/build/default/outputs/default/entry-default-unsigned.hap

# УСТАНОВКА на эмулятор (неподписанный ставится в dev-режиме):
hdc -t 127.0.0.1:5555 install -r <путь к .hap>   # копируй hap в короткий путь, hdc не любит длинные

# ПЕРЕСБОРКА КОНТЕЙНЕРА (если менялись ассеты; тянет из ms_models):
python tools/build_hosdict.py     # → entry/src/main/resources/rawfile/dict/ru.hoskey.hosdict (118MB, 23 ассета)

# ПЕРЕСБОРКА MatrixNet-модели:
python tools/build_ranker_model.py    # → ms_models/ranker_model.bin (инвариант 2274)

# СИНТАКС-ЧЕК C++ на хосте (быстро, MatrixNet-часть не под #ifdef → проверяется):
cd entry/src/main/cpp && g++ -std=c++17 -fsyntax-only -I hosdict_engine -I . hosdict_engine/keyboard_engine.cpp

# ДИАГНОСТИКА движка на устройстве:
hdc -t 127.0.0.1:5555 shell "hilog -x | grep -iE 'Native engine open|word_chain|matrixnet'"
```

## 5. Грабли (важно!)
- **scratchpad вытирается между ходами** — все артефакты, которые нужны надолго,
  клади в проект или `ms_models`. (Поэтому генератор модели — в `tools/`.)
- **Эмулятор**: DevEco «PuraAPI24», hdc-таргет `127.0.0.1:5555`, HarmonyOS API 24 (6.1.0),
  резолюция плавает (видел 1320×2856 и 1308×2880 — калибруй координаты по свежему скрину).
  Поднять из CLI: `Emulator.exe -start PuraAPI24` (в `C:/Program Files/Huawei/DevEco Studio/tools/emulator/`).
  Если hdc не видит: `hdc kill` (или убить процессы hdc), `hdc start`, `hdc tconn 127.0.0.1:5555`.
- **uinput по клавишам на эмуляторе флакистый** (дропает/мажет тапы, ⌫/✕ ненадёжны) —
  ручной набор пользователем надёжнее. Свайп спейсбара для смены языка работает.
- **Язык клавы сбрасывается на English** при рестарте процесса — свайп спейсбара до «Russian»
  (в списке много языков: English→Persian→Polish→Italian→Arabic→…→Russian).
- **frida-attach подавляет показ IME** (нужен unfocus+refocus + поллинг
  `dumpsys input_method | mInputShown`). frida-server 16.7.19 уже на телефоне/эмуле;
  скрипты автора: `yandex_extracted/frida_*.js`.
- **Android-телефон** (Mi 9, adb) — для Frida-эталона настоящей Яндекс-клавы; сейчас
  не подключён. Пакет `ru.yandex.androidkeyboard`. Раскладка сбрасывается так же.
- `InputClient.deleteForward` в HarmonyOS удаляет ПЕРЕД курсором (backspace).
- HAP CLI-сборка НЕ подписана (`signingConfigs: []`); эмулятор dev-режима принимает
  неподписанный, физический телефон — нет (нужна IDE или полная подпись).

## 6. Эталон телефона (Яндекс-клава, RU, снят визуально)
```
прог   -> Проголосовала | Программа | Проголосуй
мага   -> Магазин | Маган | Магаз | Магазины
привет -> Приветик | Приветствую | Приветики
спаси  -> Спасибо | Спасибочки | Спасибки
кот    -> Котик | Коть | Котя | Котёнок
дом    -> Дома | Домой | Домофон | Домашний
здрав  -> Здравствуйте | Здравствуй | Здравия
погод  -> Погода | Погоди | Погодь | Погодка
```

## 7. Текущее состояние одной строкой
Нейро-дополнение + step-1 частотный ре-ранк + next-word + autocc — ЖИВЫЕ и на
устройстве, выдача ≈ телефон (6/8). MatrixNet-оценщик точный (bit-exact) и упакован,
но ВЫКЛЮЧЕН для нейро (нужны его топ-фичи lm_ngr_main+transfeme). Свайп/tap/emoji/
голос/др.языки — не начаты. CLI build+install работает.
