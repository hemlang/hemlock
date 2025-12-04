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
