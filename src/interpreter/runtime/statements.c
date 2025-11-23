#include "internal.h"

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
                value_release(condition);  // Release condition before branching
                eval_stmt(stmt->as.if_stmt.then_branch, env, ctx);
            } else if (stmt->as.if_stmt.else_branch != NULL) {
                value_release(condition);  // Release condition before branching
                eval_stmt(stmt->as.if_stmt.else_branch, env, ctx);
            } else {
                value_release(condition);  // Release condition if no else branch
            }
            // No need to check return here, it propagates automatically
            break;
        }

        case STMT_WHILE: {
            for (;;) {
                Value condition = eval_expr(stmt->as.while_stmt.condition, env, ctx);

                if (!value_is_truthy(condition)) {
                    value_release(condition);  // Release condition before breaking
                    break;
                }

                value_release(condition);  // Release condition after checking

                // Create new environment for this iteration
                Environment *iter_env = env_new(env);
                eval_stmt(stmt->as.while_stmt.body, iter_env, ctx);
                env_release(iter_env);

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
                // Check for exception/return after initializer
                if (ctx->return_state.is_returning || ctx->exception_state.is_throwing) {
                    env_release(loop_env);
                    break;
                }
            }

            // Loop
            for (;;) {
                // Check condition
                if (stmt->as.for_loop.condition) {
                    Value cond = eval_expr(stmt->as.for_loop.condition, loop_env, ctx);
                    // Check for exception after condition evaluation
                    if (ctx->exception_state.is_throwing) {
                        value_release(cond);  // Release condition before breaking
                        break;
                    }
                    if (!value_is_truthy(cond)) {
                        value_release(cond);  // Release condition before breaking
                        break;
                    }
                    value_release(cond);  // Release condition after checking
                }

                // Execute body (create new environment for this iteration)
                Environment *iter_env = env_new(loop_env);
                eval_stmt(stmt->as.for_loop.body, iter_env, ctx);
                env_release(iter_env);

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
                    Value incr_result = eval_expr(stmt->as.for_loop.increment, loop_env, ctx);
                    value_release(incr_result);  // Release increment expression result
                    // Check for exception after increment
                    if (ctx->exception_state.is_throwing) {
                        break;
                    }
                }
            }

            env_release(loop_env);
            break;
        }

        case STMT_FOR_IN: {
            Value iterable = eval_expr(stmt->as.for_in.iterable, env, ctx);

            // Check for exception after evaluating iterable
            if (ctx->exception_state.is_throwing) {
                value_release(iterable);  // Release iterable before breaking
                break;
            }

            // Validate iterable type before creating loop environment
            if (iterable.type != VAL_ARRAY && iterable.type != VAL_OBJECT && iterable.type != VAL_STRING) {
                value_release(iterable);  // Release iterable before breaking
                ctx->exception_state.exception_value = val_string("for-in requires array, object, or string");
                ctx->exception_state.is_throwing = 1;
                break;
            }

            Environment *loop_env = env_new(env);

            if (iterable.type == VAL_ARRAY) {
                Array *arr = iterable.as.as_array;

                for (int i = 0; i < arr->length; i++) {
                    // Create new environment for this iteration
                    Environment *iter_env = env_new(loop_env);

                    // Bind variables
                    if (stmt->as.for_in.key_var) {
                        env_set(iter_env, stmt->as.for_in.key_var, val_i32(i), ctx);
                        // Check for exception from env_set
                        if (ctx->exception_state.is_throwing) {
                            env_release(iter_env);
                            break;
                        }
                    }
                    env_set(iter_env, stmt->as.for_in.value_var, arr->elements[i], ctx);
                    // Check for exception from env_set
                    if (ctx->exception_state.is_throwing) {
                        env_release(iter_env);
                        break;
                    }

                    // Execute body
                    eval_stmt(stmt->as.for_in.body, iter_env, ctx);
                    env_release(iter_env);

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
                    // Create new environment for this iteration
                    Environment *iter_env = env_new(loop_env);

                    // Bind variables
                    if (stmt->as.for_in.key_var) {
                        env_set(iter_env, stmt->as.for_in.key_var, val_string(obj->field_names[i]), ctx);
                        // Check for exception from env_set
                        if (ctx->exception_state.is_throwing) {
                            env_release(iter_env);
                            break;
                        }
                    }
                    env_set(iter_env, stmt->as.for_in.value_var, obj->field_values[i], ctx);
                    // Check for exception from env_set
                    if (ctx->exception_state.is_throwing) {
                        env_release(iter_env);
                        break;
                    }

                    // Execute body
                    eval_stmt(stmt->as.for_in.body, iter_env, ctx);
                    env_release(iter_env);

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
            } else if (iterable.type == VAL_STRING) {
                String *str = iterable.as.as_string;

                // Compute character length if not cached
                if (str->char_length < 0) {
                    str->char_length = utf8_count_codepoints(str->data, str->length);
                }

                for (int i = 0; i < str->char_length; i++) {
                    // Create new environment for this iteration
                    Environment *iter_env = env_new(loop_env);

                    // Bind index if key_var is specified
                    if (stmt->as.for_in.key_var) {
                        env_set(iter_env, stmt->as.for_in.key_var, val_i32(i), ctx);
                        // Check for exception from env_set
                        if (ctx->exception_state.is_throwing) {
                            env_release(iter_env);
                            break;
                        }
                    }

                    // Get the rune at position i
                    int byte_pos = utf8_byte_offset(str->data, str->length, i);
                    uint32_t codepoint = utf8_decode_at(str->data, byte_pos);

                    env_set(iter_env, stmt->as.for_in.value_var, val_rune(codepoint), ctx);
                    // Check for exception from env_set
                    if (ctx->exception_state.is_throwing) {
                        env_release(iter_env);
                        break;
                    }

                    // Execute body
                    eval_stmt(stmt->as.for_in.body, iter_env, ctx);
                    env_release(iter_env);

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
            }

            env_release(loop_env);
            value_release(iterable);  // Release iterable after loop completes
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

        case STMT_ENUM: {
            // Create enum type definition
            EnumType *type = malloc(sizeof(EnumType));
            type->name = strdup(stmt->as.enum_decl.name);
            type->num_variants = stmt->as.enum_decl.num_variants;

            // Copy variant names
            type->variant_names = malloc(sizeof(char*) * type->num_variants);
            type->variant_values = malloc(sizeof(int32_t) * type->num_variants);

            // Evaluate variant values (auto-increment or explicit)
            int32_t auto_value = 0;
            for (int i = 0; i < type->num_variants; i++) {
                type->variant_names[i] = strdup(stmt->as.enum_decl.variant_names[i]);

                if (stmt->as.enum_decl.variant_values[i] != NULL) {
                    // Explicit value - evaluate the expression
                    Value val = eval_expr(stmt->as.enum_decl.variant_values[i], env, ctx);
                    if (val.type != VAL_I32) {
                        fprintf(stderr, "Runtime error: Enum variant value must be i32\n");
                        exit(1);
                    }
                    type->variant_values[i] = val.as.as_i32;
                    auto_value = val.as.as_i32 + 1;  // Next auto value
                } else {
                    // Auto value
                    type->variant_values[i] = auto_value;
                    auto_value++;
                }
            }

            // Register the enum type
            register_enum_type(type);

            // Create a namespace object with the enum variants
            Object *obj = malloc(sizeof(Object));
            obj->type_name = strdup(type->name);
            obj->num_fields = type->num_variants;
            obj->capacity = type->num_variants;
            obj->field_names = malloc(sizeof(char*) * type->num_variants);
            obj->field_values = malloc(sizeof(Value) * type->num_variants);
            obj->ref_count = 1;

            for (int i = 0; i < type->num_variants; i++) {
                obj->field_names[i] = strdup(type->variant_names[i]);
                obj->field_values[i] = val_i32(type->variant_values[i]);
            }

            Value enum_obj;
            enum_obj.type = VAL_OBJECT;
            enum_obj.as.as_object = obj;

            // Bind the enum namespace to the environment
            env_define(env, type->name, enum_obj, 1, ctx);  // 1 = const
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

                    // Clear exception state and release the exception value
                    // (env_set retained it, so we can release the context's reference)
                    value_release(ctx->exception_state.exception_value);
                    ctx->exception_state.is_throwing = 0;

                    // Execute catch block
                    eval_stmt(stmt->as.try_stmt.catch_block, catch_env, ctx);

                    env_release(catch_env);
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
            // Evaluate the value to throw and retain it
            // (so it survives past environment cleanups during unwinding)
            ctx->exception_state.exception_value = eval_expr(stmt->as.throw_stmt.value, env, ctx);
            value_retain(ctx->exception_state.exception_value);
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
                        value_release(case_value);  // Release case value after comparison
                        matched_case = i;
                        break;
                    }
                    value_release(case_value);  // Release case value after comparison
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

            value_release(switch_value);  // Release switch value after switch completes
            break;
        }

        case STMT_DEFER: {
            // Push the deferred call onto the defer stack
            // It will be executed when the function returns (or exits with exception)
            defer_stack_push(&ctx->defer_stack, stmt->as.defer_stmt.call, env);
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
            // Release exception value before exiting
            value_release(ctx->exception_state.exception_value);
            exit(1);
        }
    }
}
