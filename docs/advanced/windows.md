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
- `setjmp` is used in its non-unwinding form (`_setjmp(buf, NULL)`).
  mingw-w64's default makes the CRT's `longjmp` run a full SEH unwind,
  which faults inside `RtlVirtualUnwind` when it walks generated closure
  frames; Hemlock's `throw` runs no destructors and no defers, so there
  is nothing for an unwind to do.
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
| HTTP/WebSocket | libwebsockets is not probed for Windows builds; `@stdlib/http` and `@stdlib/websocket` are unavailable. |
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
