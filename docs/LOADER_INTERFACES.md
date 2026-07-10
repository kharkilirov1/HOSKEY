# Loader Interfaces

Status: loader-level contracts for family-based package loading

## Goal
Define the loader-facing interface for every currently important asset family, even where semantic decoding is still partial.

## Common contract

```cpp
struct LoadContext {
  std::filesystem::path package_root;
  bool strict_required = true;
};

struct LoadReport {
  std::string asset_key;
  std::string format;
  std::filesystem::path absolute_path;
  size_t byte_size = 0;
  std::vector<std::string> notes;
};

class IAssetLoader {
 public:
  virtual ~IAssetLoader() = default;
  virtual bool Supports(std::string_view format) const = 0;
  virtual Status Load(const AssetDescriptor& asset,
                      const LoadContext& context,
                      DictionaryStore& store,
                      LoadReport* report) = 0;
};
```

## Loader stages

| Stage | Meaning |
| --- | --- |
| `opaque` | file exists, bytes loaded, family id validated |
| `structural` | header / table / sentinel facts extracted |
| `semantic` | rule/trie entries materially decoded into engine-ready structures |

Smart-v1 only requires `opaque` or `structural` for most families.

## Family contracts

### `succinct_lexicon_v1`

| Item | Contract |
| --- | --- |
| Source asset | `trie` |
| Loader | `LexiconLoader` |
| Minimum work | validate file exists, load bytes, capture first header words |
| Current facts | magic-like `1nc7`, tiny header, then dense packed body |
| Engine use in v1 | opaque lexicon handle |
| Expected later | terminal lookup, prefix iteration, frequency extraction |

### `succinct_lexicon_k13421`

| Item | Contract |
| --- | --- |
| Source asset | `KEYBOARD-13421_trie` |
| Loader | `LexiconLoader` |
| Minimum work | load as separate family, do not reuse default parser blindly |
| Current facts | dense bit-packed body from byte zero, no obvious small header |
| Engine use in v1 | staged experimental family only |
| Expected later | alternate lexicon backend or converter path |

### `tfmtrie_v1`

| Item | Contract |
| --- | --- |
| Source asset | `rules.bin` |
| Loader | `TransfemeRuleLoader` |
| Minimum work | validate `TfmTrie` magic, retain header facts and payload bytes |
| Current facts | aligned header page, index regions near `0x1000`/`0x1F80`, packed Unicode symbol words, `|` separator candidate |
| Engine use in v1 | structural resource for rule engine hooks |
| Expected later | class-table parser, rule atom graph, normalized transform application |

### `tfmtrie_k13421`

| Item | Contract |
| --- | --- |
| Source asset | `KEYBOARD-13421_rules.bin` |
| Loader | `TransfemeRuleLoader` |
| Minimum work | load as distinct family and avoid default-header assumptions |
| Current facts | sparse header, packed Unicode payload, different byte layout than default `TfmTrie` |
| Engine use in v1 | optional staged experimental family |
| Expected later | dedicated experimental transfeme parser |

### `normdict_v2`

| Item | Contract |
| --- | --- |
| Source asset | `normdict` |
| Loader | `NormDictLoader` |
| Minimum work | load bytes and extract self-description markers |
| Current facts | contains `Meta.Type = NormDictionary`, `Language = ru`, capability flags, format version marker |
| Engine use in v1 | normalization metadata, future lowercase/canonicalization hook |
| Expected later | lookup API for canonical forms and normalization suggestions |

### `chd_hash`

| Item | Contract |
| --- | --- |
| Source asset | `lemmer_mhash` |
| Loader | `LemmerLoader` |
| Minimum work | validate presence of `ChdTbl` / `ChdHash` signatures and stage bytes |
| Current facts | looks like CHD/perfect-hash-like static lookup structure |
| Engine use in v1 | paired lemma resource |
| Expected later | lemma-rule index lookup |

### `lemma_rules_program`

| Item | Contract |
| --- | --- |
| Source asset | `lemmer_rules` |
| Loader | `LemmerLoader` |
| Minimum work | stage bytes as a separate morphology program, not as a `TfmTrie` sibling |
| Current facts | noisy symbolic program/table, distinct from transfeme layout |
| Engine use in v1 | opaque rule program resource |
| Expected later | lemma transforms and additional-form generation |

### `policy_blob`

| Item | Contract |
| --- | --- |
| Source assets | `blacklist`, `blacklist_suggest`, `abbr`, `autocorrect_blocker` |
| Loader | `PolicyLoader` |
| Minimum work | stage compact binary blob with semantic key |
| Current facts | compact compiled policy resources, not plain-text source lists |
| Engine use in v1 | gate checks and future policy-specific decoders |
| Expected later | per-policy query APIs |

### `feature_json`

| Item | Contract |
| --- | --- |
| Source assets | `features_*.json`, and optionally `config` |
| Loader | `FeatureConfigLoader` |
| Minimum work | read UTF-8 JSON text, extract section and feature-name hints |
| Current facts | `Base/Features`, `Combos`, `KeyboardSpecific/Features` patterns are already visible |
| Engine use in v1 | expose feature catalogue to scorer/autocorrect module |
| Expected later | real typed schema and feature dispatch tables |

## Store contract

`DictionaryStore` should provide two stable storage forms:

1. `LoadedBinaryAsset` for opaque or structural binary families.
2. `LoadedFeatureConfig` for JSON configs and future typed config documents.

It must not hardwire Yandex filenames. All lookups go through manifest keys such as:
- `lexicon.default`
- `rules.transfeme_default`
- `lemma.hash`
- `policy.autocorrect_blocker`
- `features.autocc`

## Failure policy

| Case | Behavior |
| --- | --- |
| Missing required asset | fail package open |
| Missing optional asset | record note, continue |
| Unsupported family id | fail if required, otherwise skip |
| Structural mismatch | fail required asset, keep report for diagnostics |
| Semantic decode not implemented | succeed with `opaque`/`structural` status if byte-level validation is enough for current stage |

## Suggested code split

- `LexiconLoader`
- `TransfemeRuleLoader`
- `NormDictLoader`
- `LemmerLoader`
- `PolicyLoader`
- `FeatureConfigLoader`

`HosdictPackage` owns the loader set and orchestrates them by manifest family id.
