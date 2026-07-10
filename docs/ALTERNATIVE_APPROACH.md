# Альтернативный подход: Индекс слов

## Проблема

LOUDS навигация сложна для двухуровневой структуры с разбросанными токенами.

## Решение

**Предвычислить все слова из trie один раз и сохранить индекс.**

### Алгоритм

1. **Offline обработка** (один раз):
   ```python
   # DFS по всему trie
   # Для каждого пути до листа:
   #   Собрать токены → слово
   #   Сохранить: word → [node_ids]
   ```

2. **Runtime**:
   - Загрузить индекс слов
   - Prefix matching по отсортированному списку
   - O(log N) поиск

### Преимущества

1. **Простота**: Нет сложной LOUDS навигации
2. **Скорость**: O(log N) вместо O(depth) с Select/Rank
3. **Память**: ~2MB для 100K слов (приемлемо)

### Недостатки

1. **Время загрузки**: Нужно построить индекс при первом запуске
2. **Не generic**: Привязано к конкретной структуре

## Реализация

```cpp
class TrieIndex {
  std::vector<std::string> words_;
  std::vector<std::pair<uint32_t, uint32_t>> node_ranges_; // [start, end] node IDs
  
public:
  Status Build(const uint8_t* trie_data, size_t size);
  
  // Binary search for prefix
  std::vector<std::string> FindCompletions(const std::string& prefix, int max_count);
};
```

## Итог

Для HOSKEY можно:
1. Использовать `extracted_words.txt` как fallback (уже работает)
2. Добавить `TrieIndex` для быстрого prefix matching (оптимизация)
3. Оставить LOUDS навигацию для Neural LM (когда разберемся)
