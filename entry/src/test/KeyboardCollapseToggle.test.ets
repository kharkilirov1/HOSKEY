import { describe, it, expect } from '@ohos/hypium';

function toggleCollapsed(current: boolean): boolean { return !current; }

export default function keyboardCollapseToggleTest() {
  describe('KeyboardCollapseToggle', () => {
    it('collapse when open', 0, () => {
      const next = toggleCollapsed(false);
      expect(next).assertTrue();
    });
    it('expand when collapsed', 0, () => {
      const next = toggleCollapsed(true);
      expect(next).assertFalse();
    });
  });
}