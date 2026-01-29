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
 * Dictionary and cache statistics
 */
export declare interface DictStats {
  wordCount: number;
  memoryUsage: number;
  // Cache statistics
  cacheSize: number;
  cacheHits: number;
  cacheMisses: number;
  cacheHitRate: number;
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
 * FlatTrie statistics
 */
export declare interface FlatTrieStats {
  wordCount: number;
  memoryUsage: number;
  locale: string;
}

/**
 * Trail interpolation parameters (Yandex-style)
 */
export declare interface TrailParams {
  minSamplingDistance?: number;  // Min distance between sampled points (default: 3.0)
  maxAngleRadians?: number;      // Max angle change before adding points (default: ~15°)
  maxSegmentLength?: number;     // Max segment length before subdivision (default: 20.0)
  maxInterpolationSteps?: number; // Max interpolation steps per segment (default: 10)
}

/**
 * Trail render parameters (Yandex-style)
 */
export declare interface TrailRenderParams {
  maxWidth?: number;       // Width at start of fade (default: 12.0)
  minWidth?: number;       // Width at end of fade (default: 2.0)
  widthRatio?: number;     // Overall width multiplier (default: 1.0)
  fadeStartTimeMs?: number;  // When fade begins (default: 100ms)
  fadeDurationMs?: number;   // How long fade takes (default: 400ms)
  trailColor?: number;     // ARGB color (default: 0xFF4285F4 Google blue)
  shadowEnabled?: boolean; // Enable shadow effect (default: true)
}

/**
 * Combined trail initialization parameters
 */
export declare interface TrailInitParams extends TrailParams, TrailRenderParams {}

/**
 * Input batch request for processInputBatch
 */
export declare interface InputBatchRequest {
  currentWord: string;
  prevWord?: string;
  suggestionLimit?: number;
  checkAutocorrect?: boolean;
  applyRules?: boolean;
}

/**
 * Input batch response from processInputBatch
 */
export declare interface InputBatchResponse {
  exists: boolean;
  frequency: number;
  suggestions: SuggestResult[];
  autocorrection?: SuggestResult;
  ruleApplied?: string;
}

/**
 * Batch Operations namespace
 */
export declare namespace batchOps {
  /**
   * Check if multiple words exist in dictionary
   * @param words Array of words to check
   * @returns Array of booleans (true if exists)
   */
  function batchContains(words: string[]): boolean[];

  /**
   * Get frequencies of multiple words
   * @param words Array of words
   * @returns Array of frequencies
   */
  function batchGetFrequency(words: string[]): number[];

  /**
   * Get suggestions for multiple prefixes
   * @param prefixes Array of prefixes
   * @param limit Max suggestions per prefix
   * @returns Array of suggestion arrays
   */
  function batchGetSuggestions(prefixes: string[], limit: number): SuggestResult[][];

  /**
   * Process input with all operations in one call
   * @param request Input batch request
   * @returns Combined response
   */
  function processInputBatch(request: InputBatchRequest): InputBatchResponse;

  /**
   * Load default autocorrect rules for language
   * @param language Language code ('ru', 'en')
   * @returns true if rules loaded
   */
  function loadAutocorrectRules(language: string): boolean;

  /**
   * Add custom autocorrect rule
   * @param wrong Wrong form
   * @param correct Correct form
   */
  function addAutocorrectRule(wrong: string, correct: string): void;

  /**
   * Apply autocorrect rules to a word
   * @param word Word to check
   * @returns Corrected word or original if no rule matches
   */
  function applyAutocorrectRule(word: string): string;
}

/**
 * Native Dictionary Module Interface
 */
declare interface NativeDictModule {
  /**
   * Load binary dictionary from file path asynchronously
   * @param path - Path to dictionary file (.dict or .txt)
   * @returns Promise that resolves to true if loaded successfully
   * @throws TypeError if path is not a string
   */
  loadDictionary(path: string): Promise<boolean>;

  /**
   * Load binary dictionary SYNCHRONOUSLY - no libuv/async overhead
   * Use this for faster loading when UI blocking is acceptable
   *
   * With optimized TrieNode (unordered_map instead of children_[256]):
   * - Memory: 800MB -> ~20MB
   * - Load time: ~20s -> ~1-2s (expected)
   *
   * @param path - Path to dictionary file (.dict or .txt)
   * @returns true if loaded successfully
   * @throws TypeError if path is not a string
   */
  loadDictionarySync(path: string): boolean;

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

  // ============ FlatTrie API (instant loading) ============

  /**
   * Load pre-serialized .flat dictionary for instant loading (<50ms)
   * @param path - Path to .flat file
   * @returns true if loaded successfully
   */
  loadFlatDictionary(path: string): boolean;

  /**
   * Convert .dict file to optimized .flat format
   * @param inputPath - Path to input .dict file
   * @param outputPath - Path to output .flat file
   * @param locale - Optional language code (e.g., 'ru', 'en')
   * @returns true if conversion successful
   */
  convertToFlatFormat(inputPath: string, outputPath: string, locale?: string): boolean;

  /**
   * Get FlatTrie statistics
   * @returns FlatTrie stats or null if not loaded
   */
  getFlatTrieStats(): FlatTrieStats | null;

  /**
   * Batch operations namespace
   */
  batchOps: typeof batchOps;

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

  // ============ Learning Methods ============

  /**
   * Add learned word with bigram context
   * @param word - Word to learn
   * @param prevWord - Previous word for bigram context
   * @param count - Usage count (default 1)
   */
  addLearnedWord(word: string, prevWord: string, count?: number): void;

  /**
   * Save user dictionary to file
   * @param path - Path to save file
   * @returns true if saved successfully
   */
  saveUserDictionary(path: string): boolean;

  /**
   * Load user dictionary from file
   * @param path - Path to load from
   * @returns true if loaded successfully
   */
  loadUserDictionary(path: string): boolean;

  /**
   * Clear all learned words
   */
  clearLearnedWords(): void;

  /**
   * Get count of learned words
   * @returns Number of learned words
   */
  getLearnedWordsCount(): number;

  /**
   * Remove a learned word
   * @param word - Word to remove
   * @returns true if removed
   */
  removeLearnedWord(word: string): boolean;

  /**
   * Get learned boost score for word with context
   * @param word - Word to check
   * @param prevWord - Previous word for context
   * @returns Boost score (0 if not learned)
   */
  getLearnedBoost(word: string, prevWord: string): number;

  /**
   * Get count of bigram learned words
   * @returns Number of bigram entries
   */
  getBigramLearnedWordsCount(): number;

  /**
   * Legacy: Add learned word simple (no context)
   */
  addLearnedWordSimple(word: string, frequency: number): void;

  /**
   * Legacy: Record word usage
   */
  recordWordUsage(word: string): void;

  /**
   * Legacy: Save user dict
   */
  saveUserDict(path: string): boolean;

  /**
   * Legacy: Load user dict
   */
  loadUserDict(path: string): boolean;

  // ============ Gesture Trail API (Yandex-style) ============

  /**
   * Initialize trail data system
   * @param params - Optional trail parameters
   * @returns true if initialized
   */
  initTrailData(params?: TrailInitParams): boolean;

  /**
   * Add a point to the trail drawer
   * @param x - X coordinate
   * @param y - Y coordinate
   * @param timestamp - Timestamp in milliseconds
   */
  addTrailPoint(x: number, y: number, timestamp: number): void;

  /**
   * Process drawer points and update visibility
   * @param currentTime - Current timestamp in milliseconds
   * @returns Number of visible points
   */
  updateTrailData(currentTime: number): number;

  /**
   * Get trail segments for rendering
   * @param currentTime - Current timestamp in milliseconds
   * @returns Float32Array of segments (7 floats each: x0, y0, w0, x1, y1, w1, alpha) or null
   */
  getTrailSegments(currentTime: number): Float32Array | null;

  /**
   * Reset trail data for new gesture
   */
  resetTrailData(): void;

  /**
   * Update trail render parameters
   * @param params - Render parameters to update
   */
  setTrailRenderParams(params: TrailRenderParams): void;

  /**
   * Compact trail buffers to save memory
   */
  compactTrailBuffers(): void;
}

/**
 * Default export - Native Dictionary Module
 */
declare const nativeDict: NativeDictModule;
export default nativeDict;

// Named exports for standalone function usage
export declare function loadDictionary(path: string): Promise<boolean>;
export declare function loadDictionarySync(path: string): boolean;
export declare function contains(word: string): boolean;
export declare function getFrequency(word: string): number;
export declare function getSuggestions(prefix: string, limit: number): SuggestResult[];
export declare function findAutocorrection(word: string, threshold: number): SuggestResult | null;
export declare function setProximityInfo(layout: string, keyWidth: number, keyHeight: number): boolean;
export declare function getStats(): DictStats;
export declare function unload(): void;
export declare function setSwipeKeyboardLayout(keys: Array<KeyBounds>): boolean;
export declare function processSwipePath(points: Array<TouchPoint>): SwipeResult | null;

// FlatTrie functions
export declare function loadFlatDictionary(path: string): boolean;
export declare function convertToFlatFormat(inputPath: string, outputPath: string, locale?: string): boolean;
export declare function getFlatTrieStats(): FlatTrieStats | null;

// Learning functions
export declare function addLearnedWord(word: string, prevWord: string, count?: number): void;
export declare function saveUserDictionary(path: string): boolean;
export declare function loadUserDictionary(path: string): boolean;
export declare function clearLearnedWords(): void;
export declare function getLearnedWordsCount(): number;
export declare function removeLearnedWord(word: string): boolean;
export declare function getLearnedBoost(word: string, prevWord: string): number;
export declare function getBigramLearnedWordsCount(): number;

// Trail functions (Yandex-style)
export declare function initTrailData(params?: TrailInitParams): boolean;
export declare function addTrailPoint(x: number, y: number, timestamp: number): void;
export declare function updateTrailData(currentTime: number): number;
export declare function getTrailSegments(currentTime: number): Float32Array | null;
export declare function resetTrailData(): void;
export declare function setTrailRenderParams(params: TrailRenderParams): void;
export declare function compactTrailBuffers(): void;
