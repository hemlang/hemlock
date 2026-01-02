/*
 * Hemlock Code Generator - Statement Code Generation
 *
 * Handles code generation for all statement types.
 */

#include "codegen_internal.h"

// ========== STATEMENT CODE GENERATION ==========

void codegen_stmt(CodegenContext *ctx, Stmt *stmt) {
    switch (stmt->type) {
        case STMT_LET: {
            codegen_add_local(ctx, stmt->as.let.name);
            // Add to current scope for proper lexical scoping
            if (ctx->current_scope) {
                scope_add_var(ctx->current_scope, stmt->as.let.name);
            }
            char *safe_name = codegen_sanitize_ident(stmt->as.let.name);

            // OPTIMIZATION: Check if this typed variable can be unboxed
            // Unboxed variables use native C types for 5-10x faster arithmetic
            if (ctx->optimize && ctx->type_ctx && stmt->as.let.type_annotation && stmt->as.let.value) {
                CheckedTypeKind native_type = type_check_can_unbox_annotation(stmt->as.let.type_annotation);
                if (native_type != CHECKED_UNKNOWN) {
                    // Check if variable is marked as unboxable (escape analysis passed)
                    if (type_check_is_typed_var(ctx->type_ctx, stmt->as.let.name) ||
                        type_check_get_unboxable(ctx->type_ctx, stmt->as.let.name) != CHECKED_UNKNOWN) {
                        const char *c_type = checked_type_to_c_type(native_type);
                        const char *unbox_cast = checked_type_to_unbox_cast(native_type);
                        if (c_type && unbox_cast) {
                            // Generate unboxed variable with native C type
                            char *value = codegen_expr(ctx, stmt->as.let.value);
                            codegen_writeln(ctx, "%s %s = %s(%s);", c_type, safe_name, unbox_cast, value);
                            codegen_writeln(ctx, "hml_release(&%s);", value);
                            free(value);
                            free(safe_name);
                            break;
                        }
                    }
                }
            }

            // If we reach here, we're generating standard boxed code
            // Clear any unboxable mark to avoid mismatch with codegen_expr_ident
            if (ctx->type_ctx) {
                type_check_clear_unboxable(ctx->type_ctx, stmt->as.let.name);
            }

            // Standard boxed variable handling
            if (stmt->as.let.value) {
                char *value = codegen_expr(ctx, stmt->as.let.value);
                // Check if there's a custom object type annotation (for duck typing)
                if (stmt->as.let.type_annotation &&
                    stmt->as.let.type_annotation->kind == TYPE_CUSTOM_OBJECT &&
                    stmt->as.let.type_annotation->type_name) {
                    codegen_writeln(ctx, "HmlValue %s = hml_validate_object_type(%s, \"%s\");",
                                  safe_name, value, stmt->as.let.type_annotation->type_name);
                } else if (stmt->as.let.type_annotation &&
                           stmt->as.let.type_annotation->kind == TYPE_ARRAY) {
                    // Typed array: let arr: array<type> = [...]
                    Type *elem_type = stmt->as.let.type_annotation->element_type;
                    const char *hml_type = elem_type ? type_kind_to_hml_val(elem_type->kind) : NULL;
                    if (!hml_type) hml_type = "HML_VAL_NULL";
                    codegen_writeln(ctx, "HmlValue %s = hml_validate_typed_array(%s, %s);",
                                  safe_name, value, hml_type);
                } else if (stmt->as.let.type_annotation) {
                    // Primitive type annotation: let x: i64 = 0;
                    // Convert value to the annotated type with range checking
                    const char *hml_type = type_kind_to_hml_val(stmt->as.let.type_annotation->kind);
                    if (hml_type) {
                        codegen_writeln(ctx, "HmlValue %s = hml_convert_to_type(%s, %s);",
                                      safe_name, value, hml_type);
                    } else {
                        codegen_writeln(ctx, "HmlValue %s = %s;", safe_name, value);
                    }
                } else {
                    codegen_writeln(ctx, "HmlValue %s = %s;", safe_name, value);
                }
                free(value);

                // If the value was a function expression, set its name for better error reporting
                if (stmt->as.let.value->type == EXPR_FUNCTION) {
                    codegen_writeln(ctx, "hml_function_set_name(%s, \"%s\");", safe_name, stmt->as.let.name);
                }

                // Check if this was a self-referential function (e.g., let factorial = fn(n) { ... factorial(n-1) ... })
                // If so, update the closure environment to point to the now-initialized variable
                if (ctx->last_closure_env_id >= 0 && ctx->last_closure_captured) {
                    for (int i = 0; i < ctx->last_closure_num_captured; i++) {
                        if (strcmp(ctx->last_closure_captured[i], stmt->as.let.name) == 0) {
                            codegen_writeln(ctx, "hml_closure_env_set(_env_%d, %d, %s);",
                                          ctx->last_closure_env_id, i, safe_name);
                        }
                    }
                    // Reset the tracking - we've handled this closure
                    ctx->last_closure_env_id = -1;
                }
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_val_null();", safe_name);
            }
            free(safe_name);
            break;
        }

        case STMT_CONST: {
            codegen_add_local(ctx, stmt->as.const_stmt.name);
            codegen_add_const(ctx, stmt->as.const_stmt.name);
            // Add to current scope for proper lexical scoping
            if (ctx->current_scope) {
                scope_add_var(ctx->current_scope, stmt->as.const_stmt.name);
            }
            char *safe_name = codegen_sanitize_ident(stmt->as.const_stmt.name);
            if (stmt->as.const_stmt.value) {
                char *value = codegen_expr(ctx, stmt->as.const_stmt.value);
                codegen_writeln(ctx, "const HmlValue %s = %s;", safe_name, value);
                free(value);
            } else {
                codegen_writeln(ctx, "const HmlValue %s = hml_val_null();", safe_name);
            }
            free(safe_name);
            break;
        }

        case STMT_EXPR: {
            char *value = codegen_expr(ctx, stmt->as.expr);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(value);
            break;
        }

        case STMT_IF: {
            // OPTIMIZATION: Compile-time evaluation of constant conditions
            // Proof: if (true) { A } else { B } -> A (true is always truthy)
            // Proof: if (false) { A } else { B } -> B (false is always falsy)
            if (ctx->optimize && stmt->as.if_stmt.condition->type == EXPR_BOOL) {
                if (stmt->as.if_stmt.condition->as.boolean) {
                    // if (true) -> always execute then branch
                    codegen_stmt(ctx, stmt->as.if_stmt.then_branch);
                } else {
                    // if (false) -> skip then branch, execute else if present
                    if (stmt->as.if_stmt.else_branch) {
                        codegen_stmt(ctx, stmt->as.if_stmt.else_branch);
                    }
                    // if no else branch, emit nothing
                }
                break;
            }

            // OPTIMIZATION: Constant null check
            // Proof: if (null) -> always false (null is falsy)
            if (ctx->optimize && stmt->as.if_stmt.condition->type == EXPR_NULL) {
                if (stmt->as.if_stmt.else_branch) {
                    codegen_stmt(ctx, stmt->as.if_stmt.else_branch);
                }
                break;
            }

            // OPTIMIZATION: Constant number check
            // Proof: if (0) -> false (0 is falsy), if (non-zero) -> true
            if (ctx->optimize && stmt->as.if_stmt.condition->type == EXPR_NUMBER) {
                int is_truthy = 0;
                if (stmt->as.if_stmt.condition->as.number.is_float) {
                    is_truthy = stmt->as.if_stmt.condition->as.number.float_value != 0.0;
                } else {
                    is_truthy = stmt->as.if_stmt.condition->as.number.int_value != 0;
                }
                if (is_truthy) {
                    codegen_stmt(ctx, stmt->as.if_stmt.then_branch);
                } else if (stmt->as.if_stmt.else_branch) {
                    codegen_stmt(ctx, stmt->as.if_stmt.else_branch);
                }
                break;
            }

            char *cond = codegen_expr(ctx, stmt->as.if_stmt.condition);
            codegen_writeln(ctx, "if (hml_to_bool(%s)) {", cond);
            codegen_indent_inc(ctx);
            codegen_stmt(ctx, stmt->as.if_stmt.then_branch);
            codegen_indent_dec(ctx);
            if (stmt->as.if_stmt.else_branch) {
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_stmt(ctx, stmt->as.if_stmt.else_branch);
                codegen_indent_dec(ctx);
            }
            codegen_writeln(ctx, "}");
            codegen_writeln(ctx, "hml_release(&%s);", cond);
            free(cond);
            break;
        }

        case STMT_WHILE: {
            ctx->loop_depth++;
            codegen_writeln(ctx, "while (1) {");
            codegen_indent_inc(ctx);
            char *cond = codegen_expr(ctx, stmt->as.while_stmt.condition);
            codegen_writeln(ctx, "if (!hml_to_bool(%s)) { hml_release(&%s); break; }", cond, cond);
            codegen_writeln(ctx, "hml_release(&%s);", cond);
            codegen_stmt(ctx, stmt->as.while_stmt.body);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            ctx->loop_depth--;
            free(cond);
            break;
        }

        case STMT_FOR: {
            ctx->loop_depth++;

            // OPTIMIZATION: Analyze loop for unboxable counter
            if (ctx->type_ctx) {
                type_check_analyze_for_loop(ctx->type_ctx, stmt);
            }

            // Check if we can generate an optimized loop with native counter
            const char *counter_name = NULL;
            CheckedTypeKind counter_type = CHECKED_UNKNOWN;
            if (ctx->type_ctx && stmt->as.for_loop.initializer &&
                stmt->as.for_loop.initializer->type == STMT_LET) {
                counter_name = stmt->as.for_loop.initializer->as.let.name;
                counter_type = type_check_get_unboxable(ctx->type_ctx, counter_name);
            }

            if (ctx->optimize && ctx->type_ctx && counter_type == CHECKED_I32 &&
                type_check_is_loop_counter(ctx->type_ctx, counter_name)) {
                // OPTIMIZED: Generate loop with native int32_t counter
                codegen_writeln(ctx, "{");
                codegen_indent_inc(ctx);

                // Get initial value
                Stmt *init = stmt->as.for_loop.initializer;
                int32_t init_val = (int32_t)init->as.let.value->as.number.int_value;
                char *safe_name = codegen_sanitize_ident(counter_name);

                // Declare native counter
                codegen_writeln(ctx, "int32_t %s = %d;", safe_name, init_val);
                codegen_add_local(ctx, counter_name);

                // Create continue label
                char *continue_label = codegen_label(ctx);
                codegen_push_for_continue(ctx, continue_label);

                // Generate optimized condition
                Expr *cond = stmt->as.for_loop.condition;
                if (cond && cond->type == EXPR_BINARY) {
                    const char *op_str = NULL;
                    switch (cond->as.binary.op) {
                        case OP_LESS: op_str = "<"; break;
                        case OP_LESS_EQUAL: op_str = "<="; break;
                        case OP_GREATER: op_str = ">"; break;
                        case OP_GREATER_EQUAL: op_str = ">="; break;
                        case OP_EQUAL: op_str = "=="; break;
                        case OP_NOT_EQUAL: op_str = "!="; break;
                        default: break;
                    }

                    if (op_str) {
                        // Determine the bound expression
                        Expr *bound_expr = NULL;
                        int counter_on_left = 0;
                        if (cond->as.binary.left->type == EXPR_IDENT &&
                            strcmp(cond->as.binary.left->as.ident.name, counter_name) == 0) {
                            bound_expr = cond->as.binary.right;
                            counter_on_left = 1;
                        } else if (cond->as.binary.right->type == EXPR_IDENT &&
                                   strcmp(cond->as.binary.right->as.ident.name, counter_name) == 0) {
                            bound_expr = cond->as.binary.left;
                            counter_on_left = 0;
                        }

                        if (bound_expr && bound_expr->type == EXPR_NUMBER &&
                            !bound_expr->as.number.is_float) {
                            // Constant bound - fully optimized loop
                            int32_t bound = (int32_t)bound_expr->as.number.int_value;
                            if (counter_on_left) {
                                codegen_writeln(ctx, "while (%s %s %d) {", safe_name, op_str, bound);
                            } else {
                                codegen_writeln(ctx, "while (%d %s %s) {", bound, op_str, safe_name);
                            }
                        } else if (bound_expr) {
                            // Dynamic bound - evaluate once before loop
                            char *bound_val = codegen_expr(ctx, bound_expr);
                            codegen_writeln(ctx, "int32_t _bound = hml_to_i32(%s);", bound_val);
                            codegen_writeln(ctx, "hml_release_if_needed(&%s);", bound_val);
                            if (counter_on_left) {
                                codegen_writeln(ctx, "while (%s %s _bound) {", safe_name, op_str);
                            } else {
                                codegen_writeln(ctx, "while (_bound %s %s) {", op_str, safe_name);
                            }
                            free(bound_val);
                        } else {
                            // Fallback
                            codegen_writeln(ctx, "while (1) {");
                        }
                    } else {
                        codegen_writeln(ctx, "while (1) {");
                    }
                } else {
                    codegen_writeln(ctx, "while (1) {");
                }

                codegen_indent_inc(ctx);

                // Body - but we need to handle references to the counter specially
                // The body expects an HmlValue, so we create a temporary when needed
                codegen_stmt(ctx, stmt->as.for_loop.body);

                // Continue label
                codegen_writeln(ctx, "%s:;", continue_label);

                // Optimized increment
                Expr *inc = stmt->as.for_loop.increment;
                if (inc) {
                    if (inc->type == EXPR_POSTFIX_INC || inc->type == EXPR_PREFIX_INC) {
                        codegen_writeln(ctx, "%s++;", safe_name);
                    } else if (inc->type == EXPR_POSTFIX_DEC || inc->type == EXPR_PREFIX_DEC) {
                        codegen_writeln(ctx, "%s--;", safe_name);
                    } else if (inc->type == EXPR_ASSIGN &&
                               strcmp(inc->as.assign.name, counter_name) == 0 &&
                               inc->as.assign.value->type == EXPR_BINARY) {
                        Expr *binop = inc->as.assign.value;
                        if (binop->as.binary.left->type == EXPR_IDENT &&
                            strcmp(binop->as.binary.left->as.ident.name, counter_name) == 0 &&
                            binop->as.binary.right->type == EXPR_NUMBER &&
                            !binop->as.binary.right->as.number.is_float) {
                            int32_t step = (int32_t)binop->as.binary.right->as.number.int_value;
                            if (binop->as.binary.op == OP_ADD) {
                                codegen_writeln(ctx, "%s += %d;", safe_name, step);
                            } else if (binop->as.binary.op == OP_SUB) {
                                codegen_writeln(ctx, "%s -= %d;", safe_name, step);
                            }
                        }
                    }
                }

                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
                codegen_pop_for_continue(ctx);
                free(continue_label);
                free(safe_name);
            } else {
                // STANDARD: Generate loop with boxed HmlValue counter
                // Clear any unboxable mark since we're NOT unboxing this counter
                if (ctx->type_ctx && counter_name) {
                    type_check_clear_unboxable(ctx->type_ctx, counter_name);
                }
                codegen_writeln(ctx, "{");
                codegen_indent_inc(ctx);
                // Initializer
                if (stmt->as.for_loop.initializer) {
                    codegen_stmt(ctx, stmt->as.for_loop.initializer);
                }
                // Create continue label for this for loop (continue jumps here, before increment)
                char *continue_label = codegen_label(ctx);
                codegen_push_for_continue(ctx, continue_label);

                codegen_writeln(ctx, "while (1) {");
                codegen_indent_inc(ctx);
                // Condition
                if (stmt->as.for_loop.condition) {
                    char *cond = codegen_expr(ctx, stmt->as.for_loop.condition);
                    codegen_writeln(ctx, "if (!hml_to_bool(%s)) { hml_release(&%s); break; }", cond, cond);
                    codegen_writeln(ctx, "hml_release(&%s);", cond);
                    free(cond);
                }
                // Body
                codegen_stmt(ctx, stmt->as.for_loop.body);
                // Continue label - continue jumps here to execute increment
                codegen_writeln(ctx, "%s:;", continue_label);
                // Increment
                if (stmt->as.for_loop.increment) {
                    char *inc = codegen_expr(ctx, stmt->as.for_loop.increment);
                    codegen_writeln(ctx, "hml_release(&%s);", inc);
                    free(inc);
                }
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
                codegen_pop_for_continue(ctx);
                free(continue_label);
            }
            ctx->loop_depth--;
            break;
        }

        case STMT_FOR_IN: {
            // Generate for-in loop for arrays, objects, or strings
            // for (let val in iterable) or for (let key, val in iterable)
            ctx->loop_depth++;
            codegen_writeln(ctx, "{");
            codegen_indent_inc(ctx);

            // Create continue label for this for-in loop (continue jumps here, before increment)
            char *continue_label = codegen_label(ctx);
            codegen_push_for_continue(ctx, continue_label);

            // Evaluate the iterable
            char *iter_val = codegen_expr(ctx, stmt->as.for_in.iterable);
            codegen_writeln(ctx, "hml_retain(&%s);", iter_val);

            // Check for valid iterable type (array, object, or string)
            codegen_writeln(ctx, "if (%s.type != HML_VAL_ARRAY && %s.type != HML_VAL_OBJECT && %s.type != HML_VAL_STRING) {",
                          iter_val, iter_val, iter_val);
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "hml_release(&%s);", iter_val);
            codegen_writeln(ctx, "hml_runtime_error(\"for-in requires array, object, or string\");");
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");

            // Index counter
            char *idx_var = codegen_temp(ctx);
            codegen_writeln(ctx, "int32_t %s = 0;", idx_var);

            // Get the length based on type
            char *len_var = codegen_temp(ctx);
            codegen_writeln(ctx, "int32_t %s;", len_var);
            codegen_writeln(ctx, "if (%s.type == HML_VAL_OBJECT) {", iter_val);
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "%s = hml_object_num_fields(%s);", len_var, iter_val);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "} else if (%s.type == HML_VAL_STRING) {", iter_val);
            codegen_indent_inc(ctx);
            // Use UTF-8 character count for strings
            codegen_writeln(ctx, "%s = hml_string_char_count(%s).as.as_i32;", len_var, iter_val);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "} else {");
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "%s = hml_array_length(%s).as.as_i32;", len_var, iter_val);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");

            codegen_writeln(ctx, "while (%s < %s) {", idx_var, len_var);
            codegen_indent_inc(ctx);

            // Create key and value variables based on iterable type
            // Sanitize variable names to avoid C keyword conflicts
            char *safe_key_var = stmt->as.for_in.key_var ? codegen_sanitize_ident(stmt->as.for_in.key_var) : NULL;
            char *safe_value_var = codegen_sanitize_ident(stmt->as.for_in.value_var);

            if (stmt->as.for_in.key_var) {
                codegen_writeln(ctx, "HmlValue %s;", safe_key_var);
                codegen_add_local(ctx, stmt->as.for_in.key_var);
            }
            codegen_writeln(ctx, "HmlValue %s;", safe_value_var);
            codegen_add_local(ctx, stmt->as.for_in.value_var);

            // Handle object iteration
            codegen_writeln(ctx, "if (%s.type == HML_VAL_OBJECT) {", iter_val);
            codegen_indent_inc(ctx);
            if (stmt->as.for_in.key_var) {
                codegen_writeln(ctx, "%s = hml_object_key_at(%s, %s);", safe_key_var, iter_val, idx_var);
            }
            codegen_writeln(ctx, "%s = hml_object_value_at(%s, %s);", safe_value_var, iter_val, idx_var);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "} else if (%s.type == HML_VAL_STRING) {", iter_val);
            codegen_indent_inc(ctx);
            // Handle string iteration - use UTF-8 aware rune extraction
            if (stmt->as.for_in.key_var) {
                codegen_writeln(ctx, "%s = hml_val_i32(%s);", safe_key_var, idx_var);
            }
            char *idx_val_str = codegen_temp(ctx);
            codegen_writeln(ctx, "HmlValue %s = hml_val_i32(%s);", idx_val_str, idx_var);
            codegen_writeln(ctx, "%s = hml_string_rune_at(%s, %s);", safe_value_var, iter_val, idx_val_str);
            codegen_writeln(ctx, "hml_release(&%s);", idx_val_str);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "} else {");
            codegen_indent_inc(ctx);
            // Handle array iteration
            if (stmt->as.for_in.key_var) {
                codegen_writeln(ctx, "%s = hml_val_i32(%s);", safe_key_var, idx_var);
            }
            char *idx_val = codegen_temp(ctx);
            codegen_writeln(ctx, "HmlValue %s = hml_val_i32(%s);", idx_val, idx_var);
            codegen_writeln(ctx, "%s = hml_array_get(%s, %s);", safe_value_var, iter_val, idx_val);
            codegen_writeln(ctx, "hml_release(&%s);", idx_val);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");

            // Generate body
            codegen_stmt(ctx, stmt->as.for_in.body);

            // Continue label - continue jumps here to release variables and increment
            codegen_writeln(ctx, "%s:;", continue_label);

            // Release loop variables
            if (stmt->as.for_in.key_var) {
                codegen_writeln(ctx, "hml_release(&%s);", safe_key_var);
            }
            codegen_writeln(ctx, "hml_release(&%s);", safe_value_var);

            // Free sanitized names
            if (safe_key_var) free(safe_key_var);
            free(safe_value_var);

            // Increment index
            codegen_writeln(ctx, "%s++;", idx_var);

            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");

            // Cleanup
            codegen_writeln(ctx, "hml_release(&%s);", iter_val);

            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            codegen_pop_for_continue(ctx);
            free(continue_label);
            ctx->loop_depth--;

            free(iter_val);
            free(len_var);
            free(idx_var);
            break;
        }

        case STMT_BLOCK: {
            // Push a new scope for proper lexical scoping in blocks
            codegen_push_scope(ctx);
            codegen_writeln(ctx, "{");
            codegen_indent_inc(ctx);

            // OPTIMIZATION: Dead code elimination
            // Skip statements after return, throw, break, continue
            // Proof: Control flow terminators make subsequent code unreachable
            for (int i = 0; i < stmt->as.block.count; i++) {
                codegen_stmt(ctx, stmt->as.block.statements[i]);

                // Check if this statement terminates control flow
                if (ctx->optimize) {
                    Stmt *s = stmt->as.block.statements[i];
                    if (s->type == STMT_RETURN || s->type == STMT_THROW ||
                        s->type == STMT_BREAK || s->type == STMT_CONTINUE) {
                        // Skip remaining statements (they are dead code)
                        // Note: We don't warn here because this may be intentional
                        // (e.g., conditional returns with code below)
                        break;
                    }
                }
            }

            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            codegen_pop_scope(ctx);
            break;
        }

        case STMT_RETURN: {
            // Check if we're inside a try-finally block
            const char *finally_label = codegen_get_finally_label(ctx);
            if (finally_label) {
                // Inside try-finally: save return value and goto finally
                const char *ret_var = codegen_get_return_value_var(ctx);
                const char *has_ret = codegen_get_has_return_var(ctx);
                if (stmt->as.return_stmt.value) {
                    char *value = codegen_expr(ctx, stmt->as.return_stmt.value);
                    codegen_writeln(ctx, "%s = %s;", ret_var, value);
                    free(value);
                } else {
                    codegen_writeln(ctx, "%s = hml_val_null();", ret_var);
                }
                codegen_writeln(ctx, "%s = 1;", has_ret);
                codegen_writeln(ctx, "hml_exception_pop();");
                codegen_writeln(ctx, "goto %s;", finally_label);
            } else if (ctx->defer_stack) {
                // We have defers - need to save return value, execute defers, then return
                char *ret_val = codegen_temp(ctx);
                if (stmt->as.return_stmt.value) {
                    char *value = codegen_expr(ctx, stmt->as.return_stmt.value);
                    codegen_writeln(ctx, "HmlValue %s = %s;", ret_val, value);
                    free(value);
                } else {
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", ret_val);
                }
                // Execute all defers in LIFO order
                codegen_defer_execute_all(ctx);
                // Execute any runtime defers (from loops) - only if this function has defers
                if (ctx->has_defers) {
                    codegen_writeln(ctx, "hml_defer_execute_all();");
                }
                if (ctx->stack_check) {
                    codegen_writeln(ctx, "HML_CALL_EXIT();");
                }
                codegen_writeln(ctx, "return %s;", ret_val);
                free(ret_val);
            } else {
                // No defers or try-finally - check for tail call optimization
                // OPTIMIZATION: If returning a tail call to the current function,
                // convert to parameter reassignment + goto instead of actual call
                if (ctx->tail_call_func_name && stmt->as.return_stmt.value &&
                    is_tail_call_expr(stmt->as.return_stmt.value, ctx->tail_call_func_name)) {
                    // Tail call optimization: reassign parameters and goto start
                    Expr *call_expr = stmt->as.return_stmt.value;
                    Expr *func = ctx->tail_call_func_expr;
                    int num_params = func->as.function.num_params;

                    // Evaluate new argument values first (before releasing old ones)
                    char **new_arg_vals = malloc(num_params * sizeof(char*));
                    // Initialize all elements to avoid uninitialized access warnings
                    for (int i = 0; i < num_params; i++) {
                        if (i < call_expr->as.call.num_args) {
                            new_arg_vals[i] = codegen_expr(ctx, call_expr->as.call.args[i]);
                        } else {
                            // Fill in defaults for missing args
                            new_arg_vals[i] = strdup("hml_val_null()");
                        }
                    }

                    // Release old parameter values and assign new ones
                    for (int i = 0; i < num_params; i++) {
                        char *safe_param = codegen_sanitize_ident(func->as.function.param_names[i]);
                        codegen_writeln(ctx, "hml_release(&%s);", safe_param);
                        codegen_writeln(ctx, "%s = %s;", safe_param, new_arg_vals[i]);
                        free(safe_param);
                        free(new_arg_vals[i]);
                    }
                    free(new_arg_vals);

                    // Jump back to the start of the function
                    codegen_writeln(ctx, "goto %s;", ctx->tail_call_label);
                } else if (stmt->as.return_stmt.value) {
                    // Regular return with value
                    char *value = codegen_expr(ctx, stmt->as.return_stmt.value);
                    // Execute any runtime defers (from loops) - only if this function has defers
                    if (ctx->has_defers) {
                        codegen_writeln(ctx, "hml_defer_execute_all();");
                    }
                    if (ctx->stack_check) {
                        codegen_writeln(ctx, "HML_CALL_EXIT();");
                    }
                    codegen_writeln(ctx, "return %s;", value);
                    free(value);
                } else {
                    // Execute any runtime defers (from loops) - only if this function has defers
                    if (ctx->has_defers) {
                        codegen_writeln(ctx, "hml_defer_execute_all();");
                    }
                    if (ctx->stack_check) {
                        codegen_writeln(ctx, "HML_CALL_EXIT();");
                    }
                    codegen_writeln(ctx, "return hml_val_null();");
                }
            }
            break;
        }

        case STMT_BREAK: {
            // If inside a switch, use goto to exit (so continue still works for loops)
            const char *switch_end = codegen_get_switch_end_label(ctx);
            if (switch_end) {
                codegen_writeln(ctx, "goto %s;", switch_end);
            } else {
                codegen_writeln(ctx, "break;");
            }
            break;
        }

        case STMT_CONTINUE: {
            // If inside a for loop, use goto to jump to before the increment
            const char *for_continue = codegen_get_for_continue_label(ctx);
            if (for_continue) {
                codegen_writeln(ctx, "goto %s;", for_continue);
            } else {
                codegen_writeln(ctx, "continue;");
            }
            break;
        }

        case STMT_TRY: {
            codegen_writeln(ctx, "{");
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "HmlExceptionContext *_ex_ctx = hml_exception_push();");

            // Track if we need to re-throw after finally
            int has_finally = stmt->as.try_stmt.finally_block != NULL;
            int has_catch = stmt->as.try_stmt.catch_block != NULL;

            // Generate unique names for try-finally support (for return to jump to finally)
            // This is only needed when inside a function (at top-level, no return is possible)
            char *finally_label = NULL;
            char *return_value_var = NULL;
            char *has_return_var = NULL;
            int needs_return_tracking = has_finally && ctx->in_function;

            if (needs_return_tracking) {
                finally_label = codegen_label(ctx);
                return_value_var = codegen_temp(ctx);
                has_return_var = codegen_temp(ctx);

                // Declare variables for tracking return from try block
                codegen_writeln(ctx, "HmlValue %s = hml_val_null();", return_value_var);
                codegen_writeln(ctx, "int %s = 0;", has_return_var);

                // Push try-finally context so return statements inside use goto
                codegen_push_try_finally(ctx, finally_label, return_value_var, has_return_var);
            }

            if (has_finally && !has_catch) {
                // Track exception state for try-finally without catch
                codegen_writeln(ctx, "int _had_exception = 0;");
                codegen_writeln(ctx, "HmlValue _saved_exception = hml_val_null();");
            }

            codegen_writeln(ctx, "if (setjmp(_ex_ctx->exception_buf) == 0) {");
            codegen_indent_inc(ctx);
            // Try block
            codegen_stmt(ctx, stmt->as.try_stmt.try_block);
            codegen_indent_dec(ctx);
            if (has_catch) {
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                // Catch block - declare catch param as shadow var to shadow main vars
                if (stmt->as.try_stmt.catch_param) {
                    char *safe_catch_param = codegen_sanitize_ident(stmt->as.try_stmt.catch_param);
                    codegen_add_shadow(ctx, stmt->as.try_stmt.catch_param);
                    codegen_writeln(ctx, "HmlValue %s = hml_exception_get_value();", safe_catch_param);
                    codegen_stmt(ctx, stmt->as.try_stmt.catch_block);
                    codegen_writeln(ctx, "hml_release(&%s);", safe_catch_param);
                    // Remove catch param from shadow vars so outer scope variable is used again
                    codegen_remove_shadow(ctx, stmt->as.try_stmt.catch_param);
                    free(safe_catch_param);
                } else {
                    codegen_stmt(ctx, stmt->as.try_stmt.catch_block);
                }
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            } else if (has_finally) {
                // try-finally without catch: save exception for re-throw
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "_had_exception = 1;");
                codegen_writeln(ctx, "_saved_exception = hml_exception_get_value();");
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            } else {
                codegen_writeln(ctx, "}");
            }

            // Pop exception context BEFORE finally block
            // This ensures exceptions in finally go to outer handler
            codegen_writeln(ctx, "hml_exception_pop();");

            // Finally block
            if (has_finally) {
                // Pop try-finally context before generating finally
                // (return statements in finally should not jump to itself)
                if (needs_return_tracking) {
                    codegen_pop_try_finally(ctx);

                    // Generate the finally label (jumped to from return statements in try)
                    codegen_writeln(ctx, "%s:;", finally_label);
                }

                codegen_stmt(ctx, stmt->as.try_stmt.finally_block);

                // Re-throw saved exception if try threw and there was no catch
                if (!has_catch) {
                    codegen_writeln(ctx, "if (_had_exception) {");
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "hml_throw(_saved_exception);");
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "}");
                }

                // Check if we should return (from a return statement in the try block)
                if (needs_return_tracking) {
                    codegen_writeln(ctx, "if (%s) {", has_return_var);
                    codegen_indent_inc(ctx);
                    // Execute any runtime defers (from loops) - only if this function has defers
                    if (ctx->has_defers) {
                        codegen_writeln(ctx, "hml_defer_execute_all();");
                    }
                    if (ctx->stack_check) {
                        codegen_writeln(ctx, "HML_CALL_EXIT();");
                    }
                    codegen_writeln(ctx, "return %s;", return_value_var);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "}");

                    free(finally_label);
                    free(return_value_var);
                    free(has_return_var);
                }
            }

            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            break;
        }

        case STMT_THROW: {
            char *value = codegen_expr(ctx, stmt->as.throw_stmt.value);
            // Execute defers before throwing (they must run)
            if (ctx->defer_stack) {
                codegen_defer_execute_all(ctx);
            }
            codegen_writeln(ctx, "hml_throw(%s);", value);
            free(value);
            break;
        }

        case STMT_SWITCH: {
            // Generate switch with fall-through using goto labels
            // This matches interpreter behavior where execution continues
            // from matched case until break is encountered
            char *expr_val = codegen_expr(ctx, stmt->as.switch_stmt.expr);
            int num_cases = stmt->as.switch_stmt.num_cases;
            int has_default = 0;
            int default_idx = -1;

            // Find default case
            for (int i = 0; i < num_cases; i++) {
                if (stmt->as.switch_stmt.case_values[i] == NULL) {
                    has_default = 1;
                    default_idx = i;
                    break;
                }
            }

            // Generate unique labels for this switch
            char **case_labels = malloc(num_cases * sizeof(char*));
            for (int i = 0; i < num_cases; i++) {
                case_labels[i] = codegen_label(ctx);
            }
            char *end_label = codegen_label(ctx);

            // Track switch context so break generates goto end_label
            codegen_push_switch(ctx, end_label);

            codegen_writeln(ctx, "{");
            codegen_indent_inc(ctx);

            // Pre-generate all case values to avoid scoping issues
            char **case_vals = malloc(num_cases * sizeof(char*));
            for (int i = 0; i < num_cases; i++) {
                if (stmt->as.switch_stmt.case_values[i] == NULL) {
                    case_vals[i] = NULL;
                } else {
                    case_vals[i] = codegen_expr(ctx, stmt->as.switch_stmt.case_values[i]);
                }
            }

            // Generate case matching logic - jump to first matching case
            for (int i = 0; i < num_cases; i++) {
                if (case_vals[i] == NULL) continue;  // Skip default in matching
                codegen_writeln(ctx, "if (hml_to_bool(hml_binary_op(HML_OP_EQUAL, %s, %s))) goto %s;",
                              expr_val, case_vals[i], case_labels[i]);
            }

            // If no case matched, jump to default or end
            if (has_default) {
                codegen_writeln(ctx, "goto %s;", case_labels[default_idx]);
            } else {
                codegen_writeln(ctx, "goto %s;", end_label);
            }

            // Generate case bodies with labels - fall-through happens naturally
            for (int i = 0; i < num_cases; i++) {
                codegen_writeln(ctx, "%s:;", case_labels[i]);
                codegen_stmt(ctx, stmt->as.switch_stmt.case_bodies[i]);
                // No automatic break - fall through to next case
            }

            // End label for cleanup
            codegen_writeln(ctx, "%s:;", end_label);

            // Release case values
            for (int i = 0; i < num_cases; i++) {
                if (case_vals[i]) {
                    codegen_writeln(ctx, "hml_release(&%s);", case_vals[i]);
                    free(case_vals[i]);
                }
            }
            free(case_vals);

            codegen_writeln(ctx, "hml_release(&%s);", expr_val);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");

            // Pop switch context
            codegen_pop_switch(ctx);

            // Free labels
            for (int i = 0; i < num_cases; i++) {
                free(case_labels[i]);
            }
            free(case_labels);
            free(end_label);
            free(expr_val);
            break;
        }

        case STMT_DEFER: {
            ctx->has_defers = 1;  // Mark that this function has defers
            // Always use runtime defer stack - this correctly handles:
            // - Defers inside loops
            // - Defers inside conditionals (if/else branches)
            // - Nested control flow
            if (stmt->as.defer_stmt.call->type == EXPR_CALL) {
                // Get the function being called and its arguments
                Expr *call_expr = stmt->as.defer_stmt.call;
                char *fn_val = codegen_expr(ctx, call_expr->as.call.func);
                int num_args = call_expr->as.call.num_args;

                if (num_args == 0) {
                    // No arguments - use simpler push
                    codegen_writeln(ctx, "hml_defer_push_call(%s);", fn_val);
                    codegen_writeln(ctx, "hml_release(&%s);", fn_val);
                } else {
                    // Has arguments - evaluate them and push with args
                    char **arg_vals = malloc(sizeof(char*) * num_args);
                    for (int i = 0; i < num_args; i++) {
                        arg_vals[i] = codegen_expr(ctx, call_expr->as.call.args[i]);
                    }
                    // Build array of arguments
                    codegen_writeln(ctx, "{");
                    ctx->indent++;
                    codegen_writeln(ctx, "HmlValue _defer_args[%d];", num_args);
                    for (int i = 0; i < num_args; i++) {
                        codegen_writeln(ctx, "_defer_args[%d] = %s;", i, arg_vals[i]);
                    }
                    codegen_writeln(ctx, "hml_defer_push_call_with_args(%s, _defer_args, %d);", fn_val, num_args);
                    // Release the values (runtime defer has its own copies)
                    for (int i = 0; i < num_args; i++) {
                        codegen_writeln(ctx, "hml_release(&%s);", arg_vals[i]);
                        free(arg_vals[i]);
                    }
                    codegen_writeln(ctx, "hml_release(&%s);", fn_val);
                    free(arg_vals);
                    ctx->indent--;
                    codegen_writeln(ctx, "}");
                }
                free(fn_val);
            } else {
                // For non-call expressions (like identifiers), evaluate and push as 0-arg call
                char *val = codegen_expr(ctx, stmt->as.defer_stmt.call);
                codegen_writeln(ctx, "hml_defer_push_call(%s);", val);
                codegen_writeln(ctx, "hml_release(&%s);", val);
                free(val);
            }
            break;
        }

        case STMT_ENUM: {
            // Generate enum as a const object with variant values
            const char *raw_enum_name = stmt->as.enum_decl.name;

            // Determine the correct variable name with prefix
            char prefixed_name[CODEGEN_MANGLED_NAME_SIZE];
            const char *enum_name = raw_enum_name;
            if (ctx->current_module && !codegen_is_local(ctx, raw_enum_name)) {
                snprintf(prefixed_name, sizeof(prefixed_name), "%s%s",
                        ctx->current_module->module_prefix, raw_enum_name);
                enum_name = prefixed_name;
            } else if (codegen_is_main_var(ctx, raw_enum_name)) {
                snprintf(prefixed_name, sizeof(prefixed_name), "_main_%s", raw_enum_name);
                enum_name = prefixed_name;
            }

            codegen_writeln(ctx, "%s = hml_val_object();", enum_name);

            int next_value = 0;
            for (int i = 0; i < stmt->as.enum_decl.num_variants; i++) {
                char *variant_name = stmt->as.enum_decl.variant_names[i];

                if (stmt->as.enum_decl.variant_values[i]) {
                    // Explicit value - generate and use it
                    char *val = codegen_expr(ctx, stmt->as.enum_decl.variant_values[i]);
                    codegen_writeln(ctx, "hml_object_set_field(%s, \"%s\", %s);",
                                  enum_name, variant_name, val);
                    codegen_writeln(ctx, "hml_release(&%s);", val);

                    // Extract numeric value for next_value calculation
                    // For simplicity, assume explicit values are integer literals
                    Expr *value_expr = stmt->as.enum_decl.variant_values[i];
                    if (value_expr->type == EXPR_NUMBER && !value_expr->as.number.is_float) {
                        next_value = (int)value_expr->as.number.int_value + 1;
                    }
                    free(val);
                } else {
                    // Auto-incrementing value
                    codegen_writeln(ctx, "hml_object_set_field(%s, \"%s\", hml_val_i32(%d));",
                                  enum_name, variant_name, next_value);
                    next_value++;
                }
            }

            // Add enum as local variable (using raw name for lookup)
            codegen_add_local(ctx, raw_enum_name);
            break;
        }

        case STMT_DEFINE_OBJECT: {
            // Generate type definition registration at runtime
            char *type_name = stmt->as.define_object.name;
            int num_fields = stmt->as.define_object.num_fields;

            // Generate field definitions array
            codegen_writeln(ctx, "{");
            ctx->indent++;
            codegen_writeln(ctx, "HmlTypeField _type_fields_%s[%d];",
                          type_name, num_fields > 0 ? num_fields : 1);

            for (int i = 0; i < num_fields; i++) {
                char *field_name = stmt->as.define_object.field_names[i];
                Type *field_type = stmt->as.define_object.field_types[i];
                int is_optional = stmt->as.define_object.field_optional[i];
                Expr *default_expr = stmt->as.define_object.field_defaults[i];

                codegen_writeln(ctx, "_type_fields_%s[%d].name = \"%s\";",
                              type_name, i, field_name);

                // Map Type to HML_VAL_* type using helper (-1 means any type)
                const char *type_str = field_type ? type_kind_to_hml_val(field_type->kind) : NULL;
                if (type_str) {
                    codegen_writeln(ctx, "_type_fields_%s[%d].type_kind = %s;",
                                  type_name, i, type_str);
                } else {
                    codegen_writeln(ctx, "_type_fields_%s[%d].type_kind = -1;",
                                  type_name, i);
                }
                codegen_writeln(ctx, "_type_fields_%s[%d].is_optional = %d;",
                              type_name, i, is_optional);

                // Generate default value if present
                if (default_expr) {
                    char *default_val = codegen_expr(ctx, default_expr);
                    codegen_writeln(ctx, "_type_fields_%s[%d].default_value = %s;",
                                  type_name, i, default_val);
                    free(default_val);
                } else {
                    codegen_writeln(ctx, "_type_fields_%s[%d].default_value = hml_val_null();",
                                  type_name, i);
                }
            }

            // Register the type
            codegen_writeln(ctx, "hml_register_type(\"%s\", _type_fields_%s, %d);",
                          type_name, type_name, num_fields);
            ctx->indent--;
            codegen_writeln(ctx, "}");
            break;
        }

        case STMT_IMPORT: {
            // Handle module imports
            if (!ctx->module_cache) {
                codegen_warning(ctx, stmt->line, "import without module cache: \"%s\"", stmt->as.import_stmt.module_path);
                break;
            }

            // Resolve the import path
            const char *importer_path = ctx->current_module ? ctx->current_module->absolute_path : NULL;
            char *resolved = module_resolve_path(ctx->module_cache, importer_path, stmt->as.import_stmt.module_path);
            if (!resolved) {
                codegen_error(ctx, stmt->line, "could not resolve import \"%s\"", stmt->as.import_stmt.module_path);
                break;
            }

            // Get or compile the module
            CompiledModule *imported = module_get_cached(ctx->module_cache, resolved);
            if (!imported) {
                imported = module_compile(ctx, resolved);
            }
            free(resolved);

            if (!imported) {
                codegen_error(ctx, stmt->line, "failed to compile import \"%s\"", stmt->as.import_stmt.module_path);
                break;
            }

            // Generate import binding code
            codegen_writeln(ctx, "// Import from \"%s\"", stmt->as.import_stmt.module_path);

            if (stmt->as.import_stmt.is_namespace) {
                char *ns_name = stmt->as.import_stmt.namespace_name;
                if (ns_name != NULL) {
                    // Namespace import: import * as name from "module"
                    // Create an object containing all exports

                    // Determine the correct variable name
                    // - In module context: use module prefix (e.g., _mod15_env)
                    // - In main file: use _main_ prefix (e.g., _main_env)
                    char prefixed_name[CODEGEN_MANGLED_NAME_SIZE];
                    const char *var_name = ns_name;
                    if (ctx->current_module) {
                        snprintf(prefixed_name, sizeof(prefixed_name), "%s%s",
                                ctx->current_module->module_prefix, ns_name);
                        var_name = prefixed_name;
                    } else if (codegen_is_main_var(ctx, ns_name)) {
                        snprintf(prefixed_name, sizeof(prefixed_name), "_main_%s", ns_name);
                        var_name = prefixed_name;
                    }

                    // Initialize namespace object with exports (variable already declared as static)
                    codegen_writeln(ctx, "%s = hml_val_object();", var_name);
                    codegen_add_local(ctx, ns_name);

                    for (int i = 0; i < imported->num_exports; i++) {
                        ExportedSymbol *exp = &imported->exports[i];
                        codegen_writeln(ctx, "hml_object_set_field(%s, \"%s\", %s);", var_name, exp->name, exp->mangled_name);
                    }
                } else {
                    // Star import: import * from "module" - import all exports directly
                    for (int i = 0; i < imported->num_exports; i++) {
                        ExportedSymbol *exp = &imported->exports[i];
                        codegen_writeln(ctx, "HmlValue %s = %s;", exp->name, exp->mangled_name);
                        codegen_add_local(ctx, exp->name);
                    }
                }
            } else {
                // Named imports: import { a, b as c } from "module"
                for (int i = 0; i < stmt->as.import_stmt.num_imports; i++) {
                    const char *import_name = stmt->as.import_stmt.import_names[i];
                    const char *alias = stmt->as.import_stmt.import_aliases[i];
                    const char *bind_name = alias ? alias : import_name;

                    // Find the export in the imported module
                    ExportedSymbol *exp = module_find_export(imported, import_name);
                    if (exp) {
                        codegen_writeln(ctx, "HmlValue %s = %s;", bind_name, exp->mangled_name);
                        codegen_add_local(ctx, bind_name);
                    } else {
                        codegen_error(ctx, stmt->line, "'%s' is not exported from module \"%s\"",
                                     import_name, stmt->as.import_stmt.module_path);
                        codegen_writeln(ctx, "HmlValue %s = hml_val_null();", bind_name);
                        codegen_add_local(ctx, bind_name);
                    }
                }
            }
            break;
        }

        case STMT_EXPORT: {
            // Handle export statements
            if (stmt->as.export_stmt.is_declaration && stmt->as.export_stmt.declaration) {
                // Export declaration: export let x = 1; or export fn foo() {}
                Stmt *decl = stmt->as.export_stmt.declaration;

                // If we're in a module context, use prefixed names
                if (ctx->current_module) {
                    const char *name = NULL;
                    if (decl->type == STMT_LET) {
                        name = decl->as.let.name;
                    } else if (decl->type == STMT_CONST) {
                        name = decl->as.const_stmt.name;
                    } else if (decl->type == STMT_EXTERN_FN) {
                        name = decl->as.extern_fn.function_name;
                    }

                    if (name) {
                        // Generate assignment to global mangled name (already declared as static)
                        char mangled[CODEGEN_MANGLED_NAME_SIZE];
                        snprintf(mangled, sizeof(mangled), "%s%s", ctx->current_module->module_prefix, name);

                        if (decl->type == STMT_LET && decl->as.let.value) {
                            // Check if it's a function definition
                            if (decl->as.let.value->type == EXPR_FUNCTION) {
                                Expr *func = decl->as.let.value;
                                int num_required = count_required_params(func->as.function.param_defaults, func->as.function.num_params);
                                codegen_writeln(ctx, "%s = hml_val_function((void*)%sfn_%s, %d, %d, %d);",
                                              mangled, ctx->current_module->module_prefix, name,
                                              func->as.function.num_params, num_required, func->as.function.is_async);
                            } else {
                                char *val = codegen_expr(ctx, decl->as.let.value);
                                codegen_writeln(ctx, "%s = %s;", mangled, val);
                                free(val);
                            }
                        } else if (decl->type == STMT_CONST && decl->as.const_stmt.value) {
                            char *val = codegen_expr(ctx, decl->as.const_stmt.value);
                            codegen_writeln(ctx, "%s = %s;", mangled, val);
                            free(val);
                        } else if (decl->type == STMT_EXTERN_FN) {
                            // Export extern function - assign wrapper function to module global
                            int num_params = decl->as.extern_fn.num_params;
                            codegen_writeln(ctx, "%s = hml_val_function((void*)hml_fn_%s, %d, %d, 0);",
                                          mangled, name, num_params, num_params);
                        }
                    } else {
                        // For non-variable exports, just generate the declaration
                        codegen_stmt(ctx, decl);
                    }
                } else {
                    // Not in module context, just generate the declaration
                    codegen_stmt(ctx, decl);
                }
            } else if (stmt->as.export_stmt.is_reexport) {
                // Re-export: export { a, b } from "other"
                // This is handled during module compilation, no runtime code needed
                codegen_writeln(ctx, "// Re-export from \"%s\" (handled at compile time)", stmt->as.export_stmt.module_path);
            } else {
                // Export list: export { a, b }
                // This just marks existing variables as exported, no code needed
                codegen_writeln(ctx, "// Export list (handled at compile time)");
            }
            break;
        }

        case STMT_IMPORT_FFI:
            // Load the FFI library - assigns to global _ffi_lib
            codegen_writeln(ctx, "_ffi_lib = hml_ffi_load(\"%s\");", stmt->as.import_ffi.library_path);
            break;

        case STMT_EXTERN_FN:
            // Wrapper function is generated in codegen_program, nothing to do here
            break;

        default:
            codegen_error(ctx, stmt->line, "unsupported statement type %d", stmt->type);
            break;
    }
}
