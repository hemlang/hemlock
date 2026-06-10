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
#define HEMLOCK_VERSION_MINOR 7
#define HEMLOCK_VERSION_PATCH 0

#define HEMLOCK_VERSION "2.7.0"
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
| **2.7.0** | 2026-06-10 | Language-ergonomics and parity batch: object-literal method shorthand (`{ fn name() {...} }`); side-effect imports (`import "./suite.hml";`); `string.rfind()`; one-argument `substr(start)`; `@stdlib/strings.from_bytes()`; `@stdlib/bytes` IEEE 754 float bit casts; codepoint-correct `find`/`substr`/`slice` in both backends; parser forward-progress guard (no more infinite loop on certain syntax errors); FFI buffer-to-`ptr` marshaling fix; large interpreter memory-safety batch (spawn deep-copy, type-registry locking, HTTPS context leak, `dns_resolve` thread safety) |
| **2.6.0** | 2026-06-03 | `hemlockc` links statically by default on Linux (opt out with `--dynamic`), fixing the libwebsockets ABI-skew SIGSEGV on mismatched distros; socket refcounting fixes a use-after-free for sockets stored in arrays/objects; env-first wrapper shims fix argument shifting when builtins are used as first-class values |
| **2.5.7** | 2026-06-02 | `task_free()` detaches dropped joinable task threads (thread-resource leak); accepted sockets are refcounted, closing the per-connection `accept()` leak |
| **2.5.6** | 2026-05-18 | Fix compiled property-assignment RHS leak — every `obj.field = value` orphaned the value |
| **2.5.5** | 2026-05-18 | Interpreter concurrency fixes: shared-object reads from spawned tasks no longer rebuild the hash table on the read path (heap corruption); property inline cache bypassed once any task has spawned |
| **2.5.4** | 2026-05-18 | Fix per-spawn argument leak in compiled `spawn`/`spawn_with` (per-connection bleed for spawn-per-request servers) |
| **2.5.3** | 2026-05-18 | macOS heap-corruption fix: explicit `free()` of a buffer no longer double-frees the refcount-managed handle |
| **2.5.2** | 2026-05-18 | Regression fix: reverted two mis-scoped 2.5.0 codegen leak fixes that generated invalid C — 2.5.0/2.5.1 are broken, upgrade to 2.5.2 |
| **2.5.1** | 2026-05-18 | `@stdlib/sqlite` no longer leaks a C string/blob for every bound string/blob parameter (SQLite now frees via a bind destructor) |
| **2.5.0** | 2026-05-17 | Memory-correctness release: per-construct leakhunt harness + sanitizer stress harness; fixes for `for-in`/`obj.keys()`, indexed-assignment RHS, `string.split`, throw-unwind, and closure-capture leaks; frozen immortal string pool (UAF); lock-free `object_lookup_field` |
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
| **1.8.7** | 2026 | Fix multi-argument print/eprint in compiler codegen |
| **1.8.6** | 2026 | Fix segfault in hml_string_append_inplace for SSO strings |
| **1.8.5** | 2026 | 5 new array methods (every, some, indexOf, sort, fill), major performance optimizations, memory leak fixes |
| **1.8.4** | 2026 | Graceful handling for reserved keywords (def, func, var, class), fix flaky CI tests |
| **1.8.3** | 2026 | Code polish: consolidate magic numbers, standardize error messages |
| **1.8.2** | 2026 | Memory leak prevention: exception-safe eval, task/channel cleanup, optimizer fixes |
| **1.8.1** | 2026 | Fix use-after-free bug in function return value handling |
| **1.8.0** | 2026 | Pattern matching, arena allocator, memory leak fixes |
| **1.7.5** | 2026 | Fix formatter else-if indentation bug |
| **1.7.4** | 2026 | Formatter improvements: function parameter, binary expr, import, and method chain line breaking |
| **1.7.3** | 2026 | Fix formatter comment and blank line preservation |
| **1.7.2** | 2026 | Maintenance release |
| **1.7.1** | 2026 | Single-line if/while/for statements (braceless syntax) |
| **1.7.0** | 2026 | Type aliases, function types, const params, method signatures, loop labels, named args, null coalescing |
| **1.6.7** | 2026 | Octal literals, block comments, hex/unicode escapes, numeric separators |
| **1.6.6** | 2026 | Float literals without leading zero, fix strength reduction bug |
| **1.6.5** | 2026 | Fix for-in loop syntax without 'let' keyword |
| **1.6.4** | 2026 | Hotfix release |
| **1.6.3** | 2026 | Fix runtime method dispatch for file, channel, socket types |
| **1.6.2** | 2026 | Patch release |
| **1.6.1** | 2026 | Patch release |
| **1.6.0** | 2025 | Compile-time type checking in hemlockc, LSP integration, compound bitwise operators (`&=`, `\|=`, `^=`, `<<=`, `>>=`, `%=`) |
| **1.5.0** | 2024 | Full type system, async/await, atomics, 39 stdlib modules, FFI struct support, 99 parity tests |
| **1.3.0** | 2025 | Proper lexical block scoping (JS-like let/const semantics), per-iteration loop closures |
| **1.2.3** | 2025 | Import star syntax (`import * from`) |
| **1.2.2** | 2025 | Add `export extern` support, cross-platform test fixes |
| **1.2.1** | 2025 | Fix macOS test failures (RSA key generation, directory symlinks) |
| **1.2.0** | 2025 | AST optimizer, apply() builtin, unbuffered channels, 7 new stdlib modules, 97 parity tests |
| **1.1.3** | 2025 | Documentation updates, consistency fixes |
| **1.1.1** | 2025 | Bug fixes and improvements |
| **1.1.0** | 2024 | Unified versioning across all components |
| **1.0.x** | 2024 | Initial release series |

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
