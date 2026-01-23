## Summary

---




---





```text
class PredictionConfig {
  static readonly LEARN_THRESHOLD = 3;


  // Anti-garbage filters
  static readonly MIN_WORD_LENGTH = 1;

}
```


---



```text
// 2. Add 'en' to baseLexiconLanguages array below
```

```text
hasBaseLexicon(lang: string): boolean
getBaseLexicon(lang: string): BaseLexicon | null
```


---


```text
} else {
  // Still pending, increment count
  m.pendingWords.set(w, newPendingCount);

  this.updateBigramTrigram(m, w, prev, prev2, now);
  m.lastUsed.set(w, now);
}
```

```text
}
  }
}
```


---


```text
  if (this.isWordLearned(m, wordLower) || this.isBaseWord(lang, wordLower)) return null;
```

```text
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

