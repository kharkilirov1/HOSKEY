# HOSKEY

[![HarmonyOS](https://img.shields.io/badge/HarmonyOS-5.0+-red)](https://developer.huawei.com/consumer/en/harmonyos/)
[![ArkTS](https://img.shields.io/badge/ArkTS-5.0-blue)](https://developer.huawei.com/consumer/en/arkts/)
[![License](https://img.shields.io/badge/License-AGPL--3.0-green)](#лицензия)

**HOSKEY** — клавиатура для HarmonyOS с нативным движком предсказаний на C++.

## Возможности

- **Нативный движок предсказаний** — C++ Trie с OpenBoard словарями (200k+ слов)
- **20 языков** — словари для bg, bn, da, de, el, en, eo, es, fr, hu, hy, it, nl, pl, pt_br, pt_pt, ro, ru, sv, tr
- **6 раскладок** — English, Русский, Polski, Italiano, العربية, فارسی
- **Голосовой ввод** — распознавание речи
- **Буфер обмена** — быстрая вставка
- **Эмодзи** — панель с категориями

## Установка

### Требования
- HarmonyOS 5.0+
- DevEco Studio 5.0+ (для разработки)

### Сборка
```bash
git clone https://github.com/user/HOSKEY.git
cd HOSKEY
# Открыть в DevEco Studio и собрать
```

### Настройка клавиатуры

1. **Установите HAP** на устройство
2. Откройте **Настройки** → **Система** → **Клавиатура**
3. **Включите HOSKEY** в списке клавиатур
4. **Выберите HOSKEY** как клавиатуру по умолчанию
5. Откройте любое приложение с текстовым полем
6. Нажмите на поле ввода — появится клавиатура

### Переключение языков
- **Долгое нажатие на пробел** — меню выбора языка
- **Свайп по пробелу** — быстрое переключение

## Архитектура

```
entry/src/main/
├── ets/
│   ├── InputMethodExtensionAbility/  # IME сервис
│   │   ├── model/
│   │   │   ├── NativeDictionary.ets  # NAPI обёртка для C++
│   │   │   ├── OptimizedPredictionModel.ets  # Движок предсказаний
│   │   │   └── KeyboardController.ets  # Контроллер панели
│   │   └── pages/
│   │       └── Index.ets  # UI клавиатуры
│   └── pages/
│       └── SetupGuidePage.ets  # Страница настройки
├── cpp/
│   ├── napi_init.cpp  # NAPI точка входа
│   └── dictionary_hoskey/  # C++ Trie движок
└── resources/
    └── rawfile/
        └── main_*.dict  # Бинарные словари
```

## Словари

| Язык | Файл | Размер |
|------|------|--------|
| Русский | main_ru.dict | 2.2 MB |
| English | main_en.dict | 2.9 MB |
| Deutsch | main_de.dict | 1.6 MB |
| Español | main_es.dict | 1.4 MB |
| Français | main_fr.dict | 1.3 MB |
| Italiano | main_it.dict | 1.1 MB |
| Polski | main_pl.dict | 1.2 MB |
| ... | ... | ... |

**Итого:** 20 словарей, ~35 MB

## Разработка

### Добавление нового языка

1. Добавить словарь `main_XX.dict` в `resources/rawfile/`
2. Добавить раскладку в `KeyboardKeyData.ets`
3. Добавить код языка в `LanguageCode` тип
4. Добавить в `ALLOWED_LANGUAGES`

### Логи для отладки

```bash
hdc shell hilog | grep -E "HOSKEY|NativeDictionary"
```

## Лицензия

AGPL-3.0 — см. [LICENSE.md](LICENSE.md)

---

*HOSKEY — HarmonyOS Keyboard*
