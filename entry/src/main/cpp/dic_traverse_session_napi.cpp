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

#include "suggest/core/session/dic_traverse_session.h"
#include "napi_helpers.h"
#include "constants.h"

using namespace hoskey;

namespace latinime {

// NAPI wrapper for the dic traverse session instance
struct DicTraverseSessionWrapper {
    DicTraverseSession* session;
    ~DicTraverseSessionWrapper() {
        if (session) {
            DicTraverseSession::releaseSessionInstance(session);
            session = nullptr;  // Prevent dangling pointer
        }
    }
};

/**
 * Safely get locale string from NAPI value
 * @returns empty string on error
 */
static std::string SafeGetLocale(napi_env env, napi_value value) {
    if (env == nullptr || value == nullptr) {
        OH_LOG_ERROR(LOG_APP, "SafeGetLocale: null env or value");
        return "";
    }

    size_t requiredLength = 0;
    napi_status status = napi_get_value_string_utf8(env, value, nullptr, 0, &requiredLength);
    if (status != napi_ok) {
        OH_LOG_ERROR(LOG_APP, "SafeGetLocale: failed to get length, status=%d", status);
        return "";
    }

    if (requiredLength == 0) {
        return "";
    }

    std::vector<char> buffer(requiredLength + 1, '\0');
    size_t copiedLength = 0;
    status = napi_get_value_string_utf8(env, value, buffer.data(), buffer.size(), &copiedLength);
    if (status != napi_ok) {
        OH_LOG_ERROR(LOG_APP, "SafeGetLocale: failed to copy, status=%d", status);
        return "";
    }

    return std::string(buffer.data(), copiedLength);
}

static napi_value NewDicTraverseSession(napi_env env, napi_callback_info info) {
    size_t argc = 2;  // Now 2 arguments: locale and dictSize
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 2) {
        napi_throw_error(env, nullptr, "Wrong number of arguments: expected locale and dictSize");
        return nullptr;
    }

    // Get locale string safely
    std::string locale = SafeGetLocale(env, args[0]);
    
    // Get dictSize
    int64_t dictSize;
    napi_get_value_int64(env, args[1], &dictSize);
    
    // Use factory method
    DicTraverseSession *session = static_cast<DicTraverseSession*>(
        DicTraverseSession::getSessionInstance(locale.c_str(), dictSize));

    // Wrap in external value
    DicTraverseSessionWrapper* wrapper = new DicTraverseSessionWrapper{session};
    napi_value result;
    napi_create_external(env, wrapper, 
        [](napi_env env, void* data, void* hint) {
            delete static_cast<DicTraverseSessionWrapper*>(data);
        }, 
        nullptr, &result);

    return result;
}

static napi_value ReleaseDicTraverseSession(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 1) {
        napi_throw_error(env, nullptr, "Wrong number of arguments");
        return nullptr;
    }

    // Get session wrapper - safely handle null/invalid
    DicTraverseSessionWrapper* wrapper = nullptr;
    napi_status status = napi_get_value_external(env, args[0], reinterpret_cast<void**>(&wrapper));

    if (status != napi_ok || wrapper == nullptr) {
        OH_LOG_WARN(LOG_APP, "ReleaseDicTraverseSession: invalid or null wrapper");
        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    // Safely release session (check for already-released to prevent double-free)
    if (wrapper->session != nullptr) {
        OH_LOG_INFO(LOG_APP, "ReleaseDicTraverseSession: releasing session");
        DicTraverseSession::releaseSessionInstance(wrapper->session);
        wrapper->session = nullptr;
    } else {
        OH_LOG_WARN(LOG_APP, "ReleaseDicTraverseSession: already released (no double-free)");
    }
    // Note: wrapper itself is freed by NAPI finalizer callback

    napi_value result;
    napi_get_undefined(env, &result);
    return result;
}

static napi_value GetPrevWord(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 1) {
        napi_throw_error(env, nullptr, "Wrong number of arguments");
        return nullptr;
    }

    // Get session wrapper
    DicTraverseSessionWrapper* wrapper;
    napi_get_value_external(env, args[0], reinterpret_cast<void**>(&wrapper));
    if (!wrapper || !wrapper->session) {
        napi_value result;
        napi_get_null(env, &result);
        return result;
    }

    // Since we can't directly access the prev word from the session,
    // we'll return an empty string for now
    napi_value result;
    napi_create_string_utf8(env, "", NAPI_AUTO_LENGTH, &result);
    return result;
}

// Export functions for module registration
napi_value RegisterDicTraverseSession(napi_env env) {
    napi_value exports;
    napi_create_object(env, &exports);
    
    napi_property_descriptor descriptors[] = {
        { "newSession", nullptr, NewDicTraverseSession, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "release", nullptr, ReleaseDicTraverseSession, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "getPrevWord", nullptr, GetPrevWord, nullptr, nullptr, nullptr, napi_default, nullptr }
    };

    napi_define_properties(env, exports, sizeof(descriptors) / sizeof(descriptors[0]), descriptors);
    return exports;
}

} // namespace latinime