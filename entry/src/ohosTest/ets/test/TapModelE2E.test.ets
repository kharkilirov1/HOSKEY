import { describe, it, expect } from '@ohos/hypium';
import {
  abilityDelegatorRegistry, Component, Driver, ON, PointerMatrix
} from '@kit.TestKit';

interface TapPoint {
  x: number;
  y: number;
}

async function findScroll(driver: Driver): Promise<Component> {
  for (let attempt: number = 0; attempt < 30; attempt++) {
    const matches: Component[] = await driver.findComponents(ON.type('Scroll'));
    if (matches !== null && matches.length > 0) return matches[0];
    await driver.delayMs(500);
  }
  throw new Error('SetupGuide Scroll was not found');
}

async function findEditor(driver: Driver, scroll: Component): Promise<Component> {
  const center = await scroll.getBoundsCenter();
  for (let attempt: number = 0; attempt < 12; attempt++) {
    const inputs: Component[] = await driver.findComponents(ON.type('TextInput'));
    if (inputs !== null && inputs.length > 0) return inputs[0];
    await driver.swipe(center.x, center.y + 700, center.x, center.y - 700, 900);
    await driver.delayMs(250);
  }
  throw new Error('SetupGuide TextInput was not found after scrolling');
}

async function ensureRussianLayout(driver: Driver): Promise<Component> {
  const russian: Component[] = await driver.findComponents(ON.text('Russian'));
  if (russian !== null && russian.length > 0) return russian[0];
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

async function findKeyboardComponent(driver: Driver, text: string): Promise<Component> {
  const labels: string[] = text.toUpperCase() === text
    ? [text] : [text, text.toUpperCase()];
  for (let attempt: number = 0; attempt < 20; attempt++) {
    let best: Component | null = null;
    let bestY: number = -1;
    for (let labelIndex: number = 0; labelIndex < labels.length; labelIndex++) {
      const matches: Component[] = await driver.findComponents(ON.text(labels[labelIndex]));
      if (matches === null) continue;
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
    if (best !== null) return best;
    await driver.delayMs(250);
  }
  throw new Error('Required keyboard text is not visible: ' + text);
}

async function clickKeyboardText(driver: Driver, text: string): Promise<void> {
  await (await findKeyboardComponent(driver, text)).click();
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
            center.y < spaceY && center.y > spaceY - 800) return true;
        } catch (_err) {
          continue;
        }
      }
    }
    await driver.delayMs(1000);
  }
  return false;
}

async function waitForEditorText(editor: Component, expected: string): Promise<string> {
  let actual: string = '';
  for (let attempt: number = 0; attempt < 40; attempt++) {
    actual = (await editor.getText()).toLowerCase();
    if (actual === expected) return actual;
    await new Promise<void>((resolve: () => void): void => {
      setTimeout(resolve, 250);
    });
  }
  return actual;
}

function routeField(line: string, key: string): string {
  const prefix: string = key + '=';
  const parts: string[] = line.split(',');
  for (const part of parts) {
    const trimmed: string = part.trim();
    if (trimmed.startsWith(prefix)) return trimmed.substring(prefix.length);
  }
  return '';
}

export default function TapModelE2ETest() {
  describe('TapModelE2ETest', () => {
    it('runsRankerClassifierForCenterAndRealBoundaryTap', 0, async () => {
      const delegator = abilityDelegatorRegistry.getAbilityDelegator();
      await delegator.executeShellCommand(
        'aa start -b com.opentech.openboard -a EntryAbility -m entry');
      const driver: Driver = Driver.create();
      await driver.delayMs(3000);

      const scroll: Component = await findScroll(driver);
      const editor: Component = await findEditor(driver, scroll);
      await editor.clearText();
      await editor.click();
      await driver.delayMs(1800);
      const russianSpace: Component = await ensureRussianLayout(driver);
      const spaceCenter = await russianSpace.getBoundsCenter();
      await clickKeyboardText(driver, 'п');
      expect(await waitForPrefixSuggestion(driver, 'п', spaceCenter.y)).assertTrue();
      await clickKeyboardText(driver, '⌫');
      await driver.delayMs(400);

      await delegator.executeShellCommand('hilog -r');
      const typo: string[] = ['п', 'р', 'и', 'в', 'н', 'т'];
      for (const key of typo) await clickKeyboardText(driver, key);
      expect(await waitForEditorText(editor, 'привнт')).assertEqual('привнт');
      await driver.delayMs(1200);
      const centerShell = await delegator.executeShellCommand(
        "hilog -x | grep 'HOSKEY_TAP_ROUTE' | grep 'path=ranker_classifier' | " +
        "grep -v 'AATool' | tail -n 1");
      const centerLine: string = centerShell.stdResult;
      const centerRank: number = Number(routeField(centerLine, 'rank'));
      expect(centerLine.includes('path=ranker_classifier')).assertTrue();
      expect(routeField(centerLine, 'touch_seq')).assertEqual('6');
      expect(Number(routeField(centerLine, 'pts')) >= 6).assertTrue();

      for (let index: number = 0; index < typo.length; index++) {
        await clickKeyboardText(driver, '⌫');
      }
      expect(await waitForEditorText(editor, '')).assertEqual('');
      await delegator.executeShellCommand('hilog -r');

      for (const key of ['п', 'р', 'и', 'в']) {
        await clickKeyboardText(driver, key);
      }
      const nCenter = await (await findKeyboardComponent(driver, 'н'))
        .getBoundsCenter();
      const gCenter = await (await findKeyboardComponent(driver, 'г'))
        .getBoundsCenter();
      const edge: TapPoint = {
        x: Math.round(nCenter.x + (gCenter.x - nCenter.x) * 0.43),
        y: Math.round(nCenter.y + (gCenter.y - nCenter.y) * 0.43)
      };
      const tap: PointerMatrix = PointerMatrix.create(1, 2);
      tap.setPoint(0, 0, edge);
      tap.setPoint(0, 1, edge);
      expect(await driver.injectMultiPointerAction(tap, 120)).assertTrue();
      await clickKeyboardText(driver, 'т');
      const edgeText: string = await waitForEditorText(editor, 'привнт');
      await driver.delayMs(1200);
      const edgeShell = await delegator.executeShellCommand(
        "hilog -x | grep 'HOSKEY_TAP_ROUTE' | grep 'path=ranker_classifier' | " +
        "grep -v 'AATool' | tail -n 1");
      const edgeLine: string = edgeShell.stdResult;
      const edgeRank: number = Number(routeField(edgeLine, 'rank'));

      console.info('[HOSKEY_TAP_MODEL_E2E] center_rank=' + centerRank +
        ',edge_rank=' + edgeRank + ',edge_editor=' + edgeText);
      expect(edgeText).assertEqual('привнт');
      expect(edgeLine.includes('path=ranker_classifier')).assertTrue();
      expect(routeField(edgeLine, 'touch_seq')).assertEqual('6');
      expect(Number(routeField(edgeLine, 'pts')) >= 6).assertTrue();
      expect(Number.isFinite(centerRank) && Number.isFinite(edgeRank)).assertTrue();
      expect(Math.abs(centerRank - edgeRank) > 0.000001).assertTrue();
    });
  });
}
