import vibrator from '@ohos.vibrator';
import { BusinessError } from '@kit.BasicServicesKit';

export enum FeedbackType {
  Light = 'light',      // Легкое нажатие на обычную клавишу
  Medium = 'medium',    // Нажатие на специальную клавишу
  Heavy = 'heavy'       // Backspace, delete
}

class FeedbackController {
  private hapticEnabled: boolean = true;
  private soundEnabled: boolean = true;

  // Enable/disable haptic feedback
  setHapticEnabled(enabled: boolean): void {
    this.hapticEnabled = enabled;
  }

  // Enable/disable sound feedback
  setSoundEnabled(enabled: boolean): void {
    this.soundEnabled = enabled;
  }

  // Vibrate with different intensities
  async vibrate(type: FeedbackType): Promise<void> {
    if (!this.hapticEnabled) {
      return;
    }

    try {
      let duration = 10; // Default light vibration

      switch (type) {
        case FeedbackType.Light:
          duration = 8; // Very light for regular keys
          break;
        case FeedbackType.Medium:
          duration = 12; // Medium for special keys
          break;
        case FeedbackType.Heavy:
          duration = 20; // Stronger for backspace/delete
          break;
      }

      // Use preset vibration effect for better UX
      await vibrator.startVibration({
        type: 'time',
        duration: duration
      });
    } catch (err) {
      const error = err as BusinessError;
      console.warn(`FeedbackController: Vibration failed. Code: ${error.code}, Msg: ${error.message}`);
    }
  }

  // Play click sound
  async playSound(type: FeedbackType): Promise<void> {
    if (!this.soundEnabled) {
      return;
    }

    // TODO: Implement sound playback when sound resources are available
    // For now, we rely on system sounds which are handled automatically
    // by HarmonyOS for keyboard input
  }

  // Combined feedback (vibration + sound)
  async provideFeedback(type: FeedbackType): Promise<void> {
    // Execute both in parallel for immediate response
    await Promise.all([
      this.vibrate(type),
      this.playSound(type)
    ]);
  }

  // Stop all feedback
  async stopFeedback(): Promise<void> {
    try {
      await vibrator.stopVibration();
    } catch (err) {
      const error = err as BusinessError;
      console.warn(`FeedbackController: Stop vibration failed. Code: ${error.code}, Msg: ${error.message}`);
    }
  }
}

const feedbackController = new FeedbackController();
export default feedbackController;
