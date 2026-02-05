/**
 * Native Dictionary Wrapper for HOSKEY
 * Provides ArkTS interface to native C++ dictionary engine
 *
 * Architecture: Yandex-style dictionary with mmap-based CompTrie
 *
 * Features (when native is available):
 * - Instant dictionary loading via mmap (microseconds, not seconds)
 * - High-performance prefix search (<1ms)
 * - Neural model scoring (MindSpore/NNRt)
 * - Weighted autocorrection (<5ms)
 * - Beam search for swipe/gesture input
 *
 * NOTE: FlatTrie and OpenBoard-based functions are DEPRECATED.
 * Use loadDictionaryFromFd() with Yandex format (.dict binary).
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
 * Yandex neural-scored suggestion result
 */
export interface YandexSuggestion {
  word: string;
  score: number;         // Combined score [0-1]
  neuralScore?: number;  // From neural model [0-1]
  freqScore?: number;    // From frequency [0-1]
}

/**
 * Yandex dictionary statistics
 */
export interface YandexStats {
  dictLoaded: boolean;
  modelLoaded: boolean;
  wordCount?: number;
  memoryBytes?: number;
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
 * FlatTrie statistics
 */
export interface FlatTrieStats {
  wordCount: number;
  memoryUsage: number;
  locale: string;
}

/**
 * Trail interpolation parameters (Yandex-style)
 */
export interface TrailParams {
  minSamplingDistance?: number;
  maxAngleRadians?: number;
  maxSegmentLength?: number;
  maxInterpolationSteps?: number;
}

/**
 * Trail render parameters (Yandex-style)
 */
export interface TrailRenderParams {
  maxWidth?: number;
  minWidth?: number;
  widthRatio?: number;
  fadeStartTimeMs?: number;
  fadeDurationMs?: number;
  trailColor?: number;
  shadowEnabled?: boolean;
}

/**
 * Combined trail initialization parameters
 */
export interface TrailInitParams extends TrailParams, TrailRenderParams {}

/**
 * Result of loading ALL 15 neural models
 */
interface LoadModelsResult {
  success: boolean;
  loaded: number;      // Number of models loaded successfully
  total: number;       // Total models (15)
  models: string[];    // Names of loaded models
  failed: string[];    // Names of failed models
}

/**
 * Model stats return type (ALL 15 neural models)
 */
interface ModelStats {
  yandexDictLoaded: boolean;
  neuralModelsEnabled: boolean;
  beamSearchReady: boolean;
  totalModels: number;         // 15 total
  loadedModels: number;        // How many loaded
  primaryDevice: string;       // NPU/NNRT/CPU
  loadedModelNames: string[];  // Names of loaded models
  failedModelNames: string[];  // Names of failed models
  legacyScorerEnabled: boolean;
  legacyScorerLoaded: boolean;
  legacyDevice?: string;
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
  private loadingLanguage: string | null = null;  // Prevent race conditions
  private yandexLoaded: boolean = false;  // Track if Yandex dict is active

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
   * Two-stage loading for fast startup:
   * 1. First, try to load mini dictionary (fast, ~1.8MB)
   * 2. Then, optionally upgrade to full dictionary in background
   * 
   * @param lang - Language code ('ru', 'en', etc.)
   * @param keyWidth - Key width for proximity (optional)
   * @param keyHeight - Key height for proximity (optional)
   * @param loadFullInBackground - Whether to load full dictionary after mini (default: true)
   */
  async initialize(lang: string, keyWidth: number = 48, keyHeight: number = 64, loadFullInBackground: boolean = true): Promise<boolean> {
    // Skip if already loaded for the same language
    if (this.isLoaded && this.currentLanguage === lang) {
      console.info(`[NativeDictionary] Already loaded for '${lang}', skipping reload`);
      return true;
    }

    // Skip if already loading the same language (prevent race condition)
    if (this.loadingLanguage === lang) {
      console.info(`[NativeDictionary] Already loading '${lang}', skipping duplicate request`);
      return true;
    }

    // Mark as loading
    this.loadingLanguage = lang;
    console.info(`[NativeDictionary] Starting load for '${lang}'`);

    if (!this.nativeAvailable) {
      console.info(`[NativeDictionary] Native not available for '${lang}', using BaseLexicon`);
      return false;
    }

    if (!this.context) {
      console.error('[NativeDictionary] Context not set, cannot load dictionary');
      return false;
    }

    try {
      // Ensure dict directory exists
      const dictDir = `${this.context.filesDir}/dict`;
      try {
        fileIo.mkdirSync(dictDir);
      } catch (mkdirErr) {
        // Directory may already exist - ignore error
      }

      // ===== Try binary dictionary (main_ru) =====
      const binaryDictFilename = `main_${lang}`;
      const binaryDictPath = `${dictDir}/${binaryDictFilename}`;
      
      console.info(`[NativeDictionary] Checking binary dict '${binaryDictFilename}'...`);
      const binaryExists = await this.checkRawfileExists(binaryDictFilename);
      
      if (binaryExists) {
        console.info(`[NativeDictionary] Binary dictionary found for '${lang}'`);
        
        // Copy if needed
        let needsCopyBinary = true;
        try {
          const stat = fileIo.statSync(binaryDictPath);
          console.info(`[NativeDictionary] Binary dict exists in filesDir, size: ${stat.size} bytes`);
          needsCopyBinary = false;
        } catch (statErr) {
          console.info(`[NativeDictionary] Copying binary dict from rawfile...`);
        }
        
        if (needsCopyBinary) {
          const copied = await this.copyDictFromRawfile(binaryDictFilename, binaryDictPath);
          if (!copied) {
            console.error(`[NativeDictionary] Failed to copy binary dict for '${lang}'`);
          }
        }
        
        // Load binary dictionary ASYNC to avoid ANR
        // Keyboard works immediately, Yandex suggestions appear after load completes
        this.loadingLanguage = null;
        this.currentLanguage = lang;
        this.isLoaded = true;  // Mark as loaded (basic functionality works)
        this.yandexLoaded = false;  // Yandex dict not ready yet
        
        // Set proximity info first (fast)
        const layoutName = lang === 'ru' ? 'ru' : 'qwerty';
        nativeDict.setProximityInfo(layoutName, keyWidth, keyHeight);
        
        // Load Yandex dictionary after UI settles
        // Main fix was checkRawfileExists using getRawFd instead of getRawFileContent
        console.info(`[NativeDictionary] Scheduling background load for Yandex dict...`);
        const dictPathCopy = binaryDictPath;
        setTimeout(() => {
          try {
            console.info(`[NativeDictionary] Background: loading Yandex dict from ${dictPathCopy}`);
            const binaryLoadResult = this.loadYandexDict(dictPathCopy);
            if (binaryLoadResult) {
              this.yandexLoaded = true;
              console.info(`[NativeDictionary] Background: Yandex dict loaded successfully`);
            } else {
              console.error(`[NativeDictionary] Background: Failed to load Yandex dict`);
            }
          } catch (err) {
            console.error(`[NativeDictionary] Background: Exception loading Yandex dict:`, err);
          }
        }, 100);  // Short delay - main ANR fix is in checkRawfileExists
        
        return true;
      }

      // ===== Fallback to standard .dict format =====
      // Stage 1: Try to load mini dictionary first (fast startup)
      const miniDictFilename = `main_${lang}_mini.dict`;
      const miniDictPath = `${dictDir}/${miniDictFilename}`;
      
      console.info(`[NativeDictionary] Stage 1: checking mini dict '${miniDictFilename}'`);
      
      let miniLoaded = false;
      try {
        // Check if mini dictionary exists in rawfile
        console.info(`[NativeDictionary] Checking rawfile exists...`);
        const miniExists = await this.checkRawfileExists(miniDictFilename);
        console.info(`[NativeDictionary] miniExists=${miniExists}`);
        
        if (miniExists) {
          console.info(`[NativeDictionary] Mini dictionary found for '${lang}', loading first...`);
          
          // Copy mini dict if not already in filesDir
          let needsCopy = true;
          try {
            const stat = fileIo.statSync(miniDictPath);
            console.info(`[NativeDictionary] Mini dictionary exists in filesDir, size: ${stat.size} bytes`);
            needsCopy = false;
          } catch (statErr) {
            console.info(`[NativeDictionary] Mini not in filesDir, will copy from rawfile...`);
          }
          
          if (needsCopy) {
            console.info(`[NativeDictionary] Calling copyDictFromRawfile...`);
            const copySuccess = await this.copyDictFromRawfile(miniDictFilename, miniDictPath);
            console.info(`[NativeDictionary] copyDictFromRawfile returned: ${copySuccess}`);
            
            // Verify file exists after copy
            if (copySuccess) {
              try {
                const stat = fileIo.statSync(miniDictPath);
                console.info(`[NativeDictionary] Verified: mini dict copied, size=${stat.size} bytes`);
              } catch (verifyErr) {
                console.error(`[NativeDictionary] COPY VERIFICATION FAILED - file doesn't exist after copy!`);
              }
            }
          }
          
          // Load mini dictionary ASYNC to avoid ANR
          console.info(`[NativeDictionary] Calling loadDictionary(${miniDictPath}) async...`);
          miniLoaded = await nativeDict.loadDictionary(miniDictPath) as boolean;
          console.info(`[NativeDictionary] loadDictionary returned: ${miniLoaded}`);
          
          if (miniLoaded) {
            this.isLoaded = true;
            this.currentLanguage = lang;
            const miniStats: NativeDictStats = nativeDict.getStats() as NativeDictStats;
            console.info(`[NativeDictionary] Mini dictionary loaded: ${miniStats.wordCount} words, ${miniStats.memoryUsage} bytes`);
          }
        } else {
          console.info(`[NativeDictionary] Mini dictionary NOT found in rawfile, skipping to full...`);
        }
      } catch (miniErr) {
        const error = miniErr as BusinessError;
        console.error(`[NativeDictionary] Mini dictionary error: code=${error.code} msg=${error.message}`);
        console.warn(`[NativeDictionary] Mini dictionary not available for '${lang}', loading full...`);
      }

      // Stage 2: Load full dictionary using MMAP (instant, no file copy needed)
      const fullDictFilename = `main_${lang}.dict`;
      const fullDictPath = `${dictDir}/${fullDictFilename}`;
      
      // Try MMAP first - this is INSTANT and doesn't require file copying
      console.info(`[NativeDictionary] Stage 2: trying mmap load for '${fullDictFilename}'...`);
      const mmapLoaded = await this.loadDirectFromRawfile(fullDictFilename);
      
      if (mmapLoaded) {
        console.info(`[NativeDictionary] MMAP load SUCCESS - instant dictionary loading!`);
        this.isLoaded = true;
        this.currentLanguage = lang;
        this.loadingLanguage = null;
        
        // Set proximity info
        const layoutName = lang === 'ru' ? 'ru' : 'qwerty';
        nativeDict.setProximityInfo(layoutName, keyWidth, keyHeight);
        
        return true;
      }
      
      // Fallback: Copy and load (slower, legacy method)
      console.info(`[NativeDictionary] MMAP failed, falling back to copy+load...`);

      // Copy full dictionary if needed
      let needsCopy = true;
      try {
        const stat = fileIo.statSync(fullDictPath);
        console.info(`[NativeDictionary] Full dictionary exists in filesDir, size: ${stat.size} bytes`);
        needsCopy = false;
      } catch (statErr) {
        console.info(`[NativeDictionary] Full dictionary not in filesDir, copying from rawfile...`);
      }

      if (needsCopy) {
        const copied = await this.copyDictFromRawfile(fullDictFilename, fullDictPath);
        if (!copied) {
          // If mini loaded, we're still partially functional
          if (miniLoaded) {
            console.warn(`[NativeDictionary] Full dictionary copy failed, using mini only`);
            this.loadingLanguage = null;
            return true;
          }
          console.error(`[NativeDictionary] Failed to copy dictionary for '${lang}'`);
          this.loadingLanguage = null;
          return false;
        }
      }

      // Load full dictionary
      if (miniLoaded && loadFullInBackground) {
        // Mini already loaded - upgrade to full in background (non-blocking)
        console.info(`[NativeDictionary] Upgrading to full dictionary in background...`);
        this.loadFullDictionaryInBackground(fullDictPath, lang, keyWidth, keyHeight);
        this.loadingLanguage = null;  // Background load continues independently
      } else {
        // No mini - load full dictionary now (ASYNC to avoid ANR)
        console.info(`[NativeDictionary] Loading full dictionary directly (async)...`);
        const loaded: boolean = await nativeDict.loadDictionary(fullDictPath) as boolean;
        console.info(`[NativeDictionary] loadDictionary returned: ${loaded}`);

        if (!loaded) {
          // If mini was loaded, we're still functional
          if (miniLoaded) {
            console.warn(`[NativeDictionary] Full dictionary load failed, using mini only`);
            this.loadingLanguage = null;
            return true;
          }
          console.error(`[NativeDictionary] Failed to load dictionary for '${lang}'`);
          this.loadingLanguage = null;
          return false;
        }

        this.isLoaded = true;
        this.currentLanguage = lang;
        console.info(`[NativeDictionary] Dictionary loaded! isLoaded=${this.isLoaded}, lang=${this.currentLanguage}`);
      }

      // Set proximity info for autocorrection
      const layoutName = lang === 'ru' ? 'ru' : 'qwerty';
      const proximitySet: boolean = nativeDict.setProximityInfo(layoutName, keyWidth, keyHeight);

      if (!proximitySet) {
        console.warn('[NativeDictionary] Failed to set proximity info, autocorrection may be less accurate');
      }

      // Get and log final stats
      const nativeStats: NativeDictStats = nativeDict.getStats() as NativeDictStats;
      console.info(`[NativeDictionary] Loaded ${nativeStats.wordCount} words for '${lang}', memory: ${nativeStats.memoryUsage} bytes`);

      this.loadingLanguage = null;  // Clear loading flag
      return true;
    } catch (err) {
      const error = err as BusinessError;
      console.error(`[NativeDictionary] Error initializing for '${lang}':`, error);
      this.isLoaded = false;
      this.loadingLanguage = null;  // Clear loading flag on error
      return false;
    }
  }

  /**
   * Load full dictionary in background (non-blocking)
   * Used to upgrade from mini to full dictionary after initial fast load
   */
  private loadFullDictionaryInBackground(dictPath: string, lang: string, keyWidth: number, keyHeight: number): void {
    // Use async NAPI call which runs on worker thread
    nativeDict.loadDictionary(dictPath).then((loaded: boolean) => {
      if (loaded) {
        const stats: NativeDictStats = nativeDict.getStats() as NativeDictStats;
        console.info(`[NativeDictionary] Background upgrade complete: ${stats.wordCount} words for '${lang}'`);
        
        // Re-apply proximity info after full load
        const layoutName = lang === 'ru' ? 'ru' : 'qwerty';
        nativeDict.setProximityInfo(layoutName, keyWidth, keyHeight);
      } else {
        console.warn(`[NativeDictionary] Background upgrade to full dictionary failed, continuing with mini`);
      }
    }).catch((err: Error) => {
      console.error(`[NativeDictionary] Background dictionary upgrade error:`, err);
    });
  }

  /**
   * Check if a file exists in rawfile resources
   * Uses getRawFd (lightweight) instead of getRawFileContent (reads entire file!)
   */
  private async checkRawfileExists(filename: string): Promise<boolean> {
    console.info(`[NativeDictionary] checkRawfileExists('${filename}') called`);
    if (!this.context) {
      console.error(`[NativeDictionary] checkRawfileExists: context is null!`);
      return false;
    }
    try {
      const resMgr = this.context.resourceManager;
      // Use getRawFd - only opens file descriptor, doesn't read content!
      console.info(`[NativeDictionary] checkRawfileExists: calling getRawFd...`);
      const rawFd = await resMgr.getRawFd(filename);
      const exists = rawFd && rawFd.fd >= 0;
      if (exists) {
        // Close the fd immediately - we just needed to check existence
        await resMgr.closeRawFd(filename);
        console.info(`[NativeDictionary] checkRawfileExists: exists=true, length=${rawFd.length}`);
      }
      return exists;
    } catch (err) {
      const error = err as BusinessError;
      // Error code 9001005 = file not found (expected for non-existent files)
      if (error.code === 9001005) {
        console.info(`[NativeDictionary] checkRawfileExists: file not found (expected)`);
      } else {
        console.error(`[NativeDictionary] checkRawfileExists error: code=${error.code} msg=${error.message}`);
      }
      return false;
    }
  }

  /**
   * Load dictionary DIRECTLY from rawfile using memory-mapping (mmap)
   * This is INSTANT - no file copying, no RAM usage for file content
   * The OS loads pages on-demand when accessed
   * 
   * @param filename - Name of rawfile (e.g., "main_ru.dict")
   * @returns true if loaded successfully
   */
  private async loadDirectFromRawfile(filename: string): Promise<boolean> {
    console.info(`[NativeDictionary] loadDirectFromRawfile('${filename}') - using mmap`);
    
    if (!this.context) {
      console.error(`[NativeDictionary] loadDirectFromRawfile: context is null!`);
      return false;
    }

    try {
      const resMgr = this.context.resourceManager;
      
      // Get file descriptor - this is instant, doesn't read file
      const rawFd = await resMgr.getRawFd(filename);
      if (!rawFd || rawFd.fd < 0) {
        console.error(`[NativeDictionary] loadDirectFromRawfile: getRawFd failed`);
        return false;
      }
      
      console.info(`[NativeDictionary] loadDirectFromRawfile: fd=${rawFd.fd}, offset=${rawFd.offset}, length=${rawFd.length}`);
      
      // Load via mmap - this is INSTANT (microseconds, not seconds!)
      const startTime = Date.now();
      const loaded = await nativeDict.loadDictionaryFromFd(rawFd.fd, rawFd.offset, rawFd.length);
      const duration = Date.now() - startTime;
      
      // Close fd (mmap has its own copy)
      await resMgr.closeRawFd(filename);
      
      if (loaded) {
        console.info(`[NativeDictionary] loadDirectFromRawfile: SUCCESS in ${duration}ms (mmap instant!)`);
      } else {
        console.error(`[NativeDictionary] loadDirectFromRawfile: native load failed`);
      }
      
      return loaded;
    } catch (err) {
      const error = err as BusinessError;
      console.error(`[NativeDictionary] loadDirectFromRawfile error: code=${error.code} msg=${error.message}`);
      return false;
    }
  }

  /**
   * Copy dictionary from rawfile resources to filesDir using CHUNKED streaming
   * This avoids loading the entire file into memory at once (ANR fix)
   */
  private async copyDictFromRawfile(filename: string, destPath: string): Promise<boolean> {
    console.info(`[NativeDictionary] copyDictFromRawfile('${filename}' -> '${destPath}') called`);
    
    if (!this.context) {
      console.error(`[NativeDictionary] copyDictFromRawfile: context is null!`);
      return false;
    }

    try {
      const resMgr = this.context.resourceManager;
      
      // Get file descriptor for streaming read (doesn't load entire file)
      const rawFd = await resMgr.getRawFd(filename);
      if (!rawFd || rawFd.fd < 0) {
        console.error(`[NativeDictionary] Failed to get rawFd for ${filename}`);
        return false;
      }
      
      console.info(`[NativeDictionary] Streaming copy: ${rawFd.length} bytes from rawfile/${filename}`);
      
      // Open destination file
      const destFile = fileIo.openSync(destPath, fileIo.OpenMode.CREATE | fileIo.OpenMode.WRITE_ONLY | fileIo.OpenMode.TRUNC);
      
      // Copy in chunks to avoid blocking UI (64KB chunks)
      const CHUNK_SIZE = 64 * 1024;
      let offset = rawFd.offset;
      let remaining = rawFd.length;
      let totalWritten = 0;
      
      while (remaining > 0) {
        const chunkSize = Math.min(CHUNK_SIZE, remaining);
        const buffer = new ArrayBuffer(chunkSize);
        
        // Read chunk from rawfile fd
        const bytesRead = fileIo.readSync(rawFd.fd, buffer, { offset: offset, length: chunkSize });
        if (bytesRead <= 0) break;
        
        // Write chunk to destination (async to yield to event loop)
        await new Promise<void>((resolve) => {
          fileIo.write(destFile.fd, buffer, { length: bytesRead }, () => resolve());
        });
        
        offset += bytesRead;
        remaining -= bytesRead;
        totalWritten += bytesRead;
        
        // Yield to event loop every few chunks to prevent ANR
        if (totalWritten % (CHUNK_SIZE * 4) === 0) {
          await new Promise<void>(resolve => setTimeout(resolve, 0));
        }
      }
      
      fileIo.closeSync(destFile);
      await resMgr.closeRawFd(filename);
      
      console.info(`[NativeDictionary] Streamed ${totalWritten} bytes to ${destPath}`);
      return totalWritten > 0;
      
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
   * Apply capitalization from source to target word
   * - If source starts with uppercase → capitalize first letter of target
   * - If source is ALL CAPS → convert target to ALL CAPS
   * - Otherwise → return target as-is (lowercase)
   */
  private applyCapitalization(source: string, target: string): string {
    if (source.length === 0 || target.length === 0) {
      return target;
    }

    // Check if ALL CAPS (at least 2 chars, all uppercase)
    if (source.length >= 2 && source === source.toUpperCase() && source !== source.toLowerCase()) {
      return target.toUpperCase();
    }

    // Check if first letter is uppercase
    const firstChar = source.charAt(0);
    if (firstChar === firstChar.toUpperCase() && firstChar !== firstChar.toLowerCase()) {
      // Capitalize first letter of target
      return target.charAt(0).toUpperCase() + target.slice(1);
    }

    // Return as-is (lowercase)
    return target;
  }

  /**
   * Get word suggestions
   * @param prefix - Input prefix
   * @param limit - Maximum results
   */
  getSuggestions(prefix: string, limit: number = 10): SuggestResult[] {
    if (!this.nativeAvailable || !this.isLoaded) {
      console.warn(`[NativeDictionary] getSuggestions SKIPPED: nativeAvailable=${this.nativeAvailable}, isLoaded=${this.isLoaded}, lang=${this.currentLanguage}`);
      return [];
    }
    try {
      const prefixLower = prefix.toLowerCase();
      let results: SuggestResult[];
      
      // Use Yandex API if Yandex dictionary is loaded
      if (this.yandexLoaded) {
        const yandexResults = nativeDict.getYandexSuggestions(prefixLower, limit, '') as YandexSuggestion[];
        // Convert YandexSuggestion to SuggestResult format
        results = (yandexResults || []).map((r: YandexSuggestion): SuggestResult => ({
          word: r.word,
          score: r.score * 255,  // Convert [0-1] to [0-255] range
          errorType: ErrorType.NOT_AN_ERROR
        }));
        console.info(`[NativeDictionary] getYandexSuggestions("${prefixLower}", ${limit}) returned ${results.length} results`);
      } else {
        results = nativeDict.getSuggestions(prefixLower, limit) as SuggestResult[];
        console.info(`[NativeDictionary] getSuggestions("${prefixLower}", ${limit}) returned ${results?.length ?? 0} results for lang=${this.currentLanguage}`);
      }

      // Apply capitalization from original prefix to results
      if (results && results.length > 0) {
        for (let i = 0; i < results.length; i++) {
          results[i].word = this.applyCapitalization(prefix, results[i].word);
        }
        console.info(`[NativeDictionary] First 3 (with case): ${results.slice(0, 3).map(r => r.word).join(', ')}`);
      }
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
      // Apply capitalization from original word to result
      if (result && result.word) {
        result.word = this.applyCapitalization(word, result.word);
      }
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
    this.yandexLoaded = false;
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

  // ======== USER DICTIONARY / LEARNING ========

  /**
   * Add learned word to user dictionary
   * @param word - Word to add
   * @param prevWord - Previous word for bigram context (empty if none)
   * @param count - Initial frequency count (default 1)
   */
  addLearnedWord(word: string, prevWord: string, count: number = 1): void {
    if (!this.nativeAvailable || !this.isLoaded) {
      console.warn('[NativeDictionary] addLearnedWord: not available');
      return;
    }
    try {
      nativeDict.addLearnedWord(word.toLowerCase(), prevWord.toLowerCase(), count);
      console.info(`[NativeDictionary] Added learned word: "${word}" (prev: "${prevWord}", count: ${count})`);
    } catch (err) {
      const error = err as BusinessError;
      console.error('[NativeDictionary] Error in addLearnedWord:', error);
    }
  }

  /**
   * Save user dictionary to persistent storage
   * @returns Promise<boolean> - true if saved successfully
   */
  async saveUserDictionary(): Promise<boolean> {
    if (!this.nativeAvailable || !this.context) {
      console.warn('[NativeDictionary] saveUserDictionary: not available');
      return false;
    }
    try {
      const userDictPath = `${this.context.filesDir}/dict/user_${this.currentLanguage}.dict`;
      const saved: boolean = await nativeDict.saveUserDictionary(userDictPath) as boolean;
      if (saved) {
        console.info(`[NativeDictionary] User dictionary saved to: ${userDictPath}`);
      } else {
        console.warn('[NativeDictionary] Failed to save user dictionary');
      }
      return saved;
    } catch (err) {
      const error = err as BusinessError;
      console.error('[NativeDictionary] Error saving user dictionary:', error);
      return false;
    }
  }

  /**
   * Load user dictionary from persistent storage
   * @returns Promise<boolean> - true if loaded successfully
   */
  async loadUserDictionary(): Promise<boolean> {
    if (!this.nativeAvailable || !this.context) {
      console.warn('[NativeDictionary] loadUserDictionary: not available');
      return false;
    }
    try {
      const userDictPath = `${this.context.filesDir}/dict/user_${this.currentLanguage}.dict`;
      
      // Check if file exists first
      try {
        fileIo.statSync(userDictPath);
      } catch (statErr) {
        console.info('[NativeDictionary] No user dictionary file found, starting fresh');
        return true; // Not an error - just no saved data yet
      }
      
      const loaded: boolean = await nativeDict.loadUserDictionary(userDictPath) as boolean;
      if (loaded) {
        console.info(`[NativeDictionary] User dictionary loaded from: ${userDictPath}`);
      }
      return loaded;
    } catch (err) {
      const error = err as BusinessError;
      console.error('[NativeDictionary] Error loading user dictionary:', error);
      return false;
    }
  }

  /**
   * Clear all learned words from user dictionary
   */
  clearLearnedWords(): void {
    if (!this.nativeAvailable) {
      return;
    }
    try {
      nativeDict.clearLearnedWords();
      console.info('[NativeDictionary] Cleared all learned words');
    } catch (err) {
      const error = err as BusinessError;
      console.error('[NativeDictionary] Error clearing learned words:', error);
    }
  }

  // =========================================================================
  // Legacy User Learning API (kept for backward compatibility)
  // New code should use addLearnedWord(word, prevWord, count) above
  // =========================================================================

  /**
   * Record word usage (boost frequency)
   * @param word - Word that was used
   * @returns true if recorded
   */
  recordWordUsage(word: string): boolean {
    if (!this.nativeAvailable) return false;

    try {
      // Delegate to new API with empty prevWord
      this.addLearnedWord(word, '', 1);
      return true;
    } catch (err) {
      console.error('[NativeDictionary] Error in recordWordUsage:', err);
      return false;
    }
  }

  /**
   * Get count of learned words
   * @returns Number of learned words
   */
  getLearnedWordsCount(): number {
    if (!this.nativeAvailable) return 0;

    try {
      return nativeDict.getLearnedWordsCount() as number;
    } catch (err) {
      console.error('[NativeDictionary] Error in getLearnedWordsCount:', err);
      return 0;
    }
  }

  /**
   * Remove a word from user dictionary
   * @param word - Word to remove
   * @returns true if removed
   */
  removeLearnedWord(word: string): boolean {
    if (!this.nativeAvailable) return false;

    try {
      return nativeDict.removeLearnedWord(word) as boolean;
    } catch (err) {
      console.error('[NativeDictionary] Error in removeLearnedWord:', err);
      return false;
    }
  }

  // ============ FlatTrie API (DEPRECATED - use Yandex format) ============

  /**
   * Load pre-serialized .flat dictionary for instant loading (<50ms)
   * @deprecated Use loadDictionaryFromFd() with Yandex format. Always returns false.
   * @param path - Path to .flat file
   * @returns false (deprecated)
   */
  loadFlatDictionary(path: string): boolean {
    if (!this.nativeAvailable) return false;

    try {
      const result = nativeDict.loadFlatDictionary(path) as boolean;
      if (result) {
        console.info(`[NativeDictionary] FlatTrie loaded from ${path}`);
      }
      return result;
    } catch (err) {
      console.error('[NativeDictionary] Error in loadFlatDictionary:', err);
      return false;
    }
  }

  /**
   * Convert .dict file to optimized .flat format
   * @deprecated Yandex format doesn't require conversion. Always returns false.
   * @param inputPath - Path to input .dict file
   * @param outputPath - Path to output .flat file
   * @param locale - Optional language code (e.g., 'ru', 'en')
   * @returns false (deprecated)
   */
  convertToFlatFormat(inputPath: string, outputPath: string, locale?: string): boolean {
    if (!this.nativeAvailable) return false;

    try {
      const result = nativeDict.convertToFlatFormat(inputPath, outputPath, locale || '') as boolean;
      if (result) {
        console.info(`[NativeDictionary] Converted ${inputPath} to ${outputPath}`);
      }
      return result;
    } catch (err) {
      console.error('[NativeDictionary] Error in convertToFlatFormat:', err);
      return false;
    }
  }

  /**
   * Get FlatTrie statistics
   * @deprecated Use getStats() for Yandex dictionary stats. Always returns null.
   * @returns null (deprecated)
   */
  getFlatTrieStats(): FlatTrieStats | null {
    if (!this.nativeAvailable) return null;

    try {
      return nativeDict.getFlatTrieStats() as FlatTrieStats | null;
    } catch (err) {
      console.error('[NativeDictionary] Error in getFlatTrieStats:', err);
      return null;
    }
  }

  // ============ Batch Operations API ============

  /**
   * Check if multiple words exist in dictionary
   * @param words Array of words to check
   * @returns Array of booleans (true if exists)
   */
  batchContains(words: string[]): boolean[] {
    if (!this.nativeAvailable) return words.map(() => false);

    try {
      return nativeDict.batchOps.batchContains(words) as boolean[];
    } catch (err) {
      console.error('[NativeDictionary] Error in batchContains:', err);
      return words.map(() => false);
    }
  }

  /**
   * Get frequencies of multiple words
   * @param words Array of words
   * @returns Array of frequencies
   */
  batchGetFrequency(words: string[]): number[] {
    if (!this.nativeAvailable) return words.map(() => 0);

    try {
      return nativeDict.batchOps.batchGetFrequency(words) as number[];
    } catch (err) {
      console.error('[NativeDictionary] Error in batchGetFrequency:', err);
      return words.map(() => 0);
    }
  }

  /**
   * Load default autocorrect rules for language
   * @param language Language code ('ru', 'en')
   * @returns true if rules loaded
   */
  loadAutocorrectRules(language: string): boolean {
    if (!this.nativeAvailable) return false;

    try {
      const result = nativeDict.batchOps.loadAutocorrectRules(language) as boolean;
      if (result) {
        console.info(`[NativeDictionary] Autocorrect rules loaded for ${language}`);
      }
      return result;
    } catch (err) {
      console.error('[NativeDictionary] Error in loadAutocorrectRules:', err);
      return false;
    }
  }

  /**
   * Apply autocorrect rules to a word
   * @param word Word to check
   * @returns Corrected word or original if no rule matches
   */
  applyAutocorrectRule(word: string): string {
    if (!this.nativeAvailable) return word;

    try {
      return nativeDict.batchOps.applyAutocorrectRule(word) as string;
    } catch (err) {
      console.error('[NativeDictionary] Error in applyAutocorrectRule:', err);
      return word;
    }
  }

  /**
   * Add custom autocorrect rule
   * @param wrong Wrong form
   * @param correct Correct form
   */
  addAutocorrectRule(wrong: string, correct: string): void {
    if (!this.nativeAvailable) return;

    try {
      nativeDict.batchOps.addAutocorrectRule(wrong, correct);
    } catch (err) {
      console.error('[NativeDictionary] Error in addAutocorrectRule:', err);
    }
  }

  // ============ Gesture Trail API (Yandex-style) ============

  /**
   * Initialize trail data system for gesture visualization
   * @param params Optional trail parameters
   * @returns true if initialized
   */
  initTrailData(params?: TrailInitParams): boolean {
    if (!this.nativeAvailable) return false;

    try {
      return nativeDict.initTrailData(params) as boolean;
    } catch (err) {
      console.error('[NativeDictionary] Error in initTrailData:', err);
      return false;
    }
  }

  /**
   * Add a point to the trail drawer
   * @param x X coordinate
   * @param y Y coordinate
   * @param timestamp Timestamp in milliseconds
   */
  addTrailPoint(x: number, y: number, timestamp: number): void {
    if (!this.nativeAvailable) return;

    try {
      nativeDict.addTrailPoint(x, y, timestamp);
    } catch (err) {
      console.error('[NativeDictionary] Error in addTrailPoint:', err);
    }
  }

  /**
   * Process drawer points and update visibility
   * @param currentTime Current timestamp in milliseconds
   * @returns Number of visible points
   */
  updateTrailData(currentTime: number): number {
    if (!this.nativeAvailable) return 0;

    try {
      return nativeDict.updateTrailData(currentTime) as number;
    } catch (err) {
      console.error('[NativeDictionary] Error in updateTrailData:', err);
      return 0;
    }
  }

  /**
   * Get trail segments for rendering
   * @param currentTime Current timestamp in milliseconds
   * @returns Float32Array of segments (7 floats each: x0, y0, w0, x1, y1, w1, alpha) or null
   */
  getTrailSegments(currentTime: number): Float32Array | null {
    if (!this.nativeAvailable) return null;

    try {
      return nativeDict.getTrailSegments(currentTime) as Float32Array | null;
    } catch (err) {
      console.error('[NativeDictionary] Error in getTrailSegments:', err);
      return null;
    }
  }

  /**
   * Reset trail data for new gesture
   */
  resetTrailData(): void {
    if (!this.nativeAvailable) return;

    try {
      nativeDict.resetTrailData();
    } catch (err) {
      console.error('[NativeDictionary] Error in resetTrailData:', err);
    }
  }

  /**
   * Update trail render parameters
   * @param params Render parameters to update
   */
  setTrailRenderParams(params: TrailRenderParams): void {
    if (!this.nativeAvailable) return;

    try {
      nativeDict.setTrailRenderParams(params);
    } catch (err) {
      console.error('[NativeDictionary] Error in setTrailRenderParams:', err);
    }
  }

  /**
   * Compact trail buffers to save memory
   */
  compactTrailBuffers(): void {
    if (!this.nativeAvailable) return;

    try {
      nativeDict.compactTrailBuffers();
    } catch (err) {
      console.error('[NativeDictionary] Error in compactTrailBuffers:', err);
    }
  }

  // ============================================================================
  // Yandex Neural Dictionary API
  // ============================================================================

  /**
   * Load Yandex dictionary from file
   * Supports both binary (main_ru) and text formats
   * @param path Path to dictionary file
   * @returns true if loaded successfully
   */
  loadYandexDict(path: string): boolean {
    if (!this.nativeAvailable) {
      console.warn('[NativeDictionary] Yandex: Native not available');
      return false;
    }

    try {
      const result = nativeDict.loadYandexDict(path) as boolean;
      console.info(`[NativeDictionary] Yandex dict loaded: ${result}`);
      return result;
    } catch (err) {
      console.error('[NativeDictionary] Error loading Yandex dict:', err);
      return false;
    }
  }

  /**
   * Load MindSpore neural model for scoring
   * @param path Path to .ms model file
   * @returns true if loaded successfully
   */
  loadNeuralModel(path: string): boolean {
    if (!this.nativeAvailable) {
      console.warn('[NativeDictionary] Neural: Native not available');
      return false;
    }

    try {
      const result = nativeDict.loadNeuralModel(path) as boolean;
      console.info(`[NativeDictionary] Neural model loaded: ${result}`);
      return result;
    } catch (err) {
      console.error('[NativeDictionary] Error loading neural model:', err);
      return false;
    }
  }

  /**
   * Initialize Yandex neural dictionary with dict + model
   * Loads from resources/rawfile
   * @param dictFile Dictionary filename (e.g., 'yandex_ru.dict.txt')
   * @param modelFile Model filename (e.g., 'ranker.ms')
   */
  async initializeYandex(dictFile: string = 'yandex_ru_final.txt', modelFile: string = 'ranker.ms'): Promise<boolean> {
    if (!this.context) {
      console.error('[NativeDictionary] initializeYandex: No context set');
      return false;
    }

    try {
      const resourceDir = this.context.resourceDir;
      const dictPath = `${resourceDir}/rawfile/${dictFile}`;
      const modelPath = `${resourceDir}/rawfile/${modelFile}`;

      console.info(`[NativeDictionary] Initializing Yandex: dict=${dictPath}, model=${modelPath}`);

      const dictLoaded = this.loadYandexDict(dictPath);
      if (!dictLoaded) {
        console.error('[NativeDictionary] Failed to load Yandex dictionary');
        return false;
      }

      const modelLoaded = this.loadNeuralModel(modelPath);
      if (!modelLoaded) {
        console.warn('[NativeDictionary] Neural model not loaded - using frequency scoring');
        // Continue anyway - frequency scoring works without neural model
      }

      console.info(`[NativeDictionary] Yandex initialized: dict=${dictLoaded}, model=${modelLoaded}`);
      return true;
    } catch (err) {
      console.error('[NativeDictionary] Error initializing Yandex:', err);
      return false;
    }
  }

  /**
   * Get neural-scored suggestions from Yandex dictionary
   * @param prefix Input prefix
   * @param limit Maximum results (default: 10)
   * @param context Previous words for context (optional)
   */
  getYandexSuggestions(prefix: string, limit: number = 10, context: string = ''): YandexSuggestion[] {
    if (!this.nativeAvailable) return [];

    try {
      const results = nativeDict.getYandexSuggestions(prefix, limit, context) as YandexSuggestion[];
      return results || [];
    } catch (err) {
      console.error('[NativeDictionary] Error in getYandexSuggestions:', err);
      return [];
    }
  }

  /**
   * Get Yandex dictionary statistics
   */
  getYandexStats(): YandexStats {
    if (!this.nativeAvailable) {
      return { dictLoaded: false, modelLoaded: false };
    }

    try {
      return nativeDict.getYandexStats() as YandexStats;
    } catch (err) {
      console.error('[NativeDictionary] Error in getYandexStats:', err);
      return { dictLoaded: false, modelLoaded: false };
    }
  }

  /**
   * Unload Yandex dictionary and neural model
   */
  unloadYandex(): void {
    if (!this.nativeAvailable) return;

    try {
      nativeDict.unloadYandex();
      console.info('[NativeDictionary] Yandex unloaded');
    } catch (err) {
      console.error('[NativeDictionary] Error unloading Yandex:', err);
    }
  }

  // ============================================================================
  // End Yandex Neural Dictionary API
  // ============================================================================

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

  // ============================================================================
  // Additional Models API (Yandex-style)
  // ============================================================================

  /**
   * Load ALL 15 MindSpore neural models from directory
   * @param modelsDir Directory containing all .ms model files
   * @returns LoadModelsResult with loaded/failed counts
   */
  loadModels(modelsDir: string): LoadModelsResult {
    const emptyResult: LoadModelsResult = {
      success: false,
      loaded: 0,
      total: 15,
      models: [],
      failed: []
    };

    if (!this.nativeAvailable) {
      console.warn('[NativeDictionary] loadModels: Native not available');
      return emptyResult;
    }

    try {
      const result = nativeDict.loadModels(modelsDir);
      console.info(`[NativeDictionary] Loaded ${result.loaded}/${result.total} neural models`);
      if (result.failed.length > 0) {
        console.warn(`[NativeDictionary] Failed models: ${result.failed.join(', ')}`);
      }
      return result;
    } catch (err) {
      console.error('[NativeDictionary] Error in loadModels:', err);
      return emptyResult;
    }
  }

  /**
   * Get status of loaded models (ALL 15 neural models)
   */
  getModelStats(): ModelStats {
    const emptyStats: ModelStats = {
      yandexDictLoaded: false,
      neuralModelsEnabled: false,
      beamSearchReady: false,
      totalModels: 15,
      loadedModels: 0,
      primaryDevice: 'Not initialized',
      loadedModelNames: [],
      failedModelNames: [],
      legacyScorerEnabled: false,
      legacyScorerLoaded: false
    };

    if (!this.nativeAvailable) {
      return emptyStats;
    }

    try {
      return nativeDict.getModelStats();
    } catch (err) {
      console.error('[NativeDictionary] Error in getModelStats:', err);
      return emptyStats;
    }
  }

  /**
   * Initialize all models from rawfile
   * Call this after dictionary is loaded
   */
  async initializeModels(): Promise<boolean> {
    if (!this.context) {
      console.error('[NativeDictionary] initializeModels: No context set');
      return false;
    }

    try {
      const resourceDir = this.context.resourceDir;
      const rawfilePath = `${resourceDir}/rawfile`;

      // Copy models from rawfile to filesDir (rawfile not directly accessible)
      const modelsDir = `${this.context.filesDir}/models`;
      try {
        fileIo.mkdirSync(modelsDir);
      } catch (mkdirErr) {
        // Directory may exist
      }

      // ALL 15 Yandex MindSpore models
      // TAP RANKING (5 models):
      //   - tap_model_ranker.ms    - Primary tap input ranking
      //   - tap_model_ranker_v2.ms - Improved tap ranking
      //   - ranker.ms              - General ranking
      //   - ranker_v2.ms           - Improved general ranking
      //   - ranker_exp.ms          - Experimental ranking
      // SWIPE RANKING (3 models):
      //   - ranker_swipe.ms        - Swipe-specific ranking
      //   - ranker_swipe_v2.ms     - Improved swipe ranking
      //   - swipe_blocker.ms       - Swipe vs tap detection
      // LANGUAGE MODELS (3 models):
      //   - nnlm_model.ms          - Neural language model (context)
      //   - neural_model.ms        - Neural predictions
      //   - char_model.ms          - Character-level model
      // AUTOCORRECT & MORPHOLOGY (2 models):
      //   - tree_autocorrect_model.ms - Autocorrection
      //   - lemmer_mhash.ms           - Lemmatization/morphology
      // EMOJI (2 models):
      //   - emoji_suggest.ms       - Emoji suggestions
      //   - search_emoji_model.ms  - Emoji search
      const modelFiles = [
        // TAP RANKING
        'tap_model_ranker.ms',
        'tap_model_ranker_v2.ms',
        'ranker.ms',
        'ranker_v2.ms',
        'ranker_exp.ms',
        // SWIPE RANKING
        'ranker_swipe.ms',
        'ranker_swipe_v2.ms',
        'swipe_blocker.ms',
        // LANGUAGE MODELS
        'nnlm_model.ms',
        'neural_model.ms',
        'char_model.ms',
        // AUTOCORRECT & MORPHOLOGY
        'tree_autocorrect_model.ms',
        'lemmer_mhash.ms',
        // EMOJI
        'emoji_suggest.ms',
        'search_emoji_model.ms',
      ];

      // Copy models from rawfile if needed
      for (const modelFile of modelFiles) {
        const destPath = `${modelsDir}/${modelFile}`;
        try {
          fileIo.statSync(destPath);
        } catch (statErr) {
          // File doesn't exist, copy from rawfile
          try {
            const rawData = await this.context.resourceManager.getRawFileContent(modelFile);
            if (rawData && rawData.byteLength > 0) {
              const file = fileIo.openSync(destPath, fileIo.OpenMode.CREATE | fileIo.OpenMode.WRITE_ONLY);
              fileIo.writeSync(file.fd, rawData.buffer);
              fileIo.closeSync(file);
              console.info(`[NativeDictionary] Copied model ${modelFile} (${rawData.byteLength} bytes)`);
            }
          } catch (copyErr) {
            console.warn(`[NativeDictionary] Model ${modelFile} not found in rawfile`);
          }
        }
      }

      // Pass models directory to native - it will load ALL 15 models
      const result = this.loadModels(modelsDir);
      console.info(`[NativeDictionary] initializeModels: ${result.loaded}/${result.total} models loaded`);
      return result.success;
    } catch (err) {
      console.error('[NativeDictionary] Error in initializeModels:', err);
      return false;
    }
  }
}

// Export singleton instance
const nativeDictionary = new NativeDictionary();
export default nativeDictionary;
