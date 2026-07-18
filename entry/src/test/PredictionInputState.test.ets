import { describe, it, expect } from '@ohos/hypium';
import {
  derivePredictionInputState,
  PredictionInputState
} from '../main/ets/InputMethodExtensionAbility/model/PredictionInputState';

export default function predictionInputStateTest() {
  describe('PredictionInputState', () => {
    it('restores the current prefix after deleting a word boundary', 0, () => {
      const afterSpace: PredictionInputState = derivePredictionInputState('\u043f ');
      const restored: PredictionInputState = derivePredictionInputState('\u043f');

      expect(afterSpace.previousWord).assertEqual('\u043f');
      expect(afterSpace.currentWord).assertEqual('');
      expect(restored.previousWord).assertEqual('');
      expect(restored.currentWord).assertEqual('\u043f');
      expect(restored.currentWord + '\u0440').assertEqual('\u043f\u0440');
    });
  });
}
