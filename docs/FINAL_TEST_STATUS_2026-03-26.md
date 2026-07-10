# HOSKEY Final Test Status — 2026-03-26

## Current runtime

Active runtime is the new `hosdict_engine` path.

- legacy JNI/Yandex path has been removed from the active repo
- active NAPI bridge: `entry/src/main/cpp/napi_bridge/hosdict_bridge.cpp`
- active native runtime: `entry/src/main/cpp/hosdict_engine/*`
- active ArkTS wrapper: `entry/src/main/ets/InputMethodExtensionAbility/model/HoskeyEngine.ets`

## Current behavior

The app now builds and runs against the new engine path.

Implemented and testable now:

- `open(dictDir)`
- `getSuggestions(request)`
- `checkWord(request)`
- native diagnostics
- native loaded-asset inspection
- native autocorrect apply on `Space` / `Enter`

## Important implementation note

The repo is now moving to a **container-first baseline**:

1. canonical runtime source is `ru.smart.default.hosdict`
2. `HosdictPackage::OpenKnownRuLayout(...)` now expects a `.hosdict` container
3. `DictInstaller` installs the container, not loose raw lexicon fallback files
4. legacy text/binary payload fallback (`extracted_words.txt`, `dict_ru_full`) is no longer part of the active native path

This means the architecture is cleaner and closer to the final design, but successful `open()` no longer implies final suggestion quality: core decoders still need to be completed for `trie`, `tfmtrie`, `normdict`, and `lemmer`.

## Build environment fixed

Resolved local build blockers:

- `DEVECO_SDK_HOME` now points to local mirrored SDK root:
  - `C:\Users\Kharki\DevEcoSDK\default`
- `JAVA_HOME` should point to:
  - `C:\Program Files\Huawei\DevEco Studio\jbr`
- helper script added:
  - `tools/build_final.ps1`

## Verified build output

Successful build artifact paths:

- `build/outputs/default/HOSKEY-default-unsigned.app`
- `entry/build/default/outputs/default/app/entry-default.hap`
- `entry/build/default/outputs/default/entry-default-unsigned.hap`

Build verification:

- `hvigor assembleApp -p product=default -p buildMode=debug`
- result: `BUILD SUCCESSFUL`

## Known remaining warnings / open work

- ArkTS deprecation warning around `getContext(...)` in `SetupGuidePage.ets`
- signing is not configured in `build-profile.json5`, so generated package is unsigned
- `trie` decoder is still incomplete
- `tfmtrie` semantic decode is still incomplete
- `normdict` and `lemmer_*` are structurally staged, not semantically finished

## Recommended user test flow

1. Run `powershell -ExecutionPolicy Bypass -File .\\tools\\build_final.ps1`
2. Open:
   - `entry/build/default/outputs/default/app/entry-default.hap`
3. Install/sign as needed in DevEco
4. Open setup page and verify:
   - module loads
   - `ru.smart.default.hosdict` installs into app storage
   - engine attempts to open container-first package
