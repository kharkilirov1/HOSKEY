 import { display } from '@kit.ArkUI';
import { inputMethodEngine, InputMethodExtensionContext } from '@kit.IMEKit';
import { BusinessError } from '@kit.BasicServicesKit';

const inputMethodAbility: inputMethodEngine.InputMethodAbility = inputMethodEngine.getInputMethodAbility();

export class KeyboardController {
  private mContext: InputMethodExtensionContext | undefined = undefined;
  private panel: inputMethodEngine.Panel | undefined = undefined;
  private textInputClient: inputMethodEngine.InputClient | undefined = undefined;
  private currentEnterKeyType: number = inputMethodEngine.ENTER_KEY_TYPE_UNSPECIFIED;
  private enterKeyTypeCallback?: (enterKeyType: number) => void;
  private textBufferCache: string = '';
  private cursorPosition: number = 0;
  constructor() {}

  public onCreate(context: InputMethodExtensionContext): void {
    console.info('OpenBoard Keyboard: KeyboardController onCreate called');
    this.mContext = context;
    this.initWindow();
    this.registerListener();
  }

  public onDestroy(): void {
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
        this.panel.hide();
        console.info('OpenBoard Keyboard: Panel hidden');
      } catch (err) {
        const error = err as BusinessError;
        console.error(`OpenBoard Keyboard: Failed to hide panel. Code: ${error.code}, Msg: ${error.message}`);
      }
    }
  }

  private showKeyboard(): void {
    if (this.panel) {
      try {
        this.panel.show();
        console.info('OpenBoard Keyboard: Panel shown');
      } catch (err) {
        const error = err as BusinessError;
        console.error(`OpenBoard Keyboard: Failed to show panel. Code: ${error.code}, Msg: ${error.message}`);
      }
    }
  }

  public insertText(text: string): void {
    if (!this.textInputClient) {
      console.warn('OpenBoard Keyboard: Text input client not available');
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
      console.warn('OpenBoard Keyboard: Text input client not available');
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
      console.warn('OpenBoard Keyboard: Text input client not available');
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
        console.error(`OpenBoard Keyboard: Failed to move cursor. Code: ${error.code}, Msg: ${error.message}`);
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

  private queryTextFromIME(): Promise<string> {
    return new Promise((resolve, reject) => {
      if (!this.textInputClient) {
        reject(new Error('No input client'));
        return;
      }

      // Simple approach - skip initial text sync for now
      console.warn('OpenBoard Keyboard: Text query not available in this version');
      resolve('');
    });
  }

  private initWindow(): void {
    if (this.mContext === undefined) {
      return;
    }
    let dWidth: number = 360;
    let dHeight: number = 640;
    try {
      const dis = display.getDefaultDisplaySync();
      dWidth = dis.width;
      dHeight = dis.height;
    } catch (err) {
      const error = err as BusinessError;
      console.error(`OpenBoard Keyboard: Failed to get display. Code: ${error.code}, Msg: ${error.message}`);
    }
    const keyHeightRate = 0.30; // 30% of screen height for all devices
    const keyHeight = dHeight * keyHeightRate;
    const nonBarPosition = dHeight - keyHeight;

    const panelInfo: inputMethodEngine.PanelInfo = {
      type: inputMethodEngine.PanelType.SOFT_KEYBOARD,
      flag: inputMethodEngine.PanelFlag.FLG_FIXED
    };

    inputMethodAbility.createPanel(this.mContext, panelInfo).then(async (inputPanel: inputMethodEngine.Panel) => {
      try {
        this.panel = inputPanel;
        if (this.panel) {
          try {
            await this.panel.resize(dWidth, keyHeight);
            await this.panel.moveTo(0, nonBarPosition);
            await this.panel.setUiContent('InputMethodExtensionAbility/pages/Index');
            console.info(`OpenBoard Keyboard: Panel initialized with fixed height ${keyHeight}px`);
          } catch (resizeErr) {
            const error = resizeErr as BusinessError;
            console.error(`OpenBoard Keyboard: Failed to resize/move panel. Code: ${error.code}, Msg: ${error.message}`);
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

  private registerListener(): void {
    this.registerInputListener();
  }

  private inputStartCallback = async (_: inputMethodEngine.KeyboardController, textInputClient: inputMethodEngine.InputClient): Promise<void> => {
    this.textInputClient = textInputClient;
    this.updateEnterKeyType();
    await this.syncInitialBuffer();
    this.hideIfDestroyedAndRecreate();
    this.showKeyboard();
  };

  private registerInputListener(): void {
    inputMethodAbility.on('inputStart', this.inputStartCallback);
    inputMethodAbility.on('inputStop', this.inputStopCallback);
  }

  private hideIfDestroyedAndRecreate(): void {
    if (!this.panel) {
      this.initWindow();
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
  };

  private unregisterListener(): void {
    inputMethodAbility.off('inputStart', this.inputStartCallback);
    inputMethodAbility.off('inputStop', this.inputStopCallback);
  }
}

const keyboardController = new KeyboardController();
export default keyboardController;
