// ThemeManager - Dynamic keyboard themes (Yandex style)

import { preferencesManager } from './PreferencesManager';

// Theme color structure
export interface ThemeColor {
  r: number;
  g: number;
  b: number;
  a?: number;
}

// Full theme definition
export interface KeyboardTheme {
  id: string;
  name: string;
  category: string;
  
  // Main colors
  bg: ThemeColor;           // Background
  text: ThemeColor;         // Primary text
  textSecondary: ThemeColor; // Secondary text
  accent: ThemeColor;       // Accent color
  swipe: ThemeColor;        // Swipe trail
  
  // Key colors
  keyBg: ThemeColor;        // Regular key background
  keyBgFunc: ThemeColor;    // Function key background (shift, backspace)
  keyBgPressed: ThemeColor; // Pressed key
  
  // Options
  isKeysLightBg: boolean;
  isKeysShadowEnabled: boolean;
  keyBackgroundOpacity: number;
  
  // Preview
  previewColors: string[];  // For theme grid preview
}

// Convert ThemeColor to hex string
export function colorToHex(color: ThemeColor): string {
  const r = color.r.toString(16).padStart(2, '0');
  const g = color.g.toString(16).padStart(2, '0');
  const b = color.b.toString(16).padStart(2, '0');
  if (color.a !== undefined && color.a < 1) {
    const a = Math.round(color.a * 255).toString(16).padStart(2, '0');
    return `#${a}${r}${g}${b}`;
  }
  return `#${r}${g}${b}`;
}

// Built-in themes
const THEMES: KeyboardTheme[] = [
  // Light theme (Yandex Default Day)
  {
    id: 'light',
    name: 'Светлая',
    category: 'default',
    bg: { r: 235, g: 238, b: 240 },
    text: { r: 0, g: 0, b: 0 },
    textSecondary: { r: 153, g: 153, b: 153 },
    accent: { r: 78, g: 102, b: 255 },
    swipe: { r: 78, g: 102, b: 255 },
    keyBg: { r: 255, g: 255, b: 255 },
    keyBgFunc: { r: 172, g: 179, b: 190 },
    keyBgPressed: { r: 200, g: 200, b: 200 },
    isKeysLightBg: true,
    isKeysShadowEnabled: true,
    keyBackgroundOpacity: 1.0,
    previewColors: ['#EBEEF0', '#FFFFFF', '#4E66FF']
  },
  
  // Dark theme (Yandex Default Night)
  {
    id: 'dark',
    name: 'Тёмная',
    category: 'default',
    bg: { r: 18, g: 18, b: 18 },
    text: { r: 231, g: 231, b: 231 },
    textSecondary: { r: 128, g: 128, b: 128 },
    accent: { r: 78, g: 102, b: 255 },
    swipe: { r: 78, g: 102, b: 255 },
    keyBg: { r: 45, g: 45, b: 45 },
    keyBgFunc: { r: 60, g: 60, b: 60 },
    keyBgPressed: { r: 80, g: 80, b: 80 },
    isKeysLightBg: false,
    isKeysShadowEnabled: true,
    keyBackgroundOpacity: 1.0,
    previewColors: ['#121212', '#2D2D2D', '#4E66FF']
  },
  
  // AMOLED Dark
  {
    id: 'amoled',
    name: 'AMOLED',
    category: 'dark',
    bg: { r: 0, g: 0, b: 0 },
    text: { r: 255, g: 255, b: 255 },
    textSecondary: { r: 128, g: 128, b: 128 },
    accent: { r: 78, g: 102, b: 255 },
    swipe: { r: 78, g: 102, b: 255 },
    keyBg: { r: 28, g: 28, b: 28 },
    keyBgFunc: { r: 45, g: 45, b: 45 },
    keyBgPressed: { r: 60, g: 60, b: 60 },
    isKeysLightBg: false,
    isKeysShadowEnabled: false,
    keyBackgroundOpacity: 1.0,
    previewColors: ['#000000', '#1C1C1C', '#4E66FF']
  },
  
  // Blue accent
  {
    id: 'ocean',
    name: 'Океан',
    category: 'color',
    bg: { r: 20, g: 30, b: 48 },
    text: { r: 220, g: 230, b: 255 },
    textSecondary: { r: 128, g: 150, b: 180 },
    accent: { r: 64, g: 156, b: 255 },
    swipe: { r: 64, g: 156, b: 255 },
    keyBg: { r: 35, g: 50, b: 75 },
    keyBgFunc: { r: 50, g: 70, b: 100 },
    keyBgPressed: { r: 60, g: 90, b: 130 },
    isKeysLightBg: false,
    isKeysShadowEnabled: true,
    keyBackgroundOpacity: 1.0,
    previewColors: ['#141E30', '#23324B', '#409CFF']
  },
  
  // Purple/Pink gradient feel
  {
    id: 'neon',
    name: 'Неон',
    category: 'color',
    bg: { r: 25, g: 15, b: 35 },
    text: { r: 255, g: 220, b: 255 },
    textSecondary: { r: 180, g: 140, b: 180 },
    accent: { r: 255, g: 64, b: 255 },
    swipe: { r: 255, g: 64, b: 255 },
    keyBg: { r: 50, g: 30, b: 60 },
    keyBgFunc: { r: 70, g: 45, b: 85 },
    keyBgPressed: { r: 100, g: 60, b: 120 },
    isKeysLightBg: false,
    isKeysShadowEnabled: true,
    keyBackgroundOpacity: 1.0,
    previewColors: ['#190F23', '#321E3C', '#FF40FF']
  },
  
  // Mint/Green
  {
    id: 'mint',
    name: 'Мята',
    category: 'color',
    bg: { r: 235, g: 250, b: 245 },
    text: { r: 20, g: 60, b: 50 },
    textSecondary: { r: 100, g: 140, b: 130 },
    accent: { r: 0, g: 200, b: 150 },
    swipe: { r: 0, g: 200, b: 150 },
    keyBg: { r: 255, g: 255, b: 255 },
    keyBgFunc: { r: 200, g: 230, b: 220 },
    keyBgPressed: { r: 180, g: 220, b: 210 },
    isKeysLightBg: true,
    isKeysShadowEnabled: true,
    keyBackgroundOpacity: 1.0,
    previewColors: ['#EBFAF5', '#FFFFFF', '#00C896']
  },
  
  // Warm/Orange
  {
    id: 'sunset',
    name: 'Закат',
    category: 'color',
    bg: { r: 45, g: 25, b: 20 },
    text: { r: 255, g: 230, b: 200 },
    textSecondary: { r: 180, g: 140, b: 120 },
    accent: { r: 255, g: 120, b: 50 },
    swipe: { r: 255, g: 120, b: 50 },
    keyBg: { r: 70, g: 40, b: 30 },
    keyBgFunc: { r: 90, g: 55, b: 40 },
    keyBgPressed: { r: 120, g: 70, b: 50 },
    isKeysLightBg: false,
    isKeysShadowEnabled: true,
    keyBackgroundOpacity: 1.0,
    previewColors: ['#2D1914', '#46281E', '#FF7832']
  }
];

class ThemeManager {
  private static instance: ThemeManager;
  private currentThemeId: string = 'light';
  private systemDarkMode: boolean = false;
  private useSystemTheme: boolean = true;

  private constructor() {}

  static getInstance(): ThemeManager {
    if (!ThemeManager.instance) {
      ThemeManager.instance = new ThemeManager();
    }
    return ThemeManager.instance;
  }

  // Get all available themes
  getThemes(): KeyboardTheme[] {
    return THEMES;
  }

  // Get themes by category
  getThemesByCategory(category: string): KeyboardTheme[] {
    return THEMES.filter(t => t.category === category);
  }

  // Get theme categories
  getCategories(): string[] {
    const cats = new Set<string>();
    THEMES.forEach(t => cats.add(t.category));
    return Array.from(cats);
  }

  // Get theme by ID
  getTheme(id: string): KeyboardTheme | undefined {
    return THEMES.find(t => t.id === id);
  }

  // Get current theme
  getCurrentTheme(): KeyboardTheme {
    if (this.useSystemTheme) {
      return this.systemDarkMode ? 
        (this.getTheme('dark') || THEMES[1]) : 
        (this.getTheme('light') || THEMES[0]);
    }
    return this.getTheme(this.currentThemeId) || THEMES[0];
  }

  // Set current theme
  setTheme(id: string): void {
    if (this.getTheme(id)) {
      this.currentThemeId = id;
      this.useSystemTheme = false;
      // TODO: Save to preferences
    }
  }

  // Use system theme (auto day/night)
  setUseSystemTheme(use: boolean): void {
    this.useSystemTheme = use;
    // TODO: Save to preferences
  }

  // Update system dark mode status
  setSystemDarkMode(isDark: boolean): void {
    this.systemDarkMode = isDark;
  }

  // Check if using system theme
  isUsingSystemTheme(): boolean {
    return this.useSystemTheme;
  }

  // Get current theme ID (or 'system' if auto)
  getCurrentThemeId(): string {
    return this.useSystemTheme ? 'system' : this.currentThemeId;
  }
}

export const themeManager = ThemeManager.getInstance();
export default themeManager;
