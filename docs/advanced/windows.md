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

In a MINGW64 shell:

```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-zlib make
make
```

The Makefile detects MSYS2/MinGW via `uname` and configures itself the
same way as a cross build.

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
- AF_UNIX sockets use Windows 10's native support (`afunix.h`).
- File I/O defaults to binary mode (no CRLF translation) to keep byte
  counts and written data identical across platforms.

## Limitations

Calling any of these throws a runtime error on Windows:

| Area | Detail |
|------|--------|
| Process model | `fork()`, `exec()`, `exec_argv()`, `posix_spawn()`, `wait()`/`waitpid()`, `kill()`, `getuid()`-family — Windows has no fork/exec or POSIX uids. `get_pid()`, `pipe()`, fd I/O, and `system`-style invocation via the compiler driver still work. |
| FFI | Builds use `HEMLOCK_NO_FFI` because libffi is not packaged for MinGW cross builds. `extern fn`, `ffi_open`, and callbacks throw. On MSYS2 you can install `mingw-w64-x86_64-libffi` and override via `EXTRA_CFLAGS`/`EXTRA_LDFLAGS`. |
| Crypto | `HEMLOCK_NO_OPENSSL`: `sha1`/`sha256`/`sha512`/`md5` and ECDSA builtins throw. The pure-Hemlock `@stdlib/hash` implementations still work. |
| Regex | MinGW has no POSIX `<regex.h>`; `@stdlib/regex` throws. |
| Signals | Only the signals the Windows CRT supports (`SIGINT`, `SIGTERM`, `SIGABRT`, `SIGSEGV`, `SIGFPE`, `SIGILL`) can be handled. Other constants exist but `signal()`/`raise()` on them fails. |
| HTTP/WebSocket | libwebsockets is not probed for Windows builds; `@stdlib/http` and `@stdlib/websocket` are unavailable. |
| Terminal | `@stdlib/termios` (raw mode) is POSIX-only. |
| LSP | stdio transport works; `--lsp-tcp` mode is disabled. |

Everything else — the language core, async/channels/atomics, buffers and
manual memory, TCP/UDP/Unix sockets, DNS, file and directory I/O, mmap,
zlib compression, JSON/CSV/TOML/YAML, math, strings — works on Windows.
