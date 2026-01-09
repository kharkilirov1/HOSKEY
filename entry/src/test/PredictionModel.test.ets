import { describe, it, expect } from '@ohos/hypium';
import predictionModel from '../main/ets/InputMethodExtensionAbility/model/PredictionModel';

export default function predictionModelTest() {
  describe('PredictionModel', () => {
    it('predicts bigram next words', 0, async () => {
      const lang = 'test-en-1';
      // Record words multiple times to reach LEARN_THRESHOLD
      predictionModel.recordWord(lang, null, 'hello');
      predictionModel.recordWord(lang, null, 'hello');
      predictionModel.recordWord(lang, null, 'hello');
      predictionModel.recordWord(lang, 'hello', 'world');
      predictionModel.recordWord(lang, 'hello', 'world');
      predictionModel.recordWord(lang, 'hello', 'world');
      predictionModel.recordWord(lang, 'hello', 'there');
      predictionModel.recordWord(lang, 'hello', 'there');
      predictionModel.recordWord(lang, 'hello', 'there');

      const res = await predictionModel.predict(lang, 'hello', null, 3);
      expect(res[0]).assertEqual('world');
    });

    it('filters predictions by prefix', 0, async () => {
      const lang = 'test-en-2';
      // Record words multiple times
      predictionModel.recordWord(lang, 'swift', 'keyboard');
      predictionModel.recordWord(lang, 'swift', 'keyboard');
      predictionModel.recordWord(lang, 'swift', 'keyboard');
      predictionModel.recordWord(lang, 'swift', 'key');
      predictionModel.recordWord(lang, 'swift', 'key');
      predictionModel.recordWord(lang, 'swift', 'key');

      const res = await predictionModel.predict(lang, 'swift', 'key', 3);
      expect(res[0]).assertEqual('keyboard');
    });

    it('shows pending words after 1 occurrence with prefix', 0, async () => {
      const lang = 'test-en-3';
      // Type word only ONCE (below LEARN_THRESHOLD=3)
      predictionModel.recordWord(lang, null, 'testing');

      // Should appear in suggestions due to PENDING_SUGGEST_THRESHOLD=1
      const res = await predictionModel.predict(lang, null, 'test', 3);
      expect(res).assertContain('testing');
    });

    it('context predictions work for pending words', 0, async () => {
      const lang = 'test-en-4';
      // Type bigram only TWICE (below LEARN_THRESHOLD)
      predictionModel.recordWord(lang, 'hello', 'friend');
      predictionModel.recordWord(lang, 'hello', 'friend');

      // Bigram should work even though 'friend' is pending
      const res = await predictionModel.predict(lang, 'hello', null, 3);
      expect(res).assertContain('friend');
    });

    it('works without BaseLexicon', 0, async () => {
      const lang = 'test-en-5'; // No BaseLexicon for 'test-*' languages
      // Record some words
      predictionModel.recordWord(lang, null, 'apple');
      predictionModel.recordWord(lang, null, 'apple');
      predictionModel.recordWord(lang, null, 'apple');

      // Should still get predictions from user model
      const res = await predictionModel.predict(lang, null, 'app', 3);
      expect(res[0]).assertEqual('apple');
    });
  });
}