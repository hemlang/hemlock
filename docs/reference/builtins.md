# Built-in Functions Reference

Complete reference for all built-in functions and constants in Hemlock v2.0.0.

> **v2.0.0 Breaking Change:** 63 builtins were moved from the global namespace to
> `@stdlib` modules. Functions like `sin()`, `open()`, `exec()`, `signal()`, and
> constants like `SIGINT`, `AF_INET` now require imports. See [Moved to Stdlib](#moved-to-stdlib-modules)
> for the full list.

---

## Global Builtins (No Import Required)

These are available everywhere without any `import` statement.

### I/O

| Function | Description |
|----------|-------------|
| `print(value, ...)` | Print values to stdout with newline |
| `write(value)` | Print value to stdout without newline (flushes immediately) |
| `eprint(value, ...)` | Print values to stderr with newline |
| `read_line()` | Read a line from stdin; returns `string` or `null` on EOF |

```hemlock
print("Hello", "world");    // Hello world\n
write("no newline");        // no newline (no \n)
eprint("error!");           // -> stderr
let name = read_line();     // blocks until input
```

### Memory Management

| Function | Description |
|----------|-------------|
| `alloc(size)` | Allocate `size` bytes of raw memory, returns `ptr` |
| `talloc(type, count)` | Type-aware allocation: `talloc(i32, 10)` allocates 10 i32s |
| `realloc(ptr, new_size)` | Resize previously allocated memory |
| `free(ptr)` | Free allocated memory |
| `memset(ptr, value, size)` | Set `size` bytes to `value` |
| `memcpy(dest, src, size)` | Copy `size` bytes from `src` to `dest` |
| `buffer(size)` | Create a bounds-checked buffer of `size` bytes |

```hemlock
let p = alloc(64);       // raw pointer, no bounds checking
let b = buffer(64);      // safe buffer, bounds checked
memset(p, 0, 64);
memcpy(b, p, 64);
free(p);                 // manual cleanup
```

### Type System

| Function | Description |
|----------|-------------|
| `typeof(value)` | Returns type name as string (`"i32"`, `"string"`, etc.) |
| `sizeof(type)` | Returns byte size of a type (`sizeof(i32)` → 4) |

**Type constructors** (used for conversion and typed allocation):

`i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, `f32`, `f64`, `bool`, `rune`, `ptr`

**Type aliases:** `integer` (i32), `number` (f64), `byte` (u8)

```hemlock
typeof(42);         // "i32"
typeof("hello");    // "string"
sizeof(i64);        // 8
let n = i32("42");  // parse string to i32
```

### Control Flow

| Function | Description |
|----------|-------------|
| `assert(condition, message?)` | Panic if condition is false |
| `panic(message)` | Immediate unrecoverable exit (not catchable by try/catch) |

```hemlock
assert(x > 0, "x must be positive");
panic("unrecoverable error");
```

### Concurrency

| Function | Description |
|----------|-------------|
| `spawn(fn, args...)` | Spawn an async task, returns task handle |
| `spawn_with(options, fn, args...)` | Spawn with per-thread config (`stack_size` in bytes, `name` string max 16 chars) |
| `join(task)` | Wait for task completion, returns result |
| `detach(task)` | Let task run independently (fire and forget) |
| `channel(capacity?)` | Create a communication channel (0 = unbuffered) |
| `select(channels)` | Wait on multiple channels |
| `apply(fn, args_array)` | Call function with array of arguments |

```hemlock
let task = spawn(fn(n) { return n * n; }, 42);
let result = join(task);  // 1764

let ch = channel(10);
ch.send("hello");
let msg = ch.recv();
```

### Pointer / FFI Helpers

These are global because they're low-level primitives used with `alloc`/`free`.

| Function | Description |
|----------|-------------|
| `ptr_offset(ptr, bytes)` | Offset a pointer by bytes |
| `ptr_null()` | Get a null pointer |
| `ptr_to_buffer(ptr, size)` | Wrap a pointer in a bounds-checked buffer |
| `buffer_ptr(buffer)` | Get the raw pointer from a buffer |
| `ffi_sizeof(type_name)` | Get FFI type size |

**Pointer read/write/deref** for all numeric types:

```
ptr_read_i8, ptr_read_i16, ptr_read_i32, ptr_read_i64
ptr_read_u8, ptr_read_u16, ptr_read_u32, ptr_read_u64
ptr_read_f32, ptr_read_f64, ptr_read_ptr

ptr_write_i8, ptr_write_i16, ptr_write_i32, ptr_write_i64
ptr_write_u8, ptr_write_u16, ptr_write_u32, ptr_write_u64
ptr_write_f32, ptr_write_f64, ptr_write_ptr

ptr_deref_i8, ptr_deref_i16, ptr_deref_i32, ptr_deref_i64
ptr_deref_u8, ptr_deref_u16, ptr_deref_u32, ptr_deref_u64
ptr_deref_f32, ptr_deref_f64, ptr_deref_ptr
```

All `ptr_read_*`, `ptr_write_*`, and `ptr_deref_*` functions accept both `ptr` and `buffer` types directly:

```hemlock
let p = alloc(8);
ptr_write_i32(p, 42);
let val = ptr_read_i32(p);  // 42
let p2 = ptr_offset(p, 4);
ptr_write_i32(p2, 99);
free(p);

// Also works directly with buffers (no buffer_ptr() needed)
let buf = buffer(8);
ptr_write_i32(buf, 42);
let bval = ptr_read_i32(buf);  // 42
```

---

## Moved to Stdlib Modules

These builtins were moved in v2.0.0 and now require imports.

### `@stdlib/math`

```hemlock
import { sin, cos, sqrt, floor, PI } from "@stdlib/math";
```

**Functions:** `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sqrt`, `pow`, `exp`, `log`, `log10`, `log2`, `floor`, `ceil`, `round`, `trunc`, `floori`, `ceili`, `roundi`, `trunci`, `div`, `divi`, `abs`, `min`, `max`, `clamp`, `rand`, `rand_range`, `seed`

**Constants:** `PI`, `E`, `TAU`, `INF`, `NAN`

### `@stdlib/env`

```hemlock
import { getenv, setenv } from "@stdlib/env";
```

**Functions:** `getenv`, `setenv`, `unsetenv`, `get_pid`, `exit`

### `@stdlib/signal`

```hemlock
import { signal, raise, SIGUSR1 } from "@stdlib/signal";
```

**Functions:** `signal`, `raise`

**Constants:** `SIGINT`, `SIGTERM`, `SIGHUP`, `SIGQUIT`, `SIGABRT`, `SIGUSR1`, `SIGUSR2`, `SIGALRM`, `SIGCHLD`, `SIGPIPE`, `SIGCONT`, `SIGSTOP`, `SIGTSTP`, `SIGTTIN`, `SIGTTOU`

### `@stdlib/net`

```hemlock
import { AF_INET, SOCK_STREAM, socket_create } from "@stdlib/net";
```

**Functions:** `socket_create`, `dns_resolve`, `poll`

**Constants:** `AF_INET`, `AF_INET6`, `SOCK_STREAM`, `SOCK_DGRAM`, `IPPROTO_TCP`, `IPPROTO_UDP`, `SOL_SOCKET`, `SO_REUSEADDR`, `SO_KEEPALIVE`, `SO_RCVTIMEO`, `SO_SNDTIMEO`, `POLLIN`, `POLLOUT`, `POLLERR`, `POLLHUP`, `POLLNVAL`, `POLLPRI`

### `@stdlib/process`

```hemlock
import { exec, fork, kill } from "@stdlib/process";
```

**Functions:** `exec`, `exec_argv`, `fork`, `wait`, `waitpid`, `kill`, `abort`, `exit`, `get_pid`, `getppid`, `getuid`, `geteuid`, `getgid`, `getegid`

### `@stdlib/fs`

```hemlock
import { open, read_file } from "@stdlib/fs";
```

**Functions:** `open`, `read_file`, `write_file`, `append_file`, `remove_file`, `rename`, `copy_file`, `is_file`, `is_dir`, `file_stat`, `make_dir`, `remove_dir`, `list_dir`, `cwd`, `chdir`, `absolute_path`, `exists`

### `@stdlib/time`

```hemlock
import { now, sleep } from "@stdlib/time";
```

**Functions:** `now`, `time_ms`, `sleep`, `clock`

### `@stdlib/atomic`

```hemlock
import { atomic_load_i32, atomic_cas_i32, atomic_fence } from "@stdlib/atomic";
```

**Functions (i32):** `atomic_load_i32`, `atomic_store_i32`, `atomic_add_i32`, `atomic_sub_i32`, `atomic_and_i32`, `atomic_or_i32`, `atomic_xor_i32`, `atomic_cas_i32`, `atomic_exchange_i32`

**Functions (i64):** `atomic_load_i64`, `atomic_store_i64`, `atomic_add_i64`, `atomic_sub_i64`, `atomic_and_i64`, `atomic_or_i64`, `atomic_xor_i64`, `atomic_cas_i64`, `atomic_exchange_i64`

**Functions:** `atomic_fence`

### `@stdlib/debug`

```hemlock
import { task_debug_info, set_stack_limit } from "@stdlib/debug";
```

**Functions:** `task_debug_info`, `set_stack_limit`, `get_stack_limit`

### `@stdlib/ffi`

```hemlock
import { callback, callback_free } from "@stdlib/ffi";
```

**Functions:** `callback`, `callback_free`

### `@stdlib/strings`

```hemlock
import { string_concat_many } from "@stdlib/strings";
```

**Functions:** `string_concat_many`

---

## Migration Guide (v1.x → v2.0.0)

### Before (v1.x)
```hemlock
let x = sin(PI / 2);
let pid = get_pid();
signal(SIGUSR1, handler);
let f = open("file.txt", "r");
let r = exec("echo hello");
```

### After (v2.0.0)
```hemlock
import { sin, PI } from "@stdlib/math";
import { get_pid } from "@stdlib/env";
import { signal, SIGUSR1 } from "@stdlib/signal";
import { open } from "@stdlib/fs";
import { exec } from "@stdlib/process";

let x = sin(PI / 2);
let pid = get_pid();
signal(SIGUSR1, handler);
let f = open("file.txt", "r");
let r = exec("echo hello");
```

The function calls themselves are identical — only the imports change.
