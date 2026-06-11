/*
 * Hemlock Builtin Registry
 *
 * Single source of truth for all builtin functions.
 * Both interpreter and compiler include this header.
 *
 * Each builtin is defined with:
 *   - Name (string)
 *   - Arity (-1 for variadic)
 *   - Flags (HML_BUILTIN_* constants)
 *   - Return type hint (for type checker)
 */

#ifndef HEMLOCK_BUILTINS_REGISTRY_H
#define HEMLOCK_BUILTINS_REGISTRY_H

#include <stddef.h>  // NULL
#include <string.h>  // strcmp

// ========== BUILTIN FLAGS ==========

#define HML_BUILTIN_PUBLIC      0x01  // Exposed in global scope
#define HML_BUILTIN_INTERNAL    0x02  // Prefixed with __ for stdlib use
#define HML_BUILTIN_VARIADIC    0x04  // Variable number of arguments
#define HML_BUILTIN_NORETURN    0x08  // Never returns (panic, exit)
#define HML_BUILTIN_PURE        0x10  // No side effects (for optimization)

// ========== RETURN TYPE HINTS ==========
// These match the type checker's CheckedType primitives

typedef enum {
    HML_RET_NULL = 0,
    HML_RET_I32,
    HML_RET_I64,
    HML_RET_F64,
    HML_RET_BOOL,
    HML_RET_STRING,
    HML_RET_PTR,
    HML_RET_BUFFER,
    HML_RET_ARRAY,
    HML_RET_OBJECT,
    HML_RET_TASK,
    HML_RET_CHANNEL,
    HML_RET_FILE,
    HML_RET_ANY,        // Dynamic/unknown type
} HmlBuiltinReturnType;

// ========== BUILTIN METADATA ==========

typedef struct {
    const char *name;           // Function name
    int arity;                  // Expected args (-1 = variadic)
    int flags;                  // HML_BUILTIN_* flags
    HmlBuiltinReturnType ret;   // Return type hint
} HmlBuiltinInfo;

// ========== BUILTIN REGISTRY ==========
// X-macro for defining all builtins in one place
// Usage: Define HML_BUILTIN(name, arity, flags, return_type) before including

#define HML_BUILTINS_CORE(X) \
    /* I/O */ \
    X("print",      1,  HML_BUILTIN_PUBLIC | HML_BUILTIN_VARIADIC, HML_RET_NULL) \
    X("eprint",     1,  HML_BUILTIN_PUBLIC | HML_BUILTIN_VARIADIC, HML_RET_NULL) \
    X("read_line",  0,  HML_BUILTIN_PUBLIC, HML_RET_STRING) \
    \
    /* Introspection */ \
    X("typeof",     1,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_STRING) \
    X("typeid",     1,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_I32) \
    /* Note: len() is NOT a builtin - use .length or .byte_length property */ \
    \
    /* Memory */ \
    X("alloc",      1,  HML_BUILTIN_PUBLIC, HML_RET_PTR) \
    X("talloc",     1,  HML_BUILTIN_PUBLIC, HML_RET_PTR) \
    X("realloc",    2,  HML_BUILTIN_PUBLIC, HML_RET_PTR) \
    X("free",       1,  HML_BUILTIN_PUBLIC, HML_RET_NULL) \
    X("memset",     3,  HML_BUILTIN_PUBLIC, HML_RET_NULL) \
    X("memcpy",     3,  HML_BUILTIN_PUBLIC, HML_RET_NULL) \
    X("buffer",     1,  HML_BUILTIN_PUBLIC, HML_RET_BUFFER) \
    X("sizeof",     1,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_I32) \
    \
    /* Control */ \
    X("assert",     1,  HML_BUILTIN_PUBLIC | HML_BUILTIN_VARIADIC, HML_RET_NULL) \
    X("panic",      0,  HML_BUILTIN_PUBLIC | HML_BUILTIN_VARIADIC | HML_BUILTIN_NORETURN, HML_RET_NULL) \
    \
    /* Files */ \
    X("open",       2,  HML_BUILTIN_PUBLIC, HML_RET_FILE) \
    \
    /* Process */ \
    X("exec",       1,  HML_BUILTIN_PUBLIC, HML_RET_OBJECT) \
    X("exec_argv",  2,  HML_BUILTIN_PUBLIC, HML_RET_OBJECT) \
    \
    /* Concurrency */ \
    X("spawn",      1,  HML_BUILTIN_PUBLIC | HML_BUILTIN_VARIADIC, HML_RET_TASK) \
    X("join",       1,  HML_BUILTIN_PUBLIC, HML_RET_ANY) \
    X("detach",     1,  HML_BUILTIN_PUBLIC, HML_RET_NULL) \
    X("channel",    1,  HML_BUILTIN_PUBLIC, HML_RET_CHANNEL) \
    X("select",     1,  HML_BUILTIN_PUBLIC | HML_BUILTIN_VARIADIC, HML_RET_ANY) \
    \
    /* Signals */ \
    X("signal",     2,  HML_BUILTIN_PUBLIC, HML_RET_NULL) \
    X("raise",      1,  HML_BUILTIN_PUBLIC, HML_RET_I32) \
    \
    /* Utility */ \
    X("apply",      2,  HML_BUILTIN_PUBLIC | HML_BUILTIN_VARIADIC, HML_RET_ANY) \
    X("set_stack_limit", 1, HML_BUILTIN_PUBLIC, HML_RET_NULL) \
    X("get_stack_limit", 0, HML_BUILTIN_PUBLIC, HML_RET_I32)

#define HML_BUILTINS_MATH(X) \
    /* Math (public and internal aliases) */ \
    X("sin",    1,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_F64) \
    X("cos",    1,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_F64) \
    X("tan",    1,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_F64) \
    X("asin",   1,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_F64) \
    X("acos",   1,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_F64) \
    X("atan",   1,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_F64) \
    X("atan2",  2,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_F64) \
    X("sqrt",   1,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_F64) \
    X("pow",    2,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_F64) \
    X("exp",    1,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_F64) \
    X("log",    1,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_F64) \
    X("log10",  1,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_F64) \
    X("log2",   1,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_F64) \
    X("floor",  1,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_F64) \
    X("ceil",   1,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_F64) \
    X("round",  1,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_F64) \
    X("trunc",  1,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_F64) \
    X("floori", 1,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_I64) \
    X("ceili",  1,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_I64) \
    X("roundi", 1,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_I64) \
    X("trunci", 1,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_I64) \
    X("div",    2,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_F64) \
    X("divi",   2,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_I64) \
    /* Internal math (prefixed) */ \
    X("__sin",    1,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_F64) \
    X("__cos",    1,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_F64) \
    X("__tan",    1,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_F64) \
    X("__asin",   1,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_F64) \
    X("__acos",   1,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_F64) \
    X("__atan",   1,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_F64) \
    X("__atan2",  2,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_F64) \
    X("__sqrt",   1,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_F64) \
    X("__pow",    2,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_F64) \
    X("__exp",    1,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_F64) \
    X("__log",    1,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_F64) \
    X("__log10",  1,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_F64) \
    X("__log2",   1,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_F64) \
    X("__floor",  1,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_F64) \
    X("__ceil",   1,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_F64) \
    X("__round",  1,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_F64) \
    X("__trunc",  1,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_F64) \
    X("__floori", 1,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_I64) \
    X("__ceili",  1,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_I64) \
    X("__roundi", 1,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_I64) \
    X("__trunci", 1,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_I64) \
    X("__div",    2,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_F64) \
    X("__divi",   2,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_I64) \
    X("__abs",    1,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_F64) \
    X("__min",    2,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_F64) \
    X("__max",    2,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_F64) \
    X("__clamp",  3,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_F64) \
    X("__rand",   0,  HML_BUILTIN_INTERNAL, HML_RET_F64) \
    X("__rand_range", 2, HML_BUILTIN_INTERNAL, HML_RET_I64) \
    X("__seed",   1,  HML_BUILTIN_INTERNAL, HML_RET_NULL)

#define HML_BUILTINS_PTR(X) \
    /* Pointer operations */ \
    X("ptr_null",       0,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_PTR) \
    X("ptr_offset",     2,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_PTR) \
    X("ptr_to_buffer",  2,  HML_BUILTIN_PUBLIC, HML_RET_BUFFER) \
    X("buffer_ptr",     1,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_PTR) \
    /* Read operations */ \
    X("ptr_deref_i8",   1,  HML_BUILTIN_PUBLIC, HML_RET_I32) \
    X("ptr_deref_i16",  1,  HML_BUILTIN_PUBLIC, HML_RET_I32) \
    X("ptr_deref_i32",  1,  HML_BUILTIN_PUBLIC, HML_RET_I32) \
    X("ptr_read_i32",   1,  HML_BUILTIN_PUBLIC, HML_RET_I32) \
    X("ptr_deref_i64",  1,  HML_BUILTIN_PUBLIC, HML_RET_I64) \
    X("ptr_deref_u8",   1,  HML_BUILTIN_PUBLIC, HML_RET_I32) \
    X("ptr_deref_u16",  1,  HML_BUILTIN_PUBLIC, HML_RET_I32) \
    X("ptr_deref_u32",  1,  HML_BUILTIN_PUBLIC, HML_RET_I64) \
    X("ptr_deref_u64",  1,  HML_BUILTIN_PUBLIC, HML_RET_I64) \
    X("ptr_deref_f32",  1,  HML_BUILTIN_PUBLIC, HML_RET_F64) \
    X("ptr_deref_f64",  1,  HML_BUILTIN_PUBLIC, HML_RET_F64) \
    X("ptr_deref_ptr",  1,  HML_BUILTIN_PUBLIC, HML_RET_PTR) \
    /* Write operations */ \
    X("ptr_write_i8",   2,  HML_BUILTIN_PUBLIC, HML_RET_NULL) \
    X("ptr_write_i16",  2,  HML_BUILTIN_PUBLIC, HML_RET_NULL) \
    X("ptr_write_i32",  2,  HML_BUILTIN_PUBLIC, HML_RET_NULL) \
    X("ptr_write_i64",  2,  HML_BUILTIN_PUBLIC, HML_RET_NULL) \
    X("ptr_write_u8",   2,  HML_BUILTIN_PUBLIC, HML_RET_NULL) \
    X("ptr_write_u16",  2,  HML_BUILTIN_PUBLIC, HML_RET_NULL) \
    X("ptr_write_u32",  2,  HML_BUILTIN_PUBLIC, HML_RET_NULL) \
    X("ptr_write_u64",  2,  HML_BUILTIN_PUBLIC, HML_RET_NULL) \
    X("ptr_write_f32",  2,  HML_BUILTIN_PUBLIC, HML_RET_NULL) \
    X("ptr_write_f64",  2,  HML_BUILTIN_PUBLIC, HML_RET_NULL) \
    X("ptr_write_ptr",  2,  HML_BUILTIN_PUBLIC, HML_RET_NULL)

#define HML_BUILTINS_ATOMIC(X) \
    /* Atomic i32 */ \
    X("atomic_load_i32",     1,  HML_BUILTIN_PUBLIC, HML_RET_I32) \
    X("atomic_store_i32",    2,  HML_BUILTIN_PUBLIC, HML_RET_NULL) \
    X("atomic_add_i32",      2,  HML_BUILTIN_PUBLIC, HML_RET_I32) \
    X("atomic_sub_i32",      2,  HML_BUILTIN_PUBLIC, HML_RET_I32) \
    X("atomic_and_i32",      2,  HML_BUILTIN_PUBLIC, HML_RET_I32) \
    X("atomic_or_i32",       2,  HML_BUILTIN_PUBLIC, HML_RET_I32) \
    X("atomic_xor_i32",      2,  HML_BUILTIN_PUBLIC, HML_RET_I32) \
    X("atomic_cas_i32",      3,  HML_BUILTIN_PUBLIC, HML_RET_BOOL) \
    X("atomic_exchange_i32", 2,  HML_BUILTIN_PUBLIC, HML_RET_I32) \
    /* Atomic i64 */ \
    X("atomic_load_i64",     1,  HML_BUILTIN_PUBLIC, HML_RET_I64) \
    X("atomic_store_i64",    2,  HML_BUILTIN_PUBLIC, HML_RET_NULL) \
    X("atomic_add_i64",      2,  HML_BUILTIN_PUBLIC, HML_RET_I64) \
    X("atomic_sub_i64",      2,  HML_BUILTIN_PUBLIC, HML_RET_I64) \
    X("atomic_and_i64",      2,  HML_BUILTIN_PUBLIC, HML_RET_I64) \
    X("atomic_or_i64",       2,  HML_BUILTIN_PUBLIC, HML_RET_I64) \
    X("atomic_xor_i64",      2,  HML_BUILTIN_PUBLIC, HML_RET_I64) \
    X("atomic_cas_i64",      3,  HML_BUILTIN_PUBLIC, HML_RET_BOOL) \
    X("atomic_exchange_i64", 2,  HML_BUILTIN_PUBLIC, HML_RET_I64) \
    /* Memory fence */ \
    X("atomic_fence",        0,  HML_BUILTIN_PUBLIC, HML_RET_NULL)

#define HML_BUILTINS_ENV(X) \
    /* Environment (public) */ \
    X("getenv",     1,  HML_BUILTIN_PUBLIC, HML_RET_STRING) \
    X("setenv",     2,  HML_BUILTIN_PUBLIC, HML_RET_NULL) \
    X("unsetenv",   1,  HML_BUILTIN_PUBLIC, HML_RET_NULL) \
    X("get_pid",    0,  HML_BUILTIN_PUBLIC, HML_RET_I32) \
    /* Environment (internal) */ \
    X("__getenv",   1,  HML_BUILTIN_INTERNAL, HML_RET_STRING) \
    X("__setenv",   2,  HML_BUILTIN_INTERNAL, HML_RET_NULL) \
    X("__unsetenv", 1,  HML_BUILTIN_INTERNAL, HML_RET_NULL) \
    X("__exit",     1,  HML_BUILTIN_INTERNAL | HML_BUILTIN_NORETURN, HML_RET_NULL) \
    X("__get_pid",  0,  HML_BUILTIN_INTERNAL, HML_RET_I32) \
    X("__getppid",  0,  HML_BUILTIN_INTERNAL, HML_RET_I32) \
    X("__getuid",   0,  HML_BUILTIN_INTERNAL, HML_RET_I32) \
    X("__geteuid",  0,  HML_BUILTIN_INTERNAL, HML_RET_I32) \
    X("__getgid",   0,  HML_BUILTIN_INTERNAL, HML_RET_I32) \
    X("__getegid",  0,  HML_BUILTIN_INTERNAL, HML_RET_I32) \
    X("__fork",     0,  HML_BUILTIN_INTERNAL, HML_RET_I32) \
    X("__wait",     1,  HML_BUILTIN_INTERNAL, HML_RET_I32) \
    X("__waitpid",  2,  HML_BUILTIN_INTERNAL, HML_RET_I32) \
    X("__kill",     2,  HML_BUILTIN_INTERNAL, HML_RET_I32) \
    X("__exec",     1,  HML_BUILTIN_INTERNAL, HML_RET_OBJECT) \
    X("__exec_argv", 1, HML_BUILTIN_INTERNAL, HML_RET_OBJECT) \
    X("__abort",    0,  HML_BUILTIN_INTERNAL | HML_BUILTIN_NORETURN, HML_RET_NULL)

#define HML_BUILTINS_TIME(X) \
    X("__now",       0,  HML_BUILTIN_INTERNAL, HML_RET_I64) \
    X("__time_ms",   0,  HML_BUILTIN_INTERNAL, HML_RET_I64) \
    X("__sleep",     1,  HML_BUILTIN_INTERNAL, HML_RET_NULL) \
    X("__clock",     0,  HML_BUILTIN_INTERNAL, HML_RET_F64) \
    X("__localtime", 1,  HML_BUILTIN_INTERNAL, HML_RET_OBJECT) \
    X("__gmtime",    1,  HML_BUILTIN_INTERNAL, HML_RET_OBJECT) \
    X("__mktime",    1,  HML_BUILTIN_INTERNAL, HML_RET_I64) \
    X("__strftime",  2,  HML_BUILTIN_INTERNAL, HML_RET_STRING)

#define HML_BUILTINS_FS(X) \
    X("__exists",       1,  HML_BUILTIN_INTERNAL, HML_RET_BOOL) \
    X("__read_file",    1,  HML_BUILTIN_INTERNAL, HML_RET_STRING) \
    X("__write_file",   2,  HML_BUILTIN_INTERNAL, HML_RET_BOOL) \
    X("__append_file",  2,  HML_BUILTIN_INTERNAL, HML_RET_BOOL) \
    X("__make_dir",     1,  HML_BUILTIN_INTERNAL, HML_RET_BOOL) \
    X("__remove_dir",   1,  HML_BUILTIN_INTERNAL, HML_RET_BOOL) \
    X("__list_dir",     1,  HML_BUILTIN_INTERNAL, HML_RET_ARRAY) \
    X("__remove_file",  1,  HML_BUILTIN_INTERNAL, HML_RET_BOOL) \
    X("__rename",       2,  HML_BUILTIN_INTERNAL, HML_RET_BOOL) \
    X("__copy_file",    2,  HML_BUILTIN_INTERNAL, HML_RET_BOOL) \
    X("__is_file",      1,  HML_BUILTIN_INTERNAL, HML_RET_BOOL) \
    X("__is_dir",       1,  HML_BUILTIN_INTERNAL, HML_RET_BOOL) \
    X("__file_stat",    1,  HML_BUILTIN_INTERNAL, HML_RET_OBJECT) \
    X("__cwd",          0,  HML_BUILTIN_INTERNAL, HML_RET_STRING) \
    X("__chdir",        1,  HML_BUILTIN_INTERNAL, HML_RET_BOOL) \
    X("__absolute_path", 1, HML_BUILTIN_INTERNAL, HML_RET_STRING)

#define HML_BUILTINS_OS(X) \
    X("__platform",     0,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_STRING) \
    X("__arch",         0,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_STRING) \
    X("__hostname",     0,  HML_BUILTIN_INTERNAL, HML_RET_STRING) \
    X("__username",     0,  HML_BUILTIN_INTERNAL, HML_RET_STRING) \
    X("__homedir",      0,  HML_BUILTIN_INTERNAL, HML_RET_STRING) \
    X("__cpu_count",    0,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_I32) \
    X("__total_memory", 0,  HML_BUILTIN_INTERNAL, HML_RET_I64) \
    X("__free_memory",  0,  HML_BUILTIN_INTERNAL, HML_RET_I64) \
    X("__os_version",   0,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_STRING) \
    X("__os_name",      0,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_STRING) \
    X("__tmpdir",       0,  HML_BUILTIN_INTERNAL, HML_RET_STRING) \
    X("__uptime",       0,  HML_BUILTIN_INTERNAL, HML_RET_F64)

#define HML_BUILTINS_NET(X) \
    X("socket_create",  2,  HML_BUILTIN_PUBLIC, HML_RET_I32) \
    X("dns_resolve",    1,  HML_BUILTIN_PUBLIC, HML_RET_STRING) \
    X("poll",           2,  HML_BUILTIN_PUBLIC, HML_RET_ARRAY)

#define HML_BUILTINS_CRYPTO(X) \
    X("__random_bytes", 1, HML_BUILTIN_INTERNAL, HML_RET_BUFFER) \
    X("__sha256",   1,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_STRING) \
    X("__sha512",   1,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_STRING) \
    X("__md5",      1,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_STRING) \
    X("__ecdsa_generate_key", 0, HML_BUILTIN_INTERNAL, HML_RET_PTR) \
    X("__ecdsa_free_key",     1, HML_BUILTIN_INTERNAL, HML_RET_NULL) \
    X("__ecdsa_sign",         2, HML_BUILTIN_INTERNAL, HML_RET_STRING) \
    X("__ecdsa_verify",       3, HML_BUILTIN_INTERNAL, HML_RET_BOOL)

#define HML_BUILTINS_COMPRESSION(X) \
    X("__zlib_compress",       1,  HML_BUILTIN_INTERNAL, HML_RET_BUFFER) \
    X("__zlib_decompress",     1,  HML_BUILTIN_INTERNAL, HML_RET_BUFFER) \
    X("__gzip_compress",       1,  HML_BUILTIN_INTERNAL, HML_RET_BUFFER) \
    X("__gzip_decompress",     1,  HML_BUILTIN_INTERNAL, HML_RET_BUFFER) \
    X("__zlib_compress_bound", 1,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_I64) \
    X("__crc32",               1,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_I64) \
    X("__adler32",             1,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_I64)

#define HML_BUILTINS_REGEX(X) \
    X("__regex_compile",      1,  HML_BUILTIN_INTERNAL, HML_RET_PTR) \
    X("__regex_test",         2,  HML_BUILTIN_INTERNAL, HML_RET_BOOL) \
    X("__regex_match",        2,  HML_BUILTIN_INTERNAL, HML_RET_ARRAY) \
    X("__regex_free",         1,  HML_BUILTIN_INTERNAL, HML_RET_NULL) \
    X("__regex_error",        1,  HML_BUILTIN_INTERNAL, HML_RET_STRING) \
    X("__regex_replace",      3,  HML_BUILTIN_INTERNAL, HML_RET_STRING) \
    X("__regex_replace_all",  3,  HML_BUILTIN_INTERNAL, HML_RET_STRING)

#define HML_BUILTINS_FFI(X) \
    X("callback",       2,  HML_BUILTIN_PUBLIC, HML_RET_PTR) \
    X("callback_free",  1,  HML_BUILTIN_PUBLIC, HML_RET_NULL) \
    X("ffi_sizeof",     1,  HML_BUILTIN_PUBLIC | HML_BUILTIN_PURE, HML_RET_I32)

#define HML_BUILTINS_INTERNAL(X) \
    /* Internal helpers */ \
    X("__read_u32",         1,  HML_BUILTIN_INTERNAL, HML_RET_I64) \
    X("__read_u64",         1,  HML_BUILTIN_INTERNAL, HML_RET_I64) \
    X("__read_ptr",         1,  HML_BUILTIN_INTERNAL, HML_RET_PTR) \
    X("__strerror",         1,  HML_BUILTIN_INTERNAL | HML_BUILTIN_PURE, HML_RET_STRING) \
    X("__dirent_name",      1,  HML_BUILTIN_INTERNAL, HML_RET_STRING) \
    X("__string_to_cstr",   1,  HML_BUILTIN_INTERNAL, HML_RET_PTR) \
    X("__cstr_to_string",   1,  HML_BUILTIN_INTERNAL, HML_RET_STRING) \
    X("__string_from_bytes", 1, HML_BUILTIN_INTERNAL, HML_RET_STRING) \
    X("string_concat_many", -1, HML_BUILTIN_INTERNAL | HML_BUILTIN_VARIADIC, HML_RET_STRING) \
    X("task_debug_info",    1,  HML_BUILTIN_INTERNAL, HML_RET_OBJECT)

// Combine all builtin groups
#define HML_ALL_BUILTINS(X) \
    HML_BUILTINS_CORE(X) \
    HML_BUILTINS_MATH(X) \
    HML_BUILTINS_PTR(X) \
    HML_BUILTINS_ATOMIC(X) \
    HML_BUILTINS_ENV(X) \
    HML_BUILTINS_TIME(X) \
    HML_BUILTINS_FS(X) \
    HML_BUILTINS_OS(X) \
    HML_BUILTINS_NET(X) \
    HML_BUILTINS_CRYPTO(X) \
    HML_BUILTINS_COMPRESSION(X) \
    HML_BUILTINS_REGEX(X) \
    HML_BUILTINS_FFI(X) \
    HML_BUILTINS_INTERNAL(X)

// ========== UTILITY MACROS ==========

// Count builtins at compile time
#define HML_COUNT_BUILTIN(name, arity, flags, ret) +1
#define HML_BUILTIN_COUNT (0 HML_ALL_BUILTINS(HML_COUNT_BUILTIN))

// Generate static array of builtin info
#define HML_MAKE_BUILTIN_INFO(name, arity, flags, ret) {name, arity, flags, ret},

// Example usage:
// static const HmlBuiltinInfo hml_builtins[] = {
//     HML_ALL_BUILTINS(HML_MAKE_BUILTIN_INFO)
// };

// ========== LOOKUP FUNCTIONS ==========

// Check if a name is a known builtin
// Returns pointer to HmlBuiltinInfo or NULL
static inline const HmlBuiltinInfo* hml_lookup_builtin(const char *name) {
    static const HmlBuiltinInfo builtins[] = {
        HML_ALL_BUILTINS(HML_MAKE_BUILTIN_INFO)
        {NULL, 0, 0, 0}  // Sentinel
    };

    for (int i = 0; builtins[i].name != NULL; i++) {
        if (strcmp(builtins[i].name, name) == 0) {
            return &builtins[i];
        }
    }
    return NULL;
}

// Check if name is any builtin (public or internal)
static inline int hml_is_builtin(const char *name) {
    return hml_lookup_builtin(name) != NULL;
}

// Check if name is a public builtin (not prefixed with __)
static inline int hml_is_public_builtin(const char *name) {
    const HmlBuiltinInfo *info = hml_lookup_builtin(name);
    return info && (info->flags & HML_BUILTIN_PUBLIC);
}

#endif /* HEMLOCK_BUILTINS_REGISTRY_H */
