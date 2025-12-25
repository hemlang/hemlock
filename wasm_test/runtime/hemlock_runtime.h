/*
 * Hemlock Runtime Library - WASM Version
 *
 * Minimal header for WebAssembly compilation.
 * Many features are stubbed out or unavailable.
 */

#ifndef HEMLOCK_RUNTIME_H
#define HEMLOCK_RUNTIME_H

#include "hemlock_value.h"
#include <setjmp.h>

// Forward declarations
typedef struct HmlClosureEnv HmlClosureEnv;

// ========== RUNTIME INITIALIZATION ==========

void hml_runtime_init(int argc, char **argv);
void hml_runtime_cleanup(void);
HmlValue hml_get_args(void);

// ========== BINARY OPERATIONS ==========

typedef enum {
    HML_OP_ADD,
    HML_OP_SUB,
    HML_OP_MUL,
    HML_OP_DIV,
    HML_OP_MOD,
    HML_OP_EQUAL,
    HML_OP_NOT_EQUAL,
    HML_OP_LESS,
    HML_OP_LESS_EQUAL,
    HML_OP_GREATER,
    HML_OP_GREATER_EQUAL,
    HML_OP_AND,
    HML_OP_OR,
    HML_OP_BIT_AND,
    HML_OP_BIT_OR,
    HML_OP_BIT_XOR,
    HML_OP_LSHIFT,
    HML_OP_RSHIFT,
} HmlBinaryOp;

HmlValue hml_binary_op(HmlBinaryOp op, HmlValue left, HmlValue right);

// ========== BUILTIN FUNCTIONS ==========

void hml_print(HmlValue val);
const char* hml_typeof(HmlValue val);

// ========== STRING OPERATIONS ==========

HmlValue hml_string_concat(HmlValue a, HmlValue b);

// ========== ARRAY OPERATIONS ==========

void hml_array_push(HmlValue arr, HmlValue val);
HmlValue hml_array_length(HmlValue arr);
HmlValue hml_array_get(HmlValue arr, HmlValue index);
void hml_array_set(HmlValue arr, HmlValue index, HmlValue val);

// ========== METHOD CALLS ==========

extern __thread HmlValue hml_self;
HmlValue hml_call_method(HmlValue obj, const char *method, HmlValue *args, int num_args);

// ========== EXCEPTION HANDLING ==========

typedef struct HmlExceptionContext {
    jmp_buf exception_buf;
    HmlValue exception_value;
    int is_active;
    struct HmlExceptionContext *prev;
} HmlExceptionContext;

HmlExceptionContext* hml_exception_push(void);
void hml_exception_pop(void);
__attribute__((noreturn)) void hml_throw(HmlValue exception_value);
HmlValue hml_exception_get_value(void);
__attribute__((noreturn)) void hml_runtime_error(const char *format, ...);

// ========== CLOSURE SUPPORT ==========

typedef struct HmlClosureEnv {
    HmlValue *captured;
    int num_captured;
    int ref_count;
} HmlClosureEnv;

HmlClosureEnv* hml_closure_env_new(int num_vars);
void hml_closure_env_free(HmlClosureEnv *env);
void hml_closure_env_retain(HmlClosureEnv *env);
void hml_closure_env_release(HmlClosureEnv *env);
HmlValue hml_closure_env_get(HmlClosureEnv *env, int index);
void hml_closure_env_set(HmlClosureEnv *env, int index, HmlValue val);

// ========== CALL STACK TRACKING ==========

#ifndef HML_MAX_CALL_DEPTH
#define HML_MAX_CALL_DEPTH 10000
#endif

extern __thread int hml_g_call_depth;
extern __thread int hml_g_max_call_depth;

#define HML_CALL_ENTER() do { \
    if (__builtin_expect(++hml_g_call_depth > hml_g_max_call_depth, 0)) { \
        hml_g_call_depth = 0; \
        hml_runtime_error("Maximum call stack depth exceeded (infinite recursion?)"); \
    } \
} while(0)

#define HML_CALL_EXIT() do { \
    hml_g_call_depth--; \
} while(0)

// ========== UTILITY MACROS ==========

#define HML_STRING(s) hml_val_string(s)
#define HML_I32(n) hml_val_i32(n)
#define HML_F64(n) hml_val_f64(n)
#define HML_BOOL(b) hml_val_bool(b)
#define HML_NULL hml_val_null()

#endif // HEMLOCK_RUNTIME_H
