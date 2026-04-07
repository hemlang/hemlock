# Array API Reference

Complete reference for Hemlock's array type and all 24 array methods.

---

## Overview

Arrays in Hemlock are **dynamic, heap-allocated** sequences that can hold mixed types. They provide comprehensive methods for data manipulation and processing.

**Key Features:**
- Dynamic sizing (automatic growth)
- Zero-indexed
- Mixed types allowed
- 23 built-in methods
- Heap-allocated with capacity tracking

---

## Array Type

**Type:** `array`

**Properties:**
- `.length` - Number of elements (i32)

**Literal Syntax:** Square brackets `[elem1, elem2, ...]`

**Examples:**
```hemlock
let arr = [1, 2, 3, 4, 5];
print(arr.length);     // 5

// Mixed types
let mixed = [1, "hello", true, null];
print(mixed.length);   // 4

// Empty array
let empty = [];
print(empty.length);   // 0
```

---

## Indexing

Arrays support zero-based indexing using `[]`:

**Read Access:**
```hemlock
let arr = [10, 20, 30];
print(arr[0]);         // 10
print(arr[1]);         // 20
print(arr[2]);         // 30
```

**Write Access:**
```hemlock
let arr = [10, 20, 30];
arr[0] = 99;
arr[1] = 88;
print(arr);            // [99, 88, 30]
```

**Note:** Direct indexing has no bounds checking. Use methods for safety.

---

## Array Properties

### .length

Get the number of elements in the array.

**Type:** `i32`

**Examples:**
```hemlock
let arr = [1, 2, 3];
print(arr.length);     // 3

let empty = [];
print(empty.length);   // 0

// Length changes dynamically
arr.push(4);
print(arr.length);     // 4

arr.pop();
print(arr.length);     // 3
```

---

## Array Methods

### Capacity Management

#### reserve

Pre-allocate capacity for future elements without changing the array's length. Useful for avoiding repeated reallocations during bulk inserts.

**Signature:**
```hemlock
array.reserve(n: i32): null
```

**Parameters:**
- `n` - Number of elements to pre-allocate capacity for

**Returns:** `null`

**Mutates:** Yes (modifies internal capacity, but not length or elements)

**Examples:**
```hemlock
let arr = [];
arr.reserve(1000);
print(arr.length);     // 0 - reserve doesn't change length

// Push 1000 elements without any reallocation
for (let i = 0; i < 1000; i++) {
    arr.push(i);
}
print(arr.length);     // 1000

// Reserve on pre-populated array preserves existing elements
let data = [1, 2, 3];
data.reserve(1000);
print(data.length);    // 3
print(data[0]);        // 1

// Reserve smaller than current capacity is a no-op
data.reserve(5);
print(data.length);    // 3 (unchanged)

// Reserve with zero is valid (no-op)
let empty = [];
empty.reserve(0);
print(empty.length);   // 0
```

**Behavior:**
- Pre-allocates internal storage for at least `n` elements
- Does not change the array's length or existing elements
- If `n` is less than or equal to current capacity, this is a no-op
- Improves performance when the number of elements to insert is known in advance

---

### Stack Operations

#### push

Add element to end of array.

**Signature:**
```hemlock
array.push(value: any): null
```

**Parameters:**
- `value` - Element to add

**Returns:** `null`

**Mutates:** Yes (modifies array in place)

**Examples:**
```hemlock
let arr = [1, 2, 3];
arr.push(4);           // [1, 2, 3, 4]
arr.push(5);           // [1, 2, 3, 4, 5]
arr.push("hello");     // [1, 2, 3, 4, 5, "hello"]
```

---

#### pop

Remove and return last element.

**Signature:**
```hemlock
array.pop(): any
```

**Returns:** Last element (removed from array)

**Mutates:** Yes (modifies array in place)

**Examples:**
```hemlock
let arr = [1, 2, 3];
let last = arr.pop();  // 3
print(arr);            // [1, 2]

let last2 = arr.pop(); // 2
print(arr);            // [1]
```

**Error:** Runtime error if array is empty.

---

### Queue Operations

#### shift

Remove and return first element.

**Signature:**
```hemlock
array.shift(): any
```

**Returns:** First element (removed from array)

**Mutates:** Yes (modifies array in place)

**Examples:**
```hemlock
let arr = [1, 2, 3];
let first = arr.shift();  // 1
print(arr);               // [2, 3]

let first2 = arr.shift(); // 2
print(arr);               // [3]
```

**Error:** Runtime error if array is empty.

---

#### unshift

Add element to beginning of array.

**Signature:**
```hemlock
array.unshift(value: any): null
```

**Parameters:**
- `value` - Element to add

**Returns:** `null`

**Mutates:** Yes (modifies array in place)

**Examples:**
```hemlock
let arr = [2, 3];
arr.unshift(1);        // [1, 2, 3]
arr.unshift(0);        // [0, 1, 2, 3]
```

---

### Insertion & Removal

#### insert

Insert element at specific index.

**Signature:**
```hemlock
array.insert(index: i32, value: any): null
```

**Parameters:**
- `index` - Position to insert at (0-based)
- `value` - Element to insert

**Returns:** `null`

**Mutates:** Yes (modifies array in place)

**Examples:**
```hemlock
let arr = [1, 2, 4, 5];
arr.insert(2, 3);      // [1, 2, 3, 4, 5]

let arr2 = [1, 3];
arr2.insert(1, 2);     // [1, 2, 3]

// Insert at end
arr2.insert(arr2.length, 4);  // [1, 2, 3, 4]
```

**Behavior:** Shifts elements at and after index to the right.

---

#### remove

Remove and return element at index.

**Signature:**
```hemlock
array.remove(index: i32): any
```

**Parameters:**
- `index` - Position to remove from (0-based)

**Returns:** Removed element

**Mutates:** Yes (modifies array in place)

**Examples:**
```hemlock
let arr = [1, 2, 3, 4, 5];
let removed = arr.remove(0);  // 1
print(arr);                   // [2, 3, 4, 5]

let removed2 = arr.remove(2); // 4
print(arr);                   // [2, 3, 5]
```

**Behavior:** Shifts elements after index to the left.

**Error:** Runtime error if index out of bounds.

---

### Search & Find

#### find

Find first occurrence of value.

**Signature:**
```hemlock
array.find(value: any): i32
```

**Parameters:**
- `value` - Value to search for

**Returns:** Index of first occurrence, or `-1` if not found

**Examples:**
```hemlock
let arr = [10, 20, 30, 40];
let idx = arr.find(30);      // 2
let idx2 = arr.find(99);     // -1 (not found)

// Find first duplicate
let arr2 = [1, 2, 3, 2, 4];
let idx3 = arr2.find(2);     // 1 (first occurrence)
```

**Comparison:** Uses value equality for primitives and strings.

---

#### contains

Check if array contains value.

**Signature:**
```hemlock
array.contains(value: any): bool
```

**Parameters:**
- `value` - Value to search for

**Returns:** `true` if found, `false` otherwise

**Examples:**
```hemlock
let arr = [10, 20, 30, 40];
let has = arr.contains(20);  // true
let has2 = arr.contains(99); // false

// Works with strings
let words = ["hello", "world"];
let has3 = words.contains("hello");  // true
```

---

### Slicing & Extraction

#### slice

Extract subarray by range (end exclusive).

**Signature:**
```hemlock
array.slice(start: i32, end?: i32): array
```

**Parameters:**
- `start` - Starting index (0-based, inclusive)
- `end` - Ending index (exclusive). Defaults to `array.length` if omitted.

**Returns:** New array with elements from [start, end)

**Mutates:** No (returns new array)

**Examples:**
```hemlock
let arr = [1, 2, 3, 4, 5];
let sub = arr.slice(1, 4);   // [2, 3, 4]
let first_three = arr.slice(0, 3);  // [1, 2, 3]
let last_two = arr.slice(3, 5);     // [4, 5]

// Single-arg: from index to end
let tail = arr.slice(2);     // [3, 4, 5]
let copy = arr.slice(0);     // [1, 2, 3, 4, 5] (shallow copy)

// Empty slice
let empty = arr.slice(2, 2); // []
```

---

#### first

Get first element without removing.

**Signature:**
```hemlock
array.first(): any
```

**Returns:** First element

**Mutates:** No

**Examples:**
```hemlock
let arr = [1, 2, 3];
let f = arr.first();         // 1
print(arr);                  // [1, 2, 3] (unchanged)
```

**Error:** Runtime error if array is empty.

---

#### last

Get last element without removing.

**Signature:**
```hemlock
array.last(): any
```

**Returns:** Last element

**Mutates:** No

**Examples:**
```hemlock
let arr = [1, 2, 3];
let l = arr.last();          // 3
print(arr);                  // [1, 2, 3] (unchanged)
```

**Error:** Runtime error if array is empty.

---

### Array Manipulation

#### reverse

Reverse array in place.

**Signature:**
```hemlock
array.reverse(): null
```

**Returns:** `null`

**Mutates:** Yes (modifies array in place)

**Examples:**
```hemlock
let arr = [1, 2, 3, 4, 5];
arr.reverse();               // [5, 4, 3, 2, 1]
print(arr);                  // [5, 4, 3, 2, 1]

let words = ["hello", "world"];
words.reverse();             // ["world", "hello"]
```

---

#### clear

Remove all elements from array.

**Signature:**
```hemlock
array.clear(): null
```

**Returns:** `null`

**Mutates:** Yes (modifies array in place)

**Examples:**
```hemlock
let arr = [1, 2, 3, 4, 5];
arr.clear();
print(arr);                  // []
print(arr.length);           // 0
```

---

### Array Combination

#### concat

Concatenate with another array.

**Signature:**
```hemlock
array.concat(other: array): array
```

**Parameters:**
- `other` - Array to concatenate

**Returns:** New array with elements from both arrays

**Mutates:** No (returns new array)

**Examples:**
```hemlock
let a = [1, 2, 3];
let b = [4, 5, 6];
let combined = a.concat(b);  // [1, 2, 3, 4, 5, 6]
print(a);                    // [1, 2, 3] (unchanged)
print(b);                    // [4, 5, 6] (unchanged)

// Chain concatenations
let c = [7, 8];
let all = a.concat(b).concat(c);  // [1, 2, 3, 4, 5, 6, 7, 8]
```

---

### Functional Operations

#### map

Transform each element using a callback function.

**Signature:**
```hemlock
array.map(callback: fn): array
```

**Parameters:**
- `callback` - Function that takes an element and returns transformed value

**Returns:** New array with transformed elements

**Mutates:** No (returns new array)

**Examples:**
```hemlock
let arr = [1, 2, 3, 4, 5];
let doubled = arr.map(fn(x) { return x * 2; });
print(doubled);  // [2, 4, 6, 8, 10]

let names = ["alice", "bob"];
let upper = names.map(fn(s) { return s.to_upper(); });
print(upper);  // ["ALICE", "BOB"]
```

---

#### filter

Select elements that match a predicate.

**Signature:**
```hemlock
array.filter(predicate: fn): array
```

**Parameters:**
- `predicate` - Function that takes an element and returns bool

**Returns:** New array with elements where predicate returned true

**Mutates:** No (returns new array)

**Examples:**
```hemlock
let arr = [1, 2, 3, 4, 5, 6];
let evens = arr.filter(fn(x) { return x % 2 == 0; });
print(evens);  // [2, 4, 6]

let words = ["hello", "hi", "hey", "goodbye"];
let short = words.filter(fn(s) { return s.length < 4; });
print(short);  // ["hi", "hey"]
```

---

#### reduce

Reduce array to single value using accumulator.

**Signature:**
```hemlock
array.reduce(callback: fn, initial: any): any
```

**Parameters:**
- `callback` - Function that takes (accumulator, element) and returns new accumulator
- `initial` - Starting value for the accumulator

**Returns:** Final accumulated value

**Mutates:** No

**Examples:**
```hemlock
let arr = [1, 2, 3, 4, 5];
let sum = arr.reduce(fn(acc, x) { return acc + x; }, 0);
print(sum);  // 15

let product = arr.reduce(fn(acc, x) { return acc * x; }, 1);
print(product);  // 120

// Find max value
let max = arr.reduce(fn(acc, x) {
    if (x > acc) { return x; }
    return acc;
}, arr[0]);
print(max);  // 5
```

---

### String Conversion

#### join

Join elements into string with delimiter.

**Signature:**
```hemlock
array.join(delimiter: string): string
```

**Parameters:**
- `delimiter` - String to place between elements

**Returns:** String with all elements joined

**Examples:**
```hemlock
let words = ["hello", "world", "foo"];
let joined = words.join(" ");  // "hello world foo"

let numbers = [1, 2, 3];
let csv = numbers.join(",");   // "1,2,3"

// Works with mixed types
let mixed = [1, "hello", true, null];
print(mixed.join(" | "));  // "1 | hello | true | null"

// Empty delimiter
let arr = ["a", "b", "c"];
let s = arr.join("");          // "abc"
```

**Behavior:** Automatically converts all elements to strings, including rune values returned by `string.chars()`.

```hemlock
// Join rune arrays (from chars())
let chars = "hello".chars();
print(chars.join("-"));   // "h-e-l-l-o"

// String reversal idiom
let reversed = "hello".chars();
reversed.reverse();
print(reversed.join(""));  // "olleh"
```

---

## Method Chaining

Array methods can be chained for concise operations:

**Examples:**
```hemlock
// Chain slice and join
let result = ["apple", "banana", "cherry", "date"]
    .slice(0, 2)
    .join(" and ");  // "apple and banana"

// Chain concat and slice
let combined = [1, 2, 3]
    .concat([4, 5, 6])
    .slice(2, 5);    // [3, 4, 5]

// Complex chaining
let words = ["hello", "world", "foo", "bar"];
let result2 = words
    .slice(0, 3)
    .concat(["baz"])
    .join("-");      // "hello-world-foo-baz"
```

---

## Complete Method Summary

### Mutating Methods

Methods that modify the array in place:

| Method     | Signature                  | Returns   | Description                    |
|------------|----------------------------|-----------|--------------------------------|
| `push`     | `(value: any)`             | `null`    | Add to end                     |
| `pop`      | `()`                       | `any`     | Remove from end                |
| `shift`    | `()`                       | `any`     | Remove from start              |
| `unshift`  | `(value: any)`             | `null`    | Add to start                   |
| `insert`   | `(index: i32, value: any)` | `null`    | Insert at index                |
| `remove`   | `(index: i32)`             | `any`     | Remove at index                |
| `reverse`  | `()`                       | `null`    | Reverse in place               |
| `clear`    | `()`                       | `null`    | Remove all elements            |
| `reserve`  | `(n: i32)`                 | `null`    | Pre-allocate capacity          |

### Non-Mutating Methods

Methods that return new values without modifying the original:

| Method     | Signature                  | Returns   | Description                    |
|------------|----------------------------|-----------|--------------------------------|
| `find`     | `(value: any)`             | `i32`     | Find first occurrence          |
| `contains` | `(value: any)`             | `bool`    | Check if contains value        |
| `slice`    | `(start: i32, end: i32)`   | `array`   | Extract subarray               |
| `first`    | `()`                       | `any`     | Get first element              |
| `last`     | `()`                       | `any`     | Get last element               |
| `concat`   | `(other: array)`           | `array`   | Concatenate arrays             |
| `join`     | `(delimiter: string)`      | `string`  | Join elements into string      |
| `map`      | `(callback: fn)`           | `array`   | Transform each element         |
| `filter`   | `(predicate: fn)`          | `array`   | Select matching elements       |
| `reduce`   | `(callback: fn, initial: any)` | `any` | Reduce to single value         |
| `every`    | `(predicate: fn)`          | `bool`    | Check if all elements match    |
| `some`     | `(predicate: fn)`          | `bool`    | Check if any element matches   |
| `indexOf`  | `(value: any)`             | `i32`     | Find index of value (-1 if not found) |
| `sort`     | `(compare?: fn)`           | `null`    | Sort in place (optional comparator) |
| `fill`     | `(value: any)`             | `null`    | Fill all elements with value   |

---

## Usage Patterns

### Stack Usage

```hemlock
let stack = [];

// Push elements
stack.push(1);
stack.push(2);
stack.push(3);

// Pop elements
while (stack.length > 0) {
    let item = stack.pop();
    print(item);  // 3, 2, 1
}
```

### Queue Usage

```hemlock
let queue = [];

// Enqueue
queue.push(1);
queue.push(2);
queue.push(3);

// Dequeue
while (queue.length > 0) {
    let item = queue.shift();
    print(item);  // 1, 2, 3
}
```

### Array Transformation

```hemlock
// Filter (manual)
let numbers = [1, 2, 3, 4, 5, 6];
let evens = [];
let i = 0;
while (i < numbers.length) {
    if (numbers[i] % 2 == 0) {
        evens.push(numbers[i]);
    }
    i = i + 1;
}

// Map (manual)
let numbers2 = [1, 2, 3, 4, 5];
let doubled = [];
let j = 0;
while (j < numbers2.length) {
    doubled.push(numbers2[j] * 2);
    j = j + 1;
}
```

### Building Arrays

```hemlock
let arr = [];

// Build array with loop
let i = 0;
while (i < 10) {
    arr.push(i * 10);
    i = i + 1;
}

print(arr);  // [0, 10, 20, 30, 40, 50, 60, 70, 80, 90]
```

---

## Implementation Details

**Capacity Management:**
- Arrays automatically grow when needed
- Capacity doubles when exceeded
- Use `reserve(n)` to pre-allocate capacity for bulk inserts

**Value Comparison:**
- `find()` and `contains()` use value equality
- Works correctly for primitives and strings
- Objects/arrays compared by reference

**Memory:**
- Heap-allocated
- No automatic freeing (manual memory management)
- No bounds checking on direct index access

---

## See Also

- [Type System](type-system.md) - Array type details
- [String API](string-api.md) - String join() results
- [Operators](operators.md) - Array indexing operator
