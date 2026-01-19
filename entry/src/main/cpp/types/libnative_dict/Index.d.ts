/**
 * HOSKEY Native Dictionary Module
 * Type declarations for N-API bridge
 */

/**
 * Error types for autocorrection
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
}

/**
 * Load binary dictionary from file path
 * @param path - Path to dictionary file (.dict or .txt)
 * @returns true if loaded successfully
 */
export function loadDictionary(path: string): boolean;

/**
 * Check if word exists in dictionary
 * @param word - Word to check
 * @returns true if word exists
 */
export function contains(word: string): boolean;

/**
 * Get frequency of word in dictionary
 * @param word - Word to lookup
 * @returns frequency value (0-255) or 0 if not found
 */
export function getFrequency(word: string): number;

/**
 * Get word suggestions for input
 * @param prefix - Input prefix to search
 * @param limit - Maximum number of results
 * @returns Array of suggestion results
 */
export function getSuggestions(prefix: string, limit: number): SuggestResult[];

/**
 * Find best autocorrection for word
 * @param word - Word to autocorrect
 * @param threshold - Confidence threshold (default 0.185)
 * @returns Best autocorrection or null if none found
 */
export function findAutocorrection(word: string, threshold: number): SuggestResult | null;

/**
 * Initialize keyboard proximity information
 * @param layout - Keyboard layout name ('qwerty', 'ru', 'azerty', 'qwertz')
 * @param keyWidth - Key width in pixels
 * @param keyHeight - Key height in pixels
 * @returns true if initialized successfully
 */
export function setProximityInfo(layout: string, keyWidth: number, keyHeight: number): boolean;

/**
 * Get dictionary statistics
 * @returns Dictionary stats object
 */
export function getStats(): DictStats;

/**
 * Unload dictionary and free memory
 */
export function unload(): void;

/**
 * Set keyboard layout for swipe recognition
 * @param keys - Array of key bounds {key: string, centerX: number, centerY: number, width: number, height: number}
 * @returns true if set successfully
 */
export function setSwipeKeyboardLayout(keys: Array<{
  key: string;
  centerX: number;
  centerY: number;
  width: number;
  height: number;
}>): boolean;

/**
 * Process swipe path and return recognized word
 * @param points - Array of touch points {x: number, y: number, timestamp: number}
 * @returns Swipe result or null if invalid
 */
export function processSwipePath(points: Array<{
  x: number;
  y: number;
  timestamp: number;
}>): {
  bestWord: string;
  alternatives: string[];
  confidence: number;
  rawSequence: string;
} | null;
