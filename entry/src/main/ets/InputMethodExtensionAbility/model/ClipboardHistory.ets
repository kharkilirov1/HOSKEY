import preferences from '@ohos.data.preferences';
import { Context } from '@kit.AbilityKit';
import { BusinessError } from '@kit.BasicServicesKit';

export enum ClipboardItemType {
  Text = 'text',
  URL = 'url',
  Email = 'email',
  Phone = 'phone',
  Image = 'image',
  Link = 'link',
  File = 'file',
  Emoji = 'emoji'
}

export interface ClipboardItem {
  id: string;
  text: string;
  type: ClipboardItemType;
  timestamp: number;
  isPinned: boolean;
}

class ClipboardHistory {
  private context: Context | null = null;
  private items: ClipboardItem[] = [];
  private maxItems: number = 20;
  private prefsStore: preferences.Preferences | null = null;

  setContext(ctx: Context): void {
    this.context = ctx;
  }

  async load(): Promise<void> {
    if (!this.context) {
      console.warn('ClipboardHistory: Cannot load - context not set');
      return;
    }

    try {
      this.prefsStore = await preferences.getPreferences(this.context, 'clipboard_history');
      const stored = this.prefsStore.getSync('items', '[]') as string;
      const parsed: ClipboardItem[] = JSON.parse(stored) as ClipboardItem[];

      if (Array.isArray(parsed)) {
        this.items = parsed.map((item: ClipboardItem): ClipboardItem => {
          return {
            id: item.id || this.generateId(),
            text: item.text || '',
            type: item.type || ClipboardItemType.Text,
            timestamp: item.timestamp || Date.now(),
            isPinned: item.isPinned || false
          };
        });
      }

      console.info(`ClipboardHistory: Loaded ${this.items.length} items from storage`);
    } catch (err) {
      const error = err as BusinessError;
      console.error(`ClipboardHistory: Failed to load. Code: ${error.code}, Msg: ${error.message}`);
      this.items = [];
    }
  }

  async save(): Promise<void> {
    if (!this.prefsStore) {
      console.warn('ClipboardHistory: Cannot save - preferences not initialized');
      return;
    }

    try {
      await this.prefsStore.put('items', JSON.stringify(this.items));
      await this.prefsStore.flush();
      console.info(`ClipboardHistory: Saved ${this.items.length} items to storage`);
    } catch (err) {
      const error = err as BusinessError;
      console.error(`ClipboardHistory: Failed to save. Code: ${error.code}, Msg: ${error.message}`);
    }
  }

  private generateId(): string {
    return `${Date.now()}-${Math.random().toString(36).substring(2, 11)}`;
  }

  private detectType(text: string): ClipboardItemType {
    if (!text || text.trim().length === 0) {
      return ClipboardItemType.Text;
    }

    const trimmed = text.trim();

    // Image detection - check if it looks like an image URL
    const imageExtensions = ['.jpg', '.jpeg', '.png', '.gif', '.bmp', '.webp', '.svg'];
    if (imageExtensions.some(ext => trimmed.toLowerCase().includes(ext))) {
      return ClipboardItemType.Image;
    }

    // File detection - check if it looks like a file path
    const fileExtensions = ['.pdf', '.doc', '.docx', '.xls', '.xlsx', '.ppt', '.pptx', '.txt', '.zip', '.rar', '.mp3', '.mp4', '.avi', '.mov'];
    if (fileExtensions.some(ext => trimmed.toLowerCase().includes(ext)) || 
        (trimmed.includes('/') && trimmed.includes('.')) || 
        trimmed.startsWith('/')) {
      return ClipboardItemType.File;
    }

    // URL detection - more comprehensive
    const urlPattern = /^(https?:\/\/)?([\w-]+(\.[\w-]+)+)(:\d+)?(\/[^\s]*)?$/i;
    const simpleUrlPattern = /^(www\.[\w-]+(\.[\w-]+)+)(\/[^\s]*)?$/i;
    if (urlPattern.test(trimmed) || simpleUrlPattern.test(trimmed)) {
      return ClipboardItemType.URL;
    }

    // Link detection - similar to URL but for links specifically
    if (trimmed.includes('http://') || trimmed.includes('https://')) {
      return ClipboardItemType.Link;
    }

    // Email detection
    const emailPattern = /^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$/;
    if (emailPattern.test(trimmed)) {
      return ClipboardItemType.Email;
    }

    // Phone detection - international formats
    const phonePattern = /^[\+]?[(]?[0-9]{1,4}[)]?[-\s\.]?[(]?[0-9]{1,4}[)]?[-\s\.]?[0-9]{1,5}[-\s\.]?[0-9]{1,6}$/;
    const digitsOnly = trimmed.replace(/[\s\-\(\)\.]/g, '');
    if (phonePattern.test(trimmed) && digitsOnly.length >= 7 && digitsOnly.length <= 15) {
      return ClipboardItemType.Phone;
    }

    // Emoji detection - check if text contains mostly emojis
    const emojiRegex = /(?:\ud83c[\udf00-\udfff]|\ud83d[\udc00-\ude4f\ude80-\udeff]|[\u2600-\u27bf]\ufe0f?|\ud83c[\udde6-\uddff]){1,}/g;
    const emojiMatches = trimmed.match(emojiRegex);
    if (emojiMatches && emojiMatches.length > 0) {
      const emojiCount = emojiMatches.join('').length;
      const totalLength = trimmed.replace(/\s/g, '').length;
      if (emojiCount > 0 && totalLength > 0 && emojiCount / totalLength > 0.3) { // More than 30% emojis
        return ClipboardItemType.Emoji;
      }
    }

    return ClipboardItemType.Text;
  }

  addItem(text: string): void {
    if (!text || text.trim().length === 0) {
      return;
    }

    // Check if item already exists (avoid duplicates)
    const existingIndex = this.items.findIndex(item => item.text === text && !item.isPinned);
    if (existingIndex !== -1) {
      // Move to top by removing and re-adding
      this.items.splice(existingIndex, 1);
    }

    const newItem: ClipboardItem = {
      id: this.generateId(),
      text: text,
      type: this.detectType(text),
      timestamp: Date.now(),
      isPinned: false
    };

    // Add to the beginning
    this.items.unshift(newItem);

    // Keep only maxItems (but preserve pinned items)
    const pinnedItems = this.items.filter(item => item.isPinned);
    const unpinnedItems = this.items.filter(item => !item.isPinned).slice(0, this.maxItems - pinnedItems.length);
    this.items = [...pinnedItems, ...unpinnedItems];

    this.save();
  }

  getItems(): ClipboardItem[] {
    // Sort: pinned items first, then by timestamp
    return this.items.sort((a, b) => {
      if (a.isPinned && !b.isPinned) return -1;
      if (!a.isPinned && b.isPinned) return 1;
      return b.timestamp - a.timestamp;
    });
  }

  getItemsByType(type: ClipboardItemType): ClipboardItem[] {
    return this.getItems().filter(item => item.type === type);
  }

  searchItems(query: string): ClipboardItem[] {
    if (!query || query.trim().length === 0) {
      return this.getItems();
    }

    const lowerQuery = query.toLowerCase();
    return this.getItems().filter(item =>
      item.text.toLowerCase().includes(lowerQuery)
    );
  }

  togglePin(id: string): void {
    const item = this.items.find(item => item.id === id);
    if (item) {
      item.isPinned = !item.isPinned;
      this.save();
    }
  }

  deleteItem(id: string): void {
    const index = this.items.findIndex(item => item.id === id);
    if (index !== -1) {
      this.items.splice(index, 1);
      this.save();
    }
  }

  clearAll(): void {
    // Keep only pinned items
    this.items = this.items.filter(item => item.isPinned);
    this.save();
  }

  getItemById(id: string): ClipboardItem | null {
    return this.items.find(item => item.id === id) || null;
  }
}

const clipboardHistory = new ClipboardHistory();
export default clipboardHistory;