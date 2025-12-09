#include "internal.h"

Value builtin_typeof(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: typeof() expects 1 argument\n");
        exit(1);
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
        default:
            type_name = "unknown";
            break;
    }

    return val_string(type_name);
}

Value builtin_assert(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args < 1 || num_args > 2) {
        fprintf(stderr, "Runtime error: assert() expects 1-2 arguments (condition, [message])\n");
        exit(1);
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
            // Use provided message
            exception_msg = args[1];
        } else {
            // Use default message
            exception_msg = val_string("assertion failed");
        }

        // Retain the exception value so it survives past environment cleanups during unwinding
        value_retain(exception_msg);
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
        call_stack_print(&ctx->call_stack);
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
            call_stack_print(&ctx->call_stack);
            exit(1);
        }
    }

    // Print panic message, stack trace, and exit
    fprintf(stderr, "panic: %s\n", message);
    call_stack_print(&ctx->call_stack);
    exit(1);
}
