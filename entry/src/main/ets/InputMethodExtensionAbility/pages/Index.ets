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
import clipboardHistory, { ClipboardItem, ClipboardItemType } from '../model/ClipboardHistory';
import feedbackController, { FeedbackType } from '../model/FeedbackController';
import { display } from '@kit.ArkUI';
import { inputMethodEngine } from '@kit.IMEKit';
import pasteboard from '@ohos.pasteboard';

const EMOJI_KEY_DATA: KeyData = { mainValue: 'icon', secondaryValue: undefined, action: KeyAction.EMOJI_PANEL, flex: 1.2, shiftValue: undefined };

// TYPE DEFINITIONS
interface Position { x: number; y: number; }
interface LongPressInfo { key: KeyData; initialPosition: Position; }
interface SingleCharPopupInfo { char: string; position: Position; }
type LanguageCode = 'en' | 'ar' | 'it' | 'pl' | 'fa' | 'ru';
enum LayoutSwipeDirection { Left, Right }
const ALLOWED_LANGUAGES: string[] = ['en', 'ar', 'it', 'pl', 'fa', 'ru'];
const EMOJI_CATEGORIES: string[] = ['Smileys & Emotion', 'People', 'Animals & Nature', 'Food & Drink', 'Activity & Event', 'Travel & Places', 'Objects', 'Symbols', 'Flags'];

// Speech recognition simulation types
interface SpeechRecognitionResult {
  text: string;
  delay: number;
}

// HELPER FUNCTIONS
function getLanguageName(code: LanguageCode): string {
  switch (code) {
    case 'ar': return 'Arabic';
    case 'en': return 'English';
    case 'it': return 'Italian';
    case 'pl': return 'Polish';
    case 'fa': return 'Persian';
    case 'ru': return 'Русский';
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
    case 'ru': return 'Доступ к микрофону запрещен';
    default: return 'Microphone permission denied';
  }
}

function getVoiceRecognitionError(code: LanguageCode): string {
  switch (code) {
    case 'en': return 'Speech recognition failed';
    case 'pl': return 'Rozpoznawanie mowy nie powiodło się';
    case 'it': return 'Riconoscimento vocale fallito';
    case 'ar': return 'فشل التعرف على الكلام';
    case 'fa': return 'تشخیص گفتار ناموفق بود';
    case 'ru': return 'Распознавание речи не удалось';
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
          .backgroundColor(char === this.hoveredChar ? OpenBoardTheme.accent : OpenBoardTheme.regularKey)
          .borderRadius(OpenBoardTheme.radiusSmall)
          .shadow(OpenBoardTheme.shadowKey)
          .animation({ duration: 50 })
      })
    }
    .padding({ left: 8, right: 8, top: 6, bottom: 6 })
    .backgroundColor(OpenBoardTheme.specialKey)
    .borderRadius(OpenBoardTheme.radiusMedium)
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
    .backgroundColor(OpenBoardTheme.specialKey)
    .borderRadius(OpenBoardTheme.radiusMedium)
    .shadow(OpenBoardTheme.shadowPressed)
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
  onKeyTouchDown: (key: KeyData, position: Position) => void = () => {};
  onKeyTouchUp: () => void = () => {};
  onSwipe?: (direction: LayoutSwipeDirection) => void;
  onBackspaceSwipeStart: (x: number) => void = () => {};
  onBackspaceSwipeMove: (x: number) => void = () => {};
  onBackspaceSwipeEnd: () => void = () => {};
  onCursorModeStart: (x: number) => void = () => {};
  onCursorModeMove: (x: number) => void = () => {};
  onCursorModeEnd: () => void = () => {};

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
            } else if (this.keyData.action === KeyAction.Enter && this.enterKeyLabel === 'Send') {
              Image($r('app.media.send_icon'))
                .width(20)
                .height(20)
            } else if (this.keyData.mainValue === 'icon') {
              Image($r('app.media.emoji_icon'))
                .width(20)
                .height(20)
            }
          } else {
            Text(this.getDisplayValue())
              .fontSize(18)
              .fontWeight(this.keyData.action === KeyAction.Char ? 400 : 500)
              .fontColor(OpenBoardTheme.primaryText)
          }
        }
        .width('100%')
        .height('100%')
        .justifyContent(FlexAlign.Center)
        .alignItems(HorizontalAlign.Center)

        if (this.keyData.secondaryValue) {
          Text(this.keyData.secondaryValue.split(',')[0])
            .fontSize(10)
            .fontColor(OpenBoardTheme.secondaryText)
            .margin({ top: 4, right: 4 })
        }
      }
      .width('100%')
      .height('100%')
      .backgroundColor(this.isPressed ?
        OpenBoardTheme.surface : (this.keyData.action === KeyAction.Char ? OpenBoardTheme.regularKey : OpenBoardTheme.specialKey))
      .borderRadius(OpenBoardTheme.radiusMedium)
      .shadow(this.isPressed ? OpenBoardTheme.shadowPressed : OpenBoardTheme.shadowKey)
      .zIndex(this.isPressed ? 100 : 0)
      .animation({ duration: 100 })
      .onTouch((event) => {
        const touch = event.touches[0];
        if (!touch) return;
        const currentPosition: Position = { x: touch.windowX, y: touch.windowY };

        if (event.type === TouchType.Down) {
          this.touchStartX = currentPosition.x;
          this.isPressed = true;
          this.longPressTriggered = false;
          this.swipeHandled = false;

          // Show popup for character keys
          if (this.keyData.action === KeyAction.Char) {
            this.onKeyTouchDown(this.keyData, currentPosition);
          }

          // Start backspace swipe tracking
          if (this.keyData.action === KeyAction.Backspace) {
            this.onBackspaceSwipeStart(currentPosition.x);
          }

          this.longPressTimer = setTimeout(() => {
            this.longPressTriggered = true;
            this.isPressed = false;

            if (this.keyData.action === KeyAction.Backspace) {
              this.onKeyContinuousPressStart(this.keyData);
            } else if (this.keyData.action === KeyAction.Space) {
              this.onCursorModeStart(currentPosition.x);
            } else if (this.keyData.secondaryValue) {
              this.onKeyLongPress(this.keyData, currentPosition);
            } else if (this.keyData.action === KeyAction.Shift) {
              this.onKeyLongPress(this.keyData, currentPosition);
            }
          }, 500);
        }

        if (event.type === TouchType.Move) {
          if (this.longPressTriggered && this.keyData.secondaryValue) {
            this.onKeyLongPressDrag(currentPosition);
          }

          // Handle backspace swipe
          if (this.keyData.action === KeyAction.Backspace) {
            const swipeDistance = Math.abs(currentPosition.x - this.touchStartX);
            const SWIPE_THRESHOLD = 20;

            if (!this.swipeHandled && swipeDistance > SWIPE_THRESHOLD) {
              // Disable long press timer when swipe is detected
              clearTimeout(this.longPressTimer);
              this.swipeHandled = true;

              // If long press was already triggered (rapid delete active), stop it
              if (this.longPressTriggered) {
                this.onKeyContinuousPressEnd(this.keyData);
                this.longPressTriggered = false;
              }
            }

            // Always track movement for swipe-based deletion
            this.onBackspaceSwipeMove(currentPosition.x);
          }

          if (this.keyData.action === KeyAction.Space) {
            // If long press triggered, activate cursor mode
            if (this.longPressTriggered) {
              this.onCursorModeMove(currentPosition.x);
            } else {
              // Before long press, handle language switch swipe
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
        }

        if (event.type === TouchType.Up || event.type === TouchType.Cancel) {
          clearTimeout(this.longPressTimer);

          // End backspace swipe tracking
          if (this.keyData.action === KeyAction.Backspace) {
            this.onBackspaceSwipeEnd();
          }

          // End cursor mode tracking
          if (this.keyData.action === KeyAction.Space) {
            this.onCursorModeEnd();
          }

          if (this.longPressTriggered) {
            if (this.keyData.action === KeyAction.Backspace) {
              this.onKeyContinuousPressEnd(this.keyData);
            } else if (this.keyData.action === KeyAction.Space) {
              // Cursor mode was active, don't insert space
            } else if (this.keyData.secondaryValue) {
              this.onKeyLongPressEnd();
            }
          } else if (event.type === TouchType.Up && !this.longPressTriggered && !this.swipeHandled) {
            // Only trigger keyPress if no swipe was handled
            this.onKeyPress(this.keyData);
          }

          // Hide popup
          this.onKeyTouchUp();

          this.isPressed = false;
          this.longPressTriggered = false;
          this.touchStartX = 0;
          this.swipeHandled = false;
        }
      })
    }
    .padding({ left: 2, right: 2, top: 2, bottom: 2 })
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

  // Swipe-based backspace state
  @State backspaceSwipeActive: boolean = false;
  @State backspaceInitialX: number = 0;
  @State backspaceCurrentX: number = 0;

  // Cursor control mode state
  @State cursorModeActive: boolean = false;
  @State cursorInitialX: number = 0;
  @State cursorCurrentX: number = 0;

  // Layout cache for performance optimization
  private cachedLayout: KeyData[][] = [];
  private cachedLayoutKey: string = '';

  @State showEmojiPanel: boolean = false;
  @State emojiCategory: string = 'Smileys & Emotion';
  private emojiScroller: Scroller = new Scroller();

  @State showClipboardPanel: boolean = false;
  @State clipboardFilter: ClipboardItemType | 'all' = 'all';
  @State clipboardSearchQuery: string = '';
  private clipboardScroller: Scroller = new Scroller();
  private lastClipboardText: string = '';
  private clipboardMonitorTimer: number = -1;

  @State private previousWord: string = '';
  @State private currentWord: string = '';
  @State private suggestions: string[] = ['', '', ''];
  @State private lastAutocorrection: { original: string; corrected: string } | null = null;

  @State private isRecording: boolean = false;
  @State private isProcessingVoiceToggle: boolean = false;
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
    this.interruptionPopupTimer = setTimeout(() => {
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

    const processNextResult = () => {
      if (!this.isListening || this.currentRecognitionIndex >= simulatedResults.length) {
        if (this.isListening) {
          // Auto-stop after simulation completes
          this.isRecording = false;
          this.stopVoiceRecognition();
        }
        return;
      }

      const result: SpeechRecognitionResult = simulatedResults[this.currentRecognitionIndex];
      this.speechRecognitionTimer = setTimeout(() => {
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

  // Enhanced microphone click handler with race condition protection
  private async handleMicrophoneClick(): Promise<void> {
    if (this.isProcessingVoiceToggle) {
      console.info('Voice toggle already in progress, ignoring click');
      return;
    }

    this.isProcessingVoiceToggle = true;
    try {
      if (this.isRecording) {
        // Stop recording
        this.isRecording = false;
        await this.stopVoiceRecognition();
        console.info('Voice typing stopped by user');
      } else {
        // Start recording
        this.isRecording = true;
        this.showEmojiPanel = false;
        this.resetInactivityTimer();
        await this.startVoiceRecognition();
        console.info('Voice typing started');
      }
    } finally {
      this.isProcessingVoiceToggle = false;
    }
  }

  // Clipboard monitoring
  private async monitorClipboard(): Promise<void> {
    try {
      const systemPasteboard = pasteboard.getSystemPasteboard();
      const data = await systemPasteboard.getData();
      const text = data.getPrimaryText();

      if (text && text.length > 0 && text !== this.lastClipboardText) {
        // New clipboard content detected
        console.info('OpenBoard: New clipboard content detected');
        clipboardHistory.addItem(text);
        this.lastClipboardText = text;
      }
    } catch (e) {
      console.error('OpenBoard: Clipboard monitoring failed', e);
    }
  }

  private startClipboardMonitoring(): void {
    if (this.clipboardMonitorTimer !== -1) {
      return; // Already monitoring
    }

    // Initial check
    this.monitorClipboard();

    // Check every 2 seconds
    this.clipboardMonitorTimer = setInterval(() => {
      this.monitorClipboard();
    }, 2000);

    console.info('OpenBoard: Clipboard monitoring started');
  }

  private stopClipboardMonitoring(): void {
    if (this.clipboardMonitorTimer !== -1) {
      clearInterval(this.clipboardMonitorTimer);
      this.clipboardMonitorTimer = -1;
      console.info('OpenBoard: Clipboard monitoring stopped');
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
    this.availableLanguages = ALLOWED_LANGUAGES.map((c: string) => c as LanguageCode);
    if (!this.availableLanguages.includes(this.currentLanguage)) {
      this.currentLanguage = this.availableLanguages[0];
    }
    this.startInactivityTimer();
    this.startClipboardMonitoring();
    // Auto-capitalize at the start of new typing session
    this.enableAutoCapitalization();
  }

  aboutToAppear(): void {
    console.info('OpenBoard Keyboard: UI aboutToAppear called');
    keyboardController.setEnterKeyTypeCallback((enterKeyType: number) => {
      this.updateEnterKeyLabel(enterKeyType);
    });
    this.updateEnterKeyLabel(keyboardController.getCurrentEnterKeyType());
  }

  aboutToDisappear(): void {
    if (this.rapidDeleteTimer !== -1) {
      clearInterval(this.rapidDeleteTimer);
      this.rapidDeleteTimer = -1;
    }
    this.stopInactivityTimer();
    this.stopClipboardMonitoring();
    if (this.speechRecognitionTimer !== -1) {
      clearTimeout(this.speechRecognitionTimer);
      this.speechRecognitionTimer = -1;
    }
    if (this.interruptionPopupTimer !== -1) {
      clearTimeout(this.interruptionPopupTimer);
      this.interruptionPopupTimer = -1;
    }
    // Clean up speech recognition
    if (this.isRecording) {
      this.stopVoiceRecognition();
    }
  }

  private startInactivityTimer(): void {
    this.stopInactivityTimer();
    this.inactivityTimer = setInterval(() => {
      const timeSinceLastActivity = Date.now() - this.lastTypingActivity;
      if (!this.isRecording && !this.showEmojiPanel && timeSinceLastActivity >= 5000) {
        this.showPreTypingToolbar = true;
      }
    }, 1000); // Check every second
  }

  private stopInactivityTimer(): void {
    if (this.inactivityTimer !== -1) {
      clearInterval(this.inactivityTimer);
      this.inactivityTimer = -1;
    }
  }

  private resetInactivityTimer(): void {
    this.lastTypingActivity = Date.now();
    this.showPreTypingToolbar = false;
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
    this.rapidDeleteTimer = setInterval(() => {
      feedbackController.provideFeedback(FeedbackType.Heavy);
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

  private handleBackspaceSwipeStart(x: number): void {
    this.backspaceSwipeActive = true;
    this.backspaceInitialX = x;
    this.backspaceCurrentX = x;
  }

  private handleBackspaceSwipeMove(x: number): void {
    if (!this.backspaceSwipeActive) return;

    const swipeDistance = x - this.backspaceCurrentX;
    const SWIPE_THRESHOLD = 15; // pixels needed to trigger action

    if (swipeDistance < -SWIPE_THRESHOLD) {
      // Swiping left - delete character
      const textBefore = keyboardController.getTextBeforeCursor(1);
      if (textBefore.length > 0) {
        feedbackController.provideFeedback(FeedbackType.Heavy);
        keyboardController.deleteForward(1);
        this.onBackspaceTyped();
      }
      this.backspaceCurrentX = x;
    }
  }

  private handleBackspaceSwipeEnd(): void {
    this.backspaceSwipeActive = false;
    this.backspaceInitialX = 0;
    this.backspaceCurrentX = 0;
  }

  private handleCursorModeStart(x: number): void {
    this.cursorModeActive = true;
    this.cursorInitialX = x;
    this.cursorCurrentX = x;
  }

  private handleCursorModeMove(x: number): void {
    if (!this.cursorModeActive) return;

    const swipeDistance = x - this.cursorCurrentX;
    const CURSOR_THRESHOLD = 20; // pixels needed to move cursor

    if (Math.abs(swipeDistance) > CURSOR_THRESHOLD) {
      const direction = swipeDistance > 0 ? 1 : -1; // 1 = right, -1 = left
      keyboardController.moveCursor(direction);
      this.cursorCurrentX = x;
    }
  }

  private handleCursorModeEnd(): void {
    this.cursorModeActive = false;
    this.cursorInitialX = 0;
    this.cursorCurrentX = 0;
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
      feedbackController.provideFeedback(FeedbackType.Light);
      keyboardController.insertText(this.hoveredChar);
      this.onTextTyped(this.hoveredChar);
    }
    this.longPressInfo = null;
    this.hoveredChar = null;
  }

  private handleKeyTouchDown(key: KeyData, position: Position): void {
    // Show popup with the character that will be typed
    const char = (this.currentLanguage !== 'ar' && this.currentLanguage !== 'fa' &&
                  (this.shiftState === ShiftState.On || this.shiftState === ShiftState.CapsLock))
      ? (key.shiftValue ?? key.mainValue)
      : key.mainValue;
    this.singleCharPopupInfo = { char: char, position: position };
  }

  private handleKeyTouchUp(): void {
    // Hide the popup
    this.singleCharPopupInfo = null;
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
        // Light haptic for regular character keys
        feedbackController.provideFeedback(FeedbackType.Light);
        // Remember if shift was on BEFORE typing this character
        const wasShiftOnBeforeTyping = (this.shiftState === ShiftState.On);
        let text = (this.currentLanguage !== 'ar' && this.currentLanguage !== 'fa' && (this.shiftState === ShiftState.On || this.shiftState === ShiftState.CapsLock))
          ? key.shiftValue ?? key.mainValue
          : key.mainValue;
        console.info('[AutoCap] Typing char, shift state:', this.shiftState, 'text:', text, 'mainValue:', key.mainValue, 'shiftValue:', key.shiftValue);
        keyboardController.insertText(text);
        this.onTextTyped(text);
        // Only turn off shift if it was on BEFORE typing (not if enabled by typing boundary char like '.')
        if (wasShiftOnBeforeTyping) {
          console.info('[AutoCap] Disabling shift after typing character');
          this.shiftState = ShiftState.Off;
        }
        break;
      case KeyAction.Space:
        feedbackController.provideFeedback(FeedbackType.Light);
        keyboardController.insertText(' ');
        this.onBoundaryTyped(' ');
        // Auto-return to alphabet layout after typing space in symbols layout
        if (this.keyboardLayout === KeyboardLayout.Symbols1 || this.keyboardLayout === KeyboardLayout.Symbols2) {
          this.keyboardLayout = KeyboardLayout.Alphabet;
        }
        break;
      case KeyAction.Enter:
        feedbackController.provideFeedback(FeedbackType.Medium);
        keyboardController.sendEnterKey();
        this.onBoundaryTyped('\n');
        break;
      case KeyAction.Backspace:
        feedbackController.provideFeedback(FeedbackType.Heavy);
        keyboardController.deleteForward(1);
        this.onBackspaceTyped();
        break;
      case KeyAction.Shift:
        feedbackController.provideFeedback(FeedbackType.Medium);
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
        feedbackController.provideFeedback(FeedbackType.Medium);
        this.keyboardLayout = (this.keyboardLayout === KeyboardLayout.Alphabet) ?
          KeyboardLayout.Symbols1 : KeyboardLayout.Alphabet;
        break;
      case KeyAction.SwitchSymbolsAlt:
        feedbackController.provideFeedback(FeedbackType.Medium);
        this.keyboardLayout = KeyboardLayout.Symbols2;
        break;
    }
  }

  // TOGGLE CLIPBOARD PANEL
  private handleClipboardClick(): void {
    this.resetInactivityTimer();

    if (this.isRecording) {
      this.handleVoiceInterruption('clipboard');
    }

    this.showClipboardPanel = !this.showClipboardPanel;
    if (this.showClipboardPanel) {
      this.showEmojiPanel = false;
      this.clipboardSearchQuery = '';
      this.clipboardFilter = 'all';
    }
  }

  // PASTE CLIPBOARD ITEM BY ID
  private async pasteClipboardItem(item: ClipboardItem): Promise<void> {
    this.resetInactivityTimer();

    if (item.text && item.text.length > 0) {
      keyboardController.insertText(item.text);
      this.onTextTyped(item.text);
      console.info('OpenBoard: Pasted clipboard item:', item.id);

      // Close clipboard panel after pasting
      this.showClipboardPanel = false;
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
    console.info('OpenBoard Keyboard: Switched to language:', this.currentLanguage);
  }

  private updateSuggestions(): void {
    try {
      // Check for autocorrection first
      const autocorrection = this.currentWord.length >= 2
        ? predictionModel.findAutocorrection(this.currentLanguage, this.currentWord)
        : null;

      if (autocorrection && autocorrection !== this.currentWord) {
        // Show autocorrection as first suggestion, then predictions
        const list = predictionModel.predict(this.currentLanguage, this.previousWord, this.currentWord, 2);
        this.suggestions = [autocorrection, list[0] ?? '', list[1] ?? ''];
      } else {
        // Normal predictions
        const list = predictionModel.predict(this.currentLanguage, this.previousWord, this.currentWord, 3);
        this.suggestions = [list[0] ?? '', list[1] ?? '', list[2] ?? ''];
      }
    } catch (e) {
      console.warn('Prediction model error:', e);
      this.suggestions = ['', '', ''];
    }
  }

  private isBoundaryChar(ch: string): boolean {
    if (!ch) return false;
    const boundaries = [' ', '\n', ',', '.', '!', '?', ';', ':', '/', '\\', '-', '_', '"', "'", '(', ')', '[', ']', '<', '>'];
    return boundaries.indexOf(ch) >= 0;
  }

  // Check if we should auto-capitalize after this character
  private shouldAutoCapitalize(ch: string): boolean {
    if (!ch) return false;
    // Auto-capitalize after sentence-ending punctuation
    const sentenceEnders = ['.', '!', '?', '\n'];
    return sentenceEnders.indexOf(ch) >= 0;
  }

  // Enable auto-capitalization for next character
  private enableAutoCapitalization(): void {
    // Don't auto-capitalize for Arabic and Persian (they don't have uppercase)
    if (this.currentLanguage === 'ar' || this.currentLanguage === 'fa') {
      console.info('[AutoCap] Skipping auto-cap for RTL language:', this.currentLanguage);
      return;
    }
    // Only enable if not already in CapsLock mode
    if (this.shiftState !== ShiftState.CapsLock) {
      console.info('[AutoCap] Enabling auto-capitalization, shift state:', this.shiftState);
      this.shiftState = ShiftState.On;
    } else {
      console.info('[AutoCap] Already in CapsLock, not changing');
    }
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

  private onBoundaryTyped(ch: string): void {
    if (this.currentWord) {
      // Check for autocorrection on space
      if (ch === ' ') {
        const correction = predictionModel.findAutocorrection(this.currentLanguage, this.currentWord);
        if (correction && correction !== this.currentWord) {
          // Autocorrection found! Replace the typed word
          console.info(`[Autocorrect] "${this.currentWord}" → "${correction}"`);

          // Delete the typed word
          keyboardController.deleteForward(this.currentWord.length);

          // Insert the corrected word
          keyboardController.insertText(correction);

          // Track the autocorrection for potential undo
          this.lastAutocorrection = {
            original: this.currentWord,
            corrected: correction
          };

          // Update previous word to the corrected version
          this.previousWord = correction;
        } else {
          // No correction, record the word as typed
          predictionModel.recordWord(this.currentLanguage, this.previousWord, this.currentWord);
          this.previousWord = this.currentWord;
        }
      } else {
        // Non-space boundary, just record the word
        predictionModel.recordWord(this.currentLanguage, this.previousWord, this.currentWord);
        this.previousWord = this.currentWord;
      }

      this.currentWord = '';
    }
    this.updateSuggestions();
    this.resetInactivityTimer();

    // Auto-capitalize after sentence-ending punctuation
    console.info('[AutoCap] Boundary typed:', ch, 'should auto-cap:', this.shouldAutoCapitalize(ch));
    if (this.shouldAutoCapitalize(ch)) {
      this.enableAutoCapitalization();
    }
  }

  private onBackspaceTyped(): void {
    if (this.currentWord && this.currentWord.length > 0) {
      this.currentWord = this.currentWord.substring(0, this.currentWord.length - 1);
      this.updateSuggestions();
      this.resetInactivityTimer();
    }

    if (keyboardController.isBufferEmpty()) {
      this.previousWord = '';
      this.currentWord = '';
      this.updateSuggestions();
      this.showPreTypingToolbar = true;
      // Auto-capitalize at the start of text
      this.enableAutoCapitalization();
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
    this.updateSuggestions();
  }

  private getScreenWidthVp(): number {
    try {
      const displayInfo = display.getDefaultDisplaySync();
      return displayInfo.width / 3;
    } catch (e) {
      console.error('Failed to get screen width:', e);
      return 360;
    }
  }

  // Build different toolbar variants using @Builder
  @Builder
  buildStandardToolbar() {
    Row({ space: 3 }) {
      // CLOSE ICON
      Column() {
        Image($r('app.media.close_icon'))
          .width(18)
          .height(18)
      }
      .width(34)
      .height(34)
      .justifyContent(FlexAlign.Center)
      .alignItems(HorizontalAlign.Center)
      .borderRadius(OpenBoardTheme.radiusSmall)
      .backgroundColor(OpenBoardTheme.specialKey)
      .shadow(OpenBoardTheme.shadowKey)
      .onClick(() => { keyboardController.hideKeyboard(); })

      // SUGGESTIONS
      Flex({ justifyContent: FlexAlign.Center, alignItems: ItemAlign.Center }) {
        Row({ space: 0 }) {
          ForEach(this.suggestions, (suggestion: string, index: number) => {
            Column() {
              Text(suggestion)
                .fontSize(16)
                .fontWeight(500)
                .fontColor(OpenBoardTheme.primaryText)
                .textAlign(TextAlign.Center)
                .maxLines(1).textOverflow({ overflow: TextOverflow.Ellipsis })
            }
            .justifyContent(FlexAlign.Center)
            .alignItems(HorizontalAlign.Center)
            .layoutWeight(1)
            .onClick(() => {
              if (suggestion) this.pickSuggestion(suggestion);
            })

            if (index < 2) {
              Text(' | ')
                .fontSize(16)
                .fontWeight(500)
                .fontColor(OpenBoardTheme.secondaryText)
            }
          })
        }
        .width('100%')
        .justifyContent(FlexAlign.Center)
        .alignItems(VerticalAlign.Center)
      }
      .layoutWeight(1)

      // CLIPBOARD
      Column() {
        Image($r('app.media.clipboard_icon'))
          .width(18)
          .height(18)
          .fillColor(OpenBoardTheme.primaryText)
      }
      .width(34)
      .height(34)
      .justifyContent(FlexAlign.Center)
      .alignItems(HorizontalAlign.Center)
      .borderRadius(OpenBoardTheme.radiusSmall)
      .backgroundColor(OpenBoardTheme.specialKey)
      .shadow(OpenBoardTheme.shadowKey)
      .onClick(() => { this.handleClipboardClick(); })

      // MICROPHONE
      Column() {
        Image($r('app.media.mic_icon'))
          .width(20)
          .height(20)
          .fillColor(this.isRecording ? OpenBoardTheme.accent : OpenBoardTheme.primaryText)
      }
      .width(34)
      .height(34)
      .justifyContent(FlexAlign.Center)
      .alignItems(HorizontalAlign.Center)
      .borderRadius(OpenBoardTheme.radiusSmall)
      .backgroundColor(this.isRecording ? OpenBoardTheme.accent : OpenBoardTheme.specialKey)
      .shadow(OpenBoardTheme.shadowKey)
      .onClick(() => { this.handleMicrophoneClick(); })
    }
  }

  @Builder
  buildPreTypingToolbar() {
    Row({ space: 3 }) {
      // CLOSE ICON
      Column() {
        Image($r('app.media.close_icon'))
          .width(18)
          .height(18)
      }
      .width(34)
      .height(34)
      .justifyContent(FlexAlign.Center)
      .alignItems(HorizontalAlign.Center)
      .borderRadius(OpenBoardTheme.radiusSmall)
      .backgroundColor(OpenBoardTheme.specialKey)
      .shadow(OpenBoardTheme.shadowKey)
      .onClick(() => { keyboardController.hideKeyboard(); })

      // LEFT SPACER for centering
      Flex().layoutWeight(1)

      // CLIPBOARD ICON - CENTERED
      Column() {
        Image($r('app.media.clipboard_icon'))
          .width(20)
          .height(20)
          .fillColor(OpenBoardTheme.primaryText)
      }
      .width(34)
      .height(34)
      .justifyContent(FlexAlign.Center)
      .alignItems(HorizontalAlign.Center)
      .borderRadius(OpenBoardTheme.radiusSmall)
      .backgroundColor(OpenBoardTheme.specialKey)
      .shadow(OpenBoardTheme.shadowKey)
      .onClick(() => { this.handleClipboardClick(); })

      // RIGHT SPACER for centering
      Flex().layoutWeight(1)

      // MICROPHONE
      Column() {
        Image($r('app.media.mic_icon'))
          .width(20)
          .height(20)
          .fillColor(this.isRecording ? OpenBoardTheme.accent : OpenBoardTheme.primaryText)
      }
      .width(34)
      .height(34)
      .justifyContent(FlexAlign.Center)
      .alignItems(HorizontalAlign.Center)
      .borderRadius(OpenBoardTheme.radiusSmall)
      .backgroundColor(this.isRecording ? OpenBoardTheme.accent : OpenBoardTheme.specialKey)
      .shadow(OpenBoardTheme.shadowKey)
      .onClick(() => { this.handleMicrophoneClick(); })
    }
  }

  @Builder
  buildEmojiToolbar() {
    Row({ space: 3 }) {
      // RETURN TO KEYBOARD BUTTON
      Column() {
        Image($r('app.media.return_icon'))
          .width(20)
          .height(20)
          .fillColor(OpenBoardTheme.primaryText)
      }
      .width(34)
      .height(34)
      .justifyContent(FlexAlign.Center)
      .alignItems(HorizontalAlign.Center)
      .borderRadius(OpenBoardTheme.radiusSmall)
      .backgroundColor(OpenBoardTheme.specialKey)
      .shadow(OpenBoardTheme.shadowKey)
      .onClick(() => {
        this.showEmojiPanel = false;
      })

      // EMPTY SPACE
      Flex().layoutWeight(1)

      // EMPTY SPACE
      Flex().layoutWeight(1)
    }
  }

  @Builder
  buildVoiceToolbar() {
    Row({ space: 3 }) {
      // CLOSE ICON
      Column() {
        Image($r('app.media.close_icon'))
          .width(18)
          .height(18)
      }
      .width(34)
      .height(34)
      .justifyContent(FlexAlign.Center)
      .alignItems(HorizontalAlign.Center)
      .borderRadius(OpenBoardTheme.radiusSmall)
      .backgroundColor(OpenBoardTheme.specialKey)
      .shadow(OpenBoardTheme.shadowKey)
      .onClick(() => {
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
  }

  private getClipboardTypeIcon(type: ClipboardItemType): string {
    switch (type) {
      case ClipboardItemType.URL:
        return '🔗';
      case ClipboardItemType.Email:
        return '📧';
      case ClipboardItemType.Phone:
        return '📞';
      case ClipboardItemType.Text:
      default:
        return '📝';
    }
  }

  private getFilteredClipboardItems(): ClipboardItem[] {
    let items: ClipboardItem[] = [];

    if (this.clipboardFilter === 'all') {
      items = clipboardHistory.getItems();
    } else {
      items = clipboardHistory.getItemsByType(this.clipboardFilter as ClipboardItemType);
    }

    if (this.clipboardSearchQuery && this.clipboardSearchQuery.trim().length > 0) {
      items = clipboardHistory.searchItems(this.clipboardSearchQuery);
    }

    return items;
  }

  @Builder
  buildClipboardPanel() {
    Column({ space: 10 }) {
      // Category filters
      Scroll() {
        Row({ space: 8 }) {
          // All
          Text('All')
            .fontSize(14)
            .fontWeight(500)
            .fontColor(this.clipboardFilter === 'all' ? OpenBoardTheme.accent : OpenBoardTheme.secondaryText)
            .padding({ left: 16, right: 16, top: 8, bottom: 8 })
            .backgroundColor(this.clipboardFilter === 'all' ? OpenBoardTheme.surface : OpenBoardTheme.background)
            .borderRadius(OpenBoardTheme.radiusMedium)
            .shadow(this.clipboardFilter === 'all' ? OpenBoardTheme.shadowKey : { radius: 0 })
            .onClick(() => { this.clipboardFilter = 'all'; })

          // Text
          Row({ space: 4 }) {
            Text('📝')
              .fontSize(14)
            Text('Text')
              .fontSize(14)
              .fontWeight(500)
              .fontColor(this.clipboardFilter === ClipboardItemType.Text ? OpenBoardTheme.accent : OpenBoardTheme.secondaryText)
          }
          .padding({ left: 12, right: 16, top: 8, bottom: 8 })
          .backgroundColor(this.clipboardFilter === ClipboardItemType.Text ? OpenBoardTheme.surface : OpenBoardTheme.background)
          .borderRadius(OpenBoardTheme.radiusMedium)
          .shadow(this.clipboardFilter === ClipboardItemType.Text ? OpenBoardTheme.shadowKey : { radius: 0 })
          .onClick(() => { this.clipboardFilter = ClipboardItemType.Text; })

          // URL
          Row({ space: 4 }) {
            Text('🔗')
              .fontSize(14)
            Text('Links')
              .fontSize(14)
              .fontWeight(500)
              .fontColor(this.clipboardFilter === ClipboardItemType.URL ? OpenBoardTheme.accent : OpenBoardTheme.secondaryText)
          }
          .padding({ left: 12, right: 16, top: 8, bottom: 8 })
          .backgroundColor(this.clipboardFilter === ClipboardItemType.URL ? OpenBoardTheme.surface : OpenBoardTheme.background)
          .borderRadius(OpenBoardTheme.radiusMedium)
          .shadow(this.clipboardFilter === ClipboardItemType.URL ? OpenBoardTheme.shadowKey : { radius: 0 })
          .onClick(() => { this.clipboardFilter = ClipboardItemType.URL; })

          // Email
          Row({ space: 4 }) {
            Text('📧')
              .fontSize(14)
            Text('Email')
              .fontSize(14)
              .fontWeight(500)
              .fontColor(this.clipboardFilter === ClipboardItemType.Email ? OpenBoardTheme.accent : OpenBoardTheme.secondaryText)
          }
          .padding({ left: 12, right: 16, top: 8, bottom: 8 })
          .backgroundColor(this.clipboardFilter === ClipboardItemType.Email ? OpenBoardTheme.surface : OpenBoardTheme.background)
          .borderRadius(OpenBoardTheme.radiusMedium)
          .shadow(this.clipboardFilter === ClipboardItemType.Email ? OpenBoardTheme.shadowKey : { radius: 0 })
          .onClick(() => { this.clipboardFilter = ClipboardItemType.Email; })

          // Phone
          Row({ space: 4 }) {
            Text('📞')
              .fontSize(14)
            Text('Phone')
              .fontSize(14)
              .fontWeight(500)
              .fontColor(this.clipboardFilter === ClipboardItemType.Phone ? OpenBoardTheme.accent : OpenBoardTheme.secondaryText)
          }
          .padding({ left: 12, right: 16, top: 8, bottom: 8 })
          .backgroundColor(this.clipboardFilter === ClipboardItemType.Phone ? OpenBoardTheme.surface : OpenBoardTheme.background)
          .borderRadius(OpenBoardTheme.radiusMedium)
          .shadow(this.clipboardFilter === ClipboardItemType.Phone ? OpenBoardTheme.shadowKey : { radius: 0 })
          .onClick(() => { this.clipboardFilter = ClipboardItemType.Phone; })
        }
        .padding({ left: 8, right: 8 })
      }
      .scrollable(ScrollDirection.Horizontal)
      .scrollBar(BarState.Off)

      // Clipboard items list
      Scroll(this.clipboardScroller) {
        Column({ space: 8 }) {
          ForEach(this.getFilteredClipboardItems(), (item: ClipboardItem) => {
            Row({ space: 12 }) {
              // Type icon
              Text(this.getClipboardTypeIcon(item.type))
                .fontSize(20)
                .width(30)
                .textAlign(TextAlign.Center)

              // Item content
              Column({ space: 4 }) {
                Text(item.text)
                  .fontSize(14)
                  .fontWeight(400)
                  .fontColor(OpenBoardTheme.primaryText)
                  .maxLines(2)
                  .textOverflow({ overflow: TextOverflow.Ellipsis })

                // Timestamp
                Text(this.formatTimestamp(item.timestamp))
                  .fontSize(11)
                  .fontColor(OpenBoardTheme.secondaryText)
              }
              .alignItems(HorizontalAlign.Start)
              .layoutWeight(1)
              .onClick(() => {
                this.pasteClipboardItem(item);
              })

              // Pin button
              Column() {
                Text(item.isPinned ? '📌' : '📍')
                  .fontSize(16)
              }
              .width(40)
              .height(40)
              .justifyContent(FlexAlign.Center)
              .alignItems(HorizontalAlign.Center)
              .borderRadius(OpenBoardTheme.radiusSmall)
              .backgroundColor(item.isPinned ? OpenBoardTheme.accent : OpenBoardTheme.specialKey)
              .shadow(OpenBoardTheme.shadowKey)
              .onClick(() => {
                clipboardHistory.togglePin(item.id);
              })

              // Delete button
              Column() {
                Text('🗑️')
                  .fontSize(16)
              }
              .width(40)
              .height(40)
              .justifyContent(FlexAlign.Center)
              .alignItems(HorizontalAlign.Center)
              .borderRadius(OpenBoardTheme.radiusSmall)
              .backgroundColor(OpenBoardTheme.specialKey)
              .shadow(OpenBoardTheme.shadowKey)
              .onClick(() => {
                clipboardHistory.deleteItem(item.id);
              })
            }
            .width('100%')
            .padding({ left: 12, right: 12, top: 10, bottom: 10 })
            .backgroundColor(OpenBoardTheme.surface)
            .borderRadius(OpenBoardTheme.radiusMedium)
            .shadow(OpenBoardTheme.shadowKey)
          }, (item: ClipboardItem) => item.id)

          // Empty state
          if (this.getFilteredClipboardItems().length === 0) {
            Column({ space: 8 }) {
              Text('📋')
                .fontSize(48)
                .opacity(0.3)

              Text('No clipboard items')
                .fontSize(16)
                .fontColor(OpenBoardTheme.secondaryText)
                .fontWeight(500)

              Text('Copy text to see it here')
                .fontSize(14)
                .fontColor(OpenBoardTheme.secondaryText)
            }
            .width('100%')
            .height('100%')
            .justifyContent(FlexAlign.Center)
            .alignItems(HorizontalAlign.Center)
            .padding({ top: 60 })
          }
        }
        .width('100%')
        .padding({ left: 8, right: 8 })
      }
      .scrollable(ScrollDirection.Vertical)
      .scrollBar(BarState.On)
      .layoutWeight(1)
    }
    .layoutWeight(1)
    .padding({ left: 2, right: 2, bottom: 2, top: 8 })
  }

  private formatTimestamp(timestamp: number): string {
    const now = Date.now();
    const diff = now - timestamp;
    const seconds = Math.floor(diff / 1000);
    const minutes = Math.floor(seconds / 60);
    const hours = Math.floor(minutes / 60);
    const days = Math.floor(hours / 24);

    if (seconds < 60) {
      return 'Just now';
    } else if (minutes < 60) {
      return `${minutes}m ago`;
    } else if (hours < 24) {
      return `${hours}h ago`;
    } else if (days < 7) {
      return `${days}d ago`;
    } else {
      const date = new Date(timestamp);
      return `${date.getMonth() + 1}/${date.getDate()}`;
    }
  }

  @Builder
  buildClipboardToolbar() {
    Row({ space: 3 }) {
      // RETURN TO KEYBOARD BUTTON
      Column() {
        Image($r('app.media.return_icon'))
          .width(20)
          .height(20)
          .fillColor(OpenBoardTheme.primaryText)
      }
      .width(34)
      .height(34)
      .justifyContent(FlexAlign.Center)
      .alignItems(HorizontalAlign.Center)
      .borderRadius(OpenBoardTheme.radiusSmall)
      .backgroundColor(OpenBoardTheme.specialKey)
      .shadow(OpenBoardTheme.shadowKey)
      .onClick(() => {
        this.showClipboardPanel = false;
      })

      // SEARCH INPUT
      Flex({ justifyContent: FlexAlign.Center, alignItems: ItemAlign.Center }) {
        TextInput({ placeholder: 'Search clipboard...', text: this.clipboardSearchQuery })
          .fontSize(14)
          .fontColor(OpenBoardTheme.primaryText)
          .placeholderColor(OpenBoardTheme.secondaryText)
          .backgroundColor('transparent')
          .border({ width: 0 })
          .onChange((value: string) => {
            this.clipboardSearchQuery = value;
          })
      }
      .layoutWeight(1)

      // CLEAR ALL BUTTON
      Column() {
        Text('Clear')
          .fontSize(12)
          .fontWeight(500)
          .fontColor(OpenBoardTheme.primaryText)
      }
      .width(50)
      .height(34)
      .justifyContent(FlexAlign.Center)
      .alignItems(HorizontalAlign.Center)
      .borderRadius(OpenBoardTheme.radiusSmall)
      .backgroundColor(OpenBoardTheme.specialKey)
      .shadow(OpenBoardTheme.shadowKey)
      .onClick(() => {
        clipboardHistory.clearAll();
      })
    }
  }

  build() {
    Stack() {
      Column() {}
      .width('100%')
      .height('100%')
      .hitTestBehavior(HitTestMode.None)
      .backgroundColor(OpenBoardTheme.background)
      .animation({ duration: 200 })

      Column() {
        // MAIN TOOLBAR - CONDITIONAL RENDERING
        Row({ space: 3 }) {
          if (this.isRecording) {
            // Toolbar when typing with voice
            this.buildVoiceToolbar()
          } else if (this.showClipboardPanel) {
            // Toolbar when in clipboard panel
            this.buildClipboardToolbar()
          } else if (this.showEmojiPanel) {
            // Toolbar when in emoji tab
            this.buildEmojiToolbar()
          } else if (this.showPreTypingToolbar) {
            // Toolbar before starting to type and after 5 sec of not typing
            this.buildPreTypingToolbar()
          } else {
            // Standard toolbar when typing
            this.buildStandardToolbar()
          }
        }
        .width('100%')
        .height(46)
        .padding({ left: 12, right: 12, top: 6, bottom: 6 })
        .backgroundColor(OpenBoardTheme.surface)
        .borderRadius(OpenBoardTheme.radiusMedium)
        .border({ width: 1, color: OpenBoardTheme.divider })
        .shadow(OpenBoardTheme.shadowPanel)

        // MAIN CONTENT
        if (this.showClipboardPanel) {
          // Clipboard Panel
          this.buildClipboardPanel()
        } else if (!this.showEmojiPanel) {
          // Main keyboard
          Column({ space: 5 }) {
            ForEach(this.getCurrentLayout(), (row: KeyData[]) => {
              Row({ space: 3 }) {
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
                      onKeyTouchDown: (k: KeyData, p: Position): void => this.handleKeyTouchDown(k, p),
                      onKeyTouchUp: (): void => this.handleKeyTouchUp(),
                      onSwipe: key.action === KeyAction.Space ? (direction: LayoutSwipeDirection): void => this.handleSwipe(direction) : undefined,
                      onBackspaceSwipeStart: (x: number): void => this.handleBackspaceSwipeStart(x),
                      onBackspaceSwipeMove: (x: number): void => this.handleBackspaceSwipeMove(x),
                      onBackspaceSwipeEnd: (): void => this.handleBackspaceSwipeEnd(),
                      onCursorModeStart: (x: number): void => this.handleCursorModeStart(x),
                      onCursorModeMove: (x: number): void => this.handleCursorModeMove(x),
                      onCursorModeEnd: (): void => this.handleCursorModeEnd()
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
                    .borderRadius(OpenBoardTheme.radiusMedium)
                    .shadow(this.emojiCategory === cat ? OpenBoardTheme.shadowKey : { radius: 0 })
                    .onClick(() => { this.emojiCategory = cat; })
                })
              }
              .padding({ left: 8, right: 8 })
            }

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
                      // Emoji is a boundary - don't add to currentWord
                      this.onBoundaryTyped('');
                      this.resetInactivityTimer();
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
      .backgroundColor(OpenBoardTheme.background)

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
    }
    .width('100%')
    .height('100%')
  }

  getCurrentLayout(): KeyData[][] {
    // Create cache key based on current layout and language
    const cacheKey = `${this.keyboardLayout}-${this.currentLanguage}`;

    // Return cached layout if available
    if (cacheKey === this.cachedLayoutKey && this.cachedLayout.length > 0) {
      return this.cachedLayout;
    }

    // Compute layout if cache miss
    const layout = this.computeLayout();

    // Update cache
    this.cachedLayoutKey = cacheKey;
    this.cachedLayout = layout;

    return layout;
  }

  private computeLayout(): KeyData[][] {
    const insertEmojiKey = (layout: KeyData[][] | undefined): KeyData[][] => {
      if (!layout || layout.length < 4) return layout || [];
      const lastRow = layout[layout.length - 1].slice();
      const symbolsIndex = lastRow.findIndex(k => k.action === KeyAction.SwitchSymbols);
      if (symbolsIndex !== -1 && lastRow[symbolsIndex + 1]?.action === KeyAction.Space) {
        lastRow.splice(symbolsIndex + 1, 0, EMOJI_KEY_DATA);
        const originalSpaceKey = lastRow[symbolsIndex + 2];
        if (originalSpaceKey) {
          const newSpaceKey: KeyData = {
            mainValue: originalSpaceKey.mainValue,
            shiftValue: originalSpaceKey.shiftValue,
            secondaryValue: originalSpaceKey.secondaryValue,
            action: originalSpaceKey.action,
            flex: Math.max(originalSpaceKey.flex - EMOJI_KEY_DATA.flex + 0.5, 3.5)
          };
          lastRow[symbolsIndex + 2] = newSpaceKey;
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
