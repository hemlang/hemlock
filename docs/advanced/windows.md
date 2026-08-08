# Windows (MinGW-w64) Support

Hemlock builds for Windows with the MinGW-w64 toolchain, either
cross-compiled from Linux/macOS or natively under MSYS2. The result is a
self-contained `hemlock.exe` (only system DLLs required).

> Windows support is functional but younger than the Linux/macOS ports.
> The interpreter core, async runtime, networking, file I/O, and most of
> the stdlib work; the POSIX process model and a few libraries are not
> available (see [Limitations](#limitations)).

## Cross-compiling from Linux

Install the toolchain (Debian/Ubuntu):

```bash
sudo apt-get install gcc-mingw-w64-x86-64 libz-mingw-w64-dev
```

Build:

```bash
make mingw               # hemlock.exe + hemlockc.exe + runtime
make mingw-interpreter   # hemlock.exe only
make mingw-clean
```

Cross-build objects go to `build-mingw/` (and `runtime/build-mingw/`), so
they never collide with a native build in the same checkout. The Windows
runtime library stays in `runtime/build-mingw/` — when compiling Hemlock
programs on Windows, `hemlockc.exe` finds `libhemlock_runtime.a` next to
itself or via `--runtime <dir>`.

The default cross toolchain is `x86_64-w64-mingw32-gcc-posix`; override
with `make mingw MINGW_CC=...`. The **POSIX threads** flavor of MinGW-w64
is required — Hemlock's async runtime (`spawn`/`join`/channels) runs on
pthreads, which winpthreads provides on Windows.

Test the result with [Wine](https://www.winehq.org/) if you are not on
Windows:

```bash
wine ./hemlock.exe examples/hello.hml
```

## Building natively on Windows (MSYS2)

In a UCRT64 shell (what CI uses; a MINGW64 shell with the
`mingw-w64-x86_64-*` packages works too):

```bash
pacman -S make mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-zlib mingw-w64-ucrt-x86_64-libffi
make
```

The Makefile detects MSYS2/MinGW via `uname` and configures itself the
same way as a cross build. `make mingw-clean` removes only cross-build
artifacts and never touches a native build in the same checkout.

### Other native shells and toolchains

MSYS2 is what CI uses, but nothing in the build requires it. Any
POSIX-flavored shell whose `uname` reports `MINGW*`/`MSYS*` (Git Bash
does) plus GNU make and a **POSIX-threads** mingw-w64 GCC on `PATH` is
enough — for example the toolchain that ships with
[Strawberry Perl](https://strawberryperl.com/) (`C:\Strawberry\c\bin`),
which brings its own zlib and libffi, so FFI and compression are both
enabled:

```bash
make -j8            # hemlock.exe, hemlockc.exe, libhemlock_runtime.a
./hemlock.exe examples/hello.hml
```

Header sets older than mingw-w64 6.0 lack `<afunix.h>` and
`PROCESSOR_ARCHITECTURE_ARM64`; `include/hemlock_platform.h` declares
both itself when they are missing (the values are fixed by the Win32
ABI), so an older GCC is not a blocker.

One wrinkle worth knowing when driving a native build from a POSIX-ish
shell: the shell rewrites POSIX paths in **command-line arguments** into
Windows paths, so `./hemlock.exe /tmp/x.hml` works, but a `/tmp/...`
string *inside* a Hemlock program is passed through untouched and
resolves as `C:\tmp\...`, which normally does not exist. Use paths the
Windows side understands (or read one from `TEMP`) in program source.

## How the port works

- `include/hemlock_platform.h` is the platform compatibility layer:
  winsock2 for sockets, `dlopen` over `LoadLibrary`, `mmap` over
  `CreateFileMapping`/`VirtualAlloc`, `poll` over `WSAPoll`,
  `setenv`/`realpath`/`strndup`/`getline` shims, and the missing POSIX
  signal constants. Implementations live in `src/shared/platform_win32.c`
  (compiled into both the interpreter and the runtime library).
- `include/hemlock_compat.h` is a lean subset of the above that does not
  pull in `windows.h` (whose `TokenType` enumerator collides with the
  lexer's), for use in frontend/tool sources.
- Threading uses winpthreads, so the async runtime is unchanged.
- The hash builtins (`sha1`/`sha256`/`sha512`/`md5`) and the
  `__random_bytes` CSPRNG are backed by Windows CNG (`bcrypt.dll`)
  instead of OpenSSL, so hashing, `@stdlib/uuid`, and secure random
  generation need no extra libraries.
- `@stdlib/regex` runs on a bundled POSIX ERE engine (musl libc's
  TRE-based `regcomp`/`regexec`, vendored in `src/shared/regex_win32/`)
  since MinGW has no `<regex.h>`; the builtins compile the same POSIX
  code path as on Linux/macOS. Error message wording differs (TRE vs
  glibc), matching behavior does not.
- `@stdlib/termios` raw mode and `@stdlib/terminal` size detection run
  on the console API (`SetConsoleMode`, `GetConsoleScreenBufferInfo`)
  via the `__term_*` builtins. Raw mode enables virtual-terminal input
  and output, so arrow keys arrive as the same ESC sequences POSIX
  terminals send and ANSI colors/cursor escapes work.
- `exec()`/`exec_argv()` run on CreateProcess + pipes: argv vectors are
  quoted with the `CommandLineToArgvW` rules so arguments round-trip
  intact, and shell commands go through `%COMSPEC% /S /C` — so shell
  snippets must be written for cmd.exe on Windows (`dir`, `2>nul`, …),
  exactly as they must be written for `/bin/sh` on POSIX.
- AF_UNIX sockets use Windows 10's native support (`afunix.h`).
- File I/O defaults to binary mode (no CRLF translation) to keep byte
  counts and written data identical across platforms. **stdout and stderr
  are put in binary mode too**, so `print()` writes a single `\n` exactly
  as it does on POSIX and a program's output is byte-identical across
  platforms. stdin is left in text mode, where the CRT's CRLF→LF
  translation is what makes `read_line()` return `abc` and not `abc\r`.
- `setjmp` zeroes the `jmp_buf`'s `Frame` field so the CRT's `longjmp`
  skips its SEH unwind. mingw-w64's default records a frame, and unwinding
  through generated closure frames faults inside `RtlVirtualUnwind`;
  Hemlock's `throw` runs no destructors and no defers, so there is nothing
  for an unwind to do. This is effective on msvcrt and not on the UCRT —
  see [Known issue: throw from a native callback](#known-issue-throw-from-a-native-callback-on-ucrt).
- `hemlock.exe`/`hemlockc.exe` reserve a 64 MB stack
  (`-Wl,--stack`). The PE default of 2 MB is exhausted long before the
  tree-walking interpreter reaches its own 8000-frame recursion guard, so
  runaway recursion died as a silent `STATUS_STACK_OVERFLOW` instead of
  throwing a catchable error. Reserve is address space, not committed
  memory.

## Limitations

Calling any of these throws a runtime error on Windows:

| Area | Detail |
|------|--------|
| Process model (partial) | `fork()`, `wait()` (wait-for-any), `getppid()`, `getuid()`-family — Windows has no fork or POSIX uids. **Not** affected: `exec()`/`exec_argv()` (CreateProcess + pipes, shell mode via `cmd.exe /S /C`), `posix_spawn()` (detached CreateProcess; `env`/`cwd`/stdio-fd/`setsid` options work, `setsid` maps to a new process group with no console), `waitpid()` (blocking or `WNOHANG`; status is POSIX-encoded so `status >> 8` is the exit code), and `kill()` (signal 0 probes existence; other signals terminate with exit code 128+sig — Windows cannot deliver signals). `get_pid()` and `pipe()` also work. |
| FFI without libffi | FFI is **fully supported** when the MinGW toolchain has libffi (auto-detected, see below); only without it do `extern fn`, `ffi_open`, and callbacks throw (`HEMLOCK_NO_FFI`). |
| Crypto (partial) | Only the ECDSA builtins and `@stdlib/crypto`'s OpenSSL-bound functions (AES, RSA — the module's `import "libcrypto.so.3"` cannot load) throw. **Not** affected: the hash builtins (`sha1`/`sha256`/`sha512`/`md5`, and `@stdlib/hash`), `__random_bytes` (and `@stdlib/uuid`) — all backed by Windows CNG (`bcrypt.dll`, a system DLL) in both backends. |
| Signals | Only the signals the Windows CRT supports (`SIGINT`, `SIGTERM`, `SIGABRT`, `SIGSEGV`, `SIGFPE`, `SIGILL`) can be handled. Other constants exist but `signal()`/`raise()` on them fails. |
| HTTP/WebSocket | **Available on native builds** when libwebsockets is installed (`pacman -S mingw-w64-ucrt-x86_64-libwebsockets`), which the build probes for. Cross builds get no probe — a Linux host's pkg-config would answer about the wrong library — so `@stdlib/http` and `@stdlib/websocket` still throw there. See [WebSockets and HTTP](#websockets-and-http-on-windows). |
| LSP | stdio transport works; `--lsp-tcp` mode is disabled. |

Smaller quirks worth knowing:

- `poll()` maps to `WSAPoll`, which only understands **sockets** —
  polling regular file descriptors fails at runtime.
- `hemlockc` refuses input/output paths containing `%`, `!`, or `"` on
  Windows: its gcc invocation goes through cmd.exe, which expands those
  even inside double quotes with no reliable escape.
- AF_UNIX sockets need Windows 10 1803+ at runtime (`afunix.h`).
- `os_version()` uses `GetVersionEx`, which under-reports on Windows
  8.1+ unless the executable carries a compatibility manifest; treat the
  value as informational.

Everything else — the language core, async/channels/atomics, buffers and
manual memory, TCP/UDP/Unix sockets, DNS, file and directory I/O, mmap,
zlib compression, cryptographic hashing and secure random (CNG-backed,
including `@stdlib/uuid`), command execution and process management (`exec()`/`exec_argv()`,
`posix_spawn()`/`waitpid()`/`kill()`, and `@stdlib/shell`'s
`run`/`run_capture`), terminal control
(`@stdlib/termios` raw mode, `@stdlib/terminal`), POSIX ERE regex
(`@stdlib/regex`, bundled engine), JSON/CSV/TOML/YAML, math, strings —
works on Windows. Note that `@stdlib/shell`'s Unix-command
conveniences (`ls()`, `which()`, `pwd()`, …) shell out to POSIX tools
and stay Unix-only.

## WebSockets and HTTP on Windows

`@stdlib/websocket` (client *and* server) and `@stdlib/http` work on a
native Windows build. Install libwebsockets and rebuild:

```bash
pacman -S mingw-w64-ucrt-x86_64-libwebsockets
make
```

The build probes `pkg-config` and defines `HAVE_LIBWEBSOCKETS` /
`HML_HAVE_LIBWEBSOCKETS` when it is found; without it those modules
throw "not available (libwebsockets not installed)" exactly as before.
No probe runs for cross builds, since the host's pkg-config describes
the Linux copy.

`hemlockc` links libwebsockets into compiled programs too. It has to ask
for it as `-Wl,-Bdynamic -lwebsockets`: the link line passes `-static`,
under which the linker looks for `libwebsockets.a` and fails with *"have
you installed the static version of the websockets library ?"* — MSYS2
ships an import library plus a differently named
`libwebsockets_static.a`.

### Standalone binaries: `WIN_LWS_STATIC=1`

A dynamically linked build needs `libwebsockets.dll`, `libssl-3-x64.dll`
and `libuv-1.dll` on `PATH`, so it runs **only inside an MSYS2 shell** —
from `cmd.exe` it exits without printing anything. To link them in:

```bash
make WIN_LWS_STATIC=1
```

| | size | runs outside MSYS2 |
|---|---|---|
| default (dynamic) | 4.6 MB | no |
| `WIN_LWS_STATIC=1` | 12.2 MB | yes |

The released `hemlock-windows-x86_64.zip` is built this way, so the
binaries in it need nothing but Windows system DLLs. Programs compiled
by `hemlockc` still link libwebsockets dynamically.

## Known issue: a WebSocket program may not exit

A program that `spawn()`s a task holding a `WebSocketServer` produces
correct results and then **does not terminate** — the process sits there
after the last statement runs. It affects the interpreter and compiled
binaries alike.

Narrowed down by elimination:

| program | exits? |
|---------|--------|
| `WebSocketServer(...)` then `close()`, no `spawn` | yes |
| server + `spawn`, client round trip, `await`, `close()` | **no** |
| server + `spawn`, **no client at all** (accept times out) | **no** |
| server + `spawn`, no `await` | **no** |

So the trigger is handing a server to `spawn()`, not the traffic and not
the shutdown order: the spawned task gets a deep copy of the server
object, and the copy's service thread outlives `close()` on the original
(`stdlib/websocket.hml` already notes that a copy's `closed` flag does not
track the original's). Nothing then stops the process from waiting on that
thread at exit.

Workarounds until it is fixed: run the server in its own process, or
accept that the process needs killing — output written before the hang is
complete and correct. `@stdlib/websocket`'s own `listen()` loop, which
shuts down through a stop channel rather than relying on `close()` alone,
is not affected; a program built on `Server.listen()` (as gn.hml is) exits
normally.

CI bounds every WebSocket invocation with `timeout` and asserts on output
rather than exit status for this reason.

## Known issue: throw from a native callback on UCRT

A `throw` that crosses a native runtime frame — the clearest case is a
comparator passed to `array.sort()` — can segfault in **compiled**
programs before the `catch` block runs. The interpreter is unaffected
(it does not use `setjmp` at all), and so is any throw that does not
cross a native frame.

**How often:** frequently enough to matter. The rate depends on the
shape of the code, not on luck alone: a sort-comparator throw fails
about 1 run in 25, but exception-heavy code is far worse —
`tests/parity/language/error_catchable.hml` segfaults in **8 of 15
runs** on UCRT64 with GCC 16. Treat compiled exception-heavy code on
UCRT as unreliable rather than occasionally unlucky. (An earlier
revision of this page quoted only the 1-in-25 figure, which understated
it badly.)

The cause is the CRT's `longjmp` driving an SEH unwind (`RtlUnwindEx`)
through the generated closure frames. Hemlock's `throw` has nothing to
unwind, so the fix is to suppress it, and zeroing the `jmp_buf`'s
`Frame` field does that on msvcrt. The UCRT unwinds regardless of that
field, so the crash remains there. Measured with
`try { a.sort(fn(x, y) { throw "boom"; }); } catch (e) { ... }`, 25-60
runs per cell:

| mechanism | msvcrt (cross builds, Wine, GCC 8.3) | UCRT64 + GCC 16 |
|-----------|--------------------------------------|-----------------|
| mingw default `setjmp` | ~1 in 8 crash | ~1 in 25 crash |
| `Frame = 0` (**shipped**) | 0 in 60 | ~1 in 25 crash |
| `__builtin_setjmp`/`longjmp` | 0 in 60 | **25 in 25** crash |

Those UCRT rates are for the sort-comparator fixture. Exception-heavy
code fails far more often on the same builds — `error_catchable` is 8/15
on pristine `main` and 6/15 with the shipped mechanism, i.e. the
mechanism does not move the needle on UCRT either way.

That last row is why the nonlocal-goto builtins are not used even though
they look like the ideal answer: GCC's `__builtin_setjmp`/
`__builtin_longjmp` do not work cross-function on x86_64 SEH targets
with a modern GCC — the jump lands on a garbage address (`rip` in no
known function, every frame `??`), turning an intermittent failure into
a deterministic one.

To A/B this yourself, `HEMLOCK_WIN32_CRT_SETJMP` leaves `setjmp`
untouched. Define it for **both** the runtime and the generated C:

```bash
make -C runtime clean
make -C runtime static CC="gcc -DHEMLOCK_WIN32_CRT_SETJMP"
cp runtime/build/libhemlock_runtime.a ./
hemlockc.exe --cc "gcc -DHEMLOCK_WIN32_CRT_SETJMP" -o thr.exe thr.hml
```

Defining it on only one side makes `hml_throw` jump on a buffer the other
mechanism filled, which crashes on every run — a result that looks
alarming and means nothing.

The `windows-native` CI job reports this case without gating on it; the
Wine cross job asserts it hard, since the fix is real on msvcrt.

## Running the test suites on Windows

Where a native Windows build stands:

```bash
make test-borrow test-lint test-check test-cli        # fully green
make test-contracts test-formatter test-bundler       # fully green

make parity                                           # 306/320
make test-compiler                                    # 48/54
make test                                             # 678 pass, 39 fail
```

`make parity` reports no interpreter-only and no compiler-only failures —
the two backends agree with each other on every fixture. What remains
failing on both is a fixture that pins POSIX behavior, not a parity
break:

| Fixtures | Why they fail on Windows |
|----------|--------------------------|
| `file_io`, `filesystem`, `file_read_binary`, `file_stat_throws`, `fs_open_fd`, `write_file_buffer`, `stdlib_fs` | The test source hardcodes `/tmp/...`, which resolves to `C:\tmp` |
| `signals`, `signals_zero_arg`, `signal_tty_constants` | `SIGUSR1`/`SIGHUP` etc. cannot be raised or handled |
| `pipe`, `poll_constants` | `WSAPoll` only polls sockets |
| `process`, `spawn` | `fork()` has no Windows equivalent |
| `exec`, `exec_argv_quoting` | `cmd.exe`'s `echo` emits CRLF and quotes differently |
| `sockets` | `.expected` pins Linux's `SOL_SOCKET`/`SO_REUSEADDR` (Windows uses `0xFFFF`/`4`) |

The interpreter suite (`make test`) fails the same set plus the
POSIX-only stdlib categories (`stdlib_process`, `stdlib_ipc`,
`stdlib_unix_socket`, `stdlib_shell`'s Unix-command helpers,
`stdlib_crypto`'s OpenSSL functions, and anything HTTP). Neither runner
has a skip-on-Windows mechanism yet, so both exit non-zero; CI covers
Windows with the curated smoke tests in
`.github/workflows/windows-mingw.yml` instead.

## FFI on Windows (raylib games, native bindings)

FFI works on Windows when the MinGW toolchain can link libffi; the build
auto-detects it (`$(CC) --print-file-name=libffi.a`) and falls back to
the runtime-error stubs when absent.

Getting libffi:

- **MSYS2 (native):** `pacman -S mingw-w64-ucrt-x86_64-libffi` — done.
- **Cross builds:** Debian/Ubuntu don't package a MinGW libffi; build it
  from source once (~30 s). Use the release tarball — it ships a
  pre-generated `configure`, whereas the git tag needs `autogen.sh`,
  which is fragile across autotools versions:

  ```bash
  curl -fsSLO https://github.com/libffi/libffi/releases/download/v3.4.6/libffi-3.4.6.tar.gz
  tar -xzf libffi-3.4.6.tar.gz && cd libffi-3.4.6
  ./configure --host=x86_64-w64-mingw32 --prefix=/usr/x86_64-w64-mingw32 \
      --disable-shared --enable-static --disable-docs \
      CC=x86_64-w64-mingw32-gcc-posix
  make -j && sudo make install
  ```

  (`.github/workflows/windows-mingw.yml` does exactly this, cached.)

With FFI enabled, both backends load DLLs at runtime: `import "foo.dll"`
goes through `LoadLibrary`, `extern fn` calls dispatch through libffi,
and libffi is linked statically so the binaries stay self-contained.

Cross-platform bindings work unmodified: Linux library names in imports
are translated the same way the macOS port translates them to `.dylib` —
`import "libraylib.so"` tries `libraylib.dll` (MSYS2 naming), then
`raylib.dll` (official release naming). So a raylib game written against
the raylock bindings builds into a Windows `.exe` like this:

```bash
# next to raylib.dll from raylib's Windows release:
hemlockc.exe -o game.exe game.hml
./game.exe
```

Caveat: the C runtime translation maps `libc.so.6`/`libm.so.6` to
`msvcrt.dll` — common functions (`strlen`, `malloc`, ...) resolve, but
POSIX-only symbols won't exist there.
