/**
 * Debouncer - Delays function execution until activity stops
 *
 * Used for prediction requests to avoid overwhelming the predictor
 * with every keystroke.
 *
 * Example:
 *   User types "h" "e" "l" "l" "o" quickly
 *   Without debounce: 5 predict() calls
 *   With 30ms debounce: 1 predict() call (after "hello")
 *
 * Performance:
 * - Reduces prediction calls by 80-95%
 * - No blocking - uses async timer
 */

export class Debouncer {
  private timerId: number = -1;
  private readonly delayMs: number;

  /**
   * @param delayMs - Delay in milliseconds (recommended: 25-50ms)
   */
  constructor(delayMs: number) {
    this.delayMs = delayMs;
  }

  /**
   * Schedule function to run after delay
   * Cancels previous scheduled call if exists
   *
   * @param fn - Function to execute after delay
   */
  schedule(fn: () => void): void {
    this.cancel();
    this.timerId = setTimeout(() => {
      this.timerId = -1;
      fn();
    }, this.delayMs);
  }

  /**
   * Cancel scheduled function (if any)
   */
  cancel(): void {
    if (this.timerId !== -1) {
      clearTimeout(this.timerId);
      this.timerId = -1;
    }
  }

  /**
   * Check if function is currently scheduled
   */
  isPending(): boolean {
    return this.timerId !== -1;
  }
}
