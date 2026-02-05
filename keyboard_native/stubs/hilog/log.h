/**
 * Stub for HarmonyOS hilog/log.h
 * Used for desktop build testing
 */

#ifndef HILOG_LOG_H_STUB
#define HILOG_LOG_H_STUB

#include <cstdio>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOG_DEBUG = 3,
    LOG_INFO = 4,
    LOG_WARN = 5,
    LOG_ERROR = 6,
    LOG_FATAL = 7
} LogLevel;

typedef enum {
    LOG_CORE = 0,
    LOG_APP = 1
} LogType;

#define OH_LOG_DEBUG(type, ...) ((void)printf("[DEBUG] " __VA_ARGS__), printf("\n"))
#define OH_LOG_INFO(type, ...) ((void)printf("[INFO] " __VA_ARGS__), printf("\n"))
#define OH_LOG_WARN(type, ...) ((void)printf("[WARN] " __VA_ARGS__), printf("\n"))
#define OH_LOG_ERROR(type, ...) ((void)printf("[ERROR] " __VA_ARGS__), printf("\n"))
#define OH_LOG_FATAL(type, ...) ((void)printf("[FATAL] " __VA_ARGS__), printf("\n"))

#define HILOG_DEBUG(type, ...) OH_LOG_DEBUG(type, __VA_ARGS__)
#define HILOG_INFO(type, ...) OH_LOG_INFO(type, __VA_ARGS__)
#define HILOG_WARN(type, ...) OH_LOG_WARN(type, __VA_ARGS__)
#define HILOG_ERROR(type, ...) OH_LOG_ERROR(type, __VA_ARGS__)
#define HILOG_FATAL(type, ...) OH_LOG_FATAL(type, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // HILOG_LOG_H_STUB
