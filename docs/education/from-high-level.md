# From Python/JavaScript to Systems Programming

> "You already know how to program. Now learn what the computer is actually doing."

This guide helps programmers from Python, JavaScript, Ruby, or other high-level languages transition to systems programming concepts using Hemlock.

---

## What You Already Know (That Still Works)

Good news! Most of your knowledge transfers directly:

| Python/JS | Hemlock |
|-----------|---------|
| `x = 42` | `let x = 42;` |
| `if x > 0:` | `if (x > 0) { }` |
| `for i in range(10):` | `for (let i = 0; i < 10; i++) { }` |
| `def add(a, b):` | `fn add(a, b) { }` |
| `[1, 2, 3]` | `[1, 2, 3]` |
| `{"key": value}` | `{ key: value }` |
| `len(x)` | `len(x)` |
| `type(x)` | `typeof(x)` |

**The basics are the same.** The differences are in what happens underneath.

---

## The Five Big Mindset Shifts

### 1. Memory Is Your Responsibility

**In Python:**
```python
def process():
    data = [0] * 1000000  # Allocated somewhere
    return data           # Still exists
# Eventually garbage collected (when? who knows?)
```

**In Hemlock:**
```hemlock
fn process(): ptr {
    let p = alloc(1000000);  // YOU allocate
    return p;                 // YOU track it
}

let data = process();
// ... use data ...
free(data);                   // YOU free it
```

**The shift:** You decide when memory is created and destroyed. No garbage collector. No "eventually."

---

### 2. Types Have Sizes

**In Python:**
```python
x = 42           # An "integer" - could be any size
y = 99999999999  # Still an "integer" - Python handles it
```

**In Hemlock:**
```hemlock
let x: i32 = 42;           // 32 bits = 4 bytes, range: -2B to +2B
let y: i64 = 99999999999;  // 64 bits = 8 bytes, larger range
let z: i8 = 127;           // 8 bits = 1 byte, range: -128 to 127
```

**The shift:** Types determine how many bytes a value occupies and what values it can hold.

**Why it matters:**
```hemlock
let z: i8 = 127;
z = z + 1;       // OVERFLOW: wraps to -128!

let x: i32 = 127;
x = x + 1;       // Fine: 128
```

---

### 3. Variables Are Just Memory Locations

**In Python:**
```python
x = [1, 2, 3]
y = x           # y references the same list
y.append(4)
print(x)        # [1, 2, 3, 4] - x changed too!
```

**In Hemlock with managed arrays (similar behavior):**
```hemlock
let x = [1, 2, 3];
let y = x;          // y references same array
y.push(4);
print(x);           // [1, 2, 3, 4]
```

**In Hemlock with raw pointers (explicit control):**
```hemlock
let p = alloc(12);
ptr_write_i32(p, 1);
ptr_write_i32(p + 4, 2);
ptr_write_i32(p + 8, 3);

let q = p;          // q is the SAME address
ptr_write_i32(q, 99);
print(ptr_read_i32(p));  // 99 - same memory!

// To copy, you must allocate new memory
let copy = alloc(12);
memcpy(copy, p, 12);
ptr_write_i32(copy, 0);
print(ptr_read_i32(p));  // Still 99 - copy is separate

free(p);
free(copy);
```

**The shift:** Assignment copies the value (or reference), not the data. Understand what you're copying.

---

### 4. Strings Are Byte Arrays

**In Python:**
```python
s = "hello"
s[0] = "H"      # ERROR: strings are immutable
s = "H" + s[1:] # Create new string
```

**In Hemlock:**
```hemlock
let s = "hello";
s[0] = 'H';         // WORKS: strings are mutable
print(s);           // "Hello"

// Strings are UTF-8 byte sequences
print(len("hello"));    // 5 bytes
print(len("🚀"));       // 4 bytes (emoji is multi-byte)
print("🚀".length);     // 1 character
```

**The shift:** Strings are sequences of bytes. Character count ≠ byte count for non-ASCII.

---

### 5. Concurrency Is Parallel (Really)

**In Python:**
```python
import threading
def work():
    # GIL means only one thread runs Python at a time
    pass

# "Threads" don't actually run in parallel for CPU work
```

**In Hemlock:**
```hemlock
async fn compute(): i32 {
    // This ACTUALLY runs on another CPU core
    let sum = 0;
    for (let i = 0; i < 1000000; i++) {
        sum = sum + i;
    }
    return sum;
}

let t1 = spawn(compute);
let t2 = spawn(compute);  // Both running simultaneously!

let r1 = join(t1);
let r2 = join(t2);
```

**The shift:** Hemlock's `spawn` creates real OS threads with real parallelism. This means real race conditions if you're not careful.

---

## Python to Hemlock Translation Guide

### Variables

```python
# Python
x = 42
name = "Alice"
items = [1, 2, 3]
person = {"name": "Bob", "age": 30}
```

```hemlock
// Hemlock
let x = 42;
let name = "Alice";
let items = [1, 2, 3];
let person = { name: "Bob", age: 30 };
```

### Functions

```python
# Python
def greet(name, greeting="Hello"):
    return f"{greeting}, {name}!"

result = greet("Alice")
```

```hemlock
// Hemlock
fn greet(name: string, greeting?: "Hello"): string {
    return `${greeting}, ${name}!`;
}

let result = greet("Alice");
```

### Control Flow

```python
# Python
if x > 0:
    print("positive")
elif x < 0:
    print("negative")
else:
    print("zero")

for i in range(10):
    print(i)

while condition:
    do_something()
```

```hemlock
// Hemlock
if (x > 0) {
    print("positive");
} else if (x < 0) {
    print("negative");
} else {
    print("zero");
}

for (let i = 0; i < 10; i++) {
    print(i);
}

while (condition) {
    do_something();
}
```

### Lists/Arrays

```python
# Python
items = [1, 2, 3]
items.append(4)
first = items[0]
length = len(items)
```

```hemlock
// Hemlock
let items = [1, 2, 3];
items.push(4);
let first = items[0];
let length = len(items);
```

### Dictionaries/Objects

```python
# Python
person = {"name": "Alice", "age": 30}
name = person["name"]
person["email"] = "alice@example.com"
```

```hemlock
// Hemlock
let person = { name: "Alice", age: 30 };
let name = person.name;
person.email = "alice@example.com";
```

### Error Handling

```python
# Python
try:
    risky_operation()
except Exception as e:
    print(f"Error: {e}")
finally:
    cleanup()
```

```hemlock
// Hemlock
try {
    risky_operation();
} catch (e) {
    print("Error: " + e);
} finally {
    cleanup();
}
```

### Classes vs Defines

```python
# Python
class Person:
    def __init__(self, name, age):
        self.name = name
        self.age = age

    def greet(self):
        return f"Hi, I'm {self.name}"

alice = Person("Alice", 30)
```

```hemlock
// Hemlock (using define + factory)
define Person {
    name: string,
    age: i32
}

fn create_person(name: string, age: i32): Person {
    return { name: name, age: age };
}

fn greet(p: Person): string {
    return `Hi, I'm ${p.name}`;
}

let alice: Person = create_person("Alice", 30);
print(greet(alice));
```

---

## JavaScript to Hemlock Translation Guide

### Variables

```javascript
// JavaScript
let x = 42;
const name = "Alice";
var items = [1, 2, 3];
```

```hemlock
// Hemlock (all are mutable, use const modifier for params)
let x = 42;
let name = "Alice";
let items = [1, 2, 3];
```

### Arrow Functions

```javascript
// JavaScript
const double = x => x * 2;
const add = (a, b) => a + b;
```

```hemlock
// Hemlock
let double = fn(x) => x * 2;
let add = fn(a, b) => a + b;

// Or with types
fn double(x: i32): i32 => x * 2;
```

### Async/Await

```javascript
// JavaScript
async function fetchData() {
    const response = await fetch(url);
    return await response.json();
}
```

```hemlock
// Hemlock
async fn fetch_data(): object {
    let response = await http_get(url);
    return response.body.deserialize();
}

let task = spawn(fetch_data);
let data = join(task);
```

### Promises vs Tasks

```javascript
// JavaScript
const promise = doWork();
promise.then(result => console.log(result));
```

```hemlock
// Hemlock
let task = spawn(do_work);
let result = join(task);  // Blocks until complete
print(result);
```

### Destructuring

```javascript
// JavaScript
const { name, age } = person;
const [first, ...rest] = items;
```

```hemlock
// Hemlock (no destructuring yet - explicit access)
let name = person.name;
let age = person.age;
let first = items[0];
let rest = items.slice(1);
```

### Spread Operator

```javascript
// JavaScript
const combined = { ...defaults, ...options };
const all = [...arr1, ...arr2];
```

```hemlock
// Hemlock
let combined = { ...defaults, ...options };  // Object spread works!
let all = arr1.concat(arr2);                 // Use concat for arrays
```

### Null Coalescing

```javascript
// JavaScript
const name = user?.name ?? "Anonymous";
```

```hemlock
// Hemlock (same syntax!)
let name = user?.name ?? "Anonymous";
```

---

## Things That Don't Exist (and Why)

### No Garbage Collector

**Why not?** Hemlock is for learning systems programming. GC hides the fundamental concept of memory management.

**What to do instead:**
```hemlock
let p = alloc(100);
defer free(p);  // Automatic cleanup at end of scope
```

### No Classes

**Why not?** Hemlock uses composition over inheritance. `define` creates structural types.

**What to do instead:**
```hemlock
define Shape {
    fn area(): f64
}

let circle: Shape = {
    radius: 5.0,
    area: fn(): f64 => 3.14159 * self.radius * self.radius
};
```

### No Automatic String Conversion

**Why not?** Implicit conversions hide behavior.

**What to do instead:**
```hemlock
let x = 42;
print("Value: " + x);  // Explicit in template
// Or:
print(`Value: ${x}`);  // Template string
```

### No `===` Operator

**Why not?** Hemlock doesn't have JavaScript's type coercion issues.

**What to do instead:**
```hemlock
// == does what you expect
42 == 42      // true
42 == "42"    // false (different types)
```

---

## Memory Patterns for High-Level Programmers

### Pattern 1: Use Defer for Cleanup

```hemlock
fn process_file(path: string) {
    let f = open(path, "r");
    defer f.close();  // Guaranteed to run

    let data = alloc(1000);
    defer free(data);  // Multiple defers OK, run in reverse order

    // ... do work ...
    // Both cleanups happen automatically
}
```

### Pattern 2: Factory Functions Return Ownership

```hemlock
// Caller must free the result
fn create_buffer(size: i32): ptr {
    let p = alloc(size);
    memset(p, 0, size);
    return p;
}

let buf = create_buffer(100);
// ... use buf ...
free(buf);  // Caller's responsibility
```

### Pattern 3: Borrow for Read-Only Access

```hemlock
// Function reads but doesn't own
fn sum_array(p: ptr, count: i32): i32 {
    let total = 0;
    for (let i = 0; i < count; i++) {
        total = total + ptr_read_i32(p + i * sizeof(i32));
    }
    return total;  // Does NOT free p
}

let data = alloc(20);
// ... fill data ...
let s = sum_array(data, 5);  // Borrows data
free(data);  // Original owner frees
```

### Pattern 4: Use Managed Types When Possible

```hemlock
// For most code, managed arrays/objects are fine
let items = [1, 2, 3, 4, 5];  // No manual memory management
items.push(6);
items.pop();
// Reference counted - cleaned up automatically

// Only use raw pointers when you need:
// - Interop with C (FFI)
// - Custom memory layouts
// - Maximum control
```

---

## Common "Gotchas" for High-Level Programmers

### 1. Semicolons Are Required

```hemlock
// WRONG
let x = 42
print(x)

// RIGHT
let x = 42;
print(x);
```

### 2. Braces Are Required (usually)

```hemlock
// WRONG
if (x > 0)
    print("positive")

// RIGHT
if (x > 0) {
    print("positive");
}

// Also OK (single-line statements)
if (x > 0) print("positive");
```

### 3. No Implicit Returns

```hemlock
// Python: last expression is return value
// def add(a, b): a + b

// Hemlock: explicit return required
fn add(a: i32, b: i32): i32 {
    return a + b;
}

// Or use expression-bodied function
fn add(a: i32, b: i32): i32 => a + b;
```

### 4. Division Always Returns Float

```hemlock
let x = 7 / 2;    // 3.5 (float!)
let y = divi(7, 2); // 3 (integer)
```

### 5. String Indexing Returns Bytes

```hemlock
let s = "hello";
print(s[0]);      // 'h' (rune)
print(s.byte_at(0));  // 104 (ASCII code)

let emoji = "🚀";
print(len(emoji));       // 4 (bytes)
print(emoji.length);     // 1 (character)
```

---

## Your First Week Roadmap

### Day 1-2: Syntax Familiarity
- Write simple programs without memory management
- Use managed arrays and objects
- Get comfortable with Hemlock syntax

### Day 3-4: Memory Basics
- [Why Manual Memory Matters](./why-memory-matters.md)
- [Exercise 1: First Allocation](./exercises/01-first-allocation.md)
- Practice `alloc`, `free`, `defer`

### Day 5-6: Deeper Understanding
- [Stack vs Heap](./exercises/02-stack-vs-heap.md)
- [Pointers Explained](./exercises/03-pointers-explained.md)
- Understand ownership and lifetimes

### Day 7: Practical Application
- Build something small (a command-line tool)
- Mix managed and manual memory as appropriate
- Read the generated C with `hemlockc --keep-c`

---

## Recommended Learning Projects

### 1. File Counter (Beginner)
Count lines, words, and characters in a file.
```hemlock
// Use file I/O and string operations
// No manual memory needed
```

### 2. Simple Allocator (Intermediate)
Build a bump allocator.
```hemlock
// Teaches: memory management internals
// Skills: pointers, arithmetic
```

### 3. Thread Pool (Advanced)
Create a pool of workers processing tasks.
```hemlock
// Teaches: concurrency, channels
// Skills: async, spawn, join
```

### 4. Mini Database (Advanced)
Store and query records from disk.
```hemlock
// Teaches: file I/O, memory layout
// Skills: serialization, pointers
```

---

## Getting Help

- Read error messages carefully - they're designed to help
- Check [Common Mistakes](./common-mistakes.md) for debugging
- Examine generated C code: `hemlockc program.hml --keep-c`
- Print intermediate values liberally

---

*"The best time to learn systems programming was yesterday. The second best time is today."*
