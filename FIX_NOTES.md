## Summary

---




---





```typescript
class PredictionConfig {
  static readonly LEARN_THRESHOLD = 3;


  // Anti-garbage filters
  static readonly MIN_WORD_LENGTH = 1;

}
```


---



```typescript
// 2. Add 'en' to baseLexiconLanguages array below
```

```typescript
hasBaseLexicon(lang: string): boolean
getBaseLexicon(lang: string): BaseLexicon | null
```


---


```typescript
} else {
  // Still pending, increment count
  m.pendingWords.set(w, newPendingCount);

  this.updateBigramTrigram(m, w, prev, prev2, now);
  m.lastUsed.set(w, now);
}


```typescript
```


```typescript
  }
  }

}
```

```typescript
}
```


---


```typescript
  if (this.isWordLearned(m, wordLower) || this.isBaseWord(lang, wordLower)) return null;
```

```typescript
  await this.ensureBaseLexiconLoaded(lang);
  const baseLexicon = this.getBaseLexicon(lang);

  if (this.isWordLearned(m, wordLower) || this.isBaseWord(lang, wordLower)) return null;
```


---






---







```bash
```

```


---

