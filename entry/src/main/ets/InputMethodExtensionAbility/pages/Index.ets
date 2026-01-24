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
import optimizedPredictionModel from '../model/OptimizedPredictionModel';
import nativeDictionary from '../model/NativeDictionary';
import clipboardHistory, { ClipboardItem, ClipboardItemType } from '../model/ClipboardHistory';
import feedbackController, { FeedbackType } from '../model/FeedbackController';
import { display } from '@kit.ArkUI';
import { inputMethodEngine } from '@kit.IMEKit';
import pasteboard from '@ohos.pasteboard';
import { Logger } from '../utils/logger';
import { swipeEngine, SwipePoint, KeyBounds, SwipeResult } from '../model/SwipeEngine';

const logger = new Logger('Index');
const EMOJI_KEY_DATA: KeyData = { mainValue: 'icon', secondaryValue: undefined, action: KeyAction.EMOJI_PANEL, flex: 1.2, shiftValue: undefined };

// TYPE DEFINITIONS
interface Position { x: number; y: number; }
interface AreaWithGlobalPosition { globalPosition?: Position; }
interface LongPressInfo { key: KeyData; initialPosition: Position; }
interface SingleCharPopupInfo { char: string; position: Position; }
type LanguageCode = 'en' | 'it' | 'pl' | 'ru';
enum LayoutSwipeDirection { Left, Right }
enum InputMode { Tap, Swipe } // iOS-style input mode
const ALLOWED_LANGUAGES: string[] = ['en', 'it', 'pl', 'ru'];  // Only languages with native dictionaries
const EMOJI_CATEGORIES: string[] = ['Recents', 'Smileys & Emotion', 'People', 'Animals & Nature', 'Food & Drink', 'Activity & Event', 'Travel & Places', 'Objects', 'Symbols', 'Flags'];
// iOS-style category icons
const EMOJI_CATEGORY_ICONS: Record<string, string> = {
  'Recents': '🕐',
  'Smileys & Emotion': '😀',
  'People': '👤',
  'Animals & Nature': '🐻',
  'Food & Drink': '🍔',
  'Activity & Event': '⚽',
  'Travel & Places': '🚗',
  'Objects': '💡',
  'Symbols': '❤️',
  'Flags': '🏳️'
};
const DEBUG_PREDICT = false; // Disabled for performance
const DEBUG_SWIPE = false;
const MIN_SWIPE_LAYOUT_KEYS = 20;
// iOS-style swipe thresholds
const SWIPE_ACTIVATION_THRESHOLD = 20; // px to activate swipe vs tap
const SWIPE_POINT_MIN_INTERVAL_MS = 14; // 12-16ms throttle
const SWIPE_POINT_MIN_DISTANCE_PX = 8;
const SWIPE_PATH_MAX_POINTS = 180; // 160-200 max points
const SWIPE_MIN_PATH_LENGTH = 3; // Minimum 2-3 keys for word
const SWIPE_KEY_DEBOUNCE_MS = 18; // 12-25ms debounce for key collection
const SWIPE_IGNORE_INPUT_MS = 200; // 150-250ms block after swipe

// Speech recognition simulation types
interface SpeechRecognitionResult {
  text: string;
  delay: number;
}

// HELPER FUNCTIONS
function getLanguageName(code: LanguageCode): string {
  switch (code) {
    case 'en': return 'English';
    case 'it': return 'Italian';
    case 'pl': return 'Polish';
    case 'ru': return 'Русский';
    default: return 'English';
  }
}

function getVoiceDictationStatus(code: LanguageCode): string {
  switch (code) {
    case 'en': return 'Voice typing...';
    case 'pl': return 'Piszenie głosem...';
    case 'it': return 'Digitazione vocale...';
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
    case 'ru': return 'Голосовой ввод прерван';
    default: return 'Voice input interrupted';
  }
}

function getVoicePermissionError(code: LanguageCode): string {
  switch (code) {
    case 'en': return 'Microphone permission denied';
    case 'pl': return 'Odmowa uprawnień do mikrofonu';
    case 'it': return 'Permesso microfono negato';
    case 'ru': return 'Доступ к микрофону запрещен';
    default: return 'Microphone permission denied';
  }
}

function getVoiceRecognitionError(code: LanguageCode): string {
  switch (code) {
    case 'en': return 'Speech recognition failed';
    case 'pl': return 'Rozpoznawanie mowy nie powiodło się';
    case 'it': return 'Riconoscimento vocale fallito';
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
  onSwipeStart?: (key: KeyData, position: Position) => void;
  onSwipeMove?: (position: Position) => void;
  onSwipeEnd?: (commit: boolean) => void;
  onKeyBoundsUpdate?: (key: string, bounds: KeyBounds) => void;

  @State isPressed: boolean = false;
  @State private longPressTimer: number = -1;
  @State private longPressTriggered: boolean = false;
  @State private touchStartX: number = 0;
  @State private touchStartY: number = 0;
  @State private swipeHandled: boolean = false;
  @State private pendingSwipe: boolean = false;
  @State private swipeActivated: boolean = false;

  @Prop enterKeyLabel: string;
  @Prop keyboardRootOffset: Position = { x: 0, y: 0 };
  @Prop swipeInProgress: boolean = false;
  @Prop ignoreEventsUntil: number = 0;

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
    if (this.shiftState === ShiftState.On || this.shiftState === ShiftState.CapsLock) {
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
        OpenBoardTheme.surface : ((this.keyData.action === KeyAction.Char || this.keyData.action === KeyAction.Space) ? OpenBoardTheme.regularKey : OpenBoardTheme.specialKey))
      .borderRadius(OpenBoardTheme.radiusMedium)
      .shadow(this.isPressed ? OpenBoardTheme.shadowPressed : OpenBoardTheme.shadowKey)
      .zIndex(this.isPressed ? 100 : 0)
      .animation({ duration: 100 })
      .onTouch((event) => {
        const touch = event.touches[0];
        if (!touch) return;
        const currentPosition: Position = { x: touch.windowX, y: touch.windowY };
        const now = Date.now();
        const ignoreInput = this.ignoreEventsUntil > now &&
          this.keyData.action !== KeyAction.Char &&
          this.keyData.action !== KeyAction.Space;

        if (event.type === TouchType.Down) {
          if (ignoreInput) return;
          this.touchStartX = currentPosition.x;
          this.touchStartY = currentPosition.y;
          this.isPressed = true;
          this.longPressTriggered = false;
          this.swipeHandled = false;
          this.pendingSwipe = this.keyData.action === KeyAction.Char;
          this.swipeActivated = false;

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
            this.pendingSwipe = false;

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
          if (ignoreInput) return;
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

          if (this.keyData.action === KeyAction.Char && this.pendingSwipe && !this.swipeActivated && !this.longPressTriggered) {
            const deltaX = currentPosition.x - this.touchStartX;
            const deltaY = currentPosition.y - this.touchStartY;
            const distance = Math.sqrt((deltaX * deltaX) + (deltaY * deltaY));

            if (distance > SWIPE_ACTIVATION_THRESHOLD) {
              clearTimeout(this.longPressTimer);
              this.swipeHandled = true;
              this.pendingSwipe = false;
              this.swipeActivated = true;

              if (this.onSwipeStart) {
                this.onSwipeStart(this.keyData, { x: this.touchStartX, y: this.touchStartY });
              }
              if (this.onSwipeMove) {
                this.onSwipeMove(currentPosition);
              }
            }
          }
        }

        if (event.type === TouchType.Up || event.type === TouchType.Cancel) {
          clearTimeout(this.longPressTimer);

          if (ignoreInput) {
            // End backspace swipe tracking
            if (this.keyData.action === KeyAction.Backspace) {
              this.onBackspaceSwipeEnd();
            }

            // End cursor mode tracking
            if (this.keyData.action === KeyAction.Space) {
              this.onCursorModeEnd();
            }

            // Hide popup
            this.onKeyTouchUp();

            this.isPressed = false;
            this.longPressTriggered = false;
            this.touchStartX = 0;
            this.touchStartY = 0;
            this.swipeHandled = false;
            this.pendingSwipe = false;
            this.swipeActivated = false;
            return;
          }

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
          } else if (event.type === TouchType.Up && !this.longPressTriggered && !this.swipeHandled && !this.swipeInProgress) {
            // Only trigger keyPress if no swipe was handled
            this.onKeyPress(this.keyData);
          }

          // Hide popup
          this.onKeyTouchUp();

          this.isPressed = false;
          this.longPressTriggered = false;
          this.touchStartX = 0;
          this.touchStartY = 0;
          this.swipeHandled = false;
          this.pendingSwipe = false;
          this.swipeActivated = false;
        }
      })
      .onAreaChange((_oldValue: Area, newValue: Area) => {
        if (this.onKeyBoundsUpdate && this.keyData.action === KeyAction.Char) {
          const areaAny = newValue as AreaWithGlobalPosition;
          const globalPosition = areaAny.globalPosition;
          const basePosition = globalPosition ?? {
            x: (newValue.position.x as number) + this.keyboardRootOffset.x,
            y: (newValue.position.y as number) + this.keyboardRootOffset.y
          };
          const bounds: KeyBounds = {
            key: this.keyData.mainValue.toLowerCase(),
            centerX: basePosition.x + (newValue.width as number) / 2,
            centerY: basePosition.y + (newValue.height as number) / 2,
            width: newValue.width as number,
            height: newValue.height as number
          };
          this.onKeyBoundsUpdate(this.keyData.mainValue.toLowerCase(), bounds);
        }
      })
    }
    .padding({ left: 2, right: 2, top: 2, bottom: 2 })
  }
}

interface GeneratedTypeLiteralInterface_1 {
  original: string;
  corrected: string;
}

@Entry
@Component
struct Index {
  @State currentLanguage: LanguageCode = 'ru';
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
  @State emojiSwiperIndex: number = 1;  // Index in EMOJI_CATEGORIES (1 = 'Smileys & Emotion')
  @State recentEmojis: string[] = [];
  private emojiSwiperController: SwiperController = new SwiperController();
  private readonly MAX_RECENT_EMOJIS: number = 30;

  @State showClipboardPanel: boolean = false;
  @State clipboardFilter: ClipboardItemType | 'all' = 'all';
  @State clipboardSearchQuery: string = '';
  private clipboardScroller: Scroller = new Scroller();
  private lastClipboardText: string = '';
  private clipboardMonitorTimer: number = -1;

  @State private previousWord: string = '';
  @State private prevPrevWord: string = ''; // For trigram predictions
  @State private currentWord: string = '';
  @State private suggestions: string[] = ['', '', '', '', ''];
  @State private lastAutocorrection: GeneratedTypeLiteralInterface_1 | null = null;
  private suggestionsReqId: number = 0; // Race condition protection for async suggestions
  private suggestionsDebounceTimer: number | null = null; // Debounce timer for updateSuggestions

  // iOS-style swipe input states
  private inputMode: InputMode = InputMode.Tap;
  @State private isSwipeActive: boolean = false; // For UI (trail rendering)
  private swipeCommitted: boolean = false;
  private swipePath: SwipePoint[] = []; // Raw touch points
  private swipeKeySeq: string[] = []; // Collected letter sequence
  private lastKeyId: string | null = null;
  private candidateKeyId: string | null = null;
  private candidateKeyTs: number = 0;
  private swipeStartPt: Position | null = null;
  private swipeStartTs: number = 0;
  private ignoreKeysUntilTs: number = 0;
  @State private swipeTrailPoints: Position[] = []; // Trail visualization
  private lastTrailPointTs: number = 0;
  private trailCanvasContext: CanvasRenderingContext2D = new CanvasRenderingContext2D();

  // Layout bounds management (per-language)
  private keyBoundsMap: Map<string, KeyBounds> = new Map(); // Key: "${lang}:${layoutId}:${keyId}"
  @State private keyboardRootOffset: Position = { x: 0, y: 0 };
  private currentLayoutId: string = ''; // Track current layout for bounds validation

  // Error rate limiting
  private insertErrorCount: number = 0;
  private lastErrorWindowTs: number = 0;
  private lastErrorLogTs: number = 0;

  @State private isRecording: boolean = false;
  @State private isProcessingVoiceToggle: boolean = false;

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
        console.info('[HOSKEY] New clipboard content detected');
        clipboardHistory.addItem(text);
        this.lastClipboardText = text;
      }
    } catch (e) {
      console.error('[HOSKEY] Clipboard monitoring failed', e);
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

    console.info('[HOSKEY] Clipboard monitoring started');
  }

  private stopClipboardMonitoring(): void {
    if (this.clipboardMonitorTimer !== -1) {
      clearInterval(this.clipboardMonitorTimer);
      this.clipboardMonitorTimer = -1;
      console.info('[HOSKEY] Clipboard monitoring stopped');
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
                  this.currentLanguage === 'ru' ? 'Ручной ввод' : 'Manual input');
          break;
        case 'clipboard':
          message = getVoiceInterruptionMessage(this.currentLanguage) + ' - ' +
            (this.currentLanguage === 'en' ? 'Clipboard access' :
              this.currentLanguage === 'pl' ? 'Dostęp do schowka' :
                this.currentLanguage === 'it' ? 'Accesso agli appunti' :
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
    console.info('[HOSKEY] UI onPageShow called');
    this.availableLanguages = ALLOWED_LANGUAGES.map((c: string) => c as LanguageCode);
    if (!this.availableLanguages.includes(this.currentLanguage)) {
      this.currentLanguage = this.availableLanguages[0];
    }
    this.startInactivityTimer();
    this.startClipboardMonitoring();
    // Auto-capitalize at the start of new typing session
    this.enableAutoCapitalization();

    // Warmup NativeDictionary for current language
    optimizedPredictionModel.warmup(this.currentLanguage).catch((err: Error) => {
      console.error('[HOSKEY] Failed to warmup language on start:', err);
    });
  }

  // === SWIPE TYPING METHODS ===

  /**
   * Store key bounds with layout-specific key: "${lang}:${layoutId}:${keyId}"
   * Only for Alphabet layout (not Symbols)
   */
  private handleKeyBoundsUpdate(key: string, bounds: KeyBounds): void {
    // Only store bounds for Alphabet layout (not Symbols)
    if (this.keyboardLayout !== KeyboardLayout.Alphabet) {
      return;
    }

    const boundsKey = `${this.currentLanguage}:${this.currentLayoutId}:${key}`;
    this.keyBoundsMap.set(boundsKey, bounds);

    if (DEBUG_SWIPE && this.isSwipeLayoutReady()) {
      console.info(`[SwipeDebug] Stored bounds for ${boundsKey}, total=${this.keyBoundsMap.size}`);
    }
  }

  private updateSwipeEngineLayout(): void {
    const keyBounds = Array.from(this.keyBoundsMap.values());

    // Update ETS swipe engine (fallback)
    swipeEngine.setKeyboardLayout(keyBounds);

    // Update native swipe engine if available
    if (nativeDictionary.isNativeAvailable()) {
      try {
        const nativeBounds: KeyBounds[] = keyBounds.map(kb => ({
          key: kb.key,
          centerX: kb.centerX,
          centerY: kb.centerY,
          width: kb.width,
          height: kb.height
        } as KeyBounds));
        nativeDictionary.setSwipeKeyboardLayout(nativeBounds);
      } catch (err) {
        console.error('[Swipe] Failed to set native keyboard layout:', err);
      }
    }
  }

  private isSwipeLayoutReady(): boolean {
    return this.keyBoundsMap.size >= MIN_SWIPE_LAYOUT_KEYS;
  }

  /**
   * iOS-style swipe start: TouchDown handler
   * Just saves start position - swipe not yet activated
   */
  private handleSwipeStart(key: KeyData, position: Position): void {
    if (key.action !== KeyAction.Char) return;

    const now = Date.now();
    if (now < this.ignoreKeysUntilTs) return;

    // Reset state (not yet swipe mode)
    this.swipeStartPt = { x: position.x, y: position.y };
    this.swipeStartTs = now;
    this.isSwipeActive = false;
    this.swipeCommitted = false;
    this.inputMode = InputMode.Tap;

    // Clear all buffers
    this.swipePath = [];
    this.swipeKeySeq = [];
    this.swipeTrailPoints = [];
    this.lastKeyId = null;
    this.candidateKeyId = null;
    this.candidateKeyTs = 0;
    this.lastTrailPointTs = now;

    // Store first key (start key)
    const char = (this.shiftState === ShiftState.On || this.shiftState === ShiftState.CapsLock)
      ? (key.shiftValue ?? key.mainValue)
      : key.mainValue;
    this.lastKeyId = key.mainValue;
    this.swipeKeySeq.push(char.toLowerCase());
  }

  /**
   * iOS-style swipe move: Collect path & letters WITHOUT inserting text
   * NO preview updates, NO insertText calls
   */
  private handleSwipeMove(position: Position): void {
    if (this.swipeCommitted) return;

    const now = Date.now();
    if (now < this.ignoreKeysUntilTs) return;

    if (!this.swipeStartPt) return;

    // 1) Check if swipe should be activated
    if (!this.isSwipeActive) {
      const deltaX = position.x - this.swipeStartPt.x;
      const deltaY = position.y - this.swipeStartPt.y;
      const distance = Math.sqrt(deltaX * deltaX + deltaY * deltaY);

      if (distance < SWIPE_ACTIVATION_THRESHOLD) {
        return; // Still tap mode
      }

      // Activate swipe mode
      this.isSwipeActive = true;
      this.inputMode = InputMode.Swipe;

      // Initialize trail with start point
      this.swipeTrailPoints = [{ x: this.swipeStartPt.x, y: this.swipeStartPt.y }];
    }

    // 2) Collect path points with throttling
    if (this.swipePath.length > 0) {
      const lastPoint = this.swipePath[this.swipePath.length - 1];
      const deltaX = position.x - lastPoint.x;
      const deltaY = position.y - lastPoint.y;
      const distance = Math.sqrt(deltaX * deltaX + deltaY * deltaY);
      const timeSinceLast = now - lastPoint.timestamp;

      if (timeSinceLast < SWIPE_POINT_MIN_INTERVAL_MS && distance < SWIPE_POINT_MIN_DISTANCE_PX) {
        return; // Skip this point
      }
    }

    // Add path point
    if (this.swipePath.length >= SWIPE_PATH_MAX_POINTS) {
      this.swipePath.shift();
    }
    this.swipePath.push({ x: position.x, y: position.y, timestamp: now });

    // 3) Update trail visualization
    const timeSinceLastTrail = now - this.lastTrailPointTs;
    const lastTrail = this.swipeTrailPoints[this.swipeTrailPoints.length - 1];
    const trailDistance = lastTrail ?
      Math.sqrt((position.x - lastTrail.x) ** 2 + (position.y - lastTrail.y) ** 2) : 999;

    if (timeSinceLastTrail >= 16 || trailDistance >= 8) {
      this.swipeTrailPoints.push({ x: position.x, y: position.y });
      this.lastTrailPointTs = now;

      if (this.swipeTrailPoints.length > 50) {
        this.swipeTrailPoints.shift();
      }

      this.renderTrailPath();
    }

    // 4) Collect letter sequence via hit-test (NO insertText!)
    const nearestKey = this.findNearestKey(position);
    if (nearestKey && nearestKey.action === KeyAction.Char) {
      const keyId = nearestKey.mainValue;

      // Ignore keys not from current layout
      const boundsKey = `${this.currentLanguage}:${this.currentLayoutId}:${keyId}`;
      if (!this.keyBoundsMap.has(boundsKey)) {
        return; // Wrong layout/language
      }

      // Check if this is a new key
      if (keyId === this.lastKeyId) {
        return; // Same key, skip
      }

      // Debounce: candidate key must be stable for SWIPE_KEY_DEBOUNCE_MS
      if (this.candidateKeyId !== keyId) {
        this.candidateKeyId = keyId;
        this.candidateKeyTs = now;
        return;
      }

      if (now - this.candidateKeyTs >= SWIPE_KEY_DEBOUNCE_MS) {
        // Commit this letter to sequence
        const char = (this.shiftState === ShiftState.On || this.shiftState === ShiftState.CapsLock)
          ? (nearestKey.shiftValue ?? nearestKey.mainValue)
          : nearestKey.mainValue;

        this.swipeKeySeq.push(char.toLowerCase());
        this.lastKeyId = keyId;
        this.candidateKeyId = null;
      }
    }

    // ❌ NO updateSwipePreview()
    // ❌ NO insertText()
    // ❌ NO prediction/autocorrection
  }

  /**
   * iOS-style swipe end: SINGLE commit point, ONLY insertText call
   * NO autocorrection, NO onBoundaryTyped, NO mid-swipe updates
   */
  private handleSwipeEnd(commit: boolean = true): void {
    // Guard: not in swipe mode
    if (!this.isSwipeActive && this.inputMode === InputMode.Tap) {
      return;
    }

    // Guard: already committed
    if (this.swipeCommitted) {
      return;
    }

    this.swipeCommitted = true;
    const now = Date.now();

    // Block tap input for SWIPE_IGNORE_INPUT_MS
    this.ignoreKeysUntilTs = now + SWIPE_IGNORE_INPUT_MS;

    // Reset mode immediately
    this.inputMode = InputMode.Tap;
    this.isSwipeActive = false;

    // Don't commit if cancelled
    if (!commit) {
      this.resetSwipeState();
      return;
    }

    // Validate minimum requirements
    const duration = now - this.swipeStartTs;
    if (duration < 100 || this.swipeKeySeq.length < SWIPE_MIN_PATH_LENGTH) {
      this.resetSwipeState();
      return;
    }

    // Get word from swipe engine
    if (!this.isSwipeLayoutReady()) {
      this.resetSwipeState();
      return;
    }

    this.updateSwipeEngineLayout();

    // Try native swipe engine first, fallback to ETS if unavailable
    let result: SwipeResult | null = null;

    if (nativeDictionary.isNativeAvailable() && nativeDictionary.isDictionaryLoaded()) {
      try {
        const nativePoints: SwipePoint[] = this.swipePath.map(p => ({
          x: p.x,
          y: p.y,
          timestamp: p.timestamp
        } as SwipePoint));

        const nativeResult = nativeDictionary.processSwipePath(nativePoints);

        if (nativeResult) {
          // Convert native result to SwipeResult format
          result = {
            bestWord: nativeResult.bestWord,
            alternatives: nativeResult.alternatives,
            confidence: nativeResult.confidence,
            rawSequence: nativeResult.rawSequence
          };
          console.info('[Swipe] Native swipe processed:', result.bestWord);
        }
      } catch (err) {
        console.error('[Swipe] Native swipe failed, falling back to ETS:', err);
      }
    }

    // Fallback to ETS swipe engine if native failed or unavailable
    if (!result) {
      result = swipeEngine.processSwipe(this.swipePath);
      console.info('[Swipe] ETS swipe processed:', result?.bestWord || 'null');
    }

    if (result && result.bestWord) {
      let word = result.bestWord;

      // Apply capitalization
      if (this.shiftState === ShiftState.On || this.shiftState === ShiftState.CapsLock) {
        word = word.charAt(0).toUpperCase() + word.slice(1);
      }

      // SINGLE insertText with error handling
      try {
        keyboardController.insertText(word);

        // Track word for context (NO autocorrection)
        this.previousWord = word;
        this.currentWord = '';

        // Reset error counter on success
        this.insertErrorCount = 0;

        // Update suggestions AFTER commit
        this.suggestions = [
          word,
          result.alternatives[0] || '',
          result.alternatives[1] || '',
          result.alternatives[2] || '',
          result.alternatives[3] || ''
        ];

        // Reset shift if needed
        if (this.shiftState === ShiftState.On) {
          this.shiftState = ShiftState.Off;
        }

      } catch (error) {
        this.handleInsertError(error, 'swipe');
      }
    }

    // Clean up
    this.resetSwipeState();
    this.debouncedUpdateSuggestions(); // Refresh after commit
  }

  /**
   * Reset swipe state after commit/cancel
   */
  private resetSwipeState(): void {
    this.inputMode = InputMode.Tap;
    this.isSwipeActive = false;
    this.swipeCommitted = false;
    this.swipePath = [];
    this.swipeKeySeq = [];
    this.swipeTrailPoints = [];
    this.lastKeyId = null;
    this.candidateKeyId = null;
    this.candidateKeyTs = 0;
    this.swipeStartPt = null;
    this.swipeStartTs = 0;
    this.lastTrailPointTs = 0;
  }

  /**
   * Find nearest key via hit-test for swipe letter collection
   * Only returns keys from current layout/language
   */
  private findNearestKey(position: Position): KeyData | null {
    let nearestKey: KeyData | null = null;
    let minDistance = Infinity;

    const layout = this.getCurrentLayout();
    for (const row of layout) {
      for (const key of row) {
        if (key.action !== KeyAction.Char) continue;

        const boundsKey = `${this.currentLanguage}:${this.currentLayoutId}:${key.mainValue}`;
        const bounds = this.keyBoundsMap.get(boundsKey);
        if (!bounds) continue;

        // Calculate bounds from centerX/centerY + width/height
        const left = bounds.centerX - bounds.width / 2;
        const right = bounds.centerX + bounds.width / 2;
        const top = bounds.centerY - bounds.height / 2;
        const bottom = bounds.centerY + bounds.height / 2;

        // Check if point is inside bounds
        const inside = position.x >= left &&
                       position.x <= right &&
                       position.y >= top &&
                       position.y <= bottom;

        if (inside) {
          return key; // Direct hit
        }

        // Calculate distance to center
        const centerX = bounds.centerX;
        const centerY = bounds.centerY;
        const distance = Math.sqrt(
          (position.x - centerX) ** 2 +
          (position.y - centerY) ** 2
        );

        if (distance < minDistance) {
          minDistance = distance;
          nearestKey = key;
        }
      }
    }

    return nearestKey;
  }

  /**
   * Error handler with rate limiting (300ms window)
   * Prevents "burst" errors from crashing IME
   */
  private handleInsertError(error: Error | object | string, source: string): void {
    const now = Date.now();

    // Reset counter if outside error window
    if (now - this.lastErrorWindowTs > 300) {
      this.insertErrorCount = 0;
      this.lastErrorWindowTs = now;
    }

    this.insertErrorCount++;

    // Log throttling (max 1/sec)
    if (now - this.lastErrorLogTs > 1000) {
      console.error(`[${source}] insertText error (count=${this.insertErrorCount}):`, error);
      this.lastErrorLogTs = now;
    }

    // Block input on burst (>3 errors in 300ms)
    if (this.insertErrorCount > 3) {
      this.ignoreKeysUntilTs = now + 400;
      console.warn(`[${source}] Error burst detected, blocking input for 400ms`);
    }
  }

  /**
   * Render swipe trail path on Canvas
   * Draws a smooth polyline following the user's finger movement
   */
  private renderTrailPath(): void {
    if (this.swipeTrailPoints.length < 2) {
      return;
    }

    this.trailCanvasContext.clearRect(0, 0, this.trailCanvasContext.width, this.trailCanvasContext.height);

    // Trail style: semi-transparent blue line
    this.trailCanvasContext.strokeStyle = 'rgba(33, 150, 243, 0.6)'; // Blue with 60% opacity
    this.trailCanvasContext.lineWidth = 4;
    this.trailCanvasContext.lineCap = 'round';
    this.trailCanvasContext.lineJoin = 'round';

    // Draw polyline through all trail points
    this.trailCanvasContext.beginPath();
    this.trailCanvasContext.moveTo(this.swipeTrailPoints[0].x, this.swipeTrailPoints[0].y);

    for (let i = 1; i < this.swipeTrailPoints.length; i++) {
      this.trailCanvasContext.lineTo(this.swipeTrailPoints[i].x, this.swipeTrailPoints[i].y);
    }

    this.trailCanvasContext.stroke();
  }

  // === END SWIPE TYPING METHODS ===

  aboutToAppear(): void {
    console.info('[HOSKEY] UI aboutToAppear called');
    keyboardController.setEnterKeyTypeCallback((enterKeyType: number) => {
      this.updateEnterKeyLabel(enterKeyType);
    });
    this.updateEnterKeyLabel(keyboardController.getCurrentEnterKeyType());

    // Initialize currentLayoutId (iOS-style swipe)
    this.currentLayoutId = `${this.currentLanguage}_alphabet`;
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
      keyboardController.deleteBeforeCursor(1);
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
        keyboardController.deleteBeforeCursor(1);
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
    const char = (this.shiftState === ShiftState.On || this.shiftState === ShiftState.CapsLock)
      ? (key.shiftValue ?? key.mainValue)
      : key.mainValue;
    this.singleCharPopupInfo = { char: char, position: position };
  }

  private handleKeyTouchUp(): void {
    // Hide the popup
    this.singleCharPopupInfo = null;
  }

  async handleKeyPress(key: KeyData): Promise<void> {
    // iOS-style: Block ALL input during swipe
    if (this.inputMode === InputMode.Swipe || this.isSwipeActive) {
      return;
    }

    // Block during ignore window
    const now = Date.now();
    if (now < this.ignoreKeysUntilTs && key.action !== KeyAction.Char && key.action !== KeyAction.Space) {
      return;
    }

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
        let text = (this.shiftState === ShiftState.On || this.shiftState === ShiftState.CapsLock)
          ? key.shiftValue ?? key.mainValue
          : key.mainValue;
        if (now < this.ignoreKeysUntilTs && !this.isBoundaryChar(text)) {
          return;
        }

        // insertText with error handling
        try {
          keyboardController.insertText(text);
          this.insertErrorCount = 0; // Reset on success
        } catch (error) {
          this.handleInsertError(error, 'tap');
          break; // Don't proceed on error
        }

        await this.onTextTyped(text);
        // Only turn off shift if it was on BEFORE typing (not if enabled by typing boundary char like '.')
        if (wasShiftOnBeforeTyping) {
          logger.autocap('Disabling shift after typing character');
          this.shiftState = ShiftState.Off;
        }
        break;
      case KeyAction.Space:
        feedbackController.provideFeedback(FeedbackType.Light);
        await this.onBoundaryTyped(' ');
        // Auto-return to alphabet layout after typing space in symbols layout
        if (this.keyboardLayout === KeyboardLayout.Symbols1 || this.keyboardLayout === KeyboardLayout.Symbols2) {
          this.keyboardLayout = KeyboardLayout.Alphabet;
        }
        break;
      case KeyAction.Enter:
        feedbackController.provideFeedback(FeedbackType.Medium);
        keyboardController.sendEnterKey();
        await this.onBoundaryTyped('\n');
        break;
      case KeyAction.Backspace:
        feedbackController.provideFeedback(FeedbackType.Heavy);
        keyboardController.deleteBeforeCursor(1);
        await this.onBackspaceTyped();
        break;
      case KeyAction.Shift:
        feedbackController.provideFeedback(FeedbackType.Medium);
        // Use 'now' from line 1478 (function scope)
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
        const wasAlphabet = this.keyboardLayout === KeyboardLayout.Alphabet;
        this.keyboardLayout = wasAlphabet ? KeyboardLayout.Symbols1 : KeyboardLayout.Alphabet;

        // Update layoutId when switching back to Alphabet
        if (!wasAlphabet) {
          this.currentLayoutId = `${this.currentLanguage}_alphabet`;
        }
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
      console.info('[HOSKEY] Pasted clipboard item:', item.id);

      // Close clipboard panel after pasting
      this.showClipboardPanel = false;
    }
  }

  handleSwipe(direction: LayoutSwipeDirection): void {
    try {
      if (this.availableLanguages.length <= 1) {
        console.warn('[HOSKEY] Only one language enabled, cannot switch.');
        return;
      }

      let currentIndex = this.availableLanguages.indexOf(this.currentLanguage);

      // Guard: if current language not in list, reset to first
      if (currentIndex < 0) {
        console.warn('[HOSKEY] Current language not in available list, resetting');
        currentIndex = 0;
      }

      if (direction === LayoutSwipeDirection.Right) {
        currentIndex = (currentIndex - 1 + this.availableLanguages.length) % this.availableLanguages.length;
      } else {
        currentIndex = (currentIndex + 1) % this.availableLanguages.length;
      }

      const newLang = this.availableLanguages[currentIndex];
      if (!newLang) {
        console.error('[HOSKEY] Invalid language index:', currentIndex);
        return;
      }

      this.currentLanguage = newLang;
      console.info('[HOSKEY] Switched to language:', this.currentLanguage);

      // Clear keyBoundsMap on language change (iOS-style)
      this.keyBoundsMap.clear();
      this.currentLayoutId = `${this.currentLanguage}_alphabet`;

      // Warmup NativeDictionary for new language
      optimizedPredictionModel.warmup(this.currentLanguage).catch((err: Error) => {
        console.error('[HOSKEY] Failed to warmup language:', err);
      });
    } catch (err) {
      console.error('[HOSKEY] handleSwipe error:', err);
    }
  }

  /**
   * Debounced version of updateSuggestions - reduces load during fast typing
   * Waits 150ms after last keystroke before triggering prediction
   */
  private debouncedUpdateSuggestions(): void {
    // Clear existing timer
    if (this.suggestionsDebounceTimer !== null) {
      clearTimeout(this.suggestionsDebounceTimer);
    }

    // Set new timer
    this.suggestionsDebounceTimer = setTimeout(() => {
      this.suggestionsDebounceTimer = null;
      void this.updateSuggestions();
    }, 150); // 150ms debounce - good balance between responsiveness and performance
  }

  private async updateSuggestions(): Promise<void> {
    // Increment request ID to handle race conditions
    this.suggestionsReqId++;
    const currentReqId = this.suggestionsReqId;

    try {
      // Check for autocorrection first
      const autocorrection = this.currentWord.length >= 2
        ? await optimizedPredictionModel.findAutocorrection(this.currentLanguage, this.currentWord)
        : null;

      // Check if request is stale (user typed more keys while we were waiting)
      if (currentReqId !== this.suggestionsReqId) {
        return;
      }

      if (autocorrection && autocorrection !== this.currentWord) {
        // Show autocorrection as first suggestion, then predictions
        const list = await optimizedPredictionModel.predict(this.currentLanguage, this.previousWord, this.currentWord, 2, this.prevPrevWord);

        // Check again after async call
        if (currentReqId !== this.suggestionsReqId) {
          return;
        }

        this.suggestions = [autocorrection, list[0] ?? '', list[1] ?? '', list[2] ?? '', list[3] ?? ''];

        if (DEBUG_PREDICT) {
          logger.info(`[Predict] lang=${this.currentLanguage} prefix="${this.currentWord}" prev="${this.previousWord}" prev2="${this.prevPrevWord}" autocorrect="${autocorrection}" predictions=[${list.join(', ')}]`);
        }
      } else {
        // Normal predictions
        const list = await optimizedPredictionModel.predict(this.currentLanguage, this.previousWord, this.currentWord, 5, this.prevPrevWord);

        // Check again after async call
        if (currentReqId !== this.suggestionsReqId) {
          return;
        }

        this.suggestions = [list[0] ?? '', list[1] ?? '', list[2] ?? '', list[3] ?? '', list[4] ?? ''];

        if (DEBUG_PREDICT) {
          logger.info(`[Predict] lang=${this.currentLanguage} prefix="${this.currentWord}" prev="${this.previousWord}" prev2="${this.prevPrevWord}" predictions=[${list.join(', ')}]`);
        }
      }
    } catch (e) {
      console.error(`[Index] EXCEPTION in updateSuggestions:`, e);
      logger.warn('Prediction model error', e as Error);
      this.suggestions = ['', '', '', '', ''];
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
    // Only enable if not already in CapsLock mode
    if (this.shiftState !== ShiftState.CapsLock) {
      logger.autocap(`Enabling auto-capitalization, shift state: ${this.shiftState}`);
      this.shiftState = ShiftState.On;
    } else {
      logger.autocap('Already in CapsLock, not changing');
    }
  }

  private async onTextTyped(text: string): Promise<void> {
    if (!text) {
      return;
    }
    if (this.isBoundaryChar(text)) {
      // For boundary chars from KeyAction.Char, the char was already inserted
      // by handleKeyPress, so we call onBoundaryTyped with shouldInsert=false
      await this.onBoundaryTyped(text, false);
      return;
    }
    this.currentWord = this.currentWord + text;
    this.debouncedUpdateSuggestions(); // Debounced to reduce load during fast typing
    this.resetInactivityTimer();
  }

  /**
   * Handle boundary character (space, punctuation, etc.)
   * @param ch - the boundary character
   * @param shouldInsert - whether to insert the char (false if already inserted by handleKeyPress)
   */
  private async onBoundaryTyped(ch: string, shouldInsert: boolean = true): Promise<void> {
    if (this.currentWord) {
      // Check for autocorrection on space
      if (ch === ' ') {
        const correction = await optimizedPredictionModel.findAutocorrection(this.currentLanguage, this.currentWord);
        if (correction && correction !== this.currentWord) {
          // Autocorrection found! Replace the typed word
          console.info(`[Autocorrect] "${this.currentWord}" → "${correction}"`);

          // Delete the typed word before cursor using canonical delete semantics
          keyboardController.deleteBeforeCursor(this.currentWord.length);

          // Insert the corrected word
          keyboardController.insertText(correction);

          // Track the autocorrection for potential undo
          this.lastAutocorrection = {
            original: this.currentWord,
            corrected: correction
          };

          // Record corrected word with trigram context
          optimizedPredictionModel.recordWord(this.currentLanguage, this.previousWord, correction, this.prevPrevWord);

          // Shift word history for trigram
          this.prevPrevWord = this.previousWord;
          this.previousWord = correction;
        } else {
          // No correction, record the word as typed with trigram context
          optimizedPredictionModel.recordWord(this.currentLanguage, this.previousWord, this.currentWord, this.prevPrevWord);

          // Shift word history for trigram
          this.prevPrevWord = this.previousWord;
          this.previousWord = this.currentWord;
        }

        // NOTE: Space insertion moved outside if (this.currentWord) block
        // to ensure space is ALWAYS inserted, even when currentWord is empty
      } else {
        // Non-space boundary, just record the word with trigram context
        optimizedPredictionModel.recordWord(this.currentLanguage, this.previousWord, this.currentWord, this.prevPrevWord);

        // Shift word history for trigram
        this.prevPrevWord = this.previousWord;
        this.previousWord = this.currentWord;
      }

      this.currentWord = '';
    }

    // Insert the boundary character if needed
    // (shouldInsert=false when char was already inserted by handleKeyPress for KeyAction.Char)
    if (shouldInsert) {
      keyboardController.insertText(ch);
    }

    this.debouncedUpdateSuggestions(); // Debounced to reduce load
    this.resetInactivityTimer();

    // Auto-capitalize after sentence-ending punctuation
    logger.autocap(`Boundary typed: ${ch}, should auto-cap: ${this.shouldAutoCapitalize(ch)}`);
    if (this.shouldAutoCapitalize(ch)) {
      this.enableAutoCapitalization();
    }
  }

  private async onBackspaceTyped(): Promise<void> {
    if (this.currentWord && this.currentWord.length > 0) {
      this.currentWord = this.currentWord.substring(0, this.currentWord.length - 1);
      this.debouncedUpdateSuggestions(); // Debounced to reduce load
      this.resetInactivityTimer();
    }

    if (keyboardController.isBufferEmpty()) {
      // Reset all word history when buffer is empty
      this.prevPrevWord = '';
      this.previousWord = '';
      this.currentWord = '';
      this.debouncedUpdateSuggestions(); // Debounced to reduce load
      this.showPreTypingToolbar = true;
      // Auto-capitalize at the start of text
      this.enableAutoCapitalization();
    } else {
      this.showPreTypingToolbar = false;
    }
  }

  private async pickSuggestion(word: string): Promise<void> {
    this.resetInactivityTimer();
    // Delete the typed word before cursor using canonical delete semantics
    keyboardController.deleteBeforeCursor(this.currentWord.length);
    keyboardController.insertText(word);
    keyboardController.insertText(' ');

    // Record the picked suggestion word with trigram context
    // Don't call onBoundaryTyped to avoid double recording
    optimizedPredictionModel.recordWord(this.currentLanguage, this.previousWord, word, this.prevPrevWord);

    // Shift word history for trigram
    this.prevPrevWord = this.previousWord;
    this.previousWord = word;
    this.currentWord = '';
    this.debouncedUpdateSuggestions(); // Debounced to reduce load
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
          .accessibilityText('Hide keyboard')
      }
      .width(34)
      .height(34)
      .justifyContent(FlexAlign.Center)
      .alignItems(HorizontalAlign.Center)
      .borderRadius(OpenBoardTheme.radiusSmall)
      .backgroundColor(OpenBoardTheme.specialKey)
      .shadow(OpenBoardTheme.shadowKey)
      .onClick(() => { keyboardController.hideKeyboard(); })

      // SUGGESTIONS (iOS-style: NO preview during swipe)
      Flex({ justifyContent: FlexAlign.Center, alignItems: ItemAlign.Center }) {
        Row({ space: 0 }) {
          ForEach(
            this.isSwipeActive ? ['', '', '', '', ''] : this.suggestions,
            (suggestion: string, index: number) => {
            Column() {
              Text(suggestion)
                .fontSize(13)
                .fontWeight(500)
                .fontColor(OpenBoardTheme.primaryText)
                .textAlign(TextAlign.Center)
                .maxLines(1).textOverflow({ overflow: TextOverflow.Ellipsis })
                .accessibilityText(suggestion ? `Suggestion: ${suggestion}` : '')
            }
            .justifyContent(FlexAlign.Center)
            .alignItems(HorizontalAlign.Center)
            .layoutWeight(1)
            .onClick(async () => {
              if (suggestion) await this.pickSuggestion(suggestion);
            })

            if (index < 4) {
              Text('|')
                .fontSize(13)
                .fontWeight(400)
                .fontColor(OpenBoardTheme.secondaryText)
            }
          })
        }
        .width('100%')
        .justifyContent(FlexAlign.Center)
        .alignItems(VerticalAlign.Center)
      }
      .layoutWeight(1)
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
          .accessibilityText('Hide keyboard')
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
          .accessibilityText('Open clipboard panel')
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
          .accessibilityText('Return to keyboard')
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
          .accessibilityText('Hide keyboard')
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
            .accessibilityText('Microphone recording')

          Text(getVoiceDictationStatus(this.currentLanguage))
            .fontSize(16)
            .fontWeight(500)
            .fontColor(OpenBoardTheme.primaryText)
            .accessibilityText(`${getVoiceDictationStatus(this.currentLanguage)}`)

          // Show recognized text preview if available
          if (this.recognizedText) {
            Text(`: "${this.recognizedText}"`)
              .fontSize(14)
              .fontWeight(400)
              .fontColor(OpenBoardTheme.secondaryText)
              .maxLines(1)
              .textOverflow({ overflow: TextOverflow.Ellipsis })
              .accessibilityText(`Recognized text: ${this.recognizedText}`)
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
      case ClipboardItemType.Link:
        return '🔗';
      case ClipboardItemType.Email:
        return '📧';
      case ClipboardItemType.Phone:
        return '📞';
      case ClipboardItemType.Image:
        return '🖼️';
      case ClipboardItemType.File:
        return '📁';
      case ClipboardItemType.Emoji:
        return '😃';
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

          // Images
          Row({ space: 4 }) {
            Text('🖼️')
              .fontSize(14)
            Text('Images')
              .fontSize(14)
              .fontWeight(500)
              .fontColor(this.clipboardFilter === ClipboardItemType.Image ? OpenBoardTheme.accent : OpenBoardTheme.secondaryText)
          }
          .padding({ left: 12, right: 16, top: 8, bottom: 8 })
          .backgroundColor(this.clipboardFilter === ClipboardItemType.Image ? OpenBoardTheme.surface : OpenBoardTheme.background)
          .borderRadius(OpenBoardTheme.radiusMedium)
          .shadow(this.clipboardFilter === ClipboardItemType.Image ? OpenBoardTheme.shadowKey : { radius: 0 })
          .onClick(() => { this.clipboardFilter = ClipboardItemType.Image; })

          // Links
          Row({ space: 4 }) {
            Text('🔗')
              .fontSize(14)
            Text('Links')
              .fontSize(14)
              .fontWeight(500)
              .fontColor(this.clipboardFilter === ClipboardItemType.Link ? OpenBoardTheme.accent : OpenBoardTheme.secondaryText)
          }
          .padding({ left: 12, right: 16, top: 8, bottom: 8 })
          .backgroundColor(this.clipboardFilter === ClipboardItemType.Link ? OpenBoardTheme.surface : OpenBoardTheme.background)
          .borderRadius(OpenBoardTheme.radiusMedium)
          .shadow(this.clipboardFilter === ClipboardItemType.Link ? OpenBoardTheme.shadowKey : { radius: 0 })
          .onClick(() => { this.clipboardFilter = ClipboardItemType.Link; })

          // Files
          Row({ space: 4 }) {
            Text('📁')
              .fontSize(14)
            Text('Files')
              .fontSize(14)
              .fontWeight(500)
              .fontColor(this.clipboardFilter === ClipboardItemType.File ? OpenBoardTheme.accent : OpenBoardTheme.secondaryText)
          }
          .padding({ left: 12, right: 16, top: 8, bottom: 8 })
          .backgroundColor(this.clipboardFilter === ClipboardItemType.File ? OpenBoardTheme.surface : OpenBoardTheme.background)
          .borderRadius(OpenBoardTheme.radiusMedium)
          .shadow(this.clipboardFilter === ClipboardItemType.File ? OpenBoardTheme.shadowKey : { radius: 0 })
          .onClick(() => { this.clipboardFilter = ClipboardItemType.File; })

          // Emojis
          Row({ space: 4 }) {
            Text('😃')
              .fontSize(14)
            Text('Emojis')
              .fontSize(14)
              .fontWeight(500)
              .fontColor(this.clipboardFilter === ClipboardItemType.Emoji ? OpenBoardTheme.accent : OpenBoardTheme.secondaryText)
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
                .accessibilityText(this.getClipboardTypeDescription(item.type))

              // Item content
              Column({ space: 4 }) {
                Text(item.text)
                  .fontSize(14)
                  .fontWeight(400)
                  .fontColor(OpenBoardTheme.primaryText)
                  .maxLines(2)
                  .textOverflow({ overflow: TextOverflow.Ellipsis })
                  .accessibilityText(`Clipboard item: ${item.text}`)

                // Timestamp
                Text(this.formatTimestamp(item.timestamp))
                  .fontSize(11)
                  .fontColor(OpenBoardTheme.secondaryText)
                  .accessibilityText(`Copied ${this.formatTimestamp(item.timestamp)}`)
              }
              .alignItems(HorizontalAlign.Start)
              .layoutWeight(1)
              .accessibilityGroup(true)  // Group content for accessibility
              .onClick(() => {
                this.pasteClipboardItem(item);
              })

              // Pin button
              Column() {
                Text(item.isPinned ? '📌' : '📍')
                  .fontSize(16)
                  .accessibilityText(item.isPinned ? 'Unpin clipboard item' : 'Pin clipboard item')
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
                  .accessibilityText('Delete clipboard item')
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
            .accessibilityGroup(true)  // Group entire item for accessibility
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

  // Helper method to generate accessibility text for keys
  private getKeyAccessibilityText(key: KeyData): string {
    switch (key.action) {
      case KeyAction.Char:
        return key.mainValue;
      case KeyAction.Space:
        return 'Space';
      case KeyAction.Enter:
        return this.enterKeyLabel;
      case KeyAction.Backspace:
        return 'Backspace';
      case KeyAction.Shift:
        return this.shiftState !== ShiftState.Off ? 'Shift locked' : 'Shift';
      case KeyAction.SwitchAlphabet:
        return 'Switch to symbols';
      case KeyAction.SwitchSymbols:
        return 'Switch to letters';
      case KeyAction.SwitchSymbolsAlt:
        return 'Switch to more symbols';
      case KeyAction.EMOJI_PANEL:
        return 'Emoji';
      default:
        return key.mainValue;
    }
  }

  // Helper method to describe clipboard types for accessibility
  private getClipboardTypeDescription(type: ClipboardItemType): string {
    switch (type) {
      case ClipboardItemType.URL:
        return 'Link';
      case ClipboardItemType.Email:
        return 'Email address';
      case ClipboardItemType.Phone:
        return 'Phone number';
      case ClipboardItemType.Text:
      default:
        return 'Text';
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
          .accessibilityText('Return to keyboard')
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
          .accessibilityText('Search clipboard items')
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
          .accessibilityText('Clear all clipboard items')
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
      .backgroundColor(OpenBoardTheme.keyboardBackground)
      .animation({ duration: 200 })
      .accessibilityText('Background layer')  // Accessibility text for background

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
        .backgroundColor(OpenBoardTheme.keyboardBackground)
        .borderRadius(OpenBoardTheme.radiusMedium)
        .shadow(OpenBoardTheme.shadowPanel)
        .accessibilityText('Input method toolbar')  // Accessibility text for toolbar

        // MAIN CONTENT
        if (this.showClipboardPanel) {
          // Clipboard Panel
          this.buildClipboardPanel()
        } else if (!this.showEmojiPanel) {
          // Main keyboard
          Stack() {
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
                        onKeyPress: (k: KeyData): Promise<void> => this.handleKeyPress(k),
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
                        onCursorModeEnd: (): void => this.handleCursorModeEnd(),
                        onSwipeStart: key.action === KeyAction.Char
                          ? (k: KeyData, p: Position): void => this.handleSwipeStart(k, p)
                          : undefined,
                        onSwipeMove: key.action === KeyAction.Char
                          ? (p: Position): void => this.handleSwipeMove(p)
                          : undefined,
                        onKeyBoundsUpdate: (k: string, b: KeyBounds): void => this.handleKeyBoundsUpdate(k, b),
                        keyboardRootOffset: this.keyboardRootOffset,
                        swipeInProgress: this.isSwipeActive,
                        ignoreEventsUntil: this.ignoreKeysUntilTs
                      })
                        .layoutWeight(key.flex)
                        .accessibilityText(this.getKeyAccessibilityText(key))  // Add accessibility text for the key
                    }
                  })
                }
                .width('100%')
                .layoutWeight(1)
                .accessibilityGroup(true)  // Group keys in each row for accessibility
              })
            }
            .width('100%')
            .height('100%')
            .accessibilityText('Keyboard layout')  // Accessibility text for keyboard area

            Flex()
              .width('100%')
              .height('100%')
              .backgroundColor('transparent')
              .hitTestBehavior(HitTestMode.Transparent)
              .accessibilityText('Swipe gesture area')  // Accessibility text for swipe area
              .onTouch((event) => {
                const touch = event.touches[0];
                if (!touch) return;
                const currentPosition: Position = { x: touch.windowX, y: touch.windowY };
                if (Date.now() < this.ignoreKeysUntilTs) return;

                if (event.type === TouchType.Move && this.isSwipeActive) {
                  this.handleSwipeMove(currentPosition);
                }

                if ((event.type === TouchType.Up || event.type === TouchType.Cancel) && this.isSwipeActive) {
                  this.handleSwipeEnd(event.type === TouchType.Up);
                }
              })
          }
          .layoutWeight(1)
          .padding({ left: 2, right: 2, bottom: 2 })
          .onAreaChange((_oldValue: Area, newValue: Area) => {
            const areaAny = newValue as AreaWithGlobalPosition;
            const globalPosition = areaAny.globalPosition;
            const basePosition = globalPosition ?? {
              x: newValue.position.x as number,
              y: newValue.position.y as number
            };
            this.keyboardRootOffset = { x: basePosition.x, y: basePosition.y };
          })
        } else {
          // Emoji Panel (iOS-style horizontal swipe)
          Column() {
            // Emoji Grid with horizontal swiper between categories
            Swiper(this.emojiSwiperController) {
              ForEach(EMOJI_CATEGORIES, (cat: string, catIndex: number) => {
                // Each category is a scrollable page
                Scroll() {
                  Flex({ wrap: FlexWrap.Wrap, justifyContent: FlexAlign.Start, alignItems: ItemAlign.Start }) {
                    ForEach(this.getEmojiList(cat), (emoji: string, emojiIndex: number) => {
                      Text(emoji)
                        .fontSize(24)
                        .textAlign(TextAlign.Center)
                        .width('11.1%')  // 9 per row (smaller emojis = more per row)
                        .height(36)
                        .accessibilityText(`Insert ${emoji}`)
                        .onClick(async () => {
                          keyboardController.insertText(emoji);
                          this.addRecentEmoji(emoji);
                          await this.onBoundaryTyped('');
                          this.resetInactivityTimer();
                        })
                    }, (emoji: string, index: number) => `${cat}_${emoji}_${index}`)
                  }
                  .width('100%')
                  .padding({ left: 4, right: 4, top: 4 })
                }
                .scrollable(ScrollDirection.Vertical)
                .scrollBar(BarState.Off)
              }, (cat: string) => cat)
            }
            .index(this.emojiSwiperIndex)
            .indicator(false)  // No dots indicator
            .loop(false)
            .cachedCount(2)
            .layoutWeight(1)
            .onChange((index: number) => {
              this.emojiSwiperIndex = index;
              this.emojiCategory = EMOJI_CATEGORIES[index] || 'Smileys & Emotion';
            })
            .accessibilityText('Emoji selection - swipe left/right to change category')

            // Category bar at bottom (iOS-style icons)
            Row() {
              ForEach(EMOJI_CATEGORIES, (cat: string, catIndex: number) => {
                Column() {
                  Text(EMOJI_CATEGORY_ICONS[cat] || '😀')
                    .fontSize(20)
                    .opacity(this.emojiSwiperIndex === catIndex ? 1.0 : 0.4)
                }
                .layoutWeight(1)
                .height(36)
                .justifyContent(FlexAlign.Center)
                .backgroundColor(this.emojiSwiperIndex === catIndex ? OpenBoardTheme.surface : 'transparent')
                .borderRadius(6)
                .onClick(() => {
                  this.emojiSwiperIndex = catIndex;
                  this.emojiCategory = cat;
                  this.emojiSwiperController.changeIndex(catIndex, true);
                })
              }, (cat: string) => `cat_${cat}`)
            }
            .width('100%')
            .height(40)
            .padding({ left: 2, right: 2, bottom: 2 })
            .backgroundColor(OpenBoardTheme.keyboardBackground)
            .accessibilityText('Emoji categories')
          }
          .layoutWeight(1)
          .accessibilityText('Emoji panel')
        }
      }
      .alignSelf(ItemAlign.End)
      .backgroundColor(OpenBoardTheme.keyboardBackground)

      // Popups
      if (this.longPressInfo) {
        KeyPopup({ info: this.longPressInfo, hoveredChar: this.hoveredChar, keyWidth: this.popupKeyWidth })
          .position({ x: this.popupPosition.x, y: this.popupPosition.y })
          .zIndex(200)
          .accessibilityText('Character alternatives')  // Accessibility text for key popup
      }

      if (this.singleCharPopupInfo) {
        SingleKeyPopup({ char: this.singleCharPopupInfo.char })
          .position({ x: this.singleCharPopupInfo.position.x - 25, y: this.singleCharPopupInfo.position.y - 70 })
          .zIndex(150)
          .accessibilityText(`Preview character: ${this.singleCharPopupInfo.char}`)  // Accessibility text for single char popup
      }

      // Voice interruption popup
      if (this.showInterruptionPopup) {
        VoiceInterruptionPopup({
          message: this.interruptionMessage,
          popupPosition: this.interruptionPopupPosition
        })
          .zIndex(300)
          .accessibilityText(this.interruptionMessage)  // Accessibility text for interruption popup
      }

      // Swipe typing trail visualization (iOS-style: only trail, no text)
      if (this.isSwipeActive && this.swipeTrailPoints.length > 1) {
        Canvas(this.trailCanvasContext)
          .width('100%')
          .height('100%')
          .accessibilityText('Swipe gesture trail')  // Accessibility text for swipe trail
          .onReady(() => {
            this.renderTrailPath();
          })
          .zIndex(350)
      }
    }
    .width('100%')
    .height('100%')
  }

  getCurrentLayout(): KeyData[][] {
    // Create cache key based on current layout, language, and shift state
    const cacheKey = `${this.keyboardLayout}-${this.currentLanguage}-${this.shiftState}`;

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
          case 'en': return insertEmojiKey(englishLayout);
          case 'it': return insertEmojiKey(italianLayout);
          case 'pl': return insertEmojiKey(polishLayout);
          case 'ru': return insertEmojiKey(russianLayout);
          default: return insertEmojiKey(englishLayout);
        }
      case KeyboardLayout.Symbols1: return symbolsLayout1;
      case KeyboardLayout.Symbols2: return symbolsLayout2;
      default: return insertEmojiKey(englishLayout);
    }
  }

  // Add emoji to recents (iOS-style)
  private addRecentEmoji(emoji: string): void {
    // Remove if already exists
    const index = this.recentEmojis.indexOf(emoji);
    if (index > -1) {
      this.recentEmojis.splice(index, 1);
    }
    // Add to beginning
    this.recentEmojis.unshift(emoji);
    // Limit size
    if (this.recentEmojis.length > this.MAX_RECENT_EMOJIS) {
      this.recentEmojis = this.recentEmojis.slice(0, this.MAX_RECENT_EMOJIS);
    }
  }

  private getEmojiList(category: string): string[] {
    switch (category) {
      case 'Recents':
        return this.recentEmojis.length > 0 ? this.recentEmojis : ['😀', '😂', '❤️', '👍', '🙏', '😊', '🥰', '😍'];  // Default recents
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
