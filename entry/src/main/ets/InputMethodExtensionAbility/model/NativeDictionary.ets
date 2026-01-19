/**
 * Native Dictionary Wrapper for HOSKEY
 * Provides ArkTS interface to native C++ dictionary engine
 *
 * Features (when native is available):
 * - High-performance prefix search (<1ms)
 * - Weighted autocorrection (<5ms)
 * - Proximity-aware suggestions
 */

import { common } from '@kit.AbilityKit';
import { fileIo } from '@kit.CoreFileKit';

// Import native C++ module
// @ts-ignore
import nativeDict from 'libnative_dict.so';

/**
 * Error types for autocorrection (mirrors C++ enum)
 */
export enum ErrorType {
  NOT_AN_ERROR = 0,
  MATCH_WITH_WRONG_CASE = 1,
  MATCH_WITH_ACCENT_ERROR = 2,
  PROXIMITY_CORRECTION = 3,
  INSERTION_CORRECTION = 4,
  OMISSION_CORRECTION = 5,
  TRANSPOSITION_CORRECTION = 6,
  SUBSTITUTION_CORRECTION = 7
}

/**
 * Suggestion result from native engine
 */
export interface SuggestResult {
  word: string;
  score: number;
  errorType: ErrorType;
}

/**
 * Dictionary statistics
 */
export interface DictStats {
  wordCount: number;
  memoryUsage: number;
  isLoaded: boolean;
  language: string;
}

/**
 * Native Dictionary Manager
 * Real implementation using C++ NAPI bridge
 */
class NativeDictionary {
  private context: common.Context | null = null;
  private currentLanguage: string = '';
  private isLoaded: boolean = false;
  private nativeAvailable: boolean = false;

  constructor() {
    // Check if native module is available
    try {
      if (typeof nativeDict !== 'undefined' && nativeDict !== null) {
        this.nativeAvailable = true;
        console.info('[NativeDictionary] Native C++ module loaded successfully');
      } else {
        this.nativeAvailable = false;
        console.warn('[NativeDictionary] Native module not available - using BaseLexicon fallback');
      }
    } catch (err) {
      this.nativeAvailable = false;
      console.error('[NativeDictionary] Failed to load native module:', err);
    }
  }

  /**
   * Set application context for resource access
   */
  setContext(ctx: common.Context): void {
    this.context = ctx;
  }

  /**
   * Initialize dictionary for language
   * @param lang - Language code ('ru', 'en', etc.)
   * @param keyWidth - Key width for proximity (optional)
   * @param keyHeight - Key height for proximity (optional)
   */
  async initialize(lang: string, keyWidth: number = 48, keyHeight: number = 64): Promise<boolean> {
    if (!this.nativeAvailable) {
      console.info(`[NativeDictionary] Native not available for '${lang}', using BaseLexicon`);
      return false;
    }

    if (!this.context) {
      console.error('[NativeDictionary] Context not set, cannot load dictionary');
      return false;
    }

    try {
      // Construct dictionary path
      // Dictionary files are in: entry/src/main/resources/rawfile/
      const dictPath = `${this.context.resourceDir}/entry/resources/rawfile/main_${lang}.dict`;

      console.info(`[NativeDictionary] Loading dictionary from: ${dictPath}`);

      // Check if file exists
      try {
        const stat = fileIo.statSync(dictPath);
        console.info(`[NativeDictionary] Dictionary file size: ${stat.size} bytes`);
      } catch (err) {
        console.error(`[NativeDictionary] Dictionary file not found: ${dictPath}`);
        return false;
      }

      // Load dictionary via NAPI
      const loaded = nativeDict.loadDictionary(dictPath);

      if (!loaded) {
        console.error(`[NativeDictionary] Failed to load dictionary for '${lang}'`);
        return false;
      }

      // Set proximity info for autocorrection
      const layoutName = lang === 'ru' ? 'ru' : 'qwerty';
      const proximitySet = nativeDict.setProximityInfo(layoutName, keyWidth, keyHeight);

      if (!proximitySet) {
        console.warn('[NativeDictionary] Failed to set proximity info, autocorrection may be less accurate');
      }

      this.isLoaded = true;
      this.currentLanguage = lang;

      // Get and log stats
      const stats = nativeDict.getStats();
      console.info(`[NativeDictionary] Loaded ${stats.wordCount} words for '${lang}', memory: ${stats.memoryUsage} bytes`);

      return true;
    } catch (err) {
      console.error(`[NativeDictionary] Error initializing for '${lang}':`, err);
      this.isLoaded = false;
      return false;
    }
  }

  /**
   * Check if word exists in dictionary
   */
  contains(word: string): boolean {
    if (!this.nativeAvailable || !this.isLoaded) return false;

    try {
      return nativeDict.contains(word.toLowerCase());
    } catch (err) {
      console.error('[NativeDictionary] Error in contains:', err);
      return false;
    }
  }

  /**
   * Get word frequency
   */
  getFrequency(word: string): number {
    if (!this.nativeAvailable || !this.isLoaded) return 0;

    try {
      return nativeDict.getFrequency(word.toLowerCase());
    } catch (err) {
      console.error('[NativeDictionary] Error in getFrequency:', err);
      return 0;
    }
  }

  /**
   * Get word suggestions
   * @param prefix - Input prefix
   * @param limit - Maximum results
   */
  getSuggestions(prefix: string, limit: number = 10): SuggestResult[] {
    if (!this.nativeAvailable || !this.isLoaded) return [];

    try {
      const results = nativeDict.getSuggestions(prefix.toLowerCase(), limit);
      return results || [];
    } catch (err) {
      console.error('[NativeDictionary] Error in getSuggestions:', err);
      return [];
    }
  }

  /**
   * Find autocorrection for word
   * @param word - Word to correct
   * @param threshold - Confidence threshold (default 0.185)
   */
  findAutocorrection(word: string, threshold: number = 0.185): SuggestResult | null {
    if (!this.nativeAvailable || !this.isLoaded) return null;

    try {
      const result = nativeDict.findAutocorrection(word.toLowerCase(), threshold);
      return result;
    } catch (err) {
      console.error('[NativeDictionary] Error in findAutocorrection:', err);
      return null;
    }
  }

  /**
   * Get dictionary statistics
   */
  getStats(): DictStats {
    if (!this.nativeAvailable || !this.isLoaded) {
      return {
        wordCount: 0,
        memoryUsage: 0,
        isLoaded: false,
        language: ''
      };
    }

    try {
      const stats = nativeDict.getStats();
      return {
        wordCount: stats.wordCount,
        memoryUsage: stats.memoryUsage,
        isLoaded: this.isLoaded,
        language: this.currentLanguage
      };
    } catch (err) {
      console.error('[NativeDictionary] Error in getStats:', err);
      return {
        wordCount: 0,
        memoryUsage: 0,
        isLoaded: false,
        language: ''
      };
    }
  }

  /**
   * Unload dictionary and free memory
   */
  unload(): void {
    if (this.nativeAvailable && this.isLoaded) {
      try {
        nativeDict.unload();
        console.info('[NativeDictionary] Unloaded dictionary for language:', this.currentLanguage);
      } catch (err) {
        console.error('[NativeDictionary] Error unloading:', err);
      }
    }

    this.isLoaded = false;
    this.currentLanguage = '';
  }

  /**
   * Check if native module is available
   */
  isNativeAvailable(): boolean {
    return this.nativeAvailable;
  }

  /**
   * Check if dictionary is loaded
   */
  isDictionaryLoaded(): boolean {
    return this.isLoaded;
  }

  /**
   * Set keyboard layout for swipe recognition
   * @param keys - Array of key bounds
   */
  setSwipeKeyboardLayout(keys: Array<{ key: string; centerX: number; centerY: number; width: number; height: number; }>): boolean {
    if (!this.nativeAvailable) return false;

    try {
      return nativeDict.setSwipeKeyboardLayout(keys);
    } catch (err) {
      console.error('[NativeDictionary] Error in setSwipeKeyboardLayout:', err);
      return false;
    }
  }

  /**
   * Process swipe path and return recognized word
   * @param points - Array of touch points
   */
  processSwipePath(points: Array<{ x: number; y: number; timestamp: number; }>): { bestWord: string; alternatives: string[]; confidence: number; rawSequence: string; } | null {
    if (!this.nativeAvailable) return null;

    try {
      return nativeDict.processSwipePath(points);
    } catch (err) {
      console.error('[NativeDictionary] Error in processSwipePath:', err);
      return null;
    }
  }
}

// Export singleton instance
const nativeDictionary = new NativeDictionary();
export default nativeDictionary;
