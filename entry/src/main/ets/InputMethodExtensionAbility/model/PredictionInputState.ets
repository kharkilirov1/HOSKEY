export interface PredictionInputState {
  previousWord: string;
  currentWord: string;
}

const PREDICTION_BOUNDARIES: string[] = [
  ' ', '\n', ',', '.', '!', '?', ';', ':', '/', '\\', '-', '_', '"', "'", '(', ')', '[', ']', '<', '>'
];

export function isPredictionBoundary(text: string): boolean {
  return text.length > 0 && PREDICTION_BOUNDARIES.indexOf(text) >= 0;
}

export function derivePredictionInputState(textBeforeCursor: string): PredictionInputState {
  const completedWords: string[] = [];
  let currentWord: string = '';

  for (let index: number = 0; index < textBeforeCursor.length; index++) {
    const char: string = textBeforeCursor.charAt(index);
    if (isPredictionBoundary(char)) {
      if (currentWord.length > 0) {
        completedWords.push(currentWord);
        currentWord = '';
      }
    } else {
      currentWord += char;
    }
  }

  return {
    previousWord: completedWords.length > 0 ? completedWords[completedWords.length - 1] : '',
    currentWord
  };
}
