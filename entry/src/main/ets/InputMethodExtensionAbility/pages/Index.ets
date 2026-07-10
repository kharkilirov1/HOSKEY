import { OpenBoardTheme } from '../model/Theme';
import {
  arabicLayout,
  englishLayout,
  italianLayout,
  KeyAction,
  KeyboardLayout,
  KeyData,
  polishLayout,
  ShiftState,
  symbolsLayout1,
  symbolsLayout2,
  persianLayout,
  russianLayout
} from '../model/KeyboardKeyData';
import keyboardController from '../model/KeyboardController';
import predictionModel from '../model/PredictionModel';
import diagLog from '../model/DiagLog';
import { display } from '@kit.ArkUI';
import { inputMethodEngine } from '@kit.IMEKit';
import pasteboard from '@ohos.pasteboard';
import { NativeSuggestResponse, NativeSuggestion } from 'libhoskey_bridge.so';

const EMOJI_KEY_DATA: KeyData = { mainValue: 'icon', secondaryValue: undefined, action: KeyAction.EMOJI_PANEL, flex: 1.2, shiftValue: undefined };

// TYPE DEFINITIONS
interface Position { x: number; y: number; }
interface LongPressInfo { key: KeyData; initialPosition: Position; }
interface SingleCharPopupInfo { char: string; position: Position; }
type KeyTouchPhase = 'down' | 'move' | 'up' | 'cancel';
type LanguageCode = 'en' | 'ar' | 'it' | 'pl' | 'fa' | 'ru';
enum LayoutSwipeDirection { Left, Right }
const ALLOWED_LANGUAGES: string[] = ['en', 'ru', 'ar', 'it', 'pl', 'fa'];
const EMOJI_CATEGORIES: string[] = ['Smileys & Emotion', 'People', 'Animals & Nature', 'Food & Drink', 'Activity & Event', 'Travel & Places', 'Objects', 'Symbols', 'Flags'];
const SUGGESTION_FULL_COUNT: number = 3;
const SUGGESTION_FETCH_LIMIT: number = 12;
const SUGGESTION_ITEM_GAP: number = 5;
const TOOLBAR_BUTTON_SIZE: number = 28;
const KEY_CONTENT_PADDING: number = 2;
const KEY_CORNER_RADIUS: number = 13;
const KEY_ROW_GAP: number = 5;
const KEYBOARD_HORIZONTAL_PADDING: number = 6;
const KEYBOARD_TOP_PADDING: number = 5;
const KEYBOARD_BOTTOM_PADDING: number = 5;
const TOOLBAR_HEIGHT: number = 46;

// Speech recognition simulation types
interface SpeechRecognitionResult {
  text: string;
  delay: number;
}

@Component
struct NativeDebugPopup {
  @Prop modeLabel: string;
  @Prop word: string;
  @Prop suggestionsLine: string;
  @Prop diagnosticsLine: string;
  @Prop predDirect: string;
  @Prop predHandoff: string;
  @Prop willAutocorrect: boolean;

  build() {
    Column({ space: 4 }) {
      Text('Native debug: ' + this.modeLabel)
        .fontSize(13)
        .fontWeight(FontWeight.Bold)
        .fontColor(OpenBoardTheme.primaryText)
      if (this.word.length > 0) {
        Text('word: ' + this.word)
          .fontSize(12)
          .fontColor(OpenBoardTheme.secondaryText)
      }
      if (this.suggestionsLine.length > 0) {
        Text('top: ' + this.suggestionsLine)
          .fontSize(12)
          .fontColor(OpenBoardTheme.primaryText)
      }
      Text('autocorrect: ' + (this.willAutocorrect ? 'yes' : 'no'))
        .fontSize(12)
        .fontColor(OpenBoardTheme.secondaryText)
      if (this.predDirect.length > 0) {
        Text('pred_d: ' + this.predDirect)
          .fontSize(11)
          .fontColor(OpenBoardTheme.secondaryText)
      }
      if (this.predHandoff.length > 0) {
        Text('pred_h: ' + this.predHandoff)
          .fontSize(11)
          .fontColor(OpenBoardTheme.secondaryText)
      }
      if (this.diagnosticsLine.length > 0) {
        Text(this.diagnosticsLine)
          .fontSize(10)
          .fontColor(OpenBoardTheme.secondaryText)
          .maxLines(4)
          .textOverflow({ overflow: TextOverflow.Ellipsis })
      }
    }
    .width('88%')
    .padding({ left: 12, right: 12, top: 10, bottom: 10 })
    .backgroundColor(OpenBoardTheme.surface)
    .borderRadius(12)
    .border({ width: 1, color: OpenBoardTheme.divider })
    .shadow({ radius: 10, color: '#60000000', offsetY: 4 })
  }
}

// HELPER FUNCTIONS
function getLanguageName(code: LanguageCode): string {
  switch (code) {
    case 'ar': return 'Arabic';
    case 'en': return 'English';
    case 'it': return 'Italian';
    case 'pl': return 'Polish';
    case 'fa': return 'Persian';
    case 'ru': return 'Russian';
    default: return 'English';
  }
}

function getVoiceDictationStatus(code: LanguageCode): string {
  switch (code) {
    case 'en': return 'Voice typing...';
    case 'pl': return 'Piszenie głosem...';
    case 'it': return 'Digitazione vocale...';
    case 'ar': return 'الكتابة الصوتية...';
    case 'fa': return 'تایپ صوتی...';
    case 'ru': return 'Голосовой ввод...';
    default: return 'Voice typing...';
  }
}

// Voice interruption error messages in different languages
function getVoiceInterruptionMessage(code: LanguageCode): string {
  switch (code) {
    case 'en': return 'Voice input interrupted';
    case 'pl': return 'Wprowadzanie głosowe przerwane';
    case 'it': return 'Inserimento vocale interrotto';
    case 'ar': return 'تم مقاطعة الإدخال الصوتي';
    case 'fa': return 'ورودی صوتی قطع شد';
    case 'ru': return 'Голосовой ввод прерван';
    default: return 'Voice input interrupted';
  }
}

function getVoicePermissionError(code: LanguageCode): string {
  switch (code) {
    case 'en': return 'Microphone permission denied';
    case 'pl': return 'Odmowa uprawnień do mikrofonu';
    case 'it': return 'Permesso microfono negato';
    case 'ar': return 'تم رفض إذن الميكروفون';
    case 'fa': return 'اجازه میکروفون رد شد';
    case 'ru': return 'Доступ к микрофону запрещён';
    default: return 'Microphone permission denied';
  }
}

function getVoiceRecognitionError(code: LanguageCode): string {
  switch (code) {
    case 'en': return 'Speech recognition failed';
    case 'pl': return 'Rozpoznawanie mowy nie powiodło się';
    case 'it': return 'Riconoscimento vocale fallito';
    case 'ar': return 'فشل التعرف على الكلام';
    case 'fa': return 'تشخیص گفتار失敗 شد';
    case 'ru': return 'Ошибка распознавания речи';
    default: return 'Speech recognition failed';
  }
}

// COMPONENTS
@Component
struct KeyPopup {
  @Prop info: LongPressInfo;
  @Prop hoveredChar: string | null;
  @Prop keyWidth: number;
  build() {
    Row({ space: 8 }) {
      ForEach((this.info.key.secondaryValue ?? '').split(','), (char: string) => {
        Text(char)
          .fontSize(22)
          .fontWeight(500)
          .fontColor(OpenBoardTheme.primaryText)
          .width(this.keyWidth)
          .height(50)
          .textAlign(TextAlign.Center)
          .backgroundColor(char === this.hoveredChar ? OpenBoardTheme.accent : OpenBoardTheme.specialKey)
          .borderRadius(8)
          .animation({ duration: 50 })
      })
    }
    .padding({ left: 8, right: 8, top: 6, bottom: 6 })
    .backgroundColor(OpenBoardTheme.surface)
    .borderRadius(12)
    .shadow({ radius: 10, color: '#60000000', offsetY: 4 })
  }
}

@Component
struct SingleKeyPopup {
  @Prop char: string;
  build() {
    Column() {
      Text(this.char)
        .fontSize(24)
        .fontWeight(400)
        .fontColor(OpenBoardTheme.primaryText)
    }
    .width(50)
    .height(60)
    .justifyContent(FlexAlign.Center)
    .backgroundColor(OpenBoardTheme.surface)
    .borderRadius(12)
  }
}

@Component
struct LanguageIndicator {
  @Prop languageName: string;
  build() {
    Column() {
      Text(this.languageName)
        .fontSize(28)
        .fontWeight(500)
        .fontColor('#FFFFFF')
        .padding({ left: 24, right: 24, top: 12, bottom: 12 })
    }
    .backgroundColor('rgba(0, 0, 0, 0.85)')
    .borderRadius(16)
    .shadow({ radius: 20, color: '#40000000', offsetY: 4 })
  }
}

@Component
struct VoiceInterruptionPopup {
  @Prop message: string;
  @Prop popupPosition: Position;

  build() {
    Column() {
      Text(this.message)
        .fontSize(16)
        .fontWeight(500)
        .fontColor(OpenBoardTheme.primaryText)
        .padding({ left: 16, right: 16, top: 12, bottom: 12 })
    }
    .backgroundColor(OpenBoardTheme.surface)
    .borderRadius(12)
    .border({ width: 1, color: OpenBoardTheme.divider })
    .shadow({ radius: 10, color: '#40000000', offsetY: 4 })
    .position({ x: this.popupPosition.x, y: this.popupPosition.y })
  }
}

@Component
struct KeyView {
  @Prop keyData: KeyData;
  @Prop shiftState: ShiftState;
  @Prop currentLanguage: LanguageCode;
  @Prop availableLanguages: LanguageCode[];
  onKeyPress: (key: KeyData) => void = () => {};
  onKeyLongPress: (key: KeyData, position: Position) => void = () => {};
  onKeyLongPressDrag: (position: Position) => void = () => {};
  onKeyLongPressEnd: () => void = () => {};
  onKeyContinuousPressStart: (key: KeyData) => void = () => {};
  onKeyContinuousPressEnd: (key: KeyData) => void = () => {};
  onKeyTouchTelemetry: (key: KeyData, position: Position, phase: KeyTouchPhase) => void = () => {};
  onSwipe?: (direction: LayoutSwipeDirection) => void;

  @State isPressed: boolean = false;
  @State private longPressTimer: number = -1;
  @State private longPressTriggered: boolean = false;
  @State private touchStartX: number = 0;
  @State private swipeHandled: boolean = false;

  @Prop enterKeyLabel: string;

  getDisplayValue(): string {
    if (this.keyData.action === KeyAction.Shift) {
      return '';
    }
    if (this.keyData.action === KeyAction.Enter) {
      if (this.enterKeyLabel === 'Search' || this.enterKeyLabel === 'Send') {
        return '';
      }
      return this.enterKeyLabel;
    }
    if (this.currentLanguage !== 'ar' && this.currentLanguage !== 'fa' && (this.shiftState === ShiftState.On || this.shiftState === ShiftState.CapsLock)) {
      return this.keyData.shiftValue ?? this.keyData.mainValue;
    }
    if (this.keyData.action === KeyAction.Space) {
      if (this.availableLanguages && this.availableLanguages.length > 1) {
        return getLanguageName(this.currentLanguage);
      }
      return '';
    }
    if (this.keyData.mainValue === 'icon') {
      return '';
    }
    return this.keyData.mainValue;
  }

  private isPrimaryActionKey(): boolean {
    return this.keyData.action === KeyAction.Enter;
  }

  private getKeyBackgroundColor(): string {
    if (this.isPressed) {
      return this.isPrimaryActionKey() ? OpenBoardTheme.accentPressed : OpenBoardTheme.keyPressed;
    }
    if (this.isPrimaryActionKey()) {
      return OpenBoardTheme.accent;
    }
    return this.keyData.action === KeyAction.Char ? OpenBoardTheme.regularKey : OpenBoardTheme.specialKey;
  }

  private getKeyTextSize(): number {
    return this.keyData.action === KeyAction.Space ? 14 : 20;
  }

  build() {
    Flex({ justifyContent: FlexAlign.Center, alignItems: ItemAlign.Center }) {
      Stack({ alignContent: Alignment.TopEnd }) {
        Column() {
          if (this.keyData.mainValue === 'icon' ||
            (this.keyData.action === KeyAction.Shift) ||
            (this.keyData.action === KeyAction.Enter && (this.enterKeyLabel === 'Search' || this.enterKeyLabel === 'Send'))) {

            if (this.keyData.action === KeyAction.Shift) {
              Image($r('app.media.shift_icon'))
                .width(20)
                .height(20)
                .fillColor(this.shiftState !== ShiftState.Off ? OpenBoardTheme.accent : OpenBoardTheme.primaryText)
            } else if (this.keyData.action === KeyAction.Enter && this.enterKeyLabel === 'Search') {
              Image($r('app.media.search_icon'))
                .width(20)
                .height(20)
                .fillColor(OpenBoardTheme.primaryText)
            } else if (this.keyData.action === KeyAction.Enter && this.enterKeyLabel === 'Send') {
              Image($r('app.media.send_icon'))
                .width(20)
                .height(20)
                .fillColor(OpenBoardTheme.primaryText)
            } else if (this.keyData.mainValue === 'icon') {
              Image($r('app.media.emoji_icon'))
                .width(20)
                .height(20)
                .fillColor(OpenBoardTheme.primaryText)
            }
          } else {
            Text(this.getDisplayValue())
              .fontSize(this.getKeyTextSize())
              .fontWeight(this.keyData.action === KeyAction.Char ? 400 : 500)
              .fontColor(OpenBoardTheme.primaryText)
              .maxLines(1)
              .textOverflow({ overflow: TextOverflow.Ellipsis })
          }
        }
        .width('100%')
        .height('100%')
        .justifyContent(FlexAlign.Center)
        .alignItems(HorizontalAlign.Center)

        if (this.keyData.secondaryValue) {
          Text(this.keyData.secondaryValue.split(',')[0])
            .fontSize(9)
            .fontColor(OpenBoardTheme.mutedText)
            .margin({ top: 3, right: 6 })
        }
      }
      .width('100%')
      .height('100%')
      .backgroundColor(this.getKeyBackgroundColor())
      .borderRadius(KEY_CORNER_RADIUS)
      .border({ width: this.keyData.action === KeyAction.Char ? 0.5 : 0, color: OpenBoardTheme.keyBorder })
      .shadow({
        radius: this.keyData.action === KeyAction.Char && !this.isPressed ? 1.5 : 0,
        color: OpenBoardTheme.keyShadow,
        offsetY: this.keyData.action === KeyAction.Char && !this.isPressed ? 1 : 0
      })
      .zIndex(this.isPressed ? 100 : 0)
      .scale({ x: this.isPressed ? 0.985 : 1, y: this.isPressed ? 0.985 : 1 })
      .animation({ duration: 80 })
      .onTouch((event: TouchEvent) => {
        const touch = event.touches[0];
        if (!touch) return;
        const currentPosition: Position = { x: touch.windowX, y: touch.windowY };

        if (event.type === TouchType.Down) {
          this.onKeyTouchTelemetry(this.keyData, currentPosition, 'down');
          this.touchStartX = currentPosition.x;
          this.isPressed = true;
          this.longPressTriggered = false;
          this.swipeHandled = false;

          this.longPressTimer = setTimeout((): void => {
            this.longPressTriggered = true;
            this.isPressed = false;

            if (this.keyData.action === KeyAction.Backspace) {
              this.onKeyContinuousPressStart(this.keyData);
            } else if (this.keyData.secondaryValue) {
              this.onKeyLongPress(this.keyData, currentPosition);
            } else if (this.keyData.action === KeyAction.Shift) {
              this.onKeyLongPress(this.keyData, currentPosition);
            }
          }, 500);
        }

        if (event.type === TouchType.Move) {
          this.onKeyTouchTelemetry(this.keyData, currentPosition, 'move');
          if (this.longPressTriggered && this.keyData.secondaryValue) {
            this.onKeyLongPressDrag(currentPosition);
          }

          if (this.keyData.action === KeyAction.Space) {
            const swipeDistance = Math.abs(currentPosition.x - this.touchStartX);
            const SWIPE_THRESHOLD = 30;

            if (!this.swipeHandled && swipeDistance > SWIPE_THRESHOLD) {
              clearTimeout(this.longPressTimer);
              this.longPressTriggered = true;
              this.swipeHandled = true;

              if (this.onSwipe) {
                const direction = (currentPosition.x - this.touchStartX) > 0
                  ? LayoutSwipeDirection.Right
                  : LayoutSwipeDirection.Left;
                this.onSwipe(direction);
              }

              this.isPressed = false;
            }
          }
        }

        if (event.type === TouchType.Up || event.type === TouchType.Cancel) {
          this.onKeyTouchTelemetry(this.keyData, currentPosition, event.type === TouchType.Up ? 'up' : 'cancel');
          clearTimeout(this.longPressTimer);
          if (this.longPressTriggered) {
            if (this.keyData.action === KeyAction.Backspace) {
              this.onKeyContinuousPressEnd(this.keyData);
            } else if (this.keyData.secondaryValue) {
              this.onKeyLongPressEnd();
            }
          } else if (event.type === TouchType.Up && !this.longPressTriggered) {
            this.onKeyPress(this.keyData);
          }

          this.isPressed = false;
          this.longPressTriggered = false;
          this.touchStartX = 0;
          this.swipeHandled = false;
        }
      })
    }
    .padding({ left: KEY_CONTENT_PADDING, right: KEY_CONTENT_PADDING, top: KEY_CONTENT_PADDING, bottom: KEY_CONTENT_PADDING })
  }
}

@Entry
@Component
struct Index {
  @State currentLanguage: LanguageCode = 'en';
  @State shiftState: ShiftState = ShiftState.Off;
  @State lastShiftTapTs: number = 0;
  @State keyboardLayout: KeyboardLayout = KeyboardLayout.Alphabet;
  @State longPressInfo: LongPressInfo | null = null;
  @State singleCharPopupInfo: SingleCharPopupInfo | null = null;
  @State hoveredChar: string | null = null;
  @State popupPosition: Position = { x: 0, y: 0 };
  @State popupKeyWidth: number = 40;
  @State rapidDeleteTimer: number = -1;
  @State enterKeyLabel: string = 'Enter';
  @State availableLanguages: LanguageCode[] = ['en'];

  @State showEmojiPanel: boolean = false;
  @State emojiCategory: string = 'Smileys & Emotion';
  private emojiScroller: Scroller = new Scroller();

  @State private previousWord: string = '';
  @State private currentWord: string = '';
  @State private suggestions: string[] = [];
  @State private nativePredictorMode: string = 'baseline';
  @State private nativeDiagnosticsSnippet: string = '';
  @State private lastNativeDiagnosticsLogged: string = '';
  @State private showNativeDebugPopup: boolean = false;
  @State private nativeDebugWord: string = '';
  @State private nativeDebugSuggestionsLine: string = '';
  @State private nativeDebugDiagnosticsFull: string = '';
  @State private nativeDebugPredDirect: string = '';
  @State private nativeDebugPredHandoff: string = '';
  @State private nativeDebugWillAutocorrect: boolean = false;

  @State private isRecording: boolean = false;
  @State private languageIndicatorOpacity: number = 0;

  // Speech recognition states
  @State private recognizedText: string = '';
  @State private isListening: boolean = false;
  @State private speechError: string = '';

  // Voice interruption popup state
  @State private showInterruptionPopup: boolean = false;
  @State private interruptionMessage: string = '';
  @State private interruptionPopupPosition: Position = { x: 0, y: 0 };
  private interruptionPopupTimer: number = -1;

  // Speech recognition simulation
  private speechRecognitionTimer: number = -1;
  private currentRecognitionIndex: number = 0;

  // Timer for inactivity detection
  @State private inactivityTimer: number = -1;
  @State private showPreTypingToolbar: boolean = true;
  @State private lastTypingActivity: number = Date.now();
  @StorageLink('hoskeySystemPanelBottomInsetPx') private systemPanelBottomInsetPx: number = 0;
  @StorageLink('hoskeyPanelHeightPx') private panelHeightPx: number = 0;
  @StorageLink('hoskeyPanelWidthPx') private panelWidthPx: number = 0;

  // Show interruption popup
  private showVoiceInterruptionPopup(message: string): void {
    this.interruptionMessage = message;
    this.showInterruptionPopup = true;

    // Position the popup near the microphone button
    this.interruptionPopupPosition = {
      x: this.getScreenWidthVp() - 150,
      y: 100
    };

    // Auto-hide after 3 seconds
    if (this.interruptionPopupTimer !== -1) {
      clearTimeout(this.interruptionPopupTimer);
    }
    this.interruptionPopupTimer = setTimeout((): void => {
      this.showInterruptionPopup = false;
      this.interruptionPopupTimer = -1;
    }, 3000);
  }

  // Speech recognition methods
  private async startVoiceRecognition(): Promise<void> {
    try {
      console.info('Starting voice recognition for language:', this.currentLanguage);

      // Check microphone permission (simulated)
      const permissionGranted = await this.requestMicrophonePermission();
      if (!permissionGranted) {
        this.speechError = getVoicePermissionError(this.currentLanguage);
        this.isRecording = false;
        this.showVoiceInterruptionPopup(this.speechError);
        return;
      }

      this.isListening = true;
      this.recognizedText = '';
      this.speechError = '';
      this.currentRecognitionIndex = 0;

      // Start speech recognition simulation
      this.simulateSpeechRecognition();

    } catch (error) {
      console.error('Failed to start voice recognition:', error);
      this.speechError = getVoiceRecognitionError(this.currentLanguage);
      this.isRecording = false;
      this.isListening = false;
      this.showVoiceInterruptionPopup(this.speechError);
    }
  }

  private async stopVoiceRecognition(): Promise<void> {
    try {
      console.info('Stopping voice recognition');
      this.isListening = false;

      // Clear any ongoing recognition
      if (this.speechRecognitionTimer !== -1) {
        clearTimeout(this.speechRecognitionTimer);
        this.speechRecognitionTimer = -1;
      }

      // Insert recognized text
      if (this.recognizedText.trim()) {
        keyboardController.insertText(this.recognizedText + ' ');
        this.onTextTyped(this.recognizedText + ' ');
      }

      this.recognizedText = '';
      this.currentRecognitionIndex = 0;

    } catch (error) {
      console.error('Error stopping voice recognition:', error);
    }
  }

  private async requestMicrophonePermission(): Promise<boolean> {
    try {
      console.info('Requesting microphone permission');
      return true; // Assume granted for demonstration
    } catch (error) {
      console.error('Permission request error:', error);
      return false;
    }
  }

  // Simulated speech recognition for demonstration
  private simulateSpeechRecognition(): void {
    // Define simulated results with proper typing
    const simulatedResults: SpeechRecognitionResult[] = [
      { text: 'Hello', delay: 1000 },
      { text: 'Hello world', delay: 2000 },
      { text: 'Hello world this', delay: 3000 },
      { text: 'Hello world this is', delay: 4000 },
      { text: 'Hello world this is a', delay: 5000 },
      { text: 'Hello world this is a test', delay: 6000 }
    ];

    const processNextResult = (): void => {
      if (!this.isListening || this.currentRecognitionIndex >= simulatedResults.length) {
        if (this.isListening) {
          // Auto-stop after simulation completes
          this.isRecording = false;
          this.stopVoiceRecognition();
        }
        return;
      }

      const result: SpeechRecognitionResult = simulatedResults[this.currentRecognitionIndex];
      this.speechRecognitionTimer = setTimeout((): void => {
        if (this.isListening) {
          this.recognizedText = result.text;
          console.info('Speech recognition result:', result.text);
          this.currentRecognitionIndex++;
          processNextResult();
        }
      }, result.delay - (this.currentRecognitionIndex > 0 ? simulatedResults[this.currentRecognitionIndex - 1].delay : 0));
    };

    processNextResult();
  }

  // Enhanced microphone click handler
  private handleMicrophoneClick(): void {
    if (this.isRecording) {
      // Stop recording
      this.isRecording = false;
      this.stopVoiceRecognition();
      console.info('Voice typing stopped by user');
    } else {
      // Start recording
      this.isRecording = true;
      this.showEmojiPanel = false;
      this.resetInactivityTimer();
      this.startVoiceRecognition();
      console.info('Voice typing started');
    }
  }

  // Handle voice interruption with popup
  private handleVoiceInterruption(interruptionType: 'manual' | 'clipboard' | 'error'): void {
    if (this.isRecording) {
      this.isRecording = false;
      this.stopVoiceRecognition();

      let message = '';
      switch (interruptionType) {
        case 'manual':
          message = getVoiceInterruptionMessage(this.currentLanguage) + ' - ' +
            (this.currentLanguage === 'en' ? 'Manual input' :
              this.currentLanguage === 'pl' ? 'Wpis ręczny' :
                this.currentLanguage === 'it' ? 'Input manuale' :
                  this.currentLanguage === 'ar' ? 'إدخال يدوي' :
                    this.currentLanguage === 'fa' ? 'ورودی دستی' :
                      this.currentLanguage === 'ru' ? 'Ручной ввод' : 'Manual input');
          break;
        case 'clipboard':
          message = getVoiceInterruptionMessage(this.currentLanguage) + ' - ' +
            (this.currentLanguage === 'en' ? 'Clipboard access' :
              this.currentLanguage === 'pl' ? 'Dostęp do schowka' :
                this.currentLanguage === 'it' ? 'Accesso agli appunti' :
                  this.currentLanguage === 'ar' ? 'الوصول إلى الحافظة' :
                    this.currentLanguage === 'fa' ? 'دسترسی به کلیپ بورد' :
                      this.currentLanguage === 'ru' ? 'Доступ к буферу обмена' : 'Clipboard access');
          break;
        case 'error':
          message = getVoiceRecognitionError(this.currentLanguage);
          break;
      }

      console.info('Voice typing interrupted:', message);
      this.showVoiceInterruptionPopup(message);
    }
  }

  onPageShow(): void {
    console.info('OpenBoard Keyboard: UI onPageShow called');
    this.availableLanguages = ALLOWED_LANGUAGES.map((c: string): LanguageCode => c as LanguageCode);
    if (!this.availableLanguages.includes(this.currentLanguage)) {
      this.currentLanguage = this.availableLanguages[0];
    }
    keyboardController.setNativeLayoutDescriptor(this.getNativeLayoutDescriptor());
    this.startInactivityTimer();
  }

  aboutToAppear(): void {
    console.info('OpenBoard Keyboard: UI aboutToAppear called');
    keyboardController.setEnterKeyTypeCallback((enterKeyType: number): void => {
      this.updateEnterKeyLabel(enterKeyType);
    });
    this.updateEnterKeyLabel(keyboardController.getCurrentEnterKeyType());
    this.nativePredictorMode = keyboardController.getNativePredictorDebugModeLabel();
    keyboardController.setNativeLayoutDescriptor(this.getNativeLayoutDescriptor());
  }

  aboutToDisappear(): void {
    if (this.rapidDeleteTimer !== -1) {
      clearInterval(this.rapidDeleteTimer);
    }
    if (this.inactivityTimer !== -1) {
      clearTimeout(this.inactivityTimer);
    }
    if (this.speechRecognitionTimer !== -1) {
      clearTimeout(this.speechRecognitionTimer);
    }
    if (this.interruptionPopupTimer !== -1) {
      clearTimeout(this.interruptionPopupTimer);
    }
    // Clean up speech recognition
    if (this.isRecording) {
      this.stopVoiceRecognition();
    }
  }

  private startInactivityTimer(): void {
    if (this.inactivityTimer !== -1) {
      clearTimeout(this.inactivityTimer);
    }
    this.inactivityTimer = setTimeout((): void => {
      const timeSinceLastActivity = Date.now() - this.lastTypingActivity;
      if (!this.isRecording && !this.showEmojiPanel &&
        this.currentWord.length === 0 && this.suggestions.length === 0 &&
        timeSinceLastActivity >= 5000) {
        this.showPreTypingToolbar = true;
      }
      this.startInactivityTimer(); // Continue checking
    }, 1000); // Check every second
  }

  private resetInactivityTimer(): void {
    this.lastTypingActivity = Date.now();
    this.showPreTypingToolbar = false;
  }

  private keepSuggestionToolbarVisible(): void {
    if (this.currentWord.length > 0 || this.suggestions.length > 0) {
      this.showPreTypingToolbar = false;
    }
  }

  private updateEnterKeyLabel(enterKeyType: number): void {
    switch (enterKeyType) {
      case inputMethodEngine.ENTER_KEY_TYPE_GO:
        this.enterKeyLabel = 'Go';
        break;
      case inputMethodEngine.ENTER_KEY_TYPE_SEARCH:
        this.enterKeyLabel = 'Search';
        break;
      case inputMethodEngine.ENTER_KEY_TYPE_SEND:
        this.enterKeyLabel = 'Send';
        break;
      case inputMethodEngine.ENTER_KEY_TYPE_NEXT:
        this.enterKeyLabel = 'Next';
        break;
      case inputMethodEngine.ENTER_KEY_TYPE_DONE:
        this.enterKeyLabel = 'Done';
        break;
      case inputMethodEngine.ENTER_KEY_TYPE_UNSPECIFIED:
      default:
        this.enterKeyLabel = 'Enter';
        break;
    }
  }

  private handleRapidDeleteStart(): void {
    if (this.rapidDeleteTimer !== -1) {
      clearInterval(this.rapidDeleteTimer);
    }
    this.rapidDeleteTimer = setInterval((): void => {
      keyboardController.deleteForward(1);
      this.onBackspaceTyped();
    }, 100);
  }

  private handleRapidDeleteEnd(): void {
    if (this.rapidDeleteTimer !== -1) {
      clearInterval(this.rapidDeleteTimer);
      this.rapidDeleteTimer = -1;
    }
  }

  private updatePopupMetrics(touchPosition: Position): void {
    if (this.longPressInfo && this.longPressInfo.key.secondaryValue) {
      this.hoveredChar = this.longPressInfo.key.secondaryValue.split(',')[0];
      this.popupPosition = {
        x: this.longPressInfo.initialPosition.x - (this.longPressInfo.key.secondaryValue.split(',').length * this.popupKeyWidth / 2),
        y: this.longPressInfo.initialPosition.y - 70
      };
    }
  }

  private handleLongPress(key: KeyData, position: Position): void {
    if (key.action === KeyAction.Shift) {
      this.shiftState = ShiftState.CapsLock;
      return;
    }
    if (key.secondaryValue) {
      this.longPressInfo = { key: key, initialPosition: position };
      this.updatePopupMetrics(position);
    }
  }

  private handleLongPressDrag(position: Position): void {
    if (!this.longPressInfo || !this.longPressInfo.key.secondaryValue) return;

    const secondaryValues = this.longPressInfo.key.secondaryValue.split(',');
    const popupWidth = secondaryValues.length * this.popupKeyWidth;
    const startX = this.popupPosition.x;

    const relativeX = position.x - startX;
    let index = Math.floor(relativeX / this.popupKeyWidth);

    if (index < 0) index = 0;
    if (index >= secondaryValues.length) index = secondaryValues.length - 1;

    this.hoveredChar = secondaryValues[index];
  }

  private handleLongPressEnd(): void {
    if (this.longPressInfo && this.hoveredChar) {
      keyboardController.insertText(this.hoveredChar);
      this.onTextTyped(this.hoveredChar);
    }
    this.longPressInfo = null;
    this.hoveredChar = null;
  }

  private getNativeLayoutDescriptor(): string {
    let layoutName: string = 'alphabet';
    switch (this.keyboardLayout) {
      case KeyboardLayout.Symbols1:
        layoutName = 'symbols1';
        break;
      case KeyboardLayout.Symbols2:
        layoutName = 'symbols2';
        break;
      case KeyboardLayout.Alphabet:
      default:
        layoutName = 'alphabet';
        break;
    }
    return this.currentLanguage + ':' + layoutName;
  }

  private handleKeyTouchTelemetry(key: KeyData, position: Position, phase: KeyTouchPhase): void {
    keyboardController.setNativeLayoutDescriptor(this.getNativeLayoutDescriptor());
    keyboardController.recordNativeTouchPoint(key, position.x, position.y, phase);
  }

  handleKeyPress(key: KeyData): void {
    if (key.action !== KeyAction.EMOJI_PANEL) {
      this.resetInactivityTimer();
    }

    if (this.isRecording) {
      this.handleVoiceInterruption('manual');
    }

    if (key.action === KeyAction.EMOJI_PANEL) {
      this.showEmojiPanel = !this.showEmojiPanel;
      return;
    }

    switch (key.action) {
      case KeyAction.Char:
        let text = (this.currentLanguage !== 'ar' && this.currentLanguage !== 'fa' && (this.shiftState === ShiftState.On || this.shiftState === ShiftState.CapsLock))
          ? key.shiftValue ?? key.mainValue
          : key.mainValue;
        keyboardController.insertText(text);
        this.onTextTyped(text);
        if (this.shiftState === ShiftState.On) {
          this.shiftState = ShiftState.Off;
        }
        break;
      case KeyAction.Space:
        this.maybeApplyNativeAutocorrect();
        keyboardController.insertText(' ');
        this.onBoundaryTyped(' ');
        break;
      case KeyAction.Enter:
        this.maybeApplyNativeAutocorrect();
        keyboardController.sendEnterKey();
        this.onBoundaryTyped('\n');
        break;
      case KeyAction.Backspace:
        keyboardController.deleteForward(1);
        this.onBackspaceTyped();
        break;
      case KeyAction.Shift:
        const now = Date.now();
        if (this.shiftState === ShiftState.CapsLock) {
          this.shiftState = ShiftState.Off;
          this.lastShiftTapTs = now;
          break;
        }
        if (now - this.lastShiftTapTs < 400) {
          this.shiftState = ShiftState.CapsLock;
          this.lastShiftTapTs = 0;
          break;
        }
        this.shiftState = (this.shiftState === ShiftState.Off) ?
          ShiftState.On : ShiftState.Off;
        this.lastShiftTapTs = now;
        break;
      case KeyAction.SwitchAlphabet:
      case KeyAction.SwitchSymbols:
        this.keyboardLayout = (this.keyboardLayout === KeyboardLayout.Alphabet) ?
          KeyboardLayout.Symbols1 : KeyboardLayout.Alphabet;
        keyboardController.clearNativeTouchContext();
        keyboardController.setNativeLayoutDescriptor(this.getNativeLayoutDescriptor());
        break;
      case KeyAction.SwitchSymbolsAlt:
        this.keyboardLayout = KeyboardLayout.Symbols2;
        keyboardController.clearNativeTouchContext();
        keyboardController.setNativeLayoutDescriptor(this.getNativeLayoutDescriptor());
        break;
    }
  }

  // PASTE CURRENT CLIPBOARD ITEM ONLY
  private async handleClipboardClick(): Promise<void> {
    this.resetInactivityTimer();

    if (this.isRecording) {
      this.handleVoiceInterruption('clipboard');
    }

    try {
      const systemPasteboard = pasteboard.getSystemPasteboard();
      const hasData = systemPasteboard.hasDataSync();

      if (hasData) {
        const data = await systemPasteboard.getData();
        const text = data.getPrimaryText();

        if (text && text.length > 0) {
          keyboardController.insertText(text);
          this.onTextTyped(text);
          console.info('OpenBoard: Pasted clipboard text');
        }
      }
    } catch (e) {
      console.error('OpenBoard: Clipboard paste failed', e);
    }
  }

  handleSwipe(direction: LayoutSwipeDirection): void {
    if (this.availableLanguages.length <= 1) {
      console.warn('OpenBoard Keyboard: Only one language enabled, cannot switch.');
      return;
    }

    let currentIndex = this.availableLanguages.indexOf(this.currentLanguage);

    if (direction === LayoutSwipeDirection.Right) {
      currentIndex = (currentIndex - 1 + this.availableLanguages.length) % this.availableLanguages.length;
    } else {
      currentIndex = (currentIndex + 1) % this.availableLanguages.length;
    }

    this.currentLanguage = this.availableLanguages[currentIndex];
    keyboardController.clearNativeTouchContext();
    keyboardController.setNativeLayoutDescriptor(this.getNativeLayoutDescriptor());
    console.info('OpenBoard Keyboard: Switched to language:', this.currentLanguage);
  }

  private normalizeSuggestions(words: string[], limit: number = SUGGESTION_FETCH_LIMIT): string[] {
    const normalized: string[] = [];
    const seen: Set<string> = new Set();
    const currentLower: string = this.currentWord.toLowerCase();

    words.forEach((raw: string): void => {
      if (!raw) {
        return;
      }
      const word: string = raw.trim();
      if (word.length === 0) {
        return;
      }
      const key: string = word.toLowerCase();
      if (key === currentLower || seen.has(key)) {
        return;
      }
      seen.add(key);
      normalized.push(word);
    });

    return normalized.slice(0, limit);
  }

  private getFallbackSuggestions(limit: number = SUGGESTION_FETCH_LIMIT): string[] {
    return this.normalizeSuggestions(
      predictionModel.predict(this.currentLanguage, this.previousWord, this.currentWord, limit),
      limit
    );
  }

  private updateSuggestions(): void {
    let fallbackSugs: string[] = [];
    try {
      fallbackSugs = this.getFallbackSuggestions(SUGGESTION_FETCH_LIMIT);

      // Нативный hosdict engine — primary (только русский/ARM64), fallback добивает список до длинной ленты.
      if (keyboardController.isNativeEngineAvailable() && this.currentWord.length > 0) {
        const locale = this.currentLanguage === 'ru' ? 'ru_RU' : 'en_US';
        const response: NativeSuggestResponse = keyboardController.getNativeSuggestResponse(this.currentWord, this.previousWord, locale);
        const nativeSugs: string[] = response.suggestions.map((suggestion: NativeSuggestion): string => suggestion.text);
        this.nativeDiagnosticsSnippet = this.summarizeNativeDiagnostics(response.diagnostics);
        this.updateNativeDebugState(response);
        if (this.nativePredictorMode === 'verified-split' && response.diagnostics &&
          response.diagnostics !== this.lastNativeDiagnosticsLogged) {
          this.lastNativeDiagnosticsLogged = response.diagnostics;
          diagLog.add('[VS] ' + this.currentWord + ' => ' + response.diagnostics);
        }
        this.suggestions = this.normalizeSuggestions(nativeSugs.concat(fallbackSugs), SUGGESTION_FETCH_LIMIT);
        this.keepSuggestionToolbarVisible();
        return;
      }

      this.nativeDiagnosticsSnippet = '';
      this.resetNativeDebugState();
      this.suggestions = fallbackSugs;
      this.keepSuggestionToolbarVisible();
    } catch (e) {
      console.warn('Prediction model error:', e);
      this.nativeDiagnosticsSnippet = '';
      this.resetNativeDebugState();
      this.suggestions = fallbackSugs;
      this.keepSuggestionToolbarVisible();
    }
  }

  private extractDiagnosticField(diagnostics: string, key: string): string {
    const prefix: string = key + '=';
    const parts: string[] = diagnostics.split(',');
    for (const part of parts) {
      const trimmed: string = part.trim();
      if (trimmed.startsWith(prefix)) {
        return trimmed.substring(prefix.length);
      }
    }
    return '';
  }

  private updateNativeDebugState(response: NativeSuggestResponse): void {
    this.nativeDebugWord = this.currentWord;
    const suggestions: string[] = response.suggestions
      .slice(0, 5)
      .map<string>((suggestion: NativeSuggestion): string => suggestion.text);
    this.nativeDebugSuggestionsLine = suggestions.join(' | ');
    this.nativeDebugDiagnosticsFull = response.diagnostics;
    this.nativeDebugPredDirect = this.extractDiagnosticField(response.diagnostics, 'pred_d');
    this.nativeDebugPredHandoff = this.extractDiagnosticField(response.diagnostics, 'pred_h');
    this.nativeDebugWillAutocorrect = response.willAutocorrect;
  }

  private resetNativeDebugState(): void {
    this.showNativeDebugPopup = false;
    this.nativeDebugWord = '';
    this.nativeDebugSuggestionsLine = '';
    this.nativeDebugDiagnosticsFull = '';
    this.nativeDebugPredDirect = '';
    this.nativeDebugPredHandoff = '';
    this.nativeDebugWillAutocorrect = false;
  }

  private summarizeNativeDiagnostics(diagnostics: string): string {
    if (!diagnostics || this.nativePredictorMode === 'baseline') {
      return '';
    }
    const parts: string[] = diagnostics.split(',').map((part: string): string => part.trim());
    const kept: string[] = parts.filter((part: string): boolean =>
      part.startsWith('pred_') || part.startsWith('trie=') || part.startsWith('succinct=')
    );
    if (kept.length === 0) {
      return diagnostics;
    }
    return kept.join(' | ');
  }

  private isBoundaryChar(ch: string): boolean {
    if (!ch) return false;
    const boundaries: string[] = [' ', '\n', ',', '.', '!', '?', ';', ':', '/', '\\', '-', '_', '"', "'", '(', ')', '[', ']', '<', '>'];
    return boundaries.indexOf(ch) >= 0;
  }

  // Autocc: a sentence-ending punctuation arms auto-capitalization of the next word.
  private endsSentence(ch: string): boolean {
    return ch === '.' || ch === '!' || ch === '?';
  }

  private onTextTyped(text: string): void {
    if (!text) return;
    if (this.isBoundaryChar(text)) {
      this.onBoundaryTyped(text);
      return;
    }
    this.currentWord = this.currentWord + text;
    this.updateSuggestions();
    this.resetInactivityTimer();
  }

  private toggleNativePredictorMode(): void {
    const reopened = keyboardController.toggleNativeVerifiedSplitMode();
    this.nativePredictorMode = keyboardController.getNativePredictorDebugModeLabel();
    this.showNativeDebugPopup = false;
    console.info('OpenBoard Keyboard: predictor mode -> ' + this.nativePredictorMode + ', reopened=' + reopened);
    if (this.currentWord.length > 0) {
      this.updateSuggestions();
    }
  }

  private toggleNativeDebugPopup(): void {
    if (!this.nativeDiagnosticsSnippet || this.nativePredictorMode === 'baseline') {
      return;
    }
    this.showNativeDebugPopup = !this.showNativeDebugPopup;
  }

  @Builder
  buildPredictorDebugChip() {
    if (keyboardController.isNativeEngineAvailable() && this.currentLanguage === 'ru') {
      Text(this.nativePredictorMode === 'verified-split' ? 'VS' : 'B')
        .fontSize(11)
        .fontWeight(FontWeight.Bold)
        .fontColor(this.nativePredictorMode === 'verified-split' ? '#FFFFFF' : OpenBoardTheme.primaryText)
        .width(24)
        .height(26)
        .textAlign(TextAlign.Center)
        .backgroundColor(this.nativePredictorMode === 'verified-split' ? OpenBoardTheme.accent : OpenBoardTheme.suggestionChip)
        .borderRadius(14)
        .border({ width: 0, color: this.nativePredictorMode === 'verified-split' ? OpenBoardTheme.accentPressed : OpenBoardTheme.keyBorder })
        .onClick((): void => { this.toggleNativePredictorMode(); })
    }
  }

  private maybeApplyNativeAutocorrect(): void {
    if (!this.currentWord || !keyboardController.isNativeEngineAvailable()) {
      return;
    }

    const locale = this.currentLanguage === 'ru' ? 'ru_RU' : 'en_US';
    const response: NativeSuggestResponse = keyboardController.getNativeSuggestResponse(this.currentWord, this.previousWord, locale);
    const replacement = response.suggestions[0]?.text ?? '';

    if (!response.ok || !response.willAutocorrect || !replacement || replacement === this.currentWord) {
      return;
    }

    keyboardController.deleteForward(this.currentWord.length);
    keyboardController.insertText(replacement);
    this.currentWord = replacement;
  }

  private onBoundaryTyped(boundary: string): void {
    if (this.currentWord) {
      predictionModel.recordWord(this.currentLanguage, this.previousWord, this.currentWord);
      this.previousWord = this.currentWord;
      this.currentWord = '';
    }
    // Autocc: auto-capitalize the next word after a sentence boundary (. ! ?).
    if (this.shiftState === ShiftState.Off && this.endsSentence(boundary)) {
      this.shiftState = ShiftState.On;
    }
    keyboardController.clearNativeTouchContext();
    keyboardController.setNativeLayoutDescriptor(this.getNativeLayoutDescriptor());
    this.nativeDiagnosticsSnippet = '';
    this.resetNativeDebugState();
    this.updateSuggestions();
    this.resetInactivityTimer();
  }

  private onBackspaceTyped(): void {
    if (this.currentWord && this.currentWord.length > 0) {
      this.currentWord = this.currentWord.substring(0, this.currentWord.length - 1);
      this.updateSuggestions();
      this.resetInactivityTimer();
      if (this.currentWord.length === 0) {
        keyboardController.clearNativeTouchContext();
      }
    }

    if (keyboardController.isBufferEmpty()) {
      this.previousWord = '';
      this.currentWord = '';
      // Autocc: an empty field is a sentence start — capitalize the first letter.
      if (this.shiftState === ShiftState.Off) {
        this.shiftState = ShiftState.On;
      }
      this.nativeDiagnosticsSnippet = '';
      this.resetNativeDebugState();
      this.updateSuggestions();
      this.showPreTypingToolbar = this.suggestions.length === 0;
    } else {
      this.showPreTypingToolbar = false;
    }
  }

  private pickSuggestion(word: string): void {
    this.resetInactivityTimer();
    keyboardController.deleteForward(this.currentWord.length);
    keyboardController.insertText(word);
    keyboardController.insertText(' ');

    this.onBoundaryTyped(' ');
    this.previousWord = word;
    this.currentWord = '';
    this.nativeDiagnosticsSnippet = '';
    this.resetNativeDebugState();
    this.updateSuggestions();
  }

  private getScreenWidthVp(): number {
    try {
      const displayInfo = display.getDefaultDisplaySync();
      return this.getUIContext().px2vp(displayInfo.width);
    } catch (e) {
      console.error('Failed to get screen width:', e);
      return 360;
    }
  }

  private getSystemBottomInsetVp(): number {
    if (this.systemPanelBottomInsetPx <= 0) {
      return 0;
    }
    try {
      return Math.min(22, Math.max(0, this.getUIContext().px2vp(this.systemPanelBottomInsetPx)));
    } catch (e) {
      return 0;
    }
  }

  private getSuggestionChipWidthVp(): number {
    const rawWidth: number = Math.floor((this.getScreenWidthVp() - 48) / 5);
    return Math.max(58, Math.min(78, rawWidth));
  }

  private shouldShowSuggestionToolbarActions(): boolean {
    return this.currentWord.length === 0 && this.suggestions.length === 0;
  }

  private shouldShowPreTypingToolbar(): boolean {
    return this.showPreTypingToolbar && this.currentWord.length === 0 &&
      this.suggestions.length === 0 && !this.isRecording && !this.showEmojiPanel;
  }

  @Builder
  buildSuggestionStrip() {
    Scroll() {
      Row({ space: SUGGESTION_ITEM_GAP }) {
        if (this.suggestions.length === 0) {
          Text(this.currentWord.length > 0 ? this.currentWord : '')
            .fontSize(12)
            .fontWeight(500)
            .fontColor(OpenBoardTheme.mutedText)
            .height(34)
            .padding({ left: 12, right: 12 })
            .textAlign(TextAlign.Center)
            .maxLines(1)
            .backgroundColor(OpenBoardTheme.toolbarSurface)
            .borderRadius(17)
            .border({ width: 0, color: OpenBoardTheme.divider })
        } else {
          ForEach(this.suggestions, (suggestion: string, index: number) => {
            if (index < SUGGESTION_FULL_COUNT) {
              Text(suggestion)
                .fontSize(12)
                .fontWeight(500)
                .fontColor(OpenBoardTheme.primaryText)
                .height(34)
                .padding({ left: 11, right: 11 })
                .textAlign(TextAlign.Center)
                .maxLines(1)
                .backgroundColor(OpenBoardTheme.suggestionChipPressed)
                .borderRadius(17)
                .border({ width: 0, color: OpenBoardTheme.divider })
                .onClick((): void => {
                  this.pickSuggestion(suggestion);
                })
            } else {
              Text(suggestion)
                .fontSize(11)
                .fontWeight(500)
                .fontColor(OpenBoardTheme.secondaryText)
                .width(this.getSuggestionChipWidthVp())
                .height(34)
                .textAlign(TextAlign.Center)
                .maxLines(1)
                .textOverflow({ overflow: TextOverflow.Ellipsis })
                .backgroundColor(OpenBoardTheme.suggestionChip)
                .borderRadius(17)
                .border({ width: 0, color: OpenBoardTheme.divider })
                .onClick((): void => {
                  this.pickSuggestion(suggestion);
                })
            }
          })
        }
      }
      .height(40)
      .alignItems(VerticalAlign.Center)
      .padding({ left: 2, right: 2 })
    }
    .scrollable(ScrollDirection.Horizontal)
    .scrollBar(BarState.Off)
    .edgeEffect(EdgeEffect.Spring)
    .width('100%')
    .height(40)
  }

  // Build different toolbar variants using @Builder
  @Builder
  buildStandardToolbar() {
    Row({ space: 4 }) {
      if (this.shouldShowSuggestionToolbarActions()) {
        Column() {
          Image($r('app.media.close_icon'))
            .width(15)
            .height(15)
            .fillColor(OpenBoardTheme.secondaryText)
        }
        .width(TOOLBAR_BUTTON_SIZE)
        .height(TOOLBAR_BUTTON_SIZE)
        .justifyContent(FlexAlign.Center)
        .alignItems(HorizontalAlign.Center)
        .borderRadius(14)
        .backgroundColor(OpenBoardTheme.suggestionChip)
        .border({ width: 0, color: OpenBoardTheme.divider })
        .onClick((): void => { keyboardController.hideKeyboard(); })

        this.buildPredictorDebugChip()
      }

      Flex({ justifyContent: FlexAlign.Start, alignItems: ItemAlign.Center }) {
        this.buildSuggestionStrip()
      }
      .layoutWeight(1)
      .height(40)

      if (this.shouldShowSuggestionToolbarActions()) {
        Column() {
          Image($r('app.media.mic_icon'))
            .width(17)
            .height(17)
            .fillColor(OpenBoardTheme.primaryText)
        }
        .width(TOOLBAR_BUTTON_SIZE)
        .height(TOOLBAR_BUTTON_SIZE)
        .justifyContent(FlexAlign.Center)
        .alignItems(HorizontalAlign.Center)
        .borderRadius(14)
        .backgroundColor(this.isRecording ? OpenBoardTheme.accent : OpenBoardTheme.specialKey)
        .border({ width: 0, color: this.isRecording ? OpenBoardTheme.accentPressed : OpenBoardTheme.divider })
        .onClick((): void => { this.handleMicrophoneClick(); })
      }
    }
    .width('100%')
    .height('100%')
  }

  @Builder
  buildPreTypingToolbar() {
    Row({ space: 4 }) {
      // CLOSE ICON
      Column() {
        Image($r('app.media.close_icon'))
          .width(15)
          .height(15)
          .fillColor(OpenBoardTheme.secondaryText)
      }
      .width(TOOLBAR_BUTTON_SIZE)
      .height(TOOLBAR_BUTTON_SIZE)
      .justifyContent(FlexAlign.Center)
      .alignItems(HorizontalAlign.Center)
      .borderRadius(14)
      .backgroundColor(OpenBoardTheme.suggestionChip)
      .border({ width: 0, color: OpenBoardTheme.divider })
      .onClick((): void => { keyboardController.hideKeyboard(); })

      this.buildPredictorDebugChip()

      // LEFT SPACER for centering
      Flex().layoutWeight(1)

      // CLIPBOARD ICON - CENTERED
      Column() {
        Image($r('app.media.clipboard_icon'))
          .width(17)
          .height(17)
          .fillColor(OpenBoardTheme.primaryText)
      }
      .width(TOOLBAR_BUTTON_SIZE)
      .height(TOOLBAR_BUTTON_SIZE)
      .justifyContent(FlexAlign.Center)
      .alignItems(HorizontalAlign.Center)
      .borderRadius(14)
      .backgroundColor(OpenBoardTheme.specialKey)
      .border({ width: 0, color: OpenBoardTheme.divider })
      .onClick((): void => { this.handleClipboardClick(); })

      // RIGHT SPACER for centering
      Flex().layoutWeight(1)

      // MICROPHONE
      Column() {
        Image($r('app.media.mic_icon'))
          .width(17)
          .height(17)
          .fillColor(OpenBoardTheme.primaryText)
      }
      .width(TOOLBAR_BUTTON_SIZE)
      .height(TOOLBAR_BUTTON_SIZE)
      .justifyContent(FlexAlign.Center)
      .alignItems(HorizontalAlign.Center)
      .borderRadius(14)
      .backgroundColor(this.isRecording ? OpenBoardTheme.accent : OpenBoardTheme.specialKey)
      .border({ width: 0, color: this.isRecording ? OpenBoardTheme.accentPressed : OpenBoardTheme.divider })
      .onClick((): void => { this.handleMicrophoneClick(); })
    }
    .width('100%')
    .height('100%')
  }

  @Builder
  buildEmojiToolbar() {
    Row({ space: 4 }) {
      // RETURN TO KEYBOARD BUTTON
      Column() {
        Image($r('app.media.return_icon'))
          .width(17)
          .height(17)
          .fillColor(OpenBoardTheme.primaryText)
      }
      .width(TOOLBAR_BUTTON_SIZE)
      .height(TOOLBAR_BUTTON_SIZE)
      .justifyContent(FlexAlign.Center)
      .alignItems(HorizontalAlign.Center)
      .borderRadius(14)
      .backgroundColor(OpenBoardTheme.specialKey)
      .border({ width: 0, color: OpenBoardTheme.divider })
      .onClick((): void => {
        this.showEmojiPanel = false;
      })

      // EMPTY SPACE
      Flex().layoutWeight(1)

      // EMPTY SPACE
      Flex().layoutWeight(1)
    }
    .width('100%')
    .height('100%')
  }

  @Builder
  buildVoiceToolbar() {
    Row({ space: 4 }) {
      // CLOSE ICON
      Column() {
        Image($r('app.media.close_icon'))
          .width(15)
          .height(15)
          .fillColor(OpenBoardTheme.secondaryText)
      }
      .width(TOOLBAR_BUTTON_SIZE)
      .height(TOOLBAR_BUTTON_SIZE)
      .justifyContent(FlexAlign.Center)
      .alignItems(HorizontalAlign.Center)
      .borderRadius(14)
      .backgroundColor(OpenBoardTheme.suggestionChip)
      .border({ width: 0, color: OpenBoardTheme.divider })
      .onClick((): void => {
        keyboardController.hideKeyboard();
        if (this.isRecording) {
          this.stopVoiceRecognition();
        }
      })

      // VOICE RECORDING STATUS
      Flex({ justifyContent: FlexAlign.Center, alignItems: ItemAlign.Center }) {
        Row({ space: 6 }) {
          Image($r('app.media.mic_icon'))
            .width(18)
            .height(18)
            .fillColor(OpenBoardTheme.primaryText)

          Text(getVoiceDictationStatus(this.currentLanguage))
            .fontSize(16)
            .fontWeight(500)
            .fontColor(OpenBoardTheme.primaryText)

          // Show recognized text preview if available
          if (this.recognizedText) {
            Text(`: "${this.recognizedText}"`)
              .fontSize(14)
              .fontWeight(400)
              .fontColor(OpenBoardTheme.secondaryText)
              .maxLines(1)
              .textOverflow({ overflow: TextOverflow.Ellipsis })
          }
        }
        .width('100%')
        .justifyContent(FlexAlign.Center)
      }
      .layoutWeight(1)
    }
    .width('100%')
    .height('100%')
  }

  build() {
    Stack() {
      Column() {}
      .width('100%')
      .height('100%')
      .hitTestBehavior(HitTestMode.None)
      .backgroundColor(OpenBoardTheme.background)
      .animation({ duration: 200 })

      Column({ space: 4 }) {
        // MAIN TOOLBAR - CONDITIONAL RENDERING
        Row({ space: 3 }) {
          if (this.isRecording) {
            // Toolbar when typing with voice
            this.buildVoiceToolbar()
          } else if (this.showEmojiPanel) {
            // Toolbar when in emoji tab
            this.buildEmojiToolbar()
          } else if (this.shouldShowPreTypingToolbar()) {
            // Toolbar before starting to type and after 5 sec of not typing
            this.buildPreTypingToolbar()
          } else {
            // Standard toolbar when typing
            this.buildStandardToolbar()
          }
        }
        .width('100%')
        .height(TOOLBAR_HEIGHT)
        .padding({ left: 6, right: 6, top: 4, bottom: 4 })
        .backgroundColor(OpenBoardTheme.toolbarSurface)
        .borderRadius(16)
        .border({ width: 0.5, color: OpenBoardTheme.divider })
        .shadow({ radius: 0 })

        // MAIN CONTENT
        if (!this.showEmojiPanel) {
          // Main keyboard
          Column({ space: KEY_ROW_GAP }) {
            ForEach(this.getCurrentLayout(), (row: KeyData[]) => {
              Row() {
                ForEach(row, (key: KeyData) => {
                  if (key.action === KeyAction.Spacer) {
                    Flex().layoutWeight(key.flex)
                  } else {
                    KeyView({
                      keyData: key,
                      shiftState: this.shiftState,
                      currentLanguage: this.currentLanguage,
                      availableLanguages: this.availableLanguages,
                      enterKeyLabel: this.enterKeyLabel,
                      onKeyPress: (k: KeyData): void => this.handleKeyPress(k),
                      onKeyLongPress: (k: KeyData, p: Position): void => this.handleLongPress(k, p),
                      onKeyLongPressDrag: (p: Position): void => this.handleLongPressDrag(p),
                      onKeyLongPressEnd: (): void => this.handleLongPressEnd(),
                      onKeyContinuousPressStart: (): void => this.handleRapidDeleteStart(),
                      onKeyContinuousPressEnd: (): void => this.handleRapidDeleteEnd(),
                      onKeyTouchTelemetry: (k: KeyData, p: Position, phase: KeyTouchPhase): void =>
                        this.handleKeyTouchTelemetry(k, p, phase),
                      onSwipe: key.action === KeyAction.Space ? (direction: LayoutSwipeDirection): void => this.handleSwipe(direction) : undefined
                    })
                      .layoutWeight(key.flex)
                  }
                })
              }
              .width('100%')
              .layoutWeight(1)
            })
          }
          .layoutWeight(1)
          .padding({ left: 2, right: 2, bottom: 2 })
        } else {
          // Emoji Panel
          Column({ space: 10 }) {
            // Emoji Categories
            Scroll() {
              Row() {
                ForEach(EMOJI_CATEGORIES, (cat: string) => {
                  Text(cat)
                    .fontSize(14)
                    .fontWeight(500)
                    .fontColor(this.emojiCategory === cat ? OpenBoardTheme.accent : OpenBoardTheme.secondaryText)
                    .padding({ left: 16, right: 16, top: 8, bottom: 8 })
                    .backgroundColor(this.emojiCategory === cat ? OpenBoardTheme.surface : OpenBoardTheme.background)
                    .borderRadius(20)
                    .onClick(() => { this.emojiCategory = cat; })
                })
              }
              .padding({ left: 8, right: 8 })
            }
            .scrollable(ScrollDirection.Horizontal)
            .scrollBar(BarState.Off)
            .edgeEffect(EdgeEffect.Spring)

            // Emoji
            Scroll(this.emojiScroller) {
              Flex({ wrap: FlexWrap.Wrap, justifyContent: FlexAlign.Start, alignItems: ItemAlign.Start }) {
                ForEach(this.getEmojiList(this.emojiCategory), (emoji: string) => {
                  Text(emoji)
                    .fontSize(28)
                    .textAlign(TextAlign.Center)
                    .width('11%')
                    .height(46)
                    .onClick(() => {
                      keyboardController.insertText(emoji);
                      this.onTextTyped(emoji);
                    })
                })
              }
              .width('100%')
              .padding({ left: 8, right: 8 })
            }
            .scrollable(ScrollDirection.Vertical)
            .scrollBar(BarState.On)
            .onScrollFrameBegin((offset: number, state: ScrollState) => {
              return { offsetRemain: offset };
            })
          }
          .layoutWeight(1)
          .padding({ left: 2, right: 2, bottom: 2 })
        }
      }
      .alignSelf(ItemAlign.End)
      .width('100%')
      .height('100%')
      .backgroundColor(OpenBoardTheme.background)
      .padding({
        left: KEYBOARD_HORIZONTAL_PADDING,
        right: KEYBOARD_HORIZONTAL_PADDING,
        top: KEYBOARD_TOP_PADDING,
        bottom: KEYBOARD_BOTTOM_PADDING + this.getSystemBottomInsetVp()
      })

      // Popups
      if (this.longPressInfo) {
        KeyPopup({ info: this.longPressInfo, hoveredChar: this.hoveredChar, keyWidth: this.popupKeyWidth })
          .position({ x: this.popupPosition.x, y: this.popupPosition.y })
          .zIndex(200)
      }

      if (this.singleCharPopupInfo) {
        SingleKeyPopup({ char: this.singleCharPopupInfo.char })
          .position({ x: this.singleCharPopupInfo.position.x - 25, y: this.singleCharPopupInfo.position.y - 70 })
          .zIndex(150)
      }

      if (this.languageIndicatorOpacity > 0) {
        LanguageIndicator({ languageName: getLanguageName(this.currentLanguage) })
          .opacity(this.languageIndicatorOpacity)
          .position({ x: (this.getScreenWidthVp() / 2) - 100, y: 150 })
          .zIndex(250)
      }

      // Voice interruption popup
      if (this.showInterruptionPopup) {
        VoiceInterruptionPopup({
          message: this.interruptionMessage,
          popupPosition: this.interruptionPopupPosition
        })
          .zIndex(300)
      }

      if (this.showNativeDebugPopup) {
        NativeDebugPopup({
          modeLabel: this.nativePredictorMode,
          word: this.nativeDebugWord,
          suggestionsLine: this.nativeDebugSuggestionsLine,
          diagnosticsLine: this.nativeDebugDiagnosticsFull,
          predDirect: this.nativeDebugPredDirect,
          predHandoff: this.nativeDebugPredHandoff,
          willAutocorrect: this.nativeDebugWillAutocorrect
        })
          .position({ x: this.getScreenWidthVp() * 0.18, y: 54 })
          .zIndex(320)
          .onClick(() => { this.showNativeDebugPopup = false; })
      }
    }
    .width('100%')
    .height('100%')
  }

  getCurrentLayout(): KeyData[][] {
    const insertEmojiKey = (layout: KeyData[][] | undefined): KeyData[][] => {
      if (!layout || layout.length < 4) return layout || [];
      const lastRow = layout[layout.length - 1].slice();
      const commaIndex = lastRow.findIndex(k => k.mainValue === ',' || k.mainValue === '،');
      if (commaIndex !== -1 && lastRow[commaIndex + 1]?.action === KeyAction.Space) {
        lastRow.splice(commaIndex + 1, 0, EMOJI_KEY_DATA);
        const originalSpaceKey = lastRow[commaIndex + 2];
        if (originalSpaceKey) {
          const newSpaceKey: KeyData = {
            mainValue: originalSpaceKey.mainValue,
            shiftValue: originalSpaceKey.shiftValue,
            secondaryValue: originalSpaceKey.secondaryValue,
            action: originalSpaceKey.action,
            flex: Math.max(originalSpaceKey.flex - EMOJI_KEY_DATA.flex + 0.5, 3.5)
          };
          lastRow[commaIndex + 2] = newSpaceKey;
        }
        const newLayout = layout.slice(0, layout.length - 1);
        newLayout.push(lastRow);
        return newLayout;
      }
      return layout;
    };

    switch (this.keyboardLayout) {
      case KeyboardLayout.Alphabet:
        switch (this.currentLanguage) {
          case 'ar': return insertEmojiKey(arabicLayout);
          case 'en': return insertEmojiKey(englishLayout);
          case 'it': return insertEmojiKey(italianLayout);
          case 'pl': return insertEmojiKey(polishLayout);
          case 'fa': return insertEmojiKey(persianLayout);
          case 'ru': return insertEmojiKey(russianLayout);
          default: return insertEmojiKey(englishLayout);
        }
      case KeyboardLayout.Symbols1: return symbolsLayout1;
      case KeyboardLayout.Symbols2: return symbolsLayout2;
      default: return insertEmojiKey(englishLayout);
    }
  }

  private getEmojiList(category: string): string[] {
    switch (category) {
      case 'Smileys & Emotion':
        return ['😀','😁','😂','🤣','😃','😄','😅','😆','😉','😊','😋','😎','🥰','😍','😘','😗','😙','😚','🥲','🥳','🤩','🤗','🤔','🤨','😐','😶','😏','😒','🙄','😬','🤥','😟','😔','😥','🤤','😫','😩','🥺','😭','😤','😮','😱','😳','🤯','🥵','🥶','😴','😵','😷','🤕','🤢','🤮','🤧','😇','😈','👿','👹','👺','💀','👻','👽','🤖','💩','😺','😸','😹','😻','😼','😽','😾','😿','🙀','🫶','👍','👎','✊','👊','🤝','🤞','✌️','🫰','🤟','🤘','👌','🤌','🤏','👈','👉','👆','👇','☝️','✋','🤚','🖐️','S','💪','🙏','✍️','💅','👂','👃','👁️','🧠','🦷','🦴','🗣️','👤','👥','🫂'];
      case 'People':
        // Ludzie, części ciała, zawody, rodziny
        return ['👶','👧','🧒','👦','👩','🧑','👨','👵','🧓','👴','👱‍♀️','👱','👱‍♂️','👮‍♀️','👮','👮‍♂️','👷‍♀️','👷','👷‍♂️','🧑‍⚕️','👨‍⚕️','👩‍⚕️','🧑‍🎓','👨‍🎓','👩‍🎓','🧑‍🏫','👨‍🏫','👩‍🏫','🧑‍⚖️','👨‍⚖️','👩‍⚖️','🧑‍🌾','👨‍🌾','👩‍🌾','🧑‍🍳','👨‍🍳','👩‍🍳','🧑‍🔧','👨‍🔧','👩‍🔧','🧑‍🏭','👨‍🏭','👩‍🏭','🧑‍💼','👨‍💼','👩‍💼','🧑‍🔬','👨‍🔬','👩‍🔬','🧑‍💻','👨‍💻','👩‍💻','🧑‍🎤','👨‍🎤','👩‍🎤','🧑‍🎨','👨‍🎨','👩‍🎨','🧑‍✈️','👨‍✈️','👩‍✈️','🧑‍🚀','👨‍🚀','👩‍🚀','🧑‍🚒','👨‍🚒','👩‍🚒','💂‍♀️','💂','💂‍♂️','🕵️‍♀️','🕵️','🕵️‍♂️','🥷','🤵‍♀️','🤵','🤵‍♂️','👰‍♀️','👰','👰‍♂️','🤰','🤱','🧑‍🍼','👨‍🍼','👩‍🍼','👼','🎅','🤶','🧑‍🎄','🦸‍♀️','🦸','🦸‍♂️','🦹‍♀️','🦹','🦹‍♂️','🧙‍♀️','🧙','🧙‍♂️','🧚‍♀️','🧚','🧚‍♂️','🧛‍♀️','🧛','🧛‍♂️','🧜‍♀️','🧜','🧜‍♂️','🧝‍♀️','🧝','🧝‍♂️','🧞‍♀️','🧞','🧞‍♂️','🧟‍♀️','🧟','🧟‍♂️','🚶‍♀️','🚶','🚶‍♂️','🏃‍♀️','🏃','🏃‍♂️','🕺','💃','🧑‍🦽','👨‍🦽','👩‍🦽','🧑‍🦼','👨‍🦼','👩‍🦼','🧍‍♀️','🧍','🧍‍♂️',' sitting','🧎‍♀️','🧎','🧎‍♂️',' kneeling','🧑‍🦯','👨‍🦯','👩‍🦯','🧑‍ standing','👨‍ standing','👩‍ standing','👩‍❤️‍👨','👨‍❤️‍👨','👩‍❤️‍👩','familia','💋','💘','💝','💖','💗','💓','💞','💕','🧡','💛','💚','💙','💜','🤎','🖤','🤍','💯','💢','💥','💫','💦','💨','💬','👁️‍🗨️','💭','💡','🔥'];
      case 'Animals & Nature':
        // Zwierzęta, rośliny, pogoda, krajobrazy
        return ['🐶','🐱','🐭','🐹','🐰','🦊','🐻','🐼','🐨','🐯','🦁','🐮','🐷','🐽','🐸','🐵','🐒','🦍','🐔','🐧','🐦','🐤','🐣','🐥','🦆','🦢','🦅','🦉','🦩',' peacock',' parrot','🕊️','🐺','🐗','🐴','🦓','🦌','🦏',' hippopotamus','🦒','🐪','🐫','🐘','🦥','🦦','🦨','🦔','🐿️','🐁','🐀','🐇','🐈','🐉','🐲','🐊','🐢','🐍','🦎','🦖','🦕','🐋','🐳','🐬','🐠','🐡','🦈','🐙','🦑','🦐','🦀','🦞',' snail','🦋','🐛','🐜','🐝','🐞','🦗','🕷️','🦂','🦠','💐','🌸','🌼','🌻','🌷','🌹','🥀','🌺','🪴','🌲','🌳','🌴','🌵','🌾','🌿','☘️','🍀','🍁','🍂','🍃','🍄','🌰','🌎','🌍','🌏','🌑','🌕','🌓','🌔','🌖','🌗','🌘','🌙','⭐','🌟','✨','⚡','🔥','☀️','🌤️','⛅','🌥️','🌦️','☁️','🌨️','🌧️','⛈️','🌩️','❄️','⛄','🌬️','💨','🌪️','🌀','🌈','🌊','💧','☔','☂️','💦'];
      case 'Food & Drink':
        // Owoce, warzywa, dania, słodycze, napoje
        return ['🍎','🍏','🍐','🍊','🍋','🍌','🍉','🍇','🍓','🫐','🍒','🍑','🥭','🍍','🥥','🥝','🍅','🍆','🥕','🌽','🌶️','🥒','🥦','🫑','🧅','🧄','🥔','🍠','🥐','🍞','🥖','🥨','🥯','🥞','🧇','🧀','🥩','🍗','🍖','🥓','🍔','🍟','🍕','🌭','🥪','🌮','🌯','🥙','🥚','🍳','🥘','🍲','🥣','🥗',' popcorn','🧈','🧂','🥫','🍱','🍙','🍚','🍘','🍥','🍣','🍤','🍜','🍝','🍛','🍠','🍢','🍡','🥟','🥠','🥡','🍦','🍧','🍨','🍩','🍪','🎂','🍰','🧁','🥧','🍫','🍬','🍭','🍮','🍯','🍼','🥛','☕','🍵','🥤','🧋','🫗','🍺','🍻','🍷','🥂','🍾','🥃','🍸','🍹','🍶','🧊','🥣','🍴','🥄','🥢','🥡','🧧'];
      case 'Activity & Event':
        // Sport, hobby, muzyka, święta, wydarzenia
        return ['⚽','🏀','🏈','⚾','🥎','🎾','🏐','🏉','🎱','🏓',' badminton','🥊','🥋','⛸️','🎿','🏂','⛷️','🏹','🎯','🎳','⛳','🎣','🏊‍♂️','🚴‍♂️','🏃‍♂️','🚶‍♂️','🩰','🎤','🎧','🎼','🎹','🥁','🎸','🎺','🎻','🎬','🎭','🖼️','🎨','🧵','🧶','🎲','♟️','🧩','🧸','🎮','🕹️','🎰','🎡','🎢','🎠','🎊','🎉','🎁','🎈','🎀','🎗️','🎟️','🎫','🎖️','🏆','🏅','🥇','🥈','🥉','🔔','🎄','🎅','🎃','👻','🎂','💍','💎','💰','💲','💸','💵','💶','💷','💴','💳','🧾','🔗','🔒','🔓','🔑'];
      case 'Travel & Places':
        // Pojazdy, miejsca, budynki
        return ['🚗','🚕','🚙','🚌','🚎','🏎️','🚓','🚑','🚒','🚐','🚚','🚛','🚜','🏍️','🛵','🚲','🛴','🚨','⛽','🛣️','🛤️','🚏','🚦','🚥','🚧','anchor','🚢','⛴️','🚤','🛥️','🛳️','🚀','🛸','🚁','🚂','🚃','🚋','🚌','🚆','🚇','🚊','🚉','✈️','🛫','🛬','💺','🏨','motel','house','🏠','🏡','🏘️','🏚️','🏗️','🏢','🏭','🏬','🏣','郵','🏦','🏥','🏫','🏤','⛪','🕌','🕍','🕋',' fountain','⛲','🗼',' statue of liberty','🗾','🌋',' mount fuji','🗻',' at the sea','🏖️','desert','island','⛺','🌃','🏙️','🌄','🌅','🌇','🌆','🌉',' Milky Way','🌠','🎇','🎆'];
      case 'Objects':
        // Przedmioty, narzędzia, ubrania, elektronika
        return ['⌚','📱','💻','⌨️','🖨️','🖱️','📷','📸','📹','📼','💿','📀','📻','📺','📞','📠','🔋','🔌','💡','🔦','🕯️','🗑️','🛒','✉️','📧','📩','📨','📤','📥','📦','📫','📪','📬','📭','📮','📜','📃','📄','📑','📅','🗓️','📇','📈','📉','📊','📋','📌','📍','📎','🖇️','📏','📐','✂️','🪚','🔨','⛏️','🪓','🔪','🔫','🩺','🧪','🌡️','💉','💊','🩹','🚪','🪑','🛋️','🛏️','🛀','🧼','🧽','🧺','🚽','🚿','🛁','🪠','🧴','👚','👕','👖','👔','👗','👙','👘','🥻','🩱','🩲','🩳','🧥','🥼','🧣','🧤','🧢','👒','🎓','👑','💍','💎','💄','💅','🪮','🪒','🪥','🪣','🪤','🪟','🪜','🪞','🪚','🪛','🛠️','⚙️','🧱','⛓️','🔗','📏','🧮'];
      case 'Symbols':
        // Znaki, strzałki, cyfry, emotikony tekstowe
        return ['❤️','🧡','💛','💚','💙','💜','🖤','🤍','🤎','💔','❗️','❓','‼️','⁉️','✅','❌','✔️','✖️','➕','➖','➗','♾️','©️','®️','™️','℠','®️','™️','©️','®️','™️','℠','#️⃣','*️⃣','0️⃣','1️⃣','2️⃣','3️⃣','4️⃣','5️⃣','6️⃣','7️⃣','8️⃣','9️⃣','🔟','⬆️','⬇️','⬅️','➡️','↖️','↗️','↘️','↙️','↕️','↔️','↩️','↪️','⤴️','⤵️','🔄','◀️','▶️','🔼','🔽','⏏️','➿','♻️','⚜️','🔱','⚠️','⛔','🚫','🚭','🚳','🚱','🚷','📵','🔞',' P','🅿️','🚸','🈯','🈚','🈶','🈲','🈹','🈺','🈷️','🈂️','🉑','🈴','🈳','㊗️','㊙️','❌','⭕','🛑','⛑️','👑','🔑','🔔','🔕','🎶','🎵','➕','➖','➗','✖️','➿','✔️','☑️','✅','⭕','❌','🛑','⚠️','❗','❓'];
      case 'Flags':
        // Wybrane flagi
        return ['🏳️','🏴','🏁','🚩','🇦🇫','🇦🇽','🇦🇱','🇩🇿','🇦🇸','🇦🇩','🇦🇴','🇦🇮','🇦🇶','🇦🇬','🇦🇷','🇦🇲','🇦🇼','🇦🇺','🇦🇹','🇦🇿','🇧🇸','🇧🇭','🇧🇩','🇧🇧','🇧🇾','🇧🇪','🇧🇿','🇧🇯','🇧🇲','🇧🇹','🇧🇴','🇧🇦','🇧🇼','🇧🇷','🇮🇴','🇻🇬','🇧🇳','🇧🇬','🇧🇫','🇧🇮','🇰🇭','🇨🇲','🇨🇦','🇮🇨','🇨🇻','🇧🇶','🇰🇾','🇨🇫','🇹🇩','🇨🇱','🇨🇳','🇨🇽','🇨🇨','🇨🇴','🇰🇲','🇨🇬','🇨🇩','🇨🇰','🇨🇷','🇭🇷','🇨🇺','🇨🇼','🇨🇾','🇨🇿','🇩🇰','🇩🇯','🇩🇲','🇩🇴','🇪🇨','🇪🇬','🇸🇻','🇬🇶','🇪🇷','🇪🇪','🇸🇿','🇪🇹','🇪🇺','🇫🇰','🇫🇴','🇫🇯','🇫🇮','🇫🇷','🇬🇫','🇵🇫','🇹🇫','🇬🇦','🇬🇲','🇬🇪','🇩🇪','🇬🇭','🇬🇮','🇬🇷','🇬🇱','🇬🇩','🇬🇵','🇬🇺','🇬🇹','🇬🇬','🇬🇳','🇬🇼','🇬🇾','🇭🇹','🇭🇳','🇭🇰','🇭🇺','🇮🇸','🇮🇳','🇮🇩','🇮🇷','🇮🇶','🇮🇪','🇮🇲','🇮🇱','🇮🇹','🇯🇲','🇯🇵','🇯🇪','🇯🇴','🇰🇿','🇰🇪','🇰🇮','🇽🇰','🇰🇼','🇰🇬','🇱🇦','🇱🇻','🇱🇧','🇱🇸','🇱🇷','🇱🇾','🇱🇮','🇱🇹','🇱🇺','🇲🇴','🇲🇬','🇲🇼','🇲🇾','🇲🇻','🇲🇱','🇲🇹','🇲🇭','🇲🇶','🇲🇷','🇲🇺','🇾🇹','🇲🇽','🇫🇲','🇲🇩','🇲🇨','🇲🇳','🇲🇪','🇲🇸','🇲🇦','🇲🇿','🇲🇲','🇳🇦','🇳🇷','🇳🇵','🇳🇱','🇳🇨','🇳🇿','🇳🇮','🇳🇪','🇳🇬','🇳🇺','🇳🇫','🇰🇵','🇲🇰','🇲🇵','🇳🇴','🇴🇲','🇵🇰','🇵🇼','🇵🇸','🇵🇦','🇵🇬','🇵🇾','🇵🇪','🇵🇭','🇵🇳','🇵🇱','🇵🇹','🇵🇷','🇶🇦','🇷🇪','🇷🇴','🇷🇺','🇷🇼','🇸🇽','🇼🇸','🇸🇲','🇸🇹','🇸🇦','🇸🇳','🇷🇸','🇸🇨','🇸🇱','🇸🇬','🇸🇰','🇸🇮','🇸🇧','🇸🇴','🇿🇦','🇬🇸','🇰🇷','🇸🇸','🇪🇸','🇱🇰','🇸🇩','🇸🇷','🇸🇯','🇸🇪','🇨🇭','🇸🇾','🇹🇼','🇹🇯','🇹🇿','🇹🇭','🇹🇱','🇹🇬','🇹🇰','🇹🇴','🇹🇹','🇹🇳','🇹🇷','🇹🇲','🇹🇨','🇹🇻','🇺🇬','🇺🇦','🇦🇪','🇬🇧','🇺🇸','🇺🇾','🇺🇿','🇻🇺','🇻🇦','🇻🇪','🇻🇳','🇼🇫','🇪🇭','🇾🇪','🇿🇲','🇿🇼'];
      default:
        return ['🙂','😊','😂','🤣'];
    }
  }
}
