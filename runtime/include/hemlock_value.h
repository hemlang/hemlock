/*
 * Hemlock Runtime Library - Value Types
 *
 * This header defines the core Value type used by compiled Hemlock programs.
 * It's a tagged union that can hold any Hemlock value at runtime.
 */

#ifndef HEMLOCK_VALUE_H
#define HEMLOCK_VALUE_H

#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <pthread.h>
#include <math.h>  // INFINITY/NAN for non-finite float literals in generated code

// Forward declarations for heap-allocated types
typedef struct HmlString HmlString;
typedef struct HmlArray HmlArray;
typedef struct HmlObject HmlObject;
typedef struct HmlBuffer HmlBuffer;
typedef struct HmlFunction HmlFunction;
typedef struct HmlFileHandle HmlFileHandle;
typedef struct HmlTask HmlTask;
typedef struct HmlChannel HmlChannel;
typedef struct HmlSocket HmlSocket;

// Task states
typedef enum {
    HML_TASK_READY = 0,
    HML_TASK_RUNNING = 1,
    HML_TASK_COMPLETED = 2
} HmlTaskState;

// Value types (same as interpreter)
typedef enum {
    HML_VAL_I8,
    HML_VAL_I16,
    HML_VAL_I32,
    HML_VAL_I64,
    HML_VAL_U8,
    HML_VAL_U16,
    HML_VAL_U32,
    HML_VAL_U64,
    HML_VAL_F32,
    HML_VAL_F64,
    HML_VAL_BOOL,
    HML_VAL_STRING,
    HML_VAL_RUNE,
    HML_VAL_PTR,
    HML_VAL_BUFFER,
    HML_VAL_ARRAY,
    HML_VAL_OBJECT,
    HML_VAL_FILE,
    HML_VAL_FUNCTION,
    HML_VAL_BUILTIN_FN,
    HML_VAL_TASK,
    HML_VAL_CHANNEL,
    HML_VAL_SOCKET,
    HML_VAL_NULL,
} HmlValueType;

// Forward declaration for builtin function signature
struct HmlValue;
typedef struct HmlValue (*HmlBuiltinFn)(struct HmlValue *args, int num_args);

// Runtime value (tagged union)
typedef struct HmlValue {
    HmlValueType type;
    union {
        int8_t as_i8;
        int16_t as_i16;
        int32_t as_i32;
        int64_t as_i64;
        uint8_t as_u8;
        uint16_t as_u16;
        uint32_t as_u32;
        uint64_t as_u64;
        float as_f32;
        double as_f64;
        int as_bool;
        HmlString *as_string;
        uint32_t as_rune;
        void *as_ptr;
        HmlBuffer *as_buffer;
        HmlArray *as_array;
        HmlObject *as_object;
        HmlFileHandle *as_file;
        HmlFunction *as_function;
        HmlBuiltinFn as_builtin_fn;
        HmlTask *as_task;
        HmlChannel *as_channel;
        HmlSocket *as_socket;
    } as;
} HmlValue;

// Small String Optimization threshold (strings up to this size are stored inline)
#define HML_SSO_THRESHOLD 23

// Immortal-refcount sentinel. Objects whose ref_count is set to this are
// "frozen": hml_retain / hml_release must never modify the count and never
// free them. Used for the pre-allocated single-char ASCII string pool,
// which is shared process-wide and must outlive everything.
//
// Why a sentinel + threshold instead of the old bare 1000000: the pool
// strings were set to ref_count = 1000000 but retain/release still did the
// normal atomic add/sub. A long-running program that releases single-char
// strings ~1e6 times (trivially reached by a busy HTTP server: every
// `"x" + s` materializes the pooled "x" then releases it) drove the count
// to 0 and string_free()'d a pooled string the global cache still points
// at — heap-use-after-free in hml_string_concat, crashing the process.
// Freezing at a sentinel that's checked *before* the atomic op makes the
// count permanently immovable, so it can neither underflow (crash) nor
// drift upward. The threshold is far above any real live-reference count
// (a string with 256M simultaneous references is not a real scenario) and
// at/below the sentinel, so a frozen string always reads as immortal.
#define HML_REFCOUNT_IMMORTAL   (1 << 28)   // 268435456
#define HML_REFCOUNT_IMMORTAL_MIN (1 << 27) // anything >= this is "frozen"

// String struct (UTF-8, with Small String Optimization)
// Small strings (<=23 bytes) are stored inline in inline_data to reduce allocations.
// The `data` pointer always points to valid string data (either inline_data or heap).
struct HmlString {
    char *data;              // Points to inline_data (SSO) or heap allocation
    int length;              // Byte length
    int char_length;         // Codepoint length (-1 if uncalculated)
    int capacity;            // For heap: allocated size; for SSO: HML_SSO_THRESHOLD+1
    _Atomic int ref_count;
    int is_sso;              // 1 if using inline storage, 0 if heap allocated
    char inline_data[HML_SSO_THRESHOLD + 1];  // Inline storage for small strings
};

// Buffer struct (safe pointer wrapper)
struct HmlBuffer {
    void *data;
    int length;
    int capacity;
    _Atomic int ref_count;
    _Atomic int freed;   // Atomic flag: 1 if freed via free(), 0 otherwise
    HmlBuffer *parent;   // Non-NULL for zero-copy slice views (keeps parent alive)
};

// Array struct (dynamic array)
struct HmlArray {
    HmlValue *elements;
    int length;
    int capacity;
    _Atomic int ref_count;
    HmlValueType element_type;  // HML_VAL_NULL for untyped
    _Atomic int freed;   // Atomic flag: 1 if freed via free(), 0 otherwise
};

// Field entry struct - unified storage for name and value (reduces fragmentation)
typedef struct HmlFieldEntry {
    char *name;             // Field name (owned)
    HmlValue value;         // Field value
} HmlFieldEntry;

// Object struct (JavaScript-style)
struct HmlObject {
    char *type_name;        // NULL for anonymous
    HmlFieldEntry *fields;  // Unified array of field entries (reduces fragmentation)
    int num_fields;
    int capacity;
    _Atomic int ref_count;
    _Atomic int freed;   // Atomic flag: 1 if freed via free(), 0 otherwise
    int is_pooled;          // 1 if allocated from object pool
    // Hash table for O(1) field lookup (lazy initialization)
    int *hash_table;        // Maps hash slots to field indices (-1 = empty)
    int hash_capacity;      // Size of hash table (0 = not initialized)
};

// Function struct (user-defined or closure)
struct HmlFunction {
    void *fn_ptr;           // C function pointer
    void *closure_env;      // Closure environment (NULL if not a closure)
    char *name;             // Function name for error reporting (NULL for anonymous)
    char **param_names;     // Parameter names for named argument support (NULL if not needed)
    int num_params;         // Total number of parameters
    int num_required;       // Number of required parameters (for arity checking)
    int is_async;
    int has_rest_param;     // Has rest parameter (...args) - accepts unlimited extra args
    _Atomic int ref_count;
};

// File handle
struct HmlFileHandle {
    void *fp;               // FILE*
    char *path;
    char *mode;
    int closed;
};

// Task synchronization block - all pthread objects in one allocation
// This reduces fragmentation by combining mutex, cond, and thread into single block
typedef struct HmlTaskSync {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_t thread;
} HmlTaskSync;

// Task (async)
struct HmlTask {
    int id;
    int state;              // HML_TASK_READY, HML_TASK_RUNNING, HML_TASK_COMPLETED
    HmlValue result;
    int joined;
    int detached;
    HmlTaskSync *sync;      // Single allocation for all sync primitives (reduces fragmentation)
    _Atomic int ref_count;
    // For storing function and args to call
    HmlValue function;
    HmlValue *args;
    int num_args;
    char *name;             // Optional debug name (from spawn_with)
    int has_exception;      // 1 if task threw an exception
    HmlValue exception_value; // Exception value for propagation on join()
};

// Channel synchronization block - all pthread objects in one allocation
// This reduces fragmentation by combining all sync primitives into single block
typedef struct HmlChannelSync {
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    pthread_cond_t rendezvous;
} HmlChannelSync;

// Channel (for async communication)
struct HmlChannel {
    HmlValue *buffer;
    int capacity;
    int head;
    int tail;
    int count;
    int closed;
    HmlChannelSync *sync;        // Single allocation for all sync primitives (reduces fragmentation)
    _Atomic int ref_count;
    // Unbuffered channel support (rendezvous)
    HmlValue unbuffered_value;   // Value being transferred in rendezvous (inline, no separate alloc)
    int sender_waiting;          // Flag: sender is blocked waiting for receiver
    int receiver_waiting;        // Flag: receiver is blocked waiting for sender
};

// Socket (TCP/UDP networking)
struct HmlSocket {
    int fd;                 // File descriptor
    char *address;          // Bound/connected address
    int port;               // Bound/connected port
    int domain;             // AF_INET, AF_INET6
    int type;               // SOCK_STREAM, SOCK_DGRAM
    int closed;             // 1 if closed
    int listening;          // 1 if in listening mode
    int nonblocking;        // 1 if in non-blocking mode
};

// Type definition for duck typing
typedef struct HmlTypeField {
    char *name;
    int type_kind;          // HML_VAL_* type or -1 for any
    int is_optional;
    HmlValue default_value;
} HmlTypeField;

typedef struct HmlTypeDef {
    char *name;
    HmlTypeField *fields;
    int num_fields;
} HmlTypeDef;

// ========== VALUE CONSTRUCTORS ==========

HmlValue hml_val_i8(int8_t val);
HmlValue hml_val_i16(int16_t val);
HmlValue hml_val_i32(int32_t val);
HmlValue hml_val_i64(int64_t val);
HmlValue hml_val_u8(uint8_t val);
HmlValue hml_val_u16(uint16_t val);
HmlValue hml_val_u32(uint32_t val);
HmlValue hml_val_u64(uint64_t val);
HmlValue hml_val_f32(float val);
HmlValue hml_val_f64(double val);
HmlValue hml_val_bool(int val);
HmlValue hml_val_string(const char *str);
HmlValue hml_val_string_owned(char *str, int length, int capacity);
HmlValue hml_val_rune(uint32_t codepoint);
HmlValue hml_val_ptr(void *ptr);
HmlValue hml_val_buffer(int size);
HmlValue hml_val_array(void);
HmlValue hml_val_object(void);
HmlValue hml_val_null(void);
HmlValue hml_val_function(void *fn_ptr, int num_params, int num_required, int is_async);
HmlValue hml_val_function_rest(void *fn_ptr, int num_params, int num_required, int is_async, int has_rest_param);
HmlValue hml_val_function_with_env(void *fn_ptr, void *env, int num_params, int num_required, int is_async);
HmlValue hml_val_function_with_env_rest(void *fn_ptr, void *env, int num_params, int num_required, int is_async, int has_rest_param);

// Named function constructors (for better error reporting)
HmlValue hml_val_function_named(void *fn_ptr, int num_params, int num_required, int is_async, const char *name);
HmlValue hml_val_function_rest_named(void *fn_ptr, int num_params, int num_required, int is_async, int has_rest_param, const char *name);
HmlValue hml_val_function_with_env_named(void *fn_ptr, void *env, int num_params, int num_required, int is_async, const char *name);
HmlValue hml_val_function_with_env_rest_named(void *fn_ptr, void *env, int num_params, int num_required, int is_async, int has_rest_param, const char *name);

// Set function name (for late binding when name is known after creation)
void hml_function_set_name(HmlValue fn, const char *name);

// Set parameter names (for named argument support)
void hml_function_set_param_names(HmlValue fn, const char **param_names, int num_params);

// Function with parameter names (for named argument support)
HmlValue hml_val_function_with_params(void *fn_ptr, int num_params, int num_required, int is_async, int has_rest_param, const char *name, const char **param_names);

HmlValue hml_val_builtin_fn(HmlBuiltinFn fn);
HmlValue hml_val_socket(HmlSocket *sock);

// ========== REFERENCE COUNTING ==========

void hml_retain(HmlValue *val);
void hml_release(HmlValue *val);

// ========== STATIC VARIABLE CLEANUP (for compiled binaries) ==========

// Break circular references in a set of static global variables.
// Must be called before hml_release_statics() to prevent use-after-free
// in self-referential closures (e.g., objects containing closures that capture self).
void hml_break_cycles(HmlValue *statics[], int count);

// Release all static global variables, setting them to null.
void hml_release_statics(HmlValue *statics[], int count);

// ========== VALUE DEEP COPY (for thread isolation) ==========

// Deep copy a value for passing to spawned tasks.
// This ensures tasks don't share mutable state with the parent thread.
// - Primitives: returned as-is (immutable)
// - Strings, buffers, arrays, objects: deep copied
// - Raw pointers (ptr): causes runtime error (use buffer or channel instead)
// - Files, sockets, functions, tasks, channels: shared by reference (OS/coordination resources)
HmlValue hml_value_deep_copy(HmlValue val);

// ========== TYPE CHECKING ==========

int hml_is_null(HmlValue val);
int hml_is_i32(HmlValue val);
int hml_is_i64(HmlValue val);
int hml_is_f64(HmlValue val);
int hml_is_bool(HmlValue val);
int hml_is_string(HmlValue val);
int hml_is_array(HmlValue val);
int hml_is_object(HmlValue val);
int hml_is_function(HmlValue val);
int hml_is_numeric(HmlValue val);
int hml_is_integer(HmlValue val);

// ========== TYPE CONVERSION ==========

int hml_to_bool(HmlValue val);
int32_t hml_to_i32(HmlValue val);
int64_t hml_to_i64(HmlValue val);
double hml_to_f64(HmlValue val);
const char* hml_to_string_ptr(HmlValue val);  // Returns pointer to string data
HmlValue hml_to_string(HmlValue val);         // Converts any value to string

// ========== TYPE NAME ==========

const char* hml_type_name(HmlValueType type);
const char* hml_typeof_str(HmlValue val);

// ========== FAST PATH OPTIMIZATIONS ==========
// These inline functions provide optimized paths for common operations,
// matching the interpreter's fast paths for better performance.

// Fast path: Check if both values are i32 (most common case in benchmarks)
static inline int hml_both_i32(HmlValue left, HmlValue right) {
    return left.type == HML_VAL_I32 && right.type == HML_VAL_I32;
}

// Fast path: i32 addition with overflow detection
static inline HmlValue hml_i32_add(HmlValue left, HmlValue right) {
    int32_t result;
    if (__builtin_add_overflow(left.as.as_i32, right.as.as_i32, &result)) {
        extern __attribute__((noreturn)) void hml_runtime_error(const char *fmt, ...);
        hml_runtime_error("Integer overflow: i32 addition");
    }
    return (HmlValue){ .type = HML_VAL_I32, .as.as_i32 = result };
}

// Fast path: i32 subtraction with overflow detection
static inline HmlValue hml_i32_sub(HmlValue left, HmlValue right) {
    int32_t result;
    if (__builtin_sub_overflow(left.as.as_i32, right.as.as_i32, &result)) {
        extern __attribute__((noreturn)) void hml_runtime_error(const char *fmt, ...);
        hml_runtime_error("Integer overflow: i32 subtraction");
    }
    return (HmlValue){ .type = HML_VAL_I32, .as.as_i32 = result };
}

// Fast path: i32 multiplication with overflow detection
static inline HmlValue hml_i32_mul(HmlValue left, HmlValue right) {
    int32_t result;
    if (__builtin_mul_overflow(left.as.as_i32, right.as.as_i32, &result)) {
        extern __attribute__((noreturn)) void hml_runtime_error(const char *fmt, ...);
        hml_runtime_error("Integer overflow: i32 multiplication");
    }
    return (HmlValue){ .type = HML_VAL_I32, .as.as_i32 = result };
}

// Fast path: i32 division (with zero check)
static inline HmlValue hml_i32_div(HmlValue left, HmlValue right) {
    if (right.as.as_i32 == 0) {
        extern __attribute__((noreturn)) void hml_runtime_error_line(const char *fmt, ...);
        hml_runtime_error_line("Division by zero");
    }
    return (HmlValue){ .type = HML_VAL_I32, .as.as_i32 = left.as.as_i32 / right.as.as_i32 };
}

// Fast path: i32 modulo (with zero check)
static inline HmlValue hml_i32_mod(HmlValue left, HmlValue right) {
    if (right.as.as_i32 == 0) {
        extern __attribute__((noreturn)) void hml_runtime_error_line(const char *fmt, ...);
        hml_runtime_error_line("Division by zero");
    }
    return (HmlValue){ .type = HML_VAL_I32, .as.as_i32 = left.as.as_i32 % right.as.as_i32 };
}

// Fast path: i32 comparisons (return bool)
static inline HmlValue hml_i32_lt(HmlValue left, HmlValue right) {
    return (HmlValue){ .type = HML_VAL_BOOL, .as.as_bool = left.as.as_i32 < right.as.as_i32 };
}

static inline HmlValue hml_i32_le(HmlValue left, HmlValue right) {
    return (HmlValue){ .type = HML_VAL_BOOL, .as.as_bool = left.as.as_i32 <= right.as.as_i32 };
}

static inline HmlValue hml_i32_gt(HmlValue left, HmlValue right) {
    return (HmlValue){ .type = HML_VAL_BOOL, .as.as_bool = left.as.as_i32 > right.as.as_i32 };
}

static inline HmlValue hml_i32_ge(HmlValue left, HmlValue right) {
    return (HmlValue){ .type = HML_VAL_BOOL, .as.as_bool = left.as.as_i32 >= right.as.as_i32 };
}

static inline HmlValue hml_i32_eq(HmlValue left, HmlValue right) {
    return (HmlValue){ .type = HML_VAL_BOOL, .as.as_bool = left.as.as_i32 == right.as.as_i32 };
}

static inline HmlValue hml_i32_ne(HmlValue left, HmlValue right) {
    return (HmlValue){ .type = HML_VAL_BOOL, .as.as_bool = left.as.as_i32 != right.as.as_i32 };
}

// Fast path: i32 bitwise operations
static inline HmlValue hml_i32_bit_and(HmlValue left, HmlValue right) {
    return (HmlValue){ .type = HML_VAL_I32, .as.as_i32 = left.as.as_i32 & right.as.as_i32 };
}

static inline HmlValue hml_i32_bit_or(HmlValue left, HmlValue right) {
    return (HmlValue){ .type = HML_VAL_I32, .as.as_i32 = left.as.as_i32 | right.as.as_i32 };
}

static inline HmlValue hml_i32_bit_xor(HmlValue left, HmlValue right) {
    return (HmlValue){ .type = HML_VAL_I32, .as.as_i32 = left.as.as_i32 ^ right.as.as_i32 };
}

static inline HmlValue hml_i32_lshift(HmlValue left, HmlValue right) {
    int32_t r = right.as.as_i32;
    if (__builtin_expect(r < 0, 0)) {
        extern __attribute__((noreturn)) void hml_runtime_error_line(const char *fmt, ...);
        hml_runtime_error_line("Shift amount must be non-negative");
    }
    if (__builtin_expect(r >= 32, 0)) return (HmlValue){ .type = HML_VAL_I32, .as.as_i32 = 0 };
    return (HmlValue){ .type = HML_VAL_I32, .as.as_i32 = (int32_t)((uint32_t)left.as.as_i32 << r) };
}

static inline HmlValue hml_i32_rshift(HmlValue left, HmlValue right) {
    int32_t r = right.as.as_i32;
    if (__builtin_expect(r < 0, 0)) {
        extern __attribute__((noreturn)) void hml_runtime_error_line(const char *fmt, ...);
        hml_runtime_error_line("Shift amount must be non-negative");
    }
    if (__builtin_expect(r >= 32, 0)) return (HmlValue){ .type = HML_VAL_I32, .as.as_i32 = left.as.as_i32 < 0 ? -1 : 0 };
    return (HmlValue){ .type = HML_VAL_I32, .as.as_i32 = left.as.as_i32 >> r };
}

// ========== i64 FAST PATH OPERATIONS ==========
// These match the interpreter's i64 fast path for 64-bit integer operations

// Fast path: Check if both values are i64
static inline int hml_both_i64(HmlValue left, HmlValue right) {
    return left.type == HML_VAL_I64 && right.type == HML_VAL_I64;
}

// Fast path: i64 arithmetic with overflow detection
static inline HmlValue hml_i64_add(HmlValue left, HmlValue right) {
    int64_t result;
    if (__builtin_add_overflow(left.as.as_i64, right.as.as_i64, &result)) {
        extern __attribute__((noreturn)) void hml_runtime_error(const char *fmt, ...);
        hml_runtime_error("Integer overflow: i64 addition");
    }
    return (HmlValue){ .type = HML_VAL_I64, .as.as_i64 = result };
}

static inline HmlValue hml_i64_sub(HmlValue left, HmlValue right) {
    int64_t result;
    if (__builtin_sub_overflow(left.as.as_i64, right.as.as_i64, &result)) {
        extern __attribute__((noreturn)) void hml_runtime_error(const char *fmt, ...);
        hml_runtime_error("Integer overflow: i64 subtraction");
    }
    return (HmlValue){ .type = HML_VAL_I64, .as.as_i64 = result };
}

static inline HmlValue hml_i64_mul(HmlValue left, HmlValue right) {
    int64_t result;
    if (__builtin_mul_overflow(left.as.as_i64, right.as.as_i64, &result)) {
        extern __attribute__((noreturn)) void hml_runtime_error(const char *fmt, ...);
        hml_runtime_error("Integer overflow: i64 multiplication");
    }
    return (HmlValue){ .type = HML_VAL_I64, .as.as_i64 = result };
}

static inline HmlValue hml_i64_div(HmlValue left, HmlValue right) {
    if (right.as.as_i64 == 0) {
        extern __attribute__((noreturn)) void hml_runtime_error_line(const char *fmt, ...);
        hml_runtime_error_line("Division by zero");
    }
    return (HmlValue){ .type = HML_VAL_I64, .as.as_i64 = left.as.as_i64 / right.as.as_i64 };
}

static inline HmlValue hml_i64_mod(HmlValue left, HmlValue right) {
    if (right.as.as_i64 == 0) {
        extern __attribute__((noreturn)) void hml_runtime_error_line(const char *fmt, ...);
        hml_runtime_error_line("Division by zero");
    }
    return (HmlValue){ .type = HML_VAL_I64, .as.as_i64 = left.as.as_i64 % right.as.as_i64 };
}

// Fast path: i64 comparisons
static inline HmlValue hml_i64_lt(HmlValue left, HmlValue right) {
    return (HmlValue){ .type = HML_VAL_BOOL, .as.as_bool = left.as.as_i64 < right.as.as_i64 };
}

static inline HmlValue hml_i64_le(HmlValue left, HmlValue right) {
    return (HmlValue){ .type = HML_VAL_BOOL, .as.as_bool = left.as.as_i64 <= right.as.as_i64 };
}

static inline HmlValue hml_i64_gt(HmlValue left, HmlValue right) {
    return (HmlValue){ .type = HML_VAL_BOOL, .as.as_bool = left.as.as_i64 > right.as.as_i64 };
}

static inline HmlValue hml_i64_ge(HmlValue left, HmlValue right) {
    return (HmlValue){ .type = HML_VAL_BOOL, .as.as_bool = left.as.as_i64 >= right.as.as_i64 };
}

static inline HmlValue hml_i64_eq(HmlValue left, HmlValue right) {
    return (HmlValue){ .type = HML_VAL_BOOL, .as.as_bool = left.as.as_i64 == right.as.as_i64 };
}

static inline HmlValue hml_i64_ne(HmlValue left, HmlValue right) {
    return (HmlValue){ .type = HML_VAL_BOOL, .as.as_bool = left.as.as_i64 != right.as.as_i64 };
}

// Fast path: i64 bitwise operations
static inline HmlValue hml_i64_bit_and(HmlValue left, HmlValue right) {
    return (HmlValue){ .type = HML_VAL_I64, .as.as_i64 = left.as.as_i64 & right.as.as_i64 };
}

static inline HmlValue hml_i64_bit_or(HmlValue left, HmlValue right) {
    return (HmlValue){ .type = HML_VAL_I64, .as.as_i64 = left.as.as_i64 | right.as.as_i64 };
}

static inline HmlValue hml_i64_bit_xor(HmlValue left, HmlValue right) {
    return (HmlValue){ .type = HML_VAL_I64, .as.as_i64 = left.as.as_i64 ^ right.as.as_i64 };
}

static inline HmlValue hml_i64_lshift(HmlValue left, HmlValue right) {
    int64_t r = right.as.as_i64;
    if (__builtin_expect(r < 0, 0)) {
        extern __attribute__((noreturn)) void hml_runtime_error_line(const char *fmt, ...);
        hml_runtime_error_line("Shift amount must be non-negative");
    }
    if (__builtin_expect(r >= 64, 0)) return (HmlValue){ .type = HML_VAL_I64, .as.as_i64 = 0 };
    return (HmlValue){ .type = HML_VAL_I64, .as.as_i64 = (int64_t)((uint64_t)left.as.as_i64 << r) };
}

static inline HmlValue hml_i64_rshift(HmlValue left, HmlValue right) {
    int64_t r = right.as.as_i64;
    if (__builtin_expect(r < 0, 0)) {
        extern __attribute__((noreturn)) void hml_runtime_error_line(const char *fmt, ...);
        hml_runtime_error_line("Shift amount must be non-negative");
    }
    if (__builtin_expect(r >= 64, 0)) return (HmlValue){ .type = HML_VAL_I64, .as.as_i64 = left.as.as_i64 < 0 ? -1 : 0 };
    return (HmlValue){ .type = HML_VAL_I64, .as.as_i64 = left.as.as_i64 >> r };
}

// Fast path: Check if value needs refcounting (primitives don't)
// Defined early so it can be used by array_get_i32_fast
static inline int hml_needs_refcount(HmlValue val) {
    return val.type == HML_VAL_STRING || val.type == HML_VAL_BUFFER ||
           val.type == HML_VAL_ARRAY || val.type == HML_VAL_OBJECT ||
           val.type == HML_VAL_FUNCTION || val.type == HML_VAL_TASK ||
           val.type == HML_VAL_CHANNEL;
}

// Fast path: array[i32] access (bounds checked, skip retain for primitives)
static inline HmlValue hml_array_get_i32_fast(HmlArray *arr, int32_t index) {
    if (index >= 0 && index < arr->length) {
        HmlValue result = arr->elements[index];
        // OPTIMIZATION: Skip retain for primitive types (i32, i64, f64, bool, etc.)
        if (hml_needs_refcount(result)) {
            hml_retain(&result);
        }
        return result;
    }
    // Fall through to bounds error
    extern __attribute__((noreturn)) void hml_runtime_error_loc(const char *fmt, ...);
    hml_runtime_error_loc("Array index %d out of bounds (length %d)", index, arr->length);
}

// Fast path: Conditional retain (skip for primitives)
static inline void hml_retain_if_needed(HmlValue *val) {
    if (hml_needs_refcount(*val)) {
        hml_retain(val);
    }
}

// Fast path: Conditional release (skip for primitives)
static inline void hml_release_if_needed(HmlValue *val) {
    if (hml_needs_refcount(*val)) {
        hml_release(val);
    }
}

// Fast path: array[i32] = value (bounds checked, handles refcounting)
static inline void hml_array_set_i32_fast(HmlArray *arr, int32_t index, HmlValue val) {
    extern __attribute__((noreturn)) void hml_runtime_error(const char *fmt, ...);

    if (index >= 0 && index < arr->length) {
        // Keep the optimized i32-index path behaviorally identical to
        // hml_array_set(): typed arrays must reject mismatched values before
        // mutating or changing reference counts.
        if (arr->element_type != HML_VAL_NULL && val.type != arr->element_type) {
            hml_runtime_error("Type mismatch in typed array - expected element of specific type");
        }

        HmlValue old = arr->elements[index];
        // Retain new value FIRST to prevent use-after-free when old == new
        if (hml_needs_refcount(val)) {
            hml_retain(&val);
        }
        // Then release old value
        if (hml_needs_refcount(old)) {
            hml_release(&old);
        }
        arr->elements[index] = val;
        return;
    }
    // Out-of-range write: delegate to the slow path, which grows the array
    // (filling with nulls) for index >= length and errors on negative index,
    // matching the interpreter.
    extern void hml_array_set(HmlValue arr, HmlValue index, HmlValue val);
    hml_array_set((HmlValue){ .type = HML_VAL_ARRAY, .as.as_array = arr },
                  (HmlValue){ .type = HML_VAL_I32, .as.as_i32 = index }, val);
}

// Fast path: Increment i32 variable in-place
// Returns the new value (wraps at INT32_MAX; computed unsigned to avoid UB)
static inline HmlValue hml_i32_inc(HmlValue val) {
    return (HmlValue){ .type = HML_VAL_I32, .as.as_i32 = (int32_t)((uint32_t)val.as.as_i32 + 1u) };
}

// Fast path: Decrement i32 variable in-place
// Returns the new value (wraps at INT32_MIN; computed unsigned to avoid UB)
static inline HmlValue hml_i32_dec(HmlValue val) {
    return (HmlValue){ .type = HML_VAL_I32, .as.as_i32 = (int32_t)((uint32_t)val.as.as_i32 - 1u) };
}

#endif // HEMLOCK_VALUE_H
