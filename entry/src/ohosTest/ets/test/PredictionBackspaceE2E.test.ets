import { describe, it, expect } from '@ohos/hypium';
import { abilityDelegatorRegistry, Component, Driver, ON } from '@kit.TestKit';

async function ensureRussianLayout(driver: Driver): Promise<Component> {
  const russianMatches: Component[] = await driver.findComponents(ON.text('Russian'));
  if (russianMatches !== null && russianMatches.length > 0) {
    return russianMatches[0];
  }

  const englishMatches: Component[] = await driver.findComponents(ON.text('English'));
  if (englishMatches === null || englishMatches.length === 0) {
    throw new Error('Neither Russian nor English HOSKEY layout is visible');
  }
  const englishSpace: Component = englishMatches[0];
  const spaceCenter = await englishSpace.getBoundsCenter();
  await driver.swipe(spaceCenter.x + 180, spaceCenter.y,
    spaceCenter.x - 180, spaceCenter.y, 900);
  await driver.delayMs(900);

  const switchedMatches: Component[] = await driver.findComponents(ON.text('Russian'));
  if (switchedMatches === null || switchedMatches.length === 0) {
    throw new Error('HOSKEY layout did not switch to Russian');
  }
  return switchedMatches[0];
}

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
  const scrollCenter = await scroll.getBoundsCenter();
  for (let attempt: number = 0; attempt < 12; attempt++) {
    const inputs: Component[] = await driver.findComponents(ON.type('TextInput'));
    if (inputs !== null && inputs.length > 0) {
      return inputs[0];
    }
    await driver.swipe(scrollCenter.x, scrollCenter.y + 700,
      scrollCenter.x, scrollCenter.y - 700, 900);
    await driver.delayMs(250);
  }
  throw new Error('SetupGuide TextInput was not found after scrolling');
}

async function clickVisibleText(driver: Driver, label: string): Promise<void> {
  const matches: Component[] = await driver.findComponents(ON.text(label));
  if (matches === null || matches.length === 0) {
    throw new Error('Required keyboard key is not visible: ' + label);
  }
  await matches[0].click();
}

async function findPrefixSuggestion(driver: Driver, prefix: string,
  spaceCenterY: number): Promise<string> {
  const textComponents: Component[] = await driver.findComponents(ON.type('Text'));
  if (textComponents === null) {
    return '';
  }
  for (let index: number = 0; index < textComponents.length; index++) {
    try {
      const text: string = await textComponents[index].getText();
      if (text.length > prefix.length && text.toLowerCase().startsWith(prefix)) {
        const center = await textComponents[index].getBoundsCenter();
        console.info('[HOSKEY_PREDICTION_BACKSPACE_CANDIDATE] text=' + text +
          ',y=' + center.y + ',space_y=' + spaceCenterY);
        if (center.y < spaceCenterY && center.y > spaceCenterY - 800) {
          return text;
        }
      }
    } catch (_err) {
      // Native-engine readiness refresh rebuilds the strip while polling; a
      // stale Component is expected, so reacquire the UI tree next iteration.
      continue;
    }
  }
  return '';
}

async function waitForPrefixSuggestion(driver: Driver, prefix: string,
  spaceCenterY: number): Promise<string> {
  for (let attempt: number = 0; attempt < 45; attempt++) {
    const suggestion: string = await findPrefixSuggestion(driver, prefix, spaceCenterY);
    if (suggestion.length > 0) {
      return suggestion;
    }
    await driver.delayMs(1000);
  }
  return '';
}

async function waitForEditorText(driver: Driver, originalEditor: Component,
  scroll: Component): Promise<string> {
  let lastText: string = '';
  for (let attempt: number = 0; attempt < 20; attempt++) {
    try {
      lastText = (await originalEditor.getText()).toLowerCase();
      if (lastText.length > 0) {
        return lastText;
      }
    } catch (_err) {
      // The editor can temporarily disappear from a fresh UI snapshot while
      // the keyboard candidate strip is rebuilt. Reacquire it below.
    }

    try {
      const refreshedEditor: Component = await findEditor(driver, scroll);
      lastText = (await refreshedEditor.getText()).toLowerCase();
      if (lastText.length > 0) {
        return lastText;
      }
    } catch (_err) {
      // The async diagnostics can reset the Scroll position. findEditor()
      // scrolls the state-bound probe back into view before the next retry.
    }
    await driver.delayMs(250);
  }
  return lastText;
}

export default function PredictionBackspaceE2ETest() {
  describe('PredictionBackspaceE2ETest', () => {
    it('keeps the restored prefix after deleting a space', 0, async () => {
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

      const russianSpace: Component = await ensureRussianLayout(driver);
      const spaceCenter = await russianSpace.getBoundsCenter();
      // Type while the native engine may still be loading. The ready callback
      // must refresh this already-entered prefix instead of waiting for a new key.
      await clickVisibleText(driver, '\u043f');
      await russianSpace.click();
      await clickVisibleText(driver, '\u232b');
      await clickVisibleText(driver, '\u0440');
      const suggestion: string = await waitForPrefixSuggestion(
        driver, '\u043f\u0440', spaceCenter.y);

      const committed: string = await waitForEditorText(driver, input, scroll);
      console.info('[HOSKEY_PREDICTION_BACKSPACE_E2E] editor=' + committed +
        ',prefix_suggestion=' + suggestion);
      expect(committed).assertEqual('\u043f\u0440');
      expect(suggestion.length > 0).assertTrue();
    });
  });
}
