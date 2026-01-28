# Windows Support for Hemlock

This document describes Windows support for the Hemlock programming language.

## Overview

Hemlock now includes experimental Windows support via MinGW-w64. While the core language features work, some POSIX-specific functionality has limited or no support on Windows.

## Building on Windows

### Prerequisites

1. **MinGW-w64** - Install via MSYS2:
   ```bash
   pacman -S mingw-w64-x86_64-gcc
   pacman -S mingw-w64-x86_64-make
   pacman -S mingw-w64-x86_64-libffi
   pacman -S mingw-w64-x86_64-openssl
   pacman -S mingw-w64-x86_64-zlib
   ```

2. **pthread support** - Included with MinGW-w64

### Building

From the MSYS2 MinGW64 shell:

```bash
make
```

This will build:
- `hemlock.exe` - The interpreter
- `hemlockc.exe` - The compiler

## Platform Detection

The build system automatically detects Windows and sets the `HML_WINDOWS` preprocessor macro. This is used throughout the codebase for platform-specific code paths.

## Feature Support Matrix

### Fully Supported

| Feature | Notes |
|---------|-------|
| Core language | All syntax, types, operators |
| Variables & functions | Full support |
| Objects & arrays | Full support |
| Control flow | if/else, while, for, loop, switch |
| Pattern matching | Full support |
| Type annotations | Full support |
| Closures | Full support |
| Async/await | pthreads-based |
| Channels | Full support |
| File I/O | Full support |
| Math operations | Full support |
| String operations | Full support |
| FFI (basic) | DLL loading via LoadLibrary |

### Limited Support

| Feature | Limitations |
|---------|-------------|
| Signals | Only SIGINT, SIGTERM, SIGABRT supported |
| Socket timeouts | Uses Windows-specific DWORD milliseconds |
| Non-blocking sockets | Uses ioctlsocket instead of fcntl |
| Process functions | `fork()` returns -1, `getppid()` returns 0 |
| Terminal control | termios not supported |

### Not Supported

| Feature | Reason |
|---------|--------|
| fork() | Windows doesn't have fork |
| Unix signals (SIGUSR1, etc.) | Not available on Windows |
| termios | No equivalent on Windows |
| Unix domain sockets | Not available on Windows |
| /dev/null, /dev/urandom | Use NUL and CryptoAPI instead |

## API Differences

### Socket Functions

```c
// Windows uses closesocket() instead of close() for sockets
hml_closesocket(sock);  // Cross-platform macro

// Windows uses ioctlsocket for non-blocking mode
#ifdef HML_WINDOWS
u_long mode = 1;
ioctlsocket(fd, FIONBIO, &mode);
#else
fcntl(fd, F_SETFL, O_NONBLOCK);
#endif

// Windows socket timeouts are in milliseconds (DWORD)
#ifdef HML_WINDOWS
DWORD timeout_ms = 5000;
setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout_ms, sizeof(timeout_ms));
#else
struct timeval timeout = {5, 0};
setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
```

### Winsock Initialization

Socket operations automatically initialize Winsock2 on first use:

```c
hml_winsock_init();  // Called automatically by socket_create
```

### Dynamic Library Loading

FFI automatically translates library names:
- `libc.so.6` -> `msvcrt.dll`
- `libfoo.so` -> `foo.dll`

### Path Handling

- Use either `/` or `\` as path separators (both work)
- `PATH_MAX` is mapped to `MAX_PATH` (260 characters)
- `realpath()` uses `GetFullPathNameA()`

## Test Skipping

The test runner automatically skips Windows-incompatible tests:
- Signal handler tests
- Fork-based tests
- Unix socket tests
- Termios tests

Run tests with:
```bash
./tests/run_tests.sh
```

Skipped tests are reported with `(skipped on Windows)`.

## Known Issues

1. **Console color codes**: ANSI escape sequences may not work in older Windows terminals. Use Windows Terminal or enable VT100 processing.

2. **Path length**: Windows has a 260 character path limit by default. Long paths may cause issues.

3. **pthread_cancel**: Not supported on Windows. Use cooperative cancellation patterns instead.

4. **open_memstream**: Not available. Uses tmpfile() fallback which may be slower.

## Contributing

When adding new features:

1. Check if the feature uses POSIX-specific APIs
2. Add `#ifdef HML_WINDOWS` guards for Windows-specific code
3. Provide equivalent Windows functionality where possible
4. Document any limitations in this file
5. Add appropriate tests to the skip list if needed

## Compatibility Macros

The following macros are available for cross-platform code:

```c
#ifdef HML_WINDOWS
    // Windows-specific code
#else
    // POSIX code
#endif

// Platform-independent socket operations
hml_socket_t        // SOCKET on Windows, int on POSIX
HML_INVALID_SOCKET  // INVALID_SOCKET on Windows, -1 on POSIX
hml_closesocket(s)  // closesocket() on Windows, close() on POSIX
hml_socket_error()  // WSAGetLastError() on Windows, errno on POSIX

// Platform-independent directory operations
hml_dir_t           // Custom type on Windows, DIR on POSIX
hml_dirent_t        // Custom type on Windows, struct dirent on POSIX
hml_opendir(path)   // FindFirstFile on Windows, opendir on POSIX
hml_readdir(dir)    // FindNextFile on Windows, readdir on POSIX
hml_closedir(dir)   // FindClose on Windows, closedir on POSIX
```
