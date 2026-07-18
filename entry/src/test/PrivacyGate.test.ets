import { describe, it, expect } from '@ohos/hypium';
import { PrivacyGate } from '../main/ets/InputMethodExtensionAbility/model/PrivacyGate';

export default function privacyGateTest() {
  describe('PrivacyGate', () => {
    it('fails closed until editor and security signals are known', 0, () => {
      const gate = new PrivacyGate();
      gate.updateSecurityMode(1, 0, 1);
      expect(gate.canProcessText()).assertFalse();
      gate.updateEditorPattern(0, [7, 8, 9, 11, 13]);
      expect(gate.canProcessText()).assertTrue();
    });

    it('blocks password and BASIC security contexts', 0, () => {
      const gate = new PrivacyGate();
      gate.updateSecurityMode(1, 0, 1);
      gate.updateEditorPattern(7, [7, 8, 9, 11, 13]);
      expect(gate.canProcessText()).assertFalse();
      gate.updateEditorPattern(0, [7, 8, 9, 11, 13]);
      gate.updateSecurityMode(0, 0, 1);
      expect(gate.canProcessText()).assertFalse();
    });

    it('reports the unproven incognito signal without text', 0, () => {
      const gate = new PrivacyGate();
      const diagnostics = gate.getDiagnostics();
      expect(diagnostics.includes('incognito=blocked:platform_signal_unproven')).assertTrue();
      expect(diagnostics.includes('hunter2')).assertFalse();
    });
  });
}
