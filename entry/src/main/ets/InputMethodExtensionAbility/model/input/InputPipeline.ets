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
  // Swipe state (will be implemented next)
  private isSwipeActive: boolean = false;
  private swipeStartTime: number = 0;

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
   * Handle space - FAST PATH + boundary
   */
  private handleSpace(timestamp: number): void {
    if (!this.client || this.isSwipeActive) {
      return;
    }

    // FAST PATH: Insert space immediately
    this.client.insertText(' ');

    // Mark word boundary
    this.textBuffer.commitBoundary();

    // Clear suggestions at boundary
    if (this.presenter) {
      this.presenter.clear();
    }

    // Cancel pending predictions
    this.predictionDebouncer.cancel();
  }

  /**
   * Handle enter - FAST PATH + boundary
   */
  private handleEnter(timestamp: number): void {
    if (!this.client || this.isSwipeActive) {
      return;
    }

    // FAST PATH: Send enter immediately
    this.client.sendEnter();

    // Mark word boundary
    this.textBuffer.commitBoundary();

    // Clear suggestions at boundary
    if (this.presenter) {
      this.presenter.clear();
    }

    // Cancel pending predictions
    this.predictionDebouncer.cancel();
  }

  // === SWIPE HANDLERS ===
  // NOTE: Swipe typing via InputPipeline is DEFERRED.
  // SwipeEngine core algorithm is implemented in SwipeEngine.ets
  // Integration with InputPipeline pending (requires SwipeTraceBuffer)

  private handleSwipeStart(x: number, y: number, timestamp: number): void {
    this.isSwipeActive = true;
    this.swipeStartTime = timestamp;
    this.currentMode = InputMode.Swipe;
    // DEFERRED: Initialize SwipeTraceBuffer and connect to SwipeEngine
  }

  private handleSwipeMove(x: number, y: number, timestamp: number): void {
    if (!this.isSwipeActive) {
      return;
    }
    // DEFERRED: Add point to SwipeTraceBuffer
  }

  private handleSwipeEnd(x: number, y: number, timestamp: number): void {
    if (!this.isSwipeActive) {
      return;
    }

    this.isSwipeActive = false;
    this.currentMode = InputMode.Tap;

    // DEFERRED: Process swipe trace → decode via SwipeEngine → insert word
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
