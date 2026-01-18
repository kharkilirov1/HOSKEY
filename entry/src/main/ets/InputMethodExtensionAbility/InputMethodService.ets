import { Want } from '@kit.AbilityKit';
import keyboardController from './model/KeyboardController';
import predictionModel from './model/PredictionModel';
import optimizedPredictionModel from './model/OptimizedPredictionModel';
import clipboardHistory from './model/ClipboardHistory';
import { InputMethodExtensionAbility } from '@kit.IMEKit';

export default class InputDemoService extends InputMethodExtensionAbility {
  onCreate(want: Want): void {
    keyboardController.onCreate(this.context);

    // Initialize prediction model with context and load saved data
    predictionModel.setContext(this.context);
    predictionModel.load().then(() => {
      console.info('OpenBoard: Prediction model loaded successfully');

      // Warmup BaseLexicon for default language (ru) to ensure predictions work on empty model
      return predictionModel.warmup('ru');
    }).then(() => {
      console.info('OpenBoard: BaseLexicon warmed up for default language');

      // Load dictionaries after loading saved data (now a no-op, BaseLexicon loaded via warmup)
      return predictionModel.loadDictionaries();
    }).then(() => {
      console.info('OpenBoard: Dictionaries loaded successfully');
    }).catch((err: Error) => {
      console.error('OpenBoard: Failed to initialize prediction model', err);
    });
    
    // Initialize optimized prediction model with context
    optimizedPredictionModel.setContext(this.context);
    optimizedPredictionModel.load().then(() => {
      console.info('OpenBoard: Optimized prediction model loaded successfully');

      // Warmup optimized model for default language (ru)
      return optimizedPredictionModel.warmup('ru');
    }).then(() => {
      console.info('OpenBoard: Optimized prediction model warmed up for default language');
    }).catch((err: Error) => {
      console.error('OpenBoard: Failed to initialize optimized prediction model', err);
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
    console.info("onDestroy.");

    // Save prediction models before destroying (with await to ensure completion)
    try {
      await predictionModel.save();
      console.info('OpenBoard: Prediction model saved successfully');
      
      await optimizedPredictionModel.save();
      console.info('OpenBoard: Optimized prediction model saved successfully');
    } catch (err) {
      console.error('OpenBoard: Failed to save prediction model', err);
    }

    keyboardController.onDestroy();
  }
}