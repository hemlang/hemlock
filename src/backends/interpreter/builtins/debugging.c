#include "internal.h"
#include "hemlock_limits.h"

Value builtin_typeof(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "typeof() expects 1 argument"); return val_null();
    }

    const char *type_name;
    switch (args[0].type) {
        case VAL_I8:
            type_name = "i8";
            break;
        case VAL_I16:
            type_name = "i16";
            break;
        case VAL_I32:
            type_name = "i32";
            break;
        case VAL_I64:
            type_name = "i64";
            break;
        case VAL_U8:
            type_name = "u8";
            break;
        case VAL_U16:
            type_name = "u16";
            break;
        case VAL_U32:
            type_name = "u32";
            break;
        case VAL_U64:
            type_name = "u64";
            break;
        case VAL_F32:
            type_name = "f32";
            break;
        case VAL_F64:
            type_name = "f64";
            break;
        case VAL_BOOL:
            type_name = "bool";
            break;
        case VAL_STRING:
            type_name = "string";
            break;
        case VAL_RUNE:
            type_name = "rune";
            break;
        case VAL_PTR:
            type_name = "ptr";
            break;
        case VAL_BUFFER:
            type_name = "buffer";
            break;
        case VAL_ARRAY:
            type_name = "array";
            break;
        case VAL_FILE:
            type_name = "file";
            break;
        case VAL_NULL:
            type_name = "null";
            break;
        case VAL_FUNCTION:
            type_name = "function";
            break;
        case VAL_BUILTIN_FN:
            type_name = "builtin";
            break;
        case VAL_OBJECT:
            // Check if object has a custom type name
            if (args[0].as.as_object->type_name) {
                type_name = args[0].as.as_object->type_name;
            } else {
                type_name = "object";
            }
            break;
        case VAL_TYPE:
            type_name = "type";
            break;
        case VAL_TASK:
            type_name = "task";
            break;
        case VAL_CHANNEL:
            type_name = "channel";
            break;
        case VAL_SOCKET:
            type_name = "socket";
            break;
        case VAL_WEBSOCKET:
            type_name = "websocket";
            break;
        case VAL_FFI_FUNCTION:
            type_name = "ffi function";
            break;
        case VAL_REF:
            type_name = "ref";
            break;
        default:
            type_name = "unknown";
            break;
    }

    return val_string(type_name);
}

Value builtin_typeid(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "typeid() expects 1 argument"); return val_null();
    }

    int32_t tid;
    switch (args[0].type) {
        case VAL_I8:         tid = HML_TYPEID_I8; break;
        case VAL_I16:        tid = HML_TYPEID_I16; break;
        case VAL_I32:        tid = HML_TYPEID_I32; break;
        case VAL_I64:        tid = HML_TYPEID_I64; break;
        case VAL_U8:         tid = HML_TYPEID_U8; break;
        case VAL_U16:        tid = HML_TYPEID_U16; break;
        case VAL_U32:        tid = HML_TYPEID_U32; break;
        case VAL_U64:        tid = HML_TYPEID_U64; break;
        case VAL_F32:        tid = HML_TYPEID_F32; break;
        case VAL_F64:        tid = HML_TYPEID_F64; break;
        case VAL_BOOL:       tid = HML_TYPEID_BOOL; break;
        case VAL_STRING:     tid = HML_TYPEID_STRING; break;
        case VAL_RUNE:       tid = HML_TYPEID_RUNE; break;
        case VAL_PTR:        tid = HML_TYPEID_PTR; break;
        case VAL_BUFFER:     tid = HML_TYPEID_BUFFER; break;
        case VAL_ARRAY:      tid = HML_TYPEID_ARRAY; break;
        case VAL_OBJECT:     tid = HML_TYPEID_OBJECT; break;
        case VAL_FILE:       tid = HML_TYPEID_FILE; break;
        case VAL_FUNCTION:   tid = HML_TYPEID_FUNCTION; break;
        case VAL_BUILTIN_FN: tid = HML_TYPEID_FUNCTION; break;
        case VAL_FFI_FUNCTION: tid = HML_TYPEID_FUNCTION; break;
        case VAL_TASK:       tid = HML_TYPEID_TASK; break;
        case VAL_CHANNEL:    tid = HML_TYPEID_CHANNEL; break;
        case VAL_NULL:       tid = HML_TYPEID_NULL; break;
        default:             tid = HML_TYPEID_NULL; break;
    }

    return val_i32(tid);
}

Value builtin_assert(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args < 1 || num_args > 2) {
        runtime_error(ctx, "assert() expects 1-2 arguments (condition, [message])"); return val_null();
    }

    // Check if condition is truthy
    int is_truthy = 0;
    switch (args[0].type) {
        case VAL_I8:
            is_truthy = args[0].as.as_i8 != 0;
            break;
        case VAL_I16:
            is_truthy = args[0].as.as_i16 != 0;
            break;
        case VAL_I32:
            is_truthy = args[0].as.as_i32 != 0;
            break;
        case VAL_U8:
            is_truthy = args[0].as.as_u8 != 0;
            break;
        case VAL_U16:
            is_truthy = args[0].as.as_u16 != 0;
            break;
        case VAL_U32:
            is_truthy = args[0].as.as_u32 != 0;
            break;
        case VAL_F32:
            is_truthy = args[0].as.as_f32 != 0.0f;
            break;
        case VAL_F64:
            is_truthy = args[0].as.as_f64 != 0.0;
            break;
        case VAL_BOOL:
            is_truthy = args[0].as.as_bool;
            break;
        case VAL_NULL:
            is_truthy = 0;
            break;
        case VAL_STRING:
            // Non-empty string is truthy
            is_truthy = args[0].as.as_string->length > 0;
            break;
        case VAL_PTR:
            is_truthy = args[0].as.as_ptr != NULL;
            break;
        default:
            // All other types (objects, arrays, functions, etc.) are truthy
            is_truthy = 1;
            break;
    }

    // If condition is false, throw exception
    if (!is_truthy) {
        Value exception_msg;
        if (num_args == 2) {
            // Use provided message - retain it so it survives past the caller
            // releasing the argument values during unwinding
            exception_msg = args[1];
            value_retain(exception_msg);
        } else {
            // Use default message - val_string() already returns an owned reference
            exception_msg = val_string("assertion failed");
        }

        ctx->exception_state.exception_value = exception_msg;
        ctx->exception_state.is_throwing = 1;
    }

    return val_null();
}

Value builtin_panic(Value *args, int num_args, ExecutionContext *ctx) {
    // Flush stdout first so output appears in correct order before panic message
    fflush(stdout);

    if (num_args > 1) {
        fprintf(stderr, "Runtime error: panic() expects 0 or 1 argument (message)\n");
        call_stack_print_with_source(&ctx->call_stack, get_current_source_code());
        exit(1);
    }

    // Get panic message
    const char *message = "panic!";
    if (num_args == 1) {
        if (args[0].type == VAL_STRING) {
            message = args[0].as.as_string->data;
        } else {
            // Convert non-string to string representation
            fprintf(stderr, "panic: ");
            // Print value to stderr using value_to_string
            char *str = value_to_string(args[0]);
            fprintf(stderr, "%s", str);
            free(str);
            fprintf(stderr, "\n");
            call_stack_print_with_source(&ctx->call_stack, get_current_source_code());
            exit(1);
        }
    }

    // Print panic message, stack trace, and exit
    fprintf(stderr, "panic: %s\n", message);
    call_stack_print_with_source(&ctx->call_stack, get_current_source_code());
    exit(1);
}

Value builtin_set_stack_limit(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "set_stack_limit() expects 1 argument (limit)");
        return val_null();
    }

    if (!is_integer(args[0])) {
        runtime_error(ctx, "set_stack_limit() expects an integer argument");
        return val_null();
    }

    int limit = value_to_int(args[0]);
    if (limit <= 0) {
        runtime_error(ctx, "set_stack_limit() expects a positive integer");
        return val_null();
    }

    ctx->max_stack_depth = limit;
    return val_null();
}

Value builtin_get_stack_limit(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;  // Unused
    if (num_args != 0) {
        runtime_error(ctx, "get_stack_limit() expects no arguments");
        return val_null();
    }

    return val_i32(ctx->max_stack_depth);
}
