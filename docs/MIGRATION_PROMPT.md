# InputPipeline Migration Prompt

## ЗАДАЧА

Мигрировать HarmonyOS клавиатуру HOSKEY с монолитной архитектуры (Index.ets ~2700 строк) на чистую архитектуру InputPipeline (model/input/).

## ТЕКУЩЕЕ СОСТОЯНИЕ

### Монолитная архитектура (Index.ets)
```
Index.ets (2700+ строк)
├── Состояние текста: currentWord, previousWord, prevPrevWord
├── Debouncing: suggestionsDebounceTimer, debouncedUpdateSuggestions()
├── Прямые вызовы: keyboardController.insertText/deleteBeforeCursor/sendEnterKey
├── Predictions: predictionModel.predict(), optimizedPredictionModel.predict()
├── Autocorrection: predictionModel.findAutocorrection()
├── Swipe typing: ~500 строк inline кода
└── UI: KeyView, KeyPopup, EmojiPanel, ClipboardPanel
```

### Готовая альтернативная архитектура (НЕ ИСПОЛЬЗУЕТСЯ)
```
model/input/
├── InputPipeline.ets      - Координатор событий (Fast/Slow/Swipe Path)
├── InputTypes.ets         - Типы: InputEvent, ITextClientAdapter, ISuggestionPresenter, IPredictor
├── TextBuffer.ets         - Состояние текста (currentToken, prevWord, prev2Word)
├── Debouncer.ets          - Отложенные вызовы (30ms)
├── HarmonyTextClientAdapter.ets - Адаптер для HarmonyOS API
├── PredictorFacade.ets    - LRU кэш + async predict()
└── index.ets              - Barrel export
```

## ПЛАН МИГРАЦИИ

### ЭТАП 1: Создать недостающие адаптеры

#### 1.1 Создать SuggestionPresenterAdapter
Файл: `model/input/SuggestionPresenterAdapter.ets`

```typescript
/**
 * Адаптер для обновления suggestions в UI
 * Связывает InputPipeline с @State suggestions в Index.ets
 */
import { ISuggestionPresenter } from './InputTypes';

export class SuggestionPresenterAdapter implements ISuggestionPresenter {
  private updateCallback: ((suggestions: string[]) => void) | null = null;

  /**
   * Установить callback для обновления UI
   * @param callback - Функция которая обновит @State suggestions
   */
  setUpdateCallback(callback: (suggestions: string[]) => void): void {
    this.updateCallback = callback;
  }

  show(suggestions: string[]): void {
    if (this.updateCallback) {
      // Pad to 3 elements for UI consistency
      const padded = [
        suggestions[0] ?? '',
        suggestions[1] ?? '',
        suggestions[2] ?? ''
      ];
      this.updateCallback(padded);
    }
  }

  clear(): void {
    if (this.updateCallback) {
      this.updateCallback(['', '', '']);
    }
  }
}
```

#### 1.2 Расширить InputPipeline для autocorrection и recordWord

В `InputPipeline.ets` добавить:

```typescript
// === НОВЫЕ ПОЛЯ ===
private autocorrectionEnabled: boolean = true;
private lastAutocorrection: { original: string; corrected: string } | null = null;

// === НОВЫЙ ИНТЕРФЕЙС для расширенного предиктора ===
export interface IExtendedPredictor extends IPredictor {
  findAutocorrection(lang: string, word: string): Promise<string | null>;
  recordWord(lang: string, prevWord: string, word: string, prev2Word?: string): void;
}

// === ИЗМЕНИТЬ handleSpace() ===
private async handleSpace(timestamp: number): Promise<void> {
  if (!this.client || this.isSwipeActive) return;

  const currentToken = this.textBuffer.getCurrentToken();
  
  if (currentToken.length > 0 && this.autocorrectionEnabled && this.predictor) {
    // Check autocorrection
    const extPredictor = this.predictor as IExtendedPredictor;
    if (extPredictor.findAutocorrection) {
      const correction = await extPredictor.findAutocorrection(
        this.currentLanguage, 
        currentToken
      );
      
      if (correction && correction !== currentToken) {
        // Delete typed word and insert correction
        this.client.deleteBackward(currentToken.length);
        this.client.insertText(correction);
        
        this.lastAutocorrection = { original: currentToken, corrected: correction };
        
        // Record corrected word
        if (extPredictor.recordWord) {
          extPredictor.recordWord(
            this.currentLanguage,
            this.textBuffer.getPrevWord(),
            correction,
            this.textBuffer.getPrev2Word()
          );
        }
        
        this.textBuffer.setCommittedWord(correction);
      } else {
        // No correction, record as typed
        if (extPredictor.recordWord) {
          extPredictor.recordWord(
            this.currentLanguage,
            this.textBuffer.getPrevWord(),
            currentToken,
            this.textBuffer.getPrev2Word()
          );
        }
        this.textBuffer.commitBoundary();
      }
    }
  } else {
    this.textBuffer.commitBoundary();
  }

  // Insert space
  this.client.insertText(' ');

  // Clear suggestions
  if (this.presenter) {
    this.presenter.clear();
  }

  this.predictionDebouncer.cancel();
}
```

#### 1.3 Расширить PredictorFacade

В `PredictorFacade.ets` добавить:

```typescript
import optimizedPredictionModel from '../OptimizedPredictionModel';

// Добавить методы:

async findAutocorrection(lang: string, word: string): Promise<string | null> {
  try {
    // Use optimized model if available
    const hasOptimized = optimizedPredictionModel.getOptimizedTrieDictionary(lang) ||
                         optimizedPredictionModel.getOptimizedHashMapDictionary(lang);
    
    if (hasOptimized) {
      return await optimizedPredictionModel.findAutocorrection(lang, word);
    }
    return await this.predictionModel.findAutocorrection(lang, word);
  } catch {
    return null;
  }
}

recordWord(lang: string, prevWord: string, word: string, prev2Word?: string): void {
  try {
    const hasOptimized = optimizedPredictionModel.getOptimizedTrieDictionary(lang) ||
                         optimizedPredictionModel.getOptimizedHashMapDictionary(lang);
    
    if (hasOptimized) {
      optimizedPredictionModel.recordWord(lang, prevWord, word, prev2Word);
    } else {
      this.predictionModel.recordWord(lang, prevWord, word, prev2Word);
    }
  } catch {
    // Silent fail
  }
}
```

### ЭТАП 2: Инициализация в InputMethodService.ets

```typescript
import { InputPipeline } from './model/input/InputPipeline';
import { HarmonyTextClientAdapter } from './model/input/HarmonyTextClientAdapter';
import { PredictorFacade } from './model/input/PredictorFacade';
import { SuggestionPresenterAdapter } from './model/input/SuggestionPresenterAdapter';
import { swipeEngine } from './model/SwipeEngine';

// Глобальные экземпляры для доступа из Index.ets
export const inputPipeline = new InputPipeline();
export const suggestionPresenter = new SuggestionPresenterAdapter();
export const harmonyAdapter = new HarmonyTextClientAdapter();

export default class InputDemoService extends InputMethodExtensionAbility {
  onCreate(want: Want): void {
    keyboardController.onCreate(this.context);

    // ... существующая инициализация predictionModel ...

    // Инициализация InputPipeline после загрузки моделей
    predictionModel.load().then(() => {
      return predictionModel.warmup('ru');
    }).then(() => {
      // Создать PredictorFacade
      const predictorFacade = new PredictorFacade(predictionModel, swipeEngine);
      
      // Настроить InputPipeline
      inputPipeline.setPredictor(predictorFacade);
      inputPipeline.setSuggestionPresenter(suggestionPresenter);
      inputPipeline.setLanguage('ru');
      
      console.info('OpenBoard: InputPipeline initialized');
    }).catch((err: Error) => {
      console.error('OpenBoard: Failed to initialize InputPipeline', err);
    });
  }
}
```

### ЭТАП 3: Интеграция в Index.ets

#### 3.1 Импорты и инициализация

```typescript
// ДОБАВИТЬ импорты:
import { inputPipeline, suggestionPresenter, harmonyAdapter } from '../InputMethodService';
import { InputEvent, KeyTapEvent, BackspaceEvent, SpaceEvent, EnterEvent } from '../model/input/InputTypes';

// В aboutToAppear():
aboutToAppear(): void {
  // ... существующий код ...
  
  // Подключить SuggestionPresenter к UI
  suggestionPresenter.setUpdateCallback((suggestions: string[]) => {
    this.suggestions = suggestions;
  });
}
```

#### 3.2 Заменить handleKeyPress()

```typescript
// БЫЛО (прямые вызовы):
async handleKeyPress(key: KeyData): Promise<void> {
  // ... ~200 строк кода с прямыми вызовами keyboardController ...
}

// СТАЛО (через InputPipeline):
async handleKeyPress(key: KeyData): Promise<void> {
  if (this.inputMode === InputMode.Swipe || this.isSwipeActive) return;

  const now = Date.now();
  if (now < this.ignoreKeysUntilTs && key.action !== KeyAction.Char && key.action !== KeyAction.Space) {
    return;
  }

  if (key.action !== KeyAction.EMOJI_PANEL) {
    this.resetInactivityTimer();
  }

  if (this.isRecording) {
    this.handleVoiceInterruption('manual');
  }

  // Emoji panel toggle - остается в UI
  if (key.action === KeyAction.EMOJI_PANEL) {
    this.showEmojiPanel = !this.showEmojiPanel;
    return;
  }

  switch (key.action) {
    case KeyAction.Char:
      feedbackController.provideFeedback(FeedbackType.Light);
      const wasShiftOn = (this.shiftState === ShiftState.On);
      let text = (this.currentLanguage !== 'ar' && this.currentLanguage !== 'fa' && 
                  (this.shiftState === ShiftState.On || this.shiftState === ShiftState.CapsLock))
        ? key.shiftValue ?? key.mainValue
        : key.mainValue;
      
      // Отправить событие в Pipeline
      const tapEvent: KeyTapEvent = { kind: 'KeyTap', char: text, timestamp: now };
      inputPipeline.onEvent(tapEvent);
      
      if (wasShiftOn) {
        this.shiftState = ShiftState.Off;
      }
      break;

    case KeyAction.Space:
      feedbackController.provideFeedback(FeedbackType.Light);
      const spaceEvent: SpaceEvent = { kind: 'Space', timestamp: now };
      inputPipeline.onEvent(spaceEvent);
      
      // Auto-return to alphabet layout
      if (this.keyboardLayout === KeyboardLayout.Symbols1 || this.keyboardLayout === KeyboardLayout.Symbols2) {
        this.keyboardLayout = KeyboardLayout.Alphabet;
      }
      break;

    case KeyAction.Enter:
      feedbackController.provideFeedback(FeedbackType.Medium);
      const enterEvent: EnterEvent = { kind: 'Enter', timestamp: now };
      inputPipeline.onEvent(enterEvent);
      break;

    case KeyAction.Backspace:
      feedbackController.provideFeedback(FeedbackType.Heavy);
      const backspaceEvent: BackspaceEvent = { kind: 'Backspace', timestamp: now };
      inputPipeline.onEvent(backspaceEvent);
      break;

    case KeyAction.Shift:
      feedbackController.provideFeedback(FeedbackType.Medium);
      // Shift logic остается в UI (не влияет на text input)
      if (this.shiftState === ShiftState.CapsLock) {
        this.shiftState = ShiftState.Off;
        this.lastShiftTapTs = now;
        break;
      }
      if (now - this.lastShiftTapTs < 400) {
        this.shiftState = ShiftState.CapsLock;
        this.lastShiftTapTs = 0;
        break;
      }
      this.shiftState = (this.shiftState === ShiftState.Off) ? ShiftState.On : ShiftState.Off;
      this.lastShiftTapTs = now;
      break;

    case KeyAction.SwitchAlphabet:
    case KeyAction.SwitchSymbols:
      feedbackController.provideFeedback(FeedbackType.Medium);
      const wasAlphabet = this.keyboardLayout === KeyboardLayout.Alphabet;
      this.keyboardLayout = wasAlphabet ? KeyboardLayout.Symbols1 : KeyboardLayout.Alphabet;
      if (!wasAlphabet) {
        this.currentLayoutId = `${this.currentLanguage}_alphabet`;
      }
      break;

    case KeyAction.SwitchSymbolsAlt:
      feedbackController.provideFeedback(FeedbackType.Medium);
      this.keyboardLayout = KeyboardLayout.Symbols2;
      break;
  }
}
```

#### 3.3 УДАЛИТЬ дублирующий код

Удалить из Index.ets:
- `private previousWord: string = '';`
- `private prevPrevWord: string = '';`
- `private currentWord: string = '';`
- `private suggestionsDebounceTimer: number | null = null;`
- Метод `debouncedUpdateSuggestions()`
- Метод `updateSuggestions()` 
- Метод `onTextTyped()` (логика перенесена в InputPipeline)
- Метод `onBoundaryTyped()` (логика перенесена в InputPipeline)
- Метод `onBackspaceTyped()` (логика перенесена в InputPipeline)

#### 3.4 pickSuggestion() - оставить но упростить

```typescript
private async pickSuggestion(word: string): Promise<void> {
  this.resetInactivityTimer();
  
  // Получить текущий токен из pipeline
  const state = inputPipeline.getState();
  const currentToken = state.currentToken;
  
  // Delete typed word and insert suggestion + space
  // Используем прямой доступ к HarmonyAdapter для быстрого UI отклика
  harmonyAdapter.deleteBackward(currentToken.length);
  harmonyAdapter.insertText(word + ' ');
  
  // Сообщить pipeline о коммите слова
  // TODO: Добавить метод inputPipeline.commitWord(word) для синхронизации TextBuffer
}
```

### ЭТАП 4: Swipe typing интеграция (ОПЦИОНАЛЬНО - сложно)

Swipe typing в Index.ets (~500 строк) сложная и работающая логика. Рекомендую:

**Вариант A (минимальные изменения):** Оставить swipe логику в Index.ets, но использовать InputPipeline для:
- Коммита результата: `inputPipeline.commitSwipeWord(word)`
- Обновления контекста: TextBuffer.setCommittedWord()

**Вариант B (полная миграция):** Перенести всю swipe логику в InputPipeline:
- Добавить SwipeStartEvent, SwipeMoveEvent, SwipeEndEvent handlers
- Перенести swipeKeySeq, swipePath, trail rendering
- Интегрировать с PredictorFacade.decodeSwipe()

Рекомендация: **Вариант A** - меньше риска сломать рабочий код.

### ЭТАП 5: Тестирование

#### Чеклист функциональности:
- [ ] Обычный ввод символов работает
- [ ] Backspace удаляет символы
- [ ] Space разделяет слова и триггерит autocorrection
- [ ] Enter работает (Send/Search/Done)
- [ ] Predictions появляются после 2+ символов
- [ ] Predictions обновляются при вводе
- [ ] Выбор suggestion вставляет слово
- [ ] Autocorrection заменяет слово на space
- [ ] Shift/CapsLock работает
- [ ] Swipe typing работает (если интегрирован)
- [ ] Переключение языков работает
- [ ] Emoji panel работает
- [ ] Clipboard panel работает

#### Чеклист производительности:
- [ ] Нет задержки при вводе (<16ms per keystroke)
- [ ] Predictions появляются за <100ms после ввода
- [ ] Нет утечек памяти при длительном использовании
- [ ] LRU cache работает (проверить getCacheStats())

## КРИТИЧЕСКИЕ ТОЧКИ

### 1. HarmonyOS API семантика
```typescript
// ВНИМАНИЕ: HarmonyOS API имеет обратную семантику!
// deleteForward() - удаляет ПЕРЕД курсором (как обычный Backspace)
// deleteBackward() - удаляет ПОСЛЕ курсора (как Delete)
// В HarmonyTextClientAdapter.ets уже исправлено:
deleteBackward(count) { this.client.deleteForward(count); }
```

### 2. Синхронизация состояния
TextBuffer в InputPipeline и KeyboardController.textBufferCache должны быть синхронизированы. При прямых вызовах harmonyAdapter (например в pickSuggestion) нужно также обновлять TextBuffer.

### 3. Race conditions
`suggestionsReqId` защищает от race conditions в async predictions. Эта логика уже есть в InputPipeline.isProcessing flag.

### 4. Error rate limiting
В Index.ets есть error rate limiting для insertText errors (300ms window, 3 error threshold). Нужно перенести в InputPipeline или HarmonyTextClientAdapter.

## ФАЙЛЫ ДЛЯ ИЗМЕНЕНИЯ

### Создать:
- `model/input/SuggestionPresenterAdapter.ets` - новый файл

### Изменить:
- `model/input/InputPipeline.ets` - добавить autocorrection, recordWord
- `model/input/PredictorFacade.ets` - добавить findAutocorrection, recordWord
- `model/input/InputTypes.ets` - добавить IExtendedPredictor (опционально)
- `InputMethodService.ets` - инициализация pipeline
- `pages/Index.ets` - интеграция с pipeline, удаление дублей

### Не трогать:
- `model/input/TextBuffer.ets` - готов
- `model/input/Debouncer.ets` - готов
- `model/input/HarmonyTextClientAdapter.ets` - готов (уже с fix для deleteBackward)
- `model/KeyboardController.ets` - продолжит работать параллельно

## ОЦЕНКА ТРУДОЗАТРАТ

| Этап | Время | Риск |
|------|-------|------|
| Создать SuggestionPresenterAdapter | 15 мин | Низкий |
| Расширить InputPipeline | 45 мин | Средний |
| Расширить PredictorFacade | 30 мин | Низкий |
| Инициализация в InputMethodService | 20 мин | Низкий |
| Интеграция в Index.ets | 60 мин | Высокий |
| Удаление дублей из Index.ets | 30 мин | Средний |
| Тестирование | 60 мин | - |
| **ИТОГО** | **~4 часа** | **Средний** |

## АЛЬТЕРНАТИВА: Quick Fix без миграции

Если миграция слишком рискованна, можно получить часть преимуществ без переписывания:

1. **Добавить LRU кэш в существующий updateSuggestions():**
```typescript
private predictionCache = new LRUCache<string, string[]>(500);

private async updateSuggestions(): Promise<void> {
  const cacheKey = `${this.currentLanguage}:${this.previousWord}:${this.currentWord}`;
  const cached = this.predictionCache.get(cacheKey);
  if (cached) {
    this.suggestions = cached;
    return;
  }
  // ... existing prediction logic ...
  this.predictionCache.set(cacheKey, this.suggestions);
}
```

2. **Вынести Debouncer как отдельный utility:**
```typescript
import { Debouncer } from './model/input/Debouncer';
private predictionDebouncer = new Debouncer(150);

// Заменить setTimeout на:
this.predictionDebouncer.schedule(() => this.updateSuggestions());
```

Это даст 60-80% hit rate на predictions без риска сломать рабочий код.
