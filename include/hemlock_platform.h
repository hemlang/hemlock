// Hemlock platform compatibility layer.
//
// Centralizes the differences between POSIX systems (Linux, macOS) and
// Windows (MinGW-w64). Include this instead of reaching for platform
// headers directly when code needs sockets, dynamic loading, or process
// primitives.
//
// On Windows we target MinGW-w64 with the POSIX threading model
// (winpthreads), so <pthread.h>, <unistd.h>, <dirent.h>, <sys/stat.h>,
// <sys/time.h> and friends are available. What MinGW does NOT provide:
//   - <sys/wait.h>, <spawn.h>, fork()/exec*() process model
//   - <sys/socket.h>/<netinet/*> (use winsock2 instead)
//   - <sys/un.h> unix domain sockets
//   - <sys/mman.h> mmap
//   - <dlfcn.h> (shimmed below via LoadLibrary)
//   - <poll.h> (use WSAPoll for sockets)
//   - <termios.h>
#ifndef HEMLOCK_PLATFORM_H
#define HEMLOCK_PLATFORM_H

#ifdef _WIN32

#include <stddef.h>
#include "hemlock_compat.h"

// WSAPoll and friends need Vista+; make sure generated C that includes
// this header compiles even without -D_WIN32_WINNT on the command line
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0601
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

// winsock2.h must be included before windows.h pulls in winsock.h
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
// AF_UNIX (Windows 10 1803+). <afunix.h> only shipped with mingw-w64 6.0+,
// so declare the same three things ourselves on older toolchains (e.g. the
// mingw-w64 headers bundled with Strawberry Perl's GCC) — the struct is ABI,
// not implementation, so a local copy stays correct.
#if defined(__has_include)
#  if __has_include(<afunix.h>)
#    define HML_HAVE_AFUNIX_H 1
#  endif
#endif
#ifdef HML_HAVE_AFUNIX_H
#include <afunix.h>
#else
#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 108
#endif
struct sockaddr_un {
    ADDRESS_FAMILY sun_family;
    char sun_path[UNIX_PATH_MAX];
};
#ifndef AF_UNIX
#define AF_UNIX 1
#endif
#endif
#include <windows.h>
#include <io.h>
#include <direct.h>
#include <process.h>

// ---- dlfcn shim (implemented in src/shared/platform_win32.c and
// runtime/src/platform_win32.c via LoadLibrary/GetProcAddress) ----
#define RTLD_LAZY   0x1
#define RTLD_NOW    0x2
#define RTLD_GLOBAL 0x4
#define RTLD_LOCAL  0x8
void *dlopen(const char *filename, int flags);
void *dlsym(void *handle, const char *symbol);
int dlclose(void *handle);
char *dlerror(void);

// ---- One-time platform initialization (WSAStartup) ----
void hml_platform_init(void);

// Put stdout/stderr in binary mode so "\n" stays one byte (see
// src/shared/platform_win32.c). Runs from a constructor; also called
// explicitly from the runtime's startup path so the object is guaranteed
// to be pulled out of libhemlock_runtime.a.
void hml_win32_set_binary_stdio(void);

// ---- Process status macros used with system()/_cwait exit codes ----
#ifndef WIFEXITED
#define WIFEXITED(status)   (1)
#define WEXITSTATUS(status) (status)
#define WIFSIGNALED(status) (0)
#define WTERMSIG(status)    (0)
#endif

// ---- Misc POSIX spellings ----
#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif

// GetNativeSystemInfo()'s ARM64 value predates its appearance in some
// mingw-w64 winnt.h versions; the numeric value is fixed by the Win32 ABI.
#ifndef PROCESSOR_ARCHITECTURE_ARM64
#define PROCESSOR_ARCHITECTURE_ARM64 12
#endif

// mkdir(path, mode): Windows _mkdir takes no mode argument
#define hml_mkdir(path, mode) ((void)(mode), _mkdir(path))

// Windows sockets use a distinct descriptor space and closesocket()
#define hml_closesocket(fd) closesocket(fd)
typedef int hml_socklen_t;

// Socket errors live in WSAGetLastError(), not errno
#define hml_sock_errno() ((int)WSAGetLastError())
#define HML_SOCK_EINTR WSAEINTR
#define HML_SOCK_WOULDBLOCK(e) ((e) == WSAEWOULDBLOCK)
const char *hml_sock_strerror(int err);

// poll() over winsock's WSAPoll (Vista+; struct pollfd from winsock2.h)
#define poll(fds, nfds, timeout) WSAPoll((fds), (nfds), (timeout))
typedef ULONG nfds_t;

// mmap protection/advice constants (mmap is emulated on Windows)
#ifndef PROT_NONE
#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4
#endif
#ifndef MADV_NORMAL
#define MADV_NORMAL     0
#define MADV_RANDOM     1
#define MADV_SEQUENTIAL 2
#define MADV_WILLNEED   3
#define MADV_DONTNEED   4
#endif

// ---- mmap emulation over CreateFileMapping/VirtualAlloc ----
// (implemented in platform_win32.c)
#define MAP_FAILED ((void *)-1)
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS
#define MS_ASYNC      1
#define MS_INVALIDATE 2
#define MS_SYNC       4
void *mmap(void *addr, size_t length, int prot, int flags, int fd, long long offset);
int munmap(void *addr, size_t length);
int msync(void *addr, size_t length, int flags);
int madvise(void *addr, size_t length, int advice);
int mprotect(void *addr, size_t length, int prot);

// open() flags with no Windows equivalent (no symlink semantics in MSVCRT)
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

// setenv/unsetenv shims over _putenv_s (implemented in platform_win32.c)
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);

// POSIX signal numbers missing from the Windows CRT. Only the CRT's own
// signals (SIGINT, SIGTERM, SIGABRT, ...) can actually be handled; these
// exist so the @stdlib/signal constants resolve. signal()/raise() with
// them fails at runtime with EINVAL.
#ifndef SIGHUP
#define SIGHUP   1
#define SIGQUIT  3
#define SIGUSR1  10
#define SIGUSR2  12
#define SIGPIPE  13
#define SIGALRM  14
#define SIGCHLD  17
#define SIGCONT  18
#define SIGSTOP  19
#define SIGTSTP  20
#define SIGTTIN  26
#define SIGTTOU  27
#endif

#else // !_WIN32 ---------------------------------------------------------

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/wait.h>
#include <poll.h>
#include <dlfcn.h>

#define hml_platform_init() ((void)0)
#define hml_mkdir(path, mode) mkdir(path, mode)
#define hml_closesocket(fd) close(fd)
typedef socklen_t hml_socklen_t;

#define hml_sock_errno() (errno)
#define HML_SOCK_EINTR EINTR
#define HML_SOCK_WOULDBLOCK(e) ((e) == EAGAIN || (e) == EWOULDBLOCK)
#define hml_sock_strerror(err) strerror(err)

#endif // _WIN32

#endif // HEMLOCK_PLATFORM_H
