# HemlockScript: Hemlock → WASM via Emscripten

> Portable Hemlock programs that run in the browser.

## Goal

Add a WASM compilation target to `hemlockc` so that Hemlock programs can run in
browsers and other WASM runtimes (Node/Deno/Cloudflare Workers). The approach:
compile Hemlock → C (existing pipeline) → WASM (via Emscripten), with a
browser-compatible runtime shim replacing POSIX-only builtins.

**Non-goal:** Rewriting the compiler or runtime from scratch. We leverage the
existing `hemlockc` C codegen and `libhemlock_runtime` as much as possible.

---

## Architecture

```
Hemlock source (.hml)
        ↓
   hemlockc (existing frontend + codegen)
        ↓
   Generated C code (existing)
        ↓
   emcc (Emscripten)  ←  libhemlock_runtime_wasm.a (WASM-adapted runtime)
        ↓
   program.wasm + program.js (loader/glue)
        ↓
   Browser / Node / Deno / WASM runtime
```

The key insight: `hemlockc` already emits portable C. We don't need a new
backend — we need a WASM-compatible runtime library and an Emscripten build
pipeline.

---

## Phase 1: Minimal WASM Build (Core Language Only)

**Outcome:** `make wasm` produces a `.wasm` + `.js` bundle that can run pure
computational Hemlock programs in the browser.

### 1.1 Create WASM runtime shim layer

Create `runtime/src/wasm_shim.c` with stub/replacement implementations for
POSIX-dependent functions. This file is compiled *instead of* the POSIX
implementations when targeting WASM.

**Functions to stub (error on call):**
- `fork()`, `execve()`, `waitpid()`, `kill()` — process management
- `signal()`, `raise()` — signal handling
- `dlopen()`, `dlsym()`, `dlclose()` — dynamic library loading (FFI)

**Functions to adapt:**
- `print()` / `eprint()` → Emscripten's `printf`/`fprintf(stderr)` (works as-is
  since Emscripten maps these to `console.log`/`console.error`)
- `sleep()` → `emscripten_sleep()` (requires `-sASYNCIFY`)
- `time_ms()` / `now()` → `emscripten_get_now()` or `gettimeofday()` (Emscripten
  provides these)

**Functions to disable (compile out with `#ifdef __EMSCRIPTEN__`):**
- All of `builtins_socket.c` (TCP/UDP sockets)
- All of `builtins_http.c` (libwebsockets-based HTTP)
- All of `builtins_process.c` (fork/exec/signals)
- All of `builtins_ffi.c` (dlopen-based FFI)
- Thread creation in `builtins_async.c` (pthread_create)

### 1.2 Add `#ifdef __EMSCRIPTEN__` guards to runtime

Wrap POSIX-only code in the existing runtime source files with preprocessor
guards. This is preferred over maintaining a separate fork of the runtime.

Files requiring guards:

| File | What to guard | Replacement |
|------|---------------|-------------|
| `builtins_process.c` | Entire file | Stubs that `panic("not available in WASM")` |
| `builtins_socket.c` | Entire file | Stubs |
| `builtins_http.c` | Entire file | Stubs |
| `builtins_ffi.c` | `dlopen`/`dlsym`/`ffi_call` | Stubs |
| `builtins_async.c` | `pthread_create`, channels | Single-threaded stubs (Phase 1) |
| `builtins_io.c` | `open()`, `read()`, `write()` | Emscripten MEMFS (works as-is for basic I/O) |
| `builtins_time.c` | `clock_gettime`, `nanosleep` | Emscripten equivalents |
| `builtins_crypto.c` | OpenSSL calls | Stubs (Phase 1), Web Crypto API (Phase 3) |
| `atomics.c` | Atomic ops | Emscripten provides `<stdatomic.h>`, should work |

### 1.3 Create WASM-specific Makefile target

Add to `runtime/Makefile`:

```makefile
# WASM build via Emscripten
wasm: $(BUILD_DIR)/libhemlock_runtime_wasm.a

$(BUILD_DIR)/libhemlock_runtime_wasm.a: $(WASM_OBJS)
	emar rcs $@ $^

$(BUILD_DIR)/wasm_%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	emcc $(WASM_CFLAGS) -c $< -o $@
```

Add to top-level `Makefile`:

```makefile
# Build Hemlock program for WASM
wasm: compiler runtime-wasm
	@echo "Usage: make wasm-compile FILE=program.hml"

wasm-compile: compiler runtime-wasm
	./hemlockc -c $(FILE) -o /tmp/hemlock_wasm.c
	emcc -O2 -s WASM=1 -s EXPORTED_FUNCTIONS='["_main"]' \
	     -I runtime/include \
	     /tmp/hemlock_wasm.c \
	     runtime/build/libhemlock_runtime_wasm.a \
	     -o $(basename $(FILE)).js
	@echo "Built: $(basename $(FILE)).js + $(basename $(FILE)).wasm"

runtime-wasm:
	$(MAKE) -C $(RUNTIME_DIR) wasm
```

### 1.4 Modify `hemlockc` to support WASM target

Add a `--target wasm` flag to `hemlockc` that:
1. Defines `__HEMLOCK_WASM__` in the generated C code (`#define __HEMLOCK_WASM__ 1`)
2. Emits `#include <emscripten.h>` when targeting WASM
3. Skips codegen for unsupported features (FFI extern declarations, signals)
4. Uses `emscripten_sleep()` instead of platform `sleep()`

Changes in `src/backends/compiler/main.c`:
- Add `--target wasm` CLI option
- Pass target info to `CodegenContext`

Changes in `src/backends/compiler/codegen_program.c`:
- Emit `#ifdef __EMSCRIPTEN__` preamble when targeting WASM
- Skip `extern fn` declarations (emit warning)

### 1.5 Create HTML test harness

Create `wasm/index.html` — a minimal test page:

```html
<!DOCTYPE html>
<html>
<head><title>HemlockScript</title></head>
<body>
  <pre id="output"></pre>
  <script>
    var Module = {
      print: function(text) {
        document.getElementById('output').textContent += text + '\n';
      },
      printErr: function(text) {
        console.error(text);
      }
    };
  </script>
  <script src="program.js"></script>
</body>
</html>
```

### Phase 1 deliverables
- `make wasm-compile FILE=hello.hml` produces working browser-runnable output
- All pure-computation Hemlock features work: variables, functions, closures,
  control flow, pattern matching, objects, arrays, strings, math, type system
- `print()` outputs to browser console / HTML element
- Unsupported features (FFI, sockets, process, signals) panic with clear message

### Phase 1 testing
- Add `tests/wasm/` directory with basic tests
- Script that compiles each test with emcc, runs with Node.js, compares output
- Reuse parity test infrastructure: same `.expected` files, different runner

---

## Phase 2: Browser I/O & Stdlib

**Outcome:** Hemlock programs can do useful work in the browser — file access
(virtual FS), time operations, and the portable stdlib modules work.

### 2.1 Emscripten virtual filesystem

Emscripten provides MEMFS (in-memory filesystem) by default. Hemlock's
`open()`/`read()`/`write()` calls already go through libc, so they work on
MEMFS without changes for Phase 1.

For persistent storage, add optional IDBFS (IndexedDB-backed):

```c
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// Mount persistent filesystem
EM_ASM(
    FS.mkdir('/persistent');
    FS.mount(IDBFS, {}, '/persistent');
    FS.syncfs(true, function(err) { /* loaded */ });
);
#endif
```

Add to `@stdlib/fs` WASM variant:
- `read_file()` / `write_file()` → work on MEMFS/IDBFS
- `list_dir()` → works on virtual FS
- `exists()` → works on virtual FS

### 2.2 Port the 22 already-portable stdlib modules

These modules are pure Hemlock and need zero changes:

`arena`, `assert`, `collections`, `csv`, `datetime`, `encoding`, `fmt`,
`iter`, `json`, `logging`, `math`, `path`, `random`, `regex`, `retry`,
`semver`, `strings`, `testing`, `terminal`, `toml`, `url`, `uuid`

**Work needed:** Ensure the stdlib module loader works in WASM context. The
existing `import { x } from "@stdlib/module"` mechanism compiles module code
inline — verify this works with emcc.

### 2.3 Adapt time module

- `now()` → `emscripten_get_now()` (high-resolution, already available)
- `time_ms()` → `gettimeofday()` (Emscripten provides this)
- `sleep(ms)` → `emscripten_sleep(ms)` (requires `-sASYNCIFY` flag)
- `clock()` → Emscripten provides `clock()` from libc

### 2.4 Adapt env/os/args modules

- `getenv()`/`setenv()` → Emscripten provides these (in-memory env)
- `platform()` → return `"wasm"`
- `arch()` → return `"wasm32"`
- `args` → Can be set via `Module.arguments` in JS

### Phase 2 deliverables
- 22 stdlib modules working in WASM
- Virtual filesystem for file I/O
- Time functions working
- `make wasm-test` runs stdlib tests in Node.js WASM runtime

---

## Phase 3: JavaScript Interop Bridge

**Outcome:** Hemlock WASM programs can call browser APIs and JavaScript
functions, and JavaScript can call Hemlock functions.

### 3.1 JavaScript bridge (`hemlock_js_bridge`)

Create a JS-to-WASM interop layer that replaces FFI for the browser:

```hemlock
// Instead of: import "libcrypto.so.6"; extern fn ...
// Use:        import { fetch, setTimeout } from "@wasm/browser";

import { fetch } from "@wasm/browser";
let response = await fetch("https://api.example.com/data");
print(response);
```

Implementation: Emscripten's `EM_JS` / `EM_ASM` macros for calling JS from C:

```c
// runtime/src/wasm_bridge.c
#ifdef __EMSCRIPTEN__
#include <emscripten.h>

EM_JS(char*, hml_js_fetch_sync, (const char* url), {
    // Synchronous fetch via Asyncify
    var xhr = new XMLHttpRequest();
    xhr.open('GET', UTF8ToString(url), false);
    xhr.send();
    return allocateUTF8(xhr.responseText);
});
#endif
```

### 3.2 Exported functions (Hemlock → JS)

Allow Hemlock functions to be called from JavaScript:

```hemlock
// math_utils.hml
export fn fibonacci(n: i32): i32 {
    if (n <= 1) { return n; }
    return fibonacci(n - 1) + fibonacci(n - 2);
}
```

Compiles to:
```c
EMSCRIPTEN_KEEPALIVE
HmlValue hml_fn_fibonacci(HmlClosureEnv* env, HmlValue n) { ... }
```

JS side:
```javascript
const fib = Module.cwrap('hml_fn_fibonacci', 'number', ['number', 'number']);
console.log(fib(0, 10)); // 55
```

### 3.3 Adapt networking modules for browser

Replace socket-based networking with browser APIs:

| Hemlock API | Browser replacement |
|-------------|-------------------|
| `http_get(url)` | `fetch()` via Asyncify |
| `http_post(url, body)` | `fetch()` with POST |
| `WebSocket(url)` | Browser `WebSocket` API |

These go in a new `@wasm/http` or adapt `@stdlib/http` with `#ifdef` in the
underlying runtime builtins.

### 3.4 Adapt crypto module

Replace OpenSSL with Web Crypto API:

```c
#ifdef __EMSCRIPTEN__
EM_JS(void, hml_sha256_wasm, (const char* input, int len, char* output), {
    // Use SubtleCrypto
    var data = HEAPU8.slice(input, input + len);
    crypto.subtle.digest('SHA-256', data).then(function(hash) {
        var arr = new Uint8Array(hash);
        for (var i = 0; i < 32; i++) HEAPU8[output + i] = arr[i];
    });
});
#endif
```

### Phase 3 deliverables
- JS ↔ Hemlock bidirectional function calls
- `@wasm/browser` module for browser APIs
- HTTP via fetch, WebSocket via browser WebSocket
- Crypto via Web Crypto API
- Exported Hemlock functions callable from JS

---

## Phase 4: Async & Threading (Stretch)

**Outcome:** Hemlock's `spawn`/`await` works in the browser using Web Workers.

### 4.1 Web Worker-based task spawning

Map Hemlock's `spawn()` to Web Workers:

```
Hemlock spawn(fn, args)  →  new Worker() with SharedArrayBuffer
Hemlock join(task)       →  Atomics.wait() / message passing
Hemlock channel          →  MessagePort or SharedArrayBuffer ring buffer
```

This is the hardest phase because:
- Each Worker needs its own WASM instance
- SharedArrayBuffer requires COOP/COEP headers
- Data must be serialized across Worker boundaries

### 4.2 Asyncify for blocking operations

Use Emscripten's Asyncify to handle blocking calls:
- `sleep()` → `emscripten_sleep()`
- `channel.recv()` → async wait
- `read_line()` → async stdin read

Asyncify adds ~10% code size overhead but enables synchronous-looking code.

### Phase 4 deliverables
- `spawn()` creates Web Workers
- Channels work across workers via SharedArrayBuffer
- `sleep()` and blocking I/O don't block the main thread

---

## File Changes Summary

### New files
```
runtime/src/wasm_shim.c          — WASM stubs for POSIX functions
runtime/src/wasm_bridge.c        — JS interop bridge (Phase 3)
wasm/                            — WASM output directory
wasm/index.html                  — Test harness HTML page
wasm/hemlock.js                  — Optional JS wrapper/loader API
tests/wasm/                      — WASM-specific tests
tests/wasm/run_wasm_tests.sh     — Test runner (uses Node.js)
```

### Modified files
```
Makefile                         — Add wasm, wasm-compile, wasm-test targets
runtime/Makefile                 — Add wasm build target using emcc/emar
src/backends/compiler/main.c     — Add --target wasm flag
src/backends/compiler/codegen_program.c — WASM preamble generation
runtime/src/builtins_process.c   — #ifdef __EMSCRIPTEN__ guards
runtime/src/builtins_socket.c    — #ifdef __EMSCRIPTEN__ guards
runtime/src/builtins_http.c      — #ifdef __EMSCRIPTEN__ guards
runtime/src/builtins_ffi.c       — #ifdef __EMSCRIPTEN__ guards
runtime/src/builtins_async.c     — #ifdef __EMSCRIPTEN__ guards
runtime/src/builtins_io.c        — #ifdef __EMSCRIPTEN__ for MEMFS/IDBFS
runtime/src/builtins_time.c      — #ifdef for emscripten_sleep
runtime/src/builtins_crypto.c    — #ifdef for Web Crypto stubs
runtime/include/hemlock_runtime.h — WASM feature detection macros
```

### Stdlib additions (Phase 3)
```
stdlib/wasm_browser.hml          — @wasm/browser module (fetch, DOM, etc.)
```

---

## Build Requirements

- **Emscripten SDK** (emcc, emar, emconfigure)
- **Node.js** (for running WASM tests outside browser)
- All existing build requirements (for the hemlockc compiler itself)

The compiler (`hemlockc`) still builds natively — only the *output* targets WASM.

---

## What Works Immediately (No Changes Needed)

These Hemlock features compile to standard C that Emscripten handles natively:

- All arithmetic, bitwise, logical operators
- Variables, scoping, closures
- Functions, recursion, expression-bodied functions
- if/else, while, for, loop, switch, pattern matching
- Objects, arrays, strings (all 19+23 methods)
- Type annotations and runtime type checking
- Try/catch/finally/throw
- Defer
- Template strings
- Null coalescing (`??`, `?.`, `??=`)
- Named arguments
- Compound types, type aliases
- `print()`, `eprint()` (via Emscripten's console mapping)
- `alloc()`/`free()`/`buffer()` (linear memory)
- `typeof()`, `len()`, `sizeof()`
- Math builtins (sin, cos, sqrt, etc.)
- All reference counting / memory management

---

## Estimated Scope Per Phase

| Phase | Scope | Dependencies |
|-------|-------|-------------|
| **Phase 1** | ~800 lines new C, ~200 lines #ifdef guards, Makefile changes | Emscripten SDK |
| **Phase 2** | ~200 lines C, stdlib testing, Makefile | Phase 1 |
| **Phase 3** | ~600 lines C (bridge), ~200 lines Hemlock (stdlib) | Phase 1 |
| **Phase 4** | ~1000 lines C (Worker threading), complex | Phase 1+3 |

Recommended order: Phase 1 → Phase 2 → Phase 3 → Phase 4

Phase 1 alone gives you a useful "Hemlock in the browser" for computational
workloads. Phases 2-4 incrementally add I/O and interop.
