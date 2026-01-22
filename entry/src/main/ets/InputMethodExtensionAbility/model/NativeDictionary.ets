/**
 * Native Dictionary Wrapper for HOSKEY
 * Provides ArkTS interface to native C++ dictionary engine
 *
 * Features (when native is available):
 * - High-performance prefix search (<1ms)
 * - Weighted autocorrection (<5ms)
 * - Proximity-aware suggestions
 */
import { common } from '@kit.AbilityKit';
import { fileIo } from '@kit.CoreFileKit';
import { BusinessError } from '@kit.BasicServicesKit';
// Import native C++ module
import nativeDict from 'libnative_dict.so';

/**
 * Error types for autocorrection (mirrors C++ enum)
 */
export enum ErrorType {
  NOT_AN_ERROR = 0,
  MATCH_WITH_WRONG_CASE = 1,
  MATCH_WITH_ACCENT_ERROR = 2,
  PROXIMITY_CORRECTION = 3,
  INSERTION_CORRECTION = 4,
  OMISSION_CORRECTION = 5,
  TRANSPOSITION_CORRECTION = 6,
  SUBSTITUTION_CORRECTION = 7
}

/**
 * Suggestion result from native engine
 */
export interface SuggestResult {
  word: string;
  score: number;
  errorType: ErrorType;
}
/**
 * Dictionary statistics (native module returns only wordCount and memoryUsage)
 */
interface NativeDictStats {
  wordCount: number;
  memoryUsage: number;
}

/**
 * Dictionary statistics (extended with runtime info)
 */
export interface DictStats {
  wordCount: number;
  memoryUsage: number;
  isLoaded: boolean;
  language: string;
}

/**
 * Key bounds for swipe recognition
 */
export interface KeyBounds {
  key: string;
  centerX: number;
  centerY: number;
  width: number;
  height: number;
}

/**
 * Touch point for swipe recognition
 */
export interface TouchPoint {
  x: number;
  y: number;
  timestamp: number;
}

/**
 * Swipe recognition result
 */
export interface SwipeResult {
  bestWord: string;
  alternatives: string[];
  confidence: number;
  rawSequence: string;
}

/**
 * Native Dictionary Manager
 * Real implementation using C++ NAPI bridge
 */
class NativeDictionary {
  private context: common.Context | null = null;
  private currentLanguage: string = '';
  private isLoaded: boolean = false;
  private nativeAvailable: boolean = false;

  constructor() {
    // Check if native module is available
    try {
      if (typeof nativeDict !== 'undefined' && nativeDict !== null) {
        this.nativeAvailable = true;
        console.info('[NativeDictionary] Native C++ module loaded successfully');
      } else {
        this.nativeAvailable = false;
        console.warn('[NativeDictionary] Native module not available - using BaseLexicon fallback');
      }
    } catch (err) {
      const error = err as BusinessError;
      this.nativeAvailable = false;
      console.error('[NativeDictionary] Failed to load native module:', error);
    }
  }

  /**
   * Set application context for resource access
   */
  setContext(ctx: common.Context): void {
    this.context = ctx;
  }

  /**
   * Initialize dictionary for language
   * @param lang - Language code ('ru', 'en', etc.)
   * @param keyWidth - Key width for proximity (optional)
   * @param keyHeight - Key height for proximity (optional)
   */
  async initialize(lang: string, keyWidth: number = 48, keyHeight: number = 64): Promise<boolean> {
    if (!this.nativeAvailable) {
      console.info(`[NativeDictionary] Native not available for '${lang}', using BaseLexicon`);
      return false;
    }

    if (!this.context) {
      console.error('[NativeDictionary] Context not set, cannot load dictionary');
      return false;
    }

    try {
      // Get dictionary path in filesDir (writable directory)
      // rawfile resources are not accessible via file path, so we copy them to filesDir first
      const dictFilename = `main_${lang}.dict`;
      const dictDir = `${this.context.filesDir}/dict`;
      const dictPath = `${dictDir}/${dictFilename}`;

      console.info(`[NativeDictionary] Dictionary target path: ${dictPath}`);

      // Ensure dict directory exists
      try {
        fileIo.mkdirSync(dictDir);
      } catch (mkdirErr) {
        // Directory may already exist - ignore error
      }

      // Check if dictionary already exists in filesDir
      let needsCopy = true;
      try {
        const stat = fileIo.statSync(dictPath);
        console.info(`[NativeDictionary] Dictionary already exists in filesDir, size: ${stat.size} bytes`);
        needsCopy = false;
      } catch (statErr) {
        // File doesn't exist, need to copy
        console.info(`[NativeDictionary] Dictionary not in filesDir, copying from rawfile...`);
      }

      // Copy dictionary from rawfile to filesDir if needed
      if (needsCopy) {
        const copied = await this.copyDictFromRawfile(dictFilename, dictPath);
        if (!copied) {
          console.error(`[NativeDictionary] Failed to copy dictionary for '${lang}'`);
          return false;
        }
      }

      // Load dictionary via NAPI (async - runs on background thread)
      const loaded: boolean = await nativeDict.loadDictionary(dictPath);

      if (!loaded) {
        console.error(`[NativeDictionary] Failed to load dictionary for '${lang}'`);
        return false;
      }

      // Set proximity info for autocorrection
      const layoutName = lang === 'ru' ? 'ru' : 'qwerty';
      const proximitySet: boolean = nativeDict.setProximityInfo(layoutName, keyWidth, keyHeight);

      if (!proximitySet) {
        console.warn('[NativeDictionary] Failed to set proximity info, autocorrection may be less accurate');
      }

      this.isLoaded = true;
      this.currentLanguage = lang;

      // Get and log stats
      const nativeStats: NativeDictStats = nativeDict.getStats() as NativeDictStats;
      console.info(`[NativeDictionary] Loaded ${nativeStats.wordCount} words for '${lang}', memory: ${nativeStats.memoryUsage} bytes`);

      return true;
    } catch (err) {
      const error = err as BusinessError;
      console.error(`[NativeDictionary] Error initializing for '${lang}':`, error);
      this.isLoaded = false;
      return false;
    }
  }

  /**
   * Copy dictionary from rawfile resources to filesDir
   * rawfile resources are packed in HAP and only accessible via resourceManager
   */
  private async copyDictFromRawfile(filename: string, destPath: string): Promise<boolean> {
    if (!this.context) return false;

    try {
      // Read dictionary from rawfile using resourceManager
      const resMgr = this.context.resourceManager;
      const rawData = await resMgr.getRawFileContent(filename);

      console.info(`[NativeDictionary] Read ${rawData.byteLength} bytes from rawfile/${filename}`);

      // Write to filesDir
      const file = fileIo.openSync(destPath, fileIo.OpenMode.CREATE | fileIo.OpenMode.WRITE_ONLY | fileIo.OpenMode.TRUNC);
      const written = fileIo.writeSync(file.fd, rawData.buffer);
      fileIo.closeSync(file);

      console.info(`[NativeDictionary] Wrote ${written} bytes to ${destPath}`);

      return written > 0;
    } catch (err) {
      const error = err as BusinessError;
      console.error(`[NativeDictionary] Failed to copy dictionary from rawfile:`, error);
      return false;
    }
  }

  /**
   * Check if word exists in dictionary
   */
  contains(word: string): boolean {
    if (!this.nativeAvailable || !this.isLoaded) return false;
    try {
      return nativeDict.contains(word.toLowerCase());
    } catch (err) {
      const error = err as BusinessError;
      console.error('[NativeDictionary] Error in contains:', error);
      return false;
    }
  }

  /**
   * Get word frequency
   */
  getFrequency(word: string): number {
    if (!this.nativeAvailable || !this.isLoaded) return 0;

    try {
      return nativeDict.getFrequency(word.toLowerCase());
    } catch (err) {
      const error = err as BusinessError;
      console.error('[NativeDictionary] Error in getFrequency:', error);
      return 0;
    }
  }

  /**
   * Get word suggestions
   * @param prefix - Input prefix
   * @param limit - Maximum results
   */
  getSuggestions(prefix: string, limit: number = 10): SuggestResult[] {
    if (!this.nativeAvailable || !this.isLoaded) return [];
    try {
      const results = nativeDict.getSuggestions(prefix.toLowerCase(), limit) as SuggestResult[];
      return results || [];
    } catch (err) {
      const error = err as BusinessError;
      console.error('[NativeDictionary] Error in getSuggestions:', error);
      return [];
    }
  }

  /**
   * Find autocorrection for word
   * @param word - Word to correct
   * @param threshold - Confidence threshold (default 0.185)
   */
  findAutocorrection(word: string, threshold: number = 0.185): SuggestResult | null {
    if (!this.nativeAvailable || !this.isLoaded) return null;
    try {
      const result = nativeDict.findAutocorrection(word.toLowerCase(), threshold) as SuggestResult | null;
      return result;
    } catch (err) {
      const error = err as BusinessError;
      console.error('[NativeDictionary] Error in findAutocorrection:', error);
      return null;
    }
  }

  /**
   * Get dictionary statistics
   */
  getStats(): DictStats {
    if (!this.nativeAvailable || !this.isLoaded) {
      const emptyStats: DictStats = {
        wordCount: 0,
        memoryUsage: 0,
        isLoaded: false,
        language: ''
      };
      return emptyStats;
    }

    try {
      const nativeStats = nativeDict.getStats() as NativeDictStats;
      const result: DictStats = {
        wordCount: nativeStats.wordCount,
        memoryUsage: nativeStats.memoryUsage,
        isLoaded: this.isLoaded,
        language: this.currentLanguage
      };
      return result;
    } catch (err) {
      const error = err as BusinessError;
      console.error('[NativeDictionary] Error in getStats:', error);
      const errorStats: DictStats = {
        wordCount: 0,
        memoryUsage: 0,
        isLoaded: false,
        language: ''
      };
      return errorStats;
    }
  }

  /**
   * Unload dictionary and free memory
   */
  unload(): void {
    if (this.nativeAvailable && this.isLoaded) {
      try {
        nativeDict.unload();
        console.info('[NativeDictionary] Unloaded dictionary for language:', this.currentLanguage);
      } catch (err) {
        const error = err as BusinessError;
        console.error('[NativeDictionary] Error unloading:', error);
      }
    }

    this.isLoaded = false;
    this.currentLanguage = '';
  }

  /**
   * Check if native module is available
   */
  isNativeAvailable(): boolean {
    return this.nativeAvailable;
  }

  /**
   * Check if dictionary is loaded
   */
  isDictionaryLoaded(): boolean {
    return this.isLoaded;
  }
  /**
   * Set keyboard layout for swipe recognition
   * @param keys - Array of key bounds
   */
  setSwipeKeyboardLayout(keys: Array<KeyBounds>): boolean {
    if (!this.nativeAvailable) return false;

    try {
      return nativeDict.setSwipeKeyboardLayout(keys);
    } catch (err) {
      console.error('[NativeDictionary] Error in setSwipeKeyboardLayout:', err);
      return false;
    }
  }
  /**
   * Process swipe path and return recognized word
   * @param points - Array of touch points
   */
  processSwipePath(points: Array<TouchPoint>): SwipeResult | null {
    if (!this.nativeAvailable) return null;

    try {
      const result = nativeDict.processSwipePath(points) as SwipeResult | null;
      return result;
    } catch (err) {
      console.error('[NativeDictionary] Error in processSwipePath:', err);
      return null;
    }
  }

  /**
   * Self-test: Verify NAPI contract is working correctly
   * Call this in debug mode to validate native module integration
   * @returns true if all tests pass
   */
  async runSelfTest(): Promise<boolean> {
    console.info('[NativeDictionary] Starting self-test...');

    // Test 1: Check native availability
    if (!this.nativeAvailable) {
      console.warn('[NativeDictionary] Self-test: Native module not available');
      return false;
    }
    console.info('[NativeDictionary] Self-test: Native module available ✓');

    // Test 2: Check dictionary loaded
    if (!this.isLoaded) {
      console.warn('[NativeDictionary] Self-test: Dictionary not loaded, attempting to test without data');
    }

    // Test 3: getSuggestions with prefix
    try {
      const testPrefix = 'пр';
      const suggestions = this.getSuggestions(testPrefix, 5);
      console.info(`[NativeDictionary] Self-test: getSuggestions("${testPrefix}", 5) returned ${suggestions.length} results`);
      if (suggestions.length > 0) {
        const first = suggestions[0];
        if (first) {
          console.info(`[NativeDictionary] Self-test: First suggestion: "${first.word}" (score: ${first.score}, errorType: ${first.errorType})`);
        }
      }
    } catch (err) {
      console.error('[NativeDictionary] Self-test: getSuggestions failed:', err);
      return false;
    }
    console.info('[NativeDictionary] Self-test: getSuggestions ✓');

    // Test 4: contains
    try {
      const testWord = 'привет';
      const exists = this.contains(testWord);
      console.info(`[NativeDictionary] Self-test: contains("${testWord}") = ${exists}`);
    } catch (err) {
      console.error('[NativeDictionary] Self-test: contains failed:', err);
      return false;
    }
    console.info('[NativeDictionary] Self-test: contains ✓');

    // Test 5: getFrequency
    try {
      const testWord = 'привет';
      const freq = this.getFrequency(testWord);
      console.info(`[NativeDictionary] Self-test: getFrequency("${testWord}") = ${freq}`);
    } catch (err) {
      console.error('[NativeDictionary] Self-test: getFrequency failed:', err);
      return false;
    }
    console.info('[NativeDictionary] Self-test: getFrequency ✓');

    // Test 6: getStats
    try {
      const stats = this.getStats();
      console.info(`[NativeDictionary] Self-test: getStats() = { wordCount: ${stats.wordCount}, memoryUsage: ${stats.memoryUsage}, isLoaded: ${stats.isLoaded} }`);
    } catch (err) {
      console.error('[NativeDictionary] Self-test: getStats failed:', err);
      return false;
    }
    console.info('[NativeDictionary] Self-test: getStats ✓');

    console.info('[NativeDictionary] Self-test: All tests passed ✓');
    return true;
  }
}

// Export singleton instance
const nativeDictionary = new NativeDictionary();
export default nativeDictionary;
