![Hemlock](logo.png)

# Hemlock

> A small, unsafe language for writing unsafe things safely.

Hemlock is a systems scripting language that combines the power of C with the ergonomics of modern scripting languages. It embraces manual memory management and explicit control while providing structured async concurrency built-in.

## Documentation

For the complete language manual and documentation, visit **[hem-doc](https://github.com/hemlang/hem-doc)**.

## Design Philosophy

- **Explicit over implicit** - No hidden behavior, no magic
- **Manual memory management** - You allocate, you free
- **Dynamic by default, typed by choice** - Optional type annotations with runtime checking
- **Unsafe is a feature** - Full control when you need it, safety tools when you want them

## Quick Start

```hemlock
print("Hello, World!");
```

### Memory Management

```hemlock
// Raw pointer (dangerous but flexible)
let p: ptr = alloc(64);
memset(p, 0, 64);
free(p);

// Safe buffer (bounds checked)
let buf: buffer = buffer(64);
buf[0] = 65;
free(buf);
```

### Async Concurrency

```hemlock
async fn compute(n: i32): i32 {
    let sum = 0;
    for (let i = 0; i < n; i = i + 1) {
        sum = sum + i;
    }
    return sum;
}

// Spawn tasks on separate OS threads (real pthreads!)
let t1 = spawn(compute, 1000);
let t2 = spawn(compute, 2000);

// Wait for results (both computing in parallel)
let r1 = join(t1);
let r2 = join(t2);
```

### FFI (Foreign Function Interface)

```hemlock
import "libc.so.6";

extern fn strlen(s: string): i32;
extern fn getpid(): i32;

let len = strlen("Hello!");
let pid = getpid();
```

## Features

| Category | Highlights |
|----------|------------|
| **Types** | i8-i64, u8-u64, f32/f64, bool, string, rune, ptr, buffer, array, object |
| **Memory** | alloc, free, memset, memcpy, realloc, talloc, sizeof |
| **Strings** | UTF-8, mutable, 19 methods (substr, split, trim, replace, etc.) |
| **Arrays** | Dynamic, 18 methods (push, pop, map, filter, reduce, etc.) |
| **Concurrency** | async/await, real OS threads (pthreads), channels |
| **FFI** | Call C functions from shared libraries, export extern |
| **Error Handling** | try/catch/finally/throw, panic() |
| **I/O** | File API, signal handling, command execution |
| **Stdlib** | 39 modules (math, net, crypto, http, json, and more) |
| **Packages** | [hpm](https://github.com/hemlang/hpm) package manager with GitHub registry |

## Building

### Dependencies

**macOS:**
```bash
brew install libffi openssl@3 libwebsockets
```

**Ubuntu/Debian:**
```bash
sudo apt-get install libffi-dev libssl-dev libwebsockets-dev
```

### Compile and Test

```bash
make        # Build hemlock
make test   # Run all tests
```

### Install

```bash
sudo make install              # Install to /usr/local
make install PREFIX=~/.local   # Install to custom prefix
sudo make uninstall            # Remove installation
```

## Running Programs

```bash
./hemlock program.hml              # Run a program
./hemlock program.hml arg1 arg2    # With arguments
./hemlock                          # Start REPL
```

## Bundling & Packaging

Hemlock provides tools to bundle multi-file projects and create self-contained executables.

### Bundle (Portable Bytecode)

Resolve all imports and create a single distributable file:

```bash
./hemlock --bundle app.hml                    # Create app.hmlc
./hemlock --bundle app.hml --compress         # Create app.hmlb (smaller)
./hemlock --bundle app.hml -o dist/app.hmlc   # Custom output path
```

### Package (Self-Contained Executable)

Create a standalone executable that includes the interpreter:

```bash
./hemlock --package app.hml                   # Create ./app executable
./hemlock --package app.hml -o myapp          # Custom name
./hemlock --package app.hml --no-compress     # Faster startup, larger file
```

The packaged executable runs anywhere without needing Hemlock installed.

See [Bundling & Packaging](docs/advanced/bundling-packaging.md) for details.

## Project Status

Hemlock v1.2.2 is released with:

- Full type system with 64-bit integers and Unicode support
- Manual memory management with safe and unsafe options
- Async/await with true pthread parallelism
- 39 stdlib modules
- FFI for C interop with `export extern fn` for reusable library wrappers
- Compiler backend (C code generation)
- LSP server with go-to-definition and find-references
- HTTP PUT/DELETE/PATCH and WebSocket binary support
- [hpm](https://github.com/hemlang/hpm) package manager with GitHub-based registry
- 625+ tests with 97 parity tests (100% pass rate)

## Philosophy

> "We give you the tools to be safe (`buffer`, type annotations, bounds checking) but we don't force you to use them (`ptr`, manual memory, unsafe operations)."

Hemlock is **NOT** memory-safe. Dangling pointers, use-after-free, and buffer overflows are your responsibility. We provide the tools to help, but we don't force you to use them.

## License

MIT License

## Contributing

Hemlock is experimental and evolving. If you're interested in contributing, please read `CLAUDE.md` first to understand the design philosophy.
