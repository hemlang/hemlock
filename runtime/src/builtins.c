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

// ========== VALUE COMPARISON ==========

int hml_values_equal(HmlValue left, HmlValue right) {
    // Null comparison
    if (left.type == HML_VAL_NULL || right.type == HML_VAL_NULL) {
        return (left.type == HML_VAL_NULL && right.type == HML_VAL_NULL);
    }

    // Boolean comparison
    if (left.type == HML_VAL_BOOL && right.type == HML_VAL_BOOL) {
        return (left.as.as_bool == right.as.as_bool);
    }

    // String comparison
    if (left.type == HML_VAL_STRING && right.type == HML_VAL_STRING) {
        if (!left.as.as_string || !right.as.as_string) return 0;
        return (strcmp(left.as.as_string->data, right.as.as_string->data) == 0);
    }

    // Numeric comparison
    if (hml_is_numeric(left) && hml_is_numeric(right)) {
        double l = hml_to_f64(left);
        double r = hml_to_f64(right);
        return (l == r);
    }

    // Reference equality for arrays/objects
    if (left.type == HML_VAL_ARRAY && right.type == HML_VAL_ARRAY) {
        return (left.as.as_array == right.as.as_array);
    }
    if (left.type == HML_VAL_OBJECT && right.type == HML_VAL_OBJECT) {
        return (left.as.as_object == right.as.as_object);
    }

    // Different types are not equal
    return 0;
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

// ========== STRING METHODS ==========

HmlValue hml_string_length(HmlValue str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_i32(0);
    }
    HmlString *s = str.as.as_string;
    // For now, return byte length (UTF-8 codepoint counting can be added later)
    return hml_val_i32(s->length);
}

HmlValue hml_string_byte_length(HmlValue str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_i32(0);
    }
    return hml_val_i32(str.as.as_string->length);
}

HmlValue hml_string_char_at(HmlValue str, HmlValue index) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_null();
    }
    HmlString *s = str.as.as_string;
    int32_t idx = hml_to_i32(index);
    if (idx < 0 || idx >= s->length) {
        return hml_val_null();
    }
    // Return as rune (byte value for ASCII)
    return hml_val_rune((uint32_t)(unsigned char)s->data[idx]);
}

HmlValue hml_string_byte_at(HmlValue str, HmlValue index) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_null();
    }
    HmlString *s = str.as.as_string;
    int32_t idx = hml_to_i32(index);
    if (idx < 0 || idx >= s->length) {
        return hml_val_null();
    }
    return hml_val_u8((uint8_t)s->data[idx]);
}

HmlValue hml_string_substr(HmlValue str, HmlValue start, HmlValue length) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_string("");
    }
    HmlString *s = str.as.as_string;
    int32_t start_idx = hml_to_i32(start);
    int32_t len = hml_to_i32(length);

    // Clamp bounds
    if (start_idx < 0) start_idx = 0;
    if (start_idx >= s->length) return hml_val_string("");
    if (len < 0) len = 0;
    if (start_idx + len > s->length) len = s->length - start_idx;

    char *result = malloc(len + 1);
    memcpy(result, s->data + start_idx, len);
    result[len] = '\0';
    return hml_val_string_owned(result, len, len + 1);
}

HmlValue hml_string_slice(HmlValue str, HmlValue start, HmlValue end) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_string("");
    }
    HmlString *s = str.as.as_string;
    int32_t start_idx = hml_to_i32(start);
    int32_t end_idx = hml_to_i32(end);

    // Clamp bounds
    if (start_idx < 0) start_idx = 0;
    if (start_idx > s->length) start_idx = s->length;
    if (end_idx < start_idx) end_idx = start_idx;
    if (end_idx > s->length) end_idx = s->length;

    int len = end_idx - start_idx;
    char *result = malloc(len + 1);
    memcpy(result, s->data + start_idx, len);
    result[len] = '\0';
    return hml_val_string_owned(result, len, len + 1);
}

HmlValue hml_string_find(HmlValue str, HmlValue needle) {
    if (str.type != HML_VAL_STRING || !str.as.as_string ||
        needle.type != HML_VAL_STRING || !needle.as.as_string) {
        return hml_val_i32(-1);
    }
    HmlString *s = str.as.as_string;
    HmlString *n = needle.as.as_string;

    if (n->length == 0) return hml_val_i32(0);
    if (n->length > s->length) return hml_val_i32(-1);

    for (int i = 0; i <= s->length - n->length; i++) {
        if (memcmp(s->data + i, n->data, n->length) == 0) {
            return hml_val_i32(i);
        }
    }
    return hml_val_i32(-1);
}

HmlValue hml_string_contains(HmlValue str, HmlValue needle) {
    HmlValue pos = hml_string_find(str, needle);
    return hml_val_bool(pos.as.as_i32 >= 0);
}

HmlValue hml_string_split(HmlValue str, HmlValue delimiter) {
    HmlValue result = hml_val_array();

    if (str.type != HML_VAL_STRING || !str.as.as_string ||
        delimiter.type != HML_VAL_STRING || !delimiter.as.as_string) {
        return result;
    }

    HmlString *s = str.as.as_string;
    HmlString *d = delimiter.as.as_string;

    if (d->length == 0) {
        // Split into individual characters
        for (int i = 0; i < s->length; i++) {
            char *c = malloc(2);
            c[0] = s->data[i];
            c[1] = '\0';
            HmlValue part_val = hml_val_string_owned(c, 1, 2);
            hml_array_push(result, part_val);
        }
        return result;
    }

    int start = 0;
    for (int i = 0; i <= s->length - d->length; i++) {
        if (memcmp(s->data + i, d->data, d->length) == 0) {
            int len = i - start;
            char *part = malloc(len + 1);
            memcpy(part, s->data + start, len);
            part[len] = '\0';
            HmlValue part_val = hml_val_string_owned(part, len, len + 1);
            hml_array_push(result, part_val);
            i += d->length - 1;
            start = i + 1;
        }
    }

    // Add remaining part
    int len = s->length - start;
    char *part = malloc(len + 1);
    memcpy(part, s->data + start, len);
    part[len] = '\0';
    HmlValue part_val = hml_val_string_owned(part, len, len + 1);
    hml_array_push(result, part_val);

    return result;
}

HmlValue hml_string_trim(HmlValue str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_string("");
    }
    HmlString *s = str.as.as_string;
    int start = 0;
    int end = s->length - 1;

    while (start < s->length && (s->data[start] == ' ' || s->data[start] == '\t' ||
                                  s->data[start] == '\n' || s->data[start] == '\r')) {
        start++;
    }

    while (end >= start && (s->data[end] == ' ' || s->data[end] == '\t' ||
                            s->data[end] == '\n' || s->data[end] == '\r')) {
        end--;
    }

    int len = end - start + 1;
    if (len <= 0) return hml_val_string("");

    char *result = malloc(len + 1);
    memcpy(result, s->data + start, len);
    result[len] = '\0';
    return hml_val_string_owned(result, len, len + 1);
}

HmlValue hml_string_to_upper(HmlValue str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_string("");
    }
    HmlString *s = str.as.as_string;
    char *result = malloc(s->length + 1);
    for (int i = 0; i < s->length; i++) {
        char c = s->data[i];
        if (c >= 'a' && c <= 'z') {
            result[i] = c - 32;
        } else {
            result[i] = c;
        }
    }
    result[s->length] = '\0';
    return hml_val_string_owned(result, s->length, s->length + 1);
}

HmlValue hml_string_to_lower(HmlValue str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_string("");
    }
    HmlString *s = str.as.as_string;
    char *result = malloc(s->length + 1);
    for (int i = 0; i < s->length; i++) {
        char c = s->data[i];
        if (c >= 'A' && c <= 'Z') {
            result[i] = c + 32;
        } else {
            result[i] = c;
        }
    }
    result[s->length] = '\0';
    return hml_val_string_owned(result, s->length, s->length + 1);
}

HmlValue hml_string_starts_with(HmlValue str, HmlValue prefix) {
    if (str.type != HML_VAL_STRING || !str.as.as_string ||
        prefix.type != HML_VAL_STRING || !prefix.as.as_string) {
        return hml_val_bool(0);
    }
    HmlString *s = str.as.as_string;
    HmlString *p = prefix.as.as_string;
    if (p->length > s->length) return hml_val_bool(0);
    return hml_val_bool(memcmp(s->data, p->data, p->length) == 0);
}

HmlValue hml_string_ends_with(HmlValue str, HmlValue suffix) {
    if (str.type != HML_VAL_STRING || !str.as.as_string ||
        suffix.type != HML_VAL_STRING || !suffix.as.as_string) {
        return hml_val_bool(0);
    }
    HmlString *s = str.as.as_string;
    HmlString *p = suffix.as.as_string;
    if (p->length > s->length) return hml_val_bool(0);
    int offset = s->length - p->length;
    return hml_val_bool(memcmp(s->data + offset, p->data, p->length) == 0);
}

HmlValue hml_string_replace(HmlValue str, HmlValue old, HmlValue new_str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string ||
        old.type != HML_VAL_STRING || !old.as.as_string ||
        new_str.type != HML_VAL_STRING || !new_str.as.as_string) {
        return str;
    }
    HmlString *s = str.as.as_string;
    HmlString *o = old.as.as_string;
    HmlString *n = new_str.as.as_string;

    if (o->length == 0) {
        hml_retain(&str);
        return str;
    }

    // Find first occurrence
    int pos = -1;
    for (int i = 0; i <= s->length - o->length; i++) {
        if (memcmp(s->data + i, o->data, o->length) == 0) {
            pos = i;
            break;
        }
    }

    if (pos == -1) {
        hml_retain(&str);
        return str;
    }

    int new_len = s->length - o->length + n->length;
    char *result = malloc(new_len + 1);
    memcpy(result, s->data, pos);
    memcpy(result + pos, n->data, n->length);
    memcpy(result + pos + n->length, s->data + pos + o->length, s->length - pos - o->length);
    result[new_len] = '\0';

    return hml_val_string_owned(result, new_len, new_len + 1);
}

HmlValue hml_string_replace_all(HmlValue str, HmlValue old, HmlValue new_str) {
    if (str.type != HML_VAL_STRING || !str.as.as_string ||
        old.type != HML_VAL_STRING || !old.as.as_string ||
        new_str.type != HML_VAL_STRING || !new_str.as.as_string) {
        return str;
    }
    HmlString *s = str.as.as_string;
    HmlString *o = old.as.as_string;
    HmlString *n = new_str.as.as_string;

    if (o->length == 0) {
        hml_retain(&str);
        return str;
    }

    // Count occurrences
    int count = 0;
    for (int i = 0; i <= s->length - o->length; i++) {
        if (memcmp(s->data + i, o->data, o->length) == 0) {
            count++;
            i += o->length - 1;
        }
    }

    if (count == 0) {
        hml_retain(&str);
        return str;
    }

    int new_len = s->length - (count * o->length) + (count * n->length);
    char *result = malloc(new_len + 1);
    int result_pos = 0;

    for (int i = 0; i < s->length; ) {
        if (i <= s->length - o->length && memcmp(s->data + i, o->data, o->length) == 0) {
            memcpy(result + result_pos, n->data, n->length);
            result_pos += n->length;
            i += o->length;
        } else {
            result[result_pos++] = s->data[i++];
        }
    }
    result[new_len] = '\0';

    return hml_val_string_owned(result, new_len, new_len + 1);
}

HmlValue hml_string_repeat(HmlValue str, HmlValue count) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        return hml_val_string("");
    }
    HmlString *s = str.as.as_string;
    int32_t n = hml_to_i32(count);
    if (n <= 0) return hml_val_string("");

    int new_len = s->length * n;
    char *result = malloc(new_len + 1);
    for (int i = 0; i < n; i++) {
        memcpy(result + i * s->length, s->data, s->length);
    }
    result[new_len] = '\0';

    return hml_val_string_owned(result, new_len, new_len + 1);
}

// String indexing (returns char at position as rune)
HmlValue hml_string_index(HmlValue str, HmlValue index) {
    return hml_string_char_at(str, index);
}

void hml_string_index_assign(HmlValue str, HmlValue index, HmlValue rune) {
    if (str.type != HML_VAL_STRING || !str.as.as_string) {
        fprintf(stderr, "Runtime error: String index assignment requires string\n");
        exit(1);
    }
    if (rune.type != HML_VAL_RUNE) {
        fprintf(stderr, "Runtime error: String index assignment requires rune value\n");
        exit(1);
    }

    int idx = hml_to_i32(index);
    HmlString *s = str.as.as_string;

    if (idx < 0 || idx >= s->length) {
        fprintf(stderr, "Runtime error: String index %d out of bounds\n", idx);
        exit(1);
    }

    // For simplicity, only support single-byte characters in assignment
    // Full UTF-8 would require resizing the string
    if (rune.as.as_rune < 128) {
        s->data[idx] = (char)rune.as.as_rune;
    } else {
        fprintf(stderr, "Runtime error: String assignment of multi-byte runes not yet supported\n");
        exit(1);
    }
}

// Buffer indexing
HmlValue hml_buffer_get(HmlValue buf, HmlValue index) {
    if (buf.type != HML_VAL_BUFFER || !buf.as.as_buffer) {
        fprintf(stderr, "Runtime error: Buffer index requires buffer\n");
        exit(1);
    }

    int idx = hml_to_i32(index);
    HmlBuffer *b = buf.as.as_buffer;

    if (idx < 0 || idx >= b->length) {
        fprintf(stderr, "Runtime error: Buffer index %d out of bounds (length %d)\n", idx, b->length);
        exit(1);
    }

    uint8_t *data = (uint8_t *)b->data;
    return hml_val_u8(data[idx]);
}

void hml_buffer_set(HmlValue buf, HmlValue index, HmlValue val) {
    if (buf.type != HML_VAL_BUFFER || !buf.as.as_buffer) {
        fprintf(stderr, "Runtime error: Buffer index assignment requires buffer\n");
        exit(1);
    }

    int idx = hml_to_i32(index);
    HmlBuffer *b = buf.as.as_buffer;

    if (idx < 0 || idx >= b->length) {
        fprintf(stderr, "Runtime error: Buffer index %d out of bounds (length %d)\n", idx, b->length);
        exit(1);
    }

    uint8_t *data = (uint8_t *)b->data;
    data[idx] = (uint8_t)hml_to_i32(val);
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

HmlValue hml_array_pop(HmlValue arr) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        fprintf(stderr, "Runtime error: pop() requires array\n");
        exit(1);
    }

    HmlArray *a = arr.as.as_array;
    if (a->length == 0) {
        return hml_val_null();
    }

    HmlValue result = a->elements[a->length - 1];
    // Don't release - we're transferring ownership
    a->length--;
    return result;
}

HmlValue hml_array_shift(HmlValue arr) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        fprintf(stderr, "Runtime error: shift() requires array\n");
        exit(1);
    }

    HmlArray *a = arr.as.as_array;
    if (a->length == 0) {
        return hml_val_null();
    }

    HmlValue result = a->elements[0];
    // Shift elements left
    for (int i = 0; i < a->length - 1; i++) {
        a->elements[i] = a->elements[i + 1];
    }
    a->length--;
    return result;
}

void hml_array_unshift(HmlValue arr, HmlValue val) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        fprintf(stderr, "Runtime error: unshift() requires array\n");
        exit(1);
    }

    HmlArray *a = arr.as.as_array;

    // Grow if needed
    if (a->length >= a->capacity) {
        int new_cap = (a->capacity == 0) ? 8 : a->capacity * 2;
        a->elements = realloc(a->elements, new_cap * sizeof(HmlValue));
        a->capacity = new_cap;
    }

    // Shift elements right
    for (int i = a->length; i > 0; i--) {
        a->elements[i] = a->elements[i - 1];
    }

    a->elements[0] = val;
    hml_retain(&a->elements[0]);
    a->length++;
}

void hml_array_insert(HmlValue arr, HmlValue index, HmlValue val) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        fprintf(stderr, "Runtime error: insert() requires array\n");
        exit(1);
    }

    int idx = hml_to_i32(index);
    HmlArray *a = arr.as.as_array;

    if (idx < 0 || idx > a->length) {
        fprintf(stderr, "Runtime error: insert index %d out of bounds (length %d)\n", idx, a->length);
        exit(1);
    }

    // Grow if needed
    if (a->length >= a->capacity) {
        int new_cap = (a->capacity == 0) ? 8 : a->capacity * 2;
        a->elements = realloc(a->elements, new_cap * sizeof(HmlValue));
        a->capacity = new_cap;
    }

    // Shift elements right from idx
    for (int i = a->length; i > idx; i--) {
        a->elements[i] = a->elements[i - 1];
    }

    a->elements[idx] = val;
    hml_retain(&a->elements[idx]);
    a->length++;
}

HmlValue hml_array_remove(HmlValue arr, HmlValue index) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        fprintf(stderr, "Runtime error: remove() requires array\n");
        exit(1);
    }

    int idx = hml_to_i32(index);
    HmlArray *a = arr.as.as_array;

    if (idx < 0 || idx >= a->length) {
        fprintf(stderr, "Runtime error: remove index %d out of bounds (length %d)\n", idx, a->length);
        exit(1);
    }

    HmlValue result = a->elements[idx];
    // Shift elements left
    for (int i = idx; i < a->length - 1; i++) {
        a->elements[i] = a->elements[i + 1];
    }
    a->length--;
    return result;
}

HmlValue hml_array_find(HmlValue arr, HmlValue val) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        fprintf(stderr, "Runtime error: find() requires array\n");
        exit(1);
    }

    HmlArray *a = arr.as.as_array;
    for (int i = 0; i < a->length; i++) {
        if (hml_values_equal(a->elements[i], val)) {
            return hml_val_i32(i);
        }
    }
    return hml_val_i32(-1);
}

HmlValue hml_array_contains(HmlValue arr, HmlValue val) {
    HmlValue idx = hml_array_find(arr, val);
    return hml_val_bool(idx.as.as_i32 >= 0);
}

HmlValue hml_array_slice(HmlValue arr, HmlValue start, HmlValue end) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        fprintf(stderr, "Runtime error: slice() requires array\n");
        exit(1);
    }

    HmlArray *a = arr.as.as_array;
    int s = hml_to_i32(start);
    int e = hml_to_i32(end);

    // Clamp values
    if (s < 0) s = 0;
    if (e > a->length) e = a->length;
    if (s > e) s = e;

    int new_len = e - s;
    HmlArray *result = malloc(sizeof(HmlArray));
    result->ref_count = 1;
    result->length = new_len;
    result->capacity = (new_len > 0) ? new_len : 1;
    result->elements = malloc(result->capacity * sizeof(HmlValue));
    result->element_type = HML_VAL_NULL;

    for (int i = 0; i < new_len; i++) {
        result->elements[i] = a->elements[s + i];
        hml_retain(&result->elements[i]);
    }

    HmlValue val;
    val.type = HML_VAL_ARRAY;
    val.as.as_array = result;
    return val;
}

HmlValue hml_array_join(HmlValue arr, HmlValue delimiter) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        fprintf(stderr, "Runtime error: join() requires array\n");
        exit(1);
    }
    if (delimiter.type != HML_VAL_STRING) {
        fprintf(stderr, "Runtime error: join() requires string delimiter\n");
        exit(1);
    }

    HmlArray *a = arr.as.as_array;
    const char *delim = delimiter.as.as_string->data;
    int delim_len = delimiter.as.as_string->length;

    if (a->length == 0) {
        return hml_val_string("");
    }

    // Calculate total length
    int total_len = 0;
    for (int i = 0; i < a->length; i++) {
        HmlValue str = hml_to_string(a->elements[i]);
        total_len += str.as.as_string->length;
        if (i < a->length - 1) {
            total_len += delim_len;
        }
        hml_release(&str);
    }

    char *result = malloc(total_len + 1);
    int pos = 0;
    for (int i = 0; i < a->length; i++) {
        HmlValue str = hml_to_string(a->elements[i]);
        memcpy(result + pos, str.as.as_string->data, str.as.as_string->length);
        pos += str.as.as_string->length;
        hml_release(&str);
        if (i < a->length - 1) {
            memcpy(result + pos, delim, delim_len);
            pos += delim_len;
        }
    }
    result[total_len] = '\0';

    return hml_val_string_owned(result, total_len, total_len + 1);
}

HmlValue hml_array_concat(HmlValue arr1, HmlValue arr2) {
    if (arr1.type != HML_VAL_ARRAY || !arr1.as.as_array) {
        fprintf(stderr, "Runtime error: concat() requires array\n");
        exit(1);
    }
    if (arr2.type != HML_VAL_ARRAY || !arr2.as.as_array) {
        fprintf(stderr, "Runtime error: concat() requires array argument\n");
        exit(1);
    }

    HmlArray *a1 = arr1.as.as_array;
    HmlArray *a2 = arr2.as.as_array;
    int new_len = a1->length + a2->length;

    HmlArray *result = malloc(sizeof(HmlArray));
    result->ref_count = 1;
    result->length = new_len;
    result->capacity = (new_len > 0) ? new_len : 1;
    result->elements = malloc(result->capacity * sizeof(HmlValue));
    result->element_type = HML_VAL_NULL;

    for (int i = 0; i < a1->length; i++) {
        result->elements[i] = a1->elements[i];
        hml_retain(&result->elements[i]);
    }
    for (int i = 0; i < a2->length; i++) {
        result->elements[a1->length + i] = a2->elements[i];
        hml_retain(&result->elements[a1->length + i]);
    }

    HmlValue val;
    val.type = HML_VAL_ARRAY;
    val.as.as_array = result;
    return val;
}

void hml_array_reverse(HmlValue arr) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        fprintf(stderr, "Runtime error: reverse() requires array\n");
        exit(1);
    }

    HmlArray *a = arr.as.as_array;
    for (int i = 0; i < a->length / 2; i++) {
        HmlValue tmp = a->elements[i];
        a->elements[i] = a->elements[a->length - 1 - i];
        a->elements[a->length - 1 - i] = tmp;
    }
}

HmlValue hml_array_first(HmlValue arr) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        fprintf(stderr, "Runtime error: first() requires array\n");
        exit(1);
    }

    HmlArray *a = arr.as.as_array;
    if (a->length == 0) {
        return hml_val_null();
    }

    HmlValue result = a->elements[0];
    hml_retain(&result);
    return result;
}

HmlValue hml_array_last(HmlValue arr) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        fprintf(stderr, "Runtime error: last() requires array\n");
        exit(1);
    }

    HmlArray *a = arr.as.as_array;
    if (a->length == 0) {
        return hml_val_null();
    }

    HmlValue result = a->elements[a->length - 1];
    hml_retain(&result);
    return result;
}

void hml_array_clear(HmlValue arr) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        fprintf(stderr, "Runtime error: clear() requires array\n");
        exit(1);
    }

    HmlArray *a = arr.as.as_array;
    for (int i = 0; i < a->length; i++) {
        hml_release(&a->elements[i]);
    }
    a->length = 0;
}

// ========== HIGHER-ORDER ARRAY FUNCTIONS ==========

HmlValue hml_array_map(HmlValue arr, HmlValue callback) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        fprintf(stderr, "Runtime error: map() requires array\n");
        exit(1);
    }

    HmlArray *a = arr.as.as_array;
    HmlValue result = hml_val_array();

    for (int i = 0; i < a->length; i++) {
        HmlValue args[1] = { a->elements[i] };
        HmlValue mapped = hml_call_function(callback, args, 1);
        hml_array_push(result, mapped);
        hml_release(&mapped);
    }

    return result;
}

HmlValue hml_array_filter(HmlValue arr, HmlValue predicate) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        fprintf(stderr, "Runtime error: filter() requires array\n");
        exit(1);
    }

    HmlArray *a = arr.as.as_array;
    HmlValue result = hml_val_array();

    for (int i = 0; i < a->length; i++) {
        HmlValue args[1] = { a->elements[i] };
        HmlValue keep = hml_call_function(predicate, args, 1);
        if (hml_to_bool(keep)) {
            HmlValue elem = a->elements[i];
            hml_retain(&elem);
            hml_array_push(result, elem);
            hml_release(&elem);
        }
        hml_release(&keep);
    }

    return result;
}

HmlValue hml_array_reduce(HmlValue arr, HmlValue reducer, HmlValue initial) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        fprintf(stderr, "Runtime error: reduce() requires array\n");
        exit(1);
    }

    HmlArray *a = arr.as.as_array;

    // Handle empty array
    if (a->length == 0) {
        if (initial.type == HML_VAL_NULL) {
            fprintf(stderr, "Runtime error: reduce() of empty array with no initial value\n");
            exit(1);
        }
        hml_retain(&initial);
        return initial;
    }

    // Determine starting accumulator and index
    HmlValue acc;
    int start_idx;
    if (initial.type == HML_VAL_NULL) {
        // No initial value - use first element
        acc = a->elements[0];
        hml_retain(&acc);
        start_idx = 1;
    } else {
        acc = initial;
        hml_retain(&acc);
        start_idx = 0;
    }

    // Reduce
    for (int i = start_idx; i < a->length; i++) {
        HmlValue args[2] = { acc, a->elements[i] };
        HmlValue new_acc = hml_call_function(reducer, args, 2);
        hml_release(&acc);
        acc = new_acc;
    }

    return acc;
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

        // Check if this is a closure (has environment)
        void *closure_env = fn.as.as_function->closure_env;
        int num_params = fn.as.as_function->num_params;

        // Build args array with nulls for missing optional parameters
        // Use num_params (from function definition) to determine how many args to pass
        HmlValue padded_args[6] = {0};  // Max 5 params + 1 for safety
        for (int i = 0; i < num_params && i < 5; i++) {
            if (i < num_args) {
                padded_args[i] = args[i];
            } else {
                padded_args[i] = hml_val_null();  // Fill missing with null for optional params
            }
        }

        // Use num_params for the function signature since that's how many params it expects
        HmlClosureEnv *env = closure_env ? (HmlClosureEnv*)closure_env : NULL;

        switch (num_params) {
            case 0: {
                typedef HmlValue (*Fn0)(HmlClosureEnv*);
                Fn0 f = (Fn0)fn_ptr;
                return f(env);
            }
            case 1: {
                typedef HmlValue (*Fn1)(HmlClosureEnv*, HmlValue);
                Fn1 f = (Fn1)fn_ptr;
                return f(env, padded_args[0]);
            }
            case 2: {
                typedef HmlValue (*Fn2)(HmlClosureEnv*, HmlValue, HmlValue);
                Fn2 f = (Fn2)fn_ptr;
                return f(env, padded_args[0], padded_args[1]);
            }
            case 3: {
                typedef HmlValue (*Fn3)(HmlClosureEnv*, HmlValue, HmlValue, HmlValue);
                Fn3 f = (Fn3)fn_ptr;
                return f(env, padded_args[0], padded_args[1], padded_args[2]);
            }
            case 4: {
                typedef HmlValue (*Fn4)(HmlClosureEnv*, HmlValue, HmlValue, HmlValue, HmlValue);
                Fn4 f = (Fn4)fn_ptr;
                return f(env, padded_args[0], padded_args[1], padded_args[2], padded_args[3]);
            }
            case 5: {
                typedef HmlValue (*Fn5)(HmlClosureEnv*, HmlValue, HmlValue, HmlValue, HmlValue, HmlValue);
                Fn5 f = (Fn5)fn_ptr;
                return f(env, padded_args[0], padded_args[1], padded_args[2], padded_args[3], padded_args[4]);
            }
            default:
                fprintf(stderr, "Runtime error: Functions with more than 5 arguments not supported\n");
                exit(1);
        }
    }

    fprintf(stderr, "Runtime error: Cannot call non-function value (type: %s)\n", hml_typeof_str(fn));
    exit(1);
}

// Thread-local self for method calls
__thread HmlValue hml_self = {0};

HmlValue hml_call_method(HmlValue obj, const char *method, HmlValue *args, int num_args) {
    if (obj.type != HML_VAL_OBJECT || !obj.as.as_object) {
        fprintf(stderr, "Runtime error: Cannot call method on non-object\n");
        exit(1);
    }

    // Get the method function from the object
    HmlValue fn = hml_object_get_field(obj, method);
    if (fn.type == HML_VAL_NULL) {
        fprintf(stderr, "Runtime error: Object has no method '%s'\n", method);
        exit(1);
    }

    // Save previous self and set new one
    HmlValue prev_self = hml_self;
    hml_self = obj;
    hml_retain(&hml_self);

    // Call the method
    HmlValue result = hml_call_function(fn, args, num_args);

    // Restore previous self
    hml_release(&hml_self);
    hml_self = prev_self;

    hml_release(&fn);
    return result;
}

// ========== FILE I/O ==========

HmlValue hml_open(HmlValue path, HmlValue mode) {
    if (path.type != HML_VAL_STRING) {
        fprintf(stderr, "Error: open() expects string path\n");
        exit(1);
    }

    const char *path_str = path.as.as_string->data;
    const char *mode_str = "r";

    if (mode.type == HML_VAL_STRING) {
        mode_str = mode.as.as_string->data;
    }

    FILE *fp = fopen(path_str, mode_str);
    if (!fp) {
        fprintf(stderr, "Error: Failed to open '%s'\n", path_str);
        exit(1);
    }

    HmlFileHandle *fh = malloc(sizeof(HmlFileHandle));
    fh->fp = fp;
    fh->path = strdup(path_str);
    fh->mode = strdup(mode_str);
    fh->closed = 0;

    HmlValue result;
    result.type = HML_VAL_FILE;
    result.as.as_file = fh;
    return result;
}

HmlValue hml_file_read(HmlValue file, HmlValue size) {
    if (file.type != HML_VAL_FILE) {
        fprintf(stderr, "Error: read() expects file object\n");
        exit(1);
    }

    HmlFileHandle *fh = file.as.as_file;
    if (fh->closed) {
        fprintf(stderr, "Error: Cannot read from closed file '%s'\n", fh->path);
        exit(1);
    }

    int32_t read_size = 0;
    if (size.type == HML_VAL_I32) {
        read_size = size.as.as_i32;
    } else if (size.type == HML_VAL_I64) {
        read_size = (int32_t)size.as.as_i64;
    }

    if (read_size <= 0) {
        return hml_file_read_all(file);
    }

    char *buffer = malloc(read_size + 1);
    size_t bytes_read = fread(buffer, 1, read_size, (FILE*)fh->fp);
    buffer[bytes_read] = '\0';

    HmlValue result = hml_val_string(buffer);
    free(buffer);
    return result;
}

HmlValue hml_file_read_all(HmlValue file) {
    if (file.type != HML_VAL_FILE) {
        fprintf(stderr, "Error: read() expects file object\n");
        exit(1);
    }

    HmlFileHandle *fh = file.as.as_file;
    if (fh->closed) {
        fprintf(stderr, "Error: Cannot read from closed file '%s'\n", fh->path);
        exit(1);
    }

    FILE *fp = (FILE*)fh->fp;
    long start_pos = ftell(fp);

    fseek(fp, 0, SEEK_END);
    long end_pos = ftell(fp);
    fseek(fp, start_pos, SEEK_SET);

    long size = end_pos - start_pos;
    char *buffer = malloc(size + 1);
    size_t bytes_read = fread(buffer, 1, size, fp);
    buffer[bytes_read] = '\0';

    HmlValue result = hml_val_string(buffer);
    free(buffer);
    return result;
}

HmlValue hml_file_write(HmlValue file, HmlValue data) {
    if (file.type != HML_VAL_FILE) {
        fprintf(stderr, "Error: write() expects file object\n");
        exit(1);
    }

    HmlFileHandle *fh = file.as.as_file;
    if (fh->closed) {
        fprintf(stderr, "Error: Cannot write to closed file '%s'\n", fh->path);
        exit(1);
    }

    const char *str = "";
    if (data.type == HML_VAL_STRING) {
        str = data.as.as_string->data;
    }

    size_t bytes_written = fwrite(str, 1, strlen(str), (FILE*)fh->fp);
    return hml_val_i32((int32_t)bytes_written);
}

HmlValue hml_file_seek(HmlValue file, HmlValue position) {
    if (file.type != HML_VAL_FILE) {
        fprintf(stderr, "Error: seek() expects file object\n");
        exit(1);
    }

    HmlFileHandle *fh = file.as.as_file;
    if (fh->closed) {
        fprintf(stderr, "Error: Cannot seek in closed file '%s'\n", fh->path);
        exit(1);
    }

    long pos = 0;
    if (position.type == HML_VAL_I32) {
        pos = position.as.as_i32;
    } else if (position.type == HML_VAL_I64) {
        pos = (long)position.as.as_i64;
    }

    fseek((FILE*)fh->fp, pos, SEEK_SET);
    return hml_val_i32((int32_t)ftell((FILE*)fh->fp));
}

HmlValue hml_file_tell(HmlValue file) {
    if (file.type != HML_VAL_FILE) {
        fprintf(stderr, "Error: tell() expects file object\n");
        exit(1);
    }

    HmlFileHandle *fh = file.as.as_file;
    if (fh->closed) {
        fprintf(stderr, "Error: Cannot tell position in closed file '%s'\n", fh->path);
        exit(1);
    }

    return hml_val_i32((int32_t)ftell((FILE*)fh->fp));
}

void hml_file_close(HmlValue file) {
    if (file.type != HML_VAL_FILE) {
        return;
    }

    HmlFileHandle *fh = file.as.as_file;
    if (!fh->closed) {
        fclose((FILE*)fh->fp);
        fh->closed = 1;
    }
}

// ========== ASYNC / CONCURRENCY ==========

#include <pthread.h>
#include <stdatomic.h>

static atomic_int g_next_task_id = 1;

// Thread wrapper function
static void* task_thread_wrapper(void* arg) {
    HmlTask *task = (HmlTask*)arg;

    // Mark as running
    pthread_mutex_lock((pthread_mutex_t*)task->mutex);
    task->state = HML_TASK_RUNNING;
    pthread_mutex_unlock((pthread_mutex_t*)task->mutex);

    // Get function info
    HmlFunction *fn = task->function.as.as_function;
    void *fn_ptr = fn->fn_ptr;
    void *closure_env = fn->closure_env;

    // Call function with arguments based on num_args
    HmlValue result;
    typedef HmlValue (*Fn0)(void*);
    typedef HmlValue (*Fn1)(void*, HmlValue);
    typedef HmlValue (*Fn2)(void*, HmlValue, HmlValue);
    typedef HmlValue (*Fn3)(void*, HmlValue, HmlValue, HmlValue);
    typedef HmlValue (*Fn4)(void*, HmlValue, HmlValue, HmlValue, HmlValue);
    typedef HmlValue (*Fn5)(void*, HmlValue, HmlValue, HmlValue, HmlValue, HmlValue);

    switch (task->num_args) {
        case 0:
            result = ((Fn0)fn_ptr)(closure_env);
            break;
        case 1:
            result = ((Fn1)fn_ptr)(closure_env, task->args[0]);
            break;
        case 2:
            result = ((Fn2)fn_ptr)(closure_env, task->args[0], task->args[1]);
            break;
        case 3:
            result = ((Fn3)fn_ptr)(closure_env, task->args[0], task->args[1], task->args[2]);
            break;
        case 4:
            result = ((Fn4)fn_ptr)(closure_env, task->args[0], task->args[1], task->args[2], task->args[3]);
            break;
        case 5:
            result = ((Fn5)fn_ptr)(closure_env, task->args[0], task->args[1], task->args[2], task->args[3], task->args[4]);
            break;
        default:
            result = hml_val_null();
            break;
    }

    // Store result and mark as completed
    pthread_mutex_lock((pthread_mutex_t*)task->mutex);
    task->result = result;
    task->state = HML_TASK_COMPLETED;
    pthread_cond_signal((pthread_cond_t*)task->cond);
    pthread_mutex_unlock((pthread_mutex_t*)task->mutex);

    return NULL;
}

HmlValue hml_spawn(HmlValue fn, HmlValue *args, int num_args) {
    if (fn.type != HML_VAL_FUNCTION) {
        fprintf(stderr, "Error: spawn() expects a function\n");
        exit(1);
    }

    // Create task
    HmlTask *task = malloc(sizeof(HmlTask));
    task->id = atomic_fetch_add(&g_next_task_id, 1);
    task->state = HML_TASK_READY;
    task->result = hml_val_null();
    task->joined = 0;
    task->detached = 0;
    task->ref_count = 1;

    // Store function and args
    task->function = fn;
    hml_retain(&task->function);
    task->num_args = num_args;
    if (num_args > 0) {
        task->args = malloc(sizeof(HmlValue) * num_args);
        for (int i = 0; i < num_args; i++) {
            task->args[i] = args[i];
            hml_retain(&task->args[i]);
        }
    } else {
        task->args = NULL;
    }

    // Initialize mutex and condition variable
    task->mutex = malloc(sizeof(pthread_mutex_t));
    task->cond = malloc(sizeof(pthread_cond_t));
    pthread_mutex_init((pthread_mutex_t*)task->mutex, NULL);
    pthread_cond_init((pthread_cond_t*)task->cond, NULL);

    // Create thread
    task->thread = malloc(sizeof(pthread_t));
    pthread_create((pthread_t*)task->thread, NULL, task_thread_wrapper, task);

    // Return task value
    HmlValue result;
    result.type = HML_VAL_TASK;
    result.as.as_task = task;
    return result;
}

HmlValue hml_join(HmlValue task_val) {
    if (task_val.type != HML_VAL_TASK) {
        fprintf(stderr, "Error: join() expects a task\n");
        exit(1);
    }

    HmlTask *task = task_val.as.as_task;

    if (task->joined) {
        fprintf(stderr, "Error: Task already joined\n");
        exit(1);
    }

    if (task->detached) {
        fprintf(stderr, "Error: Cannot join a detached task\n");
        exit(1);
    }

    // Wait for task to complete
    pthread_mutex_lock((pthread_mutex_t*)task->mutex);
    while (task->state != HML_TASK_COMPLETED) {
        pthread_cond_wait((pthread_cond_t*)task->cond, (pthread_mutex_t*)task->mutex);
    }
    pthread_mutex_unlock((pthread_mutex_t*)task->mutex);

    // Join the thread
    pthread_join(*(pthread_t*)task->thread, NULL);
    task->joined = 1;

    // Return result (retained)
    HmlValue result = task->result;
    hml_retain(&result);
    return result;
}

void hml_detach(HmlValue task_val) {
    if (task_val.type != HML_VAL_TASK) {
        fprintf(stderr, "Error: detach() expects a task\n");
        exit(1);
    }

    HmlTask *task = task_val.as.as_task;

    if (task->joined) {
        fprintf(stderr, "Error: Cannot detach an already joined task\n");
        exit(1);
    }

    if (task->detached) {
        return; // Already detached
    }

    task->detached = 1;
    pthread_detach(*(pthread_t*)task->thread);
}

// Channel functions
HmlValue hml_channel(int32_t capacity) {
    HmlChannel *ch = malloc(sizeof(HmlChannel));
    ch->capacity = capacity;
    ch->buffer = malloc(sizeof(HmlValue) * capacity);
    ch->head = 0;
    ch->tail = 0;
    ch->count = 0;
    ch->closed = 0;
    ch->ref_count = 1;

    ch->mutex = malloc(sizeof(pthread_mutex_t));
    ch->not_empty = malloc(sizeof(pthread_cond_t));
    ch->not_full = malloc(sizeof(pthread_cond_t));
    pthread_mutex_init((pthread_mutex_t*)ch->mutex, NULL);
    pthread_cond_init((pthread_cond_t*)ch->not_empty, NULL);
    pthread_cond_init((pthread_cond_t*)ch->not_full, NULL);

    HmlValue result;
    result.type = HML_VAL_CHANNEL;
    result.as.as_channel = ch;
    return result;
}

void hml_channel_send(HmlValue channel, HmlValue value) {
    if (channel.type != HML_VAL_CHANNEL) {
        fprintf(stderr, "Error: send() expects a channel\n");
        exit(1);
    }

    HmlChannel *ch = channel.as.as_channel;

    pthread_mutex_lock((pthread_mutex_t*)ch->mutex);

    // Wait while buffer is full
    while (ch->count == ch->capacity && !ch->closed) {
        pthread_cond_wait((pthread_cond_t*)ch->not_full, (pthread_mutex_t*)ch->mutex);
    }

    if (ch->closed) {
        pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
        fprintf(stderr, "Error: Cannot send on closed channel\n");
        exit(1);
    }

    // Add value to buffer
    ch->buffer[ch->tail] = value;
    hml_retain(&ch->buffer[ch->tail]);
    ch->tail = (ch->tail + 1) % ch->capacity;
    ch->count++;

    pthread_cond_signal((pthread_cond_t*)ch->not_empty);
    pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
}

HmlValue hml_channel_recv(HmlValue channel) {
    if (channel.type != HML_VAL_CHANNEL) {
        fprintf(stderr, "Error: recv() expects a channel\n");
        exit(1);
    }

    HmlChannel *ch = channel.as.as_channel;

    pthread_mutex_lock((pthread_mutex_t*)ch->mutex);

    // Wait while buffer is empty
    while (ch->count == 0 && !ch->closed) {
        pthread_cond_wait((pthread_cond_t*)ch->not_empty, (pthread_mutex_t*)ch->mutex);
    }

    if (ch->count == 0 && ch->closed) {
        pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
        return hml_val_null();
    }

    // Get value from buffer
    HmlValue value = ch->buffer[ch->head];
    ch->head = (ch->head + 1) % ch->capacity;
    ch->count--;

    pthread_cond_signal((pthread_cond_t*)ch->not_full);
    pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);

    return value;
}

void hml_channel_close(HmlValue channel) {
    if (channel.type != HML_VAL_CHANNEL) {
        return;
    }

    HmlChannel *ch = channel.as.as_channel;

    pthread_mutex_lock((pthread_mutex_t*)ch->mutex);
    ch->closed = 1;
    pthread_cond_broadcast((pthread_cond_t*)ch->not_empty);
    pthread_cond_broadcast((pthread_cond_t*)ch->not_full);
    pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
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
