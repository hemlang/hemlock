# Changelog

All notable changes to Hemlock will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.8.1] - 2026-06-10

### Added

- **Windows: native crypto hashing via CNG.** The `sha1`/`sha256`/`sha512`/`md5` builtins (and `@stdlib/hash` on top of them) now work on Windows in both backends, backed by Windows CNG (`bcrypt.dll` — a system DLL, so binaries stay self-contained) instead of throwing "not available (no OpenSSL)". The implementation is `hml_win32_hash()` in `src/shared/platform_win32.c` (declared in `hemlock_compat.h`), used by the interpreter's hash builtins and the compiled runtime's `hml_hash_*`; `-lbcrypt` is linked on all Windows builds, including hemlockc's generated link commands. Only the ECDSA builtins remain unavailable on Windows (`HEMLOCK_NO_OPENSSL` — CNG produces raw `r||s` signatures where the OpenSSL builtins produce DER, so there is no compatible mapping). Windows CI now asserts known `sha256`/`md5` digests in the interpreter and compiled smoke tests, native and under Wine.
- **`__term_*` terminal-control builtins; `@stdlib/termios` and `@stdlib/terminal` work on Windows.** Four new builtins in both backends — `__term_is_tty()`, `__term_raw(enable)`, `__term_read_byte(timeout_ms)`, `__term_size()` — backed by `src/shared/term_core.c` (POSIX termios/poll/`TIOCGWINSZ`; Windows console API with virtual-terminal input/output enabled, so arrow keys arrive as the same ESC sequences POSIX terminals send and ANSI output works). `@stdlib/termios` gains a Windows code path (raw mode, `read_key`, escape-sequence parsing unchanged) while its POSIX FFI path is untouched. `@stdlib/terminal` now gets the terminal size from `__term_size()` instead of shelling out to `stty`, and progress bars/spinners/`clear()`/`print_at()` use the `write()` builtin instead of spawning `printf` through a shell per frame — faster everywhere and the only way they could work on Windows. Parity test: `tests/parity/builtins/term_control.hml`.
- **Compiler: FFI imports nested in platform gates now codegen correctly.** `import "lib"` inside `if`/`else` (how `@stdlib/termios` picks its per-platform libc) emitted `_ffi_lib_<name> = hml_ffi_load(...)` without ever declaring the global, so any program importing such a module failed to compile. The library-handle declaration pass now collects import paths recursively. Also, in builds without FFI support (`HEMLOCK_NO_FFI`, WASM), `import "lib"` statements and `extern fn` declarations now warn instead of throwing, so modules with platform-gated FFI still load; calling the extern still fails. (`ffi_open()` keeps its hard error.)
- **Windows: `exec()` / `exec_argv()` via CreateProcess.** Command execution now works on Windows in both backends instead of throwing "not supported". Shell mode (`exec(cmd)`) runs through `%COMSPEC% /S /C` the way POSIX popen runs through `/bin/sh` (stdout captured, stderr inherited); argv modes (`exec(cmd, args)`, `exec_argv(argv, opts?)`) run the program directly with stdout and stderr captured and optional `stdin:` feeding, with arguments quoted per the `CommandLineToArgvW` rules so they round-trip intact. Launch failures mirror POSIX execvp: a result object with `exit_code` 127 and the message on `stderr`, so scripts can check exit codes portably. The engine is `hml_win32_run_capture()` in the platform layer (CreateProcess + pipes; stdin writer and stderr reader run on winpthreads threads so large outputs can't deadlock). This unlocks `@stdlib/shell`'s `run()`/`run_capture()` on Windows (with cmd.exe command syntax); the module's Unix-command conveniences (`ls()`, `which()`, …) remain Unix-only. Windows CI exercises shell mode, argv quoting, exit codes, missing-program 127, and stdin feeding, native and under Wine.
- **`__random_bytes(n)` CSPRNG builtin; `@stdlib/uuid` works on Windows.** New always-available builtin returning a buffer of cryptographically secure random bytes — `BCryptGenRandom` on Windows, `/dev/urandom` everywhere else (Emscripten emulates it) — implemented in both backends. `@stdlib/crypto`'s `random_bytes()` now uses it instead of OpenSSL's `RAND_bytes` (same API, no longer needs libcrypto for this function), and `@stdlib/uuid` calls the builtin directly instead of importing `@stdlib/crypto` — previously UUID generation was broken on Windows because crypto's module-level `import "libcrypto.so.3"` cannot load there. `@stdlib/path`'s `expand_user()` now resolves `~` via the `__homedir()` builtin (`USERPROFILE` on Windows) instead of `$HOME`/`/home/<user>` guessing. Parity test: `tests/parity/modules/random_bytes_uuid.hml`.

## [2.8.0] - 2026-06-10

### Added

- **Windows (MinGW-w64) support.** Both `hemlock` and `hemlockc` (plus `libhemlock_runtime.a`) now build for Windows, cross-compiled from Linux/macOS (`make mingw`, requires `gcc-mingw-w64-x86-64` + `libz-mingw-w64-dev`) or natively under MSYS2 (auto-detected). The binaries are self-contained (only system DLLs). What works on Windows: the full language core, async/`spawn`/channels/atomics (via winpthreads), TCP/UDP/AF_UNIX sockets and DNS (via winsock2/afunix), file/directory I/O (binary mode, no CRLF translation), `mmap` (emulated over `CreateFileMapping`/`VirtualAlloc`), zlib compression, and the stdlib built on those. FFI is fully supported when the MinGW toolchain has libffi (auto-detected; MSYS2 package or a one-time cross build from source — CI does both): `import "foo.dll"` loads via `LoadLibrary`, `extern fn` dispatches through statically-linked libffi, and Linux library names translate automatically (`import "libraylib.so"` finds `libraylib.dll`/`raylib.dll`), so raylib-style bindings build into Windows game executables unmodified. POSIX-only features throw a clear runtime error: `fork`/`exec`/`posix_spawn`/`kill`/uid family, POSIX regex, OpenSSL-backed hashes/ECDSA (`HEMLOCK_NO_OPENSSL`), and FFI only when libffi is absent (`HEMLOCK_NO_FFI`). New platform layer: `include/hemlock_platform.h` + `include/hemlock_compat.h` + `src/shared/platform_win32.c` (dlopen→`LoadLibrary`, `poll`→`WSAPoll`, `realpath`/`strndup`/`getline`/`mkstemps`/`setenv` shims). Module resolution understands `C:/`-style absolute paths. CI: `.github/workflows/windows-mingw.yml` cross-builds and smoke-tests both backends under Wine, and a `windows-native` job builds natively on a real Windows runner (MSYS2/UCRT64) and exercises the full `hemlockc` → gcc subprocess pipeline (whose `system()` invocation goes through cmd.exe — `shell_quote` now emits cmd-compatible double quoting on Windows instead of POSIX single quotes). Docs: `docs/advanced/windows.md`. POSIX builds are unchanged (all platform differences live behind `#ifdef _WIN32` / feature gates).

## [2.7.0] - 2026-06-10

Language-ergonomics and parity batch driven by feedback from production
consumers (gn.hml, Witchgrid).

### Added

- **Object-literal method shorthand.** `{ fn name(...) { ... } }` is now accepted inside object literals as sugar for `name: fn(...) { ... }`. `async fn` and expression-bodied (`fn get(): i32 => ...`) forms work; entries stay comma-separated; `self` binds implicitly on method calls as before. Previously `fn` in an object literal was a parse error, forcing every callback-style API into the `name: fn() {}` spelling. Parity test: `tests/parity/language/object_method_syntax.hml`.
- **Side-effect imports.** `import "./suite.hml";` and `import "@stdlib/module";` load, cache, and execute a source module without binding any exports — the test-runner pattern from `docs/proposals/source-side-effect-imports.md` (now implemented as its Option B). Bare imports of anything else (e.g. `import "libc.so.6";`) keep their FFI shared-library semantics; the explicit `.hml` extension (or `@` prefix) is what selects source-module loading. The old `import {} from "path"` workaround still works and the formatter normalizes it to the bare form. Parity test: `tests/parity/modules/import_side_effect.hml`.
- **`string.rfind(needle)`** — codepoint index of the last occurrence, `-1` if absent, empty needle found at `string.length` (Python/JS behavior). Basename extraction is now `path.substr(path.rfind("/") + 1)` instead of `split("/")` + take-last. Parity test: `tests/parity/methods/string_rfind.hml`.
- **One-argument `substr(start)`** — everything from `start` to the end of the string, in both backends and the compile-time type checker.
- **`@stdlib/strings` `from_bytes(src)`** — build a string from a `buffer` or byte array. This is the documented replacement for the internal `__string_from_bytes` dunder that real projects had been reaching for. Parity test: `tests/parity/modules/strings_from_bytes.hml`.
- **`@stdlib/bytes` float bit casts** — `f32_to_bits`/`f32_from_bits`/`f64_to_bits`/`f64_from_bits` (IEEE 754 reinterpretation). Binary-format parsers (GGUF metadata et al.) previously had no clean way to turn a `u32` read from a file into the `f32` it spells. Parity test: `tests/parity/modules/bytes_float_bits.hml`.

### Fixed

- **Compiled `substr()`/`slice()` used byte offsets where the interpreter uses codepoint offsets.** Any multibyte string diverged between backends and the compiled result could be invalid UTF-8 (`"héllo wörld".substr(2, 3)` returned `�ll` compiled vs `llo` interpreted). The compiled runtime now does the same codepoint-position arithmetic (with the same cached `char_length`) as the interpreter. Parity test: `tests/parity/methods/string_substr_slice_utf8.hml`.
- **`string.find()` returned a byte index despite being documented as returning a codepoint index.** It now returns the codepoint index, so `s.substr(s.find(x))` composes correctly on multibyte strings (ASCII behavior is unchanged; `rfind` follows the same rule). Code that used `find()` results with byte-oriented APIs on multibyte strings should switch to `byte_at`/`to_bytes`.
- **Parser infinite loop (then OOM kill) on certain syntax errors inside blocks.** A statement that errored without consuming the offending token — e.g. `let f = fn(self) { ... };`, since `self` is a keyword and not a valid parameter name — made `block_statement()` spin forever while appending error statements until the kernel killed the process with no diagnostic. The block loop now has the same forward-progress guard as `parse_program()`. Regression test: `tests/functions/fn_param_self_invalid.hml`.
- **Formatter crash formatting star imports.** `import * from "module";` hit a NULL `namespace_name` append; it now formats correctly.
- **Segfault passing a `buffer` to an FFI `ptr` parameter (interpreter).** `hemlock_to_c_value_fast()` marshaled every `TYPE_PTR` argument via `val.as.as_ptr`, so a `buffer` argument handed C the internal `Buffer` struct pointer instead of its data — `extern fn memset(p: ptr, ...)` called with a buffer wrote over interpreter memory and crashed. Buffers now marshal as `buf->data` (matching `ptr_read`/`ptr_write`, FFI struct-field marshaling, and the compiled runtime, which already did this). Strings passed where `ptr` is expected marshal their data pointer, and `null` marshals as `NULL` for both `ptr` and `string` parameters. Regression test: `tests/ffi/buffer_arg_test.hml`.
- **Use-after-free passing sockets/websockets to `spawn()` (interpreter).** `value_deep_copy()` shared `VAL_SOCKET`/`VAL_WEBSOCKET` handles by reference *without* retaining them, while `task_free()` releases every task argument — each spawn dropped one reference, freeing the socket while the caller still held it (segfault after a few spawns). Deep copy now retains shared OS-resource handles, matching the channel/task/function cases and the compiled runtime's `hml_value_deep_copy`. Regression test: `tests/async/spawn_socket_arg.hml`.
- **Memory leak deep-copying objects into spawned tasks (interpreter).** The object branch of `value_deep_copy()` retained each freshly deep-copied field value on top of the owned reference the copy already returned, and nothing released the extra one — every object field passed to `spawn()` leaked. The store now transfers ownership (the array branch was already balanced). Verified with valgrind: object-arg spawn loops now free all blocks.
- **`VAL_WEBSOCKET` missing from value refcounting (interpreter).** WebSocket handles have an atomic `ref_count` with `websocket_retain`/`websocket_release`, but `value_needs_refcount()` and the `value_retain`/`value_release` switches omitted the type — storing a websocket in an array/object/variable skipped retains while container teardown released, the same class of over-release bug fixed for `VAL_SOCKET` in 2.6.0. Also added the missing `typeof()` names for `socket` (parity with the compiled backend), `websocket`, `ffi function`, and `ref`, which previously reported `"unknown"`.
- **Data race on the global type registries (interpreter).** `define`/`enum`/`type` statements register types at eval time into global registries with open-addressed hash tables that are freed and rebuilt on growth, while spawned task threads do concurrent lookups (parameter type annotations, `convert_to_type`) — a rebuild racing a lookup is a use-after-free. All three registries are now guarded by a `pthread_rwlock` (write on register, read on lookup; registered type structs are stable until exit so returned pointers remain valid). Regression test: `tests/async/spawn_type_registry.hml`.
- **Joinable pthread leak for dropped task handles (interpreter).** When the caller dropped a task handle racing the worker's auto-detach check, neither side detached the thread and its resources were never reclaimed. `task_free()` now `pthread_detach()`es a thread that was neither joined nor detached — the same safety net the compiled runtime gained in 2.5.7 (#583).
- **HTTPS context leak in `http_request`/`*_timeout` builtins (interpreter).** `__lws_http_request`, `__lws_http_get_timeout`, `__lws_http_post_timeout`, and `__lws_http_request_timeout` each created a fresh `lws_context` and only destroyed it for plain HTTP (`if (!ssl)`) — every HTTPS call leaked an entire context. They also skipped `LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT`, unlike the GET/POST builtins. All five entry points now share `get_http_context()` (persistent SSL context, disposable plain-HTTP contexts). Also: a response that completed on the final poll iteration was misreported as a timeout (`timeout <= 0` checked instead of `!resp->complete`), and two error paths in `http_request`/`get_timeout` leaked the parsed custom headers.
- **HTTP client hardening (interpreter).** Custom request headers containing embedded CR/LF (header injection / request splitting) are rejected; URL ports are validated to 1–65535 (previously `strtol` accepted any value including garbage, negative, or overflowing ports); the response-body accumulator guards its capacity arithmetic against `size_t` overflow.
- **`dns_resolve()` thread-safety and crash (interpreter).** Replaced deprecated `gethostbyname()` + `inet_ntoa()` (both use static storage, racy across task threads) with `getaddrinfo()` + `inet_ntop()`; also fixes a potential crash dereferencing `h_addr_list[0]` without checking the list is non-empty.
- **Socket I/O EINTR handling (interpreter).** `send`/`recv`/`sendto`/`recvfrom`/`accept` now retry on `EINTR` instead of surfacing a spurious "Interrupted system call" exception when a signal arrives mid-call (e.g. with `@stdlib/signal` handlers installed).

- **`to_string` / `string_byte_length` as first-class values failed to link.** Their codegen value-wrap path (`__to_string` / `__string_byte_length`) referenced `hml_builtin_to_string` / `hml_builtin_string_byte_length`, which were never defined — any compiled program using either builtin as a first-class value (not a direct call) failed at link time with undefined symbols. Added the missing env-first wrapper shims (matching the sibling `hml_builtin_cstr_to_string` / `hml_builtin_string_from_bytes`). Regression test: `tests/compiler/builtin_value_to_string.hml`.
- **hemlockc codegen correctness batch from a parity audit** (PR #592): width/signedness-correct integer comparisons and exact integer equality in `hml_binary_op` (u64 comparisons no longer go through int64, mixed-sign comparisons follow type promotion, boxed i32/i64 arithmetic throws on overflow); equality semantics mirror the interpreter (array == array errors, differing non-numeric types compare false); `++`/`--` preserve the operand's type and width in both backends (u16 65535++ wraps to 0, i64 no longer truncates through int32); `defer` evaluates its expression at function exit with current variable values, is scoped to the owning function via per-function defer frames, and runs during exception unwind; `finally` runs when the expression of a `return` inside `try` throws; captured locals are live-shared with closures (writes are visible both ways); escape analysis covers defer/try/for-in/switch/throw; non-finite float constants emit valid C (`INFINITY`/`NAN`); calls to undefined identifiers raise the interpreter's catchable error instead of a GCC failure; `char_at`/`byte_at`/string indexing throw on out-of-bounds; for-in re-checks the live iterable length; interpreter `print`/`write`/`eprint` accept multiple arguments; interpreter `array_set` no longer writes `elements[-1]` on negative-index errors; top-level defers run at normal program exit. 11 parity regression tests.

## [2.6.0] - 2026-06-03

Static-by-default linking on Linux, so compiled binaries are self-contained and portable across distros.

### Changed

- **`hemlockc` now links statically by default on Linux.** The volatile native libraries (libffi, libwebsockets, libssl, libcrypto, libz) are linked statically (`-Wl,-Bstatic … -Wl,-Bdynamic` for the stable ones — glibc, libcap, libuv, libev); the result is a standalone binary that doesn't depend on the host's versions. macOS continues to link dynamically by default (its system libraries are versioned consistently). Opt out with the new **`--dynamic`** flag.

### Fixed

- **HTTP/WebSocket SIGSEGV when a Hemlock binary meets a different libwebsockets at runtime.** libwebsockets does *not* keep `lws_context_creation_info` ABI-stable across releases — between 4.0 (Ubuntu 22.04) and 4.3 (Ubuntu 24.04) it **reordered** fields (`port` 0→96, `options` 104→440, `protocols` 16→8, …). A dynamically-linked binary built against one version mis-reads the struct against another and crashes in `lws_create_vhost`/`lws_snprintf` on the *first* HTTP/WS call. Confirmed with `offsetof()` across both libraries and reproduced via Witchgrid (whose agent died on its first `/register` POST on a 24.04 runner). Static linking removes the runtime dependency on the host's libwebsockets entirely. (Over-zeroing the struct, attempted in an unreleased build, can't fix *reordered* offsets.)
- **Use-after-free for sockets stored in arrays/objects (interpreter).** `VAL_SOCKET` was missing from `VALUE_NEEDS_REFCOUNT`, so the `VALUE_RETAIN`/`VALUE_RELEASE` macros were no-ops for sockets — storing a socket in a collection (array push, object literal, variable read) skipped the retain, but `array_free_internal`/`object_free_internal` release fields via the `value_release()` function directly. The net over-release freed sockets that were still in use, e.g. `poll()`'s input/result arrays, causing a flaky `SIGABRT` (`tcache unaligned chunk`) — the actual cause of the macOS CI failure on `async_io/poll_basic`. Fixed by adding `VAL_SOCKET` to the macro so it matches the `value_retain`/`value_release` switch. Regression test: `tests/stdlib_net/socket_refcount.hml`.
- **Compiled builtins used as first-class values shifted their arguments.** stdlib re-exports like `export let poll = __poll;` route calls through the function-value dispatch path, which invokes `f(env, args...)`. Several raw builtins (`poll`, `open`, `raise`, `signal`, `string_concat_many`, `task_debug_info`) take no `env` parameter, so every argument shifted by one — `env` arrived where the first real arg belonged. `poll([], 0)` then failed with "poll() expects array as first argument". On Linux the uncaught exception exited 0 (silently masking it); on **macOS it aborted (SIGABRT)**, which is how CI surfaced it on the compiled `poll_basic` test. Fixed with env-first wrapper shims (`hml_builtin_poll`, etc.) that the value-dispatch path calls correctly. Builtins invoked by their direct intrinsic name (the common case — e.g. `open(...)` from `@stdlib/fs`) were never affected. Regression test: `tests/async_io/builtin_first_class_value.hml`.

### Notes

- Compile-time FFI (`extern fn`, e.g. `@stdlib/sqlite`) still works under static linking; the linked-by-FFI libraries (libsqlite3, …) stay dynamic, which is fine since they keep stable ABIs. Only **runtime FFI** (`ffi_open`/`ffi_bind`) is unavailable in static builds — use `--dynamic` for those.
- A release built on one distro still static-links *that build host's* libwebsockets, so building on a host whose libwebsockets matches the release is the current guidance; bundling the release's own static libs (for full build-host independence) is a tracked follow-up.

## [2.5.7] - 2026-06-02

Two resource-leak fixes for long-running concurrent and server programs.

### Fixed

- **`runtime/src/value.c` — `task_free()` leaked joinable pthreads for dropped async tasks (#583).** A task handle dropped without an explicit `join()`/`detach()` left its pthread joinable, so the thread's resources were never reclaimed even after the task completed — a program that `spawn`s tasks and drops their handles slowly bled thread state. `task_free()` now `pthread_detach()`es the thread as a final safety net when it was neither joined nor detached. Applies to the compiled runtime. Surfaced by a ThreadSanitizer leak in the concurrency stress suite.
- **`SocketHandle` refcount — accepted sockets leaked (#584).** `SocketHandle` had no ownership tracking, so the per-connection sockets returned by `accept()` were never freed. Added an atomic `ref_count` field plus `socket_retain()` / `socket_release()` (`include/runtime/types.h`, `include/runtime/memory.h`, `src/backends/interpreter/builtins/net.c`, `src/backends/interpreter/values.c`) and routed the accept path and socket value lifetimes through them. Closes the ~50 B/connection accepted-socket gap that the 2.4.10 / 2.4.11 notes tracked as "needs a socket-lifetime refcount change." Applies to the interpreter backend.

## [2.5.6] - 2026-05-18

Property-assignment RHS leak — the property-assign twin of the 2.4.11 indexed-assignment leak. `obj.field = <expr>` leaked `<expr>` on every assignment.

### Fixed

- **`src/backends/compiler/codegen_expr.c` — `EXPR_SET_PROPERTY`.** `codegen_expr` returns the RHS owned (+1); `hml_object_set_field()` takes its own reference and the assignment-expression result took an independent `hml_retain`, but the RHS creation reference was never released — so every `obj.field = value` orphaned `value`. 2.4.11 fixed exactly this for the indexed-assignment path (`obj[k] = v`) but the property-assignment path was missed. Added the matching `hml_release_if_needed(&value)`.
- Real-world impact: Witchgrid's `samples_for_dashboard()` does `bundle.ram = fetch_samples(...)` (and similar) per node per dashboard request, leaking the returned array every time — the dominant residual after the per-connection `routes` deep-copy fix. LSan on the real control-plane binary: the `fetch_samples`-rooted leak (~750 KB / run, ~96% of the post-routes-fix residual) is eliminated by this; rebuilding any Hemlock service on 2.5.6 fixes it with no application change.

### Validated

- New leakhunt + CI stress guard `obj_assign_call_rhs_leak.hml`: property-assign and index-assign of an owned call result into object fields — **leaked 240,000 B / 1500 iters pre-fix → 0 post-fix**. Linux full parity **259/259** (0 interpreter-only, 0 compiler-only); macOS parity at known baseline; stress suite green (lsan/none/asan). Mirrors the 2.4.11 fix + its `objset_leak` guard.

## [2.5.5] - 2026-05-18

Interpreter concurrency fix — corrupted reads of a shared object from spawned tasks. A program that `spawn`s tasks which concurrently read a shared object tree (the Witchgrid dashboard's `render_capabilities()` shape — a stress test models it) intermittently threw `Only strings, buffers, arrays, and objects have properties` deep in a worker.

### Fixed

- **`src/backends/interpreter/values.c` — `object_lookup_field()` / `object_lookup_field_with_hash()` lazily rebuilt the hash table on the READ path.** Object literals / JSON reach readers with `hash_table == NULL`; N spawned task threads first reading the same shared object all entered `object_hash_rebuild()` and raced on `free(obj->hash_table)+malloc()` — heap corruption surfacing as a bogus "key not found", so `obj[key]` returned `null` and the next `.field` access threw. This is exactly the 2.4.5 bug — which was fixed **only in the compiled runtime**; the interpreter's copy was never given the same treatment. Fix: never rebuild on read; fall back to a lock-free, non-mutating linear scan when no hash table is present (the hash table is still built eagerly by the mutation path). Mirrors the 2.4.5 compiled-runtime fix.
- **`src/backends/interpreter/runtime/expressions.c` (get/set property) — per-AST-node property inline cache (`PropertyIC`) is shared across all spawned task threads and was mutated unsynchronized.** Concurrent threads resolved a torn `(cached_object, cached_field_index, ic_state)` group → wrong field. Added a monotonic `g_interp_has_spawned` flag (set in `builtins/concurrency.c` before any `pthread_create`); once any task has been spawned the inline-cache fast-path is bypassed in favor of the lock-free lookup. Single-threaded programs keep the cache unchanged (no perf regression).

### Validated

- Repro (`tests/stress/concurrent_string_build.hml` via the interpreter): **~10% failure pre-fix → 0/50 post-fix**. Linux full parity **259/259** (0 interpreter-only, 0 compiler-only); macOS full parity at known baseline; stress suite **9/9** (`none` + `asan`). Compiled-runtime path unaffected (it already had the 2.4.5 fix). Process note: 2.5.4 validation ran parity + stress *lsan* only; the full stress suite (all modes) is now part of the gate.

## [2.5.4] - 2026-05-18

Per-spawn argument leak fix — a dominant Witchgrid control-plane per-connection bleed. Every `spawn(fn, args…)` / `spawn_with(opts, fn, args…)` leaked each heap argument once per spawn.

### Fixed

- **`src/backends/compiler/codegen_call_async.c` — `spawn`/`spawn_with` argument ownership.** `codegen_expr` returns an owned (+1) temporary for each argument expression; `hml_spawn()` / `hml_spawn_with()` **deep-copy** their args (the task owns the copies — see `task_free`), so they do **not** consume the caller's references. The codegen released `fn_val` (and `opts_val`) but never released the argument temporaries `_spawn_args[i]` / `_spawn_with_args[i]` — so every heap-typed spawn argument (strings, arrays, objects, request data) leaked once per spawn. For a server that does `spawn(handler, request…)` per accepted connection (the Witchgrid CP/agent pattern) this leaks per connection, unbounded. Fix: release each arg temporary after the `hml_spawn[_with]()` call, mirroring the existing `fn_val`/`opts_val` releases. Safe — `hml_spawn` deep-copies synchronously before returning, so the post-call release cannot race or double-free.
- Found by the per-construct leakhunt harness: a new `tests/leakhunt/spawn_fireforget_leak.hml` (fire-and-forget spawn with a heap arg, the CP shape) leaked 56 B/spawn (`hml_val_string_owned ← hml_string_concat`, the arg temp) — `spawn_join_leak.hml` missed it only because its argument is a non-heap `i64`. Now PASS; `spawn_join_leak` still PASS. Validated full parity **259/259** on Linux and macOS, 0 interpreter-only / 0 compiler-only.

## [2.5.3] - 2026-05-18

macOS heap-corruption fix. Explicit `free()` of a refcount-managed buffer was a **double-free + use-after-free**: `hml_free()` on a `HML_VAL_BUFFER` freed both the backing data *and* the `HmlBuffer` struct, but the value is still owned by the refcount system — a top-level `let buf = buffer(N)` is released by `hml_release_statics()` at process exit (and locals by ordinary scope cleanup), and that path calls `buffer_free()` on the same struct, freeing `buf->data` and `buf` a second time. glibc silently tolerates it (Linux parity stayed green, masking the bug); macOS `libsystem_malloc` (`find_zone_and_free`) aborts with `SIGABRT`. The 2.5.2 fatal-signal backtrace handler made it loud, so it surfaced as parity divergence: programs printed the correct output, then dumped a crash trace on exit.

### Fixed

- **`runtime/src/builtins_memory.c` — `hml_free()` buffer branch.** Now frees only the backing allocation, sets `data = NULL` / `length = 0`, and leaves the `HmlBuffer` handle to the refcount system. The later `buffer_free()` (and any repeated `free()`) becomes a safe no-op — the struct is freed exactly once, the backing exactly once. No semantic change: explicit `free(buf)` still releases the large allocation immediately; the small handle is reclaimed at end of life.
- Eliminated all 8 macOS interpreter-only parity divergences (`buffers`, `memory`, `buffer_slice`, `buffer_typed_rw`, `file_read_binary`, `ptr_buffer_direct`, `type_checking`, `write_file_buffer`). Linux full parity unchanged at **259/259, 0 interpreter-only**; macOS interpreter-only **8 → 0**. Found by building 2.5.2 on macOS arm64 and reading the `SIGABRT` backtrace (`buffer_free ← hml_release ← hml_release_statics ← main`).

## [2.5.2] - 2026-05-18

Regression fix. **2.5.0 and 2.5.1 are broken — upgrade to 2.5.2.** The closure-capture (`881b5c1b`) and throw-unwind (`a3f55089`) codegen leak fixes in 2.5.0 were validated only against the leak harness, not the parity/language suite, and generated invalid C (`hml_release(&x)` for identifiers not in lexical scope) for several exception / closure / scope constructs — the compiler **failed to compile** `exceptions`, `error_handling`, `error_catchable`, `nested_exceptions`, `try_early_exit`, `closure_scoping`, `nested_scopes`, `scope_shadowing` (CI red, notably macOS).

### Fixed

- **Reverted both mis-scoped codegen leak fixes** (`881b5c1b`, `a3f55089`). Root cause: both routed capture/local releases through `codegen_emit_local_cleanup`, which is called from many scopes — emitting `hml_release` for variables whose C declarations only exist inside the closure body / a different scope, so the generated C did not compile. Reverted to restore correctness; full parity re-validated at **259/259 (100%)**, zero compile failures, zero output differences.

### Retained (unaffected by the revert)

- The **`@stdlib/sqlite` bind-leak fix** (2.5.1) and the earlier 2.5.0 leak fixes (`for-in`/`obj.keys()`, indexed-assignment RHS temp, `string.split`, JSON `is_pooled`, immortal-string-pool UAF, concurrent `obj[key]` corruption) are stdlib/runtime or independent codegen fixes, not implicated in the compile failures, and remain — parity-clean.

### Known issues

- The closure-capture-on-explicit-return and throw-unwind local leaks are **back to known-issue status** (as before 2.5.0), alongside `throw_indirect`. They need a correctly-scoped reimplementation (release at the closure-body / throwing-function exit where the C vars are in scope, NOT in the universally-called cleanup helper) and **must be validated against the full parity suite**, not just the leak harness, before re-landing.

## [2.5.1] - 2026-05-18

Patch on 2.5.0's memory-correctness work. After 2.5.0 the leakhunt synthetic corpus showed the common per-request *codegen* paths were clean, yet the Witchgrid control plane was still growing. Instrumenting the **real `cp.hml` binary** under LeakSanitizer (the synthetic loop was deliberately set aside as too generic for this) pinpointed the dominant remaining bleed in the stdlib, not codegen.

### Fixed

- **`@stdlib/sqlite` leaked a C string/blob on every parameterized query.** `bind_value` allocated a cstr via `__string_to_cstr` (or a blob via `alloc`) for each string/blob parameter and passed `destructor = null` to `sqlite3_bind_text`/`sqlite3_bind_blob` — that's `SQLITE_STATIC`: SQLite neither copies nor frees the buffer, and the binding never freed it. Every query with a string/blob parameter leaked. With a server hitting sqlite on essentially every request (the Witchgrid CP: `/register` upserts, resolve lookups, dashboard sample fetches) this was the dominant production bleed — LSan on the real CP under representative load showed `hml_string_to_cstr ← bind_value ← exec`/`query` as the top allocation site (~756 KB / ~40 K objects per 600 requests). Fixed by wrapping libc `free` once as a C-callable destructor (`__callback(__sqlite_free_destructor, ["ptr"], "void")`) and passing it as the bind destructor: SQLite now frees each value itself exactly when it's done with it (after step / finalize / reset / rebind). Fixes all six `bind_value` callers — `exec`, `query`, `query_value`, and the prepared-`Statement` API including the cross-call `stmt_bind` — with no caller changes, and ASan-verified to introduce no use-after-free / double-free.

### Known issues (characterized, tracked)

- The real-CP LSan run also pinpointed further **stdlib/runtime** ownership leaks (codegen ruled out — synthetic repros of the same shapes are clean): `@stdlib/http` leaks a string per request, `hml_spawn` leaks the `hml_value_deep_copy` of task args per spawned task, `@stdlib/net`'s `_TcpStream_from_socket` leaks an object per accepted connection, and `getenv` via `shared_secret` leaks per call. These are the subject of an in-progress stdlib-wide leak audit; each is a contained ownership fix in the same vein as the sqlite one. `throw_indirect` (intermediate-frame unwind) remains as in 2.5.0.

## [2.5.0] - 2026-05-17

A memory-correctness release. Driven by the Witchgrid control plane bleeding hundreds of MB over days in production, this is a sustained refcount/ownership audit of the compiled runtime: a per-construct LeakSanitizer harness was built to isolate codegen leaks (generated C has no `#line` info, so whole-program leak reports are unactionable — each micro-repro exercises one construct so a hit maps to one codegen path), and the backlog was drained construct by construct. Net effect: the long-running per-request bleed is root-caused and fixed across the for-in, indexed-assignment, string, exception, and closure-capture paths, plus two concurrency UAF/corruption fixes. One exception-unwind leak remains, characterized and deferred (see Known limitations).

### Added

- **Per-construct leak-hunt harness (`tests/leakhunt/`).** A driver + single-construct `*_leak.hml` micro-repro corpus that compiles each under LSan with a cached instrumented runtime (fast fix-loop verify) and captures the full leak report per construct. `tests/leakhunt/README.md` documents the detect→localize→fix→regression-lock loop and the fix-pattern taxonomy so the audit is repeatable.
- **Concurrency / lifetime stress harness under ASan / TSan / LSan.** `tests/stress/run_stress.sh` builds every `tests/stress/*.hml` as a native binary (refcount-exhaustion and concurrent heap-corruption bugs only manifest in the compiled runtime, not the interpreter) and runs it under an optional sanitizer; the `lsan` lane is a green regression guard for every leak fixed below.
- **Fatal-signal backtrace handler** (`-rdynamic`) for in-place crash diagnosis — a SIGSEGV/SIGABRT now prints a symbolized backtrace instead of dying silently.

### Fixed

- **`for k in obj.keys()` / `for x in <call>()` leaked the entire iterable + every element on each execution.** Two compounding bugs: for-in codegen retained the iterable then released it only once, never consuming the owned `+1` the iterating expression returned (the array, its elements, and backing store all leaked); and `hml_object_keys()` pushed `hml_val_string()` temps into an array whose `push()` retains, orphaning each key string's creation ref. A dashboard rendering `caps.keys()` / `flags.keys()` per request bled multiple MB/min. The redundant for-in retain was subsequently dropped for *all* iterable kinds (ident / array-literal / index / member), not just the `obj.keys()` subcase — this was the primary multi-GB Witchgrid control-plane bleed (a `for tk in ts` per request).
- **Indexed assignment `obj[k] = v` / `arr[i] = v` leaked the RHS temp** on every store — pervasive, since indexed assignment is ubiquitous. Codegen now releases the RHS temporary after the store retains it.
- **`string.split(delim)` leaked every result piece.** Each part was created via `hml_val_string_owned` (refcount 1) then `hml_array_push`'d — push retains (→2) — but `split` never released the creation ref, so every piece leaked even after the result array was freed. Same orphaned-creation-ref class as the `obj.keys()` bug; all three push sites (empty-delim char split, delimiter match, trailing remainder) fixed.
- **`throw` leaked every live heap local in the throwing function.** `STMT_THROW` emitted `hml_throw(v)` directly; `hml_throw` longjmps and never returns, so the function's normal-path local releases (the cleanup `return` runs) were skipped — every array/object/string built before the `throw` leaked, per throw, on hot error paths. `throw` codegen now emits the same scope cleanup `return` does before unwinding (the thrown value is a separately-retained copy, so it survives).
- **Closure captures leaked on every explicit `return`/`throw` from a closure.** The closure-body prologue `hml_closure_env_get`s each capture (which retains); its matching release was emitted only by a loop at the *implicit fall-through* return, leaving it as dead code after any explicit `return`. Capture release is now centralized in the single function-exit cleanup helper that all exit paths (return / throw / fall-through) call — also fixing captures leaking on the exception path — and the redundant explicit loop removed.
- **Hand-built JSON objects missing `is_pooled` initialization** caused a multi-GB serialization leak — objects constructed directly by the JSON path bypassed pool accounting and were never reclaimed.
- **Immortal ASCII string pool is now frozen**, fixing a use-after-free after ~1M releases (the shared single-char/short-string pool entries could be driven to refcount 0 and freed while still globally reachable).
- **`object_lookup_field` is now a lock-free read-only path**, fixing concurrent `obj[key]` heap corruption under multi-threaded reads.

### Known limitations

- **`throw_indirect`: a `throw` propagating through intermediate frames that hold heap locals leaks those frames' locals.** `hml_throw` longjmps straight to the nearest `try`'s `setjmp`, skipping every C frame in between, so a caller frame that is neither the thrower nor the catcher never releases its locals. This is narrower than the fixed leaks (only bites a heap-local-holding frame that is *skipped* by a propagating throw) but is real. It is not a leak fix but an exception-model change: a per-function `setjmp` shim is infeasible (Hemlock declares locals at-use, often in nested scopes — a prologue landing pad cannot reference them), and the correct approach is a runtime cleanup-registration stack walked by `hml_throw` before its `longjmp`. Characterized with a committed repro (`tests/leakhunt/throw_indirect_leak.hml`) and a full corrected design + validation-gate set in `tests/leakhunt/README.md`; deferred to its own reviewed change.

## [2.4.1] - 2026-05-14

Patch release covering three issues surfaced while building Witchgrid's agent-restart adoption path. All three have direct repros and concrete operator pain — fast follow on top of 2.4.0.

### Added

- **`file.read_binary()`** on `@stdlib/fs.open(...)` — returns a buffer instead of going through `val_string`'s strlen-truncate, so reading nul-containing files (e.g. `/proc/<pid>/cmdline`, binary blobs) preserves every byte. Mirrors the `stream.read_binary()` shipped in 2.3.1.
- **`exec_argv(...)` `stdin` option** — accepts a string that gets piped into the child's stdin via `pipe(2)`. Removes the `sh -c "cmd < file"` wrapper pattern operators were using to feed data into a child without writing a temp file.

### Fixed

- **TCP listener and accepted-client socket fds now set `FD_CLOEXEC`.** Without this, sockets created via `@stdlib/net` (or any direct socket builtin) were inherited by `posix_spawn`'d children. Concrete failure mode: a long-lived child process held the parent's listening socket alive across the parent's death, so after a `kill -9` the listener stayed bound (kernel keeps the fd alive while any process holds a reference) and a fresh parent couldn't restart on the same port until every inheriting child also exited. `fcntl(fd, F_SETFD, FD_CLOEXEC)` runs immediately after `socket(2)` in `hml_socket_create` and after `accept(2)` in `hml_socket_accept`. `dup2` within `posix_spawn`'s file_actions block clears CLOEXEC on the duped fd per POSIX, so the existing "redirect log_fd to stdout" pattern keeps working.
- **macOS Hemlock build path harden + clearer install docs** (originally on the `mac-build-docs` branch, cherry-picked onto main post-2.4.0). Runtime Makefile now probes `/opt/homebrew/include` and `/usr/local/include` when `pkg-config` is missing, and the install guide names the symptom of the "Undefined symbols: _hml_lws_http_stream_read_binary" link error so future operators on a fresh Apple Silicon checkout recognize it.

## [2.4.0] - 2026-05-13

A batch release driven by ten issues surfaced during Witchgrid's CP↔agent shared-bearer auth bring-up. Most of the fixes unblock real-world HTTP and concurrency patterns; the rest are ergonomics that make the compiler easier to live with.

### Added

- **`string.lower()` / `string.upper()` aliases** for `to_lower` / `to_upper`. Matches the spelling reach of Python/JS/Ruby and removes a runtime "String has no method 'lower'" papercut for anyone porting code in.

### Fixed

- **`@stdlib/http` POST/PUT/DELETE/PATCH now actually send custom headers.** The wrappers accepted a `headers` array but `http_request_with_redirects` and `http_request_timeout_with_redirects` only scanned it for `Content-Type:` and dropped the rest, so `Authorization`, `X-Request-Id`, etc. never reached the wire. GET worked because `__lws_http_get_with_headers` already used the `custom_headers` plumbing in the LWS callback. New `__lws_http_post_with_headers` / `__lws_http_request_with_headers` (and their `_timeout` siblings) thread the full headers array through, and the stdlib wrappers call them when `headers != null && headers.length > 0`.
- **Interpreter named-module imports are live bindings, not import-time snapshots.** `import { X } from "mod"` used to install a value snapshot of `X` taken at import time, so reassigning the exported `let X` in the source module was invisible to the importer (and to any tasks `spawn`'d from the importing scope). The compiler already used live bindings; the interpreter now matches. Practical impact: code like `let SECRET = ""; fn init() { SECRET = getenv(...); }` in a module now propagates the post-init value to spawned heartbeats / accept workers, instead of those tasks snapshotting `""` at spawn time.
- **Flow null-narrowing propagates through `?.` field access.** A `if (x == null) { return; }` immediately above a use of `x` (or a value derived from `x?.field`) now narrows correctly, so call sites no longer need a shadow `let nn = x;` rebind purely to satisfy the type checker.
- **Type error labels name the parameter instead of `'positional'`.** Errors on argument-type mismatches used to read `argument 'positional' to 'fn_name'`, which gave no hint about which arg in a multi-arg call was wrong. Now they identify the parameter by name (or by 1-based index when the callee is anonymous).
- **`get_binary` follows 3xx redirects.** Previously stopped at the first redirect, which broke pulls of GitHub release tarballs (GitHub redirects to `objects.githubusercontent.com` to S3). Same redirect-chain limit as the text path (10 hops).
- **C codegen no longer prefixes call symbols with surrounding-scope tokens.** Call sites inside a function whose parameter happened to share a prefix with the called function's name (e.g. calling `cp_post_json(agent_url + ...)` inside `port_free_on_agent(agent_url: string, ...)`) were mangling to `hml_fn_agent_cp_post_json` because the codegen accidentally prepended a token from the parameter name. Symbol generation is now stable regardless of the surrounding identifier set.
- **macOS libwebsockets startup picks up Homebrew's CA bundle by default.** `lws_create_context` was failing on Apple Silicon with `Failed to create default vhost` unless the user set `SSL_CERT_FILE=/opt/homebrew/etc/openssl@3/cert.pem`. The runtime now probes the Homebrew openssl@3 bundle automatically when running on Darwin, with a clearer error message if the bundle isn't found at all.
- **Default one-shot LWS HTTP timeout lowered** from ~30000ms to 5000ms across `__lws_http_get`, `__lws_http_post`, `__lws_http_request`, and the `_with_headers` variants. A single failed connect/request used to wedge the caller for half a minute; now it surfaces in 5s. The `_timeout` variants still honor their per-call timeout argument.

### Documentation

- Side-effect imports — using `import "./mod";` for modules that need to run boot code without exporting anything — are now documented as a deliberate pattern, with a worked example. The interpreter and compiler both supported it; only the docs were missing.

## [2.3.1] - 2026-05-12

### Added

- **`@stdlib/http.download_streaming(url, output_path)`** — bounded-memory HTTP download for large artifacts (model weights, archives, anything multi-GB). Opens a stream, reads buffer chunks, writes them straight to disk via `<path>.partial` then atomic-renames on completion. Returns `{status_code, bytes_written}` on success, throws on non-2xx.
- **`stream.read_binary(timeout_ms)`** on the `stream()` return object — same poll loop as `read()` but returns a buffer using the chunk's actual byte length, preserving 0x00 bytes that would have terminated a string read early. Use for downloading non-text content over a streamed connection.
- **`__lws_http_stream_read_binary(stream, timeout_ms)`** runtime + interpreter builtin that backs `stream.read_binary`.

### Fixed

- **`@stdlib/http.download(url, output_path)` actually writes the response body now.** The previous implementation called `f.write(response.body)`, which silently no-op'd in the compiled binary (writing a zero-byte file) and threw `"write() expects string argument"` in the interpreter. Now uses `__open(...).write_bytes(buf)` — same pattern `compression.hml`'s gunzip pipeline already uses.

## [2.3.0] - 2026-05-12

### Added

- **Streaming HTTP in `@stdlib/http`** — chunked / SSE-friendly streaming client built on the already-bundled libwebsockets. New exports:
  - `stream(method, url, body, headers, timeout_ms)` returns an object with `read(timeout_ms)`, `close()`, `status_code`, `headers`, and `done`.
  - `stream_get(url, headers, timeout_ms)` and `stream_post(url, body, headers, timeout_ms)` convenience wrappers.
  - `post_json_stream(url, data, timeout_ms)` for streaming LLM-style JSON POSTs.
  - `stream_sse(url, headers, timeout_ms)` returns a Server-Sent Events parser with `next_event(timeout_ms)`.
- **Streaming HTTP builtins available to both backends** — `__lws_http_stream_start`, `__lws_http_stream_read`, `__lws_http_stream_status`, `__lws_http_stream_headers`, `__lws_http_stream_close` are now wired identically into the interpreter and compiler/runtime.

### Fixed

- **Streaming HTTP POST/PUT/PATCH bodies in compiled binaries** — the compiler runtime previously discarded the request body and content type, so compiled `stream_post()` / `post_json_stream()` calls sent an empty body. The runtime now attaches and writes the body via `LWS_CALLBACK_CLIENT_HTTP_WRITEABLE`, matching the interpreter.
- **Interpreter/compiler parity for thrown errors in streaming HTTP** — the compiler runtime now throws the same catchable exceptions as the interpreter for invalid pointer arguments to `__lws_http_stream_read`, `__lws_http_stream_status`, and `__lws_http_stream_headers`, and for missing-libwebsockets builds. Previously the compiler silently returned `null`/`0`/`""`, diverging from interpreter behavior.
- **Cleanup of streaming request body memory on connect/spawn failures** in the compiler runtime.

## [2.2.3] - 2026-05-12

### Added

- **`@stdlib/fs` `make_dirs(path, mode?)`** — recursive directory creation helper similar to `mkdir -p`, including existing-directory tolerance and file-blocking-component errors.

### Fixed

- **Socket failures are catchable in compiled binaries** — TCP connect-refused and bind/listen failures such as `EADDRINUSE` now throw normal Hemlock exceptions instead of aborting the process.
- **`File.read()` on `/proc` and `/sys` pseudo-files** — reads no longer return an empty string just because `stat()` reports size 0 for dynamically-generated pseudo-file contents.
- **Control flow escaping `try` bodies in compiled code** — `return`, `break`, and `continue` inside `try` bodies now unwind compiler exception-handler state correctly instead of triggering longjmp aborts later.
- **Buffer memory builtins** — `memset()`, `memcpy()`, and direct `ptr_write_*()` operations on buffers now validate full access ranges before writing.
- **Numeric string concatenation regressions** — string concatenation with `f64` values and integer zeroes now preserves expected textual results in both interpreter and compiler paths.
- **Optional-chain null guards in the compiler** — values checked by `x != null`/`x == null` guards are narrowed across optional-chain expressions, eliminating false nullable-type errors.
- **CLI help parsing** — `--help` is honored after inline command flags handled by the interpreter launcher.
- **Security hardening findings** — tightened checked size conversions, buffer-growth overflow handling, byte-array index validation, and package path length handling.
- **Rune/string equality diagnostics** — the interpreter now warns on equality comparisons between `rune` and `string`, matching the compiler-side guidance that these are distinct types.

### Changed

- Refreshed filesystem and memory docs to document recursive directory creation and buffer-aware bounds checks in the memory builtins.

## [2.2.2] - 2026-05-11

### Fixed

- **Runtime `file_stat()` failures are catchable** — compiled binaries now throw normal Hemlock exceptions for `file_stat()` errors instead of terminating the process, matching try/catch expectations.
- **Typed-array fast-path assignment checks** — runtime typed-array assignments now validate values on the optimized path before storing them.
- **Non-default install prefixes** — compiler/module lookup now finds the stdlib and runtime assets in the install.sh layout even when Hemlock is installed outside the default prefix.

### Changed

- Audited and refreshed documentation for the current v2.2.2 repository state, including README status, stdlib inventory, test documentation, and stale/broken relative links.

### Added

- Added `tests/check_docs.py`, a dependency-free documentation audit that verifies stdlib implementation/documentation parity and relative Markdown links.

## [2.2.1] - 2026-05-10

### Fixed

- **HTTP POST/PUT/PATCH/DELETE bodies in compiled binaries** — the runtime's `hml_lws_http_post` / `hml_lws_http_request` / their `_timeout` variants used to `(void)body_val; (void)content_type_val;` with a "Not fully implemented yet" comment, so compiled binaries silently sent every body-bearing request as method+URL with NO `Content-Type`, NO `Content-Length`, and NO body bytes on the wire. The interpreter went through a separate code path that handled the body via `lws_callback_on_writable`, masking the bug from anyone testing only via `hemlock`. The runtime callback now matches the interpreter: `LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER` emits `Content-Type` (unless caller-supplied) and `Content-Length`; `LWS_CALLBACK_CLIENT_HTTP_WRITEABLE` writes the body bytes once. Regression test at `tests/parity/builtins/http_post_body.hml`.
- **libwebsockets startup banner in compiled binaries** — every `lws_create_context` call printed a NOTICE-level line (`[ts] N: lws_create_context: LWS: 4.3.3-... NET CLI SRV H1 H2 WS ConMon IPV6-on...`) to stderr from compiled binaries. The interpreter has silenced this since 1.x via `lws_init_logging()` in `src/backends/interpreter/builtins/websockets.c`; the runtime never had an equivalent. Added a static-guarded `hml_lws_init_logging()` to the runtime that drops the log level to `LLL_ERR` by default; set `LWS_VERBOSE=1` to opt back in.

## [2.2.0] - 2026-05-10

### Added

- **`@stdlib/process` `posix_spawn(argv, opts?)`** — detached child-process primitive backed by `posix_spawn(3)`. Returns immediately with `{ pid }`; caller reaps via `waitpid()`. Fills the gap between the racy `fork()`/`exec()` pair and the blocking `exec_argv()`. Options support `env` (`KEY=value` array), `stdin`/`stdout`/`stderr` fd redirections, `cwd` (chdir before exec, glibc 2.29+ / macOS 10.15+), and `setsid` (detach from controlling terminal). Exported as `posix_spawn` rather than `spawn` to avoid shadowing the language-level task `spawn(fn, ...)` builtin in the compiler. Documented in `stdlib/docs/process.md`.
- **`@stdlib/fs` `open_fd(path, mode?)` and `fileno(file)`** — raw POSIX file-descriptor access. `open_fd` returns an `i32` fd directly (no buffered `File` wrapper), and `fileno` extracts the fd backing an existing `File`. Together they let `posix_spawn` callers redirect a child's stdio into a log file without round-tripping through `["sh", "-c", "exec ... > log 2>&1"]`. `fileno` preserves `File` ownership of the fd; closing the file closes the fd. Sandbox policy mirrors `open()`. Documented in `stdlib/docs/fs.md`.
- **String-literal object keys** — object literals may now use string literals as field names: `let m = { "chat-mahou": 1, "user-id": 42 };`. Required when the key contains characters that aren't valid in a bare identifier (hyphens, spaces, leading digits). Shorthand is not permitted for string keys; a colon is mandatory. The formatter quotes such keys on output to keep round-trips lossless.
- **`obj?[key]` safe-index operator** — short form of the existing `obj?.[key]`, parallel to `obj?.field` safe-navigation. Returns `null` when the receiver is `null`; otherwise behaves like `obj[key]`. The lexer only emits the safe-index token when `[` immediately follows `?` (no whitespace), so `a ? [1] : [2]` ternaries are unaffected.

### Fixed

- **FFI two-library import collision** — the compiler used to emit a single shared `_ffi_lib` global plus per-symbol pointer slots, so the second `import "..."` clobbered the first library handle and lazy-resolution of any symbol from the first library failed at runtime. Most common collision: `@stdlib/sqlite` (`libsqlite3.so.0`) plus `@stdlib/uuid` (`libcrypto.so.3`). Codegen now stamps every `extern fn` with the path of its preceding `import_ffi` and emits one `_ffi_lib_<sanitized>` global per unique library path; each wrapper resolves against its own handle.
- **Compiler/interpreter parity gaps**:
  - `let xs: array<T> = [...]` now auto-fills optional fields per element when `T` is a registered object type (compiler matches interpreter behavior).
  - `export let X = X;` is now treated as `export { X };` instead of trying to redefine `X`.
  - `substr()` is type-checked for arity at compile time, mirroring the interpreter's runtime check; previously single-arg `substr` produced a confusing "String has no method 'substr'" error.
- **WASM interpreter build** — the new `posix_spawn` primitive is stubbed out under emscripten (which has no `posix_spawn(3)`); calling it in-browser throws a clear runtime error rather than failing to link. `_GNU_SOURCE` define guards prevent redefinition warnings under WASM CI.

### Build / Infrastructure

- **Incremental builds now track header dependencies** — top-level `Makefile` and `runtime/Makefile` add `-MMD -MP` to `CFLAGS` and `-include` the resulting `.d` files. Touching a header (e.g. `include/frontend/ast.h`) now correctly rebuilds the 86 transitively dependent objects instead of leaving them stale; previous behavior produced binaries linked from objects compiled against different versions of the same header.
- **emsdk bumped 3.1.51 → 4.0.7** — the older zlib port's hardcoded SHA-256 went stale after GitHub re-rolled the upstream tarball, breaking `make wasm-interpreter`.

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
