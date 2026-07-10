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

export interface NativeLoadReport {
  assetKey: string;
  format: string;
  byteSize: number;
  notes: string[];
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

declare const hoskeyBridge: {
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
};

export default hoskeyBridge;
