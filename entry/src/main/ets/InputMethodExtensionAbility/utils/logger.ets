/**
 * Centralized logging utility with debug flag control
 * Use this instead of console.info for non-critical logs
 */

// Set to true for verbose debug logging, false for production
const DEBUG = false;

export const Logger = {
  /**
   * Auto-capitalization debug logs (only when DEBUG=true)
   */
  autocap(...args: any[]): void {
    if (DEBUG) {
      console.info('[AutoCap]', ...args);
    }
  },

  /**
   * Prediction model debug logs (only when DEBUG=true)
   */
  prediction(...args: any[]): void {
    if (DEBUG) {
      console.info('[Prediction]', ...args);
    }
  },

  /**
   * General info logs - use sparingly, only for important events
   */
  info(...args: any[]): void {
    console.info(...args);
  },

  /**
   * Warning logs - always shown
   */
  warn(...args: any[]): void {
    console.warn(...args);
  },

  /**
   * Error logs - always shown
   */
  error(...args: any[]): void {
    console.error(...args);
  }
};
