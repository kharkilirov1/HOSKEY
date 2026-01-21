import { Want } from '@kit.AbilityKit';
import keyboardController from './model/KeyboardController';
import optimizedPredictionModel from './model/OptimizedPredictionModel';
import clipboardHistory from './model/ClipboardHistory';
import { InputMethodExtensionAbility } from '@kit.IMEKit';

export default class HoskeyInputMethod extends InputMethodExtensionAbility {
  onCreate(want: Want): void {
    console.info('[HOSKEY IME] onCreate called, want action:', want.action ?? 'none');
    console.info('[HOSKEY IME] Context bundleName:', this.context.extensionAbilityInfo.bundleName);

    keyboardController.onCreate(this.context);

    // Initialize optimized prediction model with context and load saved data
    optimizedPredictionModel.setContext(this.context);
    optimizedPredictionModel.load().then(() => {
      console.info('OpenBoard: Prediction model loaded successfully');

      // Warmup optimized model for default language (ru)
      return optimizedPredictionModel.warmup('ru');
    }).then(() => {
      console.info('OpenBoard: Prediction model warmed up for default language');
    }).catch((err: Error) => {
      console.error('OpenBoard: Failed to initialize prediction model', err);
    });

    // Initialize clipboard history
    clipboardHistory.setContext(this.context);
    clipboardHistory.load().then(() => {
      console.info('OpenBoard: Clipboard history loaded successfully');
    }).catch((err: Error) => {
      console.error('OpenBoard: Failed to initialize clipboard history', err);
    });
  }

  async onDestroy(): Promise<void> {
    console.info('[HOSKEY IME] onDestroy called');

    // Save prediction model before destroying (with await to ensure completion)
    try {
      await optimizedPredictionModel.save();
      console.info('OpenBoard: Prediction model saved successfully');
    } catch (err) {
      console.error('OpenBoard: Failed to save prediction model', err);
    }

    keyboardController.onDestroy();
  }
}