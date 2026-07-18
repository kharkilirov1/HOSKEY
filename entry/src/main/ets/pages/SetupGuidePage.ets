import { OpenBoardTheme } from '../InputMethodExtensionAbility/model/Theme';
import {
  HoskeyEngine,
  NativeEmojiSuggestCandidate,
  NativeEmojiSuggestResponse,
  NativePrevWordInfo,
  NativeSuggestRequest,
  NativeSuggestResponse,
  NativeSuggestion,
  NativeTouchPoint
} from '../InputMethodExtensionAbility/model/HoskeyEngine';
import { DictInstaller } from '../InputMethodExtensionAbility/model/DictInstaller';
import { common } from '@kit.AbilityKit';
import pasteboard from '@ohos.pasteboard';

const VERIFIED_SPLIT_PREFIXES: string[] = [
  'алв', 'яу', 'кыш', 'эрк', 'бои', 'авив', 'анч', 'аня', 'ася', 'боно'
];

@Entry
@Component
struct SetupGuidePage {
  @State logLines: string[] = [];
  @State nativeStatus: string = 'Запуск диагностики...';
  @State statusColor: string = '#FFA500';
  @State testResult: string = '';
  @State predictorModeLabel: string = 'baseline';
  @State compareToken: string = 'алв';
  @State compareResult: string = '';
  @State compareRunning: boolean = false;
  @State negativeAuditRunning: boolean = false;
  @State negativeAuditResult: string = '';
  @State showLogPanel: boolean = false;
  @State installedDictDir: string = '';
  private engine: HoskeyEngine = new HoskeyEngine();

  aboutToAppear(): void {
    this.runDiagnostic();
  }

  private log(msg: string): void {
    const ts = new Date().toISOString().slice(11, 23);
    this.logLines = [...this.logLines, ts + ' ' + msg];
  }

  private async yieldToUi(): Promise<void> {
    await new Promise<void>((resolve: () => void): void => {
      setTimeout(resolve, 16);
    });
  }

  private buildYnnlmProbeRequest(contextTokens: string[], disableYnnlm: boolean): NativeSuggestRequest {
    return {
      token: 'пр',
      input: 'пр',
      previousTokens: contextTokens,
      prevWordsInfo: contextTokens.map((token: string): NativePrevWordInfo => {
        return { token, wasAutocorrected: false };
      }),
      locale: 'ru-RU',
      wantAutocorrect: true,
      maxSuggestions: 6,
      dictionarySessions: [{ id: 'hoskey-default', locale: 'ru-RU' }],
      packageName: 'com.opentech.openboard',
      disableYnnlm
    };
  }

  private buildSyntheticSwipeProbePoints(): NativeTouchPoint[] {
    // Same deterministic "привет" geometry as tools/engine_acceptance.cpp.
    const centers: number[][] = [
      [409.09, 195.0], [500.0, 195.0], [500.0, 277.0],
      [227.27, 195.0], [500.0, 113.0], [583.33, 277.0]
    ];
    const points: NativeTouchPoint[] = [];
    let timeMs: number = 100;
    const firstCodePoint: number = 1087; // Unicode 'п'.
    points.push({
      x: centers[0][0], y: centers[0][1], timeMs,
      keyCode: firstCodePoint
    });
    timeMs += 24;
    for (let index: number = 1; index < centers.length; index++) {
      const previous: number[] = centers[index - 1];
      const current: number[] = centers[index];
      for (let step: number = 1; step <= 4; step++) {
        const ratio: number = step / 4.0;
        points.push({
          x: previous[0] + (current[0] - previous[0]) * ratio,
          y: previous[1] + (current[1] - previous[1]) * ratio,
          timeMs,
          keyCode: 0x20000000 | firstCodePoint
        });
        timeMs += 24;
      }
    }
    const last: number[] = centers[centers.length - 1];
    points.push({
      x: last[0], y: last[1], timeMs,
      keyCode: 0x10000000 | firstCodePoint
    });
    return points;
  }

  private buildRnntProbeRequest(points: NativeTouchPoint[], disableRnnt: boolean): NativeSuggestRequest {
    return {
      token: 'п',
      input: 'п',
      locale: 'ru-RU',
      wantAutocorrect: false,
      maxSuggestions: 12,
      isGesture: true,
      disableRnnt,
      proximityInfo: {
        layout: 'ru:alphabet',
        keyboardWidth: 1000,
        keyboardHeight: 400,
        enabled: true
      },
      points
    };
  }

  private buildTapModelProbeRequest(disableTapModel: boolean,
    mismatchEdgeOffset: number = 0.0): NativeSuggestRequest {
    const letters: number[] = [1087, 1088, 1080, 1074, 1085, 1090]; // привнт
    const centers: number[][] = [
      [409.09, 195.0], [500.0, 195.0], [500.0, 277.0],
      [227.27, 195.0], [500.0, 113.0], [583.33, 277.0]
    ];
    const points: NativeTouchPoint[] = letters.map(
      (keyCode: number, index: number): NativeTouchPoint => {
        return {
          x: centers[index][0] + (index === 4 ? mismatchEdgeOffset : 0.0),
          y: centers[index][1],
          timeMs: 100 + index * 40,
          keyCode
        };
      }
    );
    return {
      token: 'привнт',
      input: 'привнт',
      locale: 'ru-RU',
      wantAutocorrect: true,
      maxSuggestions: 12,
      disableTapModel,
      proximityInfo: {
        layout: 'ru:alphabet',
        keyboardWidth: 1000,
        keyboardHeight: 400,
        enabled: true
      },
      points
    };
  }

  private buildAutoccProbeRequest(disableAutoccModel: boolean,
    autoccVariant: string = 'default'): NativeSuggestRequest {
    const letters: number[] = [1087, 1088, 1077, 1074, 1077, 1090]; // превет
    const centers: number[][] = [
      [409.09, 195.0], [500.0, 195.0], [409.09, 113.0],
      [227.27, 195.0], [409.09, 113.0], [583.33, 277.0]
    ];
    const points: NativeTouchPoint[] = letters.map(
      (keyCode: number, index: number): NativeTouchPoint => {
        return {
          x: centers[index][0], y: centers[index][1],
          timeMs: 100 + index * 40, keyCode
        };
      }
    );
    return {
      token: 'превет',
      input: 'превет',
      locale: 'ru-RU',
      wantAutocorrect: true,
      maxSuggestions: 6,
      dictionarySessions: [{ id: 'hoskey-default', locale: 'ru-RU' }],
      packageName: 'com.opentech.openboard',
      disableAutoccModel,
      autoccVariant,
      proximityInfo: {
        layout: 'ru:alphabet', keyboardWidth: 1000,
        keyboardHeight: 400, enabled: true
      },
      points
    };
  }

  private async ensureDictInstalled(): Promise<string> {
    if (this.installedDictDir.length > 0) {
      return this.installedDictDir;
    }
    const hostContext = this.getUIContext().getHostContext();
    if (!hostContext) {
      throw new Error('UIAbilityContext is unavailable');
    }
    const ctx = hostContext as common.UIAbilityContext;
    this.log('DictInstaller.install starting...');
    this.installedDictDir = await DictInstaller.install(ctx);
    this.log('Dict installed to: ' + this.installedDictDir);
    return this.installedDictDir;
  }

  private async runDiagnostic(useVerifiedSplitOverlay: boolean = false): Promise<void> {
    this.predictorModeLabel = useVerifiedSplitOverlay ? 'verified-split' : 'baseline';
    // Шаг 1: проверяем нативный модуль
    const available = HoskeyEngine.isAvailable();
    this.log('Native module available: ' + available);
    this.log('Predictor debug mode: ' + this.predictorModeLabel);

    if (!available) {
      this.nativeStatus = 'Native модуль не загружен';
      this.statusColor = '#FF4444';
      this.log('STOP: модуль libhoskey_bridge.so не найден или не совместим');
      return;
    }

    this.nativeStatus = 'Модуль загружен, копирование словарей...';
    this.log('Module loaded OK');

    // Шаг 2: копируем словари
    let dictDir = '';
    try {
      dictDir = await this.ensureDictInstalled();
    } catch (e) {
      const err = e as Error;
      this.nativeStatus = 'Ошибка копирования словарей';
      this.statusColor = '#FF4444';
      this.log('ERROR dict install: ' + err.message);
      return;
    }

    // Шаг 3: open движка
    this.nativeStatus = 'Открытие hosdict engine в фоне...';
    this.log('Calling engine.openAsync(' + dictDir + ')');
    console.info('[SetupGuide] Calling engine.openAsync(' + dictDir + ')');
    try {
      const ok = useVerifiedSplitOverlay
        ? await this.engine.openWithVerifiedSplitOverlayAsync(dictDir)
        : await this.engine.openAsync(dictDir);
      const diag = this.engine.getDiagnostics();
      this.log('Open success: ' + ok);
      this.log('[C++] ' + diag);
      console.info('[SetupGuide] engine.openAsync result: ' + ok);
      console.info('[SetupGuide] diagnostics: ' + diag);

      if (ok) {
        const keys = this.engine.getLoadedAssetKeys();
        this.log('Loaded assets (' + keys.length + '): ' + keys.join(', '));
        console.info('[SetupGuide] Loaded assets: ' + keys.join(', '));
        const reports = this.engine.getLoadReports();
        this.log('Load reports: ' + reports.length);
        reports.forEach((report) => {
          const notes = report.notes.length > 0 ? report.notes.join(' | ') : 'no-notes';
          this.log('[REPORT] ' + report.assetKey + ' [' + report.format + ', ' + report.byteSize + ' bytes] ' + notes);
        });
        await this.yieldToUi();
      } else {
        this.nativeStatus = 'Engine open FAILED (см. лог)';
        this.statusColor = '#FF4444';
        return;
      }
    } catch (e) {
      const err = e as Error;
      this.nativeStatus = 'Engine openAsync CRASH: ' + err.message;
      this.statusColor = '#FF4444';
      this.log('CRASH: ' + err.message);
      console.error('[SetupGuide] engine.openAsync CRASH: ' + err.message);
      return;
    }

    // Шаг 4: тест suggestions
    this.nativeStatus = 'Тестируем suggestions...';
    this.log('Testing getSuggestions probes');
    try {
      const probes: string[] = ['вообщем', 'пр', 'при', 'ст', 'не', 'алв'];
      let sugs: string[] = [];
      probes.forEach((probe: string, index: number) => {
        const nativeResponse = this.engine.getSuggestionsResponse(probe, '', 'ru-RU');
        const current = nativeResponse.suggestions.map((item: NativeSuggestion): string => item.text);
        if (index === 0) {
          sugs = current;
        }
        this.log('[SUGGEST][' + probe + '] [' + current.join(', ') + ']');
        this.log('[SUGGEST][' + probe + '][diag] ' + nativeResponse.diagnostics);
        console.info('[SetupGuide] suggestions ' + probe + ': [' + current.join(', ') + ']');
        console.info('[SetupGuide] suggestions ' + probe + ' diagnostics: ' + nativeResponse.diagnostics);
      });
      await this.yieldToUi();

      const contextTokens: string[] = [
        'я', 'думаю', 'что', 'это', 'было', 'бы', 'очень', 'хорошо', 'если'
      ];
      const contextRequest: NativeSuggestRequest =
        this.buildYnnlmProbeRequest(contextTokens, false);
      const contextResponse = this.engine.getSuggestionsByRequest(contextRequest);
      await this.yieldToUi();
      const contextOffResponse = this.engine.getSuggestionsByRequest(
        this.buildYnnlmProbeRequest(contextTokens, true)
      );
      const contextOnTop: string = contextResponse.suggestions
        .map((item: NativeSuggestion): string => item.text).join(', ');
      const contextOffTop: string = contextOffResponse.suggestions
        .map((item: NativeSuggestion): string => item.text).join(', ');
      this.log('[A/B][NNLM][ON] top=[' + contextOnTop + ']');
      this.log('[A/B][NNLM][ON][diag] ' + contextResponse.diagnostics);
      this.log('[A/B][NNLM][OFF] top=[' + contextOffTop + ']');
      this.log('[A/B][NNLM][OFF][diag] ' + contextOffResponse.diagnostics);
      console.info('[SetupGuide] [A/B][NNLM][ON] top=[' + contextOnTop + ']');
      console.info('[SetupGuide] [A/B][NNLM][ON] diagnostics: ' + contextResponse.diagnostics);
      console.info('[SetupGuide] [A/B][NNLM][OFF] top=[' + contextOffTop + ']');
      console.info('[SetupGuide] [A/B][NNLM][OFF] diagnostics: ' + contextOffResponse.diagnostics);
      await this.yieldToUi();

      const emojiOnResponse: NativeEmojiSuggestResponse = this.engine.suggestEmojis({
        completedWords: ['я', 'люблю'],
        packageName: 'com.opentech.openboard',
        maxResults: 3,
        disableModel: false,
        sensitiveContext: false,
        generation: 4101
      });
      await this.yieldToUi();
      const emojiOffResponse: NativeEmojiSuggestResponse = this.engine.suggestEmojis({
        completedWords: ['я', 'люблю'],
        packageName: 'com.opentech.openboard',
        maxResults: 3,
        disableModel: true,
        sensitiveContext: false,
        generation: 4102
      });
      await this.yieldToUi();
      const emojiOtherResponse: NativeEmojiSuggestResponse = this.engine.suggestEmojis({
        completedWords: ['я', 'люблю', 'тебя'],
        packageName: 'com.opentech.openboard',
        maxResults: 3,
        disableModel: false,
        sensitiveContext: false,
        generation: 4103
      });
      const emojiOnTop: string = emojiOnResponse.candidates
        .map((item: NativeEmojiSuggestCandidate): string => item.emoji).join(', ');
      const emojiOffTop: string = emojiOffResponse.candidates
        .map((item: NativeEmojiSuggestCandidate): string => item.emoji).join(', ');
      const emojiOtherTop: string = emojiOtherResponse.candidates
        .map((item: NativeEmojiSuggestCandidate): string => item.emoji).join(', ');
      this.log('[A/B][CONTEXT_EMOJI][ON] top=[' + emojiOnTop + ']');
      this.log('[A/B][CONTEXT_EMOJI][ON][diag] ' + emojiOnResponse.diagnostics);
      this.log('[A/B][CONTEXT_EMOJI][OFF] top=[' + emojiOffTop + ']');
      this.log('[A/B][CONTEXT_EMOJI][OFF][diag] ' + emojiOffResponse.diagnostics);
      this.log('[A/B][CONTEXT_EMOJI][OTHER] top=[' + emojiOtherTop + ']');
      this.log('[A/B][CONTEXT_EMOJI][OTHER][diag] ' + emojiOtherResponse.diagnostics);
      console.info('[SetupGuide] [A/B][CONTEXT_EMOJI][ON] top=[' + emojiOnTop + ']');
      console.info('[SetupGuide] [A/B][CONTEXT_EMOJI][ON] diagnostics: ' + emojiOnResponse.diagnostics);
      console.info('[SetupGuide] [A/B][CONTEXT_EMOJI][OFF] top=[' + emojiOffTop + ']');
      console.info('[SetupGuide] [A/B][CONTEXT_EMOJI][OFF] diagnostics: ' + emojiOffResponse.diagnostics);
      console.info('[SetupGuide] [A/B][CONTEXT_EMOJI][OTHER] top=[' + emojiOtherTop + ']');
      console.info('[SetupGuide] [A/B][CONTEXT_EMOJI][OTHER] diagnostics: ' + emojiOtherResponse.diagnostics);
      await this.yieldToUi();

      const swipePoints: NativeTouchPoint[] = this.buildSyntheticSwipeProbePoints();
      const rnntOnResponse = this.engine.getSuggestionsByRequest(
        this.buildRnntProbeRequest(swipePoints, false)
      );
      await this.yieldToUi();
      const rnntOffResponse = this.engine.getSuggestionsByRequest(
        this.buildRnntProbeRequest(swipePoints, true)
      );
      const rnntOnTop: string = rnntOnResponse.suggestions
        .map((item: NativeSuggestion): string => item.text).join(', ');
      const rnntOffTop: string = rnntOffResponse.suggestions
        .map((item: NativeSuggestion): string => item.text).join(', ');
      this.log('[A/B][RNNT][ON] top=[' + rnntOnTop + ']');
      this.log('[A/B][RNNT][ON][diag] ' + rnntOnResponse.diagnostics);
      this.log('[A/B][RNNT][OFF] top=[' + rnntOffTop + ']');
      this.log('[A/B][RNNT][OFF][diag] ' + rnntOffResponse.diagnostics);
      console.info('[SetupGuide] [A/B][RNNT][ON] top=[' + rnntOnTop + ']');
      console.info('[SetupGuide] [A/B][RNNT][ON] diagnostics: ' + rnntOnResponse.diagnostics);
      console.info('[SetupGuide] [A/B][RNNT][OFF] top=[' + rnntOffTop + ']');
      console.info('[SetupGuide] [A/B][RNNT][OFF] diagnostics: ' + rnntOffResponse.diagnostics);
      await this.yieldToUi();

      const tapOnResponse = this.engine.getSuggestionsByRequest(
        this.buildTapModelProbeRequest(false)
      );
      await this.yieldToUi();
      const tapOffResponse = this.engine.getSuggestionsByRequest(
        this.buildTapModelProbeRequest(true)
      );
      await this.yieldToUi();
      const tapEdgeResponse = this.engine.getSuggestionsByRequest(
        this.buildTapModelProbeRequest(false, 36.0)
      );
      const tapOnTop: string = tapOnResponse.suggestions
        .map((item: NativeSuggestion): string => item.text).join(', ');
      const tapOffTop: string = tapOffResponse.suggestions
        .map((item: NativeSuggestion): string => item.text).join(', ');
      const tapEdgeTop: string = tapEdgeResponse.suggestions
        .map((item: NativeSuggestion): string => item.text).join(', ');
      this.log('[A/B][TAP_MODEL][ON] top=[' + tapOnTop + '] autocorrect=' +
        (tapOnResponse.willAutocorrect ? '1' : '0'));
      this.log('[A/B][TAP_MODEL][ON][diag] ' + tapOnResponse.diagnostics);
      this.log('[A/B][TAP_MODEL][OFF] top=[' + tapOffTop + '] autocorrect=' +
        (tapOffResponse.willAutocorrect ? '1' : '0'));
      this.log('[A/B][TAP_MODEL][OFF][diag] ' + tapOffResponse.diagnostics);
      this.log('[A/B][TAP_MODEL][EDGE] top=[' + tapEdgeTop + '] autocorrect=' +
        (tapEdgeResponse.willAutocorrect ? '1' : '0'));
      this.log('[A/B][TAP_MODEL][EDGE][diag] ' + tapEdgeResponse.diagnostics);
      console.info('[SetupGuide] [A/B][TAP_MODEL][ON] top=[' + tapOnTop +
        '] autocorrect=' + (tapOnResponse.willAutocorrect ? '1' : '0'));
      console.info('[SetupGuide] [A/B][TAP_MODEL][ON] diagnostics: ' +
        tapOnResponse.diagnostics);
      console.info('[SetupGuide] [A/B][TAP_MODEL][OFF] top=[' + tapOffTop +
        '] autocorrect=' + (tapOffResponse.willAutocorrect ? '1' : '0'));
      console.info('[SetupGuide] [A/B][TAP_MODEL][OFF] diagnostics: ' +
        tapOffResponse.diagnostics);
      console.info('[SetupGuide] [A/B][TAP_MODEL][EDGE] top=[' + tapEdgeTop +
        '] autocorrect=' + (tapEdgeResponse.willAutocorrect ? '1' : '0'));
      console.info('[SetupGuide] [A/B][TAP_MODEL][EDGE] diagnostics: ' +
        tapEdgeResponse.diagnostics);
      await this.yieldToUi();

      const autoccDefaultResponse: NativeSuggestResponse = this.engine.getSuggestionsByRequest(
        this.buildAutoccProbeRequest(false)
      );
      await this.yieldToUi();
      const autoccOffResponse: NativeSuggestResponse = this.engine.getSuggestionsByRequest(
        this.buildAutoccProbeRequest(true)
      );
      await this.yieldToUi();
      const autoccK13321Response: NativeSuggestResponse = this.engine.getSuggestionsByRequest(
        this.buildAutoccProbeRequest(false, 'KEYBOARD-13321')
      );
      await this.yieldToUi();
      const autoccK13421Response: NativeSuggestResponse = this.engine.getSuggestionsByRequest(
        this.buildAutoccProbeRequest(false, 'KEYBOARD-13421')
      );
      const autoccDefaultTop: string = autoccDefaultResponse.suggestions
        .map((item: NativeSuggestion): string => item.text).join(', ');
      const autoccOffTop: string = autoccOffResponse.suggestions
        .map((item: NativeSuggestion): string => item.text).join(', ');
      const autoccK13321Top: string = autoccK13321Response.suggestions
        .map((item: NativeSuggestion): string => item.text).join(', ');
      const autoccK13421Top: string = autoccK13421Response.suggestions
        .map((item: NativeSuggestion): string => item.text).join(', ');
      this.log('[A/B][AUTOCC][DEFAULT] top=[' + autoccDefaultTop + '] autocorrect=' +
        (autoccDefaultResponse.willAutocorrect ? '1' : '0'));
      this.log('[A/B][AUTOCC][DEFAULT][diag] ' + autoccDefaultResponse.diagnostics);
      this.log('[A/B][AUTOCC][OFF] top=[' + autoccOffTop + '] autocorrect=' +
        (autoccOffResponse.willAutocorrect ? '1' : '0'));
      this.log('[A/B][AUTOCC][OFF][diag] ' + autoccOffResponse.diagnostics);
      this.log('[A/B][AUTOCC][K13321] top=[' + autoccK13321Top + '] autocorrect=' +
        (autoccK13321Response.willAutocorrect ? '1' : '0'));
      this.log('[A/B][AUTOCC][K13321][diag] ' + autoccK13321Response.diagnostics);
      this.log('[A/B][AUTOCC][K13421] top=[' + autoccK13421Top + '] autocorrect=' +
        (autoccK13421Response.willAutocorrect ? '1' : '0'));
      this.log('[A/B][AUTOCC][K13421][diag] ' + autoccK13421Response.diagnostics);
      console.info('[SetupGuide] [A/B][AUTOCC][DEFAULT] top=[' + autoccDefaultTop +
        '] autocorrect=' + (autoccDefaultResponse.willAutocorrect ? '1' : '0'));
      console.info('[SetupGuide] [A/B][AUTOCC][DEFAULT] diagnostics: ' +
        autoccDefaultResponse.diagnostics);
      console.info('[SetupGuide] [A/B][AUTOCC][OFF] top=[' + autoccOffTop +
        '] autocorrect=' + (autoccOffResponse.willAutocorrect ? '1' : '0'));
      console.info('[SetupGuide] [A/B][AUTOCC][OFF] diagnostics: ' +
        autoccOffResponse.diagnostics);
      console.info('[SetupGuide] [A/B][AUTOCC][K13321] top=[' + autoccK13321Top +
        '] autocorrect=' + (autoccK13321Response.willAutocorrect ? '1' : '0'));
      console.info('[SetupGuide] [A/B][AUTOCC][K13321] diagnostics: ' +
        autoccK13321Response.diagnostics);
      console.info('[SetupGuide] [A/B][AUTOCC][K13421] top=[' + autoccK13421Top +
        '] autocorrect=' + (autoccK13421Response.willAutocorrect ? '1' : '0'));
      console.info('[SetupGuide] [A/B][AUTOCC][K13421] diagnostics: ' +
        autoccK13421Response.diagnostics);

      const autoccExecutionGate: boolean =
        autoccDefaultResponse.diagnostics.includes('autocc_model_path=tree_policy') &&
        autoccDefaultResponse.diagnostics.includes('autocc_model_attempted=1') &&
        autoccDefaultResponse.diagnostics.includes('forest_contract=exact_hobl79') &&
        autoccOffResponse.diagnostics.includes('autocc_model_path=debug_disabled') &&
        autoccOffResponse.diagnostics.includes('autocc_model_attempted=0') &&
        autoccK13321Response.diagnostics.includes('autocc_model_path=tree_policy') &&
        autoccK13321Response.diagnostics.includes('autocc_variant=KEYBOARD-13321') &&
        autoccK13321Response.diagnostics.includes('autocc_probability=') &&
        autoccK13421Response.diagnostics.includes('autocc_model_path=tree_policy') &&
        autoccK13421Response.diagnostics.includes('autocc_variant=KEYBOARD-13421') &&
        autoccK13421Response.diagnostics.includes('autocc_probability=') &&
        autoccDefaultResponse.diagnostics.includes('autocc_probability=') &&
        autoccDefaultResponse.willAutocorrect ===
          autoccK13421Response.willAutocorrect;
      const autoccProducerExact: boolean =
        autoccDefaultResponse.diagnostics.includes(
          'autocc_producer_contract=ynnlm1225_char_eos_empty_v1') &&
        autoccDefaultResponse.diagnostics.includes(
          'autocc_char_score_available=1') &&
        autoccDefaultResponse.diagnostics.includes(
          'autocc_char_context=0') &&
        autoccDefaultResponse.diagnostics.includes(
          'autocc_char_original_total=') &&
        autoccDefaultResponse.diagnostics.includes(
          'autocc_char_correction_total=');
      this.log('[GATE][AUTOCC][EXECUTION] ' +
        (autoccExecutionGate ? 'PASS' : 'FAIL'));
      this.log('[GATE][AUTOCC][PRODUCER_PARITY] ' +
        (autoccProducerExact ? 'PASS' : 'PENDING'));

      if (!autoccExecutionGate) {
        this.nativeStatus = 'Engine OK, но Autocc model gate FAILED';
        this.statusColor = '#FF4444';
        this.testResult = 'Проверьте Autocc load/invoke/variant diagnostics';
      } else if (!autoccProducerExact) {
        this.nativeStatus = 'Engine OK, Autocc neural producer parity PENDING';
        this.statusColor = '#FFA500';
        this.testResult = 'Forest исполняется, exact YNNLM char/EOS features ещё не подтверждены';
      } else if (sugs.length > 0 && sugs[0].length > 0) {
        this.nativeStatus = 'hosdict Engine: РАБОТАЕТ';
        this.statusColor = '#00CC00';
        this.testResult = 'Подсказки: ' + sugs.join(', ');
      } else {
        this.nativeStatus = 'Engine OK, но подсказки пустые';
        this.statusColor = '#FFA500';
        this.testResult = 'Движок ответил пустым массивом';
      }
    } catch (e) {
      const err = e as Error;
      this.nativeStatus = 'getSuggestions CRASH: ' + err.message;
      this.statusColor = '#FF4444';
      this.log('ERROR suggestions: ' + err.message);
    }
  }

  private formatNativeResponse(label: string, response: NativeSuggestResponse): string {
    const suggestions = response.suggestions
      .map((item: NativeSuggestion): string => item.text)
      .join(', ');
    return label + '\n' +
      '  suggestions: [' + suggestions + ']\n' +
      '  autocorrect: ' + (response.willAutocorrect ? 'yes' : 'no') + '\n' +
      '  diagnostics: ' + response.diagnostics;
  }

  private async runModelNegativeAudit(): Promise<void> {
    if (this.negativeAuditRunning) {
      return;
    }
    this.negativeAuditRunning = true;
    this.negativeAuditResult = 'Model negative audit: RUNNING';
    try {
      const dictDir: string = await this.ensureDictInstalled();
      const ok: boolean = await this.engine.openAsync(dictDir, {
        runModelNegativeAudit: true
      });
      const diagnostics: string = this.engine.getDiagnostics();
      const passed: boolean = ok &&
        diagnostics.includes('model_negative_audit={status=pass') &&
        diagnostics.includes('rnnt_missing=11/11') &&
        diagnostics.includes('ynnlm_missing=3/3') &&
        diagnostics.includes('empty_nan_layout=covered') &&
        diagnostics.includes('malformed_path=covered');
      this.negativeAuditResult = passed
        ? 'Model negative audit: PASS'
        : 'Model negative audit: FAIL';
      this.log('[MODEL_NEGATIVE_AUDIT] ' + diagnostics);
      console.info('[HOSKEY_MODEL_NEGATIVE_AUDIT] result=' +
        (passed ? 'PASS' : 'FAIL') + ',diagnostics=' + diagnostics);
    } catch (e) {
      const err = e as Error;
      this.negativeAuditResult = 'Model negative audit: FAIL';
      this.log('[MODEL_NEGATIVE_AUDIT] ERROR ' + err.message);
      console.error('[HOSKEY_MODEL_NEGATIVE_AUDIT] result=FAIL,error=' + err.message);
    } finally {
      this.negativeAuditRunning = false;
    }
  }

  private async runSplitCompare(): Promise<void> {
    this.compareRunning = true;
    this.compareResult = '';
    try {
      const available = HoskeyEngine.isAvailable();
      if (!available) {
        this.compareResult = 'Native модуль недоступен';
        this.log('Compare STOP: native module unavailable');
        return;
      }

      const token = this.compareToken.trim();
      if (token.length === 0) {
        this.compareResult = 'Введите префикс';
        return;
      }

      const dictDir = await this.ensureDictInstalled();
      if (!this.engine.isInitialized()) {
        const ok = await this.engine.openWithVerifiedSplitOverlayAsync(dictDir);
        this.log('Compare bootstrap openAsync: ' + ok);
      }

      this.log('[COMPARE] start token=' + token + ' mode=' + this.predictorModeLabel);
      const compare = this.engine.comparePredictorModes(token, '', 'ru-RU');
      const baseline = compare.baseline;
      const verified = compare.verifiedSplit;
      const splitChanged = compare.changed;

      this.compareResult =
        'Token: ' + token + '\n' +
        'Changed: ' + (splitChanged ? 'yes' : 'no') + '\n\n' +
        this.formatNativeResponse('Baseline', baseline) + '\n\n' +
        this.formatNativeResponse('Verified split', verified);

      this.log('[COMPARE] token=' + token + ' changed=' + (splitChanged ? '1' : '0'));
      this.log('[COMPARE][native] ' + compare.diagnostics);
      this.log('[COMPARE][B] ' + baseline.diagnostics);
      this.log('[COMPARE][VS] ' + verified.diagnostics);

      this.predictorModeLabel = 'verified-split';
      this.nativeStatus = 'Compare completed';
      this.statusColor = splitChanged ? '#00CC00' : '#FFA500';
    } catch (e) {
      const err = e as Error;
      this.compareResult = 'Compare failed: ' + err.message;
      this.log('ERROR compare: ' + err.message);
      this.statusColor = '#FF4444';
    } finally {
      this.compareRunning = false;
    }
  }

  build() {
    Navigation() {
      Scroll() {
        Column({ space: 18 }) {
          // --- Hero / onboarding header ---
          Column({ space: 14 }) {
            Row({ space: 12 }) {
              Text('⌨')
                .fontSize(34)
                .width(56)
                .height(56)
                .textAlign(TextAlign.Center)
                .backgroundColor(OpenBoardTheme.accentSoft)
                .borderRadius(18)

              Column({ space: 4 }) {
                Text('HOSKEY')
                  .fontSize(30)
                  .fontWeight(FontWeight.Bold)
                  .fontColor(OpenBoardTheme.primaryText)
                Text('Нативная клавиатура с Yandex‑style подсказками, 1nc7 trie и нейро‑словарём')
                  .fontSize(13)
                  .fontColor(OpenBoardTheme.secondaryText)
                  .maxLines(2)
                  .textOverflow({ overflow: TextOverflow.Ellipsis })
              }
              .layoutWeight(1)
            }
            .width('100%')

            Row({ space: 8 }) {
              Circle({ width: 10, height: 10 })
                .fill(this.statusColor)
              Text(this.nativeStatus)
                .fontSize(14)
                .fontWeight(FontWeight.Medium)
                .fontColor(OpenBoardTheme.primaryText)
                .layoutWeight(1)
            }
            .width('100%')
            .padding({ left: 12, right: 12, top: 10, bottom: 10 })
            .borderRadius(18)
            .backgroundColor('#111923')

            Flex({ wrap: FlexWrap.Wrap, justifyContent: FlexAlign.Start }) {
              Text('1nc7 native')
                .fontSize(12)
                .fontColor('#CFE5FF')
                .backgroundColor('#193A5E')
                .padding({ left: 10, right: 10, top: 6, bottom: 6 })
                .borderRadius(14)
                .margin({ right: 8, bottom: 8 })
              Text('5+ подсказок')
                .fontSize(12)
                .fontColor('#D8F7E7')
                .backgroundColor('#174C34')
                .padding({ left: 10, right: 10, top: 6, bottom: 6 })
                .borderRadius(14)
                .margin({ right: 8, bottom: 8 })
              Text('нейро vocab')
                .fontSize(12)
                .fontColor('#F2DDFF')
                .backgroundColor('#45315C')
                .padding({ left: 10, right: 10, top: 6, bottom: 6 })
                .borderRadius(14)
                .margin({ right: 8, bottom: 8 })
              Text('HarmonyOS')
                .fontSize(12)
                .fontColor(OpenBoardTheme.primaryText)
                .backgroundColor(OpenBoardTheme.specialKey)
                .padding({ left: 10, right: 10, top: 6, bottom: 6 })
                .borderRadius(14)
                .margin({ right: 8, bottom: 8 })
            }
            .width('100%')
          }
          .width('100%')
          .padding(20)
          .borderRadius(24)
          .backgroundColor('#162033')

          // --- Setup steps ---
          Column({ space: 14 }) {
            Text('Как включить клавиатуру')
              .fontSize(22)
              .fontWeight(FontWeight.Bold)
              .fontColor(OpenBoardTheme.primaryText)
              .width('100%')
            Text('Приложение — это центр настройки и диагностики. Сама клавиатура включается в системных настройках HarmonyOS.')
              .fontSize(14)
              .fontColor(OpenBoardTheme.secondaryText)
              .width('100%')

            Row({ space: 12 }) {
              Text('1')
                .fontSize(16)
                .fontWeight(FontWeight.Bold)
                .fontColor('#FFFFFF')
                .width(34)
                .height(34)
                .textAlign(TextAlign.Center)
                .backgroundColor(OpenBoardTheme.accent)
                .borderRadius(17)
              Column({ space: 3 }) {
                Text('Открой системные настройки')
                  .fontSize(16)
                  .fontWeight(FontWeight.Bold)
                  .fontColor(OpenBoardTheme.primaryText)
                Text('Settings → System & updates → Language & input / Keyboard')
                  .fontSize(13)
                  .fontColor(OpenBoardTheme.secondaryText)
              }
              .layoutWeight(1)
            }
            .width('100%')
            .padding(14)
            .borderRadius(18)
            .backgroundColor(OpenBoardTheme.backgroundDark)

            Row({ space: 12 }) {
              Text('2')
                .fontSize(16)
                .fontWeight(FontWeight.Bold)
                .fontColor('#FFFFFF')
                .width(34)
                .height(34)
                .textAlign(TextAlign.Center)
                .backgroundColor(OpenBoardTheme.accent)
                .borderRadius(17)
              Column({ space: 3 }) {
                Text('Включи HOSKEY')
                  .fontSize(16)
                  .fontWeight(FontWeight.Bold)
                  .fontColor(OpenBoardTheme.primaryText)
                Text('Разреши HOSKEY в списке input methods, выбери её клавиатурой по умолчанию и включи «Полный доступ» (Full experience). В базовом режиме система блокирует панель и клавиатура не появляется.')
                  .fontSize(13)
                  .fontColor(OpenBoardTheme.secondaryText)
              }
              .layoutWeight(1)
            }
            .width('100%')
            .padding(14)
            .borderRadius(18)
            .backgroundColor(OpenBoardTheme.backgroundDark)

            Row({ space: 12 }) {
              Text('3')
                .fontSize(16)
                .fontWeight(FontWeight.Bold)
                .fontColor('#FFFFFF')
                .width(34)
                .height(34)
                .textAlign(TextAlign.Center)
                .backgroundColor(OpenBoardTheme.accent)
                .borderRadius(17)
              Column({ space: 3 }) {
                Text('Проверь в любом поле ввода')
                  .fontSize(16)
                  .fontWeight(FontWeight.Bold)
                  .fontColor(OpenBoardTheme.primaryText)
                Text('Напечатай “пр”, “алв”, “вообщем”. В строке подсказок должно быть 5 вариантов и горизонтальный скролл.')
                  .fontSize(13)
                  .fontColor(OpenBoardTheme.secondaryText)
              }
              .layoutWeight(1)
            }
            .width('100%')
            .padding(14)
            .borderRadius(18)
            .backgroundColor(OpenBoardTheme.backgroundDark)
          }
          .width('100%')
          .padding(18)
          .borderRadius(24)
          .backgroundColor(OpenBoardTheme.surface)

          // --- Engine health / quick actions ---
          Column({ space: 14 }) {
            Row({ space: 10 }) {
              Column({ space: 4 }) {
                Text('Диагностика движка')
                  .fontSize(22)
                  .fontWeight(FontWeight.Bold)
                  .fontColor(OpenBoardTheme.primaryText)
                Text('Автотест копирует словари, открывает native engine и делает пробный запрос.')
                  .fontSize(13)
                  .fontColor(OpenBoardTheme.secondaryText)
              }
              .layoutWeight(1)
            }
            .width('100%')

            Row({ space: 10 }) {
              Column({ space: 4 }) {
                Text('Статус')
                  .fontSize(12)
                  .fontColor(OpenBoardTheme.mutedText)
                Text(this.statusColor === '#00CC00' ? 'OK' : 'WAIT')
                  .fontSize(20)
                  .fontWeight(FontWeight.Bold)
                  .fontColor(this.statusColor)
              }
              .layoutWeight(1)
              .padding(14)
              .borderRadius(18)
              .backgroundColor(OpenBoardTheme.backgroundDark)

              Column({ space: 4 }) {
                Text('Mode')
                  .fontSize(12)
                  .fontColor(OpenBoardTheme.mutedText)
                Text(this.predictorModeLabel)
                  .fontSize(18)
                  .fontWeight(FontWeight.Bold)
                  .fontColor(OpenBoardTheme.primaryText)
                  .maxLines(1)
                  .textOverflow({ overflow: TextOverflow.Ellipsis })
              }
              .layoutWeight(1)
              .padding(14)
              .borderRadius(18)
              .backgroundColor(OpenBoardTheme.backgroundDark)
            }
            .width('100%')

            if (this.testResult.length > 0) {
              Text(this.testResult)
                .fontSize(14)
                .fontWeight(FontWeight.Medium)
                .fontColor('#D8F7E7')
                .width('100%')
                .padding(12)
                .borderRadius(16)
                .backgroundColor('#173B2C')
            }

            Row({ space: 10 }) {
              Button('Перезапустить тест')
                .fontSize(14)
                .height(42)
                .layoutWeight(1)
                .borderRadius(21)
                .backgroundColor(OpenBoardTheme.accent)
                .fontColor('#FFFFFF')
                .onClick(() => {
                  this.logLines = [];
                  this.testResult = '';
                  this.predictorModeLabel = 'baseline';
                  this.nativeStatus = 'Запуск диагностики...';
                  this.statusColor = '#FFA500';
                  this.runDiagnostic();
                })

              Button('Verified split')
                .fontSize(14)
                .height(42)
                .layoutWeight(1)
                .borderRadius(21)
                .backgroundColor(OpenBoardTheme.specialKey)
                .fontColor(OpenBoardTheme.primaryText)
                .onClick(() => {
                  this.logLines = [];
                  this.testResult = '';
                  this.predictorModeLabel = 'verified-split';
                  this.nativeStatus = 'Запуск диагностики verified split...';
                  this.statusColor = '#FFA500';
                  this.runDiagnostic(true);
                })
            }
            .width('100%')

            Button(this.negativeAuditRunning ? 'Model audit running...' :
              'Model negative audit')
              .fontSize(14)
              .height(42)
              .width('100%')
              .borderRadius(21)
              .backgroundColor(OpenBoardTheme.specialKey)
              .fontColor(OpenBoardTheme.primaryText)
              .enabled(!this.negativeAuditRunning)
              .onClick(() => {
                this.runModelNegativeAudit();
              })

            if (this.negativeAuditResult.length > 0) {
              Text(this.negativeAuditResult)
                .fontSize(13)
                .fontWeight(FontWeight.Medium)
                .fontColor(this.negativeAuditResult.endsWith('PASS')
                  ? '#62E6A7' : '#FF8B8B')
                .width('100%')
            }
          }
          .width('100%')
          .padding(18)
          .borderRadius(24)
          .backgroundColor(OpenBoardTheme.surface)

          // --- Tester checklist ---
          Column({ space: 14 }) {
            Text('Чек‑лист для тестировщика')
              .fontSize(22)
              .fontWeight(FontWeight.Bold)
              .fontColor(OpenBoardTheme.primaryText)
              .width('100%')
            Text('Отправь этот блок человеку из сообщества: он быстро поймёт, что проверить и какой лог вернуть.')
              .fontSize(14)
              .fontColor(OpenBoardTheme.secondaryText)

            Text('1. Установить app/HAP и включить HOSKEY как input method.\n' +
              '2. Открыть заметки или чат, напечатать: пр, алв, она, вообщем.\n' +
              '3. Проверить: подсказки появляются сразу, UI не зависает, слова не режутся в первых 3 местах.\n' +
              '4. Если есть лаг/freeze — прислать appfreeze файл и время ввода.\n' +
              '5. Если подсказки странные — прислать скриншот + скопированный лог ниже.')
              .fontSize(14)
              .fontColor(OpenBoardTheme.primaryText)
              .lineHeight(22)
              .width('100%')
              .padding(14)
              .borderRadius(18)
              .backgroundColor(OpenBoardTheme.backgroundDark)
          }
          .width('100%')
          .padding(18)
          .borderRadius(24)
          .backgroundColor(OpenBoardTheme.surface)

          // --- Runtime compare ---
          Column({ space: 14 }) {
            Text('Проверка split‑prefix')
              .fontSize(22)
              .fontWeight(FontWeight.Bold)
              .fontColor(OpenBoardTheme.primaryText)
            Text('Для отладки Yandex‑style iterator path: сравнение baseline vs verified split overlay.')
              .fontSize(13)
              .fontColor(OpenBoardTheme.secondaryText)

            TextInput({ text: this.compareToken, placeholder: 'Введите prefix, например алв' })
              .width('100%')
              .height(46)
              .borderRadius(16)
              .backgroundColor(OpenBoardTheme.backgroundDark)
              .fontColor(OpenBoardTheme.primaryText)
              .placeholderColor(OpenBoardTheme.mutedText)
              .onChange((value: string) => {
                this.compareToken = value;
              })

            Text('Privacy gate device probe')
              .fontSize(13)
              .fontColor(OpenBoardTheme.secondaryText)
              .width('100%')

            // Real password editor for device privacy acceptance. Deliberately
            // unbound: its contents never enter SetupGuide state or diagnostics.
            TextInput({ placeholder: 'Password probe (not stored)' })
              .type(InputType.Password)
              .showPasswordIcon(false)
              .width('100%')
              .height(46)
              .borderRadius(16)
              .backgroundColor(OpenBoardTheme.backgroundDark)
              .fontColor(OpenBoardTheme.primaryText)
              .placeholderColor(OpenBoardTheme.mutedText)

            Flex({ wrap: FlexWrap.Wrap, justifyContent: FlexAlign.Start }) {
              ForEach(VERIFIED_SPLIT_PREFIXES, (prefix: string) => {
                Text(prefix)
                  .fontSize(13)
                  .fontColor(this.compareToken === prefix ? '#FFFFFF' : OpenBoardTheme.primaryText)
                  .backgroundColor(this.compareToken === prefix ? OpenBoardTheme.accent : OpenBoardTheme.specialKey)
                  .padding({ left: 11, right: 11, top: 7, bottom: 7 })
                  .borderRadius(15)
                  .margin({ right: 8, bottom: 8 })
                  .onClick(() => {
                    this.compareToken = prefix;
                  })
              })
            }
            .width('100%')

            Button(this.compareRunning ? 'Сравнение...' : 'Сравнить режимы')
              .fontSize(14)
              .height(42)
              .width('100%')
              .borderRadius(21)
              .backgroundColor(OpenBoardTheme.accent)
              .fontColor('#FFFFFF')
              .enabled(!this.compareRunning)
              .onClick(() => {
                this.runSplitCompare();
              })

            if (this.compareResult.length > 0) {
              Text(this.compareResult)
                .fontSize(12)
                .fontColor(OpenBoardTheme.secondaryText)
                .fontFamily('monospace')
                .width('100%')
                .padding(12)
                .borderRadius(16)
                .backgroundColor(OpenBoardTheme.backgroundDark)
            }
          }
          .width('100%')
          .padding(18)
          .borderRadius(24)
          .backgroundColor(OpenBoardTheme.surface)

          // --- Troubleshooting / log ---
          Column({ space: 12 }) {
            Row({ space: 10 }) {
              Column({ space: 4 }) {
                Text('Лог и troubleshooting')
                  .fontSize(22)
                  .fontWeight(FontWeight.Bold)
                  .fontColor(OpenBoardTheme.primaryText)
                Text('Если что-то не работает — скопируй лог и приложи к issue/сообщению.')
                  .fontSize(13)
                  .fontColor(OpenBoardTheme.secondaryText)
              }
              .layoutWeight(1)
            }
            .width('100%')

            Text('• Нет клавиатуры в списке: переустановить app и проверить Input Method Extension.\n' +
              '• Подсказки пустые: нажать “Перезапустить тест”.\n' +
              '• Зависание: прислать appfreeze файл из системы + токен, на котором зависло.')
              .fontSize(13)
              .fontColor(OpenBoardTheme.secondaryText)
              .lineHeight(20)
              .width('100%')

            Row({ space: 10 }) {
              Button(this.showLogPanel ? 'Скрыть лог' : 'Показать лог')
                .fontSize(14)
                .height(40)
                .layoutWeight(1)
                .borderRadius(20)
                .backgroundColor(OpenBoardTheme.specialKey)
                .fontColor(OpenBoardTheme.primaryText)
                .onClick(() => {
                  this.showLogPanel = !this.showLogPanel;
                })

              Button('Копировать')
                .fontSize(14)
                .height(40)
                .layoutWeight(1)
                .borderRadius(20)
                .backgroundColor(OpenBoardTheme.specialKey)
                .fontColor(OpenBoardTheme.primaryText)
                .onClick(() => {
                  try {
                    const text = this.logLines.join('\n');
                    const pasteData = pasteboard.createData(pasteboard.MIMETYPE_TEXT_PLAIN, text);
                    const sysBoard = pasteboard.getSystemPasteboard();
                    sysBoard.setData(pasteData);
                  } catch (e) {
                    console.error('Copy failed: ' + e);
                  }
                })
            }
            .width('100%')

            if (this.showLogPanel) {
              Text(this.logLines.length > 0 ? this.logLines.join('\n') : 'Пока лог пустой')
                .fontSize(11)
                .fontColor(OpenBoardTheme.secondaryText)
                .fontFamily('monospace')
                .width('100%')
                .padding(12)
                .borderRadius(16)
                .backgroundColor(OpenBoardTheme.backgroundDarker)
            }
          }
          .width('100%')
          .padding(18)
          .borderRadius(24)
          .backgroundColor(OpenBoardTheme.surface)
        }
        .width('100%')
        .padding({ left: 16, right: 16, top: 18, bottom: 34 })
      }
      .width('100%')
      .height('100%')
      .backgroundColor(OpenBoardTheme.background)
    }
    .title('HOSKEY')
    .titleMode(NavigationTitleMode.Mini)
  }
}
