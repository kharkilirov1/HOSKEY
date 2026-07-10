import { describe, it, expect } from '@ohos/hypium';

type LanguageCode = 'en' | 'ar' | 'it' | 'pl';

function getLanguageName(code: LanguageCode): string {
  switch (code) {
    case 'ar': return 'العربية';
    case 'en': return 'English';
    case 'it': return 'Italiano';
    case 'pl': return 'Polski';
  }
}

function spaceLabel(current: LanguageCode, available: LanguageCode[]): string {
  if (available && available.length > 1) {
    return getLanguageName(current);
  }
  return '';
}

export default function spaceLabelDisplayTest() {
  describe('SpaceLabelDisplay', () => {
    it('shows label when multiple languages enabled', 0, () => {
      const available: LanguageCode[] = ['en', 'ar'];
      const label = spaceLabel('ar', available);
      expect(label).assertEqual('العربية');
    });

    it('empty label when single language', 0, () => {
      const available: LanguageCode[] = ['en'];
      const label = spaceLabel('en', available);
      expect(label).assertEqual('');
    });
  });
}

