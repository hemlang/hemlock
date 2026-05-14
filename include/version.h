/*
 * Hemlock Version Header
 *
 * Centralized version information for all Hemlock components.
 * All components (interpreter, compiler, LSP) share this version.
 */

#ifndef HEMLOCK_VERSION_H
#define HEMLOCK_VERSION_H

#define HEMLOCK_VERSION_MAJOR 2
#define HEMLOCK_VERSION_MINOR 4
#define HEMLOCK_VERSION_PATCH 1

#define HEMLOCK_VERSION "2.4.1"
#define HEMLOCK_VERSION_STRING "Hemlock v" HEMLOCK_VERSION

/*
 * Version history:
 *   2.4.0 - HTTP/auth + ergonomics batch (10 issues from Witchgrid bring-up): @stdlib/http now threads custom headers through POST/PUT/DELETE/PATCH and the timeout variants (was: only Content-Type made it to libwebsockets, breaking Authorization on POST); interpreter named-module imports are now live bindings instead of import-time snapshots so reassigning an exported `let` is visible to importers and to spawned tasks; flow null-narrowing propagates through `?.` field access (no more `let mp = model_path` shadow-rebind after a null check); type error labels report the parameter name instead of `'positional'`; `string.lower()`/`upper()` aliases for `to_lower`/`to_upper`; `get_binary` follows 3xx redirects (GitHub→CDN tarball pulls work directly); C codegen no longer prefixes call symbols with surrounding scope tokens (function calls inside `port_free_on_agent(agent_url: ...)` no longer mangle to `hml_fn_agent_*`); macOS libwebsockets initialization picks up Homebrew CA bundles by default (no SSL_CERT_FILE workaround); default one-shot LWS HTTP timeout lowered from ~30s to 5s so a single failed call doesn't wedge the caller for half a minute (the `_timeout` variants still honor their per-call timeout); side-effect import workaround documented
 *   2.3.1 - Binary HTTP fixes: @stdlib/http.download() now actually writes the buffer body (was f.write(buffer) which silently no-op'd in compiled binaries and threw in the interpreter); new download_streaming(url, path) for bounded-memory pulls of large files (model weights, archives) — chunks straight to disk via .partial + atomic rename; new stream.read_binary() + __lws_http_stream_read_binary returning buffer chunks (existing read() routes through val_string which strlen-truncates at the first 0x00 byte, corrupting binary payloads)
 *   2.3.0 - Streaming HTTP support in @stdlib/http (chunked / SSE): stream(), stream_get(), stream_post(), post_json_stream(), stream_sse(); compiler runtime now sends POST bodies for streaming requests and throws the same catchable errors as the interpreter (invalid ptr, bad URL, connection failure) for full interpreter/compiler parity
 *   2.2.3 - Patch release with runtime/compiler hardening: catchable socket connect/bind failures, correct /proc and /sys File.read() behavior, safer buffer memory builtins, optional-chain null-guard narrowing, string concatenation numeric regressions fixed, CLI help parsing fixed, and fs.make_dirs() added
 *   2.2.2 - Release prep with compiler/runtime/install hardening: catchable runtime file_stat() failures, typed-array fast-path assignment checks, non-default-prefix stdlib/runtime lookup fixes, and refreshed documentation audits
 *   2.2.1 - HTTP POST/PUT/PATCH/DELETE body fix: compiled binaries actually send Content-Type, Content-Length, and the body bytes (interpreter was already correct); libwebsockets startup banner suppressed by default in compiled binaries to match interpreter
 *   2.2.0 - posix_spawn() primitive in @stdlib/process; fs.open_fd()/fileno() for raw fd access; string-literal object keys and obj?[key] safe-index; FFI fix for two-library import collision; compiler-strictness parity fixes (typed array of custom types, `export let X = X;` self-rebind, substr() arity); incremental builds correctly track header dependencies
 *   2.1.1 - Parser no longer hangs on malformed `match { arm => { ... } }` when block arm is parsed as object literal and fails
 *   2.1.0 - HTTP client actually sends POST/PUT/PATCH bodies; @stdlib/json exports its public API for `import * as`
 *   2.0.3 - Fix compiler symbol collision when a module exports `init` (or any name shared with the module initializer)
 *   2.0.2 - Fix i32 overflow in HashMap djb2 string hash; accumulate in i64 + mask
 *   2.0.1 - Bugfix release since v2.0.0 (runtime/compiler correctness, stability, docs)
 *   2.0.0 - BREAKING: Move 63 builtins to stdlib modules, reducing global namespace conflicts
 *   1.10.0 - (skipped, became 2.0.0)
 *   1.9.4 - Fix fire-and-forget spawn, WebSocket server SO_REUSEADDR
 *   1.9.3 - WebSocket binary data support: __lws_msg_binary builtin, fix server send_binary, fix recv binary handler
 *   1.9.2 - Compiler fixes: unboxed loop counter boxing, clear() object dispatch, exec() import shadowing
 *   1.9.1 - QoL: write() builtin, single-arg slice(), rune join(), HashMap numeric key coercion
 *   1.9.0 - WASM interpreter release artifact, version bump for release
 *   1.8.8 - Fix compiler inlining bugs, ptr subtraction type checking, catchable open() exceptions
 *   1.8.7 - Fix multi-argument print/eprint in compiler codegen
 *   1.8.6 - Fix segfault in hml_string_append_inplace for SSO strings
 *   1.8.5 - 5 new array methods (every, some, indexOf, sort, fill), major performance optimizations, memory leak fixes
 *   1.8.4 - Graceful handling for reserved keywords (def, func, var, class), fix flaky CI tests
 *   1.8.3 - Code polish: consolidate magic numbers, standardize error messages
 *   1.8.2 - Memory leak prevention: exception-safe eval, task/channel cleanup, optimizer fixes
 *   1.8.1 - Fix use-after-free bug in function return value handling
 *   1.8.0 - Pattern matching, arena allocator, memory leak fixes
 *   1.7.5 - Fix formatter else-if indentation bug
 *   1.7.4 - Formatter improvements: function parameter, binary expr, import, and method chain line breaking
 *   1.7.3 - Fix formatter comment and blank line preservation
 *   1.7.2 - Maintenance release
 *   1.7.1 - Single-line if/while/for statements (braceless syntax)
 *   1.7.0 - Type aliases, function types, const params, method signatures, loop labels, named args, null coalescing
 *   1.6.7 - Octal literals, block comments, hex/unicode escapes, numeric separators
 *   1.6.6 - Float literals without leading zero, fix strength reduction bug
 *   1.6.5 - Fix for-in loop syntax without 'let' keyword
 *   1.6.4 - Hotfix release
 *   1.6.3 - Fix runtime method dispatch for file, channel, socket types
 *   1.6.2 - Patch release
 *   1.6.1 - Patch release
 *   1.6.0 - Compile-time type checking, LSP integration, bitwise operators
 *   1.5.0 - Full type system, async/await, atomics, 39 stdlib modules
 *   1.1.0 - Unified versioning across all components
 *   1.0.x - Initial release series (interpreter only)
 *   0.1.x - Pre-release compiler development
 */

#endif /* HEMLOCK_VERSION_H */
