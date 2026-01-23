#include "napi/native_api.h"
#include <vector>
#include <memory>
#include <string>

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
        }
    }
};

// Helper function to convert NAPI string to std::string
static std::string NapiStringToString(napi_env env, napi_value strValue) {
    size_t length = 0;
    napi_get_value_string_utf8(env, strValue, nullptr, 0, &length);
    std::string result(length, '\0');
    napi_get_value_string_utf8(env, strValue, &result[0], length + 1, &length);
    return result;
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

    // Get proximity info wrapper
    ProximityInfoWrapper* wrapper;
    napi_get_value_external(env, args[0], reinterpret_cast<void**>(&wrapper));
    if (wrapper) {
        if (wrapper->proximityInfo) {
            delete wrapper->proximityInfo;
            wrapper->proximityInfo = nullptr;
        }
        // Note: We don't delete the wrapper here because the external reference handles that
    }

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