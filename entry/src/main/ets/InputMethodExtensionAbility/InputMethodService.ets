import { Want } from '@kit.AbilityKit';
import keyboardController from './model/KeyboardController';
import clipboardHistory from './model/ClipboardHistory';
import nativeDictionary from './model/NativeDictionary';
import { preferencesManager } from './model/PreferencesManager';
import { InputMethodExtensionAbility } from '@kit.IMEKit';

// Debug flag - set to true to run self-tests on startup
const DEBUG_SELF_TEST = false;

export default class HoskeyInputMethod extends InputMethodExtensionAbility {
  // Dictionary ready state tracking
  private isDictionaryReady: boolean = false;
  private pendingOperations: Array<() => void> = [];

  /**
   * Check if dictionary is loaded and ready for suggestions
   */
  public isDictionaryLoaded(): boolean {
    return this.isDictionaryReady;
  }

  /**
   * Execute callback when dictionary is ready, or immediately if already ready
   * Used for deferred operations that require dictionary
   */
  public whenDictionaryReady(callback: () => void): void {
    if (this.isDictionaryReady) {
      callback();
    } else {
      this.pendingOperations.push(callback);
    }
  }

  /**
   * Mark dictionary as ready and execute all pending operations
   */
  private markDictionaryReady(): void {
    console.info('[HOSKEY IME] Dictionary marked as ready');
    this.isDictionaryReady = true;
    
    // Execute all pending operations
    const pending = this.pendingOperations;
    this.pendingOperations = [];
    pending.forEach(op => {
      try {
        op();
      } catch (err) {
        console.error('[HOSKEY IME] Error executing pending operation:', err);
      }
    });
    console.info(`[HOSKEY IME] Executed ${pending.length} pending operations`);
  }

  onCreate(want: Want): void {
    console.info('[HOSKEY IME] onCreate called, want action:', want.action ?? 'none');

    // Safe access to context info (may be undefined during early lifecycle)
    const bundleName = this.context?.extensionAbilityInfo?.bundleName ?? 'unknown';
    console.info('[HOSKEY IME] Context bundleName:', bundleName);

    // Reset dictionary state on create
    this.isDictionaryReady = false;
    this.pendingOperations = [];

    keyboardController.onCreate(this.context);

    // Initialize PreferencesManager for settings persistence
    preferencesManager.setContext(this.context);
    console.info('[HOSKEY IME] PreferencesManager initialized');

    // Get enabled languages from preferences, default to 'ru'
    const enabledLanguages = preferencesManager.getEnabledLanguages();
    const defaultLang = enabledLanguages.length > 0 ? enabledLanguages[0] : 'ru';
    console.info(`[HOSKEY IME] Enabled languages: ${enabledLanguages.join(', ')}, default: ${defaultLang}`);

    // Initialize NativeDictionary with context (but defer loading to avoid ANR)
    nativeDictionary.setContext(this.context);
    
    // DEFER dictionary loading to let UI render first (ANR fix)
    // Without this delay, copyDictFromRawfile blocks the main thread
    setTimeout(() => {
      console.info('[HOSKEY IME] Deferred dictionary loading starting...');
      
      // Load autocorrect rules (fast)
      nativeDictionary.loadAutocorrectRules(defaultLang);
      
      // Load dictionary (slow - now runs after UI is shown)
      nativeDictionary.initialize(defaultLang).then((loaded) => {
        if (loaded) {
          console.info(`[HOSKEY] NativeDictionary initialized for language: ${defaultLang}`);
        } else {
          console.warn(`[HOSKEY] NativeDictionary failed to load for language: ${defaultLang}`);
        }
        
        // Mark dictionary as ready and execute pending operations
        this.markDictionaryReady();
      }).catch((err: Error) => {
        console.error('[HOSKEY] Failed to initialize NativeDictionary', err);
        // Even on error, mark as "ready" to unblock pending operations
        this.markDictionaryReady();
      });
    }, 500);  // 500ms delay gives UI time to render

    // Initialize clipboard history
    clipboardHistory.setContext(this.context);
    clipboardHistory.load().then(() => {
      console.info('[HOSKEY] Clipboard history loaded successfully');
    }).catch((err: Error) => {
      console.error('[HOSKEY] Failed to initialize clipboard history', err);
    });

    // Run self-test in debug mode
    if (DEBUG_SELF_TEST) {
      nativeDictionary.runSelfTest().then((passed) => {
        console.info(`[HOSKEY IME] Self-test result: ${passed ? 'PASSED' : 'FAILED'}`);
      }).catch((err: Error) => {
        console.error('[HOSKEY IME] Self-test error:', err);
      });
    }
  }

  async onDestroy(): Promise<void> {
    console.info('[HOSKEY IME] onDestroy called');

    // Save user dictionary before destroying
    try {
      await nativeDictionary.saveUserDictionary();
      console.info('[HOSKEY] User dictionary saved successfully');
    } catch (err) {
      console.error('[HOSKEY] Failed to save user dictionary', err);
    }

    keyboardController.onDestroy();
  }
}