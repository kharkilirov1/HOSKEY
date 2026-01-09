import { common } from '@kit.AbilityKit';
import { BusinessError } from '@kit.BasicServicesKit';
import { util } from '@kit.ArkTS';
import { LRUCache } from './LRUCache';

// Base lexicon entry with frequency weight
interface LexEntry {
  word: string;
  freq: number; // Frequency weight from corpus
}

// Cached prefix completion result
interface PrefixResult {
  candidates: LexEntry[];
}

// Statistics result for BaseLexicon
interface StatsResult {
  words: number;
  cached: number;
  blocked: number;
}

/**
 * BaseLexicon - Read-only frequency-based lexicon for 50k common words
 *
 * Features:
 * - Binary search for O(log n) prefix lookup
 * - LRU cache for repeated prefix queries
 * - Sorted word list for efficient range queries
 * - Separate from user personalization model
 */
export class BaseLexicon {
  private context: common.Context | null = null;
  private words: string[] = []; // Sorted alphabetically
  private freqMap: Map<string, number> = new Map(); // word -> frequency weight
  private prefixCache: LRUCache<string, PrefixResult>;
  private blockedWords: Set<string> = new Set(); // Base blocked words list
  private loaded: boolean = false;

  // Cache configuration
  private readonly CACHE_SIZE = 500; // LRU cache capacity

  // Autocorrect configuration
  private readonly AUTOCORRECT_POOL_SIZE = 2000; // Top N frequent words for autocorrect
  private topFrequentWords: string[] = []; // Pre-sorted by frequency

  constructor() {
    this.prefixCache = new LRUCache<string, PrefixResult>(this.CACHE_SIZE);
  }

  setContext(ctx: common.Context): void {
    this.context = ctx;
  }

  isLoaded(): boolean {
    console.info(`[BaseLexicon] isLoaded() = ${this.loaded}, words.length=${this.words.length}`);
    return this.loaded;
  }

  /**
   * Load base lexicon from rawfile resources
   * Call this on app initialization
   */
  async load(lang: string): Promise<void> {
    console.info(`[BaseLexicon] load("${lang}") called`);

    if (!this.context) {
      console.warn('[BaseLexicon] Context not set, cannot load');
      return;
    }

    console.info(`[BaseLexicon] Context OK, loading files for lang="${lang}"`);

    try {
      // Load word list (50k words, sorted alphabetically)
      const wordsFile = `${lang}_base_words_50k.txt`;
      console.info(`[BaseLexicon] Loading ${wordsFile}...`);
      const wordsList = await this.loadTextFile(wordsFile);
      console.info(`[BaseLexicon] Loaded ${wordsList.length} raw words from ${wordsFile}`);

      // Load frequency weights (word<TAB>weight)
      const freqFile = `${lang}_base_freq_50k.tsv`;
      console.info(`[BaseLexicon] Loading ${freqFile}...`);
      const freqData = await this.loadFrequencyData(freqFile);
      console.info(`[BaseLexicon] Loaded ${freqData.size} frequencies from ${freqFile}`);

      // Load blocked words list
      const blockedFile = `${lang}_blocked_words.txt`;
      console.info(`[BaseLexicon] Loading ${blockedFile}...`);
      const blockedList = await this.loadTextFile(blockedFile);
      console.info(`[BaseLexicon] Loaded ${blockedList.length} blocked words from ${blockedFile}`);

      // Process and validate
      this.words = wordsList
        .map(w => w.trim().toLowerCase())
        .filter(w => w.length >= 2)
        .sort(); // Ensure alphabetical order for binary search

      console.info(`[BaseLexicon] After filtering: ${this.words.length} words`);
      if (this.words.length > 0) {
        console.info(`[BaseLexicon] First 5 words: [${this.words.slice(0, 5).join(', ')}]`);
        console.info(`[BaseLexicon] Last 5 words: [${this.words.slice(-5).join(', ')}]`);
      }

      this.freqMap = freqData;

      this.blockedWords = new Set(
        blockedList.map(w => w.trim().toLowerCase()).filter(w => w.length > 0)
      );

      // Build top frequent words for autocorrect (sorted by frequency descending)
      const entriesWithFreq: LexEntry[] = [];
      this.words.forEach(word => {
        const freq = this.freqMap.get(word) ?? 0;
        if (freq > 0) {
          entriesWithFreq.push({ word, freq });
        }
      });

      entriesWithFreq.sort((a, b) => b.freq - a.freq); // Descending by frequency
      this.topFrequentWords = entriesWithFreq
        .slice(0, this.AUTOCORRECT_POOL_SIZE)
        .map(e => e.word);

      this.loaded = true;
      console.info(`[BaseLexicon] === LOADED SUCCESSFULLY ===`);
      console.info(`[BaseLexicon] Total: ${this.words.length} words, ${this.freqMap.size} frequencies, ${this.blockedWords.size} blocked words for '${lang}'`);
      console.info(`[BaseLexicon] Top 5 frequent words: [${this.topFrequentWords.slice(0, 5).join(', ')}]`);

    } catch (err) {
      const error = err as BusinessError;
      console.error(`BaseLexicon: Failed to load for '${lang}'. Code: ${error.code}, Msg: ${error.message}`);
      this.loaded = false;
    }
  }

  /**
   * Load text file from rawfile (one word per line)
   */
  private async loadTextFile(filename: string): Promise<string[]> {
    if (!this.context) return [];

    try {
      const resMgr = this.context.resourceManager;
      const fileData = await resMgr.getRawFileContent(filename);
      const decoder = util.TextDecoder.create('utf-8');
      const text = decoder.decodeToString(new Uint8Array(fileData.buffer));

      return text.split('\n')
        .map(line => line.trim())
        .filter(line => line.length > 0);

    } catch (err) {
      // File might not exist (e.g., blocked_words.txt is optional)
      console.warn(`BaseLexicon: Could not load ${filename}, using empty list`);
      return [];
    }
  }

  /**
   * Load frequency data from TSV file (word<TAB>weight)
   */
  private async loadFrequencyData(filename: string): Promise<Map<string, number>> {
    const freqMap = new Map<string, number>();

    if (!this.context) return freqMap;

    try {
      const resMgr = this.context.resourceManager;
      const fileData = await resMgr.getRawFileContent(filename);
      const decoder = util.TextDecoder.create('utf-8');
      const text = decoder.decodeToString(new Uint8Array(fileData.buffer));

      const lines = text.split('\n');
      for (const line of lines) {
        const trimmed = line.trim();
        if (!trimmed) continue;

        const parts = trimmed.split('\t');
        if (parts.length >= 2) {
          const word = parts[0].trim().toLowerCase();
          const freq = parseInt(parts[1].trim(), 10);
          if (word && !isNaN(freq)) {
            freqMap.set(word, freq);
          }
        }
      }

      console.info(`BaseLexicon: Loaded ${freqMap.size} frequency weights from ${filename}`);
      return freqMap;

    } catch (err) {
      const error = err as BusinessError;
      console.error(`BaseLexicon: Failed to load ${filename}. Code: ${error.code}, Msg: ${error.message}`);
      return freqMap;
    }
  }

  /**
   * Binary search: find first index where words[i] >= prefix
   */
  private lowerBound(prefix: string): number {
    let left = 0;
    let right = this.words.length;

    while (left < right) {
      const mid = Math.floor((left + right) / 2);
      if (this.words[mid] < prefix) {
        left = mid + 1;
      } else {
        right = mid;
      }
    }

    return left;
  }

  /**
   * Find all words matching prefix (binary search + linear scan)
   * Returns top N candidates sorted by frequency
   */
  findByPrefix(prefix: string, maxResults: number = 20): LexEntry[] {
    console.info(`[BaseLexicon] findByPrefix("${prefix}", ${maxResults})`);
    console.info(`[BaseLexicon]   loaded=${this.loaded}, words.length=${this.words.length}, freqMap.size=${this.freqMap.size}`);

    if (!this.loaded || !prefix || prefix.length === 0) {
      console.info(`[BaseLexicon]   Early return: loaded=${this.loaded}, prefix.length=${prefix?.length ?? 0}`);
      return [];
    }

    const prefixLower = prefix.toLowerCase();
    console.info(`[BaseLexicon]   prefixLower="${prefixLower}"`);

    const cacheKey = `${prefixLower}:${maxResults}`;

    // Check cache
    const cached = this.prefixCache.get(cacheKey);
    if (cached) {
      console.info(`[BaseLexicon]   Cache HIT: ${cached.candidates.length} results`);
      return cached.candidates;
    }

    // Binary search for starting position
    const startIdx = this.lowerBound(prefixLower);
    console.info(`[BaseLexicon]   Binary search startIdx=${startIdx}`);

    if (startIdx < this.words.length) {
      console.info(`[BaseLexicon]   words[${startIdx}]="${this.words[startIdx]}"`);
      console.info(`[BaseLexicon]   First 5 words from startIdx: [${this.words.slice(startIdx, startIdx + 5).join(', ')}]`);
    }

    // Collect matching words
    const matches: LexEntry[] = [];
    for (let i = startIdx; i < this.words.length; i++) {
      const word = this.words[i];

      // Stop when prefix no longer matches
      if (!word.startsWith(prefixLower)) {
        break;
      }

      // Skip blocked words
      if (this.blockedWords.has(word)) {
        continue;
      }

      const freq = this.freqMap.get(word) ?? 1;
      matches.push({ word, freq });
    }

    console.info(`[BaseLexicon]   Found ${matches.length} matches`);
    if (matches.length > 0) {
      console.info(`[BaseLexicon]   Top 3 matches: [${matches.slice(0, 3).map(m => `"${m.word}"(${m.freq})`).join(', ')}]`);
    }

    // Sort by frequency descending
    matches.sort((a, b) => b.freq - a.freq);

    // Take top N
    const topMatches = matches.slice(0, maxResults);

    // Cache result
    this.prefixCache.set(cacheKey, { candidates: topMatches });

    console.info(`[BaseLexicon]   Returning ${topMatches.length} results`);
    return topMatches;
  }

  /**
   * Get frequency weight for a word
   */
  getFrequency(word: string): number {
    return this.freqMap.get(word.toLowerCase()) ?? 0;
  }

  /**
   * Check if word exists in base lexicon
   */
  contains(word: string): boolean {
    const wordLower = word.toLowerCase();
    // Binary search for exact match
    const idx = this.lowerBound(wordLower);
    return idx < this.words.length && this.words[idx] === wordLower;
  }

  /**
   * Check if word is in blocked list
   */
  isBlocked(word: string): boolean {
    return this.blockedWords.has(word.toLowerCase());
  }

  /**
   * Add word to blocked list (runtime only, not persisted)
   */
  blockWord(word: string): void {
    this.blockedWords.add(word.toLowerCase());
    this.prefixCache.clear(); // Invalidate cache
  }

  /**
   * Get top frequent words for autocorrect (pre-sorted, limited pool)
   */
  getTopFrequentWords(): string[] {
    return this.topFrequentWords;
  }

  /**
   * Get words starting with same first letter (for autocorrect optimization)
   */
  getWordsByFirstLetter(letter: string): string[] {
    if (!this.loaded || !letter || letter.length === 0) {
      return [];
    }

    const letterLower = letter.toLowerCase();
    const startIdx = this.lowerBound(letterLower);
    const nextLetter = String.fromCharCode(letterLower.charCodeAt(0) + 1);
    const endIdx = this.lowerBound(nextLetter);

    const result: string[] = [];
    for (let i = startIdx; i < endIdx && i < this.words.length; i++) {
      if (!this.blockedWords.has(this.words[i])) {
        result.push(this.words[i]);
      }
    }

    return result;
  }

  /**
   * Clear cache (call when language changes)
   */
  clearCache(): void {
    this.prefixCache.clear();
  }

  /**
   * Get statistics
   */
  getStats(): StatsResult {
    return {
      words: this.words.length,
      cached: this.prefixCache.getSize(),
      blocked: this.blockedWords.size
    };
  }
}
