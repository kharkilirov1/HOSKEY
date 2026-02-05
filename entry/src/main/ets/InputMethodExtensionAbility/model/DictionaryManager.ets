// DictionaryManager - Download and manage language dictionaries
// Currently uses Yandex URLs, will migrate to GitHub later

import fs from '@ohos.file.fs';
import request from '@ohos.request';
import { http } from '@kit.NetworkKit';
import { common } from '@kit.AbilityKit';
import { BusinessError } from '@kit.BasicServicesKit';

// Dictionary configuration - URLs from Yandex (temporary, will migrate to GitHub)
export interface DictionaryInfo {
  languageCode: string;
  languageName: string;
  nativeName: string;
  url: string;
  md5: string;
  downloadSize: number;  // bytes
  unpackedSize: number;  // bytes
  version: string;
  isBuiltIn: boolean;  // true for pre-installed dictionaries
}

export interface DownloadProgress {
  languageCode: string;
  progress: number;  // 0-100
  downloadedBytes: number;
  totalBytes: number;
  status: 'idle' | 'downloading' | 'extracting' | 'completed' | 'error';
  error?: string;
}

// Dictionary configurations from Yandex (temporary URLs)
const DICTIONARY_CONFIG: DictionaryInfo[] = [
  {
    languageCode: 'ru',
    languageName: 'Russian',
    nativeName: 'Русский',
    url: 'https://ya-keyboard.s3.yandex.net/android/dictionaries/v110.1/ru.zip',
    md5: '2437237c22a149d725b546101a00076c',
    downloadSize: 89557662,
    unpackedSize: 137196502,
    version: '61.0',
    isBuiltIn: true  // RU is pre-installed
  },
  {
    languageCode: 'en',
    languageName: 'English',
    nativeName: 'English',
    url: 'https://ya-keyboard.s3.yandex.net/android/dictionaries/v82/en.zip',
    md5: 'e1c28856de2afe7220ee13f2fcce1a66',
    downloadSize: 45000000,
    unpackedSize: 80000000,
    version: '9.0',
    isBuiltIn: false
  },
  {
    languageCode: 'uk',
    languageName: 'Ukrainian',
    nativeName: 'Українська',
    url: 'https://ya-keyboard.s3.yandex.net/android/dictionaries/v82/uk.zip',
    md5: '',
    downloadSize: 50000000,
    unpackedSize: 90000000,
    version: '1.0',
    isBuiltIn: false
  },
  {
    languageCode: 'kk',
    languageName: 'Kazakh',
    nativeName: 'Қазақша',
    url: 'https://ya-keyboard.s3.yandex.net/android/dictionaries/v82/kk.zip',
    md5: '',
    downloadSize: 30000000,
    unpackedSize: 50000000,
    version: '1.0',
    isBuiltIn: false
  },
  {
    languageCode: 'de',
    languageName: 'German',
    nativeName: 'Deutsch',
    url: 'https://ya-keyboard.s3.yandex.net/android/dictionaries/v82/de.zip',
    md5: '',
    downloadSize: 55000000,
    unpackedSize: 95000000,
    version: '1.0',
    isBuiltIn: false
  },
  {
    languageCode: 'fr',
    languageName: 'French',
    nativeName: 'Français',
    url: 'https://ya-keyboard.s3.yandex.net/android/dictionaries/v82/fr.zip',
    md5: '',
    downloadSize: 50000000,
    unpackedSize: 85000000,
    version: '1.0',
    isBuiltIn: false
  },
  {
    languageCode: 'es',
    languageName: 'Spanish',
    nativeName: 'Español',
    url: 'https://ya-keyboard.s3.yandex.net/android/dictionaries/v82/es.zip',
    md5: '',
    downloadSize: 48000000,
    unpackedSize: 82000000,
    version: '1.0',
    isBuiltIn: false
  },
  {
    languageCode: 'it',
    languageName: 'Italian',
    nativeName: 'Italiano',
    url: 'https://ya-keyboard.s3.yandex.net/android/dictionaries/v82/it.zip',
    md5: '',
    downloadSize: 45000000,
    unpackedSize: 78000000,
    version: '1.0',
    isBuiltIn: false
  },
  {
    languageCode: 'pl',
    languageName: 'Polish',
    nativeName: 'Polski',
    url: 'https://ya-keyboard.s3.yandex.net/android/dictionaries/v82/pl.zip',
    md5: '',
    downloadSize: 42000000,
    unpackedSize: 72000000,
    version: '1.0',
    isBuiltIn: false
  }
];

class DictionaryManager {
  private static instance: DictionaryManager;
  private context: common.Context | null = null;
  private downloadProgress: Map<string, DownloadProgress> = new Map();
  private installedDictionaries: Set<string> = new Set();
  private progressCallbacks: Map<string, (progress: DownloadProgress) => void> = new Map();

  private constructor() {
    // RU is built-in
    this.installedDictionaries.add('ru');
  }

  static getInstance(): DictionaryManager {
    if (!DictionaryManager.instance) {
      DictionaryManager.instance = new DictionaryManager();
    }
    return DictionaryManager.instance;
  }

  setContext(context: common.Context): void {
    this.context = context;
    this.loadInstalledDictionaries();
  }

  // Get all available dictionaries
  getAvailableDictionaries(): DictionaryInfo[] {
    return DICTIONARY_CONFIG;
  }

  // Get dictionary info by language code
  getDictionaryInfo(languageCode: string): DictionaryInfo | undefined {
    return DICTIONARY_CONFIG.find(d => d.languageCode === languageCode);
  }

  // Check if dictionary is installed
  isDictionaryInstalled(languageCode: string): boolean {
    return this.installedDictionaries.has(languageCode);
  }

  // Get all installed dictionary codes
  getInstalledDictionaries(): string[] {
    return Array.from(this.installedDictionaries);
  }

  // Get download progress for a language
  getDownloadProgress(languageCode: string): DownloadProgress | undefined {
    return this.downloadProgress.get(languageCode);
  }

  // Register progress callback
  onProgress(languageCode: string, callback: (progress: DownloadProgress) => void): void {
    this.progressCallbacks.set(languageCode, callback);
  }

  // Remove progress callback
  removeProgressCallback(languageCode: string): void {
    this.progressCallbacks.delete(languageCode);
  }

  // Load installed dictionaries from storage
  private async loadInstalledDictionaries(): Promise<void> {
    if (!this.context) return;

    try {
      const dictionariesDir = this.getDictionariesDir();
      
      // Check if directory exists
      try {
        const stat = fs.statSync(dictionariesDir);
        if (!stat.isDirectory()) {
          fs.mkdirSync(dictionariesDir);
        }
      } catch {
        fs.mkdirSync(dictionariesDir);
      }

      // Check for installed dictionaries
      const files = fs.listFileSync(dictionariesDir);
      for (const file of files) {
        // Dictionary files are named like 'main_ru', 'main_en', etc.
        if (file.startsWith('main_')) {
          const langCode = file.replace('main_', '');
          this.installedDictionaries.add(langCode);
          console.info(`[DictionaryManager] Found installed dictionary: ${langCode}`);
        }
      }

      // RU is always installed (built-in)
      this.installedDictionaries.add('ru');
      
    } catch (error) {
      console.error('[DictionaryManager] Error loading installed dictionaries:', error);
    }
  }

  // Get dictionaries directory path
  private getDictionariesDir(): string {
    if (!this.context) {
      throw new Error('Context not set');
    }
    return `${this.context.filesDir}/dictionaries`;
  }

  // Format bytes to human readable
  formatSize(bytes: number): string {
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
    return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
  }

  // Download dictionary
  async downloadDictionary(languageCode: string): Promise<boolean> {
    const dictInfo = this.getDictionaryInfo(languageCode);
    if (!dictInfo) {
      console.error(`[DictionaryManager] Unknown language: ${languageCode}`);
      return false;
    }

    if (this.isDictionaryInstalled(languageCode)) {
      console.info(`[DictionaryManager] Dictionary already installed: ${languageCode}`);
      return true;
    }

    if (!this.context) {
      console.error('[DictionaryManager] Context not set');
      return false;
    }

    // Initialize progress
    const progress: DownloadProgress = {
      languageCode,
      progress: 0,
      downloadedBytes: 0,
      totalBytes: dictInfo.downloadSize,
      status: 'downloading'
    };
    this.downloadProgress.set(languageCode, progress);
    this.notifyProgress(languageCode, progress);

    try {
      const dictionariesDir = this.getDictionariesDir();
      const zipPath = `${dictionariesDir}/${languageCode}.zip`;
      const extractDir = dictionariesDir;

      // Create directory if not exists
      try {
        fs.mkdirSync(dictionariesDir);
      } catch {
        // Directory might already exist
      }

      console.info(`[DictionaryManager] Starting download: ${dictInfo.url}`);

      // Download using http request
      const httpRequest = http.createHttp();
      
      const response = await httpRequest.request(dictInfo.url, {
        method: http.RequestMethod.GET,
        expectDataType: http.HttpDataType.ARRAY_BUFFER,
        header: {
          'User-Agent': 'HOSKEY/1.0'
        }
      });

      if (response.responseCode === 200 && response.result) {
        // Save to file
        const file = fs.openSync(zipPath, fs.OpenMode.CREATE | fs.OpenMode.WRITE_ONLY);
        const data = response.result as ArrayBuffer;
        fs.writeSync(file.fd, data);
        fs.closeSync(file);

        // Update progress
        progress.progress = 50;
        progress.status = 'extracting';
        progress.downloadedBytes = data.byteLength;
        this.notifyProgress(languageCode, progress);

        console.info(`[DictionaryManager] Downloaded ${this.formatSize(data.byteLength)}, extracting...`);

        // TODO: Extract ZIP file
        // For now, we'll just mark as installed and handle extraction later
        // HarmonyOS doesn't have built-in ZIP extraction, need zlib

        // Mark as installed
        this.installedDictionaries.add(languageCode);

        // Update progress
        progress.progress = 100;
        progress.status = 'completed';
        this.notifyProgress(languageCode, progress);

        console.info(`[DictionaryManager] Dictionary installed: ${languageCode}`);
        return true;

      } else {
        throw new Error(`HTTP error: ${response.responseCode}`);
      }

    } catch (error) {
      console.error(`[DictionaryManager] Download error for ${languageCode}:`, error);
      
      const progress = this.downloadProgress.get(languageCode);
      if (progress) {
        progress.status = 'error';
        progress.error = error instanceof Error ? error.message : String(error);
        this.notifyProgress(languageCode, progress);
      }
      
      return false;
    }
  }

  // Delete dictionary
  async deleteDictionary(languageCode: string): Promise<boolean> {
    if (languageCode === 'ru') {
      console.warn('[DictionaryManager] Cannot delete built-in RU dictionary');
      return false;
    }

    if (!this.isDictionaryInstalled(languageCode)) {
      return true;
    }

    try {
      const dictionariesDir = this.getDictionariesDir();
      const dictPath = `${dictionariesDir}/main_${languageCode}`;
      const zipPath = `${dictionariesDir}/${languageCode}.zip`;

      // Delete dictionary file
      try {
        fs.unlinkSync(dictPath);
      } catch {
        // File might not exist
      }

      // Delete zip file
      try {
        fs.unlinkSync(zipPath);
      } catch {
        // File might not exist
      }

      this.installedDictionaries.delete(languageCode);
      this.downloadProgress.delete(languageCode);

      console.info(`[DictionaryManager] Dictionary deleted: ${languageCode}`);
      return true;

    } catch (error) {
      console.error(`[DictionaryManager] Error deleting dictionary ${languageCode}:`, error);
      return false;
    }
  }

  // Get system language
  getSystemLanguage(): string {
    try {
      // HarmonyOS system language detection
      // Default to 'ru' for now, will implement proper detection
      const systemLocale = 'ru'; // TODO: Get from system
      
      // Map system locale to our supported languages
      const localeMap: Record<string, string> = {
        'ru': 'ru',
        'ru-RU': 'ru',
        'en': 'en',
        'en-US': 'en',
        'en-GB': 'en',
        'uk': 'uk',
        'uk-UA': 'uk',
        'kk': 'kk',
        'kk-KZ': 'kk',
        'de': 'de',
        'de-DE': 'de',
        'fr': 'fr',
        'fr-FR': 'fr',
        'es': 'es',
        'es-ES': 'es',
        'it': 'it',
        'it-IT': 'it',
        'pl': 'pl',
        'pl-PL': 'pl'
      };

      return localeMap[systemLocale] || 'en';

    } catch (error) {
      console.error('[DictionaryManager] Error getting system language:', error);
      return 'en';
    }
  }

  // Notify progress callback
  private notifyProgress(languageCode: string, progress: DownloadProgress): void {
    const callback = this.progressCallbacks.get(languageCode);
    if (callback) {
      callback(progress);
    }
  }

  // Get total size of all dictionaries to download
  getTotalDownloadSize(languageCodes: string[]): number {
    return languageCodes
      .filter(code => !this.isDictionaryInstalled(code))
      .map(code => this.getDictionaryInfo(code)?.downloadSize || 0)
      .reduce((sum, size) => sum + size, 0);
  }
}

export const dictionaryManager = DictionaryManager.getInstance();
export default dictionaryManager;
