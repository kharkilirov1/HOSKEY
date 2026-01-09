# HOSKEY Prediction System Fix - Complete Overhaul

**Date**: 2026-01-09
**Branch**: `claude/review-project-audit-BS0BN`
**Status**: ✅ Complete

## Summary

Полностью переработана система предсказаний/подсказок в клавиатуре HOSKEY для HarmonyOS. Главная цель: **предсказания должны работать сразу, даже без BaseLexicon**, а архитектура должна позволять легко добавлять новые языки.

---

## Что было сломано

### 1. **Язык по умолчанию не соответствовал доступным ресурсам**
- **Проблема**: UI по умолчанию использовал `currentLanguage = 'en'`, но BaseLexicon был доступен только для `ru`
- **Результат**: При первом запуске клавиатуры предсказания не работали

### 2. **BaseLexicon был обязательным условием работы предсказаний**
- **Проблема**: `supportedLangs = ['ru']` жёстко ограничивал работу системы
- **Результат**: Для языков без BaseLexicon (например `en`) предсказания почти не появлялись

### 3. **Pending слова не участвовали в предсказаниях**
- **Проблема**: `predict()` проверял `c >= LEARN_THRESHOLD` (3 раза), игнорируя pending слова
- **Результат**: Подсказки появлялись только после многократного ввода одного и того же слова

### 4. **N-граммы не обновлялись для pending слов**
- **Проблема**: `recordWord()` вызывал `updateBigramTrigram()` только для learned слов
- **Результат**: Контекстные предсказания (следующее слово) не работали до достижения порога обучения

### 5. **Неправильный порядок проверок в автокоррекции**
- **Проблема**: `findAutocorrection()` вызывал `isBaseWord()` ДО `ensureBaseLexiconLoaded()`
- **Результат**: Автокоррекция могла работать некорректно из-за незагруженного лексикона

### 6. **Отсутствие анти-мусор фильтров**
- **Проблема**: Нет проверок на опечатки, цифры, символы, слишком короткие слова
- **Результат**: В подсказках могли появляться мусорные слова

### 7. **Разрозненная конфигурация**
- **Проблема**: Константы (LEARN_THRESHOLD, веса scoring и т.д.) были разбросаны по коду
- **Результат**: Сложно было настраивать и тестировать систему

---

## Что изменено

### 1. **Index.ets - Дефолтный язык**
**Файл**: `entry/src/main/ets/InputMethodExtensionAbility/pages/Index.ets:452`

**Было**:
```typescript
@State currentLanguage: LanguageCode = 'en';
@State availableLanguages: LanguageCode[] = ['en'];
```

**Стало**:
```typescript
@State currentLanguage: LanguageCode = 'ru'; // Default to Russian (has BaseLexicon)
@State availableLanguages: LanguageCode[] = ['ru']; // Default to Russian
```

**Зачем**: Избежать ситуации, когда UI использует язык без BaseLexicon по умолчанию

---

### 2. **PredictionModel.ets - Централизованная конфигурация**
**Файл**: `entry/src/main/ets/InputMethodExtensionAbility/model/PredictionModel.ets:57`

**Добавлен класс `PredictionConfig`**:
```typescript
class PredictionConfig {
  // Learning thresholds
  static readonly LEARN_THRESHOLD = 3;
  static readonly PENDING_SUGGEST_THRESHOLD = 1; // NEW!

  // Scoring weights
  static readonly SCORE_BASE_WEIGHT = 2;
  static readonly SCORE_LEARNED_WEIGHT = 5;
  static readonly SCORE_PENDING_WEIGHT = 1; // NEW!
  static readonly SCORE_CONTEXT_WEIGHT = 100;
  static readonly SCORE_RECENCY_WEIGHT = 1;
  static readonly SCORE_PREFIX_BOOST = 10; // NEW!

  // Anti-garbage filters
  static readonly MIN_WORD_LENGTH = 1;
  static readonly MAX_DIGIT_RATIO = 0.5;
  static readonly MAX_SYMBOL_RATIO = 0.3;

  // ... другие параметры
}
```

**Зачем**: Все настройки в одном месте, легко настраивать и экспериментировать

---

### 3. **BaseLexicon стал опциональным**
**Файл**: `entry/src/main/ets/InputMethodExtensionAbility/model/PredictionModel.ets:101`

**Было**:
```typescript
const supportedLangs = ['ru']; // Жёсткий список
supportedLangs.forEach(lang => {
  // ...
});
```

**Стало**:
```typescript
// BaseLexicon is OPTIONAL - prediction works without it
// To add new language (e.g. 'en'):
// 1. Add rawfile/en_base_words_50k.txt and en_base_freq_50k.tsv
// 2. Add 'en' to baseLexiconLanguages array below
// 3. That's it! No changes needed in predict()/recordWord()
const baseLexiconLanguages = ['ru'];
```

**Добавлены методы**:
```typescript
hasBaseLexicon(lang: string): boolean
getBaseLexicon(lang: string): BaseLexicon | null
```

**Зачем**: BaseLexicon теперь опциональное улучшение, а не условие работы

---

### 4. **recordWord() - Всегда обновлять n-граммы**
**Файл**: `entry/src/main/ets/InputMethodExtensionAbility/model/PredictionModel.ets:641`

**Было**:
```typescript
} else {
  // Still pending, increment count
  m.pendingWords.set(w, newPendingCount);
  // N-граммы НЕ обновлялись!
}
```

**Стало**:
```typescript
} else {
  // Still pending, increment count
  m.pendingWords.set(w, newPendingCount);

  // IMPORTANT: Update n-grams even for pending words!
  this.updateBigramTrigram(m, w, prev, prev2, now);

  // Track basic stats for pending words
  m.wordRank.set(w, (m.wordRank.get(w) ?? 0) + 1);
  m.lastUsed.set(w, now);
}
```

**Зачем**: Контекстные предсказания работают сразу, даже для новых слов

---

### 5. **predict() - Unified Candidate Pool с Scoring**
**Файл**: `entry/src/main/ets/InputMethodExtensionAbility/model/PredictionModel.ets:798`

**Полностью переписан метод с новой архитектурой**:

#### Unified Candidate Pool:
```typescript
const candidates = new Map<string, RankedWord>(); // Дедупликация
```

#### 5 источников кандидатов:
1. **Trigrams** (highest context)
2. **Bigrams**
3. **User learned words** (unigram)
4. **Pending words** ⭐ **NEW!** - после 1 ввода
5. **BaseLexicon** (если доступен)

#### Unified Scoring:
```typescript
const calculateScore = (word, contextCount, wordCount, isPending, baseFreq) => {
  // Context >> Learned >> Pending >> Base >> Recency >> Prefix boost
  if (contextCount > 0) {
    score += contextCount * PredictionConfig.SCORE_CONTEXT_WEIGHT;
  }
  if (!isPending && wordCount >= LEARN_THRESHOLD) {
    score += userRank * PredictionConfig.SCORE_LEARNED_WEIGHT;
  } else if (isPending) {
    score += wordCount * PredictionConfig.SCORE_PENDING_WEIGHT; // Меньший вес
  }
  score += baseFreq * PredictionConfig.SCORE_BASE_WEIGHT;
  score += recencyBonus * PredictionConfig.SCORE_RECENCY_WEIGHT;
  score += prefixBoost; // Если слово начинается с введённого префикса

  return score;
}
```

#### Anti-Garbage фильтры:
```typescript
private isGarbage(word: string, lang: string): boolean {
  // Слишком короткие (кроме "я", "в", "и" для ru)
  // Слишком много цифр (> 50%)
  // Слишком много символов (> 30%)
  // Содержит пробелы
}
```

**Зачем**:
- Предсказания работают даже без BaseLexicon
- Pending слова появляются после 1 ввода
- Контекст учитывается для всех слов
- Качество подсказок выше благодаря scoring

---

### 6. **findAutocorrection() - Правильный порядок**
**Файл**: `entry/src/main/ets/InputMethodExtensionAbility/model/PredictionModel.ets:731`

**Было**:
```typescript
async findAutocorrection(lang: string, word: string): Promise<string | null> {
  // ...
  if (this.isWordLearned(m, wordLower) || this.isBaseWord(lang, wordLower)) return null;

  await this.ensureBaseLexiconLoaded(lang); // СЛИШКОМ ПОЗДНО!
}
```

**Стало**:
```typescript
async findAutocorrection(lang: string, word: string): Promise<string | null> {
  // ...
  // IMPORTANT: Load BaseLexicon FIRST before checking isBaseWord
  await this.ensureBaseLexiconLoaded(lang);
  const baseLexicon = this.getBaseLexicon(lang);

  if (this.isWordLearned(m, wordLower) || this.isBaseWord(lang, wordLower)) return null;
}
```

**Зачем**: Корректная проверка на базовые слова после загрузки лексикона

---

### 7. **Тесты обновлены**
**Файл**: `entry/src/test/PredictionModel.test.ets`

**Добавлены новые тест-кейсы**:
1. ✅ **Pending words after 1 occurrence** - слово появляется в подсказках после одного ввода с префиксом
2. ✅ **Context predictions for pending words** - биграммы работают даже для pending слов
3. ✅ **Works without BaseLexicon** - предсказания работают для языков без лексикона

**Исправлены существующие тесты**:
- Добавлен `async/await` для всех вызовов `predict()`
- Увеличено количество повторений для достижения `LEARN_THRESHOLD`

---

## Как добавить BaseLexicon для нового языка (например `en`)

### Шаг 1: Подготовить файлы
Создайте два файла:
- `entry/src/main/resources/rawfile/en_base_words_50k.txt` - список слов (одно слово на строку)
- `entry/src/main/resources/rawfile/en_base_freq_50k.tsv` - частоты (формат: `слово\tчастота`)

### Шаг 2: Зарегистрировать язык
В файле `PredictionModel.ets:111` измените:
```typescript
const baseLexiconLanguages = ['ru', 'en']; // Добавьте 'en'
```

### Шаг 3: Всё!
Больше ничего менять не нужно. Логика `predict()` и `recordWord()` уже поддерживает любые языки.

---

## Результаты

### До исправления:
❌ Предсказания не работали для `en` (язык по умолчанию)
❌ Подсказки появлялись только после 3+ вводов
❌ Контекстные предсказания не работали для новых слов
❌ BaseLexicon был обязательным для работы
❌ Мусорные слова могли попадать в подсказки

### После исправления:
✅ Предсказания работают сразу для `ru` (новый дефолт)
✅ Подсказки появляются после 1 ввода с префиксом
✅ Контекстные предсказания работают для pending слов
✅ BaseLexicon опционален (fallback на user model)
✅ Anti-garbage фильтры улучшают качество
✅ Легко добавить новый язык (3 простых шага)

---

## Другие критичные баги (обнаруженные в процессе)

### 1. ✅ **Исправлено ранее**: HarmonyOS delete direction
В HarmonyOS InputMethodKit терминология обратная:
- `deleteForward(n)` = удаляет n символов ПЕРЕД курсором (Backspace)
- `deleteBackward(n)` = удаляет n символов ПОСЛЕ курсора (Delete)

Все вызовы `deleteBackward` заменены на `deleteForward` в 5 местах (commit `d8d5f56`).

### 2. ✅ **Исправлено ранее**: ArkTS compiler errors
- Добавлен интерфейс `VerbPattern` для явной типизации
- Исправлены destructuring parameters (не поддерживается в ArkTS)
- Исправлена ссылка на несуществующее свойство `shadowPopup` (commit `23922dd`)

### 3. ✅ **Исправлено ранее**: Swipe Typing с DTW алгоритмом
Полная реализация swipe-to-type с Dynamic Time Warping (commits `2f0a191`, `23922dd`).

---

## Тестирование

### Сборка
```bash
# Проверить сборку
hvigorw assembleHap --mode module -p module=entry@default -p product=default
```

### Тесты
```bash
# Запустить unit тесты
hvigorw test
```

### Ожидаемое поведение:
1. Клавиатура запускается с языком `ru` по умолчанию
2. После ввода слова 1 раз оно появляется в подсказках (при вводе префикса)
3. После ввода "hello world" 2 раза, при вводе "hello" предлагается "world"
4. Для языков без BaseLexicon подсказки всё равно работают

---

## Авторы
- AI Assistant (Claude)
- Branch: `claude/review-project-audit-BS0BN`
- Date: 2026-01-09
