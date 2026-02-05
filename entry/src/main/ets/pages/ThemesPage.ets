// ThemesPage - Theme selection UI (Yandex style)

import router from '@ohos.router';
import { themeManager, KeyboardTheme, colorToHex } from '../InputMethodExtensionAbility/model/ThemeManager';

@Entry
@Component
struct ThemesPage {
  @State themes: KeyboardTheme[] = [];
  @State currentThemeId: string = 'system';
  @State isSystemTheme: boolean = true;

  aboutToAppear(): void {
    this.loadData();
  }

  private loadData(): void {
    this.themes = themeManager.getThemes();
    this.currentThemeId = themeManager.getCurrentThemeId();
    this.isSystemTheme = themeManager.isUsingSystemTheme();
  }

  private selectTheme(themeId: string): void {
    if (themeId === 'system') {
      themeManager.setUseSystemTheme(true);
      this.isSystemTheme = true;
      this.currentThemeId = 'system';
    } else {
      themeManager.setTheme(themeId);
      this.isSystemTheme = false;
      this.currentThemeId = themeId;
    }
  }

  @Builder
  buildThemePreview(theme: KeyboardTheme, isSelected: boolean) {
    Column() {
      // Theme preview box
      Stack() {
        // Background
        Column()
          .width('100%')
          .height('100%')
          .backgroundColor(colorToHex(theme.bg))
          .borderRadius(12)

        // Mini keyboard preview
        Column({ space: 4 }) {
          // Row 1 - keys
          Row({ space: 3 }) {
            ForEach([1, 2, 3, 4, 5], () => {
              Column()
                .width(16)
                .height(20)
                .backgroundColor(colorToHex(theme.keyBg))
                .borderRadius(4)
            })
          }
          
          // Row 2 - keys
          Row({ space: 3 }) {
            ForEach([1, 2, 3, 4], () => {
              Column()
                .width(16)
                .height(20)
                .backgroundColor(colorToHex(theme.keyBg))
                .borderRadius(4)
            })
          }
          
          // Row 3 - with accent
          Row({ space: 3 }) {
            Column()
              .width(20)
              .height(20)
              .backgroundColor(colorToHex(theme.keyBgFunc))
              .borderRadius(4)
            Column()
              .width(50)
              .height(20)
              .backgroundColor(colorToHex(theme.keyBg))
              .borderRadius(4)
            Column()
              .width(20)
              .height(20)
              .backgroundColor(colorToHex(theme.accent))
              .borderRadius(4)
          }
        }
        .padding(12)

        // Selection indicator
        if (isSelected) {
          Column() {
            Text('✓')
              .fontSize(16)
              .fontWeight(700)
              .fontColor('#FFFFFF')
          }
          .width(24)
          .height(24)
          .justifyContent(FlexAlign.Center)
          .backgroundColor('#4E66FF')
          .borderRadius(12)
          .position({ x: '75%', y: 8 })
        }
      }
      .width(110)
      .height(90)
      .borderRadius(12)
      .border({ 
        width: isSelected ? 2 : 1, 
        color: isSelected ? '#4E66FF' : '#E0E0E0' 
      })

      // Theme name
      Text(theme.name)
        .fontSize(13)
        .fontColor(isSelected ? '#4E66FF' : '#000000')
        .fontWeight(isSelected ? 500 : 400)
        .margin({ top: 8 })
    }
    .onClick(() => {
      this.selectTheme(theme.id);
    })
  }

  @Builder
  buildSystemThemePreview(isSelected: boolean) {
    Column() {
      // Split preview (day/night)
      Stack() {
        Row() {
          // Light half
          Column()
            .width('50%')
            .height('100%')
            .backgroundColor('#EBEEF0')
            .borderRadius({ topLeft: 12, bottomLeft: 12 })

          // Dark half
          Column()
            .width('50%')
            .height('100%')
            .backgroundColor('#121212')
            .borderRadius({ topRight: 12, bottomRight: 12 })
        }
        .width('100%')
        .height('100%')

        // Icon
        Text('🌓')
          .fontSize(28)

        // Selection indicator
        if (isSelected) {
          Column() {
            Text('✓')
              .fontSize(16)
              .fontWeight(700)
              .fontColor('#FFFFFF')
          }
          .width(24)
          .height(24)
          .justifyContent(FlexAlign.Center)
          .backgroundColor('#4E66FF')
          .borderRadius(12)
          .position({ x: '75%', y: 8 })
        }
      }
      .width(110)
      .height(90)
      .borderRadius(12)
      .border({ 
        width: isSelected ? 2 : 1, 
        color: isSelected ? '#4E66FF' : '#E0E0E0' 
      })

      Text('Системная')
        .fontSize(13)
        .fontColor(isSelected ? '#4E66FF' : '#000000')
        .fontWeight(isSelected ? 500 : 400)
        .margin({ top: 8 })
    }
    .onClick(() => {
      this.selectTheme('system');
    })
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

        Text('Тема')
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

      // Themes grid
      Scroll() {
        Column() {
          // System theme section
          Text('АВТОМАТИЧЕСКИ')
            .fontSize(13)
            .fontWeight(500)
            .fontColor('#999999')
            .padding({ left: 16, right: 16, top: 20, bottom: 12 })
            .width('100%')

          Row() {
            this.buildSystemThemePreview(this.isSystemTheme)
          }
          .padding({ left: 16, right: 16 })

          // Default themes
          Text('ОСНОВНЫЕ')
            .fontSize(13)
            .fontWeight(500)
            .fontColor('#999999')
            .padding({ left: 16, right: 16, top: 24, bottom: 12 })
            .width('100%')

          Flex({ wrap: FlexWrap.Wrap, justifyContent: FlexAlign.Start }) {
            ForEach(
              this.themes.filter(t => t.category === 'default'),
              (theme: KeyboardTheme) => {
                Column() {
                  this.buildThemePreview(theme, !this.isSystemTheme && this.currentThemeId === theme.id)
                }
                .margin({ left: 8, right: 8, bottom: 16 })
              }
            )
          }
          .padding({ left: 8, right: 8 })

          // Dark themes
          Text('ТЁМНЫЕ')
            .fontSize(13)
            .fontWeight(500)
            .fontColor('#999999')
            .padding({ left: 16, right: 16, top: 8, bottom: 12 })
            .width('100%')

          Flex({ wrap: FlexWrap.Wrap, justifyContent: FlexAlign.Start }) {
            ForEach(
              this.themes.filter(t => t.category === 'dark'),
              (theme: KeyboardTheme) => {
                Column() {
                  this.buildThemePreview(theme, !this.isSystemTheme && this.currentThemeId === theme.id)
                }
                .margin({ left: 8, right: 8, bottom: 16 })
              }
            )
          }
          .padding({ left: 8, right: 8 })

          // Color themes
          Text('ЦВЕТНЫЕ')
            .fontSize(13)
            .fontWeight(500)
            .fontColor('#999999')
            .padding({ left: 16, right: 16, top: 8, bottom: 12 })
            .width('100%')

          Flex({ wrap: FlexWrap.Wrap, justifyContent: FlexAlign.Start }) {
            ForEach(
              this.themes.filter(t => t.category === 'color'),
              (theme: KeyboardTheme) => {
                Column() {
                  this.buildThemePreview(theme, !this.isSystemTheme && this.currentThemeId === theme.id)
                }
                .margin({ left: 8, right: 8, bottom: 16 })
              }
            )
          }
          .padding({ left: 8, right: 8 })

          // Bottom padding
          Blank().height(32)
        }
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
