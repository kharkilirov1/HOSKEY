# Гипотеза: Trie как Subword Vocabulary для Neural LM

## Наблюдения

### 1. ML модели рядом с trie
```
neural_model          42.6 MB  - MindSpore/ONNX neural model
neural_model.ms        4.9 MB  - MindSpore Lite model  
KEYBOARD-13321_nnlm_model 42.7 MB - Neural Language Model
trie                   4.3 MB  - Our target
```

### 2. Структура trie
- ~402K узлов с has_child
- ~362K labels
- Двухуровневая структура (main + sub)

### 3. Labels distribution
- Значения 0-25: частые (похоже на base tokens)
- Значения 26-255: редкие (extended tokens)

## Гипотеза

**Trie хранит subword токены (BPE-like), а не целые слова.**

Архитектура:
```
User Input → Trie (prefix matching) → Neural LM → Generated words
                ↓
         Subword tokens → Token IDs → Neural network → Text
```

Пример:
```
Слово "привет" в trie:
- Не хранится как целое!
- Хранится как: ["при", "вет"] или ["п", "ри", "ве", "т"]
- Neural LM комбинирует токены в слова
```

## Почему это имеет смысл

1. **Экономия памяти**: 4.3MB vs сотни MB для полного словаря
2. **Генерация новых слов**: Нейросеть может составить слова не в trie
3. **Морфология**: Обработка окончаний как отдельных токенов
4. **Современный подход**: GPT, BERT используют BPE/subword

## Последствия для HOSKEY

### Что нам нужно
1. **Не извлекать все слова** - их там может и нет!
2. **Использовать trie как prefix filter**:
   ```cpp
   // Проверить что префикс валиден
   bool IsValidPrefix(const string& prefix);
   
   // Получить возможные продолжения
   vector<string> GetCompletions(const string& prefix);
   ```
3. **Интеграция с ML Ranker**:
   ```cpp
   // Получить кандидатов из trie
   // Поранжировать через neural model
   ```

### Fallback стратегия
Пока нет ML модели:
- Использовать `extracted_words.txt` (108K слов)
- Это "замороженный" набор слов из старой версии

## Следующие шаги

1. **Проверить размер vocab в neural_model**
   - Если vocab size ≈ 362K (labels count) → подтверждается гипотеза

2. **Понять структуру main/sub trie**
   - Main: первые буквы/токены?
   - Sub: продолжения?

3. **Реализовать prefix matching** (не word extraction)
   - Найти все пути от корня до листьев
   - Это и есть допустимые token sequences
