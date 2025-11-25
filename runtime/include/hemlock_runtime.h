/*
 * Hemlock Runtime Library
 *
 * Main header file for the Hemlock runtime library.
 * Include this header in compiled Hemlock programs.
 */

#ifndef HEMLOCK_RUNTIME_H
#define HEMLOCK_RUNTIME_H

#include "hemlock_value.h"
#include <setjmp.h>

// ========== RUNTIME INITIALIZATION ==========

// Initialize the Hemlock runtime (call at start of main)
void hml_runtime_init(int argc, char **argv);

// Cleanup the Hemlock runtime (call at end of main)
void hml_runtime_cleanup(void);

// Get command-line arguments as Hemlock array
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

// Perform binary operation with automatic type promotion
HmlValue hml_binary_op(HmlBinaryOp op, HmlValue left, HmlValue right);

// ========== UNARY OPERATIONS ==========

typedef enum {
    HML_UNARY_NOT,
    HML_UNARY_NEGATE,
    HML_UNARY_BIT_NOT,
} HmlUnaryOp;

HmlValue hml_unary_op(HmlUnaryOp op, HmlValue operand);

// ========== BUILTIN FUNCTIONS ==========

// I/O
void hml_print(HmlValue val);
void hml_eprint(HmlValue val);
HmlValue hml_read_line(void);

// Type checking
const char* hml_typeof(HmlValue val);
void hml_check_type(HmlValue val, HmlValueType expected, const char *var_name);

// Assertions
void hml_assert(HmlValue condition, HmlValue message);
void hml_panic(HmlValue message);

// ========== STRING OPERATIONS ==========

HmlValue hml_string_concat(HmlValue a, HmlValue b);
HmlValue hml_string_length(HmlValue str);
HmlValue hml_string_byte_length(HmlValue str);
HmlValue hml_string_char_at(HmlValue str, HmlValue index);
HmlValue hml_string_byte_at(HmlValue str, HmlValue index);
HmlValue hml_string_substr(HmlValue str, HmlValue start, HmlValue length);
HmlValue hml_string_slice(HmlValue str, HmlValue start, HmlValue end);
HmlValue hml_string_find(HmlValue str, HmlValue needle);
HmlValue hml_string_contains(HmlValue str, HmlValue needle);
HmlValue hml_string_split(HmlValue str, HmlValue delimiter);
HmlValue hml_string_trim(HmlValue str);
HmlValue hml_string_to_upper(HmlValue str);
HmlValue hml_string_to_lower(HmlValue str);
HmlValue hml_string_starts_with(HmlValue str, HmlValue prefix);
HmlValue hml_string_ends_with(HmlValue str, HmlValue suffix);
HmlValue hml_string_replace(HmlValue str, HmlValue old, HmlValue new_str);
HmlValue hml_string_replace_all(HmlValue str, HmlValue old, HmlValue new_str);
HmlValue hml_string_repeat(HmlValue str, HmlValue count);

// String index access (returns rune)
HmlValue hml_string_index(HmlValue str, HmlValue index);
void hml_string_index_assign(HmlValue str, HmlValue index, HmlValue rune);

// ========== ARRAY OPERATIONS ==========

void hml_array_push(HmlValue arr, HmlValue val);
HmlValue hml_array_pop(HmlValue arr);
HmlValue hml_array_shift(HmlValue arr);
void hml_array_unshift(HmlValue arr, HmlValue val);
void hml_array_insert(HmlValue arr, HmlValue index, HmlValue val);
HmlValue hml_array_remove(HmlValue arr, HmlValue index);
HmlValue hml_array_find(HmlValue arr, HmlValue val);
HmlValue hml_array_contains(HmlValue arr, HmlValue val);
HmlValue hml_array_slice(HmlValue arr, HmlValue start, HmlValue end);
HmlValue hml_array_join(HmlValue arr, HmlValue delimiter);
HmlValue hml_array_concat(HmlValue arr1, HmlValue arr2);
void hml_array_reverse(HmlValue arr);
HmlValue hml_array_first(HmlValue arr);
HmlValue hml_array_last(HmlValue arr);
void hml_array_clear(HmlValue arr);
HmlValue hml_array_length(HmlValue arr);

// Array index access
HmlValue hml_array_get(HmlValue arr, HmlValue index);
void hml_array_set(HmlValue arr, HmlValue index, HmlValue val);

// Higher-order array operations
HmlValue hml_array_map(HmlValue arr, HmlValue callback);
HmlValue hml_array_filter(HmlValue arr, HmlValue predicate);
HmlValue hml_array_reduce(HmlValue arr, HmlValue reducer, HmlValue initial);

// ========== OBJECT OPERATIONS ==========

HmlValue hml_object_get_field(HmlValue obj, const char *field);
void hml_object_set_field(HmlValue obj, const char *field, HmlValue val);
int hml_object_has_field(HmlValue obj, const char *field);

// ========== MEMORY OPERATIONS ==========

HmlValue hml_alloc(int32_t size);
void hml_free(HmlValue ptr_or_buffer);
HmlValue hml_realloc(HmlValue ptr, int32_t new_size);
void hml_memset(HmlValue ptr, uint8_t byte_val, int32_t size);
void hml_memcpy(HmlValue dest, HmlValue src, int32_t size);
int32_t hml_sizeof_type(HmlValueType type);

// Buffer operations
HmlValue hml_buffer_get(HmlValue buf, HmlValue index);
void hml_buffer_set(HmlValue buf, HmlValue index, HmlValue val);
HmlValue hml_buffer_length(HmlValue buf);

// ========== FUNCTION CALLS ==========

// Call a function value with arguments
HmlValue hml_call_function(HmlValue fn, HmlValue *args, int num_args);

// Call a method on an object
HmlValue hml_call_method(HmlValue obj, const char *method, HmlValue *args, int num_args);

// ========== EXCEPTION HANDLING ==========

// Exception context using setjmp/longjmp
typedef struct HmlExceptionContext {
    jmp_buf exception_buf;
    HmlValue exception_value;
    int is_active;
    struct HmlExceptionContext *prev;
} HmlExceptionContext;

// Exception stack management
HmlExceptionContext* hml_exception_push(void);
void hml_exception_pop(void);
void hml_throw(HmlValue exception_value);
HmlValue hml_exception_get_value(void);

// ========== DEFER SUPPORT ==========

typedef void (*HmlDeferFn)(void *arg);

void hml_defer_push(HmlDeferFn fn, void *arg);
void hml_defer_pop_and_execute(void);
void hml_defer_execute_all(void);

// ========== ASYNC/CONCURRENCY ==========

// Task management
HmlValue hml_spawn(HmlValue fn, HmlValue *args, int num_args);
HmlValue hml_join(HmlValue task);
void hml_detach(HmlValue task);

// Channels
HmlValue hml_channel(int32_t capacity);
void hml_channel_send(HmlValue channel, HmlValue value);
HmlValue hml_channel_recv(HmlValue channel);
void hml_channel_close(HmlValue channel);

// ========== FILE I/O ==========

HmlValue hml_open(HmlValue path, HmlValue mode);
HmlValue hml_file_read(HmlValue file, HmlValue size);
HmlValue hml_file_read_all(HmlValue file);
HmlValue hml_file_write(HmlValue file, HmlValue data);
HmlValue hml_file_seek(HmlValue file, HmlValue position);
HmlValue hml_file_tell(HmlValue file);
void hml_file_close(HmlValue file);

// ========== MATH FUNCTIONS ==========

double hml_sin(double x);
double hml_cos(double x);
double hml_tan(double x);
double hml_sqrt(double x);
double hml_pow(double base, double exp);
double hml_exp(double x);
double hml_log(double x);
double hml_log10(double x);
double hml_floor(double x);
double hml_ceil(double x);
double hml_round(double x);
double hml_abs_f64(double x);
int64_t hml_abs_i64(int64_t x);

// ========== UTILITY MACROS ==========

// Create a string literal value (compile-time optimization)
#define HML_STRING(s) hml_val_string(s)

// Create an integer literal value
#define HML_I32(n) hml_val_i32(n)

// Create a float literal value
#define HML_F64(n) hml_val_f64(n)

// Create a boolean literal value
#define HML_BOOL(b) hml_val_bool(b)

// Create a null value
#define HML_NULL hml_val_null()

#endif // HEMLOCK_RUNTIME_H
