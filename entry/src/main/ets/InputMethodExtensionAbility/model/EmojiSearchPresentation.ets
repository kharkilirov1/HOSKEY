export type EmojiSearchEditAction = 'char' | 'space' | 'backspace' | 'clear';

export function normalizeEmojiSearchQuery(rawQuery: string): string {
  return rawQuery
    .replace(/^[\s\uFEFF\u00A0]+|[\s\uFEFF\u00A0]+$/g, '')
    .replace(/[^\S\r\n]+/g, ' ');
}

export function applyEmojiSearchEdit(query: string, action: EmojiSearchEditAction,
  value: string = ''): string {
  if (action === 'clear') {
    return '';
  }
  if (action === 'space') {
    return query + ' ';
  }
  if (action === 'char') {
    return query + value;
  }
  if (query.length === 0) {
    return query;
  }

  let cutIndex: number = query.length - 1;
  const trailingUnit: number = query.charCodeAt(cutIndex);
  if (trailingUnit >= 0xDC00 && trailingUnit <= 0xDFFF && cutIndex > 0) {
    const leadingUnit: number = query.charCodeAt(cutIndex - 1);
    if (leadingUnit >= 0xD800 && leadingUnit <= 0xDBFF) {
      cutIndex--;
    }
  }
  return query.substring(0, cutIndex);
}

export function selectEmojiPanelItems(query: string, modelResults: string[], categoryResults: string[]): string[] {
  return normalizeEmojiSearchQuery(query).length > 0 ? modelResults : categoryResults;
}
