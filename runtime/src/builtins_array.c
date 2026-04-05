/*
 * Hemlock Runtime - Array Operations
 *
 * Array manipulation and higher-order functions:
 * - Basic: push, pop, shift, unshift, insert, remove
 * - Access: get, set, length, first, last, clear
 * - Search: find, contains
 * - Transform: slice, join, concat, reverse
 * - Higher-order: map, filter, reduce
 * - Typed arrays: validate and set element type constraints
 */

#include "builtins_internal.h"
#include <stdatomic.h>

// Forward declaration from builtins.c
HmlValue hml_call_function(HmlValue fn, HmlValue *args, int num_args);

// ========== ARRAY OPERATIONS ==========

void hml_array_push(HmlValue arr, HmlValue val) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("push() requires array");
    }

    HmlArray *a = arr.as.as_array;

    // Check element type for typed arrays
    if (a->element_type != HML_VAL_NULL && val.type != a->element_type) {
        hml_runtime_error("Type mismatch in typed array - expected element of specific type");
    }

    // Grow if needed
    if (a->length >= a->capacity) {
        // SECURITY: Check for integer overflow before doubling capacity
        if (a->capacity > INT_MAX / 2) {
            hml_runtime_error("Array capacity overflow");
        }
        int new_cap = (a->capacity == 0) ? 8 : a->capacity * 2;
        HmlValue *new_elements = realloc(a->elements, new_cap * sizeof(HmlValue));
        if (!new_elements) {
            hml_runtime_error("Out of memory expanding array");
        }
        a->elements = new_elements;
        a->capacity = new_cap;
    }

    a->elements[a->length] = val;
    hml_retain(&a->elements[a->length]);
    a->length++;
}

HmlValue hml_array_get(HmlValue arr, HmlValue index) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("Index access requires array");
    }

    int idx = hml_to_i32(index);
    HmlArray *a = arr.as.as_array;

    if (idx < 0 || idx >= a->length) {
        hml_runtime_error("Array index %d out of bounds (length %d)", idx, a->length);
    }

    HmlValue result = a->elements[idx];
    // OPTIMIZATION: Skip retain for primitive types
    hml_retain_if_needed(&result);
    return result;
}

void hml_array_set(HmlValue arr, HmlValue index, HmlValue val) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("Index assignment requires array");
    }

    int idx = hml_to_i32(index);
    HmlArray *a = arr.as.as_array;

    // Check element type for typed arrays
    if (a->element_type != HML_VAL_NULL && val.type != a->element_type) {
        hml_runtime_error("Type mismatch in typed array - expected element of specific type");
    }

    if (idx < 0) {
        hml_runtime_error("Negative array index not supported");
    }

    // Extend array if needed, filling with nulls (match interpreter behavior)
    while (idx >= a->length) {
        // Grow capacity if needed
        if (a->length >= a->capacity) {
            // SECURITY: Check for integer overflow before doubling capacity
            if (a->capacity > INT_MAX / 2) {
                hml_runtime_error("Array capacity overflow");
            }
            int new_cap = (a->capacity == 0) ? 8 : a->capacity * 2;
            HmlValue *new_elements = realloc(a->elements, new_cap * sizeof(HmlValue));
            if (!new_elements) {
                hml_runtime_error("Out of memory expanding array");
            }
            a->elements = new_elements;
            a->capacity = new_cap;
        }
        a->elements[a->length] = hml_val_null();
        a->length++;
    }

    // Retain new value FIRST to prevent use-after-free when old == new
    hml_retain(&val);
    hml_release(&a->elements[idx]);
    a->elements[idx] = val;
}

HmlValue hml_array_length(HmlValue arr) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        return hml_val_i32(0);
    }
    return hml_val_i32(arr.as.as_array->length);
}

HmlValue hml_array_pop(HmlValue arr) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("pop() requires array");
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
        hml_runtime_error("shift() requires array");
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
        hml_runtime_error("unshift() requires array");
    }

    HmlArray *a = arr.as.as_array;

    // Check element type for typed arrays
    if (a->element_type != HML_VAL_NULL && val.type != a->element_type) {
        hml_runtime_error("Type mismatch in typed array - expected element of specific type");
    }

    // Grow if needed
    if (a->length >= a->capacity) {
        // SECURITY: Check for integer overflow before doubling capacity
        if (a->capacity > INT_MAX / 2) {
            hml_runtime_error("Array capacity overflow");
        }
        int new_cap = (a->capacity == 0) ? 8 : a->capacity * 2;
        HmlValue *new_elements = realloc(a->elements, new_cap * sizeof(HmlValue));
        if (!new_elements) {
            hml_runtime_error("Out of memory expanding array");
        }
        a->elements = new_elements;
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
        hml_runtime_error("insert() requires array");
    }

    int idx = hml_to_i32(index);
    HmlArray *a = arr.as.as_array;

    // Check element type for typed arrays
    if (a->element_type != HML_VAL_NULL && val.type != a->element_type) {
        hml_runtime_error("Type mismatch in typed array - expected element of specific type");
    }

    if (idx < 0 || idx > a->length) {
        hml_runtime_error("insert index %d out of bounds (length %d)", idx, a->length);
    }

    // Grow if needed
    if (a->length >= a->capacity) {
        // SECURITY: Check for integer overflow before doubling capacity
        if (a->capacity > INT_MAX / 2) {
            hml_runtime_error("Array capacity overflow");
        }
        int new_cap = (a->capacity == 0) ? 8 : a->capacity * 2;
        HmlValue *new_elements = realloc(a->elements, new_cap * sizeof(HmlValue));
        if (!new_elements) {
            hml_runtime_error("Out of memory expanding array");
        }
        a->elements = new_elements;
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
        hml_runtime_error("remove() requires array");
    }

    int idx = hml_to_i32(index);
    HmlArray *a = arr.as.as_array;

    if (idx < 0 || idx >= a->length) {
        hml_runtime_error("remove index %d out of bounds (length %d)", idx, a->length);
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
        hml_runtime_error("find() requires array");
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
        hml_runtime_error("slice() requires array");
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
    atomic_store(&result->freed, 0);  // Not freed

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
        hml_runtime_error("join() requires array");
    }
    if (delimiter.type != HML_VAL_STRING) {
        hml_runtime_error("join() requires string delimiter");
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
    if (!result) {
        hml_runtime_error("Out of memory in array join");
    }
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
        hml_runtime_error("concat() requires array");
    }
    if (arr2.type != HML_VAL_ARRAY || !arr2.as.as_array) {
        hml_runtime_error("concat() requires array argument");
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
    atomic_store(&result->freed, 0);  // Not freed

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
        hml_runtime_error("reverse() requires array");
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
        hml_runtime_error("first() requires array");
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
        hml_runtime_error("last() requires array");
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
        hml_runtime_error("clear() requires array");
    }

    HmlArray *a = arr.as.as_array;
    for (int i = 0; i < a->length; i++) {
        hml_release(&a->elements[i]);
    }
    a->length = 0;
}

void hml_array_reserve(HmlValue arr, HmlValue capacity) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("reserve() requires array");
    }

    int requested = hml_to_i32(capacity);
    if (requested < 0) {
        hml_runtime_error("reserve() capacity must be non-negative");
    }

    HmlArray *a = arr.as.as_array;
    if (requested > a->capacity) {
        if ((size_t)requested > SIZE_MAX / sizeof(HmlValue)) {
            hml_runtime_error("reserve() capacity too large");
        }
        HmlValue *new_elements = realloc(a->elements, (size_t)requested * sizeof(HmlValue));
        if (!new_elements) {
            hml_runtime_error("Out of memory in reserve()");
        }
        a->elements = new_elements;
        a->capacity = requested;
    }
}

// ========== TYPED ARRAY SUPPORT ==========

void hml_array_set_element_type(HmlValue arr, HmlValueType element_type) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("cannot set element type on non-array");
    }
    arr.as.as_array->element_type = element_type;
}

// Helper: Check if value type matches expected element type
static int hml_type_matches(HmlValue val, HmlValueType expected) {
    if (expected == HML_VAL_NULL) return 1;  // Untyped, accept anything
    return val.type == expected;
}

// Helper: Check if an HmlValueType is numeric
static int hml_is_numeric_type(HmlValueType type) {
    switch (type) {
        case HML_VAL_I8: case HML_VAL_I16: case HML_VAL_I32: case HML_VAL_I64:
        case HML_VAL_U8: case HML_VAL_U16: case HML_VAL_U32: case HML_VAL_U64:
        case HML_VAL_F32: case HML_VAL_F64:
            return 1;
        default:
            return 0;
    }
}

// Validate and set element type constraint on array
HmlValue hml_validate_typed_array(HmlValue arr, HmlValueType element_type) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("Expected array");
    }

    HmlArray *a = arr.as.as_array;

    // If HML_VAL_NULL, it's an untyped array - no constraint
    if (element_type == HML_VAL_NULL) {
        return arr;
    }

    // Validate all existing elements match the type constraint,
    // coercing numeric elements to the target numeric type instead of rejecting them.
    for (int i = 0; i < a->length; i++) {
        if (!hml_type_matches(a->elements[i], element_type)) {
            // If both source and target are numeric, coerce the element
            if (hml_is_numeric(a->elements[i]) && hml_is_numeric_type(element_type)) {
                HmlValue converted = hml_convert_to_type(a->elements[i], element_type);
                hml_release(&a->elements[i]);
                a->elements[i] = converted;
                hml_retain(&a->elements[i]);
            } else {
                hml_runtime_error("Type mismatch in typed array - expected element of specific type");
            }
        }
    }

    // Set the element type constraint
    a->element_type = element_type;
    return arr;
}

// ========== HIGHER-ORDER ARRAY FUNCTIONS ==========

HmlValue hml_array_map(HmlValue arr, HmlValue callback) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("map() requires array");
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
        hml_runtime_error("filter() requires array");
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
        hml_runtime_error("reduce() requires array");
    }

    HmlArray *a = arr.as.as_array;

    // Handle empty array
    if (a->length == 0) {
        if (initial.type == HML_VAL_NULL) {
            hml_runtime_error("reduce() of empty array with no initial value");
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

HmlValue hml_array_every(HmlValue arr, HmlValue predicate) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("every() requires array");
    }

    HmlArray *a = arr.as.as_array;

    // Empty array returns true (vacuous truth)
    if (a->length == 0) {
        return hml_val_bool(1);
    }

    for (int i = 0; i < a->length; i++) {
        HmlValue args[2] = { a->elements[i], hml_val_i32(i) };
        HmlValue result = hml_call_function(predicate, args, 1);
        int is_true = hml_to_bool(result);
        hml_release(&result);
        if (!is_true) {
            return hml_val_bool(0);
        }
    }

    return hml_val_bool(1);
}

HmlValue hml_array_some(HmlValue arr, HmlValue predicate) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("some() requires array");
    }

    HmlArray *a = arr.as.as_array;

    // Empty array returns false
    if (a->length == 0) {
        return hml_val_bool(0);
    }

    for (int i = 0; i < a->length; i++) {
        HmlValue args[2] = { a->elements[i], hml_val_i32(i) };
        HmlValue result = hml_call_function(predicate, args, 1);
        int is_true = hml_to_bool(result);
        hml_release(&result);
        if (is_true) {
            return hml_val_bool(1);
        }
    }

    return hml_val_bool(0);
}

HmlValue hml_array_index_of(HmlValue arr, HmlValue value) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("indexOf() requires array");
    }

    HmlArray *a = arr.as.as_array;
    for (int i = 0; i < a->length; i++) {
        if (hml_values_equal(a->elements[i], value)) {
            return hml_val_i32(i);
        }
    }
    return hml_val_i32(-1);
}

// Quicksort comparator context
typedef struct {
    HmlValue comparator;
    int has_comparator;
} SortContext;

// Default comparison function for sorting
static int default_compare(HmlValue a, HmlValue b) {
    // Compare by type first
    if (a.type != b.type) {
        return (int)a.type - (int)b.type;
    }

    switch (a.type) {
        case HML_VAL_I8:  return (a.as.as_i8 < b.as.as_i8) ? -1 : (a.as.as_i8 > b.as.as_i8) ? 1 : 0;
        case HML_VAL_I16: return (a.as.as_i16 < b.as.as_i16) ? -1 : (a.as.as_i16 > b.as.as_i16) ? 1 : 0;
        case HML_VAL_I32: return (a.as.as_i32 < b.as.as_i32) ? -1 : (a.as.as_i32 > b.as.as_i32) ? 1 : 0;
        case HML_VAL_I64: return (a.as.as_i64 < b.as.as_i64) ? -1 : (a.as.as_i64 > b.as.as_i64) ? 1 : 0;
        case HML_VAL_U8:  return (a.as.as_u8 < b.as.as_u8) ? -1 : (a.as.as_u8 > b.as.as_u8) ? 1 : 0;
        case HML_VAL_U16: return (a.as.as_u16 < b.as.as_u16) ? -1 : (a.as.as_u16 > b.as.as_u16) ? 1 : 0;
        case HML_VAL_U32: return (a.as.as_u32 < b.as.as_u32) ? -1 : (a.as.as_u32 > b.as.as_u32) ? 1 : 0;
        case HML_VAL_U64: return (a.as.as_u64 < b.as.as_u64) ? -1 : (a.as.as_u64 > b.as.as_u64) ? 1 : 0;
        case HML_VAL_F32: return (a.as.as_f32 < b.as.as_f32) ? -1 : (a.as.as_f32 > b.as.as_f32) ? 1 : 0;
        case HML_VAL_F64: return (a.as.as_f64 < b.as.as_f64) ? -1 : (a.as.as_f64 > b.as.as_f64) ? 1 : 0;
        case HML_VAL_BOOL: return (int)a.as.as_bool - (int)b.as.as_bool;
        case HML_VAL_STRING:
            return strcmp(a.as.as_string->data, b.as.as_string->data);
        default:
            return 0;  // Objects, arrays, etc. - no default ordering
    }
}

// Compare two values using either default or custom comparator
static int compare_values(HmlValue a, HmlValue b, SortContext *ctx) {
    if (!ctx->has_comparator) {
        return default_compare(a, b);
    }

    // Call user-provided comparator
    HmlValue args[2] = { a, b };
    HmlValue result = hml_call_function(ctx->comparator, args, 2);
    int cmp = hml_to_i32(result);
    hml_release(&result);
    return cmp;
}

// Quicksort partition
static int partition(HmlValue *arr, int low, int high, SortContext *ctx) {
    HmlValue pivot = arr[high];
    int i = low - 1;

    for (int j = low; j < high; j++) {
        if (compare_values(arr[j], pivot, ctx) <= 0) {
            i++;
            HmlValue tmp = arr[i];
            arr[i] = arr[j];
            arr[j] = tmp;
        }
    }

    HmlValue tmp = arr[i + 1];
    arr[i + 1] = arr[high];
    arr[high] = tmp;
    return i + 1;
}

// Quicksort implementation
static void quicksort(HmlValue *arr, int low, int high, SortContext *ctx) {
    if (low < high) {
        int pi = partition(arr, low, high, ctx);
        quicksort(arr, low, pi - 1, ctx);
        quicksort(arr, pi + 1, high, ctx);
    }
}

void hml_array_sort(HmlValue arr, HmlValue comparator) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("sort() requires array");
    }

    HmlArray *a = arr.as.as_array;
    if (a->length <= 1) {
        return;  // Already sorted
    }

    SortContext ctx;
    ctx.comparator = comparator;
    ctx.has_comparator = (comparator.type == HML_VAL_FUNCTION);

    quicksort(a->elements, 0, a->length - 1, &ctx);
}

void hml_array_fill(HmlValue arr, HmlValue value, HmlValue start, HmlValue end) {
    if (arr.type != HML_VAL_ARRAY || !arr.as.as_array) {
        hml_runtime_error("fill() requires array");
    }

    HmlArray *a = arr.as.as_array;

    // Get start and end indices
    int s = (start.type == HML_VAL_NULL) ? 0 : hml_to_i32(start);
    int e = (end.type == HML_VAL_NULL) ? a->length : hml_to_i32(end);

    // Handle negative indices (from end)
    if (s < 0) s = a->length + s;
    if (e < 0) e = a->length + e;

    // Clamp values
    if (s < 0) s = 0;
    if (e > a->length) e = a->length;
    if (s >= e) return;  // Nothing to fill

    // Check element type for typed arrays
    if (a->element_type != HML_VAL_NULL && value.type != a->element_type) {
        hml_runtime_error("Type mismatch in typed array - expected element of specific type");
    }

    // Fill the range
    for (int i = s; i < e; i++) {
        hml_release(&a->elements[i]);
        a->elements[i] = value;
        hml_retain(&a->elements[i]);
    }
}
