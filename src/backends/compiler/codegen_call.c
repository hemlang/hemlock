/*
 * Hemlock Code Generator - Call Expression Handling
 *
 * Handles code generation for EXPR_CALL - function calls,
 * method calls, and builtin invocations.
 *
 * Extracted from codegen_expr.c to reduce file size.
 */

#include "codegen_call_internal.h"

/*
 * Check if an expression references a main variable that is currently
 * shadowed by a block-local variable. Used to prevent incorrect inlining.
 * Returns 1 if there's a conflict, 0 if safe to inline.
 */
static int codegen_inline_has_shadow_conflict(CodegenContext *ctx, Expr *expr) {
    if (!expr) return 0;
    switch (expr->type) {
        case EXPR_IDENT:
            // A main var that's shadowed in the current scope would resolve
            // to the shadow instead of _main_<name> when inlined
            if (codegen_is_main_var(ctx, expr->as.ident.name) &&
                ctx->current_scope &&
                scope_is_defined(ctx->current_scope, expr->as.ident.name)) {
                return 1;
            }
            return 0;
        case EXPR_BINARY:
            return codegen_inline_has_shadow_conflict(ctx, expr->as.binary.left) ||
                   codegen_inline_has_shadow_conflict(ctx, expr->as.binary.right);
        case EXPR_UNARY:
            return codegen_inline_has_shadow_conflict(ctx, expr->as.unary.operand);
        case EXPR_CALL:
            if (codegen_inline_has_shadow_conflict(ctx, expr->as.call.func)) return 1;
            for (int i = 0; i < expr->as.call.num_args; i++) {
                if (codegen_inline_has_shadow_conflict(ctx, expr->as.call.args[i])) return 1;
            }
            return 0;
        case EXPR_INDEX:
            return codegen_inline_has_shadow_conflict(ctx, expr->as.index.object) ||
                   codegen_inline_has_shadow_conflict(ctx, expr->as.index.index);
        case EXPR_GET_PROPERTY:
            return codegen_inline_has_shadow_conflict(ctx, expr->as.get_property.object);
        case EXPR_TERNARY:
            return codegen_inline_has_shadow_conflict(ctx, expr->as.ternary.condition) ||
                   codegen_inline_has_shadow_conflict(ctx, expr->as.ternary.true_expr) ||
                   codegen_inline_has_shadow_conflict(ctx, expr->as.ternary.false_expr);
        default:
            return 0;
    }
}

/*
 * Generate a pointer expression for ref parameter passing.
 * For EXPR_IDENT: returns "&_main_varname" or "&varname"
 * For other expressions: evaluates to temp and returns "&temp"
 * Returns allocated string that must be freed.
 */
char* codegen_ref_arg(CodegenContext *ctx, Expr *arg) {
    if (arg->type == EXPR_IDENT) {
        // Simple variable - return address of the actual variable
        const char *var_name = arg->as.ident.name;
        char *result = malloc(CODEGEN_MANGLED_NAME_SIZE);
        if (!result) {
            fprintf(stderr, "error: Out of memory in codegen_ref_arg\n");
            exit(1);
        }

        if (codegen_is_main_var(ctx, var_name)) {
            snprintf(result, CODEGEN_MANGLED_NAME_SIZE, "&_main_%s", var_name);
        } else if (ctx->current_module && !codegen_is_local(ctx, var_name)) {
            snprintf(result, CODEGEN_MANGLED_NAME_SIZE, "&%s%s",
                    ctx->current_module->module_prefix, var_name);
        } else {
            char *safe_ident = codegen_sanitize_ident(var_name);
            snprintf(result, CODEGEN_MANGLED_NAME_SIZE, "&%s", safe_ident);
            free(safe_ident);
        }
        return result;
    } else {
        // Complex expression - evaluate to temp and take address
        // This won't work for true pass-by-ref semantics (changes won't persist)
        // but it's the best we can do for non-lvalue expressions
        char *temp = codegen_expr(ctx, arg);
        size_t len = strlen(temp) + 2;
        char *result = malloc(len);
        if (!result) {
            fprintf(stderr, "error: Out of memory in codegen_ref_arg\n");
            free(temp);
            exit(1);
        }
        snprintf(result, len, "&%s", temp);
        free(temp);
        return result;
    }
}

/*
 * Handle EXPR_CALL - generates code for call expressions.
 * This includes:
 * - Builtin function calls (print, typeof, alloc, etc.)
 * - User-defined function calls
 * - Method calls on objects
 * - FFI function calls
 *
 * Returns the temp variable name containing the result (same as result param).
 */
char* codegen_expr_call(CodegenContext *ctx, Expr *expr, char *result) {
    // Check for builtin function calls
    if (expr->as.call.func->type == EXPR_IDENT) {
        const char *fn_name = expr->as.call.func->as.ident.name;
        Expr **call_args = expr->as.call.args;
        int num_args = expr->as.call.num_args;

        // Dispatch to extracted builtin handlers
        if (codegen_call_io(ctx, expr, result, fn_name, call_args, num_args)) return result;
        if (codegen_call_types(ctx, expr, result, fn_name, call_args, num_args)) return result;
        if (codegen_call_async(ctx, expr, result, fn_name, call_args, num_args)) return result;
        if (codegen_call_strings(ctx, expr, result, fn_name, call_args, num_args)) return result;
        if (codegen_call_arrays(ctx, expr, result, fn_name, call_args, num_args)) return result;

        // Handle assert builtin
        if (strcmp(fn_name, "assert") == 0 && expr->as.call.num_args >= 1) {
            char *cond = codegen_expr(ctx, expr->as.call.args[0]);
            if (expr->as.call.num_args >= 2) {
                char *msg = codegen_expr(ctx, expr->as.call.args[1]);
                codegen_writeln(ctx, "hml_assert(%s, %s);", cond, msg);
                codegen_writeln(ctx, "hml_release(&%s);", msg);
                free(msg);
            } else {
                codegen_writeln(ctx, "hml_assert(%s, hml_val_null());", cond);
            }
            codegen_writeln(ctx, "hml_release(&%s);", cond);
            codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            free(cond);
            return result;
        }

        // Handle panic builtin
        if (strcmp(fn_name, "panic") == 0) {
            if (expr->as.call.num_args >= 1) {
                char *msg = codegen_expr(ctx, expr->as.call.args[0]);
                codegen_writeln(ctx, "hml_panic(%s);", msg);
                free(msg);
            } else {
                codegen_writeln(ctx, "hml_panic(hml_val_string(\"panic!\"));");
            }
            codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            return result;
        }

        // Handle exec builtin for command execution (1 arg: shell mode, 2 args: safe mode)
        // Skip if 'exec' is an imported symbol (e.g. from @stdlib/sqlite)
        // or a local/module function that shadows the builtin
        int exec_is_imported = 0;
        if (strcmp(fn_name, "exec") == 0 || strcmp(fn_name, "__exec") == 0) {
            if (ctx->current_module) {
                exec_is_imported = module_find_import(ctx->current_module, fn_name) != NULL ||
                                   module_find_export(ctx->current_module, fn_name) != NULL;
            } else {
                exec_is_imported = codegen_find_main_import(ctx, fn_name) != NULL;
            }
            if (!exec_is_imported && codegen_is_local(ctx, fn_name)) {
                exec_is_imported = 1;
            }
        }
        if (!exec_is_imported && (strcmp(fn_name, "exec") == 0 || strcmp(fn_name, "__exec") == 0) && expr->as.call.num_args == 1) {
            char *cmd = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_exec(%s);", result, cmd);
            codegen_writeln(ctx, "hml_release(&%s);", cmd);
            free(cmd);
            return result;
        }

        // Handle exec builtin with args array (safe mode, no shell)
        if (!exec_is_imported && (strcmp(fn_name, "exec") == 0 || strcmp(fn_name, "__exec") == 0) && expr->as.call.num_args == 2) {
            char *cmd = codegen_expr(ctx, expr->as.call.args[0]);
            char *args = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_exec_with_args(%s, %s);", result, cmd, args);
            codegen_writeln(ctx, "hml_release(&%s);", cmd);
            codegen_writeln(ctx, "hml_release(&%s);", args);
            free(cmd);
            free(args);
            return result;
        }

        // Handle exec_argv builtin for safe command execution (no shell)
        if ((strcmp(fn_name, "exec_argv") == 0 || strcmp(fn_name, "__exec_argv") == 0) && expr->as.call.num_args == 1) {
            char *args = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_exec_argv(%s);", result, args);
            codegen_writeln(ctx, "hml_release(&%s);", args);
            free(args);
            return result;
        }

        // Handle apply builtin
        if (strcmp(fn_name, "apply") == 0 && expr->as.call.num_args == 2) {
            char *fn_val = codegen_expr(ctx, expr->as.call.args[0]);
            char *args_val = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_apply(%s, %s);", result, fn_val, args_val);
            codegen_writeln(ctx, "hml_release(&%s);", fn_val);
            codegen_writeln(ctx, "hml_release(&%s);", args_val);
            free(fn_val);
            free(args_val);
            return result;
        }

        // Handle signal builtin (via @stdlib/signal import or __signal)
        if ((strcmp(fn_name, "signal") == 0 || strcmp(fn_name, "__signal") == 0) && expr->as.call.num_args == 2) {
            char *signum = codegen_expr(ctx, expr->as.call.args[0]);
            char *handler = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_signal(%s, %s);", result, signum, handler);
            codegen_writeln(ctx, "hml_release(&%s);", signum);
            codegen_writeln(ctx, "hml_release(&%s);", handler);
            free(signum);
            free(handler);
            return result;
        }

        // Handle raise builtin (via @stdlib/signal import or __raise)
        if ((strcmp(fn_name, "raise") == 0 || strcmp(fn_name, "__raise") == 0) && expr->as.call.num_args == 1) {
            char *signum = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_raise(%s);", result, signum);
            codegen_writeln(ctx, "hml_release(&%s);", signum);
            free(signum);
            return result;
        }

        // Handle alloc builtin
        if (strcmp(fn_name, "alloc") == 0 && expr->as.call.num_args == 1) {
            char *size = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_alloc(hml_to_i32(%s));", result, size);
            codegen_writeln(ctx, "hml_release(&%s);", size);
            free(size);
            return result;
        }

        // Handle free builtin
        // Note: Don't call hml_release after hml_free - the memory is already freed
        if (strcmp(fn_name, "free") == 0 && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "hml_free(%s);", ptr);
            codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            free(ptr);
            return result;
        }

        // Handle buffer builtin
        if (strcmp(fn_name, "buffer") == 0 && expr->as.call.num_args == 1) {
            char *size = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_val_buffer(hml_to_i32(%s));", result, size);
            codegen_writeln(ctx, "hml_release(&%s);", size);
            free(size);
            return result;
        }

        // Handle memset builtin
        if (strcmp(fn_name, "memset") == 0 && expr->as.call.num_args == 3) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *byte_val = codegen_expr(ctx, expr->as.call.args[1]);
            char *size = codegen_expr(ctx, expr->as.call.args[2]);
            codegen_writeln(ctx, "hml_memset(%s, (uint8_t)hml_to_i32(%s), hml_to_i32(%s));", ptr, byte_val, size);
            codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", byte_val);
            codegen_writeln(ctx, "hml_release(&%s);", size);
            free(ptr);
            free(byte_val);
            free(size);
            return result;
        }

        // Handle memcpy builtin
        if (strcmp(fn_name, "memcpy") == 0 && expr->as.call.num_args == 3) {
            char *dest = codegen_expr(ctx, expr->as.call.args[0]);
            char *src = codegen_expr(ctx, expr->as.call.args[1]);
            char *size = codegen_expr(ctx, expr->as.call.args[2]);
            codegen_writeln(ctx, "hml_memcpy(%s, %s, hml_to_i32(%s));", dest, src, size);
            codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            codegen_writeln(ctx, "hml_release(&%s);", dest);
            codegen_writeln(ctx, "hml_release(&%s);", src);
            codegen_writeln(ctx, "hml_release(&%s);", size);
            free(dest);
            free(src);
            free(size);
            return result;
        }

        // Handle realloc builtin
        if (strcmp(fn_name, "realloc") == 0 && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *size = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_realloc(%s, hml_to_i32(%s));", result, ptr, size);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", size);
            free(ptr);
            free(size);
            return result;
        }

        // ========== FFI CALLBACK BUILTINS ==========

        // callback(fn, param_types, return_type) -> ptr
        if ((strcmp(fn_name, "callback") == 0 || strcmp(fn_name, "__callback") == 0) && (expr->as.call.num_args == 2 || expr->as.call.num_args == 3)) {
            char *fn_arg = codegen_expr(ctx, expr->as.call.args[0]);
            char *param_types = codegen_expr(ctx, expr->as.call.args[1]);
            char *ret_type;
            if (expr->as.call.num_args == 3) {
                ret_type = codegen_expr(ctx, expr->as.call.args[2]);
            } else {
                ret_type = strdup("hml_val_string(\"void\")");
            }
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_callback(NULL, %s, %s, %s);", result, fn_arg, param_types, ret_type);
            codegen_writeln(ctx, "hml_release(&%s);", fn_arg);
            codegen_writeln(ctx, "hml_release(&%s);", param_types);
            if (expr->as.call.num_args == 3) {
                codegen_writeln(ctx, "hml_release(&%s);", ret_type);
            }
            free(fn_arg);
            free(param_types);
            free(ret_type);
            return result;
        }

        // callback_free(ptr)
        if ((strcmp(fn_name, "callback_free") == 0 || strcmp(fn_name, "__callback_free") == 0) && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_callback_free(NULL, %s);", result, ptr);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(ptr);
            return result;
        }

        // ptr_deref_i32(ptr) -> i32
        if (strcmp(fn_name, "ptr_deref_i32") == 0 && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_deref_i32(NULL, %s);", result, ptr);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(ptr);
            return result;
        }

        // ptr_write_i32(ptr, value)
        if (strcmp(fn_name, "ptr_write_i32") == 0 && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *value = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_write_i32(NULL, %s, %s);", result, ptr, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(value);
            return result;
        }

        // ptr_offset(ptr, offset, element_size) -> ptr
        if (strcmp(fn_name, "ptr_offset") == 0 && expr->as.call.num_args == 3) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *offset = codegen_expr(ctx, expr->as.call.args[1]);
            char *elem_size = codegen_expr(ctx, expr->as.call.args[2]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_offset(NULL, %s, %s, %s);", result, ptr, offset, elem_size);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", offset);
            codegen_writeln(ctx, "hml_release(&%s);", elem_size);
            free(ptr);
            free(offset);
            free(elem_size);
            return result;
        }

        // ptr_read_i32(ptr) -> i32 (dereference pointer-to-pointer, for qsort)
        if (strcmp(fn_name, "ptr_read_i32") == 0 && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_read_i32(NULL, %s);", result, ptr);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(ptr);
            return result;
        }

        // ========== ADDITIONAL POINTER HELPERS FOR ALL TYPES ==========

        // ptr_deref_i8(ptr) -> i8
        if (strcmp(fn_name, "ptr_deref_i8") == 0 && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_deref_i8(NULL, %s);", result, ptr);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(ptr);
            return result;
        }

        // ptr_deref_i16(ptr) -> i16
        if (strcmp(fn_name, "ptr_deref_i16") == 0 && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_deref_i16(NULL, %s);", result, ptr);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(ptr);
            return result;
        }

        // ptr_deref_i64(ptr) -> i64
        if (strcmp(fn_name, "ptr_deref_i64") == 0 && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_deref_i64(NULL, %s);", result, ptr);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(ptr);
            return result;
        }

        // ptr_deref_u8(ptr) -> u8
        if (strcmp(fn_name, "ptr_deref_u8") == 0 && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_deref_u8(NULL, %s);", result, ptr);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(ptr);
            return result;
        }

        // ptr_deref_u16(ptr) -> u16
        if (strcmp(fn_name, "ptr_deref_u16") == 0 && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_deref_u16(NULL, %s);", result, ptr);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(ptr);
            return result;
        }

        // ptr_deref_u32(ptr) -> u32
        if (strcmp(fn_name, "ptr_deref_u32") == 0 && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_deref_u32(NULL, %s);", result, ptr);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(ptr);
            return result;
        }

        // ptr_deref_u64(ptr) -> u64
        if (strcmp(fn_name, "ptr_deref_u64") == 0 && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_deref_u64(NULL, %s);", result, ptr);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(ptr);
            return result;
        }

        // ptr_deref_f32(ptr) -> f32
        if (strcmp(fn_name, "ptr_deref_f32") == 0 && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_deref_f32(NULL, %s);", result, ptr);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(ptr);
            return result;
        }

        // ptr_deref_f64(ptr) -> f64
        if (strcmp(fn_name, "ptr_deref_f64") == 0 && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_deref_f64(NULL, %s);", result, ptr);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(ptr);
            return result;
        }

        // ptr_deref_ptr(ptr) -> ptr (pointer-to-pointer)
        if (strcmp(fn_name, "ptr_deref_ptr") == 0 && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_deref_ptr(NULL, %s);", result, ptr);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(ptr);
            return result;
        }

        // ptr_write_i8(ptr, value)
        if (strcmp(fn_name, "ptr_write_i8") == 0 && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *value = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_write_i8(NULL, %s, %s);", result, ptr, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(value);
            return result;
        }

        // ptr_write_i16(ptr, value)
        if (strcmp(fn_name, "ptr_write_i16") == 0 && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *value = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_write_i16(NULL, %s, %s);", result, ptr, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(value);
            return result;
        }

        // ptr_write_i64(ptr, value)
        if (strcmp(fn_name, "ptr_write_i64") == 0 && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *value = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_write_i64(NULL, %s, %s);", result, ptr, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(value);
            return result;
        }

        // ptr_write_u8(ptr, value)
        if (strcmp(fn_name, "ptr_write_u8") == 0 && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *value = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_write_u8(NULL, %s, %s);", result, ptr, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(value);
            return result;
        }

        // ptr_write_u16(ptr, value)
        if (strcmp(fn_name, "ptr_write_u16") == 0 && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *value = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_write_u16(NULL, %s, %s);", result, ptr, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(value);
            return result;
        }

        // ptr_write_u32(ptr, value)
        if (strcmp(fn_name, "ptr_write_u32") == 0 && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *value = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_write_u32(NULL, %s, %s);", result, ptr, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(value);
            return result;
        }

        // ptr_write_u64(ptr, value)
        if (strcmp(fn_name, "ptr_write_u64") == 0 && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *value = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_write_u64(NULL, %s, %s);", result, ptr, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(value);
            return result;
        }

        // ptr_write_f32(ptr, value)
        if (strcmp(fn_name, "ptr_write_f32") == 0 && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *value = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_write_f32(NULL, %s, %s);", result, ptr, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(value);
            return result;
        }

        // ptr_write_f64(ptr, value)
        if (strcmp(fn_name, "ptr_write_f64") == 0 && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *value = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_write_f64(NULL, %s, %s);", result, ptr, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(value);
            return result;
        }

        // ptr_write_ptr(ptr, value)
        if (strcmp(fn_name, "ptr_write_ptr") == 0 && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *value = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_write_ptr(NULL, %s, %s);", result, ptr, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(value);
            return result;
        }

        // ptr_read_i8(ptr) -> i8
        if (strcmp(fn_name, "ptr_read_i8") == 0 && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_read_i8(NULL, %s);", result, ptr);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(ptr);
            return result;
        }

        // ptr_read_i16(ptr) -> i16
        if (strcmp(fn_name, "ptr_read_i16") == 0 && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_read_i16(NULL, %s);", result, ptr);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(ptr);
            return result;
        }

        // ptr_read_u8(ptr) -> u8
        if (strcmp(fn_name, "ptr_read_u8") == 0 && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_read_u8(NULL, %s);", result, ptr);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(ptr);
            return result;
        }

        // ptr_read_u16(ptr) -> u16
        if (strcmp(fn_name, "ptr_read_u16") == 0 && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_read_u16(NULL, %s);", result, ptr);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(ptr);
            return result;
        }

        // ptr_read_u32(ptr) -> u32
        if (strcmp(fn_name, "ptr_read_u32") == 0 && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_read_u32(NULL, %s);", result, ptr);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(ptr);
            return result;
        }

        // ptr_read_ptr(ptr) -> ptr
        if (strcmp(fn_name, "ptr_read_ptr") == 0 && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_read_ptr(NULL, %s);", result, ptr);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(ptr);
            return result;
        }

        // ptr_read_i64(ptr) -> i64
        if (strcmp(fn_name, "ptr_read_i64") == 0 && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_read_i64(NULL, %s);", result, ptr);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(ptr);
            return result;
        }

        // ptr_read_u64(ptr) -> u64
        if (strcmp(fn_name, "ptr_read_u64") == 0 && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_read_u64(NULL, %s);", result, ptr);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(ptr);
            return result;
        }

        // ptr_read_f32(ptr) -> f32
        if (strcmp(fn_name, "ptr_read_f32") == 0 && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_read_f32(NULL, %s);", result, ptr);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(ptr);
            return result;
        }

        // ptr_read_f64(ptr) -> f64
        if (strcmp(fn_name, "ptr_read_f64") == 0 && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_read_f64(NULL, %s);", result, ptr);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(ptr);
            return result;
        }

        // ========== ATOMIC OPERATIONS (i32) ==========

        if ((strcmp(fn_name, "atomic_load_i32") == 0 || strcmp(fn_name, "__atomic_load_i32") == 0) && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_atomic_load_i32(NULL, %s);", result, ptr);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(ptr);
            return result;
        }

        if ((strcmp(fn_name, "atomic_store_i32") == 0 || strcmp(fn_name, "__atomic_store_i32") == 0) && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *value = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_atomic_store_i32(NULL, %s, %s);", result, ptr, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(value);
            return result;
        }

        if ((strcmp(fn_name, "atomic_add_i32") == 0 || strcmp(fn_name, "__atomic_add_i32") == 0) && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *value = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_atomic_add_i32(NULL, %s, %s);", result, ptr, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(value);
            return result;
        }

        if ((strcmp(fn_name, "atomic_sub_i32") == 0 || strcmp(fn_name, "__atomic_sub_i32") == 0) && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *value = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_atomic_sub_i32(NULL, %s, %s);", result, ptr, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(value);
            return result;
        }

        if ((strcmp(fn_name, "atomic_and_i32") == 0 || strcmp(fn_name, "__atomic_and_i32") == 0) && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *value = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_atomic_and_i32(NULL, %s, %s);", result, ptr, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(value);
            return result;
        }

        if ((strcmp(fn_name, "atomic_or_i32") == 0 || strcmp(fn_name, "__atomic_or_i32") == 0) && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *value = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_atomic_or_i32(NULL, %s, %s);", result, ptr, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(value);
            return result;
        }

        if ((strcmp(fn_name, "atomic_xor_i32") == 0 || strcmp(fn_name, "__atomic_xor_i32") == 0) && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *value = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_atomic_xor_i32(NULL, %s, %s);", result, ptr, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(value);
            return result;
        }

        if ((strcmp(fn_name, "atomic_cas_i32") == 0 || strcmp(fn_name, "__atomic_cas_i32") == 0) && expr->as.call.num_args == 3) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *expected = codegen_expr(ctx, expr->as.call.args[1]);
            char *desired = codegen_expr(ctx, expr->as.call.args[2]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_atomic_cas_i32(NULL, %s, %s, %s);", result, ptr, expected, desired);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", expected);
            codegen_writeln(ctx, "hml_release(&%s);", desired);
            free(ptr);
            free(expected);
            free(desired);
            return result;
        }

        if ((strcmp(fn_name, "atomic_exchange_i32") == 0 || strcmp(fn_name, "__atomic_exchange_i32") == 0) && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *value = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_atomic_exchange_i32(NULL, %s, %s);", result, ptr, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(value);
            return result;
        }

        // ========== ATOMIC OPERATIONS (i64) ==========

        if ((strcmp(fn_name, "atomic_load_i64") == 0 || strcmp(fn_name, "__atomic_load_i64") == 0) && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_atomic_load_i64(NULL, %s);", result, ptr);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(ptr);
            return result;
        }

        if ((strcmp(fn_name, "atomic_store_i64") == 0 || strcmp(fn_name, "__atomic_store_i64") == 0) && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *value = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_atomic_store_i64(NULL, %s, %s);", result, ptr, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(value);
            return result;
        }

        if ((strcmp(fn_name, "atomic_add_i64") == 0 || strcmp(fn_name, "__atomic_add_i64") == 0) && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *value = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_atomic_add_i64(NULL, %s, %s);", result, ptr, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(value);
            return result;
        }

        if ((strcmp(fn_name, "atomic_sub_i64") == 0 || strcmp(fn_name, "__atomic_sub_i64") == 0) && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *value = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_atomic_sub_i64(NULL, %s, %s);", result, ptr, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(value);
            return result;
        }

        if ((strcmp(fn_name, "atomic_and_i64") == 0 || strcmp(fn_name, "__atomic_and_i64") == 0) && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *value = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_atomic_and_i64(NULL, %s, %s);", result, ptr, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(value);
            return result;
        }

        if ((strcmp(fn_name, "atomic_or_i64") == 0 || strcmp(fn_name, "__atomic_or_i64") == 0) && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *value = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_atomic_or_i64(NULL, %s, %s);", result, ptr, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(value);
            return result;
        }

        if ((strcmp(fn_name, "atomic_xor_i64") == 0 || strcmp(fn_name, "__atomic_xor_i64") == 0) && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *value = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_atomic_xor_i64(NULL, %s, %s);", result, ptr, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(value);
            return result;
        }

        if ((strcmp(fn_name, "atomic_cas_i64") == 0 || strcmp(fn_name, "__atomic_cas_i64") == 0) && expr->as.call.num_args == 3) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *expected = codegen_expr(ctx, expr->as.call.args[1]);
            char *desired = codegen_expr(ctx, expr->as.call.args[2]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_atomic_cas_i64(NULL, %s, %s, %s);", result, ptr, expected, desired);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", expected);
            codegen_writeln(ctx, "hml_release(&%s);", desired);
            free(ptr);
            free(expected);
            free(desired);
            return result;
        }

        if ((strcmp(fn_name, "atomic_exchange_i64") == 0 || strcmp(fn_name, "__atomic_exchange_i64") == 0) && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *value = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_atomic_exchange_i64(NULL, %s, %s);", result, ptr, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(value);
            return result;
        }

        // ========== ATOMIC FENCE ==========

        if ((strcmp(fn_name, "atomic_fence") == 0 || strcmp(fn_name, "__atomic_fence") == 0) && expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_atomic_fence(NULL);", result);
            return result;
        }

        // ========== MEMORY-MAPPED FILE I/O ==========

        // mmap_open(path, mode?) -> ptr
        if (strcmp(fn_name, "__mmap_open") == 0 && (expr->as.call.num_args == 1 || expr->as.call.num_args == 2)) {
            char *path = codegen_expr(ctx, expr->as.call.args[0]);
            if (expr->as.call.num_args == 2) {
                char *mode = codegen_expr(ctx, expr->as.call.args[1]);
                codegen_writeln(ctx, "HmlValue %s = hml_builtin_mmap_open(NULL, %s, %s);", result, path, mode);
                codegen_writeln(ctx, "hml_release(&%s);", mode);
                free(mode);
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_builtin_mmap_open(NULL, %s, hml_val_null());", result, path);
            }
            codegen_writeln(ctx, "hml_release(&%s);", path);
            free(path);
            return result;
        }

        // mmap_open_anon(size) -> ptr
        if (strcmp(fn_name, "__mmap_open_anon") == 0 && expr->as.call.num_args == 1) {
            char *size = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_mmap_open_anon(NULL, %s);", result, size);
            codegen_writeln(ctx, "hml_release(&%s);", size);
            free(size);
            return result;
        }

        // mmap_sync(ptr) -> bool
        if (strcmp(fn_name, "__mmap_sync") == 0 && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_mmap_sync(NULL, %s);", result, ptr);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(ptr);
            return result;
        }

        // mmap_close(ptr) -> bool
        if (strcmp(fn_name, "__mmap_close") == 0 && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_mmap_close(NULL, %s);", result, ptr);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(ptr);
            return result;
        }

        // mmap_size(ptr) -> i64
        if (strcmp(fn_name, "__mmap_size") == 0 && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_mmap_size(NULL, %s);", result, ptr);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(ptr);
            return result;
        }

        // mmap_advise(ptr, advice) -> bool
        if (strcmp(fn_name, "__mmap_advise") == 0 && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *advice = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_mmap_advise(NULL, %s, %s);", result, ptr, advice);
            codegen_writeln(ctx, "hml_release(&%s);", advice);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(advice);
            free(ptr);
            return result;
        }

        // mmap_protect(ptr, prot) -> bool
        if (strcmp(fn_name, "__mmap_protect") == 0 && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *prot = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_mmap_protect(NULL, %s, %s);", result, ptr, prot);
            codegen_writeln(ctx, "hml_release(&%s);", prot);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(prot);
            free(ptr);
            return result;
        }

        // ========== BYTE ORDER OPERATIONS ==========

        // bswap16(val) -> u16
        if ((strcmp(fn_name, "bswap16") == 0 || strcmp(fn_name, "__bswap16") == 0) && expr->as.call.num_args == 1) {
            char *val = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_bswap16(NULL, %s);", result, val);
            codegen_writeln(ctx, "hml_release(&%s);", val);
            free(val);
            return result;
        }

        // bswap32(val) -> u32
        if ((strcmp(fn_name, "bswap32") == 0 || strcmp(fn_name, "__bswap32") == 0) && expr->as.call.num_args == 1) {
            char *val = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_bswap32(NULL, %s);", result, val);
            codegen_writeln(ctx, "hml_release(&%s);", val);
            free(val);
            return result;
        }

        // bswap64(val) -> u64
        if ((strcmp(fn_name, "bswap64") == 0 || strcmp(fn_name, "__bswap64") == 0) && expr->as.call.num_args == 1) {
            char *val = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_bswap64(NULL, %s);", result, val);
            codegen_writeln(ctx, "hml_release(&%s);", val);
            free(val);
            return result;
        }

        // htons(val) -> u16
        if ((strcmp(fn_name, "htons") == 0 || strcmp(fn_name, "__htons") == 0) && expr->as.call.num_args == 1) {
            char *val = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_htons(NULL, %s);", result, val);
            codegen_writeln(ctx, "hml_release(&%s);", val);
            free(val);
            return result;
        }

        // htonl(val) -> u32
        if ((strcmp(fn_name, "htonl") == 0 || strcmp(fn_name, "__htonl") == 0) && expr->as.call.num_args == 1) {
            char *val = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_htonl(NULL, %s);", result, val);
            codegen_writeln(ctx, "hml_release(&%s);", val);
            free(val);
            return result;
        }

        // htonll(val) -> u64
        if ((strcmp(fn_name, "htonll") == 0 || strcmp(fn_name, "__htonll") == 0) && expr->as.call.num_args == 1) {
            char *val = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_htonll(NULL, %s);", result, val);
            codegen_writeln(ctx, "hml_release(&%s);", val);
            free(val);
            return result;
        }

        // ntohs(val) -> u16
        if ((strcmp(fn_name, "ntohs") == 0 || strcmp(fn_name, "__ntohs") == 0) && expr->as.call.num_args == 1) {
            char *val = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ntohs(NULL, %s);", result, val);
            codegen_writeln(ctx, "hml_release(&%s);", val);
            free(val);
            return result;
        }

        // ntohl(val) -> u32
        if ((strcmp(fn_name, "ntohl") == 0 || strcmp(fn_name, "__ntohl") == 0) && expr->as.call.num_args == 1) {
            char *val = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ntohl(NULL, %s);", result, val);
            codegen_writeln(ctx, "hml_release(&%s);", val);
            free(val);
            return result;
        }

        // ntohll(val) -> u64
        if ((strcmp(fn_name, "ntohll") == 0 || strcmp(fn_name, "__ntohll") == 0) && expr->as.call.num_args == 1) {
            char *val = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ntohll(NULL, %s);", result, val);
            codegen_writeln(ctx, "hml_release(&%s);", val);
            free(val);
            return result;
        }

        // is_little_endian() -> bool
        if ((strcmp(fn_name, "is_little_endian") == 0 || strcmp(fn_name, "__is_little_endian") == 0) && expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_is_little_endian(NULL);", result);
            return result;
        }

        // read_u16_be(ptr, offset) -> u16
        if ((strcmp(fn_name, "read_u16_be") == 0 || strcmp(fn_name, "__read_u16_be") == 0) && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *offset = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_read_u16_be(NULL, %s, %s);", result, ptr, offset);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", offset);
            free(ptr);
            free(offset);
            return result;
        }

        // read_u16_le(ptr, offset) -> u16
        if ((strcmp(fn_name, "read_u16_le") == 0 || strcmp(fn_name, "__read_u16_le") == 0) && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *offset = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_read_u16_le(NULL, %s, %s);", result, ptr, offset);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", offset);
            free(ptr);
            free(offset);
            return result;
        }

        // read_u32_be(ptr, offset) -> u32
        if ((strcmp(fn_name, "read_u32_be") == 0 || strcmp(fn_name, "__read_u32_be") == 0) && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *offset = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_read_u32_be(NULL, %s, %s);", result, ptr, offset);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", offset);
            free(ptr);
            free(offset);
            return result;
        }

        // read_u32_le(ptr, offset) -> u32
        if ((strcmp(fn_name, "read_u32_le") == 0 || strcmp(fn_name, "__read_u32_le") == 0) && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *offset = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_read_u32_le(NULL, %s, %s);", result, ptr, offset);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", offset);
            free(ptr);
            free(offset);
            return result;
        }

        // read_u64_be(ptr, offset) -> u64
        if ((strcmp(fn_name, "read_u64_be") == 0 || strcmp(fn_name, "__read_u64_be") == 0) && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *offset = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_read_u64_be(NULL, %s, %s);", result, ptr, offset);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", offset);
            free(ptr);
            free(offset);
            return result;
        }

        // read_u64_le(ptr, offset) -> u64
        if ((strcmp(fn_name, "read_u64_le") == 0 || strcmp(fn_name, "__read_u64_le") == 0) && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *offset = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_read_u64_le(NULL, %s, %s);", result, ptr, offset);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", offset);
            free(ptr);
            free(offset);
            return result;
        }

        // write_u16_be(ptr, offset, value)
        if ((strcmp(fn_name, "write_u16_be") == 0 || strcmp(fn_name, "__write_u16_be") == 0) && expr->as.call.num_args == 3) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *offset = codegen_expr(ctx, expr->as.call.args[1]);
            char *value = codegen_expr(ctx, expr->as.call.args[2]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_write_u16_be(NULL, %s, %s, %s);", result, ptr, offset, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", offset);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(offset);
            free(value);
            return result;
        }

        // write_u16_le(ptr, offset, value)
        if ((strcmp(fn_name, "write_u16_le") == 0 || strcmp(fn_name, "__write_u16_le") == 0) && expr->as.call.num_args == 3) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *offset = codegen_expr(ctx, expr->as.call.args[1]);
            char *value = codegen_expr(ctx, expr->as.call.args[2]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_write_u16_le(NULL, %s, %s, %s);", result, ptr, offset, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", offset);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(offset);
            free(value);
            return result;
        }

        // write_u32_be(ptr, offset, value)
        if ((strcmp(fn_name, "write_u32_be") == 0 || strcmp(fn_name, "__write_u32_be") == 0) && expr->as.call.num_args == 3) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *offset = codegen_expr(ctx, expr->as.call.args[1]);
            char *value = codegen_expr(ctx, expr->as.call.args[2]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_write_u32_be(NULL, %s, %s, %s);", result, ptr, offset, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", offset);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(offset);
            free(value);
            return result;
        }

        // write_u32_le(ptr, offset, value)
        if ((strcmp(fn_name, "write_u32_le") == 0 || strcmp(fn_name, "__write_u32_le") == 0) && expr->as.call.num_args == 3) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *offset = codegen_expr(ctx, expr->as.call.args[1]);
            char *value = codegen_expr(ctx, expr->as.call.args[2]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_write_u32_le(NULL, %s, %s, %s);", result, ptr, offset, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", offset);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(offset);
            free(value);
            return result;
        }

        // write_u64_be(ptr, offset, value)
        if ((strcmp(fn_name, "write_u64_be") == 0 || strcmp(fn_name, "__write_u64_be") == 0) && expr->as.call.num_args == 3) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *offset = codegen_expr(ctx, expr->as.call.args[1]);
            char *value = codegen_expr(ctx, expr->as.call.args[2]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_write_u64_be(NULL, %s, %s, %s);", result, ptr, offset, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", offset);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(offset);
            free(value);
            return result;
        }

        // write_u64_le(ptr, offset, value)
        if ((strcmp(fn_name, "write_u64_le") == 0 || strcmp(fn_name, "__write_u64_le") == 0) && expr->as.call.num_args == 3) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *offset = codegen_expr(ctx, expr->as.call.args[1]);
            char *value = codegen_expr(ctx, expr->as.call.args[2]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_write_u64_le(NULL, %s, %s, %s);", result, ptr, offset, value);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", offset);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(ptr);
            free(offset);
            free(value);
            return result;
        }

        // ffi_sizeof(type_name) -> i32
        if ((strcmp(fn_name, "ffi_sizeof") == 0 || strcmp(fn_name, "__ffi_sizeof") == 0) && expr->as.call.num_args == 1) {
            char *type_name = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ffi_sizeof(NULL, %s);", result, type_name);
            codegen_writeln(ctx, "hml_release(&%s);", type_name);
            free(type_name);
            return result;
        }

        // ptr_to_buffer(ptr, size) -> buffer
        if (strcmp(fn_name, "ptr_to_buffer") == 0 && expr->as.call.num_args == 2) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            char *size = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_to_buffer(NULL, %s, %s);", result, ptr, size);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            codegen_writeln(ctx, "hml_release(&%s);", size);
            free(ptr);
            free(size);
            return result;
        }

        // buffer_ptr(buffer) -> ptr
        if (strcmp(fn_name, "buffer_ptr") == 0 && expr->as.call.num_args == 1) {
            char *buf = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_buffer_ptr(NULL, %s);", result, buf);
            codegen_writeln(ctx, "hml_release(&%s);", buf);
            free(buf);
            return result;
        }

        // ptr_null() -> ptr
        if (strcmp(fn_name, "ptr_null") == 0 && expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_null(NULL);", result);
            return result;
        }

        // ========== MATH BUILTINS ==========

        // sqrt(x)
        if (strcmp(fn_name, "__sqrt") == 0 && expr->as.call.num_args == 1) {
            char *arg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_sqrt(%s);", result, arg);
            codegen_writeln(ctx, "hml_release(&%s);", arg);
            free(arg);
            return result;
        }

        // sin(x)
        if (strcmp(fn_name, "__sin") == 0 && expr->as.call.num_args == 1) {
            char *arg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_sin(%s);", result, arg);
            codegen_writeln(ctx, "hml_release(&%s);", arg);
            free(arg);
            return result;
        }

        // cos(x)
        if (strcmp(fn_name, "__cos") == 0 && expr->as.call.num_args == 1) {
            char *arg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_cos(%s);", result, arg);
            codegen_writeln(ctx, "hml_release(&%s);", arg);
            free(arg);
            return result;
        }

        // tan(x)
        if (strcmp(fn_name, "__tan") == 0 && expr->as.call.num_args == 1) {
            char *arg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_tan(%s);", result, arg);
            codegen_writeln(ctx, "hml_release(&%s);", arg);
            free(arg);
            return result;
        }

        // asin(x)
        if (strcmp(fn_name, "__asin") == 0 && expr->as.call.num_args == 1) {
            char *arg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_asin(%s);", result, arg);
            codegen_writeln(ctx, "hml_release(&%s);", arg);
            free(arg);
            return result;
        }

        // acos(x)
        if (strcmp(fn_name, "__acos") == 0 && expr->as.call.num_args == 1) {
            char *arg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_acos(%s);", result, arg);
            codegen_writeln(ctx, "hml_release(&%s);", arg);
            free(arg);
            return result;
        }

        // atan(x)
        if (strcmp(fn_name, "__atan") == 0 && expr->as.call.num_args == 1) {
            char *arg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_atan(%s);", result, arg);
            codegen_writeln(ctx, "hml_release(&%s);", arg);
            free(arg);
            return result;
        }

        // atan2(y, x)
        if (strcmp(fn_name, "__atan2") == 0 && expr->as.call.num_args == 2) {
            char *y_arg = codegen_expr(ctx, expr->as.call.args[0]);
            char *x_arg = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_atan2(%s, %s);", result, y_arg, x_arg);
            codegen_writeln(ctx, "hml_release(&%s);", y_arg);
            codegen_writeln(ctx, "hml_release(&%s);", x_arg);
            free(y_arg);
            free(x_arg);
            return result;
        }

        // floor(x)
        if (strcmp(fn_name, "__floor") == 0 && expr->as.call.num_args == 1) {
            char *arg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_floor(%s);", result, arg);
            codegen_writeln(ctx, "hml_release(&%s);", arg);
            free(arg);
            return result;
        }

        // ceil(x)
        if (strcmp(fn_name, "__ceil") == 0 && expr->as.call.num_args == 1) {
            char *arg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_ceil(%s);", result, arg);
            codegen_writeln(ctx, "hml_release(&%s);", arg);
            free(arg);
            return result;
        }

        // round(x)
        if (strcmp(fn_name, "__round") == 0 && expr->as.call.num_args == 1) {
            char *arg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_round(%s);", result, arg);
            codegen_writeln(ctx, "hml_release(&%s);", arg);
            free(arg);
            return result;
        }

        // trunc(x)
        if (strcmp(fn_name, "__trunc") == 0 && expr->as.call.num_args == 1) {
            char *arg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_trunc(%s);", result, arg);
            codegen_writeln(ctx, "hml_release(&%s);", arg);
            free(arg);
            return result;
        }

        // floori(x) - floor returning integer
        if (strcmp(fn_name, "__floori") == 0 && expr->as.call.num_args == 1) {
            char *arg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_floori(%s);", result, arg);
            codegen_writeln(ctx, "hml_release(&%s);", arg);
            free(arg);
            return result;
        }

        // ceili(x) - ceil returning integer
        if (strcmp(fn_name, "__ceili") == 0 && expr->as.call.num_args == 1) {
            char *arg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_ceili(%s);", result, arg);
            codegen_writeln(ctx, "hml_release(&%s);", arg);
            free(arg);
            return result;
        }

        // roundi(x) - round returning integer
        if (strcmp(fn_name, "__roundi") == 0 && expr->as.call.num_args == 1) {
            char *arg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_roundi(%s);", result, arg);
            codegen_writeln(ctx, "hml_release(&%s);", arg);
            free(arg);
            return result;
        }

        // trunci(x) - trunc returning integer
        if (strcmp(fn_name, "__trunci") == 0 && expr->as.call.num_args == 1) {
            char *arg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_trunci(%s);", result, arg);
            codegen_writeln(ctx, "hml_release(&%s);", arg);
            free(arg);
            return result;
        }

        // div(a, b) - float division (alias for /)
        if (strcmp(fn_name, "__div") == 0 && expr->as.call.num_args == 2) {
            char *a = codegen_expr(ctx, expr->as.call.args[0]);
            char *b = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_div(%s, %s);", result, a, b);
            codegen_writeln(ctx, "hml_release(&%s);", a);
            codegen_writeln(ctx, "hml_release(&%s);", b);
            free(a);
            free(b);
            return result;
        }

        // divi(a, b) - truncating division returning integer
        if (strcmp(fn_name, "__divi") == 0 && expr->as.call.num_args == 2) {
            char *a = codegen_expr(ctx, expr->as.call.args[0]);
            char *b = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_divi(%s, %s);", result, a, b);
            codegen_writeln(ctx, "hml_release(&%s);", a);
            codegen_writeln(ctx, "hml_release(&%s);", b);
            free(a);
            free(b);
            return result;
        }

        // abs(x)
        if (strcmp(fn_name, "__abs") == 0 && expr->as.call.num_args == 1) {
            char *arg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_abs(%s);", result, arg);
            codegen_writeln(ctx, "hml_release(&%s);", arg);
            free(arg);
            return result;
        }

        // pow(base, exp)
        if (strcmp(fn_name, "__pow") == 0 && expr->as.call.num_args == 2) {
            char *base = codegen_expr(ctx, expr->as.call.args[0]);
            char *exp_arg = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_pow(%s, %s);", result, base, exp_arg);
            codegen_writeln(ctx, "hml_release(&%s);", base);
            codegen_writeln(ctx, "hml_release(&%s);", exp_arg);
            free(base);
            free(exp_arg);
            return result;
        }

        // exp(x)
        if (strcmp(fn_name, "__exp") == 0 && expr->as.call.num_args == 1) {
            char *arg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_exp(%s);", result, arg);
            codegen_writeln(ctx, "hml_release(&%s);", arg);
            free(arg);
            return result;
        }

        // log(x)
        if (strcmp(fn_name, "__log") == 0 && expr->as.call.num_args == 1) {
            char *arg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_log(%s);", result, arg);
            codegen_writeln(ctx, "hml_release(&%s);", arg);
            free(arg);
            return result;
        }

        // log10(x)
        if (strcmp(fn_name, "__log10") == 0 && expr->as.call.num_args == 1) {
            char *arg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_log10(%s);", result, arg);
            codegen_writeln(ctx, "hml_release(&%s);", arg);
            free(arg);
            return result;
        }

        // log2(x)
        if (strcmp(fn_name, "__log2") == 0 && expr->as.call.num_args == 1) {
            char *arg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_log2(%s);", result, arg);
            codegen_writeln(ctx, "hml_release(&%s);", arg);
            free(arg);
            return result;
        }

        // min(a, b)
        if (strcmp(fn_name, "__min") == 0 && expr->as.call.num_args == 2) {
            char *a = codegen_expr(ctx, expr->as.call.args[0]);
            char *b = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_min(%s, %s);", result, a, b);
            codegen_writeln(ctx, "hml_release(&%s);", a);
            codegen_writeln(ctx, "hml_release(&%s);", b);
            free(a);
            free(b);
            return result;
        }

        // max(a, b)
        if (strcmp(fn_name, "__max") == 0 && expr->as.call.num_args == 2) {
            char *a = codegen_expr(ctx, expr->as.call.args[0]);
            char *b = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_max(%s, %s);", result, a, b);
            codegen_writeln(ctx, "hml_release(&%s);", a);
            codegen_writeln(ctx, "hml_release(&%s);", b);
            free(a);
            free(b);
            return result;
        }

        // rand()
        if (strcmp(fn_name, "__rand") == 0 && expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "HmlValue %s = hml_rand();", result);
            return result;
        }

        // seed(value)
        if (strcmp(fn_name, "__seed") == 0 && expr->as.call.num_args == 1) {
            char *arg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "hml_seed(%s);", arg);
            codegen_writeln(ctx, "hml_release(&%s);", arg);
            codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            free(arg);
            return result;
        }

        // rand_range(min, max) - also __rand_range
        if (strcmp(fn_name, "__rand_range") == 0 && expr->as.call.num_args == 2) {
            char *min_arg = codegen_expr(ctx, expr->as.call.args[0]);
            char *max_arg = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_rand_range(%s, %s);", result, min_arg, max_arg);
            codegen_writeln(ctx, "hml_release(&%s);", min_arg);
            codegen_writeln(ctx, "hml_release(&%s);", max_arg);
            free(min_arg);
            free(max_arg);
            return result;
        }

        // clamp(value, min, max) - also __clamp
        if (strcmp(fn_name, "__clamp") == 0 && expr->as.call.num_args == 3) {
            char *val = codegen_expr(ctx, expr->as.call.args[0]);
            char *min_arg = codegen_expr(ctx, expr->as.call.args[1]);
            char *max_arg = codegen_expr(ctx, expr->as.call.args[2]);
            codegen_writeln(ctx, "HmlValue %s = hml_clamp(%s, %s, %s);", result, val, min_arg, max_arg);
            codegen_writeln(ctx, "hml_release(&%s);", val);
            codegen_writeln(ctx, "hml_release(&%s);", min_arg);
            codegen_writeln(ctx, "hml_release(&%s);", max_arg);
            free(val);
            free(min_arg);
            free(max_arg);
            return result;
        }

        // ========== TIME BUILTINS ==========
        // Note: For unprefixed builtins, only use builtin if NOT a local/import

        // now() - but NOT if 'now' is a local/import (e.g., from @stdlib/datetime)
        if (strcmp(fn_name, "__now") == 0 &&
            expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "HmlValue %s = hml_now();", result);
            return result;
        }

        // time_ms() - but NOT if 'time_ms' is a local/import
        if (strcmp(fn_name, "__time_ms") == 0 &&
            expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "HmlValue %s = hml_time_ms();", result);
            return result;
        }

        // clock() - but NOT if 'clock' is a local/import
        if (strcmp(fn_name, "__clock") == 0 &&
            expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "HmlValue %s = hml_clock();", result);
            return result;
        }

        // sleep(seconds) - but NOT if 'sleep' is a local/import
        if (strcmp(fn_name, "__sleep") == 0 &&
            expr->as.call.num_args == 1) {
            char *arg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "hml_sleep(%s);", arg);
            codegen_writeln(ctx, "hml_release(&%s);", arg);
            codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            free(arg);
            return result;
        }

        // ========== DATETIME BUILTINS ==========

        // localtime(timestamp)
        if (strcmp(fn_name, "__localtime") == 0 && expr->as.call.num_args == 1) {
            char *ts = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_localtime(%s);", result, ts);
            codegen_writeln(ctx, "hml_release(&%s);", ts);
            free(ts);
            return result;
        }

        // gmtime(timestamp)
        if (strcmp(fn_name, "__gmtime") == 0 && expr->as.call.num_args == 1) {
            char *ts = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_gmtime(%s);", result, ts);
            codegen_writeln(ctx, "hml_release(&%s);", ts);
            free(ts);
            return result;
        }

        // mktime(time_obj)
        if (strcmp(fn_name, "__mktime") == 0 && expr->as.call.num_args == 1) {
            char *obj = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_mktime(%s);", result, obj);
            codegen_writeln(ctx, "hml_release(&%s);", obj);
            free(obj);
            return result;
        }

        // strftime(format, time_obj)
        if (strcmp(fn_name, "__strftime") == 0 && expr->as.call.num_args == 2) {
            char *fmt = codegen_expr(ctx, expr->as.call.args[0]);
            char *obj = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_strftime(%s, %s);", result, fmt, obj);
            codegen_writeln(ctx, "hml_release(&%s);", fmt);
            codegen_writeln(ctx, "hml_release(&%s);", obj);
            free(fmt);
            free(obj);
            return result;
        }

        // ========== ENVIRONMENT BUILTINS ==========

        // getenv(name)
        if (strcmp(fn_name, "__getenv") == 0 && expr->as.call.num_args == 1) {
            char *arg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_getenv(%s);", result, arg);
            codegen_writeln(ctx, "hml_release(&%s);", arg);
            free(arg);
            return result;
        }

        // setenv(name, value)
        if (strcmp(fn_name, "__setenv") == 0 && expr->as.call.num_args == 2) {
            char *name_arg = codegen_expr(ctx, expr->as.call.args[0]);
            char *value_arg = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "hml_setenv(%s, %s);", name_arg, value_arg);
            codegen_writeln(ctx, "hml_release(&%s);", name_arg);
            codegen_writeln(ctx, "hml_release(&%s);", value_arg);
            codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            free(name_arg);
            free(value_arg);
            return result;
        }

        // unsetenv(name)
        if (strcmp(fn_name, "__unsetenv") == 0 && expr->as.call.num_args == 1) {
            char *name_arg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "hml_unsetenv(%s);", name_arg);
            codegen_writeln(ctx, "hml_release(&%s);", name_arg);
            codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            free(name_arg);
            return result;
        }

        // exit(code)
        if (strcmp(fn_name, "__exit") == 0 && expr->as.call.num_args == 1) {
            char *arg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "hml_exit(%s);", arg);
            codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            free(arg);
            return result;
        }

        // abort()
        if (strcmp(fn_name, "__abort") == 0 && expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "hml_abort();");
            codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            return result;
        }

        // get_pid()
        if (strcmp(fn_name, "__get_pid") == 0 && expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "HmlValue %s = hml_get_pid();", result);
            return result;
        }

        // ========== FILESYSTEM BUILTINS ==========

        // cwd()
        if (strcmp(fn_name, "__cwd") == 0 && expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "HmlValue %s = hml_cwd();", result);
            return result;
        }

        // chdir(path)
        if (strcmp(fn_name, "__chdir") == 0 && expr->as.call.num_args == 1) {
            char *path_arg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_chdir(%s);", result, path_arg);
            codegen_writeln(ctx, "hml_release(&%s);", path_arg);
            free(path_arg);
            return result;
        }

        // list_dir(path)
        if (strcmp(fn_name, "__list_dir") == 0 && expr->as.call.num_args == 1) {
            char *path_arg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_list_dir(%s);", result, path_arg);
            codegen_writeln(ctx, "hml_release(&%s);", path_arg);
            free(path_arg);
            return result;
        }

        // make_dir(path, mode?)
        if (strcmp(fn_name, "__make_dir") == 0 &&
            (expr->as.call.num_args == 1 || expr->as.call.num_args == 2)) {
            char *path_arg = codegen_expr(ctx, expr->as.call.args[0]);
            if (expr->as.call.num_args == 2) {
                char *mode_arg = codegen_expr(ctx, expr->as.call.args[1]);
                codegen_writeln(ctx, "HmlValue %s = hml_make_dir(%s, %s);", result, path_arg, mode_arg);
                codegen_writeln(ctx, "hml_release(&%s);", mode_arg);
                free(mode_arg);
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_make_dir(%s, hml_val_i32(0755));", result, path_arg);
            }
            codegen_writeln(ctx, "hml_release(&%s);", path_arg);
            free(path_arg);
            return result;
        }

        // remove_dir(path)
        if (strcmp(fn_name, "__remove_dir") == 0 && expr->as.call.num_args == 1) {
            char *path_arg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_remove_dir(%s);", result, path_arg);
            codegen_writeln(ctx, "hml_release(&%s);", path_arg);
            free(path_arg);
            return result;
        }

        // ========== PROCESS MANAGEMENT BUILTINS ==========

        // getppid()
        if (strcmp(fn_name, "__getppid") == 0 && expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "HmlValue %s = hml_getppid();", result);
            return result;
        }

        // getuid()
        if (strcmp(fn_name, "__getuid") == 0 && expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "HmlValue %s = hml_getuid();", result);
            return result;
        }

        // geteuid()
        if (strcmp(fn_name, "__geteuid") == 0 && expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "HmlValue %s = hml_geteuid();", result);
            return result;
        }

        // getgid()
        if (strcmp(fn_name, "__getgid") == 0 && expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "HmlValue %s = hml_getgid();", result);
            return result;
        }

        // getegid()
        if (strcmp(fn_name, "__getegid") == 0 && expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "HmlValue %s = hml_getegid();", result);
            return result;
        }

        // fork()
        if (strcmp(fn_name, "__fork") == 0 && expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "HmlValue %s = hml_fork();", result);
            return result;
        }

        // wait()
        if (strcmp(fn_name, "__wait") == 0 && expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "HmlValue %s = hml_wait();", result);
            return result;
        }

        // waitpid(pid, options)
        if (strcmp(fn_name, "__waitpid") == 0 && expr->as.call.num_args == 2) {
            char *pid = codegen_expr(ctx, expr->as.call.args[0]);
            char *opts = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_waitpid(%s, %s);", result, pid, opts);
            codegen_writeln(ctx, "hml_release(&%s);", pid);
            codegen_writeln(ctx, "hml_release(&%s);", opts);
            free(pid);
            free(opts);
            return result;
        }

        // kill(pid, sig)
        if (strcmp(fn_name, "__kill") == 0 && expr->as.call.num_args == 2) {
            char *pid = codegen_expr(ctx, expr->as.call.args[0]);
            char *sig = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_kill(%s, %s);", result, pid, sig);
            codegen_writeln(ctx, "hml_release(&%s);", pid);
            codegen_writeln(ctx, "hml_release(&%s);", sig);
            free(pid);
            free(sig);
            return result;
        }

        // ========== SOCKET BUILTINS ==========

        // socket_create(domain, type, protocol)
        if (strcmp(fn_name, "__socket_create") == 0 && expr->as.call.num_args == 3) {
            char *domain = codegen_expr(ctx, expr->as.call.args[0]);
            char *sock_type = codegen_expr(ctx, expr->as.call.args[1]);
            char *protocol = codegen_expr(ctx, expr->as.call.args[2]);
            codegen_writeln(ctx, "HmlValue %s = hml_socket_create(%s, %s, %s);", result, domain, sock_type, protocol);
            codegen_writeln(ctx, "hml_release(&%s);", domain);
            codegen_writeln(ctx, "hml_release(&%s);", sock_type);
            codegen_writeln(ctx, "hml_release(&%s);", protocol);
            free(domain);
            free(sock_type);
            free(protocol);
            return result;
        }

        // dns_resolve(hostname)
        if (strcmp(fn_name, "__dns_resolve") == 0 && expr->as.call.num_args == 1) {
            char *hostname = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_dns_resolve(%s);", result, hostname);
            codegen_writeln(ctx, "hml_release(&%s);", hostname);
            free(hostname);
            return result;
        }

        // ========== OS INFO BUILTINS ==========

        // platform()
        if (strcmp(fn_name, "__platform") == 0 && expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "HmlValue %s = hml_platform();", result);
            return result;
        }

        // arch()
        if (strcmp(fn_name, "__arch") == 0 && expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "HmlValue %s = hml_arch();", result);
            return result;
        }

        // hostname()
        if (strcmp(fn_name, "__hostname") == 0 && expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "HmlValue %s = hml_hostname();", result);
            return result;
        }

        // username()
        if (strcmp(fn_name, "__username") == 0 && expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "HmlValue %s = hml_username();", result);
            return result;
        }

        // homedir()
        if (strcmp(fn_name, "__homedir") == 0 && expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "HmlValue %s = hml_homedir();", result);
            return result;
        }

        // cpu_count()
        if (strcmp(fn_name, "__cpu_count") == 0 && expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "HmlValue %s = hml_cpu_count();", result);
            return result;
        }

        // total_memory()
        if (strcmp(fn_name, "__total_memory") == 0 && expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "HmlValue %s = hml_total_memory();", result);
            return result;
        }

        // free_memory()
        if (strcmp(fn_name, "__free_memory") == 0 && expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "HmlValue %s = hml_free_memory();", result);
            return result;
        }

        // os_version()
        if (strcmp(fn_name, "__os_version") == 0 && expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "HmlValue %s = hml_os_version();", result);
            return result;
        }

        // os_name()
        if (strcmp(fn_name, "__os_name") == 0 && expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "HmlValue %s = hml_os_name();", result);
            return result;
        }

        // tmpdir()
        if (strcmp(fn_name, "__tmpdir") == 0 && expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "HmlValue %s = hml_tmpdir();", result);
            return result;
        }

        // uptime()
        if (strcmp(fn_name, "__uptime") == 0 && expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "HmlValue %s = hml_uptime();", result);
            return result;
        }

        // ========== COMPRESSION BUILTINS ==========

        // zlib_compress(data, level)
        if (strcmp(fn_name, "__zlib_compress") == 0 && expr->as.call.num_args == 2) {
            char *data = codegen_expr(ctx, expr->as.call.args[0]);
            char *level = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_zlib_compress(%s, %s);", result, data, level);
            codegen_writeln(ctx, "hml_release(&%s);", data);
            codegen_writeln(ctx, "hml_release(&%s);", level);
            free(data);
            free(level);
            return result;
        }

        // zlib_decompress(data, max_size)
        if (strcmp(fn_name, "__zlib_decompress") == 0 && expr->as.call.num_args == 2) {
            char *data = codegen_expr(ctx, expr->as.call.args[0]);
            char *max_size = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_zlib_decompress(%s, %s);", result, data, max_size);
            codegen_writeln(ctx, "hml_release(&%s);", data);
            codegen_writeln(ctx, "hml_release(&%s);", max_size);
            free(data);
            free(max_size);
            return result;
        }

        // gzip_compress(data, level)
        if (strcmp(fn_name, "__gzip_compress") == 0 && expr->as.call.num_args == 2) {
            char *data = codegen_expr(ctx, expr->as.call.args[0]);
            char *level = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_gzip_compress(%s, %s);", result, data, level);
            codegen_writeln(ctx, "hml_release(&%s);", data);
            codegen_writeln(ctx, "hml_release(&%s);", level);
            free(data);
            free(level);
            return result;
        }

        // gzip_decompress(data, max_size)
        if (strcmp(fn_name, "__gzip_decompress") == 0 && expr->as.call.num_args == 2) {
            char *data = codegen_expr(ctx, expr->as.call.args[0]);
            char *max_size = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_gzip_decompress(%s, %s);", result, data, max_size);
            codegen_writeln(ctx, "hml_release(&%s);", data);
            codegen_writeln(ctx, "hml_release(&%s);", max_size);
            free(data);
            free(max_size);
            return result;
        }

        // zlib_compress_bound(source_len)
        if (strcmp(fn_name, "__zlib_compress_bound") == 0 && expr->as.call.num_args == 1) {
            char *len = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_zlib_compress_bound(%s);", result, len);
            codegen_writeln(ctx, "hml_release(&%s);", len);
            free(len);
            return result;
        }

        // crc32(data)
        if (strcmp(fn_name, "__crc32") == 0 && expr->as.call.num_args == 1) {
            char *data = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_crc32_val(%s);", result, data);
            codegen_writeln(ctx, "hml_release(&%s);", data);
            free(data);
            return result;
        }

        // adler32(data)
        if (strcmp(fn_name, "__adler32") == 0 && expr->as.call.num_args == 1) {
            char *data = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_adler32_val(%s);", result, data);
            codegen_writeln(ctx, "hml_release(&%s);", data);
            free(data);
            return result;
        }

        // ========== INTERNAL HELPER BUILTINS ==========

        // read_u32(buffer) - read 32-bit unsigned int from buffer
        if (strcmp(fn_name, "__read_u32") == 0 && expr->as.call.num_args == 1) {
            char *buf = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_read_u32(%s);", result, buf);
            codegen_writeln(ctx, "hml_release(&%s);", buf);
            free(buf);
            return result;
        }

        // read_u64(buffer) - read 64-bit unsigned int from buffer
        if (strcmp(fn_name, "__read_u64") == 0 && expr->as.call.num_args == 1) {
            char *buf = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_read_u64(%s);", result, buf);
            codegen_writeln(ctx, "hml_release(&%s);", buf);
            free(buf);
            return result;
        }

        // __read_ptr(ptr) - read pointer from pointer (double indirection)
        if (strcmp(fn_name, "__read_ptr") == 0 && expr->as.call.num_args == 1) {
            char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_read_ptr(%s);", result, ptr);
            codegen_writeln(ctx, "hml_release(&%s);", ptr);
            free(ptr);
            return result;
        }

        // ========== HTTP/WEBSOCKET BUILTINS ==========

        // __lws_http_get(url) or __lws_http_get(url, headers)
        if (strcmp(fn_name, "__lws_http_get") == 0 && expr->as.call.num_args == 1) {
            char *url = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_http_get(%s);", result, url);
            codegen_writeln(ctx, "hml_release(&%s);", url);
            free(url);
            return result;
        }
        if (strcmp(fn_name, "__lws_http_get") == 0 && expr->as.call.num_args == 2) {
            char *url = codegen_expr(ctx, expr->as.call.args[0]);
            char *headers = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_http_get_with_headers(%s, %s);", result, url, headers);
            codegen_writeln(ctx, "hml_release(&%s);", url);
            codegen_writeln(ctx, "hml_release(&%s);", headers);
            free(url);
            free(headers);
            return result;
        }

        // __lws_http_post(url, body, content_type)
        if (strcmp(fn_name, "__lws_http_post") == 0 && expr->as.call.num_args == 3) {
            char *url = codegen_expr(ctx, expr->as.call.args[0]);
            char *body = codegen_expr(ctx, expr->as.call.args[1]);
            char *content_type = codegen_expr(ctx, expr->as.call.args[2]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_http_post(%s, %s, %s);", result, url, body, content_type);
            codegen_writeln(ctx, "hml_release(&%s);", url);
            codegen_writeln(ctx, "hml_release(&%s);", body);
            codegen_writeln(ctx, "hml_release(&%s);", content_type);
            free(url);
            free(body);
            free(content_type);
            return result;
        }

        // __lws_http_request(method, url, body, content_type)
        if (strcmp(fn_name, "__lws_http_request") == 0 && expr->as.call.num_args == 4) {
            char *method = codegen_expr(ctx, expr->as.call.args[0]);
            char *url = codegen_expr(ctx, expr->as.call.args[1]);
            char *body = codegen_expr(ctx, expr->as.call.args[2]);
            char *content_type = codegen_expr(ctx, expr->as.call.args[3]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_http_request(%s, %s, %s, %s);", result, method, url, body, content_type);
            codegen_writeln(ctx, "hml_release(&%s);", method);
            codegen_writeln(ctx, "hml_release(&%s);", url);
            codegen_writeln(ctx, "hml_release(&%s);", body);
            codegen_writeln(ctx, "hml_release(&%s);", content_type);
            free(method);
            free(url);
            free(body);
            free(content_type);
            return result;
        }

        // __lws_http_get_timeout(url, timeout_ms)
        if (strcmp(fn_name, "__lws_http_get_timeout") == 0 && expr->as.call.num_args == 2) {
            char *url = codegen_expr(ctx, expr->as.call.args[0]);
            char *timeout_ms = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_http_get_timeout(%s, %s);", result, url, timeout_ms);
            codegen_writeln(ctx, "hml_release(&%s);", url);
            codegen_writeln(ctx, "hml_release(&%s);", timeout_ms);
            free(url);
            free(timeout_ms);
            return result;
        }

        // __lws_http_post_timeout(url, body, content_type, timeout_ms)
        if (strcmp(fn_name, "__lws_http_post_timeout") == 0 && expr->as.call.num_args == 4) {
            char *url = codegen_expr(ctx, expr->as.call.args[0]);
            char *body = codegen_expr(ctx, expr->as.call.args[1]);
            char *content_type = codegen_expr(ctx, expr->as.call.args[2]);
            char *timeout_ms = codegen_expr(ctx, expr->as.call.args[3]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_http_post_timeout(%s, %s, %s, %s);", result, url, body, content_type, timeout_ms);
            codegen_writeln(ctx, "hml_release(&%s);", url);
            codegen_writeln(ctx, "hml_release(&%s);", body);
            codegen_writeln(ctx, "hml_release(&%s);", content_type);
            codegen_writeln(ctx, "hml_release(&%s);", timeout_ms);
            free(url);
            free(body);
            free(content_type);
            free(timeout_ms);
            return result;
        }

        // __lws_http_request_timeout(method, url, body, content_type, timeout_ms)
        if (strcmp(fn_name, "__lws_http_request_timeout") == 0 && expr->as.call.num_args == 5) {
            char *method = codegen_expr(ctx, expr->as.call.args[0]);
            char *url = codegen_expr(ctx, expr->as.call.args[1]);
            char *body = codegen_expr(ctx, expr->as.call.args[2]);
            char *content_type = codegen_expr(ctx, expr->as.call.args[3]);
            char *timeout_ms = codegen_expr(ctx, expr->as.call.args[4]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_http_request_timeout(%s, %s, %s, %s, %s);", result, method, url, body, content_type, timeout_ms);
            codegen_writeln(ctx, "hml_release(&%s);", method);
            codegen_writeln(ctx, "hml_release(&%s);", url);
            codegen_writeln(ctx, "hml_release(&%s);", body);
            codegen_writeln(ctx, "hml_release(&%s);", content_type);
            codegen_writeln(ctx, "hml_release(&%s);", timeout_ms);
            free(method);
            free(url);
            free(body);
            free(content_type);
            free(timeout_ms);
            return result;
        }

        // __lws_response_status(resp)
        if (strcmp(fn_name, "__lws_response_status") == 0 && expr->as.call.num_args == 1) {
            char *resp = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_response_status(%s);", result, resp);
            codegen_writeln(ctx, "hml_release(&%s);", resp);
            free(resp);
            return result;
        }

        // __lws_response_body(resp)
        if (strcmp(fn_name, "__lws_response_body") == 0 && expr->as.call.num_args == 1) {
            char *resp = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_response_body(%s);", result, resp);
            codegen_writeln(ctx, "hml_release(&%s);", resp);
            free(resp);
            return result;
        }

        // __lws_response_headers(resp)
        if (strcmp(fn_name, "__lws_response_headers") == 0 && expr->as.call.num_args == 1) {
            char *resp = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_response_headers(%s);", result, resp);
            codegen_writeln(ctx, "hml_release(&%s);", resp);
            free(resp);
            return result;
        }

        // __lws_response_free(resp)
        if (strcmp(fn_name, "__lws_response_free") == 0 && expr->as.call.num_args == 1) {
            char *resp = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_response_free(%s);", result, resp);
            codegen_writeln(ctx, "hml_release(&%s);", resp);
            free(resp);
            return result;
        }

        // __lws_response_redirect(resp)
        if (strcmp(fn_name, "__lws_response_redirect") == 0 && expr->as.call.num_args == 1) {
            char *resp = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_response_redirect(%s);", result, resp);
            codegen_writeln(ctx, "hml_release(&%s);", resp);
            free(resp);
            return result;
        }

        // __lws_response_body_binary(resp)
        if (strcmp(fn_name, "__lws_response_body_binary") == 0 && expr->as.call.num_args == 1) {
            char *resp = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_response_body_binary(%s);", result, resp);
            codegen_writeln(ctx, "hml_release(&%s);", resp);
            free(resp);
            return result;
        }

        // ========== STREAMING HTTP BUILTINS ==========

        // __lws_http_stream_start(method, url, body, content_type, timeout_ms)
        if (strcmp(fn_name, "__lws_http_stream_start") == 0 && expr->as.call.num_args == 5) {
            char *method = codegen_expr(ctx, expr->as.call.args[0]);
            char *url = codegen_expr(ctx, expr->as.call.args[1]);
            char *body = codegen_expr(ctx, expr->as.call.args[2]);
            char *content_type = codegen_expr(ctx, expr->as.call.args[3]);
            char *timeout_ms = codegen_expr(ctx, expr->as.call.args[4]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_http_stream_start(%s, %s, %s, %s, %s);", result, method, url, body, content_type, timeout_ms);
            codegen_writeln(ctx, "hml_release(&%s);", method);
            codegen_writeln(ctx, "hml_release(&%s);", url);
            codegen_writeln(ctx, "hml_release(&%s);", body);
            codegen_writeln(ctx, "hml_release(&%s);", content_type);
            codegen_writeln(ctx, "hml_release(&%s);", timeout_ms);
            free(method);
            free(url);
            free(body);
            free(content_type);
            free(timeout_ms);
            return result;
        }

        // __lws_http_stream_read(stream, timeout_ms)
        if (strcmp(fn_name, "__lws_http_stream_read") == 0 && expr->as.call.num_args == 2) {
            char *stream = codegen_expr(ctx, expr->as.call.args[0]);
            char *timeout_ms = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_http_stream_read(%s, %s);", result, stream, timeout_ms);
            codegen_writeln(ctx, "hml_release(&%s);", stream);
            codegen_writeln(ctx, "hml_release(&%s);", timeout_ms);
            free(stream);
            free(timeout_ms);
            return result;
        }

        // __lws_http_stream_status(stream)
        if (strcmp(fn_name, "__lws_http_stream_status") == 0 && expr->as.call.num_args == 1) {
            char *stream = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_http_stream_status(%s);", result, stream);
            codegen_writeln(ctx, "hml_release(&%s);", stream);
            free(stream);
            return result;
        }

        // __lws_http_stream_headers(stream)
        if (strcmp(fn_name, "__lws_http_stream_headers") == 0 && expr->as.call.num_args == 1) {
            char *stream = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_http_stream_headers(%s);", result, stream);
            codegen_writeln(ctx, "hml_release(&%s);", stream);
            free(stream);
            return result;
        }

        // __lws_http_stream_close(stream)
        if (strcmp(fn_name, "__lws_http_stream_close") == 0 && expr->as.call.num_args == 1) {
            char *stream = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_http_stream_close(%s);", result, stream);
            codegen_writeln(ctx, "hml_release(&%s);", stream);
            free(stream);
            return result;
        }

        // ========== CRYPTOGRAPHIC HASH BUILTINS ==========

        // __sha1(input)
        if (strcmp(fn_name, "__sha1") == 0 && expr->as.call.num_args == 1) {
            char *input = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_hash_sha1(%s);", result, input);
            codegen_writeln(ctx, "hml_release(&%s);", input);
            free(input);
            return result;
        }

        // __sha256(input)
        if (strcmp(fn_name, "__sha256") == 0 && expr->as.call.num_args == 1) {
            char *input = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_hash_sha256(%s);", result, input);
            codegen_writeln(ctx, "hml_release(&%s);", input);
            free(input);
            return result;
        }

        // __sha512(input)
        if (strcmp(fn_name, "__sha512") == 0 && expr->as.call.num_args == 1) {
            char *input = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_hash_sha512(%s);", result, input);
            codegen_writeln(ctx, "hml_release(&%s);", input);
            free(input);
            return result;
        }

        // __md5(input)
        if (strcmp(fn_name, "__md5") == 0 && expr->as.call.num_args == 1) {
            char *input = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_hash_md5(%s);", result, input);
            codegen_writeln(ctx, "hml_release(&%s);", input);
            free(input);
            return result;
        }

        // ========== ECDSA SIGNATURE BUILTINS ==========

        // __ecdsa_generate_key(curve?)
        if (strcmp(fn_name, "__ecdsa_generate_key") == 0 &&
            (expr->as.call.num_args == 0 || expr->as.call.num_args == 1)) {
            if (expr->as.call.num_args == 1) {
                char *curve = codegen_expr(ctx, expr->as.call.args[0]);
                codegen_writeln(ctx, "HmlValue %s = hml_ecdsa_generate_key(%s);", result, curve);
                codegen_writeln(ctx, "hml_release(&%s);", curve);
                free(curve);
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_ecdsa_generate_key(hml_val_null());", result);
            }
            return result;
        }

        // __ecdsa_free_key(keypair)
        if (strcmp(fn_name, "__ecdsa_free_key") == 0 && expr->as.call.num_args == 1) {
            char *keypair = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_ecdsa_free_key(%s);", result, keypair);
            codegen_writeln(ctx, "hml_release(&%s);", keypair);
            free(keypair);
            return result;
        }

        // __ecdsa_sign(data, keypair)
        if (strcmp(fn_name, "__ecdsa_sign") == 0 && expr->as.call.num_args == 2) {
            char *data = codegen_expr(ctx, expr->as.call.args[0]);
            char *keypair = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_ecdsa_sign(%s, %s);", result, data, keypair);
            codegen_writeln(ctx, "hml_release(&%s);", data);
            codegen_writeln(ctx, "hml_release(&%s);", keypair);
            free(data);
            free(keypair);
            return result;
        }

        // __ecdsa_verify(data, signature, keypair)
        if (strcmp(fn_name, "__ecdsa_verify") == 0 && expr->as.call.num_args == 3) {
            char *data = codegen_expr(ctx, expr->as.call.args[0]);
            char *sig = codegen_expr(ctx, expr->as.call.args[1]);
            char *keypair = codegen_expr(ctx, expr->as.call.args[2]);
            codegen_writeln(ctx, "HmlValue %s = hml_ecdsa_verify(%s, %s, %s);", result, data, sig, keypair);
            codegen_writeln(ctx, "hml_release(&%s);", data);
            codegen_writeln(ctx, "hml_release(&%s);", sig);
            codegen_writeln(ctx, "hml_release(&%s);", keypair);
            free(data);
            free(sig);
            free(keypair);
            return result;
        }

        // ========== WEBSOCKET BUILTINS ==========

        // __lws_ws_connect(url)
        if (strcmp(fn_name, "__lws_ws_connect") == 0 && expr->as.call.num_args == 1) {
            char *url = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_ws_connect(%s);", result, url);
            codegen_writeln(ctx, "hml_release(&%s);", url);
            free(url);
            return result;
        }

        // __lws_ws_send_text(conn, text)
        if (strcmp(fn_name, "__lws_ws_send_text") == 0 && expr->as.call.num_args == 2) {
            char *conn = codegen_expr(ctx, expr->as.call.args[0]);
            char *text = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_ws_send_text(%s, %s);", result, conn, text);
            codegen_writeln(ctx, "hml_release(&%s);", conn);
            codegen_writeln(ctx, "hml_release(&%s);", text);
            free(conn);
            free(text);
            return result;
        }

        // __lws_ws_send_binary(conn, buffer)
        if (strcmp(fn_name, "__lws_ws_send_binary") == 0 && expr->as.call.num_args == 2) {
            char *conn = codegen_expr(ctx, expr->as.call.args[0]);
            char *buffer = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_ws_send_binary(%s, %s);", result, conn, buffer);
            codegen_writeln(ctx, "hml_release(&%s);", conn);
            codegen_writeln(ctx, "hml_release(&%s);", buffer);
            free(conn);
            free(buffer);
            return result;
        }

        // __lws_ws_recv(conn, timeout_ms)
        if (strcmp(fn_name, "__lws_ws_recv") == 0 && expr->as.call.num_args == 2) {
            char *conn = codegen_expr(ctx, expr->as.call.args[0]);
            char *timeout = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_ws_recv(%s, %s);", result, conn, timeout);
            codegen_writeln(ctx, "hml_release(&%s);", conn);
            codegen_writeln(ctx, "hml_release(&%s);", timeout);
            free(conn);
            free(timeout);
            return result;
        }

        // __lws_ws_close(conn)
        if (strcmp(fn_name, "__lws_ws_close") == 0 && expr->as.call.num_args == 1) {
            char *conn = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_ws_close(%s);", result, conn);
            codegen_writeln(ctx, "hml_release(&%s);", conn);
            free(conn);
            return result;
        }

        // __lws_ws_is_closed(conn)
        if (strcmp(fn_name, "__lws_ws_is_closed") == 0 && expr->as.call.num_args == 1) {
            char *conn = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_ws_is_closed(%s);", result, conn);
            codegen_writeln(ctx, "hml_release(&%s);", conn);
            free(conn);
            return result;
        }

        // __lws_msg_type(msg)
        if (strcmp(fn_name, "__lws_msg_type") == 0 && expr->as.call.num_args == 1) {
            char *msg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_msg_type(%s);", result, msg);
            codegen_writeln(ctx, "hml_release(&%s);", msg);
            free(msg);
            return result;
        }

        // __lws_msg_text(msg)
        if (strcmp(fn_name, "__lws_msg_text") == 0 && expr->as.call.num_args == 1) {
            char *msg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_msg_text(%s);", result, msg);
            codegen_writeln(ctx, "hml_release(&%s);", msg);
            free(msg);
            return result;
        }

        // __lws_msg_len(msg)
        if (strcmp(fn_name, "__lws_msg_len") == 0 && expr->as.call.num_args == 1) {
            char *msg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_msg_len(%s);", result, msg);
            codegen_writeln(ctx, "hml_release(&%s);", msg);
            free(msg);
            return result;
        }

        // __lws_msg_binary(msg)
        if (strcmp(fn_name, "__lws_msg_binary") == 0 && expr->as.call.num_args == 1) {
            char *msg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_msg_binary(%s);", result, msg);
            codegen_writeln(ctx, "hml_release(&%s);", msg);
            free(msg);
            return result;
        }

        // __lws_msg_free(msg)
        if (strcmp(fn_name, "__lws_msg_free") == 0 && expr->as.call.num_args == 1) {
            char *msg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_msg_free(%s);", result, msg);
            codegen_writeln(ctx, "hml_release(&%s);", msg);
            free(msg);
            return result;
        }

        // __lws_ws_server_create(host, port)
        if (strcmp(fn_name, "__lws_ws_server_create") == 0 && expr->as.call.num_args == 2) {
            char *host = codegen_expr(ctx, expr->as.call.args[0]);
            char *port = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_ws_server_create(%s, %s);", result, host, port);
            codegen_writeln(ctx, "hml_release(&%s);", host);
            codegen_writeln(ctx, "hml_release(&%s);", port);
            free(host);
            free(port);
            return result;
        }

        // __lws_ws_server_accept(server, timeout_ms)
        if (strcmp(fn_name, "__lws_ws_server_accept") == 0 && expr->as.call.num_args == 2) {
            char *server = codegen_expr(ctx, expr->as.call.args[0]);
            char *timeout = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_ws_server_accept(%s, %s);", result, server, timeout);
            codegen_writeln(ctx, "hml_release(&%s);", server);
            codegen_writeln(ctx, "hml_release(&%s);", timeout);
            free(server);
            free(timeout);
            return result;
        }

        // __lws_ws_server_close(server)
        if (strcmp(fn_name, "__lws_ws_server_close") == 0 && expr->as.call.num_args == 1) {
            char *server = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_lws_ws_server_close(%s);", result, server);
            codegen_writeln(ctx, "hml_release(&%s);", server);
            free(server);
            return result;
        }

        // ========== FILESYSTEM BUILTINS ==========

        // exists(path) / __exists(path)
        if (strcmp(fn_name, "__exists") == 0 && expr->as.call.num_args == 1) {
            char *path = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_exists(%s);", result, path);
            codegen_writeln(ctx, "hml_release(&%s);", path);
            free(path);
            return result;
        }

        // read_file(path) / __read_file(path)
        if (strcmp(fn_name, "__read_file") == 0 && expr->as.call.num_args == 1) {
            char *path = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_read_file(%s);", result, path);
            codegen_writeln(ctx, "hml_release(&%s);", path);
            free(path);
            return result;
        }

        // write_file(path, content) / __write_file(path, content)
        if (strcmp(fn_name, "__write_file") == 0 && expr->as.call.num_args == 2) {
            char *path = codegen_expr(ctx, expr->as.call.args[0]);
            char *content = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_write_file(%s, %s);", result, path, content);
            codegen_writeln(ctx, "hml_release(&%s);", path);
            codegen_writeln(ctx, "hml_release(&%s);", content);
            free(path);
            free(content);
            return result;
        }

        // append_file(path, content) / __append_file(path, content)
        if (strcmp(fn_name, "__append_file") == 0 && expr->as.call.num_args == 2) {
            char *path = codegen_expr(ctx, expr->as.call.args[0]);
            char *content = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_append_file(%s, %s);", result, path, content);
            codegen_writeln(ctx, "hml_release(&%s);", path);
            codegen_writeln(ctx, "hml_release(&%s);", content);
            free(path);
            free(content);
            return result;
        }

        // remove_file(path) / __remove_file(path)
        if (strcmp(fn_name, "__remove_file") == 0 && expr->as.call.num_args == 1) {
            char *path = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_remove_file(%s);", result, path);
            codegen_writeln(ctx, "hml_release(&%s);", path);
            free(path);
            return result;
        }

        // rename(old_path, new_path) / __rename(old_path, new_path)
        if (strcmp(fn_name, "__rename") == 0 && expr->as.call.num_args == 2) {
            char *old_path = codegen_expr(ctx, expr->as.call.args[0]);
            char *new_path = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_rename_file(%s, %s);", result, old_path, new_path);
            codegen_writeln(ctx, "hml_release(&%s);", old_path);
            codegen_writeln(ctx, "hml_release(&%s);", new_path);
            free(old_path);
            free(new_path);
            return result;
        }

        // copy_file(src, dest) / __copy_file(src, dest)
        if (strcmp(fn_name, "__copy_file") == 0 && expr->as.call.num_args == 2) {
            char *src = codegen_expr(ctx, expr->as.call.args[0]);
            char *dest = codegen_expr(ctx, expr->as.call.args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_copy_file(%s, %s);", result, src, dest);
            codegen_writeln(ctx, "hml_release(&%s);", src);
            codegen_writeln(ctx, "hml_release(&%s);", dest);
            free(src);
            free(dest);
            return result;
        }

        // is_file(path) / __is_file(path)
        if (strcmp(fn_name, "__is_file") == 0 && expr->as.call.num_args == 1) {
            char *path = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_is_file(%s);", result, path);
            codegen_writeln(ctx, "hml_release(&%s);", path);
            free(path);
            return result;
        }

        // is_dir(path) / __is_dir(path)
        if (strcmp(fn_name, "__is_dir") == 0 && expr->as.call.num_args == 1) {
            char *path = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_is_dir(%s);", result, path);
            codegen_writeln(ctx, "hml_release(&%s);", path);
            free(path);
            return result;
        }

        // file_stat(path) / __file_stat(path)
        if (strcmp(fn_name, "__file_stat") == 0 && expr->as.call.num_args == 1) {
            char *path = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_file_stat(%s);", result, path);
            codegen_writeln(ctx, "hml_release(&%s);", path);
            free(path);
            return result;
        }

        // make_dir(path, [mode])
        if (strcmp(fn_name, "__make_dir") == 0 && (expr->as.call.num_args == 1 || expr->as.call.num_args == 2)) {
            char *path = codegen_expr(ctx, expr->as.call.args[0]);
            if (expr->as.call.num_args == 2) {
                char *mode = codegen_expr(ctx, expr->as.call.args[1]);
                codegen_writeln(ctx, "HmlValue %s = hml_make_dir(%s, %s);", result, path, mode);
                codegen_writeln(ctx, "hml_release(&%s);", mode);
                free(mode);
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_make_dir(%s, hml_val_u32(0755));", result, path);
            }
            codegen_writeln(ctx, "hml_release(&%s);", path);
            free(path);
            return result;
        }

        // remove_dir(path)
        if (strcmp(fn_name, "__remove_dir") == 0 && expr->as.call.num_args == 1) {
            char *path = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_remove_dir(%s);", result, path);
            codegen_writeln(ctx, "hml_release(&%s);", path);
            free(path);
            return result;
        }

        // list_dir(path)
        if (strcmp(fn_name, "__list_dir") == 0 && expr->as.call.num_args == 1) {
            char *path = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_list_dir(%s);", result, path);
            codegen_writeln(ctx, "hml_release(&%s);", path);
            free(path);
            return result;
        }

        // cwd()
        if (strcmp(fn_name, "__cwd") == 0 && expr->as.call.num_args == 0) {
            codegen_writeln(ctx, "HmlValue %s = hml_cwd();", result);
            return result;
        }

        // chdir(path)
        if (strcmp(fn_name, "__chdir") == 0 && expr->as.call.num_args == 1) {
            char *path = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_chdir(%s);", result, path);
            codegen_writeln(ctx, "hml_release(&%s);", path);
            free(path);
            return result;
        }

        // absolute_path(path) / __absolute_path(path)
        if (strcmp(fn_name, "__absolute_path") == 0 && expr->as.call.num_args == 1) {
            char *path = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_absolute_path(%s);", result, path);
            codegen_writeln(ctx, "hml_release(&%s);", path);
            free(path);
            return result;
        }

        // ========== REGEX BUILTINS ==========

        // __regex_compile(pattern, flags?) - compile a regex pattern
        if (strcmp(fn_name, "__regex_compile") == 0 && (expr->as.call.num_args == 1 || expr->as.call.num_args == 2)) {
            char *pattern = codegen_expr(ctx, expr->as.call.args[0]);
            if (expr->as.call.num_args == 2) {
                char *flags = codegen_expr(ctx, expr->as.call.args[1]);
                codegen_writeln(ctx, "HmlValue %s = hml_regex_compile(%s, %s);", result, pattern, flags);
                codegen_writeln(ctx, "hml_release(&%s);", flags);
                free(flags);
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_regex_compile(%s, hml_val_null());", result, pattern);
            }
            codegen_writeln(ctx, "hml_release(&%s);", pattern);
            free(pattern);
            return result;
        }

        // __regex_test(preg, text, eflags?) - test if text matches regex
        if (strcmp(fn_name, "__regex_test") == 0 && (expr->as.call.num_args == 2 || expr->as.call.num_args == 3)) {
            char *preg = codegen_expr(ctx, expr->as.call.args[0]);
            char *text = codegen_expr(ctx, expr->as.call.args[1]);
            if (expr->as.call.num_args == 3) {
                char *eflags = codegen_expr(ctx, expr->as.call.args[2]);
                codegen_writeln(ctx, "HmlValue %s = hml_regex_test(%s, %s, %s);", result, preg, text, eflags);
                codegen_writeln(ctx, "hml_release(&%s);", eflags);
                free(eflags);
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_regex_test(%s, %s, hml_val_null());", result, preg, text);
            }
            codegen_writeln(ctx, "hml_release(&%s);", preg);
            codegen_writeln(ctx, "hml_release(&%s);", text);
            free(preg);
            free(text);
            return result;
        }

        // __regex_match(preg, text, max_matches?) - find matches in text
        if (strcmp(fn_name, "__regex_match") == 0 && (expr->as.call.num_args == 2 || expr->as.call.num_args == 3)) {
            char *preg = codegen_expr(ctx, expr->as.call.args[0]);
            char *text = codegen_expr(ctx, expr->as.call.args[1]);
            if (expr->as.call.num_args == 3) {
                char *max_matches = codegen_expr(ctx, expr->as.call.args[2]);
                codegen_writeln(ctx, "HmlValue %s = hml_regex_match(%s, %s, %s);", result, preg, text, max_matches);
                codegen_writeln(ctx, "hml_release(&%s);", max_matches);
                free(max_matches);
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_regex_match(%s, %s, hml_val_null());", result, preg, text);
            }
            codegen_writeln(ctx, "hml_release(&%s);", preg);
            codegen_writeln(ctx, "hml_release(&%s);", text);
            free(preg);
            free(text);
            return result;
        }

        // __regex_free(preg) - free a compiled regex
        if (strcmp(fn_name, "__regex_free") == 0 && expr->as.call.num_args == 1) {
            char *preg = codegen_expr(ctx, expr->as.call.args[0]);
            codegen_writeln(ctx, "HmlValue %s = hml_regex_free(%s);", result, preg);
            codegen_writeln(ctx, "hml_release(&%s);", preg);
            free(preg);
            return result;
        }

        // __regex_error(errcode, preg?) - get error message for regex error code
        if (strcmp(fn_name, "__regex_error") == 0 && (expr->as.call.num_args == 1 || expr->as.call.num_args == 2)) {
            char *errcode = codegen_expr(ctx, expr->as.call.args[0]);
            if (expr->as.call.num_args == 2) {
                char *preg = codegen_expr(ctx, expr->as.call.args[1]);
                codegen_writeln(ctx, "HmlValue %s = hml_regex_error(%s, %s);", result, errcode, preg);
                codegen_writeln(ctx, "hml_release(&%s);", preg);
                free(preg);
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_regex_error(%s, hml_val_null());", result, errcode);
            }
            codegen_writeln(ctx, "hml_release(&%s);", errcode);
            free(errcode);
            return result;
        }

        // __regex_replace(preg, text, replacement) - replace first match
        if (strcmp(fn_name, "__regex_replace") == 0 && expr->as.call.num_args == 3) {
            char *preg = codegen_expr(ctx, expr->as.call.args[0]);
            char *text = codegen_expr(ctx, expr->as.call.args[1]);
            char *replacement = codegen_expr(ctx, expr->as.call.args[2]);
            codegen_writeln(ctx, "HmlValue %s = hml_regex_replace(%s, %s, %s);", result, preg, text, replacement);
            codegen_writeln(ctx, "hml_release(&%s);", preg);
            codegen_writeln(ctx, "hml_release(&%s);", text);
            codegen_writeln(ctx, "hml_release(&%s);", replacement);
            free(preg);
            free(text);
            free(replacement);
            return result;
        }

        // __regex_replace_all(preg, text, replacement) - replace all matches
        if (strcmp(fn_name, "__regex_replace_all") == 0 && expr->as.call.num_args == 3) {
            char *preg = codegen_expr(ctx, expr->as.call.args[0]);
            char *text = codegen_expr(ctx, expr->as.call.args[1]);
            char *replacement = codegen_expr(ctx, expr->as.call.args[2]);
            codegen_writeln(ctx, "HmlValue %s = hml_regex_replace_all(%s, %s, %s);", result, preg, text, replacement);
            codegen_writeln(ctx, "hml_release(&%s);", preg);
            codegen_writeln(ctx, "hml_release(&%s);", text);
            codegen_writeln(ctx, "hml_release(&%s);", replacement);
            free(preg);
            free(text);
            free(replacement);
            return result;
        }

        // Handle user-defined function by name (hml_fn_<name>)
        // Main file functions should use generic call path (hml_call_function)
        // to properly handle optional parameters with defaults

        // Check if this is an imported function first (even if registered as local)
        ImportBinding *import_binding = NULL;
        if (ctx->current_module) {
            import_binding = module_find_import(ctx->current_module, fn_name);
        } else {
            import_binding = codegen_find_main_import(ctx, fn_name);
        }

        // OPTIMIZATION: Function inlining for small pure functions
        // This eliminates function call overhead for simple helper functions
        // Multi-level inlining (up to HML_MAX_INLINE_DEPTH) allows nested helpers
        // like rotr() inside ep0() to be fully inlined in hot loops
        if (ctx->optimize && ctx->inline_depth < HML_MAX_INLINE_DEPTH && !import_binding && !ctx->current_module &&
            codegen_is_main_func_inlinable(ctx, fn_name) &&
            expr->as.call.num_args == codegen_get_main_func_params(ctx, fn_name)) {

            Expr *func_ast = codegen_get_main_func_ast(ctx, fn_name);
            if (func_ast && func_ast->type == EXPR_FUNCTION) {
                // Get the return expression (we know it's just "return expr")
                Stmt *body = func_ast->as.function.body;
                Expr *return_expr = body->as.block.statements[0]->as.return_stmt.value;

                // Safety check: don't inline if the return expression references
                // a main variable that is currently shadowed by a block-local variable.
                // Inlining would resolve the identifier to the shadow instead of the
                // original _main_ prefixed variable, producing incorrect results.
                int has_shadow_conflict = codegen_inline_has_shadow_conflict(ctx, return_expr);
                if (has_shadow_conflict) goto skip_inline;

                // Declare result BEFORE the block so it remains in scope after
                codegen_writeln(ctx, "HmlValue %s;", result);

                // Create a scope block for the inlined code
                codegen_writeln(ctx, "{ // INLINE: %s", fn_name);
                codegen_indent_inc(ctx);

                // Track inlining depth to prevent excessive code bloat
                ctx->inline_depth++;

                // Push a type scope for the inlined code (for type specialization)
                if (ctx->type_ctx) {
                    type_check_push_scope(ctx->type_ctx);
                }

                // Evaluate all arguments FIRST (before any parameter binding)
                // This prevents inlined param names from shadowing variables used
                // in later argument expressions (e.g., add(result, inner(a, b, i))
                // where add's param 'a' would shadow the outer 'a' during inner's eval)
                int consumed_checkpoint = codegen_consumed_checkpoint(ctx);
                int num_params = func_ast->as.function.num_params;
                char **arg_vals = malloc(num_params * sizeof(char*));
                for (int i = 0; i < num_params; i++) {
                    arg_vals[i] = codegen_expr(ctx, expr->as.call.args[i]);
                }

                // Now bind all parameters to their evaluated argument values
                for (int i = 0; i < num_params; i++) {
                    // Infer argument type for type specialization
                    if (ctx->type_ctx) {
                        CheckedType *arg_type = type_check_infer_expr(ctx->type_ctx, expr->as.call.args[i]);
                        if (arg_type && arg_type->kind != CHECKED_UNKNOWN && arg_type->kind != CHECKED_ANY) {
                            // Bind parameter name to inferred type (is_const=0, is_param=1, line=0)
                            // is_param=1 prevents unboxing optimization from treating
                            // inlined params as native C types (they are HmlValue)
                            type_check_bind(ctx->type_ctx, func_ast->as.function.param_names[i], arg_type, 0, 1, 0);
                        }
                    }

                    char *param_name = codegen_sanitize_ident(func_ast->as.function.param_names[i]);
                    codegen_writeln(ctx, "HmlValue %s = %s;", param_name, arg_vals[i]);
                    codegen_add_local(ctx, func_ast->as.function.param_names[i]);
                    // Also add as shadow so inlined params shadow main-level vars
                    // (without this, `a` resolves to `_main_a` during main-scope inlining)
                    codegen_add_shadow(ctx, func_ast->as.function.param_names[i]);
                    free(arg_vals[i]);
                    free(param_name);
                }
                free(arg_vals);

                // Generate the inlined expression
                char *inline_result = NULL;
                int returned_param_index = -1;
                if (return_expr->type == EXPR_IDENT) {
                    for (int i = 0; i < num_params; i++) {
                        if (strcmp(return_expr->as.ident.name, func_ast->as.function.param_names[i]) == 0) {
                            returned_param_index = i;
                            break;
                        }
                    }
                }
                if (returned_param_index >= 0) {
                    inline_result = codegen_sanitize_ident(func_ast->as.function.param_names[returned_param_index]);
                    codegen_mark_temp_consumed(ctx, inline_result);
                } else {
                    inline_result = codegen_expr(ctx, return_expr);
                }
                codegen_writeln(ctx, "%s = %s;", result, inline_result);

                // Release parameter bindings (primitives will be skipped)
                for (int i = 0; i < num_params; i++) {
                    char *param_name = codegen_sanitize_ident(func_ast->as.function.param_names[i]);
                    if (!codegen_is_temp_consumed(ctx, param_name)) {
                        codegen_writeln(ctx, "hml_release_if_needed(&%s);", param_name);
                    }
                    codegen_remove_local(ctx, func_ast->as.function.param_names[i]);
                    codegen_remove_shadow(ctx, func_ast->as.function.param_names[i]);
                    free(param_name);
                }
                free(inline_result);

                // Pop type scope
                if (ctx->type_ctx) {
                    type_check_pop_scope(ctx->type_ctx);
                }

                // Restore inlining depth
                ctx->inline_depth--;

                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
                codegen_restore_consumed(ctx, consumed_checkpoint);

                return result;
            }
        }
        skip_inline:

        if (codegen_is_main_func(ctx, fn_name) && !import_binding && !ctx->current_module &&
            expr->as.call.arg_names == NULL) {  // Skip direct call when named args are used
            // OPTIMIZATION: Main file function definition - call directly
            // This is safe because we know the function signature at compile time
            int expected_params = codegen_get_main_func_params(ctx, fn_name);
            int has_rest = codegen_get_main_func_has_rest(ctx, fn_name);
            int *param_is_ref = codegen_get_main_func_param_is_ref(ctx, fn_name);
            char **arg_temps = NULL;
            int *arg_is_ref = NULL;
            if (expr->as.call.num_args > 0) {
                arg_temps = malloc(expr->as.call.num_args * sizeof(char*));
                arg_is_ref = calloc(expr->as.call.num_args, sizeof(int));
                if (!arg_temps || !arg_is_ref) {
                    free(arg_temps);
                    free(arg_is_ref);
                    codegen_error(ctx, 0, "Memory allocation failed for function call arguments");
                    free(result);
                    return strdup("hml_val_null()");
                }
            }
            for (int i = 0; i < expr->as.call.num_args; i++) {
                // For ref params, use codegen_ref_arg to get address of actual variable
                if (param_is_ref && i < expected_params && param_is_ref[i]) {
                    arg_temps[i] = codegen_ref_arg(ctx, expr->as.call.args[i]);
                    arg_is_ref[i] = 1;
                } else {
                    arg_temps[i] = codegen_expr(ctx, expr->as.call.args[i]);
                    arg_is_ref[i] = 0;
                }
            }

            // For rest params, we need to collect extra args into an array
            char *rest_array_temp = NULL;
            if (has_rest && expr->as.call.num_args > expected_params && arg_temps) {
                // Create array for rest args
                rest_array_temp = codegen_temp(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_val_array();", rest_array_temp);
                for (int i = expected_params; i < expr->as.call.num_args; i++) {
                    codegen_writeln(ctx, "hml_array_push(%s, %s);", rest_array_temp, arg_temps[i]);
                }
            }

            codegen_indent(ctx);
            fprintf(ctx->output, "HmlValue %s = hml_fn_%s(NULL", result, fn_name);
            // Pass regular args (up to expected_params)
            int regular_args = has_rest ? (expr->as.call.num_args < expected_params ? expr->as.call.num_args : expected_params) : expr->as.call.num_args;
            for (int i = 0; i < regular_args && arg_temps; i++) {
                // Ref args already have & in their string, regular args are passed directly
                fprintf(ctx->output, ", %s", arg_temps[i]);
            }
            // Fill in hml_val_null() for missing optional parameters
            for (int i = regular_args; i < expected_params; i++) {
                fprintf(ctx->output, ", hml_val_null()");
            }
            // Pass rest array (or empty array if no extra args)
            if (has_rest) {
                if (rest_array_temp) {
                    fprintf(ctx->output, ", %s", rest_array_temp);
                } else {
                    fprintf(ctx->output, ", hml_val_array()");
                }
            }
            fprintf(ctx->output, ");\n");

            for (int i = 0; i < expr->as.call.num_args; i++) {
                // Only release non-ref args (ref args are just addresses, nothing to release)
                if (!arg_is_ref[i]) {
                    codegen_writeln(ctx, "hml_release(&%s);", arg_temps[i]);
                }
                free(arg_temps[i]);
            }
            if (rest_array_temp) {
                codegen_writeln(ctx, "hml_release(&%s);", rest_array_temp);
                free(rest_array_temp);
            }
            free(arg_temps);
            free(arg_is_ref);
            return result;
        } else if (codegen_is_main_var(ctx, fn_name) && !import_binding &&
                   expr->as.call.arg_names == NULL) {  // Skip direct call when named args are used
            // Main file variable that's NOT a function definition (e.g., assigned closure)
            // Use generic call path to properly handle closures
            // Fall through to generic handling
        } else if (codegen_is_local(ctx, fn_name) && !import_binding &&
                   expr->as.call.arg_names == NULL) {  // Skip direct call when named args are used
            // Check if this local is actually a captured main file function - use direct call
            if (codegen_is_main_func(ctx, fn_name) && !ctx->current_module) {
                int expected_params = codegen_get_main_func_params(ctx, fn_name);
                int has_rest = codegen_get_main_func_has_rest(ctx, fn_name);
                int *param_is_ref = codegen_get_main_func_param_is_ref(ctx, fn_name);
                char **arg_temps = NULL;
                int *arg_is_ref = NULL;
                if (expr->as.call.num_args > 0) {
                    arg_temps = malloc(expr->as.call.num_args * sizeof(char*));
                    arg_is_ref = calloc(expr->as.call.num_args, sizeof(int));
                    if (!arg_temps || !arg_is_ref) {
                        free(arg_temps);
                        free(arg_is_ref);
                        codegen_error(ctx, 0, "Memory allocation failed for function call arguments");
                        free(result);
                        return strdup("hml_val_null()");
                    }
                }
                for (int i = 0; i < expr->as.call.num_args; i++) {
                    // For ref params, use codegen_ref_arg to get address of actual variable
                    if (param_is_ref && i < expected_params && param_is_ref[i]) {
                        arg_temps[i] = codegen_ref_arg(ctx, expr->as.call.args[i]);
                        arg_is_ref[i] = 1;
                    } else {
                        arg_temps[i] = codegen_expr(ctx, expr->as.call.args[i]);
                        arg_is_ref[i] = 0;
                    }
                }

                // For rest params, we need to collect extra args into an array
                char *rest_array_temp = NULL;
                if (has_rest && expr->as.call.num_args > expected_params && arg_temps) {
                    // Create array for rest args
                    rest_array_temp = codegen_temp(ctx);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_array();", rest_array_temp);
                    for (int i = expected_params; i < expr->as.call.num_args; i++) {
                        codegen_writeln(ctx, "hml_array_push(%s, %s);", rest_array_temp, arg_temps[i]);
                    }
                }

                codegen_indent(ctx);
                fprintf(ctx->output, "HmlValue %s = hml_fn_%s(NULL", result, fn_name);
                // Pass regular args (up to expected_params)
                int regular_args = has_rest ? (expr->as.call.num_args < expected_params ? expr->as.call.num_args : expected_params) : expr->as.call.num_args;
                for (int i = 0; i < regular_args && arg_temps; i++) {
                    // Ref args already have & in their string, regular args are passed directly
                    fprintf(ctx->output, ", %s", arg_temps[i]);
                }
                // Fill in hml_val_null() for missing optional parameters
                for (int i = regular_args; i < expected_params; i++) {
                    fprintf(ctx->output, ", hml_val_null()");
                }
                // Pass rest array (or empty array if no extra args)
                if (has_rest) {
                    if (rest_array_temp) {
                        fprintf(ctx->output, ", %s", rest_array_temp);
                    } else {
                        fprintf(ctx->output, ", hml_val_array()");
                    }
                }
                fprintf(ctx->output, ");\n");

                for (int i = 0; i < expr->as.call.num_args; i++) {
                    // Only release non-ref args (ref args are just addresses, nothing to release)
                    if (!arg_is_ref[i]) {
                        codegen_writeln(ctx, "hml_release(&%s);", arg_temps[i]);
                    }
                    free(arg_temps[i]);
                }
                if (rest_array_temp) {
                    codegen_writeln(ctx, "hml_release(&%s);", rest_array_temp);
                    free(rest_array_temp);
                }
                free(arg_temps);
                free(arg_is_ref);
                return result;
            }
            // It's a true local function variable (not a main func) - call through hml_call_function
            // Fall through to generic handling
        } else if (import_binding && !import_binding->is_function) {
            // Imported variable that holds a function value (e.g., export let sleep = __sleep)
            // Fall through to generic call path - will use hml_call_function on the variable
        } else if (expr->as.call.arg_names != NULL) {
            // Named arguments used - fall through to generic call path for runtime reordering
        } else {
            // Direct call path for imported functions and module functions
            // import_binding was already set above

            // Determine the expected number of parameters for the function
            int expected_params = expr->as.call.num_args;  // Default to provided args
            if (import_binding && import_binding->is_function && import_binding->num_params > 0) {
                expected_params = import_binding->num_params;
            } else if (ctx->current_module && !import_binding) {
                // Look up export in current module for self-calls
                ExportedSymbol *exp = module_find_export(ctx->current_module, fn_name);
                if (exp && exp->is_function && exp->num_params > 0) {
                    expected_params = exp->num_params;
                }
            }

            // Try to call as hml_fn_<name> directly with NULL for closure env
            char **arg_temps = NULL;
            if (expr->as.call.num_args > 0) {
                arg_temps = malloc(expr->as.call.num_args * sizeof(char*));
                if (!arg_temps) {
                    codegen_error(ctx, 0, "Memory allocation failed for function call arguments");
                    free(result);
                    return strdup("hml_val_null()");
                }
            }
            for (int i = 0; i < expr->as.call.num_args; i++) {
                arg_temps[i] = codegen_expr(ctx, expr->as.call.args[i]);
            }

            codegen_indent(ctx);
            // All functions use closure env as first param for uniform calling convention
            if (import_binding) {
                if (import_binding->is_extern) {
                    // Extern functions use hml_fn_<name> without module prefix
                    fprintf(ctx->output, "HmlValue %s = hml_fn_%s(NULL", result,
                            import_binding->original_name);
                } else {
                    // Use the mangled function name from the import
                    // module_prefix is like "_mod1_", original_name is the export name
                    fprintf(ctx->output, "HmlValue %s = %sfn_%s(NULL", result,
                            import_binding->module_prefix, import_binding->original_name);
                }
            } else if (ctx->current_module) {
                // Check if this is an extern function - externs don't get module prefix
                if (module_is_extern_fn(ctx->current_module, fn_name)) {
                    fprintf(ctx->output, "HmlValue %s = hml_fn_%s(NULL", result, fn_name);
                } else {
                    // Function in current module - use module prefix
                    fprintf(ctx->output, "HmlValue %s = %sfn_%s(NULL", result,
                            ctx->current_module->module_prefix, fn_name);
                }
            } else {
                // Main file function definition - call directly
                fprintf(ctx->output, "HmlValue %s = hml_fn_%s(NULL", result, fn_name);
            }
            // Output provided arguments
            for (int i = 0; i < expr->as.call.num_args; i++) {
                fprintf(ctx->output, ", %s", arg_temps[i]);
            }
            // Fill in hml_val_null() for missing optional parameters
            for (int i = expr->as.call.num_args; i < expected_params; i++) {
                fprintf(ctx->output, ", hml_val_null()");
            }
            fprintf(ctx->output, ");\n");

            for (int i = 0; i < expr->as.call.num_args; i++) {
                codegen_writeln(ctx, "hml_release(&%s);", arg_temps[i]);
                free(arg_temps[i]);
            }
            free(arg_temps);
            return result;
        }
    }

    // Handle method calls: obj.method(args)
    if (expr->as.call.func->type == EXPR_GET_PROPERTY) {
        if (codegen_call_methods(ctx, expr, result, expr->as.call.func,
                                 expr->as.call.args, expr->as.call.num_args)) {
            return result;
        }
        // codegen_call_methods handles all EXPR_GET_PROPERTY cases (always returns 1)
        // If we somehow get here, fall through to generic call handling
    }


    // Generic function call handling
    char *func_val = codegen_expr(ctx, expr->as.call.func);

    // Check if func is an optional chain - if it evaluated to null, short-circuit
    // This handles: obj?.method(args) when obj is null
    int is_optional_chain_call = (expr->as.call.func->type == EXPR_OPTIONAL_CHAIN);

    if (is_optional_chain_call) {
        codegen_writeln(ctx, "HmlValue %s;", result);
        codegen_writeln(ctx, "if (%s.type == HML_VAL_NULL) {", func_val);
        codegen_indent_inc(ctx);
        codegen_writeln(ctx, "%s = hml_val_null();", result);
        codegen_indent_dec(ctx);
        codegen_writeln(ctx, "} else {");
        codegen_indent_inc(ctx);
    }

    // Reserve a counter for the args array BEFORE generating arg expressions
    // (which may increment temp_counter internally)
    int args_counter = ctx->temp_counter++;

    // Generate code for arguments
    char **arg_temps = malloc(expr->as.call.num_args * sizeof(char*));
    for (int i = 0; i < expr->as.call.num_args; i++) {
        arg_temps[i] = codegen_expr(ctx, expr->as.call.args[i]);
    }

    // Check if we have named arguments
    int has_named_args = (expr->as.call.arg_names != NULL);

    // Build args array
    if (expr->as.call.num_args > 0) {
        codegen_writeln(ctx, "HmlValue _args%d[%d];", args_counter, expr->as.call.num_args);
        for (int i = 0; i < expr->as.call.num_args; i++) {
            codegen_writeln(ctx, "_args%d[%d] = %s;", args_counter, i, arg_temps[i]);
        }

        if (has_named_args) {
            // Generate argument names array for named argument call
            int names_counter = ctx->temp_counter++;
            codegen_writeln(ctx, "const char *_arg_names%d[%d] = {", names_counter, expr->as.call.num_args);
            for (int i = 0; i < expr->as.call.num_args; i++) {
                if (expr->as.call.arg_names[i]) {
                    codegen_write(ctx, "\"%s\"", expr->as.call.arg_names[i]);
                } else {
                    codegen_write(ctx, "NULL");
                }
                if (i < expr->as.call.num_args - 1) {
                    codegen_write(ctx, ", ");
                }
            }
            codegen_writeln(ctx, "};");

            if (is_optional_chain_call) {
                codegen_writeln(ctx, "%s = hml_call_function_named(%s, _args%d, _arg_names%d, %d);",
                              result, func_val, args_counter, names_counter, expr->as.call.num_args);
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_call_function_named(%s, _args%d, _arg_names%d, %d);",
                              result, func_val, args_counter, names_counter, expr->as.call.num_args);
            }
        } else {
            if (is_optional_chain_call) {
                codegen_writeln(ctx, "%s = hml_call_function(%s, _args%d, %d);",
                              result, func_val, args_counter, expr->as.call.num_args);
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_call_function(%s, _args%d, %d);",
                              result, func_val, args_counter, expr->as.call.num_args);
            }
        }
    } else {
        if (is_optional_chain_call) {
            codegen_writeln(ctx, "%s = hml_call_function(%s, NULL, 0);", result, func_val);
        } else {
            codegen_writeln(ctx, "HmlValue %s = hml_call_function(%s, NULL, 0);", result, func_val);
        }
    }

    // Release argument temporaries
    for (int i = 0; i < expr->as.call.num_args; i++) {
        codegen_writeln(ctx, "hml_release(&%s);", arg_temps[i]);
        free(arg_temps[i]);
    }
    free(arg_temps);

    if (is_optional_chain_call) {
        codegen_indent_dec(ctx);
        codegen_writeln(ctx, "}");
    }

    // Release function temporary
    codegen_writeln(ctx, "hml_release(&%s);", func_val);
    free(func_val);

    return result;
}
