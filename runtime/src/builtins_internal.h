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
#include <signal.h>
#include <limits.h>

/* Platform detection */
#if defined(_WIN32) || defined(_WIN64) || defined(__MINGW32__) || defined(__MINGW64__)
#define HML_RT_WINDOWS 1
#else
#define HML_RT_POSIX 1
#endif

#ifdef HML_RT_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
#include <direct.h>
#include <process.h>
#include <sys/stat.h>
#include <sys/types.h>
#else
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
#endif

#include <ffi.h>

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

// Type promotion helpers (used by binary ops)
int type_priority(HmlValueType type);
HmlValueType promote_types(HmlValueType a, HmlValueType b);
HmlValue make_int_result(HmlValueType result_type, int64_t value);

// UTF-8 encoder (used by string operations)
int encode_utf8(uint32_t cp, char *out);

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
