/**
 * Native Dictionary Wrapper for HOSKEY
 * Provides ArkTS interface to native C++ dictionary engine
 *
 * NOTE: Native module (libnative_dict.so) requires C++ compilation.
 * If native is not available, predictions will use BaseLexicon (50k words).
 *
 * Features (when native is available):
 * - High-performance prefix search (<1ms)
 * - Weighted autocorrection (<5ms)
 * - Proximity-aware suggestions
 */

import { common } from '@kit.AbilityKit';
import { fileIo } from '@kit.CoreFileKit';

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
 * Stub implementation - returns defaults when native module is not available.
 * Predictions will use BaseLexicon (50k words) as fallback.
 */
class NativeDictionary {
  private context: common.Context | null = null;
  private currentLanguage: string = '';
  private isLoaded: boolean = false;
  private nativeAvailable: boolean = false;

  constructor() {
    // Native module requires C++ compilation
    // For now, this is a stub - predictions use BaseLexicon
    this.nativeAvailable = false;
    console.info('[NativeDictionary] Native module not compiled - using BaseLexicon fallback');
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
    // Native module not available - BaseLexicon will be used
    console.info(`[NativeDictionary] Native not available for '${lang}', using BaseLexicon`);
    return false;
  }

  /**
   * Check if word exists in dictionary
   */
  contains(word: string): boolean {
    return false;
  }

  /**
   * Get word frequency
   */
  getFrequency(word: string): number {
    return 0;
  }

  /**
   * Get word suggestions
   * @param prefix - Input prefix
   * @param limit - Maximum results
   */
  getSuggestions(prefix: string, limit: number = 10): SuggestResult[] {
    return [];
  }

  /**
   * Find autocorrection for word
   * @param word - Word to correct
   * @param threshold - Confidence threshold (default 0.185)
   */
  findAutocorrection(word: string, threshold: number = 0.185): SuggestResult | null {
    return null;
  }

  /**
   * Get dictionary statistics
   */
  getStats(): DictStats {
    return {
      wordCount: 0,
      memoryUsage: 0,
      isLoaded: false,
      language: ''
    };
  }

  /**
   * Unload dictionary and free memory
   */
  unload(): void {
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
}

// Export singleton instance
const nativeDictionary = new NativeDictionary();
export default nativeDictionary;
