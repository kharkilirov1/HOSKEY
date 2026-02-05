/**
 * HOSKEY Native Dictionary Module
 * Type declarations for N-API bridge
 *
 * Architecture: Yandex-style dictionary with mmap-based CompTrie
 * - Instant dictionary loading via mmap
 * - Neural model scoring (MindSpore/NNRt)
 * - Beam search for swipe/gesture input
 *
 * All methods validate argument types at runtime and throw
 * TypeError if invalid arguments are provided. Methods that
 * require a loaded dictionary return safe defaults (false, 0,
 * empty array, null) when dictionary is not loaded.
 *
 * NOTE: FlatTrie and OpenBoard-based functions are DEPRECATED.
 * Use loadDictionaryFromFd() with Yandex format instead.
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
 * @deprecated FlatTrie is legacy OpenBoard format. Use getStats() for Yandex dictionary stats.
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
   * Load dictionary from file descriptor using memory-mapping (mmap)
   * This is INSTANT - no file reading into RAM, data accessed on-demand
   * Use this for rawfile resources where fd is available
   *
   * @param fd - File descriptor from getRawFd()
   * @param offset - Offset within file where data starts
   * @param length - Length of data to map
   * @returns Promise that resolves to true if loaded successfully
   */
  loadDictionaryFromFd(fd: number, offset: number, length: number): Promise<boolean>;

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

  // ============ FlatTrie API (DEPRECATED - use Yandex format) ============

  /**
   * Load pre-serialized .flat dictionary for instant loading (<50ms)
   * @deprecated Use loadDictionaryFromFd() with Yandex format instead. Always returns false.
   * @param path - Path to .flat file
   * @returns false (deprecated)
   */
  loadFlatDictionary(path: string): boolean;

  /**
   * Convert .dict file to optimized .flat format
   * @deprecated Yandex format doesn't require conversion. Always returns false.
   * @param inputPath - Path to input .dict file
   * @param outputPath - Path to output .flat file
   * @param locale - Optional language code (e.g., 'ru', 'en')
   * @returns false (deprecated)
   */
  convertToFlatFormat(inputPath: string, outputPath: string, locale?: string): boolean;

  /**
   * Get FlatTrie statistics
   * @deprecated Use getStats() for Yandex dictionary stats. Always returns null.
   * @returns null (deprecated)
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

  // ============ Learning Methods (STUBBED - TODO: implement in YandexDict) ============

  /**
   * Add learned word with bigram context
   * @stub Currently does nothing. Will be implemented in YandexDict.
   * @param word - Word to learn
   * @param prevWord - Previous word for bigram context
   * @param count - Usage count (default 1)
   */
  addLearnedWord(word: string, prevWord: string, count?: number): void;

  /**
   * Save user dictionary to file
   * @stub Currently returns false. Will be implemented in YandexDict.
   * @param path - Path to save file
   * @returns false (not implemented)
   */
  saveUserDictionary(path: string): boolean;

  /**
   * Load user dictionary from file
   * @stub Currently returns false. Will be implemented in YandexDict.
   * @param path - Path to load from
   * @returns false (not implemented)
   */
  loadUserDictionary(path: string): boolean;

  /**
   * Clear all learned words
   * @stub Currently does nothing. Will be implemented in YandexDict.
   */
  clearLearnedWords(): void;

  /**
   * Get count of learned words
   * @stub Currently returns 0. Will be implemented in YandexDict.
   * @returns 0 (not implemented)
   */
  getLearnedWordsCount(): number;

  /**
   * Remove a learned word
   * @stub Currently returns false. Will be implemented in YandexDict.
   * @param word - Word to remove
   * @returns false (not implemented)
   */
  removeLearnedWord(word: string): boolean;

  /**
   * Get learned boost score for word with context
   * @stub Currently returns 0. Will be implemented in YandexDict.
   * @param word - Word to check
   * @param prevWord - Previous word for context
   * @returns 0 (not implemented)
   */
  getLearnedBoost(word: string, prevWord: string): number;

  /**
   * Get count of bigram learned words
   * @stub Currently returns 0. Will be implemented in YandexDict.
   * @returns 0 (not implemented)
   */
  getBigramLearnedWordsCount(): number;

  /**
   * Legacy: Add learned word simple (no context)
   * @stub Not implemented
   */
  addLearnedWordSimple(word: string, frequency: number): void;

  /**
   * Legacy: Record word usage
   * @stub Not implemented
   */
  recordWordUsage(word: string): void;

  /**
   * Legacy: Save user dict
   * @stub Not implemented
   */
  saveUserDict(path: string): boolean;

  /**
   * Legacy: Load user dict
   * @stub Not implemented
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

  // ============ Yandex Neural Dictionary API ============

  /**
   * Load Yandex dictionary from file
   * @param path - Path to dictionary (binary main_ru or text .txt)
   * @returns true if loaded successfully
   */
  loadYandexDict(path: string): boolean;

  /**
   * Load MindSpore neural model for scoring
   * @param path - Path to .ms model file
   * @returns true if loaded successfully
   */
  loadNeuralModel(path: string): boolean;

  /**
   * Load ALL 15 MindSpore neural models for Yandex keyboard
   * Models are loaded from the specified directory.
   *
   * TAP RANKING: tap_model_ranker.ms, tap_model_ranker_v2.ms, ranker.ms, ranker_v2.ms, ranker_exp.ms
   * SWIPE RANKING: ranker_swipe.ms, ranker_swipe_v2.ms, swipe_blocker.ms
   * LANGUAGE: nnlm_model.ms, neural_model.ms, char_model.ms
   * AUTOCORRECT: tree_autocorrect_model.ms, lemmer_mhash.ms
   * EMOJI: emoji_suggest.ms, search_emoji_model.ms
   *
   * @param modelsDir - Directory containing all .ms model files
   * @returns Result object with loaded/failed counts and model names
   */
  loadModels(modelsDir: string): LoadModelsResult;

  /**
   * Get status of loaded models
   * @returns Model statistics
   */
  getModelStats(): ModelStats;

  /**
   * Get neural-scored suggestions from Yandex dictionary
   * @param prefix - Input prefix
   * @param limit - Maximum results (default: 10)
   * @param context - Previous words for context (optional)
   * @returns Array of scored suggestions
   */
  getYandexSuggestions(prefix: string, limit?: number, context?: string): YandexSuggestion[];

  /**
   * Get Yandex dictionary statistics
   * @returns Stats object with dictLoaded, modelLoaded, wordCount, memoryBytes
   */
  getYandexStats(): YandexStats;

  /**
   * Unload Yandex dictionary and neural model
   */
  unloadYandex(): void;

  // ============ MultiPredictor namespace ============
  
  multiPredictor: {
    init(): boolean;
    addDictionaryPredictor(): boolean;
    addNgramPredictor(): boolean;
    removePredictor(sourceId: number): void;
    setPredictorEnabled(sourceId: number, enabled: boolean): void;
    getPredictions(currentWord: string, prevWord?: string, maxResults?: number): SuggestResult[];
    clear(): void;
    getStats(): { predictorCount: number } | null;
    // Yandex-style filtering
    addToBlacklist(word: string): void;
    removeFromBlacklist(word: string): void;
    addToAutocorrectBlocker(word: string): void;
    removeFromAutocorrectBlocker(word: string): void;
    // Score fusion params
    setFusionParams(params: FusionParams): void;
    getFusionParams(): FusionParams;
  };
}

/**
 * Yandex suggestion result
 */
declare interface YandexSuggestion {
  word: string;
  score: number;
  neuralScore?: number;
  freqScore?: number;
}

/**
 * Yandex dictionary statistics
 */
declare interface YandexStats {
  dictLoaded: boolean;
  modelLoaded: boolean;
  wordCount?: number;
  memoryBytes?: number;
}

/**
 * Result of loading all 15 neural models
 */
declare interface LoadModelsResult {
  success: boolean;
  loaded: number;      // Number of models loaded successfully
  total: number;       // Total models (15)
  models: string[];    // Names of loaded models
  failed: string[];    // Names of failed models
}

/**
 * Model statistics (ALL 15 neural models)
 */
declare interface ModelStats {
  yandexDictLoaded: boolean;
  neuralModelsEnabled: boolean;
  beamSearchReady: boolean;
  totalModels: number;         // 15 total
  loadedModels: number;        // How many loaded
  primaryDevice: string;       // NPU/NNRT/CPU
  loadedModelNames: string[];  // Names of loaded models
  failedModelNames: string[];  // Names of failed models
  legacyScorerEnabled: boolean;
  legacyScorerLoaded: boolean;
  legacyDevice?: string;
}

/**
 * Score fusion parameters (Yandex-style)
 */
declare interface FusionParams {
  dictionaryWeight?: number;
  neuralWeight?: number;
  personalWeight?: number;
  ngramWeight?: number;
  autocorrectThreshold?: number;
  maxRelativeScoreGap?: number;
}

/**
 * Default export - Native Dictionary Module
 */
declare const nativeDict: NativeDictModule;
export default nativeDict;

// Named exports for standalone function usage
export declare function loadDictionary(path: string): Promise<boolean>;
export declare function loadDictionarySync(path: string): boolean;
export declare function loadDictionaryFromFd(fd: number, offset: number, length: number): Promise<boolean>;
export declare function contains(word: string): boolean;
export declare function getFrequency(word: string): number;
export declare function getSuggestions(prefix: string, limit: number): SuggestResult[];
export declare function findAutocorrection(word: string, threshold: number): SuggestResult | null;
export declare function setProximityInfo(layout: string, keyWidth: number, keyHeight: number): boolean;
export declare function getStats(): DictStats;
export declare function unload(): void;
export declare function setSwipeKeyboardLayout(keys: Array<KeyBounds>): boolean;
export declare function processSwipePath(points: Array<TouchPoint>): SwipeResult | null;

// FlatTrie functions (DEPRECATED - always return false/null)
/** @deprecated Use loadDictionaryFromFd() with Yandex format */
export declare function loadFlatDictionary(path: string): boolean;
/** @deprecated Yandex format doesn't require conversion */
export declare function convertToFlatFormat(inputPath: string, outputPath: string, locale?: string): boolean;
/** @deprecated Use getStats() for Yandex dictionary stats */
export declare function getFlatTrieStats(): FlatTrieStats | null;

// Learning functions (STUBBED - TODO: implement in YandexDict)
/** @stub Not implemented - will be added to YandexDict */
export declare function addLearnedWord(word: string, prevWord: string, count?: number): void;
/** @stub Returns false - will be implemented in YandexDict */
export declare function saveUserDictionary(path: string): boolean;
/** @stub Returns false - will be implemented in YandexDict */
export declare function loadUserDictionary(path: string): boolean;
/** @stub Does nothing - will be implemented in YandexDict */
export declare function clearLearnedWords(): void;
/** @stub Returns 0 - will be implemented in YandexDict */
export declare function getLearnedWordsCount(): number;
/** @stub Returns false - will be implemented in YandexDict */
export declare function removeLearnedWord(word: string): boolean;
/** @stub Returns 0 - will be implemented in YandexDict */
export declare function getLearnedBoost(word: string, prevWord: string): number;
/** @stub Returns 0 - will be implemented in YandexDict */
export declare function getBigramLearnedWordsCount(): number;

// Trail functions (Yandex-style)
export declare function initTrailData(params?: TrailInitParams): boolean;
export declare function addTrailPoint(x: number, y: number, timestamp: number): void;
export declare function updateTrailData(currentTime: number): number;
export declare function getTrailSegments(currentTime: number): Float32Array | null;
export declare function resetTrailData(): void;
export declare function setTrailRenderParams(params: TrailRenderParams): void;
export declare function compactTrailBuffers(): void;
