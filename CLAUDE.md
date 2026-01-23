# OpenBoard - HarmonyOS Keyboard

## Project Overview
OpenBoard is an open-source keyboard application for **HarmonyOS 6.0** built with **ArkTS**.

## Tech Stack
- **Language**: ArkTS (TypeScript-like for HarmonyOS)
- **Platform**: HarmonyOS 6.0+
- **IDE**: DevEco Studio 6.0.1+
- **Build**: hvigor

## Project Structure
```
entry/src/main/ets/
├── InputMethodExtensionAbility/
│   ├── InputMethodService.ets      # Main IME service
│   ├── model/
│   │   ├── KeyboardController.ets  # Keyboard state management
│   │   ├── KeyboardKeyData.ets     # Key layouts (QWERTY, symbols)
│   │   ├── PredictionModel.ets     # Word prediction engine
│   │   └── Theme.ets               # Theme configuration
│   └── pages/
│       └── Index.ets               # Main keyboard UI
├── entryability/EntryAbility.ets   # App entry point
└── pages/SetupGuidePage.ets        # Setup wizard
```

## Supported Languages
- English (QWERTY)
- Polish (QWERTY)
- Italian (QWERTY)
- Persian (فارسی)

## Key Components
1. **KeyboardController** - manages keyboard state, text buffer, input handling
2. **KeyboardKeyData** - defines key layouts for all languages
3. **PredictionModel** - trigram-based word prediction with ranking
4. **InputMethodService** - HarmonyOS IME integration

## Build Commands
```bash
# Build project
hvigor assembleHap

# Run tests
hvigor test
```

## Code Style
- Use TypeScript/ArkTS conventions
- Component names in PascalCase
- Variables and functions in camelCase
- Files named after their main export

## Testing
- Unit tests: `entry/src/test/*.test.ets`
- Integration tests: `entry/src/ohosTest/ets/test/*.test.ets`
