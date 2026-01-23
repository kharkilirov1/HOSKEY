# InputPipeline Integration Guide

## 🎯 Status: Infrastructure Ready, Integration Pending

**Ready for use:**
- ✅ InputPipeline skeleton with fast/slow path architecture
- ✅ TextBuffer for state management
- ✅ HarmonyTextClientAdapter (zero-overhead wrapper)
- ✅ Debouncer for prediction requests (30ms)
- ✅ PredictorFacade with LRU cache (500 entries)
- ✅ Type-safe event system (InputEvent union types)

**Not yet integrated:**
- ❌ Index.ets UI event routing
- ❌ KeyboardController lifecycle hooks
- ❌ Swipe decoding in PredictorFacade
- ❌ Russian morphology optimization
- ❌ Performance logging

---

## 📐 Architecture Overview

```
┌───────────────────────────────────────────────────────────┐
│                      UI Layer (Index.ets)                  │
│  KeyView → onTouch → handleKeyPress(KeyData)              │
└────────────────────────┬──────────────────────────────────┘
                         │
                         │ InputEvent
                         ▼
┌───────────────────────────────────────────────────────────┐
│                    InputPipeline                           │
│                                                            │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐   │
│  │  FAST PATH   │  │  SLOW PATH   │  │  SWIPE PATH  │   │
│  │  Tap→insert  │  │  Debouncer   │  │  TraceBuffer │   │
│  │  <1ms        │  │  →Predictor  │  │  →Decode     │   │
│  └──────────────┘  └──────────────┘  └──────────────┘   │
│                                                            │
│  Components:                                               │
│  - TextBuffer: state (currentToken, prevWord)             │
│  - Debouncer: 30ms delay for predictions                  │
│  - Mode: Tap | Swipe                                       │
└────────┬───────────────────┬───────────────────┬──────────┘
         │                   │                   │
         ▼                   ▼                   ▼
   ┌─────────────┐   ┌──────────────┐   ┌──────────────┐
   │TextClient   │   │ Predictor    │   │ Suggestion   │
   │ Adapter     │   │ Facade       │   │ Presenter    │
   │(insertText) │   │(async+cache) │   │(show/clear)  │
   └─────────────┘   └──────────────┘   └──────────────┘
```

---

## 🚀 Integration Steps (TODO)

### Step 1: Add InputPipeline to Index.ets

```typescript
// Add imports
import { InputPipeline, InputEvent, ISuggestionPresenter } from '../model/input';
import { PredictorFacade } from '../model/input/PredictorFacade';
import { HarmonyTextClientAdapter } from '../model/input/HarmonyTextClientAdapter';

// In Index struct, add fields:
struct Index {
  // ... existing fields ...

  // NEW: InputPipeline infrastructure
  private inputPipeline: InputPipeline = new InputPipeline();
  private predictorFacade: PredictorFacade | null = null;
  private textClientAdapter: HarmonyTextClientAdapter = new HarmonyTextClientAdapter();

  // ...
}
```

### Step 2: Initialize in `aboutToAppear()`

```typescript
aboutToAppear(): void {
  console.info('OpenBoard Keyboard: UI aboutToAppear called');

  // Existing code...
  keyboardController.setEnterKeyTypeCallback((enterKeyType: number) => {
    this.updateEnterKeyLabel(enterKeyType);
  });

  // NEW: Initialize InputPipeline
  this.predictorFacade = new PredictorFacade(predictionModel, swipeEngine);
  this.inputPipeline.setPredictor(this.predictorFacade);

  // Create SuggestionPresenter that updates @State suggestions
  const presenter: ISuggestionPresenter = {
    show: (list: string[]) => {
      this.suggestions = [...list, '', '', ''].slice(0, 3);
    },
    clear: () => {
      this.suggestions = ['', '', ''];
    }
  };
  this.inputPipeline.setSuggestionPresenter(presenter);
}
```

### Step 3: Connect lifecycle in `onPageShow()`

```typescript
onPageShow(): void {
  console.info('OpenBoard Keyboard: UI onPageShow called');

  // Existing code...
  this.availableLanguages = ALLOWED_LANGUAGES.map((c: string) => c as LanguageCode);
  // ...

  // NEW: Set language in pipeline
  this.inputPipeline.setLanguage(this.currentLanguage);

  // Warmup BaseLexicon...
  predictionModel.warmup(this.currentLanguage).then(() => {
    const baseLexicon = predictionModel.getBaseLexicon(this.currentLanguage);
    swipeEngine.setBaseLexicon(baseLexicon);
  });
}
```

### Step 4: Add TextClient connection in KeyboardController

In `KeyboardController.ets`, modify `inputStartCallback`:

```typescript
private inputStartCallback = async (
  _: inputMethodEngine.KeyboardController,
  textInputClient: inputMethodEngine.InputClient
): Promise<void> => {
  this.textInputClient = textInputClient;

  // NEW: Pass client to Index.ets via callback
  if (this.inputClientCallback) {
    this.inputClientCallback(textInputClient);
  }

  this.updateEnterKeyType();
  await this.syncInitialBuffer();
  this.hideIfDestroyedAndRecreate();
  this.showKeyboard();
};

// Add callback setter
public setInputClientCallback(callback: (client: inputMethodEngine.InputClient) => void): void {
  this.inputClientCallback = callback;
}
```

Then in Index.ets `aboutToAppear()`:

```typescript
keyboardController.setInputClientCallback((client) => {
  this.textClientAdapter.setClient(client);
  this.inputPipeline.setClient(this.textClientAdapter);
});
```

### Step 5: Route KeyPress events to InputPipeline

In `handleKeyPress()`:

```typescript
async handleKeyPress(key: KeyData): Promise<void> {
  // ... existing code for emoji panel, voice, etc. ...

  switch (key.action) {
    case KeyAction.Char:
      feedbackController.provideFeedback(FeedbackType.Light);

      // NEW: Route to InputPipeline
      const event: InputEvent = {
        kind: 'KeyTap',
        char: key.mainValue, // TODO: handle shift state
        timestamp: Date.now()
      };
      this.inputPipeline.onEvent(event);

      // OLD CODE: Remove or comment out
      // keyboardController.insertText(text);
      // await this.onTextTyped(text);

      break;

    case KeyAction.Space:
      feedbackController.provideFeedback(FeedbackType.Light);

      // NEW: Route to InputPipeline
      this.inputPipeline.onEvent({ kind: 'Space', timestamp: Date.now() });
      break;

    case KeyAction.Backspace:
      feedbackController.provideFeedback(FeedbackType.Heavy);

      // NEW: Route to InputPipeline
      this.inputPipeline.onEvent({ kind: 'Backspace', timestamp: Date.now() });
      break;

    case KeyAction.Enter:
      feedbackController.provideFeedback(FeedbackType.Medium);

      // NEW: Route to InputPipeline
      this.inputPipeline.onEvent({ kind: 'Enter', timestamp: Date.now() });
      break;

    // ... other cases remain unchanged ...
  }
}
```

### Step 6: Remove old prediction logic

After InputPipeline integration works, remove:
- `updateSuggestions()` method (replaced by InputPipeline debouncer)
- `suggestionsDebounceTimer` field
- Direct `predictionModel.predict()` calls
- `currentWord`, `previousWord`, `prevPrevWord` tracking (replaced by TextBuffer)

---

## 🎬 Swipe Integration (Phase 2)

### Add SwipeTraceBuffer

Create `model/input/SwipeTraceBuffer.ets`:

```typescript
export class SwipeTraceBuffer {
  private points: SwipePoint[] = [];
  private lastAddedTime: number = 0;

  reset(): void {
    this.points = [];
    this.lastAddedTime = 0;
  }

  addPoint(x: number, y: number, timestamp: number): void {
    // Skip duplicates (< 8ms apart or same position)
    if (this.points.length > 0) {
      const last = this.points[this.points.length - 1];
      const timeDelta = timestamp - this.lastAddedTime;
      const distance = Math.sqrt((x - last.x) ** 2 + (y - last.y) ** 2);

      if (timeDelta < 8 || distance < 2) {
        return; // Skip
      }
    }

    this.points.push({ x, y, timestamp });
    this.lastAddedTime = timestamp;
  }

  getPoints(): SwipePoint[] {
    return this.points;
  }

  toKeyPath(keyBoundsMap: Map<string, KeyBounds>): string[] {
    const keyPath: string[] = [];
    const keyBoundsArray = Array.from(keyBoundsMap.values());

    for (const point of this.points) {
      const nearestKey = this.findNearestKey(point, keyBoundsArray);
      if (nearestKey && (keyPath.length === 0 || keyPath[keyPath.length - 1] !== nearestKey)) {
        keyPath.push(nearestKey);
      }
    }

    return keyPath;
  }

  private findNearestKey(point: SwipePoint, keys: KeyBounds[]): string | undefined {
    let minDist = Infinity;
    let nearest: string | undefined;

    for (const keyBounds of keys) {
      const dx = point.x - keyBounds.centerX;
      const dy = point.y - keyBounds.centerY;
      const dist = Math.sqrt(dx * dx + dy * dy);

      // Hit test with 1.5x radius
      const maxDist = Math.max(keyBounds.width, keyBounds.height) * 0.75;
      if (dist < maxDist && dist < minDist) {
        minDist = dist;
        nearest = keyBounds.key;
      }
    }

    return nearest;
  }
}
```

### Update InputPipeline with SwipeTraceBuffer

```typescript
// In InputPipeline.ets
import { SwipeTraceBuffer } from './SwipeTraceBuffer';

export class InputPipeline {
  // ... existing fields ...
  private swipeBuffer: SwipeTraceBuffer = new SwipeTraceBuffer();
  private keyBoundsMap: Map<string, KeyBounds> = new Map();

  setKeyBoundsMap(map: Map<string, KeyBounds>): void {
    this.keyBoundsMap = map;
  }

  private handleSwipeStart(x: number, y: number, timestamp: number): void {
    this.isSwipeActive = true;
    this.swipeStartTime = timestamp;
    this.currentMode = InputMode.Swipe;
    this.swipeBuffer.reset();
    this.swipeBuffer.addPoint(x, y, timestamp);
  }

  private handleSwipeMove(x: number, y: number, timestamp: number): void {
    if (!this.isSwipeActive) return;
    this.swipeBuffer.addPoint(x, y, timestamp);
  }

  private async handleSwipeEnd(x: number, y: number, timestamp: number): Promise<void> {
    if (!this.isSwipeActive) return;

    this.isSwipeActive = false;
    this.currentMode = InputMode.Tap;
    this.swipeBuffer.addPoint(x, y, timestamp);

    // Validate swipe
    const duration = timestamp - this.swipeStartTime;
    const points = this.swipeBuffer.getPoints();

    if (duration < 200 || points.length < 5) {
      return; // Too short
    }

    // Decode swipe
    const keyPath = this.swipeBuffer.toKeyPath(this.keyBoundsMap);
    if (keyPath.length < 2 || !this.predictor || !this.client) {
      return;
    }

    try {
      const prevWord = this.textBuffer.getPrevWord();
      const candidates = await this.predictor.decodeSwipe(this.currentLanguage, keyPath, prevWord);

      if (candidates.length > 0) {
        const bestWord = candidates[0];
        this.client.insertText(bestWord + ' ');
        this.textBuffer.setCommittedWord(bestWord);
        this.textBuffer.commitBoundary();

        // Show alternatives as suggestions
        if (this.presenter && candidates.length > 1) {
          this.presenter.show(candidates.slice(1, 4));
        }
      }
    } catch (err) {
      console.error('[InputPipeline] Swipe decode error:', err);
    }
  }
}
```

### Implement decodeSwipe in PredictorFacade

```typescript
async decodeSwipe(lang: string, keyPath: string[], prevWord: string): Promise<string[]> {
  return new Promise<string[]>((resolve) => {
    try {
      // 1. Get BaseLexicon
      const baseLexicon = this.predictionModel.getBaseLexicon(lang);
      if (!baseLexicon || !baseLexicon.isLoaded()) {
        resolve([]);
        return;
      }

      // 2. Deduplicate keyPath: AABBCC → ABC
      const dedupedPath = this.deduplicatePath(keyPath);
      if (dedupedPath.length < 2) {
        resolve([]);
        return;
      }

      // 3. Get candidates by prefix (first 3-4 chars)
      const prefixLength = Math.min(dedupedPath.length, 4);
      const prefix = dedupedPath.slice(0, prefixLength).join('');
      const words = baseLexicon.findByPrefix(prefix, 100);

      // 4. Filter by subsequence match
      const candidates: Array<{ word: string; score: number }> = [];
      for (const entry of words) {
        if (this.isSubsequenceMatch(dedupedPath, entry.word)) {
          let score = entry.freq; // Base frequency score

          // Bigram boost
          if (prevWord && prevWord.length > 0) {
            const bigramScore = this.predictionModel.getBigramScore(lang, prevWord, entry.word);
            score += bigramScore * 100;
          }

          candidates.push({ word: entry.word, score });
        }
      }

      // 5. Sort by score descending
      candidates.sort((a, b) => b.score - a.score);

      // 6. Return top 10
      resolve(candidates.slice(0, 10).map(c => c.word));
    } catch (err) {
      console.error('[PredictorFacade] decodeSwipe error:', err);
      resolve([]);
    }
  });
}

private deduplicatePath(path: string[]): string[] {
  const result: string[] = [];
  for (const key of path) {
    if (result.length === 0 || result[result.length - 1] !== key) {
      result.push(key);
    }
  }
  return result;
}

private isSubsequenceMatch(keyPath: string[], word: string): boolean {
  let keyIndex = 0;
  for (const char of word) {
    if (keyIndex < keyPath.length && char === keyPath[keyIndex]) {
      keyIndex++;
    }
  }
  // Match if at least 60% of keys are in word
  return keyIndex >= keyPath.length * 0.6;
}
```

---

## 🚫 Russian Morphology Optimization (Phase 3)

### Problem
Current morphology generation can create too many forms and hurt relevance.

### Solution: Limit + Cache

In `PredictionModel.ets`:

```typescript
// Add morphology cache
private morphCache: LRUCache<string, string[]> = new LRUCache(1000);

// Modify predict() to use cache
private expandWithMorphology(token: string, lang: string, maxForms: number = 20): string[] {
  if (lang !== 'ru' || token.length < 3) {
    return [];
  }

  // Check cache
  const cacheKey = `${lang}:${token}`;
  const cached = this.morphCache.get(cacheKey);
  if (cached !== undefined) {
    return cached;
  }

  // Generate forms
  const lemmas = this.findLemmasForPrefix(token);
  const forms: Set<string> = new Set();

  for (const lemma of lemmas.slice(0, 5)) { // Limit to 5 lemmas
    const verbForms = generateRussianVerbForms(lemma);
    for (const form of verbForms) {
      if (form.startsWith(token)) {
        forms.add(form);
        if (forms.size >= maxForms) break;
      }
    }
    if (forms.size >= maxForms) break;
  }

  const result = Array.from(forms);
  this.morphCache.set(cacheKey, result);
  return result;
}
```

---

## 📊 Performance Logging (Phase 4)

Add to `InputPipeline.ets`:

```typescript
// Enable with flag
const PERF_LOG_ENABLED = false;

interface PerfStats {
  predictCalls: number;
  predictTotalMs: number;
  swipeCalls: number;
  swipeTotalMs: number;
  cacheHitRate: number;
}

private perfStats: PerfStats = {
  predictCalls: 0,
  predictTotalMs: 0,
  swipeCalls: 0,
  swipeTotalMs: 0,
  cacheHitRate: 0
};

private async predictNow(reason: string): Promise<void> {
  // ... existing code ...

  const startTime = Date.now();
  try {
    const suggestions = await this.predictor.predict(...);
    // ...

    if (PERF_LOG_ENABLED) {
      const duration = Date.now() - startTime;
      this.perfStats.predictCalls++;
      this.perfStats.predictTotalMs += duration;
      console.info(`[InputPipeline] predict(${reason}): ${duration}ms`);
    }
  } catch (err) {
    // ...
  }
}

public getPerfStats(): PerfStats {
  const avgPredict = this.perfStats.predictCalls > 0
    ? this.perfStats.predictTotalMs / this.perfStats.predictCalls
    : 0;

  return {
    ...this.perfStats,
    avgPredictMs: avgPredict
  };
}
```

---

## ✅ Acceptance Criteria

**Before deploying:**
1. ✅ Tap typing: character appears instantly (< 1 frame)
2. ✅ Suggestions: update smoothly without lag
3. ✅ No race conditions: debouncer prevents multiple concurrent predicts
4. ✅ Swipe: word inserted only on SwipeEnd
5. ✅ Lifecycle: no memory leaks (off() called in onDestroy)
6. ✅ Performance: predict() < 20ms, decodeSwipe() < 60ms
7. ✅ Cache: 60-80% hit rate

**Testing:**
- Test on real HarmonyOS device (emulator may not show performance issues)
- Type quickly: "привет как дела" - should be instant
- Swipe "привет" - should recognize correctly
- Switch languages - predictions should update
- Background/foreground app - no crashes

---

## 📝 Summary

**What's ready:**
- Complete InputPipeline infrastructure in `model/input/`
- Type-safe event system
- Debouncing + caching
- Fast path for tap input (zero overhead)

**What's needed:**
- Integration in Index.ets (~100 lines of changes)
- KeyboardController lifecycle hooks (~20 lines)
- SwipeTraceBuffer implementation (~80 lines)
- PredictorFacade.decodeSwipe() (~60 lines)
- Testing and validation

**Total effort:** ~4-6 hours for experienced HarmonyOS developer

**Risk:** Low - can be integrated incrementally with feature flag
