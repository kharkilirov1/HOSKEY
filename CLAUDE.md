# CLAUDE.md - Project Context & Instructions

## Project Overview
**HOSKEY** - HarmonyOS Keyboard Application with OpenBoard suggest engine integration.

## Current Branch
`claude/review-project-audit-BS0BN` - Latest development branch

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

## Session Instructions

### Rules (STRICT)
1. **No guessing** - каждое изменение обосновано ссылкой на файл/строки
2. **Root cause first** - сначала причина, потом правка
3. **Verify changes** - шаги воспроизведения до/после, тест или лог
4. **API sync** - при изменении NAPI сигнатур синхронизировать ArkTS типы и d.ts
5. **Small commits** - 1 логическая правка = 1 коммит

### Goals
- A) Клавиатура в настройках IME и активируема
- B) NAPI без UB, корректная валидация аргументов
- C) ArkTS корректно обрабатывает ошибки, не ломает lifecycle IME

### Active Tasks
- [ ] Построить карту соответствий проекта
- [ ] Проверить module.json5 конфигурацию IME
- [ ] Проверить NAPI экспорты и ArkTS типы
- [ ] Исправить найденные проблемы

### Notes
<!-- Runtime notes -->

---
*Last updated: 2026-01-21*
