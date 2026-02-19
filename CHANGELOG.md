# Changelog

All notable changes to Hemlock will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.9.0] - 2026-02-19

### Added

- **WASM interpreter release artifact** - The pre-built WASM interpreter (`hemlock.js` + `hemlock.wasm`) is now included as a release artifact in GitHub releases alongside the Linux and macOS binaries. Run Hemlock programs in the browser or Node.js without compiling from source.

## [1.8.8] - 2026-02-06

### Fixed

- **Compiler inlining: nested call argument corruption** - Fixed a bug where nested function calls as arguments (e.g., `foo(x, bar(ptr_arg, ...))`) corrupted parameters during inlining. Arguments are now fully evaluated before parameter binding, preventing name shadowing between outer and inner inlined calls.
- **Compiler inlining: unboxing collision with loop counters** - Fixed a bug where inlined function parameters with the same name as a prior unboxed loop counter (e.g., `for (let x: i32 = 0; ...)` followed by inlined `create_thing(x, y)`) were incorrectly wrapped with `hml_val_i32()`. Inlined params are now registered as shadows and marked `is_param=1` to prevent the unboxing optimization from treating them as native C types. This fixes hemloco compilation.
- **Compiler `ptr - integer` type checking** - The type checker now allows pointer subtraction (`ptr - int`) for pointer arithmetic, matching the existing support for `ptr + int`.
- **Catchable `open()` exceptions** - `open()` now throws catchable exceptions via `hml_throw()` instead of calling `exit(1)` on failure. Error messages match the interpreter format: `"Failed to open '%s' with mode '%s': %s"`.

### Added

- Parity tests for pointer subtraction, open() exception handling, and nested inline function calls.

## [1.8.7] - 2026-01-28

### Fixed

- **Multi-argument print/eprint in compiler** - Fixed compiler codegen for `print()` and `eprint()` with multiple arguments (e.g., `print("x:", x, y)`). Previously, only single-argument calls were handled as builtins; multi-argument calls incorrectly generated invalid `hml_fn_print` function calls. Added `hml_print_value`, `hml_eprint_value`, `hml_print_newline`, and `hml_eprint_newline` runtime functions to support proper multi-argument printing with space separators.

## [1.8.6] - 2026-01-28

### Fixed

- **SSO string append crash** - Fixed segmentation fault in `hml_string_append_inplace` when growing strings that use Small String Optimization (SSO). SSO strings store data inline in the struct, so calling `realloc()` on them was invalid. The fix allocates a new heap buffer with `malloc()` when transitioning from SSO to heap storage.

## [1.8.5] - 2026-01-27

### Added

- **Five new array methods** - Expanding array functionality to 23 methods total:
  - `every(predicate)` - Returns true if all elements satisfy the predicate
  - `some(predicate)` - Returns true if any element satisfies the predicate
  - `indexOf(value)` - Returns the first index of a value, or -1 if not found
  - `sort(comparator?)` - Sorts array in-place with optional custom comparator
  - `fill(value, start?, end?)` - Fills array elements with a value
- **Sorting algorithm benchmark** (`examples/sorting_benchmark.hml`) - Compares 8 different sorting algorithms

### Changed

- **Major runtime performance optimizations**:
  - Inline caching extended to all object property access sites
  - Small string optimization (SSO) for reduced memory fragmentation
  - Unified field storage for objects reduces allocation overhead
  - Improved tail call optimization in hemlockc compiler
  - Consolidated sync structures for better memory layout
- **Shared code modules** - Reduced interpreter/compiler duplication:
  - Shared UTF-8 handling module
  - Unified type promotion logic across backends

### Fixed

- **6 memory leaks** identified by clang static analyzer
- **macOS double-free bug** - Reverted VisitedSet hash table optimization that caused crashes on macOS
- Removed unused `@stdlib/os` import from path module
- Removed obsolete FFI -O0 workaround

## [1.8.3] - 2026-01-20

### Added

- **`@stdlib/termios` module** - Cross-platform raw terminal input for interactive applications
  - `enable_raw_mode()` / `disable_raw_mode()` - Toggle canonical mode for instant keypresses
  - `read_key()` - Blocking single keypress read returning `{char, code, name}` object
  - `read_key_timeout(ms)` - Non-blocking read with timeout for game loops
  - Arrow key detection (`KEY_UP`, `KEY_DOWN`, `KEY_LEFT`, `KEY_RIGHT`)
  - Function keys F1-F12, navigation keys (Home, End, PageUp, PageDown)
  - Control key detection (Ctrl+C, Ctrl+D, Ctrl+Z)
  - `is_terminal()` - Check if stdin is a TTY
  - `with_raw_mode(callback)` - RAII-style scope helper with automatic cleanup
  - Platform support: Linux (`libc.so.6`) and macOS (`libSystem.B.dylib`)
  - Documentation at `stdlib/docs/termios.md`

- **Hemloco game** (`examples/hemloco.hml`) - Train town builder inspired by a classic train game
  - Terminal-based game demonstrating termios usage
  - 11 track piece types with Unicode box-drawing characters
  - Train pathfinding through track connections
  - Automatic fallback to line mode when not running in a terminal

## [1.8.2] - 2026-01-18

### Added

- **Memory ownership documentation** (`docs/advanced/memory-ownership.md`) - Comprehensive
  guide covering programmer vs runtime responsibility, reference counting, ownership
  transfer points, async/concurrency memory isolation, FFI rules, and best practices
- **Leak regression test suite** (`make leak-regression`) - ASAN-based test suite
  covering all memory leak fixes with organized test categories

### Fixed

- **Exception-safe expression evaluation** - Arrays, objects, and function call arguments
  now properly release partial allocations when exceptions are thrown mid-evaluation
- **Task result memory ownership** - `join()` now correctly retains results for the caller,
  and `task_free()` properly releases result values (fixes leak in detached tasks)
- **Channel drain on close** - `channel_free()` now releases all buffered values before
  freeing the channel (prevents leaks when channels closed with values remaining)
- **Null coalescing optimizer leak** - Optimizer now properly frees discarded AST nodes
  when constant-folding `??` expressions (e.g., `"value" ?? "default"` → `"value"`)

## [1.8.1] - 2026-01-14

### Fixed

- **Use-after-free in function return handling** - Functions without explicit return
  statements were incorrectly using stale return values from nested function calls,
  causing segfaults when closures accessed outer variables (e.g., grove.hml from
  hemlang/playground)

## [1.8.0] - 2026-01-13

### Added

- **Pattern matching** (`match` expressions) - Powerful destructuring and control flow
  - Literal patterns for integers, floats, strings, booleans, and null
  - Wildcard pattern (`_`) for catch-all matching
  - Variable binding patterns to capture matched values
  - OR patterns (`1 | 2 | 3`) for matching multiple alternatives
  - Guard expressions (`n if n > 0`) for conditional matching
  - Object destructuring (`{ x, y }`) with nested support
  - Array destructuring with rest patterns (`[first, ...rest]`)
  - Type patterns (`n: i32`) for type-based matching
  - Full parity between interpreter and compiler
- **Arena memory allocator** (`@stdlib/arena`) - Bump allocation for efficient memory management
- **macOS ARM sanity test** - CI workflow for Apple Silicon compatibility
- **AddressSanitizer (ASAN) make targets** - `make asan` and `make test-asan` for memory leak detection

### Fixed

- Multiple memory leaks in interpreter, parser, optimizer, and FFI
- Use-after-free and double-free bugs in manual memory handling
- NULL pointer dereference risks from unchecked allocations
- Type checking for object indexing and dynamic arrays
- Generic type alias substitution at runtime
- Interpreter SIGABRT crash on certain error conditions
- Clang analyzer warnings and `-Wswitch` warnings for `EXPR_MATCH`
- Regex and concurrency stability issues
- FFI callback allocation cleanup
- JSON unicode escape parsing
- Compiler error with type annotations in `alloc_with_size`

### Changed

- Array element type mismatch now produces a warning instead of an error
- Renamed `glob.match()` to `glob_match()` to avoid keyword conflict
- Renamed regex object `match` field to `find_all` to avoid keyword conflict

## [1.7.5] - 2026-01-10

### Fixed

- **Formatter else-if indentation bug** - Long else-if chains were losing indentation after the first branch
- Synced HML_SANDBOX_RESTRICT_SIGNALS flag to runtime header to fix compiler warning

## [1.7.4] - 2026-01-10

### Added

- **Function parameter line breaking** - Long parameter lists automatically break across multiple lines
- **Binary expression line breaking** - Long logical/comparison chains break at operators
- **Import statement line breaking** - Long import lists break with each item on its own line
- **Method chain line breaking** - Long method chains can break before dots

### Fixed

- Trailing newlines at end of formatted files are now removed
- Comma placement after function bodies in multiline structures is now correct
- Expression length estimation is now more accurate for property access and other expression types

## [1.7.3] - 2026-01-10

### Fixed

- Formatter now preserves blank lines between statements
- Formatter now correctly associates comments with their adjacent code instead of moving them to the top
- Parser now sets line numbers on all statement types for accurate source mapping

## [1.7.2] - 2026-01-06

### Fixed

- Fixed compiler warning for unused function in formatter

## [1.7.1] - 2026-01-04

### Added

- **Single-line if statements** - braceless syntax for simple conditionals (e.g., `if (x > 0) print(x);`)
- **Single-line while loops** - braceless syntax for simple loops (e.g., `while (x > 0) x--;`)
- **Single-line for loops** - braceless syntax for C-style and for-in loops (e.g., `for (let i = 0; i < 10; i++) print(i);`)

## [1.7.0] - 2026-01-04

### Added

- **Type aliases** (`type Name = Type;`) - named shortcuts for complex types
- **Function type annotations** (`fn(i32): i32`) - first-class function types
- **Const parameters** (`fn(const x: array)`) - deep immutability for parameters
- **Method signatures in define** (`fn method(): Type;`) - interface-like contracts
- **Self type** in method signatures - refers to the defining type
- **Loop keyword** (`loop { }`) - cleaner infinite loops
- **Loop labels** (`outer: while`) - targeted break/continue for nested loops
- **Object shorthand** (`{ name }`) - ES6-style shorthand property syntax
- **Object spread** (`{ ...obj }`) - copy and merge object fields
- **Compound duck types** (`A & B & C`) - intersection types for structural typing
- **Named arguments** (`foo(name: "value", age: 30)`)
- **Null coalescing operators** (`??`, `??=`, `?.`) for safe null handling

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
