# Concurrency API Reference

Complete reference for Hemlock's async/concurrency system.

---

## Overview

Hemlock provides **structured concurrency** with true multi-threaded parallelism using POSIX threads (pthreads). Each spawned task runs on a separate OS thread, enabling actual parallel execution across multiple CPU cores.

**Key Features:**
- True multi-threaded parallelism (not green threads)
- Async function syntax
- Task spawning and joining
- Thread-safe channels
- Exception propagation

**Threading Model:**
- ✅ Real OS threads (POSIX pthreads)
- ✅ True parallelism (multiple CPU cores)
- ✅ Kernel-scheduled (pre-emptive multitasking)
- ✅ Thread-safe synchronization (mutexes, condition variables)

---

## Async Functions

### Async Function Declaration

Functions can be declared as `async` to indicate they're designed for concurrent execution.

**Syntax:**
```hemlock
async fn function_name(params): return_type {
    // function body
}
```

**Examples:**
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

async fn factorial(n: i32): i32 {
    if (n <= 1) { return 1; }
    return n * factorial(n - 1);
}

async fn process_data(data: string) {
    print("Processing:", data);
    return null;
}
```

**Behavior:**
- `async fn` declares an asynchronous function
- Can be called synchronously (runs in current thread)
- Can be spawned as concurrent task (runs on new thread)
- When spawned, runs on its own OS thread

**Note:** The `await` keyword works as an alias for `join()` — it awaits task completion and returns the result. If used on a non-task value, it returns the value as-is.

---

## Task Management

### spawn

Create and start a new concurrent task.

**Signature:**
```hemlock
spawn(async_fn: function, ...args): task
```

**Parameters:**
- `async_fn` - Async function to execute
- `...args` - Arguments to pass to function

**Returns:** Task handle

**Examples:**
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

// Spawn single task
let t = spawn(compute, 1000);
let result = join(t);
print(result);

// Spawn multiple tasks (run in parallel!)
let t1 = spawn(compute, 100);
let t2 = spawn(compute, 200);
let t3 = spawn(compute, 300);

// All three are running simultaneously
let r1 = join(t1);
let r2 = join(t2);
let r3 = join(t3);
```

**Behavior:**
- Creates new OS thread via `pthread_create()`
- Starts executing function immediately
- Returns task handle for later joining
- Tasks run in parallel on separate CPU cores

---

### spawn_with

Create and start a new concurrent task with per-thread configuration options.

**Signature:**
```hemlock
spawn_with(options: object, async_fn: function, ...args): task
```

**Parameters:**
- `options` - Configuration object with optional fields:
  - `stack_size` - Stack size in bytes for the new thread (default: 16 MB)
  - `name` - Thread name string (max 16 characters)
- `async_fn` - Async function to execute
- `...args` - Arguments to pass to function

**Returns:** Task handle

**Examples:**
```hemlock
async fn compute(n: i32): i32 {
    let sum = 0;
    for (let i = 0; i < n; i++) {
        sum = sum + i;
    }
    return sum;
}

// Custom stack size (8 MB)
let t1 = spawn_with({ stack_size: 8 * 1024 * 1024 }, compute, 1000);
let r1 = join(t1);

// Named thread
let t2 = spawn_with({ name: "worker-1" }, compute, 500);
let r2 = join(t2);

// Both options
let t3 = spawn_with({ stack_size: 16 * 1024 * 1024, name: "compute-3" }, compute, 100);
let r3 = join(t3);

// Empty options (uses defaults - same as spawn())
let t4 = spawn_with({}, compute, 50);
let r4 = join(t4);

// Multiple function arguments
async fn add(a, b) { return a + b; }
let t5 = spawn_with({ name: "adder" }, add, 100, 200);
print(join(t5));  // 300
```

**Behavior:**
- Creates new OS thread with the specified configuration
- `stack_size` controls the thread's stack allocation (useful for deeply recursive tasks)
- `name` sets the thread name (visible in debuggers/profilers; truncated to 16 characters)
- Passing `{}` for options is equivalent to calling `spawn()`
- Returns task handle for later joining, same as `spawn()`

**See Also:** `get_default_stack_size()` / `set_default_stack_size()` in `@stdlib/async` for changing the default stack size for all `spawn()` calls.

---

### join

Wait for task completion and retrieve result.

**Signature:**
```hemlock
join(task: task): any
```

**Parameters:**
- `task` - Task handle from `spawn()`

**Returns:** Task's return value

**Examples:**
```hemlock
async fn factorial(n: i32): i32 {
    if (n <= 1) { return 1; }
    return n * factorial(n - 1);
}

let t = spawn(factorial, 10);
let result = join(t);  // Blocks until task completes
print(result);         // 3628800
```

**Behavior:**
- Blocks current thread until task completes
- Returns task's return value
- Propagates exceptions thrown by task
- Cleans up task resources after returning

**Error Handling:**
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
    print("Caught:", e);  // "Caught: Task failed!"
}
```

---

### detach

Detach task (fire-and-forget execution).

**Signature:**
```hemlock
detach(task: task): null
```

**Parameters:**
- `task` - Task handle from `spawn()`

**Returns:** `null`

**Examples:**
```hemlock
async fn background_work() {
    print("Working in background...");
    return null;
}

let t = spawn(background_work);
detach(t);  // Task continues running independently

// Cannot join detached task
// join(t);  // ERROR
```

**Behavior:**
- Task continues running independently
- Cannot `join()` detached task
- Task and thread are automatically cleaned up when the task completes

**Use Cases:**
- Fire-and-forget background tasks
- Logging/monitoring tasks
- Tasks that don't need to return values

---

## Channels

Channels provide thread-safe communication between tasks.

### channel

Create a buffered channel.

**Signature:**
```hemlock
channel(capacity: i32): channel
```

**Parameters:**
- `capacity` - Buffer size (number of values)

**Returns:** Channel object

**Examples:**
```hemlock
let ch = channel(10);  // Buffered channel with capacity 10
let ch2 = channel(1);  // Minimal buffer (synchronous)
let ch3 = channel(100); // Large buffer
```

**Behavior:**
- Creates thread-safe channel
- Uses pthread mutexes for synchronization
- Capacity is fixed at creation time

---

### Channel Methods

#### send

Send value to channel (blocks if full).

**Signature:**
```hemlock
channel.send(value: any): null
```

**Parameters:**
- `value` - Value to send (any type)

**Returns:** `null`

**Examples:**
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
let t = spawn(producer, ch, 5);
```

**Behavior:**
- Sends value to channel
- Blocks if channel is full
- Thread-safe (uses mutex)
- Returns after value is sent

---

#### recv

Receive value from channel (blocks if empty).

**Signature:**
```hemlock
channel.recv(): any
```

**Returns:** Value from channel, or `null` if channel is closed and empty

**Examples:**
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
let t = spawn(consumer, ch, 5);
```

**Behavior:**
- Receives value from channel
- Blocks if channel is empty
- Returns `null` if channel is closed and empty
- Thread-safe (uses mutex)

---

#### close

Close channel (no more sends allowed).

**Signature:**
```hemlock
channel.close(): null
```

**Returns:** `null`

**Examples:**
```hemlock
async fn producer(ch) {
    ch.send(1);
    ch.send(2);
    ch.send(3);
    ch.close();  // Signal no more values
    return null;
}

async fn consumer(ch) {
    while (true) {
        let val = ch.recv();
        if (val == null) {
            break;  // Channel closed
        }
        print(val);
    }
    return null;
}
```

**Behavior:**
- Closes channel
- No more sends allowed
- `recv()` returns `null` when channel is empty
- Thread-safe

---

## Complete Concurrency Example

### Producer-Consumer Pattern

```hemlock
async fn producer(ch, count: i32) {
    let i = 0;
    while (i < count) {
        print("Producing:", i);
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
        print("Consuming:", val);
        sum = sum + val;
        i = i + 1;
    }
    return sum;
}

// Create channel
let ch = channel(10);

// Spawn producer and consumer
let p = spawn(producer, ch, 5);
let c = spawn(consumer, ch, 5);

// Wait for completion
join(p);
let total = join(c);
print("Total:", total);  // 0+10+20+30+40 = 100
```

---

## Parallel Computation

### Multiple Tasks Example

```hemlock
async fn factorial(n: i32): i32 {
    if (n <= 1) { return 1; }
    return n * factorial(n - 1);
}

// Spawn multiple tasks (run in parallel!)
let t1 = spawn(factorial, 5);   // Thread 1
let t2 = spawn(factorial, 6);   // Thread 2
let t3 = spawn(factorial, 7);   // Thread 3
let t4 = spawn(factorial, 8);   // Thread 4

// All four are computing simultaneously!

// Wait for results
let f5 = join(t1);  // 120
let f6 = join(t2);  // 720
let f7 = join(t3);  // 5040
let f8 = join(t4);  // 40320

print(f5, f6, f7, f8);
```

---

## Task Lifecycle

### State Transitions

1. **Created** - Task spawned but not yet running
2. **Running** - Task executing on OS thread
3. **Completed** - Task finished (result available)
4. **Joined** - Result retrieved, resources cleaned up
5. **Detached** - Task continues independently

### Lifecycle Example

```hemlock
async fn work(n: i32): i32 {
    return n * 2;
}

// 1. Create task
let t = spawn(work, 21);  // State: Running

// Task executes on separate thread...

// 2. Join task
let result = join(t);     // State: Completed → Joined
print(result);            // 42

// Task resources cleaned up after join
```

### Detached Lifecycle

```hemlock
async fn background() {
    print("Background task running");
    return null;
}

// 1. Create task
let t = spawn(background);  // State: Running

// 2. Detach task
detach(t);                  // State: Detached

// Task continues running independently
// Resources cleaned up by OS when done
```

---

## Error Handling

### Exception Propagation

Exceptions thrown in tasks are propagated when joined:

```hemlock
async fn risky_operation(should_fail: i32): i32 {
    if (should_fail == 1) {
        throw "Task failed!";
    }
    return 42;
}

// Task that succeeds
let t1 = spawn(risky_operation, 0);
let result1 = join(t1);  // 42

// Task that fails
let t2 = spawn(risky_operation, 1);
try {
    let result2 = join(t2);
} catch (e) {
    print("Caught:", e);  // "Caught: Task failed!"
}
```

### Handling Multiple Tasks

```hemlock
async fn work(id: i32, should_fail: i32): i32 {
    if (should_fail == 1) {
        throw "Task " + typeof(id) + " failed";
    }
    return id * 10;
}

let t1 = spawn(work, 1, 0);
let t2 = spawn(work, 2, 1);  // Will fail
let t3 = spawn(work, 3, 0);

// Join with error handling
try {
    let r1 = join(t1);  // OK
    print("Task 1:", r1);

    let r2 = join(t2);  // Throws
    print("Task 2:", r2);  // Never reached
} catch (e) {
    print("Error:", e);  // "Error: Task 2 failed"
}

// Can still join remaining task
let r3 = join(t3);
print("Task 3:", r3);
```

---

## Performance Characteristics

### True Parallelism

```hemlock
async fn cpu_intensive(n: i32): i32 {
    let sum = 0;
    let i = 0;
    while (i < n) {
        sum = sum + i;
        i = i + 1;
    }
    return sum;
}

// Sequential execution
let start = get_time();
let r1 = cpu_intensive(10000000);
let r2 = cpu_intensive(10000000);
let sequential_time = get_time() - start;

// Parallel execution
let start2 = get_time();
let t1 = spawn(cpu_intensive, 10000000);
let t2 = spawn(cpu_intensive, 10000000);
join(t1);
join(t2);
let parallel_time = get_time() - start2;

// parallel_time should be ~50% of sequential_time on multi-core systems
```

**Proven Characteristics:**
- N tasks can utilize N CPU cores simultaneously
- Stress tests show 8-9x CPU time vs wall time (proof of parallelism)
- Thread overhead: ~8KB stack + pthread overhead per task
- Blocking operations in one task don't block others

---

## Implementation Details

### Threading Model

- **1:1 threading** - Each task = 1 OS thread (`pthread`)
- **Kernel-scheduled** - OS kernel distributes threads across cores
- **Pre-emptive multitasking** - OS can interrupt and switch threads
- **No GIL** - No Global Interpreter Lock (unlike Python)

### Synchronization

- **Mutexes** - Channels use `pthread_mutex_t`
- **Condition variables** - Blocking send/recv use `pthread_cond_t`
- **Lock-free operations** - Task state transitions are atomic

### Memory & Cleanup

- **Joined tasks** - Automatically cleaned up after `join()`
- **Detached tasks** - Automatically cleaned up when the task completes
- **Channels** - Reference-counted, freed when no longer used

---

## Limitations

- No work-stealing scheduler (1 thread per task)
- No async I/O integration (file/network operations block)
- Channel capacity fixed at creation time

---

## Complete API Summary

### Functions

| Function  | Signature                                    | Returns   | Description                    |
|-----------|----------------------------------------------|-----------|--------------------------------|
| `spawn`   | `(async_fn: function, ...args)`              | `task`    | Create and start concurrent task|
| `spawn_with` | `(options: object, async_fn: function, ...args)` | `task` | Spawn with per-thread config (stack_size, name) |
| `join`    | `(task: task)`                               | `any`     | Wait for task, get result      |
| `detach`  | `(task: task)`                               | `null`    | Detach task (fire-and-forget)  |
| `channel` | `(capacity: i32)`                            | `channel` | Create thread-safe channel     |

### Channel Methods

| Method  | Signature       | Returns | Description                      |
|---------|-----------------|---------|----------------------------------|
| `send`  | `(value: any)`  | `null`  | Send value (blocks if full)      |
| `recv`  | `()`            | `any`   | Receive value (blocks if empty)  |
| `close` | `()`            | `null`  | Close channel                    |

### Types

| Type      | Description                          |
|-----------|--------------------------------------|
| `task`    | Handle for concurrent task           |
| `channel` | Thread-safe communication channel    |

---

## Best Practices

### Do's

✅ Use channels for communication between tasks
✅ Handle exceptions from joined tasks
✅ Close channels when done sending
✅ Use `join()` to get results and clean up
✅ Spawn async functions only

### Don'ts

❌ Don't share mutable state without synchronization
❌ Don't join the same task twice
❌ Don't send on closed channels
❌ Don't spawn non-async functions
❌ Don't forget to join tasks (unless detached)

---

## See Also

- [Built-in Functions](builtins.md) - `spawn()`, `join()`, `detach()`, `channel()`
- [Type System](type-system.md) - Task and channel types
