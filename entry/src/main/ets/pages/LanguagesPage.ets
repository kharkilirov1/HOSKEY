// LanguagesPage - Language selection and dictionary download UI
// Yandex-style language management

import router from '@ohos.router';
import { dictionaryManager, DictionaryInfo, DownloadProgress } from '../InputMethodExtensionAbility/model/DictionaryManager';
import { preferencesManager } from '../InputMethodExtensionAbility/model/PreferencesManager';

@Entry
@Component
struct LanguagesPage {
  @State dictionaries: DictionaryInfo[] = [];
  @State installedLanguages: string[] = [];
  @State enabledLanguages: string[] = [];
  @State downloadingLanguages: Map<string, DownloadProgress> = new Map();
  @State isLoading: boolean = true;

  aboutToAppear(): void {
    this.loadData();
  }

  private loadData(): void {
    this.dictionaries = dictionaryManager.getAvailableDictionaries();
    this.installedLanguages = dictionaryManager.getInstalledDictionaries();
    this.enabledLanguages = preferencesManager.getEnabledLanguages();
    this.isLoading = false;
  }

  private isLanguageEnabled(langCode: string): boolean {
    return this.enabledLanguages.includes(langCode);
  }

  private isLanguageInstalled(langCode: string): boolean {
    return this.installedLanguages.includes(langCode);
  }

  private getDownloadProgress(langCode: string): DownloadProgress | undefined {
    return this.downloadingLanguages.get(langCode);
  }

  private async toggleLanguage(langCode: string): Promise<void> {
    const isEnabled = this.isLanguageEnabled(langCode);
    const isInstalled = this.isLanguageInstalled(langCode);

    if (isEnabled) {
      // Disable language
      const newEnabled = this.enabledLanguages.filter(l => l !== langCode);
      if (newEnabled.length > 0) {
        this.enabledLanguages = newEnabled;
        preferencesManager.setEnabledLanguages(newEnabled);
      }
    } else {
      // Enable language
      if (!isInstalled) {
        // Need to download first
        await this.downloadLanguage(langCode);
      }
      
      // Add to enabled
      if (!this.enabledLanguages.includes(langCode)) {
        this.enabledLanguages = [...this.enabledLanguages, langCode];
        preferencesManager.setEnabledLanguages(this.enabledLanguages);
      }
    }
  }

  private async downloadLanguage(langCode: string): Promise<void> {
    const progress: DownloadProgress = {
      languageCode: langCode,
      progress: 0,
      downloadedBytes: 0,
      totalBytes: dictionaryManager.getDictionaryInfo(langCode)?.downloadSize || 0,
      status: 'downloading'
    };

    this.downloadingLanguages.set(langCode, progress);
    this.downloadingLanguages = new Map(this.downloadingLanguages); // Trigger refresh

    // Register progress callback
    dictionaryManager.onProgress(langCode, (p) => {
      this.downloadingLanguages.set(langCode, p);
      this.downloadingLanguages = new Map(this.downloadingLanguages);

      if (p.status === 'completed') {
        this.installedLanguages = dictionaryManager.getInstalledDictionaries();
      }
    });

    // Start download
    const success = await dictionaryManager.downloadDictionary(langCode);
    
    if (success) {
      this.installedLanguages = dictionaryManager.getInstalledDictionaries();
    }

    // Clean up
    dictionaryManager.removeProgressCallback(langCode);
  }

  private formatSize(bytes: number): string {
    return dictionaryManager.formatSize(bytes);
  }

  @Builder
  buildLanguageItem(dict: DictionaryInfo) {
    Row() {
      // Language info
      Column() {
        Text(dict.nativeName)
          .fontSize(16)
          .fontWeight(500)
          .fontColor('#000000')

        Row({ space: 8 }) {
          Text(dict.languageName)
            .fontSize(13)
            .fontColor('#999999')

          if (dict.isBuiltIn) {
            Text('Встроен')
              .fontSize(11)
              .fontColor('#FFFFFF')
              .padding({ left: 6, right: 6, top: 2, bottom: 2 })
              .backgroundColor('#4E66FF')
              .borderRadius(4)
          } else if (this.isLanguageInstalled(dict.languageCode)) {
            Text('Загружен')
              .fontSize(11)
              .fontColor('#FFFFFF')
              .padding({ left: 6, right: 6, top: 2, bottom: 2 })
              .backgroundColor('#4CAF50')
              .borderRadius(4)
          } else {
            Text(this.formatSize(dict.downloadSize))
              .fontSize(11)
              .fontColor('#999999')
          }
        }
      }
      .alignItems(HorizontalAlign.Start)
      .layoutWeight(1)

      // Download progress or toggle
      if (this.getDownloadProgress(dict.languageCode)?.status === 'downloading') {
        // Show download progress
        Column() {
          Progress({
            value: this.getDownloadProgress(dict.languageCode)?.progress || 0,
            total: 100,
            type: ProgressType.Ring
          })
            .width(32)
            .height(32)
            .color('#4E66FF')
        }
      } else if (this.getDownloadProgress(dict.languageCode)?.status === 'extracting') {
        // Show extracting indicator
        LoadingProgress()
          .width(32)
          .height(32)
          .color('#4E66FF')
      } else {
        // Show toggle
        Toggle({ type: ToggleType.Switch, isOn: this.isLanguageEnabled(dict.languageCode) })
          .width(50)
          .height(28)
          .selectedColor('#4E66FF')
          .onChange(() => {
            this.toggleLanguage(dict.languageCode);
          })
      }
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

        Text('Языки')
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

      // Description
      Text('Выберите языки для клавиатуры. Словари для подсказок будут загружены автоматически.')
        .fontSize(14)
        .fontColor('#999999')
        .padding({ left: 16, right: 16, top: 12, bottom: 12 })
        .width('100%')
        .backgroundColor('#EBEEF0')

      // Languages list
      if (this.isLoading) {
        Column() {
          LoadingProgress()
            .width(48)
            .height(48)
            .color('#4E66FF')
        }
        .width('100%')
        .layoutWeight(1)
        .justifyContent(FlexAlign.Center)
      } else {
        Scroll() {
          Column() {
            // Enabled languages section
            if (this.enabledLanguages.length > 0) {
              Text('Включённые языки')
                .fontSize(13)
                .fontWeight(500)
                .fontColor('#999999')
                .padding({ left: 16, right: 16, top: 16, bottom: 8 })
                .width('100%')

              ForEach(
                this.dictionaries.filter(d => this.enabledLanguages.includes(d.languageCode)),
                (dict: DictionaryInfo) => {
                  this.buildLanguageItem(dict)
                  Divider()
                    .color('#E0E0E0')
                    .margin({ left: 16 })
                }
              )
            }

            // Available languages section
            Text('Доступные языки')
              .fontSize(13)
              .fontWeight(500)
              .fontColor('#999999')
              .padding({ left: 16, right: 16, top: 16, bottom: 8 })
              .width('100%')

            ForEach(
              this.dictionaries.filter(d => !this.enabledLanguages.includes(d.languageCode)),
              (dict: DictionaryInfo) => {
                this.buildLanguageItem(dict)
                Divider()
                  .color('#E0E0E0')
                  .margin({ left: 16 })
              }
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
    }
    .width('100%')
    .height('100%')
    .backgroundColor('#EBEEF0')
  }
}
