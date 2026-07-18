import KeyboardKeyDataTest from './KeyboardKeyData.test'
import abilityTest from './Ability.test';
import AutocorrectUndoE2ETest from './AutocorrectUndoE2E.test';
import ContextEmojiE2ETest from './ContextEmojiE2E.test';
import EmojiSearchE2ETest from './EmojiSearchE2E.test';
import PredictionBackspaceE2ETest from './PredictionBackspaceE2E.test';
import PrivacyPasswordE2ETest from './PrivacyPasswordE2E.test';
import SwipeGestureE2ETest from './SwipeGestureE2E.test';
import NeuralModelNegativeE2ETest from './NeuralModelNegativeE2E.test';
import TapModelE2ETest from './TapModelE2E.test';

export default function testsuite() {
  abilityTest();
  KeyboardKeyDataTest()
  AutocorrectUndoE2ETest();
  ContextEmojiE2ETest();
  EmojiSearchE2ETest();
  PredictionBackspaceE2ETest();
  SwipeGestureE2ETest();
  NeuralModelNegativeE2ETest();
  TapModelE2ETest();
  // Password focus is a platform-level secure takeover and intentionally
  // leaves HOSKEY hidden, so keep this terminal in the shared device suite.
  PrivacyPasswordE2ETest();
}
