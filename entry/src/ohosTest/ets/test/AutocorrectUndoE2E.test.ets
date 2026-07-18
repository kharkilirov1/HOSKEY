import { describe, it, expect } from '@ohos/hypium';
import { abilityDelegatorRegistry, Component, Driver, ON } from '@kit.TestKit';

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

async function findEditor(driver: Driver, scroll: Component): Promise<Component> {
  const center = await scroll.getBoundsCenter();
  for (let attempt: number = 0; attempt < 12; attempt++) {
    const inputs: Component[] = await driver.findComponents(ON.type('TextInput'));
    if (inputs !== null && inputs.length > 0) {
      return inputs[0];
    }
    await driver.swipe(center.x, center.y + 700, center.x, center.y - 700, 900);
    await driver.delayMs(250);
  }
  throw new Error('SetupGuide TextInput was not found after scrolling');
}

async function ensureRussianLayout(driver: Driver): Promise<Component> {
  const russian: Component[] = await driver.findComponents(ON.text('Russian'));
  if (russian !== null && russian.length > 0) {
    return russian[0];
  }

  const english: Component[] = await driver.findComponents(ON.text('English'));
  if (english === null || english.length === 0) {
    throw new Error('Neither Russian nor English HOSKEY layout is visible');
  }
  const center = await english[0].getBoundsCenter();
  await driver.swipe(center.x + 180, center.y, center.x - 180, center.y, 900);
  await driver.delayMs(900);
  const switched: Component[] = await driver.findComponents(ON.text('Russian'));
  if (switched === null || switched.length === 0) {
    throw new Error('HOSKEY layout did not switch to Russian');
  }
  return switched[0];
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

async function waitForPrefixSuggestion(driver: Driver, prefix: string,
  spaceY: number): Promise<boolean> {
  for (let attempt: number = 0; attempt < 45; attempt++) {
    const texts: Component[] = await driver.findComponents(ON.type('Text'));
    if (texts !== null) {
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
    }
    await driver.delayMs(1000);
  }
  return false;
}

async function readEditor(driver: Driver): Promise<string> {
  const editors: Component[] = await driver.findComponents(ON.type('TextInput'));
  if (editors === null || editors.length === 0) {
    return '';
  }
  return (await editors[0].getText()).toLowerCase();
}

async function waitForEditor(driver: Driver, expected: string): Promise<string> {
  let actual: string = '';
  for (let attempt: number = 0; attempt < 40; attempt++) {
    actual = await readEditor(driver);
    if (actual === expected) {
      return actual;
    }
    await driver.delayMs(250);
  }
  return actual;
}

export default function AutocorrectUndoE2ETest() {
  describe('AutocorrectUndoE2ETest', () => {
    it('undoesModelBackedAutocorrectOnceAndSuppressesRepeat', 0, async () => {
      const delegator = abilityDelegatorRegistry.getAbilityDelegator();
      await delegator.executeShellCommand(
        'aa start -b com.opentech.openboard -a EntryAbility -m entry');

      const driver: Driver = Driver.create();
      await driver.delayMs(3000);
      const scroll: Component = await findScroll(driver);
      const input: Component = await findEditor(driver, scroll);
      await input.clearText();
      await input.click();
      await driver.delayMs(1800);

      const space: Component = await ensureRussianLayout(driver);
      const spaceCenter = await space.getBoundsCenter();

      // Do not let native-engine startup race turn this into a fallback test.
      await clickKeyboardText(driver, 'п');
      expect(await waitForPrefixSuggestion(driver, 'п', spaceCenter.y)).assertTrue();
      await clickKeyboardText(driver, '⌫');

      for (const letter of ['п', 'р', 'е', 'в', 'е', 'т']) {
        await clickKeyboardText(driver, letter);
      }
      await space.click();

      const corrected: string = await waitForEditor(driver, 'привет ');
      expect(corrected).assertEqual('привет ');

      await clickKeyboardText(driver, '⌫');
      const undone: string = await waitForEditor(driver, 'превет');
      expect(undone).assertEqual('превет');

      await space.click();
      const recommitted: string = await waitForEditor(driver, 'превет ');
      console.info('[HOSKEY_AUTOCC_UNDO_E2E] corrected=' + corrected +
        ',undone=' + undone + ',recommitted=' + recommitted);
      expect(recommitted).assertEqual('превет ');
    });
  });
}
