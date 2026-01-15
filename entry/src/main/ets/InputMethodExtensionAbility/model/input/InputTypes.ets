/**
 * InputPipeline Type Definitions
 *
 * Central type system for input event handling, text buffer management,
 * and client adapter interfaces.
 */

// === INPUT MODES ===

export enum InputMode {
  Tap = 'tap',        // Regular tap typing
  Swipe = 'swipe'     // Gesture typing (swipe)
}

// === INPUT EVENTS ===

export interface KeyTapEvent {
  kind: 'KeyTap';
  char: string;
  timestamp: number;
}

export interface BackspaceEvent {
  kind: 'Backspace';
  timestamp: number;
}

export interface SpaceEvent {
  kind: 'Space';
  timestamp: number;
}

export interface EnterEvent {
  kind: 'Enter';
  timestamp: number;
}

export interface SwipeStartEvent {
  kind: 'SwipeStart';
  x: number;
  y: number;
  timestamp: number;
}

export interface SwipeMoveEvent {
  kind: 'SwipeMove';
  x: number;
  y: number;
  timestamp: number;
}

export interface SwipeEndEvent {
  kind: 'SwipeEnd';
  x: number;
  y: number;
  timestamp: number;
}

export type InputEvent =
  | KeyTapEvent
  | BackspaceEvent
  | SpaceEvent
  | EnterEvent
  | SwipeStartEvent
  | SwipeMoveEvent
  | SwipeEndEvent;

// === CLIENT ADAPTER INTERFACE ===

/**
 * Abstract interface for TextInputClient operations
 * Decouples InputPipeline from HarmonyOS-specific APIs
 */
export interface ITextClientAdapter {
  /**
   * Insert text at current cursor position
   * MUST be fast - executed on UI thread
   */
  insertText(text: string): void;

  /**
   * Delete N characters backward from cursor
   * MUST be fast - executed on UI thread
   */
  deleteBackward(count?: number): void;

  /**
   * Send Enter key (newline or form submission)
   * MUST be fast - executed on UI thread
   */
  sendEnter(): void;

  /**
   * Check if client is available
   */
  isAvailable(): boolean;
}

// === SUGGESTION PRESENTER INTERFACE ===

/**
 * Abstract interface for displaying suggestions to UI
 * Decouples InputPipeline from specific UI framework
 */
export interface ISuggestionPresenter {
  /**
   * Display suggestion list to user
   * @param suggestions - Array of suggestion strings (typically 3-5)
   */
  show(suggestions: string[]): void;

  /**
   * Clear all suggestions from UI
   */
  clear(): void;
}

// === PREDICTOR INTERFACE ===

/**
 * Abstract interface for word prediction
 * Allows InputPipeline to work with any prediction model
 */
export interface IPredictor {
  /**
   * Get word predictions based on context and current token
   * MUST be async to avoid blocking UI thread
   *
   * @param lang - Current language code
   * @param prevWord - Previous word for bigram context (empty if none)
   * @param currentToken - Current partial word being typed
   * @param maxResults - Maximum number of suggestions to return
   * @returns Promise<string[]> - Array of predicted words
   */
  predict(lang: string, prevWord: string, currentToken: string, maxResults: number): Promise<string[]>;

  /**
   * Decode swipe path into word candidates
   * MUST be async to avoid blocking UI thread
   *
   * @param lang - Current language code
   * @param keyPath - Sequence of keys from swipe trace
   * @param prevWord - Previous word for context scoring
   * @returns Promise<string[]> - Array of candidate words, ranked by score
   */
  decodeSwipe(lang: string, keyPath: string[], prevWord: string): Promise<string[]>;
}
