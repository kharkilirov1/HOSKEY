# Engine ABI Plan

Status: semantic ABI plan for the new engine

## Goal
Preserve the useful external contract of the recovered suggestion engine while simplifying the internal runtime substantially.

The new engine should keep compatibility at the semantic API level, not at the JNI binary ABI level.

## What stays compatible

### Core methods

| Method | Keep | Decision |
| --- | --- | --- |
| `open(containerPath)` | yes | Keep as the minimal external open call. Treat `containerPath` as package root or manifest path. |
| `getSuggestions(request)` | yes | Keep as primary suggestion API. |
| `checkWord(request)` | yes | Keep as primary validation API. |

### Result field semantics to preserve

From the recovered materialization contract, these names are worth keeping stable even if internal storage changes.

#### `Suggestion`
- `Text`
- `TypeFlags`
- `GroupId`
- `Tag`
- `AdditionalForms`

#### `GetSuggestionsResult`
- `Suggestions`
- `WillAutocorrect`
- `AutocorrectFeatures`
- `Emojis`
- `IsBlocked`

This gives us a clean target for typed C++ structs and future compatibility serializers.

## What does **not** stay compatible

| Old concern | Decision |
| --- | --- |
| JNI symbol names | do not preserve |
| Java ClassLoader/bootstrap assumptions | remove |
| old internal `0x68` candidate layout | replace with own structs |
| binary-to-binary container offsets | remove |
| original protobuf request bytes as primary API | replace with typed request/response |

## New external API

### C++ facade

```cpp
class KeyboardEngine {
 public:
  Status Open(const std::filesystem::path& package_root);
  SuggestResponse GetSuggestions(const SuggestRequest& request) const;
  CheckWordResponse CheckWord(const CheckWordRequest& request) const;
  void Close();
};
```

### Typed request/response shape

```cpp
struct SuggestRequest {
  std::string token;
  std::vector<std::string> previous_tokens;
  std::string locale;
  bool want_autocorrect = true;
  size_t max_suggestions = 6;
};

struct Suggestion {
  std::string text;
  uint32_t type_flags = 0;
  uint32_t group_id = 0;
  std::string tag;
  std::vector<std::string> additional_forms;
  float score = 0.0f;
};

struct SuggestResponse {
  Status status;
  std::vector<Suggestion> suggestions;
  bool will_autocorrect = false;
  std::vector<uint32_t> autocorrect_features;
  std::vector<std::string> emojis;
  bool is_blocked = false;
  std::string diagnostics;
};

struct CheckWordRequest {
  std::string token;
  std::string locale;
};

struct CheckWordResponse {
  Status status;
  bool is_known = false;
  bool is_blocked = false;
  std::string normalized_form;
  std::string diagnostics;
};
```

## ArkTS bridge ABI

The HarmonyOS-facing NAPI module should expose only a small surface:

```ts
open(packageRoot: string): boolean
getSuggestions(request: SuggestRequest): SuggestResponse
checkWord(request: CheckWordRequest): CheckWordResponse
close(): void
```

Optional development-only helpers:

```ts
getDiagnostics(): string
getLoadedAssetKeys(): string[]
```

## Compatibility strategy

### Tier 1 — typed native API
This is the default path for HOSKEY.

- ArkTS sends typed objects.
- C++ uses native structs.
- No protobuf serialization on the hot path.

### Tier 2 — optional compatibility serializer
If needed later, add an adapter that serializes `SuggestResponse` into the recovered protobuf-compatible field layout:
- suggestion field `1` -> `Text`
- suggestion field `3` -> `TypeFlags`
- suggestion field `4` -> `GroupId`
- suggestion field `5` -> `Tag`
- suggestion field `6` -> `AdditionalForms`
- result fields `1..5` -> `Suggestions`, `WillAutocorrect`, `AutocorrectFeatures`, `Emojis`, `IsBlocked`

This should be a compatibility layer, not the internal engine representation.

## Simplifications allowed internally

| Area | Simplification |
| --- | --- |
| Candidate graph | own `Suggestion` struct instead of reverse-matched binary layout |
| Rule pipeline | staged rule engine with opaque resources first |
| Scoring | heuristic scorer in v1, richer model-backed scorer in v2 |
| Personalization | start with stub/local store only |
| Bridge | Node-API only, no JNI env emulation |

## Open questions left intentionally open

1. Whether `checkWord()` should become lemma-aware in v1 or v1.5.
2. Whether `open()` should accept `packageRoot` only or also explicit `manifestPath`.
3. Whether compatibility protobuf serialization is needed at all outside debugging.

## Final ABI decision

Preserve the logical surface and useful field names.
Replace the rest with a clean native C++ API and a small HarmonyOS Node-API bridge.
