import { preferences } from '@kit.ArkData';
import { common } from '@kit.AbilityKit';
import { BusinessError } from '@kit.BasicServicesKit';
import { resourceManager } from '@kit.LocalizationKit';
import { util } from '@kit.ArkTS';
import { BaseLexicon } from './BaseLexicon';
import { Logger } from '../utils/logger';

// Debug mode flag - set to false for production
const DEBUG_MODE = false; // Disabled for performance

function debugLog(...args: string[]): void {
  if (DEBUG_MODE) {
    console.info('[PredictionModel]', ...args);
  }
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
  lemmaMap: Map<string, string>; // Morphology: word form -> lemma ("делаю" -> "делать")
  formsByLemma: Map<string, Set<string>>; // Morphology: lemma -> all seen forms
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
  lemmaMap: [string, string][]; // Morphology: word form -> lemma
  formsByLemma: [string, string[]][]; // Morphology: lemma -> forms array
}

interface VerbPattern {
  endings: string[];
  lemma: string;
}

/**
 * Centralized configuration for prediction system
 * Based on UX research and A/B testing principles
 */
class PredictionConfig {
  // Learning thresholds
  static readonly LEARN_THRESHOLD = 3; // Word becomes "learned" after 3 occurrences
  static readonly PENDING_PREFIX_THRESHOLD = 1; // Show pending with prefix after 1 occurrence
  static readonly PENDING_CONTEXT_THRESHOLD = 2; // Show pending in context after 2 occurrences
  static readonly PENDING_NGRAM_THRESHOLD = 2; // Update n-grams starting from 2 occurrences

  // Scoring weights (carefully balanced for Russian morphology)
  static readonly SCORE_CONTEXT_WEIGHT = 100; // Bigram/trigram context (highest)
  static readonly SCORE_LEARNED_WEIGHT = 8; // Learned words (strong signal)
  static readonly SCORE_PENDING_WEIGHT = 2; // Pending words (weaker signal)
  static readonly SCORE_BASE_WEIGHT = 3; // BaseLexicon frequency (good fallback)
  static readonly SCORE_RECENCY_WEIGHT = 1; // Recent usage bonus
  static readonly SCORE_MORPHOLOGY_WEIGHT = 0.7; // Related word forms (80% of base)

  // Model limits
  static readonly MAX_WORDS_PER_LANGUAGE = 10000;
  static readonly MAX_PENDING_PER_LANGUAGE = 500; // Prevent pending overflow
  static readonly MAX_BIGRAMS_PER_WORD = 100;
  static readonly MAX_TRIGRAMS_PER_KEY = 50;

  // Save settings
  static readonly SAVE_THRESHOLD = 10;
  static readonly SAVE_DEBOUNCE_MS = 3000;
  static readonly DECAY_FACTOR = 0.95;
  static readonly MIN_COUNT_THRESHOLD = 0.5;

  // Anti-garbage filters
  static readonly MIN_WORD_LENGTH = 1;
  static readonly MAX_DIGIT_RATIO = 0.4; // Max 40% digits
  static readonly MAX_SYMBOL_RATIO = 0.25; // Max 25% symbols
  static readonly MAX_REPEAT_RATIO = 0.6; // Max 60% repeating chars ("aaaaaa")
  static readonly MIN_UNIQUE_CHARS = 2; // At least 2 different characters

  /**
   * Calculate prefix boost proportional to match length
   * NOTE: Expects both word and prefix to be lowercase for performance
   */
  static calculatePrefixBoost(word: string, prefix: string): number {
    if (!prefix || prefix.length === 0) return 0;
    const matchLength = PredictionConfig.getCommonPrefixLength(word, prefix);
    return matchLength * 3; // 3 points per matched character
  }

  private static getCommonPrefixLength(str1: string, str2: string): number {
    let len = 0;
    const minLen = Math.min(str1.length, str2.length);
    for (let i = 0; i < minLen; i++) {
      if (str1[i] === str2[i]) {
        len++;
      } else {
        break;
      }
    }
    return len;
  }
}

/**
 * Generate Russian verb forms from lemma (infinitive)
 * Supports: -ать/-ять and -ить conjugations + common exceptions
 * @param lemma - verb lemma in lowercase (e.g., "делать")
 * @returns array of conjugated forms (e.g., ["делаю", "делаешь", ...])
 */
function generateRussianVerbForms(lemma: string): string[] {
  // Protection: skip short or invalid words
  if (lemma.length < 4) {
    return [];
  }

  // Exception patterns (irregular verbs)
  const exceptions: Map<string, string[]> = new Map([
    ['идти', ['иду', 'идёшь', 'идёт', 'идём', 'идёте', 'идут']],
    ['быть', ['буду', 'будешь', 'будет', 'будем', 'будете', 'будут']],
    ['дать', ['даю', 'даёшь', 'даёт', 'даём', 'даёте', 'дают']],
    ['есть', ['ем', 'ешь', 'ест', 'едим', 'едите', 'едят']],
    ['хотеть', ['хочу', 'хочешь', 'хочет', 'хотим', 'хотите', 'хотят']],
  ]);

  // Check for exceptions
  const exceptionForms = exceptions.get(lemma);
  if (exceptionForms) {
    return exceptionForms;
  }

  const forms: string[] = [];

  // Pattern 1: -ать/-ять verbs (делать -> делаю, делаешь, ...)
  if (lemma.endsWith('ать') || lemma.endsWith('ять')) {
    const stem = lemma.slice(0, -3); // "делать" -> "дел"
    forms.push(
      stem + 'аю',    // я делаю
      stem + 'аешь',  // ты делаешь
      stem + 'ает',   // он делает
      stem + 'аем',   // мы делаем
      stem + 'аете',  // вы делаете
      stem + 'ают'    // они делают
    );
    return forms;
  }

  // Pattern 2: -ить verbs (говорить -> говорю, говоришь, ...)
  if (lemma.endsWith('ить')) {
    const stem = lemma.slice(0, -3); // "говорить" -> "говор"
    forms.push(
      stem + 'ю',     // я говорю
      stem + 'ишь',   // ты говоришь
      stem + 'ит',    // он говорит
      stem + 'им',    // мы говорим
      stem + 'ите',   // вы говорите
      stem + 'ят'     // они говорят
    );
    return forms;
  }

  // Unknown pattern - return empty
  return [];
}

class PredictionModel {
  private models: Map<string, LangModel>; // User-learned models (mutable)
  private baseLexicons: Map<string, BaseLexicon>; // Base lexicons (50k words with binary search)
  private context: common.Context | null = null;
  private saveCounter: number = 0;
  private saveTimer: number | null = null;
  private isSaving: boolean = false;

  constructor() {
    this.models = new Map();
    this.baseLexicons = new Map();
  }

  setContext(ctx: common.Context): void {
    this.context = ctx;

    // Initialize base lexicons for languages that have lexicon files
    // BaseLexicon is OPTIONAL - predictions work without it via user model + pending words
    //
    // HOW TO ADD NEW LANGUAGE (e.g. 'en'):
    // 1. Create files: rawfile/en_base_words_50k.txt and en_base_freq_50k.tsv
    // 2. Add 'en' to baseLexiconLanguages array below
    // 3. Done! No code changes needed in predict()/recordWord()
    //
    // Currently supported: 'ru' (50k words with morphology)
    const baseLexiconLanguages = ['ru'];
    baseLexiconLanguages.forEach(lang => {
      const lexicon = new BaseLexicon();
      lexicon.setContext(ctx);
      this.baseLexicons.set(lang, lexicon);
    });
  }

  /**
   * Check if language has BaseLexicon registered
   */
  hasBaseLexicon(lang: string): boolean {
    return this.baseLexicons.has(lang);
  }

  /**
   * Get BaseLexicon for language (returns null if not available)
   */
  getBaseLexicon(lang: string): BaseLexicon | null {
    return this.baseLexicons.get(lang) ?? null;
  }

  /**
   * Lazy load BaseLexicon for language (only loads once when first needed)
   */
  private async ensureBaseLexiconLoaded(lang: string): Promise<void> {
    const lexicon = this.baseLexicons.get(lang);
    if (!lexicon) {
      debugLog(`No BaseLexicon configured for language '${lang}'`);
      return;
    }

    const loaded = lexicon.isLoaded();

    if (!loaded) {
      try {
        debugLog(`Lazy-loading BaseLexicon for '${lang}'...`);
        await lexicon.load(lang);
        const stats = lexicon.getStats();
        debugLog(`BaseLexicon for '${lang}' loaded, words: ${stats.words}, cached: ${stats.cached}`);
      } catch (err) {
        const error = err as BusinessError;
        console.error(`[PredictionModel] FAILED to load BaseLexicon for '${lang}'. Code: ${error.code}, Msg: ${error.message}`);
        console.warn(`PredictionModel: Continuing without BaseLexicon for '${lang}'. Predictions will use user model only.`);
        // Don't throw - graceful fallback to user model only
      }
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

  /**
   * Preload/warmup BaseLexicon for active language
   * Call this when keyboard starts or language switches to avoid delays during typing
   */
  async warmup(lang: string): Promise<void> {
    await this.ensureBaseLexiconLoaded(lang);
    console.info(`PredictionModel: Warmup complete for language '${lang}'`);
  }

  /**
   * Check if word exists in base lexicon
   * Returns false if BaseLexicon is not loaded yet (safe for sync calls from recordWord)
   */
  private isBaseWord(lang: string, word: string): boolean {
    const lexicon = this.getBaseLexicon(lang);
    if (!lexicon || !lexicon.isLoaded()) {
      // BaseLexicon not loaded yet - treat word as unknown
      // This is safe: warmup() should be called before user starts typing
      return false;
    }
    return lexicon.contains(word);
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
        blockedWords: new Set<string>(),
        lemmaMap: new Map<string, string>(),
        formsByLemma: new Map<string, Set<string>>()
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

  /**
   * Guess lemma (base form) for a word using suffix rules
   * Currently supports Russian language only
   */
  private guessLemma(word: string, lang: string): string {
    if (lang !== 'ru' || word.length < 4) {
      return word;
    }

    // Verb patterns for Russian (ordered from longest to shortest endings)
    const verbPatterns: VerbPattern[] = [
      // 1st conjugation
      { endings: ['аешь', 'аете', 'ают'], lemma: 'ать' },
      { endings: ['уешь', 'уете', 'уют'], lemma: 'овать' },
      { endings: ['яешь', 'яете', 'яют'], lemma: 'ять' },
      { endings: ['аю', 'ает', 'аем'], lemma: 'ать' },
      { endings: ['ую', 'ует', 'уем'], lemma: 'овать' },
      { endings: ['яю', 'яет', 'яем'], lemma: 'ять' },

      // 2nd conjugation
      { endings: ['ишь', 'ите'], lemma: 'ить' },
      { endings: ['ю', 'ит', 'им', 'ят'], lemma: 'ить' },
      { endings: ['у', 'ат'], lemma: 'ать' }, // держать, слышать

      // Past tense
      { endings: ['ала', 'ало', 'али'], lemma: 'ать' },
      { endings: ['ила', 'ило', 'или'], lemma: 'ить' },
      { endings: ['ела', 'ело', 'ели'], lemma: 'еть' },
      { endings: ['ал', 'ил', 'ел'], lemma: 'ть' }
    ];

    // Try each pattern
    for (const pattern of verbPatterns) {
      for (const ending of pattern.endings) {
        if (word.endsWith(ending)) {
          const stem = word.substring(0, word.length - ending.length);
          if (stem.length >= 2) { // Ensure stem is reasonable
            return stem + pattern.lemma;
          }
        }
      }
    }

    return word;
  }

  /**
   * Link a word form to its lemma in the morphology maps
   */
  private linkFormToLemma(m: LangModel, form: string, lemma: string): void {
    m.lemmaMap.set(form, lemma);

    let forms = m.formsByLemma.get(lemma);
    if (!forms) {
      forms = new Set<string>();
      m.formsByLemma.set(lemma, forms);
    }
    forms.add(form);
  }

  /**
   * Get all related word forms for a given word (including the word itself)
   */
  private getRelatedForms(m: LangModel, word: string): string[] {
    const lemma = m.lemmaMap.get(word) ?? word;
    const forms = m.formsByLemma.get(lemma);
    return forms ? Array.from(forms) : [word];
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
    return count >= PredictionConfig.LEARN_THRESHOLD;
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
      if (count >= PredictionConfig.LEARN_THRESHOLD && learnedWord !== word && Math.abs(learnedWord.length - word.length) <= threshold) {
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
        const newCount = count * PredictionConfig.DECAY_FACTOR;

        if (newCount < PredictionConfig.MIN_COUNT_THRESHOLD) {
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
      if (biMap && biMap.size > PredictionConfig.MAX_BIGRAMS_PER_WORD) {
        // Keep only top entries
        const sortedBigrams = Array.from(biMap.entries())
          .sort((a, b) => b[1] - a[1])
          .slice(0, PredictionConfig.MAX_BIGRAMS_PER_WORD);
        m.bi.set(word, new Map(sortedBigrams));
      }
    });

    // Clean up trigrams
    m.tri.forEach((triMap, key) => {
      if (triMap.size > PredictionConfig.MAX_TRIGRAMS_PER_KEY) {
        const sortedTrigrams = Array.from(triMap.entries())
          .sort((a, b) => b[1] - a[1])
          .slice(0, PredictionConfig.MAX_TRIGRAMS_PER_KEY);
        m.tri.set(key, new Map(sortedTrigrams));
      }
    });

    console.info(`OpenBoard: Pruned ${wordsToRemove} words, new size: ${m.uni.size}`);
  }

  /**
   * Prune pending words to prevent memory overflow
   * Removes least recently used pending words when limit is exceeded
   */
  private prunePendingWords(m: LangModel): void {
    const excess = m.pendingWords.size - PredictionConfig.MAX_PENDING_PER_LANGUAGE;
    if (excess <= 0) return;

    console.info(`OpenBoard: Pending words limit reached (${m.pendingWords.size}), pruning ${excess}...`);

    // Sort pending words by count (ascending), remove lowest count words
    const sorted = Array.from(m.pendingWords.entries())
      .sort((a, b) => a[1] - b[1])
      .slice(0, excess);

    sorted.forEach((entry) => {
      const word = entry[0];
      m.pendingWords.delete(word);
      // Also clean up from wordRank/lastUsed if it exists
      m.wordRank.delete(word);
      m.lastUsed.delete(word);
    });

    console.info(`OpenBoard: Pruned ${excess} pending words, new size: ${m.pendingWords.size}`);
  }

  recordWord(lang: string, prev: string | null, word: string, prev2: string | null = null): void {
    if (!word) {
      return;
    }

    const m = this.ensure(lang);
    const w = word.toLowerCase(); // Normalize to lowercase for better matching
    const now = Date.now();

    // Prune model if it's getting too large (before adding new word)
    if (m.uni.size >= PredictionConfig.MAX_WORDS_PER_LANGUAGE) {
      this.pruneModel(m);
    }

    // Step 0: Check if word is blocked
    if (m.blockedWords.has(w)) {
      debugLog(`Word "${w}" is blocked and will not be learned`);
      return;
    }

    // Step 0.5: Check if word is garbage (early filtering)
    if (this.isGarbage(w, lang)) {
      debugLog(`Word "${w}" rejected as garbage`);
      return;
    }

    // Step 1: Check word status
    const isUserLearned = m.uni.has(w); // Word in user model
    const isBase = this.isBaseWord(lang, w); // Word in base dictionary

    if (isUserLearned) {
      // Case 1: Word is in user model - update normally
      const u = m.uni.get(w) ?? 0;
      m.uni.set(w, u + 1);

      // Morphology: link form to lemma (Russian only)
      if (lang === 'ru') {
        const lemma = this.guessLemma(w, lang);
        if (lemma !== w) {
          this.linkFormToLemma(m, w, lemma);
        }
      }

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

      debugLog(`Base word "${w}" personalized (rank: ${rank + 1})`);

    } else {
      // Case 3: Word is NOT in base and NOT in user model - pending logic
      const pendingCount = m.pendingWords.get(w) ?? 0;
      const newPendingCount = pendingCount + 1;

      debugLog(`Word "${w}" typed ${newPendingCount}/${PredictionConfig.LEARN_THRESHOLD} times`);

      if (newPendingCount >= PredictionConfig.LEARN_THRESHOLD) {
        // Word has been typed 3 times! Time to learn it

        // Check if there's a similar learned word (typo detection)
        const similarWord = this.findSimilarLearnedWord(m, w, lang);

        if (similarWord) {
          // This might be a typo of an already learned word
          debugLog(`Word "${w}" is similar to learned word "${similarWord}". Removing "${w}" from pending.`);

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
          debugLog(`Learning new word "${w}" after ${PredictionConfig.LEARN_THRESHOLD} occurrences`);

          // Remove from pending
          m.pendingWords.delete(w);

          // Add to learned user model with count = LEARN_THRESHOLD (it was already typed 3 times)
          m.uni.set(w, PredictionConfig.LEARN_THRESHOLD);
          m.wordRank.set(w, PredictionConfig.LEARN_THRESHOLD);
          m.lastUsed.set(w, now);

          // Morphology: link form to lemma (Russian only)
          if (lang === 'ru') {
            const lemma = this.guessLemma(w, lang);
            if (lemma !== w) {
              this.linkFormToLemma(m, w, lemma);
            }
          }

          // Also add to bigram/trigram if context exists
          this.updateBigramTrigram(m, w, prev, prev2, now);
        }
      } else {
        // Still pending, increment count
        m.pendingWords.set(w, newPendingCount);

        // Update n-grams for pending words IF >= PENDING_NGRAM_THRESHOLD (2)
        // This allows context predictions to work quickly, but avoids polluting with single typos
        if (newPendingCount >= PredictionConfig.PENDING_NGRAM_THRESHOLD) {
          this.updateBigramTrigram(m, w, prev, prev2, now);

          // Track basic stats for pending words
          const rank = m.wordRank.get(w) ?? 0;
          m.wordRank.set(w, rank + 1);
          m.lastUsed.set(w, now);

          debugLog(`Word "${w}" pending (${newPendingCount}) - n-grams updated`);
        } else {
          debugLog(`Word "${w}" pending (${newPendingCount}) - waiting for threshold`);
        }

        // Prune pending words if too many (rate limiting)
        if (m.pendingWords.size > PredictionConfig.MAX_PENDING_PER_LANGUAGE) {
          this.prunePendingWords(m);
        }
      }
    }

    // Auto-save periodically (debounced)
    this.saveCounter++;
    if (this.saveCounter >= PredictionConfig.SAVE_THRESHOLD) {
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

  /**
   * Advanced anti-garbage filter (FIXED: removed false positives)
   * BUG FIX: Removed /^(.).?\1.?\1/i pattern that blocked normal words like "обо", "мама", "папа"
   */
  private isGarbage(word: string, lang: string): boolean {
    if (!word || word.length === 0) {
      debugLog(`  [isGarbage] "${word}" -> TRUE (empty)`);
      return true;
    }

    // 1. Too short (except allowed single chars)
    if (word.length < PredictionConfig.MIN_WORD_LENGTH) {
      debugLog(`  [isGarbage] "${word}" -> TRUE (too short)`);
      return true;
    }

    // 2. Allow common single-char words for Russian
    if (lang === 'ru' && word.length === 1) {
      const allowedSingleChars = ['я', 'в', 'и', 'к', 'о', 'с', 'у', 'а'];
      if (!allowedSingleChars.includes(word.toLowerCase())) {
        debugLog(`  [isGarbage] "${word}" -> TRUE (single char not in allowed list)`);
        return true;
      }
    }

    // 3-6. Combined character analysis (single pass for performance)
    let digitCount = 0;
    let nonLetterDigitCount = 0;
    const uniqueChars = new Set<string>();
    const wordLower = word.toLowerCase();

    for (let i = 0; i < word.length; i++) {
      const char = word[i];
      const charLower = wordLower[i];

      // Check whitespace (early exit)
      if (char === ' ' || char === '\t' || char === '\n') {
        debugLog(`  [isGarbage] "${word}" -> TRUE (contains whitespace)`);
        return true;
      }

      // Count digits
      if (char >= '0' && char <= '9') {
        digitCount++;
      }
      // Count non-letter, non-digit characters (symbols/punctuation)
      // Simple check: if not letter and not digit, it's a symbol
      else if (!((char >= 'a' && char <= 'z') || (char >= 'A' && char <= 'Z') ||
                 (char >= 'а' && char <= 'я') || (char >= 'А' && char <= 'Я') ||
                 (char >= '0' && char <= '9') || char === 'ё' || char === 'Ё')) {
        nonLetterDigitCount++;
      }

      uniqueChars.add(charLower);
    }

    // Check digit ratio
    if (digitCount / word.length > PredictionConfig.MAX_DIGIT_RATIO) {
      debugLog(`  [isGarbage] "${word}" -> TRUE (too many digits: ${digitCount}/${word.length})`);
      return true;
    }

    // Check symbol ratio
    if (nonLetterDigitCount / word.length > PredictionConfig.MAX_SYMBOL_RATIO) {
      debugLog(`  [isGarbage] "${word}" -> TRUE (too many symbols: ${nonLetterDigitCount}/${word.length})`);
      return true;
    }

    // Check unique characters
    const uniqueCount = uniqueChars.size;

    // Allow short words (length <= 3) to have only 1 unique char (interdictions: "ааа", "ммм")
    // For longer words, require at least 2 unique chars
    const minUniqueForLength = word.length <= 3 ? 1 : PredictionConfig.MIN_UNIQUE_CHARS;
    if (uniqueCount < minUniqueForLength) {
      debugLog(`  [isGarbage] "${word}" -> TRUE (unique chars ${uniqueCount} < ${minUniqueForLength})`);
      return true; // "aaaaaa" (length > 3, uniqueChars = 1)
    }

    // Check ratio of unique chars to total length (more lenient: 0.75 instead of 0.6)
    // This allows interdictions like "ааа" (repeatRatio = 0.67) to pass
    const repeatRatio = 1 - (uniqueCount / word.length);
    if (word.length > 3 && repeatRatio > 0.75) {
      debugLog(`  [isGarbage] "${word}" -> TRUE (repeat ratio ${repeatRatio.toFixed(2)} > 0.75)`);
      return true; // "aaaabbbb" (too repetitive, but only for longer words)
    }

    // 7. Keyboard mashing patterns (FIXED: removed false positive pattern)
    const mashingPatterns = [
      // QWERTY keyboard rows (full sequences only)
      /^qwerty/i, /^asdfgh/i, /^zxcvbn/i,
      // Russian keyboard rows (full sequences only)
      /^йцукен/i, /^фывапр/i, /^ячсмит/i,
      // REMOVED: /^(.).?\1.?\1/i - this blocked normal words like "обо", "око", "мама", "папа"
      // Real keyboard mashing: same char repeated 5+ times
      /^(.)\1{4,}/, // "aaaaa", "11111"
    ];

    for (const pattern of mashingPatterns) {
      if (pattern.test(word)) {
        debugLog(`  [isGarbage] "${word}" -> TRUE (matches mashing pattern: ${pattern})`);
        return true;
      }
    }

    return false;
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

    // Ensure BaseLexicon is loaded (lazy loading) - MUST BE BEFORE isBaseWord() check
    await this.ensureBaseLexiconLoaded(lang);
    const baseLexicon = this.getBaseLexicon(lang);

    // Don't autocorrect if word is already learned OR in base dictionary
    if (this.isWordLearned(m, wordLower) || this.isBaseWord(lang, wordLower)) return null;

    // Find similar word from user learned words OR base lexicon
    const threshold = Math.min(2, Math.floor(word.length * 0.3));
    let bestMatch: string | null = null;
    let bestDistance = threshold + 1;
    let bestRank = 0;

    // Priority 1: Check user learned words (small set, full scan is OK)
    m.uni.forEach((count, learnedWord) => {
      // Only consider truly learned words (count >= LEARN_THRESHOLD), not blocked
      if (count >= PredictionConfig.LEARN_THRESHOLD && !m.blockedWords.has(learnedWord) &&
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

  /**
   * IMPROVED PREDICTION ENGINE
   * - BaseLexicon optional (works without it)
   * - Pending words support with smart thresholds
   * - Preserved Russian morphology expansion
   * - Advanced scoring with PredictionConfig weights
   * - Anti-garbage filtering
   */
  async predict(lang: string, prev: string | null, prefix: string | null, k: number, prev2: string | null = null): Promise<string[]> {
    debugLog(`=== PREDICT START ===`);
    debugLog(`Lang: ${lang}, Prefix: "${prefix}", Prev: "${prev}", Prev2: "${prev2}", k: ${k}`);

    const m = this.ensure(lang);
    const now = Date.now();
    const prefixLower = prefix ? prefix.toLowerCase() : null;
    const prevLower = prev ? prev.toLowerCase() : null;
    const prev2Lower = prev2 ? prev2.toLowerCase() : null;
    const hasPrefix = prefixLower && prefixLower.length > 0;

    debugLog(`Model stats: uni=${m.uni.size}, pending=${m.pendingWords.size}, bi=${m.bi.size}, tri=${m.tri.size}`);

    // Ensure BaseLexicon is loaded if available (non-blocking)
    await this.ensureBaseLexiconLoaded(lang);
    const baseLexicon = this.getBaseLexicon(lang);
    debugLog(`BaseLexicon: ${baseLexicon ? (baseLexicon.isLoaded() ? 'LOADED' : 'NOT LOADED') : 'NOT AVAILABLE'}`);

    // Unified candidate pool (deduplication via Map)
    const candidates = new Map<string, RankedWord>();

    // Helper: Add candidate with deduplication
    const addCandidate = (word: string, count: number, score: number): void => {
      if (m.blockedWords.has(word)) {
        debugLog(`  ↳ BLOCKED: "${word}"`);
        return;
      }
      if (this.isGarbage(word, lang)) {
        debugLog(`  ↳ GARBAGE: "${word}"`);
        return;
      }
      const existing = candidates.get(word);
      if (!existing || score > existing.score) {
        debugLog(`  ↳ ADD: "${word}" (score: ${score.toFixed(1)}, count: ${count})`);
        candidates.set(word, { w: word, c: count, score: score });
      }
    };

    // Helper: Prefix filter
    const matchesPrefix = (word: string): boolean => {
      return !hasPrefix || word.startsWith(prefixLower);
    };

    // Helper: Unified scoring (improved with PredictionConfig)
    const calculateScore = (word: string, contextCount: number, wordCount: number, isPending: boolean, baseFreq: number): number => {
      const userRank = m.wordRank.get(word) ?? 0;
      const lastUse = m.lastUsed.get(word) ?? 0;

      // Recency bonus
      let recencyBonus = 0;
      if (lastUse > 0) {
        const recency = now - lastUse;
        const recencyHours = recency / (1000 * 60 * 60);
        recencyBonus = Math.max(0, 50 - (recencyHours * 0.5));
      }

      // Proportional prefix boost
      const prefixBoost = PredictionConfig.calculatePrefixBoost(word, prefixLower ?? '');

      // Scoring with improved weights
      let score = 0;
      if (contextCount > 0) {
        score += contextCount * PredictionConfig.SCORE_CONTEXT_WEIGHT;
      }
      if (!isPending && wordCount >= PredictionConfig.LEARN_THRESHOLD) {
        score += userRank * PredictionConfig.SCORE_LEARNED_WEIGHT;
      } else if (isPending) {
        score += wordCount * PredictionConfig.SCORE_PENDING_WEIGHT;
      }
      score += baseFreq * PredictionConfig.SCORE_BASE_WEIGHT;
      score += recencyBonus * PredictionConfig.SCORE_RECENCY_WEIGHT;
      score += prefixBoost;

      return score;
    };

    // SOURCE 1: Trigrams (highest context)
    debugLog(`SOURCE 1: Trigrams (prev2="${prev2Lower}", prev="${prevLower}")`);
    let source1Count = 0;
    if (prev2Lower && prevLower) {
      const triKeyStr = this.triKey(prev2Lower, prevLower);
      const triMap = m.tri.get(triKeyStr);
      if (triMap) {
        debugLog(`  Found trigram map with ${triMap.size} entries`);
        for (const [word, contextCount] of triMap.entries()) {
          // Early exit when enough high-quality candidates
          if (candidates.size >= k * 3) break;

          if (matchesPrefix(word)) {
            const wordCount = m.uni.get(word) ?? m.pendingWords.get(word) ?? 0;
            const isPending = !m.uni.has(word) && m.pendingWords.has(word);
            const baseFreq = baseLexicon?.getFrequency(word) ?? 0;

            // For context: allow pending >= PENDING_CONTEXT_THRESHOLD
            if (!isPending || (m.pendingWords.get(word) ?? 0) >= PredictionConfig.PENDING_CONTEXT_THRESHOLD) {
              const score = calculateScore(word, contextCount, wordCount, isPending, baseFreq);
              addCandidate(word, wordCount, score);
              source1Count++;
            }
          }
        }
      } else {
        debugLog(`  No trigram map found for key "${triKeyStr}"`);
      }
    }
    debugLog(`SOURCE 1: Added ${source1Count} candidates. Total: ${candidates.size}`);

    // SOURCE 2: Bigrams
    debugLog(`SOURCE 2: Bigrams (prev="${prevLower}")`);
    let source2Count = 0;
    if (prevLower) {
      const biMap = m.bi.get(prevLower);
      if (biMap) {
        debugLog(`  Found bigram map with ${biMap.size} entries`);
        for (const [word, contextCount] of biMap.entries()) {
          // Early exit when enough high-quality candidates
          if (candidates.size >= k * 3) break;

          if (matchesPrefix(word)) {
            const wordCount = m.uni.get(word) ?? m.pendingWords.get(word) ?? 0;
            const isPending = !m.uni.has(word) && m.pendingWords.has(word);
            const baseFreq = baseLexicon?.getFrequency(word) ?? 0;

            // For context: allow pending >= PENDING_CONTEXT_THRESHOLD
            if (!isPending || (m.pendingWords.get(word) ?? 0) >= PredictionConfig.PENDING_CONTEXT_THRESHOLD) {
              const score = calculateScore(word, contextCount, wordCount, isPending, baseFreq);
              addCandidate(word, wordCount, score);
              source2Count++;
            }
          }
        }
      } else {
        debugLog(`  No bigram map found for prev "${prevLower}"`);
      }
    }
    debugLog(`SOURCE 2: Added ${source2Count} candidates. Total: ${candidates.size}`);

    // SOURCE 3: Learned words (unigram)
    debugLog(`SOURCE 3: Learned words (uni map size: ${m.uni.size})`);
    let source3Count = 0;
    let source3Checked = 0;
    const maxSource3 = 200; // Limit iterations for performance

    for (const entry of m.uni.entries()) {
      if (++source3Checked > maxSource3) break; // Performance: limit scan

      const word = entry[0];
      const count = entry[1];

      if (count >= PredictionConfig.LEARN_THRESHOLD && matchesPrefix(word)) {
        const baseFreq = baseLexicon?.getFrequency(word) ?? 0;
        const score = calculateScore(word, 0, count, false, baseFreq);
        addCandidate(word, count, score);
        source3Count++;
      }
    }
    debugLog(`SOURCE 3: Added ${source3Count} candidates (checked ${source3Checked}). Total: ${candidates.size}`);

    // SOURCE 4: Pending words (with smart thresholds)
    debugLog(`SOURCE 4: Pending words (pending map size: ${m.pendingWords.size}, threshold: ${hasPrefix ? 1 : 2})`);
    let source4Count = 0;
    let source4Checked = 0;
    const maxSource4 = 100; // Limit iterations for performance

    for (const entry of m.pendingWords.entries()) {
      if (++source4Checked > maxSource4) break; // Performance: limit scan

      const word = entry[0];
      const count = entry[1];

      if (matchesPrefix(word)) {
        // With prefix: show after 1 occurrence
        // Without prefix: show after 2 occurrences
        const threshold = hasPrefix
          ? PredictionConfig.PENDING_PREFIX_THRESHOLD
          : PredictionConfig.PENDING_CONTEXT_THRESHOLD;

        if (count >= threshold) {
          const baseFreq = baseLexicon?.getFrequency(word) ?? 0;
          const score = calculateScore(word, 0, count, true, baseFreq);
          addCandidate(word, count, score);
          source4Count++;
        }
      }
    }
    debugLog(`SOURCE 4: Added ${source4Count} candidates (checked ${source4Checked}). Total: ${candidates.size}`);

    // SOURCE 5: BaseLexicon (if available) - skip if we already have enough high-quality candidates
    debugLog(`SOURCE 5: BaseLexicon`);
    let source5Count = 0;
    const enoughCandidates = candidates.size >= k * 3; // Early exit: skip BaseLexicon if we have 3x needed candidates
    if (!enoughCandidates && baseLexicon && baseLexicon.isLoaded()) {
      if (hasPrefix) {
        // Prefix search (fast O(log n))
        const lexEntries = baseLexicon.findByPrefix(prefixLower, k * 3);
        debugLog(`  Found ${lexEntries.length} entries by prefix "${prefixLower}"`);
        lexEntries.forEach(entry => {
          const score = calculateScore(entry.word, 0, 0, false, entry.freq);
          addCandidate(entry.word, 0, score);
          source5Count++;
        });
      } else {
        // Top frequent words (fallback)
        const topWords = baseLexicon.getTopFrequentWords();
        debugLog(`  Using top ${Math.min(50, topWords.length)} frequent words`);
        for (const word of topWords.slice(0, 50)) {
          const freq = baseLexicon.getFrequency(word);
          const score = calculateScore(word, 0, 0, false, freq);
          addCandidate(word, 0, score);
          source5Count++;
        }
      }
    } else {
      debugLog(`  BaseLexicon not available or not loaded`);
    }
    debugLog(`SOURCE 5: Added ${source5Count} candidates. Total: ${candidates.size}`);

    // SOURCE 5.5: Generate Russian verb forms from BaseLexicon lemmas (morphological expansion)
    // Only when we have prefix and need more candidates
    debugLog(`SOURCE 5.5: Russian verb forms generation`);
    let source5_5Count = 0;
    if (lang === 'ru' && hasPrefix && candidates.size < k * 2 && baseLexicon && baseLexicon.isLoaded()) {
      // Get broader lemma pool using shorter prefix for better coverage
      const searchPrefix = prefixLower.substring(0, Math.min(3, prefixLower.length));
      const lemmaPool = baseLexicon.findByPrefix(searchPrefix, 200); // Limit to 200 lemmas
      debugLog(`  Checking ${lemmaPool.length} lemmas from BaseLexicon with prefix "${searchPrefix}"`);

      const generatedWords = new Set<string>(); // Track generated words to avoid duplicates
      let lemmasProcessed = 0;
      const MAX_LEMMAS_TO_PROCESS = 200;
      const MAX_FORMS_PER_LEMMA = 6;

      for (const entry of lemmaPool) {
        if (lemmasProcessed >= MAX_LEMMAS_TO_PROCESS) break;
        lemmasProcessed++;

        const lemma = entry.word;

        // Skip if lemma is blocked
        if (baseLexicon.isBlocked(lemma) || m.blockedWords.has(lemma)) {
          continue;
        }

        // Generate verb forms
        const forms = generateRussianVerbForms(lemma);
        if (forms.length === 0) continue; // Not a verb or unknown pattern

        let formsAdded = 0;
        for (const form of forms) {
          if (formsAdded >= MAX_FORMS_PER_LEMMA) break;

          // Only add forms that match the full prefix and aren't already candidates
          if (form.startsWith(prefixLower) && !candidates.has(form) && !generatedWords.has(form)) {
            // Check if form is not blocked
            if (!baseLexicon.isBlocked(form) && !m.blockedWords.has(form)) {
              // Score: use lemma frequency * 0.7 to make them competitive but not dominant
              const lemmaFreq = entry.freq;
              const score = calculateScore(form, 0, 0, false, lemmaFreq) * 0.7;

              addCandidate(form, 0, score);
              generatedWords.add(form);
              source5_5Count++;
              formsAdded++;

              debugLog(`  Generated form "${form}" from lemma "${lemma}" (score: ${score.toFixed(1)})`);
            }
          }
        }
      }

      debugLog(`SOURCE 5.5: Generated ${source5_5Count} verb forms from ${lemmasProcessed} lemmas. Total: ${candidates.size}`);
    } else {
      debugLog(`  Skipping verb generation: lang=${lang}, hasPrefix=${hasPrefix}, candidates=${candidates.size}`);
    }

    // Convert to array for sorting
    let list: RankedWord[] = Array.from(candidates.values());
    debugLog(`Candidates before morphology: ${list.length}`);

    // MORPHOLOGICAL EXPANSION for Russian (PRESERVED!)
    if (lang === 'ru' && list.length < k * 5) { // Only expand if we need more candidates
      debugLog(`MORPHOLOGY: Expanding Russian word forms`);
      const seenLemmas = new Set<string>();
      const expandedList: RankedWord[] = [...list];
      const expandedWords = new Set<string>(list.map(x => x.w)); // O(1) lookup instead of O(n) find()
      let morphAddedCount = 0;
      const MAX_FORMS_PER_LEMMA = 3; // Limit forms per lemma for performance

      for (const item of list) {
        const lemma = m.lemmaMap.get(item.w) ?? item.w;
        if (!seenLemmas.has(lemma)) {
          seenLemmas.add(lemma);
          const forms = m.formsByLemma.get(lemma);
          if (forms) {
            debugLog(`  Lemma "${lemma}" has ${forms.size} forms`);
            let formsAdded = 0;
            for (const form of forms) {
              if (formsAdded >= MAX_FORMS_PER_LEMMA) break; // Limit forms per lemma

              if (matchesPrefix(form) && !expandedWords.has(form)) {
                const formCount = m.uni.get(form) ?? 0;
                if (formCount >= PredictionConfig.LEARN_THRESHOLD) {
                  const baseFreq = baseLexicon?.getFrequency(form) ?? 0;
                  const morphScore = calculateScore(form, 0, formCount, false, baseFreq) * PredictionConfig.SCORE_MORPHOLOGY_WEIGHT;
                  expandedList.push({ w: form, c: formCount, score: morphScore });
                  expandedWords.add(form); // Add to Set for O(1) duplicate checking
                  debugLog(`    Added form "${form}" (score: ${morphScore.toFixed(1)})`);
                  morphAddedCount++;
                  formsAdded++;
                }
              }
            }
          }
        }
      }

      list = expandedList;
      debugLog(`MORPHOLOGY: Added ${morphAddedCount} forms. Total: ${list.length}`);
    } else if (lang === 'ru') {
      debugLog(`MORPHOLOGY: Skipping expansion, have enough candidates (${list.length})`);
    }

    // Sort by score (highest first)
    list.sort((a, b) => b.score - a.score);
    debugLog(`Sorted candidates (top 5): ${list.slice(0, 5).map(x => `"${x.w}"(${x.score.toFixed(1)})`).join(', ')}`);

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
        debugLog(`  Final[${out.length}]: "${w}"`);
      }
    }

    // Fill with empty strings if needed
    while (out.length < k) {
      out.push('');
    }

    debugLog(`=== PREDICT END === Result: [${out.map(x => `"${x}"`).join(', ')}]`);
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
      blockedWords: Array.from(m.blockedWords),
      lemmaMap: Array.from(m.lemmaMap.entries()),
      formsByLemma: Array.from(m.formsByLemma.entries()).map((entry) => [entry[0], Array.from(entry[1])])
    };
  }

  /**
   * Validate SerializedModel structure to prevent crashes from corrupted data
   */
  private isValidSerializedModel(model: Object | null): boolean {
    if (!model || typeof model !== 'object') {
      return false;
    }

    const m = model as Record<string, Object>;

    // Check required fields are arrays
    if (!Array.isArray(m['uni']) || !Array.isArray(m['bi']) || !Array.isArray(m['tri']) ||
        !Array.isArray(m['wordRank']) || !Array.isArray(m['lastUsed'])) {
      return false;
    }

    const uniArray = m['uni'] as Object[];
    const biArray = m['bi'] as Object[];
    const triArray = m['tri'] as Object[];
    const wordRankArray = m['wordRank'] as Object[];
    const lastUsedArray = m['lastUsed'] as Object[];

    // Validate uni entries: [string, number]
    for (const entry of uniArray) {
      if (!Array.isArray(entry) || entry.length !== 2 ||
          typeof entry[0] !== 'string' || typeof entry[1] !== 'number') {
        return false;
      }
    }

    // Validate bi entries: [string, [string, number][]]
    for (const entry of biArray) {
      if (!Array.isArray(entry) || entry.length !== 2 ||
          typeof entry[0] !== 'string' || !Array.isArray(entry[1])) {
        return false;
      }
    }

    // Validate tri entries: [string, [string, number][]]
    for (const entry of triArray) {
      if (!Array.isArray(entry) || entry.length !== 2 ||
          typeof entry[0] !== 'string' || !Array.isArray(entry[1])) {
        return false;
      }
    }

    // Validate wordRank and lastUsed
    for (const entry of wordRankArray) {
      if (!Array.isArray(entry) || entry.length !== 2 ||
          typeof entry[0] !== 'string' || typeof entry[1] !== 'number') {
        return false;
      }
    }

    for (const entry of lastUsedArray) {
      if (!Array.isArray(entry) || entry.length !== 2 ||
          typeof entry[0] !== 'string' || typeof entry[1] !== 'number') {
        return false;
      }
    }

    // Optional fields
    if (m['pendingWords'] !== undefined && !Array.isArray(m['pendingWords'])) {
      return false;
    }

    if (m['blockedWords'] !== undefined && !Array.isArray(m['blockedWords'])) {
      return false;
    }

    // Optional morphology fields
    if (m['lemmaMap'] !== undefined && !Array.isArray(m['lemmaMap'])) {
      return false;
    }

    if (m['formsByLemma'] !== undefined && !Array.isArray(m['formsByLemma'])) {
      return false;
    }

    return true;
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
      blockedWords: new Set(data.blockedWords || []),
      lemmaMap: new Map(data.lemmaMap || []),
      formsByLemma: new Map((data.formsByLemma || []).map((entry) => [entry[0], new Set(entry[1])]))
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
    }, PredictionConfig.SAVE_DEBOUNCE_MS);
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

      // Parse JSON with validation
      let allModels: Record<string, Object>;
      try {
        allModels = JSON.parse(data as string) as Record<string, Object>;
      } catch (parseErr) {
        const err = parseErr as Error;
        console.error('OpenBoard: Failed to parse prediction data JSON:', err.message);
        console.warn('OpenBoard: Starting with empty model');
        return;
      }

      // Validate and import each language model
      let loadedCount = 0;
      let skippedCount = 0;

      Object.keys(allModels).forEach((lang: string) => {
        const model = allModels[lang] as Object | null;

        if (this.isValidSerializedModel(model)) {
          this.importModel(lang, model as SerializedModel);
          loadedCount++;
        } else {
          console.warn(`OpenBoard: Skipping invalid model data for language '${lang}'`);
          skippedCount++;
        }
      });

      console.info(`OpenBoard: Loaded ${loadedCount} language(s), skipped ${skippedCount} invalid`);
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

  /**
   * Load base dictionaries - now deprecated, BaseLexicon loads on-demand
   * This method is kept for API compatibility but does nothing
   * BaseLexicon will be loaded lazily via warmup() or first predict()/autocorrect() call
   */
  async loadDictionaries(): Promise<void> {
    console.info('OpenBoard: BaseLexicon will be loaded on-demand via warmup() or first use');
    console.info('OpenBoard: Call warmup(lang) after keyboard initialization to preload lexicon');
  }
}

const predictionModel = new PredictionModel();

export default predictionModel;
