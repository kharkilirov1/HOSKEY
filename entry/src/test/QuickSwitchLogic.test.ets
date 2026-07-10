import { describe, it, expect } from '@ohos/hypium';

type LanguageCode = 'en' | 'ar' | 'it' | 'pl';
enum LayoutSwipeDirection { Left, Right }

function rotate(current: LanguageCode, available: LanguageCode[], dir: LayoutSwipeDirection): LanguageCode {
  if (available.length <= 1) return current;
  let idx = available.indexOf(current);
  if (idx < 0) idx = 0;
  if (dir === LayoutSwipeDirection.Left) {
    idx = (idx + 1) % available.length;
  } else {
    idx = (idx - 1 + available.length) % available.length;
  }
  return available[idx];
}

export default function quickSwitchLogicTest() {
  describe('QuickSwitchLogic', () => {
    it('rotate left cycles forward', 0, () => {
      const available: LanguageCode[] = ['en', 'ar', 'it'];
      const curr: LanguageCode = 'ar';
      const next = rotate(curr, available, LayoutSwipeDirection.Left);
      expect(next).assertEqual('it');
    });

    it('rotate right cycles backward', 0, () => {
      const available: LanguageCode[] = ['en', 'ar', 'it'];
      const curr: LanguageCode = 'ar';
      const next = rotate(curr, available, LayoutSwipeDirection.Right);
      expect(next).assertEqual('en');
    });

    it('no-op when only one language', 0, () => {
      const available: LanguageCode[] = ['en'];
      const curr: LanguageCode = 'en';
      const next = rotate(curr, available, LayoutSwipeDirection.Left);
      expect(next).assertEqual('en');
    });
  });
}

