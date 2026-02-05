/**
 * HOSKEY Keyboard - HarmonyOS Logging Implementation
 *
 * Copyright (c) 2024-2026 HOSKEY Project
 * Licensed under the Apache License, Version 2.0
 */

#include "harmony_log.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <mutex>

#ifdef __OHOS__
#include <hilog/log.h>
#define LOG_DOMAIN 0x0001
#endif

namespace keyboard {
namespace platform {

static LogLevel g_logLevel = LogLevel::Info;
static LogCallback g_logCallback = nullptr;
static std::mutex g_logMutex;

void setLogLevel(LogLevel level) {
    g_logLevel = level;
}

LogLevel getLogLevel() {
    return g_logLevel;
}

void setLogCallback(LogCallback callback) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_logCallback = callback;
}

static void logInternal(LogLevel level, const char* tag, const char* fmt, va_list args) {
    if (level > g_logLevel) {
        return;
    }

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);

    std::lock_guard<std::mutex> lock(g_logMutex);

    // Custom callback
    if (g_logCallback) {
        g_logCallback(static_cast<int>(level), tag, buffer);
        return;
    }

#ifdef __OHOS__
    // HarmonyOS hilog
    switch (level) {
        case LogLevel::Error:
            OH_LOG_ERROR(LOG_TYPE_APP, "[%{public}s] %{public}s", tag, buffer);
            break;
        case LogLevel::Warn:
            OH_LOG_WARN(LOG_TYPE_APP, "[%{public}s] %{public}s", tag, buffer);
            break;
        case LogLevel::Info:
            OH_LOG_INFO(LOG_TYPE_APP, "[%{public}s] %{public}s", tag, buffer);
            break;
        case LogLevel::Debug:
            OH_LOG_DEBUG(LOG_TYPE_APP, "[%{public}s] %{public}s", tag, buffer);
            break;
        case LogLevel::Trace:
            OH_LOG_DEBUG(LOG_TYPE_APP, "[TRACE][%{public}s] %{public}s", tag, buffer);
            break;
        default:
            break;
    }
#else
    // Standard output for non-HarmonyOS
    const char* levelStr = "INFO";
    FILE* out = stdout;

    switch (level) {
        case LogLevel::Error:
            levelStr = "ERROR";
            out = stderr;
            break;
        case LogLevel::Warn:
            levelStr = "WARN";
            break;
        case LogLevel::Info:
            levelStr = "INFO";
            break;
        case LogLevel::Debug:
            levelStr = "DEBUG";
            break;
        case LogLevel::Trace:
            levelStr = "TRACE";
            break;
        default:
            break;
    }

    fprintf(out, "[%s][%s] %s\n", levelStr, tag, buffer);
#endif
}

void logError(const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    logInternal(LogLevel::Error, tag, fmt, args);
    va_end(args);
}

void logWarn(const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    logInternal(LogLevel::Warn, tag, fmt, args);
    va_end(args);
}

void logInfo(const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    logInternal(LogLevel::Info, tag, fmt, args);
    va_end(args);
}

void logDebug(const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    logInternal(LogLevel::Debug, tag, fmt, args);
    va_end(args);
}

void logTrace(const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    logInternal(LogLevel::Trace, tag, fmt, args);
    va_end(args);
}

} // namespace platform
} // namespace keyboard
