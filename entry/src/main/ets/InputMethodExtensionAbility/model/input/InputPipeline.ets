/**
 * InputPipeline - Central coordinator for input event processing
 *
 * Architecture:
 * ┌─────────┐
 * │ UI Keys │
 * └────┬────┘
 *      │ InputEvent
 *      ▼
 * ┌──────────────────┐
 * │  InputPipeline   │◄─── TextBuffer (state)
 * │  - Fast Path     │◄─── ITextClientAdapter
 * │  - Slow Path     │◄─── IPredictor (async)
 * │  - Swipe Path    │◄─── ISuggestionPresenter
 * └──────────────────┘
 *
 * Performance Design:
 * - FAST PATH: Tap events → insertText immediately (< 1ms)
 * - SLOW PATH: Predictions → debounced + async (30ms delay)
 * - SWIPE PATH: Buffered until SwipeEnd → decode async
 *
 * All heavy computation (predictions, morphology, ranking) happens
 * asynchronously to never block UI thread.
 */

import {
  InputEvent, InputMode, ITextClientAdapter, ISuggestionPresenter, IPredictor
} from './InputTypes';
import { TextBuffer } from './TextBuffer';
import { Debouncer } from './Debouncer';
import nativeDictionary from '../NativeDictionary';
import { TouchPoint } from '../NativeDictionary';

// Performance logging (disabled by default)
const PERF_LOG_ENABLED = false;

function perfLog(label: string, startTime: number): void {
  if (PERF_LOG_ENABLED) {
    const duration = Date.now() - startTime;
    console.info(`[InputPipeline] ${label}: ${duration}ms`);
  }
}

interface GeneratedTypeLiteralInterface_1 {
  currentToken: string;
  prevWord: string;
  mode: InputMode;
  isSwipeActive: boolean;
}

/**
 * SwipeTraceBuffer - Accumulates touch points during swipe gesture
 */
class SwipeTraceBuffer {
  private points: TouchPoint[] = [];

  addPoint(x: number, y: number, timestamp: number): void {
    this.points.push({ x, y, timestamp });
  }

  getPoints(): TouchPoint[] {
    return this.points;
  }

  getLength(): number {
    return this.points.length;
  }

  clear(): void {
    this.points = [];
  }
}

export class InputPipeline {
  // Core components
  private textBuffer: TextBuffer = new TextBuffer();
  private client: ITextClientAdapter | undefined;
  private presenter: ISuggestionPresenter | undefined;
  private predictor: IPredictor | undefined;
  // Debouncing
  private predictionDebouncer: Debouncer = new Debouncer(30); // 30ms delay

  // State
  private currentMode: InputMode = InputMode.Tap;
  private currentLanguage: string = 'en';
  private isProcessing: boolean = false;
  // Swipe state
  private isSwipeActive: boolean = false;
  private swipeStartTime: number = 0;
  private swipeBuffer: SwipeTraceBuffer = new SwipeTraceBuffer();

  constructor() {
  }

  // === CONFIGURATION ===

  /**
   * Set text input client (from inputStart event)
   */
  setClient(client: ITextClientAdapter | undefined): void {
    this.client = client;
  }

  /**
   * Set suggestion presenter for UI updates
   */
  setSuggestionPresenter(presenter: ISuggestionPresenter): void {
    this.presenter = presenter;
  }

  /**
   * Set predictor for async predictions
   */
  setPredictor(predictor: IPredictor): void {
    this.predictor = predictor;
  }

  /**
   * Set current language for predictions
   */
  setLanguage(lang: string): void {
    this.currentLanguage = lang;
  }

  // === EVENT HANDLING ===

  /**
   * Main entry point for all input events
   * Dispatches to appropriate handler based on event kind
   */
  onEvent(event: InputEvent): void {
    switch (event.kind) {
      case 'KeyTap':
        this.handleKeyTap(event.char, event.timestamp);
        break;
      case 'Backspace':
        this.handleBackspace(event.timestamp);
        break;
      case 'Space':
        this.handleSpace(event.timestamp);
        break;
      case 'Enter':
        this.handleEnter(event.timestamp);
        break;
      case 'SwipeStart':
        this.handleSwipeStart(event.x, event.y, event.timestamp);
        break;
      case 'SwipeMove':
        this.handleSwipeMove(event.x, event.y, event.timestamp);
        break;
      case 'SwipeEnd':
        this.handleSwipeEnd(event.x, event.y, event.timestamp);
        break;
    }
  }

  // === TAP HANDLERS (FAST PATH) ===

  /**
   * Handle character tap - FAST PATH
   * Must complete in < 1ms on UI thread
   */
  private handleKeyTap(char: string, timestamp: number): void {
    if (!this.client || this.isSwipeActive) {
      return;
    }

    // FAST PATH: Insert immediately
    this.client.insertText(char);

    // Update buffer
    this.textBuffer.appendChar(char);

    // SLOW PATH: Schedule prediction (debounced)
    this.schedulePrediction('tap');
  }

  /**
   * Handle backspace - FAST PATH
   */
  private handleBackspace(timestamp: number): void {
    if (!this.client || this.isSwipeActive) {
      return;
    }

    // FAST PATH: Delete immediately
    this.client.deleteBackward(1);

    // Update buffer
    this.textBuffer.backspace();

    // SLOW PATH: Schedule prediction (debounced)
    this.schedulePrediction('backspace');
  }

  /**
   * Handle space - FAST PATH + boundary + learning
   */
  private handleSpace(timestamp: number): void {
    if (!this.client || this.isSwipeActive) {
      return;
    }

    // Capture word before committing boundary
    const committedWord = this.textBuffer.getCurrentToken();
    const prevWord = this.textBuffer.getPrevWord();

    // FAST PATH: Insert space immediately
    this.client.insertText(' ');

    // Mark word boundary
    this.textBuffer.commitBoundary();

    // Record word for learning (async, fire-and-forget)
    this.onWordCommitted(committedWord, prevWord);

    // Clear suggestions at boundary
    if (this.presenter) {
      this.presenter.clear();
    }

    // Cancel pending predictions
    this.predictionDebouncer.cancel();
  }

  /**
   * Handle enter - FAST PATH + boundary + learning
   */
  private handleEnter(timestamp: number): void {
    if (!this.client || this.isSwipeActive) {
      return;
    }

    // Capture word before committing boundary
    const committedWord = this.textBuffer.getCurrentToken();
    const prevWord = this.textBuffer.getPrevWord();

    // FAST PATH: Send enter immediately
    this.client.sendEnter();

    // Mark word boundary
    this.textBuffer.commitBoundary();

    // Record word for learning (async, fire-and-forget)
    this.onWordCommitted(committedWord, prevWord);

    // Clear suggestions at boundary
    if (this.presenter) {
      this.presenter.clear();
    }

    // Cancel pending predictions
    this.predictionDebouncer.cancel();
  }

  // === SWIPE HANDLERS ===
  // Integrated with native processSwipePath via NativeDictionary

  private handleSwipeStart(x: number, y: number, timestamp: number): void {
    this.isSwipeActive = true;
    this.swipeStartTime = timestamp;
    this.currentMode = InputMode.Swipe;

    // Clear previous trace and add first point
    this.swipeBuffer.clear();
    this.swipeBuffer.addPoint(x, y, timestamp);

    // Cancel any pending predictions during swipe
    this.predictionDebouncer.cancel();

    console.info(`[InputPipeline] SwipeStart at (${x}, ${y})`);
  }

  private handleSwipeMove(x: number, y: number, timestamp: number): void {
    if (!this.isSwipeActive) {
      return;
    }
    // Add point to buffer
    this.swipeBuffer.addPoint(x, y, timestamp);
  }

  private handleSwipeEnd(x: number, y: number, timestamp: number): void {
    if (!this.isSwipeActive) {
      return;
    }

    // Add final point
    this.swipeBuffer.addPoint(x, y, timestamp);

    this.isSwipeActive = false;
    this.currentMode = InputMode.Tap;

    const points = this.swipeBuffer.getPoints();
    const pointCount = this.swipeBuffer.getLength();
    console.info(`[InputPipeline] SwipeEnd with ${pointCount} points`);

    // Minimum requirements for valid swipe
    if (pointCount < 5) {
      console.info('[InputPipeline] Swipe too short, ignoring');
      this.swipeBuffer.clear();
      return;
    }

    // Process swipe via native engine
    this.processSwipeAsync(points);

    // Clear buffer
    this.swipeBuffer.clear();
  }

  /**
   * Process swipe path asynchronously via native engine
   */
  private async processSwipeAsync(points: TouchPoint[]): Promise<void> {
    const startTime = Date.now();

    try {
      const result = nativeDictionary.processSwipePath(points);

      if (result && result.bestWord && result.bestWord.length > 0) {
        console.info(`[InputPipeline] Swipe decoded: "${result.bestWord}" (confidence: ${result.confidence})`);

        // Insert the word with trailing space
        if (this.client) {
          this.client.insertText(result.bestWord + ' ');
        }

        // Update text buffer
        this.textBuffer.commitBoundary();

        // Show alternatives as suggestions
        if (this.presenter && result.alternatives && result.alternatives.length > 0) {
          this.presenter.show(result.alternatives);
        } else if (this.presenter) {
          this.presenter.clear();
        }

        perfLog('SwipeProcess', startTime);
      } else {
        console.info('[InputPipeline] No swipe result');
        if (this.presenter) {
          this.presenter.clear();
        }
      }
    } catch (err) {
      console.error('[InputPipeline] Swipe processing error:', err);
    }
  }

  // === PREDICTION (SLOW PATH) ===

  /**
   * Schedule prediction request (debounced to avoid spam)
   * @param reason - Debug label for why prediction was requested
   */
  private schedulePrediction(reason: string): void {
    this.predictionDebouncer.schedule(() => {
      this.predictNow(reason);
    });
  }

  /**
   * Execute prediction request (async, off UI thread)
   * @param reason - Debug label
   */
  private async predictNow(reason: string): Promise<void> {
    if (this.isProcessing || !this.predictor || !this.presenter) {
      return;
    }

    const token = this.textBuffer.getCurrentToken();

    // No predictions at word boundary
    if (token.length === 0) {
      this.presenter.clear();
      return;
    }

    // Skip very short tokens (< 2 chars) - too many false positives
    if (token.length < 2) {
      return;
    }

    this.isProcessing = true;
    const startTime = Date.now();

    try {
      const prevWord = this.textBuffer.getPrevWord();
      const suggestions = await this.predictor.predict(
        this.currentLanguage,
        prevWord,
        token,
        3 // Top 3 suggestions
      );

      // Only update UI if we got results
      if (suggestions.length > 0) {
        this.presenter.show(suggestions);
      } else {
        this.presenter.clear();
      }

      perfLog(`predict(${reason})`, startTime);
    } catch (err) {
      console.error('[InputPipeline] Prediction error:', err);
      this.presenter.clear();
    } finally {
      this.isProcessing = false;
    }
  }

  // === LEARNING ===

  /**
   * Called when a word is committed (space/enter pressed)
   * Records the word for adaptive learning
   *
   * @param word - The word that was committed
   * @param prevWord - Previous word for bigram context
   */
  private onWordCommitted(word: string, prevWord: string): void {
    // Skip empty or very short words (likely typos or single chars)
    if (!word || word.length < 2) {
      return;
    }

    // Skip words that are just numbers or punctuation
    const hasLetters = /[a-zA-Zа-яА-ЯёЁ]/.test(word);
    if (!hasLetters) {
      return;
    }

    // Record via predictor (fire-and-forget, non-blocking)
    if (this.predictor) {
      try {
        this.predictor.recordWord(this.currentLanguage, prevWord, word);
      } catch (err) {
        // Don't let learning errors affect typing
        console.error('[InputPipeline] Error recording word:', err);
      }
    }
  }

  // === SESSION MANAGEMENT ===

  /**
   * Reset all state (called on inputStop or keyboard hide)
   */
  resetSession(): void {
    this.textBuffer.reset();
    this.predictionDebouncer.cancel();
    this.isSwipeActive = false;
    this.currentMode = InputMode.Tap;
    this.isProcessing = false;

    if (this.presenter) {
      this.presenter.clear();
    }
  }

  // === DEBUG ===

  /**
   * Get current state (for debugging/testing)
   */
  getState(): GeneratedTypeLiteralInterface_1 {
    return {
      currentToken: this.textBuffer.getCurrentToken(),
      prevWord: this.textBuffer.getPrevWord(),
      mode: this.currentMode,
      isSwipeActive: this.isSwipeActive
    };
  }
}
