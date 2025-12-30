import { Want } from '@kit.AbilityKit';
import keyboardController from './model/KeyboardController';
import predictionModel from './model/PredictionModel';
import { InputMethodExtensionAbility } from '@kit.IMEKit';

export default class InputDemoService extends InputMethodExtensionAbility {
  onCreate(want: Want): void {
    keyboardController.onCreate(this.context);

    // Initialize prediction model with context and load saved data
    predictionModel.setContext(this.context);
    predictionModel.load().then(() => {
      console.info('OpenBoard: Prediction model loaded successfully');
    }).catch((err) => {
      console.error('OpenBoard: Failed to load prediction model', err);
    });
  }

  onDestroy(): void {
    console.info("onDestroy.");

    // Save prediction model before destroying
    predictionModel.save().then(() => {
      console.info('OpenBoard: Prediction model saved successfully');
    }).catch((err) => {
      console.error('OpenBoard: Failed to save prediction model', err);
    });

    keyboardController.onDestroy();
  }
}