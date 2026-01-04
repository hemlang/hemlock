# Async/Concurrency in Hemlock

Hemlock provides **structured concurrency** with async/await syntax, task spawning, and channels for communication. The implementation uses POSIX threads (pthreads) for **TRUE multi-threaded parallelism**.

## Table of Contents

- [Overview](#overview)
- [Threading Model](#threading-model)
- [Async Functions](#async-functions)
- [Task Spawning](#task-spawning)
- [Channels](#channels)
- [Exception Propagation](#exception-propagation)
- [Implementation Details](#implementation-details)
- [Best Practices](#best-practices)
- [Performance Characteristics](#performance-characteristics)
- [Current Limitations](#current-limitations)

## Overview

**What this means:**
- ✅ **Real OS threads** - Each spawned task runs on a separate pthread (POSIX thread)
- ✅ **True parallelism** - Tasks execute simultaneously on multiple CPU cores
- ✅ **Kernel-scheduled** - The OS scheduler distributes tasks across available cores
- ✅ **Thread-safe channels** - Uses pthread mutexes and condition variables for synchronization

**What this is NOT:**
- ❌ **NOT green threads** - Not user-space cooperative multitasking
- ❌ **NOT async/await coroutines** - Not single-threaded event loop like JavaScript/Python asyncio
- ❌ **NOT emulated concurrency** - Not simulated parallelism

This is the **same threading model as C, C++, and Rust** when using OS threads. You get actual parallel execution across multiple cores.

## Threading Model

### 1:1 Threading

Hemlock uses a **1:1 threading model**, where:
- Each spawned task creates a dedicated OS thread via `pthread_create()`
- The OS kernel schedules threads across available CPU cores
- Pre-emptive multitasking - the OS can interrupt and switch between threads
- **No GIL** - Unlike Python, there's no Global Interpreter Lock limiting parallelism

### Synchronization Mechanisms

- **Mutexes** - Channels use `pthread_mutex_t` for thread-safe access
- **Condition variables** - Blocking send/recv use `pthread_cond_t` for efficient waiting
- **Lock-free operations** - Task state transitions are atomic

## Async Functions

Functions can be declared as `async` to indicate they're designed for concurrent execution:

```hemlock
async fn compute(n: i32): i32 {
    let sum = 0;
    let i = 0;
    while (i < n) {
        sum = sum + i;
        i = i + 1;
    }
    return sum;
}
```

### Key Points

- `async fn` declares an asynchronous function
- Async functions can be spawned as concurrent tasks using `spawn()`
- Async functions can also be called directly (runs synchronously in current thread)
- When spawned, each task runs on its **own OS thread** (not a coroutine!)
- `await` keyword is reserved for future use

### Example: Direct Call vs Spawn

```hemlock
async fn factorial(n: i32): i32 {
    if (n <= 1) { return 1; }
    return n * factorial(n - 1);
}

// Direct call - runs synchronously
let result1 = factorial(5);  // 120

// Spawned task - runs on separate thread
let task = spawn(factorial, 5);
let result2 = join(task);  // 120
```

## Task Spawning

Use `spawn()` to run async functions **in parallel on separate OS threads**:

```hemlock
async fn factorial(n: i32): i32 {
    if (n <= 1) { return 1; }
    return n * factorial(n - 1);
}

// Spawn multiple tasks - these run in PARALLEL on different CPU cores!
let t1 = spawn(factorial, 5);  // Thread 1
let t2 = spawn(factorial, 6);  // Thread 2
let t3 = spawn(factorial, 7);  // Thread 3

// All three are computing simultaneously right now!

// Wait for results
let f5 = join(t1);  // 120
let f6 = join(t2);  // 720
let f7 = join(t3);  // 5040
```

### Built-in Functions

#### spawn(async_fn, arg1, arg2, ...)

Creates a new task on a new pthread, returns task handle.

**Parameters:**
- `async_fn` - The async function to execute
- `arg1, arg2, ...` - Arguments to pass to the function

**Returns:** Task handle (opaque value used with `join()` or `detach()`)

**Example:**
```hemlock
async fn process(data: string, count: i32): i32 {
    // ... processing logic
    return count * 2;
}

let task = spawn(process, "test", 42);
```

#### join(task)

Wait for task completion (blocks until thread finishes), returns result.

**Parameters:**
- `task` - Task handle returned from `spawn()`

**Returns:** The value returned by the async function

**Example:**
```hemlock
let task = spawn(compute, 1000);
let result = join(task);  // Blocks until compute() finishes
print(result);
```

**Important:** Each task can only be joined once. Subsequent joins will error.

#### detach(task)

Fire-and-forget execution (thread runs independently, no join allowed).

**Parameters:**
- `task` - Task handle returned from `spawn()`

**Returns:** `null`

**Example:**
```hemlock
async fn background_work() {
    // Long-running background task
    // ...
}

let task = spawn(background_work);
detach(task);  // Task runs independently, cannot join
```

**Important:** Detached tasks cannot be joined. Both the pthread and Task struct are automatically cleaned up when the task completes.

## Channels

Channels provide thread-safe communication between tasks using a bounded buffer with blocking semantics.

### Creating Channels

```hemlock
let ch = channel(10);  // Create channel with buffer size of 10
```

**Parameters:**
- `capacity` (i32) - Maximum number of values the channel can hold

**Returns:** Channel object

### Channel Methods

#### send(value)

Send value to channel (blocks if full).

```hemlock
async fn producer(ch, count: i32) {
    let i = 0;
    while (i < count) {
        ch.send(i * 10);
        i = i + 1;
    }
    ch.close();
    return null;
}

let ch = channel(10);
let task = spawn(producer, ch, 5);
```

**Behavior:**
- If channel has space, value is added immediately
- If channel is full, sender blocks until space becomes available
- If channel is closed, throws exception

#### recv()

Receive value from channel (blocks if empty).

```hemlock
async fn consumer(ch, count: i32): i32 {
    let sum = 0;
    let i = 0;
    while (i < count) {
        let val = ch.recv();
        sum = sum + val;
        i = i + 1;
    }
    return sum;
}

let ch = channel(10);
let task = spawn(consumer, ch, 5);
```

**Behavior:**
- If channel has values, returns next value immediately
- If channel is empty, receiver blocks until value available
- If channel is closed and empty, returns `null`

#### close()

Close channel (recv on closed channel returns null).

```hemlock
ch.close();
```

**Behavior:**
- Prevents further `send()` operations (will throw exception)
- Allows pending `recv()` operations to complete
- Once empty, `recv()` returns `null`

### Multiplexing with select()

The `select()` function allows waiting on multiple channels simultaneously, returning when any channel has data available.

**Signature:**
```hemlock
select(channels: array, timeout_ms?: i32): object | null
```

**Parameters:**
- `channels` - Array of channel values
- `timeout_ms` (optional) - Timeout in milliseconds (-1 or omit for infinite wait)

**Returns:**
- `{ channel, value }` - Object with the channel that had data and the received value
- `null` - On timeout (if timeout was specified)

**Example:**
```hemlock
let ch1 = channel(1);
let ch2 = channel(1);

// Producer tasks
spawn(fn() {
    sleep(100);
    ch1.send("from channel 1");
});

spawn(fn() {
    sleep(50);
    ch2.send("from channel 2");
});

// Wait for first result (ch2 should be faster)
let result = select([ch1, ch2]);
print(result.value);  // "from channel 2"

// Wait for second result
let result2 = select([ch1, ch2]);
print(result2.value);  // "from channel 1"
```

**With timeout:**
```hemlock
let ch = channel(1);

// No sender, will timeout
let result = select([ch], 100);  // 100ms timeout
if (result == null) {
    print("Timed out!");
}
```

**Use cases:**
- Waiting for the fastest of multiple data sources
- Implementing timeouts on channel operations
- Event loop patterns with multiple event sources
- Fan-in: merging multiple channels into one

**Fan-in pattern:**
```hemlock
fn fan_in(channels: array, output: channel) {
    while (true) {
        let result = select(channels);
        if (result == null) {
            break;  // All channels closed
        }
        output.send(result.value);
    }
    output.close();
}
```

### Complete Producer-Consumer Example

```hemlock
async fn producer(ch, count: i32) {
    let i = 0;
    while (i < count) {
        ch.send(i * 10);
        i = i + 1;
    }
    ch.close();
    return null;
}

async fn consumer(ch, count: i32): i32 {
    let sum = 0;
    let i = 0;
    while (i < count) {
        let val = ch.recv();
        sum = sum + val;
        i = i + 1;
    }
    return sum;
}

// Create channel with buffer size
let ch = channel(10);

// Spawn producer and consumer
let p = spawn(producer, ch, 5);
let c = spawn(consumer, ch, 5);

// Wait for completion
join(p);
let total = join(c);  // 100 (0+10+20+30+40)
print(total);
```

### Multi-Producer, Multi-Consumer

Channels can be safely shared between multiple producers and consumers:

```hemlock
async fn producer(id: i32, ch, count: i32) {
    let i = 0;
    while (i < count) {
        ch.send(id * 100 + i);
        i = i + 1;
    }
}

async fn consumer(id: i32, ch, count: i32): i32 {
    let sum = 0;
    let i = 0;
    while (i < count) {
        let val = ch.recv();
        sum = sum + val;
        i = i + 1;
    }
    return sum;
}

let ch = channel(20);

// Multiple producers
let p1 = spawn(producer, 1, ch, 5);
let p2 = spawn(producer, 2, ch, 5);

// Multiple consumers
let c1 = spawn(consumer, 1, ch, 5);
let c2 = spawn(consumer, 2, ch, 5);

// Wait for all
join(p1);
join(p2);
let sum1 = join(c1);
let sum2 = join(c2);
print(sum1 + sum2);
```

## Exception Propagation

Exceptions thrown in spawned tasks are propagated when joined:

```hemlock
async fn risky_operation(should_fail: i32): i32 {
    if (should_fail == 1) {
        throw "Task failed!";
    }
    return 42;
}

let t = spawn(risky_operation, 1);
try {
    let result = join(t);
} catch (e) {
    print("Caught: " + e);  // "Caught: Task failed!"
}
```

### Exception Handling Patterns

**Pattern 1: Handle in task**
```hemlock
async fn safe_task() {
    try {
        // risky operation
    } catch (e) {
        print("Error in task: " + e);
        return null;
    }
}

let task = spawn(safe_task);
join(task);  // No exception propagated
```

**Pattern 2: Propagate to caller**
```hemlock
async fn task_that_throws() {
    throw "error";
}

let task = spawn(task_that_throws);
try {
    join(task);
} catch (e) {
    print("Caught from task: " + e);
}
```

**Pattern 3: Detached tasks with exceptions**
```hemlock
async fn detached_task() {
    try {
        // work
    } catch (e) {
        // Must handle internally - cannot propagate
        print("Error: " + e);
    }
}

let task = spawn(detached_task);
detach(task);  // Cannot catch exceptions from detached tasks
```

## Implementation Details

### Threading Architecture

- **1:1 threading** - Each spawned task creates a dedicated OS thread via `pthread_create()`
- **Kernel-scheduled** - The OS kernel schedules threads across available CPU cores
- **Pre-emptive multitasking** - The OS can interrupt and switch between threads
- **No GIL** - Unlike Python, there's no Global Interpreter Lock limiting parallelism

### Channel Implementation

Channels use a circular buffer with pthread synchronization:

```
Channel Structure:
- buffer[] - Fixed-size array of Values
- capacity - Maximum number of elements
- size - Current number of elements
- head - Read position
- tail - Write position
- mutex - pthread_mutex_t for thread-safe access
- not_empty - pthread_cond_t for blocking recv
- not_full - pthread_cond_t for blocking send
- closed - Boolean flag
- refcount - Reference count for cleanup
```

**Blocking behavior:**
- `send()` on full channel: waits on `not_full` condition variable
- `recv()` on empty channel: waits on `not_empty` condition variable
- Both are signaled when appropriate by the opposite operation

### Memory & Cleanup

- **Joined tasks:** Automatically cleaned up after `join()` returns
- **Detached tasks:** Automatically cleaned up when the task completes
- **Channels:** Reference-counted and freed when no longer used

## Best Practices

### 1. Always Close Channels

```hemlock
async fn producer(ch) {
    // ... send values
    ch.close();  // Important: signal no more values
}
```

### 2. Use Structured Concurrency

Spawn tasks and join them in the same scope:

```hemlock
fn process_data(data) {
    // Spawn tasks
    let t1 = spawn(worker, data);
    let t2 = spawn(worker, data);

    // Always join before returning
    let r1 = join(t1);
    let r2 = join(t2);

    return r1 + r2;
}
```

### 3. Handle Exceptions Appropriately

```hemlock
async fn task() {
    try {
        // risky operation
    } catch (e) {
        // Log error
        throw e;  // Re-throw if caller should know
    }
}
```

### 4. Use Appropriate Channel Capacity

- **Small capacity (1-10):** For coordination/signaling
- **Medium capacity (10-100):** For general producer-consumer
- **Large capacity (100+):** For high-throughput scenarios

```hemlock
let signal_ch = channel(1);      // Coordination
let work_ch = channel(50);       // Work queue
let buffer_ch = channel(1000);   // High throughput
```

### 5. Detach Only When Necessary

Prefer `join()` over `detach()` for better resource management:

```hemlock
// Good: Join and get result
let task = spawn(work);
let result = join(task);

// Use detach only for true fire-and-forget
let bg_task = spawn(background_logging);
detach(bg_task);  // Will run independently
```

## Performance Characteristics

### True Parallelism

- **N spawned tasks can utilize N CPU cores simultaneously**
- Proven speedup - stress tests show 8-9x CPU time vs wall time (multiple cores working)
- Linear scaling with number of cores (up to thread count)

### Thread Overhead

- Each task has ~8KB stack + pthread overhead
- Thread creation cost: ~10-20μs
- Context switch cost: ~1-5μs

### When to Use Async

**Good use cases:**
- CPU-intensive computations that can be parallelized
- I/O-bound operations (though I/O is still blocking)
- Concurrent processing of independent data
- Pipeline architectures with channels

**Not ideal for:**
- Very short tasks (thread overhead dominates)
- Tasks with heavy synchronization (contention overhead)
- Single-core systems (no parallelism benefit)

### Blocking I/O Safe

Blocking operations in one task don't block others:

```hemlock
async fn reader(filename: string) {
    let f = open(filename, "r");  // Blocks this thread only
    let content = f.read();       // Blocks this thread only
    f.close();
    return content;
}

// Both read concurrently (on different threads)
let t1 = spawn(reader, "file1.txt");
let t2 = spawn(reader, "file2.txt");

let c1 = join(t1);
let c2 = join(t2);
```

## Thread Safety Model

Hemlock uses a **message-passing** concurrency model where tasks communicate via channels rather than shared mutable state.

### Argument Isolation

When you spawn a task, **arguments are deep-copied** to prevent data races:

```hemlock
async fn modify_array(arr: array): array {
    arr.push(999);    // Modifies the COPY, not original
    arr[0] = -1;
    return arr;
}

let original = [1, 2, 3];
let task = spawn(modify_array, original);
let modified = join(task);

print(original.length);  // 3 - unchanged!
print(modified.length);  // 4 - has new element
```

**What gets deep-copied:**
- Arrays (and all elements recursively)
- Objects (and all fields recursively)
- Strings
- Buffers

**What gets shared (reference retained):**
- Channels (the communication mechanism - intentionally shared)
- Task handles (for coordination)
- Functions (code is immutable)
- File handles (OS manages concurrent access)
- Socket handles (OS manages concurrent access)

**What cannot be passed:**
- Raw pointers (`ptr`) - use `buffer` instead

### Why Message-Passing?

This follows Hemlock's "explicit over implicit" philosophy:

```hemlock
// BAD: Shared mutable state (would cause data races)
let counter = { value: 0 };
let t1 = spawn(fn() { counter.value = counter.value + 1; });  // Race!
let t2 = spawn(fn() { counter.value = counter.value + 1; });  // Race!

// GOOD: Message-passing via channels
async fn increment(ch) {
    let val = ch.recv();
    ch.send(val + 1);
}

let ch = channel(1);
ch.send(0);
let t1 = spawn(increment, ch);
join(t1);
let result = ch.recv();  // 1 - no race condition
```

### Reference Counting Thread Safety

All reference counting operations use **atomic operations** to prevent use-after-free bugs:
- `string_retain/release` - atomic
- `array_retain/release` - atomic
- `object_retain/release` - atomic
- `buffer_retain/release` - atomic
- `function_retain/release` - atomic
- `channel_retain/release` - atomic
- `task_retain/release` - atomic

This ensures safe memory management even when values are shared across threads.

### Closure Environment Access

Tasks have access to the closure environment for:
- Built-in functions (`print`, `len`, etc.)
- Global function definitions
- Constants and variables

The closure environment is protected by a per-environment mutex, making
concurrent reads and writes thread-safe:

```hemlock
let x = 10;

async fn read_closure(): i32 {
    return x;  // OK: reading closure variable (thread-safe)
}

async fn modify_closure() {
    x = 20;  // OK: writing closure variable (synchronized with mutex)
}
```

**Note:** While concurrent access is synchronized, modifying shared state from
multiple tasks can still lead to logical race conditions (non-deterministic
ordering). For predictable behavior, use channels for task communication or
return values from tasks.

If you need to return data from a task, use the return value or channels.

## Current Limitations

### 1. No Work-Stealing Scheduler

Uses 1 thread per task, which can be inefficient for many short tasks.

**Current:** 1000 tasks = 1000 threads (heavy overhead)

**Planned:** Thread pool with work stealing for better efficiency

### 3. No Async I/O Integration

File/network operations still block the thread:

```hemlock
async fn read_file(path: string) {
    let f = open(path, "r");
    let content = f.read();  // Blocks the thread
    f.close();
    return content;
}
```

**Workaround:** Use multiple threads for concurrent I/O operations

### 4. Fixed Channel Capacity

Channel capacity is set at creation and cannot be resized:

```hemlock
let ch = channel(10);
// Cannot dynamically resize to 20
```

### 5. Channel Size is Fixed

Channel buffer size cannot be changed after creation.

## Common Patterns

### Parallel Map

```hemlock
async fn map_worker(ch_in, ch_out, fn_transform) {
    while (true) {
        let val = ch_in.recv();
        if (val == null) { break; }

        let result = fn_transform(val);
        ch_out.send(result);
    }
    ch_out.close();
}

fn parallel_map(data, fn_transform, workers: i32) {
    let ch_in = channel(100);
    let ch_out = channel(100);

    // Spawn workers
    let tasks = [];
    let i = 0;
    while (i < workers) {
        tasks.push(spawn(map_worker, ch_in, ch_out, fn_transform));
        i = i + 1;
    }

    // Send data
    let i = 0;
    while (i < data.length) {
        ch_in.send(data[i]);
        i = i + 1;
    }
    ch_in.close();

    // Collect results
    let results = [];
    let i = 0;
    while (i < data.length) {
        results.push(ch_out.recv());
        i = i + 1;
    }

    // Wait for workers
    let i = 0;
    while (i < tasks.length) {
        join(tasks[i]);
        i = i + 1;
    }

    return results;
}
```

### Pipeline Architecture

```hemlock
async fn stage1(input_ch, output_ch) {
    while (true) {
        let val = input_ch.recv();
        if (val == null) { break; }
        output_ch.send(val * 2);
    }
    output_ch.close();
}

async fn stage2(input_ch, output_ch) {
    while (true) {
        let val = input_ch.recv();
        if (val == null) { break; }
        output_ch.send(val + 10);
    }
    output_ch.close();
}

// Create pipeline
let ch1 = channel(10);
let ch2 = channel(10);
let ch3 = channel(10);

let s1 = spawn(stage1, ch1, ch2);
let s2 = spawn(stage2, ch2, ch3);

// Feed input
ch1.send(1);
ch1.send(2);
ch1.send(3);
ch1.close();

// Collect output
print(ch3.recv());  // 12 (1 * 2 + 10)
print(ch3.recv());  // 14 (2 * 2 + 10)
print(ch3.recv());  // 16 (3 * 2 + 10)

join(s1);
join(s2);
```

### Fan-Out, Fan-In

```hemlock
async fn worker(id: i32, input_ch, output_ch) {
    while (true) {
        let val = input_ch.recv();
        if (val == null) { break; }

        // Process value
        let result = val * id;
        output_ch.send(result);
    }
}

let input = channel(10);
let output = channel(10);

// Fan-out: Multiple workers
let workers = 4;
let tasks = [];
let i = 0;
while (i < workers) {
    tasks.push(spawn(worker, i, input, output));
    i = i + 1;
}

// Send work
let i = 0;
while (i < 10) {
    input.send(i);
    i = i + 1;
}
input.close();

// Fan-in: Collect all results
let results = [];
let i = 0;
while (i < 10) {
    results.push(output.recv());
    i = i + 1;
}

// Wait for all workers
let i = 0;
while (i < tasks.length) {
    join(tasks[i]);
    i = i + 1;
}
```

## Summary

Hemlock's async/concurrency model provides:

- ✅ True multi-threaded parallelism using OS threads
- ✅ Simple, structured concurrency primitives
- ✅ Thread-safe channels for communication
- ✅ Exception propagation across tasks
- ✅ Proven performance on multi-core systems
- ✅ **Argument isolation** - deep copy prevents data races
- ✅ **Atomic reference counting** - safe memory management across threads

This makes Hemlock suitable for:
- Parallel computations
- Concurrent I/O operations
- Pipeline architectures
- Producer-consumer patterns

While avoiding the complexity of:
- Manual thread management
- Low-level synchronization primitives
- Deadlock-prone lock-based designs
- Shared mutable state bugs
