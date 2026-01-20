#include "napi/native_api.h"
#include <vector>
#include <memory>

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
        }
    }
};

static napi_value NewDicTraverseSession(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 1) {
        napi_throw_error(env, nullptr, "Wrong number of arguments");
        return nullptr;
    }

    // Get the dictionary structure policy (external object)
    void* dictPolicyPtr;
    napi_get_value_external(env, args[0], &dictPolicyPtr);
    
    // Create a new DicTraverseSession instance
    // Using the factory method with default parameters
    DicTraverseSession *session = 
        static_cast<DicTraverseSession*>(DicTraverseSession::getSessionInstance("en_US", 1024));

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

    // Get session wrapper
    DicTraverseSessionWrapper* wrapper;
    napi_get_value_external(env, args[0], reinterpret_cast<void**>(&wrapper));
    if (wrapper) {
        if (wrapper->session) {
            DicTraverseSession::releaseSessionInstance(wrapper->session);
            wrapper->session = nullptr;
        }
    }

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