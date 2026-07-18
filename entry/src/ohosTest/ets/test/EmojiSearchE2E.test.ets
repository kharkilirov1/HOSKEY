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

async function findEmojiPanelKey(driver: Driver, spaceX: number,
  spaceY: number): Promise<Component> {
  for (let attempt: number = 0; attempt < 20; attempt++) {
    const images: Component[] = await driver.findComponents(ON.type('Image'));
    let best: Component | null = null;
    let bestDistance: number = 1000000;
    if (images !== null) {
      for (let index: number = 0; index < images.length; index++) {
        try {
          const center = await images[index].getBoundsCenter();
          const yDistance: number = Math.abs(center.y - spaceY);
          if (center.x < spaceX && yDistance < 120 && yDistance < bestDistance) {
            best = images[index];
            bestDistance = yDistance;
          }
        } catch (_err) {
          continue;
        }
      }
    }
    if (best !== null) {
      return best;
    }
    await driver.delayMs(250);
  }
  throw new Error('Emoji panel image key was not found beside the space key');
}

async function findBottomTextInput(driver: Driver): Promise<Component> {
  for (let attempt: number = 0; attempt < 20; attempt++) {
    const inputs: Component[] = await driver.findComponents(ON.type('TextInput'));
    let best: Component | null = null;
    let bestY: number = -1;
    if (inputs !== null) {
      for (let index: number = 0; index < inputs.length; index++) {
        try {
          const center = await inputs[index].getBoundsCenter();
          if (center.y > bestY) {
            best = inputs[index];
            bestY = center.y;
          }
        } catch (_err) {
          continue;
        }
      }
    }
    if (best !== null) {
      return best;
    }
    await driver.delayMs(250);
  }
  throw new Error('Emoji search TextInput was not found');
}

async function waitForEmojiResult(driver: Driver, emoji: string): Promise<Component | null> {
  for (let attempt: number = 0; attempt < 30; attempt++) {
    const matches: Component[] = await driver.findComponents(ON.text(emoji));
    if (matches !== null && matches.length > 0) {
      return matches[0];
    }
    await driver.delayMs(500);
  }
  return null;
}

async function readTopTextInput(driver: Driver): Promise<string> {
  const inputs: Component[] = await driver.findComponents(ON.type('TextInput'));
  let value: string = '';
  let bestY: number = 1000000;
  if (inputs !== null) {
    for (let index: number = 0; index < inputs.length; index++) {
      try {
        const center = await inputs[index].getBoundsCenter();
        if (center.y < bestY) {
          bestY = center.y;
          value = await inputs[index].getText();
        }
      } catch (_err) {
        continue;
      }
    }
  }
  return value;
}

export default function EmojiSearchE2ETest() {
  describe('EmojiSearchE2ETest', () => {
    it('searchesWithNativeModelAndCommitsResult', 0, async () => {
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

      // A visible completion proves the installed native engine is ready.
      await clickKeyboardText(driver, 'п');
      expect(await waitForPrefixSuggestion(driver, 'п', spaceCenter.y)).assertTrue();
      await clickKeyboardText(driver, '⌫');

      const emojiKey: Component = await findEmojiPanelKey(
        driver, spaceCenter.x, spaceCenter.y);
      await emojiKey.click();
      await driver.delayMs(750);

      const searchInput: Component = await findBottomTextInput(driver);
      await searchInput.click();
      await driver.delayMs(500);
      for (const letter of ['к', 'о', 'т']) {
        await clickKeyboardText(driver, letter);
      }

      // The same panel key exits query editing and exposes model results.
      const editingSpace: Component = (await driver.findComponents(ON.text('Russian')))[0];
      const editingSpaceCenter = await editingSpace.getBoundsCenter();
      const editingEmojiKey: Component = await findEmojiPanelKey(
        driver, editingSpaceCenter.x, editingSpaceCenter.y);
      await editingEmojiKey.click();

      const result: Component | null = await waitForEmojiResult(driver, '😻');
      expect(result !== null).assertTrue();
      if (result !== null) {
        await result.click();
      }
      await driver.delayMs(750);

      const committed: string = await readTopTextInput(driver);
      console.info('[HOSKEY_EMOJI_SEARCH_E2E] query_length=3,result=😻' +
        ',committed=' + (committed.includes('😻') ? '1' : '0'));
      expect(committed.includes('😻')).assertTrue();
    });
  });
}
