import { describe, it, expect } from '@ohos/hypium';
import predictionModel from '../main/ets/InputMethodExtensionAbility/model/PredictionModel';

export default function predictionModelTest() {
  describe('PredictionModel', () => {
    it('predicts bigram next words', 0, () => {
      const lang = 'test-en-1';
      predictionModel.recordWord(lang, null, 'hello');
      predictionModel.recordWord(lang, 'hello', 'world');
      predictionModel.recordWord(lang, 'hello', 'world');
      predictionModel.recordWord(lang, 'hello', 'there');
      const res = predictionModel.predict(lang, 'hello', null, 3);
      expect(res[0]).assertEqual('world');
    });

    it('filters predictions by prefix', 0, () => {
      const lang = 'test-en-2';
      predictionModel.recordWord(lang, 'swift', 'keyboard');
      predictionModel.recordWord(lang, 'swift', 'key');
      const res = predictionModel.predict(lang, 'swift', 'key', 3);
      expect(res[0]).assertEqual('keyboard');
    });
  });
}