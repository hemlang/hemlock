# Hemlock Async Module

Utilities for efficient parallel task execution using thread pools.

## Overview

The async module provides:

- **ThreadPool** - Fixed-size thread pool for parallel task execution
- **parallel_map** - Convenience function for mapping over arrays in parallel

## Usage

```hemlock
import { ThreadPool, parallel_map, get_default_stack_size, set_default_stack_size } from "@stdlib/async";
```

---

## Thread Configuration

### get_default_stack_size

Get the current default stack size for spawned threads.

**Signature:**
```hemlock
get_default_stack_size(): i64
```

**Returns:** Default stack size in bytes (default: 16 MB / 16777216 bytes)

**Example:**
```hemlock
import { get_default_stack_size } from "@stdlib/async";

let size = get_default_stack_size();
print(size);  // 16777216
```

---

### set_default_stack_size

Set the default stack size for all subsequent `spawn()` calls. Does not affect already-spawned threads.

**Signature:**
```hemlock
set_default_stack_size(size: i64): null
```

**Parameters:**
- `size` - Stack size in bytes

**Example:**
```hemlock
import { set_default_stack_size, get_default_stack_size } from "@stdlib/async";

// Increase stack for deeply recursive tasks
set_default_stack_size(32 * 1024 * 1024);  // 32 MB
print(get_default_stack_size());  // 33554432

// All subsequent spawn() calls use 32 MB stacks
let task = spawn(recursive_fn, 10000);
let result = join(task);

// Restore default
set_default_stack_size(16 * 1024 * 1024);
```

**Note:** These functions wrap the internal `__get_default_stack_size()` / `__set_default_stack_size()` builtins.

---

## ThreadPool

A fixed-size thread pool for executing tasks in parallel. Uses a work queue with worker threads for efficient task distribution.

### Creating a Thread Pool

```hemlock
let pool = ThreadPool(num_workers: i32);
```

**Parameters:**
- `num_workers` - Number of worker threads (must be > 0)

**Returns:** Thread pool object

**Example:**
```hemlock
import { ThreadPool } from "@stdlib/async";

// Create pool with 4 workers
let pool = ThreadPool(4);

// Use the pool...

// Always shutdown when done
pool.shutdown();
```

### Methods

#### submit(task_fn)

Submit a zero-argument task to the pool.

**Signature:**
```hemlock
pool.submit(task_fn: fn): Future
```

**Parameters:**
- `task_fn` - Function with no arguments to execute

**Returns:** Future object for retrieving the result

**Example:**
```hemlock
fn compute() {
    return 42 * 42;
}

let future = pool.submit(compute);
let result = future.get();  // 1764
```

---

#### submit1(task_fn, arg1)

Submit a one-argument task to the pool.

**Signature:**
```hemlock
pool.submit1(task_fn: fn, arg1): Future
```

**Parameters:**
- `task_fn` - Function with one argument
- `arg1` - Argument to pass to the function

**Returns:** Future object for retrieving the result

**Example:**
```hemlock
fn square(n) {
    return n * n;
}

let future = pool.submit1(square, 10);
let result = future.get();  // 100
```

---

#### submit2(task_fn, arg1, arg2)

Submit a two-argument task to the pool.

**Signature:**
```hemlock
pool.submit2(task_fn: fn, arg1, arg2): Future
```

**Parameters:**
- `task_fn` - Function with two arguments
- `arg1` - First argument
- `arg2` - Second argument

**Returns:** Future object for retrieving the result

**Example:**
```hemlock
fn add(a, b) {
    return a + b;
}

let future = pool.submit2(add, 10, 20);
let result = future.get();  // 30
```

---

#### shutdown()

Gracefully shutdown the thread pool. Waits for all workers to finish current tasks.

**Signature:**
```hemlock
pool.shutdown(): null
```

**Example:**
```hemlock
let pool = ThreadPool(4);

// Submit tasks...

// Cleanup when done
pool.shutdown();
```

**Important:** Always call `shutdown()` when done with the pool to avoid resource leaks.

---

### Properties

#### num_workers

Number of worker threads in the pool.

**Type:** `i32`

**Access:** Read-only

---

## Future

Futures are returned by `submit`, `submit1`, and `submit2`. They represent pending results from submitted tasks.

### Methods

#### get()

Block until the result is available and return it.

**Signature:**
```hemlock
future.get(): any
```

**Returns:** The result of the task function

**Throws:** Re-throws any exception from the task

**Example:**
```hemlock
let future = pool.submit1(compute, data);
let result = future.get();  // Blocks until done
```

---

#### get_timeout(timeout_ms)

Try to get the result with a timeout.

**Signature:**
```hemlock
future.get_timeout(timeout_ms: i32): any | null
```

**Parameters:**
- `timeout_ms` - Maximum time to wait in milliseconds

**Returns:** The result if ready, `null` if timeout

**Throws:** Re-throws any exception from the task (if result is ready)

**Example:**
```hemlock
let future = pool.submit1(slow_compute, data);

// Try to get result with 100ms timeout
let result = future.get_timeout(100);
if (result == null) {
    print("Still computing...");
    result = future.get();  // Wait for completion
}
```

---

## parallel_map

Map a function over an array using a thread pool.

**Signature:**
```hemlock
parallel_map(arr: array, map_fn: fn, num_workers?: i32): array
```

**Parameters:**
- `arr` - Array of values to process
- `map_fn` - Function to apply to each element
- `num_workers` - Number of worker threads (default: 4)

**Returns:** Array of results in same order as input

**Example:**
```hemlock
import { parallel_map } from "@stdlib/async";

fn expensive_compute(n) {
    // Simulate expensive operation
    let result = 0;
    let i = 0;
    while (i < 1000000) {
        result = result + n;
        i = i + 1;
    }
    return result;
}

let data = [1, 2, 3, 4, 5, 6, 7, 8];
let results = parallel_map(data, expensive_compute, 4);
print(results);  // [1000000, 2000000, 3000000, ...]
```

---

## Complete Example

```hemlock
import { ThreadPool } from "@stdlib/async";

// CPU-bound work function
fn fibonacci(n) {
    if (n <= 1) { return n; }
    return fibonacci(n - 1) + fibonacci(n - 2);
}

// Create thread pool
let pool = ThreadPool(4);

// Submit multiple tasks
let futures = [];
let i = 30;
while (i <= 35) {
    futures.push(pool.submit1(fibonacci, i));
    i = i + 1;
}

// Collect results
print("Computing fibonacci numbers...");
i = 0;
let n = 30;
while (i < futures.length) {
    let result = futures[i].get();
    print("fib(" + n + ") = " + result);
    i = i + 1;
    n = n + 1;
}

// Cleanup
pool.shutdown();
```

---

## Error Handling

Exceptions thrown in tasks are captured and re-thrown when `get()` or `get_timeout()` is called:

```hemlock
fn failing_task() {
    throw "Something went wrong";
}

let pool = ThreadPool(2);
let future = pool.submit(failing_task);

try {
    let result = future.get();
} catch (e) {
    print("Task failed: " + e);  // "Task failed: Something went wrong"
}

pool.shutdown();
```

---

## Best Practices

1. **Match workers to CPU cores** - For CPU-bound work, use number of CPU cores
2. **Fewer workers for I/O** - For I/O-bound work, 2-4 workers is often sufficient
3. **Always shutdown** - Call `pool.shutdown()` to clean up resources
4. **Handle errors** - Wrap `future.get()` in try/catch for robust code
5. **Avoid blocking in tasks** - Keep tasks focused on computation

---

## See Also

- [async_fs](async_fs.md) - Async file system operations using ThreadPool
- [Concurrency API](../docs/reference/concurrency-api.md) - `spawn()`, `spawn_with()`, `join()`, `detach()`
- [Async Concurrency Guide](../docs/advanced/async-concurrency.md) - Language-level async/await
