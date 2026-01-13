# Why Manual Memory Matters

> "You can't truly understand computers until you understand memory."

This document explains why learning manual memory management is essential for becoming a complete programmer, even if you never write production systems code.

---

## The Hidden Machine

When you write Python or JavaScript, an enormous amount of work happens invisibly:

```python
# Python
x = [1, 2, 3, 4, 5]
y = x + [6, 7, 8]
x = None  # Is the memory freed? When? How?
```

**Questions a systems programmer asks:**
- Where does `[1, 2, 3, 4, 5]` live in memory?
- How many bytes does it occupy?
- When `y = x + [6, 7, 8]`, does it copy or reference `x`?
- When `x = None`, what happens to the original list?
- Why does this simple code potentially allocate memory three times?

These aren't pedantic questions. Understanding them explains:
- Why your program uses unexpected amounts of RAM
- Why certain operations are slow
- Why memory leaks happen
- How to write efficient code in any language

---

## What Memory Actually Is

Your computer's memory (RAM) is essentially a giant array of bytes:

```
Address:  0x0000  0x0001  0x0002  0x0003  0x0004  ...
          ┌──────┬──────┬──────┬──────┬──────┬─────
Value:    │  0x42│  0x00│  0x00│  0x00│  0xFF│ ...
          └──────┴──────┴──────┴──────┴──────┴─────
```

Every piece of data in your program lives somewhere in this array. A "pointer" is just the address (index) where the data starts.

```hemlock
// In Hemlock, you can see this directly
let p = alloc(4);           // Get 4 bytes of memory
print(p);                   // Prints something like: ptr(0x7f8a2b3c4d50)

ptr_write_i32(p, 42);       // Write the number 42 (4 bytes) at that address
let value = ptr_read_i32(p); // Read it back
print(value);               // 42

free(p);                    // Return the memory to the system
```

---

## The Two Regions: Stack and Heap

Memory is divided into two main regions with very different properties:

### The Stack: Fast but Limited

```
┌─────────────────────────────────┐
│           THE STACK             │
├─────────────────────────────────┤
│  • Automatic allocation         │
│  • Very fast (just move pointer)│
│  • Fixed size (usually ~8MB)    │
│  • LIFO: last in, first out     │
│  • Variables die when function  │
│    returns                      │
└─────────────────────────────────┘
```

```hemlock
fn example() {
    let x = 42;        // x lives on the stack
    let y = 100;       // y lives on the stack, right after x
    // When example() returns, x and y are automatically gone
}
```

### The Heap: Flexible but Manual

```
┌─────────────────────────────────┐
│           THE HEAP              │
├─────────────────────────────────┤
│  • Manual allocation            │
│  • Slower (bookkeeping needed)  │
│  • Limited only by RAM          │
│  • Allocate/free in any order   │
│  • YOU must free it             │
└─────────────────────────────────┘
```

```hemlock
fn example() {
    let p = alloc(1000000);  // 1MB on the heap
    // p (the pointer) is on the stack
    // The 1MB of data is on the heap

    // If you don't free(p) before returning,
    // you've just leaked 1MB of memory
    free(p);
}
```

### Why Two Regions?

**Stack limitations:**
```hemlock
fn cant_return_stack_pointer(): ptr {
    let x = 42;
    return &x;  // WRONG! x dies when function returns
                // The pointer becomes invalid ("dangling")
}

fn correct_heap_return(): ptr {
    let p = alloc(4);
    ptr_write_i32(p, 42);
    return p;   // CORRECT: Heap memory survives function return
                // Caller must free it eventually
}
```

---

## What Garbage Collection Hides

In Python, this "just works":

```python
def make_list():
    return [1, 2, 3, 4, 5]

x = make_list()  # Where does this memory come from?
x = None         # Where does the memory go?
```

Behind the scenes, Python's garbage collector:
1. Allocates heap memory for `[1, 2, 3, 4, 5]`
2. Tracks how many references point to it
3. Periodically scans for unreachable objects
4. Frees memory when nothing references it

**The hidden costs:**
- GC pauses can cause latency spikes
- Reference counting has overhead on every assignment
- Memory isn't freed immediately (unpredictable timing)
- Hard to reason about memory usage

**In Hemlock, you control everything:**

```hemlock
fn make_array(): array {
    return [1, 2, 3, 4, 5];  // Hemlock arrays are reference-counted
}

// For manual control, use raw pointers:
fn make_raw_data(): ptr {
    let p = alloc(20);  // 5 integers × 4 bytes
    for (let i = 0; i < 5; i++) {
        ptr_write_i32(p + i * 4, i + 1);
    }
    return p;  // Caller must free!
}

let data = make_raw_data();
// ... use data ...
free(data);  // Explicit, immediate, deterministic
```

---

## Memory Mistakes: Learning Opportunities

### 1. Use After Free

```hemlock
let p = alloc(64);
ptr_write_i32(p, 42);
free(p);

// MISTAKE: Memory is freed but pointer still exists
let x = ptr_read_i32(p);  // Undefined behavior!
```

**The lesson:** Freeing memory doesn't invalidate pointers. You must track what's valid.

**Real-world impact:** Security vulnerabilities. Attackers exploit use-after-free to execute arbitrary code.

### 2. Double Free

```hemlock
let p = alloc(64);
free(p);
free(p);  // MISTAKE: Already freed!
```

**The lesson:** Memory can only be freed once. Freeing twice corrupts the allocator's bookkeeping.

**Real-world impact:** Crashes, security vulnerabilities, data corruption.

### 3. Memory Leak

```hemlock
fn leaky() {
    let p = alloc(1000000);  // 1MB
    // Forgot to free!
}

// Call this 1000 times = 1GB leaked
for (let i = 0; i < 1000; i++) {
    leaky();
}
```

**The lesson:** Every `alloc` needs a matching `free`. No exceptions.

**Real-world impact:** Programs that slowly consume all system memory.

### 4. Dangling Pointer

```hemlock
fn get_pointer(): ptr {
    let x = 42;
    return &x;  // x lives on the stack
}

let p = get_pointer();
// x no longer exists!
let value = ptr_read_i32(p);  // Reading garbage
```

**The lesson:** Stack variables die when their function returns. Never return pointers to them.

---

## Why This Matters (Even If You Stay High-Level)

### 1. Understanding Performance

```python
# Python: Why is this slow?
result = ""
for i in range(10000):
    result += str(i)  # Creates 10,000 new strings!
```

With memory knowledge, you understand: each `+=` allocates a new string, copies the old content, adds the new character, and schedules the old string for GC.

**Efficient version:**
```python
result = "".join(str(i) for i in range(10000))  # One allocation
```

### 2. Debugging Memory Issues

Memory knowledge helps you understand:
- Why your Python program uses 10GB for a 1GB file
- Why Node.js EventEmitters can cause memory leaks
- Why adding items to a list is sometimes O(1) and sometimes O(n)

### 3. Reading Systems Code

Linux, databases, web servers, game engines - all written in C/C++. Understanding memory lets you:
- Read and understand this code
- Contribute to these projects
- Debug issues at the boundary between your code and systems code

### 4. Language Design Appreciation

Understanding memory helps you appreciate language design tradeoffs:
- Why Rust has the borrow checker
- Why Go's garbage collector is optimized for latency
- Why Java has different GC algorithms to choose from
- Why Python's reference counting + GC combination

---

## Hemlock's Approach: Graduated Safety

Hemlock gives you options at different safety levels:

### Level 1: Fully Managed (Safest)

```hemlock
// Use high-level constructs
let arr = [1, 2, 3];      // Managed array
let str = "hello";         // Managed string
let obj = { x: 1, y: 2 }; // Managed object

// Memory handled automatically via reference counting
// Similar to Python/JS, but you know it's happening
```

### Level 2: Bounds-Checked Buffers

```hemlock
// Manual allocation with safety rails
let b = buffer(64);
b[0] = 42;     // OK
b[100] = 1;    // Runtime error: "Buffer index out of bounds"

// Must still free
free(b);
```

### Level 3: Raw Pointers (Full Control)

```hemlock
// Complete manual control
let p = alloc(64);
ptr_write_i32(p, 42);      // No checking
ptr_write_i32(p + 1000, 1); // No checking (will corrupt memory!)
free(p);
```

---

## Exercises to Build Understanding

### Exercise 1: Measure Allocation

```hemlock
import { time_ms } from "@stdlib/time";

// How long does allocation take?
let start = time_ms();
for (let i = 0; i < 100000; i++) {
    let p = alloc(100);
    free(p);
}
let heap_time = time_ms() - start;
print("100k heap allocs: " + heap_time + "ms");

// Compare to "stack allocation" (just variables)
start = time_ms();
for (let i = 0; i < 100000; i++) {
    let x = 42;  // Stack - much faster
}
let stack_time = time_ms() - start;
print("100k stack vars: " + stack_time + "ms");
```

### Exercise 2: Find the Leak

```hemlock
// This program leaks memory. Can you fix it?
fn process_data(data: ptr) {
    let result = alloc(100);
    // ... do something with result ...

    if (data == ptr_null()) {
        return;  // BUG: result is never freed on this path
    }

    // ... more processing ...
    free(result);
}
```

### Exercise 3: Implement a Simple Allocator

```hemlock
// A naive bump allocator - educational, not production!
let heap = buffer(10000);  // Our "heap"
let offset = 0;

fn my_alloc(size: i32): i32 {
    let ptr = offset;
    offset = offset + size;
    if (offset > 10000) {
        panic("Out of memory!");
    }
    return ptr;  // Return offset into our buffer
}

// Note: No my_free! This allocator can't reclaim memory
// Real allocators are much more complex
```

---

## Key Takeaways

1. **Memory is finite and must be managed** - Either you manage it or a garbage collector does, but someone has to.

2. **Pointers are just addresses** - Demystify them: they're numbers that tell you where data lives.

3. **Stack is automatic, heap is manual** - Know which you're using and why.

4. **Every allocation needs deallocation** - Memory doesn't free itself.

5. **Mistakes are learning opportunities** - Crashes teach you how systems really work.

---

## Next Steps

Ready to apply this knowledge? Continue to:
- [Your First Allocation](./exercises/01-first-allocation.md) - Hands-on memory exercises
- [Common Memory Mistakes](./common-mistakes.md) - Learn to debug memory issues
- [Stack vs Heap Deep Dive](./exercises/02-stack-vs-heap.md) - More on memory regions

---

*"The computer is doing exactly what you told it to do. Learning memory teaches you to be precise about what you ask for."*
