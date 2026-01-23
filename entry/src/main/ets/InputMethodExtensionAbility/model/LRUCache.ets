// LRU Cache for prefix completion results
// Optimizes repeated lookups with O(1) get/set operations

interface CacheNode<K, V> {
  key: K;
  value: V;
  prev: CacheNode<K, V> | null;
  next: CacheNode<K, V> | null;
}

export class LRUCache<K, V> {
  private capacity: number;
  private cache: Map<K, CacheNode<K, V>>;
  private head: CacheNode<K, V> | null;
  private tail: CacheNode<K, V> | null;
  private size: number;

  constructor(capacity: number) {
    this.capacity = capacity;
    this.cache = new Map();
    this.head = null;
    this.tail = null;
    this.size = 0;
  }

  get(key: K): V | null {
    const node = this.cache.get(key);
    if (!node) return null;

    // Move to front (most recently used)
    this.moveToFront(node);
    return node.value;
  }

  set(key: K, value: V): void {
    const existing = this.cache.get(key);

    if (existing) {
      // Update existing node
      existing.value = value;
      this.moveToFront(existing);
      return;
    }

    // Create new node
    const newNode: CacheNode<K, V> = {
      key: key,
      value: value,
      prev: null,
      next: null
    };

    this.cache.set(key, newNode);
    this.addToFront(newNode);
    this.size++;

    // Evict LRU if over capacity
    if (this.size > this.capacity) {
      this.removeLRU();
    }
  }

  clear(): void {
    this.cache.clear();
    this.head = null;
    this.tail = null;
    this.size = 0;
  }

  private moveToFront(node: CacheNode<K, V>): void {
    // Remove from current position
    if (node.prev) {
      node.prev.next = node.next;
    } else {
      this.head = node.next;
    }

    if (node.next) {
      node.next.prev = node.prev;
    } else {
      this.tail = node.prev;
    }

    // Add to front
    this.addToFront(node);
  }

  private addToFront(node: CacheNode<K, V>): void {
    node.prev = null;
    node.next = this.head;

    if (this.head) {
      this.head.prev = node;
    }

    this.head = node;

    if (!this.tail) {
      this.tail = node;
    }
  }

  private removeLRU(): void {
    if (!this.tail) return;

    const lru = this.tail;
    this.cache.delete(lru.key);

    if (lru.prev) {
      lru.prev.next = null;
      this.tail = lru.prev;
    } else {
      this.head = null;
      this.tail = null;
    }

    this.size--;
  }

  getSize(): number {
    return this.size;
  }

  getCapacity(): number {
    return this.capacity;
  }
}
