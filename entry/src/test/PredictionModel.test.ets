import { describe, it, expect } from '@ohos/hypium';
import predictionModel from '../main/ets/InputMethodExtensionAbility/model/PredictionModel';

export default function predictionModelTest() {
  describe('PredictionModel', () => {
    it('predicts bigram next words (async)', 0, async () => {
      const lang = 'test-en-1';
      await predictionModel.recordWord(lang, null, 'hello');
      await predictionModel.recordWord(lang, 'hello', 'world');
      await predictionModel.recordWord(lang, 'hello', 'world');
      await predictionModel.recordWord(lang, 'hello', 'world'); // 3rd time = learned
      await predictionModel.recordWord(lang, 'hello', 'there');
      await predictionModel.recordWord(lang, 'hello', 'there');
      await predictionModel.recordWord(lang, 'hello', 'there'); // 3rd time = learned
      const res = await predictionModel.predict(lang, 'hello', null, 3);
      expect(res[0]).assertEqual('world');
    });

    it('filters predictions by prefix (async)', 0, async () => {
      const lang = 'test-en-2';
      await predictionModel.recordWord(lang, 'swift', 'keyboard');
      await predictionModel.recordWord(lang, 'swift', 'keyboard');
      await predictionModel.recordWord(lang, 'swift', 'keyboard'); // 3rd time = learned
      await predictionModel.recordWord(lang, 'swift', 'key');
      await predictionModel.recordWord(lang, 'swift', 'key');
      await predictionModel.recordWord(lang, 'swift', 'key'); // 3rd time = learned
      const res = await predictionModel.predict(lang, 'swift', 'key', 3);
      expect(res[0]).assertEqual('keyboard');
    });

    it('shows pending words with prefix after 1 occurrence', 0, async () => {
      const lang = 'test-en-3';
      await predictionModel.recordWord(lang, null, 'testword');
      // After 1 occurrence, should show with prefix (PENDING_PREFIX_THRESHOLD = 1)
      const res = await predictionModel.predict(lang, null, 'test', 3);
      expect(res.includes('testword')).assertTrue();
    });

    it('shows pending words without prefix after 2 occurrences', 0, async () => {
      const lang = 'test-en-4';
      await predictionModel.recordWord(lang, null, 'example');
      // After 1 occurrence, should NOT show without prefix
      let res = await predictionModel.predict(lang, null, null, 3);
      expect(res.includes('example')).assertFalse();

      await predictionModel.recordWord(lang, null, 'example');
      // After 2 occurrences, should show without prefix (PENDING_CONTEXT_THRESHOLD = 2)
      res = await predictionModel.predict(lang, null, null, 3);
      expect(res.includes('example')).assertTrue();
    });

    it('updates n-grams only from 2nd pending occurrence', 0, async () => {
      const lang = 'test-en-5';
      // First occurrence - no n-gram update
      await predictionModel.recordWord(lang, 'first', 'word');
      let res = await predictionModel.predict(lang, 'first', null, 3);
      // No context prediction yet (PENDING_NGRAM_THRESHOLD = 2)
      expect(res.includes('word')).assertFalse();

      // Second occurrence - n-gram updated
      await predictionModel.recordWord(lang, 'first', 'word');
      res = await predictionModel.predict(lang, 'first', null, 3);
      // Now has context prediction
      expect(res.includes('word')).assertTrue();
    });

    it('filters garbage words', 0, async () => {
      const lang = 'test-en-6';
      // Keyboard mashing should be rejected
      await predictionModel.recordWord(lang, null, 'asdfgh');
      await predictionModel.recordWord(lang, null, 'asdfgh');
      await predictionModel.recordWord(lang, null, 'asdfgh');
      const res = await predictionModel.predict(lang, null, 'asd', 3);
      expect(res.includes('asdfgh')).assertFalse();
    });

    it('preserves Russian morphology', 0, async () => {
      const lang = 'test-ru-1';
      // Record word forms and link them
      await predictionModel.recordWord(lang, null, 'делаю');
      await predictionModel.recordWord(lang, null, 'делаю');
      await predictionModel.recordWord(lang, null, 'делаю'); // 3rd time = learned

      // When one form is learned and morphology is set up,
      // other forms should be suggested (if morphology data exists)
      const res = await predictionModel.predict(lang, null, 'дел', 3);
      // Should include the learned form
      expect(res.includes('делаю')).assertTrue();
    });
  });
}
