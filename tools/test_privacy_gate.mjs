import assert from 'node:assert/strict';
import { existsSync } from 'node:fs';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..');
const sourcePath = join(
  repoRoot,
  'entry',
  'src',
  'main',
  'ets',
  'InputMethodExtensionAbility',
  'model',
  'PrivacyGate.ets'
);

function extractMethod(source, marker) {
  const markerIndex = source.indexOf(marker);
  assert.notEqual(markerIndex, -1, `Method marker is missing: ${marker}`);
  const openBrace = source.indexOf('{', markerIndex);
  let depth = 0;
  for (let index = openBrace; index < source.length; index++) {
    if (source[index] === '{') depth++;
    if (source[index] === '}') depth--;
    if (depth === 0) return source.slice(openBrace + 1, index);
  }
  throw new Error(`Method body is unterminated: ${marker}`);
}

assert.ok(existsSync(sourcePath), `Privacy gate source is missing: ${sourcePath}`);

const tempRoot = await mkdtemp(join(tmpdir(), 'hoskey-privacy-gate-'));
try {
  const tempModule = join(tempRoot, 'PrivacyGate.ts');
  await writeFile(tempModule, await readFile(sourcePath, 'utf8'), 'utf8');
  const { PrivacyGate } = await import(pathToFileURL(tempModule).href);

  const passwordPatterns = [7, 8, 9, 11, 13];
  const gate = new PrivacyGate();

  assert.equal(gate.canProcessText(), false, 'unknown state must fail closed');
  assert.match(gate.getDiagnostics(), /reason=editor_unknown/);

  gate.updateSecurityMode(1, 0, 1);
  assert.equal(gate.canProcessText(), false, 'unknown editor must remain blocked');

  gate.updateEditorPattern(0, passwordPatterns);
  assert.equal(gate.canProcessText(), true, 'normal editor in FULL mode must be allowed');
  assert.match(gate.getDiagnostics(), /privacy_gate=allowed/);

  gate.updateEditorPattern(7, passwordPatterns);
  assert.equal(gate.canProcessText(), false, 'password editor must be blocked');
  assert.match(gate.getDiagnostics(), /reason=editor_sensitive/);

  gate.updateEditorPattern(0, passwordPatterns);
  gate.updateSecurityMode(0, 0, 1);
  assert.equal(gate.canProcessText(), false, 'BASIC security mode must be blocked');
  assert.match(gate.getDiagnostics(), /reason=security_basic/);

  gate.updateSecurityMode(99, 0, 1);
  assert.equal(gate.canProcessText(), false, 'unknown security enum must fail closed');
  assert.match(gate.getDiagnostics(), /security=unknown/);

  gate.resetEditorContext();
  assert.equal(gate.canProcessText(), false, 'new editor binding must fail closed');
  assert.match(gate.getDiagnostics(), /incognito=blocked:platform_signal_unproven/);

  assert.equal(
    gate.getDiagnostics().includes('hunter2'),
    false,
    'privacy diagnostics must never contain editor text'
  );

  const predictionSourcePath = join(
    repoRoot,
    'entry',
    'src',
    'main',
    'ets',
    'InputMethodExtensionAbility',
    'model',
    'PredictionModel.ets'
  );
  let predictionSource = await readFile(predictionSourcePath, 'utf8');
  predictionSource = predictionSource
    .replace("import { preferences } from '@kit.ArkData';", 'const preferences = {};')
    .replace("import { common } from '@kit.AbilityKit';", '')
    .replace("import { BusinessError } from '@kit.BasicServicesKit';", '')
    .replace("from './PrivacyGate';", "from './PrivacyGate.ts';");
  const predictionModule = join(tempRoot, 'PredictionModel.ts');
  await writeFile(predictionModule, predictionSource, 'utf8');
  const { PredictionModel } = await import(pathToFileURL(predictionModule).href);

  const modelGate = new PrivacyGate();
  modelGate.updateSecurityMode(1, 0, 1);
  modelGate.updateEditorPattern(0, passwordPatterns);
  const model = new PredictionModel(modelGate);
  const originalInfo = console.info;
  const capturedInfo = [];
  console.info = (...args) => capturedInfo.push(args.join(' '));
  try {
    model.recordWord('host-test', null, 'publicword');
    model.recordWord('host-test', null, 'publicword');
    model.recordWord('host-test', null, 'publicword');
    capturedInfo.length = 0;

    modelGate.updateEditorPattern(7, passwordPatterns);
    assert.deepEqual(model.predict('host-test', null, 'public', 3), []);
    model.recordWord('host-test', null, 'secretword');
    model.recordWord('host-test', null, 'secretword');
    model.recordWord('host-test', null, 'secretword');
    assert.equal(capturedInfo.some((line) => line.includes('secretword')), false);

    modelGate.updateEditorPattern(0, passwordPatterns);
    assert.deepEqual(model.predict('host-test', null, 'secret', 3), []);
    assert.equal(model.predict('host-test', null, 'public', 3)[0], 'publicword');
  } finally {
    console.info = originalInfo;
  }

  const controllerSourcePath = join(
    repoRoot,
    'entry',
    'src',
    'main',
    'ets',
    'InputMethodExtensionAbility',
    'model',
    'KeyboardController.ets'
  );
  let controllerSource = await readFile(controllerSourcePath, 'utf8');
  controllerSource = controllerSource
    .replace(
      "import { display } from '@kit.ArkUI';",
      "const display = { getDefaultDisplaySync: () => ({ width: 1000, height: 1000, id: 0 }) };"
    )
    .replace(
      "import { inputMethodEngine, InputMethodExtensionContext } from '@kit.IMEKit';",
      `const inputMethodEngine = {
        ENTER_KEY_TYPE_UNSPECIFIED: 0,
        PATTERN_PASSWORD: 7,
        PATTERN_PASSWORD_NUMBER: 8,
        PATTERN_PASSWORD_SCREEN_LOCK: 9,
        PATTERN_NEW_PASSWORD: 11,
        PATTERN_ONE_TIME_CODE: 13,
        SecurityMode: { BASIC: 0, FULL: 1 },
        ImmersiveMode: { NONE_IMMERSIVE: 0, DARK_IMMERSIVE: 1, IMMERSIVE: 2, LIGHT_IMMERSIVE: 3 },
        getInputMethodAbility: () => ({ on: () => {}, off: () => {}, getSecurityMode: () => 1 }),
        getKeyboardDelegate: () => ({ on: () => {}, off: () => {} })
      };`
    )
    .replace("import { BusinessError } from '@kit.BasicServicesKit';", '')
    .replace(
      "import { KeyAction, KeyData } from './KeyboardKeyData';",
      "const KeyAction = { Char: 1, Space: 2, Backspace: 3, Enter: 4 };"
    )
    .replace(
      /import \{\s*HoskeyEngine,[\s\S]*?\} from '\.\/HoskeyEngine';/,
      `class HoskeyEngine {
        static isAvailable() { return true; }
        isInitialized() { return true; }
        getSuggestionsByRequest() { throw new Error('unexpected native call'); }
        close() {}
        async openAsync() { return true; }
        getDiagnostics() { return ''; }
        getLoadedAssetKeys() { return []; }
        getLoadReports() { return []; }
      }`
    )
    .replace("import { DictInstaller } from './DictInstaller';", "const DictInstaller = { install: async () => '' };")
    .replace("import diagLog from './DiagLog';", "const diagLog = { init: () => {}, add: () => {} };")
    .replace("import { OpenBoardTheme } from './Theme';", 'const OpenBoardTheme = {};')
    .replace("from './PrivacyGate';", "from './PrivacyGate.ts';");
  const controllerModule = join(tempRoot, 'KeyboardController.ts');
  await writeFile(controllerModule, controllerSource, 'utf8');
  const { KeyboardController } = await import(pathToFileURL(controllerModule).href);
  const { default: sharedGate } = await import(pathToFileURL(tempModule).href);

  let inputPattern = 0;
  let surroundingReads = 0;
  let nativeCalls = 0;
  const inputClient = {
    getEditorAttribute: async () => ({ inputPattern, enterKeyType: 0 }),
    getForward: async () => {
      surroundingReads++;
      return 'normal-context';
    },
    insertText: () => {}
  };
  const controller = new KeyboardController();
  assert.equal(
    typeof controller.setPrivacyStateCallback,
    'function',
    'controller must propagate privacy transitions to the UI context owner'
  );
  const privacyTransitions = [];
  controller.setPrivacyStateCallback((restricted, diagnostics) => {
    privacyTransitions.push({ restricted, diagnostics });
  });
  controller.textInputClient = inputClient;
  controller.editorAttributeRevision++;
  controller.updatePrivacySecurityMode(1);
  await controller.refreshEditorAttribute();
  await controller.syncInitialBuffer();
  assert.equal(surroundingReads, 1, 'normal editor must engage the surrounding-text path');
  assert.equal(controller.getTextBeforeCursor(100), 'normal-context');

  controller.nativeEngine = {
    isInitialized: () => true,
    getSuggestionsByRequest: () => {
      nativeCalls++;
      return {
        ok: true,
        statusCode: 0,
        statusMessage: 'ok',
        suggestions: [],
        willAutocorrect: false,
        isBlocked: false,
        diagnostics: 'normal'
      };
    }
  };
  controller.getNativeSuggestResponse('normal');
  assert.equal(nativeCalls, 1, 'normal editor must engage the native model path');

  inputPattern = 7;
  controller.editorAttributeChangedCallback({ inputPattern, enterKeyType: 0 });
  assert.equal(sharedGate.canProcessText(), false);
  assert.equal(privacyTransitions.at(-1).restricted, true);
  assert.match(privacyTransitions.at(-1).diagnostics, /reason=editor_sensitive/);
  assert.equal(controller.getTextBeforeCursor(100), '');
  controller.insertText('hunter2');
  controller.recordNativeTouchPoint({ action: 1, mainValue: 'h' }, 1, 1, 'down');
  const blockedResponse = controller.getNativeSuggestResponse('hunter2');
  assert.equal(blockedResponse.isBlocked, true);
  assert.equal(surroundingReads, 1, 'password editor must not read surrounding text');
  assert.equal(nativeCalls, 1, 'password editor must not invoke the native model');
  assert.equal(controller.nativeTouchPoints.length, 0, 'password editor must not retain touch context');
  assert.equal(controller.getPrivacyDiagnostics().includes('hunter2'), false);

  const indexSource = await readFile(
    join(
      repoRoot,
      'entry',
      'src',
      'main',
      'ets',
      'InputMethodExtensionAbility',
      'pages',
      'Index.ets'
    ),
    'utf8'
  );
  const updateSuggestionsBody = extractMethod(indexSource, 'private updateSuggestions(): void');
  assert.ok(
    updateSuggestionsBody.indexOf('keyboardController.isPrivacyRestricted()') <
      updateSuggestionsBody.indexOf('this.getFallbackSuggestions'),
    'UI privacy guard must run before fallback prediction'
  );
  const onTextTypedBody = extractMethod(indexSource, 'private onTextTyped(text: string): void');
  assert.ok(
    onTextTypedBody.indexOf('keyboardController.isPrivacyRestricted()') <
      onTextTypedBody.indexOf('this.currentWord ='),
    'UI privacy guard must run before typed text is cached'
  );
  const onBoundaryTypedBody = extractMethod(indexSource, 'private onBoundaryTyped(boundary: string): void');
  assert.ok(
    onBoundaryTypedBody.indexOf('keyboardController.isPrivacyRestricted()') <
      onBoundaryTypedBody.indexOf('predictionModel.recordWord'),
    'UI privacy guard must run before learning'
  );
  const clearContextBody = extractMethod(indexSource, 'private clearPrivacyPredictionContext(): void');
  assert.match(clearContextBody, /this\.previousWord = '';/);
  assert.match(clearContextBody, /this\.currentWord = '';/);
  assert.match(clearContextBody, /this\.suggestions = \[\];/);
  assert.match(indexSource, /setPrivacyStateCallback\(\(restricted: boolean/);

  console.log('PrivacyGate + model/controller integration: all assertions passed');
} finally {
  await rm(tempRoot, { recursive: true, force: true });
}
