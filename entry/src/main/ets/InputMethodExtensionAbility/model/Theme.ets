// File location: entry/src/main/ets/InputMethodExtensionAbility/model/Theme.ets
// OpenBoard Theme using custom theme colors

interface ShadowStyle {
  radius: number;
  color: Resource;
  offsetX: number;
  offsetY: number;
}

export class OpenBoardTheme {
  // Main backgrounds - using custom theme colors
  static readonly background = $r('app.color.theme_colors_backgroundPrimary');
  static readonly backgroundDark = $r('app.color.theme_colors_backgroundSecondary');
  static readonly backgroundDarker = $r('app.color.theme_colors_backgroundTertiary');
  static readonly surface = $r('app.color.theme_colors_backgroundPrimary');

  // Transparent backgrounds - using HarmonyOS comp tokens
  static readonly keyboardBackground = $r('app.color.keyboard_background_transparent');
  static readonly keyboardBackgroundSemi = $r('app.color.theme_colors_compBackgroundSecondary');
  static readonly keyboardBackgroundLight = $r('app.color.theme_colors_compBackgroundTertiary');

  // Accent and text colors - using custom theme colors
  static readonly accent = $r('app.color.theme_colors_brand');

  // Text colors - using custom theme colors
  static readonly primaryText = $r('app.color.theme_colors_fontPrimary');
  static readonly secondaryText = $r('app.color.theme_colors_fontSecondary');

  // Keyboard specific colors
  static readonly specialKey = $r('app.color.keyboard_special_key');
  static readonly regularKey = $r('app.color.keyboard_regular_key');
  static readonly divider = $r('app.color.theme_colors_compDivider');

  // Border radius - Slightly rounded, almost angular
  static readonly radiusSmall = 4; // Small buttons (toolbar icons)
  static readonly radiusMedium = 6; // Medium elements (keys, cards)
  static readonly radiusLarge = 8; // Large panels and containers

  // Shadows - Using custom theme colors for divider/shadow effects
  static readonly shadowKey: ShadowStyle = { 
    radius: 4, 
    color: $r('app.color.theme_colors_compDivider'), 
    offsetX: 0, 
    offsetY: 2 
  };
  static readonly shadowPressed: ShadowStyle = { 
    radius: 8, 
    color: $r('app.color.theme_colors_compDivider'), 
    offsetX: 0, 
    offsetY: 4 
  };
  static readonly shadowPanel: ShadowStyle = { 
    radius: 12, 
    color: $r('app.color.theme_colors_compDivider'), 
    offsetX: 0, 
    offsetY: 4 
  };
}