import { preferences } from '@kit.ArkData';
import { common } from '@kit.AbilityKit';
import { BusinessError } from '@kit.BasicServicesKit';
import { resourceManager } from '@kit.LocalizationKit';
import { util } from '@kit.ArkTS';
import { BaseLexicon } from './BaseLexicon';

// Base dictionary (read-only, loaded from rawfile) - DEPRECATED, use BaseLexicon instead
interface BaseDict {
  rank: Map<string, number>; // Word -> base rank (frequency in dictionary)
}

// User language model (learned, mutable)
interface LangModel {
  uni: Map<string, number>; // Unigram: word -> count
  bi: Map<string, Map<string, number>>; // Bigram: prev_word -> (next_word -> count)
  tri: Map<string, Map<string, number>>; // Trigram: (prev1_prev2) -> (next_word -> count)
  wordRank: Map<string, number>; // Word -> total usage count (rank)
  lastUsed: Map<string, number>; // Word -> timestamp of last use
  pendingWords: Map<string, number>; // Word -> occurrence count (needs 3 to learn)
  blockedWords: Set<string>; // Words that should never be learned
}

interface RankedWord {
  w: string;
  c: number;
  score: number; // Combined score for ranking
}

interface SerializedModel {
  uni: [string, number][];
  bi: [string, [string, number][]][];
  tri: [string, [string, number][]][];
  wordRank: [string, number][];
  lastUsed: [string, number][];
  pendingWords: [string, number][]; // Include pending words in serialization
  blockedWords: string[]; // Include blocked words in serialization
}

class PredictionModel {
  private models: Map<string, LangModel>; // User-learned models (mutable)
  private baseDicts: Map<string, BaseDict>; // Base dictionaries (read-only, from rawfile) - DEPRECATED
  private baseLexicons: Map<string, BaseLexicon>; // Base lexicons (50k words with binary search)
  private context: common.Context | null = null;
  private saveCounter: number = 0;
  private readonly SAVE_THRESHOLD = 10; // Save after every 10 words learned
  private readonly DECAY_FACTOR = 0.95; // Decay competing words by 5%
  private readonly MIN_COUNT_THRESHOLD = 0.5; // Remove words with count below this
  private readonly LEARN_THRESHOLD = 3; // Word must be typed 3 times to be learned

  // Model size limits to prevent unbounded memory growth
  private readonly MAX_WORDS_PER_LANGUAGE = 10000; // Maximum unique words per language
  private readonly MAX_BIGRAMS_PER_WORD = 100; // Maximum bigram entries per word
  private readonly MAX_TRIGRAMS_PER_KEY = 50; // Maximum trigram entries per key

  // Hybrid ranking weights - context dominates, user personalization matters, base lexicon is fallback
  private readonly SCORE_CONTEXT_WEIGHT = 100; // Bigram/trigram context (highest priority)
  private readonly SCORE_USER_RANK_WEIGHT = 5; // User personalization rank
  private readonly SCORE_RECENCY_WEIGHT = 1; // Recent usage bonus
  private readonly SCORE_BASE_FREQ_WEIGHT = 2; // Base lexicon frequency (fallback)

  // Save debounce
  private saveTimer: number | null = null;
  private readonly SAVE_DEBOUNCE_MS = 3000; // 3 seconds
  private isSaving: boolean = false;

  constructor() {
    this.models = new Map();
    this.baseDicts = new Map();
    this.baseLexicons = new Map();
  }

  setContext(ctx: common.Context): void {
    this.context = ctx;

    // Initialize base lexicons for supported languages (but don't load yet - lazy loading)
    const supportedLangs = ['ru', 'en'];
    supportedLangs.forEach(lang => {
      const lexicon = new BaseLexicon();
      lexicon.setContext(ctx);
      this.baseLexicons.set(lang, lexicon);
    });
  }

  /**
   * Lazy load BaseLexicon for language (only loads once when first needed)
   */
  private async ensureBaseLexiconLoaded(lang: string): Promise<void> {
    const lexicon = this.baseLexicons.get(lang);
    if (!lexicon) {
      console.warn(`PredictionModel: No BaseLexicon configured for language '${lang}'`);
      return;
    }

    if (!lexicon.isLoaded()) {
      console.info(`PredictionModel: Lazy-loading BaseLexicon for '${lang}'...`);
      await lexicon.load(lang);
      console.info(`PredictionModel: BaseLexicon for '${lang}' loaded, stats:`, lexicon.getStats());
    }
  }

  /**
   * Unload BaseLexicons for languages not in active list (memory management)
   */
  unloadUnusedLanguages(activeLangs: string[]): void {
    const activeSet = new Set(activeLangs);
    const toUnload: string[] = [];

    this.baseLexicons.forEach((lexicon, lang) => {
      if (!activeSet.has(lang) && lexicon.isLoaded()) {
        toUnload.push(lang);
      }
    });

    toUnload.forEach(lang => {
      console.info(`PredictionModel: Unloading BaseLexicon for unused language '${lang}'`);
      // Replace with fresh unloaded instance
      const lexicon = new BaseLexicon();
      if (this.context) {
        lexicon.setContext(this.context);
      }
      this.baseLexicons.set(lang, lexicon);
    });

    if (toUnload.length > 0) {
      console.info(`PredictionModel: Unloaded ${toUnload.length} unused language(s)`);
    }
  }

  // Get base dictionary for language (read-only) - DEPRECATED
  private getBaseDict(lang: string): BaseDict | null {
    return this.baseDicts.get(lang) ?? null;
  }

  // Get base lexicon for language
  private getBaseLexicon(lang: string): BaseLexicon | null {
    return this.baseLexicons.get(lang) ?? null;
  }

  // Check if word exists in base dictionary (checks both old baseDict and new baseLexicon)
  private isBaseWord(lang: string, word: string): boolean {
    // Check new BaseLexicon first
    const lexicon = this.getBaseLexicon(lang);
    if (lexicon && lexicon.isLoaded()) {
      return lexicon.contains(word);
    }

    // Fallback to old baseDict for compatibility
    const baseDict = this.getBaseDict(lang);
    return baseDict !== null && baseDict.rank.has(word);
  }

  // Ensure base dictionary exists for language
  private ensureBaseDict(lang: string): BaseDict {
    let base = this.baseDicts.get(lang);
    if (!base) {
      base = { rank: new Map<string, number>() };
      this.baseDicts.set(lang, base);
    }
    return base;
  }

  private ensure(lang: string): LangModel {
    let m: LangModel | undefined = this.models.get(lang);
    if (!m) {
      const lm: LangModel = {
        uni: new Map<string, number>(),
        bi: new Map<string, Map<string, number>>(),
        tri: new Map<string, Map<string, number>>(),
        wordRank: new Map<string, number>(),
        lastUsed: new Map<string, number>(),
        pendingWords: new Map<string, number>(),
        blockedWords: new Set<string>()
      };
      this.models.set(lang, lm);
      m = lm;
    }
    return m;
  }

  // Create a composite key for trigram lookups
  private triKey(prev1: string, prev2: string): string {
    return `${prev1}|${prev2}`;
  }

  // Update bigram and trigram context (helper method)
  private updateBigramTrigram(m: LangModel, word: string, prev: string | null, prev2: string | null, now: number): void {
    if (prev && prev.length > 0) {
      const prevLower = prev.toLowerCase();
      let nextMap = m.bi.get(prevLower);
      if (!nextMap) {
        nextMap = new Map<string, number>();
        m.bi.set(prevLower, nextMap);
      }
      const c = nextMap.get(word) ?? 0;
      nextMap.set(word, c + 1);

      // Apply decay to competing bigrams
      this.applyDecayAndCleanup(nextMap, word);

      // Remove empty bigram entries
      if (nextMap.size === 0) {
        m.bi.delete(prevLower);
      }

      // Update trigram if we have two previous words
      if (prev2 && prev2.length > 0) {
        const prev2Lower = prev2.toLowerCase();
        const triKeyStr = this.triKey(prev2Lower, prevLower);
        let triMap = m.tri.get(triKeyStr);
        if (!triMap) {
          triMap = new Map<string, number>();
          m.tri.set(triKeyStr, triMap);
        }
        const tc = triMap.get(word) ?? 0;
        triMap.set(word, tc + 1);

        // Apply decay to competing trigrams
        this.applyDecayAndCleanup(triMap, word);

        // Remove empty trigram entries
        if (triMap.size === 0) {
          m.tri.delete(triKeyStr);
        }
      }
    }
  }

  // Calculate Levenshtein distance for similarity comparison (optimized)
  private levenshteinDistance(a: string, b: string, maxDistance: number = 2): number {
    // Early exit: if strings are equal
    if (a === b) return 0;

    // Early exit: if length difference exceeds max distance
    const lengthDiff = Math.abs(a.length - b.length);
    if (lengthDiff > maxDistance) {
      return maxDistance + 1;
    }

    // Early exit: if either string is empty
    if (a.length === 0) return Math.min(b.length, maxDistance + 1);
    if (b.length === 0) return Math.min(a.length, maxDistance + 1);

    const matrix: number[][] = [];

    // Initialize first column
    for (let i = 0; i <= b.length; i++) {
      matrix[i] = [i];
    }

    // Initialize first row
    for (let j = 0; j <= a.length; j++) {
      matrix[0][j] = j;
    }

    // Fill matrix with early exit optimization
    for (let i = 1; i <= b.length; i++) {
      let minInRow = Number.MAX_VALUE;

      for (let j = 1; j <= a.length; j++) {
        if (b.charAt(i - 1) === a.charAt(j - 1)) {
          matrix[i][j] = matrix[i - 1][j - 1];
        } else {
          matrix[i][j] = Math.min(
            matrix[i - 1][j - 1] + 1, // substitution
            matrix[i][j - 1] + 1, // insertion
            matrix[i - 1][j] + 1 // deletion
          );
        }
        minInRow = Math.min(minInRow, matrix[i][j]);
      }

      // Early exit: if minimum in current row exceeds threshold, abort
      if (minInRow > maxDistance) {
        return maxDistance + 1;
      }
    }

    return matrix[b.length][a.length];
  }

  // Check if a word is truly learned (count >= LEARN_THRESHOLD)
  private isWordLearned(m: LangModel, word: string): boolean {
    const count = m.uni.get(word) ?? 0;
    return count >= this.LEARN_THRESHOLD;
  }

  // Find if there's a similar learned word (for typo detection) - checks user learned + base lexicon
  private findSimilarLearnedWord(m: LangModel, word: string, lang: string): string | null {
    const threshold = Math.min(2, Math.floor(word.length * 0.3)); // Max 2 char difference or 30% of word length

    let bestMatch: string | null = null;
    let bestDistance = threshold + 1;
    let bestRank = 0;

    // Check against user learned words (words in uni with count >= LEARN_THRESHOLD)
    m.uni.forEach((count, learnedWord) => {
      // Only consider words that are truly learned
      if (count >= this.LEARN_THRESHOLD && learnedWord !== word && Math.abs(learnedWord.length - word.length) <= threshold) {
        // Pass threshold to levenshteinDistance for early exit optimization
        const distance = this.levenshteinDistance(word, learnedWord, threshold);
        const rank = m.wordRank.get(learnedWord) ?? count;
        if (distance < bestDistance || (distance === bestDistance && rank > bestRank)) {
          bestDistance = distance;
          bestMatch = learnedWord;
          bestRank = rank;
        }
      }
    });

    // Check BaseLexicon (OPTIMIZED - limited pool, not full scan)
    const baseLexicon = this.getBaseLexicon(lang);
    if (baseLexicon && baseLexicon.isLoaded() && word.length > 0) {
      // Strategy: Check words starting with same first letter (typically 1000-3000 words)
      // Much faster than checking all 50k words
      const candidatePool = baseLexicon.getWordsByFirstLetter(word[0]);

      for (const baseWord of candidatePool) {
        if (baseWord === word || baseLexicon.isBlocked(baseWord) || m.blockedWords.has(baseWord)) continue;
        if (Math.abs(baseWord.length - word.length) > threshold) continue;

        const distance = this.levenshteinDistance(word, baseWord, threshold);
        const rank = baseLexicon.getFrequency(baseWord);

        if (distance < bestDistance || (distance === bestDistance && rank > bestRank)) {
          bestDistance = distance;
          bestMatch = baseWord;
          bestRank = rank;
        }
      }
    }

    return bestMatch;
  }

  // Apply decay to competing words in a map and remove weak entries
  private applyDecayAndCleanup(map: Map<string, number>, usedWord: string): void {
    const toDelete: string[] = [];

    map.forEach((count, word) => {
      if (word !== usedWord) {
        // Decay competing words
        const newCount = count * this.DECAY_FACTOR;

        if (newCount < this.MIN_COUNT_THRESHOLD) {
          // Mark for deletion if too weak
          toDelete.push(word);
        } else {
          map.set(word, newCount);
        }
      }
    });

    // Remove weak words
    toDelete.forEach(word => map.delete(word));
  }

  // Prune model to keep it within size limits
  private pruneModel(m: LangModel): void {
    console.info(`OpenBoard: Model size limit reached (${m.uni.size} words), pruning...`);

    // Remove bottom 10% of least frequently used words
    const wordsToRemove = Math.floor(m.uni.size * 0.1);

    // Sort words by rank (usage count)
    const sortedWords = Array.from(m.wordRank.entries())
      .sort((a, b) => a[1] - b[1]) // Sort ascending by count
      .slice(0, wordsToRemove); // Take bottom 10%

    // Remove these words from all data structures
    sortedWords.forEach((entry) => {
      const word = entry[0];
      m.uni.delete(word);
      m.wordRank.delete(word);
      m.lastUsed.delete(word);

      // Remove from bigrams where this word appears as next word
      m.bi.forEach((nextMap) => {
        nextMap.delete(word);
      });

      // Remove from trigrams where this word appears as next word
      m.tri.forEach((nextMap) => {
        nextMap.delete(word);
      });

      // Also remove if it's a bigram key with too many entries
      const biMap = m.bi.get(word);
      if (biMap && biMap.size > this.MAX_BIGRAMS_PER_WORD) {
        // Keep only top entries
        const sortedBigrams = Array.from(biMap.entries())
          .sort((a, b) => b[1] - a[1])
          .slice(0, this.MAX_BIGRAMS_PER_WORD);
        m.bi.set(word, new Map(sortedBigrams));
      }
    });

    // Clean up trigrams
    m.tri.forEach((triMap, key) => {
      if (triMap.size > this.MAX_TRIGRAMS_PER_KEY) {
        const sortedTrigrams = Array.from(triMap.entries())
          .sort((a, b) => b[1] - a[1])
          .slice(0, this.MAX_TRIGRAMS_PER_KEY);
        m.tri.set(key, new Map(sortedTrigrams));
      }
    });

    console.info(`OpenBoard: Pruned ${wordsToRemove} words, new size: ${m.uni.size}`);
  }

  recordWord(lang: string, prev: string | null, word: string, prev2: string | null = null): void {
    if (!word) {
      return;
    }

    const m = this.ensure(lang);
    const w = word.toLowerCase(); // Normalize to lowercase for better matching
    const now = Date.now();

    // Prune model if it's getting too large (before adding new word)
    if (m.uni.size >= this.MAX_WORDS_PER_LANGUAGE) {
      this.pruneModel(m);
    }

    // Step 0: Check if word is blocked
    if (m.blockedWords.has(w)) {
      console.info(`OpenBoard: Word "${w}" is blocked and will not be learned`);
      return;
    }

    // Step 1: Check word status
    const isUserLearned = m.uni.has(w); // Word in user model
    const isBase = this.isBaseWord(lang, w); // Word in base dictionary

    if (isUserLearned) {
      // Case 1: Word is in user model - update normally
      const u = m.uni.get(w) ?? 0;
      m.uni.set(w, u + 1);

      // Apply decay to competing unigrams
      this.applyDecayAndCleanup(m.uni, w);

      // Update word rank (total usage count)
      const rank = m.wordRank.get(w) ?? 0;
      m.wordRank.set(w, rank + 1);

      // Update last used timestamp
      m.lastUsed.set(w, now);

      // Update bigram/trigram context
      this.updateBigramTrigram(m, w, prev, prev2, now);

    } else if (isBase) {
      // Case 2: Word is in base dictionary (but not in user model)
      // Personalize: track usage for recency bonus, but don't add to uni (no decay/prune)
      const rank = m.wordRank.get(w) ?? 0;
      m.wordRank.set(w, rank + 1);
      m.lastUsed.set(w, now);

      // Update bigram/trigram context for better predictions
      this.updateBigramTrigram(m, w, prev, prev2, now);

      console.info(`OpenBoard: Base word "${w}" personalized (rank: ${rank + 1})`);

    } else {
      // Case 3: Word is NOT in base and NOT in user model - pending logic
      const pendingCount = m.pendingWords.get(w) ?? 0;
      const newPendingCount = pendingCount + 1;

      console.info(`OpenBoard: Word "${w}" typed ${newPendingCount}/${this.LEARN_THRESHOLD} times`);

      if (newPendingCount >= this.LEARN_THRESHOLD) {
        // Word has been typed 3 times! Time to learn it

        // Check if there's a similar learned word (typo detection)
        const similarWord = this.findSimilarLearnedWord(m, w, lang);

        if (similarWord) {
          // This might be a typo of an already learned word
          console.info(`OpenBoard: Word "${w}" is similar to learned word "${similarWord}". Removing "${w}" from pending.`);

          // Remove from pending (it's likely a typo)
          m.pendingWords.delete(w);

          // Instead, increase count of the correct word
          const u = m.uni.get(similarWord) ?? 0;
          m.uni.set(similarWord, u + 1);

          const rank = m.wordRank.get(similarWord) ?? 0;
          m.wordRank.set(similarWord, rank + 1);

          m.lastUsed.set(similarWord, now);
        } else {
          // No similar word found, learn this word!
          console.info(`OpenBoard: Learning new word "${w}" after ${this.LEARN_THRESHOLD} occurrences`);

          // Remove from pending
          m.pendingWords.delete(w);

          // Add to learned user model with count = LEARN_THRESHOLD (it was already typed 3 times)
          m.uni.set(w, this.LEARN_THRESHOLD);
          m.wordRank.set(w, this.LEARN_THRESHOLD);
          m.lastUsed.set(w, now);

          // Also add to bigram/trigram if context exists
          this.updateBigramTrigram(m, w, prev, prev2, now);
        }
      } else {
        // Still pending, increment count
        m.pendingWords.set(w, newPendingCount);
        console.info(`OpenBoard: Word "${w}" saved to pending (not yet learned)`);
      }
    }

    // Auto-save periodically (debounced)
    this.saveCounter++;
    if (this.saveCounter >= this.SAVE_THRESHOLD) {
      this.saveCounter = 0;
      this.debouncedSave();
    }
  }

  // Capitalization helper methods
  private applyCasing(word: string, reference: string): string {
    if (!reference || reference.length === 0) return word;

    // Check if reference is all uppercase
    if (reference === reference.toUpperCase() && reference !== reference.toLowerCase()) {
      return word.toUpperCase();
    }

    // Check if reference starts with uppercase
    if (reference[0] === reference[0].toUpperCase() && reference[0] !== reference[0].toLowerCase()) {
      return word.charAt(0).toUpperCase() + word.slice(1).toLowerCase();
    }

    // Otherwise, return lowercase
    return word.toLowerCase();
  }

  private shouldCapitalize(prevWord: string | null): boolean {
    if (!prevWord) return true; // Capitalize at start

    // Capitalize after sentence-ending punctuation
    const sentenceEnders = ['.', '!', '?', '。', '！', '？'];
    return sentenceEnders.includes(prevWord);
  }

  // Find best autocorrection candidate (optimized for 50k base lexicon)
  async findAutocorrection(lang: string, word: string): Promise<string | null> {
    if (!word || word.length < 2) return null;

    const m = this.ensure(lang);
    const wordLower = word.toLowerCase();

    // Don't autocorrect if word is already learned OR in base dictionary
    if (this.isWordLearned(m, wordLower) || this.isBaseWord(lang, wordLower)) return null;

    // Ensure BaseLexicon is loaded (lazy loading)
    await this.ensureBaseLexiconLoaded(lang);
    const baseLexicon = this.getBaseLexicon(lang);

    // Find similar word from user learned words OR base lexicon
    const threshold = Math.min(2, Math.floor(word.length * 0.3));
    let bestMatch: string | null = null;
    let bestDistance = threshold + 1;
    let bestRank = 0;

    // Priority 1: Check user learned words (small set, full scan is OK)
    m.uni.forEach((count, learnedWord) => {
      // Only consider truly learned words (count >= LEARN_THRESHOLD), not blocked
      if (count >= this.LEARN_THRESHOLD && !m.blockedWords.has(learnedWord) &&
          Math.abs(learnedWord.length - wordLower.length) <= threshold) {
        const distance = this.levenshteinDistance(wordLower, learnedWord, threshold);
        const rank = m.wordRank.get(learnedWord) ?? count;

        // Prefer corrections with better rank if distances are equal
        if (distance < bestDistance || (distance === bestDistance && rank > bestRank)) {
          bestDistance = distance;
          bestMatch = learnedWord;
          bestRank = rank;
        }
      }
    });

    // Priority 2: Check BaseLexicon (OPTIMIZED - limited pool, not O(50k))
    if (baseLexicon && baseLexicon.isLoaded()) {
      // Strategy: Only check top 2000 frequent words to avoid O(50k) Levenshtein
      // This covers 95%+ of real-world autocorrect cases with 25x speedup
      const candidatePool = baseLexicon.getTopFrequentWords(); // Top 2000 frequent words

      for (const baseWord of candidatePool) {
        if (baseLexicon.isBlocked(baseWord) || m.blockedWords.has(baseWord)) continue;
        if (Math.abs(baseWord.length - wordLower.length) > threshold) continue;

        const distance = this.levenshteinDistance(wordLower, baseWord, threshold);
        const rank = m.wordRank.get(baseWord) ?? baseLexicon.getFrequency(baseWord);

        // Prefer corrections with better rank if distances are equal
        if (distance < bestDistance || (distance === bestDistance && rank > bestRank)) {
          bestDistance = distance;
          bestMatch = baseWord;
          bestRank = rank;
        }
      }
    }

    // Only return correction if it's a common word (rank > 5)
    if (bestMatch && bestDistance <= threshold && bestRank > 5) {
      // Apply original capitalization to the correction
      return this.applyCasing(bestMatch, word);
    }

    return null;
  }

  async predict(lang: string, prev: string | null, prefix: string | null, k: number, prev2: string | null = null): Promise<string[]> {
    const m = this.ensure(lang);

    // Ensure BaseLexicon is loaded (lazy loading)
    await this.ensureBaseLexiconLoaded(lang);
    const baseLexicon = this.getBaseLexicon(lang);

    const list: RankedWord[] = [];
    const now = Date.now();
    const prefixLower = prefix ? prefix.toLowerCase() : null;
    const prevLower = prev ? prev.toLowerCase() : null;
    const prev2Lower = prev2 ? prev2.toLowerCase() : null;

    const filter = (s: string): boolean => {
      if (!prefixLower || prefixLower.length === 0) {
        return true;
      }
      return s.startsWith(prefixLower);
    };

    // Hybrid scoring: Context >> UserRank/Recency >> BaseFreq
    const calculateScore = (word: string, contextCount: number, isContextBased: boolean, baseFreq: number = 0): number => {
      // Get user rank (personalization)
      const userRank = m.wordRank.get(word) ?? 0;

      // Recency bonus calculation
      const lastUse = m.lastUsed.get(word) ?? 0;
      let recencyBonus = 0;
      if (lastUse > 0) {
        const recency = now - lastUse;
        const recencyHours = recency / (1000 * 60 * 60);
        recencyBonus = Math.max(0, 50 - (recencyHours * 0.5));
      }

      if (isContextBased) {
        // For context-based predictions (bigram/trigram):
        // Context dominates, user personalization is secondary
        return (contextCount * this.SCORE_CONTEXT_WEIGHT) +
               (userRank * this.SCORE_USER_RANK_WEIGHT) +
               (recencyBonus * this.SCORE_RECENCY_WEIGHT) +
               (baseFreq * this.SCORE_BASE_FREQ_WEIGHT);
      } else {
        // For general predictions (unigram/base):
        // User rank and recency matter, base frequency is fallback
        return (userRank * this.SCORE_USER_RANK_WEIGHT) +
               (recencyBonus * this.SCORE_RECENCY_WEIGHT) +
               (baseFreq * this.SCORE_BASE_FREQ_WEIGHT);
      }
    };

    // Priority 1: User trigram predictions (highest priority)
    if (prev2Lower && prevLower) {
      const triKeyStr = this.triKey(prev2Lower, prevLower);
      const triMap = m.tri.get(triKeyStr);
      if (triMap) {
        triMap.forEach((c, w) => {
          // Only suggest truly learned words, not blocked
          if (filter(w) && this.isWordLearned(m, w) && !m.blockedWords.has(w)) {
            const baseFreq = baseLexicon?.getFrequency(w) ?? 0;
            const score = calculateScore(w, c, true, baseFreq);
            list.push({ w: w, c: c, score: score });
          }
        });
      }
    }

    // Priority 2: User bigram predictions
    if (list.length < k && prevLower) {
      const nextMap = m.bi.get(prevLower);
      if (nextMap) {
        nextMap.forEach((c, w) => {
          // Only suggest truly learned words, not blocked
          if (filter(w) && this.isWordLearned(m, w) && !m.blockedWords.has(w) && !list.find(item => item.w === w)) {
            const baseFreq = baseLexicon?.getFrequency(w) ?? 0;
            const score = calculateScore(w, c, true, baseFreq);
            list.push({ w: w, c: c, score: score });
          }
        });
      }
    }

    // Priority 3: User unigram (learned words)
    if (list.length < k) {
      m.uni.forEach((c, w) => {
        // Only suggest truly learned words (count >= LEARN_THRESHOLD), not blocked
        if (c >= this.LEARN_THRESHOLD && filter(w) && !m.blockedWords.has(w) && !list.find(item => item.w === w)) {
          const baseFreq = baseLexicon?.getFrequency(w) ?? 0;
          const score = calculateScore(w, c, false, baseFreq);
          list.push({ w: w, c: c, score: score });
        }
      });
    }

    // Priority 4: Fill from BaseLexicon (50k words with binary search)
    if (list.length < k && baseLexicon && baseLexicon.isLoaded()) {
      // Use prefix search if prefix is provided (very fast O(log n) + O(m) where m = matches)
      if (prefixLower && prefixLower.length > 0) {
        const lexEntries = baseLexicon.findByPrefix(prefixLower, k * 2); // Get more candidates for better ranking
        lexEntries.forEach(entry => {
          const w = entry.word;
          // Skip if already in list, blocked, or user blocked
          if (!list.find(item => item.w === w) && !m.blockedWords.has(w)) {
            const score = calculateScore(w, 0, false, entry.freq);
            list.push({ w: w, c: 0, score: score });
          }
        });
      } else {
        // No prefix - use top frequent words (limited pool, no full scan)
        const topWords = baseLexicon.getTopFrequentWords(); // Top 2000 frequent words
        for (const w of topWords) {
          if (!list.find(item => item.w === w) && !baseLexicon.isBlocked(w) && !m.blockedWords.has(w)) {
            const freq = baseLexicon.getFrequency(w);
            const score = calculateScore(w, 0, false, freq);
            list.push({ w: w, c: 0, score: score });
          }
        }
      }
    }

    // Sort by score (highest first)
    list.sort((a, b) => b.score - a.score);

    // Extract top k unique words with proper capitalization
    const seen = new Set<string>();
    const out: string[] = [];
    const shouldCap = this.shouldCapitalize(prev);

    for (let i = 0; i < list.length && out.length < k; i++) {
      let w = list[i].w;
      if (!seen.has(w)) {
        seen.add(w);

        // Apply capitalization based on context
        if (prefix && prefix.length > 0) {
          // Match capitalization of user's input
          w = this.applyCasing(w, prefix);
        } else if (shouldCap) {
          // Capitalize at start or after sentence-ending punctuation
          w = w.charAt(0).toUpperCase() + w.slice(1);
        }

        out.push(w);
      }
    }

    // Fill with empty strings if needed
    while (out.length < k) {
      out.push('');
    }

    return out;
  }

  // Delete a word from the learned model
  deleteWord(lang: string, word: string): void {
    const m = this.ensure(lang);
    const w = word.toLowerCase();

    // Remove from unigram
    m.uni.delete(w);
    m.wordRank.delete(w);
    m.lastUsed.delete(w);

    // Remove from bigrams
    m.bi.forEach((nextMap, _) => {
      nextMap.delete(w);
    });

    // Remove from trigrams
    m.tri.forEach((nextMap, _) => {
      nextMap.delete(w);
    });

    // Remove from pending
    m.pendingWords.delete(w);

    console.info(`OpenBoard: Deleted word "${w}" from model`);

    // Auto-save after deletion (debounced)
    this.debouncedSave();
  }

  // Block a word from being learned
  blockWord(lang: string, word: string): void {
    const m = this.ensure(lang);
    const w = word.toLowerCase();

    // Add to blocked list
    m.blockedWords.add(w);

    // Also delete the word if it's already learned
    this.deleteWord(lang, word);

    console.info(`OpenBoard: Blocked word "${w}" from being learned`);

    // Auto-save after blocking (debounced)
    this.debouncedSave();
  }

  // Unblock a word (optional utility)
  unblockWord(lang: string, word: string): void {
    const m = this.ensure(lang);
    const w = word.toLowerCase();
    m.blockedWords.delete(w);

    console.info(`OpenBoard: Unblocked word "${w}"`);

    // Auto-save after unblocking (debounced)
    this.debouncedSave();
  }

  // Export model to JSON-serializable format
  private exportModel(lang: string): SerializedModel | null {
    const m = this.models.get(lang);
    if (!m) {
      return null;
    }

    return {
      uni: Array.from(m.uni.entries()),
      bi: Array.from(m.bi.entries()).map((entry: [string, Map<string, number>]) => {
        const key = entry[0];
        const map = entry[1];
        return [key, Array.from(map.entries())];
      }),
      tri: Array.from(m.tri.entries()).map((entry: [string, Map<string, number>]) => {
        const key = entry[0];
        const map = entry[1];
        return [key, Array.from(map.entries())];
      }),
      wordRank: Array.from(m.wordRank.entries()),
      lastUsed: Array.from(m.lastUsed.entries()),
      pendingWords: Array.from(m.pendingWords.entries()),
      blockedWords: Array.from(m.blockedWords)
    };
  }

  // Import model from JSON format
  private importModel(lang: string, data: SerializedModel): void {
    const m: LangModel = {
      uni: new Map(data.uni),
      bi: new Map(data.bi.map((entry: [string, [string, number][]]) => {
        const key = entry[0];
        const entries = entry[1];
        return [key, new Map(entries)];
      })),
      tri: new Map(data.tri.map((entry: [string, [string, number][]]) => {
        const key = entry[0];
        const entries = entry[1];
        return [key, new Map(entries)];
      })),
      wordRank: new Map(data.wordRank),
      lastUsed: new Map(data.lastUsed),
      pendingWords: new Map(data.pendingWords || []),
      blockedWords: new Set(data.blockedWords || [])
    };
    this.models.set(lang, m);
  }

  /**
   * Debounced save - schedules save after delay to avoid parallel writes
   */
  private debouncedSave(): void {
    // Clear existing timer
    if (this.saveTimer !== null) {
      clearTimeout(this.saveTimer);
    }

    // Schedule new save
    this.saveTimer = setTimeout(() => {
      this.save().catch((err: BusinessError) => {
        console.error('OpenBoard: Debounced save failed', err);
      });
    }, this.SAVE_DEBOUNCE_MS);
  }

  /**
   * Save all models to preferences (with race protection)
   */
  async save(): Promise<void> {
    if (!this.context) {
      console.warn('OpenBoard: Cannot save prediction data - context not set');
      return;
    }

    // Prevent parallel saves
    if (this.isSaving) {
      console.warn('OpenBoard: Save already in progress, skipping');
      return;
    }

    try {
      this.isSaving = true;
      const pref = await preferences.getPreferences(this.context, 'OpenBoard_prediction_data');
      const allModels: Record<string, SerializedModel> = {};

      this.models.forEach((model, lang) => {
        const exported = this.exportModel(lang);
        if (exported) {
          allModels[lang] = exported;
        }
      });

      await pref.put('models', JSON.stringify(allModels));
      await pref.flush();
      console.info('OpenBoard: Prediction data saved successfully');
    } catch (err) {
      const error = err as BusinessError;
      console.error(`OpenBoard: Failed to save prediction data. Code: ${error.code}, Msg: ${error.message}`);
      // Don't throw - just log the error
    } finally {
      this.isSaving = false;
    }
  }

  // Load all models from preferences
  async load(): Promise<void> {
    if (!this.context) {
      console.warn('OpenBoard: Cannot load prediction data - context not set');
      return;
    }

    try {
      const pref = await preferences.getPreferences(this.context, 'OpenBoard_prediction_data');
      const data = await pref.get('models', '');

      if (!data || typeof data !== 'string' || data.length === 0) {
        console.info('OpenBoard: No saved prediction data found');
        return;
      }

      const allModels: Record<string, SerializedModel> = JSON.parse(data as string);

      Object.keys(allModels).forEach(lang => {
        this.importModel(lang, allModels[lang]);
      });

      console.info(`OpenBoard: Loaded prediction data for ${Object.keys(allModels).length} language(s)`);
    } catch (err) {
      const error = err as BusinessError;
      console.error(`OpenBoard: Failed to load prediction data. Code: ${error.code}, Msg: ${error.message}`);
      // Don't throw - just continue with empty model
    }
  }

  // Clear all learned data for a language (optional utility)
  clearLanguage(lang: string): void {
    this.models.delete(lang);
  }

  // Clear all learned data (optional utility)
  clearAll(): void {
    this.models.clear();
  }

  // Load dictionary from rawfile
  private async loadDictionaryFromFile(filename: string): Promise<string[]> {
    if (!this.context) {
      console.warn('OpenBoard: Cannot load dictionary - context not set');
      return [];
    }

    try {
      const resMgr = this.context.resourceManager;
      const fileData = await resMgr.getRawFileContent(filename);
      const decoder = util.TextDecoder.create('utf-8');
      const text = decoder.decodeToString(new Uint8Array(fileData.buffer));

      // Split by newlines and filter empty lines
      const words = text.split('\n')
        .map(w => w.trim())
        .filter(w => w.length > 0);

      console.info(`OpenBoard: Loaded ${words.length} words from ${filename}`);
      return words;
    } catch (err) {
      const error = err as BusinessError;
      console.error(`OpenBoard: Failed to load dictionary ${filename}. Code: ${error.code}, Msg: ${error.message}`);
      return [];
    }
  }

  // Load base dictionaries and lexicons from rawfile (read-only, no learning)
  async loadDictionaries(): Promise<void> {
    interface DictionaryConfig {
      lang: string;
      file: string;
    }

    // BaseLexicon (50k words) will be loaded lazily when first needed
    // This improves startup time and memory usage
    console.info('OpenBoard: BaseLexicon will be loaded on-demand (lazy loading)');

    // Keep loading old small dictionaries for backward compatibility
    const dictionaries: DictionaryConfig[] = [
      { lang: 'en', file: 'dictionary_en.txt' },
      { lang: 'ru', file: 'dictionary_ru.txt' }
    ];

    for (const dict of dictionaries) {
      const rawWords = await this.loadDictionaryFromFile(dict.file);
      if (rawWords.length === 0) continue;

      // Filter and normalize words
      const words = rawWords
        .map(w => w.trim().toLowerCase())
        .filter(w => w.length >= 2); // Skip too short words

      const baseDict = this.ensureBaseDict(dict.lang);

      // Add words to base dictionary with rank based on position
      // Higher rank for common words (they appear first in the file)
      words.forEach((word, index) => {
        // Skip if already in base dictionary
        if (baseDict.rank.has(word)) return;

        // Weight: earlier words (more common) get higher rank
        // First 100 words: rank 50-150
        // Next 400 words: rank 20-50
        // Rest: rank 10-20
        let baseRank = 10;
        if (index < 100) {
          baseRank = 150 - index;
        } else if (index < 500) {
          baseRank = 50 - Math.floor((index - 100) / 10);
        }

        baseDict.rank.set(word, baseRank);
      });

      console.info(`OpenBoard: Loaded ${words.length} fallback base words for language '${dict.lang}' (read-only dictionary)`);
    }
  }
}

const predictionModel = new PredictionModel();

export default predictionModel;
