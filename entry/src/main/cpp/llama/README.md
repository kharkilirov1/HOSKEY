# HOSKEY LLaMA Module

Нейрокоррекция текста на базе llama.cpp.
Порт Yandex Keyboard LLaMA интеграции на HarmonyOS.

## Статус

🔶 **В разработке** — интерфейс готов, llama.cpp не подключен

## Архитектура

```
┌─────────────────────────────────────────┐
│        LlamaCorrector.ets               │  ← ArkTS API
│  - initialize(config)                   │
│  - correct(text) → CorrectionResult     │
│  - unload()                             │
└──────────────────┬──────────────────────┘
                   │ NAPI
┌──────────────────▼──────────────────────┐
│        llama_napi.cpp                   │  ← C++ Bridge
│  - LlamaBridge singleton                │
│  - Thread-safe                          │
└──────────────────┬──────────────────────┘
                   │
┌──────────────────▼──────────────────────┐
│        llama.cpp (TODO)                 │  ← ML Runtime
│  - GGUF model loading                   │
│  - Inference                            │
└─────────────────────────────────────────┘
```

## API

### ArkTS (LlamaCorrector)

```typescript
import { LlamaCorrector } from '../llama/LlamaCorrector';

// Инициализация
const corrector = LlamaCorrector.getInstance();
await corrector.initialize({
  modelPath: '/data/models/neurocorrect.gguf',
  contextSize: 256,
  useKvQuantization: false,
  probabilityThreshold: 0.387
});

// Коррекция
const result = await corrector.correct('привет как дила');
// result = {
//   text: 'привет как дела',
//   probability: 0.92,
//   success: true,
//   wasChanged: true
// }

// Выгрузка
await corrector.unload();
```

### NAPI (C++)

```cpp
#include "llama/llama_napi.h"

// В napi_init.cpp добавить:
hoskey::llama::LlamaModuleInit(env, exports);
```

## Интеграция с клавиатурой (TODO)

```typescript
// В SuggestionEngine.ets:
import { llamaCorrector } from '../llama/LlamaCorrector';

async getSuggestions(input: string): Promise<string[]> {
  const suggestions = await this.nativeSuggestions(input);
  
  // Добавить нейрокоррекцию как первый вариант
  if (llamaCorrector.isReady() && llamaCorrector.needsCorrection(input)) {
    const correction = await llamaCorrector.correct(input);
    if (correction.wasChanged && correction.probability > 0.5) {
      suggestions.unshift(correction.text);
    }
  }
  
  return suggestions;
}
```

## Файлы

```
entry/src/main/cpp/llama/
├── README.md           # Этот файл
├── llama_napi.h        # C++ заголовок
└── llama_napi.cpp      # C++ реализация

entry/src/main/ets/llama/
└── LlamaCorrector.ets  # ArkTS обёртка
```

## TODO

1. [ ] Скомпилировать llama.cpp под HarmonyOS (ARM64)
2. [ ] Подключить к CMakeLists.txt
3. [ ] Скопировать модель neurocorrect.gguf в assets
4. [ ] Раскомментировать NAPI вызовы
5. [ ] Интегрировать с SuggestionEngine
6. [ ] Тесты производительности

## Модель

Яндекс использует кастомную модель:
- Формат: GGUF
- Размер: ~94MB
- Квантизация: Q8_0
- Параметры: ~100M (6 layers, 768 embedding)
- Специализация: коррекция русского текста

Модель лежит в:
`/mnt/c/Users/Kharki/Desktop/yandex_extracted/models/`

## Параметры (из Yandex)

| Параметр | Значение | Описание |
|----------|----------|----------|
| contextSize | 256 | Макс. токенов в контексте |
| probabilityThreshold | 0.387 | Мин. уверенность |
| maxResponseLen | input × 2 | Защита от бесконечной генерации |
| useKvQuantization | false | INT8 KV cache (экономит RAM) |

## Ссылки

- [llama.cpp](https://github.com/ggerganov/llama.cpp)
- [GGUF формат](https://github.com/ggerganov/ggml/blob/master/docs/gguf.md)
- [HarmonyOS NAPI](https://developer.huawei.com/consumer/cn/doc/harmonyos-guides-V5/napi-guidelines-V5)
