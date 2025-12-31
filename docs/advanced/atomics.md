# Atomic Operations

Hemlock provides atomic operations for **lock-free concurrent programming**. These operations enable safe manipulation of shared memory across multiple threads without traditional locks or mutexes.

## Table of Contents

- [Overview](#overview)
- [When to Use Atomics](#when-to-use-atomics)
- [Memory Model](#memory-model)
- [Atomic Load and Store](#atomic-load-and-store)
- [Fetch-and-Modify Operations](#fetch-and-modify-operations)
- [Compare-and-Swap (CAS)](#compare-and-swap-cas)
- [Atomic Exchange](#atomic-exchange)
- [Memory Fence](#memory-fence)
- [Function Reference](#function-reference)
- [Common Patterns](#common-patterns)
- [Best Practices](#best-practices)
- [Limitations](#limitations)

---

## Overview

Atomic operations are **indivisible** operations that complete without the possibility of interruption. When one thread performs an atomic operation, no other thread can observe the operation in a partially-completed state.

**Key features:**
- All operations use **sequential consistency** (`memory_order_seq_cst`)
- Supported types: **i32** and **i64**
- Operations work on raw pointers allocated with `alloc()`
- Thread-safe without explicit locks

**Available operations:**
- Load/Store - Read and write values atomically
- Add/Sub - Arithmetic operations returning the old value
- And/Or/Xor - Bitwise operations returning the old value
- CAS - Compare-and-swap for conditional updates
- Exchange - Swap values atomically
- Fence - Full memory barrier

---

## When to Use Atomics

**Use atomics for:**
- Counters shared across tasks (e.g., request counts, progress tracking)
- Flags and status indicators
- Lock-free data structures
- Simple synchronization primitives
- Performance-critical concurrent code

**Use channels instead when:**
- Passing complex data between tasks
- Implementing producer-consumer patterns
- You need message-passing semantics

**Example use case - Shared counter:**
```hemlock
// Allocate shared counter
let counter = alloc(4);
ptr_write_i32(counter, 0);

async fn worker(counter: ptr, id: i32) {
    let i = 0;
    while (i < 1000) {
        atomic_add_i32(counter, 1);
        i = i + 1;
    }
}

// Spawn multiple workers
let t1 = spawn(worker, counter, 1);
let t2 = spawn(worker, counter, 2);
let t3 = spawn(worker, counter, 3);

join(t1);
join(t2);
join(t3);

// Counter will be exactly 3000 (no data races)
print(atomic_load_i32(counter));

free(counter);
```

---

## Memory Model

All Hemlock atomic operations use **sequential consistency** (`memory_order_seq_cst`), which provides the strongest memory ordering guarantees:

1. **Atomicity**: Each operation is indivisible
2. **Total ordering**: All threads see the same order of operations
3. **No reordering**: Operations are not reordered by the compiler or CPU

This makes reasoning about concurrent code simpler, at the cost of some potential performance compared to weaker memory orderings.

---

## Atomic Load and Store

### atomic_load_i32 / atomic_load_i64

Atomically read a value from memory.

**Signature:**
```hemlock
atomic_load_i32(ptr: ptr): i32
atomic_load_i64(ptr: ptr): i64
```

**Parameters:**
- `ptr` - Pointer to the memory location (must be properly aligned)

**Returns:** The value at the memory location

**Example:**
```hemlock
let p = alloc(4);
ptr_write_i32(p, 42);

let value = atomic_load_i32(p);
print(value);  // 42

free(p);
```

---

### atomic_store_i32 / atomic_store_i64

Atomically write a value to memory.

**Signature:**
```hemlock
atomic_store_i32(ptr: ptr, value: i32): null
atomic_store_i64(ptr: ptr, value: i64): null
```

**Parameters:**
- `ptr` - Pointer to the memory location
- `value` - Value to store

**Returns:** `null`

**Example:**
```hemlock
let p = alloc(8);

atomic_store_i64(p, 5000000000);
print(atomic_load_i64(p));  // 5000000000

free(p);
```

---

## Fetch-and-Modify Operations

These operations atomically modify a value and return the **old** (previous) value.

### atomic_add_i32 / atomic_add_i64

Atomically add to a value.

**Signature:**
```hemlock
atomic_add_i32(ptr: ptr, value: i32): i32
atomic_add_i64(ptr: ptr, value: i64): i64
```

**Returns:** The **old** value (before addition)

**Example:**
```hemlock
let p = alloc(4);
ptr_write_i32(p, 100);

let old = atomic_add_i32(p, 10);
print(old);                    // 100 (old value)
print(atomic_load_i32(p));     // 110 (new value)

free(p);
```

---

### atomic_sub_i32 / atomic_sub_i64

Atomically subtract from a value.

**Signature:**
```hemlock
atomic_sub_i32(ptr: ptr, value: i32): i32
atomic_sub_i64(ptr: ptr, value: i64): i64
```

**Returns:** The **old** value (before subtraction)

**Example:**
```hemlock
let p = alloc(4);
ptr_write_i32(p, 100);

let old = atomic_sub_i32(p, 25);
print(old);                    // 100 (old value)
print(atomic_load_i32(p));     // 75 (new value)

free(p);
```

---

### atomic_and_i32 / atomic_and_i64

Atomically perform bitwise AND.

**Signature:**
```hemlock
atomic_and_i32(ptr: ptr, value: i32): i32
atomic_and_i64(ptr: ptr, value: i64): i64
```

**Returns:** The **old** value (before AND)

**Example:**
```hemlock
let p = alloc(4);
ptr_write_i32(p, 0xFF);  // 255 in binary: 11111111

let old = atomic_and_i32(p, 0x0F);  // AND with 00001111
print(old);                    // 255 (old value)
print(atomic_load_i32(p));     // 15 (0xFF & 0x0F = 0x0F)

free(p);
```

---

### atomic_or_i32 / atomic_or_i64

Atomically perform bitwise OR.

**Signature:**
```hemlock
atomic_or_i32(ptr: ptr, value: i32): i32
atomic_or_i64(ptr: ptr, value: i64): i64
```

**Returns:** The **old** value (before OR)

**Example:**
```hemlock
let p = alloc(4);
ptr_write_i32(p, 0x0F);  // 15 in binary: 00001111

let old = atomic_or_i32(p, 0xF0);  // OR with 11110000
print(old);                    // 15 (old value)
print(atomic_load_i32(p));     // 255 (0x0F | 0xF0 = 0xFF)

free(p);
```

---

### atomic_xor_i32 / atomic_xor_i64

Atomically perform bitwise XOR.

**Signature:**
```hemlock
atomic_xor_i32(ptr: ptr, value: i32): i32
atomic_xor_i64(ptr: ptr, value: i64): i64
```

**Returns:** The **old** value (before XOR)

**Example:**
```hemlock
let p = alloc(4);
ptr_write_i32(p, 0xAA);  // 170 in binary: 10101010

let old = atomic_xor_i32(p, 0xFF);  // XOR with 11111111
print(old);                    // 170 (old value)
print(atomic_load_i32(p));     // 85 (0xAA ^ 0xFF = 0x55)

free(p);
```

---

## Compare-and-Swap (CAS)

The most powerful atomic operation. Atomically compares the current value with an expected value and, if they match, replaces it with a new value.

### atomic_cas_i32 / atomic_cas_i64

**Signature:**
```hemlock
atomic_cas_i32(ptr: ptr, expected: i32, desired: i32): bool
atomic_cas_i64(ptr: ptr, expected: i64, desired: i64): bool
```

**Parameters:**
- `ptr` - Pointer to the memory location
- `expected` - Value we expect to find
- `desired` - Value to store if expectation matches

**Returns:**
- `true` - Swap succeeded (value was `expected`, now is `desired`)
- `false` - Swap failed (value was not `expected`, unchanged)

**Example:**
```hemlock
let p = alloc(4);
ptr_write_i32(p, 100);

// CAS succeeds: value is 100, swap to 999
let success1 = atomic_cas_i32(p, 100, 999);
print(success1);               // true
print(atomic_load_i32(p));     // 999

// CAS fails: value is 999, not 100
let success2 = atomic_cas_i32(p, 100, 888);
print(success2);               // false
print(atomic_load_i32(p));     // 999 (unchanged)

free(p);
```

**Use cases:**
- Implementing locks and semaphores
- Lock-free data structures
- Optimistic concurrency control
- Atomic conditional updates

---

## Atomic Exchange

Atomically swap a value, returning the old value.

### atomic_exchange_i32 / atomic_exchange_i64

**Signature:**
```hemlock
atomic_exchange_i32(ptr: ptr, value: i32): i32
atomic_exchange_i64(ptr: ptr, value: i64): i64
```

**Parameters:**
- `ptr` - Pointer to the memory location
- `value` - New value to store

**Returns:** The **old** value (before exchange)

**Example:**
```hemlock
let p = alloc(4);
ptr_write_i32(p, 100);

let old = atomic_exchange_i32(p, 200);
print(old);                    // 100 (old value)
print(atomic_load_i32(p));     // 200 (new value)

free(p);
```

---

## Memory Fence

A full memory barrier that ensures all memory operations before the fence are visible to all threads before any operations after the fence.

### atomic_fence

**Signature:**
```hemlock
atomic_fence(): null
```

**Returns:** `null`

**Example:**
```hemlock
// Ensure all previous writes are visible
atomic_fence();
```

**Note:** In most cases, you don't need explicit fences because all atomic operations already use sequential consistency. Fences are useful when you need to synchronize non-atomic memory operations.

---

## Function Reference

### i32 Operations

| Function | Signature | Returns | Description |
|----------|-----------|---------|-------------|
| `atomic_load_i32` | `(ptr)` | `i32` | Load value atomically |
| `atomic_store_i32` | `(ptr, value)` | `null` | Store value atomically |
| `atomic_add_i32` | `(ptr, value)` | `i32` | Add and return old value |
| `atomic_sub_i32` | `(ptr, value)` | `i32` | Subtract and return old value |
| `atomic_and_i32` | `(ptr, value)` | `i32` | Bitwise AND and return old value |
| `atomic_or_i32` | `(ptr, value)` | `i32` | Bitwise OR and return old value |
| `atomic_xor_i32` | `(ptr, value)` | `i32` | Bitwise XOR and return old value |
| `atomic_cas_i32` | `(ptr, expected, desired)` | `bool` | Compare-and-swap |
| `atomic_exchange_i32` | `(ptr, value)` | `i32` | Exchange and return old value |

### i64 Operations

| Function | Signature | Returns | Description |
|----------|-----------|---------|-------------|
| `atomic_load_i64` | `(ptr)` | `i64` | Load value atomically |
| `atomic_store_i64` | `(ptr, value)` | `null` | Store value atomically |
| `atomic_add_i64` | `(ptr, value)` | `i64` | Add and return old value |
| `atomic_sub_i64` | `(ptr, value)` | `i64` | Subtract and return old value |
| `atomic_and_i64` | `(ptr, value)` | `i64` | Bitwise AND and return old value |
| `atomic_or_i64` | `(ptr, value)` | `i64` | Bitwise OR and return old value |
| `atomic_xor_i64` | `(ptr, value)` | `i64` | Bitwise XOR and return old value |
| `atomic_cas_i64` | `(ptr, expected, desired)` | `bool` | Compare-and-swap |
| `atomic_exchange_i64` | `(ptr, value)` | `i64` | Exchange and return old value |

### Memory Barrier

| Function | Signature | Returns | Description |
|----------|-----------|---------|-------------|
| `atomic_fence` | `()` | `null` | Full memory barrier |

---

## Common Patterns

### Pattern: Atomic Counter

```hemlock
// Thread-safe counter
let counter = alloc(4);
ptr_write_i32(counter, 0);

fn increment(): i32 {
    return atomic_add_i32(counter, 1);
}

fn decrement(): i32 {
    return atomic_sub_i32(counter, 1);
}

fn get_count(): i32 {
    return atomic_load_i32(counter);
}

// Usage
increment();  // Returns 0 (old value)
increment();  // Returns 1
increment();  // Returns 2
print(get_count());  // 3

free(counter);
```

### Pattern: Spinlock

```hemlock
// Simple spinlock implementation
let lock = alloc(4);
ptr_write_i32(lock, 0);  // 0 = unlocked, 1 = locked

fn acquire() {
    // Spin until we successfully set lock from 0 to 1
    while (!atomic_cas_i32(lock, 0, 1)) {
        // Busy wait
    }
}

fn release() {
    atomic_store_i32(lock, 0);
}

// Usage
acquire();
// ... critical section ...
release();

free(lock);
```

### Pattern: One-Time Initialization

```hemlock
let initialized = alloc(4);
ptr_write_i32(initialized, 0);  // 0 = not initialized, 1 = initialized

fn ensure_initialized() {
    // Try to be the one to initialize
    if (atomic_cas_i32(initialized, 0, 1)) {
        // We won the race, do initialization
        do_expensive_init();
    }
    // Otherwise, already initialized
}
```

### Pattern: Atomic Flag

```hemlock
let flag = alloc(4);
ptr_write_i32(flag, 0);

fn set_flag() {
    atomic_store_i32(flag, 1);
}

fn clear_flag() {
    atomic_store_i32(flag, 0);
}

fn test_and_set(): bool {
    // Returns true if flag was already set
    return atomic_exchange_i32(flag, 1) == 1;
}

fn check_flag(): bool {
    return atomic_load_i32(flag) == 1;
}
```

### Pattern: Bounded Counter

```hemlock
let counter = alloc(4);
ptr_write_i32(counter, 0);
let max_value = 100;

fn try_increment(): bool {
    while (true) {
        let current = atomic_load_i32(counter);
        if (current >= max_value) {
            return false;  // At maximum
        }
        if (atomic_cas_i32(counter, current, current + 1)) {
            return true;  // Successfully incremented
        }
        // CAS failed, another thread modified - retry
    }
}
```

---

## Best Practices

### 1. Use Proper Alignment

Pointers must be properly aligned for the data type:
- i32: 4-byte alignment
- i64: 8-byte alignment

Memory from `alloc()` is typically properly aligned.

### 2. Prefer Higher-Level Abstractions

When possible, use channels for inter-task communication. Atomics are lower-level and require careful reasoning.

```hemlock
// Prefer this:
let ch = channel(10);
spawn(fn() { ch.send(result); });
let value = ch.recv();

// Over manual atomic coordination when appropriate
```

### 3. Be Aware of ABA Problem

CAS can suffer from the ABA problem: a value changes from A to B and back to A. Your CAS succeeds, but the state may have changed in between.

### 4. Initialize Before Sharing

Always initialize atomic variables before spawning tasks that access them:

```hemlock
let counter = alloc(4);
ptr_write_i32(counter, 0);  // Initialize BEFORE spawning

let task = spawn(worker, counter);
```

### 5. Free After All Tasks Complete

Don't free atomic memory while tasks might still access it:

```hemlock
let counter = alloc(4);
ptr_write_i32(counter, 0);

let t1 = spawn(worker, counter);
let t2 = spawn(worker, counter);

join(t1);
join(t2);

// Now safe to free
free(counter);
```

---

## Limitations

### Current Limitations

1. **Only i32 and i64 supported** - No atomic operations for other types
2. **No pointer atomics** - Cannot atomically load/store pointers
3. **Sequential consistency only** - No weaker memory orderings available
4. **No atomic floating-point** - Use integer representation if needed

### Platform Notes

- Atomic operations use C11 `<stdatomic.h>` under the hood
- Available on all platforms that support POSIX threads
- Guaranteed to be lock-free on modern 64-bit systems

---

## See Also

- [Async/Concurrency](async-concurrency.md) - Task spawning and channels
- [Memory Management](../language-guide/memory.md) - Pointer and buffer allocation
- [Memory API](../reference/memory-api.md) - Allocation functions
