/*
 * Hemlock Code Generator - Statement Code Generation
 *
 * Handles code generation for all statement types.
 */

#include "codegen_internal.h"

// ========== STATEMENT CODE GENERATION ==========

static char *codegen_emit_condition_bool(CodegenContext *ctx, Expr *condition) {
    char *cond_val = codegen_expr(ctx, condition);
    char *cond_bool = codegen_temp(ctx);
    codegen_writeln(ctx, "int %s = hml_to_bool(%s);", cond_bool, cond_val);
    codegen_writeln(ctx, "hml_release(&%s);", cond_val);
    free(cond_val);
    return cond_bool;
}

void codegen_stmt(CodegenContext *ctx, Stmt *stmt) {
    // Record the current source line so runtime errors can report it (and the
    // matching source snippet), mirroring the interpreter's per-statement
    // current_line. Limited to executable statement kinds — declarations and
    // type/definition statements don't emit inline code in this context.
    if (stmt->line > 0) {
        switch (stmt->type) {
            case STMT_LET:
            case STMT_CONST:
            case STMT_EXPR:
            case STMT_RETURN:
            case STMT_THROW:
                codegen_writeln(ctx, "hml_error_line = %d;", stmt->line);
                break;
            default:
                break;
        }
    }

    switch (stmt->type) {
        case STMT_LET: {
            codegen_add_local(ctx, stmt->as.let.name);
            // Add to current scope for proper lexical scoping
            if (ctx->current_scope) {
                scope_add_var(ctx->current_scope, stmt->as.let.name);
            }
            // Record primitive annotations so reassignments enforce them with
            // the same hml_convert_to_type the declaration uses (matches the
            // interpreter's runtime annotation enforcement).
            {
                const char *annot_hml_type = codegen_annot_primitive_name(stmt->as.let.type_annotation);
                if (annot_hml_type) {
                    codegen_set_local_annot(ctx, stmt->as.let.name, annot_hml_type);
                }
            }
            char *safe_name = codegen_sanitize_ident(stmt->as.let.name);

            // OPTIMIZATION: Check if this typed variable can be unboxed
            // Unboxed variables use native C types for 5-10x faster arithmetic
            // IMPORTANT: Skip unboxing for main-level variables - they're pre-declared as HmlValue
            if (ctx->optimize && ctx->type_ctx && stmt->as.let.type_annotation && stmt->as.let.value &&
                !codegen_is_main_var(ctx, stmt->as.let.name)) {
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
                            codegen_mark_local_no_cleanup(ctx, stmt->as.let.name);
                            break;
                        }
                    }
                }
            }

            // OPTIMIZATION: Check if untyped variable can be unboxed via type inference
            // This handles patterns like: let x = 42; let sum = a + b;
            // IMPORTANT: Skip unboxing for main-level variables - they're pre-declared as HmlValue
            if (ctx->optimize && ctx->type_ctx && !stmt->as.let.type_annotation && stmt->as.let.value &&
                !codegen_is_main_var(ctx, stmt->as.let.name)) {
                CheckedTypeKind native_type = type_check_get_unboxable(ctx->type_ctx, stmt->as.let.name);
                if (native_type != CHECKED_UNKNOWN) {
                    const char *c_type = checked_type_to_c_type(native_type);
                    const char *unbox_cast = checked_type_to_unbox_cast(native_type);
                    if (c_type && unbox_cast) {
                        // Generate unboxed variable with inferred native C type
                        char *value = codegen_expr(ctx, stmt->as.let.value);
                        codegen_writeln(ctx, "%s %s = %s(%s);", c_type, safe_name, unbox_cast, value);
                        codegen_writeln(ctx, "hml_release(&%s);", value);
                        free(value);
                        free(safe_name);
                        codegen_mark_local_no_cleanup(ctx, stmt->as.let.name);
                        break;
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
                // But first check if it's a type alias - if so, use the resolved type
                int is_type_alias = 0;
                CheckedType *resolved_type = NULL;
                if (stmt->as.let.type_annotation &&
                    stmt->as.let.type_annotation->kind == TYPE_CUSTOM_OBJECT &&
                    stmt->as.let.type_annotation->type_name &&
                    ctx->type_ctx) {
                    TypeAliasDef *alias = type_check_lookup_type_alias(ctx->type_ctx, stmt->as.let.type_annotation->type_name);
                    if (alias && alias->aliased_type) {
                        is_type_alias = 1;
                        resolved_type = alias->aliased_type;
                    }
                }
                if (is_type_alias && resolved_type) {
                    // Type alias resolved - generate type check for the underlying type
                    const char *hml_type = NULL;
                    int handled = 0;
                    switch (resolved_type->kind) {
                        case CHECKED_I8: hml_type = "HML_VAL_I8"; break;
                        case CHECKED_I16: hml_type = "HML_VAL_I16"; break;
                        case CHECKED_I32: hml_type = "HML_VAL_I32"; break;
                        case CHECKED_I64: hml_type = "HML_VAL_I64"; break;
                        case CHECKED_U8: hml_type = "HML_VAL_U8"; break;
                        case CHECKED_U16: hml_type = "HML_VAL_U16"; break;
                        case CHECKED_U32: hml_type = "HML_VAL_U32"; break;
                        case CHECKED_U64: hml_type = "HML_VAL_U64"; break;
                        case CHECKED_F32: hml_type = "HML_VAL_F32"; break;
                        case CHECKED_F64: hml_type = "HML_VAL_F64"; break;
                        case CHECKED_BOOL: hml_type = "HML_VAL_BOOL"; break;
                        case CHECKED_STRING: hml_type = "HML_VAL_STRING"; break;
                        case CHECKED_RUNE: hml_type = "HML_VAL_RUNE"; break;
                        case CHECKED_PTR: hml_type = "HML_VAL_PTR"; break;
                        case CHECKED_BUFFER: hml_type = "HML_VAL_BUFFER"; break;
                        case CHECKED_NULL: hml_type = "HML_VAL_NULL"; break;
                        case CHECKED_FUNCTION: hml_type = "HML_VAL_FUNCTION"; break;
                        case CHECKED_ARRAY: hml_type = "HML_VAL_ARRAY"; break;
                        case CHECKED_OBJECT: hml_type = "HML_VAL_OBJECT"; break;
                        case CHECKED_CUSTOM:
                            // Custom type alias - validate as object type
                            if (resolved_type->type_name) {
                                codegen_writeln(ctx, "HmlValue %s = hml_validate_object_type(%s, \"%s\");",
                                              safe_name, value, resolved_type->type_name);
                                handled = 1;
                            }
                            break;
                        default:
                            hml_type = NULL;
                            break;
                    }
                    if (hml_type) {
                        codegen_writeln(ctx, "HmlValue %s = hml_convert_to_type(%s, %s);",
                                      safe_name, value, hml_type);
                    } else if (!handled) {
                        // No specific type check needed, just assign
                        codegen_writeln(ctx, "HmlValue %s = %s;", safe_name, value);
                    }
                } else if (stmt->as.let.type_annotation &&
                    stmt->as.let.type_annotation->kind == TYPE_CUSTOM_OBJECT &&
                    stmt->as.let.type_annotation->type_name) {
                    // Check if this is an enum type (use enum validation) or object type
                    int is_enum = 0;
                    if (ctx->type_ctx) {
                        EnumDef *enum_def = type_check_lookup_enum(ctx->type_ctx,
                            stmt->as.let.type_annotation->type_name);
                        is_enum = (enum_def != NULL);
                    }
                    if (is_enum) {
                        codegen_writeln(ctx, "HmlValue %s = hml_validate_enum_value(%s, \"%s\");",
                                      safe_name, value, stmt->as.let.type_annotation->type_name);
                    } else {
                        codegen_writeln(ctx, "HmlValue %s = hml_validate_object_type(%s, \"%s\");",
                                      safe_name, value, stmt->as.let.type_annotation->type_name);
                    }
                } else if (stmt->as.let.type_annotation &&
                           stmt->as.let.type_annotation->kind == TYPE_COMPOUND) {
                    // Compound type: validate against each constituent type
                    Type *compound = stmt->as.let.type_annotation;
                    codegen_writeln(ctx, "HmlValue %s = %s;", safe_name, value);
                    for (int i = 0; i < compound->num_compound_types; i++) {
                        Type *constituent = compound->compound_types[i];
                        if (constituent->kind == TYPE_CUSTOM_OBJECT && constituent->type_name) {
                            codegen_writeln(ctx, "%s = hml_validate_object_type(%s, \"%s\");",
                                          safe_name, safe_name, constituent->type_name);
                        }
                    }
                } else if (stmt->as.let.type_annotation &&
                           stmt->as.let.type_annotation->kind == TYPE_ARRAY) {
                    // Typed array: let arr: array<type> = [...]
                    Type *elem_type = stmt->as.let.type_annotation->element_type;
                    if (elem_type && elem_type->kind == TYPE_CUSTOM_OBJECT && elem_type->type_name) {
                        // Validate each element against the registered object type so
                        // optional fields get auto-filled (parity with interpreter).
                        codegen_writeln(ctx, "HmlValue %s = hml_validate_typed_array_object(%s, \"%s\");",
                                      safe_name, value, elem_type->type_name);
                    } else {
                        const char *hml_type = elem_type ? type_kind_to_hml_val(elem_type->kind) : NULL;
                        if (!hml_type) hml_type = "HML_VAL_NULL";
                        codegen_writeln(ctx, "HmlValue %s = hml_validate_typed_array(%s, %s);",
                                      safe_name, value, hml_type);
                    }
                } else if (stmt->as.let.type_annotation) {
                    // Primitive type annotation: let x: i64 = 0;
                    // Convert value to the annotated type with range checking
                    const char *hml_type = type_kind_to_hml_val(stmt->as.let.type_annotation->kind);
                    if (hml_type) {
                        if (stmt->as.let.type_annotation->nullable) {
                            // Nullable annotation: skip conversion if value is null
                            codegen_writeln(ctx, "HmlValue %s;", safe_name);
                            codegen_writeln(ctx, "if (%s.type == HML_VAL_NULL) {", value);
                            codegen_writeln(ctx, "    %s = %s;", safe_name, value);
                            codegen_writeln(ctx, "} else {");
                            codegen_writeln(ctx, "    %s = hml_convert_to_type(%s, %s);",
                                          safe_name, value, hml_type);
                            codegen_writeln(ctx, "}");
                        } else {
                            codegen_writeln(ctx, "HmlValue %s = hml_convert_to_type(%s, %s);",
                                          safe_name, value, hml_type);
                        }
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
            // If this local is captured by a closure/defer somewhere in the
            // function, seed the shared environment (the source of truth for
            // captured locals) with its initial value
            codegen_sync_captured_var(ctx, stmt->as.let.name, safe_name);
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
                } else if (stmt->as.if_stmt.condition->as.number.is_u64) {
                    is_truthy = stmt->as.if_stmt.condition->as.number.uint_value != 0;
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

            char *cond_bool = codegen_emit_condition_bool(ctx, stmt->as.if_stmt.condition);
            // @likely / @unlikely branch hints -> __builtin_expect
            int annc = stmt->as.if_stmt.annotation_count;
            Annotation **anns = stmt->as.if_stmt.annotations;
            if (annc > 0 && annotation_has(anns, annc, "likely")) {
                codegen_writeln(ctx, "if (__builtin_expect(!!(%s), 1)) {", cond_bool);
            } else if (annc > 0 && annotation_has(anns, annc, "unlikely")) {
                codegen_writeln(ctx, "if (__builtin_expect(!!(%s), 0)) {", cond_bool);
            } else {
                codegen_writeln(ctx, "if (%s) {", cond_bool);
            }
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
            free(cond_bool);
            break;
        }

        case STMT_WHILE: {
            ctx->loop_depth++;
            const char *loop_label = stmt->as.while_stmt.label;
            char *break_label = NULL;
            char *continue_label = NULL;

            if (loop_label) {
                break_label = codegen_label(ctx);
                continue_label = codegen_label(ctx);
                codegen_push_loop_label(ctx, loop_label, break_label, continue_label);
            }

            // OPTIMIZATION: At top level, shadow main variables with native locals
            // for accumulators and counters detected by the type checker
            int num_unboxed_main = 0;
            char **unboxed_main_names = NULL;
            CheckedTypeKind *unboxed_main_types = NULL;

            if (ctx->optimize && ctx->type_ctx && !ctx->in_function && !ctx->current_module) {
                // Snapshot existing unboxable var names before analysis
                int num_existing = 0;
                char **existing_names = NULL;
                for (UnboxableVar *u = ctx->type_ctx->unboxable_vars; u; u = u->next) {
                    num_existing++;
                }
                if (num_existing > 0) {
                    existing_names = malloc(num_existing * sizeof(char*));
                    int ei = 0;
                    for (UnboxableVar *u = ctx->type_ctx->unboxable_vars; u; u = u->next) {
                        existing_names[ei++] = u->name;  // borrow, don't strdup
                    }
                }

                // Run accumulator analysis for this while loop
                type_check_analyze_while_loop(ctx->type_ctx, stmt);

                // Collect NEWLY added unboxable main variables only
                // Use a temporary array with max capacity
                int max_new = 0;
                for (UnboxableVar *u = ctx->type_ctx->unboxable_vars; u; u = u->next) max_new++;
                char **new_names = malloc((max_new > 0 ? max_new : 1) * sizeof(char*));
                CheckedTypeKind *new_types = malloc((max_new > 0 ? max_new : 1) * sizeof(CheckedTypeKind));

                for (UnboxableVar *u = ctx->type_ctx->unboxable_vars; u; u = u->next) {
                    if (!codegen_is_main_var(ctx, u->name) || u->native_type == CHECKED_UNKNOWN) continue;
                    int was_existing = 0;
                    for (int e = 0; e < num_existing; e++) {
                        if (strcmp(existing_names[e], u->name) == 0) { was_existing = 1; break; }
                    }
                    if (!was_existing) {
                        new_names[num_unboxed_main] = strdup(u->name);
                        new_types[num_unboxed_main] = u->native_type;
                        num_unboxed_main++;
                    }
                }
                free(existing_names);

                if (num_unboxed_main > 0) {
                    unboxed_main_names = new_names;
                    unboxed_main_types = new_types;

                    // Generate scope block and native local declarations
                    codegen_push_scope(ctx);
                    codegen_writeln(ctx, "{ // while-loop unboxing");
                    codegen_indent_inc(ctx);
                    for (int i = 0; i < num_unboxed_main; i++) {
                        const char *c_type = checked_type_to_c_type(unboxed_main_types[i]);
                        const char *unbox_cast = checked_type_to_unbox_cast(unboxed_main_types[i]);
                        char *safe_name = codegen_sanitize_ident(unboxed_main_names[i]);
                        if (c_type && unbox_cast) {
                            codegen_writeln(ctx, "%s %s = %s(_main_%s);",
                                          c_type, safe_name, unbox_cast, unboxed_main_names[i]);
                            codegen_add_local(ctx, unboxed_main_names[i]);
                            codegen_mark_local_no_cleanup(ctx, unboxed_main_names[i]);
                        }
                        free(safe_name);
                    }
                } else {
                    free(new_names);
                    free(new_types);
                }
            }

            codegen_emit_loop_pragmas(ctx, stmt->as.while_stmt.annotations,
                                      stmt->as.while_stmt.annotation_count);
            codegen_writeln(ctx, "while (1) {");
            codegen_indent_inc(ctx);

            if (loop_label) {
                codegen_writeln(ctx, "%s:;", continue_label);
            }

            char *cond_bool = codegen_emit_condition_bool(ctx, stmt->as.while_stmt.condition);
            codegen_writeln(ctx, "if (!%s) { break; }", cond_bool);
            codegen_push_loop_body(ctx);
            codegen_stmt(ctx, stmt->as.while_stmt.body);
            codegen_pop_loop_body(ctx);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");

            // Write back unboxed locals to main variables
            if (num_unboxed_main > 0) {
                for (int i = 0; i < num_unboxed_main; i++) {
                    const char *box_func = checked_type_to_box_func(unboxed_main_types[i]);
                    char *safe_name = codegen_sanitize_ident(unboxed_main_names[i]);
                    if (box_func) {
                        codegen_writeln(ctx, "hml_release(&_main_%s);", unboxed_main_names[i]);
                        codegen_writeln(ctx, "_main_%s = %s(%s);",
                                      unboxed_main_names[i], box_func, safe_name);
                    }
                    codegen_remove_local(ctx, unboxed_main_names[i]);
                    type_check_clear_unboxable(ctx->type_ctx, unboxed_main_names[i]);
                    free(safe_name);
                    free(unboxed_main_names[i]);
                }
                free(unboxed_main_names);
                free(unboxed_main_types);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
                codegen_pop_scope(ctx);
            }

            if (loop_label) {
                codegen_writeln(ctx, "%s:;", break_label);
                codegen_pop_loop_label(ctx);
                free(break_label);
                free(continue_label);
            }

            ctx->loop_depth--;
            free(cond_bool);
            break;
        }

        case STMT_LOOP: {
            ctx->loop_depth++;
            const char *loop_label = stmt->as.loop_stmt.label;
            char *break_label = NULL;
            char *continue_label = NULL;

            if (loop_label) {
                break_label = codegen_label(ctx);
                continue_label = codegen_label(ctx);
                codegen_push_loop_label(ctx, loop_label, break_label, continue_label);
            }

            codegen_emit_loop_pragmas(ctx, stmt->as.loop_stmt.annotations,
                                      stmt->as.loop_stmt.annotation_count);
            codegen_writeln(ctx, "while (1) {");
            codegen_indent_inc(ctx);

            if (loop_label) {
                codegen_writeln(ctx, "%s:;", continue_label);
            }

            codegen_push_loop_body(ctx);
            codegen_stmt(ctx, stmt->as.loop_stmt.body);
            codegen_pop_loop_body(ctx);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");

            if (loop_label) {
                codegen_writeln(ctx, "%s:;", break_label);
                codegen_pop_loop_label(ctx);
                free(break_label);
                free(continue_label);
            }

            ctx->loop_depth--;
            break;
        }

        case STMT_FOR: {
            ctx->loop_depth++;
            const char *loop_label = stmt->as.for_loop.label;
            Annotation **for_ann = stmt->as.for_loop.annotations;
            int for_ann_count = stmt->as.for_loop.annotation_count;
            char *loop_break_label = NULL;
            char *loop_continue_label = NULL;

            if (loop_label) {
                loop_break_label = codegen_label(ctx);
                loop_continue_label = codegen_label(ctx);
                codegen_push_loop_label(ctx, loop_label, loop_break_label, loop_continue_label);
            }

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
                int opt_for_locals_start = ctx->num_locals;
                codegen_push_scope(ctx);
                codegen_writeln(ctx, "{");
                codegen_indent_inc(ctx);

                // Get initial value
                Stmt *init = stmt->as.for_loop.initializer;
                Expr *init_expr = init->as.let.value;
                char *safe_name = codegen_sanitize_ident(counter_name);

                // Declare native counter
                if (init_expr && init_expr->type == EXPR_NUMBER &&
                    !init_expr->as.number.is_float && !init_expr->as.number.is_u64) {
                    // Constant integer initializer - fully optimized
                    int32_t init_val = (int32_t)init_expr->as.number.int_value;
                    codegen_writeln(ctx, "int32_t %s = %d;", safe_name, init_val);
                } else {
                    // Dynamic initializer (e.g. `count - 1`) - evaluate at runtime.
                    // This path is reachable when a stale unboxable mark from an
                    // earlier same-named loop counter causes the optimized path
                    // to be selected even though analyze_for_loop declined to
                    // mark this loop's counter.
                    char *init_val_str = init_expr ? codegen_expr(ctx, init_expr) : NULL;
                    if (init_val_str) {
                        codegen_writeln(ctx, "int32_t %s = hml_to_i32(%s);", safe_name, init_val_str);
                        codegen_writeln(ctx, "hml_release_if_needed(&%s);", init_val_str);
                        free(init_val_str);
                    } else {
                        codegen_writeln(ctx, "int32_t %s = 0;", safe_name);
                    }
                }
                codegen_add_local(ctx, counter_name);
                codegen_mark_local_no_cleanup(ctx, counter_name);
                if (ctx->current_scope) {
                    scope_add_var(ctx->current_scope, counter_name);
                }

                // Create continue label (use loop_continue_label if this is a labeled loop)
                char *continue_label;
                if (loop_label) {
                    continue_label = strdup(loop_continue_label);
                } else {
                    continue_label = codegen_label(ctx);
                }
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
                            // Constant bound. When the literal fits in int32 we
                            // compare directly; otherwise promote the i32 counter
                            // to int64 so the bound isn't silently truncated. This
                            // keeps parity with the interpreter, which promotes
                            // both operands of the comparison.
                            int64_t bound = bound_expr->as.number.int_value;
                            codegen_emit_loop_pragmas(ctx, for_ann, for_ann_count);
                            if (bound >= INT32_MIN && bound <= INT32_MAX) {
                                if (counter_on_left) {
                                    codegen_writeln(ctx, "while (%s %s %d) {", safe_name, op_str, (int32_t)bound);
                                } else {
                                    codegen_writeln(ctx, "while (%d %s %s) {", (int32_t)bound, op_str, safe_name);
                                }
                            } else if (counter_on_left) {
                                codegen_writeln(ctx, "while ((int64_t)%s %s %" PRId64 "LL) {", safe_name, op_str, bound);
                            } else {
                                codegen_writeln(ctx, "while (%" PRId64 "LL %s (int64_t)%s) {", bound, op_str, safe_name);
                            }
                        } else if (bound_expr) {
                            // Dynamic bound - evaluate once before loop. Convert at
                            // int64 width and promote the i32 counter so an i64
                            // bound whose runtime value exceeds int32 range is not
                            // truncated (parity with the interpreter).
                            char *bound_val = codegen_expr(ctx, bound_expr);
                            codegen_writeln(ctx, "int64_t _bound = hml_to_i64(%s);", bound_val);
                            codegen_writeln(ctx, "hml_release_if_needed(&%s);", bound_val);
                            codegen_emit_loop_pragmas(ctx, for_ann, for_ann_count);
                            if (counter_on_left) {
                                codegen_writeln(ctx, "while ((int64_t)%s %s _bound) {", safe_name, op_str);
                            } else {
                                codegen_writeln(ctx, "while (_bound %s (int64_t)%s) {", op_str, safe_name);
                            }
                            free(bound_val);
                        } else {
                            // Fallback
                            codegen_emit_loop_pragmas(ctx, for_ann, for_ann_count);
                            codegen_writeln(ctx, "while (1) {");
                        }
                    } else {
                        codegen_emit_loop_pragmas(ctx, for_ann, for_ann_count);
                        codegen_writeln(ctx, "while (1) {");
                    }
                } else {
                    codegen_emit_loop_pragmas(ctx, for_ann, for_ann_count);
                    codegen_writeln(ctx, "while (1) {");
                }

                codegen_indent_inc(ctx);

                // Body - but we need to handle references to the counter specially
                // The body expects an HmlValue, so we create a temporary when needed
                codegen_push_loop_body(ctx);
                codegen_stmt(ctx, stmt->as.for_loop.body);
                codegen_pop_loop_body(ctx);

                // Continue label
                codegen_writeln(ctx, "%s:;", continue_label);

                // Optimized increment
                Expr *inc = stmt->as.for_loop.increment;
                if (inc) {
                    if (inc->type == EXPR_POSTFIX_INC || inc->type == EXPR_PREFIX_INC) {
                        // Defined wraparound (via unsigned) so an overflowing i32
                        // counter wraps like the interpreter's `i++` instead of
                        // being undefined behavior the optimizer may exploit.
                        codegen_writeln(ctx, "%s = (int32_t)((uint32_t)%s + 1u);", safe_name, safe_name);
                    } else if (inc->type == EXPR_POSTFIX_DEC || inc->type == EXPR_PREFIX_DEC) {
                        codegen_writeln(ctx, "%s = (int32_t)((uint32_t)%s - 1u);", safe_name, safe_name);
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
                // Restore num_locals (counter is out of C scope)
                ctx->num_locals = opt_for_locals_start;
                codegen_pop_scope(ctx);
                codegen_pop_for_continue(ctx);
                free(continue_label);
                free(safe_name);
            } else {
                // STANDARD: Generate loop with boxed HmlValue counter
                // Clear any unboxable mark since we're NOT unboxing this counter
                if (ctx->type_ctx && counter_name) {
                    type_check_clear_unboxable(ctx->type_ctx, counter_name);
                }
                int for_locals_start = ctx->num_locals;
                codegen_push_scope(ctx);
                codegen_writeln(ctx, "{");
                codegen_indent_inc(ctx);
                // Initializer
                if (stmt->as.for_loop.initializer) {
                    codegen_stmt(ctx, stmt->as.for_loop.initializer);
                }
                // Create continue label for this for loop (continue jumps here, before increment)
                // Use loop_continue_label if this is a labeled loop
                char *continue_label;
                if (loop_label) {
                    continue_label = strdup(loop_continue_label);
                } else {
                    continue_label = codegen_label(ctx);
                }
                codegen_push_for_continue(ctx, continue_label);

                codegen_emit_loop_pragmas(ctx, for_ann, for_ann_count);
                codegen_writeln(ctx, "while (1) {");
                codegen_indent_inc(ctx);
                // Condition
                if (stmt->as.for_loop.condition) {
                    char *cond_bool = codegen_emit_condition_bool(ctx, stmt->as.for_loop.condition);
                    codegen_writeln(ctx, "if (!%s) { break; }", cond_bool);
                    free(cond_bool);
                }
                // Body
                codegen_push_loop_body(ctx);
                codegen_stmt(ctx, stmt->as.for_loop.body);
                codegen_pop_loop_body(ctx);
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
                // Release for-loop locals (counter, etc.) before closing wrapper
                for (int i = for_locals_start; i < ctx->num_locals; i++) {
                    if (!ctx->local_needs_cleanup[i]) continue;
                    if (!ctx->local_vars[i]) continue;
                    char *safe = codegen_sanitize_ident(ctx->local_vars[i]);
                    codegen_writeln(ctx, "hml_release(&%s);", safe);
                    free(safe);
                }
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
                // Restore num_locals so outer scope doesn't see for-loop vars
                ctx->num_locals = for_locals_start;
                codegen_pop_scope(ctx);
                codegen_pop_for_continue(ctx);
                free(continue_label);
            }

            if (loop_label) {
                codegen_writeln(ctx, "%s:;", loop_break_label);
                codegen_pop_loop_label(ctx);
                free(loop_break_label);
                free(loop_continue_label);
            }

            ctx->loop_depth--;
            break;
        }

        case STMT_FOR_IN: {
            // Generate for-in loop for arrays, objects, or strings
            // for (let val in iterable) or for (let key, val in iterable)
            ctx->loop_depth++;
            const char *loop_label = stmt->as.for_in.label;
            char *loop_break_label = NULL;
            char *loop_continue_label = NULL;

            if (loop_label) {
                loop_break_label = codegen_label(ctx);
                loop_continue_label = codegen_label(ctx);
                codegen_push_loop_label(ctx, loop_label, loop_break_label, loop_continue_label);
            }

            // Push a scope for the for-in loop so loop variables shadow outer vars
            int forin_locals_start = ctx->num_locals;
            codegen_push_scope(ctx);

            codegen_writeln(ctx, "{");
            codegen_indent_inc(ctx);

            // Create continue label for this for-in loop (continue jumps here, before increment)
            // Use loop_continue_label if this is a labeled loop
            char *continue_label;
            if (loop_label) {
                continue_label = strdup(loop_continue_label);
            } else {
                continue_label = codegen_label(ctx);
            }
            codegen_push_for_continue(ctx, continue_label);

            // Evaluate the iterable.
            //
            // codegen_expr ALWAYS hands back an owned (+1) temporary
            // for every iterable shape: idents/index/member get an
            // hml_retain_if_needed'd copy, calls return their owned
            // result, literals are freshly built. That +1 is exactly
            // the reference the loop needs — it keeps the collection
            // alive for the whole loop even if the body reassigns the
            // source variable (the temp is an independent reference) —
            // and the single hml_release(&iter_val) at loop end
            // balances it.
            //
            // The for-in must therefore NOT add its own retain. It
            // used to (`hml_retain(&iter_val)` unconditionally), which
            // is a second retain against codegen_expr's already-owned
            // temp with only one matching release: every for-in over a
            // non-call iterable leaked the whole collection + contents
            // on each execution (e.g. `for tk in ts` in a request
            // loop bled MB/s; a Witchgrid CP grew GB). 2.4.7 fixed
            // only the EXPR_CALL subcase; the leak for idents/literals/
            // index/member remained. Dropping the retain entirely is
            // the correct general fix and subsumes the 2.4.7 special
            // case.
            char *iter_val = codegen_expr(ctx, stmt->as.for_in.iterable);

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

            // The element count is re-evaluated every iteration so mutation
            // of the iterable during the loop is observed (the interpreter
            // checks the live length each pass). Cheap: array/object lengths
            // are field reads and strings cache their codepoint count.
            codegen_emit_loop_pragmas(ctx, stmt->as.for_in.annotations,
                                      stmt->as.for_in.annotation_count);
            codegen_writeln(ctx, "while (%s < hml_iter_length(%s)) {", idx_var, iter_val);
            codegen_indent_inc(ctx);

            // Create key and value variables based on iterable type
            // Sanitize variable names to avoid C keyword conflicts
            char *safe_key_var = stmt->as.for_in.key_var ? codegen_sanitize_ident(stmt->as.for_in.key_var) : NULL;
            char *safe_value_var = codegen_sanitize_ident(stmt->as.for_in.value_var);

            // For-in loop variables are always emitted as boxed HmlValue.
            // Clear any stale unboxable mark left by a prior same-named variable
            // (e.g. a previous `for (let i = 0; ...)` counter), otherwise
            // codegen_expr_ident would treat this variable as a native C
            // primitive and emit hml_val_i32(x) on an HmlValue.
            if (ctx->type_ctx) {
                if (stmt->as.for_in.key_var) {
                    type_check_clear_unboxable(ctx->type_ctx, stmt->as.for_in.key_var);
                }
                type_check_clear_unboxable(ctx->type_ctx, stmt->as.for_in.value_var);
            }

            if (stmt->as.for_in.key_var) {
                codegen_writeln(ctx, "HmlValue %s;", safe_key_var);
                codegen_add_local(ctx, stmt->as.for_in.key_var);
                codegen_mark_local_no_cleanup(ctx, stmt->as.for_in.key_var);
                if (ctx->current_scope) {
                    scope_add_var(ctx->current_scope, stmt->as.for_in.key_var);
                }
            }
            codegen_writeln(ctx, "HmlValue %s;", safe_value_var);
            codegen_add_local(ctx, stmt->as.for_in.value_var);
            codegen_mark_local_no_cleanup(ctx, stmt->as.for_in.value_var);
            if (ctx->current_scope) {
                scope_add_var(ctx->current_scope, stmt->as.for_in.value_var);
            }

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

            // Keep the shared environment current for captured loop variables
            if (stmt->as.for_in.key_var) {
                codegen_sync_captured_var(ctx, stmt->as.for_in.key_var, safe_key_var);
            }
            codegen_sync_captured_var(ctx, stmt->as.for_in.value_var, safe_value_var);

            // Generate body
            codegen_push_loop_body(ctx);
            codegen_stmt(ctx, stmt->as.for_in.body);
            codegen_pop_loop_body(ctx);

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

            // Cleanup: release the iterable. Paired with the retain
            // emitted right after it was evaluated (see the gated
            // hml_retain below the codegen_expr call). For a borrowed
            // iterable that pair is net-zero; for an owned call
            // temporary the retain is skipped so this release consumes
            // the call's original +1 instead of leaking it.
            codegen_writeln(ctx, "hml_release(&%s);", iter_val);

            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            // Restore num_locals (for-in vars are out of C scope)
            ctx->num_locals = forin_locals_start;
            codegen_pop_for_continue(ctx);
            codegen_pop_scope(ctx);
            free(continue_label);

            if (loop_label) {
                codegen_writeln(ctx, "%s:;", loop_break_label);
                codegen_pop_loop_label(ctx);
                free(loop_break_label);
                free(loop_continue_label);
            }

            ctx->loop_depth--;

            free(iter_val);
            free(idx_var);
            break;
        }

        case STMT_BLOCK: {
            // Push a new scope for proper lexical scoping in blocks
            codegen_push_scope(ctx);
            int block_locals_start = ctx->num_locals;
            codegen_writeln(ctx, "{");
            codegen_indent_inc(ctx);

            // OPTIMIZATION: Dead code elimination
            // Skip statements after return, throw, break, continue
            // Proof: Control flow terminators make subsequent code unreachable
            int block_terminated = 0;
            for (int i = 0; i < stmt->as.block.count; i++) {
                codegen_stmt(ctx, stmt->as.block.statements[i]);

                // Check if this statement terminates control flow
                if (ctx->optimize) {
                    Stmt *s = stmt->as.block.statements[i];
                    if (s->type == STMT_RETURN || s->type == STMT_THROW ||
                        s->type == STMT_BREAK || s->type == STMT_CONTINUE) {
                        block_terminated = 1;
                        break;
                    }
                }
            }

            // Release block-scoped boxed locals before exiting the C block.
            // Skip if terminated (return/break/continue already handled cleanup).
            if (!block_terminated) {
                for (int i = block_locals_start; i < ctx->num_locals; i++) {
                    if (!ctx->local_needs_cleanup[i]) continue;
                    if (!ctx->local_vars[i]) continue;
                    char *safe = codegen_sanitize_ident(ctx->local_vars[i]);
                    codegen_writeln(ctx, "hml_release(&%s);", safe);
                    free(safe);
                }
            }

            // Restore num_locals so outer scopes don't see block-scoped locals.
            // Don't free local_vars strings — they may be referenced by closure
            // capture logic. They'll be freed by funcgen_restore_state at function exit.
            ctx->num_locals = block_locals_start;

            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            codegen_pop_scope(ctx);
            break;
        }

        case STMT_RETURN: {
            // Check if we're inside a try-finally block
            const char *finally_label = codegen_get_finally_label(ctx);
            if (finally_label) {
                // Inside try-finally: save return value and goto finally.
                // The return expression must be evaluated BEFORE popping the
                // exception contexts: if it throws (e.g. `return f();` where
                // f throws), the exception must land in this try's handler so
                // the finally block still runs.
                const char *ret_var = codegen_get_return_value_var(ctx);
                const char *has_ret = codegen_get_has_return_var(ctx);
                if (stmt->as.return_stmt.value) {
                    char *value = codegen_expr(ctx, stmt->as.return_stmt.value);
                    codegen_writeln(ctx, "%s = %s;", ret_var, value);
                    free(value);
                } else {
                    codegen_writeln(ctx, "%s = hml_val_null();", ret_var);
                }
                // Pop every exception context pushed by enclosing try-bodies
                // between this return and the target finally (the natural
                // pops after the try-bodies are skipped by the goto). The
                // count is exactly ctx->try_body_depth at this point: the
                // target try-finally pushes its context BEFORE we increment
                // try_body_depth for its try-body, so its own contribution
                // is included.
                codegen_emit_return_try_pops(ctx);
                codegen_writeln(ctx, "%s = 1;", has_ret);
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
                // Pop any exception contexts pushed by enclosing try-bodies
                // before returning, so a later throw on this thread does not
                // longjmp into this now-defunct stack frame.
                codegen_emit_return_try_pops(ctx);
                // Execute all defers in LIFO order
                codegen_defer_execute_all(ctx);
                // Pop this function's defer unwind context and run its defers
                codegen_emit_defer_exit(ctx);
                // Release body-local variables
                codegen_emit_local_cleanup(ctx, NULL);
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
                    int num_call_args = call_expr->as.call.num_args;

                    // Evaluate new argument values first (before releasing old ones)
                    // Handle both positional and named arguments
                    char **new_arg_vals = NULL;
                    int *arg_provided = NULL;
                    if (num_params > 0) {
                        new_arg_vals = malloc(num_params * sizeof(char*));
                        arg_provided = calloc(num_params, sizeof(int));  // Track which params have values
                        if (!new_arg_vals || !arg_provided) {
                            free(new_arg_vals);
                            free(arg_provided);
                            codegen_error(ctx, 0, "Memory allocation failed for tail call arguments");
                            break;
                        }
                    }

                    // First pass: handle positional arguments (until we hit a named one)
                    int pos_idx = 0;
                    for (int i = 0; i < num_call_args; i++) {
                        if (call_expr->as.call.arg_names && call_expr->as.call.arg_names[i]) {
                            // Named argument - find matching parameter
                            const char *arg_name = call_expr->as.call.arg_names[i];
                            for (int p = 0; p < num_params; p++) {
                                if (strcmp(func->as.function.param_names[p], arg_name) == 0) {
                                    new_arg_vals[p] = codegen_expr(ctx, call_expr->as.call.args[i]);
                                    arg_provided[p] = 1;
                                    break;
                                }
                            }
                        } else {
                            // Positional argument
                            if (pos_idx < num_params) {
                                new_arg_vals[pos_idx] = codegen_expr(ctx, call_expr->as.call.args[i]);
                                arg_provided[pos_idx] = 1;
                                pos_idx++;
                            }
                        }
                    }

                    // Second pass: fill in defaults or null for unprovided parameters
                    for (int i = 0; i < num_params; i++) {
                        if (!arg_provided[i]) {
                            // Check if there's a default value
                            if (func->as.function.param_defaults &&
                                func->as.function.param_defaults[i]) {
                                new_arg_vals[i] = codegen_expr(ctx, func->as.function.param_defaults[i]);
                            } else {
                                new_arg_vals[i] = strdup("hml_val_null()");
                            }
                        }
                    }
                    free(arg_provided);

                    // Release old parameter values and assign new ones
                    for (int i = 0; i < num_params; i++) {
                        char *safe_param = codegen_sanitize_ident(func->as.function.param_names[i]);
                        codegen_writeln(ctx, "hml_release(&%s);", safe_param);
                        codegen_writeln(ctx, "%s = %s;", safe_param, new_arg_vals[i]);
                        free(safe_param);
                        free(new_arg_vals[i]);
                    }
                    free(new_arg_vals);

                    // Release body-local variables before looping back
                    codegen_emit_local_cleanup(ctx, NULL);

                    // Pop any exception contexts pushed by enclosing try-bodies
                    // before jumping back to the function start (otherwise each
                    // tail-call iteration would leak a context and a later
                    // throw could longjmp into a defunct frame).
                    codegen_emit_return_try_pops(ctx);

                    // Check tail-call depth before jumping back
                    if (ctx->stack_check) {
                        codegen_writeln(ctx, "HML_TAIL_CALL_CHECK(_tail_depth);");
                    }
                    // Jump back to the start of the function
                    codegen_writeln(ctx, "goto %s;", ctx->tail_call_label);
                } else if (stmt->as.return_stmt.value) {
                    // Regular return with value
                    // codegen_expr retains the value (via hml_retain_if_needed for idents),
                    // so the returned copy survives local cleanup even if the source is a local
                    char *value = codegen_expr(ctx, stmt->as.return_stmt.value);
                    // Pop exception contexts pushed by enclosing try-bodies
                    // before returning (otherwise the jmp_buf survives past
                    // this stack frame and a later throw will longjmp into
                    // a defunct frame).
                    codegen_emit_return_try_pops(ctx);
                    // Pop this function's defer unwind context and run its defers
                    codegen_emit_defer_exit(ctx);
                    // Release body-local variables
                    codegen_emit_local_cleanup(ctx, NULL);
                    if (ctx->stack_check) {
                        codegen_writeln(ctx, "HML_CALL_EXIT();");
                    }
                    codegen_writeln(ctx, "return %s;", value);
                    free(value);
                } else {
                    // Pop exception contexts pushed by enclosing try-bodies
                    // before returning.
                    codegen_emit_return_try_pops(ctx);
                    // Pop this function's defer unwind context and run its defers
                    codegen_emit_defer_exit(ctx);
                    // Release body-local variables
                    codegen_emit_local_cleanup(ctx, NULL);
                    if (ctx->stack_check) {
                        codegen_writeln(ctx, "HML_CALL_EXIT();");
                    }
                    codegen_writeln(ctx, "return hml_val_null();");
                }
            }
            break;
        }

        case STMT_BREAK: {
            // Check for labeled break first
            const char *label = stmt->as.break_stmt.label;
            if (label) {
                // Labeled break: clean up all locals from here to the target loop
                codegen_emit_labeled_break_cleanup(ctx, label);
                // Pop exception contexts pushed by try-bodies nested inside
                // the target labeled loop.
                codegen_emit_labeled_break_try_pops(ctx, label);
                const char *target = codegen_get_labeled_break(ctx, label);
                if (target) {
                    codegen_writeln(ctx, "goto %s;", target);
                } else {
                    codegen_error(ctx, stmt->line, "Unknown loop label '%s'", label);
                }
            } else {
                // Unlabeled break: clean up block-scoped locals in the loop body
                const char *switch_end = codegen_get_switch_end_label(ctx);
                if (!switch_end) {
                    codegen_emit_break_cleanup(ctx);
                    // Pop exception contexts pushed by try-bodies nested inside
                    // the innermost loop being broken out of.
                    codegen_emit_break_try_pops(ctx);
                }
                // If inside a switch, use goto to exit (so continue still works for loops)
                if (switch_end) {
                    codegen_writeln(ctx, "goto %s;", switch_end);
                } else {
                    codegen_writeln(ctx, "break;");
                }
            }
            break;
        }

        case STMT_CONTINUE: {
            // Check for labeled continue first
            const char *label = stmt->as.continue_stmt.label;
            if (label) {
                // Labeled continue: clean up all locals from here to the target loop
                codegen_emit_labeled_break_cleanup(ctx, label);
                // Pop exception contexts pushed by try-bodies nested inside
                // the target labeled loop.
                codegen_emit_labeled_break_try_pops(ctx, label);
                const char *target = codegen_get_labeled_continue(ctx, label);
                if (target) {
                    codegen_writeln(ctx, "goto %s;", target);
                } else {
                    codegen_error(ctx, stmt->line, "Unknown loop label '%s'", label);
                }
            } else {
                // Unlabeled continue: clean up block-scoped locals in the loop body
                codegen_emit_break_cleanup(ctx);
                // Pop exception contexts pushed by try-bodies nested inside
                // the innermost loop being continued.
                codegen_emit_break_try_pops(ctx);
                // If inside a for loop, use goto to jump to before the increment
                const char *for_continue = codegen_get_for_continue_label(ctx);
                if (for_continue) {
                    codegen_writeln(ctx, "goto %s;", for_continue);
                } else {
                    codegen_writeln(ctx, "continue;");
                }
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

            if (has_finally) {
                // Track exception state so a throw escaping the try body
                // (no catch) or escaping the catch block still runs the
                // finally block before unwinding to the outer handler.
                codegen_writeln(ctx, "int _had_exception = 0;");
                codegen_writeln(ctx, "HmlValue _saved_exception = hml_val_null();");
            }

            codegen_writeln(ctx, "if (setjmp(_ex_ctx->exception_buf) == 0) {");
            codegen_indent_inc(ctx);
            // Track that we are now emitting code inside a try-body whose
            // exception context is still pushed on the runtime stack. Any
            // return/break/continue that escapes the try-body must pop it,
            // otherwise the jmp_buf survives past its stack frame and a
            // subsequent throw will longjmp into a dead frame.
            ctx->try_body_depth++;
            // Try block
            codegen_stmt(ctx, stmt->as.try_stmt.try_block);
            ctx->try_body_depth--;
            if (has_catch) {
                // Pop exception context on the success path (no exception thrown).
                // Emitted here while still inside the if-block, before indent_dec.
                codegen_writeln(ctx, "hml_exception_pop();");
            }
            codegen_indent_dec(ctx);
            if (has_catch) {
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                // Save exception value BEFORE popping the context (pop frees it).
                // Then pop BEFORE executing catch body so that a rethrow (throw e)
                // inside catch propagates to the outer handler instead of
                // re-entering this catch block infinitely.
                char *saved_ex_var = codegen_temp(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_exception_get_value();", saved_ex_var);
                codegen_writeln(ctx, "hml_exception_pop();");
                if (has_finally) {
                    // Guard the catch body with its own exception context so
                    // a throw propagating out of the catch still runs the
                    // finally block below before unwinding (matching the
                    // interpreter). The exception is saved and re-thrown
                    // after the finally block.
                    codegen_writeln(ctx, "HmlExceptionContext *_catch_ctx = hml_exception_push();");
                    codegen_writeln(ctx, "if (setjmp(_catch_ctx->exception_buf) == 0) {");
                    codegen_indent_inc(ctx);
                    // The catch body now runs with an extra live exception
                    // context; any return/break/continue escaping it must
                    // pop that context (same bookkeeping as a try-body).
                    ctx->try_body_depth++;
                }
                // Catch block - declare catch param as shadow var to shadow main vars
                if (stmt->as.try_stmt.catch_param) {
                    char *safe_catch_param = codegen_sanitize_ident(stmt->as.try_stmt.catch_param);
                    codegen_add_shadow(ctx, stmt->as.try_stmt.catch_param);
                    codegen_writeln(ctx, "HmlValue %s = %s;", safe_catch_param, saved_ex_var);
                    codegen_stmt(ctx, stmt->as.try_stmt.catch_block);
                    codegen_writeln(ctx, "hml_release(&%s);", safe_catch_param);
                    // Remove catch param from shadow vars so outer scope variable is used again
                    codegen_remove_shadow(ctx, stmt->as.try_stmt.catch_param);
                    free(safe_catch_param);
                } else {
                    // No catch parameter: release saved exception value and run block
                    codegen_writeln(ctx, "hml_release(&%s);", saved_ex_var);
                    codegen_stmt(ctx, stmt->as.try_stmt.catch_block);
                }
                free(saved_ex_var);
                if (has_finally) {
                    ctx->try_body_depth--;
                    // Catch body completed normally: discard its guard context.
                    codegen_writeln(ctx, "hml_exception_pop();");
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "} else {");
                    codegen_indent_inc(ctx);
                    // Catch body threw: save the exception for re-throw
                    // after the finally block runs.
                    codegen_writeln(ctx, "_had_exception = 1;");
                    codegen_writeln(ctx, "_saved_exception = hml_exception_get_value();");
                    codegen_writeln(ctx, "hml_exception_pop();");
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "}");
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

            // For no-catch cases, pop exception context here before the finally block
            // so that exceptions thrown in finally propagate to the outer handler.
            // For has_catch, the pop was already emitted in both branches above.
            if (!has_catch) {
                codegen_writeln(ctx, "hml_exception_pop();");
            }

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

                // Re-throw the saved exception if the try body threw with no
                // catch, or if the catch block itself threw.
                codegen_writeln(ctx, "if (_had_exception) {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "hml_throw(_saved_exception);");
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");

                // Check if we should return (from a return statement in the try block)
                if (needs_return_tracking) {
                    codegen_writeln(ctx, "if (%s) {", has_return_var);
                    codegen_indent_inc(ctx);
                    // Pop any exception contexts pushed by try-bodies that
                    // still enclose this finally (this try's own context was
                    // already popped at the return site before the goto).
                    codegen_emit_return_try_pops(ctx);
                    // Pop this function's defer unwind context and run its defers
                    codegen_emit_defer_exit(ctx);
                    // Release body-local variables before returning
                    codegen_emit_local_cleanup(ctx, NULL);
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
            char **case_labels = NULL;
            if (num_cases > 0) {
                case_labels = malloc(num_cases * sizeof(char*));
                if (!case_labels) {
                    codegen_error(ctx, 0, "Memory allocation failed for switch case labels");
                    free(expr_val);
                    break;
                }
            }
            for (int i = 0; i < num_cases; i++) {
                case_labels[i] = codegen_label(ctx);
            }
            char *end_label = codegen_label(ctx);

            // Track switch context so break generates goto end_label
            codegen_push_switch(ctx, end_label);

            codegen_writeln(ctx, "{");
            codegen_indent_inc(ctx);

            // Pre-generate all case values to avoid scoping issues
            char **case_vals = NULL;
            if (num_cases > 0) {
                case_vals = malloc(num_cases * sizeof(char*));
                if (!case_vals) {
                    codegen_error(ctx, 0, "Memory allocation failed for switch case values");
                    for (int i = 0; i < num_cases; i++) {
                        free(case_labels[i]);
                    }
                    free(case_labels);
                    free(expr_val);
                    break;
                }
            }
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
            // Wrap the deferred expression in a synthesized zero-arg closure
            // and push it on the runtime defer stack. The closure captures
            // referenced variables through the normal closure machinery, so
            // the expression is evaluated at function exit with the values
            // the variables have THEN - matching the interpreter, which
            // stores the AST plus its environment. (Eagerly evaluating call
            // arguments at registration time diverged: `defer print(x)`
            // printed x's registration-time value.)
            //
            // The synthesized nodes intentionally leak: ClosureInfo keeps a
            // pointer to the function expression until closure bodies are
            // generated at the end of codegen, and the compiler is a
            // short-lived process.
            Stmt *defer_body = calloc(1, sizeof(Stmt));
            Expr *defer_fn = calloc(1, sizeof(Expr));
            if (!defer_body || !defer_fn) {
                free(defer_body);
                free(defer_fn);
                codegen_error(ctx, 0, "Memory allocation failed for defer closure");
                break;
            }
            defer_body->type = STMT_EXPR;
            defer_body->line = stmt->line;
            defer_body->column = stmt->column;
            defer_body->as.expr = stmt->as.defer_stmt.call;
            defer_fn->type = EXPR_FUNCTION;
            defer_fn->line = stmt->line;
            defer_fn->column = stmt->column;
            defer_fn->as.function.body = defer_body;
            // All other function fields stay zero/NULL (no params, not async)

            char *closure_val = codegen_expr(ctx, defer_fn);
            codegen_writeln(ctx, "hml_defer_push_call(%s);", closure_val);
            codegen_writeln(ctx, "hml_release(&%s);", closure_val);
            free(closure_val);
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

            // Track variant values for registration
            int *variant_values = NULL;
            if (stmt->as.enum_decl.num_variants > 0) {
                variant_values = malloc(sizeof(int) * stmt->as.enum_decl.num_variants);
                if (!variant_values) {
                    codegen_error(ctx, 0, "Memory allocation failed for enum variant values");
                    break;
                }
            }
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
                        variant_values[i] = (int)value_expr->as.number.int_value;
                        next_value = variant_values[i] + 1;
                    } else {
                        // Non-literal value, use auto-increment
                        variant_values[i] = next_value;
                        next_value++;
                    }
                    free(val);
                } else {
                    // Auto-incrementing value
                    codegen_writeln(ctx, "hml_object_set_field(%s, \"%s\", hml_val_i32(%d));",
                                  enum_name, variant_name, next_value);
                    variant_values[i] = next_value;
                    next_value++;
                }
            }

            // Register the enum type with its valid variant values
            codegen_writeln(ctx, "{");
            ctx->indent++;
            codegen_writeln(ctx, "static const int32_t _enum_values_%s[] = {", raw_enum_name);
            ctx->indent++;
            for (int i = 0; i < stmt->as.enum_decl.num_variants; i++) {
                if (i < stmt->as.enum_decl.num_variants - 1) {
                    codegen_writeln(ctx, "%d,", variant_values[i]);
                } else {
                    codegen_writeln(ctx, "%d", variant_values[i]);
                }
            }
            ctx->indent--;
            codegen_writeln(ctx, "};");
            codegen_writeln(ctx, "hml_register_enum(\"%s\", _enum_values_%s, %d);",
                          raw_enum_name, raw_enum_name, stmt->as.enum_decl.num_variants);
            ctx->indent--;
            codegen_writeln(ctx, "}");
            free(variant_values);

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
            char *resolved = resolve_module_path(ctx->module_cache->resolver, importer_path,
                stmt->as.import_stmt.module_path);
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
                        // Defense-in-depth: #undef any name that could be a C preprocessor macro,
                        // so even if sanitization were bypassed, the generated C code compiles.
                        char *safe_name = codegen_sanitize_ident(exp->name);
                        codegen_write(ctx, "#undef %s\n", exp->name);
                        codegen_writeln(ctx, "HmlValue %s = %s;", safe_name, exp->mangled_name);
                        codegen_add_local(ctx, exp->name);
                        free(safe_name);
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
                        // Defense-in-depth: #undef any name that could be a C preprocessor macro
                        char *safe_bind = codegen_sanitize_ident(bind_name);
                        codegen_write(ctx, "#undef %s\n", bind_name);
                        codegen_writeln(ctx, "HmlValue %s = %s;", safe_bind, exp->mangled_name);
                        codegen_add_local(ctx, bind_name);
                        free(safe_bind);
                    } else {
                        codegen_error(ctx, stmt->line, "'%s' is not exported from module \"%s\"",
                                     import_name, stmt->as.import_stmt.module_path);
                        char *safe_bind = codegen_sanitize_ident(bind_name);
                        codegen_writeln(ctx, "HmlValue %s = hml_val_null();", safe_bind);
                        codegen_add_local(ctx, bind_name);
                        free(safe_bind);
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

        case STMT_IMPORT_FFI: {
            // Load the FFI library into a per-library global so two imports
            // (e.g. libsqlite3 + libcrypto via @stdlib/sqlite + @stdlib/uuid)
            // don't clobber each other. The matching wrapper for each extern_fn
            // looks up against the same `_ffi_lib_<sanitized>` name, computed
            // from the same library path string.
            char sanitized[256];
            codegen_ffi_sanitize_libname(stmt->as.import_ffi.library_path,
                                         sanitized, sizeof(sanitized));
            // Escape the raw path before embedding it in a C string literal;
            // Windows-style paths (backslashes) or quotes would otherwise emit
            // malformed C (e.g. `\l`, `\f`) or break the literal entirely.
            char *escaped_path = codegen_escape_string(stmt->as.import_ffi.library_path);
            codegen_writeln(ctx, "_ffi_lib_%s = hml_ffi_load_import(\"%s\");",
                            sanitized, escaped_path);
            free(escaped_path);
            break;
        }

        case STMT_EXTERN_FN:
            // Wrapper function is generated in codegen_program, nothing to do here
            break;

        case STMT_TYPE_ALIAS:
            // Type aliases are handled at compile time during type checking
            // No runtime code is generated for them
            break;

        default:
            codegen_error(ctx, stmt->line, "unsupported statement type %d", stmt->type);
            break;
    }
}
