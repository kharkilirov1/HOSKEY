# HOSKEY Keyboard Native Engine

Production-ready IME prediction engine for HarmonyOS with MindSpore Lite neural backend.

## Features

- **Neural Inference**: MindSpore Lite backend for neural scoring
- **Fallback Policy**: Automatic fallback to ngram/trie/rule when neural is slow/unavailable
- **Low Latency**: p50 ≤ 8ms, p95 ≤ 20ms, p99 ≤ 35ms
- **Stable ABI**: C API for binary compatibility
- **Thread-Safe**: All operations are thread-safe
- **LRU Cache**: Smart caching for frequent predictions

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                       NAPI Layer                            │
│  init() | predict() | learn() | resetSession() | close()   │
└─────────────────────────┬───────────────────────────────────┘
                          │
┌─────────────────────────▼───────────────────────────────────┐
│                      Core Engine                            │
│  ┌─────────────┐  ┌──────────────┐  ┌────────────────────┐  │
│  │   Session   │  │    Cache     │  │  Fallback Policy   │  │
│  └─────────────┘  └──────────────┘  └────────────────────┘  │
└─────────────────────────┬───────────────────────────────────┘
                          │
    ┌─────────────────────┼─────────────────────┐
    │                     │                     │
┌───▼───┐           ┌─────▼─────┐         ┌─────▼─────┐
│Neural │           │   Trie    │         │   Rule    │
│Engine │           │  Engine   │         │  Engine   │
│(MS)   │           │           │         │           │
└───────┘           └───────────┘         └───────────┘
```

## Build

### Desktop (Testing)

```bash
mkdir build && cd build
cmake -DPLATFORM=desktop -DUSE_MINDSPORE=OFF -DBUILD_TESTS=ON ..
make -j$(nproc)

# Run tests
./test_engine

# Run benchmark
./bench_predict
```

### HarmonyOS

```bash
mkdir build && cd build
cmake -DPLATFORM=ohos -DUSE_MINDSPORE=ON \
      -DCMAKE_TOOLCHAIN_FILE=$OHOS_SDK_HOME/native/build/cmake/ohos.toolchain.cmake \
      -DOHOS_ARCH=arm64-v8a ..
make -j$(nproc)
```

## API Reference

### NAPI API (ArkTS)

```typescript
// Initialize engine
const result = keyboardNative.init({
  dictPath: '/path/to/dict.bin',
  modelPath: '/path/to/model.ms',
  useNeural: true,
  maxInferMs: 20,
  cacheSize: 128
});
// result: { ok: boolean, version: string }

// Predict
const prediction = keyboardNative.predict({
  input: 'hel',
  prevWord: 'say',
  maxResults: 10,
  deadlineMs: 20
});
// prediction: {
//   candidates: [{ text: 'hello', score: 0.95, source: 'nn' }, ...],
//   latencyMs: 5.2,
//   sourceStats: { neural: 2, ngram: 1, trie: 5 }
// }

// Learn from user selection
keyboardNative.learn({
  type: 'word_selected',
  word: 'hello',
  prevWord: 'say'
});

// Reset session (new text field)
keyboardNative.resetSession();

// Get status
const status = keyboardNative.getStatus();

// Close engine
keyboardNative.close();
```

### C API

```c
#include <keyboard_native/engine_api.h>

// Create engine
KBEngineHandle* handle;
KeyboardErrorCode err = kb_create_engine(config_json, &handle);

// Predict
char* result = kb_predict_json(handle, context_json);
// ... use result ...
kb_free_string(result);

// Close
kb_close_engine(handle);
```

## Configuration

| Parameter | Default | Description |
|-----------|---------|-------------|
| `dictPath` | null | Path to dictionary file |
| `modelPath` | null | Path to neural model (.ms) |
| `useNeural` | true | Enable neural inference |
| `neuralThreads` | auto | Threads for inference |
| `maxInferMs` | 20 | Max neural inference time |
| `cacheSize` | 128 | LRU cache size |
| `enableCache` | true | Enable suggestion cache |
| `p50TargetUs` | 8000 | p50 latency target (µs) |
| `p95TargetUs` | 20000 | p95 latency target (µs) |

## Fallback Policy

```
Neural Ready && Time Budget > 5ms?
    │
    ├─ Yes ──> Run Neural Inference ──> Timeout?
    │                                      │
    │                    ┌─────────────────┴─────────────────┐
    │                    │                                   │
    │                    ▼                                   ▼
    │                 Success                            Timeout
    │                    │                                   │
    │                    ▼                                   │
    │              Return Results                            │
    │                                                        │
    └─ No ───────────────────────────────────────────────────┤
                                                             │
                                                             ▼
                                               Fallback: NGram + Trie + Rule
```

## Latency Budget

| Operation | Target (µs) | Notes |
|-----------|-------------|-------|
| Total p50 | ≤ 8,000 | 50th percentile |
| Total p95 | ≤ 20,000 | 95th percentile |
| Total p99 | ≤ 35,000 | 99th percentile |
| Neural | ≤ 15,000 | With timeout fallback |
| Trie lookup | ≤ 1,000 | Dictionary search |
| Ranking | ≤ 500 | Score fusion |

## File Structure

```
keyboard_native/
├── include/
│   ├── engine_api.h      # Public C API
│   ├── types.h           # Type definitions
│   └── error_codes.h     # Error codes
├── src/
│   ├── bridge_napi/      # NAPI bindings
│   ├── core/             # Core engine
│   ├── dict/             # Dictionary engine
│   ├── nn/               # Neural engine
│   └── platform/         # Platform abstraction
├── tests/                # Unit tests
├── benchmarks/           # Performance benchmarks
├── docs/                 # Documentation
├── CMakeLists.txt
└── README.md
```

## Error Codes

| Code | Name | Description |
|------|------|-------------|
| 0 | KB_OK | Success |
| -100 | KB_ERR_NOT_INITIALIZED | Engine not initialized |
| -200 | KB_ERR_MODEL_LOAD_FAILED | Failed to load neural model |
| -206 | KB_ERR_INFER_TIMEOUT | Neural inference timed out |
| -300 | KB_ERR_DICT_LOAD_FAILED | Failed to load dictionary |

See `error_codes.h` for full list.

## License

Apache License 2.0

## Version History

- **1.0.0** - Initial release with MindSpore Lite support
