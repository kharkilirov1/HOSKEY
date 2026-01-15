# 🤝 Contributing to OpenBoard

Thank you for your interest in contributing to OpenBoard! Your efforts are vital in making OpenBoard the best open-source keyboard for HarmonyOS. This document provides guidelines and information for developers and community members looking to contribute.

---

## ✍️ How to Contribute

### Reporting Issues

Before creating a bug report, please:

1. **Search existing issues** on the GitHub repository to avoid duplicates.
2. **Use a clear, descriptive title** that briefly summarizes the problem (e.g., "Backspace key is unresponsive in Persian layout").
3. Provide detailed information including:
   - Steps to reproduce the issue (e.g., the exact keys pressed, or context).
   - Expected vs. actual behavior.
   - HarmonyOS Version (e.g., HarmonyOS 6.0).
   - Device Model (e.g., Huawei Pura 80 Ultra).
   - Screenshots or **DevEco Studio logcat** error messages (if applicable).

### Suggesting Features

We welcome ideas for new languages, themes, and functionalities! Please:

1. **Check if the feature already exists** or is on the Roadmap in the `README.md`.
2. Create a **detailed proposal** including:
   - The user benefit and use case (e.g., why is this feature needed?).
   - Proposed implementation approach (e.g., "Add a new key action to `KeyAction` enum for quick theme switching").
   - Any potential architectural challenges related to **ArkTS** or the **HarmonyOS IME Kit**.

---

## 💻 Code Contributions (ArkTS/HarmonyOS)

### Getting Started

1. **Fork the repository**:
   ```bash
   git clone https://github.com/Open-Tech-Project/OpenBoard.git
   cd OpenBoard
2. **Set up the development environment**:
Open the project folder in DevEco Studio (6.0.1 or later).
Ensure your HarmonyOS SDK (API 9+) is installed.

3. **Create a feature branch**:
```bash
  git checkout -b feature/your-feature-name
```
**Development Guidelines**
Code Style (ArkTS)
* Follow the TypeScript/ArkTS style guides.
* Use Signals (@State, @Prop, @Link, etc.) for reactive state management within the component structure.
* Ensure explicit typing is used, especially for exported models and component properties.
* Maintain consistent naming conventions:
  * PascalCase for classes/components.
  * camelCase for variables/functions.

 **Project structure focus**
| Directory/File                              | Responsibility                                           | Contribution Focus                                                                                                   |
| ------------------------------------------- | -------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------- |
| `entry/src/main/ets/model/`                 | Core data models, layouts, prediction logic, and theme.  | Additions of new languages, improving prediction logic in `predictionModel.ets`.                                     |
| `entry/src/main/ets/components/`            | Reusable UI elements for keys, popups, and toolbars.     | UI refinement, improving key visualization, developing new toolbar elements.                                         |
| `entry/src/main/ets/inputmethodservice.ets` | Main controller communicating with the system's IME API. | Handling new key actions, managing IME context, or implementing system integration features (like clipboard access). |
| `entry/src/main/ets/pages/Index.ets`        | The main keyboard component (@Component).                | Overall layout adjustments, component wiring, gesture handling.                                                      |

**Adding New Languages**
To add support for a new language:

1. Create the full layout definition (including main, shift, and long-press characters) in model/KeyboardKeyData.ets.

2. Update the LanguageCode type to include the new language code (e.g., 'de' for German).

3. Implement basic word lists and prediction logic for the new language in model/PredictionModel.ets.
---

**Pull Request Process**

1. Ensure your code is clean and tested:
  * Run static analysis in DevEco Studio (Build > Analyze > Code Analysis).
  * Manually test your changes on a real HarmonyOS device or emulator.

1. Create a meaningful commit message:
   - Example:
     ```batch
     feat: Implement basic German QWERTZ layout
     - Added 'de' language code to `KeyData` and `LanguageController`.
     - Defined initial German QWERTZ layout in `KeyboardKeyData.ets`.
     - Updated language switching logic to cycle through 'de'.
     Closes #42
     ```

1. Push your changes:
   ```batch
   git push origin feature/your-feature-name
   ```

1. Create a Pull Request (PR):
  * Use a clear, descriptive title.

  * Reference related issues with Closes #issue-number.

  * Provide a detailed description of changes, including any specific HarmonyOS API usage.

  * Include screenshots or a short video demonstrating UI changes.
---

## 🧪 Testing

Due to the nature of Input Method Extensions in HarmonyOS, **manual testing** on a device is crucial.

### Testing Process

1. **Build and Deploy**: Deploy the built HAP package to your target HarmonyOS device using **DevEco Studio**.

2. **IME Activation**: Ensure OpenBoard is selected as the default keyboard in:
(Settings > System & updates > Language & input > Virtual keyboard).

3. **Functional Test**: Test all key elements:
- Character input (main and shift states)
- Long-press popups for extended characters
- Language switching (swipe gesture or globe key)
- Prediction suggestions (typing boundaries and word completions)

### Writing Unit Tests (If applicable)

For pure ArkTS/TS logic (like `PredictionModel.ets` or utility functions), you can write unit tests:

```typescript
// Example unit test for prediction logic
import { predictionModel } from '../model/PredictionModel';
import { expect } from '@ohos/hypium'; // Use the HarmonyOS testing framework

describe('PredictionModel', () => {
it('should return relevant suggestions for a given language', () => {
 predictionModel.recordWord('en', null, 'hello');
 const suggestions = predictionModel.getSuggestions('en', 'he');
 expect(suggestions).toContain('hello');
});

// Test the word boundary detection logic (if moved to a utility class)
it('should recognize common punctuation as word boundaries', () => {
 // ... test boundary functions
});
});
```
---
## 🔒 Security and Privacy

As a keyboard app, **security** is paramount.

- **Secrets**: Never commit any secret keys or sensitive configuration details to the repository.
- **Permissions**: Justify every permission requested in the `module.json5` file. Avoid requesting unnecessary permissions.
- **Data**: OpenBoard operates entirely **client-side**. Do not introduce any external tracking or data collection without explicit community consensus and detailed documentation.

---

## 🏆 Recognition

Contributors who make significant changes, fix critical bugs, or successfully implement new languages will be recognized in:

- **GitHub contributors page**
- **Release notes**

Thank you for being part of the **OpenTech** community and helping to build the future of input on **HarmonyOS**! 🎉
