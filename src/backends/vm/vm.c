/*
 * Hemlock Bytecode VM - Virtual Machine Implementation
 *
 * Stack-based bytecode interpreter with computed goto dispatch.
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE  // For M_PI, M_E

#include "vm.h"
#include "debug.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>

// Debug tracing
static int vm_trace_enabled = 0;

// ============================================
// Math Builtin Implementations for stdlib
// ============================================

// Value constructors for stdlib (simple inline versions)
static inline Value stdlib_val_f64(double f) {
    Value v; v.type = VAL_F64; v.as.as_f64 = f; return v;
}
static inline Value stdlib_val_null(void) {
    Value v; v.type = VAL_NULL; return v;
}

static double vm_get_number(Value v) {
    switch (v.type) {
        case VAL_I8:  return (double)v.as.as_i8;
        case VAL_I16: return (double)v.as.as_i16;
        case VAL_I32: return (double)v.as.as_i32;
        case VAL_I64: return (double)v.as.as_i64;
        case VAL_U8:  return (double)v.as.as_u8;
        case VAL_U16: return (double)v.as.as_u16;
        case VAL_U32: return (double)v.as.as_u32;
        case VAL_U64: return (double)v.as.as_u64;
        case VAL_F32: return (double)v.as.as_f32;
        case VAL_F64: return v.as.as_f64;
        default: return 0.0;
    }
}

#define VM_MATH_UNARY(name, func) \
    static Value vm_builtin_##name(Value *args, int argc, void *ctx) { \
        (void)ctx; (void)argc; \
        return stdlib_val_f64(func(vm_get_number(args[0]))); \
    }

#define VM_MATH_BINARY(name, func) \
    static Value vm_builtin_##name(Value *args, int argc, void *ctx) { \
        (void)ctx; (void)argc; \
        return stdlib_val_f64(func(vm_get_number(args[0]), vm_get_number(args[1]))); \
    }

VM_MATH_UNARY(sin, sin)
VM_MATH_UNARY(cos, cos)
VM_MATH_UNARY(tan, tan)
VM_MATH_UNARY(asin, asin)
VM_MATH_UNARY(acos, acos)
VM_MATH_UNARY(atan, atan)
VM_MATH_BINARY(atan2, atan2)
VM_MATH_UNARY(sqrt, sqrt)
VM_MATH_BINARY(pow, pow)
VM_MATH_UNARY(exp, exp)
VM_MATH_UNARY(log, log)
VM_MATH_UNARY(log10, log10)
VM_MATH_UNARY(log2, log2)
VM_MATH_UNARY(floor, floor)
VM_MATH_UNARY(ceil, ceil)
VM_MATH_UNARY(round, round)
VM_MATH_UNARY(trunc, trunc)
VM_MATH_UNARY(vm_fabs, fabs)

static Value vm_builtin_abs(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    double v = vm_get_number(args[0]);
    return stdlib_val_f64(v < 0 ? -v : v);
}

static Value vm_builtin_min(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    double a = vm_get_number(args[0]);
    double b = vm_get_number(args[1]);
    return stdlib_val_f64(a < b ? a : b);
}

static Value vm_builtin_max(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    double a = vm_get_number(args[0]);
    double b = vm_get_number(args[1]);
    return stdlib_val_f64(a > b ? a : b);
}

static Value vm_builtin_clamp(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    double v = vm_get_number(args[0]);
    double lo = vm_get_number(args[1]);
    double hi = vm_get_number(args[2]);
    if (v < lo) return stdlib_val_f64(lo);
    if (v > hi) return stdlib_val_f64(hi);
    return stdlib_val_f64(v);
}

static Value vm_builtin_rand(Value *args, int argc, void *ctx) {
    (void)args; (void)argc; (void)ctx;
    return stdlib_val_f64((double)rand() / (double)RAND_MAX);
}

static Value vm_builtin_rand_range(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    double lo = vm_get_number(args[0]);
    double hi = vm_get_number(args[1]);
    double t = (double)rand() / (double)RAND_MAX;
    return stdlib_val_f64(lo + t * (hi - lo));
}

static Value vm_builtin_seed(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    srand((unsigned int)vm_get_number(args[0]));
    return stdlib_val_null();
}

// Integer math builtins - return i64 instead of f64
static inline Value stdlib_val_i64(int64_t i) {
    Value v; v.type = VAL_I64; v.as.as_i64 = i; return v;
}

static Value vm_builtin_floori(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    return stdlib_val_i64((int64_t)floor(vm_get_number(args[0])));
}

static Value vm_builtin_ceili(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    return stdlib_val_i64((int64_t)ceil(vm_get_number(args[0])));
}

static Value vm_builtin_roundi(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    return stdlib_val_i64((int64_t)round(vm_get_number(args[0])));
}

static Value vm_builtin_trunci(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    return stdlib_val_i64((int64_t)trunc(vm_get_number(args[0])));
}

static Value vm_builtin_div(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    double a = vm_get_number(args[0]);
    double b = vm_get_number(args[1]);
    return stdlib_val_f64(floor(a / b));
}

static Value vm_builtin_divi(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    int64_t a = (int64_t)vm_get_number(args[0]);
    int64_t b = (int64_t)vm_get_number(args[1]);
    return stdlib_val_i64(a / b);
}

// Forward declarations for functions defined later
static Value vm_make_string(const char *data, int len);
static Value val_bool_vm(int b);

// Environment builtins
static Value vm_builtin_getenv(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != VAL_STRING || !args[0].as.as_string) {
        return stdlib_val_null();
    }
    const char *val = getenv(args[0].as.as_string->data);
    if (!val) return stdlib_val_null();
    return vm_make_string(val, strlen(val));
}

static Value vm_builtin_setenv(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != VAL_STRING || !args[0].as.as_string ||
        args[1].type != VAL_STRING || !args[1].as.as_string) {
        return stdlib_val_null();
    }
    setenv(args[0].as.as_string->data, args[1].as.as_string->data, 1);
    return stdlib_val_null();
}

static Value vm_builtin_unsetenv(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != VAL_STRING || !args[0].as.as_string) {
        return stdlib_val_null();
    }
    unsetenv(args[0].as.as_string->data);
    return stdlib_val_null();
}

// Time builtins
#include <sys/time.h>
#include <time.h>

static Value vm_builtin_now(Value *args, int argc, void *ctx) {
    (void)args; (void)argc; (void)ctx;
    return stdlib_val_i64((int64_t)time(NULL));
}

static Value vm_builtin_time_ms(Value *args, int argc, void *ctx) {
    (void)args; (void)argc; (void)ctx;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return stdlib_val_i64((int64_t)(tv.tv_sec * 1000LL + tv.tv_usec / 1000LL));
}

static Value vm_builtin_sleep(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    double seconds = vm_get_number(args[0]);
    if (seconds > 0) {
        struct timespec ts;
        ts.tv_sec = (time_t)seconds;
        ts.tv_nsec = (long)((seconds - ts.tv_sec) * 1e9);
        nanosleep(&ts, NULL);
    }
    return stdlib_val_null();
}

static Value vm_builtin_clock(Value *args, int argc, void *ctx) {
    (void)args; (void)argc; (void)ctx;
    return stdlib_val_f64((double)clock() / CLOCKS_PER_SEC);
}

// Platform builtins
static Value vm_builtin_platform(Value *args, int argc, void *ctx) {
    (void)args; (void)argc; (void)ctx;
#ifdef __linux__
    return vm_make_string("linux", 5);
#elif defined(__APPLE__)
    return vm_make_string("darwin", 6);
#elif defined(_WIN32)
    return vm_make_string("windows", 7);
#else
    return vm_make_string("unknown", 7);
#endif
}

static Value vm_builtin_arch(Value *args, int argc, void *ctx) {
    (void)args; (void)argc; (void)ctx;
#if defined(__x86_64__) || defined(_M_X64)
    return vm_make_string("x86_64", 6);
#elif defined(__aarch64__) || defined(_M_ARM64)
    return vm_make_string("arm64", 5);
#elif defined(__i386__) || defined(_M_IX86)
    return vm_make_string("x86", 3);
#elif defined(__arm__)
    return vm_make_string("arm", 3);
#else
    return vm_make_string("unknown", 7);
#endif
}

// File system builtins
#include <sys/stat.h>

static Value vm_builtin_exists(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != VAL_STRING || !args[0].as.as_string) {
        return val_bool_vm(0);
    }
    struct stat st;
    return val_bool_vm(stat(args[0].as.as_string->data, &st) == 0);
}

static Value vm_builtin_is_file(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != VAL_STRING || !args[0].as.as_string) {
        return val_bool_vm(0);
    }
    struct stat st;
    if (stat(args[0].as.as_string->data, &st) != 0) return val_bool_vm(0);
    return val_bool_vm(S_ISREG(st.st_mode));
}

static Value vm_builtin_is_dir(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != VAL_STRING || !args[0].as.as_string) {
        return val_bool_vm(0);
    }
    struct stat st;
    if (stat(args[0].as.as_string->data, &st) != 0) return val_bool_vm(0);
    return val_bool_vm(S_ISDIR(st.st_mode));
}

// Process builtins
#include <unistd.h>

static Value vm_builtin_get_pid(Value *args, int argc, void *ctx) {
    (void)args; (void)argc; (void)ctx;
    return stdlib_val_i64((int64_t)getpid());
}

static Value vm_builtin_getppid(Value *args, int argc, void *ctx) {
    (void)args; (void)argc; (void)ctx;
    return stdlib_val_i64((int64_t)getppid());
}

static Value vm_builtin_getuid(Value *args, int argc, void *ctx) {
    (void)args; (void)argc; (void)ctx;
    return stdlib_val_i64((int64_t)getuid());
}

static Value vm_builtin_geteuid(Value *args, int argc, void *ctx) {
    (void)args; (void)argc; (void)ctx;
    return stdlib_val_i64((int64_t)geteuid());
}

static Value vm_builtin_exit(Value *args, int argc, void *ctx) {
    (void)ctx;
    int code = 0;
    if (argc >= 1) {
        code = (int)vm_get_number(args[0]);
    }
    exit(code);
    return stdlib_val_null();  // Never reached
}

// String builtins
static Value vm_builtin_strlen(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != VAL_STRING || !args[0].as.as_string) {
        return stdlib_val_i64(0);
    }
    return stdlib_val_i64((int64_t)args[0].as.as_string->length);
}

// Hashing builtin
#include <openssl/sha.h>
#include <openssl/md5.h>

static Value vm_builtin_sha256(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != VAL_STRING || !args[0].as.as_string) {
        return stdlib_val_null();
    }
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)args[0].as.as_string->data,
           args[0].as.as_string->length, hash);

    // Convert to hex string
    char hex[SHA256_DIGEST_LENGTH * 2 + 1];
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(hex + i * 2, "%02x", hash[i]);
    }
    return vm_make_string(hex, SHA256_DIGEST_LENGTH * 2);
}

static Value vm_builtin_sha512(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != VAL_STRING || !args[0].as.as_string) {
        return stdlib_val_null();
    }
    unsigned char hash[SHA512_DIGEST_LENGTH];
    SHA512((unsigned char*)args[0].as.as_string->data,
           args[0].as.as_string->length, hash);

    // Convert to hex string
    char hex[SHA512_DIGEST_LENGTH * 2 + 1];
    for (int i = 0; i < SHA512_DIGEST_LENGTH; i++) {
        sprintf(hex + i * 2, "%02x", hash[i]);
    }
    return vm_make_string(hex, SHA512_DIGEST_LENGTH * 2);
}

static Value vm_builtin_md5(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != VAL_STRING || !args[0].as.as_string) {
        return stdlib_val_null();
    }
    unsigned char hash[MD5_DIGEST_LENGTH];
    MD5((unsigned char*)args[0].as.as_string->data,
        args[0].as.as_string->length, hash);

    // Convert to hex string
    char hex[MD5_DIGEST_LENGTH * 2 + 1];
    for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
        sprintf(hex + i * 2, "%02x", hash[i]);
    }
    return vm_make_string(hex, MD5_DIGEST_LENGTH * 2);
}

// File I/O builtins
static Value vm_builtin_read_file(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != VAL_STRING || !args[0].as.as_string) {
        return stdlib_val_null();
    }
    FILE *f = fopen(args[0].as.as_string->data, "rb");
    if (!f) return stdlib_val_null();

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    char *buffer = malloc(size + 1);
    if (!buffer) {
        fclose(f);
        return stdlib_val_null();
    }

    size_t read = fread(buffer, 1, size, f);
    buffer[read] = '\0';
    fclose(f);

    Value result = vm_make_string(buffer, (int)read);
    free(buffer);
    return result;
}

static Value vm_builtin_write_file(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != VAL_STRING || !args[0].as.as_string) {
        return val_bool_vm(0);
    }
    if (args[1].type != VAL_STRING || !args[1].as.as_string) {
        return val_bool_vm(0);
    }

    FILE *f = fopen(args[0].as.as_string->data, "wb");
    if (!f) return val_bool_vm(0);

    size_t written = fwrite(args[1].as.as_string->data, 1,
                            args[1].as.as_string->length, f);
    fclose(f);
    return val_bool_vm(written == (size_t)args[1].as.as_string->length);
}

static Value vm_builtin_append_file(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != VAL_STRING || !args[0].as.as_string) {
        return val_bool_vm(0);
    }
    if (args[1].type != VAL_STRING || !args[1].as.as_string) {
        return val_bool_vm(0);
    }

    FILE *f = fopen(args[0].as.as_string->data, "ab");
    if (!f) return val_bool_vm(0);

    size_t written = fwrite(args[1].as.as_string->data, 1,
                            args[1].as.as_string->length, f);
    fclose(f);
    return val_bool_vm(written == (size_t)args[1].as.as_string->length);
}

static Value vm_builtin_remove_file(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != VAL_STRING || !args[0].as.as_string) {
        return val_bool_vm(0);
    }
    return val_bool_vm(remove(args[0].as.as_string->data) == 0);
}

static Value vm_builtin_cwd(Value *args, int argc, void *ctx) {
    (void)args; (void)argc; (void)ctx;
    char buf[4096];
    if (getcwd(buf, sizeof(buf))) {
        return vm_make_string(buf, strlen(buf));
    }
    return stdlib_val_null();
}

static Value vm_builtin_chdir(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != VAL_STRING || !args[0].as.as_string) {
        return val_bool_vm(0);
    }
    return val_bool_vm(chdir(args[0].as.as_string->data) == 0);
}

static Value vm_builtin_rename(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != VAL_STRING || !args[0].as.as_string ||
        args[1].type != VAL_STRING || !args[1].as.as_string) {
        return val_bool_vm(0);
    }
    return val_bool_vm(rename(args[0].as.as_string->data,
                              args[1].as.as_string->data) == 0);
}

static Value vm_builtin_make_dir(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != VAL_STRING || !args[0].as.as_string) {
        return val_bool_vm(0);
    }
    return val_bool_vm(mkdir(args[0].as.as_string->data, 0755) == 0);
}

static Value vm_builtin_remove_dir(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != VAL_STRING || !args[0].as.as_string) {
        return val_bool_vm(0);
    }
    return val_bool_vm(rmdir(args[0].as.as_string->data) == 0);
}

#include <dirent.h>

static Value vm_builtin_list_dir(Value *args, int argc, void *ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != VAL_STRING || !args[0].as.as_string) {
        return stdlib_val_null();
    }

    DIR *dir = opendir(args[0].as.as_string->data);
    if (!dir) return stdlib_val_null();

    // Count entries first
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            count++;
        }
    }
    rewinddir(dir);

    // Create array
    Array *arr = malloc(sizeof(Array));
    arr->elements = malloc(sizeof(Value) * (count > 0 ? count : 1));
    arr->length = 0;
    arr->capacity = count > 0 ? count : 1;
    arr->element_type = NULL;
    arr->ref_count = 1;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            arr->elements[arr->length++] = vm_make_string(entry->d_name, strlen(entry->d_name));
        }
    }
    closedir(dir);

    Value result = {.type = VAL_ARRAY, .as.as_array = arr};
    return result;
}

static Value vm_val_builtin_fn(BuiltinFn fn) {
    Value v;
    v.type = VAL_BUILTIN_FN;
    v.as.as_builtin_fn = fn;
    return v;
}

// ============================================
// UTF-8 Helpers
// ============================================

// Get byte length of UTF-8 character from first byte
static int utf8_char_byte_length(unsigned char first_byte) {
    if ((first_byte & 0x80) == 0) return 1;      // 0xxxxxxx (ASCII)
    if ((first_byte & 0xE0) == 0xC0) return 2;   // 110xxxxx
    if ((first_byte & 0xF0) == 0xE0) return 3;   // 1110xxxx
    if ((first_byte & 0xF8) == 0xF0) return 4;   // 11110xxx
    return 1;  // Invalid, treat as 1 byte
}

// Count Unicode codepoints in UTF-8 string
static int utf8_count_codepoints(const char *data, int byte_length) {
    int count = 0;
    int pos = 0;
    while (pos < byte_length) {
        unsigned char byte = (unsigned char)data[pos];
        if ((byte & 0xC0) != 0x80) {  // Not a continuation byte
            count++;
        }
        pos++;
    }
    return count;
}

// Find byte offset of the i-th codepoint (0-indexed)
static int utf8_byte_offset(const char *data, int byte_length, int char_index) {
    int pos = 0;
    int codepoint_count = 0;
    while (pos < byte_length) {
        unsigned char byte = (unsigned char)data[pos];
        if ((byte & 0xC0) != 0x80) {  // Start byte
            if (codepoint_count == char_index) {
                return pos;
            }
            codepoint_count++;
        }
        pos++;
    }
    return pos;
}

// Decode UTF-8 codepoint at byte position
static uint32_t utf8_decode_at(const char *data, int byte_pos) {
    unsigned char b1 = (unsigned char)data[byte_pos];

    if ((b1 & 0x80) == 0) {
        return b1;  // ASCII
    }
    if ((b1 & 0xE0) == 0xC0) {
        unsigned char b2 = (unsigned char)data[byte_pos + 1];
        return ((b1 & 0x1F) << 6) | (b2 & 0x3F);
    }
    if ((b1 & 0xF0) == 0xE0) {
        unsigned char b2 = (unsigned char)data[byte_pos + 1];
        unsigned char b3 = (unsigned char)data[byte_pos + 2];
        return ((b1 & 0x0F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);
    }
    if ((b1 & 0xF8) == 0xF0) {
        unsigned char b2 = (unsigned char)data[byte_pos + 1];
        unsigned char b3 = (unsigned char)data[byte_pos + 2];
        unsigned char b4 = (unsigned char)data[byte_pos + 3];
        return ((b1 & 0x07) << 18) | ((b2 & 0x3F) << 12) | ((b3 & 0x3F) << 6) | (b4 & 0x3F);
    }
    return b1;  // Invalid, return as-is
}

// Encode codepoint to UTF-8, return number of bytes written
static int utf8_encode(uint32_t codepoint, char *buf) {
    if (codepoint < 0x80) {
        buf[0] = (char)codepoint;
        return 1;
    }
    if (codepoint < 0x800) {
        buf[0] = (char)(0xC0 | (codepoint >> 6));
        buf[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    }
    if (codepoint < 0x10000) {
        buf[0] = (char)(0xE0 | (codepoint >> 12));
        buf[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    }
    buf[0] = (char)(0xF0 | (codepoint >> 18));
    buf[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
    buf[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
    buf[3] = (char)(0x80 | (codepoint & 0x3F));
    return 4;
}

// ============================================
// Async Support
// ============================================

// Global task ID counter (atomic for thread-safety)
static atomic_int vm_next_task_id = 1;

// VM Task structure for async execution
typedef struct VMTask {
    int id;
    TaskState state;
    VMClosure *closure;     // The function to execute
    Value *args;            // Arguments
    int argc;
    Value result;           // Return value when completed
    int joined;             // Flag: task has been joined
    int detached;           // Flag: task is detached
    pthread_t thread;       // Thread handle
    pthread_mutex_t mutex;  // For thread-safe state access
    int ref_count;          // Reference count
    bool has_exception;     // Flag: task threw an exception
    Value exception;        // Exception value if thrown
} VMTask;

// Forward declarations
static VMResult vm_execute(VM *vm, int base_frame_count);
static Value vm_deep_copy(Value v);

// Create a new VMTask
static VMTask* vm_task_new(VMClosure *closure, Value *args, int argc) {
    VMTask *task = malloc(sizeof(VMTask));
    if (!task) return NULL;

    task->id = atomic_fetch_add(&vm_next_task_id, 1);
    task->state = TASK_READY;
    task->closure = closure;
    closure->ref_count++;  // Retain closure

    // Deep copy arguments for thread safety
    task->args = NULL;
    task->argc = argc;
    if (argc > 0) {
        task->args = malloc(sizeof(Value) * argc);
        for (int i = 0; i < argc; i++) {
            task->args[i] = vm_deep_copy(args[i]);
        }
    }

    task->result.type = VAL_NULL;
    task->joined = 0;
    task->detached = 0;
    task->ref_count = 1;
    task->has_exception = false;
    task->exception.type = VAL_NULL;
    pthread_mutex_init(&task->mutex, NULL);

    return task;
}

// Free a VMTask
static void vm_task_free(VMTask *task) {
    if (!task) return;

    pthread_mutex_destroy(&task->mutex);
    if (task->closure) {
        task->closure->ref_count--;
        if (task->closure->ref_count <= 0) {
            vm_closure_free(task->closure);
        }
    }
    if (task->args) {
        free(task->args);
    }
    free(task);
}

// Retain a VMTask
static void vm_task_retain(VMTask *task) {
    if (task) {
        __atomic_add_fetch(&task->ref_count, 1, __ATOMIC_SEQ_CST);
    }
}

// Release a VMTask
static void vm_task_release(VMTask *task) {
    if (task) {
        int old = __atomic_sub_fetch(&task->ref_count, 1, __ATOMIC_SEQ_CST);
        if (old == 0) {
            vm_task_free(task);
        }
    }
}

// Thread wrapper for async execution
typedef struct {
    VMTask *task;
    Chunk *chunk;
} TaskThreadArg;

static void* vm_task_thread_wrapper(void *arg) {
    TaskThreadArg *thread_arg = (TaskThreadArg*)arg;
    VMTask *task = thread_arg->task;
    Chunk *chunk = thread_arg->chunk;
    free(thread_arg);

    // Block all signals in worker thread
    sigset_t set;
    sigfillset(&set);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    // Mark as running
    pthread_mutex_lock(&task->mutex);
    task->state = TASK_RUNNING;
    pthread_mutex_unlock(&task->mutex);

    // Create a new VM for this thread
    VM *vm = vm_new();

    // Set up the call frame for the function
    CallFrame *frame = &vm->frames[vm->frame_count++];
    frame->chunk = chunk;
    frame->ip = chunk->code;
    frame->slots = vm->stack;
    frame->upvalues = task->closure->upvalues ? task->closure->upvalues[0] : NULL;
    frame->slot_count = chunk->local_count;

    // Put closure in slot 0 (placeholder)
    vm->stack[0].type = VAL_NULL;

    // Put arguments in local slots (parameters start at slot 1)
    for (int i = 0; i < task->argc && i < chunk->arity; i++) {
        vm->stack[i + 1] = task->args[i];  // +1 because slot 0 is closure
    }
    // Fill remaining params with null
    for (int i = task->argc; i < chunk->arity + chunk->optional_count; i++) {
        vm->stack[i + 1].type = VAL_NULL;  // +1 because slot 0 is closure
    }
    vm->stack_top = vm->stack + chunk->local_count;

    // Execute the function
    VMResult result = vm_execute(vm, 0);

    // Get return value
    pthread_mutex_lock(&task->mutex);
    if (result == VM_OK && vm->is_returning) {
        task->result = vm->return_value;
    } else if (vm->is_throwing) {
        task->has_exception = true;
        task->exception = vm->exception;
    }
    task->state = TASK_COMPLETED;
    pthread_mutex_unlock(&task->mutex);

    // Cleanup
    vm_free(vm);

    // Release task if detached
    if (task->detached) {
        vm_task_release(task);
    }

    return NULL;
}

// ============================================
// Channel Implementation (for VM)
// ============================================

static Channel* vm_channel_new(int capacity) {
    Channel *ch = malloc(sizeof(Channel));
    if (!ch) return NULL;

    ch->capacity = capacity;
    ch->head = 0;
    ch->tail = 0;
    ch->count = 0;
    ch->closed = 0;
    ch->ref_count = 1;

    if (capacity > 0) {
        ch->buffer = malloc(sizeof(Value) * capacity);
        if (!ch->buffer) {
            free(ch);
            return NULL;
        }
    } else {
        ch->buffer = NULL;
    }

    ch->mutex = malloc(sizeof(pthread_mutex_t));
    ch->not_empty = malloc(sizeof(pthread_cond_t));
    ch->not_full = malloc(sizeof(pthread_cond_t));
    ch->rendezvous = malloc(sizeof(pthread_cond_t));

    if (!ch->mutex || !ch->not_empty || !ch->not_full || !ch->rendezvous) {
        if (ch->buffer) free(ch->buffer);
        if (ch->mutex) free(ch->mutex);
        if (ch->not_empty) free(ch->not_empty);
        if (ch->not_full) free(ch->not_full);
        if (ch->rendezvous) free(ch->rendezvous);
        free(ch);
        return NULL;
    }

    pthread_mutex_init((pthread_mutex_t*)ch->mutex, NULL);
    pthread_cond_init((pthread_cond_t*)ch->not_empty, NULL);
    pthread_cond_init((pthread_cond_t*)ch->not_full, NULL);
    pthread_cond_init((pthread_cond_t*)ch->rendezvous, NULL);

    ch->unbuffered_value = malloc(sizeof(Value));
    if (ch->unbuffered_value) {
        ch->unbuffered_value->type = VAL_NULL;
    }
    ch->sender_waiting = 0;
    ch->receiver_waiting = 0;

    return ch;
}

// ============================================
// Value Helpers (matching interpreter semantics)
// ============================================

static Value vm_null_value(void) {
    Value v = {.type = VAL_NULL};
    return v;
}

static Value val_bool_vm(int b) {
    Value v = {.type = VAL_BOOL, .as.as_bool = b};
    return v;
}

static Value val_i32_vm(int32_t i) {
    Value v = {.type = VAL_I32, .as.as_i32 = i};
    return v;
}

static Value val_i64_vm(int64_t i) {
    Value v = {.type = VAL_I64, .as.as_i64 = i};
    return v;
}

static Value val_f64_vm(double f) {
    Value v = {.type = VAL_F64, .as.as_f64 = f};
    return v;
}

static int value_is_truthy(Value v) {
    switch (v.type) {
        case VAL_NULL: return 0;
        case VAL_BOOL: return v.as.as_bool != 0;
        case VAL_I32: return v.as.as_i32 != 0;
        case VAL_I64: return v.as.as_i64 != 0;
        case VAL_F64: return v.as.as_f64 != 0.0;
        case VAL_STRING: return v.as.as_string && v.as.as_string->length > 0;
        case VAL_ARRAY: return v.as.as_array && v.as.as_array->length > 0;
        default: return 1;  // Non-null objects are truthy
    }
}

// Convert value to double for arithmetic
static double value_to_f64(Value v) {
    switch (v.type) {
        case VAL_I8: return (double)v.as.as_i8;
        case VAL_I16: return (double)v.as.as_i16;
        case VAL_I32: return (double)v.as.as_i32;
        case VAL_I64: return (double)v.as.as_i64;
        case VAL_U8: return (double)v.as.as_u8;
        case VAL_U16: return (double)v.as.as_u16;
        case VAL_U32: return (double)v.as.as_u32;
        case VAL_U64: return (double)v.as.as_u64;
        case VAL_F32: return (double)v.as.as_f32;
        case VAL_F64: return v.as.as_f64;
        default: return 0.0;
    }
}

// Convert value to i64 for integer ops
static int64_t value_to_i64(Value v) {
    switch (v.type) {
        case VAL_I8: return (int64_t)v.as.as_i8;
        case VAL_I16: return (int64_t)v.as.as_i16;
        case VAL_I32: return (int64_t)v.as.as_i32;
        case VAL_I64: return v.as.as_i64;
        case VAL_U8: return (int64_t)v.as.as_u8;
        case VAL_U16: return (int64_t)v.as.as_u16;
        case VAL_U32: return (int64_t)v.as.as_u32;
        case VAL_U64: return (int64_t)v.as.as_u64;
        case VAL_F32: return (int64_t)v.as.as_f32;
        case VAL_F64: return (int64_t)v.as.as_f64;
        default: return 0;
    }
}

// Convert value to i32
static int32_t value_to_i32(Value v) {
    return (int32_t)value_to_i64(v);
}

// Compare two values for equality
static bool vm_values_equal(Value a, Value b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case VAL_NULL: return true;
        case VAL_BOOL: return a.as.as_bool == b.as.as_bool;
        case VAL_I8: return a.as.as_i8 == b.as.as_i8;
        case VAL_I16: return a.as.as_i16 == b.as.as_i16;
        case VAL_I32: return a.as.as_i32 == b.as.as_i32;
        case VAL_I64: return a.as.as_i64 == b.as.as_i64;
        case VAL_U8: return a.as.as_u8 == b.as.as_u8;
        case VAL_U16: return a.as.as_u16 == b.as.as_u16;
        case VAL_U32: return a.as.as_u32 == b.as.as_u32;
        case VAL_U64: return a.as.as_u64 == b.as.as_u64;
        case VAL_F32: return a.as.as_f32 == b.as.as_f32;
        case VAL_F64: return a.as.as_f64 == b.as.as_f64;
        case VAL_STRING:
            if (!a.as.as_string || !b.as.as_string) return a.as.as_string == b.as.as_string;
            return strcmp(a.as.as_string->data, b.as.as_string->data) == 0;
        case VAL_ARRAY: return a.as.as_array == b.as.as_array;
        case VAL_OBJECT: return a.as.as_object == b.as.as_object;
        default: return false;
    }
}

// Create a new string Value
static Value vm_make_string(const char *data, int len) {
    String *s = malloc(sizeof(String));
    s->data = malloc(len + 1);
    memcpy(s->data, data, len);
    s->data[len] = '\0';
    s->length = len;
    s->char_length = len;
    s->capacity = len + 1;
    s->ref_count = 1;
    Value v;
    v.type = VAL_STRING;
    v.as.as_string = s;
    return v;
}

// Deep copy a value for thread isolation
static Value vm_deep_copy(Value v) {
    switch (v.type) {
        case VAL_NULL:
        case VAL_BOOL:
        case VAL_I8:
        case VAL_I16:
        case VAL_I32:
        case VAL_I64:
        case VAL_U8:
        case VAL_U16:
        case VAL_U32:
        case VAL_U64:
        case VAL_F32:
        case VAL_F64:
        case VAL_RUNE:
            // Primitive types can be copied directly
            return v;

        case VAL_STRING:
            if (v.as.as_string) {
                return vm_make_string(v.as.as_string->data, v.as.as_string->length);
            }
            return vm_null_value();

        case VAL_ARRAY:
            if (v.as.as_array) {
                Array *src = v.as.as_array;
                Array *dst = malloc(sizeof(Array));
                dst->length = src->length;
                dst->capacity = src->capacity;
                dst->ref_count = 1;
                dst->element_type = NULL;
                dst->freed = 0;
                dst->elements = malloc(sizeof(Value) * dst->capacity);
                for (int i = 0; i < src->length; i++) {
                    dst->elements[i] = vm_deep_copy(src->elements[i]);
                }
                Value result;
                result.type = VAL_ARRAY;
                result.as.as_array = dst;
                return result;
            }
            return vm_null_value();

        case VAL_OBJECT:
            if (v.as.as_object) {
                Object *src = v.as.as_object;
                Object *dst = malloc(sizeof(Object));
                dst->type_name = src->type_name ? strdup(src->type_name) : NULL;
                dst->num_fields = src->num_fields;
                dst->capacity = src->capacity;
                dst->ref_count = 1;
                dst->freed = 0;
                dst->hash_table = NULL;
                dst->hash_capacity = 0;
                dst->field_names = malloc(sizeof(char*) * dst->capacity);
                dst->field_values = malloc(sizeof(Value) * dst->capacity);
                for (int i = 0; i < src->num_fields; i++) {
                    dst->field_names[i] = strdup(src->field_names[i]);
                    dst->field_values[i] = vm_deep_copy(src->field_values[i]);
                }
                Value result;
                result.type = VAL_OBJECT;
                result.as.as_object = dst;
                return result;
            }
            return vm_null_value();

        case VAL_CHANNEL:
            // Channels are shared between threads - just return same reference
            return v;

        default:
            // For other types (functions, files, etc.), return as-is
            return v;
    }
}

// ============================================
// Simple JSON Parser for deserialize()
// ============================================

static const char *json_skip_whitespace(const char *p, const char *end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

static Value vm_json_parse_value(const char **pp, const char *end);

static Value vm_json_parse_string(const char **pp, const char *end) {
    const char *p = *pp;
    if (p >= end || *p != '"') return vm_null_value();
    p++;  // Skip opening quote

    // Find closing quote and calculate length
    const char *start = p;
    while (p < end && *p != '"') {
        if (*p == '\\' && p + 1 < end) p++;  // Skip escaped char
        p++;
    }
    int len = p - start;
    char *buf = malloc(len + 1);

    // Copy with escape handling
    const char *src = start;
    char *dst = buf;
    while (src < p) {
        if (*src == '\\' && src + 1 < p) {
            src++;
            switch (*src) {
                case 'n': *dst++ = '\n'; break;
                case 't': *dst++ = '\t'; break;
                case 'r': *dst++ = '\r'; break;
                case '"': *dst++ = '"'; break;
                case '\\': *dst++ = '\\'; break;
                default: *dst++ = *src; break;
            }
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    int actual_len = dst - buf;

    if (p < end) p++;  // Skip closing quote
    *pp = p;

    Value result = vm_make_string(buf, actual_len);
    free(buf);
    return result;
}

static Value vm_json_parse_number(const char **pp, const char *end) {
    const char *p = *pp;
    const char *start = p;
    bool is_float = false;

    if (p < end && *p == '-') p++;
    while (p < end && *p >= '0' && *p <= '9') p++;
    if (p < end && *p == '.') {
        is_float = true;
        p++;
        while (p < end && *p >= '0' && *p <= '9') p++;
    }
    if (p < end && (*p == 'e' || *p == 'E')) {
        is_float = true;
        p++;
        if (p < end && (*p == '+' || *p == '-')) p++;
        while (p < end && *p >= '0' && *p <= '9') p++;
    }

    char buf[64];
    int len = p - start;
    if (len >= 63) len = 63;
    memcpy(buf, start, len);
    buf[len] = '\0';

    *pp = p;

    Value result;
    if (is_float) {
        result.type = VAL_F64;
        result.as.as_f64 = atof(buf);
    } else {
        int64_t n = strtoll(buf, NULL, 10);
        if (n >= INT32_MIN && n <= INT32_MAX) {
            result.type = VAL_I32;
            result.as.as_i32 = (int32_t)n;
        } else {
            result.type = VAL_I64;
            result.as.as_i64 = n;
        }
    }
    return result;
}

static Value vm_json_parse_array(const char **pp, const char *end) {
    const char *p = *pp;
    p++;  // Skip '['
    p = json_skip_whitespace(p, end);

    Array *arr = malloc(sizeof(Array));
    arr->elements = malloc(sizeof(Value) * 8);
    arr->length = 0;
    arr->capacity = 8;
    arr->element_type = NULL;
    arr->ref_count = 1;

    while (p < end && *p != ']') {
        Value elem = vm_json_parse_value(&p, end);
        if (arr->length >= arr->capacity) {
            arr->capacity *= 2;
            arr->elements = realloc(arr->elements, sizeof(Value) * arr->capacity);
        }
        arr->elements[arr->length++] = elem;

        p = json_skip_whitespace(p, end);
        if (p < end && *p == ',') {
            p++;
            p = json_skip_whitespace(p, end);
        }
    }
    if (p < end) p++;  // Skip ']'
    *pp = p;

    Value result;
    result.type = VAL_ARRAY;
    result.as.as_array = arr;
    return result;
}

static Value vm_json_parse_object(const char **pp, const char *end) {
    const char *p = *pp;
    p++;  // Skip '{'
    p = json_skip_whitespace(p, end);

    Object *obj = malloc(sizeof(Object));
    obj->type_name = NULL;
    obj->field_names = malloc(sizeof(char*) * 8);
    obj->field_values = malloc(sizeof(Value) * 8);
    obj->num_fields = 0;
    obj->capacity = 8;
    obj->ref_count = 1;

    while (p < end && *p != '}') {
        // Parse key
        Value key = vm_json_parse_string(&p, end);
        if (key.type != VAL_STRING) break;

        p = json_skip_whitespace(p, end);
        if (p < end && *p == ':') p++;
        p = json_skip_whitespace(p, end);

        // Parse value
        Value val = vm_json_parse_value(&p, end);

        // Add to object
        if (obj->num_fields >= obj->capacity) {
            obj->capacity *= 2;
            obj->field_names = realloc(obj->field_names, sizeof(char*) * obj->capacity);
            obj->field_values = realloc(obj->field_values, sizeof(Value) * obj->capacity);
        }
        obj->field_names[obj->num_fields] = strdup(key.as.as_string->data);
        obj->field_values[obj->num_fields] = val;
        obj->num_fields++;

        // Free key string
        free(key.as.as_string->data);
        free(key.as.as_string);

        p = json_skip_whitespace(p, end);
        if (p < end && *p == ',') {
            p++;
            p = json_skip_whitespace(p, end);
        }
    }
    if (p < end) p++;  // Skip '}'
    *pp = p;

    Value result;
    result.type = VAL_OBJECT;
    result.as.as_object = obj;
    return result;
}

static Value vm_json_parse_value(const char **pp, const char *end) {
    const char *p = json_skip_whitespace(*pp, end);
    *pp = p;

    if (p >= end) return vm_null_value();

    if (*p == '"') {
        return vm_json_parse_string(pp, end);
    } else if (*p == '{') {
        return vm_json_parse_object(pp, end);
    } else if (*p == '[') {
        return vm_json_parse_array(pp, end);
    } else if (*p == 't' && p + 4 <= end && strncmp(p, "true", 4) == 0) {
        *pp = p + 4;
        return val_bool_vm(true);
    } else if (*p == 'f' && p + 5 <= end && strncmp(p, "false", 5) == 0) {
        *pp = p + 5;
        return val_bool_vm(false);
    } else if (*p == 'n' && p + 4 <= end && strncmp(p, "null", 4) == 0) {
        *pp = p + 4;
        return vm_null_value();
    } else if (*p == '-' || (*p >= '0' && *p <= '9')) {
        return vm_json_parse_number(pp, end);
    }

    return vm_null_value();
}

static Value vm_json_parse(const char *json, int len) {
    const char *p = json;
    const char *end = json + len;
    return vm_json_parse_value(&p, end);
}

// Check if value is a numeric type
static int is_numeric(ValueType t) {
    return t >= VAL_I8 && t <= VAL_F64;
}

// Check if value is a float type
static int is_float(ValueType t) {
    return t == VAL_F32 || t == VAL_F64;
}

// Convert ValueType to a string name
static const char* val_type_name(ValueType t) {
    switch (t) {
        case VAL_NULL: return "null";
        case VAL_BOOL: return "bool";
        case VAL_I8: return "i8";
        case VAL_I16: return "i16";
        case VAL_I32: return "i32";
        case VAL_I64: return "i64";
        case VAL_U8: return "u8";
        case VAL_U16: return "u16";
        case VAL_U32: return "u32";
        case VAL_U64: return "u64";
        case VAL_F32: return "f32";
        case VAL_F64: return "f64";
        case VAL_STRING: return "string";
        case VAL_RUNE: return "rune";
        case VAL_PTR: return "ptr";
        case VAL_BUFFER: return "buffer";
        case VAL_ARRAY: return "array";
        case VAL_OBJECT: return "object";
        case VAL_FILE: return "file";
        case VAL_FUNCTION: return "function";
        case VAL_TASK: return "task";
        case VAL_CHANNEL: return "channel";
        default: return "unknown";
    }
}

// Convert a value to a string representation (allocates memory)
static char* value_to_string_alloc(Value v) {
    char buf[256];
    switch (v.type) {
        case VAL_NULL:
            return strdup("null");
        case VAL_BOOL:
            return strdup(v.as.as_bool ? "true" : "false");
        case VAL_I8:
            snprintf(buf, sizeof(buf), "%d", v.as.as_i8);
            return strdup(buf);
        case VAL_I16:
            snprintf(buf, sizeof(buf), "%d", v.as.as_i16);
            return strdup(buf);
        case VAL_I32:
            snprintf(buf, sizeof(buf), "%d", v.as.as_i32);
            return strdup(buf);
        case VAL_I64:
            snprintf(buf, sizeof(buf), "%ld", (long)v.as.as_i64);
            return strdup(buf);
        case VAL_U8:
            snprintf(buf, sizeof(buf), "%u", v.as.as_u8);
            return strdup(buf);
        case VAL_U16:
            snprintf(buf, sizeof(buf), "%u", v.as.as_u16);
            return strdup(buf);
        case VAL_U32:
            snprintf(buf, sizeof(buf), "%u", v.as.as_u32);
            return strdup(buf);
        case VAL_U64:
            snprintf(buf, sizeof(buf), "%lu", (unsigned long)v.as.as_u64);
            return strdup(buf);
        case VAL_F32:
        case VAL_F64: {
            double d = v.type == VAL_F32 ? v.as.as_f32 : v.as.as_f64;
            if (d == (long)d) {
                snprintf(buf, sizeof(buf), "%.0f", d);
            } else {
                snprintf(buf, sizeof(buf), "%g", d);
            }
            return strdup(buf);
        }
        case VAL_STRING:
            if (v.as.as_string) {
                return strdup(v.as.as_string->data);
            }
            return strdup("");
        case VAL_RUNE: {
            // Convert rune to UTF-8 string
            uint32_t r = v.as.as_rune;
            if (r < 0x80) {
                buf[0] = (char)r;
                buf[1] = '\0';
            } else if (r < 0x800) {
                buf[0] = (char)(0xC0 | (r >> 6));
                buf[1] = (char)(0x80 | (r & 0x3F));
                buf[2] = '\0';
            } else if (r < 0x10000) {
                buf[0] = (char)(0xE0 | (r >> 12));
                buf[1] = (char)(0x80 | ((r >> 6) & 0x3F));
                buf[2] = (char)(0x80 | (r & 0x3F));
                buf[3] = '\0';
            } else {
                buf[0] = (char)(0xF0 | (r >> 18));
                buf[1] = (char)(0x80 | ((r >> 12) & 0x3F));
                buf[2] = (char)(0x80 | ((r >> 6) & 0x3F));
                buf[3] = (char)(0x80 | (r & 0x3F));
                buf[4] = '\0';
            }
            return strdup(buf);
        }
        case VAL_ARRAY:
            return strdup("[array]");
        case VAL_OBJECT:
            return strdup("[object]");
        case VAL_FUNCTION:
            return strdup("[function]");
        default:
            return strdup("[unknown]");
    }
}

// ============================================
// Stdlib Initialization
// ============================================

// Forward declaration
void vm_define_global(VM *vm, const char *name, Value value, bool is_const);

static void vm_init_stdlib(VM *vm) {
    // Math constants (both __ prefixed and non-prefixed for bundler compatibility)
    vm_define_global(vm, "__PI", stdlib_val_f64(M_PI), true);
    vm_define_global(vm, "__E", stdlib_val_f64(M_E), true);
    vm_define_global(vm, "__TAU", stdlib_val_f64(2.0 * M_PI), true);
    vm_define_global(vm, "__INF", stdlib_val_f64(INFINITY), true);
    vm_define_global(vm, "__NAN", stdlib_val_f64(NAN), true);

    // Math functions - using VM-specific implementations
    // __ prefixed versions (for stdlib/math.hml exports)
    vm_define_global(vm, "__sin", vm_val_builtin_fn((BuiltinFn)vm_builtin_sin), true);
    vm_define_global(vm, "__cos", vm_val_builtin_fn((BuiltinFn)vm_builtin_cos), true);
    vm_define_global(vm, "__tan", vm_val_builtin_fn((BuiltinFn)vm_builtin_tan), true);
    vm_define_global(vm, "__asin", vm_val_builtin_fn((BuiltinFn)vm_builtin_asin), true);
    vm_define_global(vm, "__acos", vm_val_builtin_fn((BuiltinFn)vm_builtin_acos), true);
    vm_define_global(vm, "__atan", vm_val_builtin_fn((BuiltinFn)vm_builtin_atan), true);
    vm_define_global(vm, "__atan2", vm_val_builtin_fn((BuiltinFn)vm_builtin_atan2), true);
    vm_define_global(vm, "__sqrt", vm_val_builtin_fn((BuiltinFn)vm_builtin_sqrt), true);
    vm_define_global(vm, "__pow", vm_val_builtin_fn((BuiltinFn)vm_builtin_pow), true);
    vm_define_global(vm, "__exp", vm_val_builtin_fn((BuiltinFn)vm_builtin_exp), true);
    vm_define_global(vm, "__log", vm_val_builtin_fn((BuiltinFn)vm_builtin_log), true);
    vm_define_global(vm, "__log10", vm_val_builtin_fn((BuiltinFn)vm_builtin_log10), true);
    vm_define_global(vm, "__log2", vm_val_builtin_fn((BuiltinFn)vm_builtin_log2), true);
    vm_define_global(vm, "__floor", vm_val_builtin_fn((BuiltinFn)vm_builtin_floor), true);
    vm_define_global(vm, "__ceil", vm_val_builtin_fn((BuiltinFn)vm_builtin_ceil), true);
    vm_define_global(vm, "__round", vm_val_builtin_fn((BuiltinFn)vm_builtin_round), true);
    vm_define_global(vm, "__trunc", vm_val_builtin_fn((BuiltinFn)vm_builtin_trunc), true);
    vm_define_global(vm, "__abs", vm_val_builtin_fn((BuiltinFn)vm_builtin_abs), true);
    vm_define_global(vm, "__min", vm_val_builtin_fn((BuiltinFn)vm_builtin_min), true);
    vm_define_global(vm, "__max", vm_val_builtin_fn((BuiltinFn)vm_builtin_max), true);
    vm_define_global(vm, "__clamp", vm_val_builtin_fn((BuiltinFn)vm_builtin_clamp), true);
    vm_define_global(vm, "__rand", vm_val_builtin_fn((BuiltinFn)vm_builtin_rand), true);
    vm_define_global(vm, "__rand_range", vm_val_builtin_fn((BuiltinFn)vm_builtin_rand_range), true);
    vm_define_global(vm, "__seed", vm_val_builtin_fn((BuiltinFn)vm_builtin_seed), true);

    // Non-prefixed versions (for bundler which skips stdlib exports for these)
    vm_define_global(vm, "sin", vm_val_builtin_fn((BuiltinFn)vm_builtin_sin), true);
    vm_define_global(vm, "cos", vm_val_builtin_fn((BuiltinFn)vm_builtin_cos), true);
    vm_define_global(vm, "tan", vm_val_builtin_fn((BuiltinFn)vm_builtin_tan), true);
    vm_define_global(vm, "asin", vm_val_builtin_fn((BuiltinFn)vm_builtin_asin), true);
    vm_define_global(vm, "acos", vm_val_builtin_fn((BuiltinFn)vm_builtin_acos), true);
    vm_define_global(vm, "atan", vm_val_builtin_fn((BuiltinFn)vm_builtin_atan), true);
    vm_define_global(vm, "atan2", vm_val_builtin_fn((BuiltinFn)vm_builtin_atan2), true);
    vm_define_global(vm, "sqrt", vm_val_builtin_fn((BuiltinFn)vm_builtin_sqrt), true);
    vm_define_global(vm, "pow", vm_val_builtin_fn((BuiltinFn)vm_builtin_pow), true);
    vm_define_global(vm, "exp", vm_val_builtin_fn((BuiltinFn)vm_builtin_exp), true);
    vm_define_global(vm, "log", vm_val_builtin_fn((BuiltinFn)vm_builtin_log), true);
    vm_define_global(vm, "log10", vm_val_builtin_fn((BuiltinFn)vm_builtin_log10), true);
    vm_define_global(vm, "log2", vm_val_builtin_fn((BuiltinFn)vm_builtin_log2), true);
    vm_define_global(vm, "floor", vm_val_builtin_fn((BuiltinFn)vm_builtin_floor), true);
    vm_define_global(vm, "ceil", vm_val_builtin_fn((BuiltinFn)vm_builtin_ceil), true);
    vm_define_global(vm, "round", vm_val_builtin_fn((BuiltinFn)vm_builtin_round), true);
    vm_define_global(vm, "trunc", vm_val_builtin_fn((BuiltinFn)vm_builtin_trunc), true);

    // Integer math builtins
    vm_define_global(vm, "__floori", vm_val_builtin_fn((BuiltinFn)vm_builtin_floori), true);
    vm_define_global(vm, "__ceili", vm_val_builtin_fn((BuiltinFn)vm_builtin_ceili), true);
    vm_define_global(vm, "__roundi", vm_val_builtin_fn((BuiltinFn)vm_builtin_roundi), true);
    vm_define_global(vm, "__trunci", vm_val_builtin_fn((BuiltinFn)vm_builtin_trunci), true);
    vm_define_global(vm, "__div", vm_val_builtin_fn((BuiltinFn)vm_builtin_div), true);
    vm_define_global(vm, "__divi", vm_val_builtin_fn((BuiltinFn)vm_builtin_divi), true);
    vm_define_global(vm, "floori", vm_val_builtin_fn((BuiltinFn)vm_builtin_floori), true);
    vm_define_global(vm, "ceili", vm_val_builtin_fn((BuiltinFn)vm_builtin_ceili), true);
    vm_define_global(vm, "roundi", vm_val_builtin_fn((BuiltinFn)vm_builtin_roundi), true);
    vm_define_global(vm, "trunci", vm_val_builtin_fn((BuiltinFn)vm_builtin_trunci), true);
    vm_define_global(vm, "div", vm_val_builtin_fn((BuiltinFn)vm_builtin_div), true);
    vm_define_global(vm, "divi", vm_val_builtin_fn((BuiltinFn)vm_builtin_divi), true);

    // Environment builtins
    vm_define_global(vm, "__getenv", vm_val_builtin_fn((BuiltinFn)vm_builtin_getenv), true);
    vm_define_global(vm, "__setenv", vm_val_builtin_fn((BuiltinFn)vm_builtin_setenv), true);
    vm_define_global(vm, "__unsetenv", vm_val_builtin_fn((BuiltinFn)vm_builtin_unsetenv), true);
    vm_define_global(vm, "getenv", vm_val_builtin_fn((BuiltinFn)vm_builtin_getenv), true);
    vm_define_global(vm, "setenv", vm_val_builtin_fn((BuiltinFn)vm_builtin_setenv), true);
    vm_define_global(vm, "unsetenv", vm_val_builtin_fn((BuiltinFn)vm_builtin_unsetenv), true);

    // Time builtins
    vm_define_global(vm, "__now", vm_val_builtin_fn((BuiltinFn)vm_builtin_now), true);
    vm_define_global(vm, "__time_ms", vm_val_builtin_fn((BuiltinFn)vm_builtin_time_ms), true);
    vm_define_global(vm, "__sleep", vm_val_builtin_fn((BuiltinFn)vm_builtin_sleep), true);
    vm_define_global(vm, "__clock", vm_val_builtin_fn((BuiltinFn)vm_builtin_clock), true);

    // Platform builtins
    vm_define_global(vm, "__platform", vm_val_builtin_fn((BuiltinFn)vm_builtin_platform), true);
    vm_define_global(vm, "__arch", vm_val_builtin_fn((BuiltinFn)vm_builtin_arch), true);

    // File system builtins
    vm_define_global(vm, "__exists", vm_val_builtin_fn((BuiltinFn)vm_builtin_exists), true);
    vm_define_global(vm, "__is_file", vm_val_builtin_fn((BuiltinFn)vm_builtin_is_file), true);
    vm_define_global(vm, "__is_dir", vm_val_builtin_fn((BuiltinFn)vm_builtin_is_dir), true);

    // Process builtins
    vm_define_global(vm, "__get_pid", vm_val_builtin_fn((BuiltinFn)vm_builtin_get_pid), true);
    vm_define_global(vm, "__getppid", vm_val_builtin_fn((BuiltinFn)vm_builtin_getppid), true);
    vm_define_global(vm, "__getuid", vm_val_builtin_fn((BuiltinFn)vm_builtin_getuid), true);
    vm_define_global(vm, "__geteuid", vm_val_builtin_fn((BuiltinFn)vm_builtin_geteuid), true);
    vm_define_global(vm, "__exit", vm_val_builtin_fn((BuiltinFn)vm_builtin_exit), true);
    vm_define_global(vm, "get_pid", vm_val_builtin_fn((BuiltinFn)vm_builtin_get_pid), true);

    // String builtins
    vm_define_global(vm, "__strlen", vm_val_builtin_fn((BuiltinFn)vm_builtin_strlen), true);

    // Hash builtins
    vm_define_global(vm, "__sha256", vm_val_builtin_fn((BuiltinFn)vm_builtin_sha256), true);
    vm_define_global(vm, "__sha512", vm_val_builtin_fn((BuiltinFn)vm_builtin_sha512), true);
    vm_define_global(vm, "__md5", vm_val_builtin_fn((BuiltinFn)vm_builtin_md5), true);

    // File I/O builtins
    vm_define_global(vm, "__read_file", vm_val_builtin_fn((BuiltinFn)vm_builtin_read_file), true);
    vm_define_global(vm, "__write_file", vm_val_builtin_fn((BuiltinFn)vm_builtin_write_file), true);
    vm_define_global(vm, "__append_file", vm_val_builtin_fn((BuiltinFn)vm_builtin_append_file), true);
    vm_define_global(vm, "__remove_file", vm_val_builtin_fn((BuiltinFn)vm_builtin_remove_file), true);
    vm_define_global(vm, "__cwd", vm_val_builtin_fn((BuiltinFn)vm_builtin_cwd), true);
    vm_define_global(vm, "__chdir", vm_val_builtin_fn((BuiltinFn)vm_builtin_chdir), true);
    vm_define_global(vm, "__rename", vm_val_builtin_fn((BuiltinFn)vm_builtin_rename), true);
    vm_define_global(vm, "__make_dir", vm_val_builtin_fn((BuiltinFn)vm_builtin_make_dir), true);
    vm_define_global(vm, "__remove_dir", vm_val_builtin_fn((BuiltinFn)vm_builtin_remove_dir), true);
    vm_define_global(vm, "__list_dir", vm_val_builtin_fn((BuiltinFn)vm_builtin_list_dir), true);
}

// ============================================
// VM Lifecycle
// ============================================

VM* vm_new(void) {
    VM *vm = malloc(sizeof(VM));
    if (!vm) return NULL;

    // Initialize stack (use calloc to zero out memory)
    vm->stack = calloc(VM_STACK_INITIAL, sizeof(Value));
    vm->stack_top = vm->stack;
    vm->stack_capacity = VM_STACK_INITIAL;

    // Initialize call frames
    vm->frames = malloc(sizeof(CallFrame) * VM_FRAMES_INITIAL);
    vm->frame_count = 0;
    vm->frame_capacity = VM_FRAMES_INITIAL;

    // Initialize globals
    vm->globals.names = malloc(sizeof(char*) * VM_GLOBALS_INITIAL);
    vm->globals.values = malloc(sizeof(Value) * VM_GLOBALS_INITIAL);
    vm->globals.is_const = malloc(sizeof(bool) * VM_GLOBALS_INITIAL);
    vm->globals.count = 0;
    vm->globals.capacity = VM_GLOBALS_INITIAL;
    vm->globals.hash_table = NULL;
    vm->globals.hash_capacity = 0;

    // Control flow state
    vm->is_returning = false;
    vm->return_value = vm_null_value();
    vm->is_throwing = false;
    vm->exception = vm_null_value();
    vm->exception_frame = NULL;
    vm->is_breaking = false;
    vm->is_continuing = false;

    // Defers
    vm->defers = malloc(sizeof(DeferEntry) * VM_DEFER_INITIAL);
    vm->defer_count = 0;
    vm->defer_capacity = VM_DEFER_INITIAL;

    // Exception handlers
    vm->handlers = malloc(sizeof(ExceptionHandler) * 16);
    vm->handler_count = 0;
    vm->handler_capacity = 16;

    // Module cache
    vm->module_cache.paths = NULL;
    vm->module_cache.modules = NULL;
    vm->module_cache.count = 0;
    vm->module_cache.capacity = 0;

    // GC/memory
    vm->open_upvalues = NULL;
    vm->objects = NULL;
    vm->bytes_allocated = 0;
    vm->next_gc = 1024 * 1024;  // 1MB

    vm->max_stack_depth = 1024;
    vm->task = NULL;

    vm->pending_error = NULL;

    // Initialize stdlib globals
    vm_init_stdlib(vm);

    return vm;
}

void vm_free(VM *vm) {
    if (!vm) return;

    free(vm->stack);
    free(vm->frames);

    // Free globals
    for (int i = 0; i < vm->globals.count; i++) {
        free(vm->globals.names[i]);
        // TODO: VALUE_RELEASE(vm->globals.values[i]);
    }
    free(vm->globals.names);
    free(vm->globals.values);
    free(vm->globals.is_const);
    free(vm->globals.hash_table);

    free(vm->defers);

    // Free module cache
    for (int i = 0; i < vm->module_cache.count; i++) {
        free(vm->module_cache.paths[i]);
    }
    free(vm->module_cache.paths);
    free(vm->module_cache.modules);

    // TODO: Free all allocated objects

    free(vm);
}

void vm_reset(VM *vm) {
    vm->stack_top = vm->stack;
    vm->frame_count = 0;
    vm->is_returning = false;
    vm->is_throwing = false;
    vm->is_breaking = false;
    vm->is_continuing = false;
    vm->defer_count = 0;
}

// ============================================
// Stack Operations
// ============================================

void vm_push(VM *vm, Value value) {
    if (vm->stack_top - vm->stack >= vm->stack_capacity) {
        // Grow stack
        int old_top = vm->stack_top - vm->stack;
        vm->stack_capacity *= 2;
        vm->stack = realloc(vm->stack, sizeof(Value) * vm->stack_capacity);
        vm->stack_top = vm->stack + old_top;
    }
    *vm->stack_top++ = value;
}

Value vm_pop(VM *vm) {
    if (vm->stack_top <= vm->stack) {
        vm_runtime_error(vm, "Stack underflow");
        return vm_null_value();
    }
    return *--vm->stack_top;
}

Value vm_peek(VM *vm, int distance) {
    return vm->stack_top[-1 - distance];
}

void vm_popn(VM *vm, int count) {
    vm->stack_top -= count;
    if (vm->stack_top < vm->stack) {
        vm->stack_top = vm->stack;
        vm_runtime_error(vm, "Stack underflow");
    }
}

// ============================================
// Globals
// ============================================

static uint32_t hash_string_vm(const char *str) {
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= (uint8_t)*str++;
        hash *= 16777619;
    }
    return hash;
}

void vm_define_global(VM *vm, const char *name, Value value, bool is_const) {
    // Check for existing
    for (int i = 0; i < vm->globals.count; i++) {
        if (strcmp(vm->globals.names[i], name) == 0) {
            vm->globals.values[i] = value;
            return;
        }
    }

    // Add new
    if (vm->globals.count >= vm->globals.capacity) {
        vm->globals.capacity *= 2;
        vm->globals.names = realloc(vm->globals.names, sizeof(char*) * vm->globals.capacity);
        vm->globals.values = realloc(vm->globals.values, sizeof(Value) * vm->globals.capacity);
        vm->globals.is_const = realloc(vm->globals.is_const, sizeof(bool) * vm->globals.capacity);
    }

    vm->globals.names[vm->globals.count] = strdup(name);
    vm->globals.values[vm->globals.count] = value;
    vm->globals.is_const[vm->globals.count] = is_const;
    vm->globals.count++;
}

bool vm_get_global(VM *vm, const char *name, Value *out) {
    for (int i = 0; i < vm->globals.count; i++) {
        if (strcmp(vm->globals.names[i], name) == 0) {
            *out = vm->globals.values[i];
            return true;
        }
    }
    return false;
}

// Helper macros for setting catchable errors from static helper functions
// After calling a helper function, check vm->pending_error and handle it
#define SET_ERROR(vm, msg) do { (vm)->pending_error = (msg); } while(0)
#define SET_ERROR_FMT(vm, fmt, ...) do { \
    snprintf((vm)->error_buf, sizeof((vm)->error_buf), fmt, ##__VA_ARGS__); \
    (vm)->pending_error = (vm)->error_buf; \
} while(0)

bool vm_set_global(VM *vm, const char *name, Value value) {
    for (int i = 0; i < vm->globals.count; i++) {
        if (strcmp(vm->globals.names[i], name) == 0) {
            if (vm->globals.is_const[i]) {
                SET_ERROR_FMT(vm, "Cannot reassign constant '%s'", name);
                return false;
            }
            vm->globals.values[i] = value;
            return true;
        }
    }
    SET_ERROR_FMT(vm, "Undefined variable '%s'", name);
    return false;
}

void vm_set_args(VM *vm, int argc, char **argv) {
    // Create an array of strings from command-line arguments
    Array *arr = malloc(sizeof(Array));
    arr->elements = malloc(sizeof(Value) * (argc > 0 ? argc : 1));
    arr->length = argc;
    arr->capacity = argc > 0 ? argc : 1;
    arr->element_type = NULL;
    arr->ref_count = 1;

    for (int i = 0; i < argc; i++) {
        arr->elements[i] = vm_make_string(argv[i], strlen(argv[i]));
    }

    Value args_val = {.type = VAL_ARRAY, .as.as_array = arr};
    vm_define_global(vm, "args", args_val, false);
}

// ============================================
// VM Closures
// ============================================

VMClosure* vm_closure_new(Chunk *chunk) {
    VMClosure *closure = malloc(sizeof(VMClosure));
    closure->chunk = chunk;
    closure->upvalue_count = chunk->upvalue_count;
    if (closure->upvalue_count > 0) {
        closure->upvalues = malloc(sizeof(ObjUpvalue*) * closure->upvalue_count);
        for (int i = 0; i < closure->upvalue_count; i++) {
            closure->upvalues[i] = NULL;
        }
    } else {
        closure->upvalues = NULL;
    }
    closure->ref_count = 1;
    return closure;
}

void vm_closure_free(VMClosure *closure) {
    if (!closure) return;
    if (--closure->ref_count <= 0) {
        free(closure->upvalues);
        // Note: Don't free the chunk - it's owned by the constant pool
        free(closure);
    }
}

// Create a Value from a VMClosure (stores as function pointer)
static Value val_vm_closure(VMClosure *closure) {
    Value v;
    v.type = VAL_FUNCTION;
    // Store the closure pointer - we'll cast it back when calling
    v.as.as_function = (Function*)closure;
    return v;
}

// Check if a Value is a VM closure (vs an interpreter function)
// VM closures have a special marker - they came from the VM context
// For now, all functions in the VM are closures
static bool is_vm_closure(Value v) {
    return v.type == VAL_FUNCTION && v.as.as_function != NULL;
}

// Get the VMClosure from a Value
static VMClosure* as_vm_closure(Value v) {
    return (VMClosure*)v.as.as_function;
}

// ============================================
// Error Handling
// ============================================

void vm_runtime_error(VM *vm, const char *format, ...) {
    fflush(stdout);  // Ensure all output is printed before error
    va_list args;
    va_start(args, format);
    fprintf(stderr, "Runtime error: ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);

    // Print stack trace
    vm_print_stack_trace(vm);

    vm->is_throwing = true;
}

int vm_current_line(VM *vm) {
    if (vm->frame_count == 0) return 0;
    CallFrame *frame = &vm->frames[vm->frame_count - 1];
    int offset = frame->ip - frame->chunk->code;
    return chunk_get_line(frame->chunk, offset);
}

void vm_print_stack_trace(VM *vm) {
    for (int i = vm->frame_count - 1; i >= 0; i--) {
        CallFrame *frame = &vm->frames[i];
        int offset = frame->ip - frame->chunk->code;
        int line = chunk_get_line(frame->chunk, offset);
        fprintf(stderr, "  at %s:%d\n",
                frame->chunk->name ? frame->chunk->name : "<script>", line);
    }
}

// ============================================
// Upvalues (for closures)
// ============================================

ObjUpvalue* vm_capture_upvalue(VM *vm, Value *local) {
    ObjUpvalue *prev = NULL;
    ObjUpvalue *upvalue = vm->open_upvalues;

    // Find existing upvalue or insertion point
    while (upvalue != NULL && upvalue->location > local) {
        prev = upvalue;
        upvalue = upvalue->next;
    }

    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }

    // Create new upvalue
    ObjUpvalue *new_upvalue = malloc(sizeof(ObjUpvalue));
    new_upvalue->location = local;
    new_upvalue->closed = vm_null_value();
    new_upvalue->next = upvalue;

    if (prev == NULL) {
        vm->open_upvalues = new_upvalue;
    } else {
        prev->next = new_upvalue;
    }

    return new_upvalue;
}

void vm_close_upvalues(VM *vm, Value *last) {
    while (vm->open_upvalues != NULL && vm->open_upvalues->location >= last) {
        ObjUpvalue *upvalue = vm->open_upvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm->open_upvalues = upvalue->next;
    }
}

// ============================================
// Binary Operations with Type Promotion
// ============================================

// Determine result type for binary operation
static ValueType promote_types(ValueType a, ValueType b) {
    // Float always wins
    if (is_float(a) || is_float(b)) {
        return VAL_F64;
    }

    // Both are integers - use larger type
    int rank_a = a - VAL_I8;
    int rank_b = b - VAL_I8;
    return (rank_a > rank_b) ? a : b;
}

// Create value of specified type from int64
static Value val_int_typed(int64_t val, ValueType type) {
    Value v;
    v.type = type;
    switch (type) {
        case VAL_I8:  v.as.as_i8 = (int8_t)val; break;
        case VAL_I16: v.as.as_i16 = (int16_t)val; break;
        case VAL_I32: v.as.as_i32 = (int32_t)val; break;
        case VAL_I64: v.as.as_i64 = val; break;
        case VAL_U8:  v.as.as_u8 = (uint8_t)val; break;
        case VAL_U16: v.as.as_u16 = (uint16_t)val; break;
        case VAL_U32: v.as.as_u32 = (uint32_t)val; break;
        case VAL_U64: v.as.as_u64 = (uint64_t)val; break;
        default: v.type = VAL_I32; v.as.as_i32 = (int32_t)val; break;
    }
    return v;
}

// Create value of specified float type
static Value val_float_typed(double val, ValueType type) {
    Value v;
    v.type = type;
    if (type == VAL_F32) {
        v.as.as_f32 = (float)val;
    } else {
        v.as.as_f64 = val;
    }
    return v;
}

// Binary arithmetic with type promotion
static Value binary_add(VM *vm, Value a, Value b) {
    (void)vm; // May be unused after removing error

    // i32 fast path
    if (a.type == VAL_I32 && b.type == VAL_I32) {
        return val_i32_vm(a.as.as_i32 + b.as.as_i32);
    }

    // String concatenation - if either operand is a string, convert and concat
    if (a.type == VAL_STRING || b.type == VAL_STRING) {
        char *str_a = value_to_string_alloc(a);
        char *str_b = value_to_string_alloc(b);
        int len_a = strlen(str_a);
        int len_b = strlen(str_b);
        int total_len = len_a + len_b;

        String *s = malloc(sizeof(String));
        s->data = malloc(total_len + 1);
        memcpy(s->data, str_a, len_a);
        memcpy(s->data + len_a, str_b, len_b);
        s->data[total_len] = '\0';
        s->length = total_len;
        s->char_length = -1;
        s->capacity = total_len + 1;
        s->ref_count = 1;

        free(str_a);
        free(str_b);

        Value v = {.type = VAL_STRING, .as.as_string = s};
        return v;
    }

    // Numeric
    if (is_numeric(a.type) && is_numeric(b.type)) {
        if (is_float(a.type) || is_float(b.type)) {
            // Float promotion: use larger float type, or f32 if int + f32
            ValueType result_type = VAL_F64;
            if ((a.type == VAL_F32 || b.type == VAL_F32) &&
                a.type != VAL_F64 && b.type != VAL_F64) {
                result_type = VAL_F32;
            }
            return val_float_typed(value_to_f64(a) + value_to_f64(b), result_type);
        }
        // Both integers - use proper promoted type
        ValueType result_type = promote_types(a.type, b.type);
        return val_int_typed(value_to_i64(a) + value_to_i64(b), result_type);
    }

    SET_ERROR_FMT(vm, "Cannot add %s and %s",
                  val_type_name(a.type), val_type_name(b.type));
    return vm_null_value();
}

static Value binary_sub(VM *vm, Value a, Value b) {
    if (a.type == VAL_I32 && b.type == VAL_I32) {
        return val_i32_vm(a.as.as_i32 - b.as.as_i32);
    }
    if (is_numeric(a.type) && is_numeric(b.type)) {
        if (is_float(a.type) || is_float(b.type)) {
            ValueType result_type = VAL_F64;
            if ((a.type == VAL_F32 || b.type == VAL_F32) &&
                a.type != VAL_F64 && b.type != VAL_F64) {
                result_type = VAL_F32;
            }
            return val_float_typed(value_to_f64(a) - value_to_f64(b), result_type);
        }
        ValueType result_type = promote_types(a.type, b.type);
        return val_int_typed(value_to_i64(a) - value_to_i64(b), result_type);
    }
    SET_ERROR_FMT(vm, "Cannot subtract %s and %s",
                  val_type_name(a.type), val_type_name(b.type));
    return vm_null_value();
}

static Value binary_mul(VM *vm, Value a, Value b) {
    if (a.type == VAL_I32 && b.type == VAL_I32) {
        return val_i32_vm(a.as.as_i32 * b.as.as_i32);
    }
    if (is_numeric(a.type) && is_numeric(b.type)) {
        if (is_float(a.type) || is_float(b.type)) {
            ValueType result_type = VAL_F64;
            if ((a.type == VAL_F32 || b.type == VAL_F32) &&
                a.type != VAL_F64 && b.type != VAL_F64) {
                result_type = VAL_F32;
            }
            return val_float_typed(value_to_f64(a) * value_to_f64(b), result_type);
        }
        ValueType result_type = promote_types(a.type, b.type);
        return val_int_typed(value_to_i64(a) * value_to_i64(b), result_type);
    }
    SET_ERROR_FMT(vm, "Cannot multiply %s and %s",
                  val_type_name(a.type), val_type_name(b.type));
    return vm_null_value();
}

static Value binary_div(VM *vm, Value a, Value b) {
    // Division always returns f64 (Hemlock semantics)
    double bval = value_to_f64(b);
    if (bval == 0.0) {
        SET_ERROR(vm, "Division by zero");
        return vm_null_value();
    }
    return val_f64_vm(value_to_f64(a) / bval);
}

static Value binary_mod(VM *vm, Value a, Value b) {
    if (a.type == VAL_I32 && b.type == VAL_I32) {
        if (b.as.as_i32 == 0) {
            SET_ERROR(vm, "Division by zero");
            return vm_null_value();
        }
        return val_i32_vm(a.as.as_i32 % b.as.as_i32);
    }
    if (is_numeric(a.type) && is_numeric(b.type)) {
        if (is_float(a.type) || is_float(b.type)) {
            double bval = value_to_f64(b);
            if (bval == 0.0) {
                SET_ERROR(vm, "Division by zero");
                return vm_null_value();
            }
            return val_f64_vm(fmod(value_to_f64(a), bval));
        }
        int64_t bval = value_to_i64(b);
        if (bval == 0) {
            SET_ERROR(vm, "Division by zero");
            return vm_null_value();
        }
        if (a.type == VAL_I64 || b.type == VAL_I64) {
            return val_i64_vm(value_to_i64(a) % bval);
        }
        return val_i32_vm((int32_t)(value_to_i64(a) % bval));
    }
    SET_ERROR_FMT(vm, "Cannot modulo %s and %s",
                  val_type_name(a.type), val_type_name(b.type));
    return vm_null_value();
}

// Comparison operations
static Value binary_eq(Value a, Value b) {
    if (a.type != b.type) {
        // Cross-type numeric comparison
        if (is_numeric(a.type) && is_numeric(b.type)) {
            return val_bool_vm(value_to_f64(a) == value_to_f64(b));
        }
        return val_bool_vm(0);
    }
    switch (a.type) {
        case VAL_NULL: return val_bool_vm(1);
        case VAL_BOOL: return val_bool_vm(a.as.as_bool == b.as.as_bool);
        case VAL_I32: return val_bool_vm(a.as.as_i32 == b.as.as_i32);
        case VAL_I64: return val_bool_vm(a.as.as_i64 == b.as.as_i64);
        case VAL_F64: return val_bool_vm(a.as.as_f64 == b.as.as_f64);
        case VAL_RUNE: return val_bool_vm(a.as.as_rune == b.as.as_rune);
        case VAL_STRING:
            if (a.as.as_string == b.as.as_string) return val_bool_vm(1);
            if (!a.as.as_string || !b.as.as_string) return val_bool_vm(0);
            return val_bool_vm(strcmp(a.as.as_string->data, b.as.as_string->data) == 0);
        default:
            // Pointer equality for other types
            return val_bool_vm(a.as.as_ptr == b.as.as_ptr);
    }
}

static Value binary_lt(VM *vm, Value a, Value b) {
    if (a.type == VAL_I32 && b.type == VAL_I32) {
        return val_bool_vm(a.as.as_i32 < b.as.as_i32);
    }
    if (is_numeric(a.type) && is_numeric(b.type)) {
        return val_bool_vm(value_to_f64(a) < value_to_f64(b));
    }
    if (a.type == VAL_STRING && b.type == VAL_STRING) {
        return val_bool_vm(strcmp(a.as.as_string->data, b.as.as_string->data) < 0);
    }
    SET_ERROR_FMT(vm, "Cannot compare %s and %s",
                  val_type_name(a.type), val_type_name(b.type));
    return vm_null_value();
}

// ============================================
// Closure Call Helper (for array methods)
// ============================================

// Forward declaration of the execution loop
static VMResult vm_execute(VM *vm, int base_frame_count);

// Call a closure and return its result
// Returns vm_null_value() on error
static Value vm_call_closure(VM *vm, VMClosure *closure, Value *args, int argc) {
    // Push closure to stack (using val_vm_closure helper)
    Value closure_val = val_vm_closure(closure);
    *vm->stack_top++ = closure_val;

    // Push arguments
    for (int i = 0; i < argc; i++) {
        *vm->stack_top++ = args[i];
    }

    // Save base frame count to know when callback returns
    int base_frame_count = vm->frame_count;

    // Set up call frame (similar to BC_CALL)
    if (vm->frame_count >= vm->frame_capacity) {
        vm->frame_capacity *= 2;
        vm->frames = realloc(vm->frames, sizeof(CallFrame) * vm->frame_capacity);
    }

    Chunk *fn_chunk = closure->chunk;
    CallFrame *new_frame = &vm->frames[vm->frame_count++];
    new_frame->chunk = fn_chunk;
    new_frame->ip = fn_chunk->code;
    new_frame->slots = vm->stack_top - argc - 1;
    new_frame->upvalues = NULL;
    new_frame->slot_count = fn_chunk->local_count;

    // Store closure in slot 0 for upvalue access
    new_frame->slots[0] = closure_val;

    // Execute until we return to base frame
    VMResult result = vm_execute(vm, base_frame_count);

    if (result != VM_OK) {
        return vm_null_value();
    }

    // Result should be on stack (pushed by BC_RETURN)
    if (vm->stack_top > vm->stack) {
        return *--vm->stack_top;
    }
    return vm_null_value();
}

// ============================================
// Main Execution Loop
// ============================================

// Core execution loop - runs until frame_count reaches base_frame_count
static VMResult vm_execute(VM *vm, int base_frame_count) {
    CallFrame *frame = &vm->frames[vm->frame_count - 1];
    uint8_t *ip = frame->ip;
    Value *slots = frame->slots;

// Undefine instruction.h's parametrized macros, use local versions
#undef READ_BYTE
#undef READ_SHORT
#define READ_BYTE() (*ip++)
#define READ_SHORT() (ip += 2, (uint16_t)((ip[-2] << 8) | ip[-1]))
#define READ_CONSTANT() (frame->chunk->constants[READ_SHORT()])
#define PUSH(v) (*vm->stack_top++ = (v))
#define POP() (*--vm->stack_top)
#define PEEK(n) (vm->stack_top[-1 - (n)])

// Macro for throwing catchable errors
// If a handler exists, jump to it; otherwise return runtime error
#define THROW_ERROR(msg) do { \
    pending_exception_msg = (msg); \
    goto handle_exception; \
} while(0)

#define THROW_ERROR_FMT(fmt, ...) do { \
    snprintf(exception_buf, sizeof(exception_buf), fmt, ##__VA_ARGS__); \
    pending_exception_msg = exception_buf; \
    goto handle_exception; \
} while(0)

    // Exception handling state
    const char *pending_exception_msg = NULL;
    char exception_buf[256];

    for (;;) {
        if (vm_trace_enabled) {
            // Print stack
            printf("          ");
            for (Value *slot = vm->stack; slot < vm->stack_top; slot++) {
                printf("[ ");
                print_value(*slot);
                printf(" ]");
            }
            printf("\n");
            disassemble_instruction(frame->chunk, (int)(ip - frame->chunk->code));
        }

        uint8_t instruction = READ_BYTE();

        switch (instruction) {
            // Constants
            case BC_CONST: {
                Constant c = READ_CONSTANT();
                Value v;
                switch (c.type) {
                    case CONST_I32: v = val_i32_vm(c.as.i32); break;
                    case CONST_I64: v = val_i64_vm(c.as.i64); break;
                    case CONST_F64: v = val_f64_vm(c.as.f64); break;
                    case CONST_RUNE:
                        v.type = VAL_RUNE;
                        v.as.as_rune = c.as.rune;
                        break;
                    case CONST_STRING: {
                        // Create string value
                        String *s = malloc(sizeof(String));
                        s->data = strdup(c.as.string.data);
                        s->length = c.as.string.length;
                        s->char_length = -1;
                        s->capacity = c.as.string.length + 1;
                        s->ref_count = 1;
                        v.type = VAL_STRING;
                        v.as.as_string = s;
                        break;
                    }
                    default:
                        v = vm_null_value();
                }
                PUSH(v);
                break;
            }

            case BC_CONST_BYTE: {
                PUSH(val_i32_vm(READ_BYTE()));
                break;
            }

            case BC_NULL:
                PUSH(vm_null_value());
                break;

            case BC_TRUE:
                PUSH(val_bool_vm(1));
                break;

            case BC_FALSE:
                PUSH(val_bool_vm(0));
                break;

            case BC_ARRAY: {
                uint16_t count = READ_SHORT();
                // Allocate array
                Array *arr = malloc(sizeof(Array));
                arr->elements = malloc(sizeof(Value) * (count > 0 ? count : 1));
                arr->length = count;
                arr->capacity = count > 0 ? count : 1;
                arr->element_type = NULL;
                arr->ref_count = 1;
                // Pop elements from stack (they're in reverse order)
                for (int i = count - 1; i >= 0; i--) {
                    arr->elements[i] = POP();
                }
                Value v = {.type = VAL_ARRAY, .as.as_array = arr};
                PUSH(v);
                break;
            }

            case BC_OBJECT: {
                uint16_t count = READ_SHORT();
                // Allocate object
                Object *obj = malloc(sizeof(Object));
                obj->field_names = malloc(sizeof(char*) * (count > 0 ? count : 1));
                obj->field_values = malloc(sizeof(Value) * (count > 0 ? count : 1));
                obj->num_fields = count;
                obj->capacity = count > 0 ? count : 1;
                obj->ref_count = 1;
                // Pop key-value pairs (value first, then key, in reverse order)
                for (int i = count - 1; i >= 0; i--) {
                    Value val = POP();
                    Value key = POP();
                    if (key.type == VAL_STRING && key.as.as_string) {
                        obj->field_names[i] = strdup(key.as.as_string->data);
                    } else {
                        obj->field_names[i] = strdup("?");
                    }
                    obj->field_values[i] = val;
                }
                Value v = {.type = VAL_OBJECT, .as.as_object = obj};
                PUSH(v);
                break;
            }

            case BC_STRING_INTERP: {
                uint16_t count = READ_SHORT();
                // Concatenate all parts on the stack into a single string
                // First, compute total length
                size_t total_len = 0;
                Value *parts = vm->stack_top - count;
                for (int i = 0; i < count; i++) {
                    Value v = parts[i];
                    if (v.type == VAL_STRING && v.as.as_string) {
                        total_len += v.as.as_string->length;
                    } else {
                        // Convert to string representation
                        char buf[64];
                        switch (v.type) {
                            case VAL_NULL: total_len += 4; break;  // "null"
                            case VAL_BOOL: total_len += v.as.as_bool ? 4 : 5; break;  // "true"/"false"
                            case VAL_I32: snprintf(buf, 64, "%d", v.as.as_i32); total_len += strlen(buf); break;
                            case VAL_I64: snprintf(buf, 64, "%lld", (long long)v.as.as_i64); total_len += strlen(buf); break;
                            case VAL_F64: snprintf(buf, 64, "%g", v.as.as_f64); total_len += strlen(buf); break;
                            default: total_len += 16; break;  // "<type>"
                        }
                    }
                }

                // Allocate result string
                char *result = malloc(total_len + 1);
                char *ptr = result;

                // Build the result
                for (int i = 0; i < count; i++) {
                    Value v = parts[i];
                    if (v.type == VAL_STRING && v.as.as_string) {
                        memcpy(ptr, v.as.as_string->data, v.as.as_string->length);
                        ptr += v.as.as_string->length;
                    } else {
                        char buf[64];
                        int len = 0;
                        switch (v.type) {
                            case VAL_NULL: strcpy(ptr, "null"); len = 4; break;
                            case VAL_BOOL: strcpy(ptr, v.as.as_bool ? "true" : "false"); len = v.as.as_bool ? 4 : 5; break;
                            case VAL_I8: len = sprintf(ptr, "%d", v.as.as_i8); break;
                            case VAL_I16: len = sprintf(ptr, "%d", v.as.as_i16); break;
                            case VAL_I32: len = sprintf(ptr, "%d", v.as.as_i32); break;
                            case VAL_I64: len = sprintf(ptr, "%lld", (long long)v.as.as_i64); break;
                            case VAL_U8: len = sprintf(ptr, "%u", v.as.as_u8); break;
                            case VAL_U16: len = sprintf(ptr, "%u", v.as.as_u16); break;
                            case VAL_U32: len = sprintf(ptr, "%u", v.as.as_u32); break;
                            case VAL_U64: len = sprintf(ptr, "%llu", (unsigned long long)v.as.as_u64); break;
                            case VAL_F32: len = sprintf(ptr, "%g", v.as.as_f32); break;
                            case VAL_F64: len = sprintf(ptr, "%g", v.as.as_f64); break;
                            default: len = sprintf(ptr, "<%s>", val_type_name(v.type)); break;
                        }
                        ptr += len;
                    }
                }
                *ptr = '\0';

                // Pop all parts
                vm_popn(vm, count);

                // Create and push result string
                String *s = malloc(sizeof(String));
                s->data = result;
                s->length = ptr - result;
                s->char_length = s->length;  // TODO: compute UTF-8 char length
                s->capacity = total_len + 1;
                s->ref_count = 1;

                Value str_val = {.type = VAL_STRING, .as.as_string = s};
                PUSH(str_val);
                break;
            }

            // Variables
            case BC_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                PUSH(slots[slot]);
                break;
            }

            case BC_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                slots[slot] = PEEK(0);
                break;
            }

            case BC_GET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                // The closure is stored in slot 0 of this frame
                Value closure_val = slots[0];
                if (is_vm_closure(closure_val)) {
                    VMClosure *closure = as_vm_closure(closure_val);
                    if (closure && slot < closure->upvalue_count && closure->upvalues[slot]) {
                        ObjUpvalue *upvalue = closure->upvalues[slot];
                        PUSH(*upvalue->location);
                    } else {
                        PUSH(vm_null_value());
                    }
                } else {
                    PUSH(vm_null_value());
                }
                break;
            }

            case BC_SET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                Value closure_val = slots[0];
                if (is_vm_closure(closure_val)) {
                    VMClosure *closure = as_vm_closure(closure_val);
                    if (closure && slot < closure->upvalue_count && closure->upvalues[slot]) {
                        ObjUpvalue *upvalue = closure->upvalues[slot];
                        *upvalue->location = PEEK(0);
                    }
                }
                break;
            }

            case BC_GET_SELF: {
                // Push the method receiver (self) onto the stack
                PUSH(vm->method_self);
                break;
            }

            case BC_SET_SELF: {
                // Pop and set the method receiver (self)
                vm->method_self = POP();
                break;
            }

            case BC_GET_KEY: {
                // [iterable, index] -> [key]
                // For objects: return field name at index
                // For arrays: return index as i32
                Value idx = POP();
                Value obj = POP();
                int i = (int)value_to_i64(idx);

                if (obj.type == VAL_OBJECT && obj.as.as_object) {
                    Object *o = obj.as.as_object;
                    if (i >= 0 && i < o->num_fields) {
                        // Return field name as string
                        String *s = malloc(sizeof(String));
                        s->data = strdup(o->field_names[i]);
                        s->length = strlen(s->data);
                        s->char_length = s->length;
                        s->capacity = s->length + 1;
                        s->ref_count = 1;
                        Value v = {.type = VAL_STRING, .as.as_string = s};
                        PUSH(v);
                    } else {
                        PUSH(vm_null_value());
                    }
                } else {
                    // For arrays, the key is just the index
                    PUSH(val_i32_vm(i));
                }
                break;
            }

            case BC_SET_OBJ_TYPE: {
                // Set the type name on the object at top of stack
                Constant c = READ_CONSTANT();
                Value obj = PEEK(0);
                if (obj.type == VAL_OBJECT && obj.as.as_object) {
                    Object *o = obj.as.as_object;
                    // Free old type_name if present
                    if (o->type_name) {
                        free(o->type_name);
                    }
                    o->type_name = strdup(c.as.string.data);
                }
                break;
            }

            case BC_GET_GLOBAL: {
                Constant c = READ_CONSTANT();
                Value v;
                if (!vm_get_global(vm, c.as.string.data, &v)) {
                    THROW_ERROR_FMT("Undefined variable '%s'", c.as.string.data);
                }
                PUSH(v);
                break;
            }

            case BC_SET_GLOBAL: {
                Constant c = READ_CONSTANT();
                if (!vm_set_global(vm, c.as.string.data, PEEK(0))) {
                    pending_exception_msg = vm->pending_error;
                    vm->pending_error = NULL;
                    goto handle_exception;
                }
                break;
            }

            case BC_DEFINE_GLOBAL: {
                Constant c = READ_CONSTANT();
                vm_define_global(vm, c.as.string.data, POP(), false);
                break;
            }

            case BC_GET_PROPERTY: {
                Constant c = READ_CONSTANT();
                Value obj = POP();
                if (obj.type == VAL_OBJECT && obj.as.as_object) {
                    Object *o = obj.as.as_object;
                    const char *key = c.as.string.data;
                    // Check for built-in length property
                    if (strcmp(key, "length") == 0) {
                        PUSH(val_i32_vm(o->num_fields));
                        goto property_found;
                    }
                    for (int i = 0; i < o->num_fields; i++) {
                        if (strcmp(o->field_names[i], key) == 0) {
                            PUSH(o->field_values[i]);
                            goto property_found;
                        }
                    }
                    PUSH(vm_null_value());
                    property_found:;
                } else if (obj.type == VAL_ARRAY && obj.as.as_array) {
                    // Array properties: length
                    const char *key = c.as.string.data;
                    if (strcmp(key, "length") == 0) {
                        PUSH(val_i32_vm(obj.as.as_array->length));
                    } else {
                        PUSH(vm_null_value());
                    }
                } else if (obj.type == VAL_STRING && obj.as.as_string) {
                    // String properties: length
                    const char *key = c.as.string.data;
                    if (strcmp(key, "length") == 0) {
                        PUSH(val_i32_vm(obj.as.as_string->length));
                    } else {
                        PUSH(vm_null_value());
                    }
                } else if (obj.type == VAL_BUFFER && obj.as.as_buffer) {
                    // Buffer properties: length, capacity
                    const char *key = c.as.string.data;
                    Buffer *buf = obj.as.as_buffer;
                    if (strcmp(key, "length") == 0) {
                        PUSH(val_i32_vm(buf->length));
                    } else if (strcmp(key, "capacity") == 0) {
                        PUSH(val_i32_vm(buf->capacity));
                    } else {
                        PUSH(vm_null_value());
                    }
                } else {
                    THROW_ERROR_FMT("Cannot get property of %s", val_type_name(obj.type));
                }
                break;
            }

            case BC_SET_PROPERTY: {
                Constant c = READ_CONSTANT();
                Value val = POP();
                Value obj = POP();
                if (obj.type == VAL_OBJECT && obj.as.as_object) {
                    Object *o = obj.as.as_object;
                    const char *key = c.as.string.data;
                    // Find existing key or add new
                    for (int i = 0; i < o->num_fields; i++) {
                        if (strcmp(o->field_names[i], key) == 0) {
                            o->field_values[i] = val;
                            PUSH(val);
                            goto property_set;
                        }
                    }
                    // Add new key
                    if (o->num_fields >= o->capacity) {
                        o->capacity = o->capacity * 2;
                        o->field_names = realloc(o->field_names, sizeof(char*) * o->capacity);
                        o->field_values = realloc(o->field_values, sizeof(Value) * o->capacity);
                    }
                    o->field_names[o->num_fields] = strdup(key);
                    o->field_values[o->num_fields] = val;
                    o->num_fields++;
                    PUSH(val);
                    property_set:;
                } else {
                    THROW_ERROR_FMT("Cannot set property on %s", val_type_name(obj.type));
                }
                break;
            }

            case BC_CLOSE_UPVALUE: {
                // Close the upvalue at the top of the stack
                // This is called when a captured variable goes out of scope
                vm_close_upvalues(vm, vm->stack_top - 1);
                POP();  // Pop the closed value
                break;
            }

            case BC_GET_INDEX: {
                Value idx = POP();
                Value obj = POP();
                if (obj.type == VAL_ARRAY && obj.as.as_array) {
                    Array *arr = obj.as.as_array;
                    int i = (int)value_to_i64(idx);
                    if (i < 0 || i >= arr->length) {
                        PUSH(vm_null_value());
                    } else {
                        PUSH(arr->elements[i]);
                    }
                } else if (obj.type == VAL_STRING && obj.as.as_string) {
                    String *s = obj.as.as_string;
                    int i = (int)value_to_i64(idx);
                    // Count codepoints to get to index i
                    const char *p = s->data;
                    const char *end = s->data + s->length;
                    int cp_idx = 0;
                    uint32_t codepoint = 0;
                    while (p < end && cp_idx < i) {
                        // Skip UTF-8 sequence
                        unsigned char c = (unsigned char)*p;
                        if ((c & 0x80) == 0) { p += 1; }
                        else if ((c & 0xE0) == 0xC0) { p += 2; }
                        else if ((c & 0xF0) == 0xE0) { p += 3; }
                        else { p += 4; }
                        cp_idx++;
                    }
                    if (p >= end) {
                        PUSH(vm_null_value());
                    } else {
                        // Decode UTF-8 codepoint at position p
                        unsigned char c = (unsigned char)*p;
                        if ((c & 0x80) == 0) {
                            codepoint = c;
                        } else if ((c & 0xE0) == 0xC0) {
                            codepoint = (c & 0x1F) << 6;
                            codepoint |= ((unsigned char)p[1] & 0x3F);
                        } else if ((c & 0xF0) == 0xE0) {
                            codepoint = (c & 0x0F) << 12;
                            codepoint |= ((unsigned char)p[1] & 0x3F) << 6;
                            codepoint |= ((unsigned char)p[2] & 0x3F);
                        } else {
                            codepoint = (c & 0x07) << 18;
                            codepoint |= ((unsigned char)p[1] & 0x3F) << 12;
                            codepoint |= ((unsigned char)p[2] & 0x3F) << 6;
                            codepoint |= ((unsigned char)p[3] & 0x3F);
                        }
                        Value v = {.type = VAL_RUNE, .as.as_rune = codepoint};
                        PUSH(v);
                    }
                } else if (obj.type == VAL_BUFFER && obj.as.as_buffer) {
                    // Buffer indexing - returns byte as i32
                    Buffer *buf = obj.as.as_buffer;
                    int i = (int)value_to_i64(idx);
                    if (i < 0 || i >= buf->length) {
                        THROW_ERROR_FMT("Buffer index out of bounds: %d", i);
                    }
                    uint8_t *data = (uint8_t*)buf->data;
                    PUSH(val_i32_vm(data[i]));
                } else if (obj.type == VAL_OBJECT && obj.as.as_object) {
                    Object *o = obj.as.as_object;
                    if (idx.type == VAL_STRING && idx.as.as_string) {
                        const char *key = idx.as.as_string->data;
                        for (int i = 0; i < o->num_fields; i++) {
                            if (strcmp(o->field_names[i], key) == 0) {
                                PUSH(o->field_values[i]);
                                goto index_found;
                            }
                        }
                        PUSH(vm_null_value());
                    } else {
                        // Integer index - get by field index
                        int i = (int)value_to_i64(idx);
                        if (i >= 0 && i < o->num_fields) {
                            PUSH(o->field_values[i]);
                        } else {
                            PUSH(vm_null_value());
                        }
                    }
                    index_found:;
                } else {
                    THROW_ERROR_FMT("Cannot index %s", val_type_name(obj.type));
                }
                break;
            }

            case BC_SET_INDEX: {
                Value val = POP();
                Value idx = POP();
                Value obj = POP();
                if (obj.type == VAL_ARRAY && obj.as.as_array) {
                    Array *arr = obj.as.as_array;
                    int i = (int)value_to_i64(idx);
                    if (i < 0) {
                        THROW_ERROR_FMT("Array index out of bounds: %d", i);
                    }
                    // Grow array if needed
                    while (i >= arr->capacity) {
                        arr->capacity = arr->capacity * 2;
                        arr->elements = realloc(arr->elements, sizeof(Value) * arr->capacity);
                    }
                    // Fill with nulls if needed
                    while (arr->length <= i) {
                        arr->elements[arr->length++] = vm_null_value();
                    }
                    arr->elements[i] = val;
                    PUSH(val);
                } else if (obj.type == VAL_OBJECT && obj.as.as_object) {
                    Object *o = obj.as.as_object;
                    if (idx.type == VAL_STRING && idx.as.as_string) {
                        const char *key = idx.as.as_string->data;
                        for (int i = 0; i < o->num_fields; i++) {
                            if (strcmp(o->field_names[i], key) == 0) {
                                o->field_values[i] = val;
                                PUSH(val);
                                goto index_set;
                            }
                        }
                        // Add new key
                        if (o->num_fields >= o->capacity) {
                            o->capacity = o->capacity * 2;
                            o->field_names = realloc(o->field_names, sizeof(char*) * o->capacity);
                            o->field_values = realloc(o->field_values, sizeof(Value) * o->capacity);
                        }
                        o->field_names[o->num_fields] = strdup(key);
                        o->field_values[o->num_fields] = val;
                        o->num_fields++;
                        PUSH(val);
                        index_set:;
                    } else {
                        THROW_ERROR("Object key must be string");
                    }
                } else if (obj.type == VAL_BUFFER && obj.as.as_buffer) {
                    // Buffer indexing - write byte
                    Buffer *buf = obj.as.as_buffer;
                    int i = (int)value_to_i64(idx);
                    if (i < 0 || i >= buf->length) {
                        THROW_ERROR_FMT("Buffer index out of bounds: %d", i);
                    }
                    uint8_t *data = (uint8_t*)buf->data;
                    data[i] = (uint8_t)value_to_i64(val);
                    PUSH(val);
                } else {
                    THROW_ERROR_FMT("Cannot set index on %s", val_type_name(obj.type));
                }
                break;
            }

            // Arithmetic
            case BC_ADD: {
                Value b = POP();
                Value a = POP();
                PUSH(binary_add(vm, a, b));
                if (vm->pending_error) {
                    pending_exception_msg = vm->pending_error;
                    vm->pending_error = NULL;
                    goto handle_exception;
                }
                break;
            }

            case BC_SUB: {
                Value b = POP();
                Value a = POP();
                PUSH(binary_sub(vm, a, b));
                if (vm->pending_error) {
                    pending_exception_msg = vm->pending_error;
                    vm->pending_error = NULL;
                    goto handle_exception;
                }
                break;
            }

            case BC_MUL: {
                Value b = POP();
                Value a = POP();
                PUSH(binary_mul(vm, a, b));
                if (vm->pending_error) {
                    pending_exception_msg = vm->pending_error;
                    vm->pending_error = NULL;
                    goto handle_exception;
                }
                break;
            }

            case BC_DIV: {
                Value b = POP();
                Value a = POP();
                PUSH(binary_div(vm, a, b));
                if (vm->pending_error) {
                    pending_exception_msg = vm->pending_error;
                    vm->pending_error = NULL;
                    goto handle_exception;
                }
                break;
            }

            case BC_MOD: {
                Value b = POP();
                Value a = POP();
                PUSH(binary_mod(vm, a, b));
                if (vm->pending_error) {
                    pending_exception_msg = vm->pending_error;
                    vm->pending_error = NULL;
                    goto handle_exception;
                }
                break;
            }

            case BC_NEGATE: {
                Value a = POP();
                switch (a.type) {
                    case VAL_I32: PUSH(val_i32_vm(-a.as.as_i32)); break;
                    case VAL_I64: PUSH(val_i64_vm(-a.as.as_i64)); break;
                    case VAL_F64: PUSH(val_f64_vm(-a.as.as_f64)); break;
                    default:
                        THROW_ERROR_FMT("Cannot negate %s", val_type_name(a.type));
                }
                break;
            }

            // i32 fast paths
            case BC_ADD_I32: {
                Value b = POP();
                Value a = POP();
                PUSH(val_i32_vm(a.as.as_i32 + b.as.as_i32));
                break;
            }

            case BC_SUB_I32: {
                Value b = POP();
                Value a = POP();
                PUSH(val_i32_vm(a.as.as_i32 - b.as.as_i32));
                break;
            }

            case BC_MUL_I32: {
                Value b = POP();
                Value a = POP();
                PUSH(val_i32_vm(a.as.as_i32 * b.as.as_i32));
                break;
            }

            // Comparison
            case BC_EQ: {
                Value b = POP();
                Value a = POP();
                PUSH(binary_eq(a, b));
                break;
            }

            case BC_NE: {
                Value b = POP();
                Value a = POP();
                Value eq = binary_eq(a, b);
                PUSH(val_bool_vm(!eq.as.as_bool));
                break;
            }

            case BC_LT: {
                Value b = POP();
                Value a = POP();
                PUSH(binary_lt(vm, a, b));
                if (vm->pending_error) {
                    pending_exception_msg = vm->pending_error;
                    vm->pending_error = NULL;
                    goto handle_exception;
                }
                break;
            }

            case BC_LE: {
                Value b = POP();
                Value a = POP();
                Value lt = binary_lt(vm, a, b);
                if (vm->pending_error) {
                    pending_exception_msg = vm->pending_error;
                    vm->pending_error = NULL;
                    goto handle_exception;
                }
                Value eq = binary_eq(a, b);
                PUSH(val_bool_vm(lt.as.as_bool || eq.as.as_bool));
                break;
            }

            case BC_GT: {
                Value b = POP();
                Value a = POP();
                Value lt = binary_lt(vm, b, a);  // Swap operands
                if (vm->pending_error) {
                    pending_exception_msg = vm->pending_error;
                    vm->pending_error = NULL;
                    goto handle_exception;
                }
                PUSH(lt);
                break;
            }

            case BC_GE: {
                Value b = POP();
                Value a = POP();
                Value lt = binary_lt(vm, a, b);
                if (vm->pending_error) {
                    pending_exception_msg = vm->pending_error;
                    vm->pending_error = NULL;
                    goto handle_exception;
                }
                PUSH(val_bool_vm(!lt.as.as_bool));
                break;
            }

            case BC_EQ_I32: {
                Value b = POP();
                Value a = POP();
                PUSH(val_bool_vm(a.as.as_i32 == b.as.as_i32));
                break;
            }

            case BC_LT_I32: {
                Value b = POP();
                Value a = POP();
                PUSH(val_bool_vm(a.as.as_i32 < b.as.as_i32));
                break;
            }

            // Logical
            case BC_NOT: {
                Value a = POP();
                PUSH(val_bool_vm(!value_is_truthy(a)));
                break;
            }

            // Bitwise
            case BC_BIT_NOT: {
                Value a = POP();
                if (a.type == VAL_I32) {
                    PUSH(val_i32_vm(~a.as.as_i32));
                } else if (a.type == VAL_I64) {
                    PUSH(val_i64_vm(~a.as.as_i64));
                } else {
                    THROW_ERROR_FMT("Cannot bitwise NOT %s", val_type_name(a.type));
                }
                break;
            }

            case BC_BIT_AND: {
                Value b = POP();
                Value a = POP();
                if (a.type == VAL_I32 && b.type == VAL_I32) {
                    PUSH(val_i32_vm(a.as.as_i32 & b.as.as_i32));
                } else {
                    PUSH(val_i64_vm(value_to_i64(a) & value_to_i64(b)));
                }
                break;
            }

            case BC_BIT_OR: {
                Value b = POP();
                Value a = POP();
                if (a.type == VAL_I32 && b.type == VAL_I32) {
                    PUSH(val_i32_vm(a.as.as_i32 | b.as.as_i32));
                } else {
                    PUSH(val_i64_vm(value_to_i64(a) | value_to_i64(b)));
                }
                break;
            }

            case BC_BIT_XOR: {
                Value b = POP();
                Value a = POP();
                if (a.type == VAL_I32 && b.type == VAL_I32) {
                    PUSH(val_i32_vm(a.as.as_i32 ^ b.as.as_i32));
                } else {
                    PUSH(val_i64_vm(value_to_i64(a) ^ value_to_i64(b)));
                }
                break;
            }

            case BC_LSHIFT: {
                Value b = POP();
                Value a = POP();
                if (a.type == VAL_I32 && b.type == VAL_I32) {
                    PUSH(val_i32_vm(a.as.as_i32 << b.as.as_i32));
                } else {
                    PUSH(val_i64_vm(value_to_i64(a) << value_to_i64(b)));
                }
                break;
            }

            case BC_RSHIFT: {
                Value b = POP();
                Value a = POP();
                if (a.type == VAL_I32 && b.type == VAL_I32) {
                    PUSH(val_i32_vm(a.as.as_i32 >> b.as.as_i32));
                } else {
                    PUSH(val_i64_vm(value_to_i64(a) >> value_to_i64(b)));
                }
                break;
            }

            // Control flow
            case BC_JUMP: {
                uint16_t offset = READ_SHORT();
                ip += offset;
                break;
            }

            case BC_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                Value cond = PEEK(0);  // Don't pop - leave for explicit POP
                if (!value_is_truthy(cond)) {
                    ip += offset;
                }
                break;
            }

            case BC_JUMP_IF_TRUE: {
                uint16_t offset = READ_SHORT();
                Value cond = PEEK(0);  // Don't pop - leave for explicit POP
                if (value_is_truthy(cond)) {
                    ip += offset;
                }
                break;
            }

            case BC_COALESCE: {
                // Null coalescing: if value is NOT null, jump (keep value)
                // Otherwise, pop null and continue to evaluate fallback
                uint16_t offset = READ_SHORT();
                Value val = PEEK(0);
                if (val.type != VAL_NULL) {
                    ip += offset;  // Value is not null, skip fallback
                }
                // If null, leave on stack for explicit POP before fallback
                break;
            }

            case BC_OPTIONAL_CHAIN: {
                // Optional chaining: if value IS null, jump (keep null)
                // Otherwise, continue to property access/method call
                uint16_t offset = READ_SHORT();
                Value val = PEEK(0);
                if (val.type == VAL_NULL) {
                    ip += offset;  // Value is null, skip property access
                }
                // If not null, leave on stack for property/index/method
                break;
            }

            case BC_LOOP: {
                uint16_t offset = READ_SHORT();
                ip -= offset;
                break;
            }

            case BC_FOR_IN_INIT: {
                // No longer used - kept for compatibility
                break;
            }

            case BC_FOR_IN_NEXT: {
                // Stack: [array, index] -> [element] or jump (consume array, index)
                uint16_t offset = READ_SHORT();
                Value idx_val = POP();
                Value arr_val = POP();

                if (arr_val.type != VAL_ARRAY) {
                    THROW_ERROR("for-in requires an array or object");
                }

                int32_t idx = idx_val.as.as_i32;
                Array *arr = arr_val.as.as_array;

                if (idx >= arr->length) {
                    ip += offset;
                } else {
                    PUSH(arr->elements[idx]);
                }
                break;
            }

            case BC_POP:
                POP();
                break;

            case BC_POPN: {
                uint8_t n = READ_BYTE();
                vm_popn(vm, n);
                break;
            }

            case BC_DUP: {
                // Duplicate top of stack
                Value top = PEEK(0);
                PUSH(top);
                break;
            }

            case BC_DUP2: {
                // Duplicate top two stack values
                // [a, b] -> [a, b, a, b]
                Value b = PEEK(0);
                Value a = PEEK(1);
                PUSH(a);
                PUSH(b);
                break;
            }

            case BC_SWAP: {
                // Swap top two stack values
                // [a, b] -> [b, a]
                Value top = PEEK(0);
                Value second = PEEK(1);
                vm->stack_top[-1] = second;
                vm->stack_top[-2] = top;
                break;
            }

            case BC_BURY3: {
                // Move second-from-top under bottom of 4
                // [a, b, c, d] -> [c, a, b, d]
                // (where d is top of stack)
                Value d = PEEK(0);  // top
                Value c = PEEK(1);  // 2nd from top - this moves to bottom
                Value b = PEEK(2);
                Value a = PEEK(3);  // bottom of 4
                vm->stack_top[-4] = c;  // c moves to bottom
                vm->stack_top[-3] = a;  // a moves up
                vm->stack_top[-2] = b;  // b moves up
                vm->stack_top[-1] = d;  // d stays on top
                break;
            }

            case BC_ROT3: {
                // Rotate 3 elements, bring bottom to top
                // [a, b, c] -> [b, c, a]
                // (where c is top of stack)
                Value c = PEEK(0);  // top
                Value b = PEEK(1);
                Value a = PEEK(2);  // bottom of 3
                vm->stack_top[-3] = b;  // b moves to bottom
                vm->stack_top[-2] = c;  // c moves to middle
                vm->stack_top[-1] = a;  // a moves to top
                break;
            }

            // Print (builtin)
            case BC_PRINT: {
                uint8_t argc = READ_BYTE();
                // Print args in order (they're on stack in reverse)
                Value *args = vm->stack_top - argc;
                for (int i = 0; i < argc; i++) {
                    if (i > 0) printf(" ");
                    Value v = args[i];
                    switch (v.type) {
                        case VAL_NULL: printf("null"); break;
                        case VAL_BOOL: printf("%s", v.as.as_bool ? "true" : "false"); break;
                        case VAL_I8: printf("%d", v.as.as_i8); break;
                        case VAL_I16: printf("%d", v.as.as_i16); break;
                        case VAL_I32: printf("%d", v.as.as_i32); break;
                        case VAL_I64: printf("%lld", (long long)v.as.as_i64); break;
                        case VAL_U8: printf("%u", v.as.as_u8); break;
                        case VAL_U16: printf("%u", v.as.as_u16); break;
                        case VAL_U32: printf("%u", v.as.as_u32); break;
                        case VAL_U64: printf("%llu", (unsigned long long)v.as.as_u64); break;
                        case VAL_F32: printf("%g", v.as.as_f32); break;
                        case VAL_F64: printf("%g", v.as.as_f64); break;
                        case VAL_STRING:
                            if (v.as.as_string) printf("%s", v.as.as_string->data);
                            break;
                        case VAL_RUNE: {
                            // Print rune as character if printable, otherwise as U+XXXX
                            uint32_t r = v.as.as_rune;
                            if (r >= 32 && r < 127) {
                                printf("'%c'", (char)r);
                            } else if (r < 0x10000) {
                                printf("U+%04X", r);
                            } else {
                                printf("U+%X", r);
                            }
                            break;
                        }
                        default:
                            printf("<%s>", val_type_name(v.type));
                    }
                }
                printf("\n");
                vm_popn(vm, argc);
                PUSH(vm_null_value());  // Push null as return value
                break;
            }

            case BC_CALL_BUILTIN: {
                uint16_t builtin_id = READ_SHORT();
                uint8_t argc = READ_BYTE();
                Value *args = vm->stack_top - argc;
                Value result = vm_null_value();

                switch (builtin_id) {
                    case BUILTIN_TYPEOF: {
                        if (argc >= 1) {
                            Value v = args[0];
                            const char *type_str;
                            // Check for custom object type name
                            if (v.type == VAL_OBJECT && v.as.as_object && v.as.as_object->type_name) {
                                type_str = v.as.as_object->type_name;
                            } else {
                                type_str = val_type_name(v.type);
                            }
                            String *s = malloc(sizeof(String));
                            s->data = strdup(type_str);
                            s->length = strlen(type_str);
                            s->char_length = s->length;
                            s->capacity = s->length + 1;
                            s->ref_count = 1;
                            result.type = VAL_STRING;
                            result.as.as_string = s;
                        }
                        break;
                    }
                    case BUILTIN_PRINT: {
                        for (int i = 0; i < argc; i++) {
                            if (i > 0) printf(" ");
                            Value v = args[i];
                            switch (v.type) {
                                case VAL_NULL: printf("null"); break;
                                case VAL_BOOL: printf("%s", v.as.as_bool ? "true" : "false"); break;
                                case VAL_I32: printf("%d", v.as.as_i32); break;
                                case VAL_I64: printf("%lld", (long long)v.as.as_i64); break;
                                case VAL_F64: printf("%g", v.as.as_f64); break;
                                case VAL_STRING:
                                    if (v.as.as_string) printf("%s", v.as.as_string->data);
                                    break;
                                default:
                                    printf("<%s>", val_type_name(v.type));
                            }
                        }
                        printf("\n");
                        break;
                    }
                    case BUILTIN_EPRINT: {
                        for (int i = 0; i < argc; i++) {
                            if (i > 0) fprintf(stderr, " ");
                            Value v = args[i];
                            switch (v.type) {
                                case VAL_NULL: fprintf(stderr, "null"); break;
                                case VAL_BOOL: fprintf(stderr, "%s", v.as.as_bool ? "true" : "false"); break;
                                case VAL_I32: fprintf(stderr, "%d", v.as.as_i32); break;
                                case VAL_I64: fprintf(stderr, "%lld", (long long)v.as.as_i64); break;
                                case VAL_F64: fprintf(stderr, "%g", v.as.as_f64); break;
                                case VAL_STRING:
                                    if (v.as.as_string) fprintf(stderr, "%s", v.as.as_string->data);
                                    break;
                                default:
                                    fprintf(stderr, "<%s>", val_type_name(v.type));
                            }
                        }
                        fprintf(stderr, "\n");
                        break;
                    }
                    case BUILTIN_ASSERT: {
                        if (argc >= 1 && !value_is_truthy(args[0])) {
                            const char *msg = (argc >= 2 && args[1].type == VAL_STRING)
                                ? args[1].as.as_string->data : "Assertion failed";
                            THROW_ERROR(msg);
                        }
                        break;
                    }
                    case BUILTIN_PANIC: {
                        fflush(stdout);  // Ensure all output is printed before panic
                        const char *msg = (argc >= 1 && args[0].type == VAL_STRING)
                            ? args[0].as.as_string->data : "panic!";
                        fprintf(stderr, "panic: %s\n", msg);
                        exit(1);
                    }
                    case BUILTIN_DIVI: {
                        // Integer division (floor)
                        if (argc >= 2) {
                            int64_t a = value_to_i64(args[0]);
                            int64_t b = value_to_i64(args[1]);
                            if (b == 0) {
                                THROW_ERROR("Division by zero");
                            }
                            // Floor division: towards negative infinity
                            int64_t q = a / b;
                            if ((a ^ b) < 0 && a % b != 0) {
                                q--;  // Adjust for floor behavior
                            }
                            result = val_i64_vm(q);
                        }
                        break;
                    }
                    case BUILTIN_MODI: {
                        // Integer modulo
                        if (argc >= 2) {
                            int64_t a = value_to_i64(args[0]);
                            int64_t b = value_to_i64(args[1]);
                            if (b == 0) {
                                THROW_ERROR("Modulo by zero");
                            }
                            result = val_i64_vm(a % b);
                        }
                        break;
                    }
                    case BUILTIN_STRING_CONCAT_MANY: {
                        // string_concat_many(array) - concatenate array of strings
                        if (argc >= 1 && args[0].type == VAL_ARRAY && args[0].as.as_array) {
                            Array *arr = args[0].as.as_array;
                            // Calculate total length
                            int total_len = 0;
                            for (int i = 0; i < arr->length; i++) {
                                if (arr->elements[i].type == VAL_STRING && arr->elements[i].as.as_string) {
                                    total_len += arr->elements[i].as.as_string->length;
                                }
                            }
                            // Allocate and build result
                            char *buf = malloc(total_len + 1);
                            char *p = buf;
                            for (int i = 0; i < arr->length; i++) {
                                if (arr->elements[i].type == VAL_STRING && arr->elements[i].as.as_string) {
                                    String *s = arr->elements[i].as.as_string;
                                    memcpy(p, s->data, s->length);
                                    p += s->length;
                                }
                            }
                            *p = '\0';
                            result = vm_make_string(buf, total_len);
                            free(buf);
                        } else {
                            result = vm_make_string("", 0);
                        }
                        break;
                    }
                    case BUILTIN_OPEN: {
                        // open(path, mode?) - open file, returns file handle
                        if (argc < 1 || args[0].type != VAL_STRING) {
                            THROW_ERROR("open() expects (path: string, mode?: string)");
                        }
                        const char *path = args[0].as.as_string->data;
                        const char *mode = "r";  // Default mode
                        if (argc >= 2 && args[1].type == VAL_STRING) {
                            mode = args[1].as.as_string->data;
                        }
                        FILE *fp = fopen(path, mode);
                        if (!fp) {
                            THROW_ERROR_FMT("Failed to open '%s': %s", path, strerror(errno));
                        }
                        FileHandle *file = malloc(sizeof(FileHandle));
                        file->fp = fp;
                        file->path = strdup(path);
                        file->mode = strdup(mode);
                        file->closed = 0;
                        result.type = VAL_FILE;
                        result.as.as_file = file;
                        break;
                    }
                    case BUILTIN_READ_LINE: {
                        // read_line() - read line from stdin
                        char *line = NULL;
                        size_t len = 0;
                        ssize_t read_len = getline(&line, &len, stdin);
                        if (read_len == -1) {
                            free(line);
                            result = vm_null_value();  // EOF
                        } else {
                            // Strip newline
                            if (read_len > 0 && line[read_len - 1] == '\n') {
                                line[read_len - 1] = '\0';
                                read_len--;
                            }
                            if (read_len > 0 && line[read_len - 1] == '\r') {
                                line[read_len - 1] = '\0';
                                read_len--;
                            }
                            result = vm_make_string(line, read_len);
                            free(line);
                        }
                        break;
                    }
                    case BUILTIN_SPAWN: {
                        // spawn(async_fn, args...) - spawn async task
                        if (argc < 1) {
                            THROW_ERROR("spawn() expects at least 1 argument (async function)");
                        }
                        Value func_val = args[0];
                        if (!is_vm_closure(func_val)) {
                            THROW_ERROR("spawn() expects an async function");
                        }

                        VMClosure *closure = as_vm_closure(func_val);
                        if (!closure->chunk->is_async) {
                            THROW_ERROR("spawn() requires an async function");
                        }

                        // Create task with remaining args
                        int task_argc = argc - 1;
                        Value *task_args = (task_argc > 0) ? &args[1] : NULL;
                        VMTask *task = vm_task_new(closure, task_args, task_argc);
                        if (!task) {
                            THROW_ERROR("Failed to create task");
                        }

                        // Create thread argument
                        TaskThreadArg *thread_arg = malloc(sizeof(TaskThreadArg));
                        thread_arg->task = task;
                        thread_arg->chunk = closure->chunk;

                        // Create thread
                        int rc = pthread_create(&task->thread, NULL, vm_task_thread_wrapper, thread_arg);
                        if (rc != 0) {
                            free(thread_arg);
                            vm_task_free(task);
                            THROW_ERROR("Failed to create thread");
                        }

                        // Return task handle (using VAL_TASK)
                        result.type = VAL_TASK;
                        result.as.as_task = (Task*)task;  // Store VMTask as Task*
                        break;
                    }
                    case BUILTIN_JOIN: {
                        // join(task) - wait for task to complete
                        if (argc != 1) {
                            THROW_ERROR("join() expects 1 argument (task handle)");
                        }
                        if (args[0].type != VAL_TASK) {
                            THROW_ERROR("join() expects a task handle");
                        }

                        VMTask *task = (VMTask*)args[0].as.as_task;

                        pthread_mutex_lock(&task->mutex);
                        if (task->joined) {
                            pthread_mutex_unlock(&task->mutex);
                            THROW_ERROR("task handle already joined");
                        }
                        if (task->detached) {
                            pthread_mutex_unlock(&task->mutex);
                            THROW_ERROR("cannot join detached task");
                        }
                        task->joined = 1;
                        pthread_mutex_unlock(&task->mutex);

                        // Wait for thread to complete
                        int rc = pthread_join(task->thread, NULL);
                        if (rc != 0) {
                            THROW_ERROR("pthread_join failed");
                        }

                        // Check for exception
                        pthread_mutex_lock(&task->mutex);
                        if (task->has_exception) {
                            Value exc = task->exception;
                            pthread_mutex_unlock(&task->mutex);
                            // Re-throw the exception
                            vm->is_throwing = true;
                            vm->exception = exc;
                            vm_popn(vm, argc);
                            goto handle_exception;
                        }
                        result = task->result;
                        pthread_mutex_unlock(&task->mutex);
                        break;
                    }
                    case BUILTIN_DETACH: {
                        // detach(task) or detach(async_fn, args...) - fire and forget
                        if (argc < 1) {
                            THROW_ERROR("detach() expects at least 1 argument");
                        }

                        // Pattern 1: detach(task_handle)
                        if (args[0].type == VAL_TASK) {
                            if (argc != 1) {
                                THROW_ERROR("detach() with task handle expects exactly 1 argument");
                            }

                            VMTask *task = (VMTask*)args[0].as.as_task;

                            pthread_mutex_lock(&task->mutex);
                            if (task->joined) {
                                pthread_mutex_unlock(&task->mutex);
                                THROW_ERROR("cannot detach already joined task");
                            }
                            if (task->detached) {
                                pthread_mutex_unlock(&task->mutex);
                                THROW_ERROR("task already detached");
                            }
                            task->detached = 1;
                            pthread_mutex_unlock(&task->mutex);

                            int rc = pthread_detach(task->thread);
                            if (rc != 0) {
                                THROW_ERROR("pthread_detach failed");
                            }
                        }
                        // Pattern 2: detach(async_fn, args...) - spawn and detach
                        else if (is_vm_closure(args[0])) {
                            VMClosure *closure = as_vm_closure(args[0]);
                            if (!closure->chunk->is_async) {
                                THROW_ERROR("detach() requires an async function");
                            }

                            int task_argc = argc - 1;
                            Value *task_args = (task_argc > 0) ? &args[1] : NULL;
                            VMTask *task = vm_task_new(closure, task_args, task_argc);
                            if (!task) {
                                THROW_ERROR("Failed to create task");
                            }
                            task->detached = 1;

                            // Retain for thread
                            vm_task_retain(task);

                            TaskThreadArg *thread_arg = malloc(sizeof(TaskThreadArg));
                            thread_arg->task = task;
                            thread_arg->chunk = closure->chunk;

                            int rc = pthread_create(&task->thread, NULL, vm_task_thread_wrapper, thread_arg);
                            if (rc != 0) {
                                free(thread_arg);
                                vm_task_release(task);
                                THROW_ERROR("Failed to create thread");
                            }

                            rc = pthread_detach(task->thread);
                            if (rc != 0) {
                                vm_task_release(task);
                                THROW_ERROR("pthread_detach failed");
                            }

                            vm_task_release(task);  // Release our reference
                        } else {
                            THROW_ERROR("detach() expects a task handle or async function");
                        }
                        break;
                    }
                    case BUILTIN_CHANNEL: {
                        // channel(capacity?) - create a channel
                        int capacity = 0;  // unbuffered by default
                        if (argc > 0) {
                            if (args[0].type != VAL_I32 && args[0].type != VAL_I64) {
                                THROW_ERROR("channel() capacity must be an integer");
                            }
                            capacity = value_to_i32(args[0]);
                            if (capacity < 0) {
                                THROW_ERROR("channel() capacity cannot be negative");
                            }
                        }

                        Channel *ch = vm_channel_new(capacity);
                        if (!ch) {
                            THROW_ERROR("Failed to create channel");
                        }
                        result.type = VAL_CHANNEL;
                        result.as.as_channel = ch;
                        break;
                    }
                    // ========== Memory Operations ==========
                    case BUILTIN_ALLOC: {
                        // alloc(size) - allocate memory
                        if (argc != 1) {
                            THROW_ERROR("alloc() expects 1 argument (size in bytes)");
                        }
                        int64_t size = value_to_i64(args[0]);
                        if (size <= 0) {
                            THROW_ERROR("alloc() size must be positive");
                        }
                        void *ptr = malloc((size_t)size);
                        if (!ptr) {
                            result = vm_null_value();
                        } else {
                            result.type = VAL_PTR;
                            result.as.as_ptr = ptr;
                        }
                        break;
                    }
                    case BUILTIN_TALLOC: {
                        // talloc(type, count) - type-aware allocation
                        if (argc != 2) {
                            THROW_ERROR("talloc() expects 2 arguments (type, count)");
                        }
                        if (args[0].type != VAL_TYPE) {
                            THROW_ERROR("talloc() first argument must be a type");
                        }
                        TypeKind type = args[0].as.as_type;
                        int64_t count = value_to_i64(args[1]);
                        if (count <= 0) {
                            THROW_ERROR("talloc() count must be positive");
                        }
                        // Get element size
                        int elem_size;
                        switch (type) {
                            case TYPE_I8: case TYPE_U8: elem_size = 1; break;
                            case TYPE_I16: case TYPE_U16: elem_size = 2; break;
                            case TYPE_I32: case TYPE_U32: case TYPE_F32: elem_size = 4; break;
                            case TYPE_I64: case TYPE_U64: case TYPE_F64: elem_size = 8; break;
                            case TYPE_PTR: case TYPE_BUFFER: elem_size = sizeof(void*); break;
                            case TYPE_BOOL: elem_size = sizeof(int); break;
                            default: elem_size = 8; break;
                        }
                        size_t total_size = (size_t)elem_size * (size_t)count;
                        void *ptr = malloc(total_size);
                        if (!ptr) {
                            result = vm_null_value();
                        } else {
                            result.type = VAL_PTR;
                            result.as.as_ptr = ptr;
                        }
                        break;
                    }
                    case BUILTIN_REALLOC: {
                        // realloc(ptr, new_size) - reallocate memory
                        if (argc != 2) {
                            THROW_ERROR("realloc() expects 2 arguments (ptr, new_size)");
                        }
                        if (args[0].type != VAL_PTR) {
                            THROW_ERROR("realloc() first argument must be a pointer");
                        }
                        int64_t new_size = value_to_i64(args[1]);
                        if (new_size <= 0) {
                            THROW_ERROR("realloc() new_size must be positive");
                        }
                        void *old_ptr = args[0].as.as_ptr;
                        void *new_ptr = realloc(old_ptr, (size_t)new_size);
                        if (!new_ptr) {
                            result = vm_null_value();
                        } else {
                            result.type = VAL_PTR;
                            result.as.as_ptr = new_ptr;
                        }
                        break;
                    }
                    case BUILTIN_FREE: {
                        // free(ptr|buffer|array|object) - free memory
                        if (argc != 1) {
                            THROW_ERROR("free() expects 1 argument");
                        }
                        Value arg = args[0];
                        if (arg.type == VAL_PTR) {
                            free(arg.as.as_ptr);
                        } else if (arg.type == VAL_BUFFER) {
                            Buffer *buf = arg.as.as_buffer;
                            int expected = 0;
                            if (!atomic_compare_exchange_strong(&buf->freed, &expected, 1)) {
                                THROW_ERROR("double free detected on buffer");
                            }
                            free(buf->data);
                            buf->data = NULL;
                            buf->length = 0;
                            buf->capacity = 0;
                        } else if (arg.type == VAL_ARRAY) {
                            Array *arr = arg.as.as_array;
                            int expected = 0;
                            if (!atomic_compare_exchange_strong(&arr->freed, &expected, 1)) {
                                THROW_ERROR("double free detected on array");
                            }
                            free(arr->elements);
                            arr->elements = NULL;
                            arr->length = 0;
                            arr->capacity = 0;
                        } else if (arg.type == VAL_OBJECT) {
                            Object *obj = arg.as.as_object;
                            int expected = 0;
                            if (!atomic_compare_exchange_strong(&obj->freed, &expected, 1)) {
                                THROW_ERROR("double free detected on object");
                            }
                            for (int i = 0; i < obj->num_fields; i++) {
                                free(obj->field_names[i]);
                            }
                            free(obj->field_names);
                            free(obj->field_values);
                            if (obj->type_name) free(obj->type_name);
                            obj->field_names = NULL;
                            obj->field_values = NULL;
                            obj->type_name = NULL;
                            obj->num_fields = 0;
                        } else if (arg.type == VAL_NULL) {
                            // free(null) is a no-op
                        } else {
                            THROW_ERROR("free() requires a pointer, buffer, object, or array");
                        }
                        result = vm_null_value();
                        break;
                    }
                    case BUILTIN_MEMSET: {
                        // memset(ptr, byte, size) - set memory
                        if (argc != 3) {
                            THROW_ERROR("memset() expects 3 arguments (ptr, byte, size)");
                        }
                        if (args[0].type != VAL_PTR) {
                            THROW_ERROR("memset() first argument must be a pointer");
                        }
                        void *ptr = args[0].as.as_ptr;
                        int byte = (int)value_to_i64(args[1]);
                        int64_t size = value_to_i64(args[2]);
                        if (size < 0) {
                            THROW_ERROR("memset() size cannot be negative");
                        }
                        memset(ptr, byte, (size_t)size);
                        result = vm_null_value();
                        break;
                    }
                    case BUILTIN_MEMCPY: {
                        // memcpy(dest, src, size) - copy memory
                        if (argc != 3) {
                            THROW_ERROR("memcpy() expects 3 arguments (dest, src, size)");
                        }
                        if (args[0].type != VAL_PTR || args[1].type != VAL_PTR) {
                            THROW_ERROR("memcpy() requires pointers for dest and src");
                        }
                        void *dest = args[0].as.as_ptr;
                        void *src = args[1].as.as_ptr;
                        int64_t size = value_to_i64(args[2]);
                        if (size < 0) {
                            THROW_ERROR("memcpy() size cannot be negative");
                        }
                        memcpy(dest, src, (size_t)size);
                        result = vm_null_value();
                        break;
                    }
                    case BUILTIN_SIZEOF: {
                        // sizeof(type) - get size of type
                        if (argc != 1) {
                            THROW_ERROR("sizeof() expects 1 argument (type)");
                        }
                        int size = 0;
                        if (args[0].type == VAL_TYPE) {
                            TypeKind kind = args[0].as.as_type;
                            switch (kind) {
                                case TYPE_I8: case TYPE_U8: size = 1; break;
                                case TYPE_I16: case TYPE_U16: size = 2; break;
                                case TYPE_I32: case TYPE_U32: case TYPE_F32: size = 4; break;
                                case TYPE_I64: case TYPE_U64: case TYPE_F64: size = 8; break;
                                case TYPE_PTR: case TYPE_BUFFER: size = sizeof(void*); break;
                                case TYPE_BOOL: size = sizeof(int); break;
                                case TYPE_RUNE: size = 4; break;
                                default: size = 0; break;
                            }
                        } else if (args[0].type == VAL_STRING && args[0].as.as_string) {
                            const char *type_name = args[0].as.as_string->data;
                            if (strcmp(type_name, "i8") == 0) size = 1;
                            else if (strcmp(type_name, "i16") == 0) size = 2;
                            else if (strcmp(type_name, "i32") == 0 || strcmp(type_name, "integer") == 0) size = 4;
                            else if (strcmp(type_name, "i64") == 0) size = 8;
                            else if (strcmp(type_name, "u8") == 0 || strcmp(type_name, "byte") == 0) size = 1;
                            else if (strcmp(type_name, "u16") == 0) size = 2;
                            else if (strcmp(type_name, "u32") == 0) size = 4;
                            else if (strcmp(type_name, "u64") == 0) size = 8;
                            else if (strcmp(type_name, "f32") == 0) size = 4;
                            else if (strcmp(type_name, "f64") == 0 || strcmp(type_name, "number") == 0) size = 8;
                            else if (strcmp(type_name, "bool") == 0) size = sizeof(int);
                            else if (strcmp(type_name, "ptr") == 0) size = sizeof(void*);
                            else if (strcmp(type_name, "rune") == 0) size = 4;
                            else size = 0;
                        } else {
                            THROW_ERROR("sizeof() requires a type argument");
                        }
                        result = val_i32_vm(size);
                        break;
                    }
                    case BUILTIN_BUFFER: {
                        // buffer(size) - create a safe buffer
                        if (argc != 1) {
                            THROW_ERROR("buffer() expects 1 argument (size)");
                        }
                        int64_t size = value_to_i64(args[0]);
                        if (size <= 0) {
                            THROW_ERROR("buffer() size must be positive");
                        }
                        Buffer *buf = malloc(sizeof(Buffer));
                        if (!buf) {
                            result = vm_null_value();
                            break;
                        }
                        buf->data = malloc((size_t)size);
                        if (!buf->data) {
                            free(buf);
                            result = vm_null_value();
                            break;
                        }
                        buf->length = (int)size;
                        buf->capacity = (int)size;
                        buf->ref_count = 1;
                        atomic_store(&buf->freed, 0);
                        result.type = VAL_BUFFER;
                        result.as.as_buffer = buf;
                        break;
                    }
                    case BUILTIN_BUFFER_PTR: {
                        // buffer_ptr(buffer) - get raw pointer from buffer
                        if (argc != 1) {
                            THROW_ERROR("buffer_ptr() expects 1 argument (buffer)");
                        }
                        if (args[0].type != VAL_BUFFER) {
                            THROW_ERROR("buffer_ptr() argument must be a buffer");
                        }
                        Buffer *buf = args[0].as.as_buffer;
                        result.type = VAL_PTR;
                        result.as.as_ptr = buf->data;
                        break;
                    }
                    case BUILTIN_PTR_NULL: {
                        // ptr_null() - get null pointer
                        if (argc != 0) {
                            THROW_ERROR("ptr_null() expects no arguments");
                        }
                        result.type = VAL_PTR;
                        result.as.as_ptr = NULL;
                        break;
                    }

                    // ========== Pointer Read Operations ==========
                    case BUILTIN_PTR_READ_I8: {
                        if (argc != 1 || args[0].type != VAL_PTR) {
                            THROW_ERROR("ptr_read_i8() expects 1 pointer argument");
                        }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("ptr_read_i8() cannot read from null pointer"); }
                        result.type = VAL_I32;
                        result.as.as_i32 = *(int8_t*)ptr;
                        break;
                    }
                    case BUILTIN_PTR_READ_I16: {
                        if (argc != 1 || args[0].type != VAL_PTR) {
                            THROW_ERROR("ptr_read_i16() expects 1 pointer argument");
                        }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("ptr_read_i16() cannot read from null pointer"); }
                        result.type = VAL_I32;
                        result.as.as_i32 = *(int16_t*)ptr;
                        break;
                    }
                    case BUILTIN_PTR_READ_I32: {
                        if (argc != 1 || args[0].type != VAL_PTR) {
                            THROW_ERROR("ptr_read_i32() expects 1 pointer argument");
                        }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("ptr_read_i32() cannot read from null pointer"); }
                        result.type = VAL_I32;
                        result.as.as_i32 = *(int32_t*)ptr;
                        break;
                    }
                    case BUILTIN_PTR_READ_I64: {
                        if (argc != 1 || args[0].type != VAL_PTR) {
                            THROW_ERROR("ptr_read_i64() expects 1 pointer argument");
                        }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("ptr_read_i64() cannot read from null pointer"); }
                        result.type = VAL_I64;
                        result.as.as_i64 = *(int64_t*)ptr;
                        break;
                    }
                    case BUILTIN_PTR_READ_U8: {
                        if (argc != 1 || args[0].type != VAL_PTR) {
                            THROW_ERROR("ptr_read_u8() expects 1 pointer argument");
                        }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("ptr_read_u8() cannot read from null pointer"); }
                        result.type = VAL_I32;
                        result.as.as_i32 = *(uint8_t*)ptr;
                        break;
                    }
                    case BUILTIN_PTR_READ_U16: {
                        if (argc != 1 || args[0].type != VAL_PTR) {
                            THROW_ERROR("ptr_read_u16() expects 1 pointer argument");
                        }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("ptr_read_u16() cannot read from null pointer"); }
                        result.type = VAL_I32;
                        result.as.as_i32 = *(uint16_t*)ptr;
                        break;
                    }
                    case BUILTIN_PTR_READ_U32: {
                        if (argc != 1 || args[0].type != VAL_PTR) {
                            THROW_ERROR("ptr_read_u32() expects 1 pointer argument");
                        }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("ptr_read_u32() cannot read from null pointer"); }
                        result.type = VAL_I64;
                        result.as.as_i64 = *(uint32_t*)ptr;
                        break;
                    }
                    case BUILTIN_PTR_READ_U64: {
                        if (argc != 1 || args[0].type != VAL_PTR) {
                            THROW_ERROR("ptr_read_u64() expects 1 pointer argument");
                        }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("ptr_read_u64() cannot read from null pointer"); }
                        result.type = VAL_I64;
                        result.as.as_i64 = (int64_t)*(uint64_t*)ptr;
                        break;
                    }
                    case BUILTIN_PTR_READ_F32: {
                        if (argc != 1 || args[0].type != VAL_PTR) {
                            THROW_ERROR("ptr_read_f32() expects 1 pointer argument");
                        }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("ptr_read_f32() cannot read from null pointer"); }
                        result.type = VAL_F64;
                        result.as.as_f64 = *(float*)ptr;
                        break;
                    }
                    case BUILTIN_PTR_READ_F64: {
                        if (argc != 1 || args[0].type != VAL_PTR) {
                            THROW_ERROR("ptr_read_f64() expects 1 pointer argument");
                        }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("ptr_read_f64() cannot read from null pointer"); }
                        result.type = VAL_F64;
                        result.as.as_f64 = *(double*)ptr;
                        break;
                    }
                    case BUILTIN_PTR_READ_PTR: {
                        if (argc != 1 || args[0].type != VAL_PTR) {
                            THROW_ERROR("ptr_read_ptr() expects 1 pointer argument");
                        }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("ptr_read_ptr() cannot read from null pointer"); }
                        result.type = VAL_PTR;
                        result.as.as_ptr = *(void**)ptr;
                        break;
                    }
                    case BUILTIN_PTR_OFFSET: {
                        // ptr_offset(ptr, offset, element_size) - calculate pointer offset
                        if (argc != 3) {
                            THROW_ERROR("ptr_offset() expects 3 arguments (ptr, offset, element_size)");
                        }
                        if (args[0].type != VAL_PTR) {
                            THROW_ERROR("ptr_offset() first argument must be a pointer");
                        }
                        void *ptr = args[0].as.as_ptr;
                        int64_t offset = value_to_i64(args[1]);
                        int64_t elem_size = value_to_i64(args[2]);
                        result.type = VAL_PTR;
                        result.as.as_ptr = (char*)ptr + (offset * elem_size);
                        break;
                    }
                    case BUILTIN_PTR_DEREF_I32: {
                        // ptr_deref_i32(ptr) - alias for ptr_read_i32
                        if (argc != 1 || args[0].type != VAL_PTR) {
                            THROW_ERROR("ptr_deref_i32() expects 1 pointer argument");
                        }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("ptr_deref_i32() cannot dereference null pointer"); }
                        result.type = VAL_I32;
                        result.as.as_i32 = *(int32_t*)ptr;
                        break;
                    }

                    // ========== Pointer Write Operations ==========
                    case BUILTIN_PTR_WRITE_I8: {
                        if (argc != 2) { THROW_ERROR("ptr_write_i8() expects 2 arguments (ptr, value)"); }
                        if (args[0].type != VAL_PTR) { THROW_ERROR("ptr_write_i8() first arg must be pointer"); }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("ptr_write_i8() cannot write to null pointer"); }
                        *(int8_t*)ptr = (int8_t)value_to_i64(args[1]);
                        result = vm_null_value();
                        break;
                    }
                    case BUILTIN_PTR_WRITE_I16: {
                        if (argc != 2) { THROW_ERROR("ptr_write_i16() expects 2 arguments (ptr, value)"); }
                        if (args[0].type != VAL_PTR) { THROW_ERROR("ptr_write_i16() first arg must be pointer"); }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("ptr_write_i16() cannot write to null pointer"); }
                        *(int16_t*)ptr = (int16_t)value_to_i64(args[1]);
                        result = vm_null_value();
                        break;
                    }
                    case BUILTIN_PTR_WRITE_I32: {
                        if (argc != 2) { THROW_ERROR("ptr_write_i32() expects 2 arguments (ptr, value)"); }
                        if (args[0].type != VAL_PTR) { THROW_ERROR("ptr_write_i32() first arg must be pointer"); }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("ptr_write_i32() cannot write to null pointer"); }
                        *(int32_t*)ptr = (int32_t)value_to_i64(args[1]);
                        result = vm_null_value();
                        break;
                    }
                    case BUILTIN_PTR_WRITE_I64: {
                        if (argc != 2) { THROW_ERROR("ptr_write_i64() expects 2 arguments (ptr, value)"); }
                        if (args[0].type != VAL_PTR) { THROW_ERROR("ptr_write_i64() first arg must be pointer"); }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("ptr_write_i64() cannot write to null pointer"); }
                        *(int64_t*)ptr = value_to_i64(args[1]);
                        result = vm_null_value();
                        break;
                    }
                    case BUILTIN_PTR_WRITE_U8: {
                        if (argc != 2) { THROW_ERROR("ptr_write_u8() expects 2 arguments (ptr, value)"); }
                        if (args[0].type != VAL_PTR) { THROW_ERROR("ptr_write_u8() first arg must be pointer"); }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("ptr_write_u8() cannot write to null pointer"); }
                        *(uint8_t*)ptr = (uint8_t)value_to_i64(args[1]);
                        result = vm_null_value();
                        break;
                    }
                    case BUILTIN_PTR_WRITE_U16: {
                        if (argc != 2) { THROW_ERROR("ptr_write_u16() expects 2 arguments (ptr, value)"); }
                        if (args[0].type != VAL_PTR) { THROW_ERROR("ptr_write_u16() first arg must be pointer"); }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("ptr_write_u16() cannot write to null pointer"); }
                        *(uint16_t*)ptr = (uint16_t)value_to_i64(args[1]);
                        result = vm_null_value();
                        break;
                    }
                    case BUILTIN_PTR_WRITE_U32: {
                        if (argc != 2) { THROW_ERROR("ptr_write_u32() expects 2 arguments (ptr, value)"); }
                        if (args[0].type != VAL_PTR) { THROW_ERROR("ptr_write_u32() first arg must be pointer"); }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("ptr_write_u32() cannot write to null pointer"); }
                        *(uint32_t*)ptr = (uint32_t)value_to_i64(args[1]);
                        result = vm_null_value();
                        break;
                    }
                    case BUILTIN_PTR_WRITE_U64: {
                        if (argc != 2) { THROW_ERROR("ptr_write_u64() expects 2 arguments (ptr, value)"); }
                        if (args[0].type != VAL_PTR) { THROW_ERROR("ptr_write_u64() first arg must be pointer"); }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("ptr_write_u64() cannot write to null pointer"); }
                        *(uint64_t*)ptr = (uint64_t)value_to_i64(args[1]);
                        result = vm_null_value();
                        break;
                    }
                    case BUILTIN_PTR_WRITE_F32: {
                        if (argc != 2) { THROW_ERROR("ptr_write_f32() expects 2 arguments (ptr, value)"); }
                        if (args[0].type != VAL_PTR) { THROW_ERROR("ptr_write_f32() first arg must be pointer"); }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("ptr_write_f32() cannot write to null pointer"); }
                        *(float*)ptr = (float)value_to_f64(args[1]);
                        result = vm_null_value();
                        break;
                    }
                    case BUILTIN_PTR_WRITE_F64: {
                        if (argc != 2) { THROW_ERROR("ptr_write_f64() expects 2 arguments (ptr, value)"); }
                        if (args[0].type != VAL_PTR) { THROW_ERROR("ptr_write_f64() first arg must be pointer"); }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("ptr_write_f64() cannot write to null pointer"); }
                        *(double*)ptr = value_to_f64(args[1]);
                        result = vm_null_value();
                        break;
                    }
                    case BUILTIN_PTR_WRITE_PTR: {
                        if (argc != 2) { THROW_ERROR("ptr_write_ptr() expects 2 arguments (ptr, value)"); }
                        if (args[0].type != VAL_PTR) { THROW_ERROR("ptr_write_ptr() first arg must be pointer"); }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("ptr_write_ptr() cannot write to null pointer"); }
                        if (args[1].type != VAL_PTR && args[1].type != VAL_NULL) {
                            THROW_ERROR("ptr_write_ptr() second arg must be pointer or null");
                        }
                        *(void**)ptr = args[1].type == VAL_NULL ? NULL : args[1].as.as_ptr;
                        result = vm_null_value();
                        break;
                    }

                    // ========== Atomic Operations ==========
                    case BUILTIN_ATOMIC_LOAD_I32: {
                        if (argc != 1 || args[0].type != VAL_PTR) {
                            THROW_ERROR("atomic_load_i32() expects 1 pointer argument");
                        }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("atomic_load_i32() cannot load from null pointer"); }
                        _Atomic int32_t *aptr = (_Atomic int32_t*)ptr;
                        result.type = VAL_I32;
                        result.as.as_i32 = atomic_load(aptr);
                        break;
                    }
                    case BUILTIN_ATOMIC_STORE_I32: {
                        if (argc != 2) { THROW_ERROR("atomic_store_i32() expects 2 arguments (ptr, value)"); }
                        if (args[0].type != VAL_PTR) { THROW_ERROR("atomic_store_i32() first arg must be pointer"); }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("atomic_store_i32() cannot store to null pointer"); }
                        _Atomic int32_t *aptr = (_Atomic int32_t*)ptr;
                        atomic_store(aptr, (int32_t)value_to_i64(args[1]));
                        result = vm_null_value();
                        break;
                    }
                    case BUILTIN_ATOMIC_ADD_I32: {
                        if (argc != 2) { THROW_ERROR("atomic_add_i32() expects 2 arguments (ptr, value)"); }
                        if (args[0].type != VAL_PTR) { THROW_ERROR("atomic_add_i32() first arg must be pointer"); }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("atomic_add_i32() cannot operate on null pointer"); }
                        _Atomic int32_t *aptr = (_Atomic int32_t*)ptr;
                        result.type = VAL_I32;
                        result.as.as_i32 = atomic_fetch_add(aptr, (int32_t)value_to_i64(args[1]));
                        break;
                    }
                    case BUILTIN_ATOMIC_SUB_I32: {
                        if (argc != 2) { THROW_ERROR("atomic_sub_i32() expects 2 arguments (ptr, value)"); }
                        if (args[0].type != VAL_PTR) { THROW_ERROR("atomic_sub_i32() first arg must be pointer"); }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("atomic_sub_i32() cannot operate on null pointer"); }
                        _Atomic int32_t *aptr = (_Atomic int32_t*)ptr;
                        result.type = VAL_I32;
                        result.as.as_i32 = atomic_fetch_sub(aptr, (int32_t)value_to_i64(args[1]));
                        break;
                    }
                    case BUILTIN_ATOMIC_AND_I32: {
                        if (argc != 2) { THROW_ERROR("atomic_and_i32() expects 2 arguments (ptr, value)"); }
                        if (args[0].type != VAL_PTR) { THROW_ERROR("atomic_and_i32() first arg must be pointer"); }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("atomic_and_i32() cannot operate on null pointer"); }
                        _Atomic int32_t *aptr = (_Atomic int32_t*)ptr;
                        result.type = VAL_I32;
                        result.as.as_i32 = atomic_fetch_and(aptr, (int32_t)value_to_i64(args[1]));
                        break;
                    }
                    case BUILTIN_ATOMIC_OR_I32: {
                        if (argc != 2) { THROW_ERROR("atomic_or_i32() expects 2 arguments (ptr, value)"); }
                        if (args[0].type != VAL_PTR) { THROW_ERROR("atomic_or_i32() first arg must be pointer"); }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("atomic_or_i32() cannot operate on null pointer"); }
                        _Atomic int32_t *aptr = (_Atomic int32_t*)ptr;
                        result.type = VAL_I32;
                        result.as.as_i32 = atomic_fetch_or(aptr, (int32_t)value_to_i64(args[1]));
                        break;
                    }
                    case BUILTIN_ATOMIC_XOR_I32: {
                        if (argc != 2) { THROW_ERROR("atomic_xor_i32() expects 2 arguments (ptr, value)"); }
                        if (args[0].type != VAL_PTR) { THROW_ERROR("atomic_xor_i32() first arg must be pointer"); }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("atomic_xor_i32() cannot operate on null pointer"); }
                        _Atomic int32_t *aptr = (_Atomic int32_t*)ptr;
                        result.type = VAL_I32;
                        result.as.as_i32 = atomic_fetch_xor(aptr, (int32_t)value_to_i64(args[1]));
                        break;
                    }
                    case BUILTIN_ATOMIC_CAS_I32: {
                        if (argc != 3) { THROW_ERROR("atomic_cas_i32() expects 3 arguments (ptr, expected, desired)"); }
                        if (args[0].type != VAL_PTR) { THROW_ERROR("atomic_cas_i32() first arg must be pointer"); }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("atomic_cas_i32() cannot operate on null pointer"); }
                        _Atomic int32_t *aptr = (_Atomic int32_t*)ptr;
                        int32_t expected = (int32_t)value_to_i64(args[1]);
                        int32_t desired = (int32_t)value_to_i64(args[2]);
                        bool success = atomic_compare_exchange_strong(aptr, &expected, desired);
                        result.type = VAL_BOOL;
                        result.as.as_bool = success;
                        break;
                    }
                    case BUILTIN_ATOMIC_EXCHANGE_I32: {
                        if (argc != 2) { THROW_ERROR("atomic_exchange_i32() expects 2 arguments (ptr, value)"); }
                        if (args[0].type != VAL_PTR) { THROW_ERROR("atomic_exchange_i32() first arg must be pointer"); }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("atomic_exchange_i32() cannot operate on null pointer"); }
                        _Atomic int32_t *aptr = (_Atomic int32_t*)ptr;
                        result.type = VAL_I32;
                        result.as.as_i32 = atomic_exchange(aptr, (int32_t)value_to_i64(args[1]));
                        break;
                    }

                    // ========== Atomic i64 Operations ==========
                    case BUILTIN_ATOMIC_LOAD_I64: {
                        if (argc != 1 || args[0].type != VAL_PTR) {
                            THROW_ERROR("atomic_load_i64() expects 1 pointer argument");
                        }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("atomic_load_i64() cannot load from null pointer"); }
                        _Atomic int64_t *aptr = (_Atomic int64_t*)ptr;
                        result.type = VAL_I64;
                        result.as.as_i64 = atomic_load(aptr);
                        break;
                    }
                    case BUILTIN_ATOMIC_STORE_I64: {
                        if (argc != 2) { THROW_ERROR("atomic_store_i64() expects 2 arguments (ptr, value)"); }
                        if (args[0].type != VAL_PTR) { THROW_ERROR("atomic_store_i64() first arg must be pointer"); }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("atomic_store_i64() cannot store to null pointer"); }
                        _Atomic int64_t *aptr = (_Atomic int64_t*)ptr;
                        atomic_store(aptr, value_to_i64(args[1]));
                        result = vm_null_value();
                        break;
                    }
                    case BUILTIN_ATOMIC_ADD_I64: {
                        if (argc != 2) { THROW_ERROR("atomic_add_i64() expects 2 arguments (ptr, value)"); }
                        if (args[0].type != VAL_PTR) { THROW_ERROR("atomic_add_i64() first arg must be pointer"); }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("atomic_add_i64() cannot operate on null pointer"); }
                        _Atomic int64_t *aptr = (_Atomic int64_t*)ptr;
                        result.type = VAL_I64;
                        result.as.as_i64 = atomic_fetch_add(aptr, value_to_i64(args[1]));
                        break;
                    }
                    case BUILTIN_ATOMIC_SUB_I64: {
                        if (argc != 2) { THROW_ERROR("atomic_sub_i64() expects 2 arguments (ptr, value)"); }
                        if (args[0].type != VAL_PTR) { THROW_ERROR("atomic_sub_i64() first arg must be pointer"); }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("atomic_sub_i64() cannot operate on null pointer"); }
                        _Atomic int64_t *aptr = (_Atomic int64_t*)ptr;
                        result.type = VAL_I64;
                        result.as.as_i64 = atomic_fetch_sub(aptr, value_to_i64(args[1]));
                        break;
                    }
                    case BUILTIN_ATOMIC_AND_I64: {
                        if (argc != 2) { THROW_ERROR("atomic_and_i64() expects 2 arguments (ptr, value)"); }
                        if (args[0].type != VAL_PTR) { THROW_ERROR("atomic_and_i64() first arg must be pointer"); }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("atomic_and_i64() cannot operate on null pointer"); }
                        _Atomic int64_t *aptr = (_Atomic int64_t*)ptr;
                        result.type = VAL_I64;
                        result.as.as_i64 = atomic_fetch_and(aptr, value_to_i64(args[1]));
                        break;
                    }
                    case BUILTIN_ATOMIC_OR_I64: {
                        if (argc != 2) { THROW_ERROR("atomic_or_i64() expects 2 arguments (ptr, value)"); }
                        if (args[0].type != VAL_PTR) { THROW_ERROR("atomic_or_i64() first arg must be pointer"); }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("atomic_or_i64() cannot operate on null pointer"); }
                        _Atomic int64_t *aptr = (_Atomic int64_t*)ptr;
                        result.type = VAL_I64;
                        result.as.as_i64 = atomic_fetch_or(aptr, value_to_i64(args[1]));
                        break;
                    }
                    case BUILTIN_ATOMIC_XOR_I64: {
                        if (argc != 2) { THROW_ERROR("atomic_xor_i64() expects 2 arguments (ptr, value)"); }
                        if (args[0].type != VAL_PTR) { THROW_ERROR("atomic_xor_i64() first arg must be pointer"); }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("atomic_xor_i64() cannot operate on null pointer"); }
                        _Atomic int64_t *aptr = (_Atomic int64_t*)ptr;
                        result.type = VAL_I64;
                        result.as.as_i64 = atomic_fetch_xor(aptr, value_to_i64(args[1]));
                        break;
                    }
                    case BUILTIN_ATOMIC_CAS_I64: {
                        if (argc != 3) { THROW_ERROR("atomic_cas_i64() expects 3 arguments (ptr, expected, desired)"); }
                        if (args[0].type != VAL_PTR) { THROW_ERROR("atomic_cas_i64() first arg must be pointer"); }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("atomic_cas_i64() cannot operate on null pointer"); }
                        _Atomic int64_t *aptr = (_Atomic int64_t*)ptr;
                        int64_t expected = value_to_i64(args[1]);
                        int64_t desired = value_to_i64(args[2]);
                        bool success = atomic_compare_exchange_strong(aptr, &expected, desired);
                        result.type = VAL_BOOL;
                        result.as.as_bool = success;
                        break;
                    }
                    case BUILTIN_ATOMIC_EXCHANGE_I64: {
                        if (argc != 2) { THROW_ERROR("atomic_exchange_i64() expects 2 arguments (ptr, value)"); }
                        if (args[0].type != VAL_PTR) { THROW_ERROR("atomic_exchange_i64() first arg must be pointer"); }
                        void *ptr = args[0].as.as_ptr;
                        if (!ptr) { THROW_ERROR("atomic_exchange_i64() cannot operate on null pointer"); }
                        _Atomic int64_t *aptr = (_Atomic int64_t*)ptr;
                        result.type = VAL_I64;
                        result.as.as_i64 = atomic_exchange(aptr, value_to_i64(args[1]));
                        break;
                    }
                    case BUILTIN_ATOMIC_FENCE: {
                        // atomic_fence() - full memory barrier
                        atomic_thread_fence(memory_order_seq_cst);
                        result = vm_null_value();
                        break;
                    }

                    // ========== Signal Handling ==========
                    case BUILTIN_SIGNAL: {
                        // signal(signum, handler) - register signal handler
                        // For now, we support basic signal handling
                        if (argc != 2) {
                            THROW_ERROR("signal() expects 2 arguments (signum, handler)");
                        }
                        int signum = (int)value_to_i64(args[0]);
                        // Just acknowledge the signal registration for now
                        // Full signal handling would require storing handlers and calling them
                        result = vm_null_value();
                        (void)signum;  // Silence unused warning
                        break;
                    }

                    // ========== apply() builtin ==========
                    case BUILTIN_APPLY: {
                        // apply(fn, args_array) - call function with array of arguments
                        if (argc != 2) {
                            THROW_ERROR("apply() expects 2 arguments (function, args_array)");
                        }
                        if (!is_vm_closure(args[0])) {
                            THROW_ERROR("apply() first argument must be a function");
                        }
                        if (args[1].type != VAL_ARRAY || !args[1].as.as_array) {
                            THROW_ERROR("apply() second argument must be an array");
                        }
                        VMClosure *closure = as_vm_closure(args[0]);
                        Array *args_arr = args[1].as.as_array;
                        Chunk *fn_chunk = closure->chunk;

                        // Save the closure before popping
                        Value closure_val = args[0];

                        // Pop the apply arguments (fn, args_array)
                        vm_popn(vm, argc);

                        // Push the closure and all arguments onto the stack
                        // Stack layout should be: [closure] [arg0] [arg1] ... [argN]
                        PUSH(closure_val);
                        for (int i = 0; i < args_arr->length; i++) {
                            PUSH(args_arr->elements[i]);
                        }

                        // Push nulls for missing optional parameters
                        int call_argc = args_arr->length;
                        if (call_argc < fn_chunk->arity) {
                            int required = fn_chunk->arity - fn_chunk->optional_count;
                            if (call_argc < required) {
                                THROW_ERROR_FMT("Expected at least %d arguments but got %d", required, call_argc);
                            }
                            int missing = fn_chunk->arity - call_argc;
                            for (int i = 0; i < missing; i++) {
                                PUSH(vm_null_value());
                            }
                            call_argc = fn_chunk->arity;
                        }

                        // Save current frame state
                        frame->ip = ip;

                        // Check for stack overflow
                        if (vm->frame_count >= vm->frame_capacity) {
                            vm->frame_capacity *= 2;
                            vm->frames = realloc(vm->frames, sizeof(CallFrame) * vm->frame_capacity);
                        }

                        // Set up new call frame (same as BC_CALL)
                        CallFrame *new_frame = &vm->frames[vm->frame_count++];
                        new_frame->chunk = fn_chunk;
                        new_frame->ip = fn_chunk->code;
                        new_frame->slots = vm->stack_top - call_argc - 1;  // Include the closure slot
                        new_frame->upvalues = NULL;
                        new_frame->slot_count = fn_chunk->local_count;

                        // Update frame pointers
                        frame = new_frame;
                        ip = frame->ip;
                        slots = frame->slots;
                        continue;  // Continue dispatch loop with new frame
                    }

                    // ========== select() for channels ==========
                    case BUILTIN_SELECT: {
                        // select(channels, timeout_ms?) - wait for any channel to have data
                        if (argc < 1 || argc > 2) {
                            THROW_ERROR("select() expects 1-2 arguments (channels, timeout_ms?)");
                        }
                        if (args[0].type != VAL_ARRAY || !args[0].as.as_array) {
                            THROW_ERROR("select() first argument must be an array of channels");
                        }
                        Array *channels = args[0].as.as_array;
                        int timeout_ms = -1;  // -1 means infinite
                        if (argc > 1) {
                            timeout_ms = (int)value_to_i64(args[1]);
                        }
                        if (channels->length == 0) {
                            THROW_ERROR("select() requires at least one channel");
                        }
                        // Validate all elements are channels
                        for (int i = 0; i < channels->length; i++) {
                            if (channels->elements[i].type != VAL_CHANNEL) {
                                THROW_ERROR("select() array must contain only channels");
                            }
                        }
                        // Calculate deadline
                        struct timespec deadline;
                        struct timespec *deadline_ptr = NULL;
                        if (timeout_ms >= 0) {
                            clock_gettime(CLOCK_REALTIME, &deadline);
                            deadline.tv_sec += timeout_ms / 1000;
                            deadline.tv_nsec += (timeout_ms % 1000) * 1000000;
                            if (deadline.tv_nsec >= 1000000000) {
                                deadline.tv_sec++;
                                deadline.tv_nsec -= 1000000000;
                            }
                            deadline_ptr = &deadline;
                        }
                        // Polling loop with sleep
                        while (1) {
                            for (int i = 0; i < channels->length; i++) {
                                Channel *ch = channels->elements[i].as.as_channel;
                                pthread_mutex_t *mutex = (pthread_mutex_t*)ch->mutex;
                                pthread_mutex_lock(mutex);
                                if (ch->count > 0) {
                                    // Read the value
                                    Value msg = ch->buffer[ch->head];
                                    ch->head = (ch->head + 1) % ch->capacity;
                                    ch->count--;
                                    pthread_cond_signal((pthread_cond_t*)ch->not_full);
                                    pthread_mutex_unlock(mutex);
                                    // Create result object { channel, value }
                                    Object *res_obj = malloc(sizeof(Object));
                                    res_obj->field_names = malloc(sizeof(char*) * 2);
                                    res_obj->field_values = malloc(sizeof(Value) * 2);
                                    res_obj->field_names[0] = strdup("channel");
                                    res_obj->field_values[0] = channels->elements[i];
                                    res_obj->field_names[1] = strdup("value");
                                    res_obj->field_values[1] = msg;
                                    res_obj->num_fields = 2;
                                    res_obj->capacity = 2;
                                    res_obj->type_name = NULL;
                                    res_obj->ref_count = 1;
                                    atomic_store(&res_obj->freed, 0);
                                    result.type = VAL_OBJECT;
                                    result.as.as_object = res_obj;
                                    goto select_done;
                                }
                                if (ch->closed) {
                                    pthread_mutex_unlock(mutex);
                                    Object *res_obj = malloc(sizeof(Object));
                                    res_obj->field_names = malloc(sizeof(char*) * 2);
                                    res_obj->field_values = malloc(sizeof(Value) * 2);
                                    res_obj->field_names[0] = strdup("channel");
                                    res_obj->field_values[0] = channels->elements[i];
                                    res_obj->field_names[1] = strdup("value");
                                    res_obj->field_values[1] = vm_null_value();
                                    res_obj->num_fields = 2;
                                    res_obj->capacity = 2;
                                    res_obj->type_name = NULL;
                                    res_obj->ref_count = 1;
                                    atomic_store(&res_obj->freed, 0);
                                    result.type = VAL_OBJECT;
                                    result.as.as_object = res_obj;
                                    goto select_done;
                                }
                                pthread_mutex_unlock(mutex);
                            }
                            // Check timeout
                            if (deadline_ptr) {
                                struct timespec now;
                                clock_gettime(CLOCK_REALTIME, &now);
                                if (now.tv_sec > deadline_ptr->tv_sec ||
                                    (now.tv_sec == deadline_ptr->tv_sec && now.tv_nsec >= deadline_ptr->tv_nsec)) {
                                    result = vm_null_value();
                                    goto select_done;
                                }
                            }
                            // Sleep briefly before retrying
                            struct timespec sleep_time = {0, 1000000};  // 1ms
                            nanosleep(&sleep_time, NULL);
                        }
                        select_done:
                        break;
                    }

                    // ========== raise() for signals ==========
                    case BUILTIN_RAISE: {
                        if (argc != 1) {
                            THROW_ERROR("raise() expects 1 argument (signum)");
                        }
                        int signum = (int)value_to_i64(args[0]);
                        if (raise(signum) != 0) {
                            THROW_ERROR_FMT("raise() failed for signal %d: %s", signum, strerror(errno));
                        }
                        result = vm_null_value();
                        break;
                    }

                    // ========== exec() for command execution ==========
                    case BUILTIN_EXEC: {
                        if (argc != 1) {
                            THROW_ERROR("exec() expects 1 argument (command string)");
                        }
                        if (args[0].type != VAL_STRING || !args[0].as.as_string) {
                            THROW_ERROR("exec() argument must be a string");
                        }
                        String *command = args[0].as.as_string;
                        char *ccmd = malloc(command->length + 1);
                        memcpy(ccmd, command->data, command->length);
                        ccmd[command->length] = '\0';
                        FILE *pipe_f = popen(ccmd, "r");
                        if (!pipe_f) {
                            free(ccmd);
                            THROW_ERROR_FMT("exec() failed: %s", strerror(errno));
                        }
                        // Read output
                        char *output = NULL;
                        size_t output_size = 0;
                        size_t output_cap = 4096;
                        output = malloc(output_cap);
                        char chunk[4096];
                        size_t bytes;
                        while ((bytes = fread(chunk, 1, sizeof(chunk), pipe_f)) > 0) {
                            while (output_size + bytes > output_cap) {
                                output_cap *= 2;
                                output = realloc(output, output_cap);
                            }
                            memcpy(output + output_size, chunk, bytes);
                            output_size += bytes;
                        }
                        int status = pclose(pipe_f);
                        int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
                        free(ccmd);
                        // Create result object
                        Object *res = malloc(sizeof(Object));
                        res->field_names = malloc(sizeof(char*) * 2);
                        res->field_values = malloc(sizeof(Value) * 2);
                        res->field_names[0] = strdup("output");
                        String *out_str = malloc(sizeof(String));
                        out_str->data = output;
                        out_str->length = (int)output_size;
                        out_str->capacity = (int)output_cap;
                        out_str->ref_count = 1;
                        out_str->char_length = -1;
                        res->field_values[0].type = VAL_STRING;
                        res->field_values[0].as.as_string = out_str;
                        res->field_names[1] = strdup("exit_code");
                        res->field_values[1] = val_i32_vm(exit_code);
                        res->num_fields = 2;
                        res->capacity = 2;
                        res->type_name = NULL;
                        res->ref_count = 1;
                        atomic_store(&res->freed, 0);
                        result.type = VAL_OBJECT;
                        result.as.as_object = res;
                        break;
                    }

                    // ========== exec_argv() for safe command execution ==========
                    case BUILTIN_EXEC_ARGV: {
                        if (argc != 1) {
                            THROW_ERROR("exec_argv() expects 1 argument (array of strings)");
                        }
                        if (args[0].type != VAL_ARRAY || !args[0].as.as_array) {
                            THROW_ERROR("exec_argv() argument must be an array of strings");
                        }
                        Array *arr = args[0].as.as_array;
                        if (arr->length == 0) {
                            THROW_ERROR("exec_argv() array must not be empty");
                        }
                        // Build argv
                        char **argv = malloc((arr->length + 1) * sizeof(char*));
                        for (int i = 0; i < arr->length; i++) {
                            if (arr->elements[i].type != VAL_STRING) {
                                for (int j = 0; j < i; j++) free(argv[j]);
                                free(argv);
                                THROW_ERROR("exec_argv() array elements must be strings");
                            }
                            String *s = arr->elements[i].as.as_string;
                            argv[i] = malloc(s->length + 1);
                            memcpy(argv[i], s->data, s->length);
                            argv[i][s->length] = '\0';
                        }
                        argv[arr->length] = NULL;
                        // Create pipes
                        int stdout_pipe[2], stderr_pipe[2];
                        if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
                            for (int i = 0; i < arr->length; i++) free(argv[i]);
                            free(argv);
                            THROW_ERROR("exec_argv() pipe creation failed");
                        }
                        pid_t pid = fork();
                        if (pid < 0) {
                            for (int i = 0; i < arr->length; i++) free(argv[i]);
                            free(argv);
                            close(stdout_pipe[0]); close(stdout_pipe[1]);
                            close(stderr_pipe[0]); close(stderr_pipe[1]);
                            THROW_ERROR("exec_argv() fork failed");
                        }
                        if (pid == 0) {
                            // Child
                            close(stdout_pipe[0]);
                            close(stderr_pipe[0]);
                            dup2(stdout_pipe[1], STDOUT_FILENO);
                            dup2(stderr_pipe[1], STDERR_FILENO);
                            close(stdout_pipe[1]);
                            close(stderr_pipe[1]);
                            execvp(argv[0], argv);
                            _exit(127);
                        }
                        // Parent
                        close(stdout_pipe[1]);
                        close(stderr_pipe[1]);
                        for (int i = 0; i < arr->length; i++) free(argv[i]);
                        free(argv);
                        // Read stdout
                        char *output = NULL;
                        size_t output_size = 0;
                        size_t output_cap = 4096;
                        output = malloc(output_cap);
                        char buf[4096];
                        ssize_t n;
                        while ((n = read(stdout_pipe[0], buf, sizeof(buf))) > 0) {
                            while (output_size + n > output_cap) {
                                output_cap *= 2;
                                output = realloc(output, output_cap);
                            }
                            memcpy(output + output_size, buf, n);
                            output_size += n;
                        }
                        close(stdout_pipe[0]);
                        close(stderr_pipe[0]);
                        int status;
                        waitpid(pid, &status, 0);
                        int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
                        // Create result object
                        Object *res = malloc(sizeof(Object));
                        res->field_names = malloc(sizeof(char*) * 2);
                        res->field_values = malloc(sizeof(Value) * 2);
                        res->field_names[0] = strdup("output");
                        String *out_str = malloc(sizeof(String));
                        out_str->data = output;
                        out_str->length = (int)output_size;
                        out_str->capacity = (int)output_cap;
                        out_str->ref_count = 1;
                        out_str->char_length = -1;
                        res->field_values[0].type = VAL_STRING;
                        res->field_values[0].as.as_string = out_str;
                        res->field_names[1] = strdup("exit_code");
                        res->field_values[1] = val_i32_vm(exit_code);
                        res->num_fields = 2;
                        res->capacity = 2;
                        res->type_name = NULL;
                        res->ref_count = 1;
                        atomic_store(&res->freed, 0);
                        result.type = VAL_OBJECT;
                        result.as.as_object = res;
                        break;
                    }

                    // ========== ptr_to_buffer() ==========
                    case BUILTIN_PTR_TO_BUFFER: {
                        // ptr_to_buffer(ptr, size) - wrap raw pointer in buffer
                        if (argc != 2) {
                            THROW_ERROR("ptr_to_buffer() expects 2 arguments (ptr, size)");
                        }
                        if (args[0].type != VAL_PTR) {
                            THROW_ERROR("ptr_to_buffer() first argument must be a pointer");
                        }
                        int64_t size = value_to_i64(args[1]);
                        if (size <= 0) {
                            THROW_ERROR("ptr_to_buffer() size must be positive");
                        }
                        Buffer *buf = malloc(sizeof(Buffer));
                        buf->data = args[0].as.as_ptr;
                        buf->length = (int)size;
                        buf->capacity = (int)size;
                        buf->ref_count = 1;
                        atomic_store(&buf->freed, 0);
                        result.type = VAL_BUFFER;
                        result.as.as_buffer = buf;
                        break;
                    }

                    // ========== task_debug_info() ==========
                    case BUILTIN_TASK_DEBUG_INFO: {
                        // task_debug_info(task) - get debug info about a task
                        if (argc != 1) {
                            THROW_ERROR("task_debug_info() expects 1 argument (task)");
                        }
                        // For now, return a basic object with status info
                        // In the VM, we don't have access to the full task info easily
                        Object *info = malloc(sizeof(Object));
                        info->field_names = malloc(sizeof(char*) * 1);
                        info->field_values = malloc(sizeof(Value) * 1);
                        info->field_names[0] = strdup("status");
                        // Create string value for "unknown"
                        String *status_str = malloc(sizeof(String));
                        status_str->data = strdup("unknown");
                        status_str->length = 7;
                        status_str->capacity = 8;
                        status_str->ref_count = 1;
                        status_str->char_length = 7;
                        info->field_values[0].type = VAL_STRING;
                        info->field_values[0].as.as_string = status_str;
                        info->num_fields = 1;
                        info->capacity = 1;
                        info->type_name = NULL;
                        info->ref_count = 1;
                        atomic_store(&info->freed, 0);
                        result.type = VAL_OBJECT;
                        result.as.as_object = info;
                        break;
                    }

                    default:
                        vm_runtime_error(vm, "Builtin %d not implemented", builtin_id);
                        return VM_RUNTIME_ERROR;
                }

                vm_popn(vm, argc);
                PUSH(result);
                break;
            }

            case BC_CALL: {
                uint8_t argc = READ_BYTE();
                Value callee = PEEK(argc);

                // Handle builtin function values (from stdlib)
                if (callee.type == VAL_BUILTIN_FN) {
                    // The callee is a builtin function pointer
                    BuiltinFn fn = callee.as.as_builtin_fn;

                    // Create args array pointing to stack values
                    Value *args = vm->stack_top - argc;

                    // Call the builtin (pass NULL for ctx - VM doesn't use it)
                    Value result = fn(args, argc, NULL);

                    // Pop args and callee, push result
                    vm_popn(vm, argc + 1);
                    PUSH(result);
                    break;
                }

                if (!is_vm_closure(callee)) {
                    THROW_ERROR("Can only call functions");
                }

                VMClosure *closure = as_vm_closure(callee);
                Chunk *fn_chunk = closure->chunk;

                // Handle rest parameters
                if (fn_chunk->has_rest_param) {
                    // Rest param functions: collect extra args into array
                    int regular_params = fn_chunk->arity;
                    int rest_count = (argc > regular_params) ? argc - regular_params : 0;

                    // Create array for rest arguments
                    Array *rest_arr = malloc(sizeof(Array));
                    rest_arr->elements = malloc(sizeof(Value) * (rest_count > 0 ? rest_count : 1));
                    rest_arr->length = rest_count;
                    rest_arr->capacity = rest_count > 0 ? rest_count : 1;
                    rest_arr->element_type = NULL;
                    rest_arr->ref_count = 1;

                    // Copy rest arguments to array
                    Value *args_start = vm->stack_top - argc;
                    for (int i = 0; i < rest_count; i++) {
                        rest_arr->elements[i] = args_start[regular_params + i];
                    }

                    // Pop extra arguments from stack (keep only regular params)
                    if (rest_count > 0) {
                        vm->stack_top -= rest_count;
                    }

                    // Push null for missing regular params
                    if (argc < regular_params) {
                        int required = regular_params - fn_chunk->optional_count;
                        if (argc < required) {
                            free(rest_arr->elements);
                            free(rest_arr);
                            THROW_ERROR_FMT("Expected at least %d arguments but got %d", required, argc);
                        }
                        int missing = regular_params - argc;
                        for (int i = 0; i < missing; i++) {
                            PUSH(vm_null_value());
                        }
                    }

                    // Push the rest array
                    Value rest_val;
                    rest_val.type = VAL_ARRAY;
                    rest_val.as.as_array = rest_arr;
                    PUSH(rest_val);

                    argc = regular_params + 1;  // regular params + rest array
                } else {
                    // Check arity for non-rest-param functions
                    if (argc < fn_chunk->arity) {
                        // Allow optional params
                        int required = fn_chunk->arity - fn_chunk->optional_count;
                        if (argc < required) {
                            THROW_ERROR_FMT("Expected at least %d arguments but got %d", required, argc);
                        }
                        // Push null for missing optional parameters
                        int missing = fn_chunk->arity - argc;
                        for (int i = 0; i < missing; i++) {
                            PUSH(vm_null_value());
                        }
                        argc = fn_chunk->arity;
                    }
                }

                // Save current frame state
                frame->ip = ip;

                // Check for stack overflow
                if (vm->frame_count >= vm->frame_capacity) {
                    vm->frame_capacity *= 2;
                    vm->frames = realloc(vm->frames, sizeof(CallFrame) * vm->frame_capacity);
                }

                // Set up new call frame
                // The stack layout is: [callee] [arg0] [arg1] ... [argN]
                // After call, slots points to where callee was
                CallFrame *new_frame = &vm->frames[vm->frame_count++];
                new_frame->chunk = fn_chunk;
                new_frame->ip = fn_chunk->code;
                new_frame->slots = vm->stack_top - argc - 1;  // Include the callee slot
                new_frame->upvalues = NULL;  // TODO: support upvalues
                new_frame->slot_count = fn_chunk->local_count;

                // Update frame pointers
                frame = new_frame;
                ip = frame->ip;
                slots = frame->slots;
                break;
            }

            case BC_CLOSURE: {
                // Read function index from constant pool
                Constant c = READ_CONSTANT();
                uint8_t upvalue_count = READ_BYTE();

                if (c.type != CONST_FUNCTION) {
                    vm_runtime_error(vm, "Expected function in constant pool");
                    return VM_RUNTIME_ERROR;
                }

                Chunk *fn_chunk = c.as.function;
                VMClosure *closure = vm_closure_new(fn_chunk);

                // Capture upvalues
                for (int i = 0; i < upvalue_count; i++) {
                    uint8_t is_local = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (is_local) {
                        closure->upvalues[i] = vm_capture_upvalue(vm, slots + index);
                    } else {
                        // Get from enclosing closure's upvalues
                        // The enclosing closure is in slot 0
                        Value enclosing_val = slots[0];
                        if (is_vm_closure(enclosing_val)) {
                            VMClosure *enclosing = as_vm_closure(enclosing_val);
                            if (enclosing && index < enclosing->upvalue_count) {
                                closure->upvalues[i] = enclosing->upvalues[index];
                            } else {
                                closure->upvalues[i] = NULL;
                            }
                        } else {
                            closure->upvalues[i] = NULL;
                        }
                    }
                }

                PUSH(val_vm_closure(closure));
                break;
            }

            case BC_DEFER: {
                // Pop the closure from the stack and add to defer stack
                Value closure_val = POP();
                if (!is_vm_closure(closure_val)) {
                    THROW_ERROR("defer requires a closure");
                }

                // Ensure defer stack has capacity
                if (vm->defer_count >= vm->defer_capacity) {
                    vm->defer_capacity *= 2;
                    vm->defers = realloc(vm->defers, sizeof(DeferEntry) * vm->defer_capacity);
                }

                // Store the deferred closure
                DeferEntry *entry = &vm->defers[vm->defer_count++];
                VMClosure *closure = as_vm_closure(closure_val);
                entry->chunk = closure->chunk;
                entry->args = NULL;
                entry->arg_count = 0;
                entry->frame_index = vm->frame_count;  // Current frame index
                break;
            }

            case BC_RETURN: {
                Value result = POP();

                // Execute deferred closures in LIFO order for this frame
                int returning_frame_index = vm->frame_count;
                frame->ip = ip;  // Save IP

                while (vm->defer_count > 0 &&
                       vm->defers[vm->defer_count - 1].frame_index == returning_frame_index) {
                    DeferEntry *entry = &vm->defers[--vm->defer_count];

                    // Call the deferred closure using vm_call_closure
                    VMClosure *defer_closure = vm_closure_new(entry->chunk);
                    Value defer_result = vm_call_closure(vm, defer_closure, NULL, 0);
                    (void)defer_result;  // Ignore return value
                }

                // Close upvalues
                vm_close_upvalues(vm, slots);

                vm->frame_count--;
                if (vm->frame_count == 0) {
                    // Script/async function returning - store result
                    vm->is_returning = true;
                    vm->return_value = result;
                    return VM_OK;
                }

                if (vm->frame_count <= base_frame_count) {
                    // Callback returning to caller (vm_call_closure)
                    vm->stack_top = slots;
                    PUSH(result);
                    vm->is_returning = true;
                    vm->return_value = result;
                    return VM_OK;
                }

                // Normal function return - restore previous frame
                vm->stack_top = slots;
                PUSH(result);

                frame = &vm->frames[vm->frame_count - 1];
                ip = frame->ip;
                slots = frame->slots;
                break;
            }

            case BC_CALL_METHOD: {
                Constant method_c = READ_CONSTANT();
                uint8_t argc = READ_BYTE();
                const char *method = method_c.as.string.data;
                Value *args = vm->stack_top - argc;
                Value receiver = args[-1];  // Object is before args
                Value result = vm_null_value();

                if (receiver.type == VAL_ARRAY && receiver.as.as_array) {
                    Array *arr = receiver.as.as_array;
                    if (strcmp(method, "push") == 0 && argc >= 1) {
                        // Ensure capacity
                        if (arr->length >= arr->capacity) {
                            arr->capacity = arr->capacity * 2;
                            arr->elements = realloc(arr->elements, sizeof(Value) * arr->capacity);
                        }
                        arr->elements[arr->length++] = args[0];
                        result = val_i32_vm(arr->length);
                    } else if (strcmp(method, "pop") == 0) {
                        if (arr->length > 0) {
                            result = arr->elements[--arr->length];
                        }
                    } else if (strcmp(method, "shift") == 0) {
                        if (arr->length > 0) {
                            result = arr->elements[0];
                            memmove(arr->elements, arr->elements + 1, sizeof(Value) * (arr->length - 1));
                            arr->length--;
                        }
                    } else if (strcmp(method, "unshift") == 0 && argc >= 1) {
                        if (arr->length >= arr->capacity) {
                            arr->capacity = arr->capacity * 2;
                            arr->elements = realloc(arr->elements, sizeof(Value) * arr->capacity);
                        }
                        memmove(arr->elements + 1, arr->elements, sizeof(Value) * arr->length);
                        arr->elements[0] = args[0];
                        arr->length++;
                        result = val_i32_vm(arr->length);
                    } else if (strcmp(method, "join") == 0) {
                        const char *sep = (argc >= 1 && args[0].type == VAL_STRING)
                            ? args[0].as.as_string->data : ",";
                        // Calculate total length
                        int total = 0;
                        int sep_len = strlen(sep);
                        for (int i = 0; i < arr->length; i++) {
                            if (i > 0) total += sep_len;
                            if (arr->elements[i].type == VAL_STRING && arr->elements[i].as.as_string) {
                                total += arr->elements[i].as.as_string->length;
                            } else if (arr->elements[i].type == VAL_I32) {
                                total += 12;  // max int length
                            } else {
                                total += 10;  // placeholder
                            }
                        }
                        char *buf = malloc(total + 1);
                        buf[0] = '\0';
                        for (int i = 0; i < arr->length; i++) {
                            if (i > 0) strcat(buf, sep);
                            if (arr->elements[i].type == VAL_STRING && arr->elements[i].as.as_string) {
                                strcat(buf, arr->elements[i].as.as_string->data);
                            } else if (arr->elements[i].type == VAL_I32) {
                                char num[20];
                                sprintf(num, "%d", arr->elements[i].as.as_i32);
                                strcat(buf, num);
                            }
                        }
                        String *s = malloc(sizeof(String));
                        s->data = buf;
                        s->length = strlen(buf);
                        s->char_length = s->length;
                        s->capacity = total + 1;
                        s->ref_count = 1;
                        result.type = VAL_STRING;
                        result.as.as_string = s;
                    } else if (strcmp(method, "map") == 0 && argc >= 1) {
                        // map(callback) - transform each element
                        if (!is_vm_closure(args[0])) {
                            THROW_ERROR("map() callback must be a function");
                        }
                        VMClosure *callback = as_vm_closure(args[0]);

                        // Create new result array
                        Array *new_arr = malloc(sizeof(Array));
                        new_arr->elements = malloc(sizeof(Value) * (arr->length > 0 ? arr->length : 1));
                        new_arr->length = 0;
                        new_arr->capacity = arr->length > 0 ? arr->length : 1;
                        new_arr->element_type = NULL;
                        new_arr->ref_count = 1;

                        // Save frame state before calling closures
                        frame->ip = ip;

                        for (int i = 0; i < arr->length; i++) {
                            Value elem = arr->elements[i];
                            Value mapped = vm_call_closure(vm, callback, &elem, 1);
                            new_arr->elements[new_arr->length++] = mapped;
                        }

                        // Restore frame state after closure calls
                        frame = &vm->frames[vm->frame_count - 1];
                        ip = frame->ip;
                        slots = frame->slots;

                        result.type = VAL_ARRAY;
                        result.as.as_array = new_arr;
                    } else if (strcmp(method, "filter") == 0 && argc >= 1) {
                        // filter(callback) - keep elements where callback returns true
                        if (!is_vm_closure(args[0])) {
                            THROW_ERROR("filter() callback must be a function");
                        }
                        VMClosure *callback = as_vm_closure(args[0]);

                        // Create new result array
                        Array *new_arr = malloc(sizeof(Array));
                        new_arr->elements = malloc(sizeof(Value) * (arr->length > 0 ? arr->length : 1));
                        new_arr->length = 0;
                        new_arr->capacity = arr->length > 0 ? arr->length : 1;
                        new_arr->element_type = NULL;
                        new_arr->ref_count = 1;

                        // Save frame state
                        frame->ip = ip;

                        for (int i = 0; i < arr->length; i++) {
                            Value elem = arr->elements[i];
                            Value keep = vm_call_closure(vm, callback, &elem, 1);
                            if (value_is_truthy(keep)) {
                                if (new_arr->length >= new_arr->capacity) {
                                    new_arr->capacity *= 2;
                                    new_arr->elements = realloc(new_arr->elements, sizeof(Value) * new_arr->capacity);
                                }
                                new_arr->elements[new_arr->length++] = elem;
                            }
                        }

                        // Restore frame state
                        frame = &vm->frames[vm->frame_count - 1];
                        ip = frame->ip;
                        slots = frame->slots;

                        result.type = VAL_ARRAY;
                        result.as.as_array = new_arr;
                    } else if (strcmp(method, "reduce") == 0 && argc >= 1) {
                        // reduce(callback, initial?) - accumulate values
                        if (!is_vm_closure(args[0])) {
                            THROW_ERROR("reduce() callback must be a function");
                        }
                        VMClosure *callback = as_vm_closure(args[0]);

                        // Determine starting accumulator and index
                        Value accumulator;
                        int start_idx;
                        if (argc >= 2) {
                            accumulator = args[1];
                            start_idx = 0;
                        } else {
                            if (arr->length == 0) {
                                THROW_ERROR("reduce() on empty array with no initial value");
                            }
                            accumulator = arr->elements[0];
                            start_idx = 1;
                        }

                        // Save frame state
                        frame->ip = ip;

                        for (int i = start_idx; i < arr->length; i++) {
                            Value callback_args[2] = {accumulator, arr->elements[i]};
                            accumulator = vm_call_closure(vm, callback, callback_args, 2);
                        }

                        // Restore frame state
                        frame = &vm->frames[vm->frame_count - 1];
                        ip = frame->ip;
                        slots = frame->slots;

                        result = accumulator;
                    } else if (strcmp(method, "slice") == 0 && argc >= 2) {
                        // slice(start, end) - return new subarray
                        int start = value_to_i32(args[0]);
                        int end = value_to_i32(args[1]);
                        if (start < 0) start = 0;
                        if (start > arr->length) start = arr->length;
                        if (end < start) end = start;
                        if (end > arr->length) end = arr->length;

                        Array *new_arr = malloc(sizeof(Array));
                        int new_len = end - start;
                        new_arr->elements = malloc(sizeof(Value) * (new_len > 0 ? new_len : 1));
                        new_arr->length = new_len;
                        new_arr->capacity = new_len > 0 ? new_len : 1;
                        new_arr->element_type = NULL;
                        new_arr->ref_count = 1;
                        for (int i = 0; i < new_len; i++) {
                            new_arr->elements[i] = arr->elements[start + i];
                        }
                        result.type = VAL_ARRAY;
                        result.as.as_array = new_arr;
                    } else if (strcmp(method, "concat") == 0 && argc >= 1) {
                        // concat(other) - return new concatenated array
                        if (args[0].type != VAL_ARRAY || !args[0].as.as_array) {
                            THROW_ERROR("concat() argument must be an array");
                        }
                        Array *other = args[0].as.as_array;
                        int new_len = arr->length + other->length;
                        Array *new_arr = malloc(sizeof(Array));
                        new_arr->elements = malloc(sizeof(Value) * (new_len > 0 ? new_len : 1));
                        new_arr->length = new_len;
                        new_arr->capacity = new_len > 0 ? new_len : 1;
                        new_arr->element_type = NULL;
                        new_arr->ref_count = 1;
                        for (int i = 0; i < arr->length; i++) {
                            new_arr->elements[i] = arr->elements[i];
                        }
                        for (int i = 0; i < other->length; i++) {
                            new_arr->elements[arr->length + i] = other->elements[i];
                        }
                        result.type = VAL_ARRAY;
                        result.as.as_array = new_arr;
                    } else if (strcmp(method, "find") == 0 && argc >= 1) {
                        // find(value) - return index or -1
                        result = val_i32_vm(-1);
                        for (int i = 0; i < arr->length; i++) {
                            if (vm_values_equal(arr->elements[i], args[0])) {
                                result = val_i32_vm(i);
                                break;
                            }
                        }
                    } else if (strcmp(method, "contains") == 0 && argc >= 1) {
                        // contains(value) - return true/false
                        result = val_bool_vm(false);
                        for (int i = 0; i < arr->length; i++) {
                            if (vm_values_equal(arr->elements[i], args[0])) {
                                result = val_bool_vm(true);
                                break;
                            }
                        }
                    } else if (strcmp(method, "first") == 0) {
                        // first() - return first element or null
                        if (arr->length > 0) {
                            result = arr->elements[0];
                        }
                    } else if (strcmp(method, "last") == 0) {
                        // last() - return last element or null
                        if (arr->length > 0) {
                            result = arr->elements[arr->length - 1];
                        }
                    } else if (strcmp(method, "clear") == 0) {
                        // clear() - remove all elements
                        arr->length = 0;
                    } else if (strcmp(method, "reverse") == 0) {
                        // reverse() - reverse in place
                        int left = 0, right = arr->length - 1;
                        while (left < right) {
                            Value temp = arr->elements[left];
                            arr->elements[left] = arr->elements[right];
                            arr->elements[right] = temp;
                            left++;
                            right--;
                        }
                    } else if (strcmp(method, "insert") == 0 && argc >= 2) {
                        // insert(index, value)
                        int index = value_to_i32(args[0]);
                        if (index < 0 || index > arr->length) {
                            THROW_ERROR_FMT("insert index %d out of bounds (length %d)", index, arr->length);
                        }
                        if (arr->length >= arr->capacity) {
                            arr->capacity *= 2;
                            arr->elements = realloc(arr->elements, sizeof(Value) * arr->capacity);
                        }
                        memmove(arr->elements + index + 1, arr->elements + index, sizeof(Value) * (arr->length - index));
                        arr->elements[index] = args[1];
                        arr->length++;
                    } else if (strcmp(method, "remove") == 0 && argc >= 1) {
                        // remove(index) - remove and return element at index
                        int index = value_to_i32(args[0]);
                        if (index < 0 || index >= arr->length) {
                            THROW_ERROR_FMT("remove index %d out of bounds (length %d)", index, arr->length);
                        }
                        result = arr->elements[index];
                        memmove(arr->elements + index, arr->elements + index + 1, sizeof(Value) * (arr->length - index - 1));
                        arr->length--;
                    } else {
                        THROW_ERROR_FMT("Unknown array method: %s", method);
                    }
                } else if (receiver.type == VAL_STRING && receiver.as.as_string) {
                    String *str = receiver.as.as_string;
                    if (strcmp(method, "split") == 0) {
                        const char *sep = (argc >= 1 && args[0].type == VAL_STRING)
                            ? args[0].as.as_string->data : "";
                        // Simple split implementation
                        Array *arr = malloc(sizeof(Array));
                        arr->elements = malloc(sizeof(Value) * 8);
                        arr->length = 0;
                        arr->capacity = 8;
                        arr->element_type = NULL;
                        arr->ref_count = 1;

                        if (strlen(sep) == 0) {
                            // Split into chars
                            for (int i = 0; i < str->length; i++) {
                                if (arr->length >= arr->capacity) {
                                    arr->capacity *= 2;
                                    arr->elements = realloc(arr->elements, sizeof(Value) * arr->capacity);
                                }
                                String *ch = malloc(sizeof(String));
                                ch->data = malloc(2);
                                ch->data[0] = str->data[i];
                                ch->data[1] = '\0';
                                ch->length = 1;
                                ch->char_length = 1;
                                ch->capacity = 2;
                                ch->ref_count = 1;
                                arr->elements[arr->length].type = VAL_STRING;
                                arr->elements[arr->length].as.as_string = ch;
                                arr->length++;
                            }
                        } else {
                            // Split by separator
                            char *copy = strdup(str->data);
                            char *token = strtok(copy, sep);
                            while (token) {
                                if (arr->length >= arr->capacity) {
                                    arr->capacity *= 2;
                                    arr->elements = realloc(arr->elements, sizeof(Value) * arr->capacity);
                                }
                                String *s = malloc(sizeof(String));
                                s->data = strdup(token);
                                s->length = strlen(token);
                                s->char_length = s->length;
                                s->capacity = s->length + 1;
                                s->ref_count = 1;
                                arr->elements[arr->length].type = VAL_STRING;
                                arr->elements[arr->length].as.as_string = s;
                                arr->length++;
                                token = strtok(NULL, sep);
                            }
                            free(copy);
                        }
                        result.type = VAL_ARRAY;
                        result.as.as_array = arr;
                    } else if (strcmp(method, "contains") == 0 && argc >= 1) {
                        // Check if string contains substring
                        if (args[0].type != VAL_STRING || !args[0].as.as_string) {
                            result = val_bool_vm(false);
                        } else {
                            const char *needle = args[0].as.as_string->data;
                            result = val_bool_vm(strstr(str->data, needle) != NULL);
                        }
                    } else if (strcmp(method, "length") == 0) {
                        // String length (just return char_length)
                        result.type = VAL_I32;
                        result.as.as_i32 = str->char_length;
                    } else if (strcmp(method, "substr") == 0 && argc >= 2) {
                        // substr(start, length)
                        int start = value_to_i32(args[0]);
                        int len = value_to_i32(args[1]);
                        if (start < 0) start = 0;
                        if (start > str->length) start = str->length;
                        if (len < 0) len = 0;
                        if (start + len > str->length) len = str->length - start;
                        char *buf = malloc(len + 1);
                        memcpy(buf, str->data + start, len);
                        buf[len] = '\0';
                        result = vm_make_string(buf, len);
                        free(buf);
                    } else if (strcmp(method, "slice") == 0 && argc >= 2) {
                        // slice(start, end)
                        int start = value_to_i32(args[0]);
                        int end = value_to_i32(args[1]);
                        if (start < 0) start = 0;
                        if (start > str->length) start = str->length;
                        if (end < start) end = start;
                        if (end > str->length) end = str->length;
                        int len = end - start;
                        char *buf = malloc(len + 1);
                        memcpy(buf, str->data + start, len);
                        buf[len] = '\0';
                        result = vm_make_string(buf, len);
                        free(buf);
                    } else if (strcmp(method, "find") == 0 && argc >= 1) {
                        // find(substring)
                        if (args[0].type != VAL_STRING || !args[0].as.as_string) {
                            result = val_i32_vm(-1);
                        } else {
                            const char *needle = args[0].as.as_string->data;
                            char *found = strstr(str->data, needle);
                            result = val_i32_vm(found ? (int)(found - str->data) : -1);
                        }
                    } else if (strcmp(method, "trim") == 0) {
                        // trim() - remove leading/trailing whitespace
                        int start = 0, end = str->length;
                        while (start < end && (str->data[start] == ' ' || str->data[start] == '\t' ||
                               str->data[start] == '\n' || str->data[start] == '\r')) start++;
                        while (end > start && (str->data[end-1] == ' ' || str->data[end-1] == '\t' ||
                               str->data[end-1] == '\n' || str->data[end-1] == '\r')) end--;
                        int len = end - start;
                        char *buf = malloc(len + 1);
                        memcpy(buf, str->data + start, len);
                        buf[len] = '\0';
                        result = vm_make_string(buf, len);
                        free(buf);
                    } else if (strcmp(method, "to_upper") == 0) {
                        // to_upper()
                        char *buf = malloc(str->length + 1);
                        for (int i = 0; i < str->length; i++) {
                            buf[i] = (str->data[i] >= 'a' && str->data[i] <= 'z')
                                   ? str->data[i] - 32 : str->data[i];
                        }
                        buf[str->length] = '\0';
                        result = vm_make_string(buf, str->length);
                        free(buf);
                    } else if (strcmp(method, "to_lower") == 0) {
                        // to_lower()
                        char *buf = malloc(str->length + 1);
                        for (int i = 0; i < str->length; i++) {
                            buf[i] = (str->data[i] >= 'A' && str->data[i] <= 'Z')
                                   ? str->data[i] + 32 : str->data[i];
                        }
                        buf[str->length] = '\0';
                        result = vm_make_string(buf, str->length);
                        free(buf);
                    } else if (strcmp(method, "starts_with") == 0 && argc >= 1) {
                        // starts_with(prefix)
                        if (args[0].type != VAL_STRING || !args[0].as.as_string) {
                            result = val_bool_vm(false);
                        } else {
                            const char *prefix = args[0].as.as_string->data;
                            int prefix_len = args[0].as.as_string->length;
                            result = val_bool_vm(str->length >= prefix_len &&
                                                 strncmp(str->data, prefix, prefix_len) == 0);
                        }
                    } else if (strcmp(method, "ends_with") == 0 && argc >= 1) {
                        // ends_with(suffix)
                        if (args[0].type != VAL_STRING || !args[0].as.as_string) {
                            result = val_bool_vm(false);
                        } else {
                            const char *suffix = args[0].as.as_string->data;
                            int suffix_len = args[0].as.as_string->length;
                            result = val_bool_vm(str->length >= suffix_len &&
                                strcmp(str->data + str->length - suffix_len, suffix) == 0);
                        }
                    } else if (strcmp(method, "replace") == 0 && argc >= 2) {
                        // replace(old, new) - replace first occurrence
                        if (args[0].type != VAL_STRING || args[1].type != VAL_STRING) {
                            result.type = VAL_STRING;
                            result.as.as_string = str;
                        } else {
                            const char *old_str = args[0].as.as_string->data;
                            const char *new_str = args[1].as.as_string->data;
                            char *found = strstr(str->data, old_str);
                            if (!found) {
                                result.type = VAL_STRING;
                                result.as.as_string = str;
                            } else {
                                int old_len = args[0].as.as_string->length;
                                int new_len = args[1].as.as_string->length;
                                int result_len = str->length - old_len + new_len;
                                char *buf = malloc(result_len + 1);
                                int prefix_len = found - str->data;
                                memcpy(buf, str->data, prefix_len);
                                memcpy(buf + prefix_len, new_str, new_len);
                                memcpy(buf + prefix_len + new_len, found + old_len,
                                       str->length - prefix_len - old_len);
                                buf[result_len] = '\0';
                                result = vm_make_string(buf, result_len);
                                free(buf);
                            }
                        }
                    } else if (strcmp(method, "replace_all") == 0 && argc >= 2) {
                        // replace_all(old, new)
                        if (args[0].type != VAL_STRING || args[1].type != VAL_STRING) {
                            result.type = VAL_STRING;
                            result.as.as_string = str;
                        } else {
                            const char *old_str = args[0].as.as_string->data;
                            const char *new_str = args[1].as.as_string->data;
                            int old_len = args[0].as.as_string->length;
                            int new_len = args[1].as.as_string->length;
                            if (old_len == 0) {
                                result.type = VAL_STRING;
                                result.as.as_string = str;
                            } else {
                                // Count occurrences
                                int count = 0;
                                const char *p = str->data;
                                while ((p = strstr(p, old_str)) != NULL) {
                                    count++;
                                    p += old_len;
                                }
                                int result_len = str->length + count * (new_len - old_len);
                                char *buf = malloc(result_len + 1);
                                char *dest = buf;
                                p = str->data;
                                const char *prev = p;
                                while ((p = strstr(prev, old_str)) != NULL) {
                                    memcpy(dest, prev, p - prev);
                                    dest += p - prev;
                                    memcpy(dest, new_str, new_len);
                                    dest += new_len;
                                    prev = p + old_len;
                                }
                                strcpy(dest, prev);
                                result = vm_make_string(buf, result_len);
                                free(buf);
                            }
                        }
                    } else if (strcmp(method, "repeat") == 0 && argc >= 1) {
                        // repeat(count)
                        int count = value_to_i32(args[0]);
                        if (count <= 0) {
                            result = vm_make_string("", 0);
                        } else {
                            int result_len = str->length * count;
                            char *buf = malloc(result_len + 1);
                            for (int i = 0; i < count; i++) {
                                memcpy(buf + i * str->length, str->data, str->length);
                            }
                            buf[result_len] = '\0';
                            result = vm_make_string(buf, result_len);
                            free(buf);
                        }
                    } else if (strcmp(method, "char_at") == 0 && argc >= 1) {
                        // char_at(index) - returns rune at character index (UTF-8 aware)
                        int index = value_to_i32(args[0]);
                        int char_count = utf8_count_codepoints(str->data, str->length);
                        if (index < 0 || index >= char_count) {
                            result = vm_null_value();
                        } else {
                            int byte_pos = utf8_byte_offset(str->data, str->length, index);
                            uint32_t codepoint = utf8_decode_at(str->data, byte_pos);
                            result.type = VAL_RUNE;
                            result.as.as_rune = codepoint;
                        }
                    } else if (strcmp(method, "byte_at") == 0 && argc >= 1) {
                        // byte_at(index)
                        int index = value_to_i32(args[0]);
                        if (index < 0 || index >= str->length) {
                            result = val_i32_vm(0);
                        } else {
                            result = val_i32_vm((unsigned char)str->data[index]);
                        }
                    } else if (strcmp(method, "chars") == 0) {
                        // chars() - return array of runes (UTF-8 aware)
                        int char_count = utf8_count_codepoints(str->data, str->length);
                        Array *arr = malloc(sizeof(Array));
                        arr->elements = malloc(sizeof(Value) * (char_count > 0 ? char_count : 1));
                        arr->length = char_count;
                        arr->capacity = char_count > 0 ? char_count : 1;
                        arr->element_type = NULL;
                        arr->ref_count = 1;
                        int byte_pos = 0;
                        for (int i = 0; i < char_count; i++) {
                            uint32_t codepoint = utf8_decode_at(str->data, byte_pos);
                            arr->elements[i].type = VAL_RUNE;
                            arr->elements[i].as.as_rune = codepoint;
                            byte_pos += utf8_char_byte_length((unsigned char)str->data[byte_pos]);
                        }
                        result.type = VAL_ARRAY;
                        result.as.as_array = arr;
                    } else if (strcmp(method, "bytes") == 0) {
                        // bytes() - return array of byte values
                        Array *arr = malloc(sizeof(Array));
                        arr->elements = malloc(sizeof(Value) * (str->length > 0 ? str->length : 1));
                        arr->length = str->length;
                        arr->capacity = str->length > 0 ? str->length : 1;
                        arr->element_type = NULL;
                        arr->ref_count = 1;
                        for (int i = 0; i < str->length; i++) {
                            arr->elements[i] = val_i32_vm((unsigned char)str->data[i]);
                        }
                        result.type = VAL_ARRAY;
                        result.as.as_array = arr;
                    } else if (strcmp(method, "to_bytes") == 0) {
                        // to_bytes() - return array of byte values (u8)
                        Array *arr = malloc(sizeof(Array));
                        arr->elements = malloc(sizeof(Value) * (str->length > 0 ? str->length : 1));
                        arr->length = str->length;
                        arr->capacity = str->length > 0 ? str->length : 1;
                        arr->element_type = NULL;
                        arr->ref_count = 1;
                        for (int i = 0; i < str->length; i++) {
                            arr->elements[i].type = VAL_U8;
                            arr->elements[i].as.as_u8 = (uint8_t)str->data[i];
                        }
                        result.type = VAL_ARRAY;
                        result.as.as_array = arr;
                    } else if (strcmp(method, "deserialize") == 0) {
                        // deserialize() - parse JSON string into object/array/value
                        result = vm_json_parse(str->data, str->length);
                    } else {
                        THROW_ERROR_FMT("Unknown string method: %s", method);
                    }
                } else if (receiver.type == VAL_FILE && receiver.as.as_file) {
                    // File method calls
                    FileHandle *file = receiver.as.as_file;

                    if (strcmp(method, "read") == 0) {
                        // read() or read(size) - read text from file
                        if (file->closed) {
                            THROW_ERROR_FMT("Cannot read from closed file '%s'", file->path);
                        }
                        if (argc == 0) {
                            // Read entire file from current position
                            long current_pos = ftell(file->fp);
                            int is_seekable = (current_pos != -1 && fseek(file->fp, 0, SEEK_END) == 0);
                            if (is_seekable) {
                                long end_pos = ftell(file->fp);
                                fseek(file->fp, current_pos, SEEK_SET);
                                long size = end_pos - current_pos;
                                if (size <= 0) {
                                    result = vm_make_string("", 0);
                                } else {
                                    char *buffer = malloc(size + 1);
                                    size_t read_bytes = fread(buffer, 1, size, file->fp);
                                    buffer[read_bytes] = '\0';
                                    result = vm_make_string(buffer, read_bytes);
                                    free(buffer);
                                }
                            } else {
                                // Non-seekable: read in chunks
                                size_t capacity = 4096;
                                size_t total_read = 0;
                                char *buffer = malloc(capacity);
                                while (1) {
                                    if (total_read + 4096 > capacity) {
                                        capacity *= 2;
                                        buffer = realloc(buffer, capacity);
                                    }
                                    size_t bytes = fread(buffer + total_read, 1, 4096, file->fp);
                                    total_read += bytes;
                                    if (bytes < 4096) break;
                                }
                                buffer[total_read] = '\0';
                                result = vm_make_string(buffer, total_read);
                                free(buffer);
                            }
                        } else {
                            // Read specified size
                            int size = value_to_i32(args[0]);
                            if (size <= 0) {
                                result = vm_make_string("", 0);
                            } else {
                                char *buffer = malloc(size + 1);
                                size_t read_bytes = fread(buffer, 1, size, file->fp);
                                buffer[read_bytes] = '\0';
                                result = vm_make_string(buffer, read_bytes);
                                free(buffer);
                            }
                        }
                    } else if (strcmp(method, "write") == 0) {
                        // write(data) - write string to file
                        if (file->closed) {
                            THROW_ERROR_FMT("Cannot write to closed file '%s'", file->path);
                        }
                        if (argc < 1 || args[0].type != VAL_STRING) {
                            THROW_ERROR("write() expects string argument");
                        }
                        String *str = args[0].as.as_string;
                        size_t written = fwrite(str->data, 1, str->length, file->fp);
                        result = val_i32_vm((int32_t)written);
                    } else if (strcmp(method, "seek") == 0) {
                        // seek(position) - move file pointer
                        if (file->closed) {
                            THROW_ERROR_FMT("Cannot seek in closed file '%s'", file->path);
                        }
                        if (argc < 1) {
                            THROW_ERROR("seek() expects 1 argument (position)");
                        }
                        int position = value_to_i32(args[0]);
                        if (fseek(file->fp, position, SEEK_SET) != 0) {
                            THROW_ERROR_FMT("Seek error on file '%s': %s", file->path, strerror(errno));
                        }
                        result = val_i32_vm((int32_t)ftell(file->fp));
                    } else if (strcmp(method, "tell") == 0) {
                        // tell() - get current file position
                        if (file->closed) {
                            THROW_ERROR_FMT("Cannot tell position in closed file '%s'", file->path);
                        }
                        long pos = ftell(file->fp);
                        result = val_i32_vm((int32_t)pos);
                    } else if (strcmp(method, "close") == 0) {
                        // close() - close file
                        if (!file->closed && file->fp) {
                            fclose(file->fp);
                            file->fp = NULL;
                            file->closed = 1;
                        }
                        result = vm_null_value();
                    } else if (strcmp(method, "flush") == 0) {
                        // flush() - flush file buffer
                        if (!file->closed && file->fp) {
                            fflush(file->fp);
                        }
                        result = vm_null_value();
                    } else {
                        THROW_ERROR_FMT("File has no method '%s'", method);
                    }
                } else if (receiver.type == VAL_CHANNEL && receiver.as.as_channel) {
                    // Channel method calls
                    Channel *ch = receiver.as.as_channel;
                    pthread_mutex_t *mutex = (pthread_mutex_t*)ch->mutex;
                    pthread_cond_t *not_empty = (pthread_cond_t*)ch->not_empty;
                    pthread_cond_t *not_full = (pthread_cond_t*)ch->not_full;
                    pthread_cond_t *rendezvous = (pthread_cond_t*)ch->rendezvous;

                    if (strcmp(method, "send") == 0) {
                        // send(value) - send a message to the channel
                        if (argc != 1) {
                            THROW_ERROR("send() expects 1 argument");
                        }
                        Value msg = args[0];

                        pthread_mutex_lock(mutex);
                        if (ch->closed) {
                            pthread_mutex_unlock(mutex);
                            THROW_ERROR("cannot send to closed channel");
                        }

                        if (ch->capacity == 0) {
                            // Unbuffered channel - rendezvous with receiver
                            *(ch->unbuffered_value) = msg;
                            ch->sender_waiting = 1;
                            pthread_cond_signal(not_empty);
                            while (ch->sender_waiting && !ch->closed) {
                                pthread_cond_wait(rendezvous, mutex);
                            }
                            if (ch->closed && ch->sender_waiting) {
                                ch->sender_waiting = 0;
                                pthread_mutex_unlock(mutex);
                                THROW_ERROR("cannot send to closed channel");
                            }
                            pthread_mutex_unlock(mutex);
                        } else {
                            // Buffered channel - wait while full
                            while (ch->count >= ch->capacity && !ch->closed) {
                                pthread_cond_wait(not_full, mutex);
                            }
                            if (ch->closed) {
                                pthread_mutex_unlock(mutex);
                                THROW_ERROR("cannot send to closed channel");
                            }
                            ch->buffer[ch->tail] = msg;
                            ch->tail = (ch->tail + 1) % ch->capacity;
                            ch->count++;
                            pthread_cond_signal(not_empty);
                            pthread_mutex_unlock(mutex);
                        }
                    } else if (strcmp(method, "recv") == 0) {
                        // recv() - receive a message from the channel
                        if (argc != 0) {
                            THROW_ERROR("recv() expects 0 arguments");
                        }

                        pthread_mutex_lock(mutex);
                        if (ch->capacity == 0) {
                            // Unbuffered channel - rendezvous with sender
                            while (!ch->sender_waiting && !ch->closed) {
                                pthread_cond_wait(not_empty, mutex);
                            }
                            if (!ch->sender_waiting && ch->closed) {
                                pthread_mutex_unlock(mutex);
                                result = vm_null_value();
                            } else {
                                result = *(ch->unbuffered_value);
                                *(ch->unbuffered_value) = vm_null_value();
                                ch->sender_waiting = 0;
                                pthread_cond_signal(rendezvous);
                                pthread_mutex_unlock(mutex);
                            }
                        } else {
                            // Buffered channel - wait while empty
                            while (ch->count == 0 && !ch->closed) {
                                pthread_cond_wait(not_empty, mutex);
                            }
                            if (ch->count == 0 && ch->closed) {
                                pthread_mutex_unlock(mutex);
                                result = vm_null_value();
                            } else {
                                result = ch->buffer[ch->head];
                                ch->head = (ch->head + 1) % ch->capacity;
                                ch->count--;
                                pthread_cond_signal(not_full);
                                pthread_mutex_unlock(mutex);
                            }
                        }
                    } else if (strcmp(method, "close") == 0) {
                        // close() - close the channel
                        if (argc != 0) {
                            THROW_ERROR("close() expects 0 arguments");
                        }
                        pthread_mutex_lock(mutex);
                        ch->closed = 1;
                        pthread_cond_broadcast(not_empty);
                        pthread_cond_broadcast(not_full);
                        pthread_cond_broadcast(rendezvous);
                        pthread_mutex_unlock(mutex);
                    } else {
                        THROW_ERROR_FMT("Channel has no method '%s'", method);
                    }
                } else if (receiver.type == VAL_OBJECT && receiver.as.as_object) {
                    // Object method call - first check for built-in methods
                    Object *obj = receiver.as.as_object;

                    if (strcmp(method, "keys") == 0) {
                        // Return array of property names
                        Array *arr = malloc(sizeof(Array));
                        arr->elements = malloc(sizeof(Value) * (obj->num_fields + 1));
                        arr->length = obj->num_fields;
                        arr->capacity = obj->num_fields + 1;
                        for (int i = 0; i < obj->num_fields; i++) {
                            String *s = malloc(sizeof(String));
                            s->data = strdup(obj->field_names[i]);
                            s->length = strlen(s->data);
                            s->char_length = s->length;
                            s->capacity = s->length + 1;
                            s->ref_count = 1;
                            arr->elements[i].type = VAL_STRING;
                            arr->elements[i].as.as_string = s;
                        }
                        result.type = VAL_ARRAY;
                        result.as.as_array = arr;
                        goto object_method_done;
                    } else if (strcmp(method, "has") == 0 && argc >= 1) {
                        // Check if property exists
                        if (args[0].type != VAL_STRING) {
                            result = val_bool_vm(false);
                        } else {
                            const char *key = args[0].as.as_string->data;
                            bool found = false;
                            for (int i = 0; i < obj->num_fields; i++) {
                                if (strcmp(obj->field_names[i], key) == 0) {
                                    found = true;
                                    break;
                                }
                            }
                            result = val_bool_vm(found);
                        }
                        goto object_method_done;
                    } else if (strcmp(method, "serialize") == 0) {
                        // Simple JSON serialization
                        size_t buf_size = 1024;
                        char *buf = malloc(buf_size);
                        size_t pos = 0;
                        buf[pos++] = '{';
                        for (int i = 0; i < obj->num_fields; i++) {
                            if (i > 0) buf[pos++] = ',';
                            pos += snprintf(buf + pos, buf_size - pos, "\"%s\":", obj->field_names[i]);
                            Value v = obj->field_values[i];
                            char *val_str = value_to_string_alloc(v);
                            if (v.type == VAL_STRING) {
                                pos += snprintf(buf + pos, buf_size - pos, "\"%s\"", val_str);
                            } else {
                                pos += snprintf(buf + pos, buf_size - pos, "%s", val_str);
                            }
                            free(val_str);
                        }
                        buf[pos++] = '}';
                        buf[pos] = '\0';
                        String *s = malloc(sizeof(String));
                        s->data = buf;
                        s->length = pos;
                        s->char_length = pos;
                        s->capacity = buf_size;
                        s->ref_count = 1;
                        result.type = VAL_STRING;
                        result.as.as_string = s;
                        goto object_method_done;
                    }

                    // Not a built-in method - look up method property
                    Value method_val = vm_null_value();
                    bool found = false;

                    // Find the method in object properties
                    for (int i = 0; i < obj->num_fields; i++) {
                        if (strcmp(obj->field_names[i], method) == 0) {
                            method_val = obj->field_values[i];
                            found = true;
                            break;
                        }
                    }

                    if (!found) {
                        THROW_ERROR_FMT("Object has no method '%s'", method);
                    }

                    if (!is_vm_closure(method_val)) {
                        THROW_ERROR_FMT("Property '%s' is not a function", method);
                    }

                    VMClosure *closure = as_vm_closure(method_val);

                    // Save frame state before calling closure
                    frame->ip = ip;

                    // Set method_self so 'self' can be accessed inside the method
                    Value prev_self = vm->method_self;
                    vm->method_self = receiver;

                    // Call the method with the provided arguments
                    result = vm_call_closure(vm, closure, args, argc);

                    // Restore previous self
                    vm->method_self = prev_self;

                    // Restore frame state
                    frame = &vm->frames[vm->frame_count - 1];
                    ip = frame->ip;
                    slots = frame->slots;

                object_method_done:;
                } else {
                    THROW_ERROR_FMT("Cannot call method on %s", val_type_name(receiver.type));
                }

                // Pop args and receiver, push result
                vm_popn(vm, argc + 1);
                PUSH(result);
                break;
            }

            case BC_TYPEOF: {
                Value v = POP();
                const char *type_str;
                // Check for custom object type name
                if (v.type == VAL_OBJECT && v.as.as_object && v.as.as_object->type_name) {
                    type_str = v.as.as_object->type_name;
                } else {
                    type_str = val_type_name(v.type);
                }
                // Create string value
                String *s = malloc(sizeof(String));
                s->data = strdup(type_str);
                s->length = strlen(type_str);
                s->char_length = s->length;
                s->capacity = s->length + 1;
                s->ref_count = 1;
                Value sv;
                sv.type = VAL_STRING;
                sv.as.as_string = s;
                PUSH(sv);
                break;
            }

            case BC_CAST: {
                // Cast top of stack to specified type
                uint8_t target_type = READ_BYTE();
                Value v = POP();
                Value result;

                // Convert based on target type
                switch (target_type) {
                    case TYPE_ID_I8:
                        result.type = VAL_I8;
                        result.as.as_i8 = (int8_t)value_to_i64(v);
                        break;
                    case TYPE_ID_I16:
                        result.type = VAL_I16;
                        result.as.as_i16 = (int16_t)value_to_i64(v);
                        break;
                    case TYPE_ID_I32:
                        result.type = VAL_I32;
                        result.as.as_i32 = (int32_t)value_to_i64(v);
                        break;
                    case TYPE_ID_I64:
                        result.type = VAL_I64;
                        result.as.as_i64 = value_to_i64(v);
                        break;
                    case TYPE_ID_U8:
                        result.type = VAL_U8;
                        result.as.as_u8 = (uint8_t)value_to_i64(v);
                        break;
                    case TYPE_ID_U16:
                        result.type = VAL_U16;
                        result.as.as_u16 = (uint16_t)value_to_i64(v);
                        break;
                    case TYPE_ID_U32:
                        result.type = VAL_U32;
                        result.as.as_u32 = (uint32_t)value_to_i64(v);
                        break;
                    case TYPE_ID_U64:
                        result.type = VAL_U64;
                        result.as.as_u64 = (uint64_t)value_to_i64(v);
                        break;
                    case TYPE_ID_F32:
                        result.type = VAL_F32;
                        result.as.as_f32 = (float)value_to_f64(v);
                        break;
                    case TYPE_ID_F64:
                        result.type = VAL_F64;
                        result.as.as_f64 = value_to_f64(v);
                        break;
                    case TYPE_ID_BOOL:
                        result.type = VAL_BOOL;
                        result.as.as_bool = value_is_truthy(v);
                        break;
                    default:
                        // For other types, keep as-is
                        result = v;
                        break;
                }
                PUSH(result);
                break;
            }

            case BC_HALT:
                return VM_OK;

            case BC_NOP:
                break;

            // Exception handling
            case BC_TRY: {
                // Read catch and finally offsets (relative to position after both offsets)
                uint16_t catch_offset = READ_SHORT();
                uint16_t finally_offset = READ_SHORT();

                // Push exception handler
                if (vm->handler_count >= vm->handler_capacity) {
                    vm->handler_capacity *= 2;
                    vm->handlers = realloc(vm->handlers, sizeof(ExceptionHandler) * vm->handler_capacity);
                }

                ExceptionHandler *handler = &vm->handlers[vm->handler_count++];
                handler->catch_ip = ip + catch_offset;  // ip is now after both offsets
                handler->finally_ip = ip + finally_offset;
                handler->stack_top = vm->stack_top;
                handler->frame = frame;
                handler->frame_count = vm->frame_count;
                break;
            }

            case BC_THROW: {
                Value exception = POP();

                // Find nearest exception handler
                if (vm->handler_count > 0) {
                    ExceptionHandler *handler = &vm->handlers[vm->handler_count - 1];

                    // Restore stack to handler's saved state
                    vm->stack_top = handler->stack_top;

                    // Push exception for catch block
                    PUSH(exception);

                    // Jump to catch handler
                    ip = handler->catch_ip;
                } else {
                    // No handler - propagate as runtime error
                    vm->is_throwing = true;
                    vm->exception = exception;
                    if (exception.type == VAL_STRING && exception.as.as_string) {
                        vm_runtime_error(vm, "%s", exception.as.as_string->data);
                    } else {
                        vm_runtime_error(vm, "Uncaught exception");
                    }
                    return VM_RUNTIME_ERROR;
                }
                break;
            }

            case BC_CATCH:
                // Exception value is already on stack from BC_THROW
                // Just continue execution of catch block
                break;

            case BC_FINALLY:
                // Finally block just executes - nothing special needed
                break;

            case BC_END_TRY:
                // Pop exception handler
                if (vm->handler_count > 0) {
                    vm->handler_count--;
                }
                break;

            case BC_AWAIT: {
                // await task - same as join(task)
                Value task_val = POP();
                if (task_val.type != VAL_TASK) {
                    THROW_ERROR("await expects a task");
                }

                VMTask *task = (VMTask*)task_val.as.as_task;

                pthread_mutex_lock(&task->mutex);
                if (task->joined) {
                    pthread_mutex_unlock(&task->mutex);
                    THROW_ERROR("task handle already joined");
                }
                if (task->detached) {
                    pthread_mutex_unlock(&task->mutex);
                    THROW_ERROR("cannot await detached task");
                }
                task->joined = 1;
                pthread_mutex_unlock(&task->mutex);

                // Wait for thread to complete
                int rc = pthread_join(task->thread, NULL);
                if (rc != 0) {
                    THROW_ERROR("pthread_join failed");
                }

                // Check for exception
                pthread_mutex_lock(&task->mutex);
                if (task->has_exception) {
                    Value exc = task->exception;
                    pthread_mutex_unlock(&task->mutex);
                    vm->is_throwing = true;
                    vm->exception = exc;
                    goto handle_exception;
                }
                Value result = task->result;
                pthread_mutex_unlock(&task->mutex);

                PUSH(result);
                break;
            }

            default:
                vm_runtime_error(vm, "Unknown opcode %d", instruction);
                return VM_RUNTIME_ERROR;
        }

        // Skip exception handling if no exception pending
        continue;

handle_exception:
        // Handle a thrown exception (from THROW_ERROR macro)
        if (vm->handler_count > 0) {
            ExceptionHandler *handler = &vm->handlers[vm->handler_count - 1];

            // Restore stack to handler's saved state
            vm->stack_top = handler->stack_top;

            // Create exception string value and push it
            Value exception = vm_make_string(pending_exception_msg, strlen(pending_exception_msg));
            PUSH(exception);

            // Jump to catch handler
            ip = handler->catch_ip;

            // Restore frame if we unwound the stack
            if (vm->frame_count != handler->frame_count) {
                vm->frame_count = handler->frame_count;
                frame = &vm->frames[vm->frame_count - 1];
                slots = frame->slots;
            }

            // Clear pending exception and continue execution
            pending_exception_msg = NULL;
            continue;
        } else {
            // No handler - fatal error
            vm_runtime_error(vm, "%s", pending_exception_msg);
            return VM_RUNTIME_ERROR;
        }
    }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef PUSH
#undef POP
#undef PEEK
#undef THROW_ERROR
#undef THROW_ERROR_FMT
}

// ============================================
// Public Entry Point
// ============================================

VMResult vm_run(VM *vm, Chunk *chunk) {
    // Set up initial call frame
    CallFrame *initial_frame = &vm->frames[vm->frame_count++];
    initial_frame->chunk = chunk;
    initial_frame->ip = chunk->code;
    initial_frame->slots = vm->stack;
    initial_frame->upvalues = NULL;
    initial_frame->slot_count = chunk->local_count;

    // Execute from base frame 0
    return vm_execute(vm, 0);
}

// ============================================
// Debug
// ============================================

void vm_trace_execution(VM *vm, bool enable) {
    (void)vm;
    vm_trace_enabled = enable;
}

void vm_dump_stack(VM *vm) {
    printf("Stack: ");
    for (Value *slot = vm->stack; slot < vm->stack_top; slot++) {
        printf("[ ");
        print_value(*slot);
        printf(" ]");
    }
    printf("\n");
}

void vm_dump_globals(VM *vm) {
    printf("Globals:\n");
    for (int i = 0; i < vm->globals.count; i++) {
        printf("  %s = ", vm->globals.names[i]);
        print_value(vm->globals.values[i]);
        printf("\n");
    }
}
