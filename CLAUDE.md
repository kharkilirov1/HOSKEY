# CLAUDE.md - Project Context & Instructions

## Project Overview
**HOSKEY** - HarmonyOS Keyboard Application with OpenBoard suggest engine integration.

## Current Branch
`claude/checkout-latest-branch-IcnQd`

## Tech Stack
- **Platform**: HarmonyOS (ArkTS/ArkUI)
- **Native**: C++ with NAPI (converted from JNI)
- **Suggest Engine**: OpenBoard C++ (~11,500 lines)

## Key Directories
```
/entry/src/main/ets/          - ArkTS source code
/entry/src/main/cpp/          - C++ native code (NAPI)
/openboard_src/               - OpenBoard suggest engine source
/AppScope/                    - App configuration
```

## Completed Tasks (PROMPTs 1-5)

| PROMPT | Task | Commit | Status |
|--------|------|--------|--------|
| 1 | IME registration fix | `b481bd2` | ✅ Done |
| 2 | ArkTS-NAPI contract unification | `2091785` | ✅ Done |
| 3 | NAPI memory safety | `077b0bd` | ✅ Done |
| 4 | Panel lifecycle stabilization | `683d202` | ✅ Done |
| 5 | Final audit & cleanup | (current) | ✅ Done |

## How to Verify IME on Device

### Prerequisites
- HarmonyOS device or emulator
- DevEco Studio with HAP signing configured
- USB debugging enabled

### Verification Steps

```bash
# 1. Build and install
hdc install entry-default-signed.hap

# 2. Enable IME in settings
# Settings → System → Keyboard → Input Methods → Enable "HOSKEY"

# 3. Select as default
# Settings → System → Keyboard → Default Keyboard → Select "HOSKEY"

# 4. Test in any text field
# Open Notes, Messages, or any app with text input
# Tap on input field → keyboard should appear

# 5. Monitor logs
hdc shell hilog | grep -E "HOSKEY|HOSKEY-NATIVE"
```

### Expected Log Output (Success)
```
[HOSKEY IME] KeyboardController.onCreate called
[HOSKEY IME] initWindow: panel created successfully
[HOSKEY IME] initWindow: panel initialization COMPLETE, panelReady=true
[HOSKEY IME] inputStart: received, setting up input session
[HOSKEY IME] showKeyboard: panel shown successfully
```

## Regression Checklist

| # | Test Case | Expected Result |
|---|-----------|-----------------|
| 1 | IME appears in Settings | HOSKEY visible in Input Methods list |
| 2 | IME can be enabled | Toggle switch works, no crash |
| 3 | IME can be selected as default | Selection persists after restart |
| 4 | Panel appears on input focus | Keyboard shows within 500ms |
| 5 | Panel hides on input blur | Keyboard hides cleanly |
| 6 | Character input works | Letters insert into text field |
| 7 | Backspace deletes text | Characters removed correctly |
| 8 | Predictions appear | Suggestion bar shows 3 words |
| 9 | Empty dictionary graceful | No crash, empty suggestions |
| 10 | loadDictionary returns true | Valid .dict file loads successfully |
| 11 | unload frees resources | No memory leak, reload works |
| 12 | Language switch works | Keyboard layout changes |

## Deferred Features

Features not yet fully integrated (code exists but not wired up):

| Feature | Location | Status |
|---------|----------|--------|
| Swipe typing via InputPipeline | `InputPipeline.ets:218-241` | DEFERRED - SwipeEngine ready, integration pending |
| PredictorFacade.decodeSwipe | `PredictorFacade.ets:114-133` | DEFERRED - Returns empty, direct SwipeEngine used |
| Sound feedback | `FeedbackController.ets:64` | DEFERRED - No sound resources |
| Text query from IME | `KeyboardController.ets:324` | DEFERRED - API not available |

## Log Prefixes

| Prefix | Source |
|--------|--------|
| `[HOSKEY]` | ArkTS code |
| `[HOSKEY IME]` | KeyboardController lifecycle |
| `[HOSKEY-NATIVE]` | C++ NAPI layer |

## Session Instructions (Reference)

### Rules (STRICT)
1. **No guessing** - каждое изменение обосновано ссылкой на файл/строки
2. **Root cause first** - сначала причина, потом правка
3. **Verify changes** - шаги воспроизведения до/после, тест или лог
4. **API sync** - при изменении NAPI сигнатур синхронизировать ArkTS типы и d.ts
5. **Small commits** - 1 логическая правка = 1 коммит

---
*Last updated: 2026-01-21*
