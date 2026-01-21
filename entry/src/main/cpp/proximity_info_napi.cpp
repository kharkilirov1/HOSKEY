#include "napi/native_api.h"
#include <vector>
#include <memory>
#include <string>

// HarmonyOS logging
#include <hilog/log.h>

#undef LOG_DOMAIN
#undef LOG_TAG
#define LOG_DOMAIN 0x0001
#define LOG_TAG "HOSKEY-NATIVE"

#include "suggest/core/layout/proximity_info.h"
#include "napi_helpers.h"
#include "constants.h"

using namespace hoskey;

namespace latinime {

// NAPI wrapper for the proximity info instance
struct ProximityInfoWrapper {
    ProximityInfo* proximityInfo;
    ~ProximityInfoWrapper() {
        if (proximityInfo) {
            delete proximityInfo;
            proximityInfo = nullptr;  // Prevent dangling pointer
        }
    }
};

/**
 * Safely convert NAPI string to std::string
 * - Checks napi_status at each step
 * - Handles empty strings correctly
 * @returns empty string on any error
 */
static std::string NapiStringToString(napi_env env, napi_value strValue) {
    if (env == nullptr || strValue == nullptr) {
        OH_LOG_ERROR(LOG_APP, "NapiStringToString: null env or value");
        return "";
    }

    size_t requiredLength = 0;
    napi_status status = napi_get_value_string_utf8(env, strValue, nullptr, 0, &requiredLength);
    if (status != napi_ok) {
        OH_LOG_ERROR(LOG_APP, "NapiStringToString: failed to get length, status=%d", status);
        return "";
    }

    if (requiredLength == 0) {
        return "";
    }

    std::vector<char> buffer(requiredLength + 1, '\0');
    size_t copiedLength = 0;
    status = napi_get_value_string_utf8(env, strValue, buffer.data(), buffer.size(), &copiedLength);
    if (status != napi_ok) {
        OH_LOG_ERROR(LOG_APP, "NapiStringToString: failed to copy, status=%d", status);
        return "";
    }

    return std::string(buffer.data(), copiedLength);
}

static napi_value SetProximityInfo(napi_env env, napi_callback_info info) {
    size_t argc = 16;
    napi_value args[16];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 16) {
        napi_throw_error(env, nullptr, "Wrong number of arguments");
        return nullptr;
    }

    // Extract parameters
    int32_t displayWidth, displayHeight, gridWidth, gridHeight;
    int32_t mostCommonkeyWidth, mostCommonkeyHeight, keyCount;
    
    napi_get_value_int32(env, args[0], &displayWidth);
    napi_get_value_int32(env, args[1], &displayHeight);
    napi_get_value_int32(env, args[2], &gridWidth);
    napi_get_value_int32(env, args[3], &gridHeight);
    napi_get_value_int32(env, args[4], &mostCommonkeyWidth);
    napi_get_value_int32(env, args[5], &mostCommonkeyHeight);
    napi_get_value_int32(env, args[7], &keyCount); // keyCount is at index 7

    // Create proximity info instance
    // The constructor expects napi_env and napi_value parameters, which we have
    ProximityInfo *proximityInfo = new ProximityInfo(env, displayWidth, displayHeight,
            gridWidth, gridHeight, mostCommonkeyWidth, mostCommonkeyHeight,
            args[6], keyCount, args[8], args[9], args[10], args[11], args[12],
            args[13], args[14], args[15]);

    // Wrap in external value
    ProximityInfoWrapper* wrapper = new ProximityInfoWrapper{proximityInfo};
    napi_value result;
    napi_create_external(env, wrapper, 
        [](napi_env env, void* data, void* hint) {
            delete static_cast<ProximityInfoWrapper*>(data);
        }, 
        nullptr, &result);

    return result;
}

static napi_value ReleaseProximityInfo(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 1) {
        napi_throw_error(env, nullptr, "Wrong number of arguments");
        return nullptr;
    }

    // Get proximity info wrapper - safely handle null/invalid
    ProximityInfoWrapper* wrapper = nullptr;
    napi_status status = napi_get_value_external(env, args[0], reinterpret_cast<void**>(&wrapper));

    if (status != napi_ok || wrapper == nullptr) {
        OH_LOG_WARN(LOG_APP, "ReleaseProximityInfo: invalid or null wrapper");
        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    // Safely release proximity info (destructor handles null check)
    if (wrapper->proximityInfo != nullptr) {
        OH_LOG_INFO(LOG_APP, "ReleaseProximityInfo: releasing proximity info");
        delete wrapper->proximityInfo;
        wrapper->proximityInfo = nullptr;
    } else {
        OH_LOG_WARN(LOG_APP, "ReleaseProximityInfo: already released (no double-free)");
    }
    // Note: wrapper itself is freed by NAPI finalizer callback

    napi_value result;
    napi_get_undefined(env, &result);
    return result;
}

// Export functions for module registration
napi_value RegisterProximityInfo(napi_env env) {
    napi_value exports;
    napi_create_object(env, &exports);
    
    napi_property_descriptor descriptors[] = {
        { "setProximityInfo", nullptr, SetProximityInfo, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "release", nullptr, ReleaseProximityInfo, nullptr, nullptr, nullptr, napi_default, nullptr }
    };

    napi_define_properties(env, exports, sizeof(descriptors) / sizeof(descriptors[0]), descriptors);
    return exports;
}

} // namespace latinime