# Hemlock Debug Module

A standard library module providing debugging utilities and stack management for Hemlock programs.

## Overview

The debug module provides tools for inspecting task state and managing stack resources:

- **Task inspection** - Get debug information about spawned tasks
- **Stack management** - Configure and query thread stack size limits

These functions are intended for debugging, profiling, and advanced usage scenarios. They are not needed for typical Hemlock programs.

## Usage

```hemlock
import { task_debug_info, get_stack_limit, set_stack_limit } from "@stdlib/debug";

let limit = get_stack_limit();
print("Stack limit: " + limit + " bytes");
```

Or import all:

```hemlock
import * as debug from "@stdlib/debug";
let limit = debug.get_stack_limit();
```

---

## Task Inspection

### task_debug_info(task)
Returns debug information about a spawned task, including its current state and metadata.

**Parameters:**
- `task: task` - A task handle returned by `spawn()`

**Returns:** `object` - An object containing debug information about the task

**Use cases:**
- Inspecting task state during development
- Debugging concurrency issues
- Monitoring task lifecycle

```hemlock
import { task_debug_info } from "@stdlib/debug";

async fn slow_work(): i32 {
    // Simulate work
    let sum = 0;
    for (let i = 0; i < 1000; i++) {
        sum = sum + i;
    }
    return sum;
}

let t = spawn(slow_work);

// Inspect the task before joining
let info = task_debug_info(t);
print(info);

let result = join(t);
print("Result: " + result);

// Inspect after completion
let info_after = task_debug_info(t);
print(info_after);
```

---

## Stack Management

### set_stack_limit(bytes)
Sets the thread stack size limit for subsequently spawned tasks. This controls how much stack memory each new thread receives.

**Parameters:**
- `bytes: i32` - Stack size in bytes

**Returns:** `null`

**Use cases:**
- Increasing stack size for deeply recursive algorithms
- Reducing stack size to conserve memory when spawning many tasks
- Debugging stack overflow issues

```hemlock
import { set_stack_limit } from "@stdlib/debug";

// Increase stack size for tasks with deep recursion
set_stack_limit(4 * 1024 * 1024);  // 4 MB

// Now spawn tasks that need more stack space
let t = spawn(fn() {
    // Deep recursive work is safer with a larger stack
    fn fib(n: i32): i32 {
        if (n <= 1) { return n; }
        return fib(n - 1) + fib(n - 2);
    }
    return fib(30);
});

let result = join(t);
print(result);
```

**Note:** The new stack limit applies only to tasks spawned after the call. It does not affect the main thread or already-running tasks.

### get_stack_limit()
Returns the current thread stack size limit in bytes.

**Parameters:** None

**Returns:** `i32` - Current stack size limit in bytes

```hemlock
import { get_stack_limit, set_stack_limit } from "@stdlib/debug";

// Check the default stack limit
let default_limit = get_stack_limit();
print("Default stack limit: " + default_limit + " bytes");

// Change it
set_stack_limit(8 * 1024 * 1024);  // 8 MB

// Verify the change
let new_limit = get_stack_limit();
print("New stack limit: " + new_limit + " bytes");
```

---

## Examples

### Debugging Task State

```hemlock
import { task_debug_info } from "@stdlib/debug";

async fn worker(id: i32): string {
    let result = "Worker " + id + " done";
    return result;
}

// Spawn several tasks and inspect them
let tasks = [];
for (let i = 0; i < 3; i++) {
    tasks.push(spawn(worker, i));
}

// Inspect each task
for (let i = 0; i < tasks.length; i++) {
    let info = task_debug_info(tasks[i]);
    print("Task " + i + ": " + info);
}

// Join all tasks
for (t in tasks) {
    let result = join(t);
    print(result);
}
```

### Stack Size Tuning for Many Tasks

```hemlock
import { set_stack_limit, get_stack_limit } from "@stdlib/debug";

// When spawning many lightweight tasks, reduce stack size to save memory
let original = get_stack_limit();
set_stack_limit(256 * 1024);  // 256 KB per thread

let tasks = [];
for (let i = 0; i < 100; i++) {
    tasks.push(spawn(fn() {
        return i * i;
    }));
}

for (t in tasks) {
    join(t);
}

// Restore original stack size
set_stack_limit(original);
```

### Diagnosing Stack Overflows

```hemlock
import { set_stack_limit, get_stack_limit } from "@stdlib/debug";

// If a task crashes with a stack overflow, try increasing the limit
print("Current stack limit: " + get_stack_limit());

// Double the stack size
let current = get_stack_limit();
set_stack_limit(current * 2);
print("Increased stack limit to: " + get_stack_limit());

// Now retry the task that was overflowing
let t = spawn(fn() {
    // Deep recursion or large local allocations
    fn deep(n: i32): i32 {
        if (n <= 0) { return 0; }
        return 1 + deep(n - 1);
    }
    return deep(10000);
});

let result = join(t);
print("Recursion depth reached: " + result);
```

---

## API Reference

| Function | Parameters | Returns | Description |
|----------|-----------|---------|-------------|
| `task_debug_info` | `(task)` | `object` | Get debug info about a spawned task |
| `set_stack_limit` | `(bytes: i32)` | `null` | Set thread stack size for new tasks |
| `get_stack_limit` | `()` | `i32` | Get current thread stack size limit |

---

## See Also

- **Async module** (`@stdlib/async`) - ThreadPool and parallel_map
- **Async/concurrency** - `spawn()`, `join()`, `detach()`, `await` builtins
- **Logging module** (`@stdlib/logging`) - Structured logging for debugging

---

## License

Part of the Hemlock standard library.
