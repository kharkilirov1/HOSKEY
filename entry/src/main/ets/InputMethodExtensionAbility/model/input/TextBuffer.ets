/**
 * TextBuffer - Manages current word and context for predictions
 *
 * Responsibilities:
 * - Track current token (word being typed)
 * - Track previous word for bigram/trigram context
 * - Handle word boundaries (space, enter, punctuation)
 * - Provide context for predictions
 *
 * Performance:
 * - All operations O(1) or O(n) where n is word length (typically < 20 chars)
 * - No allocations in hot paths
 */

export class TextBuffer {
  private currentToken: string = '';
  private prevWord: string = '';
  private prev2Word: string = ''; // For trigram context (future)

  constructor() {}

  /**
   * Append character to current token
   * Fast path - called on every keystroke
   */
  appendChar(char: string): void {
    this.currentToken += char;
  }

  /**
   * Remove last character from current token
   * Fast path - called on backspace
   */
  backspace(): void {
    if (this.currentToken.length > 0) {
      this.currentToken = this.currentToken.slice(0, -1);
    } else if (this.prevWord.length > 0) {
      // Backspace into previous word - restore it as current
      this.currentToken = this.prevWord;
      this.prevWord = this.prev2Word;
      this.prev2Word = '';
    }
  }

  /**
   * Commit current token as a word boundary
   * Called on: space, enter, punctuation
   * Shifts words: current → prev → prev2
   */
  commitBoundary(): void {
    if (this.currentToken.length > 0) {
      this.prev2Word = this.prevWord;
      this.prevWord = this.currentToken;
      this.currentToken = '';
    }
  }

  /**
   * Set committed word directly (for swipe insert)
   * @param word - Word to commit (from swipe recognition)
   */
  setCommittedWord(word: string): void {
    this.prev2Word = this.prevWord;
    this.prevWord = word;
    this.currentToken = '';
  }

  /**
   * Get current partial word
   * @returns Current token (empty if at word boundary)
   */
  getCurrentToken(): string {
    return this.currentToken;
  }

  /**
   * Get previous word for bigram context
   * @returns Previous word (empty if none)
   */
  getPrevWord(): string {
    return this.prevWord;
  }

  /**
   * Get prev2 word for trigram context
   * @returns Previous previous word (empty if none)
   */
  getPrev2Word(): string {
    return this.prev2Word;
  }

  /**
   * Reset all state (on inputStop or session reset)
   */
  reset(): void {
    this.currentToken = '';
    this.prevWord = '';
    this.prev2Word = '';
  }

  /**
   * Check if we're at word boundary (empty current token)
   */
  isAtBoundary(): boolean {
    return this.currentToken.length === 0;
  }
}
