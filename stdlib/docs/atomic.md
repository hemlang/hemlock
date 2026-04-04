# Hemlock Atomic Operations Module

A standard library module providing lock-free atomic operations for concurrent programming in Hemlock.

## Overview

The atomic module provides low-level atomic primitives for building lock-free data structures and algorithms:

- **Atomic loads and stores** - Thread-safe read/write of shared memory
- **Fetch-and-modify operations** - Atomic arithmetic and bitwise operations
- **Compare-and-swap (CAS)** - Foundation for lock-free algorithms
- **Atomic exchange** - Swap values atomically
- **Memory fence** - Full memory barrier for ordering guarantees

All operations use **sequential consistency** (`memory_order_seq_cst`), providing the strongest ordering guarantees.

## Usage

```hemlock
import { atomic_load_i32, atomic_store_i32, atomic_add_i32 } from "@stdlib/atomic";

let counter = alloc(4);
ptr_write_i32(counter, 0);

atomic_add_i32(counter, 1);
let val = atomic_load_i32(counter);
print(val);  // 1

free(counter);
```

Or import all:

```hemlock
import * as atomic from "@stdlib/atomic";
atomic.atomic_store_i32(p, 42);
```

---

## Important: Pointer-Based Operations

All atomic operations work on **raw pointers** (memory obtained via `alloc()`), not on regular Hemlock variables. You must:

1. Allocate memory with `alloc()`
2. Initialize it with `ptr_write_i32()` or `ptr_write_i64()`
3. Use atomic functions on the pointer
4. Free the memory with `free()` when done

```hemlock
// CORRECT - atomic on allocated memory
let p = alloc(4);
ptr_write_i32(p, 0);
atomic_add_i32(p, 1);
free(p);

// WRONG - cannot use atomics on regular variables
// let x = 0;
// atomic_add_i32(x, 1);  // ERROR: x is not a pointer
```

---

## i32 Atomic Operations

### atomic_load_i32(ptr)
Atomically reads an i32 value from memory.

**Parameters:**
- `ptr: ptr` - Pointer to an i32 value in memory

**Returns:** `i32` - The value read atomically

```hemlock
import { atomic_load_i32 } from "@stdlib/atomic";

let p = alloc(4);
ptr_write_i32(p, 42);

let val = atomic_load_i32(p);
print(val);  // 42

free(p);
```

### atomic_store_i32(ptr, value)
Atomically writes an i32 value to memory.

**Parameters:**
- `ptr: ptr` - Pointer to an i32 location in memory
- `value: i32` - Value to store

**Returns:** `null`

```hemlock
import { atomic_store_i32, atomic_load_i32 } from "@stdlib/atomic";

let p = alloc(4);
atomic_store_i32(p, 100);
print(atomic_load_i32(p));  // 100

free(p);
```

### atomic_add_i32(ptr, value)
Atomically adds a value and returns the **old** value.

**Parameters:**
- `ptr: ptr` - Pointer to an i32 value in memory
- `value: i32` - Value to add

**Returns:** `i32` - The previous value (before the addition)

```hemlock
import { atomic_add_i32, atomic_load_i32 } from "@stdlib/atomic";

let p = alloc(4);
ptr_write_i32(p, 10);

let old = atomic_add_i32(p, 5);
print(old);                    // 10 (old value)
print(atomic_load_i32(p));     // 15 (new value)

free(p);
```

### atomic_sub_i32(ptr, value)
Atomically subtracts a value and returns the **old** value.

**Parameters:**
- `ptr: ptr` - Pointer to an i32 value in memory
- `value: i32` - Value to subtract

**Returns:** `i32` - The previous value (before the subtraction)

```hemlock
import { atomic_sub_i32, atomic_load_i32 } from "@stdlib/atomic";

let p = alloc(4);
ptr_write_i32(p, 20);

let old = atomic_sub_i32(p, 7);
print(old);                    // 20 (old value)
print(atomic_load_i32(p));     // 13 (new value)

free(p);
```

### atomic_and_i32(ptr, value)
Atomically performs bitwise AND and returns the **old** value.

**Parameters:**
- `ptr: ptr` - Pointer to an i32 value in memory
- `value: i32` - Value to AND with

**Returns:** `i32` - The previous value (before the AND)

```hemlock
import { atomic_and_i32, atomic_load_i32 } from "@stdlib/atomic";

let p = alloc(4);
ptr_write_i32(p, 0xFF);

let old = atomic_and_i32(p, 0x0F);
print(old);                    // 255 (0xFF, old value)
print(atomic_load_i32(p));     // 15 (0x0F, new value)

free(p);
```

### atomic_or_i32(ptr, value)
Atomically performs bitwise OR and returns the **old** value.

**Parameters:**
- `ptr: ptr` - Pointer to an i32 value in memory
- `value: i32` - Value to OR with

**Returns:** `i32` - The previous value (before the OR)

```hemlock
import { atomic_or_i32, atomic_load_i32 } from "@stdlib/atomic";

let p = alloc(4);
ptr_write_i32(p, 0xF0);

let old = atomic_or_i32(p, 0x0F);
print(old);                    // 240 (0xF0, old value)
print(atomic_load_i32(p));     // 255 (0xFF, new value)

free(p);
```

### atomic_xor_i32(ptr, value)
Atomically performs bitwise XOR and returns the **old** value.

**Parameters:**
- `ptr: ptr` - Pointer to an i32 value in memory
- `value: i32` - Value to XOR with

**Returns:** `i32` - The previous value (before the XOR)

```hemlock
import { atomic_xor_i32, atomic_load_i32 } from "@stdlib/atomic";

let p = alloc(4);
ptr_write_i32(p, 0xFF);

let old = atomic_xor_i32(p, 0x0F);
print(old);                    // 255 (0xFF, old value)
print(atomic_load_i32(p));     // 240 (0xF0, new value)

free(p);
```

### atomic_cas_i32(ptr, expected, desired)
Atomically compares the value at `ptr` with `expected`. If they match, stores `desired`. This is the foundation of most lock-free algorithms.

**Parameters:**
- `ptr: ptr` - Pointer to an i32 value in memory
- `expected: i32` - The value expected to be at `ptr`
- `desired: i32` - The value to store if the comparison succeeds

**Returns:** `bool` - `true` if the swap succeeded, `false` otherwise

```hemlock
import { atomic_cas_i32, atomic_load_i32 } from "@stdlib/atomic";

let p = alloc(4);
ptr_write_i32(p, 42);

// CAS succeeds: value is 42 as expected, set to 100
let success = atomic_cas_i32(p, 42, 100);
print(success);                // true
print(atomic_load_i32(p));     // 100

// CAS fails: value is 100, not 42
let failed = atomic_cas_i32(p, 42, 200);
print(failed);                 // false
print(atomic_load_i32(p));     // 100 (unchanged)

free(p);
```

### atomic_exchange_i32(ptr, value)
Atomically replaces the value at `ptr` and returns the **old** value.

**Parameters:**
- `ptr: ptr` - Pointer to an i32 value in memory
- `value: i32` - The new value to store

**Returns:** `i32` - The previous value

```hemlock
import { atomic_exchange_i32 } from "@stdlib/atomic";

let p = alloc(4);
ptr_write_i32(p, 42);

let old = atomic_exchange_i32(p, 999);
print(old);  // 42

free(p);
```

---

## i64 Atomic Operations

All i64 operations mirror their i32 counterparts but operate on 64-bit integers. Use `alloc(8)` for i64 values and `ptr_write_i64()` / `ptr_read_i64()` for initialization.

### atomic_load_i64(ptr)
Atomically reads an i64 value from memory.

**Parameters:**
- `ptr: ptr` - Pointer to an i64 value in memory

**Returns:** `i64` - The value read atomically

### atomic_store_i64(ptr, value)
Atomically writes an i64 value to memory.

**Parameters:**
- `ptr: ptr` - Pointer to an i64 location in memory
- `value: i64` - Value to store

**Returns:** `null`

### atomic_add_i64(ptr, value)
Atomically adds a value and returns the **old** value.

**Parameters:**
- `ptr: ptr` - Pointer to an i64 value in memory
- `value: i64` - Value to add

**Returns:** `i64` - The previous value (before the addition)

### atomic_sub_i64(ptr, value)
Atomically subtracts a value and returns the **old** value.

**Parameters:**
- `ptr: ptr` - Pointer to an i64 value in memory
- `value: i64` - Value to subtract

**Returns:** `i64` - The previous value (before the subtraction)

### atomic_and_i64(ptr, value)
Atomically performs bitwise AND and returns the **old** value.

**Parameters:**
- `ptr: ptr` - Pointer to an i64 value in memory
- `value: i64` - Value to AND with

**Returns:** `i64` - The previous value (before the AND)

### atomic_or_i64(ptr, value)
Atomically performs bitwise OR and returns the **old** value.

**Parameters:**
- `ptr: ptr` - Pointer to an i64 value in memory
- `value: i64` - Value to OR with

**Returns:** `i64` - The previous value (before the OR)

### atomic_xor_i64(ptr, value)
Atomically performs bitwise XOR and returns the **old** value.

**Parameters:**
- `ptr: ptr` - Pointer to an i64 value in memory
- `value: i64` - Value to XOR with

**Returns:** `i64` - The previous value (before the XOR)

### atomic_cas_i64(ptr, expected, desired)
Atomically compares and swaps a 64-bit value.

**Parameters:**
- `ptr: ptr` - Pointer to an i64 value in memory
- `expected: i64` - The value expected to be at `ptr`
- `desired: i64` - The value to store if the comparison succeeds

**Returns:** `bool` - `true` if the swap succeeded, `false` otherwise

### atomic_exchange_i64(ptr, value)
Atomically replaces a 64-bit value and returns the old value.

**Parameters:**
- `ptr: ptr` - Pointer to an i64 value in memory
- `value: i64` - The new value to store

**Returns:** `i64` - The previous value

```hemlock
import { atomic_load_i64, atomic_store_i64, atomic_add_i64 } from "@stdlib/atomic";

let p = alloc(8);
ptr_write_i64(p, 0);

atomic_add_i64(p, 5000000000);
print(atomic_load_i64(p));  // 5000000000

free(p);
```

---

## Memory Fence

### atomic_fence()
Issues a full memory fence (barrier), ensuring that all memory operations before the fence are visible to all threads before any operations after the fence.

**Parameters:** None

**Returns:** `null`

**Use cases:**
- Enforcing ordering between non-atomic and atomic operations
- Implementing custom synchronization protocols
- Ensuring visibility of writes across threads

```hemlock
import { atomic_fence } from "@stdlib/atomic";

// Ensure all prior writes are visible before continuing
ptr_write_i32(data_ptr, 42);
atomic_fence();
ptr_write_i32(flag_ptr, 1);  // Signal that data is ready
```

---

## Examples

### Atomic Counter (Multi-threaded)

```hemlock
import { atomic_add_i32, atomic_load_i32 } from "@stdlib/atomic";

let counter = alloc(4);
ptr_write_i32(counter, 0);

// Spawn multiple tasks that increment the counter
let tasks = [];
for (let i = 0; i < 10; i++) {
    let t = spawn(fn() {
        for (let j = 0; j < 1000; j++) {
            atomic_add_i32(counter, 1);
        }
    });
    tasks.push(t);
}

// Wait for all tasks to complete
for (t in tasks) {
    join(t);
}

print(atomic_load_i32(counter));  // 10000 (guaranteed correct)
free(counter);
```

### CAS Loop (Lock-Free Update)

The CAS loop is the standard pattern for lock-free updates. It retries until the update succeeds without interference from other threads.

```hemlock
import { atomic_load_i32, atomic_cas_i32 } from "@stdlib/atomic";

let value = alloc(4);
ptr_write_i32(value, 0);

// Lock-free increment using CAS loop
fn cas_increment(ptr: ptr): null {
    loop {
        let old = atomic_load_i32(ptr);
        let new_val = old + 1;
        if (atomic_cas_i32(ptr, old, new_val)) {
            break;  // CAS succeeded, done
        }
        // CAS failed (another thread changed the value), retry
    }
    return null;
}

// Safe to call from multiple threads
let tasks = [];
for (let i = 0; i < 10; i++) {
    tasks.push(spawn(cas_increment, value));
}
for (t in tasks) {
    join(t);
}

print(atomic_load_i32(value));  // 10
free(value);
```

### Spin Lock

A simple spin lock built with atomic CAS:

```hemlock
import { atomic_cas_i32, atomic_store_i32 } from "@stdlib/atomic";

let lock = alloc(4);
ptr_write_i32(lock, 0);  // 0 = unlocked, 1 = locked

fn acquire(lock_ptr: ptr): null {
    loop {
        if (atomic_cas_i32(lock_ptr, 0, 1)) {
            break;  // Acquired the lock
        }
        // Spin until lock is available
    }
    return null;
}

fn release(lock_ptr: ptr): null {
    atomic_store_i32(lock_ptr, 0);
    return null;
}

// Usage
acquire(lock);
// ... critical section ...
release(lock);

free(lock);
```

### Atomic Flag (Signal Between Threads)

```hemlock
import { atomic_store_i32, atomic_load_i32, atomic_fence } from "@stdlib/atomic";

let data = alloc(4);
let ready = alloc(4);
ptr_write_i32(data, 0);
ptr_write_i32(ready, 0);

// Producer thread
let producer = spawn(fn() {
    ptr_write_i32(data, 42);     // Write data
    atomic_fence();               // Ensure data is visible
    atomic_store_i32(ready, 1);   // Signal that data is ready
});

// Consumer thread
let consumer = spawn(fn() {
    // Wait for data to be ready
    while (atomic_load_i32(ready) == 0) {
        // Spin wait
    }
    atomic_fence();               // Ensure we see the data write
    let val = ptr_read_i32(data);
    print("Received: " + val);    // Received: 42
});

join(producer);
join(consumer);

free(data);
free(ready);
```

---

## Memory Ordering

All atomic operations in Hemlock use **sequential consistency** (`memory_order_seq_cst`). This is the strongest memory ordering guarantee:

- All threads observe all modifications in the same order
- No reordering of atomic operations across threads
- Simplest to reason about, but may have a small performance cost on some architectures

This design follows Hemlock's philosophy of **explicit over implicit** -- you get correct behavior by default without needing to reason about relaxed memory orderings.

---

## API Reference

### i32 Operations

| Function | Parameters | Returns | Description |
|----------|-----------|---------|-------------|
| `atomic_load_i32` | `(ptr)` | `i32` | Atomic read |
| `atomic_store_i32` | `(ptr, value)` | `null` | Atomic write |
| `atomic_add_i32` | `(ptr, value)` | `i32` | Add, return old |
| `atomic_sub_i32` | `(ptr, value)` | `i32` | Subtract, return old |
| `atomic_and_i32` | `(ptr, value)` | `i32` | Bitwise AND, return old |
| `atomic_or_i32` | `(ptr, value)` | `i32` | Bitwise OR, return old |
| `atomic_xor_i32` | `(ptr, value)` | `i32` | Bitwise XOR, return old |
| `atomic_cas_i32` | `(ptr, expected, desired)` | `bool` | Compare-and-swap |
| `atomic_exchange_i32` | `(ptr, value)` | `i32` | Exchange, return old |

### i64 Operations

| Function | Parameters | Returns | Description |
|----------|-----------|---------|-------------|
| `atomic_load_i64` | `(ptr)` | `i64` | Atomic read |
| `atomic_store_i64` | `(ptr, value)` | `null` | Atomic write |
| `atomic_add_i64` | `(ptr, value)` | `i64` | Add, return old |
| `atomic_sub_i64` | `(ptr, value)` | `i64` | Subtract, return old |
| `atomic_and_i64` | `(ptr, value)` | `i64` | Bitwise AND, return old |
| `atomic_or_i64` | `(ptr, value)` | `i64` | Bitwise OR, return old |
| `atomic_xor_i64` | `(ptr, value)` | `i64` | Bitwise XOR, return old |
| `atomic_cas_i64` | `(ptr, expected, desired)` | `bool` | Compare-and-swap |
| `atomic_exchange_i64` | `(ptr, value)` | `i64` | Exchange, return old |

### Fence

| Function | Parameters | Returns | Description |
|----------|-----------|---------|-------------|
| `atomic_fence` | `()` | `null` | Full memory barrier |

---

## See Also

- **Async module** (`@stdlib/async`) - ThreadPool and parallel_map
- **Memory management** - `alloc()`, `free()`, `ptr_write_*`, `ptr_read_*` builtins
- **Channels** - Higher-level thread communication via `channel()`

---

## License

Part of the Hemlock standard library.
