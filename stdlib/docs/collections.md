# Hemlock Collections Module

A standard library module providing common data structures for Hemlock programs.

## Overview

The collections module provides the following data structures:

- **HashMap** - Hash table with key-value storage
- **Queue** - First-In-First-Out (FIFO) data structure
- **Stack** - Last-In-First-Out (LIFO) data structure
- **Set** - Collection of unique values
- **LinkedList** - Doubly-linked list with efficient insertion/deletion
- **LRUCache** - Least Recently Used cache with fixed capacity

## Usage

```hemlock
import { HashMap, Queue, Stack, Set, LinkedList, LRUCache } from "@stdlib/collections";
```

Or import all:

```hemlock
import * as collections from "@stdlib/collections";
let map = collections.HashMap();
```

---

## HashMap

Hash table implementation with separate chaining for collision resolution.

### API

```hemlock
let map = HashMap();
```

**Methods:**
- `map.set(key, value)` - Set a key-value pair
- `map.get(key)` - Get value for key (returns null if not found)
- `map.get_or_default(key, default_value)` - Get value for key, or return default if not found
- `map.has(key)` - Check if key exists (returns boolean)
- `map.delete(key)` - Remove key-value pair (returns boolean)
- `map.clear()` - Remove all entries
- `map.keys()` - Get array of all keys
- `map.values()` - Get array of all values
- `map.entries()` - Get array of [key, value] pairs
- `map.each(callback)` - Iterate over entries with callback(key, value)

**Properties:**
- `map.size` - Number of entries

**Supported key types:** string, integer types, float types, boolean, null

### Example

```hemlock
import { HashMap } from "@stdlib/collections";

let map = HashMap();

// Set values
map.set("name", "Alice");
map.set("age", 30);
map.set("active", true);

// Get values
print(map.get("name"));  // "Alice"
print(map.get("age"));   // 30

// Check existence
if (map.has("active")) {
    print("User is active");
}

// Delete
map.delete("age");

// Get all keys/values
let keys = map.keys();
let values = map.values();

print("Size: " + typeof(map.size));  // 2
```

---

## Queue

First-In-First-Out (FIFO) data structure.

### API

```hemlock
let q = Queue();
```

**Methods:**
- `q.enqueue(item)` - Add item to end of queue
- `q.dequeue()` - Remove and return item from front (throws if empty)
- `q.peek()` - Get front item without removing (returns null if empty)
- `q.is_empty()` - Check if queue is empty (returns boolean)
- `q.clear()` - Remove all items
- `q.to_array()` - Get copy of queue as array
- `q.each(callback)` - Iterate over items with callback(item, index)

**Properties:**
- `q.size` - Number of items in queue

### Example

```hemlock
import { Queue } from "@stdlib/collections";

let q = Queue();

// Enqueue items
q.enqueue("first");
q.enqueue("second");
q.enqueue("third");

// Peek at front
print(q.peek());  // "first"

// Dequeue items (FIFO order)
print(q.dequeue());  // "first"
print(q.dequeue());  // "second"
print(q.dequeue());  // "third"

// Check if empty
if (q.is_empty()) {
    print("Queue is empty");
}
```

---

## Stack

Last-In-First-Out (LIFO) data structure.

### API

```hemlock
let s = Stack();
```

**Methods:**
- `s.push(item)` - Add item to top of stack
- `s.pop()` - Remove and return item from top (throws if empty)
- `s.peek()` - Get top item without removing (returns null if empty)
- `s.is_empty()` - Check if stack is empty (returns boolean)
- `s.clear()` - Remove all items
- `s.to_array()` - Get copy of stack as array
- `s.each(callback)` - Iterate over items with callback(item, index)

**Properties:**
- `s.size` - Number of items in stack

### Example

```hemlock
import { Stack } from "@stdlib/collections";

let s = Stack();

// Push items
s.push(10);
s.push(20);
s.push(30);

// Peek at top
print(s.peek());  // 30

// Pop items (LIFO order)
print(s.pop());  // 30
print(s.pop());  // 20
print(s.pop());  // 10

// Check if empty
if (s.is_empty()) {
    print("Stack is empty");
}
```

---

## Set

Collection of unique values. Automatically prevents duplicates.

### API

```hemlock
let s = Set();
```

**Methods:**
- `s.add(value)` - Add value (returns true if added, false if duplicate)
- `s.delete(value)` - Remove value (returns boolean)
- `s.has(value)` - Check if value exists (returns boolean)
- `s.clear()` - Remove all values
- `s.values()` - Get copy of all values as array
- `s.union(other_set)` - Return new set with values from both sets
- `s.intersection(other_set)` - Return new set with common values
- `s.difference(other_set)` - Return new set with values in this but not other
- `s.each(callback)` - Iterate over values with callback(value, index)

**Properties:**
- `s.size` - Number of values in set

### Example

```hemlock
import { Set } from "@stdlib/collections";

let s = Set();

// Add values
s.add(10);
s.add(20);
s.add(10);  // Duplicate, ignored

print(s.size);  // 2

// Check membership
if (s.has(10)) {
    print("10 is in the set");
}

// Set operations
let s1 = Set();
s1.add(1);
s1.add(2);
s1.add(3);

let s2 = Set();
s2.add(2);
s2.add(3);
s2.add(4);

let union = s1.union(s2);         // {1, 2, 3, 4}
let inter = s1.intersection(s2);  // {2, 3}
let diff = s1.difference(s2);     // {1}
```

---

## LinkedList

Doubly-linked list with efficient insertion and deletion at any position.

### API

```hemlock
let list = LinkedList();
```

**Methods:**
- `list.append(value)` - Add to end
- `list.prepend(value)` - Add to beginning
- `list.insert(index, value)` - Insert at index
- `list.remove(index)` - Remove and return value at index
- `list.get(index)` - Get value at index
- `list.set(index, value)` - Update value at index
- `list.index_of(value)` - Find index of value (returns -1 if not found)
- `list.contains(value)` - Check if value exists
- `list.clear()` - Remove all values
- `list.is_empty()` - Check if list is empty
- `list.to_array()` - Convert to array
- `list.reverse()` - Reverse the list in-place
- `list.each(callback)` - Iterate over values with callback(value, index)

**Properties:**
- `list.size` - Number of values in list

---

## LRUCache

Least Recently Used (LRU) cache with fixed capacity. When the cache is full, the least recently accessed item is evicted to make room for new items.

### API

```hemlock
let cache = LRUCache(100);  // Create cache with capacity 100
```

**Methods:**
- `cache.get(key)` - Get value for key (marks as recently used)
- `cache.set(key, value)` - Set value (returns evicted key or null)
- `cache.has(key)` - Check if key exists
- `cache.remove(key)` - Remove key and return value
- `cache.peek(key)` - Get value without updating access order
- `cache.clear()` - Remove all items
- `cache.size()` - Current number of items
- `cache.capacity()` - Maximum capacity
- `cache.is_empty()` - Check if empty
- `cache.is_full()` - Check if at capacity
- `cache.keys()` - Get all keys (most recent first)
- `cache.values()` - Get all values (most recent first)
- `cache.entries()` - Get all {key, value} objects
- `cache.most_recent()` - Get most recently accessed key
- `cache.least_recent()` - Get least recently accessed key
- `cache.resize(new_capacity)` - Resize cache (returns evicted keys)
- `cache.each(callback)` - Iterate with callback(key, value)

### Example

```hemlock
import { LRUCache } from "@stdlib/collections";

// Create a cache for API responses
let cache = LRUCache(1000);

fn get_user(id) {
    // Check cache first
    let cached = cache.get(id);
    if (cached != null) {
        return cached;
    }

    // Fetch from database
    let user = db.query("SELECT * FROM users WHERE id = ?", id);

    // Cache the result (may evict old entries)
    cache.set(id, user);

    return user;
}
```

### Access Order

- `get()` marks item as most recently used
- `peek()` does not change access order
- `set()` marks item as most recently used
- When full, `set()` evicts least recently used item

---

## Performance Characteristics

### HashMap
- Set: O(1) average, O(n) worst case
- Get: O(1) average, O(n) worst case
- Delete: O(1) average, O(n) worst case
- Resize: O(n) when load factor exceeded

### Queue
- Enqueue: O(1)
- Dequeue: O(1) (circular buffer)
- Peek: O(1)

### Stack
- Push: O(1)
- Pop: O(1)
- Peek: O(1)

### Set
- Add: O(1) average (HashMap-based)
- Delete: O(1) average (HashMap-based)
- Has: O(1) average (HashMap-based)
- Union/Intersection/Difference: O(n+m)

### LinkedList
- Append/Prepend: O(1)
- Insert/Remove: O(n) worst case, O(n/2) average (bidirectional traversal)
- Get/Set: O(n) worst case, O(n/2) average (bidirectional traversal)

### LRUCache
- Get: O(1)
- Set: O(1)
- Has: O(1)
- Remove: O(1)
- Peek: O(1)

---

## Implementation Notes

- **Hash Function:** Uses djb2 algorithm for strings (excellent distribution) and direct value for integers
- **Collision Resolution:** Separate chaining with arrays
- **Memory Management:** Manual - collections do not automatically free memory
- **Load Factor:** HashMap resizes at 0.75 load factor
- **Modulo Operation:** Uses native `%` operator for O(1) performance
- **Type Casting:** Efficient native type conversion for float-to-int operations
- **Set Implementation:** Uses HashMap internally for O(1) operations
- **Queue Implementation:** Circular buffer with automatic resizing for O(1) enqueue/dequeue
- **LinkedList Optimization:** Bidirectional traversal - chooses head or tail based on proximity to target index
- **LRUCache Implementation:** Doubly-linked list for O(1) access order updates combined with hash map for O(1) key lookup
- **Iterator Support:** All collections support `.each(callback)` for functional-style iteration

---

## Known Limitations

1. **No automatic memory cleanup** - users must manually manage collection lifecycle

---

## Future Improvements

- Add PriorityQueue, Deque, TreeMap, and other data structures
- Add map/filter/reduce methods for functional programming patterns
- Add bounded collections (max size enforcement)
- Add more convenience methods (compute_if_absent, put_if_absent, etc.)

---

## Testing

Run the test suite:

```bash
# Run all collections tests
make test | grep stdlib_collections

# Or run individual comprehensive tests
./hemlock tests/stdlib_collections/test_hashmap.hml
./hemlock tests/stdlib_collections/test_queue.hml
./hemlock tests/stdlib_collections/test_stack.hml
./hemlock tests/stdlib_collections/test_set.hml
./hemlock tests/stdlib_collections/test_linkedlist.hml
```

**Test Coverage:**
- Basic functionality tests in `test_basic.hml`
- Comprehensive tests for each data structure (100+ items, edge cases, error handling)
- All 259 Hemlock tests passing, including all collections tests

---

## License

Part of the Hemlock standard library.
