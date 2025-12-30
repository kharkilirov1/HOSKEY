export enum KeyAction {
  Char, Space, Backspace, Enter, Shift,
  SwitchSymbols, SwitchSymbolsAlt, SwitchAlphabet,
  Spacer, EMOJI_PANEL
}

export enum ShiftState { Off, On, CapsLock }
export enum KeyboardLayout { Alphabet, Symbols1, Symbols2 }

export interface KeyData {
  mainValue: string;
  shiftValue?: string;
  secondaryValue?: string;
  action: KeyAction;
  flex: number;
}

const SHIFT_KEY: KeyData = { mainValue: '⇧', action: KeyAction.Shift, flex: 1.5 };
const BACKSPACE_KEY: KeyData = { mainValue: '⌫', action: KeyAction.Backspace, flex: 1.5 };
const ENTER_KEY: KeyData = { mainValue: '↵', action: KeyAction.Enter, flex: 1.5 };
const SYMBOLS_KEY: KeyData = { mainValue: '?123', action: KeyAction.SwitchSymbols, flex: 1.2 };
const SPACE_KEY: KeyData = { mainValue: 'space', action: KeyAction.Space, flex: 6 };
const ALPHABET_KEY: KeyData = { mainValue: 'ABC', action: KeyAction.SwitchAlphabet, flex: 1.2 };
const SYMBOLS_ALT_KEY: KeyData = { mainValue: '=\\<', action: KeyAction.SwitchSymbolsAlt, flex: 1.2 };

const SPACER_LEFT_KEY: KeyData = { mainValue: 'spacer_left', action: KeyAction.Spacer, flex: 0.5 };
const SPACER_RIGHT_KEY: KeyData = { mainValue: 'spacer_right', action: KeyAction.Spacer, flex: 0.5 };

export const numberRow: KeyData[] = [
  { mainValue: '1', secondaryValue: '!', action: KeyAction.Char, flex: 1 },
  { mainValue: '2', secondaryValue: '@', action: KeyAction.Char, flex: 1 },
  { mainValue: '3', secondaryValue: '#', action: KeyAction.Char, flex: 1 },
  { mainValue: '4', secondaryValue: '$', action: KeyAction.Char, flex: 1 },
  { mainValue: '5', secondaryValue: '%', action: KeyAction.Char, flex: 1 },
  { mainValue: '6', secondaryValue: '^', action: KeyAction.Char, flex: 1 },
  { mainValue: '7', secondaryValue: '&', action: KeyAction.Char, flex: 1 },
  { mainValue: '8', secondaryValue: '*', action: KeyAction.Char, flex: 1 },
  { mainValue: '9', secondaryValue: '(', action: KeyAction.Char, flex: 1 },
  { mainValue: '0', secondaryValue: ')', action: KeyAction.Char, flex: 1 },
];

export const englishLayout: KeyData[][] = [
  [
    { mainValue: 'q', shiftValue: 'Q', secondaryValue: '1', action: KeyAction.Char, flex: 1 },
    { mainValue: 'w', shiftValue: 'W', secondaryValue: '2', action: KeyAction.Char, flex: 1 },
    { mainValue: 'e', shiftValue: 'E', secondaryValue: '3', action: KeyAction.Char, flex: 1 },
    { mainValue: 'r', shiftValue: 'R', secondaryValue: '4', action: KeyAction.Char, flex: 1 },
    { mainValue: 't', shiftValue: 'T', secondaryValue: '5', action: KeyAction.Char, flex: 1 },
    { mainValue: 'y', shiftValue: 'Y', secondaryValue: '6', action: KeyAction.Char, flex: 1 },
    { mainValue: 'u', shiftValue: 'U', secondaryValue: '7', action: KeyAction.Char, flex: 1 },
    { mainValue: 'i', shiftValue: 'I', secondaryValue: '8', action: KeyAction.Char, flex: 1 },
    { mainValue: 'o', shiftValue: 'O', secondaryValue: '9', action: KeyAction.Char, flex: 1 },
    { mainValue: 'p', shiftValue: 'P', secondaryValue: '0', action: KeyAction.Char, flex: 1 },
  ],
  [
    SPACER_LEFT_KEY,
    { mainValue: 'a', shiftValue: 'A', action: KeyAction.Char, flex: 1 },
    { mainValue: 's', shiftValue: 'S', action: KeyAction.Char, flex: 1 },
    { mainValue: 'd', shiftValue: 'D', action: KeyAction.Char, flex: 1 },
    { mainValue: 'f', shiftValue: 'F', action: KeyAction.Char, flex: 1 },
    { mainValue: 'g', shiftValue: 'G', action: KeyAction.Char, flex: 1 },
    { mainValue: 'h', shiftValue: 'H', action: KeyAction.Char, flex: 1 },
    { mainValue: 'j', shiftValue: 'J', action: KeyAction.Char, flex: 1 },
    { mainValue: 'k', shiftValue: 'K', action: KeyAction.Char, flex: 1 },
    { mainValue: 'l', shiftValue: 'L', action: KeyAction.Char, flex: 1 },
    SPACER_RIGHT_KEY,
  ],
  [
    SHIFT_KEY,
    { mainValue: 'z', shiftValue: 'Z', action: KeyAction.Char, flex: 1 },
    { mainValue: 'x', shiftValue: 'X', action: KeyAction.Char, flex: 1 },
    { mainValue: 'c', shiftValue: 'C', action: KeyAction.Char, flex: 1 },
    { mainValue: 'v', shiftValue: 'V', action: KeyAction.Char, flex: 1 },
    { mainValue: 'b', shiftValue: 'B', action: KeyAction.Char, flex: 1 },
    { mainValue: 'n', shiftValue: 'N', action: KeyAction.Char, flex: 1 },
    { mainValue: 'm', shiftValue: 'M', action: KeyAction.Char, flex: 1 },
    BACKSPACE_KEY,
  ],
  [
    SYMBOLS_KEY,
    { mainValue: ',', shiftValue: '<', action: KeyAction.Char, flex: 0.8 },
    SPACE_KEY,
    { mainValue: '.', shiftValue: '>', secondaryValue: '?,!,#,;,\',",:,@', action: KeyAction.Char, flex: 0.8 },
    ENTER_KEY,
  ]
];

export const arabicLayout: KeyData[][] = [
  [
    { mainValue: 'ض', secondaryValue: 'َ', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ص', secondaryValue: 'ً', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ث', secondaryValue: 'ُ', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ق', secondaryValue: 'ٌ', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ف', secondaryValue: 'إ', action: KeyAction.Char, flex: 1 },
    { mainValue: 'غ', secondaryValue: '‘', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ع', secondaryValue: '’', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ه', secondaryValue: '÷', action: KeyAction.Char, flex: 1 },
    { mainValue: 'خ', secondaryValue: '×', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ح', secondaryValue: '؛', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ج', secondaryValue: '>', action: KeyAction.Char, flex: 1 },
  ],
  [
    { mainValue: 'ش', secondaryValue: 'ِ', action: KeyAction.Char, flex: 1 },
    { mainValue: 'س', secondaryValue: 'ٍ', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ي', secondaryValue: ']', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ب', secondaryValue: '[', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ل', secondaryValue: 'لأ,لآ,ﻹ', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ا', secondaryValue: 'أ,آ', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ت', secondaryValue: 'ـ', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ن', secondaryValue: '،', action: KeyAction.Char, flex: 1 },
    { mainValue: 'م', secondaryValue: '/', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ك', secondaryValue: ':', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ة', secondaryValue: 'ى,ئ,ؤ,ء', action: KeyAction.Char, flex: 1 },
  ],
  [
    { mainValue: 'ظ', secondaryValue: '}', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ط', secondaryValue: '{', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ذ', secondaryValue: 'ّ', action: KeyAction.Char, flex: 1 },
    { mainValue: 'د', secondaryValue: '<', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ز', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ر', secondaryValue: 'ْ', action: KeyAction.Char, flex: 1 },
    { mainValue: 'و', secondaryValue: '؟', action: KeyAction.Char, flex: 1 },
    { mainValue: '◌', secondaryValue: 'َ,ُ,ِ,ّ,ْ,ً,ٌ,ٍ', action: KeyAction.Char, flex: 1 },
    BACKSPACE_KEY,
  ],
  [
    SYMBOLS_KEY,
    { mainValue: '،', action: KeyAction.Char, flex: 0.8 },
    SPACE_KEY,
    { mainValue: '.', secondaryValue: '!,?,#,;,\',",:,@', action: KeyAction.Char, flex: 0.8 },
    ENTER_KEY,
  ]
];

export const italianLayout: KeyData[][] = [
  [
    { mainValue: 'q', shiftValue: 'Q', secondaryValue: '1', action: KeyAction.Char, flex: 1 },
    { mainValue: 'w', shiftValue: 'W', secondaryValue: '2', action: KeyAction.Char, flex: 1 },
    { mainValue: 'e', shiftValue: 'E', secondaryValue: '3,é,è,€', action: KeyAction.Char, flex: 1 },
    { mainValue: 'r', shiftValue: 'R', secondaryValue: '4', action: KeyAction.Char, flex: 1 },
    { mainValue: 't', shiftValue: 'T', secondaryValue: '5', action: KeyAction.Char, flex: 1 },
    { mainValue: 'y', shiftValue: 'Y', secondaryValue: '6', action: KeyAction.Char, flex: 1 },
    { mainValue: 'u', shiftValue: 'U', secondaryValue: '7,ú,ù,ü', action: KeyAction.Char, flex: 1 },
    { mainValue: 'i', shiftValue: 'I', secondaryValue: '8,í,ì,î', action: KeyAction.Char, flex: 1 },
    { mainValue: 'o', shiftValue: 'O', secondaryValue: '9,ó,ò,ô', action: KeyAction.Char, flex: 1 },
    { mainValue: 'p', shiftValue: 'P', secondaryValue: '0', action: KeyAction.Char, flex: 1 },
  ],
  [
    SPACER_LEFT_KEY,
    { mainValue: 'a', shiftValue: 'A', secondaryValue: '@,à,á,â', action: KeyAction.Char, flex: 1 },
    { mainValue: 's', shiftValue: 'S', secondaryValue: '§', action: KeyAction.Char, flex: 1 },
    { mainValue: 'd', shiftValue: 'D', action: KeyAction.Char, flex: 1 },
    { mainValue: 'f', shiftValue: 'F', action: KeyAction.Char, flex: 1 },
    { mainValue: 'g', shiftValue: 'G', action: KeyAction.Char, flex: 1 },
    { mainValue: 'h', shiftValue: 'H', action: KeyAction.Char, flex: 1 },
    { mainValue: 'j', shiftValue: 'J', action: KeyAction.Char, flex: 1 },
    { mainValue: 'k', shiftValue: 'K', action: KeyAction.Char, flex: 1 },
    { mainValue: 'l', shiftValue: 'L', action: KeyAction.Char, flex: 1 },
    SPACER_RIGHT_KEY,
  ],
  [
    SHIFT_KEY,
    { mainValue: 'z', shiftValue: 'Z', action: KeyAction.Char, flex: 1 },
    { mainValue: 'x', shiftValue: 'X', action: KeyAction.Char, flex: 1 },
    { mainValue: 'c', shiftValue: 'C', action: KeyAction.Char, flex: 1 },
    { mainValue: 'v', shiftValue: 'V', action: KeyAction.Char, flex: 1 },
    { mainValue: 'b', shiftValue: 'B', action: KeyAction.Char, flex: 1 },
    { mainValue: 'n', shiftValue: 'N', action: KeyAction.Char, flex: 1 },
    { mainValue: 'm', shiftValue: 'M', action: KeyAction.Char, flex: 1 },
    BACKSPACE_KEY,
  ],
  [
    SYMBOLS_KEY,
    { mainValue: ',', shiftValue: ';', action: KeyAction.Char, flex: 0.8 },
    SPACE_KEY,
    { mainValue: '.', shiftValue: ':', secondaryValue: '?,!,#,;,\',",:,@', action: KeyAction.Char, flex: 0.8 },
    ENTER_KEY,
  ]
];

export const polishLayout: KeyData[][] = [
  [
    { mainValue: 'q', shiftValue: 'Q', secondaryValue: '1', action: KeyAction.Char, flex: 1 },
    { mainValue: 'w', shiftValue: 'W', secondaryValue: '2', action: KeyAction.Char, flex: 1 },
    { mainValue: 'e', shiftValue: 'E', secondaryValue: '3,ę,é', action: KeyAction.Char, flex: 1 },
    { mainValue: 'r', shiftValue: 'R', secondaryValue: '4', action: KeyAction.Char, flex: 1 },
    { mainValue: 't', shiftValue: 'T', secondaryValue: '5', action: KeyAction.Char, flex: 1 },
    { mainValue: 'y', shiftValue: 'Y', secondaryValue: '6', action: KeyAction.Char, flex: 1 },
    { mainValue: 'u', shiftValue: 'U', secondaryValue: '7', action: KeyAction.Char, flex: 1 },
    { mainValue: 'i', shiftValue: 'I', secondaryValue: '8', action: KeyAction.Char, flex: 1 },
    { mainValue: 'o', shiftValue: 'O', secondaryValue: '9,ó', action: KeyAction.Char, flex: 1 },
    { mainValue: 'p', shiftValue: 'P', secondaryValue: '0', action: KeyAction.Char, flex: 1 },
  ],
  [
    SPACER_LEFT_KEY,
    { mainValue: 'a', shiftValue: 'A', secondaryValue: '@,ą', action: KeyAction.Char, flex: 1 },
    { mainValue: 's', shiftValue: 'S', secondaryValue: 'ś', action: KeyAction.Char, flex: 1 },
    { mainValue: 'd', shiftValue: 'D', action: KeyAction.Char, flex: 1 },
    { mainValue: 'f', shiftValue: 'F', action: KeyAction.Char, flex: 1 },
    { mainValue: 'g', shiftValue: 'G', action: KeyAction.Char, flex: 1 },
    { mainValue: 'h', shiftValue: 'H', action: KeyAction.Char, flex: 1 },
    { mainValue: 'j', shiftValue: 'J', action: KeyAction.Char, flex: 1 },
    { mainValue: 'k', shiftValue: 'K', action: KeyAction.Char, flex: 1 },
    { mainValue: 'l', shiftValue: 'L', secondaryValue: 'ł', action: KeyAction.Char, flex: 1 },
    SPACER_RIGHT_KEY,
  ],
  [
    SHIFT_KEY,
    { mainValue: 'z', shiftValue: 'Z', secondaryValue: 'ź,ż', action: KeyAction.Char, flex: 1 },
    { mainValue: 'x', shiftValue: 'X', action: KeyAction.Char, flex: 1 },
    { mainValue: 'c', shiftValue: 'C', secondaryValue: 'ć', action: KeyAction.Char, flex: 1 },
    { mainValue: 'v', shiftValue: 'V', action: KeyAction.Char, flex: 1 },
    { mainValue: 'b', shiftValue: 'B', action: KeyAction.Char, flex: 1 },
    { mainValue: 'n', shiftValue: 'N', secondaryValue: 'ń', action: KeyAction.Char, flex: 1 },
    { mainValue: 'm', shiftValue: 'M', action: KeyAction.Char, flex: 1 },
    BACKSPACE_KEY,
  ],
  [
    SYMBOLS_KEY,
    { mainValue: ',', shiftValue: ';', action: KeyAction.Char, flex: 0.8 },
    SPACE_KEY,
    { mainValue: '.', shiftValue: ':', secondaryValue: '?,!,#,;,\',:,@', action: KeyAction.Char, flex: 0.8 },
    ENTER_KEY,
  ]
];

export const symbolsLayout1: KeyData[][] = [
  [
    { mainValue: '1', action: KeyAction.Char, flex: 1 }, { mainValue: '2', action: KeyAction.Char, flex: 1 },
    { mainValue: '3', action: KeyAction.Char, flex: 1 }, { mainValue: '4', action: KeyAction.Char, flex: 1 },
    { mainValue: '5', action: KeyAction.Char, flex: 1 }, { mainValue: '6', action: KeyAction.Char, flex: 1 },
    { mainValue: '7', action: KeyAction.Char, flex: 1 }, { mainValue: '8', action: KeyAction.Char, flex: 1 },
    { mainValue: '9', action: KeyAction.Char, flex: 1 }, { mainValue: '0', action: KeyAction.Char, flex: 1 },
  ],
  [
    { mainValue: '@', action: KeyAction.Char, flex: 1 }, { mainValue: '#', action: KeyAction.Char, flex: 1 },
    { mainValue: '$', action: KeyAction.Char, flex: 1 }, { mainValue: '_', action: KeyAction.Char, flex: 1 },
    { mainValue: '&', action: KeyAction.Char, flex: 1 }, { mainValue: '-', action: KeyAction.Char, flex: 1 },
    { mainValue: '+', action: KeyAction.Char, flex: 1 }, { mainValue: '(', action: KeyAction.Char, flex: 1 },
    { mainValue: ')', action: KeyAction.Char, flex: 1 }, { mainValue: '/', action: KeyAction.Char, flex: 1 },
  ],
  [
    SYMBOLS_ALT_KEY,
    { mainValue: '*', action: KeyAction.Char, flex: 1 }, { mainValue: '"', action: KeyAction.Char, flex: 1 },
    { mainValue: "'", action: KeyAction.Char, flex: 1 }, { mainValue: ':', action: KeyAction.Char, flex: 1 },
    { mainValue: ';', action: KeyAction.Char, flex: 1 }, { mainValue: '!', action: KeyAction.Char, flex: 1 },
    { mainValue: '?', action: KeyAction.Char, flex: 1 },
    BACKSPACE_KEY,
  ],
  [
    ALPHABET_KEY,
    { mainValue: ',', action: KeyAction.Char, flex: 1 },
    SPACE_KEY,
    { mainValue: '.', action: KeyAction.Char, flex: 1 },
    ENTER_KEY,
  ]
];

export const symbolsLayout2: KeyData[][] = [
  [
    { mainValue: '`', action: KeyAction.Char, flex: 1 }, { mainValue: '~', action: KeyAction.Char, flex: 1 },
    { mainValue: '\\', action: KeyAction.Char, flex: 1 }, { mainValue: '|', action: KeyAction.Char, flex: 1 },
    { mainValue: '•', action: KeyAction.Char, flex: 1 }, { mainValue: '√', action: KeyAction.Char, flex: 1 },
    { mainValue: 'π', action: KeyAction.Char, flex: 1 }, { mainValue: '÷', action: KeyAction.Char, flex: 1 },
    { mainValue: '×', action: KeyAction.Char, flex: 1 }, { mainValue: '¶', action: KeyAction.Char, flex: 1 },
  ],
  [
    { mainValue: '€', action: KeyAction.Char, flex: 1 }, { mainValue: '£', action: KeyAction.Char, flex: 1 },
    { mainValue: '¢', action: KeyAction.Char, flex: 1 }, { mainValue: '¥', action: KeyAction.Char, flex: 1 },
    { mainValue: '^', action: KeyAction.Char, flex: 1 }, { mainValue: '°', action: KeyAction.Char, flex: 1 },
    { mainValue: '=', action: KeyAction.Char, flex: 1 }, { mainValue: '{', action: KeyAction.Char, flex: 1 },
    { mainValue: '}', action: KeyAction.Char, flex: 1 }, { mainValue: '©', action: KeyAction.Char, flex: 1 },
  ],
  [
    SYMBOLS_KEY,
    { mainValue: '%', action: KeyAction.Char, flex: 1 }, { mainValue: '®', action: KeyAction.Char, flex: 1 },
    { mainValue: '™', action: KeyAction.Char, flex: 1 }, { mainValue: '✓', action: KeyAction.Char, flex: 1 },
    { mainValue: '[', action: KeyAction.Char, flex: 1 }, { mainValue: ']', action: KeyAction.Char, flex: 1 },
    { mainValue: '<', action: KeyAction.Char, flex: 1 },
    BACKSPACE_KEY,
  ],
  [
    ALPHABET_KEY,
    { mainValue: ',', action: KeyAction.Char, flex: 1 },
    SPACE_KEY,
    { mainValue: '.', action: KeyAction.Char, flex: 1 },
    ENTER_KEY,
  ]
];

export const persianLayout: KeyData[][] = [
  [
    { mainValue: 'ض', secondaryValue: 'َ', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ص', secondaryValue: 'ً', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ث', secondaryValue: 'ُ', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ق', secondaryValue: 'ٌ', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ف', secondaryValue: 'إ,أ,آ', action: KeyAction.Char, flex: 1 },
    { mainValue: 'غ', secondaryValue: ''', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ع', secondaryValue: ''', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ه', secondaryValue: 'ۀ,ة', action: KeyAction.Char, flex: 1 },
    { mainValue: 'خ', secondaryValue: '÷', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ح', secondaryValue: '؛', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ج', secondaryValue: '>', action: KeyAction.Char, flex: 1 },
  ],
  [
    { mainValue: 'ش', secondaryValue: 'ِ', action: KeyAction.Char, flex: 1 },
    { mainValue: 'س', secondaryValue: 'ٍ', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ی', secondaryValue: 'ى,ئ,ء', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ب', secondaryValue: '[', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ل', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ا', secondaryValue: 'أ,آ,إ', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ت', secondaryValue: '‌', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ن', secondaryValue: '،', action: KeyAction.Char, flex: 1 },
    { mainValue: 'م', secondaryValue: '/', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ک', action: KeyAction.Char, flex: 1 },
    { mainValue: 'گ', action: KeyAction.Char, flex: 1 },
  ],
  [
    { mainValue: 'ظ', secondaryValue: '}', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ط', secondaryValue: '{', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ذ', secondaryValue: 'ّ', action: KeyAction.Char, flex: 1 },
    { mainValue: 'د', secondaryValue: '<', action: KeyAction.Char, flex: 1 },
    { mainValue: 'پ', action: KeyAction.Char, flex: 1 },
    { mainValue: 'چ', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ژ', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ر', secondaryValue: 'ْ', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ز', secondaryValue: '؟', action: KeyAction.Char, flex: 1 },
    { mainValue: '◌', secondaryValue: 'َ,ُ,ِ,ّ,ْ,ً,ٌ,ٍ', action: KeyAction.Char, flex: 1 },
    BACKSPACE_KEY,
  ],
  [
    SYMBOLS_KEY,
    { mainValue: '،', action: KeyAction.Char, flex: 0.8 },
    SPACE_KEY,
    { mainValue: '.', secondaryValue: '!,?,#,;,\',",:,@', action: KeyAction.Char, flex: 0.8 },
    ENTER_KEY,
  ]
];

export const russianLayout: KeyData[][] = [
  [
    { mainValue: 'й', shiftValue: 'Й', secondaryValue: '1', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ц', shiftValue: 'Ц', secondaryValue: '2', action: KeyAction.Char, flex: 1 },
    { mainValue: 'у', shiftValue: 'У', secondaryValue: '3', action: KeyAction.Char, flex: 1 },
    { mainValue: 'к', shiftValue: 'К', secondaryValue: '4', action: KeyAction.Char, flex: 1 },
    { mainValue: 'е', shiftValue: 'Е', secondaryValue: '5,ё', action: KeyAction.Char, flex: 1 },
    { mainValue: 'н', shiftValue: 'Н', secondaryValue: '6', action: KeyAction.Char, flex: 1 },
    { mainValue: 'г', shiftValue: 'Г', secondaryValue: '7', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ш', shiftValue: 'Ш', secondaryValue: '8', action: KeyAction.Char, flex: 1 },
    { mainValue: 'щ', shiftValue: 'Щ', secondaryValue: '9', action: KeyAction.Char, flex: 1 },
    { mainValue: 'з', shiftValue: 'З', secondaryValue: '0', action: KeyAction.Char, flex: 1 },
  ],
  [
    SPACER_LEFT_KEY,
    { mainValue: 'ф', shiftValue: 'Ф', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ы', shiftValue: 'Ы', action: KeyAction.Char, flex: 1 },
    { mainValue: 'в', shiftValue: 'В', action: KeyAction.Char, flex: 1 },
    { mainValue: 'а', shiftValue: 'А', action: KeyAction.Char, flex: 1 },
    { mainValue: 'п', shiftValue: 'П', action: KeyAction.Char, flex: 1 },
    { mainValue: 'р', shiftValue: 'Р', action: KeyAction.Char, flex: 1 },
    { mainValue: 'о', shiftValue: 'О', action: KeyAction.Char, flex: 1 },
    { mainValue: 'л', shiftValue: 'Л', action: KeyAction.Char, flex: 1 },
    { mainValue: 'д', shiftValue: 'Д', action: KeyAction.Char, flex: 1 },
    SPACER_RIGHT_KEY,
  ],
  [
    SHIFT_KEY,
    { mainValue: 'я', shiftValue: 'Я', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ч', shiftValue: 'Ч', action: KeyAction.Char, flex: 1 },
    { mainValue: 'с', shiftValue: 'С', action: KeyAction.Char, flex: 1 },
    { mainValue: 'м', shiftValue: 'М', action: KeyAction.Char, flex: 1 },
    { mainValue: 'и', shiftValue: 'И', action: KeyAction.Char, flex: 1 },
    { mainValue: 'т', shiftValue: 'Т', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ь', shiftValue: 'Ь', secondaryValue: 'ъ', action: KeyAction.Char, flex: 1 },
    { mainValue: 'б', shiftValue: 'Б', action: KeyAction.Char, flex: 1 },
    { mainValue: 'ю', shiftValue: 'Ю', action: KeyAction.Char, flex: 1 },
    BACKSPACE_KEY,
  ],
  [
    SYMBOLS_KEY,
    { mainValue: ',', shiftValue: ';', action: KeyAction.Char, flex: 0.8 },
    SPACE_KEY,
    { mainValue: '.', shiftValue: ':', secondaryValue: '?,!,#,;,\',",:,@', action: KeyAction.Char, flex: 0.8 },
    ENTER_KEY,
  ]
];