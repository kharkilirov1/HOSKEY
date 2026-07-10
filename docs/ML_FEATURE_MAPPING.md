# ML Feature Mapping — Yandex Models -> HOSKEY Engine

Generated: 2026-03-31

---

## features_ranker.json (40 features total)

### Base Features (24)

| # | Feature Name | Operand | Category | HOSKEY Computation | Available? |
|---|---|---|---|---|---|
| 1 | length | original | base | `DecodeUtf8(original).size()` | YES |
| 2 | lm_ngr_main | original | neural | `neural_model` n-gram score on original | NEEDS_NEURAL |
| 3 | lm_1gr_static | original | trie | `log(predictor_.GetWeight(original))` unigram | NEEDS_TRIE_WEIGHTS |
| 4 | lm_2gr_static | original | trie | `log(predictor_.GetBigramWeight(prev, original))` | NEEDS_TRIE_WEIGHTS |
| 5 | lm_ngr_static | original | trie | `log(predictor_.GetNgramWeight(context, original))` | NEEDS_TRIE_WEIGHTS |
| 6 | letter_lm_main | original | neural | `char_model` score on original | NEEDS_NEURAL |
| 7 | ndict | original | base | `predictor_.Contains(original) ? 1.0 : 0.0` | YES |
| 8 | length | correction | base | `DecodeUtf8(correction).size()` | YES |
| 9 | lm_ngr_main | correction | neural | `neural_model` n-gram score on correction | NEEDS_NEURAL |
| 10 | lm_1gr_static | correction | trie | `log(predictor_.GetWeight(correction))` unigram | NEEDS_TRIE_WEIGHTS |
| 11 | lm_2gr_static | correction | trie | `log(predictor_.GetBigramWeight(prev, correction))` | NEEDS_TRIE_WEIGHTS |
| 12 | lm_ngr_static | correction | trie | `log(predictor_.GetNgramWeight(context, correction))` | NEEDS_TRIE_WEIGHTS |
| 13 | letter_lm_main | correction | neural | `char_model` score on correction | NEEDS_NEURAL |
| 14 | ndict | correction | base | `predictor_.Contains(correction) ? 1.0 : 0.0` | YES |
| 15 | no_change | [original, correction] | base | `(original == correction) ? 1.0 : 0.0` | YES |
| 16 | diacr_only | [original, correction] | base | Check if only diacritics differ | YES (implement) |
| 17 | distance_transfeme | [original, correction] | base | `TfmTrieDecoder` proximity score | YES |
| 18 | distance_per_letter_transfeme | [original, correction] | base | `distance_transfeme / max(length, 1)` | YES |
| 19 | levenshtein | [original, correction] | base | `LevenshteinDistanceUtf8(original, correction)` | YES |
| 20 | levenshtein_per_letter | [original, correction] | base | `levenshtein / max(length, 1)` | YES |
| 21 | bleu_2 | [original, correction] | base | BLEU-2 score computation | YES (implement) |
| 22 | bleu_3 | [original, correction] | base | BLEU-3 score computation | YES (implement) |
| 23 | bleu_4 | [original, correction] | base | BLEU-4 score computation | YES (implement) |
| 24 | letter_precision_1 | [original, correction] | base | 1-gram letter overlap precision | YES (implement) |
| 25 | letter_precision_2 | [original, correction] | base | 2-gram letter overlap precision | YES (implement) |
| 26 | letter_precision_3 | [original, correction] | base | 3-gram letter overlap precision | YES (implement) |
| 27 | is_permutation | [original, correction] | base | Check if one is permutation of other | YES (implement) |
| 28 | kendall_tau_distance | [original, correction] | base | Rank correlation distance | YES (implement) |

### Combos (5)

| # | Feature Name | Formula | HOSKEY Computation | Available? |
|---|---|---|---|---|
| 29 | sum(distance_transfeme, lm_ngr_main.correction) | `tfm(o,c) + ngr_main(c)` | Partial: tfm YES, ngr_main NEEDS_NEURAL | PARTIAL |
| 30 | sum(distance_transfeme, lm_ngr_static.correction) | `tfm(o,c) + ngr_static(c)` | Partial: tfm YES, ngr_static NEEDS_TRIE_WEIGHTS | PARTIAL |
| 31 | sub(lm_ngr_main.original, lm_ngr_main.correction) | `ngr_main(o) - ngr_main(c)` | Both operands need neural_model | NEEDS_NEURAL |
| 32 | sub(lm_1gr_static.original, lm_1gr_static.correction) | `1gr(o) - 1gr(c)` | Both operands need trie weights | NEEDS_TRIE_WEIGHTS |
| 33 | sub(length.original, length.correction) | `len(o) - len(c)` | Simple subtraction | YES |

### KeyboardSpecific (11)

| # | Feature Name | Category | HOSKEY Computation | Available? |
|---|---|---|---|---|
| 34 | geometry_distance_per_letter | touch | Key coordinate distance / letter count | NEEDS_TOUCH_COORDS |
| 35 | geometry_distance_original | touch | Geometry distance for original word | NEEDS_TOUCH_COORDS |
| 36 | geometry_distance_per_letter_original | touch | Geometry distance original / letter count | NEEDS_TOUCH_COORDS |
| 37 | geometry_distance_rel_gap | touch | Relative gap between correction and original geometry | NEEDS_TOUCH_COORDS |
| 38 | geometry_distance_rel_gap_and_char_lm_per_letter | touch+neural | Geometry gap combined with char_model | NEEDS_TOUCH_COORDS + NEEDS_NEURAL |
| 39 | geometry_distance_ngram_per_letter | touch | N-gram weighted geometry distance / letter | NEEDS_TOUCH_COORDS |
| 40 | geometry_distance_ngram_and_char_lm | touch+neural | N-gram geometry combined with char_model | NEEDS_TOUCH_COORDS + NEEDS_NEURAL |
| 41 | geometry_distance_ngram_original | touch | N-gram geometry for original | NEEDS_TOUCH_COORDS |
| 42 | geometry_distance_ngram_per_letter_original | touch | N-gram geometry original / letter | NEEDS_TOUCH_COORDS |
| 43 | geometry_distance_ngram_rel_gap | touch | N-gram geometry relative gap | NEEDS_TOUCH_COORDS |
| 44 | neural_oov | neural | Neural out-of-vocabulary flag | NEEDS_NEURAL |

---

## features_tap_model.json (37 features total)

### Base Features (32)

| # | Feature Name | Category | HOSKEY Computation | Available? |
|---|---|---|---|---|
| 1 | tap_log_likelihood_unigram | trie | `log(predictor_.GetWeight(word))` tap-weighted | NEEDS_TRIE_WEIGHTS |
| 2 | tap_log_likelihood_ngram | trie+neural | N-gram log likelihood with tap model | NEEDS_TRIE_WEIGHTS |
| 3 | char_lm_mod | neural | Modified char_model score | NEEDS_NEURAL |
| 4 | word_lm_1 | trie | Unigram word LM score | NEEDS_TRIE_WEIGHTS |
| 5 | word_lm_cond_1 | trie+neural | Conditional word LM (context-dependent) | NEEDS_TRIE_WEIGHTS |
| 6 | neural_char_lm_n | neural | `char_model` inference per position | NEEDS_NEURAL |
| 7 | neural_char_lm_cond_n | neural | `char_model` conditional per position | NEEDS_NEURAL |
| 8 | neural_char_lm_cond_log_n | neural | `log(char_model conditional)` per position | NEEDS_NEURAL |
| 9 | neural_char_lm_per_letter_n | neural | `char_model` per letter normalized | NEEDS_NEURAL |
| 10 | context_prefix_len | base | Length of typed prefix | YES |
| 11 | context_last_word_len | base | Length of last word in context | YES |
| 12 | context_shift_x | touch | Horizontal shift of context | NEEDS_TOUCH_COORDS |
| 13 | context_shift_y | touch | Vertical shift of context | NEEDS_TOUCH_COORDS |
| 14 | has_continuation | base | Whether word continues beyond current input | YES |
| 15 | prior_probability_static | trie | Static prior from trie frequency | NEEDS_TRIE_WEIGHTS |
| 16 | prior_probability | trie+neural | Neural-adjusted prior probability | NEEDS_NEURAL |
| 17 | distance_transfeme_original_correction | base | `TfmTrieDecoder` proximity (original->correction) | YES |
| 18 | distance_transfeme_correction_original | base | `TfmTrieDecoder` proximity (correction->original) | YES |
| 19 | tap_x | touch | X coordinate of tap event | NEEDS_TOUCH_COORDS |
| 20 | tap_y_m | touch | Y coordinate of tap event (modified) | NEEDS_TOUCH_COORDS |
| 21 | tap_key_x | touch | X coordinate of key center for tap | NEEDS_TOUCH_COORDS |
| 22 | letter_x | touch | X coordinate of target letter key | NEEDS_TOUCH_COORDS |
| 23 | letter_y_m | touch | Y coordinate of target letter key (modified) | NEEDS_TOUCH_COORDS |
| 24 | letter_key_x | touch | X coordinate of letter key center | NEEDS_TOUCH_COORDS |
| 25 | tap_distance_key_x | touch | `abs(tap_x - tap_key_x)` | NEEDS_TOUCH_COORDS |
| 26 | tap_distance_x | touch | `abs(tap_x - letter_x)` | NEEDS_TOUCH_COORDS |
| 27 | tap_distance_key_y | touch | `abs(tap_y - key_y)` | NEEDS_TOUCH_COORDS |
| 28 | tap_distance_y_m | touch | `abs(tap_y_m - letter_y_m)` | NEEDS_TOUCH_COORDS |
| 29 | prev_tap_distance_letter_key_x | touch | Previous tap distance to letter key X | NEEDS_TOUCH_COORDS |
| 30 | prev_tap_distance_letter_key_y | touch | Previous tap distance to letter key Y | NEEDS_TOUCH_COORDS |
| 31 | is_same | base | `(original == correction) ? 1.0 : 0.0` | YES |
| 32 | is_last_space | base | Whether last input char was space | YES |

### Stats (aggregate over per-letter features) (5)

| # | Feature Name | Aggregation | HOSKEY Computation | Available? |
|---|---|---|---|---|
| 33 | max(tap_log_likelihood_ngram) | max | Max of per-letter tap ngram likelihood | NEEDS_TRIE_WEIGHTS |
| 34 | mean(neural_char_lm_cond_n) | mean | Mean of per-letter neural char LM | NEEDS_NEURAL |
| 35 | max(neural_char_lm_per_letter_n) | max | Max of per-letter neural char LM | NEEDS_NEURAL |
| 36 | min(prev_tap_distance_letter_key_x) | min | Min previous tap distance X | NEEDS_TOUCH_COORDS |
| 37 | max(prev_tap_distance_letter_key_x) | max | Max previous tap distance X | NEEDS_TOUCH_COORDS |

---

## features_autocc.json (74 features total)

### Base Features (43)

| # | Feature Name | Operand | Category | HOSKEY Computation | Available? |
|---|---|---|---|---|---|
| 1 | oov | original | base | `!predictor_.Contains(original) ? 1.0 : 0.0` | YES |
| 2 | length | original | base | `DecodeUtf8(original).size()` | YES |
| 3 | has_apostrophe | original | base | `original.find('\'') != npos ? 1.0 : 0.0` | YES |
| 4 | has_hyphen | original | base | `original.find('-') != npos ? 1.0 : 0.0` | YES |
| 5 | lm_ngr_main | original | neural | `neural_model` n-gram score on original | NEEDS_NEURAL |
| 6 | lm_1gr_static | original | trie | `log(predictor_.GetWeight(original))` unigram | NEEDS_TRIE_WEIGHTS |
| 7 | lm_2gr_static | original | trie | `log(predictor_.GetBigramWeight(prev, original))` | NEEDS_TRIE_WEIGHTS |
| 8 | lm_ngr_static | original | trie | `log(predictor_.GetNgramWeight(context, original))` | NEEDS_TRIE_WEIGHTS |
| 9 | letter_lm_main | original | neural | `char_model` score on original | NEEDS_NEURAL |
| 10 | ndict | original | base | `predictor_.Contains(original) ? 1.0 : 0.0` | YES |
| 11 | length | correction | base | `DecodeUtf8(correction).size()` | YES |
| 12 | has_apostrophe | correction | base | `correction.find('\'') != npos ? 1.0 : 0.0` | YES |
| 13 | has_hyphen | correction | base | `correction.find('-') != npos ? 1.0 : 0.0` | YES |
| 14 | lm_ngr_main | correction | neural | `neural_model` n-gram score on correction | NEEDS_NEURAL |
| 15 | lm_1gr_static | correction | trie | `log(predictor_.GetWeight(correction))` unigram | NEEDS_TRIE_WEIGHTS |
| 16 | lm_2gr_static | correction | trie | `log(predictor_.GetBigramWeight(prev, correction))` | NEEDS_TRIE_WEIGHTS |
| 17 | lm_ngr_static | correction | trie | `log(predictor_.GetNgramWeight(context, correction))` | NEEDS_TRIE_WEIGHTS |
| 18 | letter_lm_main | correction | neural | `char_model` score on correction | NEEDS_NEURAL |
| 19 | ndict | correction | base | `predictor_.Contains(correction) ? 1.0 : 0.0` | YES |
| 20 | no_change | [original, correction] | base | `(original == correction) ? 1.0 : 0.0` | YES |
| 21 | diacr_only | [original, correction] | base | Check if only diacritics differ | YES (implement) |
| 22 | norm_only | [original, correction] | base | Check if only normalization differs (case, etc.) | YES (implement) |
| 23 | distance_transfeme | [original, correction] | base | `TfmTrieDecoder` proximity (o->c) | YES |
| 24 | distance1_transfeme | [original, correction] | base | `TfmTrieDecoder` distance variant 1 (o->c) | YES |
| 25 | distance2_transfeme | [original, correction] | base | `TfmTrieDecoder` distance variant 2 (o->c) | YES |
| 26 | distance3_transfeme | [original, correction] | base | `TfmTrieDecoder` distance variant 3 (o->c) | YES |
| 27 | distance4_transfeme | [original, correction] | base | `TfmTrieDecoder` distance variant 4 (o->c) | YES |
| 28 | distance5_transfeme | [original, correction] | base | `TfmTrieDecoder` distance variant 5 (o->c) | YES |
| 29 | distance_per_letter_transfeme | [original, correction] | base | `distance_transfeme / max(length, 1)` (o->c) | YES |
| 30 | distance_transfeme | [correction, original] | base | `TfmTrieDecoder` proximity (c->o) | YES |
| 31 | distance1_transfeme | [correction, original] | base | `TfmTrieDecoder` distance variant 1 (c->o) | YES |
| 32 | distance2_transfeme | [correction, original] | base | `TfmTrieDecoder` distance variant 2 (c->o) | YES |
| 33 | distance3_transfeme | [correction, original] | base | `TfmTrieDecoder` distance variant 3 (c->o) | YES |
| 34 | distance4_transfeme | [correction, original] | base | `TfmTrieDecoder` distance variant 4 (c->o) | YES |
| 35 | distance5_transfeme | [correction, original] | base | `TfmTrieDecoder` distance variant 5 (c->o) | YES |
| 36 | distance_per_letter_transfeme | [correction, original] | base | `distance_transfeme / max(length, 1)` (c->o) | YES |
| 37 | levenshtein | [original, correction] | base | `LevenshteinDistanceUtf8(original, correction)` | YES |
| 38 | levenshtein_per_letter | [original, correction] | base | `levenshtein / max(length, 1)` | YES |
| 39 | bleu_1 | [original, correction] | base | BLEU-1 score computation | YES (implement) |
| 40 | bleu_2 | [original, correction] | base | BLEU-2 score computation | YES (implement) |
| 41 | bleu_3 | [original, correction] | base | BLEU-3 score computation | YES (implement) |
| 42 | bleu_4 | [original, correction] | base | BLEU-4 score computation | YES (implement) |
| 43 | letter_precision_1 | [original, correction] | base | 1-gram letter overlap precision | YES (implement) |
| 44 | letter_precision_2 | [original, correction] | base | 2-gram letter overlap precision | YES (implement) |
| 45 | letter_precision_3 | [original, correction] | base | 3-gram letter overlap precision | YES (implement) |
| 46 | letter_precision_4 | [original, correction] | base | 4-gram letter overlap precision | YES (implement) |
| 47 | is_permutation | [original, correction] | base | Check if one is permutation of other | YES (implement) |
| 48 | kendall_tau_distance | [original, correction] | base | Rank correlation distance | YES (implement) |
| 49 | has_same_stemm | [original, correction] | base | Morphological stemming comparison | CANNOT_COMPUTE (needs stemmer) |

### Combos (5)

| # | Feature Name | Formula | HOSKEY Computation | Available? |
|---|---|---|---|---|
| 50 | sum(distance_transfeme, lm_ngr_main.correction) | `tfm(o,c) + ngr_main(c)` | Partial: tfm YES, ngr_main NEEDS_NEURAL | PARTIAL |
| 51 | sum(distance_transfeme, lm_ngr_static.correction) | `tfm(o,c) + ngr_static(c)` | Partial: tfm YES, ngr_static NEEDS_TRIE_WEIGHTS | PARTIAL |
| 52 | sub(lm_ngr_main.original, lm_ngr_main.correction) | `ngr_main(o) - ngr_main(c)` | Both need neural_model | NEEDS_NEURAL |
| 53 | sub(lm_ngr_static.original, lm_ngr_static.correction) | `ngr_static(o) - ngr_static(c)` | Both need trie weights | NEEDS_TRIE_WEIGHTS |
| 54 | sub(length.original, length.correction) | `len(o) - len(c)` | Simple subtraction | YES |

### KeyboardSpecific (26)

| # | Feature Name | Category | HOSKEY Computation | Available? |
|---|---|---|---|---|
| 55 | geometry_distance | touch | Key coordinate distance (correction) | NEEDS_TOUCH_COORDS |
| 56 | geometry_distance_per_letter | touch | Geometry distance / letter count | NEEDS_TOUCH_COORDS |
| 57 | geometry_distance_and_char_lm | touch+neural | Geometry + char_model combined | NEEDS_TOUCH_COORDS + NEEDS_NEURAL |
| 58 | geometry_distance_and_char_lm_per_letter | touch+neural | (Geometry + char_model) / letter | NEEDS_TOUCH_COORDS + NEEDS_NEURAL |
| 59 | geometry_distance_original | touch | Geometry distance for original | NEEDS_TOUCH_COORDS |
| 60 | geometry_distance_per_letter_original | touch | Geometry distance original / letter | NEEDS_TOUCH_COORDS |
| 61 | geometry_distance_rel_gap | touch | Relative gap correction vs original | NEEDS_TOUCH_COORDS |
| 62 | geometry_distance_rel_gap_per_letter | touch | Relative gap / letter | NEEDS_TOUCH_COORDS |
| 63 | geometry_distance_rel_gap_and_char_lm_per_letter | touch+neural | Gap + char_model / letter | NEEDS_TOUCH_COORDS + NEEDS_NEURAL |
| 64 | geometry_distance_ngram | touch | N-gram weighted geometry | NEEDS_TOUCH_COORDS |
| 65 | geometry_distance_ngram_per_letter | touch | N-gram geometry / letter | NEEDS_TOUCH_COORDS |
| 66 | geometry_distance_ngram_and_char_lm | touch+neural | N-gram geometry + char_model | NEEDS_TOUCH_COORDS + NEEDS_NEURAL |
| 67 | geometry_distance_ngram_and_char_lm_per_letter | touch+neural | N-gram geometry + char_model / letter | NEEDS_TOUCH_COORDS + NEEDS_NEURAL |
| 68 | geometry_distance_ngram_original | touch | N-gram geometry for original | NEEDS_TOUCH_COORDS |
| 69 | geometry_distance_ngram_per_letter_original | touch | N-gram geometry original / letter | NEEDS_TOUCH_COORDS |
| 70 | geometry_distance_ngram_rel_gap | touch | N-gram geometry relative gap | NEEDS_TOUCH_COORDS |
| 71 | geometry_distance_ngram_rel_gap_per_letter | touch | N-gram geometry gap / letter | NEEDS_TOUCH_COORDS |
| 72 | geometry_distance_ngram_rel_gap_and_char_lm_per_letter | touch+neural | N-gram gap + char_model / letter | NEEDS_TOUCH_COORDS + NEEDS_NEURAL |
| 73 | neural_oov | neural | Neural out-of-vocabulary flag | NEEDS_NEURAL |
| 74 | neural_char_lm_original | neural | `char_model` score on original | NEEDS_NEURAL |
| 75 | neural_char_lm_original_per_letter | neural | `char_model` per letter on original | NEEDS_NEURAL |
| 76 | neural_char_lm_cond_original | neural | `char_model` conditional on original | NEEDS_NEURAL |
| 77 | neural_char_lm_correction | neural | `char_model` score on correction | NEEDS_NEURAL |
| 78 | neural_char_lm_correction_per_letter | neural | `char_model` per letter on correction | NEEDS_NEURAL |
| 79 | neural_char_lm_cond_correction | neural | `char_model` conditional on correction | NEEDS_NEURAL |

---

## Summary — All Three Models Combined

### Grand Total: 151 features (40 + 37 + 74)

### Unique feature slots by availability:

| Status | Ranker | Tap Model | Autocc | Total |
|---|---|---|---|---|
| **YES** (available now) | 16 | 6 | 28 | **50** |
| **YES (implement)** | 8 | 0 | 10 | **18** |
| **NEEDS_TRIE_WEIGHTS** | 5 | 4 | 5 | **14** |
| **NEEDS_NEURAL** | 5 | 8 | 6 | **19** |
| **NEEDS_TOUCH_COORDS** | 7 | 14 | 12 | **33** |
| **NEEDS_TOUCH + NEURAL** | 2 | 0 | 6 | **8** |
| **PARTIAL** (mixed deps) | 2 | 0 | 2 | **4** |
| **CANNOT_COMPUTE** | 0 | 0 | 1 | **1** |
| **NEEDS_NEURAL (stats)** | 0 | 2 | 0 | **2** |
| **NEEDS_TOUCH (stats)** | 0 | 2 | 0 | **2** |
| **NEEDS_TRIE (stats)** | 0 | 1 | 0 | **1** |

### Simplified breakdown (151 total):

- **Available now (YES + implement)**: 68/151 (45%)
- **Available with trie weights**: 15/151 (10%)
- **Needs neural model**: 21/151 (14%)
- **Needs touch coordinates**: 35/151 (23%)
- **Needs both touch + neural**: 8/151 (5%)
- **Partial (mixed deps)**: 4/151 (3%)
- **Cannot compute**: 1/151 (<1%)

### Implementation Priority

**Phase 1 — No dependencies (68 features, 45%)**:
- String metrics: length, levenshtein, levenshtein_per_letter
- Dictionary: ndict, oov
- Comparison: no_change, diacr_only, norm_only, is_permutation
- Text analysis: has_apostrophe, has_hyphen, has_continuation, is_same, is_last_space
- TfmTrie distances: distance_transfeme (all variants), distance_per_letter_transfeme
- N-gram overlap: bleu_1..4, letter_precision_1..4
- Rank metrics: kendall_tau_distance
- Context: context_prefix_len, context_last_word_len
- Combo: sub(length.original, length.correction)

**Phase 2 — Trie weights (15 features, 10%)**:
- Requires frequency data in CompTrie: lm_1gr_static, lm_2gr_static, lm_ngr_static
- tap_log_likelihood_unigram, word_lm_1, prior_probability_static
- Combos using lm_ngr_static

**Phase 3 — Neural models (21 features, 14%)**:
- Requires MindSpore inference: neural_model (lm_ngr_main), char_model (letter_lm_main, neural_char_lm_*)
- neural_oov, char_lm_mod, prior_probability

**Phase 4 — Touch coordinates (35 features, 23%)**:
- Requires ArkTS keyboard bridge: tap_x/y, letter_x/y, geometry_distance_*
- All keyboard-specific features

**Phase 5 — Combined touch + neural (8 features, 5%)**:
- geometry_distance_and_char_lm_*, geometry_distance_rel_gap_and_char_lm_*

**Blocked**:
- `has_same_stemm` — needs a morphological stemmer (Yandex uses lemmer_mhash.ms model)

---

## Mapping Reference — Core Computations

| Yandex Feature | HOSKEY Equivalent |
|---|---|
| `length` | `DecodeUtf8(word).size()` |
| `ndict` | `predictor_.Contains(word) ? 1.0 : 0.0` |
| `oov` | `!predictor_.Contains(word) ? 1.0 : 0.0` |
| `levenshtein` | `LevenshteinDistanceUtf8(a, b)` |
| `levenshtein_per_letter` | `levenshtein / max(length, 1)` |
| `distance_transfeme` | `TfmTrieDecoder::Distance(a, b)` |
| `distance_per_letter_transfeme` | `distance_transfeme / max(length, 1)` |
| `distanceN_transfeme` | `TfmTrieDecoder::DistanceN(a, b, N)` — variant N of proximity |
| `lm_1gr_static` | `log(predictor_.GetWeight(word))` — unigram frequency from trie |
| `lm_2gr_static` | `log(predictor_.GetBigramWeight(prev, word))` — bigram from trie |
| `lm_ngr_static` | `log(predictor_.GetNgramWeight(ctx, word))` — n-gram from trie |
| `lm_ngr_main` | `NeuralModel::Score(ctx, word)` — neural_model.ms inference |
| `letter_lm_main` | `CharModel::Score(word)` — char_model.ms inference |
| `neural_char_lm_*` | `CharModel::ScorePerLetter(word)` — char_model.ms per-position |
| `geometry_distance*` | Keyboard key coordinate euclidean distance — needs ArkTS touch data |
| `tap_x/tap_y` | Touch event coordinates from ArkTS `onTouch()` |
| `letter_x/letter_y` | Key center coordinates from keyboard layout |
| `no_change` | `(original == correction) ? 1.0 : 0.0` |
| `diacr_only` | Strip diacritics, compare: `(stripped_a == stripped_b) ? 1.0 : 0.0` |
| `norm_only` | Normalize case, compare: `(norm_a == norm_b) ? 1.0 : 0.0` |
| `bleu_N` | Standard BLEU-N on character n-grams |
| `letter_precision_N` | `count(shared N-grams) / count(total N-grams in correction)` |
| `is_permutation` | Sort chars, compare: `(sorted_a == sorted_b) ? 1.0 : 0.0` |
| `kendall_tau_distance` | Count inversions in character mapping |
| `has_same_stemm` | Needs `lemmer_mhash.ms` model — NOT available without stemmer |
| `has_apostrophe` | `word.find('\'') != npos ? 1.0 : 0.0` |
| `has_hyphen` | `word.find('-') != npos ? 1.0 : 0.0` |
| `is_same` | `(original == correction) ? 1.0 : 0.0` |
| `is_last_space` | `(lastChar == ' ') ? 1.0 : 0.0` |
| `has_continuation` | `predictor_.HasPrefix(word) ? 1.0 : 0.0` |
| `context_prefix_len` | Length of typed prefix before current position |
| `context_last_word_len` | Length of previous word in context |
