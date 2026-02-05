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

#ifndef HOSKEY_DIC_TRAVERSE_SESSION_NAPI_H
#define HOSKEY_DIC_TRAVERSE_SESSION_NAPI_H

#include "napi/native_api.h"

namespace latinime {

// Function to register dic traverse session functions with NAPI
napi_value RegisterDicTraverseSession(napi_env env);

} // namespace latinime

#endif // HOSKEY_DIC_TRAVERSE_SESSION_NAPI_H