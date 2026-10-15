# Migration Guide: v1.x to v2.0.0

## Breaking Change: Builtins Moved to Stdlib

Hemlock 2.0.0 moved 63 global builtins into `@stdlib` modules to reduce namespace pollution. Code that uses these functions without imports will get "undefined variable" errors.

## Quick Fix

Add the appropriate `import` statement for each function. The table below shows where each builtin moved.

### Math Functions

```hemlock
// Before (v1.x)
let x = sin(3.14);
let y = floor(2.7);
let z = divi(10, 3);

// After (v2.0.0)
import { sin, floor, divi } from "@stdlib/math";
let x = sin(3.14);
let y = floor(2.7);
let z = divi(10, 3);
```

**Moved functions:** `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinh`, `cosh`, `tanh`, `sqrt`, `cbrt`, `exp`, `log`, `log2`, `log10`, `floor`, `ceil`, `round`, `trunc`, `abs`, `pow`, `fmod`, `min`, `max`, `rand`, `div`, `divi`, `floori`, `ceili`, `roundi`, `trunci`

### Signal Handling

```hemlock
// Before
signal(SIGINT, handler);

// After
import { signal, raise, SIGINT, SIGTERM, SIGUSR1 } from "@stdlib/signal";
signal(SIGINT, handler);
```

### File System

```hemlock
// Before
let f = open("file.txt", "r");

// After
import { open } from "@stdlib/fs";
let f = open("file.txt", "r");
```

### Process / Environment

```hemlock
// Before
let home = getenv("HOME");
exec("ls");

// After
import { getenv, setenv } from "@stdlib/env";
import { exec } from "@stdlib/process";
```

### Networking

```hemlock
// Before
let sock = socket_create(AF_INET, SOCK_STREAM, 0);

// After
import { socket_create, AF_INET, SOCK_STREAM } from "@stdlib/net";
```

### Atomic Operations

```hemlock
// Before
atomic_store(ptr, 42);

// After
import { atomic_store, atomic_load, atomic_add } from "@stdlib/atomic";
```

### FFI Callbacks

```hemlock
// Before
let cb = callback(my_func);

// After
import { callback, callback_free } from "@stdlib/ffi";
```

### Debug / Stack

```hemlock
// Before
let info = task_debug_info(task);

// After
import { task_debug_info, set_stack_limit } from "@stdlib/debug";
```

## Complete Module Mapping

| Function(s) | New Module |
|-------------|-----------|
| Math functions (sin, cos, sqrt, etc.) | `@stdlib/math` |
| `signal`, `raise`, SIG* constants | `@stdlib/signal` |
| `open` | `@stdlib/fs` |
| `exec`, `exec_argv` | `@stdlib/process` |
| `getenv`, `setenv` | `@stdlib/env` |
| AF_*, SOCK_*, POLL* constants, `socket_create`, `dns_resolve`, `poll` | `@stdlib/net` |
| `atomic_*` operations | `@stdlib/atomic` |
| `callback`, `callback_free`, `ffi_sizeof` | `@stdlib/ffi` |
| `task_debug_info`, `set_stack_limit`, `get_stack_limit` | `@stdlib/debug` |
| `string_concat_many` | `@stdlib/strings` |
| `get_default_stack_size`, `set_default_stack_size` | `@stdlib/async` |

## No Other Breaking Changes

All other language features, syntax, and APIs remain backwards-compatible. Existing code only needs import additions.
