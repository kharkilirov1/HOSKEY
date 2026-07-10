# HOSKEY Parity Roadmap

## Цель

Цель проекта — не просто “рабочая клавиатура”, а **максимально полный runtime parity с Яндекс.Клавиатурой** по:

- suggest request / response contract
- predictor pipeline
- autocorrect behavior
- live IME context handling
- personalization
- geometry / proximity input
- emoji / voice / post-processing / remote hooks

Done для проекта больше **не определяется** как “fallback работает”.
Done определяется как **поведение живой клавиатуры статистически и функционально совпадает с эталоном на acceptance corpus**.

## Текущий статус на 2026-04-13

### Уже есть

- Native hosdict engine интегрирован в IME runtime.
- Baseline vs verified-split compare работает без UI freeze.
- Основной live suggestion row уже получает подсказки из native path.
- В NAPI/ArkTS contract добавлен первый parity-oriented слой полей beyond `token/previousTokens`.

### Ещё нет полного parity

- Нет полного live proximity / touch points pipeline.
- Neural / geometric feature path пока не доведён до эталонного уровня.
- Нет полного personal dictionary / blacklist / shortcut / session parity.
- Нет emoji / voice / remote predictor parity.
- Нет acceptance corpus, который формально закрывает parity goal.

## Definition of Done

Проект считается завершённым только если выполнены все пункты:

1. **API parity**
   - Все подтверждённые поля `SuggestRequest` поддержаны в bridge/runtime.
   - Все подтверждённые поля `SuggestResponse` доступны в IME/UI.

2. **Behavior parity**
   - На acceptance corpus расхождение top-1 / top-3 / autocorrect decision находится в допустимом пороге.
   - Нет систематических regressions на split-prefix, fuzzy, context, blocked words.

3. **Runtime parity**
   - Live IME не блокирует UI.
   - Все debug / compare / live suggest path используют один и тот же реальный pipeline.

4. **Feature parity**
   - Personalization, geometry, emoji, voice, remote/postprocess покрыты или явно замоканы с documented gap.

## Workstreams

### Phase A — Request/Response Contract Parity

**Цель:** довести runtime contract до reverse-confirmed формы.

#### A1. Bridge fields
- [x] `token`
- [x] `previousTokens`
- [x] `locale`
- [x] `wantAutocorrect`
- [x] `maxSuggestions`
- [x] `input`
- [x] `fullContext`
- [x] `optionsArray`
- [x] `emojiFactor`
- [x] `packageName`
- [x] `isUrgent`
- [x] `fieldActionId`
- [x] `gender`
- [x] `age`
- [x] `prevWordsInfo`
- [x] `dictionarySessions`
- [x] `proximityInfo`
- [x] `points`

#### A2. IME request plumbing
- [x] Передавать live `fullContext` из text buffer cache
- [x] Передавать `fieldActionId`
- [x] Передавать session/package/options metadata
- [ ] Передавать реальный app/package context из IME API, а не static constant
- [x] Передавать live touch/proximity data из UI
- [ ] Передавать extended context history beyond simple previous word

#### A3. Response parity
- [x] `suggestions`
- [x] `willAutocorrect`
- [x] `isBlocked`
- [x] `diagnostics`
- [ ] `autocorrectFeatures`
- [ ] `emojis`

### Phase B — Predictor Pipeline Parity

**Цель:** приблизить сам engine к фактической Yandex-style fusion architecture.

#### B1. Existing stack hardening
- [x] exact / prefix / fuzzy predictor
- [x] tfmtrie typo candidates
- [x] lemma expansion
- [x] ML reranking
- [x] verified-split overlay compare path

#### B2. Missing parity slices
- [ ] parallel predictor orchestration
- [ ] neural predictor integration into live rank path
- [x] geometric features from touch/proximity
- [ ] richer autocorrect features / blockers
- [ ] emoji search / prefix suggest branch
- [ ] remote predictor compatibility layer

### Phase C — Live IME Geometry / Proximity Parity

**Цель:** уйти от word-only runtime к real keyboard behavior.

- [x] Capture tap coordinates from keyboard UI
- [x] Build `points[]` request stream
- [x] Describe live layout into `proximityInfo`
- [x] Feed geometry into ranker features
- [ ] Add swipe history / gesture hooks
- [ ] Validate hit-key ambiguity cases against acceptance corpus

### Phase D — Personalization Parity

**Цель:** приблизить поведение к user-adaptive keyboard.

- [ ] personal n-gram update path
- [ ] personal shortcut support
- [ ] personal dictionary persistence
- [ ] prune / stats / diagnostics
- [ ] personal blacklist
- [ ] autocorrect blocker parity

### Phase E — Emoji / Voice / Remote / Postprocess

- [ ] emoji candidate API + ranking
- [ ] voice session compatibility hooks
- [ ] speech post-process path
- [ ] remote predictor toggle / diagnostics

### Phase F — Acceptance & Regression Gates

**Цель:** формально мерить parity, а не оценивать “на глаз”.

- [ ] Собрать acceptance corpus из:
  - typo cases
  - split-prefix cases
  - context-sensitive cases
  - autocorrect cases
  - blocked/personalized cases
  - geometry ambiguity cases
- [ ] Сделать baseline runner against live engine
- [ ] Считать top-1 / top-3 / autocorrect agreement
- [ ] Ввести regression gate для каждого parity slice

## Ближайший критический путь

### Сейчас
1. Закрыть **Phase A** до полезного runtime уровня.
2. Подключить **live IME context** к request builder.
3. Включить **acceptance logging** для реальной клавиатуры.

### После этого
4. Реализовать **touch/proximity capture**.
5. Довести **neural + geometric ranking**.
6. Добавить **personalization state**.

## Правило приоритета

Если возникает выбор между:

- ещё одним UI debug улучшением
- ещё одним частным словарным фиксапом
- или системным parity slice

приоритет всегда у **systemic parity slice**, если он сокращает разрыв с reverse-confirmed поведением.
