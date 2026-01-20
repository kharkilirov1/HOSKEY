/**
 * HOSKEY Native Dictionary Module
 * Type declarations for N-API bridge
 */

/**
 * Error types for autocorrection
 */
export declare enum ErrorType {
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
export declare interface SuggestResult {
  word: string;
  score: number;
  errorType: ErrorType;
}

/**
 * Dictionary statistics
 */
export declare interface DictStats {
  wordCount: number;
  memoryUsage: number;
}

/**
 * Key bounds for swipe recognition
 */
export declare interface KeyBounds {
  key: string;
  centerX: number;
  centerY: number;
  width: number;
  height: number;
}

/**
 * Touch point for swipe recognition
 */
export declare interface TouchPoint {
  x: number;
  y: number;
  timestamp: number;
}

/**
 * Swipe recognition result
 */
export declare interface SwipeResult {
  bestWord: string;
  alternatives: string[];
  confidence: number;
  rawSequence: string;
}

/**
 * Native Dictionary Module Interface
 */
declare interface NativeDictModule {
  /**
   * Load binary dictionary from file path
   * @param path - Path to dictionary file (.dict or .txt)
   * @returns true if loaded successfully
   */
  loadDictionary(path: string): boolean;

  /**
   * Check if word exists in dictionary
   * @param word - Word to check
   * @returns true if word exists
   */
  contains(word: string): boolean;

  /**
   * Get frequency of word in dictionary
   * @param word - Word to lookup
   * @returns frequency value (0-255) or 0 if not found
   */
  getFrequency(word: string): number;

  /**
   * Get word suggestions for input
   * @param prefix - Input prefix to search
   * @param limit - Maximum number of results
   * @returns Array of suggestion results
   */
  getSuggestions(prefix: string, limit: number): SuggestResult[];

  /**
   * Find best autocorrection for word
   * @param word - Word to autocorrect
   * @param threshold - Confidence threshold (default 0.185)
   * @returns Best autocorrection or null if none found
   */
  findAutocorrection(word: string, threshold: number): SuggestResult | null;

  /**
   * Initialize keyboard proximity information
   * @param layout - Keyboard layout name ('qwerty', 'ru', 'azerty', 'qwertz')
   * @param keyWidth - Key width in pixels
   * @param keyHeight - Key height in pixels
   * @returns true if initialized successfully
   */
  setProximityInfo(layout: string, keyWidth: number, keyHeight: number): boolean;

  /**
   * Get dictionary statistics
   * @returns Dictionary stats object
   */
  getStats(): DictStats;

  /**
   * Unload dictionary and free memory
   */
  unload(): void;

  /**
   * Set keyboard layout for swipe recognition
   * @param keys - Array of key bounds
   * @returns true if set successfully
   */
  setSwipeKeyboardLayout(keys: Array<KeyBounds>): boolean;

  /**
   * Process swipe path and return recognized word
   * @param points - Array of touch points
   * @returns Swipe result or null if invalid
   */
  processSwipePath(points: Array<TouchPoint>): SwipeResult | null;
}

/**
 * Default export - Native Dictionary Module
 */
declare const nativeDict: NativeDictModule;
export default nativeDict;

// Named exports for standalone function usage
export declare function loadDictionary(path: string): boolean;
export declare function contains(word: string): boolean;
export declare function getFrequency(word: string): number;
export declare function getSuggestions(prefix: string, limit: number): SuggestResult[];
export declare function findAutocorrection(word: string, threshold: number): SuggestResult | null;
export declare function setProximityInfo(layout: string, keyWidth: number, keyHeight: number): boolean;
export declare function getStats(): DictStats;
export declare function unload(): void;
export declare function setSwipeKeyboardLayout(keys: Array<KeyBounds>): boolean;
export declare function processSwipePath(points: Array<TouchPoint>): SwipeResult | null;
