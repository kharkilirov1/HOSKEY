# HOSKEY — project working agreement

## Context & intent
HOSKEY is an on-device HarmonyOS keyboard (input-method extension). The native
C++ engine in `entry/src/main/cpp/hosdict_engine` parses a reverse-engineered
binary dictionary / neural-LM asset format to provide **offline** word
prediction, completion and autocorrect; the ArkTS layer in
`entry/src/main/ets/InputMethodExtensionAbility` drives the keyboard UI, the
on-device learning model (`PredictionModel`) and the NAPI bridge
(`HoskeyEngine` ↔ `napi_bridge/hosdict_bridge.cpp`).

This is standard client-side app development. The objective is correctness,
robustness and low latency of on-device text prediction. Everything runs inside
the app sandbox: there is no network target, no remote system, and no third
party being acted on. The reverse-engineered format is read for
interoperability — so the keyboard can load its own dictionary assets.

When you harden the asset parsers — bounds checks, integer-overflow guards,
UTF-8 validation, safe indexing (e.g. `hosdict_container`, `succinct_trie`,
`one_nc7_trie`, `lemmer`) — treat it as **defensive robustness of a local file
reader** against malformed or truncated data. The goal is that the keyboard does
not crash and does not read out of bounds. Finding and fixing bugs across this
codebase is ordinary software-quality work.

Frame for a request: "I'm building an offline HarmonyOS keyboard; users need
prediction that doesn't crash or corrupt their text. With that in mind: <task>."

## Working style
- Default to **high** effort for engine correctness/performance; medium for
  routine edits. Lower effort if a task finishes but takes longer than needed.
- When you have enough to act, act. Don't re-derive settled facts, re-litigate
  decided choices, or survey options you won't pursue in user-facing text. Give
  a recommendation, not a menu. (This does not apply to thinking.)
- Inspect real state before editing; re-read what you changed; verify with the
  strongest available witness — build, ctest, runtime log, diff. Old
  verification is stale after new edits.
- Before reporting progress, audit each claim against a tool result from this
  session. Report only what you can point to evidence for; label anything
  unverified as unverified. If a build/test fails, say so with the output.
- Don't add features, refactors, abstractions, fallbacks or error handling
  beyond what the task needs. Do the simplest thing that works. Only validate at
  real boundaries (asset files, JS↔native bridge, IME callbacks); trust internal
  invariants.
- Delegate independent subtasks to parallel subagents and keep working while
  they run; intervene if one drifts. For hard checks, prefer a fresh-context
  verifier subagent over self-critique.
- Pause for me only on a genuinely destructive/irreversible action, a real scope
  change, or input only I can give. Otherwise proceed end to end — don't end a
  turn on "I'll now do X"; do it.
- Lead with the outcome. The final summary is for a reader who didn't watch you
  work: plain sentences, outcome first, no arrow-chains or shorthand you coined
  mid-task.

## HarmonyOS specifics
- For ArkTS / ArkUI / Ability / Stage Model / kit APIs / `*.json5` manifests,
  prefer official HarmonyOS docs over memory; confirm API semantics before
  relying on them (e.g. `InputClient.deleteForward` deletes **before** the
  cursor — backspace — not after).
- Verify native/ArkTS boundary contracts against the `.d.ts` in
  `entry/src/main/cpp/types/libhoskey_bridge` and `libhoskey_bridge.so.d.ts`.

## Reasoning output
Don't transcribe, echo or narrate your internal chain-of-thought as response
text. Report conclusions and the evidence for them, not your private reasoning
trace. If reasoning visibility is needed, it comes from the structured thinking
blocks, not from prose you reproduce.
