#include "internal.h"

// ========== FFI CALLBACK BUILTINS ==========

// callback(fn, param_types, return_type) - Create a C-callable function pointer
// param_types is an array of type name strings: ["ptr", "ptr"]
// return_type is a type name string: "i32"
Value builtin_callback(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args < 2 || num_args > 3) {
        runtime_error(ctx, "callback() expects 2-3 arguments (fn, param_types, [return_type])");
        return val_null();
    }

    // First argument: function
    if (args[0].type != VAL_FUNCTION) {
        runtime_error(ctx, "callback() first argument must be a function");
        return val_null();
    }
    Function *fn = args[0].as.as_function;

    // Second argument: array of parameter type names
    if (args[1].type != VAL_ARRAY) {
        runtime_error(ctx, "callback() second argument must be an array of type names");
        return val_null();
    }
    Array *param_arr = args[1].as.as_array;
    int num_params = param_arr->length;

    // Build parameter types
    Type **param_types = malloc(sizeof(Type*) * num_params);
    if (!param_types) {
        runtime_error(ctx, "callback(): memory allocation failed");
        return val_null();
    }
    for (int i = 0; i < num_params; i++) {
        Value type_val = param_arr->elements[i];
        if (type_val.type != VAL_STRING) {
            runtime_error(ctx, "callback() param_types must contain type name strings");
            free(param_types);
            return val_null();
        }
        param_types[i] = type_from_string(type_val.as.as_string->data);
    }

    // Third argument: return type (optional, defaults to void)
    Type *return_type;
    if (num_args >= 3) {
        if (args[2].type != VAL_STRING) {
            runtime_error(ctx, "callback() return_type must be a type name string");
            for (int i = 0; i < num_params; i++) {
                free(param_types[i]);
            }
            free(param_types);
            return val_null();
        }
        return_type = type_from_string(args[2].as.as_string->data);
    } else {
        return_type = type_from_string("void");
    }

    // Create the callback
    FFICallback *cb = ffi_create_callback(fn, param_types, num_params, return_type, ctx);

    // Note: param_types are now owned by FFICallback, so don't free them here
    // But we do need to free return_type if callback creation failed
    if (cb == NULL) {
        for (int i = 0; i < num_params; i++) {
            free(param_types[i]);
        }
        free(param_types);
        free(return_type);
        return val_null();
    }

    // Return the C-callable function pointer as a ptr
    return val_ptr(ffi_callback_get_ptr(cb));
}

// callback_free(ptr) - Free a callback created by callback()
// Note: The ptr is the function pointer returned by callback()
Value builtin_callback_free(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "callback_free() expects 1 argument (ptr)");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "callback_free() argument must be a ptr returned by callback()");
        return val_null();
    }

    void *ptr = args[0].as.as_ptr;

    // Free the callback by its code pointer
    int success = ffi_free_callback_by_ptr(ptr);
    if (!success) {
        runtime_error(ctx, "callback_free(): pointer is not a valid callback");
        return val_null();
    }

    return val_null();
}

// ptr_read_i32(ptr) - Read an i32 from a pointer (for qsort comparators)
Value builtin_ptr_read_i32(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "ptr_read_i32() expects 1 argument (ptr)");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "ptr_read_i32() argument must be a ptr");
        return val_null();
    }

    void *ptr = args[0].as.as_ptr;
    if (ptr == NULL) {
        runtime_error(ctx, "ptr_read_i32() cannot read from null pointer");
        return val_null();
    }

    // Read through pointer-to-pointer (qsort passes ptr to element, not element itself)
    int32_t *actual_ptr = *(int32_t**)ptr;
    return val_i32(*actual_ptr);
}

// ptr_deref_i32(ptr) - Dereference a pointer to read an i32 directly
Value builtin_ptr_deref_i32(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "ptr_deref_i32() expects 1 argument (ptr)");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "ptr_deref_i32() argument must be a ptr");
        return val_null();
    }

    void *ptr = args[0].as.as_ptr;
    if (ptr == NULL) {
        runtime_error(ctx, "ptr_deref_i32() cannot dereference null pointer");
        return val_null();
    }

    // Direct dereference
    return val_i32(*(int32_t*)ptr);
}

// ptr_write_i32(ptr, value) - Write an i32 to a pointer
Value builtin_ptr_write_i32(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "ptr_write_i32() expects 2 arguments (ptr, value)");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "ptr_write_i32() first argument must be a ptr");
        return val_null();
    }

    void *ptr = args[0].as.as_ptr;
    if (ptr == NULL) {
        runtime_error(ctx, "ptr_write_i32() cannot write to null pointer");
        return val_null();
    }

    // Convert value to i32
    int32_t value;
    if (args[1].type == VAL_I32) {
        value = args[1].as.as_i32;
    } else if (args[1].type == VAL_I64) {
        value = (int32_t)args[1].as.as_i64;
    } else if (args[1].type == VAL_I16) {
        value = args[1].as.as_i16;
    } else if (args[1].type == VAL_I8) {
        value = args[1].as.as_i8;
    } else if (args[1].type == VAL_U32) {
        value = (int32_t)args[1].as.as_u32;
    } else if (args[1].type == VAL_U16) {
        value = args[1].as.as_u16;
    } else if (args[1].type == VAL_U8) {
        value = args[1].as.as_u8;
    } else {
        runtime_error(ctx, "ptr_write_i32() second argument must be an integer");
        return val_null();
    }

    *(int32_t*)ptr = value;
    return val_null();
}

// ptr_offset(ptr, offset, element_size) - Calculate pointer offset
Value builtin_ptr_offset(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 3) {
        runtime_error(ctx, "ptr_offset() expects 3 arguments (ptr, offset, element_size)");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "ptr_offset() first argument must be a ptr");
        return val_null();
    }

    void *ptr = args[0].as.as_ptr;
    int64_t offset = value_to_int64(args[1]);
    int64_t element_size = value_to_int64(args[2]);

    // Calculate new pointer
    char *base = (char*)ptr;
    return val_ptr(base + (offset * element_size));
}

// ========== ADDITIONAL POINTER HELPERS FOR ALL TYPES ==========

// ptr_deref_i8(ptr) - Dereference a pointer to read an i8
Value builtin_ptr_deref_i8(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "ptr_deref_i8() expects 1 argument (ptr)");
        return val_null();
    }
    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "ptr_deref_i8() argument must be a ptr");
        return val_null();
    }
    void *ptr = args[0].as.as_ptr;
    if (ptr == NULL) {
        runtime_error(ctx, "ptr_deref_i8() cannot dereference null pointer");
        return val_null();
    }
    return val_i8(*(int8_t*)ptr);
}

// ptr_deref_i16(ptr) - Dereference a pointer to read an i16
Value builtin_ptr_deref_i16(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "ptr_deref_i16() expects 1 argument (ptr)");
        return val_null();
    }
    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "ptr_deref_i16() argument must be a ptr");
        return val_null();
    }
    void *ptr = args[0].as.as_ptr;
    if (ptr == NULL) {
        runtime_error(ctx, "ptr_deref_i16() cannot dereference null pointer");
        return val_null();
    }
    return val_i16(*(int16_t*)ptr);
}

// ptr_deref_i64(ptr) - Dereference a pointer to read an i64
Value builtin_ptr_deref_i64(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "ptr_deref_i64() expects 1 argument (ptr)");
        return val_null();
    }
    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "ptr_deref_i64() argument must be a ptr");
        return val_null();
    }
    void *ptr = args[0].as.as_ptr;
    if (ptr == NULL) {
        runtime_error(ctx, "ptr_deref_i64() cannot dereference null pointer");
        return val_null();
    }
    return val_i64(*(int64_t*)ptr);
}

// ptr_deref_u8(ptr) - Dereference a pointer to read a u8
Value builtin_ptr_deref_u8(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "ptr_deref_u8() expects 1 argument (ptr)");
        return val_null();
    }
    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "ptr_deref_u8() argument must be a ptr");
        return val_null();
    }
    void *ptr = args[0].as.as_ptr;
    if (ptr == NULL) {
        runtime_error(ctx, "ptr_deref_u8() cannot dereference null pointer");
        return val_null();
    }
    return val_u8(*(uint8_t*)ptr);
}

// ptr_deref_u16(ptr) - Dereference a pointer to read a u16
Value builtin_ptr_deref_u16(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "ptr_deref_u16() expects 1 argument (ptr)");
        return val_null();
    }
    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "ptr_deref_u16() argument must be a ptr");
        return val_null();
    }
    void *ptr = args[0].as.as_ptr;
    if (ptr == NULL) {
        runtime_error(ctx, "ptr_deref_u16() cannot dereference null pointer");
        return val_null();
    }
    return val_u16(*(uint16_t*)ptr);
}

// ptr_deref_u32(ptr) - Dereference a pointer to read a u32
Value builtin_ptr_deref_u32(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "ptr_deref_u32() expects 1 argument (ptr)");
        return val_null();
    }
    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "ptr_deref_u32() argument must be a ptr");
        return val_null();
    }
    void *ptr = args[0].as.as_ptr;
    if (ptr == NULL) {
        runtime_error(ctx, "ptr_deref_u32() cannot dereference null pointer");
        return val_null();
    }
    return val_u32(*(uint32_t*)ptr);
}

// ptr_deref_u64(ptr) - Dereference a pointer to read a u64
Value builtin_ptr_deref_u64(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "ptr_deref_u64() expects 1 argument (ptr)");
        return val_null();
    }
    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "ptr_deref_u64() argument must be a ptr");
        return val_null();
    }
    void *ptr = args[0].as.as_ptr;
    if (ptr == NULL) {
        runtime_error(ctx, "ptr_deref_u64() cannot dereference null pointer");
        return val_null();
    }
    return val_u64(*(uint64_t*)ptr);
}

// ptr_deref_f32(ptr) - Dereference a pointer to read an f32
Value builtin_ptr_deref_f32(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "ptr_deref_f32() expects 1 argument (ptr)");
        return val_null();
    }
    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "ptr_deref_f32() argument must be a ptr");
        return val_null();
    }
    void *ptr = args[0].as.as_ptr;
    if (ptr == NULL) {
        runtime_error(ctx, "ptr_deref_f32() cannot dereference null pointer");
        return val_null();
    }
    return val_f32(*(float*)ptr);
}

// ptr_deref_f64(ptr) - Dereference a pointer to read an f64
Value builtin_ptr_deref_f64(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "ptr_deref_f64() expects 1 argument (ptr)");
        return val_null();
    }
    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "ptr_deref_f64() argument must be a ptr");
        return val_null();
    }
    void *ptr = args[0].as.as_ptr;
    if (ptr == NULL) {
        runtime_error(ctx, "ptr_deref_f64() cannot dereference null pointer");
        return val_null();
    }
    return val_f64(*(double*)ptr);
}

// ptr_deref_ptr(ptr) - Dereference a pointer to read a pointer (pointer-to-pointer)
Value builtin_ptr_deref_ptr(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "ptr_deref_ptr() expects 1 argument (ptr)");
        return val_null();
    }
    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "ptr_deref_ptr() argument must be a ptr");
        return val_null();
    }
    void *ptr = args[0].as.as_ptr;
    if (ptr == NULL) {
        runtime_error(ctx, "ptr_deref_ptr() cannot dereference null pointer");
        return val_null();
    }
    return val_ptr(*(void**)ptr);
}

// ========== POINTER WRITE HELPERS FOR ALL TYPES ==========

// ptr_write_i8(ptr, value) - Write an i8 to a pointer
Value builtin_ptr_write_i8(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "ptr_write_i8() expects 2 arguments (ptr, value)");
        return val_null();
    }
    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "ptr_write_i8() first argument must be a ptr");
        return val_null();
    }
    void *ptr = args[0].as.as_ptr;
    if (ptr == NULL) {
        runtime_error(ctx, "ptr_write_i8() cannot write to null pointer");
        return val_null();
    }
    *(int8_t*)ptr = (int8_t)value_to_int(args[1]);
    return val_null();
}

// ptr_write_i16(ptr, value) - Write an i16 to a pointer
Value builtin_ptr_write_i16(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "ptr_write_i16() expects 2 arguments (ptr, value)");
        return val_null();
    }
    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "ptr_write_i16() first argument must be a ptr");
        return val_null();
    }
    void *ptr = args[0].as.as_ptr;
    if (ptr == NULL) {
        runtime_error(ctx, "ptr_write_i16() cannot write to null pointer");
        return val_null();
    }
    *(int16_t*)ptr = (int16_t)value_to_int(args[1]);
    return val_null();
}

// ptr_write_i64(ptr, value) - Write an i64 to a pointer
Value builtin_ptr_write_i64(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "ptr_write_i64() expects 2 arguments (ptr, value)");
        return val_null();
    }
    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "ptr_write_i64() first argument must be a ptr");
        return val_null();
    }
    void *ptr = args[0].as.as_ptr;
    if (ptr == NULL) {
        runtime_error(ctx, "ptr_write_i64() cannot write to null pointer");
        return val_null();
    }
    *(int64_t*)ptr = value_to_int64(args[1]);
    return val_null();
}

// ptr_write_u8(ptr, value) - Write a u8 to a pointer
Value builtin_ptr_write_u8(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "ptr_write_u8() expects 2 arguments (ptr, value)");
        return val_null();
    }
    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "ptr_write_u8() first argument must be a ptr");
        return val_null();
    }
    void *ptr = args[0].as.as_ptr;
    if (ptr == NULL) {
        runtime_error(ctx, "ptr_write_u8() cannot write to null pointer");
        return val_null();
    }
    *(uint8_t*)ptr = (uint8_t)value_to_int(args[1]);
    return val_null();
}

// ptr_write_u16(ptr, value) - Write a u16 to a pointer
Value builtin_ptr_write_u16(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "ptr_write_u16() expects 2 arguments (ptr, value)");
        return val_null();
    }
    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "ptr_write_u16() first argument must be a ptr");
        return val_null();
    }
    void *ptr = args[0].as.as_ptr;
    if (ptr == NULL) {
        runtime_error(ctx, "ptr_write_u16() cannot write to null pointer");
        return val_null();
    }
    *(uint16_t*)ptr = (uint16_t)value_to_int(args[1]);
    return val_null();
}

// ptr_write_u32(ptr, value) - Write a u32 to a pointer
Value builtin_ptr_write_u32(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "ptr_write_u32() expects 2 arguments (ptr, value)");
        return val_null();
    }
    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "ptr_write_u32() first argument must be a ptr");
        return val_null();
    }
    void *ptr = args[0].as.as_ptr;
    if (ptr == NULL) {
        runtime_error(ctx, "ptr_write_u32() cannot write to null pointer");
        return val_null();
    }
    *(uint32_t*)ptr = (uint32_t)value_to_int64(args[1]);
    return val_null();
}

// ptr_write_u64(ptr, value) - Write a u64 to a pointer
Value builtin_ptr_write_u64(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "ptr_write_u64() expects 2 arguments (ptr, value)");
        return val_null();
    }
    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "ptr_write_u64() first argument must be a ptr");
        return val_null();
    }
    void *ptr = args[0].as.as_ptr;
    if (ptr == NULL) {
        runtime_error(ctx, "ptr_write_u64() cannot write to null pointer");
        return val_null();
    }
    *(uint64_t*)ptr = (uint64_t)value_to_int64(args[1]);
    return val_null();
}

// ptr_write_f32(ptr, value) - Write an f32 to a pointer
Value builtin_ptr_write_f32(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "ptr_write_f32() expects 2 arguments (ptr, value)");
        return val_null();
    }
    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "ptr_write_f32() first argument must be a ptr");
        return val_null();
    }
    void *ptr = args[0].as.as_ptr;
    if (ptr == NULL) {
        runtime_error(ctx, "ptr_write_f32() cannot write to null pointer");
        return val_null();
    }
    *(float*)ptr = (float)value_to_float(args[1]);
    return val_null();
}

// ptr_write_f64(ptr, value) - Write an f64 to a pointer
Value builtin_ptr_write_f64(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "ptr_write_f64() expects 2 arguments (ptr, value)");
        return val_null();
    }
    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "ptr_write_f64() first argument must be a ptr");
        return val_null();
    }
    void *ptr = args[0].as.as_ptr;
    if (ptr == NULL) {
        runtime_error(ctx, "ptr_write_f64() cannot write to null pointer");
        return val_null();
    }
    *(double*)ptr = value_to_float(args[1]);
    return val_null();
}

// ptr_write_ptr(ptr, value) - Write a pointer to a pointer location
Value builtin_ptr_write_ptr(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "ptr_write_ptr() expects 2 arguments (ptr, value)");
        return val_null();
    }
    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "ptr_write_ptr() first argument must be a ptr");
        return val_null();
    }
    void *ptr = args[0].as.as_ptr;
    if (ptr == NULL) {
        runtime_error(ctx, "ptr_write_ptr() cannot write to null pointer");
        return val_null();
    }
    if (args[1].type != VAL_PTR && args[1].type != VAL_NULL) {
        runtime_error(ctx, "ptr_write_ptr() second argument must be a ptr or null");
        return val_null();
    }
    *(void**)ptr = (args[1].type == VAL_NULL) ? NULL : args[1].as.as_ptr;
    return val_null();
}

// ========== FFI UTILITY FUNCTIONS ==========

// ffi_sizeof(type_name) - Get the size in bytes of an FFI type
Value builtin_ffi_sizeof(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "ffi_sizeof() expects 1 argument (type_name)");
        return val_null();
    }
    if (args[0].type != VAL_STRING) {
        runtime_error(ctx, "ffi_sizeof() argument must be a type name string");
        return val_null();
    }

    const char *name = args[0].as.as_string->data;

    if (strcmp(name, "i8") == 0) return val_i32(sizeof(int8_t));
    if (strcmp(name, "i16") == 0) return val_i32(sizeof(int16_t));
    if (strcmp(name, "i32") == 0) return val_i32(sizeof(int32_t));
    if (strcmp(name, "i64") == 0) return val_i32(sizeof(int64_t));
    if (strcmp(name, "u8") == 0) return val_i32(sizeof(uint8_t));
    if (strcmp(name, "u16") == 0) return val_i32(sizeof(uint16_t));
    if (strcmp(name, "u32") == 0) return val_i32(sizeof(uint32_t));
    if (strcmp(name, "u64") == 0) return val_i32(sizeof(uint64_t));
    if (strcmp(name, "f32") == 0) return val_i32(sizeof(float));
    if (strcmp(name, "f64") == 0) return val_i32(sizeof(double));
    if (strcmp(name, "ptr") == 0) return val_i32(sizeof(void*));
    if (strcmp(name, "size_t") == 0 || strcmp(name, "usize") == 0) return val_i32(sizeof(size_t));
    if (strcmp(name, "intptr_t") == 0 || strcmp(name, "isize") == 0) return val_i32(sizeof(intptr_t));

    runtime_error(ctx, "ffi_sizeof(): unknown type '%s'", name);
    return val_null();
}

// ptr_to_buffer(ptr, size) - Copy data from a pointer into a new buffer
Value builtin_ptr_to_buffer(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "ptr_to_buffer() expects 2 arguments (ptr, size)");
        return val_null();
    }
    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "ptr_to_buffer() first argument must be a ptr");
        return val_null();
    }
    void *ptr = args[0].as.as_ptr;
    if (ptr == NULL) {
        runtime_error(ctx, "ptr_to_buffer() cannot read from null pointer");
        return val_null();
    }
    int32_t size = value_to_int(args[1]);
    if (size <= 0) {
        runtime_error(ctx, "ptr_to_buffer() size must be positive");
        return val_null();
    }

    // Create a new buffer and copy data from the pointer
    Value buf_val = val_buffer(size);
    if (buf_val.type == VAL_NULL) {
        runtime_error(ctx, "ptr_to_buffer() failed to allocate buffer");
        return val_null();
    }
    memcpy(buf_val.as.as_buffer->data, ptr, size);
    return buf_val;
}

// buffer_ptr(buffer) - Get the raw pointer from a buffer
Value builtin_buffer_ptr(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "buffer_ptr() expects 1 argument (buffer)");
        return val_null();
    }
    if (args[0].type != VAL_BUFFER) {
        runtime_error(ctx, "buffer_ptr() argument must be a buffer");
        return val_null();
    }
    Buffer *buf = args[0].as.as_buffer;
    return val_ptr(buf->data);
}

// ptr_null() - Get a null pointer constant
Value builtin_ptr_null(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        runtime_error(ctx, "ptr_null() expects no arguments");
        return val_null();
    }
    return val_ptr(NULL);
}
