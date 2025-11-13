#include "internal.h"

// ========== ARRAY METHOD HANDLING ==========

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

Value call_array_method(Array *arr, const char *method, Value *args, int num_args) {
    // push(value) - add element to end
    if (strcmp(method, "push") == 0) {
        if (num_args != 1) {
            fprintf(stderr, "Runtime error: push() expects 1 argument\n");
            exit(1);
        }
        array_push(arr, args[0]);
        return val_null();
    }

    // pop() - remove and return last element
    if (strcmp(method, "pop") == 0) {
        if (num_args != 0) {
            fprintf(stderr, "Runtime error: pop() expects no arguments\n");
            exit(1);
        }
        return array_pop(arr);
    }

    // shift() - remove and return first element
    if (strcmp(method, "shift") == 0) {
        if (num_args != 0) {
            fprintf(stderr, "Runtime error: shift() expects no arguments\n");
            exit(1);
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
            fprintf(stderr, "Runtime error: unshift() expects 1 argument\n");
            exit(1);
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
            fprintf(stderr, "Runtime error: insert() expects 2 arguments (index, value)\n");
            exit(1);
        }
        if (!is_integer(args[0])) {
            fprintf(stderr, "Runtime error: insert() index must be an integer\n");
            exit(1);
        }
        int32_t index = value_to_int(args[0]);
        if (index < 0 || index > arr->length) {
            fprintf(stderr, "Runtime error: insert() index out of bounds\n");
            exit(1);
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
            fprintf(stderr, "Runtime error: remove() expects 1 argument (index)\n");
            exit(1);
        }
        if (!is_integer(args[0])) {
            fprintf(stderr, "Runtime error: remove() index must be an integer\n");
            exit(1);
        }
        int32_t index = value_to_int(args[0]);
        if (index < 0 || index >= arr->length) {
            fprintf(stderr, "Runtime error: remove() index out of bounds\n");
            exit(1);
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
            fprintf(stderr, "Runtime error: find() expects 1 argument (value)\n");
            exit(1);
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
            fprintf(stderr, "Runtime error: contains() expects 1 argument (value)\n");
            exit(1);
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
            fprintf(stderr, "Runtime error: slice() expects 2 arguments (start, end)\n");
            exit(1);
        }
        if (!is_integer(args[0]) || !is_integer(args[1])) {
            fprintf(stderr, "Runtime error: slice() arguments must be integers\n");
            exit(1);
        }
        int32_t start = value_to_int(args[0]);
        int32_t end = value_to_int(args[1]);

        if (start < 0 || start > arr->length) {
            fprintf(stderr, "Runtime error: slice() start index out of bounds\n");
            exit(1);
        }
        if (end < start || end > arr->length) {
            fprintf(stderr, "Runtime error: slice() end index out of bounds\n");
            exit(1);
        }

        Array *result = array_new();
        for (int i = start; i < end; i++) {
            array_push(result, arr->elements[i]);
        }
        return val_array(result);
    }

    // join(delimiter) - join array elements into string
    if (strcmp(method, "join") == 0) {
        if (num_args != 1) {
            fprintf(stderr, "Runtime error: join() expects 1 argument (delimiter)\n");
            exit(1);
        }
        if (args[0].type != VAL_STRING) {
            fprintf(stderr, "Runtime error: join() delimiter must be a string\n");
            exit(1);
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
            fprintf(stderr, "Runtime error: Memory allocation failed in join()\n");
            exit(1);
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
            fprintf(stderr, "Runtime error: concat() expects 1 argument (array)\n");
            exit(1);
        }
        if (args[0].type != VAL_ARRAY) {
            fprintf(stderr, "Runtime error: concat() argument must be an array\n");
            exit(1);
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
            fprintf(stderr, "Runtime error: reverse() expects no arguments\n");
            exit(1);
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
            fprintf(stderr, "Runtime error: first() expects no arguments\n");
            exit(1);
        }
        if (arr->length == 0) {
            return val_null();
        }
        return arr->elements[0];
    }

    // last() - get last element
    if (strcmp(method, "last") == 0) {
        if (num_args != 0) {
            fprintf(stderr, "Runtime error: last() expects no arguments\n");
            exit(1);
        }
        if (arr->length == 0) {
            return val_null();
        }
        return arr->elements[arr->length - 1];
    }

    // clear() - remove all elements
    if (strcmp(method, "clear") == 0) {
        if (num_args != 0) {
            fprintf(stderr, "Runtime error: clear() expects no arguments\n");
            exit(1);
        }
        arr->length = 0;
        return val_null();
    }

    fprintf(stderr, "Runtime error: Array has no method '%s'\n", method);
    exit(1);
}
