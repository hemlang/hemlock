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
#define HEMLOCK_VERSION_MINOR 2
#define HEMLOCK_VERSION_PATCH 1

#define HEMLOCK_VERSION "2.2.1"
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
