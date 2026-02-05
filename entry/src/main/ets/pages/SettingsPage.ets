// SettingsPage - Main keyboard settings (Yandex style)

import router from '@ohos.router';
import { preferencesManager } from '../InputMethodExtensionAbility/model/PreferencesManager';

interface SettingItem {
  icon: string;
  title: string;
  subtitle?: string;
  action: 'navigate' | 'toggle' | 'slider';
  route?: string;
  key?: string;
  value?: boolean | number;
}

@Entry
@Component
struct SettingsPage {
  // Input settings
  @State autocorrectEnabled: boolean = true;
  @State suggestionsEnabled: boolean = true;
  @State soundEnabled: boolean = false;
  @State vibrationEnabled: boolean = true;
  @State swipeTypingEnabled: boolean = true;

  // Appearance settings  
  @State keyboardHeight: number = 260;

  // Languages
  @State enabledLanguagesCount: number = 2;

  aboutToAppear(): void {
    this.loadSettings();
  }

  private loadSettings(): void {
    const languages = preferencesManager.getEnabledLanguages();
    this.enabledLanguagesCount = languages.length;
    this.keyboardHeight = preferencesManager.getKeyboardHeight();
    // TODO: Load other settings from preferences
  }

  private getEnabledLanguagesText(): string {
    const languages = preferencesManager.getEnabledLanguages();
    const names: Record<string, string> = {
      'en': 'English',
      'ru': 'Русский',
      'uk': 'Українська',
      'kk': 'Қазақша',
      'de': 'Deutsch',
      'fr': 'Français',
      'es': 'Español',
      'it': 'Italiano',
      'pl': 'Polski'
    };
    
    if (languages.length <= 2) {
      return languages.map(l => names[l] || l).join(', ');
    }
    return `${languages.length} языка`;
  }

  @Builder
  buildSectionHeader(title: string) {
    Text(title)
      .fontSize(13)
      .fontWeight(500)
      .fontColor('#999999')
      .padding({ left: 16, right: 16, top: 24, bottom: 8 })
      .width('100%')
  }

  @Builder
  buildNavigationItem(icon: string, title: string, subtitle: string, route: string) {
    Row() {
      Text(icon)
        .fontSize(22)
        .width(40)

      Column() {
        Text(title)
          .fontSize(16)
          .fontColor('#000000')
        
        if (subtitle) {
          Text(subtitle)
            .fontSize(13)
            .fontColor('#999999')
            .margin({ top: 2 })
        }
      }
      .alignItems(HorizontalAlign.Start)
      .layoutWeight(1)

      Text('›')
        .fontSize(20)
        .fontColor('#CCCCCC')
    }
    .width('100%')
    .padding({ left: 16, right: 16, top: 14, bottom: 14 })
    .backgroundColor('#FFFFFF')
    .onClick(() => {
      router.pushUrl({ url: route });
    })
  }

  @Builder
  buildToggleItem(icon: string, title: string, subtitle: string, isOn: boolean, onChange: (value: boolean) => void) {
    Row() {
      Text(icon)
        .fontSize(22)
        .width(40)

      Column() {
        Text(title)
          .fontSize(16)
          .fontColor('#000000')
        
        if (subtitle) {
          Text(subtitle)
            .fontSize(13)
            .fontColor('#999999')
            .margin({ top: 2 })
        }
      }
      .alignItems(HorizontalAlign.Start)
      .layoutWeight(1)

      Toggle({ type: ToggleType.Switch, isOn: isOn })
        .width(50)
        .height(28)
        .selectedColor('#4E66FF')
        .onChange((value: boolean) => {
          onChange(value);
        })
    }
    .width('100%')
    .padding({ left: 16, right: 16, top: 14, bottom: 14 })
    .backgroundColor('#FFFFFF')
  }

  @Builder
  buildSliderItem(icon: string, title: string, value: number, min: number, max: number, onChange: (value: number) => void) {
    Column() {
      Row() {
        Text(icon)
          .fontSize(22)
          .width(40)

        Text(title)
          .fontSize(16)
          .fontColor('#000000')
          .layoutWeight(1)

        Text(`${value}`)
          .fontSize(14)
          .fontColor('#999999')
      }

      Slider({
        value: value,
        min: min,
        max: max,
        step: 10
      })
        .width('100%')
        .margin({ top: 8 })
        .trackColor('#E0E0E0')
        .selectedColor('#4E66FF')
        .onChange((value: number) => {
          onChange(value);
        })
    }
    .width('100%')
    .padding({ left: 16, right: 16, top: 14, bottom: 14 })
    .backgroundColor('#FFFFFF')
  }

  build() {
    Column() {
      // Header
      Row() {
        Image($r('app.media.return_icon'))
          .width(24)
          .height(24)
          .onClick(() => {
            router.back();
          })

        Text('Настройки')
          .fontSize(20)
          .fontWeight(600)
          .fontColor('#000000')
          .margin({ left: 16 })

        Blank()
      }
      .width('100%')
      .height(56)
      .padding({ left: 16, right: 16 })
      .backgroundColor('#FFFFFF')

      // Settings list
      Scroll() {
        Column() {
          // Languages section
          this.buildSectionHeader('ЯЗЫКИ')
          
          this.buildNavigationItem(
            '🌍',
            'Языки ввода',
            this.getEnabledLanguagesText(),
            'pages/LanguagesPage'
          )

          // Appearance section
          this.buildSectionHeader('ВНЕШНИЙ ВИД')

          this.buildNavigationItem(
            '🎨',
            'Тема',
            'Системная',
            'pages/ThemesPage'
          )

          this.buildSliderItem(
            '📏',
            'Высота клавиатуры',
            this.keyboardHeight,
            200,
            350,
            (value: number) => {
              this.keyboardHeight = value;
              preferencesManager.setKeyboardHeight(value);
            }
          )

          // Input section
          this.buildSectionHeader('ВВОД')

          this.buildToggleItem(
            '✨',
            'Автокоррекция',
            'Исправлять ошибки автоматически',
            this.autocorrectEnabled,
            (value: boolean) => {
              this.autocorrectEnabled = value;
            }
          )
          Divider().color('#E0E0E0').margin({ left: 56 })

          this.buildToggleItem(
            '💡',
            'Подсказки слов',
            'Показывать варианты при вводе',
            this.suggestionsEnabled,
            (value: boolean) => {
              this.suggestionsEnabled = value;
            }
          )
          Divider().color('#E0E0E0').margin({ left: 56 })

          this.buildToggleItem(
            '👆',
            'Свайп-ввод',
            'Вводить слова жестами',
            this.swipeTypingEnabled,
            (value: boolean) => {
              this.swipeTypingEnabled = value;
            }
          )

          // Feedback section
          this.buildSectionHeader('ОБРАТНАЯ СВЯЗЬ')

          this.buildToggleItem(
            '🔊',
            'Звук нажатия',
            'Звуковой отклик при нажатии',
            this.soundEnabled,
            (value: boolean) => {
              this.soundEnabled = value;
            }
          )
          Divider().color('#E0E0E0').margin({ left: 56 })

          this.buildToggleItem(
            '📳',
            'Вибрация',
            'Тактильный отклик при нажатии',
            this.vibrationEnabled,
            (value: boolean) => {
              this.vibrationEnabled = value;
            }
          )

          // About section
          this.buildSectionHeader('О ПРИЛОЖЕНИИ')

          this.buildNavigationItem(
            'ℹ️',
            'О HOSKEY',
            'Версия 1.0.0',
            'pages/SetupGuidePage'
          )

          // Bottom padding
          Blank()
            .height(32)
        }
        .width('100%')
      }
      .layoutWeight(1)
      .scrollBar(BarState.Off)
      .backgroundColor('#EBEEF0')
    }
    .width('100%')
    .height('100%')
    .backgroundColor('#EBEEF0')
  }
}
