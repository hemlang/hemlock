# Hemlock on Windows

This document describes Windows-specific build requirements, limitations, and compatibility notes.

## Build Requirements

- **MinGW-w64** (GCC for Windows) - tested with Strawberry Perl's MinGW
- **libffi** - for FFI support
- **zlib** - for compression
- **OpenSSL** - for crypto (sha256, sha512, md5)
- **pthreads** - for async/concurrency

## Building

```bash
make
```

This produces:
- `hemlock.exe` - the interpreter
- `hemlockc.exe` - the compiler
- `libhemlock_runtime.a` - runtime library for compiled programs

## Windows Limitations

### Not Supported (Stubbed)

These features return errors or stubs on Windows:

| Feature | Limitation | Workaround |
|---------|------------|------------|
| **POSIX Regex** | `regex.h` not available | Use string methods or external library |
| **fork()** | Always returns -1 | Use `exec()` with separate process |
| **waitpid()** | Returns -1 | Process control limited |
| **kill()** | Returns -1 | Use Windows TerminateProcess via FFI |
| **getppid()** | Returns 0 | Not available on Windows |
| **getuid()/getgid()** | Returns 0 | Windows doesn't have Unix UIDs |
| **ECDSA Crypto** | Requires OpenSSL 3.0+ | Use external crypto library or upgrade OpenSSL |
| **Terminal Raw Mode (termios)** | Not available | Use Windows Console API via FFI |

### Partially Supported

| Feature | Notes |
|---------|-------|
| **Signals** | Only basic signals (SIGINT, SIGTERM, etc.) |
| **poll()** | Uses WSAPoll (Vista+), may behave differently |
| **exec()** | Uses `_popen()`, shell metacharacters work differently |
| **File paths** | Both `/` and `\` work, but prefer `/` or use `path.join()` |

### Fully Supported

- Interpreter execution (`hemlock.exe`)
- Compiler C code generation (`hemlockc.exe -c`)
- Type checking (`hemlockc.exe --check`)
- Async/await and channels
- Networking (sockets, HTTP)
- File I/O
- Memory management (alloc/free)
- FFI (with Windows DLLs)
- SHA256, SHA512, MD5 hashing
- Compression (zlib, gzip)
- Most stdlib modules

## Technical Notes

### Header Include Order

Windows requires careful header include order:
1. `_WIN32_WINNT` must be defined BEFORE any Windows headers
2. `winsock2.h` MUST be included BEFORE `windows.h`

This is handled by `runtime/include/hemlock_compat.h`.

### API Version

The build targets Windows Vista+ (`_WIN32_WINNT=0x0600`) to enable:
- WSAPoll for socket polling
- GetTickCount64 for uptime
- Other Vista+ APIs

### Compatibility Files

Windows-specific compatibility code is located in:
- `runtime/include/hemlock_compat.h` - Platform detection and Windows header setup
- `src/backends/interpreter/builtins/internal.h` - Interpreter builtins compatibility
- `runtime/src/builtins_internal.h` - Runtime library compatibility
- Individual source files with `#ifdef HML_WINDOWS` blocks

## Testing on Windows

Some tests may fail or behave differently on Windows:
- Tests using regex will fail
- Tests using fork/waitpid will fail
- Tests relying on Unix signals may behave differently
- Path separator tests may need adjustment

Run tests with:
```bash
timeout 60 make test  # Use timeout to handle potential hangs
```

## Known Issues

1. **Console encoding**: Windows console may not display UTF-8 correctly by default. Use `chcp 65001` to enable UTF-8.

2. **Line endings**: Git may convert line endings. Configure with:
   ```bash
   git config core.autocrlf input
   ```

3. **Long paths**: Windows has a 260-character path limit by default. Enable long paths in Windows settings if needed.
