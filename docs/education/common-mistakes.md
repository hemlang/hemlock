# Common Mistakes and How to Debug Them

> "Every crash is a lesson. Every bug is a teacher."

This guide covers the most common mistakes when learning systems programming with Hemlock, why they happen, how to recognize them, and how to fix them.

---

## Table of Contents

1. [Memory Mistakes](#memory-mistakes)
   - [Double Free](#1-double-free)
   - [Use After Free](#2-use-after-free)
   - [Memory Leak](#3-memory-leak)
   - [Buffer Overflow](#4-buffer-overflow)
   - [Dangling Pointer](#5-dangling-pointer)
   - [Null Pointer Dereference](#6-null-pointer-dereference)
   - [Uninitialized Memory](#7-uninitialized-memory)
2. [Type Mistakes](#type-mistakes)
   - [Integer Overflow](#8-integer-overflow)
   - [Type Mismatch](#9-type-mismatch)
   - [Precision Loss](#10-precision-loss)
3. [Concurrency Mistakes](#concurrency-mistakes)
   - [Race Condition](#11-race-condition)
   - [Use After Spawn](#12-use-after-spawn)
   - [Deadlock](#13-deadlock)
4. [Logic Mistakes](#logic-mistakes)
   - [Off-by-One Error](#14-off-by-one-error)
   - [Integer Division](#15-integer-division)
   - [Forgetting Defer](#16-forgetting-defer)

---

## Memory Mistakes

### 1. Double Free

**What it is:** Freeing the same memory twice.

**Example:**
```hemlock
let p = alloc(64);
// ... use p ...
free(p);

// Later, forgetting it was freed...
free(p);  // CRASH: Double free!
```

**Why it's bad:** The memory allocator's internal bookkeeping gets corrupted. This can cause crashes, data corruption, or security vulnerabilities.

**Symptoms:**
- Runtime error: "Double-free detected"
- In C: Crash with no message, or exploitable vulnerability

**How to fix:**
```hemlock
// Option 1: Set to null after free
free(p);
p = ptr_null();

// Now double-free is detectable
if (p != ptr_null()) {
    free(p);
}

// Option 2: Track ownership clearly
fn destroy_thing(p: ptr) {
    // Document: This function takes ownership and frees
    free(p);
}
```

**Prevention:**
- Set pointers to `ptr_null()` immediately after freeing
- Document ownership: who allocates, who frees
- Use `defer` for cleanup

---

### 2. Use After Free

**What it is:** Accessing memory that has been freed.

**Example:**
```hemlock
let p = alloc(64);
ptr_write_i32(p, 42);
free(p);

// Memory is freed but pointer still exists
let x = ptr_read_i32(p);  // UNDEFINED BEHAVIOR!
print(x);  // Might print 42, garbage, or crash
```

**Why it's bad:** The freed memory may be reallocated for something else. Reading gives garbage; writing corrupts other data.

**Symptoms:**
- Random crashes
- Corrupted data
- "Impossible" values appearing
- Works sometimes, fails other times

**How to fix:**
```hemlock
// Always nullify after free
free(p);
p = ptr_null();

// Check before use
if (p != ptr_null()) {
    let x = ptr_read_i32(p);
}
```

---

### 3. Memory Leak

**What it is:** Allocating memory but never freeing it.

**Example:**
```hemlock
fn process_data() {
    let p = alloc(1000);
    // ... do work ...

    if (error_occurred()) {
        return;  // LEAK! Forgot to free p
    }

    free(p);
}
```

**Why it's bad:** Memory accumulates over time. Long-running programs eventually exhaust system memory.

**Symptoms:**
- Memory usage grows continuously
- Program slows down over time
- Eventually: "Out of memory" error

**How to fix:**
```hemlock
// Use defer for automatic cleanup
fn process_data() {
    let p = alloc(1000);
    defer free(p);  // Will run no matter how function exits

    if (error_occurred()) {
        return;  // defer handles cleanup
    }

    // ... more work ...
}  // defer runs here too
```

**Prevention:**
- Use `defer` for all allocations
- Match every `alloc` with a `free`
- Use tracking tools to detect leaks

---

### 4. Buffer Overflow

**What it is:** Writing past the end of allocated memory.

**Example:**
```hemlock
let p = alloc(10);  // Only 10 bytes

// Writing beyond bounds
for (let i = 0; i < 20; i++) {
    ptr_write_u8(p + i, i);  // Overflow when i >= 10!
}

free(p);
```

**Why it's bad:** Overwrites adjacent memory, corrupting data or control structures. The #1 source of security vulnerabilities.

**Symptoms:**
- Crashes
- Corrupted data in unrelated variables
- Security exploits (in real-world code)

**How to fix:**
```hemlock
// Option 1: Use buffer for bounds checking
let b = buffer(10);
b[15] = 42;  // Runtime error: "Index out of bounds"

// Option 2: Check bounds manually
let size = 10;
let p = alloc(size);
for (let i = 0; i < 20; i++) {
    if (i < size) {
        ptr_write_u8(p + i, i);
    } else {
        print("Warning: skipping out-of-bounds write");
    }
}
free(p);
```

---

### 5. Dangling Pointer

**What it is:** A pointer to memory that no longer belongs to you.

**Example:**
```hemlock
fn get_local_address(): ptr {
    let x = 42;  // x is on the stack
    return &x;   // Return address of stack variable
}  // x is destroyed here!

let p = get_local_address();
let value = ptr_read_i32(p);  // DANGLING: x no longer exists
```

**Why it's bad:** Stack memory is reused. Your pointer now points to some other function's variables or garbage.

**Symptoms:**
- Random values
- Values that change unexpectedly
- Crashes when dereferencing

**How to fix:**
```hemlock
// Option 1: Return the value, not a pointer
fn get_value(): i32 {
    let x = 42;
    return x;  // Value is copied out
}

// Option 2: Allocate on heap (caller must free)
fn get_heap_value(): ptr {
    let p = alloc(sizeof(i32));
    ptr_write_i32(p, 42);
    return p;
}
```

---

### 6. Null Pointer Dereference

**What it is:** Trying to read or write through a null pointer.

**Example:**
```hemlock
fn find_item(key: string): ptr {
    // Returns null if not found
    return ptr_null();
}

let p = find_item("missing");
let value = ptr_read_i32(p);  // CRASH: Null dereference!
```

**Why it's bad:** Address 0 is never valid memory. Dereferencing it crashes the program.

**Symptoms:**
- Immediate crash
- "Segmentation fault" in C
- Runtime error in Hemlock

**How to fix:**
```hemlock
let p = find_item("missing");
if (p == ptr_null()) {
    print("Item not found");
} else {
    let value = ptr_read_i32(p);
    print("Found: " + value);
}
```

---

### 7. Uninitialized Memory

**What it is:** Reading memory before writing to it.

**Example:**
```hemlock
let p = alloc(100);
// Forgot to initialize!
let x = ptr_read_i32(p);  // Reading garbage
print(x);  // Unpredictable value
```

**Why it's bad:** Uninitialized memory contains whatever was there before - could be zeros, old data, or garbage.

**Symptoms:**
- Different results each run
- "Impossible" values
- Works on your machine, fails elsewhere

**How to fix:**
```hemlock
// Option 1: Zero-initialize with memset
let p = alloc(100);
memset(p, 0, 100);

// Option 2: Initialize immediately
let p = alloc(sizeof(i32));
ptr_write_i32(p, 0);  // Explicit initialization

// Option 3: Use talloc (zeroed allocation)
let p = talloc(i32, 25);  // 25 zeroed i32s
```

---

## Type Mistakes

### 8. Integer Overflow

**What it is:** Arithmetic that exceeds a type's range.

**Example:**
```hemlock
let x: i8 = 127;    // i8 max is 127
x = x + 1;          // Overflow! Wraps to -128
print(x);           // -128
```

**Why it's bad:** Values wrap around unexpectedly. Can cause infinite loops, security bugs, or corrupted calculations.

**Symptoms:**
- Negative numbers appearing from positive arithmetic
- Infinite loops
- Wrong calculations

**How to fix:**
```hemlock
// Use a larger type
let x: i32 = 127;
x = x + 1;
print(x);  // 128

// Or check before operating
let x: i8 = 127;
if (x < 127) {
    x = x + 1;
} else {
    print("Would overflow!");
}
```

---

### 9. Type Mismatch

**What it is:** Using a value as the wrong type.

**Example:**
```hemlock
let p = alloc(4);
ptr_write_i32(p, 42);

// Read as wrong type
let f = ptr_read_f32(p);  // Interprets integer bits as float!
print(f);  // Some weird float value, not 42.0
```

**Why it's bad:** Memory is just bytes. Interpretation depends on how you read it.

**Symptoms:**
- Completely wrong values
- NaN or infinity in floats
- Garbage when reading strings

**How to fix:**
```hemlock
// Be consistent in read/write types
let p = alloc(4);
ptr_write_f32(p, 42.0);
let f = ptr_read_f32(p);  // Correct
print(f);  // 42.0
```

---

### 10. Precision Loss

**What it is:** Converting between types that can't represent the same values.

**Example:**
```hemlock
let big: i64 = 9007199254740993;  // Larger than f64 can represent exactly
let f: f64 = big;                  // Precision lost!
print(f);  // 9007199254740992 (off by 1)
```

**Why it's bad:** Silent data corruption. Looks correct but isn't.

**Symptoms:**
- Off-by-one errors in large numbers
- Financial calculations going wrong
- IDs that don't match

**How to fix:**
```hemlock
// Keep types consistent
let big: i64 = 9007199254740993;
// Don't convert to float if you need exact precision

// Use string for display
print("Big number: " + big);
```

---

## Concurrency Mistakes

### 11. Race Condition

**What it is:** Two tasks accessing shared data without synchronization.

**Example:**
```hemlock
let counter = 0;

async fn increment() {
    for (let i = 0; i < 1000; i++) {
        counter = counter + 1;  // RACE: read-modify-write is not atomic
    }
}

let t1 = spawn(increment);
let t2 = spawn(increment);
join(t1);
join(t2);

print(counter);  // Often NOT 2000!
```

**Why it's bad:** Both tasks read the same value, add 1, and write back. Some increments are lost.

**Symptoms:**
- Non-deterministic results
- Lost updates
- Intermittent failures

**How to fix:**
```hemlock
// Option 1: Use atomic operations
let p = alloc(sizeof(i32));
atomic_store_i32(p, 0);

async fn increment() {
    for (let i = 0; i < 1000; i++) {
        atomic_add_i32(p, 1);  // Atomic: no race
    }
}

// Option 2: Use channels for communication
let ch = channel(1);
// Send updates through channel instead of shared state
```

---

### 12. Use After Spawn

**What it is:** Freeing memory while a spawned task is still using it.

**Example:**
```hemlock
async fn worker(data: ptr) {
    sleep(100);  // Simulating work
    print(ptr_read_i32(data));  // CRASH: data is freed!
}

let p = alloc(4);
ptr_write_i32(p, 42);
spawn(worker, p);
free(p);  // Freed while worker is still running!
```

**Why it's bad:** The spawned task has a reference to memory you freed.

**Symptoms:**
- Crashes in async code
- Corrupted data
- Intermittent failures

**How to fix:**
```hemlock
// Wait for task before freeing
let p = alloc(4);
ptr_write_i32(p, 42);
let task = spawn(worker, p);
join(task);  // Wait for completion
free(p);     // Safe now
```

---

### 13. Deadlock

**What it is:** Tasks waiting on each other forever.

**Example:**
```hemlock
let ch1 = channel(0);  // Unbuffered
let ch2 = channel(0);

async fn task1() {
    ch1.recv();       // Wait for ch1
    ch2.send("done"); // Then send on ch2
}

async fn task2() {
    ch2.recv();       // Wait for ch2 (but task1 is waiting for ch1!)
    ch1.send("done");
}

spawn(task1);
spawn(task2);
// DEADLOCK: Both tasks waiting forever
```

**Why it's bad:** Program hangs forever with no error message.

**Symptoms:**
- Program hangs
- No output
- CPU at 0%

**How to fix:**
```hemlock
// Establish a clear order
// OR use buffered channels
let ch1 = channel(1);  // Buffered: send doesn't block if empty
let ch2 = channel(1);

// OR use timeout mechanisms
// OR restructure to avoid circular dependencies
```

---

## Logic Mistakes

### 14. Off-by-One Error

**What it is:** Loop bounds or index calculations off by one.

**Example:**
```hemlock
let arr = [1, 2, 3, 4, 5];

// WRONG: Accessing arr[5] which doesn't exist
for (let i = 0; i <= len(arr); i++) {
    print(arr[i]);  // Crashes on last iteration
}
```

**Symptoms:**
- Index out of bounds errors
- Missing first or last element
- Extra iteration

**How to fix:**
```hemlock
// Use < not <=
for (let i = 0; i < len(arr); i++) {
    print(arr[i]);
}

// Or use for-in
for (x in arr) {
    print(x);
}
```

---

### 15. Integer Division

**What it is:** Using `/` expecting integer result.

**Example:**
```hemlock
let a = 7;
let b = 2;
let result = a / b;
print(result);  // 3.5 (float!) not 3
```

**In Hemlock:** The `/` operator always returns a float!

**How to fix:**
```hemlock
// Use divi for integer division
let result = divi(a, b);
print(result);  // 3

// Or cast the float result
let result = i32(a / b);
print(result);  // 3
```

---

### 16. Forgetting Defer

**What it is:** Returning early without cleaning up.

**Example:**
```hemlock
fn process_file(path: string) {
    let f = open(path, "r");

    if (!validate(path)) {
        return;  // LEAK: File not closed!
    }

    let data = f.read();
    f.close();
    return data;
}
```

**How to fix:**
```hemlock
fn process_file(path: string) {
    let f = open(path, "r");
    defer f.close();  // Will ALWAYS run

    if (!validate(path)) {
        return;  // defer handles close
    }

    return f.read();
}  // defer runs here too
```

---

## Debugging Strategies

### 1. Add Print Statements

```hemlock
print("DEBUG: Entering function");
print("DEBUG: p = " + p);
print("DEBUG: value = " + ptr_read_i32(p));
```

### 2. Check Assumptions

```hemlock
fn process(p: ptr, size: i32) {
    // Verify inputs
    if (p == ptr_null()) {
        panic("process: p is null");
    }
    if (size <= 0) {
        panic("process: size must be positive, got " + size);
    }
    // ... proceed ...
}
```

### 3. Use Assertions

```hemlock
import { assert, assert_eq } from "@stdlib/assert";

assert(p != ptr_null(), "Pointer should not be null");
assert_eq(len(arr), 5, "Array should have 5 elements");
```

### 4. Isolate the Problem

- Comment out code until the bug disappears
- Add code back until it reappears
- That's where the bug lives

### 5. Compile and Inspect

```bash
./hemlockc program.hml -o program --keep-c
cat program.c  # See what the compiler generated
```

---

## Quick Reference: Error Messages

| Error | Cause | Fix |
|-------|-------|-----|
| "Double-free detected" | Freeing same pointer twice | Null pointer after free |
| "Index out of bounds" | Array/buffer access beyond size | Check index < length |
| "Null pointer dereference" | Reading/writing through null | Check `p != ptr_null()` |
| "Type mismatch" | Wrong type in annotation | Fix type or conversion |
| "Division by zero" | Dividing by 0 | Check divisor before dividing |

---

## Summary

**Memory rules:**
1. Every `alloc` needs exactly one `free`
2. Never use memory after freeing it
3. Never return pointers to stack variables
4. Always check for null before dereferencing
5. Initialize memory before reading it

**Concurrency rules:**
1. Protect shared data with atomics or channels
2. Wait for tasks before freeing their data
3. Avoid circular wait patterns

**General rules:**
1. Use `defer` for cleanup
2. Check bounds before indexing
3. Use `divi()` for integer division
4. Validate assumptions early

---

*"Debugging is twice as hard as writing the code in the first place. Therefore, if you write the code as cleverly as possible, you are, by definition, not smart enough to debug it."* — Brian Kernighan
