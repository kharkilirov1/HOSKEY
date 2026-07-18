import { describe, it, expect } from '@ohos/hypium';
import { abilityDelegatorRegistry, Component, Driver, ON, PointerMatrix } from '@kit.TestKit';

interface GesturePoint {
  x: number;
  y: number;
}

function appendSegment(points: GesturePoint[], from: GesturePoint, to: GesturePoint): void {
  const steps: number = 4;
  for (let step: number = 1; step <= steps; step++) {
    const ratio: number = step / steps;
    points.push({
      x: Math.round(from.x + (to.x - from.x) * ratio),
      y: Math.round(from.y + (to.y - from.y) * ratio)
    });
  }
}

async function findKeyCenter(driver: Driver, label: string): Promise<GesturePoint> {
  const labels: string[] = [label];
  const upperLabel: string = label.toUpperCase();
  if (upperLabel !== label) {
    labels.push(upperLabel);
  }
  for (let attempt: number = 0; attempt < 20; attempt++) {
    for (let labelIndex: number = 0; labelIndex < labels.length; labelIndex++) {
      const matches: Component[] = await driver.findComponents(ON.text(labels[labelIndex]));
      if (matches !== null && matches.length > 0) {
        try {
          const center = await matches[0].getBoundsCenter();
          return { x: center.x, y: center.y };
        } catch (_err) {
          // The candidate strip is rebuilt after the readiness probe/backspace.
          // Retry when the component snapshot becomes stale during that rebuild.
        }
      }
    }
    await driver.delayMs(250);
  }
  throw new Error('Required keyboard key is not visible: ' + label);
}

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

async function waitForPrefixSuggestion(driver: Driver, prefix: string,
  spaceCenterY: number): Promise<string> {
  for (let attempt: number = 0; attempt < 45; attempt++) {
    const textComponents: Component[] = await driver.findComponents(ON.type('Text'));
    if (textComponents !== null) {
      for (let index: number = 0; index < textComponents.length; index++) {
        try {
          const text: string = await textComponents[index].getText();
          if (text.length > prefix.length && text.toLowerCase().startsWith(prefix)) {
            const center = await textComponents[index].getBoundsCenter();
            if (center.y < spaceCenterY && center.y > spaceCenterY - 800) {
              return text;
            }
          }
        } catch (_err) {
          continue;
        }
      }
    }
    await driver.delayMs(1000);
  }
  return '';
}

export default function SwipeGestureE2ETest() {
  describe('SwipeGestureE2ETest', () => {
    it('commitsModelBackedRussianSwipeIntoEditor', 0, async () => {
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
      expect(await russianSpace.getText()).assertEqual('Russian');
      const spaceCenter = await russianSpace.getBoundsCenter();
      // Probe functional readiness instead of sleeping for an assumed load time.
      // Backspace clears the probe and its touch trace before the swipe itself.
      await clickVisibleText(driver, '\u043f');
      const readySuggestion: string = await waitForPrefixSuggestion(
        driver, '\u043f', spaceCenter.y);
      expect(readySuggestion.length > 0).assertTrue();
      await clickVisibleText(driver, '\u232b');
      await driver.delayMs(500);

      const labels: string[] = ['п', 'р', 'и', 'в', 'е', 'т'];
      const keyCenters: GesturePoint[] = [];
      for (let index: number = 0; index < labels.length; index++) {
        keyCenters.push(await findKeyCenter(driver, labels[index]));
      }

      const gesture: GesturePoint[] = [keyCenters[0]];
      for (let index: number = 1; index < keyCenters.length; index++) {
        appendSegment(gesture, keyCenters[index - 1], keyCenters[index]);
      }

      const pointers: PointerMatrix = PointerMatrix.create(1, gesture.length);
      for (let step: number = 0; step < gesture.length; step++) {
        pointers.setPoint(0, step, gesture[step]);
      }

      const injected: boolean = await driver.injectMultiPointerAction(pointers, 900);
      expect(injected).assertTrue();
      await driver.delayMs(2500);

      const editor: Component = await driver.findComponent(ON.type('TextInput'));
      const committed: string = await editor.getText();
      console.info('[HOSKEY_SWIPE_E2E] committed_length=' + committed.length);
      expect(committed.toLowerCase()).assertContain('привет');
    })
  })
}
