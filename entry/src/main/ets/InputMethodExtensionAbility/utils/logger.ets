import { ILogger } from './ILogger';

/**
 * Centralized logging utility with debug flag control
 * Use this instead of console.info for non-critical logs
 * ArkTS-compliant implementation
 */

// Set to true for verbose debug logging, false for production
const DEBUG: boolean = false;

/**
 * Logger class for conditional logging with prefix support
 */
export class Logger implements ILogger {
  private prefix: string;

  constructor(prefix: string = '') {
    this.prefix = prefix;
  }

  /**
   * Auto-capitalization debug logs (only when DEBUG=true)
   */
  autocap(message: string, ...args: string[]): void {
    if (DEBUG) {
      const fullMessage: string = this.prefix ? `[${this.prefix}][AutoCap] ${message}` : `[AutoCap] ${message}`;
      console.info(fullMessage, ...args);
    }
  }

  /**
   * Prediction model debug logs (only when DEBUG=true)
   */
  prediction(message: string): void {
    if (DEBUG) {
      const fullMessage: string = this.prefix ? `[${this.prefix}][Prediction] ${message}` : `[Prediction] ${message}`;
      console.info(fullMessage);
    }
  }

  /**
   * General info logs - use sparingly, only for important events
   */
  info(message: string): void {
    const fullMessage: string = this.prefix ? `[${this.prefix}] ${message}` : message;
    console.info(fullMessage);
  }

  /**
   * Warning logs - always shown
   */
  warn(message: string, error?: Error): void {
    const fullMessage: string = this.prefix ? `[${this.prefix}] ${message}` : message;
    if (error) {
      console.warn(fullMessage, error);
    } else {
      console.warn(fullMessage);
    }
  }

  /**
   * Error logs - always shown
   */
  error(message: string, error?: Error): void {
    const fullMessage: string = this.prefix ? `[${this.prefix}] ${message}` : message;
    if (error) {
      console.error(fullMessage, error);
    } else {
      console.error(fullMessage);
    }
  }
}

