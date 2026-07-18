import { describe, it, expect } from '@ohos/hypium';
import { abilityDelegatorRegistry } from '@kit.TestKit';
import { DictInstaller } from '../../../main/ets/InputMethodExtensionAbility/model/DictInstaller';
import { HoskeyEngine } from '../../../main/ets/InputMethodExtensionAbility/model/HoskeyEngine';

export default function NeuralModelNegativeE2ETest() {
  describe('NeuralModelNegativeE2ETest', () => {
    it('rejectsMalformedNnlmAndEveryRequiredRnntComponentOnDevice', 0,
      async () => {
        const delegator = abilityDelegatorRegistry.getAbilityDelegator();
        const dictDir: string = await DictInstaller.install(
          delegator.getAppContext());
        const engine: HoskeyEngine = new HoskeyEngine();
        const ok: boolean = await engine.openAsync(dictDir, {
          runModelNegativeAudit: true
        });
        const diagnostics: string = engine.getDiagnostics();
        const passed: boolean = ok &&
          diagnostics.includes('model_negative_audit={status=pass') &&
          diagnostics.includes('passed=26;total=26') &&
          diagnostics.includes('rnnt_missing=11/11') &&
          diagnostics.includes('ynnlm_missing=3/3') &&
          diagnostics.includes('rnnt_wrong_shape=covered') &&
          diagnostics.includes('ynnlm_wrong_shape=covered') &&
          diagnostics.includes('empty_nan_layout=covered') &&
          diagnostics.includes('malformed_path=covered');
        console.info('[HOSKEY_MODEL_NEGATIVE_AUDIT_E2E] result=' +
          (passed ? 'PASS' : 'FAIL') + ',diagnostics=' + diagnostics);
        expect(passed).assertTrue();
      });
  });
}
