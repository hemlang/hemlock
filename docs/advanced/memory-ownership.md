# Memory Ownership in Hemlock

> "We give you the tools to be safe, but we don't force you to use them."

This document describes the memory ownership semantics in Hemlock, covering both programmer-managed memory and runtime-managed values.

## Table of Contents

1. [The Contract](#the-contract)
2. [Programmer-Managed Memory](#programmer-managed-memory)
3. [Runtime-Managed Values](#runtime-managed-values)
4. [Ownership Transfer Points](#ownership-transfer-points)
5. [Async and Concurrency](#async-and-concurrency)
6. [FFI Memory Rules](#ffi-memory-rules)
7. [Exception Safety](#exception-safety)
8. [Best Practices](#best-practices)

---

## The Contract

Hemlock has a clear division of memory management responsibility:

| Memory Type | Managed By | Cleanup Method |
|-------------|------------|----------------|
| Raw pointers (`ptr`) | **Programmer** | `free(ptr)` |
| Buffers (`buffer`) | **Programmer** | `free(buf)` |
| Strings, Arrays, Objects | **Runtime** | Automatic (reference counting) |
| Functions, Closures | **Runtime** | Automatic (reference counting) |
| Tasks, Channels | **Runtime** | Automatic (reference counting) |

**The core principle:** If you allocate it explicitly, you free it explicitly. Everything else is handled automatically.

---

## Programmer-Managed Memory

### Raw Pointers

```hemlock
let p = alloc(64);       // Allocate 64 bytes
memset(p, 0, 64);        // Initialize
// ... use the memory ...
free(p);                 // Your responsibility!
```

**Rules:**
- `alloc()` returns memory you own
- You must call `free()` when done
- Double-free will crash (by design)
- Use-after-free is undefined behavior
- Pointer arithmetic is allowed but unchecked

### Typed Allocation

```hemlock
let arr = talloc("i32", 100);  // Allocate 100 i32s (400 bytes)
ptr_write_i32(arr, 0, 42);     // Write to index 0
let val = ptr_read_i32(arr, 0); // Read from index 0
free(arr);                      // Still your responsibility
```

### Buffers (Safe Alternative)

```hemlock
let buf = buffer(64);    // Bounds-checked buffer
buf[0] = 42;             // Safe indexing
// buf[100] = 1;         // Runtime error: out of bounds
free(buf);               // Still needs explicit free
```

**Key difference:** Buffers provide bounds checking for indexing, direct `ptr_read_*`/`ptr_write_*` access, and `memset()`/`memcpy()` buffer operands; raw pointers do not.

---

## Runtime-Managed Values

### Reference Counting

Heap-allocated values use atomic reference counting:

```hemlock
let s1 = "hello";        // String allocated, refcount = 1
let s2 = s1;             // s2 shares s1, refcount = 2
// When both go out of scope, refcount → 0, memory freed
```

**Reference-counted types:**
- `string` - UTF-8 text
- `array` - Dynamic arrays
- `object` - Key-value objects
- `function` - Closures
- `task` - Async task handles
- `channel` - Communication channels

### Cycle Detection

The runtime handles cycles in object graphs:

```hemlock
let a = { ref: null };
let b = { ref: a };
a.ref = b;               // Cycle: a → b → a
// Runtime uses visited sets to detect and break cycles during cleanup
```

---

## Ownership Transfer Points

### Variable Binding

```hemlock
let x = [1, 2, 3];       // Array created with refcount 1
                         // x owns the reference
```

### Function Returns

```hemlock
fn make_array() {
    return [1, 2, 3];    // Array ownership transfers to caller
}
let arr = make_array();  // arr now owns the reference
```

### Assignment

```hemlock
let a = "hello";
let b = a;               // Shared reference (refcount incremented)
b = "world";             // a still has "hello", b has "world"
```

### Channel Operations

```hemlock
let ch = channel(10);
ch.send(msg);            // Reference transferred into the channel
                         // (retained - NOT copied). For compound values
                         // the sender now aliases the receiver: stop
                         // touching msg's interior after sending it.

let msg = ch.recv();     // Receives ownership from channel
```

Primitives (integers, floats, bools, runes, null) are copied by value.
Strings, arrays, objects, and buffers cross by reference - the sanctioned
discipline is ownership transfer. See
[The Hemlock Memory Model](memory-model.md).

### Task Spawning

```hemlock
let data = { x: 1 };
let task = spawn(worker, data);  // data is DEEP COPIED for isolation
data.x = 2;                       // Safe - task has its own copy
let result = join(task);          // result ownership transfers to caller
```

---

## Async and Concurrency

### Thread Isolation

Spawned tasks receive **deep copies** of mutable arguments:

```hemlock
async fn worker(data) {
    data.x = 100;        // Modifies task's copy only
    return data;
}

let obj = { x: 1 };
let task = spawn(worker, obj);
obj.x = 2;               // Safe - doesn't affect task
let result = join(task);
print(obj.x);            // 2 (unchanged by task)
print(result.x);         // 100 (task's modified copy)
```

### Shared Coordination Objects

Some types are shared by reference (not copied):
- **Channels** - For inter-task communication
- **Tasks** - For coordination (join/detach)

```hemlock
let ch = channel(1);
spawn(producer, ch);     // Same channel, not a copy
spawn(consumer, ch);     // Both tasks share the channel
```

### Task Results

```hemlock
let task = spawn(compute);
let result = join(task);  // Caller owns the result
                          // Task's reference is released when task is freed
```

### Detached Tasks

```hemlock
detach(spawn(background_work));
// Task runs independently
// Result is automatically released when task completes
// No leak even though nobody calls join()
```

---

## FFI Memory Rules

### Passing to C Functions

```hemlock
extern fn strlen(s: string): i32;

let s = "hello";
let len = strlen(s);     // Hemlock retains ownership
                         // String is valid during call
                         // C function should NOT free it
```

### Receiving from C Functions

```hemlock
extern fn strdup(s: string): ptr;

let copy = strdup("hello");  // C allocated this memory
free(copy);                   // Your responsibility to free
```

### Struct Passing (Compiler Only)

```hemlock
// Define C struct layout
ffi_struct Point { x: f64, y: f64 }

extern fn make_point(x: f64, y: f64): Point;

let p = make_point(1.0, 2.0);  // Returned by value, copied
                                // No cleanup needed for stack structs
```

### Callback Memory

```hemlock
// When C calls back into Hemlock:
// - Arguments are owned by C (don't free)
// - Return value ownership transfers to C
```

---

## Exception Safety

### Guarantees

The runtime provides these guarantees:

1. **No leak on normal exit** - All runtime-managed values cleaned up
2. **No leak on exception** - Temporaries released during stack unwinding
3. **Defer runs on exception** - Cleanup code executes

### Expression Evaluation

```hemlock
// If this throws during array creation:
let arr = [f(), g(), h()];  // Partial array is released

// If this throws during function call:
foo(a(), b(), c());         // Previously evaluated args released
```

### Defer for Cleanup

```hemlock
fn process_file() {
    let f = open("data.txt", "r");
    defer f.close();         // Runs on return OR exception

    let data = f.read();
    if (data == "") {
        throw "Empty file";  // f.close() still runs!
    }
    return data;
}
```

---

## Best Practices

### 1. Prefer Runtime-Managed Types

```hemlock
// Prefer this:
let data = [1, 2, 3, 4, 5];

// Over this (unless you need low-level control):
let data = talloc("i32", 5);
// ... must remember to free ...
```

### 2. Use Defer for Manual Memory

```hemlock
fn process() {
    let buf = alloc(1024);
    defer free(buf);        // Guaranteed cleanup

    // ... use buf ...
    // No need to free at every return point
}
```

### 3. Avoid Raw Pointers in Async

```hemlock
// WRONG - pointer may be freed before task completes
let p = alloc(64);
spawn(worker, p);          // Task gets the pointer value
free(p);                   // Oops! Task still using it

// RIGHT - use channels or copy data
let ch = channel(1);
let data = buffer(64);
// ... fill data ...
ch.send(data);             // Deep copied
spawn(worker, ch);
free(data);                // Safe - task has its own copy
```

### 4. Close Channels When Done

```hemlock
let ch = channel(10);
// ... use channel ...
ch.close();                // Drains and releases buffered values
```

### 5. Join or Detach Tasks

```hemlock
let task = spawn(work);

// Option 1: Wait for result
let result = join(task);

// Option 2: Fire and forget
// detach(task);

// DON'T: Let task handle go out of scope without join or detach
// (It will be cleaned up, but result may leak)
```

---

## Debugging Memory Issues

### Enable ASAN

```bash
make asan
ASAN_OPTIONS=detect_leaks=1 ./hemlock script.hml
```

### Run Leak Regression Tests

```bash
make leak-regression       # Full suite
make leak-regression-quick # Skip comprehensive test
```

### Valgrind

```bash
make valgrind-check FILE=script.hml
```

---

## Summary

| Operation | Memory Behavior |
|-----------|-----------------|
| `alloc(n)` | Allocates, you free |
| `buffer(n)` | Allocates with bounds check, you free |
| `"string"` | Runtime manages |
| `[array]` | Runtime manages |
| `{object}` | Runtime manages |
| `spawn(fn)` | Deep copies args, runtime manages task |
| `join(task)` | Caller owns result |
| `detach(task)` | Runtime releases result when done |
| `ch.send(v)` | Copies value into channel |
| `ch.recv()` | Caller owns received value |
| `ch.close()` | Drains and releases buffered values |
