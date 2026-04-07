/*
 * Hemlock Code Generator - Expression Code Generation
 *
 * Handles code generation for all expression types.
 *
 * This file has been refactored to reduce size:
 * - EXPR_IDENT handling is in codegen_expr_ident.c
 */

#include "codegen_internal.h"
#include "codegen_expr_internal.h"


char* codegen_expr(CodegenContext *ctx, Expr *expr) {
    char *result = codegen_temp(ctx);

    switch (expr->type) {
        case EXPR_NUMBER:
            if (expr->as.number.is_float) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_f64(%g);", result, expr->as.number.float_value);
            } else {
                // Check if it fits in i32
                if (expr->as.number.int_value >= INT32_MIN && expr->as.number.int_value <= INT32_MAX) {
                    codegen_writeln(ctx, "HmlValue %s = hml_val_i32(%d);", result, (int32_t)expr->as.number.int_value);
                } else {
                    codegen_writeln(ctx, "HmlValue %s = hml_val_i64(%" PRId64 "L);", result, expr->as.number.int_value);
                }
            }
            break;

        case EXPR_BOOL:
            codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%d);", result, expr->as.boolean);
            break;

        case EXPR_STRING: {
            char *escaped = codegen_escape_string(expr->as.string);
            codegen_writeln(ctx, "HmlValue %s = hml_val_string(\"%s\");", result, escaped);
            free(escaped);
            break;
        }

        case EXPR_RUNE:
            codegen_writeln(ctx, "HmlValue %s = hml_val_rune(%u);", result, expr->as.rune);
            break;

        case EXPR_NULL:
            codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            break;

        case EXPR_IDENT:
            codegen_expr_ident(ctx, expr, result);
            break;

        case EXPR_BINARY:
            return codegen_expr_binary(ctx, expr, result);

        case EXPR_UNARY: {
            // OPTIMIZATION: Double negation elimination
            // Proof: !!x = x (for boolean values), and !!x is equivalent to bool(x)
            // Proof: -(-x) = x (negation is self-inverse)
            if (ctx->optimize) {
                Expr *inner = get_double_negation_inner(expr);
                if (inner) {
                    if (expr->as.unary.op == UNARY_NOT) {
                        // !!x -> convert to bool (hml_to_bool returns int, wrap in bool)
                        char *inner_val = codegen_expr(ctx, inner);
                        codegen_writeln(ctx, "HmlValue %s = hml_val_bool(hml_to_bool(%s));",
                                      result, inner_val);
                        codegen_writeln(ctx, "hml_release_if_needed(&%s);", inner_val);
                        free(inner_val);
                        break;
                    } else if (expr->as.unary.op == UNARY_NEGATE) {
                        // -(-x) -> x (identity)
                        char *inner_val = codegen_expr(ctx, inner);
                        codegen_writeln(ctx, "HmlValue %s = %s;", result, inner_val);
                        free(inner_val);
                        break;
                    }
                }
            }

            // OPTIMIZATION: Constant folding for unary operations on literals
            if (expr->as.unary.operand->type == EXPR_NUMBER &&
                !expr->as.unary.operand->as.number.is_float) {
                int64_t val = expr->as.unary.operand->as.number.int_value;
                int can_fold = 1;

                switch (expr->as.unary.op) {
                    case UNARY_NEGATE:
                        val = -val;
                        break;
                    case UNARY_BIT_NOT:
                        val = ~val;
                        break;
                    default:
                        can_fold = 0;
                        break;
                }

                if (can_fold) {
                    if (val >= INT32_MIN && val <= INT32_MAX) {
                        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(%d);", result, (int32_t)val);
                    } else {
                        codegen_writeln(ctx, "HmlValue %s = hml_val_i64(%" PRId64 "L);", result, val);
                    }
                    break;
                }
            }

            // Constant folding for NOT on boolean literals
            if (expr->as.unary.op == UNARY_NOT && expr->as.unary.operand->type == EXPR_BOOL) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%d);", result, !expr->as.unary.operand->as.boolean);
                break;
            }

            char *operand = codegen_expr(ctx, expr->as.unary.operand);

            // Note: Type inference for unary ops is disabled.
            // Use generic path with runtime dispatch.
            codegen_writeln(ctx, "HmlValue %s = hml_unary_op(%s, %s);",
                          result, codegen_hml_unary_op(expr->as.unary.op), operand);
            codegen_writeln(ctx, "hml_release_if_needed(&%s);", operand);
            free(operand);
            break;
        }

        case EXPR_TERNARY: {
            char *cond = codegen_expr(ctx, expr->as.ternary.condition);
            codegen_writeln(ctx, "HmlValue %s;", result);
            codegen_writeln(ctx, "if (hml_to_bool(%s)) {", cond);
            codegen_indent_inc(ctx);
            char *true_val = codegen_expr(ctx, expr->as.ternary.true_expr);
            codegen_writeln(ctx, "%s = %s;", result, true_val);
            free(true_val);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "} else {");
            codegen_indent_inc(ctx);
            char *false_val = codegen_expr(ctx, expr->as.ternary.false_expr);
            codegen_writeln(ctx, "%s = %s;", result, false_val);
            free(false_val);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            codegen_writeln(ctx, "hml_release(&%s);", cond);
            free(cond);
            break;
        }


        case EXPR_CALL:
            codegen_expr_call(ctx, expr, result);
            break;

        case EXPR_ASSIGN: {
            // Check for const reassignment at compile time
            if (codegen_is_const(ctx, expr->as.assign.name)) {
                codegen_error(ctx, expr->line, "cannot assign to const variable '%s'",
                             expr->as.assign.name);
                codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                break;
            }

            // OPTIMIZATION: Check if assigning to an unboxed variable
            // Allow main variables that have been shadowed by native locals (e.g., while-loop unboxing)
            if (ctx->optimize && ctx->type_ctx &&
                (!codegen_is_main_var(ctx, expr->as.assign.name) || codegen_is_local(ctx, expr->as.assign.name))) {
                CheckedTypeKind native_type = type_check_get_unboxable(ctx->type_ctx, expr->as.assign.name);
                if (native_type != CHECKED_UNKNOWN) {
                    const char *unbox_cast = checked_type_to_unbox_cast(native_type);
                    const char *box_func = checked_type_to_box_func(native_type);
                    if (unbox_cast && box_func) {
                        char *value = codegen_expr(ctx, expr->as.assign.value);
                        char *safe_var_name = codegen_sanitize_ident(expr->as.assign.name);
                        // Unbox value and assign to native variable
                        codegen_writeln(ctx, "%s = %s(%s);", safe_var_name, unbox_cast, value);
                        codegen_writeln(ctx, "hml_release(&%s);", value);
                        // Return boxed value as result
                        codegen_writeln(ctx, "HmlValue %s = %s(%s);", result, box_func, safe_var_name);
                        free(value);
                        free(safe_var_name);
                        break;
                    }
                }
            }

            // OPTIMIZATION: Detect pattern "x = x + y" for in-place string append
            // This turns O(n²) repeated string concatenation into O(n) amortized
            Expr *val_expr = expr->as.assign.value;
            if (val_expr->type == EXPR_BINARY && val_expr->as.binary.op == OP_ADD) {
                Expr *left = val_expr->as.binary.left;
                Expr *right = val_expr->as.binary.right;

                // Check if left operand is the same variable being assigned
                if (left->type == EXPR_IDENT && strcmp(left->as.ident.name, expr->as.assign.name) == 0) {
                    // Check if right operand is definitely a string (literal only)
                    // We can't assume EXPR_IDENT is a string since it could be a number
                    // String indexing returns a rune but the variable type is unknown at compile time
                    int definitely_string = (right->type == EXPR_STRING);

                    if (definitely_string) {
                        // Generate in-place append
                        char *rhs = codegen_expr(ctx, right);

                        // Determine the correct variable name with prefix
                        char *safe_var_name = NULL;
                        const char *var_name = expr->as.assign.name;
                        char prefixed_var[256];
                        if (ctx->current_module && !codegen_is_local(ctx, var_name)) {
                            snprintf(prefixed_var, sizeof(prefixed_var), "%s%s",
                                    ctx->current_module->module_prefix, var_name);
                            var_name = prefixed_var;
                        } else if (codegen_is_local(ctx, var_name) && (ctx->current_module || ctx->in_function)) {
                            // Local variable in module or function shadows main var - use sanitized bare name
                            safe_var_name = codegen_sanitize_ident(var_name);
                            var_name = safe_var_name;
                        } else if (codegen_is_main_var(ctx, expr->as.assign.name)) {
                            snprintf(prefixed_var, sizeof(prefixed_var), "_main_%s", expr->as.assign.name);
                            var_name = prefixed_var;
                        }

                        // Use in-place append - this modifies dest directly if refcount==1
                        codegen_writeln(ctx, "hml_string_append_inplace(&%s, %s);", var_name, rhs);
                        codegen_writeln(ctx, "hml_release_if_needed(&%s);", rhs);
                        free(rhs);

                        // Result is the variable itself
                        codegen_writeln(ctx, "HmlValue %s = %s;", result, var_name);
                        codegen_writeln(ctx, "hml_retain(&%s);", result);
                        if (safe_var_name) free(safe_var_name);
                        break;
                    }
                }
            }

            char *value = codegen_expr(ctx, expr->as.assign.value);
            // Determine the correct variable name with prefix
            // Note: safe_var_name is allocated when needed and must be freed
            char *safe_var_name = NULL;
            const char *var_name = expr->as.assign.name;
            char prefixed_name[CODEGEN_MANGLED_NAME_SIZE];
            if (ctx->current_module && !codegen_is_local(ctx, var_name)) {
                // Module context - use module prefix
                snprintf(prefixed_name, sizeof(prefixed_name), "%s%s",
                        ctx->current_module->module_prefix, var_name);
                var_name = prefixed_name;
            } else if (codegen_is_shadow(ctx, var_name)) {
                // Shadow variable (like catch param) - use sanitized bare name
                safe_var_name = codegen_sanitize_ident(var_name);
                var_name = safe_var_name;
            } else if (codegen_is_local(ctx, var_name) && (ctx->current_module || ctx->in_function || !codegen_is_main_var(ctx, var_name))) {
                // Local variable - use sanitized bare name
                // In module/function context, locals always shadow main vars
                safe_var_name = codegen_sanitize_ident(var_name);
                var_name = safe_var_name;
            } else if (codegen_is_main_var(ctx, expr->as.assign.name)) {
                // Main file top-level variable - use _main_ prefix
                snprintf(prefixed_name, sizeof(prefixed_name), "_main_%s", expr->as.assign.name);
                var_name = prefixed_name;
            }
            // Check if this is a ref parameter - if so, dereference for assignment
            // Note: codegen_expr returns an "owned" value (already retained/created with refcount 1).
            // Assignment transfers ownership: release old, assign new. No additional retain needed.
            if (codegen_is_ref_param(ctx, expr->as.assign.name)) {
                codegen_writeln(ctx, "hml_release(%s);", var_name);  // Already a pointer
                codegen_writeln(ctx, "*%s = %s;", var_name, value);
            } else {
                codegen_writeln(ctx, "hml_release(&%s);", var_name);
                codegen_writeln(ctx, "%s = %s;", var_name, value);
            }

            // If we're inside a closure and this is a captured variable,
            // update the closure environment so the change is visible to other closures
            if (ctx->current_closure && ctx->current_closure->num_captured > 0) {
                for (int i = 0; i < ctx->current_closure->num_captured; i++) {
                    if (strcmp(ctx->current_closure->captured_vars[i], expr->as.assign.name) == 0) {
                        // Use shared_env_indices if using shared environment, otherwise use local index
                        int env_index = ctx->current_closure->shared_env_indices ?
                                       ctx->current_closure->shared_env_indices[i] : i;
                        codegen_writeln(ctx, "hml_closure_env_set(_closure_env, %d, %s);", env_index, var_name);
                        break;
                    }
                }
            }

            // Use dereferenced value for result if ref param
            if (codegen_is_ref_param(ctx, expr->as.assign.name)) {
                codegen_writeln(ctx, "HmlValue %s = *%s;", result, var_name);
            } else {
                codegen_writeln(ctx, "HmlValue %s = %s;", result, var_name);
            }
            codegen_writeln(ctx, "hml_retain(&%s);", result);
            free(value);
            if (safe_var_name) free(safe_var_name);
            break;
        }

        case EXPR_GET_PROPERTY: {
            char *obj = codegen_expr(ctx, expr->as.get_property.object);

            // Check for built-in properties like .length
            if (strcmp(expr->as.get_property.property, "length") == 0) {
                codegen_writeln(ctx, "HmlValue %s;", result);
                codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_array_length(%s);", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_STRING) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_string_length(%s);", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_BUFFER) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_buffer_length(%s);", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_object_get_field_required(%s, \"length\");", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            // Socket properties: fd, address, port, closed
            } else if (strcmp(expr->as.get_property.property, "fd") == 0) {
                codegen_writeln(ctx, "HmlValue %s;", result);
                codegen_writeln(ctx, "if (%s.type == HML_VAL_SOCKET) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_socket_get_fd(%s);", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_object_get_field_required(%s, \"fd\");", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            } else if (strcmp(expr->as.get_property.property, "address") == 0) {
                codegen_writeln(ctx, "HmlValue %s;", result);
                codegen_writeln(ctx, "if (%s.type == HML_VAL_SOCKET) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_socket_get_address(%s);", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_object_get_field_required(%s, \"address\");", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            } else if (strcmp(expr->as.get_property.property, "port") == 0) {
                codegen_writeln(ctx, "HmlValue %s;", result);
                codegen_writeln(ctx, "if (%s.type == HML_VAL_SOCKET) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_socket_get_port(%s);", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_object_get_field_required(%s, \"port\");", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            } else if (strcmp(expr->as.get_property.property, "closed") == 0) {
                codegen_writeln(ctx, "HmlValue %s;", result);
                codegen_writeln(ctx, "if (%s.type == HML_VAL_SOCKET) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_socket_get_closed(%s);", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_object_get_field_required(%s, \"closed\");", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            // String byte_length property
            } else if (strcmp(expr->as.get_property.property, "byte_length") == 0) {
                codegen_writeln(ctx, "HmlValue %s;", result);
                codegen_writeln(ctx, "if (%s.type == HML_VAL_STRING) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_string_byte_length(%s);", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_object_get_field_required(%s, \"byte_length\");", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            // Buffer capacity property
            } else if (strcmp(expr->as.get_property.property, "capacity") == 0) {
                codegen_writeln(ctx, "HmlValue %s;", result);
                codegen_writeln(ctx, "if (%s.type == HML_VAL_BUFFER) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_buffer_capacity(%s);", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_object_get_field_required(%s, \"capacity\");", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            } else {
                // Regular property access - throws error if field not found (parity with interpreter)
                codegen_writeln(ctx, "HmlValue %s = hml_object_get_field_required(%s, \"%s\");",
                              result, obj, expr->as.get_property.property);
            }
            codegen_writeln(ctx, "hml_release(&%s);", obj);
            free(obj);
            break;
        }

        case EXPR_SET_PROPERTY: {
            char *obj = codegen_expr(ctx, expr->as.set_property.object);
            char *value = codegen_expr(ctx, expr->as.set_property.value);
            codegen_writeln(ctx, "hml_object_set_field(%s, \"%s\", %s);",
                          obj, expr->as.set_property.property, value);
            codegen_writeln(ctx, "HmlValue %s = %s;", result, value);
            codegen_writeln(ctx, "hml_retain(&%s);", result);
            codegen_writeln(ctx, "hml_release(&%s);", obj);
            free(obj);
            free(value);
            break;
        }

        case EXPR_INDEX: {
            // Note: Type inference for index expressions is disabled.
            // We use runtime type checking for now.
            int idx_is_i32 = 0;
            int obj_is_array = 0;

            char *obj = codegen_expr(ctx, expr->as.index.object);
            char *idx = codegen_expr(ctx, expr->as.index.index);
            codegen_writeln(ctx, "HmlValue %s;", result);

            if (obj_is_array && idx_is_i32) {
                // OPTIMIZATION: Both array and i32 index known at compile time
                // Skip runtime type checks entirely
                codegen_writeln(ctx, "%s = hml_array_get_i32_fast(%s.as.as_array, %s.as.as_i32);", result, obj, idx);
            } else if (idx_is_i32) {
                // OPTIMIZATION: Index is known i32 - skip index type check
                codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_array_get_i32_fast(%s.as.as_array, %s.as.as_i32);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_STRING) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_string_index(%s, %s);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_BUFFER) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_buffer_get(%s, %s);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_PTR) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_ptr_get(%s, %s);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_val_null();", result);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            } else {
                // General case: full runtime type checking
                codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY && %s.type == HML_VAL_I32) {", obj, idx);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_array_get_i32_fast(%s.as.as_array, %s.as.as_i32);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_ARRAY) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_array_get(%s, %s);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_STRING) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_string_index(%s, %s);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_BUFFER) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_buffer_get(%s, %s);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_PTR) {", obj);
                codegen_indent_inc(ctx);
                // Raw pointer indexing - no bounds checking (unsafe!)
                codegen_writeln(ctx, "%s = hml_ptr_get(%s, %s);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_OBJECT && %s.type == HML_VAL_STRING) {", obj, idx);
                codegen_indent_inc(ctx);
                // Dynamic object property access with string key
                codegen_writeln(ctx, "%s = hml_object_get_field(%s, %s.as.as_string->data);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_OBJECT) {", obj);
                codegen_indent_inc(ctx);
                // Dynamic object property access with non-string key (auto-coerce to string)
                codegen_writeln(ctx, "%s = hml_object_get_field_coerce(%s, %s);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_val_null();", result);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            }
            // Use optimized release that skips primitives (index is often i32)
            codegen_writeln(ctx, "hml_release_if_needed(&%s);", obj);
            codegen_writeln(ctx, "hml_release_if_needed(&%s);", idx);
            free(obj);
            free(idx);
            break;
        }

        case EXPR_INDEX_ASSIGN: {
            // Note: Type inference for index assignment is disabled.
            // We use runtime type checking for now.
            int idx_is_i32 = 0;
            int obj_is_array = 0;

            char *obj = codegen_expr(ctx, expr->as.index_assign.object);
            char *idx = codegen_expr(ctx, expr->as.index_assign.index);
            char *value = codegen_expr(ctx, expr->as.index_assign.value);

            if (obj_is_array && idx_is_i32) {
                // OPTIMIZATION: Both array and i32 index known at compile time
                codegen_writeln(ctx, "hml_array_set_i32_fast(%s.as.as_array, %s.as.as_i32, %s);", obj, idx, value);
            } else if (idx_is_i32) {
                // OPTIMIZATION: Index is known i32 - skip index type check
                codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "hml_array_set_i32_fast(%s.as.as_array, %s.as.as_i32, %s);", obj, idx, value);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_STRING) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "hml_string_index_assign(%s, %s, %s);", obj, idx, value);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_BUFFER) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "hml_buffer_set(%s, %s, %s);", obj, idx, value);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_PTR) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "hml_ptr_set(%s, %s, %s);", obj, idx, value);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            } else {
                // General case: full runtime type checking
                codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY && %s.type == HML_VAL_I32) {", obj, idx);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "hml_array_set_i32_fast(%s.as.as_array, %s.as.as_i32, %s);", obj, idx, value);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_ARRAY) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "hml_array_set(%s, %s, %s);", obj, idx, value);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_STRING) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "hml_string_index_assign(%s, %s, %s);", obj, idx, value);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_BUFFER) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "hml_buffer_set(%s, %s, %s);", obj, idx, value);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_PTR) {", obj);
                codegen_indent_inc(ctx);
                // Raw pointer indexing - no bounds checking (unsafe!)
                codegen_writeln(ctx, "hml_ptr_set(%s, %s, %s);", obj, idx, value);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_OBJECT && %s.type == HML_VAL_STRING) {", obj, idx);
                codegen_indent_inc(ctx);
                // Dynamic object property assignment with string key
                codegen_writeln(ctx, "hml_object_set_field(%s, %s.as.as_string->data, %s);", obj, idx, value);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_OBJECT) {", obj);
                codegen_indent_inc(ctx);
                // Dynamic object property assignment with non-string key (auto-coerce to string)
                codegen_writeln(ctx, "hml_object_set_field_coerce(%s, %s, %s);", obj, idx, value);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            }
            codegen_writeln(ctx, "HmlValue %s = %s;", result, value);
            codegen_writeln(ctx, "hml_retain_if_needed(&%s);", result);
            codegen_writeln(ctx, "hml_release_if_needed(&%s);", obj);
            codegen_writeln(ctx, "hml_release_if_needed(&%s);", idx);
            free(obj);
            free(idx);
            free(value);
            break;
        }

        case EXPR_ARRAY_LITERAL: {
            codegen_writeln(ctx, "HmlValue %s = hml_val_array();", result);
            for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                char *elem = codegen_expr(ctx, expr->as.array_literal.elements[i]);
                codegen_writeln(ctx, "hml_array_push(%s, %s);", result, elem);
                codegen_writeln(ctx, "hml_release(&%s);", elem);
                free(elem);
            }
            break;
        }

        case EXPR_OBJECT_LITERAL: {
            codegen_writeln(ctx, "HmlValue %s = hml_val_object();", result);
            for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
                if (expr->as.object_literal.field_names[i] == NULL) {
                    // Spread operator: ...expr
                    char *spread_val = codegen_expr(ctx, expr->as.object_literal.field_values[i]);
                    char *spread_idx = codegen_temp(ctx);
                    char *spread_key = codegen_temp(ctx);
                    char *spread_field = codegen_temp(ctx);
                    codegen_writeln(ctx, "for (int %s = 0; %s < hml_object_num_fields(%s); %s++) {",
                                  spread_idx, spread_idx, spread_val, spread_idx);
                    codegen_writeln(ctx, "  HmlValue %s = hml_object_key_at(%s, %s);",
                                  spread_key, spread_val, spread_idx);
                    codegen_writeln(ctx, "  HmlValue %s = hml_object_value_at(%s, %s);",
                                  spread_field, spread_val, spread_idx);
                    codegen_writeln(ctx, "  hml_object_set_field(%s, %s.as.as_string->data, %s);",
                                  result, spread_key, spread_field);
                    codegen_writeln(ctx, "  hml_release(&%s);", spread_key);
                    codegen_writeln(ctx, "  hml_release(&%s);", spread_field);
                    codegen_writeln(ctx, "}");
                    codegen_writeln(ctx, "hml_release(&%s);", spread_val);
                    free(spread_val);
                    free(spread_idx);
                    free(spread_key);
                    free(spread_field);
                } else {
                    // Normal field: name: value
                    char *val = codegen_expr(ctx, expr->as.object_literal.field_values[i]);
                    codegen_writeln(ctx, "hml_object_set_field(%s, \"%s\", %s);",
                                  result, expr->as.object_literal.field_names[i], val);
                    codegen_writeln(ctx, "hml_release(&%s);", val);
                    free(val);
                }
            }
            break;
        }

        case EXPR_FUNCTION: {
            // Generate anonymous function with closure support
            char *func_name = codegen_anon_func(ctx);

            // Create a scope for analyzing free variables
            Scope *func_scope = scope_new(NULL);

            // Add parameters to the function's scope
            for (int i = 0; i < expr->as.function.num_params; i++) {
                scope_add_var(func_scope, expr->as.function.param_names[i]);
            }

            // Find free variables
            FreeVarSet *free_vars = free_var_set_new();
            find_free_vars_stmt(expr->as.function.body, func_scope, free_vars);

            // Filter out builtins and global functions from free vars
            // (We only want to capture actual local variables)
            FreeVarSet *captured = free_var_set_new();
            for (int i = 0; i < free_vars->num_vars; i++) {
                const char *var = free_vars->vars[i];
                // Check if it's a local variable in the current scope
                if (codegen_is_local(ctx, var)) {
                    free_var_set_add(captured, var);
                }
            }

            // Register closure for later code generation
            // If using shared environment, store indices into shared env in captured_vars
            ClosureInfo *closure = malloc(sizeof(ClosureInfo));
            if (!closure) {
                codegen_error(ctx, 0, "Memory allocation failed for closure info");
                free(result);
                return strdup("hml_val_null()");
            }
            closure->func_name = strdup(func_name);
            if (!closure->func_name) {
                codegen_error(ctx, 0, "Memory allocation failed for closure function name");
                free(closure);
                free(result);
                return strdup("hml_val_null()");
            }
            closure->func_expr = expr;
            closure->source_module = ctx->current_module;  // Save module context for function resolution
            closure->next = ctx->closures;
            ctx->closures = closure;

            // Check if any captured variable is block-scoped (defined in a block inside loops).
            // Block-scoped captures need per-iteration environments for correct JS-like semantics.
            int has_block_scoped_capture = 0;
            if (ctx->current_scope) {
                for (int i = 0; i < captured->num_vars; i++) {
                    if (scope_is_defined(ctx->current_scope, captured->vars[i])) {
                        has_block_scoped_capture = 1;
                        break;
                    }
                }
            }

            if (captured->num_vars == 0) {
                // No captures - simple function pointer
                closure->captured_vars = NULL;
                closure->num_captured = 0;
                closure->shared_env_indices = NULL;
                int num_required = count_required_params(expr->as.function.param_defaults, expr->as.function.num_params);
                int has_rest = expr->as.function.rest_param ? 1 : 0;
                codegen_writeln(ctx, "HmlValue %s = hml_val_function_rest((void*)%s, %d, %d, %d, %d);",
                              result, func_name, expr->as.function.num_params, num_required, expr->as.function.is_async, has_rest);
                // Set parameter names for named argument support
                if (expr->as.function.num_params > 0) {
                    int param_names_counter = ctx->temp_counter++;
                    codegen_writeln(ctx, "const char *_param_names%d[%d] = {", param_names_counter, expr->as.function.num_params);
                    for (int i = 0; i < expr->as.function.num_params; i++) {
                        codegen_write(ctx, "\"%s\"", expr->as.function.param_names[i]);
                        if (i < expr->as.function.num_params - 1) {
                            codegen_write(ctx, ", ");
                        }
                    }
                    codegen_writeln(ctx, "};");
                    codegen_writeln(ctx, "hml_function_set_param_names(%s, _param_names%d, %d);", result, param_names_counter, expr->as.function.num_params);
                }
            } else if (ctx->shared_env_name && !has_block_scoped_capture) {
                // Use the shared environment only if no block-scoped captures.
                // Block-scoped captures (like loop-local variables) need per-closure environments.
                // Store the captured variable names and their shared env indices
                closure->captured_vars = malloc(captured->num_vars * sizeof(char*));
                closure->shared_env_indices = malloc(captured->num_vars * sizeof(int));
                if (!closure->captured_vars || !closure->shared_env_indices) {
                    codegen_error(ctx, 0, "Memory allocation failed for closure captured vars");
                    free(closure->captured_vars);
                    free(closure->shared_env_indices);
                    closure->captured_vars = NULL;
                    closure->shared_env_indices = NULL;
                    closure->num_captured = 0;
                } else {
                    closure->num_captured = captured->num_vars;
                    for (int i = 0; i < captured->num_vars; i++) {
                        closure->captured_vars[i] = strdup(captured->vars[i]);
                        closure->shared_env_indices[i] = shared_env_get_index(ctx, captured->vars[i]);
                    }
                }

                // Update the shared environment with current values of captured variables
                for (int i = 0; i < captured->num_vars; i++) {
                    int shared_idx = shared_env_get_index(ctx, captured->vars[i]);
                    if (shared_idx >= 0) {
                        // Determine which variable name to use:
                        // - Main file vars are stored as _main_<name> in C
                        // - Module-local vars are stored as <name> in C (sanitized)
                        // - Module-local vars should shadow outer (main) vars with same name
                        if (ctx->current_module && codegen_is_local(ctx, captured->vars[i])) {
                            char *safe_cap = codegen_sanitize_ident(captured->vars[i]);
                            codegen_writeln(ctx, "hml_closure_env_set(%s, %d, %s);",
                                          ctx->shared_env_name, shared_idx, safe_cap);
                            free(safe_cap);
                        } else if (codegen_is_main_var(ctx, captured->vars[i])) {
                            codegen_writeln(ctx, "hml_closure_env_set(%s, %d, _main_%s);",
                                          ctx->shared_env_name, shared_idx, captured->vars[i]);
                        } else {
                            char *safe_cap = codegen_sanitize_ident(captured->vars[i]);
                            codegen_writeln(ctx, "hml_closure_env_set(%s, %d, %s);",
                                          ctx->shared_env_name, shared_idx, safe_cap);
                            free(safe_cap);
                        }
                    }
                }
                int num_required = count_required_params(expr->as.function.param_defaults, expr->as.function.num_params);
                int has_rest = expr->as.function.rest_param ? 1 : 0;
                // Retain the shared env for this closure (each closure owns a reference)
                codegen_writeln(ctx, "hml_closure_env_retain(%s);", ctx->shared_env_name);
                codegen_writeln(ctx, "HmlValue %s = hml_val_function_with_env_rest((void*)%s, (void*)%s, %d, %d, %d, %d);",
                              result, func_name, ctx->shared_env_name, expr->as.function.num_params, num_required, expr->as.function.is_async, has_rest);
                // Set parameter names for named argument support
                if (expr->as.function.num_params > 0) {
                    int param_names_counter = ctx->temp_counter++;
                    codegen_writeln(ctx, "const char *_param_names%d[%d] = {", param_names_counter, expr->as.function.num_params);
                    for (int i = 0; i < expr->as.function.num_params; i++) {
                        codegen_write(ctx, "\"%s\"", expr->as.function.param_names[i]);
                        if (i < expr->as.function.num_params - 1) {
                            codegen_write(ctx, ", ");
                        }
                    }
                    codegen_writeln(ctx, "};");
                    codegen_writeln(ctx, "hml_function_set_param_names(%s, _param_names%d, %d);", result, param_names_counter, expr->as.function.num_params);
                }

                // Track for self-reference fixup
                ctx->last_closure_env_id = -1;  // Using shared env, different mechanism
                if (ctx->last_closure_captured) {
                    for (int i = 0; i < ctx->last_closure_num_captured; i++) {
                        free(ctx->last_closure_captured[i]);
                    }
                    free(ctx->last_closure_captured);
                }
                ctx->last_closure_captured = malloc(sizeof(char*) * captured->num_vars);
                if (ctx->last_closure_captured) {
                    ctx->last_closure_num_captured = captured->num_vars;
                    for (int i = 0; i < captured->num_vars; i++) {
                        ctx->last_closure_captured[i] = strdup(captured->vars[i]);
                    }
                } else {
                    ctx->last_closure_num_captured = 0;
                }
            } else {
                // No shared environment - create a per-closure environment (original behavior)
                closure->captured_vars = malloc(captured->num_vars * sizeof(char*));
                if (!closure->captured_vars) {
                    codegen_error(ctx, 0, "Memory allocation failed for closure captured vars");
                    closure->num_captured = 0;
                } else {
                    closure->num_captured = captured->num_vars;
                }
                closure->shared_env_indices = NULL;  // Not using shared environment
                for (int i = 0; i < closure->num_captured; i++) {
                    closure->captured_vars[i] = strdup(captured->vars[i]);
                }

                int env_id = ctx->temp_counter;
                codegen_writeln(ctx, "HmlClosureEnv *_env_%d = hml_closure_env_new(%d);",
                              env_id, captured->num_vars);
                for (int i = 0; i < captured->num_vars; i++) {
                    // Determine which variable name to use:
                    // - Main file vars are stored as _main_<name> in C
                    // - Module-local vars are stored as sanitized name in C
                    if (ctx->current_module && codegen_is_local(ctx, captured->vars[i])) {
                        char *safe_cap = codegen_sanitize_ident(captured->vars[i]);
                        codegen_writeln(ctx, "hml_closure_env_set(_env_%d, %d, %s);",
                                      env_id, i, safe_cap);
                        free(safe_cap);
                    } else if (codegen_is_main_var(ctx, captured->vars[i])) {
                        codegen_writeln(ctx, "hml_closure_env_set(_env_%d, %d, _main_%s);",
                                      env_id, i, captured->vars[i]);
                    } else {
                        char *safe_cap = codegen_sanitize_ident(captured->vars[i]);
                        codegen_writeln(ctx, "hml_closure_env_set(_env_%d, %d, %s);",
                                      env_id, i, safe_cap);
                        free(safe_cap);
                    }
                }
                int num_required = count_required_params(expr->as.function.param_defaults, expr->as.function.num_params);
                int has_rest = expr->as.function.rest_param ? 1 : 0;
                codegen_writeln(ctx, "HmlValue %s = hml_val_function_with_env_rest((void*)%s, (void*)_env_%d, %d, %d, %d, %d);",
                              result, func_name, env_id, expr->as.function.num_params, num_required, expr->as.function.is_async, has_rest);
                ctx->temp_counter++;
                // Set parameter names for named argument support
                if (expr->as.function.num_params > 0) {
                    int param_names_counter = ctx->temp_counter++;
                    codegen_writeln(ctx, "const char *_param_names%d[%d] = {", param_names_counter, expr->as.function.num_params);
                    for (int i = 0; i < expr->as.function.num_params; i++) {
                        codegen_write(ctx, "\"%s\"", expr->as.function.param_names[i]);
                        if (i < expr->as.function.num_params - 1) {
                            codegen_write(ctx, ", ");
                        }
                    }
                    codegen_writeln(ctx, "};");
                    codegen_writeln(ctx, "hml_function_set_param_names(%s, _param_names%d, %d);", result, param_names_counter, expr->as.function.num_params);
                }

                // Track this closure for potential self-reference fixup in let statements
                ctx->last_closure_env_id = env_id;
                // Copy captured variable names since 'captured' will be freed
                if (ctx->last_closure_captured) {
                    for (int i = 0; i < ctx->last_closure_num_captured; i++) {
                        free(ctx->last_closure_captured[i]);
                    }
                    free(ctx->last_closure_captured);
                }
                ctx->last_closure_captured = malloc(sizeof(char*) * captured->num_vars);
                if (ctx->last_closure_captured) {
                    ctx->last_closure_num_captured = captured->num_vars;
                    for (int i = 0; i < captured->num_vars; i++) {
                        ctx->last_closure_captured[i] = strdup(captured->vars[i]);
                    }
                } else {
                    ctx->last_closure_num_captured = 0;
                }
            }

            scope_free(func_scope);
            free_var_set_free(free_vars);
            free_var_set_free(captured);
            free(func_name);
            break;
        }

        case EXPR_PREFIX_INC: {
            // ++x is equivalent to x = x + 1, returns new value
            if (expr->as.prefix_inc.operand->type == EXPR_IDENT) {
                const char *raw_var = expr->as.prefix_inc.operand->as.ident.name;
                const char *var;
                char prefixed_name[CODEGEN_MANGLED_NAME_SIZE];
                char *safe_var = NULL;
                if (ctx->current_module && !codegen_is_local(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "%s%s",
                            ctx->current_module->module_prefix, raw_var);
                    var = prefixed_name;
                } else if (codegen_is_main_var(ctx, raw_var) && !codegen_is_local(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "_main_%s", raw_var);
                    var = prefixed_name;
                } else {
                    // Local variable - sanitize to avoid C keyword conflicts
                    safe_var = codegen_sanitize_ident(raw_var);
                    var = safe_var;
                }
                // Check if variable is unboxed (native C type)
                // Skip if captured variable - captured vars are always HmlValue in closure env
                if (ctx->optimize && ctx->type_ctx && !codegen_is_main_var(ctx, raw_var) &&
                    !is_captured_variable(ctx, raw_var)) {
                    CheckedTypeKind native_type = type_check_get_unboxable(ctx->type_ctx, raw_var);
                    if (native_type != CHECKED_UNKNOWN && checked_kind_is_numeric(native_type)) {
                        const char *box_func = checked_type_to_box_func(native_type);
                        if (box_func) {
                            // Unboxed variable - use simple C increment
                            codegen_writeln(ctx, "++%s;", var);
                            codegen_writeln(ctx, "HmlValue %s = %s(%s);", result, box_func, var);
                            if (safe_var) free(safe_var);
                            break;
                        }
                    }
                }
                // Fast path for i32, fallback to generic binary_op
                codegen_writeln(ctx, "%s = %s.type == HML_VAL_I32 ? hml_i32_inc(%s) : hml_binary_op(HML_OP_ADD, %s, hml_val_i32(1));", var, var, var, var);
                // If captured variable, update closure environment
                if (ctx->current_closure && ctx->current_closure->num_captured > 0) {
                    for (int i = 0; i < ctx->current_closure->num_captured; i++) {
                        if (strcmp(ctx->current_closure->captured_vars[i], raw_var) == 0) {
                            int env_index = ctx->current_closure->shared_env_indices ?
                                           ctx->current_closure->shared_env_indices[i] : i;
                            codegen_writeln(ctx, "hml_closure_env_set(_closure_env, %d, %s);", env_index, var);
                            break;
                        }
                    }
                }
                codegen_writeln(ctx, "HmlValue %s = %s;", result, var);
                codegen_writeln(ctx, "hml_retain_if_needed(&%s);", result);
                if (safe_var) free(safe_var);
            } else if (expr->as.prefix_inc.operand->type == EXPR_INDEX) {
                // ++arr[i]
                char *arr = codegen_expr(ctx, expr->as.prefix_inc.operand->as.index.object);
                char *idx = codegen_expr(ctx, expr->as.prefix_inc.operand->as.index.index);
                char *old_val = codegen_temp(ctx);
                char *new_val = codegen_temp(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_array_get(%s, %s);", old_val, arr, idx);
                codegen_writeln(ctx, "HmlValue %s = %s.type == HML_VAL_I32 ? hml_i32_inc(%s) : hml_binary_op(HML_OP_ADD, %s, hml_val_i32(1));", new_val, old_val, old_val, old_val);
                codegen_writeln(ctx, "hml_array_set(%s, %s, %s);", arr, idx, new_val);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, new_val);
                codegen_writeln(ctx, "hml_retain(&%s);", result);
                codegen_writeln(ctx, "hml_release(&%s);", old_val);
                codegen_writeln(ctx, "hml_release(&%s);", new_val);
                codegen_writeln(ctx, "hml_release(&%s);", idx);
                codegen_writeln(ctx, "hml_release(&%s);", arr);
                free(arr); free(idx); free(old_val); free(new_val);
            } else if (expr->as.prefix_inc.operand->type == EXPR_GET_PROPERTY) {
                // ++obj.prop
                char *obj = codegen_expr(ctx, expr->as.prefix_inc.operand->as.get_property.object);
                const char *prop = expr->as.prefix_inc.operand->as.get_property.property;
                char *old_val = codegen_temp(ctx);
                char *new_val = codegen_temp(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_object_get_field(%s, \"%s\");", old_val, obj, prop);
                codegen_writeln(ctx, "HmlValue %s = %s.type == HML_VAL_I32 ? hml_i32_inc(%s) : hml_binary_op(HML_OP_ADD, %s, hml_val_i32(1));", new_val, old_val, old_val, old_val);
                codegen_writeln(ctx, "hml_object_set_field(%s, \"%s\", %s);", obj, prop, new_val);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, new_val);
                codegen_writeln(ctx, "hml_retain_if_needed(&%s);", result);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", old_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", new_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", obj);
                free(obj); free(old_val); free(new_val);
            } else {
                codegen_writeln(ctx, "hml_runtime_error(\"Invalid operand for ++\");");
                codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            }
            break;
        }

        case EXPR_PREFIX_DEC: {
            if (expr->as.prefix_dec.operand->type == EXPR_IDENT) {
                const char *raw_var = expr->as.prefix_dec.operand->as.ident.name;
                const char *var;
                char prefixed_name[CODEGEN_MANGLED_NAME_SIZE];
                char *safe_var = NULL;
                if (ctx->current_module && !codegen_is_local(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "%s%s",
                            ctx->current_module->module_prefix, raw_var);
                    var = prefixed_name;
                } else if (codegen_is_main_var(ctx, raw_var) && !codegen_is_local(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "_main_%s", raw_var);
                    var = prefixed_name;
                } else {
                    // Local variable - sanitize to avoid C keyword conflicts
                    safe_var = codegen_sanitize_ident(raw_var);
                    var = safe_var;
                }
                // Check if variable is unboxed (native C type)
                // Skip if captured variable - captured vars are always HmlValue in closure env
                if (ctx->optimize && ctx->type_ctx && !codegen_is_main_var(ctx, raw_var) &&
                    !is_captured_variable(ctx, raw_var)) {
                    CheckedTypeKind native_type = type_check_get_unboxable(ctx->type_ctx, raw_var);
                    if (native_type != CHECKED_UNKNOWN && checked_kind_is_numeric(native_type)) {
                        const char *box_func = checked_type_to_box_func(native_type);
                        if (box_func) {
                            // Unboxed variable - use simple C decrement
                            codegen_writeln(ctx, "--%s;", var);
                            codegen_writeln(ctx, "HmlValue %s = %s(%s);", result, box_func, var);
                            if (safe_var) free(safe_var);
                            break;
                        }
                    }
                }
                // Fast path for i32, fallback to generic binary_op
                codegen_writeln(ctx, "%s = %s.type == HML_VAL_I32 ? hml_i32_dec(%s) : hml_binary_op(HML_OP_SUB, %s, hml_val_i32(1));", var, var, var, var);
                // If captured variable, update closure environment
                if (ctx->current_closure && ctx->current_closure->num_captured > 0) {
                    for (int i = 0; i < ctx->current_closure->num_captured; i++) {
                        if (strcmp(ctx->current_closure->captured_vars[i], raw_var) == 0) {
                            int env_index = ctx->current_closure->shared_env_indices ?
                                           ctx->current_closure->shared_env_indices[i] : i;
                            codegen_writeln(ctx, "hml_closure_env_set(_closure_env, %d, %s);", env_index, var);
                            break;
                        }
                    }
                }
                codegen_writeln(ctx, "HmlValue %s = %s;", result, var);
                codegen_writeln(ctx, "hml_retain_if_needed(&%s);", result);
                if (safe_var) free(safe_var);
            } else if (expr->as.prefix_dec.operand->type == EXPR_INDEX) {
                // --arr[i]
                char *arr = codegen_expr(ctx, expr->as.prefix_dec.operand->as.index.object);
                char *idx = codegen_expr(ctx, expr->as.prefix_dec.operand->as.index.index);
                char *old_val = codegen_temp(ctx);
                char *new_val = codegen_temp(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_array_get(%s, %s);", old_val, arr, idx);
                codegen_writeln(ctx, "HmlValue %s = %s.type == HML_VAL_I32 ? hml_i32_dec(%s) : hml_binary_op(HML_OP_SUB, %s, hml_val_i32(1));", new_val, old_val, old_val, old_val);
                codegen_writeln(ctx, "hml_array_set(%s, %s, %s);", arr, idx, new_val);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, new_val);
                codegen_writeln(ctx, "hml_retain_if_needed(&%s);", result);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", old_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", new_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", idx);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", arr);
                free(arr); free(idx); free(old_val); free(new_val);
            } else if (expr->as.prefix_dec.operand->type == EXPR_GET_PROPERTY) {
                // --obj.prop
                char *obj = codegen_expr(ctx, expr->as.prefix_dec.operand->as.get_property.object);
                const char *prop = expr->as.prefix_dec.operand->as.get_property.property;
                char *old_val = codegen_temp(ctx);
                char *new_val = codegen_temp(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_object_get_field(%s, \"%s\");", old_val, obj, prop);
                codegen_writeln(ctx, "HmlValue %s = %s.type == HML_VAL_I32 ? hml_i32_dec(%s) : hml_binary_op(HML_OP_SUB, %s, hml_val_i32(1));", new_val, old_val, old_val, old_val);
                codegen_writeln(ctx, "hml_object_set_field(%s, \"%s\", %s);", obj, prop, new_val);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, new_val);
                codegen_writeln(ctx, "hml_retain_if_needed(&%s);", result);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", old_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", new_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", obj);
                free(obj); free(old_val); free(new_val);
            } else {
                codegen_writeln(ctx, "hml_runtime_error(\"Invalid operand for --\");");
                codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            }
            break;
        }

        case EXPR_POSTFIX_INC: {
            // x++ returns old value, then increments
            if (expr->as.postfix_inc.operand->type == EXPR_IDENT) {
                const char *raw_var = expr->as.postfix_inc.operand->as.ident.name;
                const char *var;
                char prefixed_name[CODEGEN_MANGLED_NAME_SIZE];
                char *safe_var = NULL;
                if (ctx->current_module && !codegen_is_local(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "%s%s",
                            ctx->current_module->module_prefix, raw_var);
                    var = prefixed_name;
                } else if (codegen_is_main_var(ctx, raw_var) && !codegen_is_local(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "_main_%s", raw_var);
                    var = prefixed_name;
                } else {
                    // Local variable - sanitize to avoid C keyword conflicts
                    safe_var = codegen_sanitize_ident(raw_var);
                    var = safe_var;
                }
                // Check if variable is unboxed (native C type)
                // Skip if captured variable - captured vars are always HmlValue in closure env
                if (ctx->optimize && ctx->type_ctx && !codegen_is_main_var(ctx, raw_var) &&
                    !is_captured_variable(ctx, raw_var)) {
                    CheckedTypeKind native_type = type_check_get_unboxable(ctx->type_ctx, raw_var);
                    if (native_type != CHECKED_UNKNOWN && checked_kind_is_numeric(native_type)) {
                        const char *box_func = checked_type_to_box_func(native_type);
                        if (box_func) {
                            // Unboxed variable - return old value, then increment
                            codegen_writeln(ctx, "HmlValue %s = %s(%s);", result, box_func, var);
                            codegen_writeln(ctx, "%s++;", var);
                            if (safe_var) free(safe_var);
                            break;
                        }
                    }
                }
                codegen_writeln(ctx, "HmlValue %s = %s;", result, var);
                codegen_writeln(ctx, "hml_retain_if_needed(&%s);", result);
                // Fast path for i32, fallback to generic binary_op
                codegen_writeln(ctx, "%s = %s.type == HML_VAL_I32 ? hml_i32_inc(%s) : hml_binary_op(HML_OP_ADD, %s, hml_val_i32(1));", var, var, var, var);
                // If captured variable, update closure environment
                if (ctx->current_closure && ctx->current_closure->num_captured > 0) {
                    for (int i = 0; i < ctx->current_closure->num_captured; i++) {
                        if (strcmp(ctx->current_closure->captured_vars[i], raw_var) == 0) {
                            int env_index = ctx->current_closure->shared_env_indices ?
                                           ctx->current_closure->shared_env_indices[i] : i;
                            codegen_writeln(ctx, "hml_closure_env_set(_closure_env, %d, %s);", env_index, var);
                            break;
                        }
                    }
                }
                if (safe_var) free(safe_var);
            } else if (expr->as.postfix_inc.operand->type == EXPR_INDEX) {
                // arr[i]++
                char *arr = codegen_expr(ctx, expr->as.postfix_inc.operand->as.index.object);
                char *idx = codegen_expr(ctx, expr->as.postfix_inc.operand->as.index.index);
                char *old_val = codegen_temp(ctx);
                char *new_val = codegen_temp(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_array_get(%s, %s);", old_val, arr, idx);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, old_val);  // Return old value
                codegen_writeln(ctx, "hml_retain_if_needed(&%s);", result);
                codegen_writeln(ctx, "HmlValue %s = %s.type == HML_VAL_I32 ? hml_i32_inc(%s) : hml_binary_op(HML_OP_ADD, %s, hml_val_i32(1));", new_val, old_val, old_val, old_val);
                codegen_writeln(ctx, "hml_array_set(%s, %s, %s);", arr, idx, new_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", old_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", new_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", idx);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", arr);
                free(arr); free(idx); free(old_val); free(new_val);
            } else if (expr->as.postfix_inc.operand->type == EXPR_GET_PROPERTY) {
                // obj.prop++
                char *obj = codegen_expr(ctx, expr->as.postfix_inc.operand->as.get_property.object);
                const char *prop = expr->as.postfix_inc.operand->as.get_property.property;
                char *old_val = codegen_temp(ctx);
                char *new_val = codegen_temp(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_object_get_field(%s, \"%s\");", old_val, obj, prop);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, old_val);  // Return old value
                codegen_writeln(ctx, "hml_retain_if_needed(&%s);", result);
                codegen_writeln(ctx, "HmlValue %s = %s.type == HML_VAL_I32 ? hml_i32_inc(%s) : hml_binary_op(HML_OP_ADD, %s, hml_val_i32(1));", new_val, old_val, old_val, old_val);
                codegen_writeln(ctx, "hml_object_set_field(%s, \"%s\", %s);", obj, prop, new_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", old_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", new_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", obj);
                free(obj); free(old_val); free(new_val);
            } else {
                codegen_writeln(ctx, "hml_runtime_error(\"Invalid operand for ++\");");
                codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            }
            break;
        }

        case EXPR_POSTFIX_DEC: {
            if (expr->as.postfix_dec.operand->type == EXPR_IDENT) {
                const char *raw_var = expr->as.postfix_dec.operand->as.ident.name;
                const char *var;
                char prefixed_name[CODEGEN_MANGLED_NAME_SIZE];
                char *safe_var = NULL;
                if (ctx->current_module && !codegen_is_local(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "%s%s",
                            ctx->current_module->module_prefix, raw_var);
                    var = prefixed_name;
                } else if (codegen_is_main_var(ctx, raw_var) && !codegen_is_local(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "_main_%s", raw_var);
                    var = prefixed_name;
                } else {
                    // Local variable - sanitize to avoid C keyword conflicts
                    safe_var = codegen_sanitize_ident(raw_var);
                    var = safe_var;
                }
                // Check if variable is unboxed (native C type)
                // Skip if captured variable - captured vars are always HmlValue in closure env
                if (ctx->optimize && ctx->type_ctx && !codegen_is_main_var(ctx, raw_var) &&
                    !is_captured_variable(ctx, raw_var)) {
                    CheckedTypeKind native_type = type_check_get_unboxable(ctx->type_ctx, raw_var);
                    if (native_type != CHECKED_UNKNOWN && checked_kind_is_numeric(native_type)) {
                        const char *box_func = checked_type_to_box_func(native_type);
                        if (box_func) {
                            // Unboxed variable - return old value, then decrement
                            codegen_writeln(ctx, "HmlValue %s = %s(%s);", result, box_func, var);
                            codegen_writeln(ctx, "%s--;", var);
                            if (safe_var) free(safe_var);
                            break;
                        }
                    }
                }
                codegen_writeln(ctx, "HmlValue %s = %s;", result, var);
                codegen_writeln(ctx, "hml_retain_if_needed(&%s);", result);
                // Fast path for i32, fallback to generic binary_op
                codegen_writeln(ctx, "%s = %s.type == HML_VAL_I32 ? hml_i32_dec(%s) : hml_binary_op(HML_OP_SUB, %s, hml_val_i32(1));", var, var, var, var);
                // If captured variable, update closure environment
                if (ctx->current_closure && ctx->current_closure->num_captured > 0) {
                    for (int i = 0; i < ctx->current_closure->num_captured; i++) {
                        if (strcmp(ctx->current_closure->captured_vars[i], raw_var) == 0) {
                            int env_index = ctx->current_closure->shared_env_indices ?
                                           ctx->current_closure->shared_env_indices[i] : i;
                            codegen_writeln(ctx, "hml_closure_env_set(_closure_env, %d, %s);", env_index, var);
                            break;
                        }
                    }
                }
                if (safe_var) free(safe_var);
            } else if (expr->as.postfix_dec.operand->type == EXPR_INDEX) {
                // arr[i]--
                char *arr = codegen_expr(ctx, expr->as.postfix_dec.operand->as.index.object);
                char *idx = codegen_expr(ctx, expr->as.postfix_dec.operand->as.index.index);
                char *old_val = codegen_temp(ctx);
                char *new_val = codegen_temp(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_array_get(%s, %s);", old_val, arr, idx);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, old_val);  // Return old value
                codegen_writeln(ctx, "hml_retain_if_needed(&%s);", result);
                codegen_writeln(ctx, "HmlValue %s = %s.type == HML_VAL_I32 ? hml_i32_dec(%s) : hml_binary_op(HML_OP_SUB, %s, hml_val_i32(1));", new_val, old_val, old_val, old_val);
                codegen_writeln(ctx, "hml_array_set(%s, %s, %s);", arr, idx, new_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", old_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", new_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", idx);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", arr);
                free(arr); free(idx); free(old_val); free(new_val);
            } else if (expr->as.postfix_dec.operand->type == EXPR_GET_PROPERTY) {
                // obj.prop--
                char *obj = codegen_expr(ctx, expr->as.postfix_dec.operand->as.get_property.object);
                const char *prop = expr->as.postfix_dec.operand->as.get_property.property;
                char *old_val = codegen_temp(ctx);
                char *new_val = codegen_temp(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_object_get_field(%s, \"%s\");", old_val, obj, prop);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, old_val);  // Return old value
                codegen_writeln(ctx, "hml_retain_if_needed(&%s);", result);
                codegen_writeln(ctx, "HmlValue %s = %s.type == HML_VAL_I32 ? hml_i32_dec(%s) : hml_binary_op(HML_OP_SUB, %s, hml_val_i32(1));", new_val, old_val, old_val, old_val);
                codegen_writeln(ctx, "hml_object_set_field(%s, \"%s\", %s);", obj, prop, new_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", old_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", new_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", obj);
                free(obj); free(old_val); free(new_val);
            } else {
                codegen_writeln(ctx, "hml_runtime_error(\"Invalid operand for --\");");
                codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            }
            break;
        }

        case EXPR_STRING_INTERPOLATION: {
            // Build the string by concatenating parts
            codegen_writeln(ctx, "HmlValue %s = hml_val_string(\"\");", result);

            for (int i = 0; i <= expr->as.string_interpolation.num_parts; i++) {
                // Add string part (there are num_parts+1 string parts)
                if (expr->as.string_interpolation.string_parts[i] &&
                    strlen(expr->as.string_interpolation.string_parts[i]) > 0) {
                    char *escaped = codegen_escape_string(expr->as.string_interpolation.string_parts[i]);
                    char *part_temp = codegen_temp(ctx);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_string(\"%s\");", part_temp, escaped);
                    codegen_writeln(ctx, "HmlValue _concat%d = hml_string_concat(%s, %s);", ctx->temp_counter, result, part_temp);
                    codegen_writeln(ctx, "hml_release(&%s);", result);
                    codegen_writeln(ctx, "hml_release(&%s);", part_temp);
                    codegen_writeln(ctx, "%s = _concat%d;", result, ctx->temp_counter);
                    free(escaped);
                    free(part_temp);
                }

                // Add expression part (there are num_parts expression parts)
                if (i < expr->as.string_interpolation.num_parts) {
                    char *expr_val = codegen_expr(ctx, expr->as.string_interpolation.expr_parts[i]);
                    codegen_writeln(ctx, "HmlValue _concat%d = hml_string_concat(%s, %s);", ctx->temp_counter, result, expr_val);
                    codegen_writeln(ctx, "hml_release(&%s);", result);
                    codegen_writeln(ctx, "hml_release(&%s);", expr_val);
                    codegen_writeln(ctx, "%s = _concat%d;", result, ctx->temp_counter);
                    free(expr_val);
                }
            }
            break;
        }

        case EXPR_AWAIT: {
            // await expr - if value is a task, join it; otherwise return as-is
            char *awaited = codegen_expr(ctx, expr->as.await_expr.awaited_expr);
            codegen_writeln(ctx, "HmlValue %s;", result);
            codegen_writeln(ctx, "if (%s.type == HML_VAL_TASK) {", awaited);
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "%s = hml_join(%s);", result, awaited);
            codegen_writeln(ctx, "hml_release(&%s);", awaited);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "} else {");
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "%s = %s;", result, awaited);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            free(awaited);
            break;
        }

        case EXPR_NULL_COALESCE: {
            // left ?? right
            char *left = codegen_expr(ctx, expr->as.null_coalesce.left);
            codegen_writeln(ctx, "HmlValue %s;", result);
            codegen_writeln(ctx, "if (!hml_is_null(%s)) {", left);
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "%s = %s;", result, left);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "} else {");
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "hml_release(&%s);", left);
            char *right = codegen_expr(ctx, expr->as.null_coalesce.right);
            codegen_writeln(ctx, "%s = %s;", result, right);
            free(right);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            free(left);
            break;
        }

        case EXPR_OPTIONAL_CHAIN: {
            // obj?.property or obj?.[index] or obj?.method()
            char *obj = codegen_expr(ctx, expr->as.optional_chain.object);
            codegen_writeln(ctx, "HmlValue %s;", result);
            codegen_writeln(ctx, "if (hml_is_null(%s)) {", obj);
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "%s = hml_val_null();", result);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "} else {");
            codegen_indent_inc(ctx);

            if (expr->as.optional_chain.is_property) {
                // obj?.property - check for built-in properties like .length
                const char *prop = expr->as.optional_chain.property;
                if (strcmp(prop, "length") == 0) {
                    codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY) {", obj);
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "%s = hml_array_length(%s);", result, obj);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "} else if (%s.type == HML_VAL_STRING) {", obj);
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "%s = hml_string_length(%s);", result, obj);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "} else if (%s.type == HML_VAL_BUFFER) {", obj);
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "%s = hml_buffer_length(%s);", result, obj);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "} else {");
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "%s = hml_object_get_field(%s, \"length\");", result, obj);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "}");
                } else {
                    codegen_writeln(ctx, "%s = hml_object_get_field(%s, \"%s\");", result, obj, prop);
                }
            } else if (expr->as.optional_chain.is_call) {
                // obj?.(args) - call obj directly if not null
                int num_args = expr->as.optional_chain.num_args;
                int args_counter = ctx->temp_counter++;

                // Evaluate arguments
                char **arg_temps = NULL;
                if (num_args > 0) {
                    arg_temps = malloc(num_args * sizeof(char*));
                    if (!arg_temps) {
                        codegen_error(ctx, 0, "Memory allocation failed for optional chain call arguments");
                        free(result);
                        free(obj);
                        return strdup("hml_val_null()");
                    }
                }
                for (int i = 0; i < num_args; i++) {
                    arg_temps[i] = codegen_expr(ctx, expr->as.optional_chain.args[i]);
                }

                // Build args array and call
                if (num_args > 0) {
                    codegen_writeln(ctx, "HmlValue _args%d[%d];", args_counter, num_args);
                    for (int i = 0; i < num_args; i++) {
                        codegen_writeln(ctx, "_args%d[%d] = %s;", args_counter, i, arg_temps[i]);
                    }
                    codegen_writeln(ctx, "%s = hml_call_function(%s, _args%d, %d);",
                                  result, obj, args_counter, num_args);
                } else {
                    codegen_writeln(ctx, "%s = hml_call_function(%s, NULL, 0);", result, obj);
                }

                // Release argument temporaries
                for (int i = 0; i < num_args; i++) {
                    codegen_writeln(ctx, "hml_release(&%s);", arg_temps[i]);
                    free(arg_temps[i]);
                }
                free(arg_temps);
            } else {
                // obj?.[index]
                char *idx = codegen_expr(ctx, expr->as.optional_chain.index);
                codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_array_get(%s, %s);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_STRING) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_string_index(%s, %s);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_BUFFER) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_buffer_get(%s, %s);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_val_null();", result);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
                codegen_writeln(ctx, "hml_release(&%s);", idx);
                free(idx);
            }

            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            codegen_writeln(ctx, "hml_release(&%s);", obj);
            free(obj);
            break;
        }

        case EXPR_MATCH: {
            // Match expression: match (scrutinee) { pattern => body, ... }
            char *scrutinee = codegen_expr(ctx, expr->as.match_expr.scrutinee);
            codegen_writeln(ctx, "HmlValue %s;", result);

            // Generate labels for each arm and the end
            char **arm_labels = NULL;
            if (expr->as.match_expr.num_arms > 0) {
                arm_labels = malloc(expr->as.match_expr.num_arms * sizeof(char*));
                if (!arm_labels) {
                    codegen_error(ctx, 0, "Memory allocation failed for match expression arm labels");
                    free(result);
                    free(scrutinee);
                    return strdup("hml_val_null()");
                }
            }
            char *end_label = codegen_label(ctx);
            char *no_match_label = codegen_label(ctx);

            for (int i = 0; i < expr->as.match_expr.num_arms; i++) {
                arm_labels[i] = codegen_label(ctx);
            }

            // Generate pattern matching code for each arm
            for (int i = 0; i < expr->as.match_expr.num_arms; i++) {
                MatchArm *arm = &expr->as.match_expr.arms[i];
                char *next_arm = (i + 1 < expr->as.match_expr.num_arms) ? arm_labels[i + 1] : no_match_label;

                codegen_writeln(ctx, "// Match arm %d", i);
                codegen_writeln(ctx, "%s:;", arm_labels[i]);
                int arm_locals_start = ctx->num_locals;
                codegen_push_scope(ctx);
                codegen_writeln(ctx, "{");
                codegen_indent_inc(ctx);

                // Generate pattern match condition
                codegen_pattern_match(ctx, arm->pattern, scrutinee, next_arm);

                // Generate guard check if present
                if (arm->guard) {
                    char *guard_val = codegen_expr(ctx, arm->guard);
                    codegen_writeln(ctx, "if (!hml_to_bool(%s)) {", guard_val);
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "hml_release(&%s);", guard_val);
                    // Release any bindings created by pattern before jumping
                    codegen_writeln(ctx, "goto %s;", next_arm);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "}");
                    codegen_writeln(ctx, "hml_release(&%s);", guard_val);
                    free(guard_val);
                }

                // Evaluate body and store result
                char *body_val = codegen_expr(ctx, arm->body);
                codegen_writeln(ctx, "%s = %s;", result, body_val);
                free(body_val);

                // Jump to end
                codegen_writeln(ctx, "goto %s;", end_label);

                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
                // Restore num_locals (match arm bindings are out of C scope)
                ctx->num_locals = arm_locals_start;
                codegen_pop_scope(ctx);
            }

            // No match - runtime error
            codegen_writeln(ctx, "%s:;", no_match_label);
            codegen_writeln(ctx, "hml_runtime_error(\"No pattern matched in match expression\");");
            codegen_writeln(ctx, "%s = hml_val_null();", result);

            // End label
            codegen_writeln(ctx, "%s:;", end_label);

            // Cleanup
            codegen_writeln(ctx, "hml_release(&%s);", scrutinee);
            free(scrutinee);

            for (int i = 0; i < expr->as.match_expr.num_arms; i++) {
                free(arm_labels[i]);
            }
            free(arm_labels);
            free(end_label);
            free(no_match_label);
            break;
        }

        default:
            codegen_error(ctx, expr->line, "unsupported expression type %d", expr->type);
            codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            break;
    }

    return result;
}


