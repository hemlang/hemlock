/*
 * Hemlock Runtime Library - Value Types (WASM-compatible)
 *
 * This header defines the core Value type used by compiled Hemlock programs.
 * Modified for WebAssembly compatibility (no atomics, no threads).
 */

#ifndef HEMLOCK_VALUE_H
#define HEMLOCK_VALUE_H

#include <stdint.h>
#include <stddef.h>

// WASM: Replace _Atomic with regular int (single-threaded)
#define _Atomic

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

// String struct (heap-allocated, UTF-8)
struct HmlString {
    char *data;
    int length;
    int char_length;
    int capacity;
    int ref_count;
};

// Buffer struct (safe pointer wrapper)
struct HmlBuffer {
    void *data;
    int length;
    int capacity;
    int ref_count;
    _Atomic int freed;
};

// Array struct (dynamic array)
struct HmlArray {
    HmlValue *elements;
    int length;
    int capacity;
    int ref_count;
    HmlValueType element_type;
    _Atomic int freed;
};

// Object struct (JavaScript-style)
struct HmlObject {
    char *type_name;
    char **field_names;
    HmlValue *field_values;
    int num_fields;
    int capacity;
    int ref_count;
    _Atomic int freed;
};

// Function struct (user-defined or closure)
struct HmlFunction {
    void *fn_ptr;
    void *closure_env;
    int num_params;
    int num_required;
    int is_async;
    int has_rest_param;
    int ref_count;
};

// File handle
struct HmlFileHandle {
    void *fp;
    char *path;
    char *mode;
    int closed;
};

// Task (async) - stub for WASM
struct HmlTask {
    int id;
    int state;
    HmlValue result;
    int joined;
    int detached;
    void *thread;
    void *mutex;
    void *cond;
    int ref_count;
    HmlValue function;
    HmlValue *args;
    int num_args;
};

// Channel (for async communication) - stub for WASM
struct HmlChannel {
    HmlValue *buffer;
    int capacity;
    int head;
    int tail;
    int count;
    int closed;
    void *mutex;
    void *not_empty;
    void *not_full;
    int ref_count;
    HmlValue *unbuffered_value;
    int sender_waiting;
    int receiver_waiting;
    void *rendezvous;
};

// Socket - stub for WASM
struct HmlSocket {
    int fd;
    char *address;
    int port;
    int domain;
    int type;
    int closed;
    int listening;
    int nonblocking;
};

// Type definition for duck typing
typedef struct HmlTypeField {
    char *name;
    int type_kind;
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
HmlValue hml_val_builtin_fn(HmlBuiltinFn fn);

// ========== REFERENCE COUNTING ==========

void hml_retain(HmlValue *val);
void hml_release(HmlValue *val);

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
const char* hml_to_string_ptr(HmlValue val);

// ========== TYPE NAME ==========

const char* hml_type_name(HmlValueType type);
const char* hml_typeof_str(HmlValue val);

// ========== FAST PATH OPTIMIZATIONS ==========

static inline int hml_both_i32(HmlValue left, HmlValue right) {
    return left.type == HML_VAL_I32 && right.type == HML_VAL_I32;
}

static inline HmlValue hml_i32_add(HmlValue left, HmlValue right) {
    return (HmlValue){ .type = HML_VAL_I32, .as.as_i32 = left.as.as_i32 + right.as.as_i32 };
}

static inline HmlValue hml_i32_sub(HmlValue left, HmlValue right) {
    return (HmlValue){ .type = HML_VAL_I32, .as.as_i32 = left.as.as_i32 - right.as.as_i32 };
}

static inline HmlValue hml_i32_mul(HmlValue left, HmlValue right) {
    return (HmlValue){ .type = HML_VAL_I32, .as.as_i32 = left.as.as_i32 * right.as.as_i32 };
}

static inline HmlValue hml_i32_div(HmlValue left, HmlValue right) {
    if (right.as.as_i32 == 0) {
        extern __attribute__((noreturn)) void hml_runtime_error(const char *fmt, ...);
        hml_runtime_error("Division by zero");
    }
    return (HmlValue){ .type = HML_VAL_I32, .as.as_i32 = left.as.as_i32 / right.as.as_i32 };
}

static inline HmlValue hml_i32_mod(HmlValue left, HmlValue right) {
    if (right.as.as_i32 == 0) {
        extern __attribute__((noreturn)) void hml_runtime_error(const char *fmt, ...);
        hml_runtime_error("Division by zero");
    }
    return (HmlValue){ .type = HML_VAL_I32, .as.as_i32 = left.as.as_i32 % right.as.as_i32 };
}

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

// Fast path: Check if both values are i64
static inline int hml_both_i64(HmlValue left, HmlValue right) {
    return left.type == HML_VAL_I64 && right.type == HML_VAL_I64;
}

static inline HmlValue hml_i64_add(HmlValue left, HmlValue right) {
    return (HmlValue){ .type = HML_VAL_I64, .as.as_i64 = left.as.as_i64 + right.as.as_i64 };
}

static inline HmlValue hml_i64_sub(HmlValue left, HmlValue right) {
    return (HmlValue){ .type = HML_VAL_I64, .as.as_i64 = left.as.as_i64 - right.as.as_i64 };
}

static inline HmlValue hml_i64_mul(HmlValue left, HmlValue right) {
    return (HmlValue){ .type = HML_VAL_I64, .as.as_i64 = left.as.as_i64 * right.as.as_i64 };
}

static inline HmlValue hml_i64_div(HmlValue left, HmlValue right) {
    if (right.as.as_i64 == 0) {
        extern __attribute__((noreturn)) void hml_runtime_error(const char *fmt, ...);
        hml_runtime_error("Division by zero");
    }
    return (HmlValue){ .type = HML_VAL_I64, .as.as_i64 = left.as.as_i64 / right.as.as_i64 };
}

static inline HmlValue hml_i64_mod(HmlValue left, HmlValue right) {
    if (right.as.as_i64 == 0) {
        extern __attribute__((noreturn)) void hml_runtime_error(const char *fmt, ...);
        hml_runtime_error("Division by zero");
    }
    return (HmlValue){ .type = HML_VAL_I64, .as.as_i64 = left.as.as_i64 % right.as.as_i64 };
}

// Fast path: Check if value needs refcounting
static inline int hml_needs_refcount(HmlValue val) {
    return val.type == HML_VAL_STRING || val.type == HML_VAL_BUFFER ||
           val.type == HML_VAL_ARRAY || val.type == HML_VAL_OBJECT ||
           val.type == HML_VAL_FUNCTION || val.type == HML_VAL_TASK ||
           val.type == HML_VAL_CHANNEL;
}

// Fast path: Conditional retain
static inline void hml_retain_if_needed(HmlValue *val) {
    if (hml_needs_refcount(*val)) {
        hml_retain(val);
    }
}

// Fast path: Conditional release
static inline void hml_release_if_needed(HmlValue *val) {
    if (hml_needs_refcount(*val)) {
        hml_release(val);
    }
}

#endif // HEMLOCK_VALUE_H
