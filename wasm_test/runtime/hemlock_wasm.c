/*
 * Hemlock Runtime Library - WASM-compatible Subset
 *
 * A minimal runtime for WebAssembly that excludes:
 * - FFI (dlopen, libffi)
 * - pthreads (async/spawn/channels)
 * - signals
 * - fork/exec
 * - networking
 * - crypto (OpenSSL)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <setjmp.h>

// Include the value header
#include "hemlock_value.h"

// Forward declarations for functions used before definition
void hml_array_push(HmlValue arr, HmlValue val);

// WASM: Stub for atomic_store (single-threaded, not needed)
#define atomic_store(ptr, val) (*(ptr) = (val))

// Forward declarations
typedef struct HmlClosureEnv HmlClosureEnv;

// ========== CALL STACK TRACKING ==========
#ifndef HML_MAX_CALL_DEPTH
#define HML_MAX_CALL_DEPTH 10000
#endif

__thread int hml_g_call_depth = 0;
__thread int hml_g_max_call_depth = HML_MAX_CALL_DEPTH;

#define HML_CALL_ENTER() do { \
    if (__builtin_expect(++hml_g_call_depth > hml_g_max_call_depth, 0)) { \
        hml_g_call_depth = 0; \
        hml_runtime_error("Maximum call stack depth exceeded (infinite recursion?)"); \
    } \
} while(0)

#define HML_CALL_EXIT() do { \
    hml_g_call_depth--; \
} while(0)

// ========== EXCEPTION HANDLING ==========

typedef struct HmlExceptionContext {
    jmp_buf exception_buf;
    HmlValue exception_value;
    int is_active;
    struct HmlExceptionContext *prev;
} HmlExceptionContext;

static HmlExceptionContext *g_exception_stack = NULL;

HmlExceptionContext* hml_exception_push(void) {
    HmlExceptionContext *ctx = malloc(sizeof(HmlExceptionContext));
    ctx->is_active = 1;
    ctx->exception_value = hml_val_null();
    ctx->prev = g_exception_stack;
    g_exception_stack = ctx;
    return ctx;
}

void hml_exception_pop(void) {
    if (g_exception_stack) {
        HmlExceptionContext *ctx = g_exception_stack;
        g_exception_stack = ctx->prev;
        hml_release(&ctx->exception_value);
        free(ctx);
    }
}

__attribute__((noreturn)) void hml_throw(HmlValue exception_value) {
    if (g_exception_stack && g_exception_stack->is_active) {
        g_exception_stack->exception_value = exception_value;
        hml_retain(&g_exception_stack->exception_value);
        longjmp(g_exception_stack->exception_buf, 1);
    }
    fprintf(stderr, "Uncaught exception\n");
    exit(1);
}

HmlValue hml_exception_get_value(void) {
    if (g_exception_stack) {
        return g_exception_stack->exception_value;
    }
    return hml_val_null();
}

__attribute__((noreturn)) void hml_runtime_error(const char *format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    HmlValue err = hml_val_string(buffer);
    hml_throw(err);
}

// ========== GLOBAL STATE ==========

static int g_argc = 0;
static char **g_argv = NULL;

// Defer stack
typedef void (*HmlDeferFn)(void *arg);

typedef struct DeferEntry {
    HmlDeferFn fn;
    void *arg;
    struct DeferEntry *next;
} DeferEntry;

static DeferEntry *g_defer_stack = NULL;

// ========== RUNTIME INITIALIZATION ==========

void hml_runtime_init(int argc, char **argv) {
    g_argc = argc;
    g_argv = argv;
    g_exception_stack = NULL;
    g_defer_stack = NULL;
}

void hml_runtime_cleanup(void) {
    // Execute remaining defers
    while (g_defer_stack) {
        DeferEntry *entry = g_defer_stack;
        g_defer_stack = entry->next;
        if (entry->fn) {
            entry->fn(entry->arg);
        }
        free(entry);
    }

    // Clear exception stack
    while (g_exception_stack) {
        hml_exception_pop();
    }
}

HmlValue hml_get_args(void) {
    HmlValue arr = hml_val_array();
    for (int i = 0; i < g_argc; i++) {
        HmlValue str = hml_val_string(g_argv[i]);
        hml_array_push(arr, str);
    }
    return arr;
}

// ========== VALUE CONSTRUCTORS ==========

HmlValue hml_val_i8(int8_t val) {
    HmlValue v;
    v.type = HML_VAL_I8;
    v.as.as_i8 = val;
    return v;
}

HmlValue hml_val_i16(int16_t val) {
    HmlValue v;
    v.type = HML_VAL_I16;
    v.as.as_i16 = val;
    return v;
}

HmlValue hml_val_i32(int32_t val) {
    HmlValue v;
    v.type = HML_VAL_I32;
    v.as.as_i32 = val;
    return v;
}

HmlValue hml_val_i64(int64_t val) {
    HmlValue v;
    v.type = HML_VAL_I64;
    v.as.as_i64 = val;
    return v;
}

HmlValue hml_val_u8(uint8_t val) {
    HmlValue v;
    v.type = HML_VAL_U8;
    v.as.as_u8 = val;
    return v;
}

HmlValue hml_val_u16(uint16_t val) {
    HmlValue v;
    v.type = HML_VAL_U16;
    v.as.as_u16 = val;
    return v;
}

HmlValue hml_val_u32(uint32_t val) {
    HmlValue v;
    v.type = HML_VAL_U32;
    v.as.as_u32 = val;
    return v;
}

HmlValue hml_val_u64(uint64_t val) {
    HmlValue v;
    v.type = HML_VAL_U64;
    v.as.as_u64 = val;
    return v;
}

HmlValue hml_val_f32(float val) {
    HmlValue v;
    v.type = HML_VAL_F32;
    v.as.as_f32 = val;
    return v;
}

HmlValue hml_val_f64(double val) {
    HmlValue v;
    v.type = HML_VAL_F64;
    v.as.as_f64 = val;
    return v;
}

HmlValue hml_val_bool(int val) {
    HmlValue v;
    v.type = HML_VAL_BOOL;
    v.as.as_bool = val ? 1 : 0;
    return v;
}

HmlValue hml_val_string(const char *str) {
    HmlValue v;
    v.type = HML_VAL_STRING;

    int len = (str != NULL) ? strlen(str) : 0;
    int capacity = len + 1;

    HmlString *s = malloc(sizeof(HmlString));
    s->data = malloc(capacity);
    if (str != NULL) {
        memcpy(s->data, str, len);
    }
    s->data[len] = '\0';
    s->length = len;
    s->char_length = -1;
    s->capacity = capacity;
    s->ref_count = 1;

    v.as.as_string = s;
    return v;
}

HmlValue hml_val_string_owned(char *str, int length, int capacity) {
    HmlValue v;
    v.type = HML_VAL_STRING;

    HmlString *s = malloc(sizeof(HmlString));
    s->data = str;
    s->length = length;
    s->char_length = -1;
    s->capacity = capacity;
    s->ref_count = 1;

    v.as.as_string = s;
    return v;
}

HmlValue hml_val_rune(uint32_t codepoint) {
    HmlValue v;
    v.type = HML_VAL_RUNE;
    v.as.as_rune = codepoint;
    return v;
}

HmlValue hml_val_ptr(void *ptr) {
    HmlValue v;
    v.type = HML_VAL_PTR;
    v.as.as_ptr = ptr;
    return v;
}

HmlValue hml_val_buffer(int size) {
    HmlBuffer *b = malloc(sizeof(HmlBuffer));
    if (!b) {
        return hml_val_null();
    }
    b->data = calloc(size, 1);
    if (!b->data) {
        free(b);
        return hml_val_null();
    }
    b->length = size;
    b->capacity = size;
    b->ref_count = 1;
    atomic_store(&b->freed, 0);

    HmlValue v;
    v.type = HML_VAL_BUFFER;
    v.as.as_buffer = b;
    return v;
}

HmlValue hml_val_array(void) {
    HmlValue v;
    v.type = HML_VAL_ARRAY;

    HmlArray *a = malloc(sizeof(HmlArray));
    a->elements = NULL;
    a->length = 0;
    a->capacity = 0;
    a->ref_count = 1;
    a->element_type = HML_VAL_NULL;
    atomic_store(&a->freed, 0);

    v.as.as_array = a;
    return v;
}

HmlValue hml_val_object(void) {
    HmlValue v;
    v.type = HML_VAL_OBJECT;

    HmlObject *o = malloc(sizeof(HmlObject));
    o->type_name = NULL;
    o->field_names = NULL;
    o->field_values = NULL;
    o->num_fields = 0;
    o->capacity = 0;
    o->ref_count = 1;
    atomic_store(&o->freed, 0);

    v.as.as_object = o;
    return v;
}

HmlValue hml_val_null(void) {
    HmlValue v;
    v.type = HML_VAL_NULL;
    v.as.as_ptr = NULL;
    return v;
}

HmlValue hml_val_function(void *fn_ptr, int num_params, int num_required, int is_async) {
    HmlValue v;
    v.type = HML_VAL_FUNCTION;

    HmlFunction *f = malloc(sizeof(HmlFunction));
    f->fn_ptr = fn_ptr;
    f->closure_env = NULL;
    f->num_params = num_params;
    f->num_required = num_required;
    f->is_async = is_async;
    f->has_rest_param = 0;
    f->ref_count = 1;

    v.as.as_function = f;
    return v;
}

HmlValue hml_val_function_rest(void *fn_ptr, int num_params, int num_required, int is_async, int has_rest_param) {
    HmlValue v;
    v.type = HML_VAL_FUNCTION;

    HmlFunction *f = malloc(sizeof(HmlFunction));
    f->fn_ptr = fn_ptr;
    f->closure_env = NULL;
    f->num_params = num_params;
    f->num_required = num_required;
    f->is_async = is_async;
    f->has_rest_param = has_rest_param;
    f->ref_count = 1;

    v.as.as_function = f;
    return v;
}

HmlValue hml_val_function_with_env(void *fn_ptr, void *env, int num_params, int num_required, int is_async) {
    HmlValue v;
    v.type = HML_VAL_FUNCTION;

    HmlFunction *f = malloc(sizeof(HmlFunction));
    f->fn_ptr = fn_ptr;
    f->closure_env = env;
    f->num_params = num_params;
    f->num_required = num_required;
    f->is_async = is_async;
    f->has_rest_param = 0;
    f->ref_count = 1;

    v.as.as_function = f;
    return v;
}

HmlValue hml_val_function_with_env_rest(void *fn_ptr, void *env, int num_params, int num_required, int is_async, int has_rest_param) {
    HmlValue v;
    v.type = HML_VAL_FUNCTION;

    HmlFunction *f = malloc(sizeof(HmlFunction));
    f->fn_ptr = fn_ptr;
    f->closure_env = env;
    f->num_params = num_params;
    f->num_required = num_required;
    f->is_async = is_async;
    f->has_rest_param = has_rest_param;
    f->ref_count = 1;

    v.as.as_function = f;
    return v;
}

HmlValue hml_val_builtin_fn(HmlBuiltinFn fn) {
    HmlValue v;
    v.type = HML_VAL_BUILTIN_FN;
    v.as.as_builtin_fn = fn;
    return v;
}

// ========== REFERENCE COUNTING ==========

void hml_retain(HmlValue *val) {
    if (val == NULL) return;

    switch (val->type) {
        case HML_VAL_STRING:
            if (val->as.as_string) val->as.as_string->ref_count++;
            break;
        case HML_VAL_BUFFER:
            if (val->as.as_buffer) val->as.as_buffer->ref_count++;
            break;
        case HML_VAL_ARRAY:
            if (val->as.as_array) val->as.as_array->ref_count++;
            break;
        case HML_VAL_OBJECT:
            if (val->as.as_object) val->as.as_object->ref_count++;
            break;
        case HML_VAL_FUNCTION:
            if (val->as.as_function) val->as.as_function->ref_count++;
            break;
        default:
            break;
    }
}

static void string_free(HmlString *str) {
    if (str) {
        free(str->data);
        free(str);
    }
}

static void buffer_free(HmlBuffer *buf) {
    if (buf) {
        free(buf->data);
        free(buf);
    }
}

static void array_free(HmlArray *arr);
static void object_free(HmlObject *obj);

void hml_release(HmlValue *val) {
    if (val == NULL) return;

    switch (val->type) {
        case HML_VAL_STRING:
            if (val->as.as_string) {
                val->as.as_string->ref_count--;
                if (val->as.as_string->ref_count <= 0) {
                    string_free(val->as.as_string);
                }
                val->as.as_string = NULL;
            }
            break;
        case HML_VAL_BUFFER:
            if (val->as.as_buffer) {
                val->as.as_buffer->ref_count--;
                if (val->as.as_buffer->ref_count <= 0) {
                    buffer_free(val->as.as_buffer);
                }
                val->as.as_buffer = NULL;
            }
            break;
        case HML_VAL_ARRAY:
            if (val->as.as_array) {
                val->as.as_array->ref_count--;
                if (val->as.as_array->ref_count <= 0) {
                    array_free(val->as.as_array);
                }
                val->as.as_array = NULL;
            }
            break;
        case HML_VAL_OBJECT:
            if (val->as.as_object) {
                val->as.as_object->ref_count--;
                if (val->as.as_object->ref_count <= 0) {
                    object_free(val->as.as_object);
                }
                val->as.as_object = NULL;
            }
            break;
        case HML_VAL_FUNCTION:
            if (val->as.as_function) {
                val->as.as_function->ref_count--;
                if (val->as.as_function->ref_count <= 0) {
                    free(val->as.as_function);
                }
                val->as.as_function = NULL;
            }
            break;
        default:
            break;
    }
}

static void array_free(HmlArray *arr) {
    if (arr) {
        for (int i = 0; i < arr->length; i++) {
            hml_release(&arr->elements[i]);
        }
        free(arr->elements);
        free(arr);
    }
}

static void object_free(HmlObject *obj) {
    if (obj) {
        for (int i = 0; i < obj->num_fields; i++) {
            free(obj->field_names[i]);
            hml_release(&obj->field_values[i]);
        }
        free(obj->field_names);
        free(obj->field_values);
        free(obj->type_name);
        free(obj);
    }
}

// ========== TYPE CHECKING ==========

int hml_is_null(HmlValue val) { return val.type == HML_VAL_NULL; }
int hml_is_i32(HmlValue val) { return val.type == HML_VAL_I32; }
int hml_is_i64(HmlValue val) { return val.type == HML_VAL_I64; }
int hml_is_f64(HmlValue val) { return val.type == HML_VAL_F64; }
int hml_is_bool(HmlValue val) { return val.type == HML_VAL_BOOL; }
int hml_is_string(HmlValue val) { return val.type == HML_VAL_STRING; }
int hml_is_array(HmlValue val) { return val.type == HML_VAL_ARRAY; }
int hml_is_object(HmlValue val) { return val.type == HML_VAL_OBJECT; }
int hml_is_function(HmlValue val) { return val.type == HML_VAL_FUNCTION || val.type == HML_VAL_BUILTIN_FN; }

int hml_is_numeric(HmlValue val) {
    switch (val.type) {
        case HML_VAL_I8: case HML_VAL_I16: case HML_VAL_I32: case HML_VAL_I64:
        case HML_VAL_U8: case HML_VAL_U16: case HML_VAL_U32: case HML_VAL_U64:
        case HML_VAL_F32: case HML_VAL_F64: case HML_VAL_RUNE:
            return 1;
        default:
            return 0;
    }
}

int hml_is_integer(HmlValue val) {
    switch (val.type) {
        case HML_VAL_I8: case HML_VAL_I16: case HML_VAL_I32: case HML_VAL_I64:
        case HML_VAL_U8: case HML_VAL_U16: case HML_VAL_U32: case HML_VAL_U64:
        case HML_VAL_RUNE:
            return 1;
        default:
            return 0;
    }
}

// ========== TYPE CONVERSION ==========

int hml_to_bool(HmlValue val) {
    switch (val.type) {
        case HML_VAL_BOOL: return val.as.as_bool;
        case HML_VAL_I32: return val.as.as_i32 != 0;
        case HML_VAL_I64: return val.as.as_i64 != 0;
        case HML_VAL_F64: return val.as.as_f64 != 0.0;
        case HML_VAL_STRING: return val.as.as_string != NULL && val.as.as_string->length > 0;
        case HML_VAL_ARRAY: return val.as.as_array != NULL && val.as.as_array->length > 0;
        case HML_VAL_NULL: return 0;
        default: return 1;
    }
}

int32_t hml_to_i32(HmlValue val) {
    switch (val.type) {
        case HML_VAL_I8: return (int32_t)val.as.as_i8;
        case HML_VAL_I16: return (int32_t)val.as.as_i16;
        case HML_VAL_I32: return val.as.as_i32;
        case HML_VAL_I64: return (int32_t)val.as.as_i64;
        case HML_VAL_U8: return (int32_t)val.as.as_u8;
        case HML_VAL_U16: return (int32_t)val.as.as_u16;
        case HML_VAL_U32: return (int32_t)val.as.as_u32;
        case HML_VAL_U64: return (int32_t)val.as.as_u64;
        case HML_VAL_F32: return (int32_t)val.as.as_f32;
        case HML_VAL_F64: return (int32_t)val.as.as_f64;
        case HML_VAL_BOOL: return val.as.as_bool ? 1 : 0;
        case HML_VAL_RUNE: return (int32_t)val.as.as_rune;
        default: return 0;
    }
}

int64_t hml_to_i64(HmlValue val) {
    switch (val.type) {
        case HML_VAL_I32: return (int64_t)val.as.as_i32;
        case HML_VAL_I64: return val.as.as_i64;
        case HML_VAL_F64: return (int64_t)val.as.as_f64;
        case HML_VAL_BOOL: return val.as.as_bool ? 1 : 0;
        default: return 0;
    }
}

double hml_to_f64(HmlValue val) {
    switch (val.type) {
        case HML_VAL_I32: return (double)val.as.as_i32;
        case HML_VAL_I64: return (double)val.as.as_i64;
        case HML_VAL_F32: return (double)val.as.as_f32;
        case HML_VAL_F64: return val.as.as_f64;
        case HML_VAL_BOOL: return val.as.as_bool ? 1.0 : 0.0;
        default: return 0.0;
    }
}

const char* hml_to_string_ptr(HmlValue val) {
    if (val.type == HML_VAL_STRING && val.as.as_string) {
        return val.as.as_string->data;
    }
    return NULL;
}

// ========== TYPE NAME ==========

const char* hml_type_name(HmlValueType type) {
    switch (type) {
        case HML_VAL_I8: return "i8";
        case HML_VAL_I16: return "i16";
        case HML_VAL_I32: return "i32";
        case HML_VAL_I64: return "i64";
        case HML_VAL_U8: return "u8";
        case HML_VAL_U16: return "u16";
        case HML_VAL_U32: return "u32";
        case HML_VAL_U64: return "u64";
        case HML_VAL_F32: return "f32";
        case HML_VAL_F64: return "f64";
        case HML_VAL_BOOL: return "bool";
        case HML_VAL_STRING: return "string";
        case HML_VAL_RUNE: return "rune";
        case HML_VAL_PTR: return "ptr";
        case HML_VAL_BUFFER: return "buffer";
        case HML_VAL_ARRAY: return "array";
        case HML_VAL_OBJECT: return "object";
        case HML_VAL_FILE: return "file";
        case HML_VAL_FUNCTION: return "function";
        case HML_VAL_BUILTIN_FN: return "builtin_fn";
        case HML_VAL_TASK: return "task";
        case HML_VAL_CHANNEL: return "channel";
        case HML_VAL_SOCKET: return "socket";
        case HML_VAL_NULL: return "null";
        default: return "unknown";
    }
}

const char* hml_typeof_str(HmlValue val) {
    if (val.type == HML_VAL_OBJECT && val.as.as_object && val.as.as_object->type_name) {
        return val.as.as_object->type_name;
    }
    return hml_type_name(val.type);
}

const char* hml_typeof(HmlValue val) {
    return hml_typeof_str(val);
}

// ========== BINARY OPERATIONS ==========

typedef enum {
    HML_OP_ADD, HML_OP_SUB, HML_OP_MUL, HML_OP_DIV, HML_OP_MOD,
    HML_OP_EQUAL, HML_OP_NOT_EQUAL,
    HML_OP_LESS, HML_OP_LESS_EQUAL, HML_OP_GREATER, HML_OP_GREATER_EQUAL,
    HML_OP_AND, HML_OP_OR,
    HML_OP_BIT_AND, HML_OP_BIT_OR, HML_OP_BIT_XOR, HML_OP_LSHIFT, HML_OP_RSHIFT,
} HmlBinaryOp;

HmlValue hml_binary_op(HmlBinaryOp op, HmlValue left, HmlValue right) {
    // For now, simple numeric operations
    double l = hml_to_f64(left);
    double r = hml_to_f64(right);

    switch (op) {
        case HML_OP_ADD: return hml_val_f64(l + r);
        case HML_OP_SUB: return hml_val_f64(l - r);
        case HML_OP_MUL: return hml_val_f64(l * r);
        case HML_OP_DIV:
            if (r == 0) hml_runtime_error("Division by zero");
            return hml_val_f64(l / r);
        case HML_OP_MOD: return hml_val_f64(fmod(l, r));
        case HML_OP_EQUAL: return hml_val_bool(l == r);
        case HML_OP_NOT_EQUAL: return hml_val_bool(l != r);
        case HML_OP_LESS: return hml_val_bool(l < r);
        case HML_OP_LESS_EQUAL: return hml_val_bool(l <= r);
        case HML_OP_GREATER: return hml_val_bool(l > r);
        case HML_OP_GREATER_EQUAL: return hml_val_bool(l >= r);
        case HML_OP_AND: return hml_val_bool(l && r);
        case HML_OP_OR: return hml_val_bool(l || r);
        case HML_OP_BIT_AND: return hml_val_i64((int64_t)l & (int64_t)r);
        case HML_OP_BIT_OR: return hml_val_i64((int64_t)l | (int64_t)r);
        case HML_OP_BIT_XOR: return hml_val_i64((int64_t)l ^ (int64_t)r);
        case HML_OP_LSHIFT: return hml_val_i64((int64_t)l << (int)r);
        case HML_OP_RSHIFT: return hml_val_i64((int64_t)l >> (int)r);
        default: return hml_val_null();
    }
}

// ========== PRINT ==========

static void print_value_to_file(FILE *out, HmlValue val);

static void print_value_to_file(FILE *out, HmlValue val) {
    switch (val.type) {
        case HML_VAL_I8: fprintf(out, "%d", val.as.as_i8); break;
        case HML_VAL_I16: fprintf(out, "%d", val.as.as_i16); break;
        case HML_VAL_I32: fprintf(out, "%d", val.as.as_i32); break;
        case HML_VAL_I64: fprintf(out, "%lld", (long long)val.as.as_i64); break;
        case HML_VAL_U8: fprintf(out, "%u", val.as.as_u8); break;
        case HML_VAL_U16: fprintf(out, "%u", val.as.as_u16); break;
        case HML_VAL_U32: fprintf(out, "%u", val.as.as_u32); break;
        case HML_VAL_U64: fprintf(out, "%llu", (unsigned long long)val.as.as_u64); break;
        case HML_VAL_F32: fprintf(out, "%g", val.as.as_f32); break;
        case HML_VAL_F64: fprintf(out, "%g", val.as.as_f64); break;
        case HML_VAL_BOOL: fprintf(out, "%s", val.as.as_bool ? "true" : "false"); break;
        case HML_VAL_STRING:
            if (val.as.as_string) fprintf(out, "%s", val.as.as_string->data);
            break;
        case HML_VAL_NULL: fprintf(out, "null"); break;
        case HML_VAL_ARRAY:
            fprintf(out, "[");
            if (val.as.as_array) {
                for (int i = 0; i < val.as.as_array->length; i++) {
                    if (i > 0) fprintf(out, ", ");
                    print_value_to_file(out, val.as.as_array->elements[i]);
                }
            }
            fprintf(out, "]");
            break;
        case HML_VAL_OBJECT:
            fprintf(out, "{");
            if (val.as.as_object) {
                for (int i = 0; i < val.as.as_object->num_fields; i++) {
                    if (i > 0) fprintf(out, ", ");
                    fprintf(out, "%s: ", val.as.as_object->field_names[i]);
                    print_value_to_file(out, val.as.as_object->field_values[i]);
                }
            }
            fprintf(out, "}");
            break;
        case HML_VAL_FUNCTION: fprintf(out, "<function>"); break;
        default: fprintf(out, "<unknown>"); break;
    }
}

void hml_print(HmlValue val) {
    print_value_to_file(stdout, val);
    printf("\n");
    fflush(stdout);
}

// ========== STRING OPERATIONS ==========

HmlValue hml_string_concat(HmlValue a, HmlValue b) {
    // Convert to strings if needed
    char buffer[64];
    const char *str_a = NULL;
    const char *str_b = NULL;
    int len_a = 0, len_b = 0;
    int free_a = 0, free_b = 0;

    if (a.type == HML_VAL_STRING && a.as.as_string) {
        str_a = a.as.as_string->data;
        len_a = a.as.as_string->length;
    } else if (a.type == HML_VAL_I32) {
        snprintf(buffer, sizeof(buffer), "%d", a.as.as_i32);
        str_a = strdup(buffer);
        len_a = strlen(str_a);
        free_a = 1;
    } else if (a.type == HML_VAL_I64) {
        snprintf(buffer, sizeof(buffer), "%lld", (long long)a.as.as_i64);
        str_a = strdup(buffer);
        len_a = strlen(str_a);
        free_a = 1;
    } else if (a.type == HML_VAL_F64) {
        snprintf(buffer, sizeof(buffer), "%g", a.as.as_f64);
        str_a = strdup(buffer);
        len_a = strlen(str_a);
        free_a = 1;
    } else if (a.type == HML_VAL_BOOL) {
        str_a = a.as.as_bool ? "true" : "false";
        len_a = strlen(str_a);
    } else if (a.type == HML_VAL_NULL) {
        str_a = "null";
        len_a = 4;
    } else {
        str_a = "";
        len_a = 0;
    }

    if (b.type == HML_VAL_STRING && b.as.as_string) {
        str_b = b.as.as_string->data;
        len_b = b.as.as_string->length;
    } else if (b.type == HML_VAL_I32) {
        snprintf(buffer, sizeof(buffer), "%d", b.as.as_i32);
        str_b = strdup(buffer);
        len_b = strlen(str_b);
        free_b = 1;
    } else if (b.type == HML_VAL_I64) {
        snprintf(buffer, sizeof(buffer), "%lld", (long long)b.as.as_i64);
        str_b = strdup(buffer);
        len_b = strlen(str_b);
        free_b = 1;
    } else if (b.type == HML_VAL_F64) {
        snprintf(buffer, sizeof(buffer), "%g", b.as.as_f64);
        str_b = strdup(buffer);
        len_b = strlen(str_b);
        free_b = 1;
    } else if (b.type == HML_VAL_BOOL) {
        str_b = b.as.as_bool ? "true" : "false";
        len_b = strlen(str_b);
    } else if (b.type == HML_VAL_NULL) {
        str_b = "null";
        len_b = 4;
    } else {
        str_b = "";
        len_b = 0;
    }

    int total_len = len_a + len_b;
    char *result = malloc(total_len + 1);
    memcpy(result, str_a, len_a);
    memcpy(result + len_a, str_b, len_b);
    result[total_len] = '\0';

    if (free_a) free((void*)str_a);
    if (free_b) free((void*)str_b);

    return hml_val_string_owned(result, total_len, total_len + 1);
}

// ========== ARRAY OPERATIONS ==========

void hml_array_push(HmlValue arr, HmlValue val) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) return;

    HmlArray *a = arr.as.as_array;
    if (a->length >= a->capacity) {
        int new_cap = a->capacity == 0 ? 4 : a->capacity * 2;
        a->elements = realloc(a->elements, new_cap * sizeof(HmlValue));
        a->capacity = new_cap;
    }

    a->elements[a->length] = val;
    hml_retain(&a->elements[a->length]);
    a->length++;
}

HmlValue hml_array_length(HmlValue arr) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) return hml_val_i32(0);
    return hml_val_i32(arr.as.as_array->length);
}

HmlValue hml_array_get(HmlValue arr, HmlValue index) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) return hml_val_null();
    int idx = hml_to_i32(index);
    if (idx < 0 || idx >= arr.as.as_array->length) return hml_val_null();
    HmlValue result = arr.as.as_array->elements[idx];
    hml_retain(&result);
    return result;
}

void hml_array_set(HmlValue arr, HmlValue index, HmlValue val) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) return;
    int idx = hml_to_i32(index);
    if (idx < 0 || idx >= arr.as.as_array->length) return;
    hml_release(&arr.as.as_array->elements[idx]);
    arr.as.as_array->elements[idx] = val;
    hml_retain(&arr.as.as_array->elements[idx]);
}

// ========== METHOD CALLS ==========

__thread HmlValue hml_self = {0};

HmlValue hml_call_method(HmlValue obj, const char *method, HmlValue *args, int num_args) {
    (void)args;
    (void)num_args;

    // Handle array methods
    if (obj.type == HML_VAL_ARRAY) {
        if (strcmp(method, "length") == 0) {
            return hml_array_length(obj);
        }
    }

    // Handle string methods
    if (obj.type == HML_VAL_STRING) {
        if (strcmp(method, "length") == 0) {
            if (obj.as.as_string) {
                return hml_val_i32(obj.as.as_string->length);
            }
            return hml_val_i32(0);
        }
    }

    hml_runtime_error("Unknown method: %s", method);
}

// ========== CLOSURE ENVIRONMENT ==========

struct HmlClosureEnv {
    HmlValue *captured;
    int num_captured;
    int ref_count;
};

HmlClosureEnv* hml_closure_env_new(int num_vars) {
    HmlClosureEnv *env = malloc(sizeof(HmlClosureEnv));
    env->captured = calloc(num_vars, sizeof(HmlValue));
    env->num_captured = num_vars;
    env->ref_count = 1;

    for (int i = 0; i < num_vars; i++) {
        env->captured[i] = hml_val_null();
    }

    return env;
}

void hml_closure_env_free(HmlClosureEnv *env) {
    if (env) {
        for (int i = 0; i < env->num_captured; i++) {
            hml_release(&env->captured[i]);
        }
        free(env->captured);
        free(env);
    }
}

void hml_closure_env_retain(HmlClosureEnv *env) {
    if (env) env->ref_count++;
}

void hml_closure_env_release(HmlClosureEnv *env) {
    if (env) {
        env->ref_count--;
        if (env->ref_count <= 0) {
            hml_closure_env_free(env);
        }
    }
}

HmlValue hml_closure_env_get(HmlClosureEnv *env, int index) {
    if (env && index >= 0 && index < env->num_captured) {
        HmlValue val = env->captured[index];
        hml_retain(&val);
        return val;
    }
    return hml_val_null();
}

void hml_closure_env_set(HmlClosureEnv *env, int index, HmlValue val) {
    if (env && index >= 0 && index < env->num_captured) {
        hml_release(&env->captured[index]);
        env->captured[index] = val;
        hml_retain(&env->captured[index]);
    }
}
