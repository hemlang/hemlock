![Hemlock](logo.png)

# Hemlock

> A small, unsafe language for writing unsafe things safely.

Hemlock is a systems scripting language that combines the power of C with the ergonomics of modern scripting languages. It embraces manual memory management and explicit control while providing structured async concurrency built-in.

## Documentation

The checked-in documentation starts at **[docs/README.md](docs/README.md)**. Standard library module references live in **[stdlib/docs/](stdlib/docs/)**, and the release history is in **[CHANGELOG.md](CHANGELOG.md)**.

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
| **Arrays** | Dynamic, 23 methods (push, pop, map, filter, reduce, sort, fill, etc.) |
| **Concurrency** | async/await, real OS threads (pthreads), channels |
| **FFI** | Call C functions from shared libraries, export extern |
| **Error Handling** | try/catch/finally/throw, panic() |
| **I/O** | File API, signal handling, command execution |
| **Stdlib** | 53 modules (math, net, crypto, signal, atomic, ffi, vector, and more) |
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

**Quick Install (recommended):**
```bash
curl -fsSL https://raw.githubusercontent.com/hemlang/hemlock/main/install.sh | bash
```

**From source:**
```bash
sudo make install              # Install to /usr/local
make install PREFIX=~/.local   # Install to custom prefix
sudo make uninstall            # Remove installation
```

## WebAssembly (Browser)

Hemlock can run in a web browser by compiling the interpreter to WebAssembly via [Emscripten](https://emscripten.org/).

```bash
# Install Emscripten (one-time setup)
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk && ./emsdk install latest && ./emsdk activate latest && source ./emsdk_env.sh
cd ..

# Build the WASM interpreter
make wasm-interpreter

# Serve the browser example (builds WASM if needed)
make wasm-browser-example
# Open http://localhost:8080/examples/wasm-browser/index.html
```

Or use it from JavaScript:

```javascript
var Module = {
    print: function(text) { console.log(text); },
    onRuntimeInitialized: function() {
        var hemlockEval = Module.cwrap('hemlock_eval', 'number', ['string']);
        hemlockEval('print("Hello from Hemlock WASM!");');
    },
    noInitialRun: true
};
```

See [Installation - WASM Build](docs/getting-started/installation.md#webassembly-wasm-build) for full details and `examples/wasm-browser/` for a complete browser integration example.

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

Hemlock v2.4.0 is the current checked-in release. Highlights include:

- **v2.4.0 HTTP/auth + ergonomics batch** - `@stdlib/http` POST/PUT/DELETE/PATCH now actually send custom headers (`Authorization`, `X-Request-Id`, etc. were silently dropped before — only `Content-Type` made it past the wrapper). Interpreter named-module imports are live bindings instead of import-time snapshots, so reassigning an exported `let` propagates to importers and to spawned tasks. Flow null-narrowing follows `?.` field access. Type error labels report the parameter name instead of `'positional'`. `string.lower()`/`upper()` aliases. `get_binary` follows 3xx redirects. C codegen no longer prefixes call symbols with surrounding-scope tokens. macOS libwebsockets picks up Homebrew CA bundles automatically. Default LWS HTTP timeout dropped from ~30s to 5s.
- **v2.3.1 binary HTTP fixes** - `@stdlib/http.download(url, path)` now actually writes the buffer body (was silently writing zero-byte files in compiled binaries); new `download_streaming(url, path)` for bounded-memory pulls of large artifacts; new `stream.read_binary()` preserves 0x00 bytes that `read()` would have truncated at.
- **v2.3.0 streaming HTTP** - `@stdlib/http` now exports `stream()`, `stream_get()`, `stream_post()`, `post_json_stream()`, and `stream_sse()` for chunked HTTP and Server-Sent Events. Built on the already-bundled libwebsockets, so no new dependency. The compiler runtime now sends POST/PUT/PATCH bodies for streaming requests and throws the same catchable exceptions as the interpreter on invalid arguments — full interpreter/compiler parity.
- **v2.2.3 hardening release** - Socket connect/bind failures are catchable in compiled binaries, `/proc` and `/sys` pseudo-files read correctly, buffer memory builtins validate ranges, optional-chain null guards narrow in the compiler, CLI help parsing is stricter, and `@stdlib/fs` now includes recursive `make_dirs()`.
- **v2.2.2 hardening release** - Runtime `file_stat()` failures are catchable, typed-array fast-path assignments have stricter checks, non-default install prefixes find their stdlib/runtime layout, and documentation audit tooling is checked in.
- **v2.2.1 HTTP body fix** - Compiled binaries now send `Content-Type`, `Content-Length`, and request body bytes for POST/PUT/PATCH/DELETE, matching interpreter behavior.
- **v2.2.0 process and filesystem additions** - `@stdlib/process` exports `posix_spawn(argv, opts?)`; `@stdlib/fs` exports raw file-descriptor helpers `open_fd()` and `fileno()`.
- **Safer module/codegen behavior** - Fixes include two-library FFI import collision handling, module `init` symbol-collision prevention, and stricter compiler/interpreter parity checks.
- **Reduced builtin conflicts since v2.0.0** - 63 former globals moved to `@stdlib` modules (math, signal, net, process, fs, atomic, debug, ffi) to reduce global namespace pollution.
- Full type system with 64-bit integers and Unicode support.
- Pattern matching with destructuring, guards, and rest syntax.
- Expression-bodied functions, type aliases, named arguments, null coalescing, string-literal object keys, and safe dynamic indexing (`obj?[key]`).
- Manual memory management with safe (`buffer`) and unsafe (`ptr`) options.
- Async/await with pthread-backed task execution and channels.
- 53 stdlib modules with one API document per module.
- FFI for C interop with `export extern fn` for reusable library wrappers.
- Compiler backend (C code generation), formatter, bundler/packager, and LSP tooling.
- [hpm](https://github.com/hemlang/hpm) package manager with GitHub-based registry.
- A 1,400+ file Hemlock test corpus, including interpreter/compiler parity tests.

## Philosophy

> "We give you the tools to be safe (`buffer`, type annotations, bounds checking) but we don't force you to use them (`ptr`, manual memory, unsafe operations)."

Hemlock is **NOT** memory-safe. Dangling pointers, use-after-free, and buffer overflows are your responsibility. We provide the tools to help, but we don't force you to use them.

## License

MIT License

## Contributing

Hemlock is experimental and evolving. If you're interested in contributing, please read `CLAUDE.md` first to understand the design philosophy.
