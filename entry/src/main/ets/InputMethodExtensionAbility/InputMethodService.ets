import { Want } from '@kit.AbilityKit';
import keyboardController from './model/KeyboardController';
import predictionModel from './model/PredictionModel';
import clipboardHistory from './model/ClipboardHistory';
import { InputMethodExtensionAbility } from '@kit.IMEKit';

export default class InputDemoService extends InputMethodExtensionAbility {
  onCreate(want: Want): void {
    keyboardController.onCreate(this.context);

    // Initialize prediction model with context and load saved data
    predictionModel.setContext(this.context);
    predictionModel.load().then(() => {
      console.info('OpenBoard: Prediction model loaded successfully');

      // Load dictionaries after loading saved data
      return predictionModel.loadDictionaries();
    }).then(() => {
      console.info('OpenBoard: Dictionaries loaded successfully');
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

  onDestroy(): void {
    console.info("onDestroy.");

    // Save prediction model before destroying
    predictionModel.save().then(() => {
      console.info('OpenBoard: Prediction model saved successfully');
    }).catch((err: Error) => {
      console.error('OpenBoard: Failed to save prediction model', err);
    });

    keyboardController.onDestroy();
  }
}