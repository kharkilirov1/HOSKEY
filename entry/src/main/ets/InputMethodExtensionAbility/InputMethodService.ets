import { Want } from '@kit.AbilityKit';
import keyboardController from './model/KeyboardController';
import { InputMethodExtensionAbility } from '@kit.IMEKit';

export default class InputDemoService extends InputMethodExtensionAbility {
  onCreate(want: Want): void {
    keyboardController.onCreate(this.context);
  }

  onDestroy(): void {
    console.info("onDestroy.");
    keyboardController.onDestroy();
  }
}