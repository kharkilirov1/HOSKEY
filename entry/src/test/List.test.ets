import localUnitTest from './LocalUnit.test';
import emojiPanelToggleTest from './EmojiPanelToggle.test';
import keyboardCollapseToggleTest from './KeyboardCollapseToggle.test';
import predictionModelTest from './PredictionModel.test';

export default function testsuite() {
  localUnitTest();
  emojiPanelToggleTest();
  keyboardCollapseToggleTest();
  predictionModelTest();
}