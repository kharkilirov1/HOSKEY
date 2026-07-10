import { describe, it, expect } from '@ohos/hypium';

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
  });
}