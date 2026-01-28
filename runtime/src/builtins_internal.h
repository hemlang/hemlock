/*
 * Hemlock Runtime Library - Internal Header for Builtins
 *
 * Shared declarations used across builtin module files.
 */

#ifndef HEMLOCK_BUILTINS_INTERNAL_H
#define HEMLOCK_BUILTINS_INTERNAL_H

#include "../include/hemlock_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <time.h>
#include <limits.h>
#include <ffi.h>

// ========== WINDOWS COMPATIBILITY ==========
#ifdef HML_WINDOWS
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <io.h>
    #include <process.h>
    #include <direct.h>

    // Windows doesn't have unistd.h - provide compatibility
    #define access _access
    #define F_OK 0
    #define R_OK 4
    #define W_OK 2
    #define X_OK 1

    // Windows directory functions
    #define getcwd _getcwd
    #define chdir _chdir
    #define mkdir(path, mode) _mkdir(path)
    #define rmdir _rmdir

    // Windows file descriptor functions
    #define close _close
    #define read _read
    #define write _write
    #define dup _dup
    #define dup2 _dup2
    #define fileno _fileno
    #define isatty _isatty

    // Windows process functions
    #define getpid _getpid

    // Windows doesn't have these - stub them
    #define getppid() 0
    #define getuid() 0
    #define getgid() 0
    #define geteuid() 0
    #define getegid() 0
    #define fork() (-1)

    // Socket compatibility
    typedef SOCKET hml_socket_t;
    #define HML_INVALID_SOCKET INVALID_SOCKET
    #define hml_closesocket(s) closesocket(s)
    #define hml_socket_error() WSAGetLastError()

    // poll() compatibility - use WSAPoll on Windows
    // Note: MinGW's winsock2.h provides POLLIN/POLLOUT, so we don't redefine them
    #define poll WSAPoll

    // usleep compatibility (Windows uses Sleep with milliseconds)
    // MinGW provides usleep via unistd.h, but we provide a fallback
    #ifndef usleep
    static inline void hml_usleep(unsigned int usec) {
        Sleep(usec / 1000);
    }
    #define usleep hml_usleep
    #endif

    // MinGW provides nanosleep and clock_gettime via pthread_time.h
    // No need to redefine them

    // realpath compatibility for Windows
    static inline char* hml_realpath(const char *path, char *resolved_path) {
        DWORD len = GetFullPathNameA(path, PATH_MAX, resolved_path, NULL);
        if (len == 0 || len > PATH_MAX) return NULL;
        // Check if the file/directory actually exists
        DWORD attrs = GetFileAttributesA(resolved_path);
        if (attrs == INVALID_FILE_ATTRIBUTES) return NULL;
        return resolved_path;
    }
    #define realpath hml_realpath

    // Dynamic library loading compatibility
    #define dlopen(path, mode) ((void*)LoadLibraryA(path))
    #define dlsym(handle, name) ((void*)GetProcAddress((HMODULE)(handle), name))
    #define dlclose(handle) (FreeLibrary((HMODULE)(handle)) ? 0 : -1)
    static inline const char* dlerror(void) {
        static char buf[256];
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
                       0, buf, sizeof(buf), NULL);
        return buf;
    }
    #define RTLD_LAZY 0
    #define RTLD_NOW 0

    // Directory entry compatibility
    typedef struct hml_dirent {
        char d_name[260];  // MAX_PATH
    } hml_dirent_t;

    typedef struct hml_dir {
        HANDLE hFind;
        WIN32_FIND_DATAA data;
        int first;
        hml_dirent_t entry;
    } hml_dir_t;

    static inline hml_dir_t* hml_opendir(const char *path) {
        hml_dir_t *dir = malloc(sizeof(hml_dir_t));
        if (!dir) return NULL;

        char search_path[MAX_PATH];
        snprintf(search_path, MAX_PATH, "%s\\*", path);

        dir->hFind = FindFirstFileA(search_path, &dir->data);
        if (dir->hFind == INVALID_HANDLE_VALUE) {
            free(dir);
            return NULL;
        }
        dir->first = 1;
        return dir;
    }

    static inline hml_dirent_t* hml_readdir(hml_dir_t *dir) {
        if (!dir) return NULL;
        if (dir->first) {
            dir->first = 0;
            strncpy(dir->entry.d_name, dir->data.cFileName, 260);
            return &dir->entry;
        }
        if (FindNextFileA(dir->hFind, &dir->data)) {
            strncpy(dir->entry.d_name, dir->data.cFileName, 260);
            return &dir->entry;
        }
        return NULL;
    }

    static inline void hml_closedir(hml_dir_t *dir) {
        if (dir) {
            FindClose(dir->hFind);
            free(dir);
        }
    }

    // strndup compatibility for Windows
    static inline char* hml_strndup(const char *s, size_t n) {
        size_t len = strnlen(s, n);
        char *result = malloc(len + 1);
        if (result) {
            memcpy(result, s, len);
            result[len] = '\0';
        }
        return result;
    }
    #ifndef strndup
    #define strndup hml_strndup
    #endif

    // getline compatibility for Windows
    static inline ssize_t hml_getline(char **lineptr, size_t *n, FILE *stream) {
        if (!lineptr || !n || !stream) return -1;

        size_t pos = 0;
        int c;

        if (*lineptr == NULL || *n == 0) {
            *n = 128;
            *lineptr = malloc(*n);
            if (!*lineptr) return -1;
        }

        while ((c = fgetc(stream)) != EOF) {
            if (pos + 1 >= *n) {
                size_t new_size = *n * 2;
                char *new_ptr = realloc(*lineptr, new_size);
                if (!new_ptr) return -1;
                *lineptr = new_ptr;
                *n = new_size;
            }
            (*lineptr)[pos++] = (char)c;
            if (c == '\n') break;
        }

        if (pos == 0 && c == EOF) return -1;
        (*lineptr)[pos] = '\0';
        return (ssize_t)pos;
    }
    #ifndef getline
    #define getline hml_getline
    #endif

    // MinGW provides ssize_t via corecrt.h

#else
    // POSIX systems
    #include <signal.h>
    #include <unistd.h>
    #include <sys/wait.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <sys/utsname.h>
    #include <dirent.h>
    #include <dlfcn.h>
    #include <pwd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <fcntl.h>
    #include <poll.h>

    // POSIX socket compatibility
    typedef int hml_socket_t;
    #define HML_INVALID_SOCKET (-1)
    #define hml_closesocket(s) close(s)
    #define hml_socket_error() errno

    // POSIX directory compatibility (use native)
    typedef struct dirent hml_dirent_t;
    typedef DIR hml_dir_t;
    #define hml_opendir opendir
    #define hml_readdir readdir
    #define hml_closedir closedir
#endif

#ifdef HML_HAVE_ZLIB
#include <zlib.h>
#endif

// OpenSSL for cryptographic functions
#include <openssl/sha.h>
#include <openssl/md5.h>
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/err.h>

#ifdef __linux__
#include <sys/sysinfo.h>
#endif

#ifdef __APPLE__
#include <sys/sysctl.h>
#include <mach/mach.h>
#endif

// ========== GLOBAL STATE (defined in builtins_core.c) ==========

extern int g_argc;
extern char **g_argv;
extern HmlExceptionContext *g_exception_stack;

// Defer stack
typedef struct DeferEntry {
    HmlDeferFn fn;
    void *arg;
    struct DeferEntry *next;
} DeferEntry;

extern DeferEntry *g_defer_stack;

// Random seed state (defined in builtins_math.c)
extern int g_rand_seeded;

// OpenSSL includes (for crypto module)
#include <openssl/ssl.h>
#include <openssl/crypto.h>

// ========== HELPER FUNCTIONS (defined in builtins_core.c) ==========

// Print a value to a file stream (used by print, eprint, etc.)
void print_value_to(FILE *out, HmlValue val);

// UTF-8 encoding helper
int utf8_encode_rune(uint32_t codepoint, char *out);

// Type checking helpers
int hml_is_integer_type(HmlValue val);
int hml_is_float_type(HmlValue val);
int64_t hml_val_to_int64(HmlValue val);
double hml_val_to_double(HmlValue val);

// Type promotion helper (used by binary ops, defined in builtins_ops.c)
// Note: type_priority() and promote_types() are now static inline in builtins_ops.c,
// using the shared type_promotion module for the actual logic.
HmlValue make_int_result(HmlValueType result_type, int64_t value);

// UTF-8 encoder (used by string operations)
int encode_utf8(uint32_t cp, char *out);

// Sandbox functions (defined in builtins_core.c)
void hml_sandbox_init(int flags, const char *root_path);
int hml_sandbox_check(int restriction_flag);
int hml_sandbox_path_allowed(const char *path, int is_write);
void hml_sandbox_error(const char *operation);

// ========== BUILTIN WRAPPER MACRO ==========

// Macro to reduce boilerplate for simple 1-arg builtin wrappers
#define DEFINE_BUILTIN_WRAPPER_0(name) \
    HmlValue hml_builtin_##name(HmlClosureEnv *env) { \
        (void)env; \
        return hml_##name(); \
    }

#define DEFINE_BUILTIN_WRAPPER_1(name) \
    HmlValue hml_builtin_##name(HmlClosureEnv *env, HmlValue arg1) { \
        (void)env; \
        return hml_##name(arg1); \
    }

#define DEFINE_BUILTIN_WRAPPER_2(name) \
    HmlValue hml_builtin_##name(HmlClosureEnv *env, HmlValue arg1, HmlValue arg2) { \
        (void)env; \
        return hml_##name(arg1, arg2); \
    }

#define DEFINE_BUILTIN_WRAPPER_3(name) \
    HmlValue hml_builtin_##name(HmlClosureEnv *env, HmlValue arg1, HmlValue arg2, HmlValue arg3) { \
        (void)env; \
        return hml_##name(arg1, arg2, arg3); \
    }

#endif // HEMLOCK_BUILTINS_INTERNAL_H
