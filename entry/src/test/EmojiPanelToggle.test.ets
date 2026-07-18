import { describe, it, expect } from '@ohos/hypium';
import {
  applyEmojiSearchEdit,
  normalizeEmojiSearchQuery,
  selectEmojiPanelItems
} from '../main/ets/InputMethodExtensionAbility/model/EmojiSearchPresentation';

function computeColumns(screenWidthVp: number): number {
  const cell = 46;
  const cols = Math.floor((screenWidthVp - 16) / cell);
  return Math.max(6, Math.min(10, cols));
}

function toggle(current: boolean): boolean {
  return !current;
}

export default function emojiPanelToggleTest() {
  describe('EmojiPanelToggle', () => {
    it('toggle opens when closed', 0, () => {
      const next = toggle(false);
      expect(next).assertTrue();
    });

    it('toggle closes when open', 0, () => {
      const next = toggle(true);
      expect(next).assertFalse();
    });

    it('columns min at small width', 0, () => {
      const cols = computeColumns(280);
      expect(cols).assertEqual(6);
    });

    it('columns max at wide width', 0, () => {
      const cols = computeColumns(720);
      expect(cols).assertEqual(10);
    });

    it('normalizes emoji search whitespace without flattening lines', 0, () => {
      const query = normalizeEmojiSearchQuery('\uFEFF  КОТ\u00A0\t  кот\nдва  ');
      expect(query).assertEqual('КОТ кот\nдва');
    });

    it('uses only model output for a non-empty emoji query', 0, () => {
      const items = selectEmojiPanelItems('кот', ['😻', '🐈'], ['🙂', '🐈']);
      expect(items.length).assertEqual(2);
      expect(items[0]).assertEqual('😻');
      expect(items[1]).assertEqual('🐈');
    });

    it('uses the selected category when the emoji query is empty', 0, () => {
      const items = selectEmojiPanelItems('  ', ['😻'], ['🙂', '😊']);
      expect(items.length).assertEqual(2);
      expect(items[0]).assertEqual('🙂');
    });

    it('edits an emoji query locally without committing it to the editor', 0, () => {
      let query = applyEmojiSearchEdit('', 'char', 'к');
      query = applyEmojiSearchEdit(query, 'char', 'о');
      query = applyEmojiSearchEdit(query, 'char', 'т');
      query = applyEmojiSearchEdit(query, 'space');
      expect(query).assertEqual('кот ');
      expect(normalizeEmojiSearchQuery(query)).assertEqual('кот');
    });

    it('emoji query backspace removes one Unicode code point', 0, () => {
      expect(applyEmojiSearchEdit('кот', 'backspace')).assertEqual('ко');
      expect(applyEmojiSearchEdit('кот😻', 'backspace')).assertEqual('кот');
      expect(applyEmojiSearchEdit('', 'backspace')).assertEqual('');
    });
  });
}
