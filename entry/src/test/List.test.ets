import localUnitTest from './LocalUnit.test';
import emojiPanelToggleTest from './EmojiPanelToggle.test';
import keyboardCollapseToggleTest from './KeyboardCollapseToggle.test';
import predictionInputStateTest from './PredictionInputState.test';
import predictionModelTest from './PredictionModel.test';
import privacyGateTest from './PrivacyGate.test';

export default function testsuite() {
  localUnitTest();
  emojiPanelToggleTest();
  keyboardCollapseToggleTest();
  privacyGateTest();
  predictionInputStateTest();
  predictionModelTest();
}
