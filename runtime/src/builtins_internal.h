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
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <time.h>
#include <limits.h>
#include <inttypes.h>

// Thread stack size for spawned tasks (must match HML_THREAD_STACK_SIZE in hemlock_limits.h)
// 16 MB gives headroom for deeply nested call stacks in spawned tasks.
#ifndef HML_THREAD_STACK_SIZE
#define HML_THREAD_STACK_SIZE (16 * 1024 * 1024)
#endif

#ifdef __EMSCRIPTEN__
// WASM build: minimal POSIX headers via Emscripten
#include <emscripten.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
// POSIX spellings of the platform macros from hemlock_platform.h (which
// the WASM build does not include)
#define hml_mkdir(path, mode) mkdir(path, mode)
#elif defined(_WIN32)
// Windows (MinGW-w64) build: winsock + platform shims from hemlock_platform.h,
// plus the POSIX-ish headers MinGW does provide.
// Relative path: harnesses that rebuild the runtime with their own CFLAGS
// (e.g. tests/stress/run_stress.sh) don't pass -I../include.
#include "../../include/hemlock_platform.h"
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#ifndef HEMLOCK_NO_FFI
#include <ffi.h>
#endif
#else
// Native build: full POSIX headers
#include "../../include/hemlock_platform.h"
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <dirent.h>
#include <dlfcn.h>
#ifndef HEMLOCK_NO_FFI
#include <ffi.h>
#endif
#include <pwd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#endif // __EMSCRIPTEN__

#ifndef __EMSCRIPTEN__
#ifdef HML_HAVE_ZLIB
#include <zlib.h>
#endif

#ifndef HEMLOCK_NO_OPENSSL
// OpenSSL for cryptographic functions
#include <openssl/sha.h>
#include <openssl/md5.h>
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#endif
#endif // !__EMSCRIPTEN__

#ifdef __linux__
#ifndef __EMSCRIPTEN__
#include <sys/sysinfo.h>
#endif
#endif

#ifdef __APPLE__
#include <sys/sysctl.h>
#include <mach/mach.h>
#endif

// ========== GLOBAL STATE (defined in builtins_core.c) ==========

extern int g_argc;
extern char **g_argv;
extern __thread HmlExceptionContext *g_exception_stack;

// Defer stack
typedef struct DeferEntry {
    HmlDeferFn fn;
    void *arg;
    struct DeferEntry *next;
} DeferEntry;

extern __thread DeferEntry *g_defer_stack;

// Random seed state (defined in builtins_math.c)
extern int g_rand_seeded;

// OpenSSL includes (for crypto module)
#if !defined(__EMSCRIPTEN__) && !defined(HEMLOCK_NO_OPENSSL)
#include <openssl/ssl.h>
#include <openssl/crypto.h>
#endif

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

// Store a freshly created (+1) value in an object field. set_field retains,
// so the creation reference must be dropped or the value leaks.
static inline void hml_object_set_field_owned(HmlValue obj, const char *field, HmlValue val) {
    hml_object_set_field(obj, field, val);
    hml_release(&val);
}

// value.c: whether a pooled object's fields are still the pool's inline array.
int hml_obj_fields_in_pool_storage(HmlObject *obj);

// Allocate the output buffer for a string concatenation of `total` bytes
// (plus the NUL). Lengths are int, so sums must be computed in 64 bits and
// checked here; mirrors the interpreter's fatal string_concat() overflow.
static inline char *hml_concat_alloc(int64_t total) {
    if (total < 0 || total > INT_MAX - 1) {
        hml_fatal_error("String concatenation overflow - result too large");
    }
    char *buf = malloc((size_t)total + 1);
    if (!buf) {
        hml_fatal_error("Memory allocation failed");
    }
    return buf;
}

#endif // HEMLOCK_BUILTINS_INTERNAL_H
