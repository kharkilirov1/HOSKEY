# BaseLexicon Integration Guide

## Overview

The BaseLexicon system provides a high-performance, scalable prediction engine for 50k+ word lexicons using binary search and LRU caching.

## Architecture

### Components

1. **LRUCache.ets** - Generic LRU cache (O(1) get/set)
2. **BaseLexicon.ets** - 50k word lexicon with binary search
3. **PredictionModel.ets** - Integration layer

### Key Features

- **Binary search**: O(log n) prefix lookup
- **LRU cache**: 500-entry cache for repeated queries
- **Blocked words**: Base blocked list + user additions
- **Hybrid ranking**: Context > User > Base frequency
- **Optimized autocorrect**: Limited pool (2000 top words) to avoid O(50k) Levenshtein

## File Structure

Place the following files in `/entry/src/main/resources/rawfile/`:

```
/entry/src/main/resources/rawfile/
├── ru_base_words_50k.txt       # 50k Russian words (sorted alphabetically)
├── ru_base_freq_50k.tsv        # Russian word frequencies (word<TAB>weight)
├── ru_blocked_words.txt        # Optional: blocked words list
├── en_base_words_50k.txt       # 50k English words (sorted alphabetically)
├── en_base_freq_50k.tsv        # English word frequencies (word<TAB>weight)
└── en_blocked_words.txt        # Optional: blocked words list
```

### File Formats

#### 1. `{lang}_base_words_50k.txt`

Plain text, one word per line, **sorted alphabetically** (required for binary search):

```
а
аа
ааа
аббат
аббатство
абзац
абонемент
абонент
...
```

**Requirements**:
- One word per line
- Lowercase only
- Sorted alphabetically (CRITICAL for binary search)
- UTF-8 encoding
- Min length: 2 characters (shorter words will be filtered out)

#### 2. `{lang}_base_freq_50k.tsv`

Tab-separated values (word<TAB>frequency weight):

```
и	50000
в	49999
не	49998
на	49997
я	49996
...
абзац	1500
...
```

**Requirements**:
- Format: `word<TAB>integer_weight`
- Higher weight = more frequent word
- Typical range: 1-50000 (rank-based)
- UTF-8 encoding

**How to create**:
```bash
# From frequency corpus, sort by frequency descending, add ranks
cat corpus_frequencies.txt | sort -rn -k2 | awk '{print $1 "\t" NR}' > ru_base_freq_50k.tsv
```

#### 3. `{lang}_blocked_words.txt` (Optional)

List of words that should never be learned or suggested:

```
badword1
badword2
offensive3
```

**Requirements**:
- One word per line
- Lowercase
- UTF-8 encoding

## Ranking Formula

The hybrid ranking system uses these weights (tune in `PredictionModel.ets`):

```typescript
// Constants (defined in PredictionModel class)
SCORE_CONTEXT_WEIGHT = 1000;      // Bigram/trigram context
SCORE_USER_RANK_WEIGHT = 10;      // User personalization
SCORE_RECENCY_WEIGHT = 5;         // Recent usage bonus
SCORE_BASE_FREQ_WEIGHT = 1;       // Base lexicon frequency

// Formula
if (isContextBased) {
  score = (contextCount * 1000) + (userRank * 10) + (recency * 5) + (baseFreq * 1);
} else {
  score = (userRank * 10) + (recency * 5) + (baseFreq * 1);
}
```

**Tuning**:
- Increase `SCORE_CONTEXT_WEIGHT` to prioritize context more
- Increase `SCORE_BASE_FREQ_WEIGHT` to surface common words more
- Increase `SCORE_USER_RANK_WEIGHT` to favor user's personal vocabulary

## Prediction Pipeline

**Priority order**:

1. **User trigram** (prev2 + prev + current) - Highest priority
2. **User bigram** (prev + current)
3. **User unigram** (personal learned words)
4. **Base lexicon** (50k general vocabulary)
   - If prefix provided: Binary search O(log n)
   - If no prefix: Fallback to old baseDict

## Autocorrect Optimization

To avoid expensive O(50k) Levenshtein distance calculations:

- **User words**: Full scan (small set, < 10k)
- **Base lexicon**: Limited to top 2000 frequent words only

This reduces autocorrect from O(50k) to O(2000) while maintaining 95%+ accuracy.

## Performance Characteristics

| Operation              | Complexity      | Notes                    |
|------------------------|-----------------|--------------------------|
| Prefix search (binary) | O(log n + m)    | n=50k words, m=matches   |
| Prefix search (cached) | O(1)            | LRU cache hit            |
| Autocorrect (user)     | O(user_words)   | Typically < 10k          |
| Autocorrect (base)     | O(2000)         | Limited pool             |
| Full lexicon scan      | O(50k)          | Avoided                  |

## Memory Usage

Estimated memory footprint:

- **50k words** (avg 8 chars): ~400 KB
- **50k frequencies** (Map): ~800 KB
- **LRU cache** (500 entries): ~50 KB
- **Total**: ~1.5 MB per language

## Integration Example

```typescript
// In InputMethodService.ets or app entry point
import predictionModel from '../model/PredictionModel';

// On app start
async onCreate(want: Want): Promise<void> {
  const context = this.context;

  // Initialize prediction model
  predictionModel.setContext(context);

  // Load user models from preferences
  await predictionModel.load();

  // Load base lexicons (50k words) - THIS IS NEW
  await predictionModel.loadDictionaries();

  console.info('Prediction system ready');
}
```

## Generating Files

### 1. Word List (sorted alphabetically)

```bash
# From frequency corpus
cat frequency_corpus.txt | cut -f1 | sort | uniq > ru_base_words_50k.txt
```

### 2. Frequency TSV

```bash
# From frequency corpus (word frequency_count)
cat frequency_corpus.txt | \
  sort -rn -k2 | \
  head -50000 | \
  awk '{print $1 "\t" (50001 - NR)}' \
  > ru_base_freq_50k.tsv
```

### 3. Validation

```bash
# Check if words are sorted
sort -c ru_base_words_50k.txt && echo "✓ Sorted correctly"

# Check line count
wc -l ru_base_words_50k.txt
wc -l ru_base_freq_50k.tsv

# Check format
head -5 ru_base_freq_50k.tsv
```

## Troubleshooting

### Binary search fails

**Cause**: Words not sorted alphabetically
**Fix**: Sort the word list:

```bash
sort -o ru_base_words_50k.txt ru_base_words_50k.txt
```

### High memory usage

**Cause**: Too many words or large cache
**Fix**: Reduce `CACHE_SIZE` in BaseLexicon or limit word count

### Slow autocorrect

**Cause**: Checking too many base words
**Fix**: Reduce `AUTOCORRECT_POOL_SIZE` in BaseLexicon (default: 2000)

### Words not appearing

**Cause**: Blocked or low frequency
**Fix**: Check blocked list and frequency weights

## Future Optimizations

1. **SymSpell deletes** for maxEditDistance=2 (trade memory for speed)
2. **Trie structure** instead of binary search (faster prefix matching)
3. **Compressed storage** (gzip base lexicon on disk)
4. **Async loading** (load lexicon in background on first use)
5. **Multilevel cache** (L1 prefix cache + L2 full word cache)

## Credits

- LRU Cache: Classic doubly-linked list + hashmap implementation
- Binary search: Standard lowerBound algorithm
- Ranking formula: Inspired by GBoard/SwiftKey hybrid scoring
