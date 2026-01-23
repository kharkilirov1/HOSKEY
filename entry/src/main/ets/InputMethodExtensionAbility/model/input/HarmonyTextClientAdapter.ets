/**
 * HarmonyTextClientAdapter - Adapts HarmonyOS InputClient to ITextClientAdapter
 *
 * Wraps HarmonyOS-specific inputMethodEngine.InputClient with
 * a clean interface that InputPipeline can use.
 *
 * Performance:
 * - Zero overhead - direct passthrough to native client
 * - All methods are synchronous and fast
 */

import { inputMethodEngine } from '@kit.IMEKit';
import { ITextClientAdapter } from './InputTypes';

export class HarmonyTextClientAdapter implements ITextClientAdapter {
  private client: inputMethodEngine.InputClient | undefined;

  constructor(client?: inputMethodEngine.InputClient) {
    this.client = client;
  }

  /**
   * Update the underlying client (called on inputStart)
   */
  setClient(client: inputMethodEngine.InputClient | undefined): void {
    this.client = client;
  }

  /**
   * Insert text at cursor - FAST PATH
   */
  /**
   * @throws
   */
  insertText(text: string): void {
    if (!this.client) {
      return;
    }
    try {
      this.client.insertText(text);
    } catch {
      // Silent catch for ArkTS compliance - hot path optimization
    }
  }

  /**
   * Delete N characters backward - FAST PATH
   */
  /**
   * @throws
   */
  deleteBackward(count: number = 1): void {
    if (!this.client) {
      return;
    }
    try {
      // HarmonyOS: deleteForward actually deletes BEFORE cursor
      this.client.deleteForward(count);
    } catch {
      // Silent catch for ArkTS compliance - hot path optimization
    }
  }

  /**
   * Send Enter key - FAST PATH
   */
  /**
   * @throws
   */
  sendEnter(): void {
    if (!this.client) {
      return;
    }
    try {
      this.client.sendKeyFunction(17); // 17 is the key code for ENTER

    } catch {
      // Silent catch for ArkTS compliance - hot path optimization
    }
  }

  /**
   * Check if client is available
   */
  isAvailable(): boolean {
    return this.client !== undefined;
  }
}
