# Changelog

All notable changes to Hemlock will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.6.7] - 2026-01-02

### Added

- Octal literals with `0o` prefix (e.g., `0o777`, `0O123`)
- Block comments (`/* ... */`) for multi-line comments
- Hex escape sequences in strings, template strings, and runes (`\xNN`)
- Unicode escape sequences in strings and template strings (`\u{XXXX}`)
- Numeric separators (underscores) for improved readability (e.g., `1_000_000`, `0xFF_FF`)
- 4 new parity tests for lexer enhancements

## [1.6.6] - 2026-01-02

### Added

- Float literals without leading zero (e.g., `.5`, `.123`, `.5e2`)

### Fixed

- Strength reduction optimizer incorrectly converted float*integer to shift operation

## [1.6.5] - 2026-01-02

### Fixed

- Parser now supports for-in loop syntax without 'let' keyword: `for (item in array) { }`

## [1.6.4] - 2026-01-02

### Changed

- Version bump

## [1.6.3] - 2026-01-02

### Fixed

- Runtime method dispatch for `HML_VAL_FILE` type (read, write, seek, tell, close, read_bytes, write_bytes)
- Runtime method dispatch for `HML_VAL_CHANNEL` type (send, recv, recv_timeout, send_timeout, close)
- Runtime method dispatch for `HML_VAL_SOCKET` type (bind, listen, accept, connect, send, recv, sendto, recvfrom, setsockopt, set_timeout, set_nonblocking, close)
- Missing `deserialize` method dispatch for strings

### Added

- `hml_file_read_bytes()` and `hml_file_write_bytes()` runtime functions for binary file I/O

## [1.6.2] - 2026-01-01

### Changed

- Version bump

## [1.6.1] - 2026-01-01

### Changed

- Version bump

## [1.6.0] - 2025-12-31

### Added

- **Compile-time type checking** in hemlockc compiler (enabled by default)
  - `--check` flag for type checking only without compilation
  - `--no-type-check` flag to disable type checking
  - `--strict-types` flag to warn on implicit `any` types
- **LSP integration** with hemlockc's type checking for real-time diagnostics
- **Compound bitwise assignment operators**: `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`
- **Color constants** support
- Unboxing optimization hints from type checker for better performance
- Comprehensive LSP test suite

### Changed

- **Breaking**: Type annotations no longer parse strings implicitly
  - Old: `let n: i32 = "42";` (no longer works)
  - New: `let n = i32("42");` (use type constructor)
- **Type precision improvement**: i64/u64 + f32 now promotes to f64 to preserve precision
- Unified type system (merged type_infer into type_check)
- Type checker now allows valid runtime conversions

### Fixed

- Function parameters incorrectly treated as unboxable
- Unboxing optimization mismatch causing GCC errors
- Imported module-level variables in main file
- Closure upvalue handling
- Various LSP bugs and diagnostics
- Memory management documentation inconsistencies
- Closure mutation documentation

### Internal

- Cleaned up code duplication and removed dead code
- Added comprehensive test infrastructure improvements

## [1.5.0] - 2024-12-01

### Added

- Full type system (i8-i64, u8-u64, f32/f64, bool, string, rune, ptr, buffer, array, object, enum, file, task, channel)
- UTF-8 strings with 19 methods
- Arrays with 18 methods including map/filter/reduce
- Manual memory management with `talloc()` and `sizeof()`
- Async/await with true pthread parallelism
- Atomic operations for lock-free concurrent programming
- 39 stdlib modules
- FFI for C interop with `export extern fn`
- FFI struct support in compiler
- FFI pointer helpers (`ptr_null`, `ptr_read_*`, `ptr_write_*`)
- defer, try/catch/finally/throw, panic
- File I/O, signal handling, command execution
- hpm package manager with GitHub-based registry
- Compiler backend (C code generation) with 100% interpreter parity
- LSP server with go-to-definition and find-references
- AST optimization pass and variable resolution for O(1) lookup
- apply() builtin for dynamic function calls
- Unbuffered channels and many-params support
- 99 parity tests (100% pass rate)
