import { preferences } from '@kit.ArkData';
import { common } from '@kit.AbilityKit';
import { BusinessError } from '@kit.BasicServicesKit';
import privacyGate, { PrivacyGate } from './PrivacyGate';

// Interfaces for language models
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

export class PredictionModel {
  private models: Map<string, LangModel>;
  private context: common.UIAbilityContext | null = null;
  private privacyGate: PrivacyGate;
  private saveCounter: number = 0;
  private readonly SAVE_THRESHOLD = 10; // Save after every 10 words learned
  private readonly DECAY_FACTOR = 0.95; // Decay competing words by 5%
  private readonly MIN_COUNT_THRESHOLD = 0.5; // Remove words with count below this
  private readonly LEARN_THRESHOLD = 3; // Word must be typed 3 times to be learned

  constructor(gate: PrivacyGate = privacyGate) {
    this.models = new Map();
    this.privacyGate = gate;
  }

  setContext(ctx: common.UIAbilityContext): void {
    this.context = ctx;
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

  // Calculate Levenshtein distance for similarity comparison
  private levenshteinDistance(a: string, b: string): number {
    if (a.length === 0) return b.length;
    if (b.length === 0) return a.length;

    const matrix: number[][] = [];

    for (let i = 0; i <= b.length; i++) {
      matrix[i] = [i];
    }

    for (let j = 0; j <= a.length; j++) {
      matrix[0][j] = j;
    }

    for (let i = 1; i <= b.length; i++) {
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
      }
    }

    return matrix[b.length][a.length];
  }

  // Find if there's a similar learned word (for typo detection)
  private findSimilarLearnedWord(m: LangModel, word: string): string | null {
    // Only a single-edit difference counts as a possible typo, and only an
    // already-established word (count >= LEARN_THRESHOLD) may absorb it. This
    // stops distinct valid words one edit apart (e.g. "стол"/"стал") from
    // permanently masking each other.
    const threshold = 1;

    let bestMatch: string | null = null;
    let bestDistance = threshold + 1;

    m.uni.forEach((count, learnedWord) => {
      if (learnedWord !== word && count >= this.LEARN_THRESHOLD &&
          Math.abs(learnedWord.length - word.length) <= threshold) {
        const distance = this.levenshteinDistance(word, learnedWord);
        if (distance < bestDistance && distance <= threshold) {
          bestDistance = distance;
          bestMatch = learnedWord;
        }
      }
    });

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

  recordWord(lang: string, prev: string | null, word: string, prev2: string | null = null): void {
    if (!this.privacyGate.canProcessText()) {
      return;
    }
    if (!word) {
      return;
    }

    const m = this.ensure(lang);
    const w = word.toLowerCase(); // Normalize to lowercase for better matching
    const now = Date.now();

    // Step 0: Check if word is blocked
    if (m.blockedWords.has(w)) {
      console.info(`OpenBoard: Word "${w}" is blocked and will not be learned`);
      return;
    }

    // Step 1: Check if word is already learned
    const isLearned = m.uni.has(w);

    if (isLearned) {
      // Word is already learned, update normally
      const u = m.uni.get(w) ?? 0;
      m.uni.set(w, u + 1);

      // Apply decay to competing unigrams
      this.applyDecayAndCleanup(m.uni, w);

      // Update word rank (total usage count)
      const rank = m.wordRank.get(w) ?? 0;
      m.wordRank.set(w, rank + 1);

      // Update last used timestamp
      m.lastUsed.set(w, now);

      // Update bigram if we have previous word
      if (prev && prev.length > 0) {
        const prevLower = prev.toLowerCase();
        let nextMap = m.bi.get(prevLower);
        if (!nextMap) {
          nextMap = new Map<string, number>();
          m.bi.set(prevLower, nextMap);
        }
        const c = nextMap.get(w) ?? 0;
        nextMap.set(w, c + 1);

        // Apply decay to competing bigrams
        this.applyDecayAndCleanup(nextMap, w);

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
          const tc = triMap.get(w) ?? 0;
          triMap.set(w, tc + 1);

          // Apply decay to competing trigrams
          this.applyDecayAndCleanup(triMap, w);

          // Remove empty trigram entries
          if (triMap.size === 0) {
            m.tri.delete(triKeyStr);
          }
        }
      }
    } else {
      // Word is NOT yet learned - check pending status
      const pendingCount = m.pendingWords.get(w) ?? 0;
      const newPendingCount = pendingCount + 1;

      console.info(`OpenBoard: Word "${w}" typed ${newPendingCount}/${this.LEARN_THRESHOLD} times`);

      if (newPendingCount >= this.LEARN_THRESHOLD) {
        // A word that reached the learn threshold is always learned as its own
        // entry. If it is a single edit from an established word it may be a
        // typo, so reinforce that word and seed the new one low (probation, so a
        // real typo decays out); otherwise seed it at LEARN_THRESHOLD so a
        // genuine new word survives decay from a handful of other words
        // (see applyDecayAndCleanup).
        m.pendingWords.delete(w);

        const similarWord = this.findSimilarLearnedWord(m, w);
        if (similarWord) {
          console.info(`OpenBoard: Word "${w}" is one edit from learned "${similarWord}"; reinforcing both.`);
          const su = m.uni.get(similarWord) ?? 0;
          m.uni.set(similarWord, su + 1);
          const srank = m.wordRank.get(similarWord) ?? 0;
          m.wordRank.set(similarWord, srank + 1);
          m.lastUsed.set(similarWord, now);
        } else {
          console.info(`OpenBoard: Learning word "${w}" after ${this.LEARN_THRESHOLD} occurrences`);
        }

        // Learn w as its own entry: probation seed (1) when it looks like a
        // typo, full seed (LEARN_THRESHOLD) otherwise.
        m.uni.set(w, similarWord ? 1 : this.LEARN_THRESHOLD);
        m.wordRank.set(w, 1);
        m.lastUsed.set(w, now);

        // Also add to bigram/trigram if context exists
        if (prev && prev.length > 0) {
          const prevLower = prev.toLowerCase();
          let nextMap = m.bi.get(prevLower);
          if (!nextMap) {
            nextMap = new Map<string, number>();
            m.bi.set(prevLower, nextMap);
          }
          nextMap.set(w, 1);

          if (prev2 && prev2.length > 0) {
            const prev2Lower = prev2.toLowerCase();
            const triKeyStr = this.triKey(prev2Lower, prevLower);
            let triMap = m.tri.get(triKeyStr);
            if (!triMap) {
              triMap = new Map<string, number>();
              m.tri.set(triKeyStr, triMap);
            }
            triMap.set(w, 1);
          }
        }
      } else {
        // Still pending, increment count
        m.pendingWords.set(w, newPendingCount);
        console.info(`OpenBoard: Word "${w}" saved to pending (not yet learned)`);
      }
    }

    // Auto-save periodically
    this.saveCounter++;
    if (this.saveCounter >= this.SAVE_THRESHOLD) {
      this.saveCounter = 0;
      this.save().catch((err: BusinessError) => {
        console.error('OpenBoard: Failed to auto-save prediction data', err);
      });
    }
  }

  predict(lang: string, prev: string | null, prefix: string | null, k: number, prev2: string | null = null): string[] {
    if (!this.privacyGate.canProcessText()) {
      return [];
    }
    const m = this.ensure(lang);
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

    // Enhanced scoring: heavily prioritize context over general frequency
    const calculateScore = (word: string, contextCount: number, isContextBased: boolean): number => {
      const rank = m.wordRank.get(word) ?? 0;
      const lastUse = m.lastUsed.get(word) ?? 0;
      const recency = now - lastUse;
      const recencyHours = recency / (1000 * 60 * 60);

      // Recency bonus decreases over time (max 50 points)
      const recencyBonus = Math.max(0, 50 - (recencyHours * 0.5));

      if (isContextBased) {
        // For context-based predictions (bigram/trigram):
        // Context count is MUCH more important than general rank
        // contextCount gets 100x weight, rank gets 1x weight
        return (contextCount * 100) + (rank * 1) + recencyBonus;
      } else {
        // For general predictions (unigram):
        // Only rank and recency matter
        return (rank * 2) + recencyBonus;
      }
    };

    // Priority 1: Try trigram predictions (highest priority)
    if (prev2Lower && prevLower) {
      const triKeyStr = this.triKey(prev2Lower, prevLower);
      const triMap = m.tri.get(triKeyStr);
      if (triMap) {
        triMap.forEach((c, w) => {
          if (filter(w)) {
            const score = calculateScore(w, c, true); // Context-based
            list.push({ w: w, c: c, score: score });
          }
        });
      }
    }

    // Priority 2: Try bigram predictions
    if (list.length < k && prevLower) {
      const nextMap = m.bi.get(prevLower);
      if (nextMap) {
        nextMap.forEach((c, w) => {
          if (filter(w) && !list.find(item => item.w === w)) {
            const score = calculateScore(w, c, true); // Context-based
            list.push({ w: w, c: c, score: score });
          }
        });
      }
    }

    // Priority 3: Fall back to unigram (most frequently used words)
    if (list.length < k) {
      m.uni.forEach((c, w) => {
        if (filter(w) && !list.find(item => item.w === w)) {
          const score = calculateScore(w, c, false); // NOT context-based
          list.push({ w: w, c: c, score: score });
        }
      });
    }

    // Sort by score (highest first)
    list.sort((a, b) => b.score - a.score);

    // Extract top k unique words
    const seen = new Set<string>();
    const out: string[] = [];
    for (let i = 0; i < list.length && out.length < k; i++) {
      const w = list[i].w;
      if (!seen.has(w)) {
        seen.add(w);
        out.push(w);
      }
    }

    // Return only real suggestions. Padding to k with '' used to let callers
    // that don't strip empties treat a blank as a candidate (e.g. autocorrect
    // to ""); consumers that want a fixed length can pad themselves.
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

    // Auto-save after deletion
    this.save().catch((err: BusinessError) => {
      console.error('OpenBoard: Failed to save after deleting word', err);
    });
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

    // Auto-save after blocking
    this.save().catch((err: BusinessError) => {
      console.error('OpenBoard: Failed to save after blocking word', err);
    });
  }

  // Unblock a word (optional utility)
  unblockWord(lang: string, word: string): void {
    const m = this.ensure(lang);
    const w = word.toLowerCase();
    m.blockedWords.delete(w);

    console.info(`OpenBoard: Unblocked word "${w}"`);

    // Auto-save after unblocking
    this.save().catch((err: BusinessError) => {
      console.error('OpenBoard: Failed to save after unblocking word', err);
    });
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

  // Save all models to preferences
  async save(): Promise<void> {
    if (!this.privacyGate.canProcessText()) {
      return;
    }
    if (!this.context) {
      console.warn('OpenBoard: Cannot save prediction data - context not set');
      return;
    }

    try {
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
}

const predictionModel = new PredictionModel();

export default predictionModel;
