# 1nc7 Decoder - Прорыв в понимании

## Найденная структура

### Bitvector комбинации
```
BV0 @ 130744: pop=369743 (main louds?)
BV1 @ 292744: pop=369743 (main has_child?)
BV2 @ 448936: pop=801071 (sub louds)
BV3 @ 605128: pop=402824 (sub has_child?)
```

### Ключевое открытие
**BV2 (sub louds) + BV1 (main has_child) даёт лучшую корреляцию!**

```
Position 1 → node 0, has_child[0]=1  ✓ (root has children)
Position 2 → node 1, has_child[1]=0  ✓ (node 1 is leaf)
Position 4 → node 2, has_child[2]=1  ✓ (node 2 has children)
Position 5 → node 3, has_child[3]=1  ✓ (node 3 has children)
```

### LOUDS структура (первые 32 бита)
```
Bits: 01101110010001011010000000000000
      || |||  |   | |||
      01 234  5   6 789...
      
Pos 1: node 0 start
Pos 2: node 1 start (child of 0)
Pos 3: 0 (end of node 0's children)
Pos 4: node 2 start (child of node 1)
Pos 5: node 3 start (sibling of 2)
Pos 6: node 4 start (sibling of 3)
Pos 7: 0 (end of node 1's children)
```

### Проблема
**has_child[1] = 0 но node 1 имеет детей (nodes 2,3,4)!**

Возможные объяснения:
1. has_child относится к другому уровню (main vs sub)
2. has_child[i] соответствует позиции i, не node ID
3. Двухуровневая структура: main→sub transition

### Рабочая гипотеза
```
Main trie (BV0+BV1): первый уровень (корень → первые буквы)
Sub trie (BV2+BV1): второй уровень (продолжения)

ИЛИ

Sub trie использует has_child из main trie!
```

### Следующий шаг
Реализовать навигацию без has_child:
- Использовать только LOUDS для поиска детей
- has_child использовать только как оптимизацию/подсказку
- Если LOUDS говорит есть дети, но has_child=0 - доверять LOUDS

### Тесты
- Префикс с label=0 (байт 0) - VALID ✓
- Root имеет 1 ребенка с label=0
- Node 1 имеет 3 ребенка (labels 0, 106, 1)
- И так далее...
