/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef LATINIME_LOG_UTILS_H
#define LATINIME_LOG_UTILS_H

#include "defines.h"

namespace latinime {

class LogUtils {
 public:
    // HarmonyOS port: Removed JNI dependency, now uses simple printf logging
    static void logInfo(const char *const format, ...)
#ifdef __GNUC__
        __attribute__ ((format (printf, 1, 2)))
#endif // __GNUC__
        ;

 private:
    DISALLOW_COPY_AND_ASSIGN(LogUtils);
};
} // namespace latinime
#endif // LATINIME_LOG_UTILS_H
