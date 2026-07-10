#include <atomic>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <napi/native_api.h>

#include "../hosdict_engine/keyboard_engine.h"

namespace {

using hoskey::engine::CheckWordRequest;
using hoskey::engine::CheckWordResponse;
using hoskey::engine::KeyboardEngine;
using hoskey::engine::KeyboardDebugConfig;
using hoskey::engine::LoadReport;
using hoskey::engine::PredictorModeCompareResponse;
using hoskey::engine::PredictorOverlayMode;
using hoskey::engine::Status;
using hoskey::engine::SuggestRequest;
using hoskey::engine::SuggestResponse;
using hoskey::engine::Suggestion;

std::mutex g_mutex;
std::mutex g_diagnostics_mutex;
std::atomic_int g_open_jobs{0};
KeyboardEngine g_engine;
std::string g_diagnostics;

void SetDiagnostics(const std::string& text) {
  std::lock_guard<std::mutex> lock(g_diagnostics_mutex);
  g_diagnostics = text;
}

std::string GetDiagnosticsSnapshot() {
  std::lock_guard<std::mutex> lock(g_diagnostics_mutex);
  return g_diagnostics;
}

bool IsEngineOpening() {
  return g_open_jobs.load() > 0;
}

std::string FormatStatus(const Status& status) {
  if (status.ok()) {
    return "OK";
  }
  return "code=" + std::to_string(static_cast<int>(status.code)) + " message=" + status.message;
}

std::string ReadString(napi_env env, napi_value value) {
  size_t length = 0;
  napi_get_value_string_utf8(env, value, nullptr, 0, &length);
  std::string result(length + 1, '\0');
  napi_get_value_string_utf8(env, value, result.data(), result.size(), &length);
  result.resize(length);
  return result;
}

bool ReadBoolProperty(napi_env env, napi_value object, const char* key, bool default_value) {
  napi_value value;
  bool has_property = false;
  napi_has_named_property(env, object, key, &has_property);
  if (!has_property) {
    return default_value;
  }
  napi_get_named_property(env, object, key, &value);
  bool result = default_value;
  napi_get_value_bool(env, value, &result);
  return result;
}

uint32_t ReadUInt32Property(napi_env env, napi_value object, const char* key, uint32_t default_value) {
  napi_value value;
  bool has_property = false;
  napi_has_named_property(env, object, key, &has_property);
  if (!has_property) {
    return default_value;
  }
  napi_get_named_property(env, object, key, &value);
  uint32_t result = default_value;
  napi_get_value_uint32(env, value, &result);
  return result;
}

double ReadDoubleProperty(napi_env env, napi_value object, const char* key, double default_value) {
  napi_value value;
  bool has_property = false;
  napi_has_named_property(env, object, key, &has_property);
  if (!has_property) {
    return default_value;
  }
  napi_get_named_property(env, object, key, &value);
  double result = default_value;
  napi_get_value_double(env, value, &result);
  return result;
}

std::string ReadStringProperty(napi_env env, napi_value object, const char* key, const std::string& default_value) {
  napi_value value;
  bool has_property = false;
  napi_has_named_property(env, object, key, &has_property);
  if (!has_property) {
    return default_value;
  }
  napi_get_named_property(env, object, key, &value);
  return ReadString(env, value);
}

std::vector<std::string> ReadStringArrayProperty(napi_env env, napi_value object, const char* key) {
  napi_value value;
  bool has_property = false;
  napi_has_named_property(env, object, key, &has_property);
  if (!has_property) {
    return {};
  }

  napi_get_named_property(env, object, key, &value);
  bool is_array = false;
  napi_is_array(env, value, &is_array);
  if (!is_array) {
    return {};
  }

  uint32_t length = 0;
  napi_get_array_length(env, value, &length);
  std::vector<std::string> result;
  result.reserve(length);
  for (uint32_t index = 0; index < length; ++index) {
    napi_value element;
    napi_get_element(env, value, index, &element);
    result.push_back(ReadString(env, element));
  }
  return result;
}

template <typename T, typename Fn>
std::vector<T> ReadObjectArrayProperty(napi_env env, napi_value object, const char* key, Fn read_item) {
  napi_value value;
  bool has_property = false;
  napi_has_named_property(env, object, key, &has_property);
  if (!has_property) {
    return {};
  }

  napi_get_named_property(env, object, key, &value);
  bool is_array = false;
  napi_is_array(env, value, &is_array);
  if (!is_array) {
    return {};
  }

  uint32_t length = 0;
  napi_get_array_length(env, value, &length);
  std::vector<T> result;
  result.reserve(length);
  for (uint32_t index = 0; index < length; ++index) {
    napi_value element;
    napi_get_element(env, value, index, &element);
    result.push_back(read_item(element));
  }
  return result;
}

PredictorOverlayMode ParsePredictorOverlayMode(const std::string& value) {
  if (value == "split" || value == "split-only" || value == "split_only") {
    return PredictorOverlayMode::kSplitOnly;
  }
  if (value == "all" || value == "on" || value == "true" || value == "1") {
    return PredictorOverlayMode::kAll;
  }
  return PredictorOverlayMode::kDisabled;
}

KeyboardDebugConfig ReadKeyboardDebugConfig(napi_env env, napi_value object) {
  KeyboardDebugConfig config;
  napi_valuetype type = napi_undefined;
  napi_typeof(env, object, &type);
  if (type != napi_object) {
    return config;
  }

  napi_value overlay_value;
  bool has_overlay_mode = false;
  napi_has_named_property(env, object, "predictorOverlayMode", &has_overlay_mode);
  if (has_overlay_mode) {
    napi_get_named_property(env, object, "predictorOverlayMode", &overlay_value);
    config.override_predictor_overlay_mode = true;
    config.predictor_overlay_mode =
        ParsePredictorOverlayMode(ReadString(env, overlay_value));
  }

  napi_value allowlist_value;
  bool has_allowlist = false;
  napi_has_named_property(env, object, "predictorSplitAllowlist", &has_allowlist);
  if (has_allowlist) {
    napi_get_named_property(env, object, "predictorSplitAllowlist", &allowlist_value);
    bool enabled = false;
    napi_get_value_bool(env, allowlist_value, &enabled);
    config.override_predictor_split_allowlist = true;
    config.predictor_split_allowlist = enabled;
  }
  return config;
}

SuggestRequest ReadSuggestRequest(napi_env env, napi_value object) {
  SuggestRequest request;
  request.token = ReadStringProperty(env, object, "token", "");
  request.previous_tokens = ReadStringArrayProperty(env, object, "previousTokens");
  request.locale = ReadStringProperty(env, object, "locale", "ru-RU");
  request.want_autocorrect = ReadBoolProperty(env, object, "wantAutocorrect", true);
  // Clamp maxSuggestions: napi_get_value_uint32 coerces a negative JS number to
  // a value near 4.29e9, which would drive a huge allocation in the engine.
  const uint32_t requested_max = ReadUInt32Property(env, object, "maxSuggestions", 6);
  request.max_suggestions = requested_max > 64u ? 64u : requested_max;
  request.input = ReadStringProperty(env, object, "input", "");
  request.full_context = ReadStringProperty(env, object, "fullContext", "");
  request.options_array = ReadStringArrayProperty(env, object, "optionsArray");
  request.emoji_factor = ReadDoubleProperty(env, object, "emojiFactor", 0.0);
  request.package_name = ReadStringProperty(env, object, "packageName", "");
  request.is_urgent = ReadBoolProperty(env, object, "isUrgent", false);
  request.field_action_id = ReadUInt32Property(env, object, "fieldActionId", 0);
  request.gender = ReadUInt32Property(env, object, "gender", 0);
  request.age = ReadUInt32Property(env, object, "age", 0);

  request.prev_words_info = ReadObjectArrayProperty<SuggestRequest::PrevWordInfo>(
      env, object, "prevWordsInfo",
      [&](napi_value item) {
        SuggestRequest::PrevWordInfo info;
        info.token = ReadStringProperty(env, item, "token", "");
        info.was_autocorrected =
            ReadBoolProperty(env, item, "wasAutocorrected", false);
        return info;
      });

  request.dictionary_sessions =
      ReadObjectArrayProperty<SuggestRequest::DictionarySession>(
          env, object, "dictionarySessions",
          [&](napi_value item) {
            SuggestRequest::DictionarySession session;
            session.id = ReadStringProperty(env, item, "id", "");
            session.locale = ReadStringProperty(env, item, "locale", "");
            return session;
          });

  napi_value proximity_value;
  bool has_proximity = false;
  napi_has_named_property(env, object, "proximityInfo", &has_proximity);
  if (has_proximity) {
    napi_get_named_property(env, object, "proximityInfo", &proximity_value);
    request.proximity_info.layout =
        ReadStringProperty(env, proximity_value, "layout", "");
    request.proximity_info.keyboard_width =
        ReadUInt32Property(env, proximity_value, "keyboardWidth", 0);
    request.proximity_info.keyboard_height =
        ReadUInt32Property(env, proximity_value, "keyboardHeight", 0);
    request.proximity_info.enabled =
        ReadBoolProperty(env, proximity_value, "enabled", false);
  }

  request.points = ReadObjectArrayProperty<SuggestRequest::TouchPoint>(
      env, object, "points",
      [&](napi_value item) {
        SuggestRequest::TouchPoint point;
        point.x = ReadDoubleProperty(env, item, "x", 0.0);
        point.y = ReadDoubleProperty(env, item, "y", 0.0);
        point.time_ms = ReadUInt32Property(env, item, "timeMs", 0);
        point.key_code = ReadUInt32Property(env, item, "keyCode", 0);
        return point;
      });
  return request;
}

CheckWordRequest ReadCheckWordRequest(napi_env env, napi_value object) {
  CheckWordRequest request;
  request.token = ReadStringProperty(env, object, "token", "");
  request.locale = ReadStringProperty(env, object, "locale", "ru-RU");
  return request;
}

void SetNamedString(napi_env env, napi_value object, const char* key, const std::string& value) {
  napi_value napi_string;
  napi_create_string_utf8(env, value.c_str(), value.size(), &napi_string);
  napi_set_named_property(env, object, key, napi_string);
}

void SetNamedBool(napi_env env, napi_value object, const char* key, bool value) {
  napi_value napi_bool;
  napi_get_boolean(env, value, &napi_bool);
  napi_set_named_property(env, object, key, napi_bool);
}

void SetNamedUInt32(napi_env env, napi_value object, const char* key, uint32_t value) {
  napi_value napi_number;
  napi_create_uint32(env, value, &napi_number);
  napi_set_named_property(env, object, key, napi_number);
}

void SetNamedDouble(napi_env env, napi_value object, const char* key, double value) {
  napi_value napi_number;
  napi_create_double(env, value, &napi_number);
  napi_set_named_property(env, object, key, napi_number);
}

napi_value MakeStringArray(napi_env env, const std::vector<std::string>& values) {
  napi_value array;
  napi_create_array_with_length(env, values.size(), &array);
  for (uint32_t index = 0; index < values.size(); ++index) {
    napi_value value;
    napi_create_string_utf8(env, values[index].c_str(), values[index].size(), &value);
    napi_set_element(env, array, index, value);
  }
  return array;
}

napi_value MakeLoadReportObject(napi_env env, const LoadReport& report) {
  napi_value object;
  napi_create_object(env, &object);
  SetNamedString(env, object, "assetKey", report.asset_key);
  SetNamedString(env, object, "format", report.format);
  SetNamedDouble(env, object, "byteSize", static_cast<double>(report.byte_size));
  napi_set_named_property(env, object, "notes", MakeStringArray(env, report.notes));
  return object;
}

napi_value MakeLoadReportArray(napi_env env, const std::vector<LoadReport>& reports) {
  napi_value array;
  napi_create_array_with_length(env, reports.size(), &array);
  for (uint32_t index = 0; index < reports.size(); ++index) {
    napi_set_element(env, array, index, MakeLoadReportObject(env, reports[index]));
  }
  return array;
}

napi_value MakeSuggestionObject(napi_env env, const Suggestion& suggestion) {
  napi_value object;
  napi_create_object(env, &object);
  SetNamedString(env, object, "text", suggestion.text);
  SetNamedUInt32(env, object, "typeFlags", suggestion.type_flags);
  SetNamedUInt32(env, object, "groupId", suggestion.group_id);
  SetNamedString(env, object, "tag", suggestion.tag);
  SetNamedDouble(env, object, "score", suggestion.score);
  return object;
}

napi_value MakeSuggestResponse(napi_env env, const SuggestResponse& response) {
  napi_value object;
  napi_create_object(env, &object);

  SetNamedBool(env, object, "ok", response.status.ok());
  SetNamedUInt32(env, object, "statusCode", static_cast<uint32_t>(response.status.code));
  SetNamedString(env, object, "statusMessage", response.status.message);
  SetNamedBool(env, object, "willAutocorrect", response.will_autocorrect);
  SetNamedBool(env, object, "isBlocked", response.is_blocked);
  SetNamedString(env, object, "diagnostics", response.diagnostics);

  napi_value suggestions;
  napi_create_array_with_length(env, response.suggestions.size(), &suggestions);
  for (uint32_t index = 0; index < response.suggestions.size(); ++index) {
    napi_set_element(env, suggestions, index, MakeSuggestionObject(env, response.suggestions[index]));
  }
  napi_set_named_property(env, object, "suggestions", suggestions);
  return object;
}

napi_value MakeCheckWordResponse(napi_env env, const CheckWordResponse& response) {
  napi_value object;
  napi_create_object(env, &object);
  SetNamedBool(env, object, "ok", response.status.ok());
  SetNamedUInt32(env, object, "statusCode", static_cast<uint32_t>(response.status.code));
  SetNamedString(env, object, "statusMessage", response.status.message);
  SetNamedBool(env, object, "isKnown", response.is_known);
  SetNamedBool(env, object, "isBlocked", response.is_blocked);
  SetNamedString(env, object, "normalizedForm", response.normalized_form);
  SetNamedString(env, object, "diagnostics", response.diagnostics);
  return object;
}

napi_value MakePredictorModeCompareResponse(napi_env env,
                                           const PredictorModeCompareResponse& response) {
  napi_value object;
  napi_create_object(env, &object);
  SetNamedBool(env, object, "ok", response.status.ok());
  SetNamedUInt32(env, object, "statusCode",
                 static_cast<uint32_t>(response.status.code));
  SetNamedString(env, object, "statusMessage", response.status.message);
  SetNamedBool(env, object, "changed", response.changed);
  SetNamedString(env, object, "diagnostics", response.diagnostics);
  napi_set_named_property(env, object, "baseline",
                          MakeSuggestResponse(env, response.baseline));
  napi_set_named_property(env, object, "verifiedSplit",
                          MakeSuggestResponse(env, response.verified_split));
  return object;
}

napi_value NapiOpen(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value args[2];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

  bool ok = false;
  std::string diagnostics;
  if (argc == 0) {
    diagnostics = "open requires packageRoot";
  } else {
    const std::string package_root = ReadString(env, args[0]);
    KeyboardDebugConfig debug_config;
    if (argc >= 2) {
      debug_config = ReadKeyboardDebugConfig(env, args[1]);
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    const Status status = g_engine.Open(package_root, debug_config);
    ok = status.ok();
    diagnostics = "open(" + package_root + "): " + FormatStatus(status);
    if (ok) {
      diagnostics += ", assets=" + std::to_string(g_engine.package().store().LoadedKeys().size());
      const std::string package_diag = g_engine.package().DiagnosticsSummary();
      if (!package_diag.empty()) {
        diagnostics += ", reports=" + package_diag;
      }
    }
  }
  SetDiagnostics(diagnostics);

  napi_value result;
  napi_get_boolean(env, ok, &result);
  return result;
}

struct OpenAsyncWork {
  napi_async_work work = nullptr;
  napi_deferred deferred = nullptr;
  std::string package_root;
  KeyboardDebugConfig debug_config;
  bool ok = false;
  std::string diagnostics;
};

void ExecuteOpenAsync(napi_env /*env*/, void* data) {
  auto* work = static_cast<OpenAsyncWork*>(data);
  if (!work) return;

  std::lock_guard<std::mutex> lock(g_mutex);
  const Status status = g_engine.Open(work->package_root, work->debug_config);
  work->ok = status.ok();
  work->diagnostics = "openAsync(" + work->package_root + "): " +
                      FormatStatus(status);
  if (work->ok) {
    work->diagnostics += ", assets=" +
                         std::to_string(g_engine.package().store().LoadedKeys().size());
    const std::string package_diag = g_engine.package().DiagnosticsSummary();
    if (!package_diag.empty()) {
      work->diagnostics += ", reports=" + package_diag;
    }
  }
}

void CompleteOpenAsync(napi_env env, napi_status status, void* data) {
  auto* work = static_cast<OpenAsyncWork*>(data);
  if (!work) return;

  g_open_jobs.fetch_sub(1);

  if (status != napi_ok && work->diagnostics.empty()) {
    work->ok = false;
    work->diagnostics = "openAsync cancelled or failed before execution";
  }
  SetDiagnostics(work->diagnostics);

  napi_value result;
  napi_get_boolean(env, work->ok, &result);
  napi_resolve_deferred(env, work->deferred, result);
  napi_delete_async_work(env, work->work);
  delete work;
}

napi_value NapiOpenAsync(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value args[2];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

  napi_value promise;
  napi_deferred deferred;
  napi_create_promise(env, &deferred, &promise);

  if (argc == 0) {
    SetDiagnostics("openAsync requires packageRoot");
    napi_value result;
    napi_get_boolean(env, false, &result);
    napi_resolve_deferred(env, deferred, result);
    return promise;
  }

  auto* work = new OpenAsyncWork();
  work->deferred = deferred;
  work->package_root = ReadString(env, args[0]);
  if (argc >= 2) {
    work->debug_config = ReadKeyboardDebugConfig(env, args[1]);
  }

  napi_value resource_name;
  napi_create_string_utf8(env, "HoskeyEngineOpen", NAPI_AUTO_LENGTH,
                          &resource_name);
  napi_status create_status = napi_create_async_work(
      env, nullptr, resource_name, ExecuteOpenAsync, CompleteOpenAsync, work,
      &work->work);
  if (create_status != napi_ok) {
    delete work;
    SetDiagnostics("openAsync failed to create async work");
    napi_value result;
    napi_get_boolean(env, false, &result);
    napi_resolve_deferred(env, deferred, result);
    return promise;
  }

  const std::string queued_diagnostics =
      "openAsync(" + work->package_root + "): queued";
  g_open_jobs.fetch_add(1);
  napi_status queue_status = napi_queue_async_work(env, work->work);
  if (queue_status != napi_ok) {
    g_open_jobs.fetch_sub(1);
    napi_delete_async_work(env, work->work);
    delete work;
    SetDiagnostics("openAsync failed to queue async work");
    napi_value result;
    napi_get_boolean(env, false, &result);
    napi_resolve_deferred(env, deferred, result);
    return promise;
  }

  SetDiagnostics(queued_diagnostics);
  return promise;
}

napi_value NapiGetSuggestions(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

  SuggestResponse response;
  if (IsEngineOpening()) {
    response.status = Status::InvalidArgument("engine opening");
    response.diagnostics = "engine_opening";
  } else if (argc == 0) {
    response.status = Status::InvalidArgument("getSuggestions requires request object");
  } else {
    const SuggestRequest request = ReadSuggestRequest(env, args[0]);
    const std::string label = !request.token.empty() ? request.token : request.input;
    std::lock_guard<std::mutex> lock(g_mutex);
    response = g_engine.GetSuggestions(request);
    SetDiagnostics("getSuggestions(" + label + "): " + FormatStatus(response.status) + ", count=" +
                   std::to_string(response.suggestions.size()));
  }
  if (argc == 0) {
    SetDiagnostics(response.status.message);
  }

  return MakeSuggestResponse(env, response);
}

napi_value NapiCheckWord(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

  CheckWordResponse response;
  if (IsEngineOpening()) {
    response.status = Status::InvalidArgument("engine opening");
    response.diagnostics = "engine_opening";
  } else if (argc == 0) {
    response.status = Status::InvalidArgument("checkWord requires request object");
  } else {
    const CheckWordRequest request = ReadCheckWordRequest(env, args[0]);
    std::lock_guard<std::mutex> lock(g_mutex);
    response = g_engine.CheckWord(request);
    SetDiagnostics("checkWord(" + request.token + "): " + FormatStatus(response.status));
  }
  if (argc == 0) {
    SetDiagnostics(response.status.message);
  }

  return MakeCheckWordResponse(env, response);
}

napi_value NapiComparePredictorModes(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

  PredictorModeCompareResponse response;
  if (IsEngineOpening()) {
    response.status = Status::InvalidArgument("engine opening");
    response.diagnostics = "engine_opening";
  } else if (argc == 0) {
    response.status =
        Status::InvalidArgument("comparePredictorModes requires request object");
  } else {
    const SuggestRequest request = ReadSuggestRequest(env, args[0]);
    const std::string label = !request.token.empty() ? request.token : request.input;
    std::lock_guard<std::mutex> lock(g_mutex);
    response = g_engine.ComparePredictorModes(request);
    SetDiagnostics("comparePredictorModes(" + label + "): " +
                   FormatStatus(response.status));
  }
  if (argc == 0) {
    SetDiagnostics(response.status.message);
  }

  return MakePredictorModeCompareResponse(env, response);
}

napi_value NapiClose(napi_env env, napi_callback_info info) {
  (void)info;
  if (IsEngineOpening()) {
    SetDiagnostics("engine close skipped: openAsync still running");
    napi_value undefined_value;
    napi_get_undefined(env, &undefined_value);
    return undefined_value;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  g_engine.Close();
  SetDiagnostics("engine closed");
  napi_value undefined_value;
  napi_get_undefined(env, &undefined_value);
  return undefined_value;
}

napi_value NapiGetDecoderDiagnostics(napi_env env, napi_callback_info /*info*/) {
  if (IsEngineOpening()) {
    napi_value js_result;
    const std::string result = "Engine opening\n";
    napi_create_string_utf8(env, result.c_str(), result.size(), &js_result);
    return js_result;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  std::string result;

  if (g_engine.is_open()) {
    // TfmTrie diagnostics
    const auto& tfmtrie = g_engine.tfmtrie();
    if (tfmtrie.is_parsed()) {
      result += tfmtrie.DiagnosticDump();
    } else {
      result += "TfmTrie: not parsed\n";
    }

    // Lemmer diagnostics
    const auto& lemmer = g_engine.lemmer();
    if (lemmer.loaded()) {
      result += "Lemmer: hash=" + std::to_string(lemmer.hash_loaded())
              + " rules=" + std::to_string(lemmer.rules_loaded())
              + " keys=" + std::to_string(lemmer.num_keys())
              + " rules_count=" + std::to_string(lemmer.num_rules()) + "\n";
      // Test lemmatization
      std::string test = lemmer.Lemmatize("домов");
      result += "Lemmer test: домов -> " + test + "\n";
    } else {
      result += "Lemmer: not loaded\n";
    }

    // NormDict diagnostics
    const auto& normdict = g_engine.normdict();
    if (normdict.loaded()) {
      result += "NormDict: lang=" + normdict.language()
              + " order=" + std::to_string(normdict.max_order()) + "\n";
    } else {
      result += "NormDict: not loaded\n";
    }
  } else {
    result = "Engine not open\n";
  }

  napi_value js_result;
  napi_create_string_utf8(env, result.c_str(), result.size(), &js_result);
  return js_result;
}

napi_value NapiGetDiagnostics(napi_env env, napi_callback_info info) {
  (void)info;
  const std::string diagnostics = GetDiagnosticsSnapshot();
  napi_value result;
  napi_create_string_utf8(env, diagnostics.c_str(), diagnostics.size(), &result);
  return result;
}

napi_value NapiGetLoadedAssetKeys(napi_env env, napi_callback_info info) {
  (void)info;
  if (IsEngineOpening()) {
    return MakeStringArray(env, {});
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  const auto keys = g_engine.package().store().LoadedKeys();
  return MakeStringArray(env, keys);
}

napi_value NapiGetLoadReports(napi_env env, napi_callback_info info) {
  (void)info;
  if (IsEngineOpening()) {
    return MakeLoadReportArray(env, {});
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  return MakeLoadReportArray(env, g_engine.package().reports());
}

napi_value InitModule(napi_env env, napi_value exports) {
  napi_property_descriptor descriptors[] = {
      {"open", nullptr, NapiOpen, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"openAsync", nullptr, NapiOpenAsync, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"getSuggestions", nullptr, NapiGetSuggestions, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"comparePredictorModes", nullptr, NapiComparePredictorModes, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"checkWord", nullptr, NapiCheckWord, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"close", nullptr, NapiClose, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"getDiagnostics", nullptr, NapiGetDiagnostics, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"getLoadedAssetKeys", nullptr, NapiGetLoadedAssetKeys, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"getLoadReports", nullptr, NapiGetLoadReports, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"getDecoderDiagnostics", nullptr, NapiGetDecoderDiagnostics, nullptr, nullptr, nullptr, napi_default, nullptr},
  };
  napi_define_properties(env, exports, sizeof(descriptors) / sizeof(descriptors[0]), descriptors);
  return exports;
}

}  // namespace

EXTERN_C_START
static napi_module g_hoskey_module = {
    1,
    0,
    nullptr,
    InitModule,
    "hoskey_bridge",
    nullptr,
    {0},
};
EXTERN_C_END

extern "C" __attribute__((constructor)) void RegisterHoskeyModule(void) {
  napi_module_register(&g_hoskey_module);
}
