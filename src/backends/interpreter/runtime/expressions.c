#include "internal.h"

// Forward declaration for binary operations (in binary_ops.c)
Value eval_binary_expr(Expr *expr, Environment *env, ExecutionContext *ctx);

// ========== HELPER FUNCTIONS ==========

// Get the type name of a value for error messages
static const char* get_value_type_name(Value val) {
    switch (val.type) {
        case VAL_NULL: return "null";
        case VAL_BOOL: return "bool";
        case VAL_I8: return "i8";
        case VAL_I16: return "i16";
        case VAL_I32: return "i32";
        case VAL_I64: return "i64";
        case VAL_U8: return "u8";
        case VAL_U16: return "u16";
        case VAL_U32: return "u32";
        case VAL_U64: return "u64";
        case VAL_F32: return "f32";
        case VAL_F64: return "f64";
        case VAL_RUNE: return "rune";
        case VAL_STRING: return "string";
        case VAL_ARRAY: return "array";
        case VAL_OBJECT: return "object";
        case VAL_FUNCTION: return "function";
        case VAL_BUILTIN_FN: return "builtin function";
        case VAL_FFI_FUNCTION: return "ffi function";
        case VAL_PTR: return "pointer";
        case VAL_BUFFER: return "buffer";
        case VAL_FILE: return "file";
        case VAL_TASK: return "task";
        case VAL_CHANNEL: return "channel";
        case VAL_SOCKET: return "socket";
        default: return "unknown";
    }
}

// Helper to add two values (for increment operations)
static Value value_add_one(Value val, ExecutionContext *ctx) {
    if (is_float(val)) {
        double v = value_to_float(val);
        return (val.type == VAL_F32) ? val_f32((float)(v + 1.0)) : val_f64(v + 1.0);
    } else if (is_integer(val)) {
        int64_t v = value_to_int(val);
        ValueType result_type = val.type;
        return promote_value(val_i32((int32_t)(v + 1)), result_type);
    } else {
        runtime_error(ctx, "Can only increment numeric values");
        return val_null();  // Unreachable
    }
}

// Helper to subtract one from a value (for decrement operations)
static Value value_sub_one(Value val, ExecutionContext *ctx) {
    if (is_float(val)) {
        double v = value_to_float(val);
        return (val.type == VAL_F32) ? val_f32((float)(v - 1.0)) : val_f64(v - 1.0);
    } else if (is_integer(val)) {
        int64_t v = value_to_int(val);
        ValueType result_type = val.type;
        return promote_value(val_i32((int32_t)(v - 1)), result_type);
    } else {
        runtime_error(ctx, "Can only decrement numeric values");
        return val_null();  // Unreachable
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

        case EXPR_RUNE:
            return val_rune(expr->as.rune);

        case EXPR_UNARY: {
            Value operand = eval_expr(expr->as.unary.operand, env, ctx);
            Value unary_result = val_null();

            switch (expr->as.unary.op) {
                case UNARY_NOT:
                    unary_result = val_bool(!value_is_truthy(operand));
                    break;

                case UNARY_NEGATE:
                    if (is_float(operand)) {
                        unary_result = val_f64(-value_to_float(operand));
                    } else if (is_integer(operand)) {
                        // Preserve the original type when negating
                        switch (operand.type) {
                            case VAL_I8: unary_result = val_i8(-operand.as.as_i8); break;
                            case VAL_I16: unary_result = val_i16(-operand.as.as_i16); break;
                            case VAL_I32: unary_result = val_i32(-operand.as.as_i32); break;
                            case VAL_I64: unary_result = val_i64(-operand.as.as_i64); break;
                            case VAL_U8: unary_result = val_i16(-(int16_t)operand.as.as_u8); break;  // promote to i16
                            case VAL_U16: unary_result = val_i32(-(int32_t)operand.as.as_u16); break; // promote to i32
                            case VAL_U32: unary_result = val_i64(-(int64_t)operand.as.as_u32); break; // promote to i64
                            case VAL_U64: {
                                // Special case: u64 negation - check if value fits in i64
                                if (operand.as.as_u64 <= INT64_MAX) {
                                    unary_result = val_i64(-(int64_t)operand.as.as_u64);
                                } else {
                                    runtime_error(ctx, "Cannot negate u64 value larger than INT64_MAX");
                                }
                                break;
                            }
                            default:
                                runtime_error(ctx, "Cannot negate non-integer value");
                        }
                    } else {
                        runtime_error(ctx, "Cannot negate non-numeric value");
                    }
                    break;

                case UNARY_BIT_NOT:
                    if (is_integer(operand)) {
                        // Bitwise NOT - preserve the original type
                        switch (operand.type) {
                            case VAL_I8: unary_result = val_i8(~operand.as.as_i8); break;
                            case VAL_I16: unary_result = val_i16(~operand.as.as_i16); break;
                            case VAL_I32: unary_result = val_i32(~operand.as.as_i32); break;
                            case VAL_I64: unary_result = val_i64(~operand.as.as_i64); break;
                            case VAL_U8: unary_result = val_u8(~operand.as.as_u8); break;
                            case VAL_U16: unary_result = val_u16(~operand.as.as_u16); break;
                            case VAL_U32: unary_result = val_u32(~operand.as.as_u32); break;
                            case VAL_U64: unary_result = val_u64(~operand.as.as_u64); break;
                            default:
                                runtime_error(ctx, "Cannot apply bitwise NOT to non-integer value");
                        }
                    } else {
                        runtime_error(ctx, "Cannot apply bitwise NOT to non-integer value");
                    }
                    break;
            }
            // Release operand after unary operation
            VALUE_RELEASE(operand);
            return unary_result;
        }

        case EXPR_TERNARY: {
            Value condition = eval_expr(expr->as.ternary.condition, env, ctx);
            Value result = {0};
            if (value_is_truthy(condition)) {
                result = eval_expr(expr->as.ternary.true_expr, env, ctx);
            } else {
                result = eval_expr(expr->as.ternary.false_expr, env, ctx);
            }
            VALUE_RELEASE(condition);  // Release condition after checking
            return result;
        }

        case EXPR_IDENT:
            // Use fast resolved lookup if available, else fall back to hash lookup
            if (expr->as.ident.resolved.is_resolved) {
                return env_get_resolved(env, expr->as.ident.resolved.depth, expr->as.ident.resolved.slot);
            }
            return env_get(env, expr->as.ident.name, ctx);

        case EXPR_ASSIGN: {
            Value value = eval_expr(expr->as.assign.value, env, ctx);
            // Use fast resolved assignment if available, else fall back to hash lookup
            if (expr->as.assign.resolved.is_resolved) {
                env_set_resolved(env, expr->as.assign.resolved.depth, expr->as.assign.resolved.slot, value, ctx);
            } else {
                env_set(env, expr->as.assign.name, value, ctx);
            }
            return value;
        }

        case EXPR_BINARY:
            return eval_binary_expr(expr, env, ctx);

        case EXPR_CALL: {
            // Check if this is a method call (obj.method(...))
            int is_method_call = 0;
            Value method_self = {0};

            if (expr->as.call.func->type == EXPR_GET_PROPERTY) {
                is_method_call = 1;
                method_self = eval_expr(expr->as.call.func->as.get_property.object, env, ctx);

                // Special handling for file methods
                if (method_self.type == VAL_FILE) {
                    const char *method = expr->as.call.func->as.get_property.property;

                    // Evaluate arguments (stack-allocated for small counts)
                    Value method_stack_args[8];
                    Value *args = NULL;
                    if (expr->as.call.num_args > 0) {
                        args = (expr->as.call.num_args <= 8) ? method_stack_args : malloc(sizeof(Value) * expr->as.call.num_args);
                        for (int i = 0; i < expr->as.call.num_args; i++) {
                            args[i] = eval_expr(expr->as.call.args[i], env, ctx);
                        }
                    }

                    Value result = call_file_method(method_self.as.as_file, method, args, expr->as.call.num_args, ctx);
                    // Release argument values (file methods don't retain them)
                    if (args) {
                        for (int i = 0; i < expr->as.call.num_args; i++) {
                            VALUE_RELEASE(args[i]);
                        }
                        if (args != method_stack_args) free(args);
                    }
                    VALUE_RELEASE(method_self);  // Release method receiver
                    return result;
                }

                // Special handling for socket methods
                if (method_self.type == VAL_SOCKET) {
                    const char *method = expr->as.call.func->as.get_property.property;

                    // Evaluate arguments (stack-allocated for small counts)
                    Value method_stack_args[8];
                    Value *args = NULL;
                    if (expr->as.call.num_args > 0) {
                        args = (expr->as.call.num_args <= 8) ? method_stack_args : malloc(sizeof(Value) * expr->as.call.num_args);
                        for (int i = 0; i < expr->as.call.num_args; i++) {
                            args[i] = eval_expr(expr->as.call.args[i], env, ctx);
                        }
                    }

                    Value result = call_socket_method(method_self.as.as_socket, method, args, expr->as.call.num_args, ctx);
                    // Release argument values (socket methods don't retain them)
                    if (args) {
                        for (int i = 0; i < expr->as.call.num_args; i++) {
                            VALUE_RELEASE(args[i]);
                        }
                        if (args != method_stack_args) free(args);
                    }
                    VALUE_RELEASE(method_self);  // Release method receiver
                    return result;
                }

                // Special handling for array methods
                if (method_self.type == VAL_ARRAY) {
                    const char *method = expr->as.call.func->as.get_property.property;

                    // Evaluate arguments (stack-allocated for small counts)
                    Value method_stack_args[8];
                    Value *args = NULL;
                    if (expr->as.call.num_args > 0) {
                        args = (expr->as.call.num_args <= 8) ? method_stack_args : malloc(sizeof(Value) * expr->as.call.num_args);
                        for (int i = 0; i < expr->as.call.num_args; i++) {
                            args[i] = eval_expr(expr->as.call.args[i], env, ctx);
                        }
                    }

                    Value result = call_array_method(method_self.as.as_array, method, args, expr->as.call.num_args, expr->line, ctx);
                    // Release argument values (array methods don't retain them)
                    if (args) {
                        for (int i = 0; i < expr->as.call.num_args; i++) {
                            VALUE_RELEASE(args[i]);
                        }
                        if (args != method_stack_args) free(args);
                    }
                    VALUE_RELEASE(method_self);  // Release method receiver
                    return result;
                }

                // Special handling for string methods
                if (method_self.type == VAL_STRING) {
                    const char *method = expr->as.call.func->as.get_property.property;

                    // Evaluate arguments (stack-allocated for small counts)
                    Value method_stack_args[8];
                    Value *args = NULL;
                    if (expr->as.call.num_args > 0) {
                        args = (expr->as.call.num_args <= 8) ? method_stack_args : malloc(sizeof(Value) * expr->as.call.num_args);
                        for (int i = 0; i < expr->as.call.num_args; i++) {
                            args[i] = eval_expr(expr->as.call.args[i], env, ctx);
                        }
                    }

                    Value result = call_string_method(method_self.as.as_string, method, args, expr->as.call.num_args, expr->line, ctx);
                    // Release argument values (string methods don't retain them)
                    if (args) {
                        for (int i = 0; i < expr->as.call.num_args; i++) {
                            VALUE_RELEASE(args[i]);
                        }
                        if (args != method_stack_args) free(args);
                    }
                    VALUE_RELEASE(method_self);  // Release method receiver
                    return result;
                }

                // Special handling for buffer methods (to_string)
                if (method_self.type == VAL_BUFFER) {
                    const char *method = expr->as.call.func->as.get_property.property;

                    if (strcmp(method, "to_string") == 0) {
                        Buffer *buf = method_self.as.as_buffer;
                        // Convert buffer to string (interpret as UTF-8)
                        char *str_data = malloc(buf->length + 1);
                        memcpy(str_data, buf->data, buf->length);
                        str_data[buf->length] = '\0';
                        Value result = val_string(str_data);
                        free(str_data);
                        VALUE_RELEASE(method_self);
                        return result;
                    }

                    runtime_error(ctx, "Unknown buffer method '%s'", method);
                    VALUE_RELEASE(method_self);
                    return val_null();
                }

                // Special handling for channel methods
                if (method_self.type == VAL_CHANNEL) {
                    const char *method = expr->as.call.func->as.get_property.property;

                    // Evaluate arguments (stack-allocated for small counts)
                    Value method_stack_args[8];
                    Value *args = NULL;
                    if (expr->as.call.num_args > 0) {
                        args = (expr->as.call.num_args <= 8) ? method_stack_args : malloc(sizeof(Value) * expr->as.call.num_args);
                        for (int i = 0; i < expr->as.call.num_args; i++) {
                            args[i] = eval_expr(expr->as.call.args[i], env, ctx);
                        }
                    }

                    Value result = call_channel_method(method_self.as.as_channel, method, args, expr->as.call.num_args, ctx);
                    // Release argument values (channel methods don't retain them)
                    if (args) {
                        for (int i = 0; i < expr->as.call.num_args; i++) {
                            VALUE_RELEASE(args[i]);
                        }
                        if (args != method_stack_args) free(args);
                    }
                    VALUE_RELEASE(method_self);  // Release method receiver
                    return result;
                }

                // Special handling for object built-in methods (e.g., serialize, keys)
                // But user-defined methods take precedence over built-ins
                if (method_self.type == VAL_OBJECT) {
                    const char *method = expr->as.call.func->as.get_property.property;

                    // Only handle built-in object methods here (serialize, keys, has, delete)
                    // BUT first check if the object has a user-defined method with this name
                    if (strcmp(method, "serialize") == 0 || strcmp(method, "keys") == 0 ||
                        strcmp(method, "has") == 0 || strcmp(method, "delete") == 0) {
                        // Check if object has a user-defined function with this name
                        Object *obj = method_self.as.as_object;
                        int has_user_method = 0;
                        int method_idx = object_lookup_field(obj, method);
                        if (method_idx >= 0 && obj->field_values[method_idx].type == VAL_FUNCTION) {
                            has_user_method = 1;
                        }

                        // Only use built-in if no user-defined method exists
                        if (!has_user_method) {
                            // Evaluate arguments
                            Value *args = NULL;
                            if (expr->as.call.num_args > 0) {
                                args = malloc(sizeof(Value) * expr->as.call.num_args);
                                for (int i = 0; i < expr->as.call.num_args; i++) {
                                    args[i] = eval_expr(expr->as.call.args[i], env, ctx);
                                }
                            }

                            Value result = call_object_method(method_self.as.as_object, method, args, expr->as.call.num_args, ctx);
                            // Release argument values (object methods don't retain them)
                            if (args) {
                                for (int i = 0; i < expr->as.call.num_args; i++) {
                                    VALUE_RELEASE(args[i]);
                                }
                                free(args);
                            }
                            VALUE_RELEASE(method_self);  // Release method receiver
                            return result;
                        }
                        // Has user method - fall through to user-defined method handling
                    }
                    // For user-defined methods, fall through to normal function call handling
                }
            }

            // Evaluate the function expression
            // For method calls, we already have method_self - don't re-evaluate the object
            Value func;
            if (is_method_call && method_self.type == VAL_OBJECT) {
                // Get the method from the object directly
                const char *method_name = expr->as.call.func->as.get_property.property;
                Object *obj = method_self.as.as_object;
                int method_idx = object_lookup_field(obj, method_name);
                if (method_idx >= 0) {
                    func = obj->field_values[method_idx];
                    VALUE_RETAIN(func);
                } else {
                    runtime_error_at(ctx, expr->line, "Object has no method '%s'", method_name);
                    VALUE_RELEASE(method_self);
                    return val_null();
                }
            } else {
                func = eval_expr(expr->as.call.func, env, ctx);
            }

            // Check for optional chain short-circuit: obj?.method(args) when obj is null
            // If func came from an optional chain that returned null, short-circuit the entire call
            if (expr->as.call.func->type == EXPR_OPTIONAL_CHAIN && func.type == VAL_NULL) {
                if (is_method_call) {
                    VALUE_RELEASE(method_self);
                }
                return val_null();
            }

            // Evaluate arguments - use stack allocation for small arg counts (common case)
            #define MAX_STACK_ARGS 8
            Value stack_args[MAX_STACK_ARGS];
            Value *args = NULL;
            int args_on_heap = 0;
            if (expr->as.call.num_args > 0) {
                if (expr->as.call.num_args <= MAX_STACK_ARGS) {
                    args = stack_args;
                } else {
                    args = malloc(sizeof(Value) * expr->as.call.num_args);
                    args_on_heap = 1;
                }
                for (int i = 0; i < expr->as.call.num_args; i++) {
                    args[i] = eval_expr(expr->as.call.args[i], env, ctx);
                }
            }

            Value result = {0};
            int should_release_args = 1;  // Track whether we need to release args

            if (func.type == VAL_BUILTIN_FN) {
                // Call builtin function
                BuiltinFn fn = func.as.as_builtin_fn;
                result = fn(args, expr->as.call.num_args, ctx);
                // Builtin functions don't retain args, so we must release them
                should_release_args = 1;
            } else if (func.type == VAL_FUNCTION) {
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

                // Check argument count (must be between required and total params)
                // If function has rest param, allow unlimited extra args
                int max_args = fn->rest_param ? INT_MAX : fn->num_params;
                if (expr->as.call.num_args < required_params || expr->as.call.num_args > max_args) {
                    if (fn->rest_param) {
                        runtime_error(ctx, "Function expects at least %d arguments, got %d",
                                required_params, expr->as.call.num_args);
                    } else if (required_params == fn->num_params) {
                        runtime_error(ctx, "Function expects %d arguments, got %d",
                                fn->num_params, expr->as.call.num_args);
                    } else {
                        runtime_error(ctx, "Function expects %d-%d arguments, got %d",
                                required_params, fn->num_params, expr->as.call.num_args);
                    }
                    // Release function and args before returning
                    VALUE_RELEASE(func);
                    if (args) {
                        for (int i = 0; i < expr->as.call.num_args; i++) {
                            VALUE_RELEASE(args[i]);
                        }
                        if (args_on_heap) free(args);
                    }
                    return val_null();
                }

                // Determine function name for stack trace
                const char *fn_name = "<anonymous>";
                if (is_method_call && expr->as.call.func->type == EXPR_GET_PROPERTY) {
                    fn_name = expr->as.call.func->as.get_property.property;
                } else if (expr->as.call.func->type == EXPR_IDENT) {
                    fn_name = expr->as.call.func->as.ident.name;
                }

                // Check for stack overflow (prevent infinite recursion)
                if (ctx->call_stack.count >= ctx->max_stack_depth) {
                    runtime_error(ctx, "Maximum call stack depth exceeded (infinite recursion?)");
                    // Release function and args before returning
                    VALUE_RELEASE(func);
                    if (args) {
                        for (int i = 0; i < expr->as.call.num_args; i++) {
                            VALUE_RELEASE(args[i]);
                        }
                        if (args_on_heap) free(args);
                    }
                    return val_null();
                }

                // Push call onto stack trace (with line number from call site)
                call_stack_push_line(&ctx->call_stack, fn_name, expr->line);

                // Create call environment with closure_env as parent
                Environment *call_env = env_new(fn->closure_env);

                // Bind parameters FIRST using fast path with pre-computed hashes
                // This must happen before 'self' injection to preserve slot order
                // for resolved variable lookups (params at slots 0, 1, 2, ...)
                for (int i = 0; i < fn->num_params; i++) {
                    Value arg_value = {0};

                    // Use provided argument or evaluate default
                    if (i < expr->as.call.num_args) {
                        // Argument was provided
                        arg_value = args[i];
                    } else {
                        // Argument missing - use default value
                        if (fn->param_defaults && fn->param_defaults[i]) {
                            // Evaluate default expression in the closure environment
                            arg_value = eval_expr(fn->param_defaults[i], fn->closure_env, ctx);
                        } else {
                            // Should never happen if arity check is correct
                            runtime_error(ctx, "Missing required parameter '%s'", fn->param_names[i]);
                        }
                    }

                    // Type check if parameter has type annotation
                    if (fn->param_types[i]) {
                        arg_value = convert_to_type(arg_value, fn->param_types[i], call_env, ctx);
                    }

                    // Use fast param binding with pre-computed hash (skips redundant checks)
                    env_define_param(call_env, fn->param_names[i], fn->param_hashes[i], arg_value);
                }

                // Bind rest parameter if present (collect extra args into array)
                if (fn->rest_param) {
                    Array *rest_arr = array_new();
                    int extra_count = expr->as.call.num_args - fn->num_params;
                    if (extra_count > 0 && args) {
                        for (int i = fn->num_params; i < expr->as.call.num_args; i++) {
                            Value arg = args[i];
                            // Type check if rest param has type annotation (array element type)
                            if (fn->rest_param_type) {
                                arg = convert_to_type(arg, fn->rest_param_type, call_env, ctx);
                            }
                            array_push(rest_arr, arg);
                        }
                    }
                    env_define(call_env, fn->rest_param, val_array(rest_arr), 0, ctx);
                }

                // Inject 'self' AFTER parameters to preserve slot order for resolved lookups
                if (is_method_call) {
                    env_set(call_env, "self", method_self, ctx);
                    VALUE_RELEASE(method_self);  // Release original reference (env_set retained it)
                }

                // Save defer stack depth before executing function body
                int defer_depth_before = ctx->defer_stack.count;

                // Execute body
                ctx->return_state.is_returning = 0;
                eval_stmt(fn->body, call_env, ctx);

                // Execute deferred calls (in LIFO order) before returning
                // This happens even if there was an exception
                if (ctx->defer_stack.count > defer_depth_before) {
                    // Create a temporary defer stack with just this function's defers
                    DeferStack local_defers;
                    local_defers.count = ctx->defer_stack.count - defer_depth_before;
                    local_defers.capacity = local_defers.count;
                    local_defers.calls = &ctx->defer_stack.calls[defer_depth_before];
                    local_defers.envs = &ctx->defer_stack.envs[defer_depth_before];

                    // Execute the defers
                    defer_stack_execute(&local_defers, ctx);

                    // Restore defer stack to pre-function depth
                    ctx->defer_stack.count = defer_depth_before;
                }

                // Get result
                result = ctx->return_state.return_value;

                // Check return type if specified (but not if exception is being thrown)
                if (fn->return_type && !ctx->exception_state.is_throwing) {
                    // null return type allows functions to not return a value explicitly
                    if (!ctx->return_state.is_returning && fn->return_type->kind != TYPE_NULL) {
                        runtime_error(ctx, "Function with return type must return a value");
                    }
                    result = convert_to_type(result, fn->return_type, call_env, ctx);
                }

                // Reset return state
                ctx->return_state.is_returning = 0;

                // Retain result for the caller (so it survives call_env cleanup)
                // The caller now owns this reference
                VALUE_RETAIN(result);

                // Pop call from stack trace (but not if exception is active - preserve stack for error reporting)
                if (!ctx->exception_state.is_throwing) {
                    call_stack_pop(&ctx->call_stack);
                }

                // Release call environment (reference counted - will be freed when no longer used)
                env_release(call_env);
                // User-defined functions retained args via env_set, so don't release them again
                should_release_args = 0;
            } else if (func.type == VAL_FFI_FUNCTION) {
                // Call FFI function
                FFIFunction *ffi_func = (FFIFunction*)func.as.as_ffi_function;
                result = ffi_call_function(ffi_func, args, expr->as.call.num_args, ctx);
                // FFI functions don't retain args, so we must release them
                should_release_args = 1;
            } else if (func.type == VAL_TYPE) {
                // Type constructor: i32("42"), f64("3.14"), bool("true"), etc.
                if (expr->as.call.num_args != 1 || args == NULL) {
                    runtime_error(ctx, "Type constructor expects exactly 1 argument");
                    result = val_null();  // Unreachable, but satisfies analyzer
                } else {
                    TypeKind target_kind = func.as.as_type;
                    // Create a temporary Type struct for parse_string_to_type
                    Type temp_type = { .kind = target_kind, .nullable = 0, .type_name = NULL, .element_type = NULL };
                    // Use parse_string_to_type which allows string parsing (unlike convert_to_type)
                    result = parse_string_to_type(args[0], &temp_type, env, ctx);
                }
                // Type constructors don't retain args via env_set, so we must release them
                should_release_args = 1;
            } else {
                // Provide a descriptive error message with context
                const char *type_name = get_value_type_name(func);
                if (expr->as.call.func->type == EXPR_IDENT) {
                    runtime_error_at(ctx, expr->line, "'%s' is not a function (got %s)",
                            expr->as.call.func->as.ident.name, type_name);
                } else if (expr->as.call.func->type == EXPR_GET_PROPERTY) {
                    runtime_error_at(ctx, expr->line, "'%s' is not a function (got %s)",
                            expr->as.call.func->as.get_property.property, type_name);
                } else {
                    runtime_error_at(ctx, expr->line, "Value is not a function (got %s)", type_name);
                }
            }

            // Release args if needed (for builtin/FFI functions)
            if (args && should_release_args) {
                for (int i = 0; i < expr->as.call.num_args; i++) {
                    VALUE_RELEASE(args[i]);
                }
            }

            // Free args array (only if heap-allocated)
            if (args_on_heap) {
                free(args);
            }

            // Release function value
            VALUE_RELEASE(func);
            return result;
        }

        case EXPR_GET_PROPERTY: {
            Value object = eval_expr(expr->as.get_property.object, env, ctx);
            const char *property = expr->as.get_property.property;
            Value result = {0};

            if (object.type == VAL_STRING) {
                String *str = object.as.as_string;

                // .length property - returns codepoint count
                if (strcmp(property, "length") == 0) {
                    // Compute character length if not cached
                    if (str->char_length < 0) {
                        str->char_length = utf8_count_codepoints(str->data, str->length);
                    }
                    result = val_i32(str->char_length);
                } else if (strcmp(property, "byte_length") == 0) {
                    // .byte_length property - returns byte count
                    result = val_i32(str->length);
                } else {
                    runtime_error(ctx, "Unknown property '%s' for string", property);
                }
            } else if (object.type == VAL_BUFFER) {
                if (strcmp(property, "length") == 0) {
                    result = val_int(object.as.as_buffer->length);
                } else if (strcmp(property, "capacity") == 0) {
                    result = val_int(object.as.as_buffer->capacity);
                } else {
                    runtime_error(ctx, "Unknown property '%s' for buffer", property);
                }
            } else if (object.type == VAL_FILE) {
                FileHandle *file = object.as.as_file;
                if (strcmp(property, "path") == 0) {
                    result = val_string(file->path);
                } else if (strcmp(property, "mode") == 0) {
                    result = val_string(file->mode);
                } else if (strcmp(property, "closed") == 0) {
                    result = val_bool(file->closed);
                } else {
                    runtime_error(ctx, "Unknown property '%s' for file", property);
                }
            } else if (object.type == VAL_SOCKET) {
                // Socket properties (read-only)
                result = get_socket_property(object.as.as_socket, property, ctx);
            } else if (object.type == VAL_ARRAY) {
                // Array properties
                if (strcmp(property, "length") == 0) {
                    result = val_i32(object.as.as_array->length);
                } else {
                    runtime_error(ctx, "Array has no property '%s'", property);
                }
            } else if (object.type == VAL_OBJECT) {
                // Look up field in object using hash table
                Object *obj = object.as.as_object;
                int idx = object_lookup_field(obj, property);
                if (idx >= 0) {
                    result = obj->field_values[idx];
                    // Retain the field value so it survives object release
                    VALUE_RETAIN(result);

                    // If the result is a function, bind 'self' to the object
                    // This enables method references like spawn(obj.method, ...)
                    if (result.type == VAL_FUNCTION) {
                            Function *orig_fn = result.as.as_function;

                            // Create a new environment with 'self' bound
                            Environment *bound_env = env_new(orig_fn->closure_env);
                            env_define(bound_env, "self", object, 0, ctx);

                            // Create a shallow copy of the function with the new closure
                            Function *bound_fn = malloc(sizeof(Function));
                            bound_fn->is_async = orig_fn->is_async;
                            bound_fn->param_names = orig_fn->param_names;
                            bound_fn->param_types = orig_fn->param_types;
                            bound_fn->param_defaults = orig_fn->param_defaults;
                            bound_fn->param_hashes = orig_fn->param_hashes;  // Share pre-computed hashes
                            bound_fn->num_params = orig_fn->num_params;
                            bound_fn->rest_param = orig_fn->rest_param;  // Share rest param name
                            bound_fn->rest_param_type = orig_fn->rest_param_type;  // Share type
                            bound_fn->return_type = orig_fn->return_type;
                            bound_fn->body = orig_fn->body;
                            bound_fn->closure_env = bound_env;
                            bound_fn->ref_count = 1;
                            bound_fn->is_bound = 1;  // Mark as bound - don't free param arrays

                            // Note: object is retained by env_define above
                            // Note: result (orig_fn) was retained at line 1186
                            // We don't release it here since bound_fn shares pointers with it

                            // Return the bound function (no need to release object, env_define holds it)
                            return val_function(bound_fn);
                        }

                    VALUE_RELEASE(object);
                    return result;
                }
                runtime_error(ctx, "Object has no field '%s'", property);
            } else {
                runtime_error(ctx, "Only strings, buffers, arrays, and objects have properties");
            }

            // Release object after accessing property
            VALUE_RELEASE(object);
            return result;
        }

        case EXPR_INDEX: {
            Value object = eval_expr(expr->as.index.object, env, ctx);
            Value index_val = eval_expr(expr->as.index.index, env, ctx);
            Value result = {0};

            // FAST PATH: array[i32] - most common indexing case
            if (object.type == VAL_ARRAY && index_val.type == VAL_I32) {
                Array *arr = object.as.as_array;
                int32_t index = index_val.as.as_i32;
                if (index >= 0 && index < arr->length) {
                    result = arr->elements[index];
                    VALUE_RETAIN(result);
                    VALUE_RELEASE(object);
                    return result;
                }
                // Fall through to normal path for bounds error
            }

            // Object property access with string key
            if (object.type == VAL_OBJECT && index_val.type == VAL_STRING) {
                Object *obj = object.as.as_object;
                const char *key = index_val.as.as_string->data;

                // Look up field by key using hash table
                int idx = object_lookup_field(obj, key);
                if (idx >= 0) {
                    result = obj->field_values[idx];
                    VALUE_RETAIN(result);
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    return result;
                }

                // Field not found, return null
                VALUE_RELEASE(object);
                VALUE_RELEASE(index_val);
                return val_null();
            }

            // For arrays, strings, and buffers, index must be an integer
            if (!is_integer(index_val)) {
                runtime_error(ctx, "Index must be an integer");
            }

            int32_t index = value_to_int(index_val);

            if (object.type == VAL_STRING) {
                String *str = object.as.as_string;

                // Compute character length if not cached
                if (str->char_length < 0) {
                    str->char_length = utf8_count_codepoints(str->data, str->length);
                }

                // Check bounds using character count (not byte count)
                if (index < 0 || index >= str->char_length) {
                    runtime_error(ctx, "String index %d out of bounds (length=%d)", index, str->char_length);
                }

                // Find byte offset of the i-th codepoint
                int byte_pos = utf8_byte_offset(str->data, str->length, index);

                // Decode the codepoint at that position
                uint32_t codepoint = utf8_decode_at(str->data, byte_pos);

                result = val_rune(codepoint);  // New value, safe to release object
            } else if (object.type == VAL_BUFFER) {
                Buffer *buf = object.as.as_buffer;

                if (index < 0 || index >= buf->length) {
                    runtime_error(ctx, "Buffer index %d out of bounds (length %d)", index, buf->length);
                }

                // Return the byte as an integer (u8)
                result = val_u8(((unsigned char *)buf->data)[index]);  // New value, safe to release object
            } else if (object.type == VAL_ARRAY) {
                // Array indexing
                result = array_get(object.as.as_array, index, ctx);
                // Retain the element so it survives array release
                VALUE_RETAIN(result);
            } else if (object.type == VAL_PTR) {
                // Raw pointer indexing - no bounds checking (unsafe!)
                void *ptr = object.as.as_ptr;
                if (ptr == NULL) {
                    runtime_error(ctx, "Cannot index into null pointer");
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    return val_null();
                }
                // Return the byte as u8
                result = val_u8(((unsigned char *)ptr)[index]);
            } else {
                runtime_error(ctx, "Only strings, buffers, arrays, pointers, and objects can be indexed");
                VALUE_RELEASE(object);
                VALUE_RELEASE(index_val);
                return val_null();
            }

            // Release the object and index after use
            VALUE_RELEASE(object);
            VALUE_RELEASE(index_val);
            return result;
        }

        case EXPR_INDEX_ASSIGN: {
            Value object = eval_expr(expr->as.index_assign.object, env, ctx);
            Value index_val = eval_expr(expr->as.index_assign.index, env, ctx);
            Value value = eval_expr(expr->as.index_assign.value, env, ctx);

            // FAST PATH: array[i32] = value - most common assignment case
            if (object.type == VAL_ARRAY && index_val.type == VAL_I32) {
                Array *arr = object.as.as_array;
                int32_t index = index_val.as.as_i32;
                // Check bounds and untyped array (most common case)
                if (index >= 0 && index < arr->length && !arr->element_type) {
                    // Release old value, retain new value
                    VALUE_RELEASE(arr->elements[index]);
                    VALUE_RETAIN(value);
                    arr->elements[index] = value;
                    VALUE_RELEASE(object);
                    return value;
                }
                // Fall through to normal path for bounds extension, typed arrays, or error
            }

            // Object property assignment with string key
            if (object.type == VAL_OBJECT && index_val.type == VAL_STRING) {
                Object *obj = object.as.as_object;
                const char *key = index_val.as.as_string->data;

                // Look for existing field using hash table
                int idx = object_lookup_field(obj, key);
                if (idx >= 0) {
                    // Update existing field
                    VALUE_RELEASE(obj->field_values[idx]);
                    obj->field_values[idx] = value;
                    VALUE_RETAIN(value);
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    return value;
                }

                // Add new field - invalidate hash table (will fall back to linear search)
                if (obj->hash_table) {
                    free(obj->hash_table);
                    obj->hash_table = NULL;
                    obj->hash_capacity = 0;
                }
                obj->num_fields++;
                obj->field_names = realloc(obj->field_names, obj->num_fields * sizeof(char *));
                obj->field_values = realloc(obj->field_values, obj->num_fields * sizeof(Value));
                obj->field_names[obj->num_fields - 1] = strdup(key);
                obj->field_values[obj->num_fields - 1] = value;
                VALUE_RETAIN(value);
                VALUE_RELEASE(object);
                VALUE_RELEASE(index_val);
                return value;
            }

            // For arrays, strings, and buffers, index must be an integer
            if (!is_integer(index_val)) {
                runtime_error(ctx, "Index must be an integer");
            }

            int32_t index = value_to_int(index_val);

            if (object.type == VAL_ARRAY) {
                // Array assignment - value can be any type
                array_set(object.as.as_array, index, value, ctx);
                VALUE_RELEASE(object);
                VALUE_RELEASE(index_val);
                return value;
            }

            // For strings and buffers, value must be an integer (byte) or rune
            if (!is_integer(value) && value.type != VAL_RUNE) {
                runtime_error(ctx, "Index value must be an integer (byte) or rune for strings/buffers");
            }

            if (object.type == VAL_STRING) {
                String *str = object.as.as_string;

                if (index < 0 || index >= str->length) {
                    runtime_error(ctx, "String index %d out of bounds (length %d)", index, str->length);
                }

                // Get the rune value (either from rune type or integer)
                uint32_t rune_val;
                if (value.type == VAL_RUNE) {
                    rune_val = value.as.as_rune;
                } else {
                    rune_val = (uint32_t)value_to_int(value);
                }

                // Calculate bytes needed for new rune
                int new_len;
                if (rune_val < 0x80) new_len = 1;
                else if (rune_val < 0x800) new_len = 2;
                else if (rune_val < 0x10000) new_len = 3;
                else new_len = 4;

                // Get byte length of existing character at this position
                int old_len = utf8_char_byte_length((unsigned char)str->data[index]);
                if (index + old_len > str->length) {
                    old_len = str->length - index;
                }

                if (new_len == old_len) {
                    // Same size - just overwrite in place
                    utf8_encode(rune_val, str->data + index);
                } else {
                    // Different size - need to resize string
                    int new_total = str->length - old_len + new_len;
                    char *new_data = malloc(new_total + 1);
                    if (!new_data) {
                        runtime_error(ctx, "Failed to allocate memory for string resize");
                        VALUE_RELEASE(object);
                        VALUE_RELEASE(index_val);
                        VALUE_RELEASE(value);
                        return val_null();
                    }

                    // Copy prefix (before index)
                    memcpy(new_data, str->data, index);

                    // Encode new rune
                    utf8_encode(rune_val, new_data + index);

                    // Copy suffix (after old character)
                    int suffix_start = index + old_len;
                    int suffix_len = str->length - suffix_start;
                    if (suffix_len > 0) {
                        memcpy(new_data + index + new_len, str->data + suffix_start, suffix_len);
                    }

                    new_data[new_total] = '\0';

                    // Replace string data
                    free(str->data);
                    str->data = new_data;
                    str->length = new_total;
                    str->char_length = -1;  // Invalidate cached character count
                }

                VALUE_RELEASE(object);
                VALUE_RELEASE(index_val);
                // Don't release value - it's returned
                return value;
            } else if (object.type == VAL_BUFFER) {
                Buffer *buf = object.as.as_buffer;

                if (index < 0 || index >= buf->length) {
                    runtime_error(ctx, "Buffer index %d out of bounds (length %d)", index, buf->length);
                }

                // Buffers are mutable - set the byte
                ((unsigned char *)buf->data)[index] = (unsigned char)value_to_int(value);
                VALUE_RELEASE(object);
                VALUE_RELEASE(index_val);
                // Don't release value - it's returned
                return value;
            } else if (object.type == VAL_PTR) {
                // Raw pointer indexing - no bounds checking (unsafe!)
                void *ptr = object.as.as_ptr;
                if (ptr == NULL) {
                    runtime_error(ctx, "Cannot index into null pointer");
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    VALUE_RELEASE(value);
                    return val_null();
                }
                // Treat as byte array
                ((unsigned char *)ptr)[index] = (unsigned char)value_to_int(value);
                VALUE_RELEASE(object);
                VALUE_RELEASE(index_val);
                return value;
            } else {
                runtime_error(ctx, "Only strings, buffers, arrays, pointers, and objects support index assignment");
                VALUE_RELEASE(object);
                VALUE_RELEASE(index_val);
                VALUE_RELEASE(value);
                return val_null();
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
                    // Copy nullable flag
                    fn->param_types[i]->nullable = expr->as.function.param_types[i]->nullable;
                    // Copy type_name for custom types (enums and objects)
                    if (expr->as.function.param_types[i]->type_name) {
                        fn->param_types[i]->type_name = strdup(expr->as.function.param_types[i]->type_name);
                    }
                    // Copy element_type for arrays
                    if (expr->as.function.param_types[i]->element_type) {
                        fn->param_types[i]->element_type = type_new(expr->as.function.param_types[i]->element_type->kind);
                        fn->param_types[i]->element_type->nullable = expr->as.function.param_types[i]->element_type->nullable;
                        if (expr->as.function.param_types[i]->element_type->type_name) {
                            fn->param_types[i]->element_type->type_name = strdup(expr->as.function.param_types[i]->element_type->type_name);
                        }
                    }
                } else {
                    fn->param_types[i] = NULL;
                }
            }

            // Store parameter defaults (AST expressions, not evaluated yet)
            // We share the AST nodes (not copied) since they're immutable
            if (expr->as.function.param_defaults) {
                fn->param_defaults = malloc(sizeof(Expr*) * expr->as.function.num_params);
                for (int i = 0; i < expr->as.function.num_params; i++) {
                    fn->param_defaults[i] = expr->as.function.param_defaults[i];
                }
            } else {
                fn->param_defaults = NULL;
            }

            // Pre-compute parameter name hashes for fast binding at call time
            if (expr->as.function.num_params > 0) {
                fn->param_hashes = malloc(sizeof(uint32_t) * expr->as.function.num_params);
                for (int i = 0; i < expr->as.function.num_params; i++) {
                    fn->param_hashes[i] = hash_string(fn->param_names[i]);
                }
            } else {
                fn->param_hashes = NULL;
            }

            fn->num_params = expr->as.function.num_params;

            // Copy rest parameter (varargs) if present
            if (expr->as.function.rest_param) {
                fn->rest_param = strdup(expr->as.function.rest_param);
                if (expr->as.function.rest_param_type) {
                    fn->rest_param_type = type_new(expr->as.function.rest_param_type->kind);
                    fn->rest_param_type->nullable = expr->as.function.rest_param_type->nullable;
                    if (expr->as.function.rest_param_type->type_name) {
                        fn->rest_param_type->type_name = strdup(expr->as.function.rest_param_type->type_name);
                    }
                } else {
                    fn->rest_param_type = NULL;
                }
            } else {
                fn->rest_param = NULL;
                fn->rest_param_type = NULL;
            }

            // Copy return type (may be NULL)
            if (expr->as.function.return_type) {
                fn->return_type = type_new(expr->as.function.return_type->kind);
                // Copy nullable flag
                fn->return_type->nullable = expr->as.function.return_type->nullable;
                // Copy type_name for custom types (enums and objects)
                if (expr->as.function.return_type->type_name) {
                    fn->return_type->type_name = strdup(expr->as.function.return_type->type_name);
                }
                // Copy element_type for arrays
                if (expr->as.function.return_type->element_type) {
                    fn->return_type->element_type = type_new(expr->as.function.return_type->element_type->kind);
                    fn->return_type->element_type->nullable = expr->as.function.return_type->element_type->nullable;
                    if (expr->as.function.return_type->element_type->type_name) {
                        fn->return_type->element_type->type_name = strdup(expr->as.function.return_type->element_type->type_name);
                    }
                }
            } else {
                fn->return_type = NULL;
            }

            // Store body AST (shared, not copied)
            fn->body = expr->as.function.body;

            // CRITICAL: Capture current environment and retain it
            fn->closure_env = env;
            env_retain(env);  // Increment ref count since closure captures env

            // Initialize reference count to 1 (creator owns the first reference)
            // This ensures that when stored in the environment and later retained by tasks,
            // the function isn't prematurely freed when the environment is cleaned up
            fn->ref_count = 1;
            fn->is_bound = 0;  // Not a bound method - owns param arrays

            return val_function(fn);
        }

        case EXPR_ARRAY_LITERAL: {
            // Create array and evaluate elements
            Array *arr = array_new();

            for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                Value element = eval_expr(expr->as.array_literal.elements[i], env, ctx);
                array_push(arr, element);
                VALUE_RELEASE(element);  // array_push retained it
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
                // eval_expr returns with refcount 1, object now owns this reference
                obj->num_fields++;
            }

            return val_object(obj);
        }

        case EXPR_SET_PROPERTY: {
            Value object = eval_expr(expr->as.set_property.object, env, ctx);
            const char *property = expr->as.set_property.property;
            Value value = eval_expr(expr->as.set_property.value, env, ctx);

            if (object.type != VAL_OBJECT) {
                VALUE_RELEASE(object);
                VALUE_RELEASE(value);
                runtime_error(ctx, "Only objects can have properties set");
                return val_null();  // Return after error
            }

            Object *obj = object.as.as_object;

            // Look for existing field using hash table
            int idx = object_lookup_field(obj, property);
            if (idx >= 0) {
                // Release old value, store new value (object now owns it)
                VALUE_RELEASE(obj->field_values[idx]);
                obj->field_values[idx] = value;
                // eval_expr gave us ownership, object now owns the value
                // Return the value (retained for caller)
                VALUE_RETAIN(value);
                VALUE_RELEASE(object);
                return value;
            }

            // Field doesn't exist - add it dynamically!
            // Invalidate hash table (will be rebuilt on next lookup)
            if (obj->hash_table) {
                free(obj->hash_table);
                obj->hash_table = NULL;
                obj->hash_capacity = 0;
            }

            if (obj->num_fields >= obj->capacity) {
                // Grow arrays (handle capacity=0 case)
                int new_capacity = (obj->capacity == 0) ? 4 : obj->capacity * 2;
                char **new_names = realloc(obj->field_names, sizeof(char*) * new_capacity);
                if (!new_names) {
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(value);
                    runtime_error(ctx, "Failed to grow object capacity");
                    return val_null();
                }
                obj->field_names = new_names;

                Value *new_values = realloc(obj->field_values, sizeof(Value) * new_capacity);
                if (!new_values) {
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(value);
                    runtime_error(ctx, "Failed to grow object capacity");
                    return val_null();
                }
                obj->field_values = new_values;
                obj->capacity = new_capacity;
            }

            obj->field_names[obj->num_fields] = strdup(property);
            // Store value (object now owns it)
            obj->field_values[obj->num_fields] = value;
            obj->num_fields++;

            // Return the value (retained for caller)
            VALUE_RETAIN(value);
            VALUE_RELEASE(object);
            return value;
        }

        case EXPR_PREFIX_INC: {
            // ++x: increment then return new value
            Expr *operand = expr->as.prefix_inc.operand;

            if (operand->type == EXPR_IDENT) {
                // Simple variable: ++x
                Value old_val = env_get(env, operand->as.ident.name, ctx);  // Retains old value
                Value new_val = value_add_one(old_val, ctx);
                VALUE_RELEASE(old_val);  // Release old value after incrementing
                env_set(env, operand->as.ident.name, new_val, ctx);
                return new_val;
            } else if (operand->type == EXPR_INDEX) {
                // Array/buffer/string index: ++arr[i]
                Value object = eval_expr(operand->as.index.object, env, ctx);
                Value index_val = eval_expr(operand->as.index.index, env, ctx);

                if (!is_integer(index_val)) {
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    runtime_error(ctx, "Index must be an integer");
                }
                int32_t index = value_to_int(index_val);

                if (object.type == VAL_ARRAY) {
                    Value old_val = array_get(object.as.as_array, index, ctx);
                    Value new_val = value_add_one(old_val, ctx);
                    array_set(object.as.as_array, index, new_val, ctx);
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    return new_val;
                } else {
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    runtime_error(ctx, "Can only use ++ on array elements");
                }
            } else if (operand->type == EXPR_GET_PROPERTY) {
                // Object property: ++obj.field
                Value object = eval_expr(operand->as.get_property.object, env, ctx);
                const char *property = operand->as.get_property.property;
                if (object.type != VAL_OBJECT) {
                    VALUE_RELEASE(object);
                    runtime_error(ctx, "Can only increment object properties");
                }
                Object *obj = object.as.as_object;
                for (int i = 0; i < obj->num_fields; i++) {
                    if (strcmp(obj->field_names[i], property) == 0) {
                        Value old_val = obj->field_values[i];
                        Value new_val = value_add_one(old_val, ctx);
                        obj->field_values[i] = new_val;
                        VALUE_RELEASE(object);
                        return new_val;
                    }
                }
                VALUE_RELEASE(object);
                runtime_error(ctx, "Property '%s' not found", property);
            } else {
                runtime_error(ctx, "Invalid operand for ++");
            }
            return val_null();  // Unreachable, but silences fallthrough warning
        }

        case EXPR_PREFIX_DEC: {
            // --x: decrement then return new value
            Expr *operand = expr->as.prefix_dec.operand;

            if (operand->type == EXPR_IDENT) {
                Value old_val = env_get(env, operand->as.ident.name, ctx);  // Retains old value
                Value new_val = value_sub_one(old_val, ctx);
                VALUE_RELEASE(old_val);  // Release old value after decrementing
                env_set(env, operand->as.ident.name, new_val, ctx);
                return new_val;
            } else if (operand->type == EXPR_INDEX) {
                Value object = eval_expr(operand->as.index.object, env, ctx);
                Value index_val = eval_expr(operand->as.index.index, env, ctx);

                if (!is_integer(index_val)) {
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    runtime_error(ctx, "Index must be an integer");
                }
                int32_t index = value_to_int(index_val);

                if (object.type == VAL_ARRAY) {
                    Value old_val = array_get(object.as.as_array, index, ctx);
                    Value new_val = value_sub_one(old_val, ctx);
                    array_set(object.as.as_array, index, new_val, ctx);
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    return new_val;
                } else {
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    runtime_error(ctx, "Can only use -- on array elements");
                }
            } else if (operand->type == EXPR_GET_PROPERTY) {
                Value object = eval_expr(operand->as.get_property.object, env, ctx);
                const char *property = operand->as.get_property.property;
                if (object.type != VAL_OBJECT) {
                    VALUE_RELEASE(object);
                    runtime_error(ctx, "Can only decrement object properties");
                }
                Object *obj = object.as.as_object;
                for (int i = 0; i < obj->num_fields; i++) {
                    if (strcmp(obj->field_names[i], property) == 0) {
                        Value old_val = obj->field_values[i];
                        Value new_val = value_sub_one(old_val, ctx);
                        obj->field_values[i] = new_val;
                        VALUE_RELEASE(object);
                        return new_val;
                    }
                }
                VALUE_RELEASE(object);
                runtime_error(ctx, "Property '%s' not found", property);
            } else {
                runtime_error(ctx, "Invalid operand for --");
            }
            return val_null();  // Unreachable, but silences fallthrough warning
        }

        case EXPR_POSTFIX_INC: {
            // x++: return old value then increment
            Expr *operand = expr->as.postfix_inc.operand;

            if (operand->type == EXPR_IDENT) {
                Value old_val = env_get(env, operand->as.ident.name, ctx);  // Retains old value
                Value new_val = value_add_one(old_val, ctx);
                env_set(env, operand->as.ident.name, new_val, ctx);
                // Return old value (still retained from env_get, caller now owns it)
                return old_val;
            } else if (operand->type == EXPR_INDEX) {
                Value object = eval_expr(operand->as.index.object, env, ctx);
                Value index_val = eval_expr(operand->as.index.index, env, ctx);

                if (!is_integer(index_val)) {
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    runtime_error(ctx, "Index must be an integer");
                }
                int32_t index = value_to_int(index_val);

                if (object.type == VAL_ARRAY) {
                    Value old_val = array_get(object.as.as_array, index, ctx);
                    Value new_val = value_add_one(old_val, ctx);
                    array_set(object.as.as_array, index, new_val, ctx);
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    return old_val;
                } else {
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    runtime_error(ctx, "Can only use ++ on array elements");
                }
            } else if (operand->type == EXPR_GET_PROPERTY) {
                Value object = eval_expr(operand->as.get_property.object, env, ctx);
                const char *property = operand->as.get_property.property;
                if (object.type != VAL_OBJECT) {
                    VALUE_RELEASE(object);
                    runtime_error(ctx, "Can only increment object properties");
                }
                Object *obj = object.as.as_object;
                for (int i = 0; i < obj->num_fields; i++) {
                    if (strcmp(obj->field_names[i], property) == 0) {
                        Value old_val = obj->field_values[i];
                        Value new_val = value_add_one(old_val, ctx);
                        obj->field_values[i] = new_val;
                        VALUE_RETAIN(old_val);  // Retain for caller
                        VALUE_RELEASE(object);
                        return old_val;
                    }
                }
                VALUE_RELEASE(object);
                runtime_error(ctx, "Property '%s' not found", property);
            } else {
                runtime_error(ctx, "Invalid operand for ++");
            }
            return val_null();  // Unreachable, but silences fallthrough warning
        }

        case EXPR_POSTFIX_DEC: {
            // x--: return old value then decrement
            Expr *operand = expr->as.postfix_dec.operand;

            if (operand->type == EXPR_IDENT) {
                Value old_val = env_get(env, operand->as.ident.name, ctx);  // Retains old value
                Value new_val = value_sub_one(old_val, ctx);
                env_set(env, operand->as.ident.name, new_val, ctx);
                // Return old value (still retained from env_get, caller now owns it)
                return old_val;
            } else if (operand->type == EXPR_INDEX) {
                Value object = eval_expr(operand->as.index.object, env, ctx);
                Value index_val = eval_expr(operand->as.index.index, env, ctx);

                if (!is_integer(index_val)) {
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    runtime_error(ctx, "Index must be an integer");
                }
                int32_t index = value_to_int(index_val);

                if (object.type == VAL_ARRAY) {
                    Value old_val = array_get(object.as.as_array, index, ctx);
                    Value new_val = value_sub_one(old_val, ctx);
                    array_set(object.as.as_array, index, new_val, ctx);
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    return old_val;
                } else {
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    runtime_error(ctx, "Can only use -- on array elements");
                }
            } else if (operand->type == EXPR_GET_PROPERTY) {
                Value object = eval_expr(operand->as.get_property.object, env, ctx);
                const char *property = operand->as.get_property.property;
                if (object.type != VAL_OBJECT) {
                    VALUE_RELEASE(object);
                    runtime_error(ctx, "Can only decrement object properties");
                }
                Object *obj = object.as.as_object;
                for (int i = 0; i < obj->num_fields; i++) {
                    if (strcmp(obj->field_names[i], property) == 0) {
                        Value old_val = obj->field_values[i];
                        Value new_val = value_sub_one(old_val, ctx);
                        obj->field_values[i] = new_val;
                        VALUE_RETAIN(old_val);  // Retain for caller
                        VALUE_RELEASE(object);
                        return old_val;
                    }
                }
                VALUE_RELEASE(object);
                runtime_error(ctx, "Property '%s' not found", property);
            } else {
                runtime_error(ctx, "Invalid operand for --");
            }
            return val_null();  // Unreachable, but silences fallthrough warning
        }

        case EXPR_STRING_INTERPOLATION: {
            // Evaluate string interpolation: "prefix ${expr1} middle ${expr2} suffix"
            // Build the final string by concatenating string parts and evaluated expressions

            int num_parts = expr->as.string_interpolation.num_parts;
            char **string_parts = expr->as.string_interpolation.string_parts;
            Expr **expr_parts = expr->as.string_interpolation.expr_parts;

            // Calculate total length needed
            int total_len = 0;
            for (int i = 0; i <= num_parts; i++) {
                total_len += strlen(string_parts[i]);
            }

            // Evaluate expression parts and convert to strings
            char **expr_strings = malloc(sizeof(char*) * num_parts);
            for (int i = 0; i < num_parts; i++) {
                Value expr_val = eval_expr(expr_parts[i], env, ctx);
                expr_strings[i] = value_to_string(expr_val);
                VALUE_RELEASE(expr_val);  // Release after converting to string
                total_len += strlen(expr_strings[i]);
            }

            // Build final string
            char *result = malloc(total_len + 1);
            result[0] = '\0';

            for (int i = 0; i < num_parts; i++) {
                strcat(result, string_parts[i]);
                strcat(result, expr_strings[i]);
                free(expr_strings[i]);
            }
            strcat(result, string_parts[num_parts]);  // Final string part

            free(expr_strings);

            Value val = val_string(result);
            free(result);
            return val;
        }

        case EXPR_AWAIT: {
            // Evaluate the expression
            Value awaited = eval_expr(expr->as.await_expr.awaited_expr, env, ctx);

            // If it's a task handle, automatically join it
            if (awaited.type == VAL_TASK) {
                Value args[1] = { awaited };
                Value result = builtin_join(args, 1, ctx);
                VALUE_RELEASE(awaited);  // Release task handle after joining
                return result;
            }

            // For other values (including direct async function calls),
            // just return the value as-is (already evaluated)
            return awaited;
        }

        case EXPR_OPTIONAL_CHAIN: {
            // Evaluate the object expression
            Value object_val = eval_expr(expr->as.optional_chain.object, env, ctx);

            // If object is null, short-circuit and return null
            if (object_val.type == VAL_NULL) {
                return val_null();
            }

            // Otherwise, perform the operation based on the type
            if (expr->as.optional_chain.is_property) {
                // Optional property access: obj?.property
                const char *property = expr->as.optional_chain.property;
                Value result = {0};

                // Handle property access for different types (similar to EXPR_GET_PROPERTY)
                if (object_val.type == VAL_STRING) {
                    String *str = object_val.as.as_string;

                    if (strcmp(property, "length") == 0) {
                        if (str->char_length < 0) {
                            str->char_length = utf8_count_codepoints(str->data, str->length);
                        }
                        result = val_i32(str->char_length);
                    } else if (strcmp(property, "byte_length") == 0) {
                        result = val_i32(str->length);
                    } else {
                        runtime_error(ctx, "Unknown property '%s' for string", property);
                    }
                } else if (object_val.type == VAL_ARRAY) {
                    if (strcmp(property, "length") == 0) {
                        result = val_i32(object_val.as.as_array->length);
                    } else {
                        runtime_error(ctx, "Unknown property '%s' for array", property);
                    }
                } else if (object_val.type == VAL_BUFFER) {
                    if (strcmp(property, "length") == 0) {
                        result = val_i32(object_val.as.as_buffer->length);
                    } else if (strcmp(property, "capacity") == 0) {
                        result = val_i32(object_val.as.as_buffer->capacity);
                    } else {
                        runtime_error(ctx, "Unknown property '%s' for buffer", property);
                    }
                } else if (object_val.type == VAL_FILE) {
                    FileHandle *f = object_val.as.as_file;
                    if (strcmp(property, "path") == 0) {
                        result = val_string(f->path);
                    } else if (strcmp(property, "mode") == 0) {
                        result = val_string(f->mode);
                    } else if (strcmp(property, "closed") == 0) {
                        result = val_bool(f->closed);
                    } else {
                        runtime_error(ctx, "Unknown property '%s' for file", property);
                    }
                } else if (object_val.type == VAL_OBJECT) {
                    Object *obj = object_val.as.as_object;
                    for (int i = 0; i < obj->num_fields; i++) {
                        if (strcmp(obj->field_names[i], property) == 0) {
                            result = obj->field_values[i];
                            VALUE_RETAIN(result);
                            VALUE_RELEASE(object_val);
                            return result;
                        }
                    }
                    // For optional chaining, return null for missing properties
                    VALUE_RELEASE(object_val);
                    return val_null();
                } else {
                    runtime_error(ctx, "Cannot access property on non-object value");
                }

                VALUE_RELEASE(object_val);
                return result;
            } else if (expr->as.optional_chain.is_call) {
                // obj?.(args) - call obj directly if not null
                int num_args = expr->as.optional_chain.num_args;
                Value *args = NULL;
                if (num_args > 0) {
                    args = malloc(sizeof(Value) * num_args);
                    for (int i = 0; i < num_args; i++) {
                        args[i] = eval_expr(expr->as.optional_chain.args[i], env, ctx);
                    }
                }

                Value result = {0};
                if (object_val.type == VAL_FUNCTION) {
                    Function *fn = object_val.as.as_function;

                    // Create call environment with closure_env as parent
                    Environment *call_env = env_new(fn->closure_env);

                    // Bind parameters
                    for (int i = 0; i < fn->num_params; i++) {
                        Value arg_value = (i < num_args) ? args[i] : val_null();

                        // Type check if parameter has type annotation
                        if (fn->param_types[i]) {
                            arg_value = convert_to_type(arg_value, fn->param_types[i], call_env, ctx);
                        }

                        env_set(call_env, fn->param_names[i], arg_value, ctx);
                    }

                    // Execute body
                    ctx->return_state.is_returning = 0;
                    eval_stmt(fn->body, call_env, ctx);

                    // Get return value
                    result = ctx->return_state.is_returning ? ctx->return_state.return_value : val_null();
                    ctx->return_state.is_returning = 0;

                    // Clean up
                    env_release(call_env);
                } else if (object_val.type == VAL_BUILTIN_FN) {
                    BuiltinFn fn = object_val.as.as_builtin_fn;
                    result = fn(args, num_args, ctx);
                } else {
                    runtime_error(ctx, "Cannot call non-function value");
                    result = val_null();
                }

                // Cleanup args
                if (args) {
                    for (int i = 0; i < num_args; i++) {
                        VALUE_RELEASE(args[i]);
                    }
                    free(args);
                }
                VALUE_RELEASE(object_val);
                return result;
            } else {
                // Optional indexing: obj?.[index]
                Value index_val = eval_expr(expr->as.optional_chain.index, env, ctx);

                if (!is_integer(index_val)) {
                    runtime_error(ctx, "Index must be an integer");
                }

                int32_t index = value_to_int(index_val);
                Value result = {0};

                if (object_val.type == VAL_ARRAY) {
                    result = array_get(object_val.as.as_array, index, ctx);
                    VALUE_RETAIN(result);
                } else if (object_val.type == VAL_STRING) {
                    String *str = object_val.as.as_string;

                    // Compute character length if not cached
                    if (str->char_length < 0) {
                        str->char_length = utf8_count_codepoints(str->data, str->length);
                    }

                    // Check bounds using character count (not byte count)
                    if (index < 0 || index >= str->char_length) {
                        runtime_error(ctx, "String index out of bounds");
                    }

                    // Find byte offset of the i-th codepoint
                    int byte_pos = utf8_byte_offset(str->data, str->length, index);

                    // Decode the codepoint at that position
                    uint32_t codepoint = utf8_decode_at(str->data, byte_pos);

                    result = val_rune(codepoint);
                } else if (object_val.type == VAL_BUFFER) {
                    Buffer *buf = object_val.as.as_buffer;

                    if (index < 0 || index >= buf->length) {
                        runtime_error(ctx, "Buffer index out of bounds");
                    }

                    result = val_u8(((unsigned char *)buf->data)[index]);
                } else {
                    runtime_error(ctx, "Cannot index non-array/non-string/non-buffer value");
                }

                VALUE_RELEASE(object_val);
                VALUE_RELEASE(index_val);
                return result;
            }
            break;  // Prevent fall-through
        }

        case EXPR_NULL_COALESCE: {
            // Evaluate the left operand
            Value left_val = eval_expr(expr->as.null_coalesce.left, env, ctx);

            // If left is not null, return it
            if (left_val.type != VAL_NULL) {
                return left_val;
            }

            // Left is null - release it (no-op for null, but consistent)
            VALUE_RELEASE(left_val);
            // Evaluate and return the right operand
            return eval_expr(expr->as.null_coalesce.right, env, ctx);
        }
    }

    return val_null();
}

