import { display } from '@kit.ArkUI';
import { inputMethodEngine, InputMethodExtensionContext } from '@kit.IMEKit';
import { BusinessError } from '@kit.BasicServicesKit';
import { KeyAction, KeyData } from './KeyboardKeyData';
import {
  HoskeyEngine,
  NativeDictionarySession,
  NativeOpenDebugOptions,
  NativePrevWordInfo,
  NativeProximityInfo,
  NativeSuggestion,
  NativeSuggestRequest,
  NativeSuggestResponse,
  NativeTouchPoint
} from './HoskeyEngine';
import { DictInstaller } from './DictInstaller';
import diagLog from './DiagLog';
import { OpenBoardTheme } from './Theme';

const inputMethodAbility: inputMethodEngine.InputMethodAbility = inputMethodEngine.getInputMethodAbility();
const keyboardDelegate: inputMethodEngine.KeyboardDelegate = inputMethodEngine.getKeyboardDelegate();
const PANEL_HEIGHT_RATE_PORTRAIT: number = 0.36;
const PANEL_HEIGHT_RATE_PORTRAIT_COMPACT: number = 0.39;
const PANEL_HEIGHT_RATE_LANDSCAPE: number = 0.52;
const PANEL_MIN_HEIGHT_PX: number = 260;
const PANEL_MAX_HEIGHT_PX: number = 900;
const PANEL_SAFE_BOTTOM_INSET_STORAGE_KEY: string = 'hoskeySystemPanelBottomInsetPx';
const PANEL_WIDTH_STORAGE_KEY: string = 'hoskeyPanelWidthPx';
const PANEL_HEIGHT_STORAGE_KEY: string = 'hoskeyPanelHeightPx';
const PANEL_DISPLAY_ID_STORAGE_KEY: string = 'hoskeyPanelDisplayId';

export class KeyboardController {
  private mContext: InputMethodExtensionContext | undefined = undefined;
  private panel: inputMethodEngine.Panel | undefined = undefined;
  private textInputClient: inputMethodEngine.InputClient | undefined = undefined;
  private currentEnterKeyType: number = inputMethodEngine.ENTER_KEY_TYPE_UNSPECIFIED;
  private enterKeyTypeCallback?: (enterKeyType: number) => void;
  private textBufferCache: string = '';
  private cursorPosition: number = 0;
  private nativeEngine: HoskeyEngine = new HoskeyEngine();
  private installedDictDir: string = '';
  private nativeOpenOptions: NativeOpenDebugOptions = {};
  private nativeTouchPoints: NativeTouchPoint[] = [];
  private nativeLayoutDescriptor: string = 'unknown';
  private panelWidthPx: number = 0;
  private panelHeightPx: number = 0;

  private inputStartCallback = async (_: inputMethodEngine.KeyboardController, textInputClient: inputMethodEngine.InputClient): Promise<void> => {
    this.textInputClient = textInputClient;
    this.updateEnterKeyType();
    await this.syncInitialBuffer();
    this.hideIfDestroyedAndRecreate();
  };

  private editorAttributeChangedCallback = (attr: inputMethodEngine.EditorAttribute): void => {
    this.currentEnterKeyType = attr.enterKeyType;
    if (this.enterKeyTypeCallback) {
      this.enterKeyTypeCallback(this.currentEnterKeyType);
    }
    this.applyEditorImmersiveMode(attr.immersiveMode);
    if (this.panel) {
      void this.applyRecommendedPanelMetrics();
      void this.applySystemPanelButtonColors();
    }
    // The editor/field may have changed; re-ground the text cache so the
    // suggestion context can't silently drift from the real editor.
    void this.syncInitialBuffer();
  };

  constructor() {}

  public onCreate(context: InputMethodExtensionContext): void {
    console.info('OpenBoard Keyboard: KeyboardController onCreate called');
    this.mContext = context;
    this.initWindow();
    this.registerListener();

    // Диагностика + инициализация нативного hosdict engine
    diagLog.init(context);
    diagLog.add('KeyboardController.onCreate');
    diagLog.add('Native module available: ' + HoskeyEngine.isAvailable());

    if (HoskeyEngine.isAvailable()) {
      diagLog.add('Starting DictInstaller...');
      DictInstaller.install(context).then((dictDir: string) => {
        this.installedDictDir = dictDir;
        diagLog.add('Dict installed to: ' + dictDir);
        void this.openNativeEngineAsync(dictDir);
      }).catch((err: Error) => {
        diagLog.add('ERROR: Dict install failed: ' + err.message);
      });
    } else {
      diagLog.add('Skipping native engine (not ARM64 or module missing)');
    }
  }

  public getNativeSuggestions(word: string, prevWord: string = '', locale: string = 'ru-RU'): string[] {
    return this.getNativeSuggestResponse(word, prevWord, locale).suggestions
      .map((suggestion: NativeSuggestion): string => suggestion.text);
  }

  public getNativeSuggestResponse(word: string, prevWord: string = '', locale: string = 'ru-RU'): NativeSuggestResponse {
    return this.nativeEngine.getSuggestionsByRequest(this.buildNativeSuggestRequest(word, prevWord, locale));
  }

  public isNativeEngineAvailable(): boolean {
    return this.nativeEngine.isInitialized();
  }

  public getNativePredictorDebugModeLabel(): string {
    if (this.nativeOpenOptions.predictorOverlayMode === 'split' &&
      this.nativeOpenOptions.predictorSplitAllowlist === true) {
      return 'verified-split';
    }
    if (this.nativeOpenOptions.predictorOverlayMode === 'all') {
      return 'overlay-all';
    }
    if (this.nativeOpenOptions.predictorOverlayMode === 'split') {
      return 'split';
    }
    return 'baseline';
  }

  public toggleNativeVerifiedSplitMode(): boolean {
    const useVerifiedSplit = this.getNativePredictorDebugModeLabel() !== 'verified-split';
    return this.setNativePredictorDebugOptions(
      useVerifiedSplit ? {
        predictorOverlayMode: 'split',
        predictorSplitAllowlist: true
      } : {
        predictorOverlayMode: 'disabled',
        predictorSplitAllowlist: false
      }
    );
  }

  public setNativePredictorDebugOptions(options: NativeOpenDebugOptions): boolean {
    this.nativeOpenOptions = options;
    if (!this.installedDictDir || !HoskeyEngine.isAvailable()) {
      diagLog.add('Native predictor mode staged: ' + this.getNativePredictorDebugModeLabel());
      return false;
    }
    void this.openNativeEngineAsync(this.installedDictDir);
    return true;
  }

  public onDestroy(): void {
    this.nativeEngine.close();
    this.installedDictDir = '';
    this.nativeOpenOptions = {};
    this.unregisterListener();
    this.textInputClient = undefined;
    this.currentEnterKeyType = inputMethodEngine.ENTER_KEY_TYPE_UNSPECIFIED;
    this.textBufferCache = '';
    this.cursorPosition = 0;

    if (this.panel) {
      try {
        inputMethodAbility.destroyPanel(this.panel);
      } catch (err) {
        const error = err as BusinessError;
        console.error(`OpenBoard Keyboard: Failed to destroy panel. Code: ${error.code}, Msg: ${error.message}`);
      }
      this.panel = undefined;
    }
    if (this.mContext) {
      try {
        this.mContext.destroy();
      } catch (err) {
        const error = err as BusinessError;
        console.error(`OpenBoard Keyboard: Failed to destroy context. Code: ${error.code}, Msg: ${error.message}`);
      }
      this.mContext = undefined;
    }
  }

  public hideKeyboard(): void {
    if (this.panel) {
      try {
        inputMethodAbility.destroyPanel(this.panel);
      } catch (err) {
        const error = err as BusinessError;
        console.error(`OpenBoard Keyboard: Failed to hide panel. Code: ${error.code}, Msg: ${error.message}`);
      }
      this.panel = undefined;
    }
    this.textInputClient = undefined;
    this.currentEnterKeyType = inputMethodEngine.ENTER_KEY_TYPE_UNSPECIFIED;
  }

  public insertText(text: string): void {
    if (this.textInputClient) {
      try {
        this.textInputClient.insertText(text);
        this.updateCacheOnInsert(text);
      } catch (err) {
        const error = err as BusinessError;
        console.error(`OpenBoard Keyboard: Failed to insert text. Code: ${error.code}, Msg: ${error.message}`);
      }
    }
  }

  public deleteForward(length: number): void {
    if (this.textInputClient) {
      try {
        this.textInputClient.deleteForward(length);
        // HarmonyOS InputClient.deleteForward deletes BEFORE the cursor (backspace),
        // so the local cache must be updated as a backward deletion.
        this.updateCacheOnDeleteBackward(length);
      } catch (err) {
        const error = err as BusinessError;
        console.error(`OpenBoard Keyboard: Failed to delete forward. Code: ${error.code}, Msg: ${error.message}`);
      }
    }
  }

  public deleteBackward(length: number): void {
    if (this.textInputClient) {
      try {
        this.textInputClient.deleteBackward(length);
        // HarmonyOS InputClient.deleteBackward deletes AFTER the cursor (forward
        // delete), so the local cache must be updated as a forward deletion.
        this.updateCacheOnDeleteForward(length);
      } catch (err) {
        const error = err as BusinessError;
        console.error(`OpenBoard Keyboard: Failed to delete backward. Code: ${error.code}, Msg: ${error.message}`);
      }
    }
  }

  public sendEnterKey(): void {
    if (this.textInputClient) {
      try {
        this.textInputClient.sendKeyFunction(this.currentEnterKeyType);
        this.updateCacheOnInsert('\n');
      } catch (err) {
        const error = err as BusinessError;
        console.error(`OpenBoard Keyboard: Failed to send enter key. Code: ${error.code}, Msg: ${error.message}`);
        try {
          this.textInputClient.insertText('\n');
          this.updateCacheOnInsert('\n');
        } catch (insertErr) {
          const insertError = insertErr as BusinessError;
          console.error(`OpenBoard Keyboard: Failed to insert newline. Code: ${insertError.code}, Msg: ${insertError.message}`);
        }
      }
    }
  }

  public getCurrentEnterKeyType(): number {
    return this.currentEnterKeyType;
  }

  public setEnterKeyTypeCallback(callback: (enterKeyType: number) => void): void {
    this.enterKeyTypeCallback = callback;
  }

  public getTextBeforeCursor(maxLength: number): string {
    const start = Math.max(0, this.cursorPosition - maxLength);
    return this.textBufferCache.slice(start, this.cursorPosition);
  }

  public setNativeLayoutDescriptor(layout: string): void {
    this.nativeLayoutDescriptor = layout && layout.length > 0 ? layout : 'unknown';
  }

  public clearNativeTouchContext(): void {
    this.nativeTouchPoints = [];
  }

  public recordNativeTouchPoint(key: KeyData, x: number, y: number, phase: string): void {
    if (!this.shouldCaptureNativeTouchForKey(key)) {
      return;
    }
    const point: NativeTouchPoint = {
      x,
      y,
      timeMs: Date.now() % 4294967295,
      keyCode: this.mapKeyToNativeCode(key, phase)
    };
    this.nativeTouchPoints.push(point);
    if (this.nativeTouchPoints.length > 24) {
      this.nativeTouchPoints = this.nativeTouchPoints.slice(this.nativeTouchPoints.length - 24);
    }
  }

  private buildNativeSuggestRequest(word: string, prevWord: string = '', locale: string = 'ru-RU'): NativeSuggestRequest {
    const fullContext: string = this.getTextBeforeCursor(96);
    const contextWithoutCurrent: string =
      word.length > 0 && fullContext.endsWith(word)
        ? fullContext.slice(0, fullContext.length - word.length)
        : fullContext;
    const previousTokens: string[] = this.extractContextTokens(contextWithoutCurrent).slice(-3);
    if (prevWord.length > 0 &&
      (previousTokens.length === 0 || previousTokens[previousTokens.length - 1] !== prevWord)) {
      previousTokens.push(prevWord);
    }

    const optionsArray: string[] = [
      'ime:hoskey',
      'locale:' + locale,
      'overlay:' + this.getNativePredictorDebugModeLabel(),
      'autocorrect:on'
    ];
    const prevWordsInfo: NativePrevWordInfo[] = previousTokens.map((token: string): NativePrevWordInfo => {
      return {
        token,
        wasAutocorrected: false
      };
    });
    const dictionarySessions: NativeDictionarySession[] = [{
      id: 'hoskey-' + locale,
      locale
    }];
    const proximityInfo: NativeProximityInfo = {
      layout: this.nativeLayoutDescriptor,
      keyboardWidth: Math.round(this.panelWidthPx),
      keyboardHeight: Math.round(this.panelHeightPx),
      enabled: this.panelWidthPx > 0 && this.panelHeightPx > 0
    };

    return {
      token: word,
      input: word,
      previousTokens,
      prevWordsInfo,
      locale,
      wantAutocorrect: true,
      maxSuggestions: 12,
      fullContext,
      optionsArray,
      packageName: 'com.opentech.openboard',
      fieldActionId: this.currentEnterKeyType,
      dictionarySessions,
      proximityInfo,
      points: this.nativeTouchPoints.slice()
    };
  }

  private extractContextTokens(text: string): string[] {
    if (!text) {
      return [];
    }
    return text
      .split(/[^A-Za-zА-Яа-яЁё0-9]+/)
      .map((token: string): string => token.trim())
      .filter((token: string): boolean => token.length > 0);
  }

  private shouldCaptureNativeTouchForKey(key: KeyData): boolean {
    return key.action === KeyAction.Char ||
      key.action === KeyAction.Space ||
      key.action === KeyAction.Backspace ||
      key.action === KeyAction.Enter;
  }

  private mapKeyToNativeCode(key: KeyData, phase: string): number {
    const phaseBit: number = phase === 'move' ? 0x20000000 : (phase === 'up' ? 0x10000000 : 0);
    switch (key.action) {
      case KeyAction.Space:
        return phaseBit + 32;
      case KeyAction.Enter:
        return phaseBit + 13;
      case KeyAction.Backspace:
        return phaseBit + 8;
      case KeyAction.Char:
      default:
        return phaseBit + (key.mainValue.length > 0 ? key.mainValue.charCodeAt(0) : 0);
    }
  }

  public isBufferEmpty(): boolean {
    return this.textBufferCache.length === 0;
  }

  private updateCacheOnInsert(text: string): void {
    this.textBufferCache =
      this.textBufferCache.slice(0, this.cursorPosition) +
        text +
      this.textBufferCache.slice(this.cursorPosition);
    this.cursorPosition += text.length;
  }

  private updateCacheOnDeleteForward(length: number): void {
    this.textBufferCache =
      this.textBufferCache.slice(0, this.cursorPosition) +
      this.textBufferCache.slice(this.cursorPosition + length);
  }

  private updateCacheOnDeleteBackward(length: number): void {
    this.textBufferCache =
      this.textBufferCache.slice(0, this.cursorPosition - length) +
      this.textBufferCache.slice(this.cursorPosition);
    this.cursorPosition = Math.max(0, this.cursorPosition - length);
  }

  private async syncInitialBuffer(): Promise<void> {
    if (!this.textInputClient) return;

    try {
      const initialText = await this.queryTextFromIME();
      this.textBufferCache = initialText;
      this.cursorPosition = initialText.length;
      console.info(`OpenBoard Keyboard: Initial buffer synced, length: ${initialText.length}`);
    } catch (err) {
      console.warn('OpenBoard Keyboard: Could not sync initial buffer, starting empty');
      this.textBufferCache = '';
      this.cursorPosition = 0;
    }
  }

  private async queryTextFromIME(): Promise<string> {
    const client = this.textInputClient;
    if (!client) {
      throw new Error('No input client');
    }
    // getForward returns the text immediately BEFORE the cursor — exactly what
    // getTextBeforeCursor() feeds the native engine. Seed the cache with it and
    // let syncInitialBuffer place the cursor at its end. Fall back to empty text
    // if the platform rejects the query.
    try {
      const before = await client.getForward(1024);
      return before ?? '';
    } catch (err) {
      console.warn('OpenBoard Keyboard: getForward failed, starting empty: ' + err);
      return '';
    }
  }

  private async openNativeEngineAsync(dictDir: string): Promise<boolean> {
    const mode = this.getNativePredictorDebugModeLabel();
    diagLog.add('Native engine openAsync [' + mode + ']: queued');
    const openResult = await this.nativeEngine.openAsync(dictDir, this.nativeOpenOptions);
    diagLog.add('Native engine open [' + mode + ']: ' + (openResult ? 'OK' : 'FAIL'));
    diagLog.add('Native diagnostics: ' + this.nativeEngine.getDiagnostics());
    if (openResult) {
      diagLog.add('Loaded assets: ' + this.nativeEngine.getLoadedAssetKeys().join(', '));
      const reports = this.nativeEngine.getLoadReports();
      diagLog.add('Load reports: ' + reports.length);
    }
    return openResult;
  }

  private initWindow(): void {
    if (this.mContext === undefined) {
      return;
    }

    const panelInfo: inputMethodEngine.PanelInfo = {
      type: inputMethodEngine.PanelType.SOFT_KEYBOARD,
      flag: inputMethodEngine.PanelFlag.FLG_FIXED
    };

    inputMethodAbility.createPanel(this.mContext, panelInfo).then(async (inputPanel: inputMethodEngine.Panel) => {
      try {
        this.panel = inputPanel;
        if (this.panel) {
          try {
            await this.applyRecommendedPanelMetrics();
            await this.applySystemPanelButtonColors();
            await this.panel.setUiContent('InputMethodExtensionAbility/pages/Index');
            console.info(`OpenBoard Keyboard: Panel initialized with Huawei fixed-panel metrics ${this.panelWidthPx}x${this.panelHeightPx}px`);
          } catch (resizeErr) {
            const error = resizeErr as BusinessError;
            console.error(`OpenBoard Keyboard: Failed to apply panel metrics/content. Code: ${error.code}, Msg: ${error.message}`);
          }
        }
      } catch (err) {
        const error = err as BusinessError;
        console.error(`OpenBoard Keyboard: Failed to initialize panel UI. Code: ${error.code}, Msg: ${error.message}`);
      }
    }).catch((err: BusinessError) => {
      console.error(`Failed to createPanel, code: ${err.code}, message: ${err.message}`);
    });
  }

  private getCurrentDisplaySize(): [number, number] {
    try {
      const dis = display.getDefaultDisplaySync();
      return [Math.round(dis.width), Math.round(dis.height)];
    } catch (err) {
      const error = err as BusinessError;
      console.error(`OpenBoard Keyboard: Failed to get display. Code: ${error.code}, Msg: ${error.message}`);
      return [360, 640];
    }
  }

  private getRecommendedPanelHeightPx(displayWidth: number, displayHeight: number): number {
    const isLandscape = displayWidth > displayHeight;
    const rate = isLandscape
      ? PANEL_HEIGHT_RATE_LANDSCAPE
      : (displayHeight >= 1200 ? PANEL_HEIGHT_RATE_PORTRAIT : PANEL_HEIGHT_RATE_PORTRAIT_COMPACT);
    const rawHeight = displayHeight * rate;
    const maxByDisplay = displayHeight * (isLandscape ? 0.62 : 0.43);
    const maxHeight = Math.max(180, Math.min(PANEL_MAX_HEIGHT_PX, maxByDisplay));
    const minHeight = Math.min(PANEL_MIN_HEIGHT_PX, maxHeight);
    return Math.round(Math.min(maxHeight, Math.max(minHeight, rawHeight)));
  }

  private updatePanelMetricStorage(displayId: number = 0, bottomInsetPx: number = 0): void {
    AppStorage.setOrCreate(PANEL_WIDTH_STORAGE_KEY, Math.round(this.panelWidthPx));
    AppStorage.setOrCreate(PANEL_HEIGHT_STORAGE_KEY, Math.round(this.panelHeightPx));
    AppStorage.setOrCreate(PANEL_DISPLAY_ID_STORAGE_KEY, displayId);
    AppStorage.setOrCreate(PANEL_SAFE_BOTTOM_INSET_STORAGE_KEY, Math.max(0, Math.round(bottomInsetPx)));
  }

  private async refreshSystemPanelInsets(): Promise<void> {
    if (!this.panel) {
      this.updatePanelMetricStorage();
      return;
    }
    try {
      const displayId = await this.panel.getDisplayId();
      const insets = await this.panel.getSystemPanelCurrentInsets(displayId);
      this.updatePanelMetricStorage(displayId, insets.bottom);
      console.info(`OpenBoard Keyboard: System panel insets left=${insets.left}, right=${insets.right}, bottom=${insets.bottom}, display=${displayId}`);
    } catch (err) {
      const error = err as BusinessError;
      this.updatePanelMetricStorage();
      console.warn(`OpenBoard Keyboard: Failed to get system panel insets. Code: ${error.code}, Msg: ${error.message}`);
    }
  }

  private async applySystemPanelButtonColors(): Promise<void> {
    if (!this.panel) {
      return;
    }
    try {
      await this.panel.setSystemPanelButtonColor(
        OpenBoardTheme.systemPanelButtonFill,
        OpenBoardTheme.systemPanelButtonBackground
      );
    } catch (err) {
      const error = err as BusinessError;
      console.warn(`OpenBoard Keyboard: Failed to set system panel button colors. Code: ${error.code}, Msg: ${error.message}`);
    }
  }

  private applyEditorImmersiveMode(mode?: inputMethodEngine.ImmersiveMode): void {
    if (!this.panel || mode === undefined) {
      return;
    }
    try {
      let panelMode: inputMethodEngine.ImmersiveMode = inputMethodEngine.ImmersiveMode.NONE_IMMERSIVE;
      if (mode === inputMethodEngine.ImmersiveMode.DARK_IMMERSIVE ||
        mode === inputMethodEngine.ImmersiveMode.IMMERSIVE) {
        panelMode = inputMethodEngine.ImmersiveMode.DARK_IMMERSIVE;
      } else if (mode === inputMethodEngine.ImmersiveMode.LIGHT_IMMERSIVE) {
        panelMode = inputMethodEngine.ImmersiveMode.LIGHT_IMMERSIVE;
      }
      this.panel.setImmersiveMode(panelMode);
    } catch (err) {
      const error = err as BusinessError;
      console.warn(`OpenBoard Keyboard: Failed to apply immersive mode. Code: ${error.code}, Msg: ${error.message}`);
    }
  }

  private async applyRecommendedPanelMetrics(): Promise<void> {
    if (!this.panel) {
      return;
    }
    const displaySize = this.getCurrentDisplaySize();
    const displayWidth = displaySize[0];
    const displayHeight = displaySize[1];
    const keyHeight = this.getRecommendedPanelHeightPx(displayWidth, displayHeight);
    this.panelWidthPx = displayWidth;
    this.panelHeightPx = keyHeight;
    this.updatePanelMetricStorage();
    await this.panel.resize(displayWidth, keyHeight);
    await this.refreshSystemPanelInsets();
  }

  private registerListener(): void {
    this.registerInputListener();
    keyboardDelegate.on('editorAttributeChanged', this.editorAttributeChangedCallback);
  }

  private registerInputListener(): void {
    inputMethodAbility.on('inputStart', this.inputStartCallback);
    inputMethodAbility.on('inputStop', this.inputStopCallback);
  }

  private hideIfDestroyedAndRecreate(): void {
    if (!this.panel) {
      this.initWindow();
    } else {
      void this.applyRecommendedPanelMetrics();
      void this.applySystemPanelButtonColors();
    }
  }

  private updateEnterKeyType(): void {
    if (!this.textInputClient) {
      return;
    }
    try {
      this.textInputClient.getEditorAttribute((err: BusinessError, editorAttribute: inputMethodEngine.EditorAttribute) => {
        if (err) {
          console.error(`OpenBoard Keyboard: Failed to get editor attribute. Code: ${err.code}, Msg: ${err.message}`);
          this.currentEnterKeyType = inputMethodEngine.ENTER_KEY_TYPE_UNSPECIFIED;
          return;
        }
        this.currentEnterKeyType = editorAttribute.enterKeyType;
        console.info(`OpenBoard Keyboard: Enter key type updated to ${this.currentEnterKeyType}`);
        if (this.enterKeyTypeCallback) {
          this.enterKeyTypeCallback(this.currentEnterKeyType);
        }
      });
    } catch (err) {
      console.error(`OpenBoard Keyboard: Failed to call getEditorAttribute: ${err}`);
      this.currentEnterKeyType = inputMethodEngine.ENTER_KEY_TYPE_UNSPECIFIED;
    }
  }

  private inputStopCallback = (): void => {
    console.info('OpenBoard Keyboard: Input stopped');
    this.textInputClient = undefined;
    this.textBufferCache = '';
    this.cursorPosition = 0;
    this.currentEnterKeyType = inputMethodEngine.ENTER_KEY_TYPE_UNSPECIFIED;
    this.clearNativeTouchContext();
  };

  private unregisterListener(): void {
    inputMethodAbility.off('inputStart', this.inputStartCallback);
    inputMethodAbility.off('inputStop', this.inputStopCallback);
    keyboardDelegate.off('editorAttributeChanged', this.editorAttributeChangedCallback);
  }
}

const keyboardController = new KeyboardController();
export default keyboardController;
