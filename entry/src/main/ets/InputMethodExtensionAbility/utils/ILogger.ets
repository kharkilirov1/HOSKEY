/**
 * Logger interface for type-safe logging
 * ArkTS-compliant interface definition
 */
export interface ILogger {
  /**
   * Auto-capitalization debug logs
   */
  autocap(message: string, ...args: string[]): void;

  /**
   * Prediction model debug logs
   */
  prediction(message: string): void;

  /**
   * General info logs
   */
  info(message: string): void;

  /**
   * Warning logs
   */
  warn(message: string, error?: Error): void;

  /**
   * Error logs
   */
  error(message: string, error?: Error): void;
}
