# Arrays

Hemlock provides **dynamic arrays** with comprehensive methods for data manipulation and processing. Arrays can hold mixed types and grow automatically as needed.

## Overview

```hemlock
// Array literals
let arr = [1, 2, 3, 4, 5];
print(arr[0]);         // 1
print(arr.length);     // 5

// Mixed types allowed
let mixed = [1, "hello", true, null];

// Dynamic sizing
arr.push(6);           // Grows automatically
arr.push(7);
print(arr.length);     // 7
```

## Array Literals

### Basic Syntax

```hemlock
let numbers = [1, 2, 3, 4, 5];
let strings = ["apple", "banana", "cherry"];
let booleans = [true, false, true];
```

### Empty Arrays

```hemlock
let arr = [];  // Empty array

// Add elements later
arr.push(1);
arr.push(2);
arr.push(3);
```

### Mixed Types

Arrays can contain different types:

```hemlock
let mixed = [
    42,
    "hello",
    true,
    null,
    [1, 2, 3],
    { x: 10, y: 20 }
];

print(mixed[0]);  // 42
print(mixed[1]);  // "hello"
print(mixed[4]);  // [1, 2, 3] (nested array)
```

### Nested Arrays

```hemlock
let matrix = [
    [1, 2, 3],
    [4, 5, 6],
    [7, 8, 9]
];

print(matrix[0][0]);  // 1
print(matrix[1][2]);  // 6
print(matrix[2][1]);  // 8
```

### Typed Arrays

Arrays can have type annotations to enforce element types:

```hemlock
// Typed array syntax
let nums: array<i32> = [1, 2, 3, 4, 5];
let names: array<string> = ["Alice", "Bob", "Carol"];
let flags: array<bool> = [true, false, true];

// Type checking at runtime
let valid: array<i32> = [1, 2, 3];       // OK
let invalid: array<i32> = [1, "two", 3]; // Runtime error: type mismatch

// Nested typed arrays
let matrix: array<array<i32>> = [
    [1, 2, 3],
    [4, 5, 6]
];
```

**Type annotation behavior:**
- Elements are type-checked when added to the array
- Type mismatches cause runtime errors
- Without type annotation, arrays accept mixed types

## Indexing

### Reading Elements

Zero-indexed access:

```hemlock
let arr = [10, 20, 30, 40, 50];

print(arr[0]);  // 10 (first element)
print(arr[4]);  // 50 (last element)

// Out of bounds returns null (no error)
print(arr[10]);  // null
```

### Writing Elements

```hemlock
let arr = [1, 2, 3];

arr[0] = 10;    // Modify existing
arr[1] = 20;
print(arr);     // [10, 20, 3]

// Can assign beyond current length (grows array)
arr[5] = 60;    // Creates [10, 20, 3, null, null, 60]
```

### Negative Indices

**Not supported** - Use positive indices only:

```hemlock
let arr = [1, 2, 3];
print(arr[-1]);  // ERROR or undefined behavior

// Use length for last element
print(arr[arr.length - 1]);  // 3
```

## Properties

### `.length` Property

Returns the number of elements:

```hemlock
let arr = [1, 2, 3, 4, 5];
print(arr.length);  // 5

// Empty array
let empty = [];
print(empty.length);  // 0

// After modifications
arr.push(6);
print(arr.length);  // 6
```

## Array Methods

Hemlock provides 18 array methods for comprehensive manipulation.

### Stack Operations

**`push(value)`** - Add element to end:
```hemlock
let arr = [1, 2, 3];
arr.push(4);           // [1, 2, 3, 4]
arr.push(5);           // [1, 2, 3, 4, 5]

print(arr.length);     // 5
```

**`pop()`** - Remove and return last element:
```hemlock
let arr = [1, 2, 3, 4, 5];
let last = arr.pop();  // Returns 5, arr is now [1, 2, 3, 4]

print(last);           // 5
print(arr.length);     // 4
```

### Queue Operations

**`shift()`** - Remove and return first element:
```hemlock
let arr = [1, 2, 3];
let first = arr.shift();   // Returns 1, arr is now [2, 3]

print(first);              // 1
print(arr);                // [2, 3]
```

**`unshift(value)`** - Add element to beginning:
```hemlock
let arr = [2, 3];
arr.unshift(1);            // [1, 2, 3]
arr.unshift(0);            // [0, 1, 2, 3]
```

### Insertion and Removal

**`insert(index, value)`** - Insert element at index:
```hemlock
let arr = [1, 2, 4, 5];
arr.insert(2, 3);      // Insert 3 at index 2: [1, 2, 3, 4, 5]

arr.insert(0, 0);      // Insert at beginning: [0, 1, 2, 3, 4, 5]
```

**`remove(index)`** - Remove and return element at index:
```hemlock
let arr = [1, 2, 3, 4, 5];
let removed = arr.remove(2);  // Returns 3, arr is now [1, 2, 4, 5]

print(removed);               // 3
print(arr);                   // [1, 2, 4, 5]
```

### Search Operations

**`find(value)`** - Find first occurrence:
```hemlock
let arr = [10, 20, 30, 40];
let idx = arr.find(30);      // 2 (index of first occurrence)
let idx2 = arr.find(99);     // -1 (not found)

// Works with any type
let words = ["apple", "banana", "cherry"];
let idx3 = words.find("banana");  // 1
```

**`contains(value)`** - Check if array contains value:
```hemlock
let arr = [10, 20, 30, 40];
let has = arr.contains(20);  // true
let has2 = arr.contains(99); // false
```

### Extraction Operations

**`slice(start, end)`** - Extract subarray (end exclusive):
```hemlock
let arr = [1, 2, 3, 4, 5];
let sub = arr.slice(1, 4);   // [2, 3, 4] (indices 1, 2, 3)
let first = arr.slice(0, 2); // [1, 2]

// Original unchanged
print(arr);                  // [1, 2, 3, 4, 5]
```

**`first()`** - Get first element (without removing):
```hemlock
let arr = [1, 2, 3];
let f = arr.first();         // 1 (without removing)
print(arr);                  // [1, 2, 3] (unchanged)
```

**`last()`** - Get last element (without removing):
```hemlock
let arr = [1, 2, 3];
let l = arr.last();          // 3 (without removing)
print(arr);                  // [1, 2, 3] (unchanged)
```

### Transformation Operations

**`reverse()`** - Reverse array in-place:
```hemlock
let arr = [1, 2, 3, 4, 5];
arr.reverse();               // [5, 4, 3, 2, 1]

print(arr);                  // [5, 4, 3, 2, 1] (modified)
```

**`join(delimiter)`** - Join elements into string:
```hemlock
let words = ["hello", "world", "foo"];
let joined = words.join(" ");  // "hello world foo"

let numbers = [1, 2, 3];
let csv = numbers.join(",");   // "1,2,3"

// Works with mixed types
let mixed = [1, "hello", true, null];
print(mixed.join(" | "));  // "1 | hello | true | null"
```

**`concat(other)`** - Concatenate with another array:
```hemlock
let a = [1, 2, 3];
let b = [4, 5, 6];
let combined = a.concat(b);  // [1, 2, 3, 4, 5, 6] (new array)

// Originals unchanged
print(a);                    // [1, 2, 3]
print(b);                    // [4, 5, 6]
```

### Utility Operations

**`clear()`** - Remove all elements:
```hemlock
let arr = [1, 2, 3, 4, 5];
arr.clear();                 // []

print(arr.length);           // 0
print(arr);                  // []
```

## Method Chaining

Methods that return arrays or values enable chaining:

```hemlock
let result = [1, 2, 3]
    .concat([4, 5, 6])
    .slice(2, 5);  // [3, 4, 5]

let text = ["apple", "banana", "cherry"]
    .slice(0, 2)
    .join(" and ");  // "apple and banana"

let numbers = [5, 3, 8, 1, 9]
    .slice(1, 4)
    .concat([10, 11]);  // [3, 8, 1, 10, 11]
```

## Complete Method Reference

| Method | Parameters | Returns | Mutates | Description |
|--------|-----------|---------|---------|-------------|
| `push(value)` | any | void | Yes | Add element to end |
| `pop()` | - | any | Yes | Remove and return last |
| `shift()` | - | any | Yes | Remove and return first |
| `unshift(value)` | any | void | Yes | Add element to beginning |
| `insert(index, value)` | i32, any | void | Yes | Insert at index |
| `remove(index)` | i32 | any | Yes | Remove and return at index |
| `find(value)` | any | i32 | No | Find first occurrence (-1 if not found) |
| `contains(value)` | any | bool | No | Check if contains value |
| `slice(start, end)` | i32, i32 | array | No | Extract subarray (new array) |
| `join(delimiter)` | string | string | No | Join into string |
| `concat(other)` | array | array | No | Concatenate (new array) |
| `reverse()` | - | void | Yes | Reverse in-place |
| `first()` | - | any | No | Get first element |
| `last()` | - | any | No | Get last element |
| `clear()` | - | void | Yes | Remove all elements |
| `map(callback)` | fn | array | No | Transform each element |
| `filter(predicate)` | fn | array | No | Select matching elements |
| `reduce(callback, initial)` | fn, any | any | No | Reduce to single value |

## Implementation Details

### Memory Model

- **Heap-allocated** - Dynamic capacity
- **Automatic growth** - Doubles capacity when exceeded
- **No automatic shrinking** - Capacity doesn't decrease
- **No bounds checking on indexing** - Use methods for safety

### Capacity Management

```hemlock
let arr = [];  // Initial capacity: 0

arr.push(1);   // Grows to capacity 1
arr.push(2);   // Grows to capacity 2
arr.push(3);   // Grows to capacity 4 (doubles)
arr.push(4);   // Still capacity 4
arr.push(5);   // Grows to capacity 8 (doubles)
```

### Value Comparison

`find()` and `contains()` use value equality:

```hemlock
// Primitives: compare by value
let arr = [1, 2, 3];
arr.contains(2);  // true

// Strings: compare by value
let words = ["hello", "world"];
words.contains("hello");  // true

// Objects: compare by reference
let obj1 = { x: 10 };
let obj2 = { x: 10 };
let arr2 = [obj1];
arr2.contains(obj1);  // true (same reference)
arr2.contains(obj2);  // false (different reference)
```

## Common Patterns

### Functional Operations (map/filter/reduce)

Arrays have built-in `map`, `filter`, and `reduce` methods:

```hemlock
// map - transform each element
let numbers = [1, 2, 3, 4, 5];
let doubled = numbers.map(fn(x) { return x * 2; });
print(doubled);  // [2, 4, 6, 8, 10]

// filter - select matching elements
let evens = numbers.filter(fn(x) { return x % 2 == 0; });
print(evens);  // [2, 4]

// reduce - accumulate to single value
let sum = numbers.reduce(fn(acc, x) { return acc + x; }, 0);
print(sum);  // 15

// Chaining functional operations
let result = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
    .filter(fn(x) { return x % 2 == 0; })  // [2, 4, 6, 8, 10]
    .map(fn(x) { return x * x; })          // [4, 16, 36, 64, 100]
    .reduce(fn(acc, x) { return acc + x; }, 0);  // 220
```

### Pattern: Array as Stack

```hemlock
let stack = [];

// Push onto stack
stack.push(1);
stack.push(2);
stack.push(3);

// Pop from stack
let top = stack.pop();    // 3
let next = stack.pop();   // 2
```

### Pattern: Array as Queue

```hemlock
let queue = [];

// Enqueue (add to end)
queue.push(1);
queue.push(2);
queue.push(3);

// Dequeue (remove from front)
let first = queue.shift();   // 1
let second = queue.shift();  // 2
```

## Best Practices

1. **Use methods over direct indexing** - Bounds checking and clarity
2. **Check bounds** - Direct indexing doesn't bounds-check
3. **Prefer immutable operations** - Use `slice()` and `concat()` over mutation
4. **Initialize with capacity** - If you know the size (not currently supported)
5. **Use `contains()` for membership** - Clearer than manual loops
6. **Chain methods** - More readable than nested calls

## Common Pitfalls

### Pitfall: Direct Index Out of Bounds

```hemlock
let arr = [1, 2, 3];

// No bounds checking!
arr[10] = 99;  // Creates sparse array with nulls
print(arr.length);  // 11 (not 3!)

// Better: Use push() or check length
if (arr.length <= 10) {
    arr.push(99);
}
```

### Pitfall: Mutation vs. New Array

```hemlock
let arr = [1, 2, 3];

// Mutates original
arr.reverse();
print(arr);  // [3, 2, 1]

// Returns new array
let sub = arr.slice(0, 2);
print(arr);  // [3, 2, 1] (unchanged)
print(sub);  // [3, 2]
```

### Pitfall: Reference Equality

```hemlock
let obj = { x: 10 };
let arr = [obj];

// Same reference: true
arr.contains(obj);  // true

// Different reference: false
arr.contains({ x: 10 });  // false (different object)
```

### Pitfall: Long-lived Arrays

```hemlock
// Arrays in local scope are auto-freed, but global/long-lived arrays need attention
let global_cache = [];  // Module-level, persists until program exit

fn add_to_cache(item) {
    global_cache.push(item);  // Grows indefinitely
}

// For long-lived data, consider:
// - Clearing the array periodically: global_cache.clear();
// - Freeing early when done: free(global_cache);
```

## Examples

### Example: Array Statistics

```hemlock
fn mean(arr) {
    let sum = 0;
    let i = 0;
    while (i < arr.length) {
        sum = sum + arr[i];
        i = i + 1;
    }
    return sum / arr.length;
}

fn max(arr) {
    if (arr.length == 0) {
        return null;
    }

    let max_val = arr[0];
    let i = 1;
    while (i < arr.length) {
        if (arr[i] > max_val) {
            max_val = arr[i];
        }
        i = i + 1;
    }
    return max_val;
}

let numbers = [3, 7, 2, 9, 1];
print(mean(numbers));  // 4.4
print(max(numbers));   // 9
```

### Example: Array Deduplication

```hemlock
fn unique(arr) {
    let result = [];
    let i = 0;
    while (i < arr.length) {
        if (!result.contains(arr[i])) {
            result.push(arr[i]);
        }
        i = i + 1;
    }
    return result;
}

let numbers = [1, 2, 2, 3, 1, 4, 3, 5];
let uniq = unique(numbers);  // [1, 2, 3, 4, 5]
```

### Example: Array Chunking

```hemlock
fn chunk(arr, size) {
    let result = [];
    let i = 0;

    while (i < arr.length) {
        let chunk = arr.slice(i, i + size);
        result.push(chunk);
        i = i + size;
    }

    return result;
}

let numbers = [1, 2, 3, 4, 5, 6, 7, 8];
let chunks = chunk(numbers, 3);
// [[1, 2, 3], [4, 5, 6], [7, 8]]
```

### Example: Array Flattening

```hemlock
fn flatten(arr) {
    let result = [];
    let i = 0;

    while (i < arr.length) {
        if (typeof(arr[i]) == "array") {
            // Nested array - flatten it
            let nested = flatten(arr[i]);
            let j = 0;
            while (j < nested.length) {
                result.push(nested[j]);
                j = j + 1;
            }
        } else {
            result.push(arr[i]);
        }
        i = i + 1;
    }

    return result;
}

let nested = [1, [2, 3], [4, [5, 6]], 7];
let flat = flatten(nested);  // [1, 2, 3, 4, 5, 6, 7]
```

### Example: Sorting (Bubble Sort)

```hemlock
fn sort(arr) {
    let n = arr.length;
    let i = 0;

    while (i < n) {
        let j = 0;
        while (j < n - i - 1) {
            if (arr[j] > arr[j + 1]) {
                // Swap
                let temp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = temp;
            }
            j = j + 1;
        }
        i = i + 1;
    }
}

let numbers = [5, 2, 8, 1, 9];
sort(numbers);  // Modifies in-place
print(numbers);  // [1, 2, 5, 8, 9]
```

## Limitations

Current limitations:

- **No bounds checking on indexing** - Direct access is unchecked
- **Reference equality for objects** - `find()` and `contains()` use reference comparison
- **No array destructuring** - No `let [a, b] = arr` syntax
- **No spread operator** - No `[...arr1, ...arr2]` syntax

**Note:** Arrays are refcounted and automatically freed when scope exits. See [Memory Management](memory.md#internal-reference-counting) for details.

## Related Topics

- [Strings](strings.md) - String methods similar to array methods
- [Objects](objects.md) - Arrays are also object-like
- [Functions](functions.md) - Higher-order functions with arrays
- [Control Flow](control-flow.md) - Iterating over arrays

## See Also

- **Dynamic Sizing**: Arrays grow automatically with capacity doubling
- **Methods**: 18 comprehensive methods for manipulation including map/filter/reduce
- **Memory**: See [Memory](memory.md) for array allocation details
