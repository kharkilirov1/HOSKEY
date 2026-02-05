import { OpenBoardTheme } from '../InputMethodExtensionAbility/model/Theme';
import router from '@ohos.router';

@Entry
@Component
struct SetupGuidePage {
  @State testText: string = '';
  @State isKeyboardActive: boolean = false;

  build() {
    Column() {
      // Header
      Text('HOSKEY')
        .fontSize(32)
        .fontWeight(FontWeight.Bold)
        .fontColor(OpenBoardTheme.primaryText)
        .margin({ top: 40, bottom: 8 })

      Text('Клавиатура для HarmonyOS')
        .fontSize(16)
        .fontColor(OpenBoardTheme.secondaryText)
        .margin({ bottom: 32 })

      // Setup Instructions
      Column({ space: 16 }) {
        Text('Настройка клавиатуры')
          .fontSize(20)
          .fontWeight(FontWeight.Medium)
          .fontColor(OpenBoardTheme.primaryText)
          .alignSelf(ItemAlign.Start)

        this.StepItem('1', 'Откройте Настройки → Система → Клавиатура')
        this.StepItem('2', 'Включите HOSKEY в списке клавиатур')
        this.StepItem('3', 'Выберите HOSKEY как клавиатуру по умолчанию')
        this.StepItem('4', 'Вернитесь сюда и нажмите на поле ввода ниже')
      }
      .padding(20)
      .backgroundColor('#1A1A1A')
      .borderRadius(16)
      .width('90%')

      // Test Input Field
      Column({ space: 12 }) {
        Text('Тестовое поле ввода')
          .fontSize(18)
          .fontWeight(FontWeight.Medium)
          .fontColor(OpenBoardTheme.primaryText)
          .alignSelf(ItemAlign.Start)

        TextArea({ text: this.testText, placeholder: 'Нажмите здесь, чтобы проверить клавиатуру...' })
          .onChange((value: string) => {
            this.testText = value;
          })
          .onFocus(() => {
            this.isKeyboardActive = true;
          })
          .onBlur(() => {
            this.isKeyboardActive = false;
          })
          .height(120)
          .fontSize(16)
          .fontColor(OpenBoardTheme.primaryText)
          .backgroundColor('#2A2A2A')
          .borderRadius(12)
          .padding(12)

        if (this.isKeyboardActive) {
          Text('Клавиатура активна')
            .fontSize(14)
            .fontColor('#4CAF50')
        }

        if (this.testText.length > 0) {
          Row({ space: 8 }) {
            Text(`Символов: ${this.testText.length}`)
              .fontSize(14)
              .fontColor(OpenBoardTheme.secondaryText)

            Button('Очистить')
              .fontSize(14)
              .height(32)
              .backgroundColor('#333')
              .onClick(() => {
                this.testText = '';
              })
          }
        }
      }
      .padding(20)
      .backgroundColor('#1A1A1A')
      .borderRadius(16)
      .width('90%')
      .margin({ top: 24 })

      // Tips
      Column({ space: 8 }) {
        Text('Подсказки')
          .fontSize(16)
          .fontWeight(FontWeight.Medium)
          .fontColor(OpenBoardTheme.primaryText)
          .alignSelf(ItemAlign.Start)

        Text('• Долгое нажатие на пробел — выбор языка')
          .fontSize(14)
          .fontColor(OpenBoardTheme.secondaryText)
        Text('• Свайп по пробелу — быстрое переключение языка')
          .fontSize(14)
          .fontColor(OpenBoardTheme.secondaryText)
        Text('• Свайп влево по Backspace — удалить всё')
          .fontSize(14)
          .fontColor(OpenBoardTheme.secondaryText)
      }
      .padding(20)
      .backgroundColor('#1A1A1A')
      .borderRadius(16)
      .width('90%')
      .margin({ top: 24 })

      // Settings button
      Button('⚙️ Настройки')
        .fontSize(16)
        .fontWeight(FontWeight.Medium)
        .fontColor('#FFFFFF')
        .backgroundColor('#4E66FF')
        .borderRadius(12)
        .width('90%')
        .height(50)
        .margin({ top: 24 })
        .onClick(() => {
          router.pushUrl({ url: 'pages/SettingsPage' });
        })

      Blank()

      // Version
      Text('v1.0.0 • NativeDictionary Engine')
        .fontSize(12)
        .fontColor(OpenBoardTheme.secondaryText)
        .margin({ bottom: 20 })
    }
    .width('100%')
    .height('100%')
    .backgroundColor(OpenBoardTheme.background)
  }

  @Builder
  StepItem(number: string, text: string) {
    Row({ space: 12 }) {
      Text(number)
        .fontSize(16)
        .fontWeight(FontWeight.Bold)
        .fontColor('#FFFFFF')
        .textAlign(TextAlign.Center)
        .width(28)
        .height(28)
        .backgroundColor('#007AFF')
        .borderRadius(14)

      Text(text)
        .fontSize(15)
        .fontColor(OpenBoardTheme.primaryText)
        .layoutWeight(1)
    }
    .width('100%')
  }
}
