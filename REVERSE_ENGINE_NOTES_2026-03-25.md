# HOSKEY reverse notes — 2026-03-25

## Что подтверждено

### 1. JNI entrypoints в `libjni_ykeyboard3.so`
Подтверждены строками из бинаря и PyGhidra:

- `Java_ru_yandex_androidkeyboard_nativecode_Native_00024BinaryDictionary_open`
- `Java_ru_yandex_androidkeyboard_nativecode_Native_00024BinaryDictionary_close`
- `Java_ru_yandex_androidkeyboard_nativecode_Native_00024BinaryDictionary_checkWord`
- `Java_ru_yandex_androidkeyboard_nativecode_Native_00024BinaryDictionary_checkAbbreviation`
- `Java_ru_yandex_androidkeyboard_nativecode_Native_00024BinaryDictionary_addPersonalNGramCount`
- `Java_ru_yandex_androidkeyboard_nativecode_Native_00024BinaryDictionary_addPersonalShortcut`
- `Java_ru_yandex_androidkeyboard_nativecode_Native_00024BinaryDictionary_updatePersonalDictionary`
- `Java_ru_yandex_androidkeyboard_nativecode_Native_00024BinaryDictionary_savePersonalDictionary`
- `Java_ru_yandex_androidkeyboard_nativecode_Native_00024BinaryDictionary_prunePersonalDictionary`
- `Java_ru_yandex_androidkeyboard_nativecode_Native_00024BinaryDictionary_getPersonalDictionaryStats`
- `Java_ru_yandex_androidkeyboard_nativecode_Native_00024BinaryDictionary_updatePersonalBlacklist`
- `Java_ru_yandex_androidkeyboard_nativecode_Native_00024BinaryDictionary_updatePersonalAutocorrectBlocker`
- `Java_ru_yandex_androidkeyboard_nativecode_Native_00024BinaryDictionary_getSwipeHistory`
- `Java_ru_yandex_androidkeyboard_nativecode_Native_00024DictionaryFacilitator_getSuggestions`
- `Java_ru_yandex_androidkeyboard_nativecode_Native_00024DictionaryFacilitator_getEmojis`
- `Java_ru_yandex_androidkeyboard_nativecode_Native_00024DictionaryFacilitator_newVoiceSession`
- `Java_ru_yandex_androidkeyboard_nativecode_Native_00024DictionaryFacilitator_postProcessSpeechToText`
- `Java_ru_yandex_androidkeyboard_nativecode_Native_00024ProximityInfo_create`
- `Java_ru_yandex_androidkeyboard_nativecode_Native_00024ProximityInfo_release`
- `Java_ru_yandex_androidkeyboard_nativecode_Native_00024ProximityInfo_detectHitKey`
- `Java_ru_yandex_androidkeyboard_nativecode_Native_00024ProximityInfo_detectHitKeyWithDictionary`
- `Java_ru_yandex_androidkeyboard_nativecode_Native_00024ProximityInfo_describeDynamicLayout`
- `Java_ru_yandex_androidkeyboard_nativecode_Native_00024NgramDistributionTracker_open`
- `Java_ru_yandex_androidkeyboard_nativecode_Native_00024NgramDistributionTracker_close`
- `Java_ru_yandex_androidkeyboard_nativecode_Native_00024NgramDistributionTracker_tryTakeState`
- `Java_ru_yandex_androidkeyboard_nativecode_Native_00024NgramDistributionTracker_saveState`
- `Java_ru_yandex_androidkeyboard_nativecode_Native_00024InputModel_update`
- `Java_ru_yandex_androidkeyboard_nativecode_Native_00024SuggestEvent_notify`

### 2. JNI_OnLoad ожидает Java/JNI окружение
Из PyGhidra decompile (`JNI_OnLoad @ 0x00352c26`) подтверждено:

- получает `JNIEnv` через `JavaVM->GetEnv(..., 0x10004)`
- использует `java/lang/ClassLoader`
- использует `java/lang/Thread`
- вызывает `Thread.currentThread()`
- вызывает `thread.getContextClassLoader()`
- вызывает `ClassLoader.loadClass(String)`
- создаёт и хранит global refs
- имеет exception-handling helper, который вызывает `getMessage()` у exception

Подтверждённые строки из бинаря:

- `java/lang/Thread`
- `java/lang/ClassLoader`
- `loadClass`
- `()Ljava/lang/Thread;`
- `()Ljava/lang/ClassLoader;`
- `(Ljava/lang/String;)Ljava/lang/Class;`

### 3. Deobfuscated Java wrappers уже дают high-level контракт
Есть артефакты:

- `C:\Users\Kharki\Desktop\yandex_extracted\sources\Native_BinaryDictionary.java`
- `C:\Users\Kharki\Desktop\yandex_extracted\sources\Native_DictionaryFacilitator.java`
- `C:\Users\Kharki\Desktop\yandex_extracted\deobfuscated\suggest\README.md`
- `...\SuggestRequest.java`
- `...\SuggestResponse.java`
- `...\DictionaryOpenParams.java`

Это означает, что blind reverse больше не нужен как основной путь: внешний API уже реконструирован.

## Подтверждённый protobuf-контракт

### SuggestRequest
Подтверждены поля:

1. `dictionarySessions`
2. `locale`
3. `proximityInfo`
4. `points`
5. `input`
6. `prevWordsInfo`
7. `optionsArray`
8. `emojiFactor`
9. `packageName`
10. `isUrgent`
11. `fullContext`
12. `fieldActionId`
13. `gender`
14. `age`

### SuggestResponse
Подтверждены поля:

1. `suggestions`
2. `willAutocorrect`
3. `autocorrectFeatures`
4. `emojis`
5. `isBlocked`

### DictionaryOpenParams
Критичные поля:

- `sourceDir`
- `dictOffset`
- `dictSize`
- `isUpdatable`
- `language`
- `tapModelEffect`
- `personalizationEffect`
- `enableGeometricFeatures`
- `enableRemotePredictor`
- `enableAdditionalForms`
- `emojiModelFilename`
- `tapModelBundle`

## Что видно по архитектуре движка
Строки из бинаря подтверждают несколько predictor-подсистем:

- `Predictor/Ngram`
- `Predictor/Neural`
- `Predictor/Remote`
- `Predictor/Parallel`
- `Predictor/Autocorrect`
- `Predictor/EmojiSearch`
- `Predictor/EmojiPrefixSuggest`
- `NgramPredictor`
- `NeuralPredictor`
- `RemotePredictor`
- `ParallelPredictor`
- `PrecomputedPredictor`

Практический вывод: внутри не «один словарь», а pipeline из n-gram + neural + remote + score fusion.

## Состояние HarmonyOS-интеграции
По device diaglog подтверждено:

- ArkTS → NAPI мост работает
- `engine.init()` заходит в C++
- `fake JNIEnv` создаётся
- загрузка падает раньше `JNI_OnLoad` на `dlopen(libjni_ykeyboard3.so)`
- текущий blocker: `__android_log_write: symbol not found`

Следствие: пока основной runtime blocker — loader/symbol resolution, а не protobuf и не словари.

## Сопоставление с текущим shim
В `entry/src/main/cpp/jni_shim/fake_jnienv.cpp` уже покрыты базовые вызовы для `JNI_OnLoad`:

- `GetEnv`
- `FindClass`
- `GetMethodID`
- `GetStaticMethodID`
- `CallStaticObjectMethod`
- `CallObjectMethod`
- `ExceptionOccurred`
- `ExceptionClear`
- `NewGlobalRef`
- `DeleteGlobalRef`
- `DeleteLocalRef`

Это соответствует уже подтверждённой части `JNI_OnLoad`.

Но есть важное замечание:
- в `android_shim.cpp` дублируются `__android_log_*`, `__system_property_get`, `android_set_abort_message`
- те же символы экспортируются отдельным `liblog.so`
- это потенциальная зона конфликтов после устранения текущего `dlopen` blocker

## Практический вывод

### Уже можно делать без полного реверса
1. Стабилизировать загрузку `.so`
2. Работать по deobfuscated protobuf-контракту
3. Воспроизводить `open/getSuggestions/checkWord` своим host-кодом
4. Постепенно заменять native black-box своими модулями

### Что ещё имеет смысл reverse’ить точечно
1. минимальный обязательный JNI contract после `JNI_OnLoad`
2. parse/serialize path у `getSuggestions`
3. что из `DictionaryOpenParams` реально обязательно для `main_ru`
4. какие predictor flags можно выключить без деградации загрузки

## Артефакты reverse

- `C:\Users\Kharki\ghidra_yandex\YKB.rep\hoskey_reverse_targets_pyghidra.txt`
- `C:\Users\Kharki\ghidra_yandex\YKB.rep\hoskey_helpers_analysis.txt`
- `C:\Users\Kharki\ghidra_scripts\reverse_hoskey_pyghidra.py`
- `C:\Users\Kharki\ghidra_scripts\reverse_hoskey_helpers.py`

## Следующий логичный шаг
С инженерной точки зрения приоритет сейчас такой:

1. починить preload/visibility `liblog.so` и убрать ambiguity вокруг Android log symbols
2. после успешного `dlopen` снять новый device log
3. если библиотека дойдёт до `JNI_OnLoad`, проверить следующий реальный missing contract вместо гадания
4. параллельно готовить свой host-side protobuf contract по deobfuscated Java wrappers
