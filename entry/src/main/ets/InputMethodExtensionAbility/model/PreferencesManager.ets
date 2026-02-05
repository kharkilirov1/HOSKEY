/**
 * PreferencesManager - Persistent keyboard settings storage
 *
 * Uses @ohos.data.preferences for HarmonyOS persistent storage.
 * Manages: enabled languages, key padding, key height, toolbar height.
 */

import { preferences } from '@kit.ArkData';
import { common } from '@kit.AbilityKit';

const PREFERENCES_NAME = 'hoskey_settings';

// All available languages in the keyboard
export const ALL_LANGUAGES: string[] = ['en', 'ru', 'uk', 'kk', 'de', 'fr', 'es', 'it', 'pl'];

// Default settings - will be set based on system language
const DEFAULT_ENABLED_LANGUAGES: string[] = ['ru', 'en'];
const DEFAULT_KEYBOARD_HEIGHT: number = 260; // Total keyboard height in pixels

// Settings keys
const KEY_ENABLED_LANGUAGES = 'enabled_languages';
const KEY_KEYBOARD_HEIGHT = 'keyboard_height';

export interface KeyboardSettings {
  enabledLanguages: string[];
  keyboardHeight: number;
}

// Helper to copy array without spread operator (ArkTS compatibility)
function copyStringArray(arr: string[]): string[] {
  const result: string[] = [];
  for (let i = 0; i < arr.length; i++) {
    result.push(arr[i]);
  }
  return result;
}

class PreferencesManager {
  private prefs: preferences.Preferences | null = null;
  private context: common.Context | null = null;
  private isInitialized: boolean = false;

  // Cached settings
  private cachedSettings: KeyboardSettings = {
    enabledLanguages: copyStringArray(DEFAULT_ENABLED_LANGUAGES),
    keyboardHeight: DEFAULT_KEYBOARD_HEIGHT
  };

  /**
   * Set context (called from InputMethodService.onCreate)
   */
  setContext(ctx: common.Context): void {
    this.context = ctx;
    // Initialize preferences asynchronously
    this.initializeAsync();
  }

  /**
   * Initialize the preferences manager asynchronously
   */
  private async initializeAsync(): Promise<void> {
    if (this.isInitialized || !this.context) {
      return;
    }

    try {
      this.prefs = await preferences.getPreferences(this.context, PREFERENCES_NAME);
      await this.loadSettings();
      this.isInitialized = true;
      console.info('[HOSKEY PreferencesManager] Initialized successfully');
    } catch (error) {
      console.error('[HOSKEY PreferencesManager] Failed to initialize:', error);
      // Use defaults on error
    }
  }

  /**
   * Initialize the preferences manager with context (legacy method, kept for compatibility)
   * @deprecated Use setContext instead
   */
  async initialize(context: common.Context): Promise<void> {
    if (this.isInitialized) {
      return;
    }

    try {
      this.context = context;
      this.prefs = await preferences.getPreferences(context, PREFERENCES_NAME);
      await this.loadSettings();
      this.isInitialized = true;
      console.info('[HOSKEY PreferencesManager] Initialized successfully');
    } catch (error) {
      console.error('[HOSKEY PreferencesManager] Failed to initialize:', error);
      // Use defaults on error
    }
  }

  /**
   * Load all settings from storage
   */
  private async loadSettings(): Promise<void> {
    if (!this.prefs) {
      return;
    }

    try {
      // Load enabled languages
      const langJson = await this.prefs.get(KEY_ENABLED_LANGUAGES, JSON.stringify(DEFAULT_ENABLED_LANGUAGES)) as string;
      const languages = JSON.parse(langJson) as string[];
      // Filter to only valid languages
      this.cachedSettings.enabledLanguages = languages.filter(l => ALL_LANGUAGES.includes(l));
      if (this.cachedSettings.enabledLanguages.length === 0) {
        this.cachedSettings.enabledLanguages = copyStringArray(DEFAULT_ENABLED_LANGUAGES);
      }

      // Load keyboard height
      this.cachedSettings.keyboardHeight = await this.prefs.get(KEY_KEYBOARD_HEIGHT, DEFAULT_KEYBOARD_HEIGHT) as number;

      console.info('[HOSKEY PreferencesManager] Settings loaded:', JSON.stringify(this.cachedSettings));
    } catch (error) {
      console.error('[HOSKEY PreferencesManager] Failed to load settings:', error);
    }
  }

  /**
   * Get current settings (cached)
   */
  getSettings(): KeyboardSettings {
    return {
      enabledLanguages: copyStringArray(this.cachedSettings.enabledLanguages),
      keyboardHeight: this.cachedSettings.keyboardHeight
    };
  }

  /**
   * Get enabled languages
   */
  getEnabledLanguages(): string[] {
    return copyStringArray(this.cachedSettings.enabledLanguages);
  }

  /**
   * Set enabled languages
   */
  async setEnabledLanguages(languages: string[]): Promise<void> {
    // Filter to valid languages and ensure at least one
    const valid = languages.filter(l => ALL_LANGUAGES.includes(l));
    if (valid.length === 0) {
      console.warn('[HOSKEY PreferencesManager] Cannot disable all languages');
      return;
    }

    this.cachedSettings.enabledLanguages = valid;

    if (this.prefs) {
      try {
        await this.prefs.put(KEY_ENABLED_LANGUAGES, JSON.stringify(valid));
        await this.prefs.flush();
        console.info('[HOSKEY PreferencesManager] Enabled languages saved:', valid);
      } catch (error) {
        console.error('[HOSKEY PreferencesManager] Failed to save languages:', error);
      }
    }
  }

  /**
   * Toggle a single language on/off
   */
  async toggleLanguage(lang: string): Promise<boolean> {
    if (!ALL_LANGUAGES.includes(lang)) {
      return false;
    }

    const current = copyStringArray(this.cachedSettings.enabledLanguages);
    const index = current.indexOf(lang);

    if (index >= 0) {
      // Trying to disable - check if it's the last one
      if (current.length === 1) {
        console.warn('[HOSKEY PreferencesManager] Cannot disable the last language');
        return false;
      }
      current.splice(index, 1);
    } else {
      // Enable
      current.push(lang);
    }

    await this.setEnabledLanguages(current);
    return true;
  }

  /**
   * Check if a language is enabled
   */
  isLanguageEnabled(lang: string): boolean {
    return this.cachedSettings.enabledLanguages.includes(lang);
  }

  /**
   * Get keyboard height
   */
  getKeyboardHeight(): number {
    return this.cachedSettings.keyboardHeight;
  }

  /**
   * Set keyboard height (200-350)
   */
  async setKeyboardHeight(height: number): Promise<void> {
    const clamped = Math.max(200, Math.min(350, height));
    this.cachedSettings.keyboardHeight = clamped;

    if (this.prefs) {
      try {
        await this.prefs.put(KEY_KEYBOARD_HEIGHT, clamped);
        await this.prefs.flush();
      } catch (error) {
        console.error('[HOSKEY PreferencesManager] Failed to save keyboard height:', error);
      }
    }
  }

  /**
   * Reset all settings to defaults
   */
  async resetToDefaults(): Promise<void> {
    this.cachedSettings = {
      enabledLanguages: copyStringArray(DEFAULT_ENABLED_LANGUAGES),
      keyboardHeight: DEFAULT_KEYBOARD_HEIGHT
    };

    if (this.prefs) {
      try {
        await this.prefs.clear();
        await this.prefs.flush();
        console.info('[HOSKEY PreferencesManager] Settings reset to defaults');
      } catch (error) {
        console.error('[HOSKEY PreferencesManager] Failed to reset settings:', error);
      }
    }
  }
}

// Singleton instance
export const preferencesManager = new PreferencesManager();
