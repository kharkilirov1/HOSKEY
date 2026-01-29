import { display } from '@kit.ArkUI';
import { inputMethodEngine, InputMethodExtensionContext } from '@kit.IMEKit';
import { BusinessError } from '@kit.BasicServicesKit';

const inputMethodAbility: inputMethodEngine.InputMethodAbility = inputMethodEngine.getInputMethodAbility();

// Logging prefix for easy filtering
const LOG_TAG = '[HOSKEY IME]';

export class KeyboardController {
  private mContext: InputMethodExtensionContext | undefined = undefined;
  private panel: inputMethodEngine.Panel | undefined = undefined;
  private textInputClient: inputMethodEngine.InputClient | undefined = undefined;
  private currentEnterKeyType: number = inputMethodEngine.ENTER_KEY_TYPE_UNSPECIFIED;
  private enterKeyTypeCallback?: (enterKeyType: number) => void;
  private textBufferCache: string = '';
  private cursorPosition: number = 0;

  // Panel state tracking
  private panelReady: boolean = false;
  private panelInitializing: boolean = false;
  private panelDestroyed: boolean = false;
  
  // Bug #5 Fix: Promise-based synchronization for panel lifecycle
  private panelReadyPromise: Promise<void> | null = null;
  private panelReadyResolve: (() => void) | null = null;
  private pendingShowKeyboard: boolean = false;

  constructor() {}

  public onCreate(context: InputMethodExtensionContext): void {
    console.info(`${LOG_TAG} KeyboardController.onCreate called`);

    // Safe access to context info (may be undefined during early lifecycle)
    const abilityName = context?.extensionAbilityInfo?.name ?? 'unknown';
    const bundleName = context?.extensionAbilityInfo?.bundleName ?? 'unknown';
    console.info(`${LOG_TAG} Context abilityName: ${abilityName}`);
    console.info(`${LOG_TAG} Context bundleName: ${bundleName}`);

    this.mContext = context;
    this.panelDestroyed = false;
    this.panelReady = false;
    this.panelInitializing = false;

    this.initWindow();
    this.registerListener();

    console.info(`${LOG_TAG} KeyboardController.onCreate complete, waiting for panel initialization`);
  }

  public onDestroy(): void {
    console.info(`${LOG_TAG} KeyboardController.onDestroy called`);

    // 1. Unregister listeners first to prevent callbacks during teardown
    this.unregisterListener();
    console.info(`${LOG_TAG} onDestroy: listeners unregistered`);

    // 2. Clear input client and state
    this.textInputClient = undefined;
    this.currentEnterKeyType = inputMethodEngine.ENTER_KEY_TYPE_UNSPECIFIED;
    this.textBufferCache = '';
    this.cursorPosition = 0;
    console.info(`${LOG_TAG} onDestroy: input state cleared`);

    // 3. Destroy panel if exists
    if (this.panel) {
      console.info(`${LOG_TAG} onDestroy: destroying panel`);
      try {
        inputMethodAbility.destroyPanel(this.panel);
        console.info(`${LOG_TAG} onDestroy: panel destroyed successfully`);
      } catch (err) {
        const error = err as BusinessError;
        console.error(`${LOG_TAG} onDestroy: failed to destroy panel. Code: ${error.code}, Msg: ${error.message}`);
      }
      this.panel = undefined;
    }

    // 4. Mark panel state
    this.panelReady = false;
    this.panelInitializing = false;
    this.panelDestroyed = true;

    // 5. Destroy context last
    if (this.mContext) {
      console.info(`${LOG_TAG} onDestroy: destroying context`);
      try {
        this.mContext.destroy();
        console.info(`${LOG_TAG} onDestroy: context destroyed successfully`);
      } catch (err) {
        const error = err as BusinessError;
        console.error(`${LOG_TAG} onDestroy: failed to destroy context. Code: ${error.code}, Msg: ${error.message}`);
      }
      this.mContext = undefined;
    }

    console.info(`${LOG_TAG} KeyboardController.onDestroy complete`);
  }

  public hideKeyboard(): void {
    console.info(`${LOG_TAG} hideKeyboard called, panelReady=${this.panelReady}`);

    if (!this.panel) {
      console.warn(`${LOG_TAG} hideKeyboard: panel is null, nothing to hide`);
      return;
    }

    if (!this.panelReady) {
      console.warn(`${LOG_TAG} hideKeyboard: panel not ready yet`);
      return;
    }

    try {
      this.panel.hide();
      console.info(`${LOG_TAG} hideKeyboard: panel hidden successfully`);
    } catch (err) {
      const error = err as BusinessError;
      console.error(`${LOG_TAG} hideKeyboard: failed. Code: ${error.code}, Msg: ${error.message}`);

      // If hide fails, panel might be destroyed
      if (error.code === 12800004 || error.code === 12800010) { // Common "panel destroyed" error codes
        console.warn(`${LOG_TAG} hideKeyboard: panel appears destroyed, marking for recreation`);
        this.markPanelDestroyed();
      }
    }
  }

  private showKeyboard(): void {
    console.info(`${LOG_TAG} showKeyboard called, panelReady=${this.panelReady}, panel=${this.panel !== undefined}`);

    if (!this.panel) {
      console.warn(`${LOG_TAG} showKeyboard: panel is null, cannot show`);
      return;
    }

    if (!this.panelReady) {
      console.warn(`${LOG_TAG} showKeyboard: panel not ready yet, deferring show`);
      return;
    }

    try {
      this.panel.show();
      console.info(`${LOG_TAG} showKeyboard: panel shown successfully`);
    } catch (err) {
      const error = err as BusinessError;
      console.error(`${LOG_TAG} showKeyboard: failed. Code: ${error.code}, Msg: ${error.message}`);

      // If show fails, panel might be destroyed
      if (error.code === 12800004 || error.code === 12800010) {
        console.warn(`${LOG_TAG} showKeyboard: panel appears destroyed, marking for recreation`);
        this.markPanelDestroyed();
      }
    }
  }

  private markPanelDestroyed(): void {
    this.panelReady = false;
    this.panelDestroyed = true;
    this.panel = undefined;
  }

  public insertText(text: string): void {
    if (!this.textInputClient) {
      console.warn(`${LOG_TAG} insertText: no textInputClient`);
      return;
    }
    try {
      this.textInputClient.insertText(text);
      this.updateCacheOnInsert(text);
    } catch {
      // Silent catch for ArkTS compliance - hot path optimization
    }
  }

  public deleteForward(length: number): void {
    if (length <= 0) return;
    if (!this.textInputClient) {
      console.warn(`${LOG_TAG} deleteForward: no textInputClient`);
      return;
    }
    try {
      this.textInputClient.deleteForward(length);
      this.updateCacheOnDeleteForward(length);
    } catch {
      // Silent catch for ArkTS compliance - hot path optimization
    }
  }

  public deleteBackward(length: number): void {
    if (length <= 0) return;
    if (!this.textInputClient) {
      console.warn(`${LOG_TAG} deleteBackward: no textInputClient`);
      return;
    }
    try {
      this.textInputClient.deleteBackward(length);
      this.updateCacheOnDeleteBackward(length);
    } catch {
      // Silent catch for ArkTS compliance - hot path optimization
    }
  }

  public deleteBeforeCursor(length: number = 1): void {
    if (length <= 0) return;
    // HarmonyOS: deleteForward removes text BEFORE cursor (opposite naming!)
    this.deleteForward(length);
  }

  public deleteAfterCursor(length: number = 1): void {
    if (length <= 0) return;
    // HarmonyOS: deleteBackward removes text AFTER cursor (opposite naming!)
    this.deleteBackward(length);
  }

  public moveCursor(direction: number): void {
    if (this.textInputClient) {
      try {
        this.textInputClient.moveCursor(direction);
        this.updateCacheOnCursorMove(direction);
      } catch (err) {
        const error = err as BusinessError;
        console.error(`${LOG_TAG} moveCursor: failed. Code: ${error.code}, Msg: ${error.message}`);
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
        console.error(`${LOG_TAG} sendEnterKey: sendKeyFunction failed. Code: ${error.code}, Msg: ${error.message}`);
        try {
          this.textInputClient.insertText('\n');
          this.updateCacheOnInsert('\n');
        } catch (insertErr) {
          const insertError = insertErr as BusinessError;
          console.error(`${LOG_TAG} sendEnterKey: insertText fallback failed. Code: ${insertError.code}, Msg: ${insertError.message}`);
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
    this.updateCacheOnDeleteAfterCursor(length);
  }

  private updateCacheOnDeleteBackward(length: number): void {
    this.updateCacheOnDeleteBeforeCursor(length);
  }

  private updateCacheOnDeleteBeforeCursor(length: number): void {
    if (length <= 0) return;
    const safeCursor = Math.max(0, Math.min(this.cursorPosition, this.textBufferCache.length));
    const safeLength = Math.min(length, safeCursor);
    if (safeLength <= 0) return;
    const deleteStart = Math.max(0, safeCursor - safeLength);
    this.textBufferCache =
      this.textBufferCache.slice(0, deleteStart) +
      this.textBufferCache.slice(safeCursor);
    this.cursorPosition = deleteStart;
  }

  private updateCacheOnDeleteAfterCursor(length: number): void {
    if (length <= 0) return;
    const safeCursor = Math.max(0, Math.min(this.cursorPosition, this.textBufferCache.length));
    const maxDeletable = Math.max(0, this.textBufferCache.length - safeCursor);
    const safeLength = Math.min(length, maxDeletable);
    if (safeLength <= 0) return;
    const deleteEnd = safeCursor + safeLength;
    this.textBufferCache =
      this.textBufferCache.slice(0, safeCursor) +
      this.textBufferCache.slice(deleteEnd);
  }

  private updateCacheOnCursorMove(direction: number): void {
    // direction: positive = right/forward, negative = left/backward
    this.cursorPosition = Math.max(0, Math.min(this.textBufferCache.length, this.cursorPosition + direction));
  }

  private async syncInitialBuffer(): Promise<void> {
    if (!this.textInputClient) {
      console.warn(`${LOG_TAG} syncInitialBuffer: no textInputClient`);
      return;
    }

    try {
      const initialText = await this.queryTextFromIME();
      this.textBufferCache = initialText;
      this.cursorPosition = initialText.length;
      console.info(`${LOG_TAG} syncInitialBuffer: synced, length=${initialText.length}`);
    } catch (err) {
      console.warn(`${LOG_TAG} syncInitialBuffer: could not sync, starting empty`);
      this.textBufferCache = '';
      this.cursorPosition = 0;
    }
  }

  private queryTextFromIME(): Promise<string> {
    return new Promise((resolve, reject) => {
      if (!this.textInputClient) {
        reject(new Error('No input client'));
        return;
      }

      // Simple approach - skip initial text sync for now
      // TODO: Implement proper text query when API is available
      resolve('');
    });
  }

  private initWindow(): void {
    console.info(`${LOG_TAG} initWindow called`);

    if (this.mContext === undefined) {
      console.error(`${LOG_TAG} initWindow: mContext is undefined, cannot create panel`);
      return;
    }

    if (this.panelInitializing) {
      console.warn(`${LOG_TAG} initWindow: panel already initializing, skipping`);
      return;
    }

    if (this.panelReady && this.panel) {
      console.info(`${LOG_TAG} initWindow: panel already ready, skipping`);
      return;
    }

    this.panelInitializing = true;
    this.panelDestroyed = false;
    
    // Bug #5 Fix: Create promise for panel ready state BEFORE async operation
    this.panelReadyPromise = new Promise<void>((resolve) => {
      this.panelReadyResolve = resolve;
    });

    // Get display dimensions
    let dWidth: number = 360;
    let dHeight: number = 640;
    try {
      const dis = display.getDefaultDisplaySync();
      dWidth = dis.width;
      dHeight = dis.height;
      console.info(`${LOG_TAG} initWindow: display size ${dWidth}x${dHeight}`);
    } catch (err) {
      const error = err as BusinessError;
      console.error(`${LOG_TAG} initWindow: failed to get display. Code: ${error.code}, Msg: ${error.message}`);
      console.info(`${LOG_TAG} initWindow: using fallback display size ${dWidth}x${dHeight}`);
    }

    const keyHeightRate = 0.30; // 30% of screen height for all devices
    const keyHeight = dHeight * keyHeightRate;
    const nonBarPosition = dHeight - keyHeight;

    const panelInfo: inputMethodEngine.PanelInfo = {
      type: inputMethodEngine.PanelType.SOFT_KEYBOARD,
      flag: inputMethodEngine.PanelFlag.FLG_FIXED
    };

    console.info(`${LOG_TAG} initWindow: creating panel (SOFT_KEYBOARD, FLG_FIXED)`);

    inputMethodAbility.createPanel(this.mContext, panelInfo).then(async (inputPanel: inputMethodEngine.Panel) => {
      console.info(`${LOG_TAG} initWindow: panel created successfully`);
      this.panel = inputPanel;

      try {
        // Step 1: Resize panel
        await this.panel.resize(dWidth, keyHeight);
        console.info(`${LOG_TAG} initWindow: panel resized to ${dWidth}x${keyHeight}`);

        // Step 2: Move panel to position
        await this.panel.moveTo(0, nonBarPosition);
        console.info(`${LOG_TAG} initWindow: panel moved to (0, ${nonBarPosition})`);

        // Step 3: Set UI content
        const uiContentPath = 'InputMethodExtensionAbility/pages/Index';
        console.info(`${LOG_TAG} initWindow: setting UI content: ${uiContentPath}`);

        await this.panel.setUiContent(uiContentPath);
        console.info(`${LOG_TAG} initWindow: UI content set successfully`);

        // Mark panel as ready
        this.panelReady = true;
        this.panelInitializing = false;
        console.info(`${LOG_TAG} initWindow: panel initialization COMPLETE, panelReady=true`);
        
        // Bug #5 Fix: Resolve the promise to unblock waiters
        if (this.panelReadyResolve) {
          this.panelReadyResolve();
          this.panelReadyResolve = null;
        }

        // Bug #5 Fix: Handle pending show request (if inputStart came before panel was ready)
        if (this.pendingShowKeyboard) {
          console.info(`${LOG_TAG} initWindow: handling pending showKeyboard request`);
          this.pendingShowKeyboard = false;
          this.showKeyboard();
        } else if (this.textInputClient) {
          // If there's a pending input session, show keyboard now
          console.info(`${LOG_TAG} initWindow: textInputClient exists, showing keyboard`);
          this.showKeyboard();
        }

      } catch (configErr) {
        const error = configErr as BusinessError;
        console.error(`${LOG_TAG} initWindow: failed to configure panel. Code: ${error.code}, Msg: ${error.message}`);

        // Detailed error analysis
        if (error.message && error.message.includes('UiContent')) {
          console.error(`${LOG_TAG} initWindow: setUiContent failed - check that 'InputMethodExtensionAbility/pages/Index' exists in main_pages.json`);
        }

        this.panelInitializing = false;
        this.panelReady = false;

        // Clean up failed panel
        if (this.panel) {
          try {
            inputMethodAbility.destroyPanel(this.panel);
          } catch (destroyErr) {
            console.error(`${LOG_TAG} initWindow: failed to destroy failed panel`);
          }
          this.panel = undefined;
        }
      }
    }).catch((err: BusinessError) => {
      console.error(`${LOG_TAG} initWindow: createPanel failed. Code: ${err.code}, Msg: ${err.message}`);
      this.panelInitializing = false;
      this.panelReady = false;
    });
  }

  private registerListener(): void {
    console.info(`${LOG_TAG} registerListener: registering IME event listeners`);
    this.registerInputListener();
  }

  private inputStartCallback = async (_: inputMethodEngine.KeyboardController, textInputClient: inputMethodEngine.InputClient): Promise<void> => {
    console.info(`${LOG_TAG} inputStart: received, setting up input session`);

    this.textInputClient = textInputClient;
    console.info(`${LOG_TAG} inputStart: textInputClient set`);

    this.updateEnterKeyType();
    await this.syncInitialBuffer();

    // Ensure panel is ready before showing
    await this.ensurePanelReady();

    // Now show keyboard
    this.showKeyboard();

    console.info(`${LOG_TAG} inputStart: complete`);
  };

  private registerInputListener(): void {
    inputMethodAbility.on('inputStart', this.inputStartCallback);
    inputMethodAbility.on('inputStop', this.inputStopCallback);
    console.info(`${LOG_TAG} registerInputListener: inputStart and inputStop registered`);
  }

  private async ensurePanelReady(): Promise<void> {
    console.info(`${LOG_TAG} ensurePanelReady: checking panel state`);

    // If panel is destroyed or null, recreate it
    if (this.panelDestroyed || !this.panel) {
      console.info(`${LOG_TAG} ensurePanelReady: panel needs recreation (destroyed=${this.panelDestroyed}, panel=${this.panel !== undefined})`);
      this.initWindow();
    }

    // Bug #5 Fix: Use promise-based waiting instead of polling
    if (!this.panelReady) {
      console.info(`${LOG_TAG} ensurePanelReady: waiting for panel to become ready (promise-based)`);
      
      if (this.panelReadyPromise) {
        // Wait for promise with timeout
        const timeoutPromise = new Promise<void>((_, reject) => {
          setTimeout(() => reject(new Error('Panel ready timeout')), 3000);
        });
        
        try {
          await Promise.race([this.panelReadyPromise, timeoutPromise]);
          console.info(`${LOG_TAG} ensurePanelReady: panel ready (promise resolved)`);
        } catch (err) {
          console.error(`${LOG_TAG} ensurePanelReady: timeout waiting for panel after 3000ms`);
          // Mark pending so showKeyboard will be called when panel is ready
          this.pendingShowKeyboard = true;
        }
      } else {
        // Fallback to old polling method if promise not available
        console.warn(`${LOG_TAG} ensurePanelReady: no panelReadyPromise, falling back to polling`);
        const maxWaitMs = 3000;
        const checkIntervalMs = 50;
        let waited = 0;

        while (!this.panelReady && waited < maxWaitMs) {
          await this.delay(checkIntervalMs);
          waited += checkIntervalMs;
        }

        if (this.panelReady) {
          console.info(`${LOG_TAG} ensurePanelReady: panel ready after ${waited}ms (polling)`);
        } else {
          console.error(`${LOG_TAG} ensurePanelReady: timeout waiting for panel after ${maxWaitMs}ms`);
        }
      }
    } else {
      console.info(`${LOG_TAG} ensurePanelReady: panel already ready`);
    }
  }

  private delay(ms: number): Promise<void> {
    return new Promise(resolve => setTimeout(resolve, ms));
  }

  private updateEnterKeyType(): void {
    if (!this.textInputClient) {
      console.warn(`${LOG_TAG} updateEnterKeyType: no textInputClient`);
      return;
    }
    try {
      this.textInputClient.getEditorAttribute((err: BusinessError, editorAttribute: inputMethodEngine.EditorAttribute) => {
        if (err) {
          console.error(`${LOG_TAG} updateEnterKeyType: failed. Code: ${err.code}, Msg: ${err.message}`);
          this.currentEnterKeyType = inputMethodEngine.ENTER_KEY_TYPE_UNSPECIFIED;
          return;
        }
        this.currentEnterKeyType = editorAttribute.enterKeyType;
        console.info(`${LOG_TAG} updateEnterKeyType: enterKeyType=${this.currentEnterKeyType}`);
        if (this.enterKeyTypeCallback) {
          this.enterKeyTypeCallback(this.currentEnterKeyType);
        }
      });
    } catch (err) {
      console.error(`${LOG_TAG} updateEnterKeyType: exception - ${err}`);
      this.currentEnterKeyType = inputMethodEngine.ENTER_KEY_TYPE_UNSPECIFIED;
    }
  }

  private inputStopCallback = (): void => {
    console.info(`${LOG_TAG} inputStop: received, cleaning up input session`);

    // Hide keyboard first
    this.hideKeyboard();

    // Clear input state
    this.textInputClient = undefined;
    this.textBufferCache = '';
    this.cursorPosition = 0;
    this.currentEnterKeyType = inputMethodEngine.ENTER_KEY_TYPE_UNSPECIFIED;

    console.info(`${LOG_TAG} inputStop: complete, state cleared`);
  };

  private unregisterListener(): void {
    console.info(`${LOG_TAG} unregisterListener: removing IME event listeners`);
    try {
      inputMethodAbility.off('inputStart', this.inputStartCallback);
      inputMethodAbility.off('inputStop', this.inputStopCallback);
      console.info(`${LOG_TAG} unregisterListener: listeners removed successfully`);
    } catch (err) {
      console.error(`${LOG_TAG} unregisterListener: failed to remove listeners - ${err}`);
    }
  }
}

const keyboardController = new KeyboardController();
export default keyboardController;
