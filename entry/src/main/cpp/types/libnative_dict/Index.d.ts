/**
 * HOSKEY Native Dictionary Module
 * Type declarations for N-API bridge
 *
 * All methods validate argument types at runtime and throw
 * TypeError if invalid arguments are provided. Methods that
 * require a loaded dictionary return safe defaults (false, 0,
 * empty array, null) when dictionary is not loaded.
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
   * @throws TypeError if path is not a string
   */
  loadDictionary(path: string): boolean;

  /**
   * Check if word exists in dictionary
   * @param word - Word to check
   * @returns true if word exists, false if not found or dictionary not loaded
   * @throws TypeError if word is not a string
   */
  contains(word: string): boolean;

  /**
   * Get frequency of word in dictionary
   * @param word - Word to lookup
   * @returns frequency value (0-255) or 0 if not found or dictionary not loaded
   * @throws TypeError if word is not a string
   */
  getFrequency(word: string): number;

  /**
   * Get word suggestions for input
   * @param prefix - Input prefix to search
   * @param limit - Maximum number of results (1-100, clamped)
   * @returns Array of suggestion results, empty if dictionary not loaded
   * @throws TypeError if prefix is not a string or limit is not a number
   */
  getSuggestions(prefix: string, limit: number): SuggestResult[];

  /**
   * Find best autocorrection for word
   * @param word - Word to autocorrect
   * @param threshold - Confidence threshold (0.0-1.0, clamped)
   * @returns Best autocorrection or null if none found or dictionary not loaded
   * @throws TypeError if word is not a string or threshold is not a number
   */
  findAutocorrection(word: string, threshold: number): SuggestResult | null;

  /**
   * Initialize keyboard proximity information
   * @param layout - Keyboard layout name ('qwerty', 'ru', 'azerty', 'qwertz')
   * @param keyWidth - Key width in pixels (must be positive)
   * @param keyHeight - Key height in pixels (must be positive)
   * @returns true if initialized successfully
   * @throws TypeError if layout is not a string or dimensions are not numbers
   * @throws Error if keyWidth or keyHeight are not positive
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
   * @throws TypeError if keys is not an array
   */
  setSwipeKeyboardLayout(keys: Array<KeyBounds>): boolean;

  /**
   * Process swipe path and return recognized word
   * @param points - Array of touch points (min 5 points, min 50px distance)
   * @returns Swipe result or null if invalid or prerequisites not met
   * @throws TypeError if points is not an array
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
