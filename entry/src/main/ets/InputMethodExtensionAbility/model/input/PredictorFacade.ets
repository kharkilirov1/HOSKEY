/**
 * PredictorFacade - Native Dictionary Wrapper for IPredictor
 *
 * Responsibilities:
 * - Implement IPredictor interface using NativeDictionary directly
 * - Add LRU caching for frequent queries (token prefix cache)
 * - Handle user word learning via native API (bigram-aware)
 * - Provide autocorrection through native engine
 *
 * Performance optimizations:
 * - Direct native calls (<1ms for suggestions)
 * - Cache hit rate: ~60-80% for typical typing
 * - Cache size: 500 entries (balanced between RAM and hit rate)
 */

import { IPredictor } from './InputTypes';
import nativeDictionary, { SuggestResult } from '../NativeDictionary';
import { SwipeEngine } from '../SwipeEngine';
import { LRUCache } from '../LRUCache';

// Import native module directly for learning functions
import nativeDict from 'libnative_dict.so';

/**
 * Cache key format: "lang:prevWord:token"
 * Example: "ru:привет:при" → ["привет", "пример", "принести"]
 */
function makeCacheKey(lang: string, prevWord: string, token: string): string {
  return `${lang}:${prevWord}:${token}`;
}

interface CacheStats {
  size: number;
  hits: number;
  misses: number;
  hitRate: number;
}

export class PredictorFacade implements IPredictor {
  private swipeEngine: SwipeEngine;
  // LRU cache for predictions: key → string[]
  private predictionCache: LRUCache<string, string[]>;
  // Performance tracking
  private cacheHits: number = 0;
  private cacheMisses: number = 0;
  // Current language (for learning)
  private currentLang: string = '';

  constructor(swipeEngine: SwipeEngine) {
    this.swipeEngine = swipeEngine;

    // Cache size: 500 entries
    // Typical entry: ~100 bytes (3 words × ~30 chars)
    // Total RAM: ~50KB (acceptable overhead)
    this.predictionCache = new LRUCache<string, string[]>(500);
  }

  /**
   * Set current language (needed for user dict path)
   */
  setLanguage(lang: string): void {
    if (this.currentLang !== lang) {
      this.clearCache();  // Clear cache on language switch
      this.currentLang = lang;
    }
  }

  /**
   * Get word predictions (async, cached)
   * Uses NativeDictionary.getSuggestions() directly
   *
   * @param lang - Current language code
   * @param prevWord - Previous word for bigram context
   * @param currentToken - Current partial word being typed
   * @param maxResults - Maximum suggestions to return (default: 3)
   * @returns Promise<string[]> - Array of predicted words
   */
  async predict(
    lang: string,
    prevWord: string,
    currentToken: string,
    maxResults: number = 3
  ): Promise<string[]> {
    // Update current language
    this.currentLang = lang;

    // Return empty if no token
    if (!currentToken || currentToken.length === 0) {
      return [];
    }

    // Normalize inputs for cache consistency
    const normalizedToken = currentToken.toLowerCase();
    const normalizedPrev = prevWord.toLowerCase();

    // Check cache first
    const cacheKey = makeCacheKey(lang, normalizedPrev, normalizedToken);
    const cached = this.predictionCache.get(cacheKey);

    if (cached !== undefined) {
      this.cacheHits++;
      return cached || [];
    }

    this.cacheMisses++;

    try {
      // Check if native dictionary is available and loaded
      if (!nativeDictionary.isNativeAvailable() || !nativeDictionary.isDictionaryLoaded()) {
        console.warn('[PredictorFacade] Native dictionary not ready');
        return [];
      }

      // Get suggestions with learned boost
      // Call native getSuggestions (which includes user dictionary results)
      const results: SuggestResult[] = nativeDictionary.getSuggestions(currentToken, maxResults * 2);

      // Apply learned boost from bigram context
      const boostedResults = results.map((r: SuggestResult) => {
        const boost = nativeDict.getLearnedBoost(r.word, normalizedPrev) as number;
        return {
          word: r.word,
          score: r.score + (boost / 100.0),  // Normalize boost to 0-1 range addition
          errorType: r.errorType
        };
      });

      // Sort by boosted score
      boostedResults.sort((a, b) => b.score - a.score);

      // Extract words from results (already sorted)
      const predictions: string[] = boostedResults
        .slice(0, maxResults)
        .map((r) => r.word);

      // Store in cache
      this.predictionCache.set(cacheKey, predictions);

      return predictions;
    } catch (err) {
      console.error('[PredictorFacade] Prediction error:', err);
      return [];
    }
  }

  /**
   * Decode swipe path into word candidates (async)
   *
   * NOTE: Primary swipe processing is done directly in InputPipeline.processSwipeAsync()
   * using nativeDictionary.processSwipePath(). This method is kept for interface
   * compatibility but delegates to the native implementation.
   *
   * @param lang - Current language code
   * @param keyPath - Sequence of keys from swipe trace (not used, prefer TouchPoint[])
   * @param prevWord - Previous word for context scoring
   * @returns Promise<string[]> - Ranked candidate words
   */
  async decodeSwipe(
    lang: string,
    keyPath: string[],
    prevWord: string
  ): Promise<string[]> {
    try {
      // Swipe processing is handled directly in InputPipeline via
      // nativeDictionary.processSwipePath() with TouchPoint[] coordinates.
      // This method returns empty as keyPath (string[]) doesn't contain
      // the coordinate information needed for proper swipe decoding.
      // See InputPipeline.processSwipeAsync() for the actual implementation.
      console.debug('[PredictorFacade] decodeSwipe called - use InputPipeline.processSwipeAsync instead');
      return [];
    } catch (err) {
      console.error('[PredictorFacade] Swipe decode error:', err);
      return [];
    }
  }

  /**
   * Record word usage for adaptive learning
   * Updates bigram frequencies for better predictions
   *
   * @param lang - Current language code
   * @param prevWord - Previous word (for bigram context)
   * @param word - The word that was committed
   * @param prev2Word - Two words back (optional, for trigram - not used currently)
   */
  recordWord(lang: string, prevWord: string, word: string, prev2Word?: string): void {
    if (!word || word.length < 2) {
      return;
    }

    try {
      if (!nativeDictionary.isNativeAvailable()) {
        return;
      }

      // Record with bigram context
      nativeDict.addLearnedWord(word, prevWord, 1);

      // Clear cache to pick up new learned words
      this.clearCache();

      console.debug(`[PredictorFacade] recordWord("${word}", prev="${prevWord}")`);
    } catch (err) {
      console.error('[PredictorFacade] recordWord error:', err);
    }
  }

  /**
   * Find best autocorrection for a word
   *
   * @param lang - Current language code
   * @param word - Word to find correction for
   * @param threshold - Confidence threshold (0.0-1.0, default 0.185)
   * @returns Promise<string | null> - Corrected word or null if no correction
   */
  async findAutocorrection(lang: string, word: string, threshold: number = 0.185): Promise<string | null> {
    if (!word || word.length < 2) {
      return null;
    }

    try {
      if (!nativeDictionary.isNativeAvailable() || !nativeDictionary.isDictionaryLoaded()) {
        return null;
      }

      const result = nativeDictionary.findAutocorrection(word, threshold);

      if (result && result.word) {
        return result.word;
      }

      return null;
    } catch (err) {
      console.error('[PredictorFacade] findAutocorrection error:', err);
      return null;
    }
  }

  /**
   * Check if word exists in dictionary
   *
   * @param lang - Current language code
   * @param word - Word to check
   * @returns true if word is in dictionary
   */
  contains(lang: string, word: string): boolean {
    if (!word) {
      return false;
    }

    try {
      if (!nativeDictionary.isNativeAvailable() || !nativeDictionary.isDictionaryLoaded()) {
        return false;
      }

      return nativeDictionary.contains(word);
    } catch (err) {
      console.error('[PredictorFacade] contains error:', err);
      return false;
    }
  }

  /**
   * Save user dictionary to file
   * Call this periodically or on app pause
   *
   * @param path - File path to save to
   * @returns boolean - true on success
   */
  saveUserDict(path: string): boolean {
    try {
      if (!nativeDictionary.isNativeAvailable()) {
        return false;
      }

      // Save both formats
      const success1 = nativeDict.saveUserDict(path) as boolean;  // Legacy trie format
      const jsonPath = path.replace('.dict', '.json');
      const success2 = nativeDict.saveUserDictionary(jsonPath) as boolean;  // Bigram JSON

      console.info(`[PredictorFacade] saveUserDict: trie=${success1}, json=${success2}`);
      return success1 || success2;
    } catch (err) {
      console.error('[PredictorFacade] saveUserDict error:', err);
      return false;
    }
  }

  /**
   * Load user dictionary from file
   * Call this on app startup after main dictionary is loaded
   *
   * @param path - File path to load from
   * @returns boolean - true on success
   */
  loadUserDict(path: string): boolean {
    try {
      if (!nativeDictionary.isNativeAvailable()) {
        return false;
      }

      // Load both formats
      const success1 = nativeDict.loadUserDict(path) as boolean;  // Legacy trie format
      const jsonPath = path.replace('.dict', '.json');
      const success2 = nativeDict.loadUserDictionary(jsonPath) as boolean;  // Bigram JSON

      if (success1 || success2) {
        this.clearCache();  // Invalidate cache with new words
      }

      console.info(`[PredictorFacade] loadUserDict: trie=${success1}, json=${success2}`);
      return success1 || success2;
    } catch (err) {
      console.error('[PredictorFacade] loadUserDict error:', err);
      return false;
    }
  }

  /**
   * Get learned words count
   *
   * @returns number - Number of learned words
   */
  getLearnedWordsCount(): number {
    try {
      if (!nativeDictionary.isNativeAvailable()) {
        return 0;
      }

      const trieCount = nativeDict.getLearnedWordsCount() as number;
      const bigramCount = nativeDict.getBigramLearnedWordsCount() as number;
      return trieCount + bigramCount;
    } catch (err) {
      console.error('[PredictorFacade] getLearnedWordsCount error:', err);
      return 0;
    }
  }

  /**
   * Clear prediction cache (on language switch or low memory)
   */
  clearCache(): void {
    this.predictionCache.clear();
    this.cacheHits = 0;
    this.cacheMisses = 0;
  }

  /**
   * Get cache statistics (for debugging/monitoring)
   */
  getCacheStats(): CacheStats {
    const totalRequests = this.cacheHits + this.cacheMisses;
    const hitRate = totalRequests > 0 ? this.cacheHits / totalRequests : 0;

    return {
      size: this.predictionCache.getSize(),
      hits: this.cacheHits,
      misses: this.cacheMisses,
      hitRate: Math.round(hitRate * 100) / 100
    };
  }
}
