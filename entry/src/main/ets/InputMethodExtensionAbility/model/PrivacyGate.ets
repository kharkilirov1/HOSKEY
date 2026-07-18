export type PrivacyEditorState = 'unknown' | 'normal' | 'sensitive';
export type PrivacySecurityState = 'unknown' | 'basic' | 'full';
type PrivacyBlockReason = 'none' | 'editor_unknown' | 'security_unknown' | 'editor_sensitive' | 'security_basic';

const INCOGNITO_DIAGNOSTIC: string = 'blocked:platform_signal_unproven';

export class PrivacyGate {
  private editorState: PrivacyEditorState = 'unknown';
  private securityState: PrivacySecurityState = 'unknown';

  public reset(): void {
    this.editorState = 'unknown';
    this.securityState = 'unknown';
  }

  public resetEditorContext(): void {
    this.editorState = 'unknown';
  }

  public resetSecurityMode(): void {
    this.securityState = 'unknown';
  }

  public updateEditorPattern(inputPattern: number, sensitivePatterns: number[]): void {
    this.editorState = sensitivePatterns.indexOf(inputPattern) >= 0 ? 'sensitive' : 'normal';
  }

  public updateSecurityMode(mode: number, basicMode: number, fullMode: number): void {
    if (mode === basicMode) {
      this.securityState = 'basic';
      return;
    }
    if (mode === fullMode) {
      this.securityState = 'full';
      return;
    }
    this.securityState = 'unknown';
  }

  public canProcessText(): boolean {
    return this.getBlockReason() === 'none';
  }

  public getDiagnostics(): string {
    const reason: PrivacyBlockReason = this.getBlockReason();
    const state: string = reason === 'none' ? 'allowed' : 'blocked';
    return 'privacy_gate=' + state +
      ',reason=' + reason +
      ',editor=' + this.editorState +
      ',security=' + this.securityState +
      ',incognito=' + INCOGNITO_DIAGNOSTIC;
  }

  private getBlockReason(): PrivacyBlockReason {
    if (this.editorState === 'sensitive') {
      return 'editor_sensitive';
    }
    if (this.securityState === 'basic') {
      return 'security_basic';
    }
    if (this.editorState === 'unknown') {
      return 'editor_unknown';
    }
    if (this.securityState === 'unknown') {
      return 'security_unknown';
    }
    return 'none';
  }
}

const privacyGate = new PrivacyGate();
export default privacyGate;
