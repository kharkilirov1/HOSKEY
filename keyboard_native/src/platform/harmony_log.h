/**
 * HOSKEY Keyboard - HarmonyOS Logging
 *
 * Platform abstraction for logging.
 * Uses hilog on HarmonyOS, stdout/stderr on other platforms.
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#ifndef KEYBOARD_NATIVE_HARMONY_LOG_H
#define KEYBOARD_NATIVE_HARMONY_LOG_H

namespace keyboard {
namespace platform {

/**
 * Log levels
 */
enum class LogLevel {
    Off = 0,
    Error = 1,
    Warn = 2,
    Info = 3,
    Debug = 4,
    Trace = 5
};

/**
 * Set global log level
 */
void setLogLevel(LogLevel level);

/**
 * Get current log level
 */
LogLevel getLogLevel();

/**
 * Log functions
 */
void logError(const char* tag, const char* fmt, ...);
void logWarn(const char* tag, const char* fmt, ...);
void logInfo(const char* tag, const char* fmt, ...);
void logDebug(const char* tag, const char* fmt, ...);
void logTrace(const char* tag, const char* fmt, ...);

/**
 * Log callback type
 */
using LogCallback = void (*)(int level, const char* tag, const char* message);

/**
 * Set custom log callback
 */
void setLogCallback(LogCallback callback);

} // namespace platform
} // namespace keyboard

#endif // KEYBOARD_NATIVE_HARMONY_LOG_H
