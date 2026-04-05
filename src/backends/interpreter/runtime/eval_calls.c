// Function/method call evaluation (extracted from expressions.c)

#include "expressions_internal.h"

// Forward declaration for recursive evaluation
Value eval_expr(Expr *expr, Environment *env, ExecutionContext *ctx);

// Evaluate function and method call expressions
Value eval_call_expr(Expr *expr, Environment *env, ExecutionContext *ctx) {
            // Check if this is a method call (obj.method(...))
            int is_method_call = 0;
            Value method_self = {0};

            if (expr->as.call.func->type == EXPR_GET_PROPERTY) {
                is_method_call = 1;
                method_self = eval_expr(expr->as.call.func->as.get_property.object, env, ctx);

                // METHOD DISPATCH INLINE CACHE:
                // Cache the receiver type to skip the if-chain on subsequent calls
                MethodIC *mic = &expr->as.call.ic;

                // Fast path: if monomorphic, check cached type first
                if (mic->ic_state == HML_IC_STATE_MONOMORPHIC &&
                    mic->cached_receiver_type == (int)method_self.type) {
                    // Cache hit - type matches, continue to type-specific handler below
                    // The switch below will handle it efficiently
                } else if (mic->ic_state != HML_IC_STATE_MEGAMORPHIC) {
                    // Update cache: first time or different type
                    if (mic->ic_state == HML_IC_STATE_UNINITIALIZED) {
                        mic->cached_receiver_type = (int)method_self.type;
                        mic->ic_state = HML_IC_STATE_MONOMORPHIC;
                        mic->miss_count = 0;
                    } else if (mic->cached_receiver_type != (int)method_self.type) {
                        // Different type - this call site is polymorphic
                        mic->miss_count++;
                        if (mic->miss_count >= HML_IC_MAX_MISSES) {
                            mic->ic_state = HML_IC_STATE_MEGAMORPHIC;
                        } else {
                            mic->cached_receiver_type = (int)method_self.type;
                        }
                    }
                }

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

                // Special handling for buffer methods (to_string, slice)
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

                    if (strcmp(method, "slice") == 0) {
                        int num_args = expr->as.call.num_args;
                        if (num_args < 1 || num_args > 2) {
                            runtime_error(ctx, "buffer.slice() expects 1-2 arguments (start[, end])");
                            VALUE_RELEASE(method_self);
                            return val_null();
                        }
                        Value start_val = eval_expr(expr->as.call.args[0], env, ctx);
                        Buffer *buf = method_self.as.as_buffer;
                        int32_t start = value_to_int(start_val);
                        int32_t end = (num_args == 2) ? value_to_int(eval_expr(expr->as.call.args[1], env, ctx)) : buf->length;
                        VALUE_RELEASE(start_val);

                        // Clamp bounds
                        if (start < 0) start = 0;
                        if (end > buf->length) end = buf->length;
                        if (start > end) start = end;

                        // Find root owner (follow parent chain)
                        Buffer *root = buf;
                        while (root->parent) {
                            root = root->parent;
                        }

                        // Create zero-copy view
                        Buffer *view = malloc(sizeof(Buffer));
                        view->data = (uint8_t *)buf->data + start;
                        view->length = end - start;
                        view->capacity = end - start;
                        view->ref_count = 1;
                        atomic_store(&view->freed, 0);
                        view->parent = root;
                        buffer_retain(root);  // Keep root alive

                        Value result = {0};
                        result.type = VAL_BUFFER;
                        result.as.as_buffer = view;
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
                    PropertyIC *ic = &expr->as.call.func->as.get_property.ic;

                    // Only handle built-in object methods here (serialize, keys, has, delete)
                    // BUT first check if the object has a user-defined method with this name
                    if (strcmp(method, "serialize") == 0 || strcmp(method, "keys") == 0 ||
                        strcmp(method, "has") == 0 || strcmp(method, "delete") == 0) {
                        // Check if object has a user-defined function with this name
                        Object *obj = method_self.as.as_object;
                        int has_user_method = 0;
                        int method_idx = -1;

                        // INLINE CACHE FAST PATH for method lookup
                        if (ic->ic_state == HML_IC_STATE_MONOMORPHIC &&
                            ic->cached_object == (void*)obj &&
                            ic->cached_field_index >= 0) {
                            if (object_validate_ic(obj, ic->cached_field_index, method)) {
                                method_idx = ic->cached_field_index;
                            } else {
                                ic->ic_state = HML_IC_STATE_UNINITIALIZED;
                                ic->cached_object = NULL;
                                ic->cached_field_index = -1;
                            }
                        }

                        // CACHE MISS: Do full lookup
                        if (method_idx < 0) {
                            if (ic->cached_hash == 0) {
                                ic->cached_hash = hash_string(method);
                            }
                            method_idx = object_lookup_field_with_hash(obj, method, ic->cached_hash);
                            if (method_idx >= 0 && ic->ic_state != HML_IC_STATE_MEGAMORPHIC) {
                                if (ic->ic_state == HML_IC_STATE_UNINITIALIZED) {
                                    ic->cached_object = (void*)obj;
                                    ic->cached_field_index = method_idx;
                                    ic->ic_state = HML_IC_STATE_MONOMORPHIC;
                                    ic->miss_count = 0;
                                } else if (ic->cached_object != (void*)obj) {
                                    ic->miss_count++;
                                    if (ic->miss_count >= HML_IC_MAX_MISSES) {
                                        ic->ic_state = HML_IC_STATE_MEGAMORPHIC;
                                    } else {
                                        ic->cached_object = (void*)obj;
                                        ic->cached_field_index = method_idx;
                                    }
                                }
                            }
                        }

                        if (method_idx >= 0 && obj->fields[method_idx].value.type == VAL_FUNCTION) {
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
                // Get the method from the object directly using inline cache
                const char *method_name = expr->as.call.func->as.get_property.property;
                Object *obj = method_self.as.as_object;
                PropertyIC *ic = &expr->as.call.func->as.get_property.ic;
                int method_idx = -1;

                // INLINE CACHE FAST PATH for method lookup
                if (ic->ic_state == HML_IC_STATE_MONOMORPHIC &&
                    ic->cached_object == (void*)obj &&
                    ic->cached_field_index >= 0) {
                    if (object_validate_ic(obj, ic->cached_field_index, method_name)) {
                        method_idx = ic->cached_field_index;
                    } else {
                        ic->ic_state = HML_IC_STATE_UNINITIALIZED;
                        ic->cached_object = NULL;
                        ic->cached_field_index = -1;
                    }
                }

                // CACHE MISS: Do full lookup
                if (method_idx < 0) {
                    if (ic->cached_hash == 0) {
                        ic->cached_hash = hash_string(method_name);
                    }
                    method_idx = object_lookup_field_with_hash(obj, method_name, ic->cached_hash);
                    if (method_idx >= 0 && ic->ic_state != HML_IC_STATE_MEGAMORPHIC) {
                        if (ic->ic_state == HML_IC_STATE_UNINITIALIZED) {
                            ic->cached_object = (void*)obj;
                            ic->cached_field_index = method_idx;
                            ic->ic_state = HML_IC_STATE_MONOMORPHIC;
                            ic->miss_count = 0;
                        } else if (ic->cached_object != (void*)obj) {
                            ic->miss_count++;
                            if (ic->miss_count >= HML_IC_MAX_MISSES) {
                                ic->ic_state = HML_IC_STATE_MEGAMORPHIC;
                            } else {
                                ic->cached_object = (void*)obj;
                                ic->cached_field_index = method_idx;
                            }
                        }
                    }
                }

                if (method_idx >= 0) {
                    func = obj->fields[method_idx].value;
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
            // Uses HML_MAX_STACK_ARGS from hemlock_limits.h
            Value stack_args[HML_MAX_STACK_ARGS];
            Value *args = NULL;
            int args_on_heap = 0;
            if (expr->as.call.num_args > 0) {
                if (expr->as.call.num_args <= HML_MAX_STACK_ARGS) {
                    args = stack_args;
                } else {
                    args = malloc(sizeof(Value) * expr->as.call.num_args);
                    args_on_heap = 1;
                }
                for (int i = 0; i < expr->as.call.num_args; i++) {
                    args[i] = eval_expr(expr->as.call.args[i], env, ctx);
                    // Exception safety: if arg evaluation threw, release previous args
                    if (ctx->exception_state.is_throwing) {
                        // Release all args evaluated so far (including this one)
                        for (int j = 0; j <= i; j++) {
                            VALUE_RELEASE(args[j]);
                        }
                        if (args_on_heap) free(args);
                        VALUE_RELEASE(func);
                        return val_null();
                    }
                }
            }

            Value result = {0};
            int should_release_args = 1;  // Track whether we need to release args

            if (func.type == VAL_BUILTIN_FN) {
                // Call builtin function
                BuiltinFn fn = func.as.as_builtin_fn;
                // Set source location for profiler allocation tracking
                ctx->current_source_file = get_current_source_file();
                ctx->current_line = expr->line;
                result = fn(args, expr->as.call.num_args, ctx);
                // Builtin functions don't retain args, so we must release them
                should_release_args = 1;
            } else if (func.type == VAL_FUNCTION) {
                // Call user-defined function
                Function *fn = func.as.as_function;

                // Handle named arguments: reorder args array to match parameter order
                // Also track which argument indices are used for named vs positional
                Value reordered_stack[HML_MAX_STACK_ARGS];
                Value *reordered_args = NULL;
                int reordered_on_heap = 0;
                int *arg_used = NULL;  // Track which args have been used
                Value *original_args = args;  // Keep reference to original for cleanup
                int original_args_on_heap = args_on_heap;

                if (expr->as.call.arg_names != NULL) {
                    // Allocate reordered array
                    if (fn->num_params <= HML_MAX_STACK_ARGS) {
                        reordered_args = reordered_stack;
                    } else {
                        reordered_args = malloc(sizeof(Value) * fn->num_params);
                        reordered_on_heap = 1;
                    }

                    // Initialize reordered_args to null values
                    for (int i = 0; i < fn->num_params; i++) {
                        reordered_args[i] = val_null();
                    }

                    // Track which arguments have been used
                    arg_used = malloc(sizeof(int) * expr->as.call.num_args);
                    for (int i = 0; i < expr->as.call.num_args; i++) {
                        arg_used[i] = 0;
                    }

                    // First pass: place named arguments in their correct positions
                    for (int i = 0; i < expr->as.call.num_args; i++) {
                        if (expr->as.call.arg_names[i] != NULL) {
                            // Find the parameter with this name
                            uint32_t arg_hash = hash_string(expr->as.call.arg_names[i]);
                            int found = 0;
                            for (int j = 0; j < fn->num_params; j++) {
                                if (fn->param_hashes[j] == arg_hash &&
                                    strcmp(fn->param_names[j], expr->as.call.arg_names[i]) == 0) {
                                    // Check if this parameter was already filled
                                    if (reordered_args[j].type != VAL_NULL) {
                                        runtime_error(ctx, "Duplicate argument for parameter '%s'",
                                                    fn->param_names[j]);
                                        // Cleanup and return
                                        VALUE_RELEASE(func);
                                        for (int k = 0; k < expr->as.call.num_args; k++) {
                                            VALUE_RELEASE(args[k]);
                                        }
                                        if (args_on_heap) free(args);
                                        free(arg_used);
                                        if (reordered_on_heap) free(reordered_args);
                                        return val_null();
                                    }
                                    reordered_args[j] = args[i];
                                    arg_used[i] = 1;
                                    found = 1;
                                    break;
                                }
                            }
                            if (!found) {
                                runtime_error(ctx, "Unknown parameter name '%s'",
                                            expr->as.call.arg_names[i]);
                                // Cleanup and return
                                VALUE_RELEASE(func);
                                for (int k = 0; k < expr->as.call.num_args; k++) {
                                    VALUE_RELEASE(args[k]);
                                }
                                if (args_on_heap) free(args);
                                free(arg_used);
                                if (reordered_on_heap) free(reordered_args);
                                return val_null();
                            }
                        }
                    }

                    // Second pass: place positional arguments in remaining slots
                    int next_positional_slot = 0;
                    for (int i = 0; i < expr->as.call.num_args; i++) {
                        if (!arg_used[i]) {
                            // Find the next unfilled slot
                            while (next_positional_slot < fn->num_params &&
                                   reordered_args[next_positional_slot].type != VAL_NULL) {
                                next_positional_slot++;
                            }
                            if (next_positional_slot < fn->num_params) {
                                reordered_args[next_positional_slot] = args[i];
                                arg_used[i] = 1;
                                next_positional_slot++;
                            }
                            // Extra positional args will be handled by rest param logic
                        }
                    }

                    free(arg_used);
                    // Now reordered_args contains arguments in parameter order
                    // The original args values have been moved to reordered_args
                    // Free the original array memory if it was on heap (values were moved, not copied)
                    if (original_args_on_heap) {
                        free(original_args);
                    }
                    // Switch to use reordered_args instead of args for parameter binding
                    args = reordered_args;
                    args_on_heap = reordered_on_heap;
                }

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
                // For named arguments, count non-null slots in reordered array
                int provided_args = expr->as.call.num_args;
                if (expr->as.call.arg_names != NULL) {
                    // Count how many required params are covered
                    int covered_required = 0;
                    for (int i = 0; i < fn->num_params; i++) {
                        if (args[i].type != VAL_NULL) {
                            if (!fn->param_defaults || !fn->param_defaults[i]) {
                                covered_required++;
                            }
                        }
                    }
                    // With named args, check that all required params are covered
                    if (covered_required < required_params) {
                        runtime_error(ctx, "Missing required parameter(s)");
                        VALUE_RELEASE(func);
                        // Release original evaluated args
                        for (int i = 0; i < fn->num_params; i++) {
                            if (args[i].type != VAL_NULL) {
                                VALUE_RELEASE(args[i]);
                            }
                        }
                        if (reordered_on_heap) free(reordered_args);
                        return val_null();
                    }
                } else {
                    // Original arity check for positional-only calls
                    int max_args = fn->rest_param ? INT_MAX : fn->num_params;
                    if (provided_args < required_params || provided_args > max_args) {
                        if (fn->rest_param) {
                            runtime_error(ctx, "Function expects at least %d arguments, got %d",
                                    required_params, provided_args);
                        } else if (required_params == fn->num_params) {
                            runtime_error(ctx, "Function expects %d arguments, got %d",
                                    fn->num_params, provided_args);
                        } else {
                            runtime_error(ctx, "Function expects %d-%d arguments, got %d",
                                    required_params, fn->num_params, provided_args);
                        }
                        // Release function and args before returning
                        VALUE_RELEASE(func);
                        if (args) {
                            for (int i = 0; i < provided_args; i++) {
                                VALUE_RELEASE(args[i]);
                            }
                            if (args_on_heap) free(args);
                        }
                        return val_null();
                    }
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

                // Profile: enter function
                PROFILER_ENTER(ctx, fn_name, get_current_source_file(), expr->line);

                // Create call environment with closure_env as parent
                Environment *call_env = env_new(fn->closure_env);

                // Bind parameters FIRST using fast path with pre-computed hashes
                // This must happen before 'self' injection to preserve slot order
                // for resolved variable lookups (params at slots 0, 1, 2, ...)
                int has_named_args = (expr->as.call.arg_names != NULL);
                for (int i = 0; i < fn->num_params; i++) {
                    Value arg_value = {0};

                    // Check if this is a ref parameter
                    int is_ref_param = fn->param_is_ref && fn->param_is_ref[i];

                    // Determine if this parameter has an argument provided
                    // For named args, check if slot is non-null; for positional, check index
                    int has_arg = has_named_args ? (args[i].type != VAL_NULL) : (i < expr->as.call.num_args);

                    if (is_ref_param && has_arg) {
                        // For ref parameters, create a reference to the original location
                        // With named args, we need to find the original arg_expr
                        Expr *arg_expr = NULL;
                        if (has_named_args) {
                            // Find which original argument corresponds to this parameter
                            for (int j = 0; j < expr->as.call.num_args; j++) {
                                if (expr->as.call.arg_names[j] &&
                                    strcmp(expr->as.call.arg_names[j], fn->param_names[i]) == 0) {
                                    arg_expr = expr->as.call.args[j];
                                    break;
                                }
                            }
                            // If not found by name, it was positional - find the first unused positional
                            if (!arg_expr) {
                                int positional_idx = 0;
                                for (int j = 0; j < expr->as.call.num_args; j++) {
                                    if (!expr->as.call.arg_names[j]) {
                                        if (positional_idx == i) {
                                            arg_expr = expr->as.call.args[j];
                                            break;
                                        }
                                        positional_idx++;
                                    }
                                }
                            }
                        } else {
                            arg_expr = expr->as.call.args[i];
                        }

                        Reference *ref = NULL;

                        if (arg_expr && arg_expr->type == EXPR_IDENT) {
                            // Reference to a variable
                            ref = reference_new_variable(env, arg_expr->as.ident.name);
                        } else if (arg_expr && arg_expr->type == EXPR_INDEX) {
                            // Reference to an array element
                            Value arr_val = eval_expr(arg_expr->as.index.object, env, ctx);
                            Value idx_val = eval_expr(arg_expr->as.index.index, env, ctx);
                            if (arr_val.type == VAL_ARRAY) {
                                int64_t index = 0;
                                switch (idx_val.type) {
                                    case VAL_I8: index = idx_val.as.as_i8; break;
                                    case VAL_I16: index = idx_val.as.as_i16; break;
                                    case VAL_I32: index = idx_val.as.as_i32; break;
                                    case VAL_I64: index = idx_val.as.as_i64; break;
                                    case VAL_U8: index = idx_val.as.as_u8; break;
                                    case VAL_U16: index = idx_val.as.as_u16; break;
                                    case VAL_U32: index = idx_val.as.as_u32; break;
                                    case VAL_U64: index = (int64_t)idx_val.as.as_u64; break;
                                    default:
                                        runtime_error_at(ctx, arg_expr->line, "Array index must be an integer");
                                        break;
                                }
                                ref = reference_new_array_index(arr_val.as.as_array, (int)index);
                            } else {
                                runtime_error_at(ctx, arg_expr->line, "ref argument must be an array element");
                            }
                            VALUE_RELEASE(arr_val);
                            VALUE_RELEASE(idx_val);
                        } else if (arg_expr && arg_expr->type == EXPR_GET_PROPERTY) {
                            // Reference to an object property
                            Value obj_val = eval_expr(arg_expr->as.get_property.object, env, ctx);
                            if (obj_val.type == VAL_OBJECT) {
                                ref = reference_new_object_property(obj_val.as.as_object, arg_expr->as.get_property.property);
                            } else {
                                runtime_error_at(ctx, arg_expr->line, "ref argument must be an object property");
                            }
                            VALUE_RELEASE(obj_val);
                        } else if (arg_expr) {
                            runtime_error_at(ctx, arg_expr->line, "ref argument must be a variable, array element, or object property");
                        }

                        if (ref) {
                            arg_value = val_ref(ref);
                            // Release the eagerly evaluated value since we're using a ref
                            VALUE_RELEASE(args[i]);
                        } else {
                            // Error already reported, use null
                            arg_value = val_null();
                            VALUE_RELEASE(args[i]);
                        }
                    } else if (has_arg) {
                        // Regular parameter - use provided argument
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

                    // Type check if parameter has type annotation (skip for refs)
                    if (!is_ref_param && fn->param_types[i]) {
                        arg_value = convert_to_type(arg_value, fn->param_types[i], call_env, ctx);
                    }

                    // Use fast param binding with pre-computed hash (skips redundant checks)
                    env_define_param(call_env, fn->param_names[i], fn->param_hashes[i], arg_value);

                    // Release default param value if we created it (not from args array)
                    // For default params or ref params, arg_value was created locally and needs release
                    if (!has_arg || is_ref_param) {
                        VALUE_RELEASE(arg_value);
                    }
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
                    Value rest_val = val_array(rest_arr);
                    env_define(call_env, fn->rest_param, rest_val, 0, ctx);
                    VALUE_RELEASE(rest_val);  // Release caller's reference (env_define retained it)
                }

                // Inject 'self' AFTER parameters to preserve slot order for resolved lookups
                if (is_method_call) {
                    env_set(call_env, "self", method_self, ctx);
                    VALUE_RELEASE(method_self);  // Release original reference (env_set retained it)
                }

                // Save defer stack depth before executing function body
                int defer_depth_before = ctx->defer_stack.count;

                // Execute body - reset return state first
                ctx->return_state.is_returning = 0;
                ctx->return_state.return_value = val_null();  // Reset to prevent stale values
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

                // Get result:
                // - Use null if exception is being thrown
                // - Use null if function didn't explicitly return (is_returning == 0)
                //   This prevents stale return_value from nested calls being used
                // - Otherwise use the actual return_value
                if (ctx->exception_state.is_throwing || !ctx->return_state.is_returning) {
                    result = val_null();
                } else {
                    result = ctx->return_state.return_value;
                }

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

                // Note: result already has correct ref_count from eval_expr:
                // - For returned variables: env_get retained it, env_release will release local copy
                // - For new values: created with ref_count=1, env_release doesn't affect it
                // No additional retain needed here.

                // Profile: exit function (always, even on exception, for accurate timing)
                PROFILER_EXIT(ctx);

                // Pop call from stack trace (but not if exception is active - preserve stack for error reporting)
                if (!ctx->exception_state.is_throwing) {
                    call_stack_pop(&ctx->call_stack);
                }

                // Release call environment (reference counted - will be freed when no longer used)
                env_release(call_env);
                // User-defined functions retained args via env_define_param, but we still need to
                // release our reference from the args array. The env has its own retained copy.
                // Note: Must release BEFORE exiting this block because 'args' might point to
                // 'reordered_stack' which is a local stack variable that will go out of scope.
                if (args) {
                    for (int i = 0; i < expr->as.call.num_args; i++) {
                        VALUE_RELEASE(args[i]);
                    }
                }
                // Free args array if heap-allocated (handles both original and reordered cases)
                if (args_on_heap) {
                    free(args);
                    args = NULL;
                    args_on_heap = 0;
                }
                // Don't release again in the common cleanup below
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
