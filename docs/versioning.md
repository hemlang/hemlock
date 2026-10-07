# Hemlock Versioning

This document describes the versioning strategy for Hemlock.

## Version Format

Hemlock uses **Semantic Versioning** (SemVer):

```
MAJOR.MINOR.PATCH
```

| Component | When to Increment |
|-----------|-------------------|
| **MAJOR** | Breaking changes to language semantics, stdlib API, or binary formats |
| **MINOR** | New features, backward-compatible additions |
| **PATCH** | Bug fixes, performance improvements, documentation |

## Unified Versioning

All Hemlock components share a **single version number**:

- **Interpreter** (`hemlock`)
- **Compiler** (`hemlockc`)
- **LSP Server** (`hemlock --lsp`)
- **Standard Library** (`@stdlib/*`)

The version is defined in `include/version.h`:

```c
#define HEMLOCK_VERSION_MAJOR 2
#define HEMLOCK_VERSION_MINOR 4
#define HEMLOCK_VERSION_PATCH 1

#define HEMLOCK_VERSION "2.4.1"
```

### Checking Versions

```bash
# Interpreter version
hemlock --version

# Compiler version
hemlockc --version
```

## Compatibility Guarantees

### Within a MAJOR Version

- Source code that works in `X.Y.0` will work in `X.Y.Z` (any patch)
- Source code that works in `X.0.Z` will work in `X.Y.Z` (any minor/patch)
- Compiled `.hmlb` bundles are compatible within the same MAJOR version
- Standard library APIs are stable (additions only, no removals)

### Across MAJOR Versions

- Breaking changes are documented in release notes
- Migration guides provided for significant changes
- Deprecated features warned for at least one minor release before removal

## Binary Format Versioning

Hemlock uses separate version numbers for binary formats:

| Format | Version | Location |
|--------|---------|----------|
| `.hmlc` (AST bundle) | `HMLC_VERSION` | `include/ast_serialize.h` |
| `.hmlb` (compressed bundle) | Same as HMLC | Uses zlib compression |
| `.hmlp` (packaged executable) | Magic: `HMLP` | Self-contained format |

Binary format versions increment independently when serialization changes.

## Standard Library Versioning

The standard library (`@stdlib/*`) is versioned **with the main release**:

```hemlock
// Always uses the stdlib bundled with your Hemlock installation
import { HashMap } from "@stdlib/collections";
import { sin, cos } from "@stdlib/math";
```

### Stdlib Compatibility

- New modules may be added in MINOR releases
- New functions may be added to existing modules in MINOR releases
- Function signatures are stable within a MAJOR version
- Deprecated functions are marked and documented before removal

## Version History

| Version | Date | Highlights |
|---------|------|------------|
| **2.11.0** | 2026-10-06 | Exact, strict JSON (de)serialization; substantially more precise borrow checker; `main_loop()`/`main_loop_stop()` in `@stdlib/time` for a proper wasm frame loop |
| **2.10.3** | 2026-08-08 | A program that `spawn()`s a `WebSocketServer` now exits (close paths call `lws_cancel_service()`) |
| **2.10.2** | 2026-08-08 | RNG seeded at startup (every process previously got the same `rand()`/`randint()` sequence); Windows static libwebsockets link only requests archives that exist |
| **2.10.1** | 2026-08-08 | Fix use-after-free when an HTTP redirect follows a completed request; Windows CI installs pkgconf and asserts libwebsockets is compiled in |
| **2.10.0** | 2026-08-08 | `@stdlib/websocket` and `@stdlib/http` work on Windows; Windows binaries ship with releases |
| **2.9.1** | 2026-07-28 | `array.sort()` is a stable O(n log n) merge sort on both backends (was quadratic, and segfaulted on large presorted arrays in compiled code) |
| **2.9.0** | 2026-07-27 | Semantics hardening: integer overflow policy, type annotations, buffer safety, async memory model and refcounting audited and fixed; `hemlock check`; generated stdlib API index |
| **2.8.2** | 2026-07-25 | Static lint pass; `--strict-types` usable on multi-file projects; unknown named arguments rejected at compile time; single-char string pool data race fixed |
| **2.8.1** | 2026-06-13 | Windows: CNG crypto hashing, `posix_spawn()`/`waitpid()`/`kill()`, bundled POSIX ERE regex, `@stdlib/termios`/`terminal`; compiled `@stdlib/mmap` fixed |
| **2.8.0** | 2026-06-10 | Windows (MinGW-w64) support for `hemlock`, `hemlockc` and the runtime (untagged; date from the version-bump commit) |
| **2.7.0** | 2026-06-09 | Object-literal method shorthand; side-effect imports; `string.rfind()`; parity batch from production consumers |
| **2.6.2** | 2026-06-05 | Built on 2.6.0: compiled `to_string`/`string_byte_length` as first-class values link; `HttpServer` survives empty/malformed requests; compiled worker threads block signals and zero-parameter signal handlers work (binaries report 2.6.0) |
| **2.6.1** | 2026-06-04 | **Regressed build — do not use.** Tagged on a stale v2.3.1-era branch; reverts everything from 2.4.0 through 2.6.0 and reports itself as 2.3.1 |
| **2.6.0** | 2026-06-03 | `hemlockc` links statically by default on Linux (`--dynamic` to opt out); fixes libwebsockets ABI-mismatch SIGSEGV, socket use-after-free in collections, and shifted arguments for builtins used as values |
| **2.5.7** | 2026-06-02 | Dropped task handles no longer leak joinable pthreads; accepted sockets are refcounted and freed |
| **2.5.6** | 2026-05-18 | Fix property-assignment RHS leak (`obj.field = <expr>`) in compiled code |
| **2.5.5** | 2026-05-18 | Interpreter: fix corrupted concurrent reads of shared objects from spawned tasks |
| **2.5.4** | 2026-05-18 | Fix per-spawn argument leak in `spawn`/`spawn_with` |
| **2.5.3** | 2026-05-18 | Fix double-free/use-after-free on explicit `free()` of a buffer (macOS heap corruption) |
| **2.5.2** | 2026-05-18 | Reverts two mis-scoped 2.5.0 codegen leak fixes that generated invalid C — **2.5.0 and 2.5.1 are broken, use 2.5.2+** |
| **2.5.1** | 2026-05-18 | `@stdlib/sqlite` no longer leaks a string/blob per parameterized query |
| **2.5.0** | 2026-05-17 | Memory-correctness release: compiled-runtime refcount/ownership audit, per-construct leak-hunt harness, ASan/TSan/LSan stress harness |
| **2.4.11** | 2026-05-15 | Fix pervasive leak of the RHS temp in indexed assignment (`obj[k] = v`) |
| **2.4.10** | 2026-05-15 | Fix multi-GB leak from uninitialized `is_pooled` on hand-built JSON objects |
| **2.4.9** | 2026-05-15 | Fix multi-GB leak from a redundant for-in iterable retain |
| **2.4.8** | 2026-05-15 | Fix silent infinite loop from missing `func_params` in codegen state |
| **2.4.7** | 2026-05-15 | Fix major refcount leak in `for k in obj.keys()`; concurrency/lifetime stress harness under ASan + TSan |
| **2.4.6** | 2026-05-15 | Freeze the immortal ASCII string pool (fix use-after-free after ~1M releases) |
| **2.4.5** | 2026-05-15 | Lock-free read-only `object_lookup_field` (fix concurrent `obj[key]` heap corruption) |
| **2.4.4** | 2026-05-15 | Fatal-signal backtrace handler and `-rdynamic` for in-place crash diagnosis |
| **2.4.3** | 2026-05-14 | macOS: partial fixes for `sleep()` timer coalescing on idle daemons |
| **2.4.2** | 2026-05-14 | macOS crash fixes: sqlite cross-thread use, libwebsockets SSL re-init, server fds 0/1/2 |
| **2.4.1** | 2026-05-14 | TCP listener + accepted-client socket fds set `FD_CLOEXEC` so `posix_spawn`'d children don't inherit (and pin) the parent's listener after a crash; new `file.read_binary()` returns a buffer preserving 0x00 bytes (mirrors `stream.read_binary` from 2.3.1); `exec_argv()` gained a `stdin` option that pipes a string into the child via `pipe(2)`; macOS Makefile pkg-config-fallback patch from `mac-build-docs` |
| **2.4.0** | 2026-05-13 | `@stdlib/http` POST/PUT/DELETE/PATCH thread custom headers; interpreter named-module imports are live bindings (post-init reassignments visible to spawned tasks); flow null-narrowing follows `?.`; type-error labels name the parameter; `string.lower()`/`upper()` aliases; `get_binary` follows 3xx; codegen call-symbol stability; macOS LWS auto-finds Homebrew CA bundles; default LWS HTTP timeout dropped 30s → 5s |
| **2.3.1** | 2026-05-12 | Binary HTTP fixes: `@stdlib/http.download()` actually writes the buffer body; new `download_streaming(url, path)` for bounded-memory large pulls; new `stream.read_binary()` + `__lws_http_stream_read_binary` preserving 0x00 bytes |
| **2.3.0** | 2026-05-12 | Streaming HTTP support: `stream()`, `stream_get()`, `stream_post()`, `post_json_stream()`, `stream_sse()`; compiler runtime sends POST bodies for streaming requests with full interpreter parity on catchable errors |
| **2.2.3** | 2026-05-12 | Catchable socket connect/bind failures; `/proc`/`/sys` `File.read()` fix; safer buffer memory builtins; optional-chain null-guard narrowing; recursive `fs.make_dirs()`; CLI help parsing and numeric string-concat fixes |
| **2.2.2** | 2026-05-11 | Catchable runtime `file_stat()` failures; typed-array fast-path assignment checks; non-default-prefix stdlib/runtime lookup fixes; documentation audit/check tooling |
| **2.2.1** | 2026-05-10 | Compiled binaries actually send HTTP POST/PUT/PATCH bodies (interpreter was already correct); libwebsockets startup banner suppressed by default in compiled binaries |
| **2.2.0** | 2026-05-10 | `posix_spawn()` primitive in `@stdlib/process`; `fs.open_fd()`/`fileno()` for raw fd access; string-literal object keys and `obj?[key]` safe-index; FFI two-library import fix; incremental builds track header deps |
| **2.1.1** | 2026-04-21 | Parser no longer hangs on malformed `match` arms parsed as object literals |
| **2.1.0** | 2026-04-21 | HTTP client actually sends POST/PUT/PATCH bodies; `@stdlib/json` exports its public API for namespace imports |
| **2.0.3** | 2026-04-21 | Fix compiler symbol collision when a module exports `init` |
| **2.0.2** | 2026-04-21 | Fix i32 overflow in HashMap djb2 string hash |
| **2.0.1** | 2026-04-18 | Patch release with bug fixes and stability improvements since v2.0.0 |
| **2.0.0** | 2026-04-05 | Breaking release: 63 builtins moved to stdlib modules |
| **1.8.7** | 2026-01-28 | Fix multi-argument print/eprint in compiler codegen |
| **1.8.6** | 2026-01-27 | Fix segfault in hml_string_append_inplace for SSO strings |
| **1.8.5** | 2026-01-27 | 5 new array methods (every, some, indexOf, sort, fill), major performance optimizations, memory leak fixes |
| **1.8.4** | 2026-01-19 | Graceful handling for reserved keywords (def, func, var, class), fix flaky CI tests |
| **1.8.3** | 2026-01-17 | Code polish: consolidate magic numbers, standardize error messages |
| **1.8.2** | 2026-01-17 | Memory leak prevention: exception-safe eval, task/channel cleanup, optimizer fixes |
| **1.8.1** | 2026-01-13 | Fix use-after-free bug in function return value handling |
| **1.8.0** | 2026-01-13 | Pattern matching, arena allocator, memory leak fixes |
| **1.7.5** | 2026-01-10 | Fix formatter else-if indentation bug |
| **1.7.4** | 2026-01-10 | Formatter improvements: function parameter, binary expr, import, and method chain line breaking |
| **1.7.3** | 2026-01-10 | Fix formatter comment and blank line preservation |
| **1.7.2** | 2026-01-06 | Maintenance release |
| **1.7.1** | 2026-01-04 | Single-line if/while/for statements (braceless syntax) |
| **1.7.0** | 2026-01-04 | Type aliases, function types, const params, method signatures, loop labels, named args, null coalescing |
| **1.6.7** | 2026-01-02 | Octal literals, block comments, hex/unicode escapes, numeric separators |
| **1.6.6** | 2026-01-02 | Float literals without leading zero, fix strength reduction bug |
| **1.6.5** | 2026-01-01 | Fix for-in loop syntax without 'let' keyword |
| **1.6.4** | 2026-01-01 | Hotfix release |
| **1.6.3** | 2026-01-01 | Fix runtime method dispatch for file, channel, socket types |
| **1.6.2** | 2026-01-01 | Patch release |
| **1.6.1** | 2026-01-01 | Patch release |
| **1.6.0** | 2025-12-31 | Compile-time type checking in hemlockc, LSP integration, compound bitwise operators (`&=`, `\|=`, `^=`, `<<=`, `>>=`, `%=`) |
| **1.5.0** | 2025-12-30 | Full type system, async/await, atomics, 39 stdlib modules, FFI struct support, 99 parity tests |
| **1.3.0** | 2025-12-24 | Proper lexical block scoping (JS-like let/const semantics), per-iteration loop closures |
| **1.2.3** | 2025-12-24 | Import star syntax (`import * from`) |
| **1.2.2** | 2025-12-23 | Add `export extern` support, cross-platform test fixes |
| **1.2.1** | 2025-12-23 | Fix macOS test failures (RSA key generation, directory symlinks) |
| **1.2.0** | 2025-12-23 | AST optimizer, apply() builtin, unbuffered channels, 7 new stdlib modules, 97 parity tests |
| **1.1.3** | 2025-12-15 | Documentation updates, consistency fixes |
| **1.1.1** | 2025-12-13 | Bug fixes and improvements |
| **1.1.0** | 2025-12-11 | Unified versioning across all components |
| **1.0.x** | 2025-12-06 | Initial release series |

## Release Process

1. Version bump in `include/version.h`
2. Update changelog
3. Run full test suite (`make test-all`)
4. Tag release in git
5. Build release artifacts

## Checking Compatibility

To verify your code works with a specific Hemlock version:

```bash
# Run tests against installed version
make test

# Check parity between interpreter and compiler
make parity
```

## Future: Project Manifests

A future release may introduce optional project manifests for version constraints:

```hemlock
// Hypothetical project.hml
define Project {
    name: "my-app",
    version: "1.0.0",
    hemlock: ">=1.1.0"
}
```

This is not yet implemented but is part of the roadmap.
