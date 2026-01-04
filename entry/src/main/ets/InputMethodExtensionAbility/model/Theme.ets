// File location: entry/src/main/ets/InputMethodExtensionAbility/model/Theme.ets
// OpenBoard Theme using HarmonyOS system color tokens
// These colors automatically adapt to system light/dark mode
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
}
