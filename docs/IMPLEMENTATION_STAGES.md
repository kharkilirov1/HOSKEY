# Implementation Stages

Status: staged execution plan from design to real engine

## Stage boundaries

The project should be executed in three hard layers:

1. `Smart v1` — serious baseline smart keyboard.
2. `Smart v2` — higher-quality ranking and experiment support.
3. `Later` — neural, swipe, emoji, and voice parity work.

## Smart v1

### Mandatory assets

| Asset | Why it is mandatory |
| --- | --- |
| `trie` | default lexicon / LM backbone |
| `rules.bin` | default transfeme-rule family |
| `normdict` | normalization and canonicalization layer |
| `lemmer_mhash` | lemma lookup structure |
| `lemmer_rules` | lemma/morphology rule program |
| `blacklist` | hard block policy |
| `autocorrect_blocker` | autocorrect safety gate |
| `features_autocc.json` | autocorrect feature catalogue |
| `features_ranker.json` | ranking feature catalogue |
| `features_tap_model.json` | tap-model feature catalogue |

### Recommended low-cost add-ons inside v1

- `blacklist_suggest`
- `abbr`
- top-level `config`

### Required deliverables

1. `hosdict` manifest and normalized package layout.
2. Package opening and required-asset validation.
3. Stub/structural loaders for all mandatory families.
4. `KeyboardEngine.open()` / `getSuggestions()` / `checkWord()` typed API.
5. ArkTS bridge integration plan for `InputMethodExtensionAbility`.

### Exit criteria

- package opens successfully from HarmonyOS app storage,
- all mandatory assets are discoverable by manifest key,
- engine returns stable typed responses,
- no dependency on `libjni_ykeyboard3.so` for the new path.

## Smart v2

### Additional assets

| Asset group | Add in v2 |
| --- | --- |
| Experimental lexicon/rules | `KEYBOARD-13421_trie`, `KEYBOARD-13421_rules.bin` |
| Ranking models | `ranker`, `ranker_swipe`, `ranker_swipe_backoff` |
| Tap models | `tap_model_ranker`, `tap_model_classifier`, `KEYBOARD-13421_*` variants |
| Autocorrect models | `tree_autocorrect_model`, `KEYBOARD-13321_autocc`, `KEYBOARD-13421_autocc` |
| Swipe blockers/features | `swipe_blocker`, `features_ranker_swipe*.json`, `features_swipe_blocker.json` |

### v2 goals

1. Support manifest-driven profile selection between default and experimental families.
2. Upgrade `ContextScorer` and `AutocorrectEngine` to use feature configs and optional model hooks.
3. Add richer policy and personalization behavior.
4. Introduce async bridge execution if latency grows.

### Exit criteria

- engine can switch between default and experiment profiles without code changes,
- additional feature configs are parsed and exposed cleanly,
- ranking/autocorrect modules have real model hooks instead of pure stubs.

## Later

### Deferred asset groups

| Group | Assets |
| --- | --- |
| Neural | `neural_model`, `KEYBOARD-13321_nnlm_model`, `ms_models/*`, `gguf/*` |
| Voice | `rnnt_model` and other voice-related resources |
| Emoji | `emoji_model`, `search_emoji_model` |
| Full swipe parity | broader swipe model family and geometry-specific tuning |

### Later goals

- neural rescoring and/or NNLM integration,
- MindSpore/GGUF evaluation on HarmonyOS feasibility,
- swipe and emoji parity features,
- optional speech-related integrations.

## Concrete execution order

| Milestone | Output |
| --- | --- |
| M0 | architecture docs + skeleton code |
| M1 | manifest-driven package open |
| M2 | structural loaders for mandatory families |
| M3 | heuristic suggestion/checkWord engine |
| M4 | rule/lemma/normdict semantic decode passes |
| M5 | v2 ranking/autocorrect feature work |

## Things explicitly out of scope right now

- broad reverse of all `.so` modules,
- byte-perfect reimplementation of every original internal structure,
- immediate ASR/voice/neural parity,
- migrating the entire old runtime surface before the new core exists.
