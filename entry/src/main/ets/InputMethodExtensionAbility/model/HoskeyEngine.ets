import nativeModule from 'libhoskey_bridge.so';

export interface NativeSuggestion {
  text: string;
  typeFlags: number;
  groupId: number;
  tag: string;
  score: number;
}

export interface NativeSuggestRequest {
  token: string;
  previousTokens?: string[];
  locale?: string;
  wantAutocorrect?: boolean;
  maxSuggestions?: number;
  input?: string;
  fullContext?: string;
  optionsArray?: string[];
  emojiFactor?: number;
  packageName?: string;
  isUrgent?: boolean;
  fieldActionId?: number;
  gender?: number;
  age?: number;
  prevWordsInfo?: NativePrevWordInfo[];
  dictionarySessions?: NativeDictionarySession[];
  proximityInfo?: NativeProximityInfo;
  points?: NativeTouchPoint[];
}

export interface NativePrevWordInfo {
  token: string;
  wasAutocorrected?: boolean;
}

export interface NativeDictionarySession {
  id: string;
  locale?: string;
}

export interface NativeProximityInfo {
  layout?: string;
  keyboardWidth?: number;
  keyboardHeight?: number;
  enabled?: boolean;
}

export interface NativeTouchPoint {
  x: number;
  y: number;
  timeMs?: number;
  keyCode?: number;
}

export type NativePredictorOverlayMode = 'disabled' | 'all' | 'split';

export interface NativeOpenDebugOptions {
  predictorOverlayMode?: NativePredictorOverlayMode;
  predictorSplitAllowlist?: boolean;
}

export interface NativeSuggestResponse {
  ok: boolean;
  statusCode: number;
  statusMessage: string;
  suggestions: NativeSuggestion[];
  willAutocorrect: boolean;
  isBlocked: boolean;
  diagnostics: string;
}

export interface NativeCheckWordRequest {
  token: string;
  locale?: string;
}

export interface NativeCheckWordResponse {
  ok: boolean;
  statusCode: number;
  statusMessage: string;
  isKnown: boolean;
  isBlocked: boolean;
  normalizedForm: string;
  diagnostics: string;
}

export interface NativePredictorModeCompareResponse {
  ok: boolean;
  statusCode: number;
  statusMessage: string;
  changed: boolean;
  diagnostics: string;
  baseline: NativeSuggestResponse;
  verifiedSplit: NativeSuggestResponse;
}

export interface NativeLoadReport {
  assetKey: string;
  format: string;
  byteSize: number;
  notes: string[];
}

interface HoskeyBridgeApi {
  open(dictDir: string, options?: NativeOpenDebugOptions): boolean;
  openAsync(dictDir: string, options?: NativeOpenDebugOptions): Promise<boolean>;
  getSuggestions(request: NativeSuggestRequest): NativeSuggestResponse;
  comparePredictorModes(request: NativeSuggestRequest): NativePredictorModeCompareResponse;
  checkWord(request: NativeCheckWordRequest): NativeCheckWordResponse;
  close(): void;
  getDiagnostics(): string;
  getLoadedAssetKeys(): string[];
  getLoadReports(): NativeLoadReport[];
  getDecoderDiagnostics(): string;
}

const hoskeyBridge: HoskeyBridgeApi = nativeModule as HoskeyBridgeApi;

let nativeAvailable = false;

try {
  if (nativeModule) {
    nativeAvailable = true;
    console.info('[HoskeyEngine] Native module loaded');
  }
} catch (e) {
  console.warn('[HoskeyEngine] Native module not available');
}

export class HoskeyEngine {
  private initialized: boolean = false;
  private opening: boolean = false;

  static isAvailable(): boolean {
    return nativeAvailable;
  }

  open(dictDir: string, options?: NativeOpenDebugOptions): boolean {
    if (!nativeAvailable) {
      this.initialized = false;
      return false;
    }

    try {
      const result = hoskeyBridge.open(dictDir, options ?? {});
      this.initialized = !!result;
      return this.initialized;
    } catch (e) {
      console.error('[HoskeyEngine] open failed: ' + e);
      this.initialized = false;
      return false;
    }
  }

  async openAsync(dictDir: string, options?: NativeOpenDebugOptions): Promise<boolean> {
    if (!nativeAvailable) {
      this.opening = false;
      this.initialized = false;
      return false;
    }

    this.opening = true;
    this.initialized = false;
    try {
      const result = await hoskeyBridge.openAsync(dictDir, options ?? {});
      this.initialized = !!result;
      return this.initialized;
    } catch (e) {
      console.error('[HoskeyEngine] openAsync failed: ' + e);
      this.initialized = false;
      return false;
    } finally {
      this.opening = false;
    }
  }

  openWithVerifiedSplitOverlay(dictDir: string): boolean {
    return this.open(dictDir, {
      predictorOverlayMode: 'split',
      predictorSplitAllowlist: true
    });
  }

  openWithVerifiedSplitOverlayAsync(dictDir: string): Promise<boolean> {
    return this.openAsync(dictDir, {
      predictorOverlayMode: 'split',
      predictorSplitAllowlist: true
    });
  }

  isInitialized(): boolean {
    return this.initialized;
  }

  isOpening(): boolean {
    return this.opening;
  }

  getSuggestionsByRequest(request: NativeSuggestRequest): NativeSuggestResponse {
    if (!this.initialized || (!request.token && !request.input)) {
      return {
        ok: false, statusCode: 0, statusMessage: 'Not initialized',
        suggestions: [], willAutocorrect: false, isBlocked: false, diagnostics: ''
      };
    }

    try {
      return hoskeyBridge.getSuggestions(request);
    } catch (e) {
      console.error('[HoskeyEngine] getSuggestions failed: ' + e);
      return {
        ok: false, statusCode: 0, statusMessage: '' + e,
        suggestions: [], willAutocorrect: false, isBlocked: false, diagnostics: ''
      };
    }
  }

  getSuggestionsResponse(word: string, prevWord: string = '', locale: string = 'ru-RU'): NativeSuggestResponse {
    const request: NativeSuggestRequest = {
      token: word,
      input: word,
      previousTokens: prevWord.length > 0 ? [prevWord] : [],
      prevWordsInfo: prevWord.length > 0 ? [{ token: prevWord, wasAutocorrected: false }] : [],
      locale,
      wantAutocorrect: true,
      maxSuggestions: 6,
      dictionarySessions: [{ id: 'hoskey-default', locale }],
      packageName: 'com.opentech.openboard'
    };
    return this.getSuggestionsByRequest(request);
  }

  getSuggestions(word: string, prevWord: string = '', locale: string = 'ru-RU'): string[] {
    return this.getSuggestionsResponse(word, prevWord, locale).suggestions.map((suggestion: NativeSuggestion): string => suggestion.text);
  }

  checkWord(word: string, locale: string = 'ru-RU'): NativeCheckWordResponse {
    if (!this.initialized || !word) {
      return {
        ok: false, statusCode: 0, statusMessage: 'Not initialized',
        isKnown: false, isBlocked: false, normalizedForm: word, diagnostics: ''
      };
    }

    try {
      return hoskeyBridge.checkWord({ token: word, locale } as NativeCheckWordRequest);
    } catch (e) {
      console.error('[HoskeyEngine] checkWord failed: ' + e);
      return {
        ok: false, statusCode: 0, statusMessage: '' + e,
        isKnown: false, isBlocked: false, normalizedForm: word, diagnostics: ''
      };
    }
  }

  comparePredictorModesByRequest(request: NativeSuggestRequest): NativePredictorModeCompareResponse {
    if (!this.initialized || (!request.token && !request.input)) {
      return {
        ok: false, statusCode: 0, statusMessage: 'Not initialized',
        changed: false, diagnostics: '',
        baseline: {
          ok: false, statusCode: 0, statusMessage: 'Not initialized',
          suggestions: [], willAutocorrect: false, isBlocked: false, diagnostics: ''
        },
        verifiedSplit: {
          ok: false, statusCode: 0, statusMessage: 'Not initialized',
          suggestions: [], willAutocorrect: false, isBlocked: false, diagnostics: ''
        }
      };
    }

    try {
      return hoskeyBridge.comparePredictorModes(request);
    } catch (e) {
      console.error('[HoskeyEngine] comparePredictorModes failed: ' + e);
      return {
        ok: false, statusCode: 0, statusMessage: '' + e,
        changed: false, diagnostics: '',
        baseline: {
          ok: false, statusCode: 0, statusMessage: '' + e,
          suggestions: [], willAutocorrect: false, isBlocked: false, diagnostics: ''
        },
        verifiedSplit: {
          ok: false, statusCode: 0, statusMessage: '' + e,
          suggestions: [], willAutocorrect: false, isBlocked: false, diagnostics: ''
        }
      };
    }
  }

  comparePredictorModes(word: string, prevWord: string = '', locale: string = 'ru-RU'): NativePredictorModeCompareResponse {
    const request: NativeSuggestRequest = {
      token: word,
      input: word,
      previousTokens: prevWord.length > 0 ? [prevWord] : [],
      prevWordsInfo: prevWord.length > 0 ? [{ token: prevWord, wasAutocorrected: false }] : [],
      locale,
      wantAutocorrect: true,
      maxSuggestions: 6,
      dictionarySessions: [{ id: 'hoskey-default', locale }],
      packageName: 'com.opentech.openboard'
    };
    return this.comparePredictorModesByRequest(request);
  }

  getDiagnostics(): string {
    if (!nativeAvailable) return 'Native module unavailable';
    try {
      return hoskeyBridge.getDiagnostics();
    } catch (e) {
      return 'getDiagnostics failed: ' + e;
    }
  }

  getLoadedAssetKeys(): string[] {
    if (!nativeAvailable || !this.initialized) return [];
    try {
      return hoskeyBridge.getLoadedAssetKeys();
    } catch (e) {
      return [];
    }
  }

  getLoadReports(): NativeLoadReport[] {
    if (!nativeAvailable || !this.initialized) return [];
    try {
      return hoskeyBridge.getLoadReports();
    } catch (e) {
      return [];
    }
  }

  close(): void {
    if (this.initialized) {
      try { hoskeyBridge.close(); } catch (e) {}
      this.initialized = false;
    }
  }
}
