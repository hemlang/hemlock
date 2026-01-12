/*
 * Hemlock Runtime Library - Memory Builtins
 *
 * This file implements:
 * - Pointer index operations (ptr_get, ptr_set)
 * - FFI callback operations
 * - Memory operations (alloc, free, realloc, memset, memcpy, talloc)
 * - Additional pointer helpers for all types
 */

#include "builtins_internal.h"

// ========== POINTER INDEX OPERATIONS ==========

HmlValue hml_ptr_get(HmlValue ptr, HmlValue index) {
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("Pointer index requires pointer");
    }
    void *p = ptr.as.as_ptr;
    if (p == NULL) {
        hml_runtime_error("Cannot index into null pointer");
    }
    int idx = hml_to_i32(index);
    // Return the byte as u8 (matching interpreter behavior)
    return hml_val_u8(((unsigned char *)p)[idx]);
}

void hml_ptr_set(HmlValue ptr, HmlValue index, HmlValue val) {
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("Pointer index assignment requires pointer");
    }
    void *p = ptr.as.as_ptr;
    if (p == NULL) {
        hml_runtime_error("Cannot index into null pointer");
    }
    int idx = hml_to_i32(index);
    // Treat as byte array (matching interpreter behavior)
    ((unsigned char *)p)[idx] = (unsigned char)hml_to_i32(val);
}

// ========== FFI CALLBACK OPERATIONS ==========

// Forward declaration
HmlValue hml_builtin_callback(HmlClosureEnv *env, HmlValue fn, HmlValue param_types, HmlValue return_type);
HmlValue hml_builtin_callback_free(HmlClosureEnv *env, HmlValue ptr);

// Create an FFI callback that wraps a Hemlock function
// Delegates to hml_builtin_callback which has the full implementation
HmlValue hml_callback_create(HmlValue fn, HmlValue arg_types, HmlValue ret_type) {
    return hml_builtin_callback(NULL, fn, arg_types, ret_type);
}

// Free an FFI callback
void hml_callback_free(HmlValue callback) {
    hml_builtin_callback_free(NULL, callback);
}

// ========== MEMORY OPERATIONS ==========

HmlValue hml_alloc(int32_t size) {
    if (size <= 0) {
        hml_runtime_error("alloc() requires positive size");
    }
    void *ptr = malloc(size);
    if (!ptr) {
        return hml_val_null();
    }
    return hml_val_ptr(ptr);
}

void hml_free(HmlValue ptr_or_buffer) {
    if (ptr_or_buffer.type == HML_VAL_PTR) {
        if (ptr_or_buffer.as.as_ptr) {
            free(ptr_or_buffer.as.as_ptr);
        }
    } else if (ptr_or_buffer.type == HML_VAL_BUFFER) {
        if (ptr_or_buffer.as.as_buffer) {
            if (ptr_or_buffer.as.as_buffer->data) {
                free(ptr_or_buffer.as.as_buffer->data);
            }
            free(ptr_or_buffer.as.as_buffer);
        }
    } else if (ptr_or_buffer.type == HML_VAL_ARRAY) {
        if (ptr_or_buffer.as.as_array) {
            HmlArray *arr = ptr_or_buffer.as.as_array;
            // Release all elements
            for (int i = 0; i < arr->length; i++) {
                hml_release(&arr->elements[i]);
            }
            free(arr->elements);
            free(arr);
        }
    } else if (ptr_or_buffer.type == HML_VAL_OBJECT) {
        if (ptr_or_buffer.as.as_object) {
            HmlObject *obj = ptr_or_buffer.as.as_object;
            // Release all field values and free names
            for (int i = 0; i < obj->num_fields; i++) {
                hml_release(&obj->field_values[i]);
                free(obj->field_names[i]);
            }
            free(obj->field_names);
            free(obj->field_values);
            if (obj->type_name) free(obj->type_name);
            free(obj);
        }
    } else if (ptr_or_buffer.type == HML_VAL_NULL) {
        // free(null) is a safe no-op (like C's free(NULL))
    } else {
        hml_runtime_error("free() requires pointer, buffer, object, or array");
    }
}

HmlValue hml_realloc(HmlValue ptr, int32_t new_size) {
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("realloc() requires pointer");
    }
    if (new_size <= 0) {
        hml_runtime_error("realloc() requires positive size");
    }
    void *new_ptr = realloc(ptr.as.as_ptr, new_size);
    if (!new_ptr) {
        return hml_val_null();
    }
    return hml_val_ptr(new_ptr);
}

void hml_memset(HmlValue ptr, uint8_t byte_val, int32_t size) {
    if (ptr.type == HML_VAL_PTR) {
        memset(ptr.as.as_ptr, byte_val, size);
    } else if (ptr.type == HML_VAL_BUFFER) {
        memset(ptr.as.as_buffer->data, byte_val, size);
    } else {
        hml_runtime_error("memset() requires pointer or buffer");
    }
}

void hml_memcpy(HmlValue dest, HmlValue src, int32_t size) {
    void *dest_ptr = NULL;
    void *src_ptr = NULL;

    if (dest.type == HML_VAL_PTR) {
        dest_ptr = dest.as.as_ptr;
    } else if (dest.type == HML_VAL_BUFFER) {
        dest_ptr = dest.as.as_buffer->data;
    } else {
        hml_runtime_error("memcpy() dest requires pointer or buffer");
    }

    if (src.type == HML_VAL_PTR) {
        src_ptr = src.as.as_ptr;
    } else if (src.type == HML_VAL_BUFFER) {
        src_ptr = src.as.as_buffer->data;
    } else {
        hml_runtime_error("memcpy() src requires pointer or buffer");
    }

    memcpy(dest_ptr, src_ptr, size);
}

int32_t hml_sizeof_type(HmlValueType type) {
    switch (type) {
        case HML_VAL_I8:    return 1;
        case HML_VAL_U8:    return 1;
        case HML_VAL_I16:   return 2;
        case HML_VAL_U16:   return 2;
        case HML_VAL_I32:   return 4;
        case HML_VAL_U32:   return 4;
        case HML_VAL_I64:   return 8;
        case HML_VAL_U64:   return 8;
        case HML_VAL_F32:   return 4;
        case HML_VAL_F64:   return 8;
        case HML_VAL_BOOL:  return 1;
        case HML_VAL_PTR:   return 8;
        case HML_VAL_RUNE:  return 4;
        default:            return 0;
    }
}

// Helper to convert string type name to HmlValueType
static HmlValueType hml_type_from_string(const char *name) {
    if (strcmp(name, "i8") == 0) return HML_VAL_I8;
    if (strcmp(name, "i16") == 0) return HML_VAL_I16;
    if (strcmp(name, "i32") == 0 || strcmp(name, "integer") == 0) return HML_VAL_I32;
    if (strcmp(name, "i64") == 0) return HML_VAL_I64;
    if (strcmp(name, "u8") == 0 || strcmp(name, "byte") == 0) return HML_VAL_U8;
    if (strcmp(name, "u16") == 0) return HML_VAL_U16;
    if (strcmp(name, "u32") == 0) return HML_VAL_U32;
    if (strcmp(name, "u64") == 0) return HML_VAL_U64;
    if (strcmp(name, "f32") == 0) return HML_VAL_F32;
    if (strcmp(name, "f64") == 0 || strcmp(name, "number") == 0) return HML_VAL_F64;
    if (strcmp(name, "bool") == 0) return HML_VAL_BOOL;
    if (strcmp(name, "ptr") == 0) return HML_VAL_PTR;
    if (strcmp(name, "rune") == 0) return HML_VAL_RUNE;
    return HML_VAL_NULL;  // Unknown type
}

HmlValue hml_talloc(HmlValue type_name, HmlValue count) {
    // Type name must be a string
    if (type_name.type != HML_VAL_STRING || !type_name.as.as_string) {
        hml_runtime_error("talloc() first argument must be a type name string");
    }

    // Count must be an integer
    if (!hml_is_integer(count)) {
        hml_runtime_error("talloc() second argument must be an integer count");
    }

    int32_t n = hml_to_i32(count);
    if (n <= 0) {
        hml_runtime_error("talloc() count must be positive");
    }

    HmlValueType elem_type = hml_type_from_string(type_name.as.as_string->data);
    if (elem_type == HML_VAL_NULL) {
        hml_runtime_error("talloc() unknown type '%s'", type_name.as.as_string->data);
    }

    int32_t elem_size = hml_sizeof_type(elem_type);
    if (elem_size == 0) {
        hml_runtime_error("talloc() type '%s' has no known size", type_name.as.as_string->data);
    }

    // Check for integer overflow before multiplication
    size_t safe_elem_size = (size_t)elem_size;
    size_t safe_count = (size_t)n;
    if (safe_count > 0 && safe_elem_size > SIZE_MAX / safe_count) {
        hml_runtime_error("talloc() overflow: %s * %d would exceed maximum allocation size",
                         type_name.as.as_string->data, n);
    }

    size_t total_size = safe_elem_size * safe_count;
    void *ptr = malloc(total_size);
    if (!ptr) {
        return hml_val_null();
    }

    return hml_val_ptr(ptr);
}

HmlValue hml_builtin_talloc(HmlClosureEnv *env, HmlValue type_name, HmlValue count) {
    (void)env;
    return hml_talloc(type_name, count);
}

// ========== ADDITIONAL POINTER HELPERS FOR ALL TYPES ==========

// Builtin: ptr_deref_i8(ptr) -> i8
HmlValue hml_builtin_ptr_deref_i8(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_deref_i8() argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_deref_i8() cannot dereference null pointer");
    }
    return hml_val_i8(*(int8_t*)p);
}

// Builtin: ptr_deref_i16(ptr) -> i16
HmlValue hml_builtin_ptr_deref_i16(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_deref_i16() argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_deref_i16() cannot dereference null pointer");
    }
    return hml_val_i16(*(int16_t*)p);
}

// Builtin: ptr_deref_i64(ptr) -> i64
HmlValue hml_builtin_ptr_deref_i64(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_deref_i64() argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_deref_i64() cannot dereference null pointer");
    }
    return hml_val_i64(*(int64_t*)p);
}

// Builtin: ptr_deref_u8(ptr) -> u8
HmlValue hml_builtin_ptr_deref_u8(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_deref_u8() argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_deref_u8() cannot dereference null pointer");
    }
    return hml_val_u8(*(uint8_t*)p);
}

// Builtin: ptr_deref_u16(ptr) -> u16
HmlValue hml_builtin_ptr_deref_u16(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_deref_u16() argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_deref_u16() cannot dereference null pointer");
    }
    return hml_val_u16(*(uint16_t*)p);
}

// Builtin: ptr_deref_u32(ptr) -> u32
HmlValue hml_builtin_ptr_deref_u32(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_deref_u32() argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_deref_u32() cannot dereference null pointer");
    }
    return hml_val_u32(*(uint32_t*)p);
}

// Builtin: ptr_deref_u64(ptr) -> u64
HmlValue hml_builtin_ptr_deref_u64(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_deref_u64() argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_deref_u64() cannot dereference null pointer");
    }
    return hml_val_u64(*(uint64_t*)p);
}

// Builtin: ptr_deref_f32(ptr) -> f32
HmlValue hml_builtin_ptr_deref_f32(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_deref_f32() argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_deref_f32() cannot dereference null pointer");
    }
    return hml_val_f32(*(float*)p);
}

// Builtin: ptr_deref_f64(ptr) -> f64
HmlValue hml_builtin_ptr_deref_f64(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_deref_f64() argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_deref_f64() cannot dereference null pointer");
    }
    return hml_val_f64(*(double*)p);
}

// Builtin: ptr_deref_ptr(ptr) -> ptr (pointer-to-pointer)
HmlValue hml_builtin_ptr_deref_ptr(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_deref_ptr() argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_deref_ptr() cannot dereference null pointer");
    }
    return hml_val_ptr(*(void**)p);
}

// Builtin: ptr_write_i8(ptr, value)
HmlValue hml_builtin_ptr_write_i8(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_write_i8() first argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_write_i8() cannot write to null pointer");
    }
    *(int8_t*)p = (int8_t)hml_to_i32(value);
    return hml_val_null();
}

// Builtin: ptr_write_i16(ptr, value)
HmlValue hml_builtin_ptr_write_i16(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_write_i16() first argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_write_i16() cannot write to null pointer");
    }
    *(int16_t*)p = (int16_t)hml_to_i32(value);
    return hml_val_null();
}

// Builtin: ptr_write_i64(ptr, value)
HmlValue hml_builtin_ptr_write_i64(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_write_i64() first argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_write_i64() cannot write to null pointer");
    }
    *(int64_t*)p = hml_to_i64(value);
    return hml_val_null();
}

// Builtin: ptr_write_u8(ptr, value)
HmlValue hml_builtin_ptr_write_u8(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_write_u8() first argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_write_u8() cannot write to null pointer");
    }
    *(uint8_t*)p = (uint8_t)hml_to_i32(value);
    return hml_val_null();
}

// Builtin: ptr_write_u16(ptr, value)
HmlValue hml_builtin_ptr_write_u16(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_write_u16() first argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_write_u16() cannot write to null pointer");
    }
    *(uint16_t*)p = (uint16_t)hml_to_i32(value);
    return hml_val_null();
}

// Builtin: ptr_write_u32(ptr, value)
HmlValue hml_builtin_ptr_write_u32(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_write_u32() first argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_write_u32() cannot write to null pointer");
    }
    *(uint32_t*)p = (uint32_t)hml_to_i64(value);
    return hml_val_null();
}

// Builtin: ptr_write_u64(ptr, value)
HmlValue hml_builtin_ptr_write_u64(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_write_u64() first argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_write_u64() cannot write to null pointer");
    }
    *(uint64_t*)p = (uint64_t)hml_to_i64(value);
    return hml_val_null();
}

// Builtin: ptr_write_f32(ptr, value)
HmlValue hml_builtin_ptr_write_f32(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_write_f32() first argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_write_f32() cannot write to null pointer");
    }
    *(float*)p = (float)hml_to_f64(value);
    return hml_val_null();
}

// Builtin: ptr_write_f64(ptr, value)
HmlValue hml_builtin_ptr_write_f64(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_write_f64() first argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_write_f64() cannot write to null pointer");
    }
    *(double*)p = hml_to_f64(value);
    return hml_val_null();
}

// Builtin: ptr_write_ptr(ptr, value)
HmlValue hml_builtin_ptr_write_ptr(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_write_ptr() first argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_write_ptr() cannot write to null pointer");
    }
    if (value.type == HML_VAL_NULL) {
        *(void**)p = NULL;
    } else if (value.type == HML_VAL_PTR) {
        *(void**)p = value.as.as_ptr;
    } else {
        hml_runtime_error("ptr_write_ptr() second argument must be a ptr or null");
    }
    return hml_val_null();
}

// Builtin: ffi_sizeof(type_name) -> i32
HmlValue hml_builtin_ffi_sizeof(HmlClosureEnv *env, HmlValue type_name) {
    (void)env;
    if (type_name.type != HML_VAL_STRING || !type_name.as.as_string) {
        hml_runtime_error("ffi_sizeof() argument must be a type name string");
    }
    const char *name = type_name.as.as_string->data;

    if (strcmp(name, "i8") == 0) return hml_val_i32(sizeof(int8_t));
    if (strcmp(name, "i16") == 0) return hml_val_i32(sizeof(int16_t));
    if (strcmp(name, "i32") == 0) return hml_val_i32(sizeof(int32_t));
    if (strcmp(name, "i64") == 0) return hml_val_i32(sizeof(int64_t));
    if (strcmp(name, "u8") == 0) return hml_val_i32(sizeof(uint8_t));
    if (strcmp(name, "u16") == 0) return hml_val_i32(sizeof(uint16_t));
    if (strcmp(name, "u32") == 0) return hml_val_i32(sizeof(uint32_t));
    if (strcmp(name, "u64") == 0) return hml_val_i32(sizeof(uint64_t));
    if (strcmp(name, "f32") == 0) return hml_val_i32(sizeof(float));
    if (strcmp(name, "f64") == 0) return hml_val_i32(sizeof(double));
    if (strcmp(name, "ptr") == 0) return hml_val_i32(sizeof(void*));
    if (strcmp(name, "size_t") == 0 || strcmp(name, "usize") == 0) return hml_val_i32(sizeof(size_t));
    if (strcmp(name, "intptr_t") == 0 || strcmp(name, "isize") == 0) return hml_val_i32(sizeof(intptr_t));

    hml_runtime_error("ffi_sizeof(): unknown type '%s'", name);
    return hml_val_null();  // Unreachable
}

// Builtin: ptr_to_buffer(ptr, size) -> buffer
HmlValue hml_builtin_ptr_to_buffer(HmlClosureEnv *env, HmlValue ptr, HmlValue size) {
    (void)env;
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_to_buffer() first argument must be a ptr");
    }
    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_to_buffer() cannot read from null pointer");
    }
    int32_t sz = hml_to_i32(size);
    if (sz <= 0) {
        hml_runtime_error("ptr_to_buffer() size must be positive");
    }

    // Create a new buffer and copy data from the pointer
    HmlValue buf = hml_val_buffer(sz);
    memcpy(buf.as.as_buffer->data, p, sz);
    return buf;
}

// Builtin: buffer_ptr(buffer) -> ptr
HmlValue hml_builtin_buffer_ptr(HmlClosureEnv *env, HmlValue buf) {
    (void)env;
    if (buf.type != HML_VAL_BUFFER || !buf.as.as_buffer) {
        hml_runtime_error("buffer_ptr() argument must be a buffer");
    }
    return hml_val_ptr(buf.as.as_buffer->data);
}

// Builtin: ptr_null() -> ptr
HmlValue hml_builtin_ptr_null(HmlClosureEnv *env) {
    (void)env;
    return hml_val_ptr(NULL);
}

// Compression and cryptographic operations moved to builtins_crypto.c

