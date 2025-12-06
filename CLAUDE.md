# Hemlock Language Design Philosophy

> "A small, unsafe language for writing unsafe things safely."

This document captures the core design principles for AI assistants working with Hemlock.
For detailed documentation, see `docs/README.md` and the `stdlib/docs/` directory.

---

## Core Identity

Hemlock is a **systems scripting language** with manual memory management and explicit control:
- The power of C with modern scripting ergonomics
- Structured async concurrency built-in
- No hidden behavior or magic

**Hemlock is NOT:** Memory-safe, a GC language, or hiding complexity.
**Hemlock IS:** Explicit over implicit, educational, a "C scripting layer" for systems work.

---

## Design Principles

### 1. Explicit Over Implicit
- Semicolons mandatory (no ASI)
- Manual memory management (alloc/free)
- Type annotations optional but checked at runtime

### 2. Dynamic by Default, Typed by Choice
- Every value has a runtime type tag
- Literals infer types: `42` → i32, `5000000000` → i64, `3.14` → f64
- Optional type annotations enforce runtime checks

### 3. Unsafe is a Feature
- Pointer arithmetic allowed (user's responsibility)
- No bounds checking on raw `ptr` (use `buffer` for safety)
- Double-free crashes allowed

### 4. Structured Concurrency First-Class
- `async`/`await` built-in with pthread-based parallelism
- Channels for communication
- `spawn`/`join`/`detach` for task management

### 5. C-like Syntax
- `{}` blocks always required
- Operators match C: `+`, `-`, `*`, `/`, `%`, `&&`, `||`, `!`, `&`, `|`, `^`, `<<`, `>>`
- Type syntax: `let x: type = value;`

---

## Quick Reference

### Types
```
Signed:   i8, i16, i32, i64
Unsigned: u8, u16, u32, u64
Floats:   f32, f64
Other:    bool, string, rune, array, ptr, buffer, null, object, file, task, channel
Aliases:  integer (i32), number (f64), byte (u8)
```

**Type promotion:** i8 → i16 → i32 → i64 → f32 → f64 (floats always win)

### Literals
```hemlock
let x = 42;              // i32
let big = 5000000000;    // i64 (> i32 max)
let hex = 0xDEADBEEF;    // hex literal
let bin = 0b1010;        // binary literal
let pi = 3.14;           // f64
let s = "hello";         // string
let ch = 'A';            // rune
let emoji = '🚀';        // rune (Unicode)
let arr = [1, 2, 3];     // array
let obj = { x: 10 };     // object
```

### Memory
```hemlock
let p = alloc(64);       // raw pointer
let b = buffer(64);      // safe buffer (bounds checked)
memset(p, 0, 64);
memcpy(dest, src, 64);
free(p);                 // manual cleanup required
```

### Control Flow
```hemlock
if (x > 0) { } else if (x < 0) { } else { }
while (cond) { break; continue; }
for (let i = 0; i < 10; i = i + 1) { }
for (item in array) { }
switch (x) { case 1: break; default: break; }
defer cleanup();         // runs when function returns
```

### Functions
```hemlock
fn add(a: i32, b: i32): i32 { return a + b; }
fn greet(name: string, msg?: "Hello") { print(msg + " " + name); }
let f = fn(x) { return x * 2; };  // anonymous/closure
```

### Objects & Enums
```hemlock
define Person { name: string, age: i32, active?: true }
let p: Person = { name: "Alice", age: 30 };
let json = p.serialize();
let restored = json.deserialize();

enum Color { RED, GREEN, BLUE }
enum Status { OK = 0, ERROR = 1 }
```

### Error Handling
```hemlock
try { throw "error"; } catch (e) { print(e); } finally { cleanup(); }
panic("unrecoverable");  // exits immediately, not catchable
```

### Async/Concurrency
```hemlock
async fn compute(n: i32): i32 { return n * n; }
let task = spawn(compute, 42);
let result = await task;     // or join(task)
detach(spawn(background_work));

let ch = channel(10);
ch.send(value);
let val = ch.recv();
ch.close();
```

### File I/O
```hemlock
let f = open("file.txt", "r");  // modes: r, w, a, r+, w+, a+
let content = f.read();
f.write("data");
f.seek(0);
f.close();
```

### Signals
```hemlock
signal(SIGINT, fn(sig) { print("Interrupted"); });
raise(SIGUSR1);
```

---

## String Methods (19)

`substr`, `slice`, `find`, `contains`, `split`, `trim`, `to_upper`, `to_lower`,
`starts_with`, `ends_with`, `replace`, `replace_all`, `repeat`, `char_at`,
`byte_at`, `chars`, `bytes`, `to_bytes`, `deserialize`

Template strings: `` `Hello ${name}!` ``

## Array Methods (18)

`push`, `pop`, `shift`, `unshift`, `insert`, `remove`, `find`, `contains`,
`slice`, `join`, `concat`, `reverse`, `first`, `last`, `clear`, `map`, `filter`, `reduce`

Typed arrays: `let nums: array<i32> = [1, 2, 3];`

---

## Standard Library (21 modules)

Import with `@stdlib/` prefix:
```hemlock
import { sin, cos, PI } from "@stdlib/math";
import { HashMap, Queue, Set } from "@stdlib/collections";
import { read_file, write_file } from "@stdlib/fs";
import { TcpStream, UdpSocket } from "@stdlib/net";
```

| Module | Description |
|--------|-------------|
| `collections` | HashMap, Queue, Stack, Set, LinkedList |
| `math` | sin, cos, sqrt, pow, rand, PI, E |
| `time` | now, time_ms, sleep, clock |
| `datetime` | DateTime class, formatting, parsing |
| `env` | getenv, setenv, exit, get_pid |
| `process` | fork, exec, wait, kill |
| `fs` | read_file, write_file, list_dir, exists |
| `net` | TcpListener, TcpStream, UdpSocket |
| `regex` | compile, test (POSIX ERE) |
| `strings` | pad_left, is_alpha, reverse, lines |
| `compression` | gzip, gunzip, deflate |
| `crypto` | aes_encrypt, rsa_sign, random_bytes |
| `encoding` | base64_encode, hex_encode, url_encode |
| `hash` | sha256, sha512, md5, djb2 |
| `http` | http_get, http_post, http_request |
| `json` | parse, stringify, pretty, get, set |
| `logging` | Logger with levels |
| `os` | platform, arch, cpu_count, hostname |
| `terminal` | ANSI colors and styles |
| `testing` | describe, test, expect |
| `websocket` | WebSocket client |

See `stdlib/docs/` for detailed module documentation.

---

## FFI (Foreign Function Interface)

Call C functions from shared libraries:
```hemlock
let lib = ffi_open("libc.so.6");
let puts = ffi_bind(lib, "puts", [FFI_POINTER], FFI_INT);
puts("Hello from C!");
ffi_close(lib);
```

Types: `FFI_INT`, `FFI_DOUBLE`, `FFI_POINTER`, `FFI_STRING`, `FFI_VOID`, etc.

---

## Project Structure

```
hemlock/
├── src/                  # Implementation
│   ├── lexer.c, parser/, interpreter/
│   ├── compiler/         # C code generation
│   └── lsp/              # Language Server Protocol
├── runtime/              # Compiled program runtime
├── stdlib/               # Standard library (21 modules)
│   └── docs/             # Module documentation
├── docs/                 # Full documentation
│   ├── language-guide/   # Types, strings, arrays, etc.
│   ├── reference/        # API references
│   └── advanced/         # Async, FFI, signals, etc.
├── tests/                # 491 tests
└── examples/             # Example programs
```

---

## What NOT to Do

❌ Add implicit behavior (ASI, GC, auto-cleanup)
❌ Hide complexity (magic optimizations, hidden refcounts)
❌ Break existing semantics (semicolons, manual memory, mutable strings)
❌ Lose precision in implicit conversions

---

## Testing

```bash
make test              # Run all tests
make test | grep async # Filter by category
```

Test categories: primitives, memory, strings, arrays, functions, objects, async, ffi, defer, signals, switch, bitwise, typed_arrays, modules, stdlib_*

---

## Version

**v1.0** - Current release with:
- Full type system (i8-i64, u8-u64, f32/f64, bool, string, rune, ptr, buffer, array, object, enum, file, task, channel)
- UTF-8 strings with 19 methods
- Arrays with 18 methods including map/filter/reduce
- Manual memory management with `talloc()` and `sizeof()`
- Async/await with true pthread parallelism
- 21 stdlib modules
- FFI for C interop
- defer, try/catch/finally/throw, panic
- File I/O, signal handling, command execution
- Compiler backend (C code generation)
- LSP server for IDE support
- 491 tests (100% pass rate)

---

## Philosophy

> We give you the tools to be safe (`buffer`, type annotations, bounds checking) but we don't force you to use them (`ptr`, manual memory, unsafe operations).

**If you're not sure whether a feature fits Hemlock, ask: "Does this give the programmer more explicit control, or does it hide something?"**

If it hides, it probably doesn't belong in Hemlock.
