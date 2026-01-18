<img src="https://github.com/Open-Tech-Project/OpenTube_readme/blob/main/Group%20335.png" align="left" width="60" height="60" alt="Project Logo">
# OpenBoard

[![HarmonyOS](https://img.shields.io/badge/HarmonyOS-6.0-red)](https://consumer.huawei.com/cn/harmonyos-6/)
[![ArkTS](https://img.shields.io/badge/ArkTS-Language-blue)](https://developer.harmonyos.com/en/docs/documentation/doc-guides/arkts-get-started-0000001774119986)
[![AppGallery](https://img.shields.io/badge/Available%20on-AppGallery-orange)](https://appgallery.huawei.com/app/detail?id=com.huawei.hmsapp.appgallery&channelId=SHARE&source=appshare)
[![License](https://img.shields.io/badge/License-Open%20Source-green)](#-license)

OpenBoard is a modern, open-source keyboard application designed specifically for **HarmonyOS 6.0**. Built with ArkTS, OpenBoard delivers a seamless typing experience with advanced features and multi-language support, available exclusively on Huawei AppGallery.

## 🌟 Features

- **Multi-Language Support**: Full keyboard layouts for Polish, English, Italian, and Persian
- **Smart Word Prediction**: Context-aware suggestions in all supported languages
- **Voice Typing**: Speech-to-text functionality across all languages
- **Clipboard Integration**: Quick access to paste recently copied items
- **Emoji Panel**: Extensive emoji collection with categorized browsing
- **Smart Toolbars**: Context-aware interface that adapts to your typing needs
- **Gesture Support**: Swipe gestures for language switching and navigation
- **Customizable Themes**: Beautiful, responsive design with theme support

## 📱 Supported Languages

| Language | Status | Layout | Prediction |
|----------|--------|--------|------------|
| English | ✅ Full Support | QWERTY | ✅ Available |
| Polish | ✅ Full Support | QWERTY | ✅ Available |
| Italian | ✅ Full Support | QWERTY | ✅ Available |
| Persian | ✅ Full Support | فارسی | ✅ Available |
## 🚀 Quick Start

### Prerequisites

- **HarmonyOS 6.0** or later
- **DevEco Studio 6.0.1 or later** (for development)
- **ArkTS** development language

### Installation

1. **Download from AppGallery**
   - [Visit Huawei Harmony os AppGallery](https://appgallery.huawei.com/app/detail?id=com.huawei.hmsapp.appgallery&channelId=SHARE&source=appshare)
   - Search for "OpenBoard"
   - Install the application

2. **Enable OpenBoard as Default Keyboard**
   - Go to **Settings** > **System & updates** > **Language & input** > **Virtual keyboard**
   - Select **OpenBoard** as your default keyboard
   - Grant necessary permissions for voice typing and clipboard access

## 🛠️ Architecture

OpenBoard is built using a modular architecture with the following key components:

- **Keyboard Controller**: Service comunicating between the keyboard and system (`inputMethodService.ets`)
- **Keyboard Controller**: Database of keyboard layouts (`keyboardKeyData.ets`)
- **Prediction Engine**: Smart word suggestions (`predictionModel.ets`)
- **Layout System**: Dynamic keyboard layouts for all languages
- **UI Components**: ArkTS-based responsive interface
- **Input Method Extension**: HarmonyOS IME integration

### Key Components
```typescript
// Core keyboard structure
- KeyboardKeyData (Alphabet, Symbols, special keys)
- Multi-language layout definitions
- Prediction model integration
```
## 🎯 Smart Toolbars

OpenBoard features intelligent toolbars that adapt to your usage context:

1. **Standard Typing Toolbar**
   - Layout: `[Close] [Suggestion 1 | Suggestion 2 | Suggestion 3]`
   - Active during regular typing
   - Shows word predictions

1. **Pre-Typing Toolbar**
   - Layout: `[Close] [Clipboard]`
   - Appears after 5 seconds of inactivity
   - Quick access to clipboard

1. **Emoji Toolbar**
   - Layout: `[⌨ Return]`
   - Active in emoji selection mode
   - Easy return to main keyboard

1. **Voice Typing Toolbar**
   - Layout: `[Close] [🎤 Typing with voice...]`
   - Active during voice input sessions
   - Shows voice recognition status

## 🤝 Contributing

We welcome contributions from the HarmonyOS developer community! Please read our Contributing Guidelines before submitting pull requests.

### Development Setup

1. **Fork the repository**
   ```bash
   git clone https://github.com/Open-Tech-Project/OpenBoard.git
   cd OpenBoard
   ```

1. **Open in DevEco Studio**
   - Import project into DevEco Studio
   - Set up signing certificates

1. **Build and Test thoroughly**
   - Connect HarmonyOS device or emulator
   - Build project: Build > Build Project
   - Run on target device

1. **Adding New Languages**
   - Create layout definition in `model/KeyboardKeyData.ets`
   - Add language code to `LanguageCode` type
   - Implement prediction model integration
   - Update language switching logic

1. **Open a Pull Request**

## 📄 License

This project is licensed under the GNU Affero General Public License, Version 3 or later (AGPLv3+).

Copyright (C) 2025 The OpenBoard Project Authors

Key License Points

Free Software: You are free to run, study, share, and modify this software.

Full Text: The complete license is included in the file LICENSE in this repository.

No Warranty: This program is provided without any warranty.

AGPL Requirement: If you modify and run a version of OpenBoard as a public network service, you must prominently offer all users access to the Corresponding Source Code of your modified version.

---

Made with ❤️ by the OpenTech community






