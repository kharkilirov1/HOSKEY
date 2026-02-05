/*
 * Copyright (C) 2013, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "log_utils.h"

#include <cstdio>
#include <stdarg.h>

#include "defines.h"

// HarmonyOS hilog support (optional)
#ifdef __OHOS__
#include <hilog/log.h>
#define LOG_DOMAIN 0x0000
#define LOG_TAG "LatinIME"
#endif

namespace latinime {

/* static */ void LogUtils::logInfo(const char *const format, ...) {
    static const int DEFAULT_LINE_SIZE = 256;
    char buffer[DEFAULT_LINE_SIZE];

    va_list argList;
    va_start(argList, format);
    const int size = vsnprintf(buffer, DEFAULT_LINE_SIZE, format, argList);
    va_end(argList);

    if (size >= DEFAULT_LINE_SIZE) {
        // Buffer was too small, allocate larger
        va_start(argList, format);
        char largeBuffer[size + 1];
        vsnprintf(largeBuffer, size + 1, format, argList);
        va_end(argList);
#ifdef __OHOS__
        OH_LOG_INFO(LOG_APP, "%{public}s", largeBuffer);
#else
        printf("[LatinIME] %s\n", largeBuffer);
#endif
    } else {
#ifdef __OHOS__
        OH_LOG_INFO(LOG_APP, "%{public}s", buffer);
#else
        printf("[LatinIME] %s\n", buffer);
#endif
    }
}

} // namespace latinime
