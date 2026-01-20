/*
 * Copyright (c) [2026] [Your Name or Project HOSKEY]
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

#include "napi_helpers.h"
#include <cstring> // for memset

namespace hoskey {

bool napiGetIntArrayRegion(napi_env env, napi_value jArray,
                           uint32_t start, uint32_t len, int32_t* buffer) {
    if (!buffer || len == 0) {
        return false;
    }

    uint32_t arrayLen = 0;
    napi_status status = napi_get_array_length(env, jArray, &arrayLen);
    if (status != napi_ok || start + len > arrayLen) {
        // If array is invalid or too short, fill with zeros as a safe fallback
        memset(buffer, 0, len * sizeof(int32_t));
        return false;
    }

    for (uint32_t i = 0; i < len; ++i) {
        napi_value element;
        status = napi_get_element(env, jArray, start + i, &element);
        if (status != napi_ok) {
            buffer[i] = 0;
            continue;
        }

        int32_t value;
        status = napi_get_value_int32(env, element, &value);
        if (status != napi_ok) {
            buffer[i] = 0;
        } else {
            buffer[i] = value;
        }
    }
    return true;
}

bool napiGetFloatArrayRegion(napi_env env, napi_value jArray,
                             uint32_t start, uint32_t len, float* buffer) {
    if (!buffer || len == 0) {
        return false;
    }

    uint32_t arrayLen = 0;
    napi_status status = napi_get_array_length(env, jArray, &arrayLen);
    if (status != napi_ok || start + len > arrayLen) {
        memset(buffer, 0, len * sizeof(float));
        return false;
    }

    for (uint32_t i = 0; i < len; ++i) {
        napi_value element;
        status = napi_get_element(env, jArray, start + i, &element);
        if (status != napi_ok) {
            buffer[i] = 0.0f;
            continue;
        }

        double value;
        status = napi_get_value_double(env, element, &value);
        if (status != napi_ok) {
            buffer[i] = 0.0f;
        } else {
            buffer[i] = static_cast<float>(value);
        }
    }
    return true;
}

bool napiSetIntArrayRegion(napi_env env, napi_value jArray,
                           uint32_t start, uint32_t len, const int32_t* buffer) {
    if (!buffer || len == 0) {
        return false;
    }

    uint32_t arrayLen = 0;
    napi_status status = napi_get_array_length(env, jArray, &arrayLen);
    if (status != napi_ok || start + len > arrayLen) {
        return false;
    }

    for (uint32_t i = 0; i < len; ++i) {
        napi_value element;
        status = napi_create_int32(env, buffer[i], &element);
        if (status != napi_ok) {
            continue;
        }
        napi_set_element(env, jArray, start + i, element);
    }
    return true;
}

bool napiSetFloatArrayRegion(napi_env env, napi_value jArray,
                             uint32_t start, uint32_t len, const float* buffer) {
    if (!buffer || len == 0) {
        return false;
    }

    uint32_t arrayLen = 0;
    napi_status status = napi_get_array_length(env, jArray, &arrayLen);
    if (status != napi_ok || start + len > arrayLen) {
        return false;
    }

    for (uint32_t i = 0; i < len; ++i) {
        napi_value element;
        status = napi_create_double(env, static_cast<double>(buffer[i]), &element);
        if (status != napi_ok) {
            continue;
        }
        napi_set_element(env, jArray, start + i, element);
    }
    return true;
}

napi_value napiCreateInt(napi_env env, int32_t value) {
    napi_value result;
    napi_status status = napi_create_int32(env, value, &result);
    if (status != napi_ok) {
        return nullptr;
    }
    return result;
}

napi_value napiCreateDouble(napi_env env, double value) {
    napi_value result;
    napi_status status = napi_create_double(env, value, &result);
    if (status != napi_ok) {
        return nullptr;
    }
    return result;
}

bool napiIsNullOrUndefined(napi_env env, napi_value value) {
    if (value == nullptr) {
        return true;
    }
    napi_valuetype type;
    napi_status status = napi_typeof(env, value, &type);
    if (status != napi_ok) {
        return true; // Treat as null on error
    }
    return (type == napi_null || type == napi_undefined);
}

uint32_t napiGetArrayLengthChecked(napi_env env, napi_value array) {
    if (napiIsNullOrUndefined(env, array)) {
        return 0;
    }

    bool isArray = false;
    napi_is_array(env, array, &isArray);
    if (!isArray) {
        return 0;
    }

    uint32_t length = 0;
    napi_status status = napi_get_array_length(env, array, &length);
    if (status != napi_ok) {
        return 0;
    }
    return length;
}

napi_value napiCreateIntArray(napi_env env, const int32_t* buffer, uint32_t len) {
    napi_value resultArray;
    napi_status status = napi_create_array_with_length(env, len, &resultArray);
    if (status != napi_ok) {
        return nullptr;
    }

    for (uint32_t i = 0; i < len; ++i) {
        napi_value element;
        status = napi_create_int32(env, buffer[i], &element);
        if (status != napi_ok) {
            // In case of error, we still return the partially filled array
            break;
        }
        napi_set_element(env, resultArray, i, element);
    }
    return resultArray;
}

napi_value napiCreateFloatArray(napi_env env, const float* buffer, uint32_t len) {
    napi_value resultArray;
    napi_status status = napi_create_array_with_length(env, len, &resultArray);
    if (status != napi_ok) {
        return nullptr;
    }

    for (uint32_t i = 0; i < len; ++i) {
        napi_value element;
        status = napi_create_double(env, static_cast<double>(buffer[i]), &element);
        if (status != napi_ok) {
            break;
        }
        napi_set_element(env, resultArray, i, element);
    }
    return resultArray;
}

} // namespace hoskey