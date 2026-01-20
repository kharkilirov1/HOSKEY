/*
 * Copyright (c) 2026 Project HOSKEY
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

#ifndef HOSKEY_CONSTANTS_H
#define HOSKEY_CONSTANTS_H

// Define constants that might be missing from the original OpenBoard implementation
#ifndef NOT_A_PROBABILITY
#define NOT_A_PROBABILITY -1
#endif

#ifndef MAX_RESULTS
#define MAX_RESULTS 100
#endif

#ifndef MAX_WORD_LENGTH
#define MAX_WORD_LENGTH 48
#endif

#ifndef CODE_POINT_BEGINNING_OF_SENTENCE
#define CODE_POINT_BEGINNING_OF_SENTENCE -100
#endif

#ifndef ASSERT
#define ASSERT(condition) ((void)0)
#endif

#ifndef AKLOGE
#define AKLOGE(fmt, ...) ((void)0)
#endif

#ifndef DEBUG_DICT
#define DEBUG_DICT 0
#endif

#ifndef NOT_AN_INDEX
#define NOT_AN_INDEX -1
#endif

#ifndef MAX_KEY_COUNT_IN_A_KEYBOARD
#define MAX_KEY_COUNT_IN_A_KEYBOARD 64
#endif

#endif // HOSKEY_CONSTANTS_H