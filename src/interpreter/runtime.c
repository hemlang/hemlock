#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ========== EXECUTION CONTEXT IMPLEMENTATION ==========

ExecutionContext* exec_context_new(void) {
    ExecutionContext *ctx = malloc(sizeof(ExecutionContext));
    if (!ctx) {
        fprintf(stderr, "Fatal error: Failed to allocate execution context\n");
        exit(1);
    }
    ctx->return_state.is_returning = 0;
    ctx->return_state.return_value = val_null();
    ctx->loop_state.is_breaking = 0;
    ctx->loop_state.is_continuing = 0;
    ctx->exception_state.is_throwing = 0;
    ctx->exception_state.exception_value = val_null();
    call_stack_init(&ctx->call_stack);
    return ctx;
}

void exec_context_free(ExecutionContext *ctx) {
    if (ctx) {
        call_stack_free(&ctx->call_stack);
        free(ctx);
    }
}

// ========== CALL STACK IMPLEMENTATION ==========

void call_stack_init(CallStack *stack) {
    stack->capacity = 64;
    stack->count = 0;
    stack->frames = malloc(sizeof(CallFrame) * stack->capacity);
    if (!stack->frames) {
        fprintf(stderr, "Fatal error: Failed to initialize call stack\n");
        exit(1);
    }
}

void call_stack_push(CallStack *stack, const char *function_name) {
    if (stack->capacity == 0) {
        call_stack_init(stack);
    }

    if (stack->count >= stack->capacity) {
        stack->capacity *= 2;
        CallFrame *new_frames = realloc(stack->frames, sizeof(CallFrame) * stack->capacity);
        if (!new_frames) {
            fprintf(stderr, "Fatal error: Failed to grow call stack\n");
            exit(1);
        }
        stack->frames = new_frames;
    }

    stack->frames[stack->count].function_name = strdup(function_name);
    stack->frames[stack->count].line = 0;  // TODO: Add line tracking
    stack->count++;
}

void call_stack_pop(CallStack *stack) {
    if (stack->count > 0) {
        stack->count--;
        free(stack->frames[stack->count].function_name);
    }
}

void call_stack_print(CallStack *stack) {
    if (stack->count == 0) {
        return;
    }

    fprintf(stderr, "\nStack trace (most recent call first):\n");
    for (int i = stack->count - 1; i >= 0; i--) {
        fprintf(stderr, "  at %s()\n", stack->frames[i].function_name);
    }
}

void call_stack_free(CallStack *stack) {
    for (int i = 0; i < stack->count; i++) {
        free(stack->frames[i].function_name);
    }
    free(stack->frames);
    stack->frames = NULL;
    stack->count = 0;
    stack->capacity = 0;
}

// ========== HELPER FUNCTIONS ==========

// Helper to add two values (for increment operations)
static Value value_add_one(Value val) {
    if (is_float(val)) {
        double v = value_to_float(val);
        return (val.type == VAL_F32) ? val_f32((float)(v + 1.0)) : val_f64(v + 1.0);
    } else if (is_integer(val)) {
        int64_t v = value_to_int(val);
        ValueType result_type = val.type;
        return promote_value(val_i32((int32_t)(v + 1)), result_type);
    } else {
        fprintf(stderr, "Runtime error: Can only increment numeric values\n");
        exit(1);
    }
}

// Helper to subtract one from a value (for decrement operations)
static Value value_sub_one(Value val) {
    if (is_float(val)) {
        double v = value_to_float(val);
        return (val.type == VAL_F32) ? val_f32((float)(v - 1.0)) : val_f64(v - 1.0);
    } else if (is_integer(val)) {
        int64_t v = value_to_int(val);
        ValueType result_type = val.type;
        return promote_value(val_i32((int32_t)(v - 1)), result_type);
    } else {
        fprintf(stderr, "Runtime error: Can only decrement numeric values\n");
        exit(1);
    }
}

// ========== EXPRESSION EVALUATION ==========

Value eval_expr(Expr *expr, Environment *env, ExecutionContext *ctx) {
    switch (expr->type) {
        case EXPR_NUMBER:
            if (expr->as.number.is_float) {
                return val_float(expr->as.number.float_value);
            } else {
                int64_t value = expr->as.number.int_value;
                // Use i32 for values that fit in 32-bit range, otherwise i64
                if (value >= INT32_MIN && value <= INT32_MAX) {
                    return val_int((int32_t)value);
                } else {
                    return val_i64(value);
                }
            }
            break;

        case EXPR_BOOL:
            return val_bool(expr->as.boolean);

        case EXPR_NULL:
            return val_null();

        case EXPR_STRING:
            return val_string(expr->as.string);

        case EXPR_UNARY: {
            Value operand = eval_expr(expr->as.unary.operand, env, ctx);

            switch (expr->as.unary.op) {
                case UNARY_NOT:
                    return val_bool(!value_is_truthy(operand));

                case UNARY_NEGATE:
                    if (is_float(operand)) {
                        return val_f64(-value_to_float(operand));
                    } else if (is_integer(operand)) {
                        // Preserve the original type when negating
                        switch (operand.type) {
                            case VAL_I8: return val_i8(-operand.as.as_i8);
                            case VAL_I16: return val_i16(-operand.as.as_i16);
                            case VAL_I32: return val_i32(-operand.as.as_i32);
                            case VAL_I64: return val_i64(-operand.as.as_i64);
                            case VAL_U8: return val_i16(-(int16_t)operand.as.as_u8);  // promote to i16
                            case VAL_U16: return val_i32(-(int32_t)operand.as.as_u16); // promote to i32
                            case VAL_U32: return val_i64(-(int64_t)operand.as.as_u32); // promote to i64
                            case VAL_U64: {
                                // Special case: u64 negation - check if value fits in i64
                                if (operand.as.as_u64 <= INT64_MAX) {
                                    return val_i64(-(int64_t)operand.as.as_u64);
                                } else {
                                    fprintf(stderr, "Runtime error: Cannot negate u64 value larger than INT64_MAX\n");
                                    exit(1);
                                }
                            }
                            default:
                                fprintf(stderr, "Runtime error: Cannot negate non-integer value\n");
                                exit(1);
                        }
                    }
                    fprintf(stderr, "Runtime error: Cannot negate non-numeric value\n");
                    exit(1);
            }
            break;
        }

        case EXPR_TERNARY: {
            Value condition = eval_expr(expr->as.ternary.condition, env, ctx);
            if (value_is_truthy(condition)) {
                return eval_expr(expr->as.ternary.true_expr, env, ctx);
            } else {
                return eval_expr(expr->as.ternary.false_expr, env, ctx);
            }
        }

        case EXPR_IDENT:
            return env_get(env, expr->as.ident, ctx);

        case EXPR_ASSIGN: {
            Value value = eval_expr(expr->as.assign.value, env, ctx);
            env_set(env, expr->as.assign.name, value, ctx);
            return value;
        }

        case EXPR_BINARY: {
            // Handle && and || with short-circuit evaluation
            if (expr->as.binary.op == OP_AND) {
                Value left = eval_expr(expr->as.binary.left, env, ctx);
                if (!value_is_truthy(left)) return val_bool(0);

                Value right = eval_expr(expr->as.binary.right, env, ctx);
                return val_bool(value_is_truthy(right));
            }

            if (expr->as.binary.op == OP_OR) {
                Value left = eval_expr(expr->as.binary.left, env, ctx);
                if (value_is_truthy(left)) return val_bool(1);

                Value right = eval_expr(expr->as.binary.right, env, ctx);
                return val_bool(value_is_truthy(right));
            }

            // Evaluate both operands
            Value left = eval_expr(expr->as.binary.left, env, ctx);
            Value right = eval_expr(expr->as.binary.right, env, ctx);

            // String concatenation
            if (expr->as.binary.op == OP_ADD && left.type == VAL_STRING && right.type == VAL_STRING) {
                String *result = string_concat(left.as.as_string, right.as.as_string);
                return (Value){ .type = VAL_STRING, .as.as_string = result };
            }

            // Pointer arithmetic
            if (left.type == VAL_PTR && is_integer(right)) {
                if (expr->as.binary.op == OP_ADD) {
                    void *ptr = left.as.as_ptr;
                    int32_t offset = value_to_int(right);
                    return val_ptr((char *)ptr + offset);
                } else if (expr->as.binary.op == OP_SUB) {
                    void *ptr = left.as.as_ptr;
                    int32_t offset = value_to_int(right);
                    return val_ptr((char *)ptr - offset);
                }
            }

            if (is_integer(left) && right.type == VAL_PTR) {
                if (expr->as.binary.op == OP_ADD) {
                    int32_t offset = value_to_int(left);
                    void *ptr = right.as.as_ptr;
                    return val_ptr((char *)ptr + offset);
                }
            }

            // Numeric operations
            if (!is_numeric(left) || !is_numeric(right)) {
                fprintf(stderr, "Runtime error: Binary operation requires numeric operands\n");
                exit(1);
            }

            // Determine result type and promote operands
            ValueType result_type = promote_types(left.type, right.type);
            left = promote_value(left, result_type);
            right = promote_value(right, result_type);

            // Perform operation based on result type
            if (is_float(left)) {
                // Float operation
                double l = value_to_float(left);
                double r = value_to_float(right);

                switch (expr->as.binary.op) {
                    case OP_ADD:
                        return (result_type == VAL_F32) ? val_f32((float)(l + r)) : val_f64(l + r);
                    case OP_SUB:
                        return (result_type == VAL_F32) ? val_f32((float)(l - r)) : val_f64(l - r);
                    case OP_MUL:
                        return (result_type == VAL_F32) ? val_f32((float)(l * r)) : val_f64(l * r);
                    case OP_DIV:
                        if (r == 0.0) {
                            fprintf(stderr, "Runtime error: Division by zero\n");
                            exit(1);
                        }
                        return (result_type == VAL_F32) ? val_f32((float)(l / r)) : val_f64(l / r);
                    case OP_EQUAL: return val_bool(l == r);
                    case OP_NOT_EQUAL: return val_bool(l != r);
                    case OP_LESS: return val_bool(l < r);
                    case OP_LESS_EQUAL: return val_bool(l <= r);
                    case OP_GREATER: return val_bool(l > r);
                    case OP_GREATER_EQUAL: return val_bool(l >= r);
                    default: break;
                }
            } else {
                // Integer operation - handle each result type properly to avoid truncation
                switch (expr->as.binary.op) {
                    case OP_ADD:
                    case OP_SUB:
                    case OP_MUL:
                    case OP_DIV: {
                        // Extract values according to the promoted type
                        switch (result_type) {
                            case VAL_I8: {
                                int8_t l = left.as.as_i8;
                                int8_t r = right.as.as_i8;
                                if (expr->as.binary.op == OP_DIV && r == 0) {
                                    fprintf(stderr, "Runtime error: Division by zero\n");
                                    exit(1);
                                }
                                int8_t result = (expr->as.binary.op == OP_ADD) ? (l + r) :
                                               (expr->as.binary.op == OP_SUB) ? (l - r) :
                                               (expr->as.binary.op == OP_MUL) ? (l * r) : (l / r);
                                return val_i8(result);
                            }
                            case VAL_I16: {
                                int16_t l = left.as.as_i16;
                                int16_t r = right.as.as_i16;
                                if (expr->as.binary.op == OP_DIV && r == 0) {
                                    fprintf(stderr, "Runtime error: Division by zero\n");
                                    exit(1);
                                }
                                int16_t result = (expr->as.binary.op == OP_ADD) ? (l + r) :
                                                (expr->as.binary.op == OP_SUB) ? (l - r) :
                                                (expr->as.binary.op == OP_MUL) ? (l * r) : (l / r);
                                return val_i16(result);
                            }
                            case VAL_I32: {
                                int32_t l = left.as.as_i32;
                                int32_t r = right.as.as_i32;
                                if (expr->as.binary.op == OP_DIV && r == 0) {
                                    fprintf(stderr, "Runtime error: Division by zero\n");
                                    exit(1);
                                }
                                int32_t result = (expr->as.binary.op == OP_ADD) ? (l + r) :
                                                (expr->as.binary.op == OP_SUB) ? (l - r) :
                                                (expr->as.binary.op == OP_MUL) ? (l * r) : (l / r);
                                return val_i32(result);
                            }
                            case VAL_I64: {
                                int64_t l = left.as.as_i64;
                                int64_t r = right.as.as_i64;
                                if (expr->as.binary.op == OP_DIV && r == 0) {
                                    fprintf(stderr, "Runtime error: Division by zero\n");
                                    exit(1);
                                }
                                int64_t result = (expr->as.binary.op == OP_ADD) ? (l + r) :
                                                (expr->as.binary.op == OP_SUB) ? (l - r) :
                                                (expr->as.binary.op == OP_MUL) ? (l * r) : (l / r);
                                return val_i64(result);
                            }
                            case VAL_U8: {
                                uint8_t l = left.as.as_u8;
                                uint8_t r = right.as.as_u8;
                                if (expr->as.binary.op == OP_DIV && r == 0) {
                                    fprintf(stderr, "Runtime error: Division by zero\n");
                                    exit(1);
                                }
                                uint8_t result = (expr->as.binary.op == OP_ADD) ? (l + r) :
                                                (expr->as.binary.op == OP_SUB) ? (l - r) :
                                                (expr->as.binary.op == OP_MUL) ? (l * r) : (l / r);
                                return val_u8(result);
                            }
                            case VAL_U16: {
                                uint16_t l = left.as.as_u16;
                                uint16_t r = right.as.as_u16;
                                if (expr->as.binary.op == OP_DIV && r == 0) {
                                    fprintf(stderr, "Runtime error: Division by zero\n");
                                    exit(1);
                                }
                                uint16_t result = (expr->as.binary.op == OP_ADD) ? (l + r) :
                                                 (expr->as.binary.op == OP_SUB) ? (l - r) :
                                                 (expr->as.binary.op == OP_MUL) ? (l * r) : (l / r);
                                return val_u16(result);
                            }
                            case VAL_U32: {
                                uint32_t l = left.as.as_u32;
                                uint32_t r = right.as.as_u32;
                                if (expr->as.binary.op == OP_DIV && r == 0) {
                                    fprintf(stderr, "Runtime error: Division by zero\n");
                                    exit(1);
                                }
                                uint32_t result = (expr->as.binary.op == OP_ADD) ? (l + r) :
                                                 (expr->as.binary.op == OP_SUB) ? (l - r) :
                                                 (expr->as.binary.op == OP_MUL) ? (l * r) : (l / r);
                                return val_u32(result);
                            }
                            case VAL_U64: {
                                uint64_t l = left.as.as_u64;
                                uint64_t r = right.as.as_u64;
                                if (expr->as.binary.op == OP_DIV && r == 0) {
                                    fprintf(stderr, "Runtime error: Division by zero\n");
                                    exit(1);
                                }
                                uint64_t result = (expr->as.binary.op == OP_ADD) ? (l + r) :
                                                 (expr->as.binary.op == OP_SUB) ? (l - r) :
                                                 (expr->as.binary.op == OP_MUL) ? (l * r) : (l / r);
                                return val_u64(result);
                            }
                            default:
                                fprintf(stderr, "Runtime error: Invalid integer type for arithmetic\n");
                                exit(1);
                        }
                    }

                    // Comparison operations - can use wider types for comparison
                    case OP_EQUAL:
                    case OP_NOT_EQUAL:
                    case OP_LESS:
                    case OP_LESS_EQUAL:
                    case OP_GREATER:
                    case OP_GREATER_EQUAL: {
                        // For comparisons, we need to handle signed vs unsigned properly
                        int is_signed = (result_type == VAL_I8 || result_type == VAL_I16 ||
                                        result_type == VAL_I32 || result_type == VAL_I64);

                        if (is_signed) {
                            int64_t l, r;
                            switch (result_type) {
                                case VAL_I8: l = left.as.as_i8; r = right.as.as_i8; break;
                                case VAL_I16: l = left.as.as_i16; r = right.as.as_i16; break;
                                case VAL_I32: l = left.as.as_i32; r = right.as.as_i32; break;
                                case VAL_I64: l = left.as.as_i64; r = right.as.as_i64; break;
                                default: l = r = 0; break;
                            }
                            switch (expr->as.binary.op) {
                                case OP_EQUAL: return val_bool(l == r);
                                case OP_NOT_EQUAL: return val_bool(l != r);
                                case OP_LESS: return val_bool(l < r);
                                case OP_LESS_EQUAL: return val_bool(l <= r);
                                case OP_GREATER: return val_bool(l > r);
                                case OP_GREATER_EQUAL: return val_bool(l >= r);
                                default: break;
                            }
                        } else {
                            uint64_t l, r;
                            switch (result_type) {
                                case VAL_U8: l = left.as.as_u8; r = right.as.as_u8; break;
                                case VAL_U16: l = left.as.as_u16; r = right.as.as_u16; break;
                                case VAL_U32: l = left.as.as_u32; r = right.as.as_u32; break;
                                case VAL_U64: l = left.as.as_u64; r = right.as.as_u64; break;
                                default: l = r = 0; break;
                            }
                            switch (expr->as.binary.op) {
                                case OP_EQUAL: return val_bool(l == r);
                                case OP_NOT_EQUAL: return val_bool(l != r);
                                case OP_LESS: return val_bool(l < r);
                                case OP_LESS_EQUAL: return val_bool(l <= r);
                                case OP_GREATER: return val_bool(l > r);
                                case OP_GREATER_EQUAL: return val_bool(l >= r);
                                default: break;
                            }
                        }
                        break;
                    }
                    default: break;
                }
            }

            fprintf(stderr, "Runtime error: Unknown binary operator\n");
            exit(1);
        }

        case EXPR_CALL: {
            // Check if this is a method call (obj.method(...))
            int is_method_call = 0;
            Value method_self;

            if (expr->as.call.func->type == EXPR_GET_PROPERTY) {
                is_method_call = 1;
                method_self = eval_expr(expr->as.call.func->as.get_property.object, env, ctx);

                // Special handling for file methods
                if (method_self.type == VAL_FILE) {
                    const char *method = expr->as.call.func->as.get_property.property;

                    // Evaluate arguments
                    Value *args = NULL;
                    if (expr->as.call.num_args > 0) {
                        args = malloc(sizeof(Value) * expr->as.call.num_args);
                        for (int i = 0; i < expr->as.call.num_args; i++) {
                            args[i] = eval_expr(expr->as.call.args[i], env, ctx);
                        }
                    }

                    Value result = call_file_method(method_self.as.as_file, method, args, expr->as.call.num_args);
                    if (args) free(args);
                    return result;
                }

                // Special handling for array methods
                if (method_self.type == VAL_ARRAY) {
                    const char *method = expr->as.call.func->as.get_property.property;

                    // Evaluate arguments
                    Value *args = NULL;
                    if (expr->as.call.num_args > 0) {
                        args = malloc(sizeof(Value) * expr->as.call.num_args);
                        for (int i = 0; i < expr->as.call.num_args; i++) {
                            args[i] = eval_expr(expr->as.call.args[i], env, ctx);
                        }
                    }

                    Value result = call_array_method(method_self.as.as_array, method, args, expr->as.call.num_args);
                    if (args) free(args);
                    return result;
                }

                // Special handling for string methods
                if (method_self.type == VAL_STRING) {
                    const char *method = expr->as.call.func->as.get_property.property;

                    // Evaluate arguments
                    Value *args = NULL;
                    if (expr->as.call.num_args > 0) {
                        args = malloc(sizeof(Value) * expr->as.call.num_args);
                        for (int i = 0; i < expr->as.call.num_args; i++) {
                            args[i] = eval_expr(expr->as.call.args[i], env, ctx);
                        }
                    }

                    Value result = call_string_method(method_self.as.as_string, method, args, expr->as.call.num_args);
                    if (args) free(args);
                    return result;
                }

                // Special handling for channel methods
                if (method_self.type == VAL_CHANNEL) {
                    const char *method = expr->as.call.func->as.get_property.property;

                    // Evaluate arguments
                    Value *args = NULL;
                    if (expr->as.call.num_args > 0) {
                        args = malloc(sizeof(Value) * expr->as.call.num_args);
                        for (int i = 0; i < expr->as.call.num_args; i++) {
                            args[i] = eval_expr(expr->as.call.args[i], env, ctx);
                        }
                    }

                    Value result = call_channel_method(method_self.as.as_channel, method, args, expr->as.call.num_args);
                    if (args) free(args);
                    return result;
                }
            }

            // Evaluate the function expression
            Value func = eval_expr(expr->as.call.func, env, ctx);

            // Evaluate arguments
            Value *args = NULL;
            if (expr->as.call.num_args > 0) {
                args = malloc(sizeof(Value) * expr->as.call.num_args);
                for (int i = 0; i < expr->as.call.num_args; i++) {
                    args[i] = eval_expr(expr->as.call.args[i], env, ctx);
                }
            }

            Value result;

            if (func.type == VAL_BUILTIN_FN) {
                // Call builtin function
                BuiltinFn fn = func.as.as_builtin_fn;
                result = fn(args, expr->as.call.num_args, ctx);
            } else if (func.type == VAL_FUNCTION) {
                // Call user-defined function
                Function *fn = func.as.as_function;

                // Check argument count
                if (expr->as.call.num_args != fn->num_params) {
                    fprintf(stderr, "Runtime error: Function expects %d arguments, got %d\n",
                            fn->num_params, expr->as.call.num_args);
                    exit(1);
                }

                // Determine function name for stack trace
                const char *fn_name = "<anonymous>";
                if (is_method_call && expr->as.call.func->type == EXPR_GET_PROPERTY) {
                    fn_name = expr->as.call.func->as.get_property.property;
                } else if (expr->as.call.func->type == EXPR_IDENT) {
                    fn_name = expr->as.call.func->as.ident;
                }

                // Push call onto stack trace
                call_stack_push(&ctx->call_stack, fn_name);

                // Create call environment with closure_env as parent
                Environment *call_env = env_new(fn->closure_env);

                // Inject 'self' if this is a method call
                if (is_method_call) {
                    env_set(call_env, "self", method_self, ctx);
                }

                // Bind parameters
                for (int i = 0; i < fn->num_params; i++) {
                    Value arg_value = args[i];

                    // Type check if parameter has type annotation
                    if (fn->param_types[i]) {
                        arg_value = convert_to_type(arg_value, fn->param_types[i], call_env, ctx);
                    }

                    env_set(call_env, fn->param_names[i], arg_value, ctx);
                }

                // Execute body
                ctx->return_state.is_returning = 0;
                eval_stmt(fn->body, call_env, ctx);

                // Get result
                result = ctx->return_state.return_value;

                // Check return type if specified
                if (fn->return_type) {
                    if (!ctx->return_state.is_returning) {
                        fprintf(stderr, "Runtime error: Function with return type must return a value\n");
                        exit(1);
                    }
                    result = convert_to_type(result, fn->return_type, call_env, ctx);
                }

                // Reset return state
                ctx->return_state.is_returning = 0;

                // Pop call from stack trace (but not if exception is active - preserve stack for error reporting)
                if (!ctx->exception_state.is_throwing) {
                    call_stack_pop(&ctx->call_stack);
                }

                // Cleanup
                // NOTE: We don't free call_env here because closures might reference it
                // This is a known memory leak in v0.1, to be fixed with refcounting in v0.2
                // env_free(call_env);
            } else if (func.type == VAL_FFI_FUNCTION) {
                // Call FFI function
                FFIFunction *ffi_func = (FFIFunction*)func.as.as_ffi_function;
                result = ffi_call_function(ffi_func, args, expr->as.call.num_args, ctx);
            } else {
                fprintf(stderr, "Runtime error: Value is not a function\n");
                exit(1);
            }

            if (args) free(args);
            return result;
        }

        case EXPR_GET_PROPERTY: {
            Value object = eval_expr(expr->as.get_property.object, env, ctx);
            const char *property = expr->as.get_property.property;

            if (object.type == VAL_STRING) {
                if (strcmp(property, "length") == 0) {
                    return val_int(object.as.as_string->length);
                }
                fprintf(stderr, "Runtime error: Unknown property '%s' for string\n", property);
                exit(1);
            } else if (object.type == VAL_BUFFER) {
                if (strcmp(property, "length") == 0) {
                    return val_int(object.as.as_buffer->length);
                } else if (strcmp(property, "capacity") == 0) {
                    return val_int(object.as.as_buffer->capacity);
                }
                fprintf(stderr, "Runtime error: Unknown property '%s' for buffer\n", property);
                exit(1);
            } else if (object.type == VAL_FILE) {
                FileHandle *file = object.as.as_file;
                if (strcmp(property, "mode") == 0) {
                    return val_string(file->mode);
                } else if (strcmp(property, "closed") == 0) {
                    return val_bool(file->closed);
                }
                fprintf(stderr, "Runtime error: Unknown property '%s' for file\n", property);
                exit(1);
            } else if (object.type == VAL_ARRAY) {
                // Array properties
                if (strcmp(property, "length") == 0) {
                    return val_i32(object.as.as_array->length);
                }
                fprintf(stderr, "Runtime error: Array has no property '%s'\n", property);
                exit(1);
            } else if (object.type == VAL_OBJECT) {
                // Look up field in object
                Object *obj = object.as.as_object;
                for (int i = 0; i < obj->num_fields; i++) {
                    if (strcmp(obj->field_names[i], property) == 0) {
                        return obj->field_values[i];
                    }
                }
                fprintf(stderr, "Runtime error: Object has no field '%s'\n", property);
                exit(1);
            } else {
                fprintf(stderr, "Runtime error: Only strings, buffers, arrays, and objects have properties\n");
                exit(1);
            }
        }

        case EXPR_INDEX: {
            Value object = eval_expr(expr->as.index.object, env, ctx);
            Value index_val = eval_expr(expr->as.index.index, env, ctx);

            if (!is_integer(index_val)) {
                fprintf(stderr, "Runtime error: Index must be an integer\n");
                exit(1);
            }

            int32_t index = value_to_int(index_val);

            if (object.type == VAL_STRING) {
                String *str = object.as.as_string;

                if (index < 0 || index >= str->length) {
                    fprintf(stderr, "Runtime error: String index %d out of bounds (length %d)\n",
                            index, str->length);
                    exit(1);
                }

                // Return the byte as an integer (u8/char)
                return val_u8((unsigned char)str->data[index]);
            } else if (object.type == VAL_BUFFER) {
                Buffer *buf = object.as.as_buffer;

                if (index < 0 || index >= buf->length) {
                    fprintf(stderr, "Runtime error: Buffer index %d out of bounds (length %d)\n",
                            index, buf->length);
                    exit(1);
                }

                // Return the byte as an integer (u8)
                return val_u8(((unsigned char *)buf->data)[index]);
            } else if (object.type == VAL_ARRAY) {
                // Array indexing
                return array_get(object.as.as_array, index);
            } else {
                fprintf(stderr, "Runtime error: Only strings, buffers, and arrays can be indexed\n");
                exit(1);
            }
        }

        case EXPR_INDEX_ASSIGN: {
            Value object = eval_expr(expr->as.index_assign.object, env, ctx);
            Value index_val = eval_expr(expr->as.index_assign.index, env, ctx);
            Value value = eval_expr(expr->as.index_assign.value, env, ctx);

            if (!is_integer(index_val)) {
                fprintf(stderr, "Runtime error: Index must be an integer\n");
                exit(1);
            }

            int32_t index = value_to_int(index_val);

            if (object.type == VAL_ARRAY) {
                // Array assignment - value can be any type
                array_set(object.as.as_array, index, value);
                return value;
            }

            // For strings and buffers, value must be an integer (byte)
            if (!is_integer(value)) {
                fprintf(stderr, "Runtime error: Index value must be an integer (byte) for strings/buffers\n");
                exit(1);
            }

            if (object.type == VAL_STRING) {
                String *str = object.as.as_string;

                if (index < 0 || index >= str->length) {
                    fprintf(stderr, "Runtime error: String index %d out of bounds (length %d)\n",
                            index, str->length);
                    exit(1);
                }

                // Strings are mutable - set the byte
                str->data[index] = (char)value_to_int(value);
                return value;
            } else if (object.type == VAL_BUFFER) {
                Buffer *buf = object.as.as_buffer;

                if (index < 0 || index >= buf->length) {
                    fprintf(stderr, "Runtime error: Buffer index %d out of bounds (length %d)\n",
                            index, buf->length);
                    exit(1);
                }

                // Buffers are mutable - set the byte
                ((unsigned char *)buf->data)[index] = (unsigned char)value_to_int(value);
                return value;
            } else {
                fprintf(stderr, "Runtime error: Only strings, buffers, and arrays support index assignment\n");
                exit(1);
            }
        }

        case EXPR_FUNCTION: {
            // Create function object and capture current environment
            Function *fn = malloc(sizeof(Function));

            // Copy is_async flag
            fn->is_async = expr->as.function.is_async;

            // Copy parameter names
            fn->param_names = malloc(sizeof(char*) * expr->as.function.num_params);
            for (int i = 0; i < expr->as.function.num_params; i++) {
                fn->param_names[i] = strdup(expr->as.function.param_names[i]);
            }

            // Copy parameter types (may be NULL)
            fn->param_types = malloc(sizeof(Type*) * expr->as.function.num_params);
            for (int i = 0; i < expr->as.function.num_params; i++) {
                if (expr->as.function.param_types[i]) {
                    fn->param_types[i] = type_new(expr->as.function.param_types[i]->kind);
                } else {
                    fn->param_types[i] = NULL;
                }
            }

            fn->num_params = expr->as.function.num_params;

            // Copy return type (may be NULL)
            if (expr->as.function.return_type) {
                fn->return_type = type_new(expr->as.function.return_type->kind);
            } else {
                fn->return_type = NULL;
            }

            // Store body AST (shared, not copied)
            fn->body = expr->as.function.body;

            // CRITICAL: Capture current environment
            fn->closure_env = env;

            return val_function(fn);
        }

        case EXPR_ARRAY_LITERAL: {
            // Create array and evaluate elements
            Array *arr = array_new();

            for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                Value element = eval_expr(expr->as.array_literal.elements[i], env, ctx);
                array_push(arr, element);
            }

            return val_array(arr);
        }

        case EXPR_OBJECT_LITERAL: {
            // Create anonymous object
            Object *obj = object_new(NULL, expr->as.object_literal.num_fields);

            // Evaluate and store fields
            for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
                obj->field_names[i] = strdup(expr->as.object_literal.field_names[i]);
                obj->field_values[i] = eval_expr(expr->as.object_literal.field_values[i], env, ctx);
                obj->num_fields++;
            }

            return val_object(obj);
        }

        case EXPR_SET_PROPERTY: {
            Value object = eval_expr(expr->as.set_property.object, env, ctx);
            const char *property = expr->as.set_property.property;
            Value value = eval_expr(expr->as.set_property.value, env, ctx);

            if (object.type != VAL_OBJECT) {
                fprintf(stderr, "Runtime error: Only objects can have properties set\n");
                exit(1);
            }

            Object *obj = object.as.as_object;

            // Look for existing field
            for (int i = 0; i < obj->num_fields; i++) {
                if (strcmp(obj->field_names[i], property) == 0) {
                    obj->field_values[i] = value;
                    return value;
                }
            }

            // Field doesn't exist - add it dynamically!
            if (obj->num_fields >= obj->capacity) {
                // Grow arrays
                obj->capacity *= 2;
                obj->field_names = realloc(obj->field_names, sizeof(char*) * obj->capacity);
                obj->field_values = realloc(obj->field_values, sizeof(Value) * obj->capacity);
            }

            obj->field_names[obj->num_fields] = strdup(property);
            obj->field_values[obj->num_fields] = value;
            obj->num_fields++;

            return value;
        }

        case EXPR_PREFIX_INC: {
            // ++x: increment then return new value
            Expr *operand = expr->as.prefix_inc.operand;

            if (operand->type == EXPR_IDENT) {
                // Simple variable: ++x
                Value old_val = env_get(env, operand->as.ident, ctx);
                Value new_val = value_add_one(old_val);
                env_set(env, operand->as.ident, new_val, ctx);
                return new_val;
            } else if (operand->type == EXPR_INDEX) {
                // Array/buffer/string index: ++arr[i]
                Value object = eval_expr(operand->as.index.object, env, ctx);
                Value index_val = eval_expr(operand->as.index.index, env, ctx);

                if (!is_integer(index_val)) {
                    fprintf(stderr, "Runtime error: Index must be an integer\n");
                    exit(1);
                }
                int32_t index = value_to_int(index_val);

                if (object.type == VAL_ARRAY) {
                    Value old_val = array_get(object.as.as_array, index);
                    Value new_val = value_add_one(old_val);
                    array_set(object.as.as_array, index, new_val);
                    return new_val;
                } else {
                    fprintf(stderr, "Runtime error: Can only use ++ on array elements\n");
                    exit(1);
                }
            } else if (operand->type == EXPR_GET_PROPERTY) {
                // Object property: ++obj.field
                Value object = eval_expr(operand->as.get_property.object, env, ctx);
                const char *property = operand->as.get_property.property;
                if (object.type != VAL_OBJECT) {
                    fprintf(stderr, "Runtime error: Can only increment object properties\n");
                    exit(1);
                }
                Object *obj = object.as.as_object;
                for (int i = 0; i < obj->num_fields; i++) {
                    if (strcmp(obj->field_names[i], property) == 0) {
                        Value old_val = obj->field_values[i];
                        Value new_val = value_add_one(old_val);
                        obj->field_values[i] = new_val;
                        return new_val;
                    }
                }
                fprintf(stderr, "Runtime error: Property '%s' not found\n", property);
                exit(1);
            } else {
                fprintf(stderr, "Runtime error: Invalid operand for ++\n");
                exit(1);
            }
        }

        case EXPR_PREFIX_DEC: {
            // --x: decrement then return new value
            Expr *operand = expr->as.prefix_dec.operand;

            if (operand->type == EXPR_IDENT) {
                Value old_val = env_get(env, operand->as.ident, ctx);
                Value new_val = value_sub_one(old_val);
                env_set(env, operand->as.ident, new_val, ctx);
                return new_val;
            } else if (operand->type == EXPR_INDEX) {
                Value object = eval_expr(operand->as.index.object, env, ctx);
                Value index_val = eval_expr(operand->as.index.index, env, ctx);

                if (!is_integer(index_val)) {
                    fprintf(stderr, "Runtime error: Index must be an integer\n");
                    exit(1);
                }
                int32_t index = value_to_int(index_val);

                if (object.type == VAL_ARRAY) {
                    Value old_val = array_get(object.as.as_array, index);
                    Value new_val = value_sub_one(old_val);
                    array_set(object.as.as_array, index, new_val);
                    return new_val;
                } else {
                    fprintf(stderr, "Runtime error: Can only use -- on array elements\n");
                    exit(1);
                }
            } else if (operand->type == EXPR_GET_PROPERTY) {
                Value object = eval_expr(operand->as.get_property.object, env, ctx);
                const char *property = operand->as.get_property.property;
                if (object.type != VAL_OBJECT) {
                    fprintf(stderr, "Runtime error: Can only decrement object properties\n");
                    exit(1);
                }
                Object *obj = object.as.as_object;
                for (int i = 0; i < obj->num_fields; i++) {
                    if (strcmp(obj->field_names[i], property) == 0) {
                        Value old_val = obj->field_values[i];
                        Value new_val = value_sub_one(old_val);
                        obj->field_values[i] = new_val;
                        return new_val;
                    }
                }
                fprintf(stderr, "Runtime error: Property '%s' not found\n", property);
                exit(1);
            } else {
                fprintf(stderr, "Runtime error: Invalid operand for --\n");
                exit(1);
            }
        }

        case EXPR_POSTFIX_INC: {
            // x++: return old value then increment
            Expr *operand = expr->as.postfix_inc.operand;

            if (operand->type == EXPR_IDENT) {
                Value old_val = env_get(env, operand->as.ident, ctx);
                Value new_val = value_add_one(old_val);
                env_set(env, operand->as.ident, new_val, ctx);
                return old_val;  // Return old value!
            } else if (operand->type == EXPR_INDEX) {
                Value object = eval_expr(operand->as.index.object, env, ctx);
                Value index_val = eval_expr(operand->as.index.index, env, ctx);

                if (!is_integer(index_val)) {
                    fprintf(stderr, "Runtime error: Index must be an integer\n");
                    exit(1);
                }
                int32_t index = value_to_int(index_val);

                if (object.type == VAL_ARRAY) {
                    Value old_val = array_get(object.as.as_array, index);
                    Value new_val = value_add_one(old_val);
                    array_set(object.as.as_array, index, new_val);
                    return old_val;
                } else {
                    fprintf(stderr, "Runtime error: Can only use ++ on array elements\n");
                    exit(1);
                }
            } else if (operand->type == EXPR_GET_PROPERTY) {
                Value object = eval_expr(operand->as.get_property.object, env, ctx);
                const char *property = operand->as.get_property.property;
                if (object.type != VAL_OBJECT) {
                    fprintf(stderr, "Runtime error: Can only increment object properties\n");
                    exit(1);
                }
                Object *obj = object.as.as_object;
                for (int i = 0; i < obj->num_fields; i++) {
                    if (strcmp(obj->field_names[i], property) == 0) {
                        Value old_val = obj->field_values[i];
                        Value new_val = value_add_one(old_val);
                        obj->field_values[i] = new_val;
                        return old_val;
                    }
                }
                fprintf(stderr, "Runtime error: Property '%s' not found\n", property);
                exit(1);
            } else {
                fprintf(stderr, "Runtime error: Invalid operand for ++\n");
                exit(1);
            }
        }

        case EXPR_POSTFIX_DEC: {
            // x--: return old value then decrement
            Expr *operand = expr->as.postfix_dec.operand;

            if (operand->type == EXPR_IDENT) {
                Value old_val = env_get(env, operand->as.ident, ctx);
                Value new_val = value_sub_one(old_val);
                env_set(env, operand->as.ident, new_val, ctx);
                return old_val;
            } else if (operand->type == EXPR_INDEX) {
                Value object = eval_expr(operand->as.index.object, env, ctx);
                Value index_val = eval_expr(operand->as.index.index, env, ctx);

                if (!is_integer(index_val)) {
                    fprintf(stderr, "Runtime error: Index must be an integer\n");
                    exit(1);
                }
                int32_t index = value_to_int(index_val);

                if (object.type == VAL_ARRAY) {
                    Value old_val = array_get(object.as.as_array, index);
                    Value new_val = value_sub_one(old_val);
                    array_set(object.as.as_array, index, new_val);
                    return old_val;
                } else {
                    fprintf(stderr, "Runtime error: Can only use -- on array elements\n");
                    exit(1);
                }
            } else if (operand->type == EXPR_GET_PROPERTY) {
                Value object = eval_expr(operand->as.get_property.object, env, ctx);
                const char *property = operand->as.get_property.property;
                if (object.type != VAL_OBJECT) {
                    fprintf(stderr, "Runtime error: Can only decrement object properties\n");
                    exit(1);
                }
                Object *obj = object.as.as_object;
                for (int i = 0; i < obj->num_fields; i++) {
                    if (strcmp(obj->field_names[i], property) == 0) {
                        Value old_val = obj->field_values[i];
                        Value new_val = value_sub_one(old_val);
                        obj->field_values[i] = new_val;
                        return old_val;
                    }
                }
                fprintf(stderr, "Runtime error: Property '%s' not found\n", property);
                exit(1);
            } else {
                fprintf(stderr, "Runtime error: Invalid operand for --\n");
                exit(1);
            }
        }

        case EXPR_AWAIT: {
            // For MVP: await just evaluates the expression synchronously
            // TODO: Implement proper async/await scheduling
            Value awaited = eval_expr(expr->as.await_expr.awaited_expr, env, ctx);

            // If it's a task, we should join it
            // For now, just return the value as-is
            return awaited;
        }
    }

    return val_null();
}

// ========== STATEMENT EVALUATION ==========

void eval_stmt(Stmt *stmt, Environment *env, ExecutionContext *ctx) {
    switch (stmt->type) {
        case STMT_LET: {
            Value value = eval_expr(stmt->as.let.value, env, ctx);
            // If there's a type annotation, convert/check the value
            if (stmt->as.let.type_annotation != NULL) {
                value = convert_to_type(value, stmt->as.let.type_annotation, env, ctx);
            }
            env_define(env, stmt->as.let.name, value, 0, ctx);  // 0 = mutable
            break;
        }

        case STMT_CONST: {
            Value value = eval_expr(stmt->as.const_stmt.value, env, ctx);
            // If there's a type annotation, convert/check the value
            if (stmt->as.const_stmt.type_annotation != NULL) {
                value = convert_to_type(value, stmt->as.const_stmt.type_annotation, env, ctx);
            }
            env_define(env, stmt->as.const_stmt.name, value, 1, ctx);  // 1 = const
            break;
        }

        case STMT_EXPR: {
            eval_expr(stmt->as.expr, env, ctx);
            break;
        }

        case STMT_IF: {
            Value condition = eval_expr(stmt->as.if_stmt.condition, env, ctx);

            if (value_is_truthy(condition)) {
                eval_stmt(stmt->as.if_stmt.then_branch, env, ctx);
            } else if (stmt->as.if_stmt.else_branch != NULL) {
                eval_stmt(stmt->as.if_stmt.else_branch, env, ctx);
            }
            // No need to check return here, it propagates automatically
            break;
        }

        case STMT_WHILE: {
            for (;;) {
                Value condition = eval_expr(stmt->as.while_stmt.condition, env, ctx);

                if (!value_is_truthy(condition)) break;

                eval_stmt(stmt->as.while_stmt.body, env, ctx);

                // Check for break/continue/return/exception
                if (ctx->loop_state.is_breaking) {
                    ctx->loop_state.is_breaking = 0;
                    break;
                }
                if (ctx->loop_state.is_continuing) {
                    ctx->loop_state.is_continuing = 0;
                    continue;
                }
                if (ctx->return_state.is_returning || ctx->exception_state.is_throwing) {
                    break;
                }
            }
            break;
        }

        case STMT_FOR: {
            // Create new environment for loop scope
            Environment *loop_env = env_new(env);

            // Execute initializer
            if (stmt->as.for_loop.initializer) {
                eval_stmt(stmt->as.for_loop.initializer, loop_env, ctx);
            }

            // Loop
            for (;;) {
                // Check condition
                if (stmt->as.for_loop.condition) {
                    Value cond = eval_expr(stmt->as.for_loop.condition, loop_env, ctx);
                    if (!value_is_truthy(cond)) {
                        break;
                    }
                }

                // Execute body
                eval_stmt(stmt->as.for_loop.body, loop_env, ctx);

                // Check for break/continue/return/exception
                if (ctx->loop_state.is_breaking) {
                    ctx->loop_state.is_breaking = 0;
                    break;
                }
                if (ctx->loop_state.is_continuing) {
                    ctx->loop_state.is_continuing = 0;
                    // Fall through to increment
                }
                if (ctx->return_state.is_returning || ctx->exception_state.is_throwing) {
                    break;
                }

                // Execute increment
                if (stmt->as.for_loop.increment) {
                    eval_expr(stmt->as.for_loop.increment, loop_env, ctx);
                }
            }

            env_free(loop_env);
            break;
        }

        case STMT_FOR_IN: {
            Value iterable = eval_expr(stmt->as.for_in.iterable, env, ctx);

            Environment *loop_env = env_new(env);

            if (iterable.type == VAL_ARRAY) {
                Array *arr = iterable.as.as_array;

                for (int i = 0; i < arr->length; i++) {
                    // Bind variables
                    if (stmt->as.for_in.key_var) {
                        env_set(loop_env, stmt->as.for_in.key_var, val_i32(i), ctx);
                    }
                    env_set(loop_env, stmt->as.for_in.value_var, arr->elements[i], ctx);

                    // Execute body
                    eval_stmt(stmt->as.for_in.body, loop_env, ctx);

                    // Check break/continue/return/exception
                    if (ctx->loop_state.is_breaking) {
                        ctx->loop_state.is_breaking = 0;
                        break;
                    }
                    if (ctx->loop_state.is_continuing) {
                        ctx->loop_state.is_continuing = 0;
                        continue;
                    }
                    if (ctx->return_state.is_returning || ctx->exception_state.is_throwing) {
                        break;
                    }
                }
            } else if (iterable.type == VAL_OBJECT) {
                Object *obj = iterable.as.as_object;

                for (int i = 0; i < obj->num_fields; i++) {
                    // Bind variables
                    if (stmt->as.for_in.key_var) {
                        env_set(loop_env, stmt->as.for_in.key_var, val_string(obj->field_names[i]), ctx);
                    }
                    env_set(loop_env, stmt->as.for_in.value_var, obj->field_values[i], ctx);

                    // Execute body
                    eval_stmt(stmt->as.for_in.body, loop_env, ctx);

                    // Check break/continue/return/exception
                    if (ctx->loop_state.is_breaking) {
                        ctx->loop_state.is_breaking = 0;
                        break;
                    }
                    if (ctx->loop_state.is_continuing) {
                        ctx->loop_state.is_continuing = 0;
                        continue;
                    }
                    if (ctx->return_state.is_returning || ctx->exception_state.is_throwing) {
                        break;
                    }
                }
            } else {
                fprintf(stderr, "Runtime error: for-in requires array or object\n");
                exit(1);
            }

            env_free(loop_env);
            break;
        }

        case STMT_BREAK:
            ctx->loop_state.is_breaking = 1;
            break;

        case STMT_CONTINUE:
            ctx->loop_state.is_continuing = 1;
            break;

        case STMT_BLOCK: {
            for (int i = 0; i < stmt->as.block.count; i++) {
                eval_stmt(stmt->as.block.statements[i], env, ctx);
                // Check if a return/break/continue/exception happened
                if (ctx->return_state.is_returning || ctx->loop_state.is_breaking ||
                    ctx->loop_state.is_continuing || ctx->exception_state.is_throwing) {
                    break;
                }
            }
            break;
        }

        case STMT_RETURN: {
            // Evaluate return value (or null if none)
            if (stmt->as.return_stmt.value) {
                ctx->return_state.return_value = eval_expr(stmt->as.return_stmt.value, env, ctx);
            } else {
                ctx->return_state.return_value = val_null();
            }
            ctx->return_state.is_returning = 1;
            break;
        }

        case STMT_DEFINE_OBJECT: {
            // Create object type definition
            ObjectType *type = malloc(sizeof(ObjectType));
            type->name = strdup(stmt->as.define_object.name);
            type->num_fields = stmt->as.define_object.num_fields;

            // Copy field information
            type->field_names = malloc(sizeof(char*) * type->num_fields);
            type->field_types = malloc(sizeof(Type*) * type->num_fields);
            type->field_optional = malloc(sizeof(int) * type->num_fields);
            type->field_defaults = malloc(sizeof(Expr*) * type->num_fields);

            for (int i = 0; i < type->num_fields; i++) {
                type->field_names[i] = strdup(stmt->as.define_object.field_names[i]);
                type->field_types[i] = stmt->as.define_object.field_types[i];
                type->field_optional[i] = stmt->as.define_object.field_optional[i];
                type->field_defaults[i] = stmt->as.define_object.field_defaults[i];
            }

            // Register the type
            register_object_type(type);
            break;
        }

        case STMT_TRY: {
            // Execute try block
            eval_stmt(stmt->as.try_stmt.try_block, env, ctx);

            // Check if exception was thrown
            if (ctx->exception_state.is_throwing) {
                // Exception thrown - execute catch block if present
                if (stmt->as.try_stmt.catch_block != NULL) {
                    // Create new scope for catch parameter
                    Environment *catch_env = env_new(env);
                    env_set(catch_env, stmt->as.try_stmt.catch_param, ctx->exception_state.exception_value, ctx);

                    // Clear exception state
                    ctx->exception_state.is_throwing = 0;

                    // Execute catch block
                    eval_stmt(stmt->as.try_stmt.catch_block, catch_env, ctx);

                    env_free(catch_env);
                }
            }

            // Execute finally block if present (always executes)
            if (stmt->as.try_stmt.finally_block != NULL) {
                // Save current state (return/exception/break/continue)
                int was_returning = ctx->return_state.is_returning;
                Value saved_return = ctx->return_state.return_value;
                int was_throwing = ctx->exception_state.is_throwing;
                Value saved_exception = ctx->exception_state.exception_value;
                int was_breaking = ctx->loop_state.is_breaking;
                int was_continuing = ctx->loop_state.is_continuing;

                // Clear states before finally
                ctx->return_state.is_returning = 0;
                ctx->exception_state.is_throwing = 0;
                ctx->loop_state.is_breaking = 0;
                ctx->loop_state.is_continuing = 0;

                // Execute finally block
                eval_stmt(stmt->as.try_stmt.finally_block, env, ctx);

                // If finally didn't throw/return/break/continue, restore previous state
                if (!ctx->return_state.is_returning && !ctx->exception_state.is_throwing &&
                    !ctx->loop_state.is_breaking && !ctx->loop_state.is_continuing) {
                    ctx->return_state.is_returning = was_returning;
                    ctx->return_state.return_value = saved_return;
                    ctx->exception_state.is_throwing = was_throwing;
                    ctx->exception_state.exception_value = saved_exception;
                    ctx->loop_state.is_breaking = was_breaking;
                    ctx->loop_state.is_continuing = was_continuing;
                }
            }
            break;
        }

        case STMT_THROW: {
            // Evaluate the value to throw
            ctx->exception_state.exception_value = eval_expr(stmt->as.throw_stmt.value, env, ctx);
            ctx->exception_state.is_throwing = 1;
            break;
        }

        case STMT_SWITCH: {
            // Evaluate the switch expression
            Value switch_value = eval_expr(stmt->as.switch_stmt.expr, env, ctx);

            // Find matching case or default
            int matched_case = -1;
            int default_case = -1;

            for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
                if (stmt->as.switch_stmt.case_values[i] == NULL) {
                    // This is the default case
                    default_case = i;
                } else {
                    // Evaluate case value and compare
                    Value case_value = eval_expr(stmt->as.switch_stmt.case_values[i], env, ctx);

                    if (values_equal(switch_value, case_value)) {
                        matched_case = i;
                        break;
                    }
                }
            }

            // If no case matched, use default if available
            if (matched_case == -1 && default_case != -1) {
                matched_case = default_case;
            }

            // Execute from matched case onwards (fall-through behavior)
            if (matched_case != -1) {
                for (int i = matched_case; i < stmt->as.switch_stmt.num_cases; i++) {
                    eval_stmt(stmt->as.switch_stmt.case_bodies[i], env, ctx);

                    // Check for break, return, continue, or exception
                    if (ctx->loop_state.is_breaking) {
                        ctx->loop_state.is_breaking = 0;
                        break;
                    }
                    if (ctx->loop_state.is_continuing) {
                        // Continue propagates up to enclosing loop, exit switch
                        break;
                    }
                    if (ctx->return_state.is_returning || ctx->exception_state.is_throwing) {
                        break;
                    }
                }
            }
            break;
        }

        case STMT_IMPORT:
            // Import statements are handled by the module system during module loading
            // If we encounter one here, it's a no-op (module has already been loaded)
            break;

        case STMT_IMPORT_FFI:
            execute_import_ffi(stmt, ctx);
            break;

        case STMT_EXTERN_FN:
            execute_extern_fn(stmt, env, ctx);
            break;

        case STMT_EXPORT:
            // Export statements are handled by the module system during module loading
            // During normal execution, just execute the declaration if present
            if (stmt->as.export_stmt.is_declaration) {
                eval_stmt(stmt->as.export_stmt.declaration, env, ctx);
            }
            // Export lists and re-exports are no-ops during execution
            break;
    }
}

void eval_program(Stmt **stmts, int count, Environment *env, ExecutionContext *ctx) {
    for (int i = 0; i < count; i++) {
        eval_stmt(stmts[i], env, ctx);

        // Check for uncaught exception
        if (ctx->exception_state.is_throwing) {
            fprintf(stderr, "Runtime error: ");
            print_value(ctx->exception_state.exception_value);
            fprintf(stderr, "\n");
            // Print stack trace
            call_stack_print(&ctx->call_stack);
            // Clear stack for next execution (REPL mode)
            call_stack_free(&ctx->call_stack);
            exit(1);
        }
    }
}
