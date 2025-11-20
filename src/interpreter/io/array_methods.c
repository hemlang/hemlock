#include "internal.h"
#include <stdarg.h>

// ========== RUNTIME ERROR HELPER ==========

static Value throw_runtime_error(ExecutionContext *ctx, const char *format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    ctx->exception_state.exception_value = val_string(buffer);
    value_retain(ctx->exception_state.exception_value);
    ctx->exception_state.is_throwing = 1;
    return val_null();
}

// ========== ARRAY METHOD HANDLING ==========

// Helper function to check if value matches array element type
static Value check_array_element_type_for_method(Array *arr, Value val, ExecutionContext *ctx) {
    if (!arr->element_type) {
        return val_null();  // Untyped array, allow any value
    }

    TypeKind expected = arr->element_type->kind;
    int type_matches = 0;

    switch (expected) {
        case TYPE_I8:   type_matches = (val.type == VAL_I8); break;
        case TYPE_I16:  type_matches = (val.type == VAL_I16); break;
        case TYPE_I32:  type_matches = (val.type == VAL_I32); break;
        case TYPE_I64:  type_matches = (val.type == VAL_I64); break;
        case TYPE_U8:   type_matches = (val.type == VAL_U8); break;
        case TYPE_U16:  type_matches = (val.type == VAL_U16); break;
        case TYPE_U32:  type_matches = (val.type == VAL_U32); break;
        case TYPE_U64:  type_matches = (val.type == VAL_U64); break;
        case TYPE_F32:  type_matches = (val.type == VAL_F32); break;
        case TYPE_F64:  type_matches = (val.type == VAL_F64); break;
        case TYPE_BOOL: type_matches = (val.type == VAL_BOOL); break;
        case TYPE_STRING: type_matches = (val.type == VAL_STRING); break;
        case TYPE_RUNE: type_matches = (val.type == VAL_RUNE); break;
        case TYPE_PTR:  type_matches = (val.type == VAL_PTR); break;
        case TYPE_BUFFER: type_matches = (val.type == VAL_BUFFER); break;
        default:
            return throw_runtime_error(ctx, "Unsupported array element type constraint");
    }

    if (!type_matches) {
        return throw_runtime_error(ctx, "Type mismatch in typed array - expected element of specific type");
    }

    return val_null();
}

// Helper: Compare two values for equality
int values_equal(Value a, Value b) {
    if (a.type != b.type) {
        return 0;
    }

    switch (a.type) {
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
        case VAL_BOOL: return a.as.as_bool == b.as.as_bool;
        case VAL_STRING:
            return strcmp(a.as.as_string->data, b.as.as_string->data) == 0;
        case VAL_PTR: return a.as.as_ptr == b.as.as_ptr;
        case VAL_NULL: return 1;
        default: return 0;  // Objects, arrays, functions compared by reference
    }
}

Value call_array_method(Array *arr, const char *method, Value *args, int num_args, ExecutionContext *ctx) {
    // push(value) - add element to end
    if (strcmp(method, "push") == 0) {
        if (num_args != 1) {
            return throw_runtime_error(ctx, "push() expects 1 argument");
        }
        array_push(arr, args[0]);
        return val_null();
    }

    // pop() - remove and return last element
    if (strcmp(method, "pop") == 0) {
        if (num_args != 0) {
            return throw_runtime_error(ctx, "pop() expects no arguments");
        }
        return array_pop(arr);
    }

    // shift() - remove and return first element
    if (strcmp(method, "shift") == 0) {
        if (num_args != 0) {
            return throw_runtime_error(ctx, "shift() expects no arguments");
        }
        if (arr->length == 0) {
            return val_null();
        }
        Value first = arr->elements[0];
        // Shift all elements left
        for (int i = 1; i < arr->length; i++) {
            arr->elements[i - 1] = arr->elements[i];
        }
        arr->length--;
        return first;
    }

    // unshift(value) - add element to beginning
    if (strcmp(method, "unshift") == 0) {
        if (num_args != 1) {
            return throw_runtime_error(ctx, "unshift() expects 1 argument");
        }
        // Check type constraint
        Value check_result = check_array_element_type_for_method(arr, args[0], ctx);
        if (ctx->exception_state.is_throwing) {
            return check_result;
        }

        // Ensure capacity
        if (arr->length >= arr->capacity) {
            arr->capacity *= 2;
            arr->elements = realloc(arr->elements, sizeof(Value) * arr->capacity);
        }
        // Shift all elements right
        for (int i = arr->length; i > 0; i--) {
            arr->elements[i] = arr->elements[i - 1];
        }
        value_retain(args[0]);
        arr->elements[0] = args[0];
        arr->length++;
        return val_null();
    }

    // insert(index, value) - insert element at index
    if (strcmp(method, "insert") == 0) {
        if (num_args != 2) {
            return throw_runtime_error(ctx, "insert() expects 2 arguments (index, value)");
        }
        if (!is_integer(args[0])) {
            return throw_runtime_error(ctx, "insert() index must be an integer");
        }
        int32_t index = value_to_int(args[0]);
        if (index < 0 || index > arr->length) {
            return throw_runtime_error(ctx, "insert() index out of bounds");
        }
        // Check type constraint
        Value check_result = check_array_element_type_for_method(arr, args[1], ctx);
        if (ctx->exception_state.is_throwing) {
            return check_result;
        }

        // Ensure capacity
        if (arr->length >= arr->capacity) {
            arr->capacity *= 2;
            arr->elements = realloc(arr->elements, sizeof(Value) * arr->capacity);
        }
        // Shift elements right from index
        for (int i = arr->length; i > index; i--) {
            arr->elements[i] = arr->elements[i - 1];
        }
        value_retain(args[1]);
        arr->elements[index] = args[1];
        arr->length++;
        return val_null();
    }

    // remove(index) - remove and return element at index
    if (strcmp(method, "remove") == 0) {
        if (num_args != 1) {
            return throw_runtime_error(ctx, "remove() expects 1 argument (index)");
        }
        if (!is_integer(args[0])) {
            return throw_runtime_error(ctx, "remove() index must be an integer");
        }
        int32_t index = value_to_int(args[0]);
        if (index < 0 || index >= arr->length) {
            return throw_runtime_error(ctx, "remove() index out of bounds");
        }
        Value removed = arr->elements[index];
        // Shift elements left from index
        for (int i = index; i < arr->length - 1; i++) {
            arr->elements[i] = arr->elements[i + 1];
        }
        arr->length--;
        return removed;
    }

    // find(value) - find first occurrence, return index or -1
    if (strcmp(method, "find") == 0) {
        if (num_args != 1) {
            return throw_runtime_error(ctx, "find() expects 1 argument (value)");
        }
        for (int i = 0; i < arr->length; i++) {
            if (values_equal(arr->elements[i], args[0])) {
                return val_i32(i);
            }
        }
        return val_i32(-1);
    }

    // contains(value) - check if array contains value
    if (strcmp(method, "contains") == 0) {
        if (num_args != 1) {
            return throw_runtime_error(ctx, "contains() expects 1 argument (value)");
        }
        for (int i = 0; i < arr->length; i++) {
            if (values_equal(arr->elements[i], args[0])) {
                return val_bool(1);
            }
        }
        return val_bool(0);
    }

    // slice(start, end) - extract subarray (end is exclusive)
    if (strcmp(method, "slice") == 0) {
        if (num_args != 2) {
            return throw_runtime_error(ctx, "slice() expects 2 arguments (start, end)");
        }
        if (!is_integer(args[0]) || !is_integer(args[1])) {
            return throw_runtime_error(ctx, "slice() arguments must be integers");
        }
        int32_t start = value_to_int(args[0]);
        int32_t end = value_to_int(args[1]);

        // Clamp bounds to valid range (Python/JS/Rust behavior)
        if (start < 0) start = 0;
        if (start > arr->length) start = arr->length;
        if (end < start) end = start;  // Empty slice if end < start
        if (end > arr->length) end = arr->length;

        Array *result = array_new();
        for (int i = start; i < end; i++) {
            array_push(result, arr->elements[i]);
        }
        return val_array(result);
    }

    // join(delimiter) - join array elements into string
    if (strcmp(method, "join") == 0) {
        if (num_args != 1) {
            return throw_runtime_error(ctx, "join() expects 1 argument (delimiter)");
        }
        if (args[0].type != VAL_STRING) {
            return throw_runtime_error(ctx, "join() delimiter must be a string");
        }
        String *delim = args[0].as.as_string;

        if (arr->length == 0) {
            return val_string("");
        }

        // Calculate total size needed
        size_t total_len = 0;
        for (int i = 0; i < arr->length; i++) {
            if (arr->elements[i].type == VAL_STRING) {
                total_len += arr->elements[i].as.as_string->length;
            } else {
                // For non-strings, estimate size (we'll use sprintf later)
                total_len += 32;  // Generous estimate for numbers
            }
            if (i < arr->length - 1) {
                total_len += delim->length;
            }
        }

        char *result = malloc(total_len + 1);
        if (!result) {
            return throw_runtime_error(ctx, "Memory allocation failed in join()");
        }
        size_t pos = 0;

        for (int i = 0; i < arr->length; i++) {
            // Convert element to string
            size_t remaining = total_len + 1 - pos;

            if (arr->elements[i].type == VAL_STRING) {
                String *s = arr->elements[i].as.as_string;
                memcpy(result + pos, s->data, s->length);
                pos += s->length;
            } else if (arr->elements[i].type == VAL_I8) {
                int written = snprintf(result + pos, remaining, "%d", arr->elements[i].as.as_i8);
                pos += (written > 0) ? written : 0;
            } else if (arr->elements[i].type == VAL_I16) {
                int written = snprintf(result + pos, remaining, "%d", arr->elements[i].as.as_i16);
                pos += (written > 0) ? written : 0;
            } else if (arr->elements[i].type == VAL_I32) {
                int written = snprintf(result + pos, remaining, "%d", arr->elements[i].as.as_i32);
                pos += (written > 0) ? written : 0;
            } else if (arr->elements[i].type == VAL_U8) {
                int written = snprintf(result + pos, remaining, "%u", arr->elements[i].as.as_u8);
                pos += (written > 0) ? written : 0;
            } else if (arr->elements[i].type == VAL_U16) {
                int written = snprintf(result + pos, remaining, "%u", arr->elements[i].as.as_u16);
                pos += (written > 0) ? written : 0;
            } else if (arr->elements[i].type == VAL_U32) {
                int written = snprintf(result + pos, remaining, "%u", arr->elements[i].as.as_u32);
                pos += (written > 0) ? written : 0;
            } else if (arr->elements[i].type == VAL_F32) {
                int written = snprintf(result + pos, remaining, "%g", arr->elements[i].as.as_f32);
                pos += (written > 0) ? written : 0;
            } else if (arr->elements[i].type == VAL_F64) {
                int written = snprintf(result + pos, remaining, "%g", arr->elements[i].as.as_f64);
                pos += (written > 0) ? written : 0;
            } else if (arr->elements[i].type == VAL_BOOL) {
                const char *s = arr->elements[i].as.as_bool ? "true" : "false";
                size_t len = strlen(s);
                memcpy(result + pos, s, len);
                pos += len;
            } else if (arr->elements[i].type == VAL_NULL) {
                memcpy(result + pos, "null", 4);
                pos += 4;
            } else {
                memcpy(result + pos, "[object]", 8);
                pos += 8;
            }

            // Add delimiter between elements
            if (i < arr->length - 1) {
                memcpy(result + pos, delim->data, delim->length);
                pos += delim->length;
            }
        }
        result[pos] = '\0';

        return val_string_take(result, pos, total_len + 1);
    }

    // concat(other) - concatenate arrays (returns new array)
    if (strcmp(method, "concat") == 0) {
        if (num_args != 1) {
            return throw_runtime_error(ctx, "concat() expects 1 argument (array)");
        }
        if (args[0].type != VAL_ARRAY) {
            return throw_runtime_error(ctx, "concat() argument must be an array");
        }
        Array *other = args[0].as.as_array;
        Array *result = array_new();

        // Copy elements from first array
        for (int i = 0; i < arr->length; i++) {
            array_push(result, arr->elements[i]);
        }
        // Copy elements from second array
        for (int i = 0; i < other->length; i++) {
            array_push(result, other->elements[i]);
        }
        return val_array(result);
    }

    // reverse() - reverse array in-place
    if (strcmp(method, "reverse") == 0) {
        if (num_args != 0) {
            return throw_runtime_error(ctx, "reverse() expects no arguments");
        }
        int left = 0;
        int right = arr->length - 1;
        while (left < right) {
            Value temp = arr->elements[left];
            arr->elements[left] = arr->elements[right];
            arr->elements[right] = temp;
            left++;
            right--;
        }
        return val_null();
    }

    // first() - get first element
    if (strcmp(method, "first") == 0) {
        if (num_args != 0) {
            return throw_runtime_error(ctx, "first() expects no arguments");
        }
        if (arr->length == 0) {
            return val_null();
        }
        return arr->elements[0];
    }

    // last() - get last element
    if (strcmp(method, "last") == 0) {
        if (num_args != 0) {
            return throw_runtime_error(ctx, "last() expects no arguments");
        }
        if (arr->length == 0) {
            return val_null();
        }
        return arr->elements[arr->length - 1];
    }

    // clear() - remove all elements
    if (strcmp(method, "clear") == 0) {
        if (num_args != 0) {
            return throw_runtime_error(ctx, "clear() expects no arguments");
        }
        arr->length = 0;
        return val_null();
    }

    return throw_runtime_error(ctx, "Array has no method '%s'", method);
}
