import { describe, it, expect } from '@ohos/hypium';
import { PredictionModel } from '../main/ets/InputMethodExtensionAbility/model/PredictionModel';
import { PrivacyGate } from '../main/ets/InputMethodExtensionAbility/model/PrivacyGate';

function createAllowedModel(): [PredictionModel, PrivacyGate] {
  const gate = new PrivacyGate();
  gate.updateSecurityMode(1, 0, 1);
  gate.updateEditorPattern(0, [7, 8, 9, 11, 13]);
  return [new PredictionModel(gate), gate];
}

export default function predictionModelTest() {
  describe('PredictionModel', () => {
    it('predicts bigram next words', 0, () => {
      const pair = createAllowedModel();
      const predictionModel = pair[0];
      const lang = 'test-en-1';
      predictionModel.recordWord(lang, null, 'hello');
      predictionModel.recordWord(lang, 'hello', 'world');
      predictionModel.recordWord(lang, 'hello', 'world');
      predictionModel.recordWord(lang, 'hello', 'world');
      predictionModel.recordWord(lang, 'hello', 'there');
      const res = predictionModel.predict(lang, 'hello', null, 3);
      expect(res[0]).assertEqual('world');
    });

    it('filters predictions by prefix', 0, () => {
      const pair = createAllowedModel();
      const predictionModel = pair[0];
      const lang = 'test-en-2';
      predictionModel.recordWord(lang, 'swift', 'keyboard');
      predictionModel.recordWord(lang, 'swift', 'keyboard');
      predictionModel.recordWord(lang, 'swift', 'keyboard');
      predictionModel.recordWord(lang, 'swift', 'key');
      const res = predictionModel.predict(lang, 'swift', 'key', 3);
      expect(res[0]).assertEqual('keyboard');
    });

    it('does not predict or learn while privacy gate is blocked', 0, () => {
      const pair = createAllowedModel();
      const predictionModel = pair[0];
      const gate = pair[1];
      const lang = 'test-private';

      predictionModel.recordWord(lang, null, 'publicword');
      predictionModel.recordWord(lang, null, 'publicword');
      predictionModel.recordWord(lang, null, 'publicword');
      gate.updateEditorPattern(7, [7, 8, 9, 11, 13]);
      expect(predictionModel.predict(lang, null, 'public', 3).length).assertEqual(0);

      predictionModel.recordWord(lang, null, 'secretword');
      predictionModel.recordWord(lang, null, 'secretword');
      predictionModel.recordWord(lang, null, 'secretword');
      gate.updateEditorPattern(0, [7, 8, 9, 11, 13]);
      expect(predictionModel.predict(lang, null, 'secret', 3).length).assertEqual(0);
    });
  });
}
