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

Hemlock v2.7.0 is the current checked-in release. Highlights include:

- **v2.7.0 language ergonomics + parity batch** - Object-literal method shorthand (`{ fn name() {...} }` as sugar for `name: fn() {...}`); side-effect imports (`import "./suite.hml";` / `import "@stdlib/module";` run a module without binding exports); `string.rfind()` and one-argument `substr(start)`; `@stdlib/strings.from_bytes()`; `@stdlib/bytes` IEEE 754 float bit casts (`f32_to_bits` et al.). `find()`/`substr()`/`slice()` are now codepoint-correct in both backends (compiled multibyte strings previously diverged), the parser no longer infinite-loops on certain syntax errors, FFI marshals `buffer` arguments to `ptr` parameters correctly, and an interpreter memory-safety batch fixes spawn deep-copy bugs, type-registry data races, HTTPS context leaks, and `dns_resolve()` thread safety.
- **v2.6.0 static-by-default linking on Linux** - `hemlockc` binaries are self-contained and portable across distros (libwebsockets does not keep its ABI stable between releases, which crashed dynamically-linked binaries on mismatched hosts); opt out with `--dynamic`. Also fixes a socket use-after-free in collections and argument shifting when builtins are used as first-class values.
- **v2.5.x memory-correctness series** - A sustained refcount/ownership audit of the compiled runtime driven by production usage: per-construct LeakSanitizer harness (`tests/leakhunt/`), sanitizer stress harness, and fixes for the `for-in`/`obj.keys()`, indexed-assignment, `string.split`, throw-unwind, closure-capture, property-assignment, and per-spawn-argument leaks, plus `@stdlib/sqlite` bind-parameter leaks, a macOS buffer double-free, interpreter shared-object read corruption, and joinable-pthread/accepted-socket resource leaks.
- **v2.4.x HTTP/auth + ergonomics batch** - `@stdlib/http` POST/PUT/DELETE/PATCH send custom headers (`Authorization` etc. were silently dropped before); interpreter named-module imports are live bindings instead of import-time snapshots; flow null-narrowing follows `?.`; `string.lower()`/`upper()` aliases; socket fds set `FD_CLOEXEC`; `file.read_binary()` preserves 0x00 bytes; `exec_argv()` gained a `stdin` option.
- **v2.3.x streaming HTTP** - `@stdlib/http` exports `stream()`, `stream_get()`, `stream_post()`, `post_json_stream()`, and `stream_sse()` for chunked HTTP and Server-Sent Events, built on the already-bundled libwebsockets; `download_streaming(url, path)` for bounded-memory pulls of large artifacts.
- **v2.2.x hardening releases** - Catchable socket/file errors in compiled binaries, `posix_spawn()`, `fs.open_fd()`/`fileno()`, recursive `make_dirs()`, stricter compiler/interpreter parity checks, and documentation audit tooling.
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
