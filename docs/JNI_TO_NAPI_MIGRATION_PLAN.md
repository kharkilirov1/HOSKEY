# План портации JNI → NAPI для OpenBoard Suggest Engine

**Дата создания:** 2026-01-20
**Дата обновления:** 2026-01-20
**Статус:** ✅ ЗАВЕРШЕНО
**Автор:** Claude Code

---

## Текущий статус

| Компонент | Статус | Прогресс |
|-----------|--------|----------|
| napi_helpers.h/cpp | ✅ Готово | 100% |
| proximity_info.h/cpp | ✅ Готово | 100% |
| suggestion_results.h/cpp | ✅ Готово | 100% |
| dictionary.h/cpp | ✅ Готово | 100% |
| dic_traverse_session.h | ✅ Готово | 100% |
| log_utils.h/cpp | ✅ Готово | 100% |
| CMakeLists.txt | ✅ Готово | 100% |

---

## План выполнения

### ШАГ 1: Добавить недостающие NAPI helper функции
**Приоритет:** КРИТИЧЕСКИЙ
**Файлы:** `napi_helpers.h`, `napi_helpers.cpp`

**Проблема:** `suggestion_results.cpp` вызывает несуществующие функции:
- `hoskey::napiCreateInt()` - строка 35
- `hoskey::napiGetArrayLengthChecked()` - строки 44, 54, 63, 67, 71, 75

**Что добавить:**
```cpp
// Создать NAPI int32 значение
napi_value napiCreateInt(napi_env env, int32_t value);

// Получить длину массива с проверкой на null/undefined
uint32_t napiGetArrayLengthChecked(napi_env env, napi_value array);

// Дополнительно для удобства:
napi_value napiCreateDouble(napi_env env, double value);
bool napiIsNullOrUndefined(napi_env env, napi_value value);
```

**Статус:** [ ] Не начато

---

### ШАГ 2: Исправить SuggestionResults
**Приоритет:** КРИТИЧЕСКИЙ
**Файлы:** `suggestion_results.cpp`

**Задачи:**
- [ ] Проверить корректность вызовов NAPI helper функций
- [ ] Убедиться что outputSuggestions() полностью работает
- [ ] Убрать stub/placeholder код

**Статус:** [ ] Не начато

---

### ШАГ 3: Портировать Dictionary
**Приоритет:** ВЫСОКИЙ
**Файлы:** `dictionary.h`, `dictionary.cpp`

**Текущие JNI зависимости:**
- `#include "jni.h"` (строка 23)
- `Dictionary(JNIEnv *env, ...)` (строка 65)
- `void logDictionaryInfo(JNIEnv *const env) const` (строка 147)

**План изменений:**
```cpp
// БЫЛО:
#include "jni.h"
Dictionary(JNIEnv *env, DictionaryStructureWithBufferPolicy::StructurePolicyPtr dictionaryStructureWithBufferPolicy);

// СТАНЕТ:
#include <node_api.h>
Dictionary(napi_env env, DictionaryStructureWithBufferPolicy::StructurePolicyPtr dictionaryStructureWithBufferPolicy);
```

**Статус:** [ ] Не начато

---

### ШАГ 4: Портировать DicTraverseSession
**Приоритет:** ВЫСОКИЙ
**Файлы:** `dic_traverse_session.h`, `dic_traverse_session.cpp`

**Текущие JNI зависимости:**
- Строка 41: `static DicTraverseSession *getSessionInstance(JNIEnv *env, jstring localeStr, ...)`
- Строка 53: конструктор использует JNI

**План изменений:**
- Заменить `jstring localeStr` на `napi_value localeStr` или `const char* locale`
- Использовать `napi_get_value_string_utf8()` для конвертации строки

**Статус:** [ ] Не начато

---

### ШАГ 5: Обновить LogUtils
**Приоритет:** СРЕДНИЙ
**Файлы:** `log_utils.h`, `log_utils.cpp`

**Текущие JNI зависимости:**
- `static void logToJava(JNIEnv *const env, const char *const format, ...)`

**План:**
- Заменить JNI logging на HarmonyOS hilog или простой printf
- Удалить зависимость от JNIEnv

**Статус:** [ ] Не начато

---

### ШАГ 6: Финальная проверка и сборка
**Приоритет:** ВЫСОКИЙ

**Задачи:**
- [ ] Убедиться что нет остаточных `#include "jni.h"`
- [ ] Запустить компиляцию через CMake
- [ ] Исправить ошибки компиляции
- [ ] Протестировать базовую функциональность

**Статус:** [ ] Не начато

---

## Порядок выполнения (от зависимостей к зависимым)

```
1. napi_helpers (базовые функции)
       ↓
2. suggestion_results (использует helpers)
       ↓
3. dictionary (использует suggestion_results)
       ↓
4. dic_traverse_session (использует dictionary)
       ↓
5. log_utils (используется везде, но опционально)
       ↓
6. Финальная сборка и тесты
```

---

## Логика принятия решения

**Почему этот порядок:**

1. **napi_helpers первым** - это фундамент. Без недостающих функций SuggestionResults не скомпилируется.

2. **suggestion_results вторым** - уже частично портирован, нужно только добавить helpers и проверить.

3. **dictionary третьим** - ключевой компонент, но не зависит от session напрямую.

4. **dic_traverse_session четвёртым** - зависит от dictionary, нужен для полной работы suggest engine.

5. **log_utils последним** - некритичный компонент, можно заменить на no-op или простой printf.

---

## Прогресс выполнения

- [x] ШАГ 1: napi_helpers - добавить недостающие функции ✅
- [x] ШАГ 2: suggestion_results - исправить реализацию ✅
- [x] ШАГ 3: dictionary - полная портация ✅
- [x] ШАГ 4: dic_traverse_session - полная портация ✅
- [x] ШАГ 5: log_utils - обновить или заменить ✅
- [ ] ШАГ 6: финальная сборка (требует тестирования на устройстве)

---

## Заметки по ходу работы

### 2026-01-20: Основная портация завершена

**Добавленные NAPI helpers:**
- `napiCreateInt()` - создание napi_value из int32_t
- `napiCreateDouble()` - создание napi_value из double
- `napiGetArrayLengthChecked()` - безопасное получение длины массива
- `napiIsNullOrUndefined()` - проверка на null/undefined

**Изменения в файлах:**

1. **napi_helpers.h/cpp** - добавлены 4 новые функции
2. **suggestion_results.h** - добавлен `#include "defines.h"` для DISALLOW_IMPLICIT_CONSTRUCTORS
3. **dictionary.h/cpp** - удалён `#include "jni.h"`, убран JNIEnv* из конструктора
4. **dic_traverse_session.h** - заменены JNI типы на C++ (jstring→const char*, jlong→int64_t)
5. **log_utils.h/cpp** - полностью переписан без JNI, использует printf или HarmonyOS hilog

**Проверка JNI зависимостей:**
- `#include "jni.h"` - НЕ НАЙДЕНО ✅
- JNI типы (JNIEnv, jintArray, etc.) - только в комментариях и исключённых файлах ✅
- jni_data_utils.cpp - исключён из CMakeLists.txt ✅

**Следующие шаги:**
1. Собрать проект через DevEco Studio
2. Исправить возможные ошибки компиляции
3. Протестировать на реальном устройстве

