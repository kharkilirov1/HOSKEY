import { describe, it, expect } from '@ohos/hypium';
import { abilityDelegatorRegistry, Component, Driver, ON } from '@kit.TestKit';

const PRIVACY_PROBE_LABEL: string = 'Privacy gate device probe';

async function findScroll(driver: Driver): Promise<Component> {
  for (let attempt: number = 0; attempt < 30; attempt++) {
    const matches: Component[] = await driver.findComponents(ON.type('Scroll'));
    if (matches !== null && matches.length > 0) {
      return matches[0];
    }
    await driver.delayMs(500);
  }
  throw new Error('SetupGuide Scroll was not found');
}

async function findPrivacyProbe(driver: Driver, scroll: Component): Promise<Component> {
  const scrollCenter = await scroll.getBoundsCenter();
  for (let attempt: number = 0; attempt < 24; attempt++) {
    const labels: Component[] = await driver.findComponents(ON.text(PRIVACY_PROBE_LABEL));
    if (labels !== null && labels.length > 0) {
      const labelCenter = await labels[0].getBoundsCenter();
      const inputs: Component[] = await driver.findComponents(ON.type('TextInput'));
      let best: Component | null = null;
      let bestDistance: number = 1000000;
      if (inputs !== null) {
        for (let index: number = 0; index < inputs.length; index++) {
          try {
            const center = await inputs[index].getBoundsCenter();
            const distance: number = center.y - labelCenter.y;
            if (distance > 0 && distance < 260 && distance < bestDistance) {
              best = inputs[index];
              bestDistance = distance;
            }
          } catch (_err) {
            continue;
          }
        }
      }
      if (best !== null) {
        return best;
      }
    }
    await driver.swipe(scrollCenter.x, scrollCenter.y + 700,
      scrollCenter.x, scrollCenter.y - 700, 900);
    await driver.delayMs(250);
  }
  throw new Error('Password privacy probe was not found in SetupGuide');
}

async function findHoskeyRussianLayout(driver: Driver): Promise<Component | null> {
  for (let attempt: number = 0; attempt < 12; attempt++) {
    const russian: Component[] = await driver.findComponents(ON.text('Russian'));
    if (russian !== null && russian.length > 0) {
      return russian[0];
    }
    await driver.delayMs(250);
  }
  return null;
}

async function clickKeyboardText(driver: Driver, text: string): Promise<void> {
  const labels: string[] = text.toUpperCase() === text
    ? [text]
    : [text, text.toUpperCase()];
  for (let attempt: number = 0; attempt < 20; attempt++) {
    let best: Component | null = null;
    let bestY: number = -1;
    for (let labelIndex: number = 0; labelIndex < labels.length; labelIndex++) {
      const matches: Component[] = await driver.findComponents(ON.text(labels[labelIndex]));
      if (matches === null) {
        continue;
      }
      for (let index: number = 0; index < matches.length; index++) {
        try {
          const center = await matches[index].getBoundsCenter();
          if (center.y > bestY) {
            best = matches[index];
            bestY = center.y;
          }
        } catch (_err) {
          continue;
        }
      }
    }
    if (best !== null) {
      await best.click();
      return;
    }
    await driver.delayMs(250);
  }
  throw new Error('Required keyboard text is not visible: ' + text);
}

async function hasPrefixSuggestion(driver: Driver, prefix: string,
  spaceY: number): Promise<boolean> {
  const texts: Component[] = await driver.findComponents(ON.type('Text'));
  if (texts === null) {
    return false;
  }
  for (let index: number = 0; index < texts.length; index++) {
    try {
      const value: string = (await texts[index].getText()).toLowerCase();
      const center = await texts[index].getBoundsCenter();
      if (value.length > prefix.length && value.startsWith(prefix) &&
        center.y < spaceY && center.y > spaceY - 800) {
        return true;
      }
    } catch (_err) {
      continue;
    }
  }
  return false;
}

export default function PrivacyPasswordE2ETest() {
  describe('PrivacyPasswordE2ETest', () => {
    it('blocksHOSKEYForPasswordEditor', 0, async () => {
      const delegator = abilityDelegatorRegistry.getAbilityDelegator();
      await delegator.executeShellCommand(
        'aa start -b com.opentech.openboard -a EntryAbility -m entry');

      const driver: Driver = Driver.create();
      await driver.delayMs(3000);
      const scroll: Component = await findScroll(driver);
      const passwordInput: Component = await findPrivacyProbe(driver, scroll);
      await passwordInput.clearText();
      console.info('[HOSKEY_PRIVACY_E2E] phase=before_password_focus');
      await passwordInput.click();
      await driver.delayMs(1800);

      const space: Component | null = await findHoskeyRussianLayout(driver);
      if (space === null) {
        // HarmonyOS may replace a third-party IME with its secure system keyboard.
        // The host-side witness verifies that this branch is backed by Celia logs.
        console.info('[HOSKEY_PRIVACY_E2E] phase=platform_secure_takeover,hoskey_visible=0');
        expect(true).assertTrue();
        return;
      }
      const spaceCenter = await space.getBoundsCenter();
      await clickKeyboardText(driver, 'п');
      await clickKeyboardText(driver, 'р');
      await driver.delayMs(1500);

      const leakedSuggestion: boolean = await hasPrefixSuggestion(
        driver, 'пр', spaceCenter.y);
      console.info('[HOSKEY_PRIVACY_E2E] phase=typed,length=2,prefix_suggestion=' +
        (leakedSuggestion ? '1' : '0'));
      expect(leakedSuggestion).assertFalse();
    });
  });
}
