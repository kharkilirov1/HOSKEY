// File location: entry/src/main/ets/InputMethodExtensionAbility/model/Theme.ets
// OpenBoard Theme using HarmonyOS system color tokens
// These colors automatically adapt to system light/dark mode

interface ShadowStyle {
  radius: number;
  color: string;
  offsetX: number;
  offsetY: number;
}

export class OpenBoardTheme {
  // Main backgrounds - using Celia's exact background color
  static readonly background = '#242424'; // Celia keyboard background
  static readonly backgroundDark = '#242424';
  static readonly backgroundDarker = '#242424';
  static readonly surface = '#242424'; // toolbar uses same color as keyboard

  // Accent color - using system emphasize color for consistency
  static readonly accent = $r('sys.color.ohos_id_color_emphasize');

  // Text colors - adapt to system theme
  static readonly primaryText = $r('sys.color.ohos_id_color_text_primary');
  static readonly secondaryText = $r('sys.color.ohos_id_color_text_secondary');

  // Key colors - special keys darker, regular keys same as before
  static readonly specialKey = '#3D3D3D'; // Shift, Enter, Backspace
  static readonly regularKey = $r('sys.color.ohos_id_color_tertiary');
  static readonly divider = $r('sys.color.ohos_id_color_palette_aux1');

  // Border radius - HarmonyOS design guidelines
  static readonly radiusSmall = 8; // Small buttons (toolbar icons)
  static readonly radiusMedium = 12; // Medium elements (keys, cards)
  static readonly radiusLarge = 16; // Large panels and containers

  // Shadows - HarmonyOS elevation system
  static readonly shadowKey: ShadowStyle = { radius: 4, color: '#30000000', offsetX: 0, offsetY: 2 }; // Keys
  static readonly shadowPressed: ShadowStyle = { radius: 8, color: '#50000000', offsetX: 0, offsetY: 4 }; // Pressed state
  static readonly shadowPanel: ShadowStyle = { radius: 12, color: '#40000000', offsetX: 0, offsetY: 4 }; // Panels/toolbars
}
