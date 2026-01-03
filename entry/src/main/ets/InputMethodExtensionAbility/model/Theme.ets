// File location: entry/src/main/ets/InputMethodExtensionAbility/model/Theme.ets
// OpenBoard Theme using HarmonyOS system color tokens
// These colors automatically adapt to system light/dark mode
export class OpenBoardTheme {
  // Main backgrounds - adapt to system theme
  static readonly background = $r('sys.color.ohos_id_color_background');
  static readonly backgroundDark = $r('sys.color.ohos_id_color_sub_background');
  static readonly backgroundDarker = $r('sys.color.ohos_id_color_sub_background');
  static readonly surface = $r('sys.color.ohos_id_color_sub_background');

  // Accent color - using system emphasize color for consistency
  static readonly accent = $r('sys.color.ohos_id_color_emphasize');

  // Text colors - adapt to system theme
  static readonly primaryText = $r('sys.color.ohos_id_color_text_primary');
  static readonly secondaryText = $r('sys.color.ohos_id_color_text_secondary');

  // Key colors - all keys use same color for consistency (like Celia)
  static readonly specialKey = $r('sys.color.ohos_id_color_tertiary');
  static readonly regularKey = $r('sys.color.ohos_id_color_tertiary');
  static readonly divider = $r('sys.color.ohos_id_color_palette_aux1');
}
