/**
 * Centralized logging utility with debug flag control
 * Use this instead of console.info for non-critical logs
 */

// Set to true for verbose debug logging, false for production
const DEBUG = false;

/**
 * Logger class for conditional logging
 */
class LoggerImpl {
  /**
   * Auto-capitalization debug logs (only when DEBUG=true)
   */
  autocap(message: string, ...args: string[]): void {
    if (DEBUG) {
      console.info('[AutoCap]', message, ...args);
    }
  }

  /**
   * Prediction model debug logs (only when DEBUG=true)
   */
  prediction(message: string): void {
    if (DEBUG) {
      console.info('[Prediction]', message);
    }
  }

  /**
   * General info logs - use sparingly, only for important events
   */
  info(message: string): void {
    console.info(message);
  }

  /**
   * Warning logs - always shown
   */
  warn(message: string, error?: Error | Object): void {
    if (error) {
      console.warn(message, error);
    } else {
      console.warn(message);
    }
  }

  /**
   * Error logs - always shown
   */
  error(message: string, error?: Error | Object): void {
    if (error) {
      console.error(message, error);
    } else {
      console.error(message);
    }
  }
}

export const Logger = new LoggerImpl();

