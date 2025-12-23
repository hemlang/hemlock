#include "internal.h"

Value builtin_read_u32(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: __read_u32() expects 1 argument (ptr)\n");
        exit(1);
    }

    if (args[0].type != VAL_PTR) {
        fprintf(stderr, "Runtime error: __read_u32() requires a pointer\n");
        exit(1);
    }

    uint32_t *ptr = (uint32_t*)args[0].as.as_ptr;
    return val_u32(*ptr);
}

Value builtin_read_u64(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: __read_u64() expects 1 argument (ptr)\n");
        exit(1);
    }

    if (args[0].type != VAL_PTR) {
        fprintf(stderr, "Runtime error: __read_u64() requires a pointer\n");
        exit(1);
    }

    uint64_t *ptr = (uint64_t*)args[0].as.as_ptr;
    return val_u64(*ptr);
}

// Read a pointer from memory (for pointer-to-pointer / double indirection FFI calls)
Value builtin_read_ptr(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: __read_ptr() expects 1 argument (ptr)\n");
        exit(1);
    }

    if (args[0].type != VAL_PTR) {
        fprintf(stderr, "Runtime error: __read_ptr() requires a pointer\n");
        exit(1);
    }

    void **pptr = (void**)args[0].as.as_ptr;
    return val_ptr(*pptr);
}

Value builtin_strerror_fn(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    (void)ctx;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: __strerror() expects 0 arguments\n");
        exit(1);
    }

    return val_string(strerror(errno));
}

Value builtin_dirent_name(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: __dirent_name() expects 1 argument (dirent ptr)\n");
        exit(1);
    }

    if (args[0].type != VAL_PTR) {
        fprintf(stderr, "Runtime error: __dirent_name() requires a pointer\n");
        exit(1);
    }

    struct dirent *entry = (struct dirent*)args[0].as.as_ptr;
    return val_string(entry->d_name);
}

Value builtin_string_to_cstr(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: __string_to_cstr() expects 1 argument (string)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: __string_to_cstr() requires a string\n");
        exit(1);
    }

    String *str = args[0].as.as_string;
    char *cstr = malloc(str->length + 1);
    if (!cstr) {
        fprintf(stderr, "Runtime error: __string_to_cstr() memory allocation failed\n");
        exit(1);
    }
    memcpy(cstr, str->data, str->length);
    cstr[str->length] = '\0';
    return val_ptr(cstr);
}

Value builtin_cstr_to_string(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: __cstr_to_string() expects 1 argument (ptr)\n");
        exit(1);
    }

    if (args[0].type != VAL_PTR) {
        fprintf(stderr, "Runtime error: __cstr_to_string() requires a pointer\n");
        exit(1);
    }

    char *cstr = (char*)args[0].as.as_ptr;
    if (!cstr) {
        return val_string("");
    }
    return val_string(cstr);
}

// Convert an array of bytes (integers 0-255) to a UTF-8 string
// This allows proper reconstruction of multi-byte UTF-8 sequences
Value builtin_string_from_bytes(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: __string_from_bytes() expects 1 argument (array of bytes or buffer)\n");
        exit(1);
    }

    char *data = NULL;
    int length = 0;

    if (args[0].type == VAL_BUFFER) {
        // Handle buffer input
        Buffer *buf = args[0].as.as_buffer;
        if (!buf || !buf->data) {
            return val_string("");
        }
        length = buf->length;
        data = malloc(length + 1);
        if (!data) {
            fprintf(stderr, "Runtime error: __string_from_bytes() memory allocation failed\n");
            exit(1);
        }
        memcpy(data, buf->data, length);
        data[length] = '\0';
    } else if (args[0].type == VAL_ARRAY) {
        // Handle array input - each element should be an integer byte value
        Array *arr = args[0].as.as_array;
        if (!arr || arr->length == 0) {
            return val_string("");
        }
        length = arr->length;
        data = malloc(length + 1);
        if (!data) {
            fprintf(stderr, "Runtime error: __string_from_bytes() memory allocation failed\n");
            exit(1);
        }

        for (int i = 0; i < arr->length; i++) {
            Value elem = arr->elements[i];
            int byte_val;

            // Accept any integer type
            if (elem.type == VAL_I8) {
                byte_val = (unsigned char)elem.as.as_i8;
            } else if (elem.type == VAL_I16) {
                byte_val = elem.as.as_i16 & 0xFF;
            } else if (elem.type == VAL_I32) {
                byte_val = elem.as.as_i32 & 0xFF;
            } else if (elem.type == VAL_I64) {
                byte_val = (int)(elem.as.as_i64 & 0xFF);
            } else if (elem.type == VAL_U8) {
                byte_val = elem.as.as_u8;
            } else if (elem.type == VAL_U16) {
                byte_val = elem.as.as_u16 & 0xFF;
            } else if (elem.type == VAL_U32) {
                byte_val = elem.as.as_u32 & 0xFF;
            } else if (elem.type == VAL_U64) {
                byte_val = (int)(elem.as.as_u64 & 0xFF);
            } else {
                free(data);
                fprintf(stderr, "Runtime error: __string_from_bytes() array element at index %d is not an integer\n", i);
                exit(1);
            }

            data[i] = (char)byte_val;
        }
        data[length] = '\0';
    } else {
        fprintf(stderr, "Runtime error: __string_from_bytes() requires array or buffer argument\n");
        exit(1);
    }

    // Use val_string_take to avoid copying the data again
    return val_string_take(data, length, length + 1);
}

// apply(fn, args_array) - Call a function with an array of arguments
// This allows calling functions with dynamic argument counts
Value builtin_apply(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "apply() expects 2 arguments (function, args_array)");
        return val_null();
    }

    Value func = args[0];
    Value args_val = args[1];

    // Check first argument is a function
    if (func.type != VAL_FUNCTION && func.type != VAL_BUILTIN_FN) {
        runtime_error(ctx, "apply() first argument must be a function");
        return val_null();
    }

    // Check second argument is an array
    if (args_val.type != VAL_ARRAY) {
        runtime_error(ctx, "apply() second argument must be an array");
        return val_null();
    }

    Array *arr = args_val.as.as_array;
    int call_num_args = arr->length;

    // Build argument array from the array elements
    Value *call_args = NULL;
    if (call_num_args > 0) {
        call_args = malloc(sizeof(Value) * call_num_args);
        for (int i = 0; i < call_num_args; i++) {
            call_args[i] = arr->elements[i];
        }
    }

    Value result;

    if (func.type == VAL_BUILTIN_FN) {
        // Call builtin function directly
        BuiltinFn fn = func.as.as_builtin_fn;
        result = fn(call_args, call_num_args, ctx);
    } else {
        // Call user-defined function
        Function *fn = func.as.as_function;

        // Calculate number of required parameters (those without defaults)
        int required_params = 0;
        if (fn->param_defaults) {
            for (int i = 0; i < fn->num_params; i++) {
                if (!fn->param_defaults[i]) {
                    required_params++;
                }
            }
        } else {
            required_params = fn->num_params;
        }

        // Check argument count
        int max_args = fn->rest_param ? INT_MAX : fn->num_params;
        if (call_num_args < required_params || call_num_args > max_args) {
            if (call_args) free(call_args);
            if (fn->rest_param) {
                runtime_error(ctx, "apply(): function expects at least %d arguments, got %d",
                        required_params, call_num_args);
            } else if (required_params == fn->num_params) {
                runtime_error(ctx, "apply(): function expects %d arguments, got %d",
                        fn->num_params, call_num_args);
            } else {
                runtime_error(ctx, "apply(): function expects %d-%d arguments, got %d",
                        required_params, fn->num_params, call_num_args);
            }
            return val_null();
        }

        // Create call environment with closure_env as parent
        Environment *call_env = env_new(fn->closure_env);

        // Bind parameters
        for (int i = 0; i < fn->num_params; i++) {
            Value arg_value = {0};

            if (i < call_num_args && call_args != NULL) {
                arg_value = call_args[i];
            } else if (fn->param_defaults && fn->param_defaults[i]) {
                // Evaluate default expression in closure environment
                arg_value = eval_expr(fn->param_defaults[i], fn->closure_env, ctx);
            }

            // Type check if parameter has type annotation
            if (fn->param_types[i]) {
                arg_value = convert_to_type(arg_value, fn->param_types[i], call_env, ctx);
                if (ctx->exception_state.is_throwing) {
                    env_release(call_env);
                    if (call_args) free(call_args);
                    return val_null();
                }
            }

            env_set(call_env, fn->param_names[i], arg_value, ctx);
        }

        // Bind rest parameter if present
        if (fn->rest_param) {
            Array *rest_arr = array_new();
            for (int i = fn->num_params; i < call_num_args && call_args != NULL; i++) {
                Value arg = call_args[i];
                if (fn->rest_param_type) {
                    arg = convert_to_type(arg, fn->rest_param_type, call_env, ctx);
                }
                array_push(rest_arr, arg);
            }
            env_set(call_env, fn->rest_param, val_array(rest_arr), ctx);
        }

        // Execute function body
        ctx->return_state.is_returning = 0;
        eval_stmt(fn->body, call_env, ctx);

        // Get return value
        result = ctx->return_state.is_returning ? ctx->return_state.return_value : val_null();
        ctx->return_state.is_returning = 0;

        // Clean up
        env_release(call_env);
    }

    if (call_args) free(call_args);
    return result;
}
