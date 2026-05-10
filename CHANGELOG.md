# Changelog

All notable changes to Hemlock will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [2.2.0] - 2026-05-10

### Added

- **`@stdlib/process.posix_spawn()`** — launch external programs through `posix_spawnp(3)` and return immediately with `{ pid }`. The new primitive accepts argv arrays plus optional `env`, `stdin`, `stdout`, `stderr`, `cwd`, and `setsid` settings for shell-free process creation, fd redirection, working-directory selection, and session detachment.
- **Raw file descriptor access in `@stdlib/fs`** — `open_fd(path, mode?)` opens a path and returns the underlying POSIX fd directly, while `fileno(file)` exposes the fd backing an open Hemlock File handle. These pair with `@stdlib/ipc` fd helpers and `posix_spawn()` redirection.
- **String-literal object keys** — object literals now accept quoted keys such as `{ "user-id": 42 }`, enabling fields with hyphens, spaces, leading digits, or other names that are not valid bare identifiers.
- **`obj?[key]` safe indexing** — bracket safe-indexing can now use the concise `?[` form in addition to the existing `?.[` optional-index spelling. Object string-key lookups return `null` on missing keys and null receivers still short-circuit.

### Fixed

- **Compiler parity for strict language cases** — typed arrays of custom object types now auto-fill optional fields per element, `export let X = X;` is treated as an export self-rebind instead of a duplicate definition, and invalid `substr()` arity is reported by the compiler consistently.
- **FFI library handle collisions** — compiled programs that import multiple FFI-backed libraries now keep one runtime handle per library instead of clobbering a shared `_ffi_lib` global.
- **Formatter round-tripping for quoted object keys** — the formatter now preserves quotes for object field names that are not valid bare identifiers.
- **Incremental C builds after header changes** — the top-level and runtime Makefiles now emit and include `.d` dependency files via `-MMD -MP`, so touching a header rebuilds dependent objects instead of linking stale artifacts.
- **WASM interpreter build compatibility** for native-only `posix_spawn()` support.

### Changed

- **WASM CI toolchain** — Emscripten was updated from 3.1.51 to 4.0.7 to pick up the current zlib port hash.

## [2.1.1] - 2026-04-21

### Fixed

- **Parser no longer hangs on malformed `match` expressions** — when a match arm's body started with `{ <keyword> ... }` (e.g. `1 => { return "x"; }`), the block was parsed as an object-literal expression, which failed because `return` can't be a field name. The pattern/arm parser then looped forever on the problematic token, and the interpreter never returned. Hembench's `L1-M-03 Token Classifier` task triggered this because the model idiomatically writes `_ => { return "x"; }` from other languages. Added forward-progress guards to both the `match` arm loop and `parse_program`: if an iteration finishes still pointing at the same token it started on, force-advance so the parser makes progress toward EOF.

## [2.1.0] - 2026-04-21

### Fixed

- **`@stdlib/http` POST body sent on the wire** — every request path (`post`, `post_json`, `post_json_timeout`, `post_json_stream`, `request`, and friends) accepted a body, serialized it, and then the libwebsockets runtime dropped it on the floor with a `(void)post_body;` comment. Servers received empty bodies and replied with 500s like `"attempting to parse an empty input"`. Both the one-shot (`websockets_http.c`) and streaming (`websockets_stream.c`) clients now:
  - store the body + content-type on the response/stream struct,
  - add `Content-Type` (unless the caller supplied one) and `Content-Length` headers in the handshake callback,
  - signal `lws_client_http_body_pending()` and request a writable callback,
  - write the body in `LWS_CALLBACK_CLIENT_HTTP_WRITEABLE` with `lws_write(..., LWS_WRITE_HTTP)`,
  - free the body on every success and failure path.
- **`@stdlib/json` public API is now actually exported** — `parse`, `stringify`, `pretty`, `get`, `set`, `has`, `delete`, `parse_file`, `stringify_file`, `pretty_file`, `is_valid`, `validate`, `is_object`, `is_array`, `is_string`, `is_number`, `is_bool`, `is_null`, `type_of`, `clone`, `merge` now use `export fn`. Named imports (`import { parse } from "@stdlib/json"`) continued to work through stdlib magic, but namespace imports (`import * as json from "@stdlib/json"; json.parse(...)`) threw `Object has no method 'parse'`. That's fixed.

## [2.0.3] - 2026-04-21

### Fixed

- **Compiler** - Modules that exported a function named `init` produced a C symbol collision with the auto-generated module initializer. The symbol was `_mod<N>_init` in both cases, causing "redeclared as different kind of symbol" errors at C-compile time. The generated initializer now lives in a reserved `_hml_init` compiler namespace (`_mod<N>__hml_init`) so user-exported symbols can't collide. Noticed while releasing hpm 1.2.0.

## [2.0.2] - 2026-04-21

### Fixed

- **`@stdlib/collections` HashMap** - `HashMap.keys()` and related operations crashed with `Integer overflow: i32 addition` for many string keys. The djb2 hash loop accumulated in a checked i32 and threw mid-loop once the running value passed `INT32_MAX`. The accumulator is now an i64 masked to 31 bits each iteration; intermediate overflow is impossible and bucket indices remain positive. Hashes are stable within a run but not identical to 2.0.1.

## [2.0.1] - 2026-04-18

### Added

- **`@stdlib/decimal` module** - Number formatting (`to_fixed`, `to_hex`), parsing (`parse_int`, `parse_float`), and `StringBuilder` utility
- **`@stdlib/mmap` module** - Memory-mapped file I/O (`mmap`, `munmap`, `msync`) with compiler and runtime support
- **`@stdlib/matrix` module** - Dense matrix operations (add, multiply, transpose, determinant, inverse, LU decomposition)
- **`@stdlib/unix_socket` module** - Unix domain sockets (AF_UNIX stream/datagram) with advanced tests
- **Pipe IPC support** - Low-level fd-based pipe operations in `@stdlib/ipc`
- **`array.findIndex()` method** - Returns the index of the first element matching a predicate
- **`array.lastIndexOf()` method** - Returns the last index of a value
- **`array.flat()` method** - Flattens nested arrays by one level
- **`string.trim_start()` method** - Trims whitespace from start of string
- **`string.trim_end()` method** - Trims whitespace from end of string
- **`sha1`, `crc32`, `adler32` hash functions** in `@stdlib/hash`
- **Custom HTTP request headers** in `@stdlib/http`
- **Object key coercion** - Integer, bool, float, and rune keys auto-coerce to strings via bracket notation
- Expanded `@stdlib/iter` with `map`, `filter`, `reduce` and other core functions
- Expanded `@stdlib/math` with `sign`, `cbrt`, `hypot`, `gcd`, `lcm`, and hyperbolic functions
- Expanded `@stdlib/datetime` with `is_leap_year`, `days_in_month`, and validation
- Expanded `@stdlib/testing` with deep object comparison
- Expanded stdlib test coverage for 8 under-tested modules

### Fixed

- **OR-pattern bindings in interpreter** - Variable bindings from matched OR pattern arms are now properly propagated to the match body (previously silently discarded)
- **Typed pattern matching on custom objects** - `match val { Point p => ... }` now checks the object's actual type name instead of matching any object
- **AST serialization for complex patterns** - `PATTERN_OR`, `PATTERN_OBJECT`, and `PATTERN_ARRAY` are now properly serialized/deserialized in `.hmlc` cache files (previously written as empty stubs)
- **`divi()` truncation** - Now truncates toward zero consistently across all code paths
- **Closure scoping** - Fixed correctness in both interpreter and compiler
- **Type promotion** - Fixed compiler promotion logic for mixed arithmetic
- **Float-to-int conversion edge cases** - Proper handling of NaN, Inf, and out-of-range values
- **Shift operation semantics** - Well-defined behavior for signed and oversized shifts
- **Integer boundary overflow detection** - Catchable errors for hex/bin/oct literals near max values
- **`@stdlib/testing` callback dispatch** - Fixed method call self-injection
- **`print()` stdout flushing** - Interpreter now flushes stdout after print output
- **Data race in ref_count** - Fixed flaky `async_env_stress` test on macOS ARM
- **Object hash table lazy init race** - Fixed concurrent access during initialization
- **Compiler memory leaks** - Release local variables at function exit, fix refcount in variable reassignment, block-scope cleanup
- **Compiler closure environment lifetime** - Proper refcounting for shared closure environments
- **Clang warnings** - Fixed const-correctness, missing prototypes, strict prototypes
- Documentation inaccuracies and stdlib compilation errors for `hemlockc`

## [2.0.0] - 2026-04-05

### Breaking Changes

- **Reduced global builtins** - 63 builtins moved from global namespace to stdlib modules. Code using bare `sin()`, `getenv()`, `signal()`, `open()`, `exec()`, `SIGINT`, `AF_INET`, etc. must now import from the appropriate `@stdlib` module.

### Added

- **`@stdlib/signal` module** - Signal handling functions (`signal`, `raise`) and all POSIX signal constants (`SIGINT`, `SIGTERM`, `SIGUSR1`, etc.)
- **`@stdlib/atomic` module** - All 19 atomic operations (load, store, add, sub, and, or, xor, cas, exchange for i32/i64) plus `atomic_fence`
- **`@stdlib/debug` module** - Task inspection (`task_debug_info`) and stack management (`set_stack_limit`, `get_stack_limit`)
- **`@stdlib/ffi` module** - FFI callback management (`callback`, `callback_free`, `ffi_sizeof`)
- **Expanded `@stdlib/math`** - Added `div`, `divi`, `floori`, `ceili`, `roundi`, `trunci` exports
- **Expanded `@stdlib/net`** - Socket constants (`AF_INET`, `SOCK_STREAM`, etc.), poll constants (`POLLIN`, `POLLOUT`, etc.), and networking functions (`socket_create`, `dns_resolve`, `poll`)
- **Expanded `@stdlib/fs`** - Added `open` export
- **Expanded `@stdlib/strings`** - Added `string_concat_many` export
- **Expanded `@stdlib/async`** - Added `get_default_stack_size`, `set_default_stack_size` exports
- **C macro conflict prevention** - Compiler sanitizes imported names that conflict with C system macros (`SIG*`, `AF_*`, `SOCK_*`, etc.)
- **`array.reserve(n)` method** - Pre-allocate array capacity to avoid repeated reallocations during bulk inserts
- **`str.byte_ptr()` method** - Returns a raw `ptr` to the string's internal byte buffer for zero-allocation access with `memcpy` and pointer operations
- **`buffer.slice(start, end)` method** - Zero-copy buffer views that reference the parent buffer's memory instead of allocating and copying. Views hold a reference to the root buffer to prevent use-after-free, and chained slices correctly track the root owner
- **`ptr_read/write/deref` accept buffers directly** - All pointer builtins (`ptr_read_*`, `ptr_write_*`, `ptr_deref_*`) now accept both `ptr` and `buffer` types, extracting `buffer->data` automatically
- **`spawn_with()` builtin** - Per-thread configuration with `stack_size` and `name` options: `spawn_with({ stack_size: 4194304, name: "worker" }, fn, args...)`
- **WebSocket binary data support** - `__lws_msg_binary` builtin for extracting binary message data as a buffer; server-side `send_binary` for binary frame transmission

### Changed

- `signal()` and `raise()` now require `import { signal, raise } from "@stdlib/signal"`
- `open()` now requires `import { open } from "@stdlib/fs"`
- `exec()` and `exec_argv()` now require `import { exec } from "@stdlib/process"`
- Math functions (`sin`, `cos`, `sqrt`, `floor`, etc.) now require `import from "@stdlib/math"`
- Environment functions (`getenv`, `setenv`) now require `import from "@stdlib/env"`
- Signal constants (`SIGINT`, `SIGTERM`, etc.) now require `import from "@stdlib/signal"`
- Socket/poll constants now require `import from "@stdlib/net"`
- Atomic operations now require `import from "@stdlib/atomic"`
- `callback`/`callback_free` now require `import from "@stdlib/ffi"`
- `task_debug_info`, `set_stack_limit`, `get_stack_limit` now require `import from "@stdlib/debug"`
- Stdlib module count increased from 43 to 46
- **Major codebase refactoring** - Split 5 large source files (3600-3800 lines) into focused modules: `type_check.c` → 9 files, `codegen_call.c` → 7 files, `websockets.c` → 4 files, `formatter.c` → 6 files, `expressions.c`/`codegen_expr.c` → smaller files

### Fixed

- **Compiler expression-level unboxing** - Native C types now propagate through expression trees instead of boxing/unboxing at every operation. `hml_i32_add(hml_val_i32(i), hml_val_i32(1))` becomes `hml_val_i32((i + 1))`, eliminating intermediate allocations
- **Multi-level function inlining** - Inlining depth increased from 1 to 3, allowing nested helpers (e.g., `rotr()` inside `ep0()` inside `sha256_transform()`) to be fully inlined. Benchmark improvements: primes_sieve -40%, binary_tree -27%, json_serialize -37%
- **While-loop accumulator unboxing** - Top-level while loops now detect accumulator/counter variables and shadow them with native C locals, eliminating boxing overhead
- **Fire-and-forget spawn use-after-free** - Worker thread now holds a reference to the task, preventing premature cleanup when the task handle is discarded without `join()`
- **WebSocket server SO_REUSEADDR** - Added `LWS_SERVER_OPTION_ALLOW_LISTEN_SHARE` for rapid port rebind
- **WebSocket server close race condition** - Fixed segfault on Linux when closing WebSocket servers
- **Thread stack overflow on sequential spawns** - Fixed stack overflow when spawning sequential WebSocket servers; applied thread stack size to WebSocket service threads
- **For-in loop variable scoping** - Interpreter now uses `env_define()` instead of `env_set()` for loop variables, preventing modification of outer variables with the same name. Compiler now pushes a lexical scope around for-in loops
- **Compiled recursive stack overflow** - Tail-call optimized functions now detect infinite recursion via `HML_TAIL_CALL_CHECK()` macro instead of segfaulting
- **8 compiler/runtime fixes** - Float division returns IEEE 754 Inf/NaN, object/array reference equality, `read(0)` returns empty string, closed file operations throw catchable exceptions, typed array numeric coercion, try/catch rethrow propagation, `find()`/`contains()` method dispatch for non-string/array objects, nullable type annotations skip conversion for null values

## [1.9.2] - 2026-04-03

### Fixed

- **Compiler unboxed loop counter boxing** - Fixed a critical codegen bug where optimized loop counters (native `int32_t`) were not properly re-boxed to `HmlValue` when referenced in expressions. The `codegen_is_main_var` check incorrectly prevented boxing when a main-level variable name shadowed an unboxed loop counter inside a module/closure function. Added scope-added variable tracking for optimized loop counters and local-variable shadowing detection in the unbox check. Fixes compilation of `@stdlib/collections` (HashMap, Queue, Stack, Set, LinkedList) and `@stdlib/encoding` (base64, hex).
- **`clear()` object method dispatch** - The compiler now performs runtime type checking before dispatching `.clear()` calls. Previously, `.clear()` always generated `hml_array_clear()` regardless of the receiver type, causing "clear() requires array" errors on HashMap/Set/Stack objects. Now falls back to `hml_call_method()` for non-array types.
- **`exec()` import shadowing** - The compiler's builtin `exec()` handler now checks for import bindings, module exports, and local function definitions before dispatching to the system exec builtin (`hml_exec`). This fixes `@stdlib/sqlite` which exports its own `exec()` function for SQL execution.
- **Removed stale debug `fprintf` statements** - Cleaned up debug output from `type_check_get_unboxable`, `type_check_mark_unboxable`, `type_check_clear_unboxable`, and `funcgen_generate_body`.

## [1.9.1] - 2026-04-02

### Added

- **`write()` builtin** - Prints a value to stdout without a trailing newline. Calls `fflush(stdout)` for immediate output. Enables inline output building (e.g., `write("1"); write(" -> "); write("2");` prints `1 -> 2` on one line). Full parity between interpreter and compiler.
- **Single-argument `slice()`** - `arr.slice(n)` and `str.slice(n)` now default the end parameter to the length, matching JavaScript/Python behavior. The two-argument form is unchanged.
- Parity tests for `write()`, single-arg `slice()`, and rune `join()`.

### Fixed

- **`join()` on rune arrays** - `"hello".chars().join("")` now correctly produces `"hello"` instead of `"[object][object]..."`. Added `VAL_RUNE` case to the interpreter's array join with proper UTF-8 encoding. The compiler runtime already handled this correctly.
- **HashMap numeric key coercion** - Keys of different numeric types now compare correctly (e.g., an `i32` key can be found with an `i64` lookup). Previously, the `typeof()` guard in `keys_equal()` rejected valid cross-type matches before `==` could apply numeric coercion.
- **HemBench task accuracy** - Fixed L1-M-02 expected output (78.53 → 78.54, proper rounding not truncation), clarified L2-E-01 variance precision in prompt, stopped leaking expected output to L5/L6 benchmark tasks.

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
