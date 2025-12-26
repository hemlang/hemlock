# Windows Support Plan for Hemlock

> Comprehensive plan for porting Hemlock to Windows

## Executive Summary

Hemlock currently targets POSIX systems (Linux, macOS). This plan outlines the architectural changes needed to add Windows support while maintaining full parity with existing platforms.

**Estimated Complexity:** High
**Key Dependencies:** Winsock2, Windows Threads API, Windows Process API

---

## 1. Build System Changes

### Current State
- **Files:** `Makefile:1-445`, `runtime/Makefile:1-98`
- Uses GCC with `uname` for platform detection
- Links: pthread, libffi, libdl, libz, libcrypto

### Windows Requirements

| Current | Windows Replacement |
|---------|---------------------|
| GCC | MSVC (`cl.exe`) or MinGW-w64 |
| `uname` detection | `%OS%` or preprocessor `_WIN32` |
| `-lpthread` | Built-in (Windows threads) |
| `-ldl` | Built-in (kernel32) |
| `.so` libraries | `.dll` libraries |
| `libcrypto` | Windows CryptoAPI or OpenSSL |

### Implementation

Create `Makefile.windows` or add conditional blocks:

```makefile
ifeq ($(OS),Windows_NT)
    CC = cl
    CFLAGS = /D_WIN32 /std:c11 /W3
    LDFLAGS = ws2_32.lib kernel32.lib advapi32.lib
    EXT = .exe
    LIB_EXT = .dll
else
    # Existing POSIX build
endif
```

---

## 2. Threading/Concurrency (CRITICAL)

### Current State
- **File:** `src/backends/interpreter/builtins/concurrency.c:8-506`
- **File:** `runtime/src/builtins_async.c:8-10`
- Uses pthreads exclusively

### POSIX → Windows API Mapping

| POSIX | Windows | Notes |
|-------|---------|-------|
| `pthread_t` | `HANDLE` | Thread handle |
| `pthread_create()` | `CreateThread()` | |
| `pthread_join()` | `WaitForSingleObject()` | |
| `pthread_detach()` | `CloseHandle()` | No direct equivalent |
| `pthread_mutex_t` | `CRITICAL_SECTION` | |
| `pthread_mutex_lock/unlock` | `EnterCriticalSection/LeaveCriticalSection` | |
| `pthread_cond_t` | `CONDITION_VARIABLE` | Vista+ |
| `pthread_cond_wait()` | `SleepConditionVariableCS()` | |
| `pthread_cond_signal()` | `WakeConditionVariable()` | |
| `pthread_sigmask()` | N/A | Signals don't exist on Windows |
| `nanosleep()` | `Sleep()` | Millisecond precision only |
| `<stdatomic.h>` | `Interlocked*` functions | Or keep stdatomic with MSVC |

### Specific Code Locations

```
concurrency.c:14-21   - Signal mask setup (remove on Windows)
concurrency.c:49      - pthread_mutex_init
concurrency.c:91      - pthread_cond_init
concurrency.c:118     - pthread_create
concurrency.c:143     - pthread_mutex_lock
concurrency.c:160     - pthread_mutex_unlock
concurrency.c:164     - pthread_join
concurrency.c:241     - pthread_detach
concurrency.c:464     - nanosleep
concurrency.c:484     - mutex operations
```

### Implementation Strategy

Create `src/compat/threading.h`:

```c
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>

    typedef HANDLE hml_thread_t;
    typedef CRITICAL_SECTION hml_mutex_t;
    typedef CONDITION_VARIABLE hml_cond_t;

    #define hml_mutex_init(m) InitializeCriticalSection(m)
    #define hml_mutex_lock(m) EnterCriticalSection(m)
    #define hml_mutex_unlock(m) LeaveCriticalSection(m)
    #define hml_mutex_destroy(m) DeleteCriticalSection(m)

    static inline int hml_thread_create(hml_thread_t *t, void *(*fn)(void*), void *arg) {
        *t = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)fn, arg, 0, NULL);
        return *t ? 0 : -1;
    }

    static inline int hml_thread_join(hml_thread_t t, void **retval) {
        WaitForSingleObject(t, INFINITE);
        CloseHandle(t);
        return 0;
    }

    static inline void hml_sleep_ms(unsigned int ms) {
        Sleep(ms);
    }
#else
    #include <pthread.h>
    #include <time.h>

    typedef pthread_t hml_thread_t;
    typedef pthread_mutex_t hml_mutex_t;
    typedef pthread_cond_t hml_cond_t;

    #define hml_mutex_init(m) pthread_mutex_init(m, NULL)
    #define hml_mutex_lock(m) pthread_mutex_lock(m)
    #define hml_mutex_unlock(m) pthread_mutex_unlock(m)
    #define hml_mutex_destroy(m) pthread_mutex_destroy(m)
    #define hml_thread_create(t, fn, arg) pthread_create(t, NULL, fn, arg)
    #define hml_thread_join(t, r) pthread_join(t, r)

    static inline void hml_sleep_ms(unsigned int ms) {
        struct timespec ts = { ms / 1000, (ms % 1000) * 1000000 };
        nanosleep(&ts, NULL);
    }
#endif
```

---

## 3. Process Management (CRITICAL)

### Current State
- **File:** `src/backends/interpreter/builtins/env.c:98-340`
- Uses fork/exec model

### POSIX → Windows API Mapping

| POSIX | Windows | Notes |
|-------|---------|-------|
| `fork()` | N/A | No equivalent |
| `execvp()` | `CreateProcess()` | Combined with fork |
| `fork()+exec()` | `CreateProcess()` | Single call |
| `popen()` | `CreateProcess()` + pipes | Custom implementation |
| `pclose()` | `CloseHandle()` | |
| `wait()`/`waitpid()` | `WaitForSingleObject()` | |
| `WIFEXITED()` | `GetExitCodeProcess()` | |
| `getpid()` | `GetCurrentProcessId()` | |
| `getppid()` | Complex (WMI/toolhelp) | Limited support |
| `kill()` | `TerminateProcess()` | No signal delivery |
| `getuid()`/`getgid()` | `GetUserName()` | Different model |

### Specific Code Locations

```
env.c:124    - getpid()
env.c:166    - popen()
env.c:217    - pclose() + WIFEXITED/WEXITSTATUS
env.c:307    - fork()
env.c:329    - execvp()
```

### Implementation Strategy

Create `src/compat/process.h`:

```c
#ifdef _WIN32
    #include <windows.h>

    typedef struct {
        HANDLE hProcess;
        HANDLE hThread;
        DWORD dwProcessId;
    } hml_process_t;

    // fork+exec replacement
    int hml_spawn(hml_process_t *proc, const char *cmd, char *const argv[]);
    int hml_wait(hml_process_t *proc, int *exit_code);
    int hml_kill(hml_process_t *proc);

    // popen replacement
    typedef struct {
        HANDLE hProcess;
        HANDLE hPipeRead;
        HANDLE hPipeWrite;
    } hml_popen_t;

    hml_popen_t *hml_popen(const char *command, const char *mode);
    int hml_pclose(hml_popen_t *p);

    #define hml_getpid() GetCurrentProcessId()
#else
    #include <unistd.h>
    #include <sys/wait.h>

    #define hml_getpid() getpid()
    // Use existing fork/exec on POSIX
#endif
```

---

## 4. File System Operations (CRITICAL)

### Current State
- **File:** `src/backends/interpreter/builtins/filesystem.c:1-150`
- **File:** `src/backends/interpreter/builtins/directories.c:1-150`
- Uses POSIX file APIs

### POSIX → Windows API Mapping

| POSIX | Windows | Notes |
|-------|---------|-------|
| `open()` | `CreateFileA()` | |
| `close()` | `CloseHandle()` | |
| `read()` | `ReadFile()` | |
| `write()` | `WriteFile()` | |
| `stat()` | `GetFileAttributesEx()` | |
| `mkdir()` | `CreateDirectoryA()` | |
| `rmdir()` | `RemoveDirectoryA()` | |
| `opendir()`/`readdir()` | `FindFirstFile()`/`FindNextFile()` | |
| `getcwd()` | `GetCurrentDirectoryA()` | |
| `chdir()` | `SetCurrentDirectoryA()` | |
| `unlink()` | `DeleteFileA()` | |
| Path separator `/` | `\` (but `/` often works) | |

### Specific Code Locations

```
filesystem.c:25     - stat()
filesystem.c:52     - open() with O_RDONLY, O_NOFOLLOW
filesystem.c:66     - fdopen()
filesystem.c:70     - close()
filesystem.c:130    - open() with O_WRONLY, O_CREAT, O_TRUNC
directories.c:32    - mkdir()
directories.c:65    - rmdir()
directories.c:98    - opendir()
directories.c:110   - readdir()
directories.c:118   - closedir()
directories.c:131   - getcwd()
```

### Implementation Strategy

Use C standard library where possible (`fopen`, `fclose`, `fread`, `fwrite`), add compatibility layer for directory operations:

```c
#ifdef _WIN32
    #include <windows.h>

    typedef struct {
        HANDLE hFind;
        WIN32_FIND_DATAA data;
        int first;
    } hml_dir_t;

    hml_dir_t *hml_opendir(const char *path);
    const char *hml_readdir(hml_dir_t *dir);
    void hml_closedir(hml_dir_t *dir);

    int hml_mkdir(const char *path, int mode);
    int hml_rmdir(const char *path);
    char *hml_getcwd(char *buf, size_t size);
#else
    #include <dirent.h>
    #include <sys/stat.h>

    typedef DIR hml_dir_t;
    #define hml_opendir(p) opendir(p)
    #define hml_readdir(d) (readdir(d) ? readdir(d)->d_name : NULL)
    #define hml_closedir(d) closedir(d)
    #define hml_mkdir(p, m) mkdir(p, m)
    #define hml_rmdir(p) rmdir(p)
    #define hml_getcwd(b, s) getcwd(b, s)
#endif
```

---

## 5. Networking (CRITICAL)

### Current State
- **File:** `src/backends/interpreter/builtins/net.c:1-900+`
- **File:** `runtime/src/builtins_socket.c:1-150`
- Uses BSD sockets

### POSIX → Windows API Mapping

| POSIX | Windows (Winsock2) | Notes |
|-------|-------------------|-------|
| `socket()` | `socket()` | Same API |
| `bind()` | `bind()` | Same API |
| `listen()` | `listen()` | Same API |
| `accept()` | `accept()` | Same API |
| `connect()` | `connect()` | Same API |
| `send()`/`recv()` | `send()`/`recv()` | Same API |
| `close(fd)` | `closesocket(sock)` | **Different!** |
| `poll()` | `WSAPoll()` | Vista+ |
| `errno` | `WSAGetLastError()` | **Different!** |
| N/A | `WSAStartup()` | **Required at init** |
| N/A | `WSACleanup()` | **Required at shutdown** |

### Specific Code Locations

```
net.c:49      - close() on socket
net.c:86      - socket()
net.c:124     - inet_ntop()
net.c:138     - inet_pton()
net.c:142     - bind()
net.c:145     - gethostbyname() [DEPRECATED - change to getaddrinfo()]
net.c:900     - poll()
main.c        - Need WSAStartup() at init
```

### Implementation Strategy

Create `src/compat/socket.h`:

```c
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")

    typedef SOCKET hml_socket_t;
    #define HML_INVALID_SOCKET INVALID_SOCKET
    #define hml_closesocket(s) closesocket(s)
    #define hml_socket_error() WSAGetLastError()

    static inline int hml_socket_init(void) {
        WSADATA wsa;
        return WSAStartup(MAKEWORD(2, 2), &wsa);
    }

    static inline void hml_socket_cleanup(void) {
        WSACleanup();
    }
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <poll.h>
    #include <unistd.h>

    typedef int hml_socket_t;
    #define HML_INVALID_SOCKET -1
    #define hml_closesocket(s) close(s)
    #define hml_socket_error() errno
    #define hml_socket_init() 0
    #define hml_socket_cleanup()
#endif
```

**Critical:** Replace `gethostbyname()` with `getaddrinfo()` (works on both platforms).

---

## 6. Signal Handling (MODERATE)

### Current State
- **File:** `src/backends/interpreter/builtins/signals.c:1-136`
- Uses POSIX signals

### Windows Limitations

Windows does **not** have POSIX signals. Available options:

| Feature | Windows Support |
|---------|-----------------|
| SIGINT (Ctrl+C) | `SetConsoleCtrlHandler()` |
| SIGTERM | Limited |
| SIGKILL | `TerminateProcess()` |
| SIGUSR1/SIGUSR2 | **Not available** |
| SIGALRM | **Not available** |
| Custom handlers | Limited to console events |

### Specific Code Locations

```
signals.c:87-102   - signal() / sigaction()
signals.c:89       - sigemptyset()
signals.c:90       - SA_RESTART
internal.h:36-40   - Signal constant definitions
```

### Implementation Strategy

Create `src/compat/signals.h`:

```c
#ifdef _WIN32
    #include <windows.h>

    // Map Hemlock signal numbers to console events
    #define HML_SIGINT  0  // Ctrl+C
    #define HML_SIGTERM 1  // Close

    typedef void (*hml_signal_handler_t)(int);

    int hml_signal(int sig, hml_signal_handler_t handler);
    int hml_raise(int sig);

    // Implementation uses SetConsoleCtrlHandler internally
#else
    #include <signal.h>

    #define HML_SIGINT  SIGINT
    #define HML_SIGTERM SIGTERM
    // ... other signals

    #define hml_signal(s, h) signal(s, h)
    #define hml_raise(s) raise(s)
#endif
```

**Note:** Document that SIGUSR1/SIGUSR2 are not available on Windows.

---

## 7. FFI / Dynamic Loading (CRITICAL)

### Current State
- **File:** `src/backends/interpreter/ffi.c:1-900+`
- **File:** `runtime/src/builtins_ffi.c:1-100+`
- Uses dlopen/dlsym

### POSIX → Windows API Mapping

| POSIX | Windows | Notes |
|-------|---------|-------|
| `dlopen()` | `LoadLibraryA()` | |
| `dlsym()` | `GetProcAddress()` | |
| `dlclose()` | `FreeLibrary()` | |
| `dlerror()` | `GetLastError()` | Need FormatMessage |
| `.so` extension | `.dll` extension | |
| `RTLD_LAZY` | N/A | Always eager on Windows |

### Specific Code Locations

```
ffi.c:195    - dlopen()
ffi.c:233    - dlclose()
ffi.c:850    - dlsym()
```

### Implementation Strategy

Create `src/compat/dlfcn.h`:

```c
#ifdef _WIN32
    #include <windows.h>

    typedef HMODULE hml_lib_t;

    static inline hml_lib_t hml_dlopen(const char *path, int flags) {
        (void)flags;
        return LoadLibraryA(path);
    }

    static inline void *hml_dlsym(hml_lib_t lib, const char *name) {
        return (void*)GetProcAddress(lib, name);
    }

    static inline int hml_dlclose(hml_lib_t lib) {
        return FreeLibrary(lib) ? 0 : -1;
    }

    static inline const char *hml_dlerror(void) {
        static char buf[256];
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
                       0, buf, sizeof(buf), NULL);
        return buf;
    }
#else
    #include <dlfcn.h>

    typedef void *hml_lib_t;
    #define hml_dlopen(p, f) dlopen(p, f)
    #define hml_dlsym(l, n) dlsym(l, n)
    #define hml_dlclose(l) dlclose(l)
    #define hml_dlerror() dlerror()
#endif
```

---

## 8. Time Functions (MODERATE)

### Current State
- **File:** `src/backends/interpreter/builtins/time.c:1-279`
- **File:** `runtime/src/builtins_time.c:1-210`

### POSIX → Windows API Mapping

| POSIX | Windows | Notes |
|-------|---------|-------|
| `time()` | `time()` | Same |
| `gettimeofday()` | `GetSystemTimeAsFileTime()` | |
| `clock_gettime()` | `QueryPerformanceCounter()` | |
| `nanosleep()` | `Sleep()` | Millisecond precision |
| `localtime()` | `localtime()` | Same |
| `strftime()` | `strftime()` | Same |

### Specific Code Locations

```
time.c:16      - time()
time.c:27      - gettimeofday()
time.c:51      - nanosleep()
time.c:63      - clock()
concurrency.c:387,455 - clock_gettime()
```

### Implementation Strategy

Create `src/compat/time.h`:

```c
#ifdef _WIN32
    #include <windows.h>

    static inline int hml_gettimeofday(struct timeval *tv) {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        ULARGE_INTEGER uli;
        uli.LowPart = ft.dwLowDateTime;
        uli.HighPart = ft.dwHighDateTime;
        // Convert to Unix epoch (100-ns intervals since 1601 to microseconds since 1970)
        uli.QuadPart -= 116444736000000000ULL;
        tv->tv_sec = (long)(uli.QuadPart / 10000000);
        tv->tv_usec = (long)((uli.QuadPart % 10000000) / 10);
        return 0;
    }

    static inline void hml_nanosleep_ns(long long ns) {
        Sleep((DWORD)(ns / 1000000));  // Convert to ms
    }
#else
    #include <sys/time.h>
    #include <time.h>

    #define hml_gettimeofday(tv) gettimeofday(tv, NULL)

    static inline void hml_nanosleep_ns(long long ns) {
        struct timespec ts = { ns / 1000000000, ns % 1000000000 };
        nanosleep(&ts, NULL);
    }
#endif
```

---

## 9. OS Information (MODERATE)

### Current State
- **File:** `src/backends/interpreter/builtins/os.c:1-379`

### POSIX → Windows API Mapping

| POSIX | Windows | Notes |
|-------|---------|-------|
| `uname()` | `GetVersionEx()` + `GetSystemInfo()` | |
| `sysconf(_SC_NPROCESSORS_ONLN)` | `GetSystemInfo()` | |
| `sysinfo()` | `GlobalMemoryStatusEx()` | |
| `getpwuid()` | `GetUserNameA()` | |
| `gethostname()` | `gethostname()` | Same (Winsock) |

### Specific Code Locations

```
os.c:49      - uname()
os.c:69      - gethostname()
os.c:90-101  - getlogin_r() / getpwuid()
os.c:149     - sysconf(_SC_NPROCESSORS_ONLN)
os.c:166     - sysinfo()
os.c:189-190 - sysconf for memory
```

---

## 10. Required Compatibility Layer Structure

```
src/compat/
├── platform.h          # Master include with platform detection
├── threading.h         # pthread → Windows threads
├── threading.c         # Implementation (if needed)
├── process.h           # fork/exec → CreateProcess
├── process.c           # Implementation
├── filesystem.h        # Directory operations
├── filesystem.c        # Implementation
├── socket.h            # BSD sockets → Winsock2
├── dlfcn.h             # dlopen → LoadLibrary
├── signals.h           # POSIX signals → Console handlers
├── signals.c           # Implementation
├── time.h              # Time functions
└── osinfo.h            # OS detection
```

---

## 11. Compiler Backend Changes

### Current State
- **File:** `src/backends/compiler/codegen_program.c`
- **File:** `src/backends/compiler/main.c`
- Generates C code, compiles with GCC

### Required Changes

1. **Compiler selection:** Use MSVC or MinGW on Windows
2. **Include paths:** Windows SDK headers
3. **Link libraries:** ws2_32.lib, kernel32.lib
4. **Runtime library:** Build Windows-compatible libhemlock_runtime.lib

### Specific Code Locations

```
codegen_program.c   - Generated includes (#include <pthread.h>, etc.)
main.c              - GCC invocation for compilation
```

---

## 12. Implementation Phases

### Phase 1: Foundation (Critical)
1. Create `src/compat/` directory structure
2. Implement threading compatibility layer
3. Implement socket compatibility layer
4. Update Makefile for Windows/MinGW

### Phase 2: Core Functionality
5. Implement process management compatibility
6. Implement file system compatibility
7. Implement FFI compatibility (dlopen)
8. Add WSAStartup/Cleanup to main.c

### Phase 3: Polish
9. Implement time function compatibility
10. Implement signal compatibility (limited)
11. Implement OS info compatibility
12. Update compiler backend for Windows

### Phase 4: Testing
13. Create Windows-specific test suite
14. Run parity tests on Windows
15. Test async/threading extensively
16. Document Windows limitations

---

## 13. Known Limitations on Windows

1. **Signal handling:** Only SIGINT (Ctrl+C) and SIGTERM available
2. **Time precision:** nanosleep limited to millisecond precision
3. **Process model:** No fork() - spawn semantics only
4. **File paths:** `/` works but `\` is native
5. **UID/GID:** Concept doesn't exist - use username instead
6. **getppid():** Complex to implement reliably

---

## 14. Files Summary

### Files to Create
| File | Purpose |
|------|---------|
| `src/compat/platform.h` | Master platform detection |
| `src/compat/threading.h` | Thread abstraction |
| `src/compat/process.h` | Process abstraction |
| `src/compat/process.c` | Process implementation |
| `src/compat/filesystem.h` | Directory operations |
| `src/compat/filesystem.c` | Directory implementation |
| `src/compat/socket.h` | Socket abstraction |
| `src/compat/dlfcn.h` | Dynamic loading abstraction |
| `src/compat/signals.h` | Signal abstraction |
| `src/compat/signals.c` | Signal implementation |
| `src/compat/time.h` | Time abstraction |
| `src/compat/osinfo.h` | OS info abstraction |
| `Makefile.windows` | Windows build (or update Makefile) |

### Files to Modify
| File | Changes |
|------|---------|
| `src/backends/interpreter/builtins/concurrency.c` | Use threading.h |
| `src/backends/interpreter/builtins/env.c` | Use process.h |
| `src/backends/interpreter/builtins/filesystem.c` | Use filesystem.h |
| `src/backends/interpreter/builtins/directories.c` | Use filesystem.h |
| `src/backends/interpreter/builtins/net.c` | Use socket.h, replace gethostbyname |
| `src/backends/interpreter/builtins/signals.c` | Use signals.h |
| `src/backends/interpreter/builtins/time.c` | Use time.h |
| `src/backends/interpreter/builtins/os.c` | Use osinfo.h |
| `src/backends/interpreter/ffi.c` | Use dlfcn.h |
| `src/backends/interpreter/main.c` | Add socket init, platform includes |
| `src/backends/compiler/main.c` | Windows compiler invocation |
| `src/backends/compiler/codegen_program.c` | Platform-aware includes |
| `runtime/src/builtins_async.c` | Use threading.h |
| `runtime/src/builtins_time.c` | Use time.h |
| `runtime/src/builtins_socket.c` | Use socket.h |
| `runtime/src/builtins_ffi.c` | Use dlfcn.h |
| `Makefile` | Add Windows detection |
| `runtime/Makefile` | Add Windows detection |

---

## 15. Recommended Toolchain

For initial Windows support, recommend **MinGW-w64** because:
1. GCC-compatible (less Makefile changes)
2. POSIX-like environment
3. Can use some POSIX headers directly
4. Easier transition path

Later, add MSVC support for native Windows development.

---

## Appendix: Quick Reference

### Platform Detection Macro
```c
#if defined(_WIN32) || defined(_WIN64)
    #define HML_WINDOWS 1
#elif defined(__APPLE__)
    #define HML_MACOS 1
#else
    #define HML_LINUX 1
#endif
```

### Include Guards Pattern
```c
// In each compat header:
#ifndef HML_COMPAT_THREADING_H
#define HML_COMPAT_THREADING_H

#include "platform.h"

#ifdef HML_WINDOWS
    // Windows implementation
#else
    // POSIX implementation
#endif

#endif // HML_COMPAT_THREADING_H
```
