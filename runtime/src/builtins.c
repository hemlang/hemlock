/*
 * Hemlock Runtime Library - Builtins Implementation
 *
 * This file implements the core builtin functions:
 * print, typeof, assert, panic, and operations.
 */

#include "../include/hemlock_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ========== GLOBAL STATE ==========

static int g_argc = 0;
static char **g_argv = NULL;
static HmlExceptionContext *g_exception_stack = NULL;

// Defer stack
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
    hml_defer_execute_all();

    // Clear exception stack
    while (g_exception_stack) {
        hml_exception_pop();
    }
}

HmlValue hml_get_args(void) {
    HmlValue arr = hml_val_array();

    // Skip the first argument (program name), start from index 1
    // args[0] in Hemlock is the script filename
    for (int i = 1; i < g_argc; i++) {
        HmlValue str = hml_val_string(g_argv[i]);
        hml_array_push(arr, str);
    }

    return arr;
}

// ========== PRINT IMPLEMENTATION ==========

// Helper to print a value to a file
static void print_value_to(FILE *out, HmlValue val) {
    switch (val.type) {
        case HML_VAL_I8:
            fprintf(out, "%d", val.as.as_i8);
            break;
        case HML_VAL_I16:
            fprintf(out, "%d", val.as.as_i16);
            break;
        case HML_VAL_I32:
            fprintf(out, "%d", val.as.as_i32);
            break;
        case HML_VAL_I64:
            fprintf(out, "%ld", val.as.as_i64);
            break;
        case HML_VAL_U8:
            fprintf(out, "%u", val.as.as_u8);
            break;
        case HML_VAL_U16:
            fprintf(out, "%u", val.as.as_u16);
            break;
        case HML_VAL_U32:
            fprintf(out, "%u", val.as.as_u32);
            break;
        case HML_VAL_U64:
            fprintf(out, "%lu", val.as.as_u64);
            break;
        case HML_VAL_F32:
            fprintf(out, "%g", val.as.as_f32);
            break;
        case HML_VAL_F64:
            fprintf(out, "%g", val.as.as_f64);
            break;
        case HML_VAL_BOOL:
            fprintf(out, "%s", val.as.as_bool ? "true" : "false");
            break;
        case HML_VAL_STRING:
            if (val.as.as_string) {
                fprintf(out, "%s", val.as.as_string->data);
            }
            break;
        case HML_VAL_RUNE:
            // Print as character if printable ASCII, else as U+XXXX
            if (val.as.as_rune >= 0x20 && val.as.as_rune < 0x7F) {
                fprintf(out, "'%c'", (char)val.as.as_rune);
            } else {
                fprintf(out, "U+%04X", val.as.as_rune);
            }
            break;
        case HML_VAL_NULL:
            fprintf(out, "null");
            break;
        case HML_VAL_PTR:
            fprintf(out, "ptr<%p>", val.as.as_ptr);
            break;
        case HML_VAL_BUFFER:
            if (val.as.as_buffer) {
                fprintf(out, "buffer[%d]", val.as.as_buffer->length);
            } else {
                fprintf(out, "buffer[null]");
            }
            break;
        case HML_VAL_ARRAY:
            if (val.as.as_array) {
                fprintf(out, "[");
                for (int i = 0; i < val.as.as_array->length; i++) {
                    if (i > 0) fprintf(out, ", ");
                    // Print string elements with quotes
                    if (val.as.as_array->elements[i].type == HML_VAL_STRING) {
                        fprintf(out, "\"");
                        print_value_to(out, val.as.as_array->elements[i]);
                        fprintf(out, "\"");
                    } else {
                        print_value_to(out, val.as.as_array->elements[i]);
                    }
                }
                fprintf(out, "]");
            } else {
                fprintf(out, "[]");
            }
            break;
        case HML_VAL_OBJECT:
            if (val.as.as_object) {
                fprintf(out, "{");
                for (int i = 0; i < val.as.as_object->num_fields; i++) {
                    if (i > 0) fprintf(out, ", ");
                    fprintf(out, "%s: ", val.as.as_object->field_names[i]);
                    print_value_to(out, val.as.as_object->field_values[i]);
                }
                fprintf(out, "}");
            } else {
                fprintf(out, "{}");
            }
            break;
        case HML_VAL_FUNCTION:
            fprintf(out, "<function>");
            break;
        case HML_VAL_BUILTIN_FN:
            fprintf(out, "<builtin>");
            break;
        case HML_VAL_TASK:
            fprintf(out, "<task>");
            break;
        case HML_VAL_CHANNEL:
            fprintf(out, "<channel>");
            break;
        case HML_VAL_FILE:
            fprintf(out, "<file>");
            break;
        default:
            fprintf(out, "<unknown>");
            break;
    }
}

void hml_print(HmlValue val) {
    print_value_to(stdout, val);
    printf("\n");
    fflush(stdout);
}

void hml_eprint(HmlValue val) {
    print_value_to(stderr, val);
    fprintf(stderr, "\n");
    fflush(stderr);
}

HmlValue hml_read_line(void) {
    char buffer[4096];
    if (fgets(buffer, sizeof(buffer), stdin) != NULL) {
        // Remove trailing newline
        int len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
        }
        return hml_val_string(buffer);
    }
    return hml_val_null();
}

// ========== TYPE CHECKING ==========

const char* hml_typeof(HmlValue val) {
    return hml_typeof_str(val);
}

void hml_check_type(HmlValue val, HmlValueType expected, const char *var_name) {
    if (val.type != expected) {
        fprintf(stderr, "Runtime error: Type mismatch for '%s': expected %s, got %s\n",
                var_name, hml_type_name(expected), hml_typeof_str(val));
        exit(1);
    }
}

// ========== ASSERTIONS ==========

void hml_assert(HmlValue condition, HmlValue message) {
    if (!hml_to_bool(condition)) {
        fprintf(stderr, "Assertion failed");
        if (message.type == HML_VAL_STRING && message.as.as_string) {
            fprintf(stderr, ": %s", message.as.as_string->data);
        }
        fprintf(stderr, "\n");
        exit(1);
    }
}

void hml_panic(HmlValue message) {
    fprintf(stderr, "panic: ");
    print_value_to(stderr, message);
    fprintf(stderr, "\n");
    exit(1);
}

// ========== BINARY OPERATIONS ==========

// Type promotion table (higher number = higher priority)
static int type_priority(HmlValueType type) {
    switch (type) {
        case HML_VAL_I8:   return 1;
        case HML_VAL_U8:   return 2;
        case HML_VAL_I16:  return 3;
        case HML_VAL_U16:  return 4;
        case HML_VAL_I32:  return 5;
        case HML_VAL_U32:  return 6;
        case HML_VAL_I64:  return 7;
        case HML_VAL_U64:  return 8;
        case HML_VAL_F32:  return 9;
        case HML_VAL_F64:  return 10;
        default:          return 0;
    }
}

static HmlValueType promote_types(HmlValueType a, HmlValueType b) {
    // Float always wins
    if (a == HML_VAL_F64 || b == HML_VAL_F64) return HML_VAL_F64;
    if (a == HML_VAL_F32 || b == HML_VAL_F32) return HML_VAL_F32;

    // Otherwise, higher priority wins
    return (type_priority(a) >= type_priority(b)) ? a : b;
}

HmlValue hml_binary_op(HmlBinaryOp op, HmlValue left, HmlValue right) {
    // String concatenation
    if (op == HML_OP_ADD && (left.type == HML_VAL_STRING || right.type == HML_VAL_STRING)) {
        return hml_string_concat(left, right);
    }

    // Boolean operations
    if (op == HML_OP_AND) {
        return hml_val_bool(hml_to_bool(left) && hml_to_bool(right));
    }
    if (op == HML_OP_OR) {
        return hml_val_bool(hml_to_bool(left) || hml_to_bool(right));
    }

    // Equality/inequality work on all types
    if (op == HML_OP_EQUAL || op == HML_OP_NOT_EQUAL) {
        int equal = 0;
        if (left.type == HML_VAL_NULL || right.type == HML_VAL_NULL) {
            equal = (left.type == HML_VAL_NULL && right.type == HML_VAL_NULL);
        } else if (left.type == HML_VAL_BOOL && right.type == HML_VAL_BOOL) {
            equal = (left.as.as_bool == right.as.as_bool);
        } else if (left.type == HML_VAL_STRING && right.type == HML_VAL_STRING) {
            equal = (strcmp(left.as.as_string->data, right.as.as_string->data) == 0);
        } else if (hml_is_numeric(left) && hml_is_numeric(right)) {
            double l = hml_to_f64(left);
            double r = hml_to_f64(right);
            equal = (l == r);
        } else {
            equal = 0;  // Different types are not equal
        }
        return hml_val_bool(op == HML_OP_EQUAL ? equal : !equal);
    }

    // Numeric operations
    if (!hml_is_numeric(left) || !hml_is_numeric(right)) {
        fprintf(stderr, "Runtime error: Cannot perform numeric operation on non-numeric types\n");
        exit(1);
    }

    HmlValueType result_type = promote_types(left.type, right.type);

    // Float operations
    if (result_type == HML_VAL_F64 || result_type == HML_VAL_F32) {
        double l = hml_to_f64(left);
        double r = hml_to_f64(right);
        double result;

        switch (op) {
            case HML_OP_ADD:      result = l + r; break;
            case HML_OP_SUB:      result = l - r; break;
            case HML_OP_MUL:      result = l * r; break;
            case HML_OP_DIV:
                if (r == 0.0) {
                    fprintf(stderr, "Runtime error: Division by zero\n");
                    exit(1);
                }
                result = l / r;
                break;
            case HML_OP_LESS:         return hml_val_bool(l < r);
            case HML_OP_LESS_EQUAL:   return hml_val_bool(l <= r);
            case HML_OP_GREATER:      return hml_val_bool(l > r);
            case HML_OP_GREATER_EQUAL: return hml_val_bool(l >= r);
            default:
                fprintf(stderr, "Runtime error: Invalid operation for floats\n");
                exit(1);
        }
        return hml_val_f64(result);
    }

    // Integer operations
    int64_t l = hml_to_i64(left);
    int64_t r = hml_to_i64(right);

    switch (op) {
        case HML_OP_ADD:
            if (result_type == HML_VAL_I32) return hml_val_i32((int32_t)(l + r));
            return hml_val_i64(l + r);
        case HML_OP_SUB:
            if (result_type == HML_VAL_I32) return hml_val_i32((int32_t)(l - r));
            return hml_val_i64(l - r);
        case HML_OP_MUL:
            if (result_type == HML_VAL_I32) return hml_val_i32((int32_t)(l * r));
            return hml_val_i64(l * r);
        case HML_OP_DIV:
            if (r == 0) {
                fprintf(stderr, "Runtime error: Division by zero\n");
                exit(1);
            }
            if (result_type == HML_VAL_I32) return hml_val_i32((int32_t)(l / r));
            return hml_val_i64(l / r);
        case HML_OP_MOD:
            if (r == 0) {
                fprintf(stderr, "Runtime error: Division by zero\n");
                exit(1);
            }
            if (result_type == HML_VAL_I32) return hml_val_i32((int32_t)(l % r));
            return hml_val_i64(l % r);
        case HML_OP_LESS:         return hml_val_bool(l < r);
        case HML_OP_LESS_EQUAL:   return hml_val_bool(l <= r);
        case HML_OP_GREATER:      return hml_val_bool(l > r);
        case HML_OP_GREATER_EQUAL: return hml_val_bool(l >= r);
        case HML_OP_BIT_AND:
            if (result_type == HML_VAL_I32) return hml_val_i32((int32_t)(l & r));
            return hml_val_i64(l & r);
        case HML_OP_BIT_OR:
            if (result_type == HML_VAL_I32) return hml_val_i32((int32_t)(l | r));
            return hml_val_i64(l | r);
        case HML_OP_BIT_XOR:
            if (result_type == HML_VAL_I32) return hml_val_i32((int32_t)(l ^ r));
            return hml_val_i64(l ^ r);
        case HML_OP_LSHIFT:
            if (result_type == HML_VAL_I32) return hml_val_i32((int32_t)(l << r));
            return hml_val_i64(l << r);
        case HML_OP_RSHIFT:
            if (result_type == HML_VAL_I32) return hml_val_i32((int32_t)(l >> r));
            return hml_val_i64(l >> r);
        default:
            break;
    }

    fprintf(stderr, "Runtime error: Unknown binary operation\n");
    exit(1);
}

// ========== UNARY OPERATIONS ==========

HmlValue hml_unary_op(HmlUnaryOp op, HmlValue operand) {
    switch (op) {
        case HML_UNARY_NOT:
            return hml_val_bool(!hml_to_bool(operand));

        case HML_UNARY_NEGATE:
            if (!hml_is_numeric(operand)) {
                fprintf(stderr, "Runtime error: Cannot negate non-numeric type\n");
                exit(1);
            }
            if (operand.type == HML_VAL_F64) {
                return hml_val_f64(-operand.as.as_f64);
            } else if (operand.type == HML_VAL_F32) {
                return hml_val_f32(-operand.as.as_f32);
            } else if (operand.type == HML_VAL_I64) {
                return hml_val_i64(-operand.as.as_i64);
            } else {
                return hml_val_i32(-hml_to_i32(operand));
            }

        case HML_UNARY_BIT_NOT:
            if (!hml_is_integer(operand)) {
                fprintf(stderr, "Runtime error: Bitwise NOT requires integer type\n");
                exit(1);
            }
            if (operand.type == HML_VAL_I64) {
                return hml_val_i64(~operand.as.as_i64);
            } else if (operand.type == HML_VAL_U64) {
                return hml_val_u64(~operand.as.as_u64);
            } else {
                return hml_val_i32(~hml_to_i32(operand));
            }
    }

    return hml_val_null();
}

// ========== STRING OPERATIONS ==========

HmlValue hml_string_concat(HmlValue a, HmlValue b) {
    // Convert both to strings
    HmlValue str_a = hml_to_string(a);
    HmlValue str_b = hml_to_string(b);

    const char *s1 = hml_to_string_ptr(str_a);
    const char *s2 = hml_to_string_ptr(str_b);

    if (!s1) s1 = "";
    if (!s2) s2 = "";

    int len1 = strlen(s1);
    int len2 = strlen(s2);
    int total = len1 + len2;

    char *result = malloc(total + 1);
    memcpy(result, s1, len1);
    memcpy(result + len1, s2, len2);
    result[total] = '\0';

    // Release converted strings if they were newly created
    hml_release(&str_a);
    hml_release(&str_b);

    return hml_val_string_owned(result, total, total + 1);
}

HmlValue hml_to_string(HmlValue val) {
    if (val.type == HML_VAL_STRING) {
        hml_retain(&val);
        return val;
    }

    char buffer[256];
    switch (val.type) {
        case HML_VAL_I8:
            snprintf(buffer, sizeof(buffer), "%d", val.as.as_i8);
            break;
        case HML_VAL_I16:
            snprintf(buffer, sizeof(buffer), "%d", val.as.as_i16);
            break;
        case HML_VAL_I32:
            snprintf(buffer, sizeof(buffer), "%d", val.as.as_i32);
            break;
        case HML_VAL_I64:
            snprintf(buffer, sizeof(buffer), "%ld", val.as.as_i64);
            break;
        case HML_VAL_U8:
            snprintf(buffer, sizeof(buffer), "%u", val.as.as_u8);
            break;
        case HML_VAL_U16:
            snprintf(buffer, sizeof(buffer), "%u", val.as.as_u16);
            break;
        case HML_VAL_U32:
            snprintf(buffer, sizeof(buffer), "%u", val.as.as_u32);
            break;
        case HML_VAL_U64:
            snprintf(buffer, sizeof(buffer), "%lu", val.as.as_u64);
            break;
        case HML_VAL_F32:
            snprintf(buffer, sizeof(buffer), "%g", val.as.as_f32);
            break;
        case HML_VAL_F64:
            snprintf(buffer, sizeof(buffer), "%g", val.as.as_f64);
            break;
        case HML_VAL_BOOL:
            return hml_val_string(val.as.as_bool ? "true" : "false");
        case HML_VAL_NULL:
            return hml_val_string("null");
        case HML_VAL_RUNE:
            // Encode rune to UTF-8
            if (val.as.as_rune < 0x80) {
                buffer[0] = (char)val.as.as_rune;
                buffer[1] = '\0';
            } else if (val.as.as_rune < 0x800) {
                buffer[0] = (char)(0xC0 | (val.as.as_rune >> 6));
                buffer[1] = (char)(0x80 | (val.as.as_rune & 0x3F));
                buffer[2] = '\0';
            } else if (val.as.as_rune < 0x10000) {
                buffer[0] = (char)(0xE0 | (val.as.as_rune >> 12));
                buffer[1] = (char)(0x80 | ((val.as.as_rune >> 6) & 0x3F));
                buffer[2] = (char)(0x80 | (val.as.as_rune & 0x3F));
                buffer[3] = '\0';
            } else {
                buffer[0] = (char)(0xF0 | (val.as.as_rune >> 18));
                buffer[1] = (char)(0x80 | ((val.as.as_rune >> 12) & 0x3F));
                buffer[2] = (char)(0x80 | ((val.as.as_rune >> 6) & 0x3F));
                buffer[3] = (char)(0x80 | (val.as.as_rune & 0x3F));
                buffer[4] = '\0';
            }
            return hml_val_string(buffer);
        default:
            return hml_val_string("<value>");
    }

    return hml_val_string(buffer);
}

// ========== ARRAY OPERATIONS ==========

void hml_array_push(HmlValue arr, HmlValue val) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        fprintf(stderr, "Runtime error: push() requires array\n");
        exit(1);
    }

    HmlArray *a = arr.as.as_array;

    // Grow if needed
    if (a->length >= a->capacity) {
        int new_cap = (a->capacity == 0) ? 8 : a->capacity * 2;
        a->elements = realloc(a->elements, new_cap * sizeof(HmlValue));
        a->capacity = new_cap;
    }

    a->elements[a->length] = val;
    hml_retain(&a->elements[a->length]);
    a->length++;
}

HmlValue hml_array_get(HmlValue arr, HmlValue index) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        fprintf(stderr, "Runtime error: Index access requires array\n");
        exit(1);
    }

    int idx = hml_to_i32(index);
    HmlArray *a = arr.as.as_array;

    if (idx < 0 || idx >= a->length) {
        fprintf(stderr, "Runtime error: Array index %d out of bounds (length %d)\n", idx, a->length);
        exit(1);
    }

    HmlValue result = a->elements[idx];
    hml_retain(&result);
    return result;
}

void hml_array_set(HmlValue arr, HmlValue index, HmlValue val) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        fprintf(stderr, "Runtime error: Index assignment requires array\n");
        exit(1);
    }

    int idx = hml_to_i32(index);
    HmlArray *a = arr.as.as_array;

    if (idx < 0 || idx >= a->length) {
        fprintf(stderr, "Runtime error: Array index %d out of bounds (length %d)\n", idx, a->length);
        exit(1);
    }

    hml_release(&a->elements[idx]);
    a->elements[idx] = val;
    hml_retain(&a->elements[idx]);
}

HmlValue hml_array_length(HmlValue arr) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        return hml_val_i32(0);
    }
    return hml_val_i32(arr.as.as_array->length);
}

// ========== OBJECT OPERATIONS ==========

HmlValue hml_object_get_field(HmlValue obj, const char *field) {
    if (obj.type != HML_VAL_OBJECT || !obj.as.as_object) {
        fprintf(stderr, "Runtime error: Property access requires object\n");
        exit(1);
    }

    HmlObject *o = obj.as.as_object;
    for (int i = 0; i < o->num_fields; i++) {
        if (strcmp(o->field_names[i], field) == 0) {
            HmlValue result = o->field_values[i];
            hml_retain(&result);
            return result;
        }
    }

    return hml_val_null();  // Field not found
}

void hml_object_set_field(HmlValue obj, const char *field, HmlValue val) {
    if (obj.type != HML_VAL_OBJECT || !obj.as.as_object) {
        fprintf(stderr, "Runtime error: Property assignment requires object\n");
        exit(1);
    }

    HmlObject *o = obj.as.as_object;

    // Check if field exists
    for (int i = 0; i < o->num_fields; i++) {
        if (strcmp(o->field_names[i], field) == 0) {
            hml_release(&o->field_values[i]);
            o->field_values[i] = val;
            hml_retain(&o->field_values[i]);
            return;
        }
    }

    // Add new field
    if (o->num_fields >= o->capacity) {
        int new_cap = (o->capacity == 0) ? 4 : o->capacity * 2;
        o->field_names = realloc(o->field_names, new_cap * sizeof(char*));
        o->field_values = realloc(o->field_values, new_cap * sizeof(HmlValue));
        o->capacity = new_cap;
    }

    o->field_names[o->num_fields] = strdup(field);
    o->field_values[o->num_fields] = val;
    hml_retain(&o->field_values[o->num_fields]);
    o->num_fields++;
}

int hml_object_has_field(HmlValue obj, const char *field) {
    if (obj.type != HML_VAL_OBJECT || !obj.as.as_object) {
        return 0;
    }

    HmlObject *o = obj.as.as_object;
    for (int i = 0; i < o->num_fields; i++) {
        if (strcmp(o->field_names[i], field) == 0) {
            return 1;
        }
    }
    return 0;
}

// ========== EXCEPTION HANDLING ==========

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

void hml_throw(HmlValue exception_value) {
    if (!g_exception_stack || !g_exception_stack->is_active) {
        // Uncaught exception
        fprintf(stderr, "Uncaught exception: ");
        print_value_to(stderr, exception_value);
        fprintf(stderr, "\n");
        exit(1);
    }

    g_exception_stack->exception_value = exception_value;
    hml_retain(&g_exception_stack->exception_value);
    longjmp(g_exception_stack->exception_buf, 1);
}

HmlValue hml_exception_get_value(void) {
    if (g_exception_stack) {
        HmlValue v = g_exception_stack->exception_value;
        hml_retain(&v);
        return v;
    }
    return hml_val_null();
}

// ========== DEFER SUPPORT ==========

void hml_defer_push(HmlDeferFn fn, void *arg) {
    DeferEntry *entry = malloc(sizeof(DeferEntry));
    entry->fn = fn;
    entry->arg = arg;
    entry->next = g_defer_stack;
    g_defer_stack = entry;
}

void hml_defer_pop_and_execute(void) {
    if (g_defer_stack) {
        DeferEntry *entry = g_defer_stack;
        g_defer_stack = entry->next;
        entry->fn(entry->arg);
        free(entry);
    }
}

void hml_defer_execute_all(void) {
    while (g_defer_stack) {
        hml_defer_pop_and_execute();
    }
}

// ========== FUNCTION CALLS ==========

HmlValue hml_call_function(HmlValue fn, HmlValue *args, int num_args) {
    if (fn.type == HML_VAL_BUILTIN_FN) {
        return fn.as.as_builtin_fn(args, num_args);
    }

    if (fn.type == HML_VAL_FUNCTION && fn.as.as_function) {
        // Call the C function pointer stored in the function struct
        void *fn_ptr = fn.as.as_function->fn_ptr;
        if (fn_ptr == NULL) {
            fprintf(stderr, "Runtime error: Function pointer is NULL\n");
            exit(1);
        }

        // Cast to appropriate function type based on number of params
        switch (num_args) {
            case 0: {
                typedef HmlValue (*Fn0)(void);
                Fn0 f = (Fn0)fn_ptr;
                return f();
            }
            case 1: {
                typedef HmlValue (*Fn1)(HmlValue);
                Fn1 f = (Fn1)fn_ptr;
                return f(args[0]);
            }
            case 2: {
                typedef HmlValue (*Fn2)(HmlValue, HmlValue);
                Fn2 f = (Fn2)fn_ptr;
                return f(args[0], args[1]);
            }
            case 3: {
                typedef HmlValue (*Fn3)(HmlValue, HmlValue, HmlValue);
                Fn3 f = (Fn3)fn_ptr;
                return f(args[0], args[1], args[2]);
            }
            case 4: {
                typedef HmlValue (*Fn4)(HmlValue, HmlValue, HmlValue, HmlValue);
                Fn4 f = (Fn4)fn_ptr;
                return f(args[0], args[1], args[2], args[3]);
            }
            case 5: {
                typedef HmlValue (*Fn5)(HmlValue, HmlValue, HmlValue, HmlValue, HmlValue);
                Fn5 f = (Fn5)fn_ptr;
                return f(args[0], args[1], args[2], args[3], args[4]);
            }
            default:
                fprintf(stderr, "Runtime error: Functions with more than 5 arguments not supported\n");
                exit(1);
        }
    }

    fprintf(stderr, "Runtime error: Cannot call non-function value (type: %s)\n", hml_typeof_str(fn));
    exit(1);
}

// ========== MATH FUNCTIONS ==========

double hml_sin(double x) { return sin(x); }
double hml_cos(double x) { return cos(x); }
double hml_tan(double x) { return tan(x); }
double hml_sqrt(double x) { return sqrt(x); }
double hml_pow(double base, double exp) { return pow(base, exp); }
double hml_exp(double x) { return exp(x); }
double hml_log(double x) { return log(x); }
double hml_log10(double x) { return log10(x); }
double hml_floor(double x) { return floor(x); }
double hml_ceil(double x) { return ceil(x); }
double hml_round(double x) { return round(x); }
double hml_abs_f64(double x) { return fabs(x); }
int64_t hml_abs_i64(int64_t x) { return llabs(x); }
