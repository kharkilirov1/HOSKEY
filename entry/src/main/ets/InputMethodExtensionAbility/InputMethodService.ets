import { Want } from '@kit.AbilityKit';
import keyboardController from './model/KeyboardController';
import optimizedPredictionModel from './model/OptimizedPredictionModel';
import clipboardHistory from './model/ClipboardHistory';
import nativeDictionary from './model/NativeDictionary';
import { InputMethodExtensionAbility } from '@kit.IMEKit';

// Debug flag - set to true to run self-tests on startup
const DEBUG_SELF_TEST = false;

export default class HoskeyInputMethod extends InputMethodExtensionAbility {
  onCreate(want: Want): void {
    console.info('[HOSKEY IME] onCreate called, want action:', want.action ?? 'none');
    console.info('[HOSKEY IME] Context bundleName:', this.context.extensionAbilityInfo.bundleName);

    keyboardController.onCreate(this.context);

    // Initialize optimized prediction model with context and load saved data
    optimizedPredictionModel.setContext(this.context);
    optimizedPredictionModel.load().then(() => {
      console.info('[HOSKEY] Prediction model loaded successfully');

      // Warmup optimized model for default language (ru)
      return optimizedPredictionModel.warmup('ru');
    }).then(() => {
      console.info('[HOSKEY] Prediction model warmed up for default language');
    }).catch((err: Error) => {
      console.error('[HOSKEY] Failed to initialize prediction model', err);
    });

    // Initialize clipboard history
    clipboardHistory.setContext(this.context);
    clipboardHistory.load().then(() => {
      console.info('[HOSKEY] Clipboard history loaded successfully');
    }).catch((err: Error) => {
      console.error('[HOSKEY] Failed to initialize clipboard history', err);
    });

    // Run self-test in debug mode
    if (DEBUG_SELF_TEST) {
      nativeDictionary.setContext(this.context);
      nativeDictionary.initialize('ru').then((loaded) => {
        if (loaded) {
          return nativeDictionary.runSelfTest();
        }
        console.warn('[HOSKEY IME] Self-test skipped: dictionary not loaded');
        return false;
      }).then((passed) => {
        console.info(`[HOSKEY IME] Self-test result: ${passed ? 'PASSED' : 'FAILED'}`);
      }).catch((err: Error) => {
        console.error('[HOSKEY IME] Self-test error:', err);
      });
    }
  }

  async onDestroy(): Promise<void> {
    console.info('[HOSKEY IME] onDestroy called');

    // Save prediction model before destroying (with await to ensure completion)
    try {
      await optimizedPredictionModel.save();
      console.info('[HOSKEY] Prediction model saved successfully');
    } catch (err) {
      console.error('[HOSKEY] Failed to save prediction model', err);
    }

    keyboardController.onDestroy();
  }
}