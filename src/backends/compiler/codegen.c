/*
 * Hemlock C Code Generator - Core
 *
 * Core functionality: context management, output helpers, variable tracking,
 * scope management, and defer support.
 *
 * Expression generation:  codegen_expr.c
 * Statement generation:   codegen_stmt.c
 * Closure analysis:       codegen_closure.c
 * Program generation:     codegen_program.c
 * Module compilation:     codegen_module.c
 */

#include "codegen_internal.h"

// ========== SAFE CAPACITY GROWTH ==========

// Helper to safely compute new capacity (returns -1 on overflow)
static int safe_double_capacity(int current, int min_default) {
    if (current == 0) return min_default;
    if (current > INT_MAX / 2) return -1;  // Would overflow
    return current * 2;
}

// ========== CONTEXT MANAGEMENT ==========

CodegenContext* codegen_new(FILE *output) {
    CodegenContext *ctx = malloc(sizeof(CodegenContext));
    if (!ctx) {
        fprintf(stderr, "error: Memory allocation failed for codegen context\n");
        return NULL;
    }
    ctx->output = output;
    ctx->indent = 0;
    ctx->temp_counter = 0;
    ctx->label_counter = 0;
    ctx->func_counter = 0;
    ctx->in_function = 0;
    ctx->inline_depth = 0;
    ctx->local_vars = NULL;
    ctx->local_needs_cleanup = NULL;
    ctx->num_locals = 0;
    ctx->local_capacity = 0;
    ctx->locals_body_start = 0;
    ctx->consumed_temps = NULL;
    ctx->num_consumed_temps = 0;
    ctx->consumed_temps_capacity = 0;
    ctx->current_scope = NULL;
    ctx->closures = NULL;
    ctx->func_params = NULL;
    ctx->num_func_params = 0;
    ctx->func_param_is_ref = NULL;
    ctx->defer_stack = NULL;
    ctx->defer_scope_depth = 0;
    ctx->current_closure = NULL;
    ctx->shared_env_name = NULL;
    ctx->shared_env_vars = NULL;
    ctx->shared_env_num_vars = 0;
    ctx->shared_env_capacity = 0;
    ctx->last_closure_env_id = -1;
    ctx->last_closure_captured = NULL;
    ctx->last_closure_num_captured = 0;
    ctx->module_cache = NULL;
    ctx->current_module = NULL;
    ctx->main_vars = NULL;
    ctx->num_main_vars = 0;
    ctx->main_vars_capacity = 0;
    ctx->main_funcs = NULL;
    ctx->main_func_params = NULL;
    ctx->main_func_has_rest = NULL;
    ctx->main_func_param_is_ref = NULL;
    ctx->main_func_ast = NULL;
    ctx->main_func_inlinable = NULL;
    ctx->num_main_funcs = 0;
    ctx->main_funcs_capacity = 0;
    ctx->main_imports = NULL;
    ctx->num_main_imports = 0;
    ctx->main_imports_capacity = 0;
    ctx->extern_fns = NULL;
    ctx->num_extern_fns = 0;
    ctx->extern_fns_capacity = 0;
    ctx->shadow_vars = NULL;
    ctx->num_shadow_vars = 0;
    ctx->shadow_vars_capacity = 0;
    ctx->const_vars = NULL;
    ctx->num_const_vars = 0;
    ctx->const_vars_capacity = 0;
    ctx->try_finally_depth = 0;
    ctx->finally_labels = NULL;
    ctx->return_value_vars = NULL;
    ctx->has_return_vars = NULL;
    ctx->try_finally_capacity = 0;
    ctx->try_body_depth = 0;
    ctx->loop_depth = 0;
    ctx->loop_body_locals = NULL;
    ctx->loop_body_try_depth = NULL;
    ctx->loop_body_depth = 0;
    ctx->loop_body_capacity = 0;
    ctx->switch_depth = 0;
    ctx->switch_end_labels = NULL;
    ctx->switch_end_capacity = 0;
    ctx->for_continue_labels = NULL;
    ctx->for_continue_depth = 0;
    ctx->for_continue_capacity = 0;
    ctx->loop_labels = NULL;
    ctx->loop_break_labels = NULL;
    ctx->loop_continue_labels = NULL;
    ctx->loop_label_body_locals = NULL;
    ctx->loop_label_try_depth = NULL;
    ctx->loop_label_depth = 0;
    ctx->loop_label_capacity = 0;
    ctx->type_ctx = NULL;  // Set by caller (main.c) if type checking enabled
    ctx->optimize = 1;  // Enable optimization by default
    ctx->stack_check = 1;  // Enable stack checking by default (can be overridden by caller)
    ctx->has_defers = 0;  // Track if any defers exist in current function
    ctx->defer_unwind_active = 0;
    ctx->tail_call_func_name = NULL;  // Tail call optimization tracking
    ctx->tail_call_label = NULL;
    ctx->tail_call_func_expr = NULL;
    ctx->error_count = 0;
    ctx->warning_count = 0;
    ctx->sandbox_flags = 0;  // No sandbox restrictions by default
    ctx->sandbox_root = NULL;  // No sandbox root directory by default
    ctx->target_wasm = 0;
    return ctx;
}

void codegen_free(CodegenContext *ctx) {
    if (ctx) {
        for (int i = 0; i < ctx->num_locals; i++) {
            if (ctx->local_vars[i]) {
                free(ctx->local_vars[i]);
            }
        }
        free(ctx->local_vars);
        free(ctx->local_needs_cleanup);

        // Free closures
        ClosureInfo *c = ctx->closures;
        while (c) {
            ClosureInfo *next = c->next;
            free(c->func_name);
            for (int i = 0; i < c->num_captured; i++) {
                free(c->captured_vars[i]);
            }
            free(c->captured_vars);
            free(c->shared_env_indices);
            free(c);
            c = next;
        }

        // Free scopes
        while (ctx->current_scope) {
            codegen_pop_scope(ctx);
        }

        // Free defers
        codegen_defer_clear(ctx);

        // Free last closure tracking
        if (ctx->last_closure_captured) {
            for (int i = 0; i < ctx->last_closure_num_captured; i++) {
                free(ctx->last_closure_captured[i]);
            }
            free(ctx->last_closure_captured);
        }

        // Free main file variables tracking
        if (ctx->main_vars) {
            for (int i = 0; i < ctx->num_main_vars; i++) {
                free(ctx->main_vars[i]);
            }
            free(ctx->main_vars);
        }

        // Free main file functions tracking
        if (ctx->main_funcs) {
            for (int i = 0; i < ctx->num_main_funcs; i++) {
                free(ctx->main_funcs[i]);
            }
            free(ctx->main_funcs);
        }
        if (ctx->main_func_params) {
            free(ctx->main_func_params);
        }
        if (ctx->main_func_has_rest) {
            free(ctx->main_func_has_rest);
        }
        if (ctx->main_func_param_is_ref) {
            for (int i = 0; i < ctx->num_main_funcs; i++) {
                if (ctx->main_func_param_is_ref[i]) {
                    free(ctx->main_func_param_is_ref[i]);
                }
            }
            free(ctx->main_func_param_is_ref);
        }
        if (ctx->main_func_ast) {
            free(ctx->main_func_ast);  // Don't free the AST nodes themselves - owned by parser
        }
        if (ctx->main_func_inlinable) {
            free(ctx->main_func_inlinable);
        }

        // Free extern fn tracking
        if (ctx->extern_fns) {
            for (int i = 0; i < ctx->num_extern_fns; i++) {
                free(ctx->extern_fns[i]);
            }
            free(ctx->extern_fns);
        }

        // Free main file imports tracking
        if (ctx->main_imports) {
            for (int i = 0; i < ctx->num_main_imports; i++) {
                free(ctx->main_imports[i].local_name);
                free(ctx->main_imports[i].original_name);
                free(ctx->main_imports[i].module_prefix);
            }
            free(ctx->main_imports);
        }

        // Free any shared closure environment metadata left by an interrupted
        // function-generation path. Normal function generation clears this when
        // restoring state, but codegen_free must also own the final cleanup.
        shared_env_clear(ctx);

        // Free tail-call label tracking if codegen is torn down mid-function.
        free(ctx->tail_call_label);

        // Free shadow variables tracking
        if (ctx->shadow_vars) {
            for (int i = 0; i < ctx->num_shadow_vars; i++) {
                free(ctx->shadow_vars[i]);
            }
            free(ctx->shadow_vars);
        }

        // Free const variables tracking
        if (ctx->const_vars) {
            for (int i = 0; i < ctx->num_const_vars; i++) {
                free(ctx->const_vars[i]);
            }
            free(ctx->const_vars);
        }

        // Free consumed temp tracking
        if (ctx->consumed_temps) {
            for (int i = 0; i < ctx->num_consumed_temps; i++) {
                free(ctx->consumed_temps[i]);
            }
            free(ctx->consumed_temps);
        }

        // Free try-finally tracking arrays
        if (ctx->finally_labels) {
            for (int i = 0; i < ctx->try_finally_depth; i++) {
                free(ctx->finally_labels[i]);
                free(ctx->return_value_vars[i]);
                free(ctx->has_return_vars[i]);
            }
            free(ctx->finally_labels);
            free(ctx->return_value_vars);
            free(ctx->has_return_vars);
        }

        // Free switch end labels stack
        if (ctx->switch_end_labels) {
            for (int i = 0; i < ctx->switch_depth; i++) {
                free(ctx->switch_end_labels[i]);
            }
            free(ctx->switch_end_labels);
        }

        // Free for-continue labels stack
        if (ctx->for_continue_labels) {
            for (int i = 0; i < ctx->for_continue_depth; i++) {
                free(ctx->for_continue_labels[i]);
            }
            free(ctx->for_continue_labels);
        }

        // Free loop body locals stack
        free(ctx->loop_body_locals);
        free(ctx->loop_body_try_depth);

        // Free loop label tracking stacks. Active entries may remain if codegen
        // aborts before the matching pop; entries below loop_label_depth still
        // own their strdup'd strings. Popped entries have already been freed.
        if (ctx->loop_labels) {
            for (int i = 0; i < ctx->loop_label_depth; i++) {
                free(ctx->loop_labels[i]);
                free(ctx->loop_break_labels[i]);
                free(ctx->loop_continue_labels[i]);
            }
            free(ctx->loop_labels);
            free(ctx->loop_break_labels);
            free(ctx->loop_continue_labels);
        }
        free(ctx->loop_label_body_locals);
        free(ctx->loop_label_try_depth);

        // Note: type_ctx is NOT freed here - it's owned by the caller (main.c)

        free(ctx);
    }
}

// ========== OUTPUT HELPERS ==========

void codegen_indent(CodegenContext *ctx) {
    for (int i = 0; i < ctx->indent; i++) {
        fprintf(ctx->output, "    ");
    }
}

void codegen_indent_inc(CodegenContext *ctx) {
    ctx->indent++;
}

void codegen_indent_dec(CodegenContext *ctx) {
    if (ctx->indent > 0) ctx->indent--;
}

void codegen_write(CodegenContext *ctx, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(ctx->output, fmt, args);
    va_end(args);
}

void codegen_writeln(CodegenContext *ctx, const char *fmt, ...) {
    codegen_indent(ctx);
    va_list args;
    va_start(args, fmt);
    vfprintf(ctx->output, fmt, args);
    va_end(args);
    fprintf(ctx->output, "\n");
}

void codegen_blank_line(CodegenContext *ctx) {
    fprintf(ctx->output, "\n");
}

void codegen_error(CodegenContext *ctx, int line, const char *fmt, ...) {
    ctx->error_count++;
    fprintf(stderr, "error");
    if (line > 0) {
        fprintf(stderr, " (line %d)", line);
    }
    fprintf(stderr, ": ");
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

void codegen_warning(CodegenContext *ctx, int line, const char *fmt, ...) {
    ctx->warning_count++;
    fprintf(stderr, "warning");
    if (line > 0) {
        fprintf(stderr, " (line %d)", line);
    }
    fprintf(stderr, ": ");
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

char* codegen_temp(CodegenContext *ctx) {
    char *name = malloc(32);
    if (!name) {
        codegen_error(ctx, 0, "Memory allocation failed for temp variable name");
        return strdup("_tmp_error");
    }
    snprintf(name, 32, "_tmp%d", ctx->temp_counter++);
    return name;
}

char* codegen_label(CodegenContext *ctx) {
    char *name = malloc(32);
    if (!name) {
        codegen_error(ctx, 0, "Memory allocation failed for label name");
        return strdup("_L_error");
    }
    snprintf(name, 32, "_L%d", ctx->label_counter++);
    return name;
}

char* codegen_anon_func(CodegenContext *ctx) {
    char *name = malloc(32);
    if (!name) {
        codegen_error(ctx, 0, "Memory allocation failed for anonymous function name");
        return strdup("hml_fn_anon_error");
    }
    snprintf(name, 32, "hml_fn_anon_%d", ctx->func_counter++);
    return name;
}

void codegen_add_local(CodegenContext *ctx, const char *name) {
    if (ctx->num_locals >= ctx->local_capacity) {
        int new_cap = safe_double_capacity(ctx->local_capacity, 16);
        if (new_cap < 0) {
            fprintf(stderr, "error: Local variable capacity overflow\n");
            exit(1);
        }
        char **new_vars = realloc(ctx->local_vars, (size_t)new_cap * sizeof(char*));
        int *new_cleanup = realloc(ctx->local_needs_cleanup, (size_t)new_cap * sizeof(int));
        if (!new_vars || !new_cleanup) {
            fprintf(stderr, "error: Failed to expand local variable storage\n");
            exit(1);
        }
        ctx->local_vars = new_vars;
        ctx->local_needs_cleanup = new_cleanup;
        ctx->local_capacity = new_cap;
    }
    // 1 = boxed HmlValue (needs hml_release), 0 = unboxed primitive (int32_t etc.)
    // Scope management is handled by restoring num_locals at block exit.
    ctx->local_needs_cleanup[ctx->num_locals] = 1;
    ctx->local_vars[ctx->num_locals++] = strdup(name);
}

int codegen_is_local(CodegenContext *ctx, const char *name) {
    for (int i = 0; i < ctx->num_locals; i++) {
        if (strcmp(ctx->local_vars[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

// Remove a local variable from scope (used for catch params that go out of scope)
// Search from the end so that the most recently added duplicate is removed first,
// preserving earlier entries (e.g., function parameters) when inlined functions
// add and then remove parameters with the same name.
void codegen_remove_local(CodegenContext *ctx, const char *name) {
    for (int i = ctx->num_locals - 1; i >= 0; i--) {
        if (strcmp(ctx->local_vars[i], name) == 0) {
            free(ctx->local_vars[i]);
            // Shift remaining elements down
            for (int j = i; j < ctx->num_locals - 1; j++) {
                ctx->local_vars[j] = ctx->local_vars[j + 1];
                ctx->local_needs_cleanup[j] = ctx->local_needs_cleanup[j + 1];
            }
            // Clear the last slot to prevent double-free when num_locals is restored
            ctx->local_vars[ctx->num_locals - 1] = NULL;
            ctx->local_needs_cleanup[ctx->num_locals - 1] = 0;
            ctx->num_locals--;
            return;
        }
    }
}

// Mark a local variable as not needing cleanup (e.g., unboxed to C primitive)
void codegen_mark_local_no_cleanup(CodegenContext *ctx, const char *name) {
    for (int i = ctx->num_locals - 1; i >= 0; i--) {
        if (ctx->local_vars[i] && strcmp(ctx->local_vars[i], name) == 0) {
            ctx->local_needs_cleanup[i] = 0;
            return;
        }
    }
}

// Emit hml_release() calls for body-local variables that need cleanup
void codegen_emit_local_cleanup(CodegenContext *ctx, const char *skip_var) {
    for (int i = ctx->locals_body_start; i < ctx->num_locals; i++) {
        if (!ctx->local_needs_cleanup[i]) continue;
        if (!ctx->local_vars[i]) continue;
        if (skip_var && strcmp(ctx->local_vars[i], skip_var) == 0) continue;
        char *safe = codegen_sanitize_ident(ctx->local_vars[i]);
        codegen_writeln(ctx, "hml_release(&%s);", safe);
        free(safe);
    }
    // Release the function's own reference to the shared closure environment.
    // Each closure that uses it has its own retained reference (via hml_closure_env_retain
    // in codegen_expr), so the env stays alive as long as any closure needs it.
    if (ctx->shared_env_name) {
        codegen_writeln(ctx, "hml_closure_env_release(%s);", ctx->shared_env_name);
    }
}

// Shadow variable tracking (locals that shadow main vars, like catch params)
void codegen_add_shadow(CodegenContext *ctx, const char *name) {
    if (ctx->num_shadow_vars >= ctx->shadow_vars_capacity) {
        int new_cap = (ctx->shadow_vars_capacity == 0) ? 8 : ctx->shadow_vars_capacity * 2;
        char **new_vars = realloc(ctx->shadow_vars, new_cap * sizeof(char*));
        if (!new_vars) {
            fprintf(stderr, "error: Failed to expand shadow variable storage\n");
            exit(1);
        }
        ctx->shadow_vars = new_vars;
        ctx->shadow_vars_capacity = new_cap;
    }
    ctx->shadow_vars[ctx->num_shadow_vars++] = strdup(name);
}

int codegen_is_shadow(CodegenContext *ctx, const char *name) {
    for (int i = 0; i < ctx->num_shadow_vars; i++) {
        if (strcmp(ctx->shadow_vars[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

void codegen_remove_shadow(CodegenContext *ctx, const char *name) {
    for (int i = 0; i < ctx->num_shadow_vars; i++) {
        if (strcmp(ctx->shadow_vars[i], name) == 0) {
            free(ctx->shadow_vars[i]);
            for (int j = i; j < ctx->num_shadow_vars - 1; j++) {
                ctx->shadow_vars[j] = ctx->shadow_vars[j + 1];
            }
            ctx->num_shadow_vars--;
            return;
        }
    }
}

// Const variable tracking (for preventing reassignment)
void codegen_add_const(CodegenContext *ctx, const char *name) {
    if (ctx->num_const_vars >= ctx->const_vars_capacity) {
        int new_cap = (ctx->const_vars_capacity == 0) ? 8 : ctx->const_vars_capacity * 2;
        char **new_vars = realloc(ctx->const_vars, new_cap * sizeof(char*));
        if (!new_vars) {
            fprintf(stderr, "error: Failed to expand const variable storage\n");
            exit(1);
        }
        ctx->const_vars = new_vars;
        ctx->const_vars_capacity = new_cap;
    }
    ctx->const_vars[ctx->num_const_vars++] = strdup(name);
}

int codegen_is_const(CodegenContext *ctx, const char *name) {
    for (int i = 0; i < ctx->num_const_vars; i++) {
        if (strcmp(ctx->const_vars[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

// Consumed temp tracking (avoid double-release when values are moved)
void codegen_mark_temp_consumed(CodegenContext *ctx, const char *name) {
    if (!name) {
        return;
    }
    for (int i = 0; i < ctx->num_consumed_temps; i++) {
        if (strcmp(ctx->consumed_temps[i], name) == 0) {
            return;
        }
    }
    if (ctx->num_consumed_temps >= ctx->consumed_temps_capacity) {
        int new_cap = safe_double_capacity(ctx->consumed_temps_capacity, 8);
        if (new_cap < 0) {
            fprintf(stderr, "error: Consumed temp capacity overflow\n");
            exit(1);
        }
        char **new_temps = realloc(ctx->consumed_temps, (size_t)new_cap * sizeof(char*));
        if (!new_temps) {
            fprintf(stderr, "error: Failed to expand consumed temp storage\n");
            exit(1);
        }
        ctx->consumed_temps = new_temps;
        ctx->consumed_temps_capacity = new_cap;
    }
    ctx->consumed_temps[ctx->num_consumed_temps++] = strdup(name);
}

int codegen_is_temp_consumed(CodegenContext *ctx, const char *name) {
    if (!name) {
        return 0;
    }
    for (int i = 0; i < ctx->num_consumed_temps; i++) {
        if (strcmp(ctx->consumed_temps[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

int codegen_consumed_checkpoint(CodegenContext *ctx) {
    return ctx->num_consumed_temps;
}

void codegen_restore_consumed(CodegenContext *ctx, int checkpoint) {
    if (checkpoint < 0 || checkpoint > ctx->num_consumed_temps) {
        return;
    }
    for (int i = checkpoint; i < ctx->num_consumed_temps; i++) {
        free(ctx->consumed_temps[i]);
    }
    ctx->num_consumed_temps = checkpoint;
}

// Try-finally context tracking (for return/break to jump to finally first)
void codegen_push_try_finally(CodegenContext *ctx, const char *finally_label,
                              const char *return_value_var, const char *has_return_var) {
    if (ctx->try_finally_depth >= ctx->try_finally_capacity) {
        int new_cap = (ctx->try_finally_capacity == 0) ? 4 : ctx->try_finally_capacity * 2;
        char **new_labels = realloc(ctx->finally_labels, new_cap * sizeof(char*));
        char **new_return_vars = realloc(ctx->return_value_vars, new_cap * sizeof(char*));
        char **new_has_vars = realloc(ctx->has_return_vars, new_cap * sizeof(char*));
        if (!new_labels || !new_return_vars || !new_has_vars) {
            // Update pointers for any successful allocations to avoid losing memory
            if (new_labels) ctx->finally_labels = new_labels;
            if (new_return_vars) ctx->return_value_vars = new_return_vars;
            if (new_has_vars) ctx->has_return_vars = new_has_vars;
            fprintf(stderr, "error: Failed to expand try-finally storage\n");
            exit(1);
        }
        ctx->finally_labels = new_labels;
        ctx->return_value_vars = new_return_vars;
        ctx->has_return_vars = new_has_vars;
        ctx->try_finally_capacity = new_cap;
    }
    char *dup_finally = strdup(finally_label);
    char *dup_return = strdup(return_value_var);
    char *dup_has = strdup(has_return_var);

    if (!dup_finally || !dup_return || !dup_has) {
        free(dup_finally);
        free(dup_return);
        free(dup_has);
        fprintf(stderr, "error: Failed to allocate try-finally strings\n");
        exit(1);
    }

    ctx->finally_labels[ctx->try_finally_depth] = dup_finally;
    ctx->return_value_vars[ctx->try_finally_depth] = dup_return;
    ctx->has_return_vars[ctx->try_finally_depth] = dup_has;
    ctx->try_finally_depth++;
}

void codegen_pop_try_finally(CodegenContext *ctx) {
    if (ctx->try_finally_depth > 0) {
        ctx->try_finally_depth--;
        free(ctx->finally_labels[ctx->try_finally_depth]);
        free(ctx->return_value_vars[ctx->try_finally_depth]);
        free(ctx->has_return_vars[ctx->try_finally_depth]);
    }
}

// Get the current (innermost) try-finally context
const char* codegen_get_finally_label(CodegenContext *ctx) {
    if (ctx->try_finally_depth > 0) {
        return ctx->finally_labels[ctx->try_finally_depth - 1];
    }
    return NULL;
}

const char* codegen_get_return_value_var(CodegenContext *ctx) {
    if (ctx->try_finally_depth > 0) {
        return ctx->return_value_vars[ctx->try_finally_depth - 1];
    }
    return NULL;
}

const char* codegen_get_has_return_var(CodegenContext *ctx) {
    if (ctx->try_finally_depth > 0) {
        return ctx->has_return_vars[ctx->try_finally_depth - 1];
    }
    return NULL;
}

// ========== SWITCH CONTEXT TRACKING ==========

void codegen_push_switch(CodegenContext *ctx, const char *end_label) {
    if (ctx->switch_depth >= ctx->switch_end_capacity) {
        int new_cap = (ctx->switch_end_capacity == 0) ? 4 : ctx->switch_end_capacity * 2;
        char **new_labels = realloc(ctx->switch_end_labels, new_cap * sizeof(char*));
        if (!new_labels) {
            fprintf(stderr, "error: Failed to expand switch label storage\n");
            exit(1);
        }
        ctx->switch_end_labels = new_labels;
        ctx->switch_end_capacity = new_cap;
    }
    ctx->switch_end_labels[ctx->switch_depth] = strdup(end_label);
    ctx->switch_depth++;
}

void codegen_pop_switch(CodegenContext *ctx) {
    if (ctx->switch_depth > 0) {
        ctx->switch_depth--;
        free(ctx->switch_end_labels[ctx->switch_depth]);
    }
}

const char* codegen_get_switch_end_label(CodegenContext *ctx) {
    if (ctx->switch_depth > 0) {
        return ctx->switch_end_labels[ctx->switch_depth - 1];
    }
    return NULL;
}

// ========== FOR-LOOP CONTINUE TRACKING ==========

void codegen_push_for_continue(CodegenContext *ctx, const char *continue_label) {
    if (ctx->for_continue_depth >= ctx->for_continue_capacity) {
        int new_cap = (ctx->for_continue_capacity == 0) ? 4 : ctx->for_continue_capacity * 2;
        char **new_labels = realloc(ctx->for_continue_labels, new_cap * sizeof(char*));
        if (!new_labels) {
            fprintf(stderr, "error: Failed to expand for-continue storage\n");
            exit(1);
        }
        ctx->for_continue_labels = new_labels;
        ctx->for_continue_capacity = new_cap;
    }
    ctx->for_continue_labels[ctx->for_continue_depth] = strdup(continue_label);
    ctx->for_continue_depth++;
}

void codegen_pop_for_continue(CodegenContext *ctx) {
    if (ctx->for_continue_depth > 0) {
        ctx->for_continue_depth--;
        free(ctx->for_continue_labels[ctx->for_continue_depth]);
    }
}

const char* codegen_get_for_continue_label(CodegenContext *ctx) {
    if (ctx->for_continue_depth > 0) {
        return ctx->for_continue_labels[ctx->for_continue_depth - 1];
    }
    return NULL;
}

// ========== LOOP LABEL TRACKING ==========

void codegen_push_loop_label(CodegenContext *ctx, const char *label, const char *break_label, const char *continue_label) {
    if (ctx->loop_label_depth >= ctx->loop_label_capacity) {
        int new_cap = (ctx->loop_label_capacity == 0) ? 4 : ctx->loop_label_capacity * 2;
        char **new_labels = realloc(ctx->loop_labels, new_cap * sizeof(char*));
        char **new_break = realloc(ctx->loop_break_labels, new_cap * sizeof(char*));
        char **new_continue = realloc(ctx->loop_continue_labels, new_cap * sizeof(char*));
        int *new_body_locals = realloc(ctx->loop_label_body_locals, new_cap * sizeof(int));
        int *new_try_depth = realloc(ctx->loop_label_try_depth, new_cap * sizeof(int));
        if (!new_labels || !new_break || !new_continue || !new_body_locals || !new_try_depth) {
            if (new_labels) ctx->loop_labels = new_labels;
            if (new_break) ctx->loop_break_labels = new_break;
            if (new_continue) ctx->loop_continue_labels = new_continue;
            if (new_body_locals) ctx->loop_label_body_locals = new_body_locals;
            if (new_try_depth) ctx->loop_label_try_depth = new_try_depth;
            fprintf(stderr, "error: Failed to expand loop label storage\n");
            exit(1);
        }
        ctx->loop_labels = new_labels;
        ctx->loop_break_labels = new_break;
        ctx->loop_continue_labels = new_continue;
        ctx->loop_label_body_locals = new_body_locals;
        ctx->loop_label_try_depth = new_try_depth;
        ctx->loop_label_capacity = new_cap;
    }
    ctx->loop_labels[ctx->loop_label_depth] = strdup(label);
    ctx->loop_break_labels[ctx->loop_label_depth] = strdup(break_label);
    ctx->loop_continue_labels[ctx->loop_label_depth] = strdup(continue_label);
    ctx->loop_label_body_locals[ctx->loop_label_depth] = ctx->num_locals;
    ctx->loop_label_try_depth[ctx->loop_label_depth] = ctx->try_body_depth;
    ctx->loop_label_depth++;
}

void codegen_pop_loop_label(CodegenContext *ctx) {
    if (ctx->loop_label_depth > 0) {
        ctx->loop_label_depth--;
        free(ctx->loop_labels[ctx->loop_label_depth]);
        free(ctx->loop_break_labels[ctx->loop_label_depth]);
        free(ctx->loop_continue_labels[ctx->loop_label_depth]);
    }
}

const char* codegen_get_labeled_break(CodegenContext *ctx, const char *label) {
    for (int i = ctx->loop_label_depth - 1; i >= 0; i--) {
        if (strcmp(ctx->loop_labels[i], label) == 0) {
            return ctx->loop_break_labels[i];
        }
    }
    return NULL;
}

const char* codegen_get_labeled_continue(CodegenContext *ctx, const char *label) {
    for (int i = ctx->loop_label_depth - 1; i >= 0; i--) {
        if (strcmp(ctx->loop_labels[i], label) == 0) {
            return ctx->loop_continue_labels[i];
        }
    }
    return NULL;
}

// ========== LOOP BODY LOCALS TRACKING ==========

void codegen_push_loop_body(CodegenContext *ctx) {
    if (ctx->loop_body_depth >= ctx->loop_body_capacity) {
        int new_cap = (ctx->loop_body_capacity == 0) ? 4 : ctx->loop_body_capacity * 2;
        int *new_stack = realloc(ctx->loop_body_locals, new_cap * sizeof(int));
        int *new_try_stack = realloc(ctx->loop_body_try_depth, new_cap * sizeof(int));
        if (!new_stack || !new_try_stack) {
            if (new_stack) ctx->loop_body_locals = new_stack;
            if (new_try_stack) ctx->loop_body_try_depth = new_try_stack;
            fprintf(stderr, "error: Failed to expand loop body locals storage\n");
            exit(1);
        }
        ctx->loop_body_locals = new_stack;
        ctx->loop_body_try_depth = new_try_stack;
        ctx->loop_body_capacity = new_cap;
    }
    ctx->loop_body_locals[ctx->loop_body_depth] = ctx->num_locals;
    ctx->loop_body_try_depth[ctx->loop_body_depth] = ctx->try_body_depth;
    ctx->loop_body_depth++;
}

void codegen_pop_loop_body(CodegenContext *ctx) {
    if (ctx->loop_body_depth > 0) {
        ctx->loop_body_depth--;
    }
}

// Emit hml_release for locals from current num_locals down to 'start'
static void codegen_emit_cleanup_range(CodegenContext *ctx, int start) {
    for (int i = start; i < ctx->num_locals; i++) {
        if (!ctx->local_needs_cleanup[i]) continue;
        if (!ctx->local_vars[i]) continue;
        char *safe = codegen_sanitize_ident(ctx->local_vars[i]);
        codegen_writeln(ctx, "hml_release(&%s);", safe);
        free(safe);
    }
}

void codegen_emit_break_cleanup(CodegenContext *ctx) {
    if (ctx->loop_body_depth <= 0) return;
    int start = ctx->loop_body_locals[ctx->loop_body_depth - 1];
    codegen_emit_cleanup_range(ctx, start);
}

void codegen_emit_labeled_break_cleanup(CodegenContext *ctx, const char *label) {
    for (int i = ctx->loop_label_depth - 1; i >= 0; i--) {
        if (strcmp(ctx->loop_labels[i], label) == 0) {
            codegen_emit_cleanup_range(ctx, ctx->loop_label_body_locals[i]);
            return;
        }
    }
}

// ========== TRY-BODY EXCEPTION CONTEXT POPS ==========

void codegen_emit_exception_pops(CodegenContext *ctx, int pops) {
    for (int i = 0; i < pops; i++) {
        codegen_writeln(ctx, "hml_exception_pop();");
    }
}

void codegen_emit_return_try_pops(CodegenContext *ctx) {
    codegen_emit_exception_pops(ctx, ctx->try_body_depth);
}

void codegen_emit_break_try_pops(CodegenContext *ctx) {
    if (ctx->loop_body_depth <= 0) return;
    int entry_depth = ctx->loop_body_try_depth[ctx->loop_body_depth - 1];
    int pops = ctx->try_body_depth - entry_depth;
    if (pops > 0) codegen_emit_exception_pops(ctx, pops);
}

void codegen_emit_labeled_break_try_pops(CodegenContext *ctx, const char *label) {
    for (int i = ctx->loop_label_depth - 1; i >= 0; i--) {
        if (strcmp(ctx->loop_labels[i], label) == 0) {
            int pops = ctx->try_body_depth - ctx->loop_label_try_depth[i];
            if (pops > 0) codegen_emit_exception_pops(ctx, pops);
            return;
        }
    }
}

// Forward declaration
int codegen_is_main_var(CodegenContext *ctx, const char *name);

// Main file variable tracking (to add prefix and avoid C name conflicts)
void codegen_add_main_var(CodegenContext *ctx, const char *name) {
    // Check for duplicates first to avoid GCC redefinition errors
    if (codegen_is_main_var(ctx, name)) {
        return;  // Already added, skip
    }
    if (ctx->num_main_vars >= ctx->main_vars_capacity) {
        int new_cap = (ctx->main_vars_capacity == 0) ? 16 : ctx->main_vars_capacity * 2;
        char **new_vars = realloc(ctx->main_vars, new_cap * sizeof(char*));
        if (!new_vars) {
            fprintf(stderr, "error: Failed to expand main variable storage\n");
            exit(1);
        }
        ctx->main_vars = new_vars;
        ctx->main_vars_capacity = new_cap;
    }
    ctx->main_vars[ctx->num_main_vars++] = strdup(name);
}

int codegen_is_main_var(CodegenContext *ctx, const char *name) {
    for (int i = 0; i < ctx->num_main_vars; i++) {
        if (strcmp(ctx->main_vars[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

// Helper to count AST nodes for inlining decision
static int count_stmt_nodes(Stmt *stmt);
static int count_expr_nodes(Expr *expr) {
    if (!expr) return 0;
    int count = 1;  // This node
    switch (expr->type) {
        case EXPR_BINARY:
            count += count_expr_nodes(expr->as.binary.left);
            count += count_expr_nodes(expr->as.binary.right);
            break;
        case EXPR_UNARY:
            count += count_expr_nodes(expr->as.unary.operand);
            break;
        case EXPR_CALL:
            count += count_expr_nodes(expr->as.call.func);
            for (int i = 0; i < expr->as.call.num_args; i++) {
                count += count_expr_nodes(expr->as.call.args[i]);
            }
            break;
        case EXPR_TERNARY:
            count += count_expr_nodes(expr->as.ternary.condition);
            count += count_expr_nodes(expr->as.ternary.true_expr);
            count += count_expr_nodes(expr->as.ternary.false_expr);
            break;
        case EXPR_FUNCTION:
            count += count_stmt_nodes(expr->as.function.body);
            break;
        default:
            break;
    }
    return count;
}

static int count_stmt_nodes(Stmt *stmt) {
    if (!stmt) return 0;
    int count = 1;
    switch (stmt->type) {
        case STMT_EXPR:
            count += count_expr_nodes(stmt->as.expr);
            break;
        case STMT_LET:
        case STMT_CONST:
            count += count_expr_nodes(stmt->as.let.value);
            break;
        case STMT_IF:
            count += count_expr_nodes(stmt->as.if_stmt.condition);
            count += count_stmt_nodes(stmt->as.if_stmt.then_branch);
            count += count_stmt_nodes(stmt->as.if_stmt.else_branch);
            break;
        case STMT_WHILE:
            count += count_expr_nodes(stmt->as.while_stmt.condition);
            count += count_stmt_nodes(stmt->as.while_stmt.body);
            break;
        case STMT_LOOP:
            count += count_stmt_nodes(stmt->as.loop_stmt.body);
            break;
        case STMT_FOR:
            count += count_stmt_nodes(stmt->as.for_loop.initializer);
            count += count_expr_nodes(stmt->as.for_loop.condition);
            count += count_expr_nodes(stmt->as.for_loop.increment);
            count += count_stmt_nodes(stmt->as.for_loop.body);
            break;
        case STMT_RETURN:
            count += count_expr_nodes(stmt->as.return_stmt.value);
            break;
        case STMT_BLOCK:
            for (int i = 0; i < stmt->as.block.count; i++) {
                count += count_stmt_nodes(stmt->as.block.statements[i]);
            }
            break;
        default:
            break;
    }
    return count;
}

// Check if a function is suitable for inlining
// Criteria: simple body (just a return statement), small expression, no recursion
static int is_function_inlinable(Expr *func) {
    if (!func || func->type != EXPR_FUNCTION) return 0;
    // No async functions
    if (func->as.function.is_async) return 0;
    // No rest params (complex to inline)
    if (func->as.function.rest_param) return 0;
    // No ref params (complex to inline)
    if (func->as.function.param_is_ref) {
        for (int i = 0; i < func->as.function.num_params; i++) {
            if (func->as.function.param_is_ref[i]) return 0;
        }
    }

    Stmt *body = func->as.function.body;
    if (!body) return 0;

    // Must be a block with exactly one return statement
    if (body->type != STMT_BLOCK) return 0;
    if (body->as.block.count != 1) return 0;

    Stmt *only_stmt = body->as.block.statements[0];
    if (!only_stmt || only_stmt->type != STMT_RETURN) return 0;

    // Check expression size (max 30 nodes for inlining)
    int node_count = count_expr_nodes(only_stmt->as.return_stmt.value);
    if (node_count > 30) return 0;

    return 1;
}

// Main file function definitions (subset of main_vars that are actual function defs)
void codegen_add_main_func(CodegenContext *ctx, const char *name, int num_params, int has_rest, int *param_is_ref, Expr *func_ast) {
    if (ctx->num_main_funcs >= ctx->main_funcs_capacity) {
        int new_cap = (ctx->main_funcs_capacity == 0) ? 16 : ctx->main_funcs_capacity * 2;
        char **new_funcs = realloc(ctx->main_funcs, new_cap * sizeof(char*));
        int *new_params = realloc(ctx->main_func_params, new_cap * sizeof(int));
        int *new_has_rest = realloc(ctx->main_func_has_rest, new_cap * sizeof(int));
        int **new_param_is_ref = realloc(ctx->main_func_param_is_ref, new_cap * sizeof(int*));
        Expr **new_ast = realloc(ctx->main_func_ast, new_cap * sizeof(Expr*));
        int *new_inlinable = realloc(ctx->main_func_inlinable, new_cap * sizeof(int));
        if (!new_funcs || !new_params || !new_has_rest || !new_param_is_ref || !new_ast || !new_inlinable) {
            // Update any successful allocations to avoid losing pointers
            if (new_funcs) ctx->main_funcs = new_funcs;
            if (new_params) ctx->main_func_params = new_params;
            if (new_has_rest) ctx->main_func_has_rest = new_has_rest;
            if (new_param_is_ref) ctx->main_func_param_is_ref = new_param_is_ref;
            if (new_ast) ctx->main_func_ast = new_ast;
            if (new_inlinable) ctx->main_func_inlinable = new_inlinable;
            fprintf(stderr, "error: Failed to expand main function storage\n");
            exit(1);
        }
        ctx->main_funcs = new_funcs;
        ctx->main_func_params = new_params;
        ctx->main_func_has_rest = new_has_rest;
        ctx->main_func_param_is_ref = new_param_is_ref;
        ctx->main_func_ast = new_ast;
        ctx->main_func_inlinable = new_inlinable;
        ctx->main_funcs_capacity = new_cap;
    }
    ctx->main_funcs[ctx->num_main_funcs] = strdup(name);
    ctx->main_func_params[ctx->num_main_funcs] = num_params;
    ctx->main_func_has_rest[ctx->num_main_funcs] = has_rest;
    // Copy param_is_ref array
    if (param_is_ref && num_params > 0) {
        ctx->main_func_param_is_ref[ctx->num_main_funcs] = malloc(num_params * sizeof(int));
        if (ctx->main_func_param_is_ref[ctx->num_main_funcs]) {
            for (int i = 0; i < num_params; i++) {
                ctx->main_func_param_is_ref[ctx->num_main_funcs][i] = param_is_ref[i];
            }
        }
    } else {
        ctx->main_func_param_is_ref[ctx->num_main_funcs] = NULL;
    }
    // Store AST and compute inlinability
    ctx->main_func_ast[ctx->num_main_funcs] = func_ast;
    ctx->main_func_inlinable[ctx->num_main_funcs] = is_function_inlinable(func_ast);
    ctx->num_main_funcs++;
}

int codegen_is_main_func(CodegenContext *ctx, const char *name) {
    for (int i = 0; i < ctx->num_main_funcs; i++) {
        if (strcmp(ctx->main_funcs[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

// Format an f64 constant as a valid C expression. %.17g alone produces
// "inf"/"nan" (not valid C) for non-finite values, and "-0" (an int constant
// that loses the sign when converted to double) for negative zero.
void codegen_format_f64(char *buf, size_t size, double value) {
    if (isnan(value)) {
        snprintf(buf, size, signbit(value) ? "(-NAN)" : "NAN");
    } else if (isinf(value)) {
        snprintf(buf, size, signbit(value) ? "(-INFINITY)" : "INFINITY");
    } else if (value == 0.0 && signbit(value)) {
        snprintf(buf, size, "-0.0");
    } else {
        snprintf(buf, size, "%.17g", value);
    }
}

void codegen_add_extern_fn(CodegenContext *ctx, const char *name) {
    if (codegen_is_extern_fn(ctx, name)) {
        return;
    }
    if (ctx->num_extern_fns >= ctx->extern_fns_capacity) {
        int new_cap = (ctx->extern_fns_capacity == 0) ? 16 : ctx->extern_fns_capacity * 2;
        char **new_fns = realloc(ctx->extern_fns, new_cap * sizeof(char*));
        if (!new_fns) {
            fprintf(stderr, "error: Failed to expand extern function storage\n");
            exit(1);
        }
        ctx->extern_fns = new_fns;
        ctx->extern_fns_capacity = new_cap;
    }
    ctx->extern_fns[ctx->num_extern_fns++] = strdup(name);
}

int codegen_is_extern_fn(CodegenContext *ctx, const char *name) {
    for (int i = 0; i < ctx->num_extern_fns; i++) {
        if (strcmp(ctx->extern_fns[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

int codegen_get_main_func_params(CodegenContext *ctx, const char *name) {
    for (int i = 0; i < ctx->num_main_funcs; i++) {
        if (strcmp(ctx->main_funcs[i], name) == 0) {
            return ctx->main_func_params[i];
        }
    }
    return -1;  // Not found
}

int codegen_get_main_func_has_rest(CodegenContext *ctx, const char *name) {
    for (int i = 0; i < ctx->num_main_funcs; i++) {
        if (strcmp(ctx->main_funcs[i], name) == 0) {
            return ctx->main_func_has_rest[i];
        }
    }
    return 0;  // Not found, assume no rest param
}

int* codegen_get_main_func_param_is_ref(CodegenContext *ctx, const char *name) {
    for (int i = 0; i < ctx->num_main_funcs; i++) {
        if (strcmp(ctx->main_funcs[i], name) == 0) {
            return ctx->main_func_param_is_ref[i];
        }
    }
    return NULL;  // Not found
}

Expr* codegen_get_main_func_ast(CodegenContext *ctx, const char *name) {
    for (int i = 0; i < ctx->num_main_funcs; i++) {
        if (strcmp(ctx->main_funcs[i], name) == 0) {
            return ctx->main_func_ast[i];
        }
    }
    return NULL;  // Not found
}

int codegen_is_main_func_inlinable(CodegenContext *ctx, const char *name) {
    for (int i = 0; i < ctx->num_main_funcs; i++) {
        if (strcmp(ctx->main_funcs[i], name) == 0) {
            return ctx->main_func_inlinable[i];
        }
    }
    return 0;  // Not found, not inlinable
}

// Main file import tracking (for function call resolution)
void codegen_add_main_import(CodegenContext *ctx, const char *local_name, const char *original_name, const char *module_prefix, int is_function, int num_params, int is_extern) {
    if (ctx->num_main_imports >= ctx->main_imports_capacity) {
        int new_cap = (ctx->main_imports_capacity == 0) ? 16 : ctx->main_imports_capacity * 2;
        ImportBinding *new_imports = realloc(ctx->main_imports, new_cap * sizeof(ImportBinding));
        if (!new_imports) {
            fprintf(stderr, "error: Failed to expand main import storage\n");
            exit(1);
        }
        ctx->main_imports = new_imports;
        ctx->main_imports_capacity = new_cap;
    }
    ImportBinding *binding = &ctx->main_imports[ctx->num_main_imports++];
    binding->local_name = strdup(local_name);
    binding->original_name = strdup(original_name);
    binding->module_prefix = strdup(module_prefix);
    binding->is_function = is_function;
    binding->num_params = num_params;
    binding->is_extern = is_extern;
}

ImportBinding* codegen_find_main_import(CodegenContext *ctx, const char *name) {
    for (int i = 0; i < ctx->num_main_imports; i++) {
        if (strcmp(ctx->main_imports[i].local_name, name) == 0) {
            return &ctx->main_imports[i];
        }
    }
    return NULL;
}

// ========== C KEYWORD HANDLING ==========

// List of C reserved keywords that need to be escaped if used as identifiers
static const char *c_keywords[] = {
    // C89/90 keywords
    "auto", "break", "case", "char", "const", "continue", "default", "do",
    "double", "else", "enum", "extern", "float", "for", "goto", "if",
    "int", "long", "register", "return", "short", "signed", "sizeof",
    "static", "struct", "switch", "typedef", "union", "unsigned", "void",
    "volatile", "while",
    // C99 keywords
    "inline", "restrict", "_Bool", "_Complex", "_Imaginary",
    // C11 keywords
    "_Alignas", "_Alignof", "_Atomic", "_Generic", "_Noreturn",
    "_Static_assert", "_Thread_local",
    // C23 keywords
    "true", "false", "nullptr", "constexpr", "static_assert", "thread_local",
    "alignas", "alignof", "bool",
    // Common identifiers that could conflict with C stdlib/runtime
    "main", "NULL",
    // C library macros and identifiers (lowercase/mixed-case, not caught by ALL_CAPS heuristic)
    "stdin", "stdout", "stderr", "errno",
    NULL  // Sentinel
};

// Check if a name is a C keyword
static int is_c_keyword(const char *name) {
    if (!name) return 0;
    for (int i = 0; c_keywords[i] != NULL; i++) {
        if (strcmp(name, c_keywords[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

// Check if a name matches the ALL_CAPS pattern typical of C preprocessor macros.
// Pattern: starts with uppercase letter, contains only [A-Z0-9_].
// This catches POSIX constants (SIGINT, AF_INET, SOCK_STREAM, POLLIN, O_RDONLY,
// CLOCK_MONOTONIC, PROT_READ, MAP_SHARED, MSG_DONTWAIT, TCP_NODELAY, etc.)
// without needing a hand-maintained list of specific prefixes.
// False positives (e.g., user variable "MAX") are harmless — the _v_ prefix
// is transparent and doesn't affect behavior.
static int is_potential_c_macro(const char *name) {
    if (!name || !name[0]) return 0;
    if (name[0] < 'A' || name[0] > 'Z') return 0;
    for (int i = 1; name[i]; i++) {
        char c = name[i];
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') continue;
        return 0;  // Contains lowercase or special char — not a macro pattern
    }
    return 1;
}

// Check if a name conflicts with Hemlock runtime or codegen internal prefixes
static int conflicts_with_runtime(const char *name) {
    if (!name || strlen(name) == 0) return 0;
    // Check for Hemlock runtime function prefix
    if (strncmp(name, "hml_", 4) == 0) return 1;
    // Check for Hemlock runtime type prefixes
    if (strncmp(name, "Hml", 3) == 0) return 1;
    if (strncmp(name, "HML_", 4) == 0) return 1;
    // Check for codegen internal prefixes
    if (strncmp(name, "_tmp", 4) == 0) return 1;
    if (strncmp(name, "_main_", 6) == 0) return 1;
    if (strncmp(name, "_mod", 4) == 0) return 1;
    if (strncmp(name, "_L", 2) == 0 && strlen(name) > 2 && name[2] >= '0' && name[2] <= '9') return 1;
    if (strncmp(name, "_env_", 5) == 0) return 1;
    if (strncmp(name, "_shared_env_", 12) == 0) return 1;
    if (strncmp(name, "_v_", 3) == 0) return 1;  // Our own sanitization prefix
    if (strncmp(name, "_ex_", 4) == 0) return 1;  // Exception context
    if (strncmp(name, "_closure_env", 12) == 0) return 1;  // Closure env param
    // Check for C preprocessor macro conflicts using ALL_CAPS heuristic.
    // This replaces the previous hand-maintained prefix list (SIG*, AF_*, SOCK_*, etc.)
    // and automatically covers any POSIX/system macro without manual updates.
    if (is_potential_c_macro(name)) return 1;
    return 0;
}

// Sanitize an identifier to avoid C keyword and runtime conflicts
// Returns a newly allocated string (caller must free)
// If the name conflicts, returns "_v_<name>", otherwise returns a copy of the name
char* codegen_sanitize_ident(const char *name) {
    if (!name) return strdup("");

    if (is_c_keyword(name) || conflicts_with_runtime(name)) {
        // Add "_v_" prefix to escape the conflict
        size_t len = strlen(name) + 4;  // "_v_" + name + null
        char *escaped = malloc(len);
        if (!escaped) {
            fprintf(stderr, "error: Memory allocation failed for sanitized identifier\n");
            return strdup(name);  // Fallback to unsanitized
        }
        snprintf(escaped, len, "_v_%s", name);
        return escaped;
    }

    return strdup(name);
}

// ========== STRING HELPERS ==========

char* codegen_escape_string(const char *str) {
    if (!str) return strdup("");

    int len = strlen(str);
    // Worst case: every char needs escaping (2x) + quotes + null
    char *escaped = malloc(len * 2 + 3);
    if (!escaped) {
        fprintf(stderr, "error: Memory allocation failed for escaped string\n");
        return strdup("");
    }
    char *p = escaped;

    while (*str) {
        switch (*str) {
            case '\n': *p++ = '\\'; *p++ = 'n'; break;
            case '\r': *p++ = '\\'; *p++ = 'r'; break;
            case '\t': *p++ = '\\'; *p++ = 't'; break;
            case '\\': *p++ = '\\'; *p++ = '\\'; break;
            case '"':  *p++ = '\\'; *p++ = '"'; break;
            default:   *p++ = *str; break;
        }
        str++;
    }
    *p = '\0';
    return escaped;
}

const char* codegen_binary_op_str(BinaryOp op) {
    switch (op) {
        case OP_ADD:           return "+";
        case OP_SUB:           return "-";
        case OP_MUL:           return "*";
        case OP_DIV:           return "/";
        case OP_MOD:           return "%";
        case OP_EQUAL:         return "==";
        case OP_NOT_EQUAL:     return "!=";
        case OP_LESS:          return "<";
        case OP_LESS_EQUAL:    return "<=";
        case OP_GREATER:       return ">";
        case OP_GREATER_EQUAL: return ">=";
        case OP_AND:           return "&&";
        case OP_OR:            return "||";
        case OP_BIT_AND:       return "&";
        case OP_BIT_OR:        return "|";
        case OP_BIT_XOR:       return "^";
        case OP_BIT_LSHIFT:    return "<<";
        case OP_BIT_RSHIFT:    return ">>";
        default:               return "?";
    }
}

const char* codegen_hml_binary_op(BinaryOp op) {
    switch (op) {
        case OP_ADD:           return "HML_OP_ADD";
        case OP_SUB:           return "HML_OP_SUB";
        case OP_MUL:           return "HML_OP_MUL";
        case OP_DIV:           return "HML_OP_DIV";
        case OP_MOD:           return "HML_OP_MOD";
        case OP_EQUAL:         return "HML_OP_EQUAL";
        case OP_NOT_EQUAL:     return "HML_OP_NOT_EQUAL";
        case OP_LESS:          return "HML_OP_LESS";
        case OP_LESS_EQUAL:    return "HML_OP_LESS_EQUAL";
        case OP_GREATER:       return "HML_OP_GREATER";
        case OP_GREATER_EQUAL: return "HML_OP_GREATER_EQUAL";
        case OP_AND:           return "HML_OP_AND";
        case OP_OR:            return "HML_OP_OR";
        case OP_BIT_AND:       return "HML_OP_BIT_AND";
        case OP_BIT_OR:        return "HML_OP_BIT_OR";
        case OP_BIT_XOR:       return "HML_OP_BIT_XOR";
        case OP_BIT_LSHIFT:    return "HML_OP_LSHIFT";
        case OP_BIT_RSHIFT:    return "HML_OP_RSHIFT";
        default:               return "HML_OP_ADD";
    }
}

const char* codegen_hml_unary_op(UnaryOp op) {
    switch (op) {
        case UNARY_NOT:     return "HML_UNARY_NOT";
        case UNARY_NEGATE:  return "HML_UNARY_NEGATE";
        case UNARY_BIT_NOT: return "HML_UNARY_BIT_NOT";
        default:            return "HML_UNARY_NOT";
    }
}

// ========== SCOPE MANAGEMENT ==========

Scope* scope_new(Scope *parent) {
    Scope *s = malloc(sizeof(Scope));
    if (!s) {
        fprintf(stderr, "error: Memory allocation failed for scope\n");
        return NULL;
    }
    s->vars = NULL;
    s->num_vars = 0;
    s->capacity = 0;
    s->parent = parent;
    return s;
}

void scope_free(Scope *scope) {
    if (scope) {
        for (int i = 0; i < scope->num_vars; i++) {
            free(scope->vars[i]);
        }
        free(scope->vars);
        free(scope);
    }
}

void scope_add_var(Scope *scope, const char *name) {
    if (!scope || !name) return;

    // Check if already exists
    for (int i = 0; i < scope->num_vars; i++) {
        if (strcmp(scope->vars[i], name) == 0) return;
    }

    // Expand if needed
    if (scope->num_vars >= scope->capacity) {
        int new_cap = (scope->capacity == 0) ? 8 : scope->capacity * 2;
        char **new_vars = realloc(scope->vars, new_cap * sizeof(char*));
        if (!new_vars) {
            fprintf(stderr, "error: Failed to expand scope variable storage\n");
            exit(1);
        }
        scope->vars = new_vars;
        scope->capacity = new_cap;
    }

    scope->vars[scope->num_vars++] = strdup(name);
}

int scope_has_var(Scope *scope, const char *name) {
    if (!scope || !name) return 0;
    for (int i = 0; i < scope->num_vars; i++) {
        if (strcmp(scope->vars[i], name) == 0) return 1;
    }
    return 0;
}

int scope_is_defined(Scope *scope, const char *name) {
    while (scope) {
        if (scope_has_var(scope, name)) return 1;
        scope = scope->parent;
    }
    return 0;
}

void codegen_push_scope(CodegenContext *ctx) {
    ctx->current_scope = scope_new(ctx->current_scope);
}

void codegen_pop_scope(CodegenContext *ctx) {
    if (ctx->current_scope) {
        Scope *old = ctx->current_scope;
        ctx->current_scope = old->parent;
        scope_free(old);
    }
}

// ========== DEFER SUPPORT ==========

void codegen_defer_push(CodegenContext *ctx, Expr *expr) {
    DeferEntry *entry = malloc(sizeof(DeferEntry));
    if (!entry) {
        codegen_error(ctx, 0, "Memory allocation failed for defer entry");
        return;
    }
    entry->expr = expr;
    entry->next = ctx->defer_stack;
    entry->scope_depth = 0;  // Not currently used
    ctx->defer_stack = entry;
}

void codegen_defer_execute_all(CodegenContext *ctx) {
    // Generate code for all current defers in LIFO order
    // Note: We iterate without consuming so multiple returns can use the same defers
    DeferEntry *entry = ctx->defer_stack;
    while (entry) {
        // Generate code to execute the deferred expression
        codegen_writeln(ctx, "// Deferred call");
        char *value = codegen_expr(ctx, entry->expr);
        codegen_writeln(ctx, "hml_release(&%s);", value);
        free(value);

        entry = entry->next;
    }
}

void codegen_defer_clear(CodegenContext *ctx) {
    while (ctx->defer_stack) {
        DeferEntry *entry = ctx->defer_stack;
        ctx->defer_stack = entry->next;
        free(entry);
    }
}

// ========== CAPTURED VARIABLE SYNC ==========

/*
 * Variables captured by closures (or defers) live in the function's shared
 * environment; closures read and write that environment. The enclosing
 * function keeps a C local as well, so every write to a captured local must
 * be mirrored into the shared environment (and reads must come from it - see
 * codegen_expr_ident) or the two copies diverge:
 *   let y = 1; let f = fn() { return y; }; y = 99; f()  must return 99.
 */
// A captured variable participates in shared-env reads/writes only when its
// C storage is a bare function-local (not a _main_ global, module symbol, or
// ref param pointer).
static int captured_var_is_bare_local(CodegenContext *ctx, const char *hml_name) {
    if (codegen_is_ref_param(ctx, hml_name)) return 0;
    if (codegen_is_shadow(ctx, hml_name)) return 1;
    if (ctx->current_scope && scope_is_defined(ctx->current_scope, hml_name)) return 1;
    return codegen_is_local(ctx, hml_name) && !codegen_is_main_var(ctx, hml_name);
}

void codegen_sync_captured_var(CodegenContext *ctx, const char *hml_name, const char *c_name) {
    if (!ctx->shared_env_name || !ctx->in_function) return;
    if (!captured_var_is_bare_local(ctx, hml_name)) return;
    int idx = shared_env_get_index(ctx, hml_name);
    if (idx < 0) return;
    codegen_writeln(ctx, "hml_closure_env_set(%s, %d, %s);", ctx->shared_env_name, idx, c_name);
}

// Returns the shared-env index if reads of this variable should go through
// the shared environment (captured local in the enclosing function), else -1.
int codegen_captured_var_env_index(CodegenContext *ctx, const char *hml_name) {
    if (!ctx->shared_env_name || !ctx->in_function) return -1;
    if (!captured_var_is_bare_local(ctx, hml_name)) return -1;
    return shared_env_get_index(ctx, hml_name);
}

// ========== DEFER FRAME SUPPORT ==========

// Pre-scan a statement tree for defer statements. Does NOT descend into
// nested function expressions - their defers belong to that function's frame.
// (defer can only appear as a statement, so scanning statements suffices.)
int codegen_body_has_defer(Stmt *stmt) {
    if (!stmt) return 0;
    switch (stmt->type) {
        case STMT_DEFER:
            return 1;
        case STMT_BLOCK:
            for (int i = 0; i < stmt->as.block.count; i++) {
                if (codegen_body_has_defer(stmt->as.block.statements[i])) return 1;
            }
            return 0;
        case STMT_IF:
            return codegen_body_has_defer(stmt->as.if_stmt.then_branch) ||
                   codegen_body_has_defer(stmt->as.if_stmt.else_branch);
        case STMT_WHILE:
            return codegen_body_has_defer(stmt->as.while_stmt.body);
        case STMT_LOOP:
            return codegen_body_has_defer(stmt->as.loop_stmt.body);
        case STMT_FOR:
            return codegen_body_has_defer(stmt->as.for_loop.initializer) ||
                   codegen_body_has_defer(stmt->as.for_loop.body);
        case STMT_FOR_IN:
            return codegen_body_has_defer(stmt->as.for_in.body);
        case STMT_TRY:
            return codegen_body_has_defer(stmt->as.try_stmt.try_block) ||
                   codegen_body_has_defer(stmt->as.try_stmt.catch_block) ||
                   codegen_body_has_defer(stmt->as.try_stmt.finally_block);
        case STMT_SWITCH:
            for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
                if (codegen_body_has_defer(stmt->as.switch_stmt.case_bodies[i])) return 1;
            }
            return 0;
        case STMT_EXPORT:
            if (stmt->as.export_stmt.is_declaration) {
                return codegen_body_has_defer(stmt->as.export_stmt.declaration);
            }
            return 0;
        default:
            return 0;
    }
}

/*
 * Emit the per-function defer frame prologue. The frame mark scopes defer
 * execution to this function (a callee's return must not run our defers).
 * The exception context makes this function's defers run when an exception
 * unwinds past it - before the catch handler - matching the interpreter.
 * Defers registered before a try/catch in the same function are NOT run by
 * the catch (they stay pending until function exit), which falls out
 * naturally: inner try contexts are pushed above this one.
 */
void codegen_emit_defer_prologue(CodegenContext *ctx, Stmt *body) {
    if (!codegen_body_has_defer(body)) return;
    ctx->has_defers = 1;
    ctx->defer_unwind_active = 1;
    codegen_writeln(ctx, "void *_defer_frame = hml_defer_frame_begin();");
    codegen_writeln(ctx, "{");
    codegen_indent_inc(ctx);
    codegen_writeln(ctx, "HmlExceptionContext *_defer_unwind = hml_exception_push();");
    codegen_writeln(ctx, "if (setjmp(_defer_unwind->exception_buf) != 0) {");
    codegen_indent_inc(ctx);
    codegen_writeln(ctx, "HmlValue _defer_ex = hml_exception_get_value();");
    codegen_writeln(ctx, "hml_exception_pop();");
    codegen_writeln(ctx, "hml_defer_execute_frame(_defer_frame);");
    codegen_writeln(ctx, "hml_throw(_defer_ex);");
    codegen_indent_dec(ctx);
    codegen_writeln(ctx, "}");
    codegen_indent_dec(ctx);
    codegen_writeln(ctx, "}");
}

// Emit the defer cleanup for an actual function exit (return or implicit
// end-of-function): pop the unwind context pushed by the prologue, then run
// this frame's pending defers.
void codegen_emit_defer_exit(CodegenContext *ctx) {
    if (!ctx->defer_unwind_active) return;
    codegen_writeln(ctx, "hml_exception_pop();  // defer unwind context");
    codegen_writeln(ctx, "hml_defer_execute_frame(_defer_frame);");
}

// ========== FUNCTION GENERATION STATE ==========

void funcgen_save_state(CodegenContext *ctx, FuncGenState *state) {
    state->num_locals = ctx->num_locals;
    state->locals_body_start = ctx->locals_body_start;
    state->defer_stack = ctx->defer_stack;
    state->in_function = ctx->in_function;
    state->has_defers = ctx->has_defers;
    state->defer_unwind_active = ctx->defer_unwind_active;
    state->module = ctx->current_module;
    state->closure = ctx->current_closure;
    state->scope = ctx->current_scope;
    state->tail_call_func_name = ctx->tail_call_func_name;
    state->tail_call_label = ctx->tail_call_label;
    state->tail_call_func_expr = ctx->tail_call_func_expr;
    state->loop_body_depth = ctx->loop_body_depth;
    state->loop_depth = ctx->loop_depth;
    state->try_body_depth = ctx->try_body_depth;
    state->func_params = ctx->func_params;
    state->num_func_params = ctx->num_func_params;
    state->func_param_is_ref = ctx->func_param_is_ref;

    // Initialize for new function
    ctx->defer_stack = NULL;
    ctx->in_function = 1;
    ctx->has_defers = 0;
    ctx->defer_unwind_active = 0;
    ctx->current_scope = NULL;  // Fresh scope for new function
    ctx->last_closure_env_id = -1;
    ctx->tail_call_func_name = NULL;
    ctx->tail_call_label = NULL;
    ctx->tail_call_func_expr = NULL;
    ctx->loop_body_depth = 0;
    ctx->loop_depth = 0;
    ctx->try_body_depth = 0;
    // funcgen_add_params() sets these for the new function; clear
    // here so an early return / a param-less function can't observe
    // the previous function's params.
    ctx->func_params = NULL;
    ctx->num_func_params = 0;
    ctx->func_param_is_ref = NULL;
}

void funcgen_restore_state(CodegenContext *ctx, FuncGenState *state) {
    codegen_defer_clear(ctx);
    ctx->defer_stack = state->defer_stack;

    // Free any locals added during the nested scope before restoring num_locals
    // This prevents memory leaks and ensures the outer scope's locals aren't corrupted
    for (int i = state->num_locals; i < ctx->num_locals; i++) {
        if (ctx->local_vars[i]) {
            free(ctx->local_vars[i]);
            ctx->local_vars[i] = NULL;
        }
    }
    ctx->num_locals = state->num_locals;
    ctx->locals_body_start = state->locals_body_start;
    ctx->in_function = state->in_function;
    ctx->has_defers = state->has_defers;
    ctx->defer_unwind_active = state->defer_unwind_active;
    ctx->current_module = state->module;
    ctx->current_closure = state->closure;
    // Free any scopes created during the function (they should already be freed by block statements)
    while (ctx->current_scope) {
        Scope *old = ctx->current_scope;
        ctx->current_scope = old->parent;
        scope_free(old);
    }
    ctx->current_scope = state->scope;
    // Free any allocated tail call labels
    free(ctx->tail_call_label);
    ctx->tail_call_func_name = state->tail_call_func_name;
    ctx->tail_call_label = state->tail_call_label;
    ctx->tail_call_func_expr = state->tail_call_func_expr;
    ctx->loop_body_depth = state->loop_body_depth;
    ctx->loop_depth = state->loop_depth;
    ctx->try_body_depth = state->try_body_depth;
    ctx->func_params = state->func_params;
    ctx->num_func_params = state->num_func_params;
    ctx->func_param_is_ref = state->func_param_is_ref;
    shared_env_clear(ctx);
}

void funcgen_add_params(CodegenContext *ctx, Expr *func) {
    // Track function parameters for unboxing checks
    // These are always HmlValue and cannot be unboxed
    ctx->func_params = func->as.function.param_names;
    ctx->num_func_params = func->as.function.num_params;
    ctx->func_param_is_ref = func->as.function.param_is_ref;

    for (int i = 0; i < func->as.function.num_params; i++) {
        codegen_add_local(ctx, func->as.function.param_names[i]);
    }
    if (func->as.function.rest_param) {
        codegen_add_local(ctx, func->as.function.rest_param);
    }
}

// Check if a variable name is a function parameter (and thus cannot be unboxed)
int codegen_is_func_param(CodegenContext *ctx, const char *name) {
    for (int i = 0; i < ctx->num_func_params; i++) {
        if (strcmp(ctx->func_params[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

// Check if a variable name is a ref parameter (pass-by-reference)
int codegen_is_ref_param(CodegenContext *ctx, const char *name) {
    if (!ctx->func_param_is_ref) return 0;
    for (int i = 0; i < ctx->num_func_params; i++) {
        if (strcmp(ctx->func_params[i], name) == 0) {
            return ctx->func_param_is_ref[i];
        }
    }
    return 0;
}

void funcgen_apply_defaults(CodegenContext *ctx, Expr *func) {
    if (!func->as.function.param_defaults) return;

    for (int i = 0; i < func->as.function.num_params; i++) {
        if (func->as.function.param_defaults[i]) {
            char *safe_param = codegen_sanitize_ident(func->as.function.param_names[i]);
            codegen_writeln(ctx, "if (%s.type == HML_VAL_NULL) {", safe_param);
            codegen_indent_inc(ctx);
            char *default_val = codegen_expr(ctx, func->as.function.param_defaults[i]);
            codegen_writeln(ctx, "%s = %s;", safe_param, default_val);
            free(default_val);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            free(safe_param);
        }
    }
}

void funcgen_setup_shared_env(CodegenContext *ctx, Expr *func, ClosureInfo *closure) {
    (void)closure;
    // Create an empty scope for scanning. We intentionally do NOT add the
    // enclosing function's params or captured vars here, because inner closures
    // need to detect ALL references to enclosing variables as free variables
    // so they get added to the shared environment. If params/captured were in
    // scope, they'd be invisible to free-var analysis and closures would fail
    // to capture them (generating self-referencing `HmlValue x = x;` instead
    // of loading from the closure environment).
    Scope *scan_scope = scope_new(NULL);

    // Clear any previous shared environment and scan for closures
    shared_env_clear(ctx);
    if (func->as.function.body->type == STMT_BLOCK) {
        for (int i = 0; i < func->as.function.body->as.block.count; i++) {
            scan_closures_stmt(ctx, func->as.function.body->as.block.statements[i], scan_scope);
        }
    } else {
        scan_closures_stmt(ctx, func->as.function.body, scan_scope);
    }
    scope_free(scan_scope);

    // Create shared environment if needed
    if (ctx->shared_env_num_vars > 0) {
        char env_name[CODEGEN_ENV_NAME_SIZE];
        snprintf(env_name, sizeof(env_name), "_shared_env_%d", ctx->temp_counter++);
        ctx->shared_env_name = strdup(env_name);
        codegen_writeln(ctx, "HmlClosureEnv *%s = hml_closure_env_new(%d);",
                      env_name, ctx->shared_env_num_vars);

        // Seed the environment with variables that are already declared at
        // this point (parameters, and for closure bodies the extracted
        // captures). The shared env is the source of truth for captured
        // locals, so it must hold their values before any read.
        for (int i = 0; i < ctx->shared_env_num_vars; i++) {
            const char *var = ctx->shared_env_vars[i];
            if (codegen_is_local(ctx, var) && !codegen_is_main_var(ctx, var) &&
                !codegen_is_ref_param(ctx, var)) {
                char *safe_var = codegen_sanitize_ident(var);
                codegen_writeln(ctx, "hml_closure_env_set(%s, %d, %s);",
                              env_name, i, safe_var);
                free(safe_var);
            }
        }
    }
}

void funcgen_generate_body(CodegenContext *ctx, Expr *func) {
    // OPTIMIZATION: Analyze function body for unboxable typed variables
    // This identifies variables like "let x: i32 = 0" that can use native C types
    if (ctx->optimize && ctx->type_ctx) {
        // Clear previous function's unboxable variables to avoid scope contamination
        type_check_clear_all_unboxable(ctx->type_ctx);
        type_check_analyze_block_for_unboxing(ctx->type_ctx, func->as.function.body);
    }

    if (func->as.function.body->type == STMT_BLOCK) {
        for (int i = 0; i < func->as.function.body->as.block.count; i++) {
            codegen_stmt(ctx, func->as.function.body->as.block.statements[i]);
        }
    } else {
        codegen_stmt(ctx, func->as.function.body);
    }
}

// ========== TYPE MAPPING HELPERS ==========

const char* type_kind_to_hml_val(TypeKind kind) {
    switch (kind) {
        case TYPE_I8:     return "HML_VAL_I8";
        case TYPE_I16:    return "HML_VAL_I16";
        case TYPE_I32:    return "HML_VAL_I32";
        case TYPE_I64:    return "HML_VAL_I64";
        case TYPE_U8:     return "HML_VAL_U8";
        case TYPE_U16:    return "HML_VAL_U16";
        case TYPE_U32:    return "HML_VAL_U32";
        case TYPE_U64:    return "HML_VAL_U64";
        case TYPE_F32:    return "HML_VAL_F32";
        case TYPE_F64:    return "HML_VAL_F64";
        case TYPE_BOOL:   return "HML_VAL_BOOL";
        case TYPE_STRING: return "HML_VAL_STRING";
        case TYPE_RUNE:   return "HML_VAL_RUNE";
        case TYPE_PTR:    return "HML_VAL_PTR";
        case TYPE_BUFFER: return "HML_VAL_BUFFER";
        case TYPE_ARRAY:  return "HML_VAL_ARRAY";
        case TYPE_NULL:   return "HML_VAL_NULL";
        default:          return NULL;
    }
}

const char* type_kind_to_ffi_type(TypeKind kind) {
    switch (kind) {
        case TYPE_I8:     return "HML_FFI_I8";
        case TYPE_I16:    return "HML_FFI_I16";
        case TYPE_I32:    return "HML_FFI_I32";
        case TYPE_I64:    return "HML_FFI_I64";
        case TYPE_U8:     return "HML_FFI_U8";
        case TYPE_U16:    return "HML_FFI_U16";
        case TYPE_U32:    return "HML_FFI_U32";
        case TYPE_U64:    return "HML_FFI_U64";
        case TYPE_F32:    return "HML_FFI_F32";
        case TYPE_F64:    return "HML_FFI_F64";
        case TYPE_PTR:    return "HML_FFI_PTR";
        case TYPE_RUNE:   return "HML_FFI_U32";  // rune is a 32-bit Unicode codepoint
        case TYPE_STRING: return "HML_FFI_STRING";
        case TYPE_VOID:   return "HML_FFI_VOID";
        case TYPE_CUSTOM_OBJECT: return "HML_FFI_STRUCT";
        default:          return "HML_FFI_VOID";
    }
}

// ========== UNBOXED TYPE HELPERS ==========

const char* checked_type_to_c_type(CheckedTypeKind kind) {
    switch (kind) {
        case CHECKED_I8:   return "int8_t";
        case CHECKED_I16:  return "int16_t";
        case CHECKED_I32:  return "int32_t";
        case CHECKED_I64:  return "int64_t";
        case CHECKED_U8:   return "uint8_t";
        case CHECKED_U16:  return "uint16_t";
        case CHECKED_U32:  return "uint32_t";
        case CHECKED_U64:  return "uint64_t";
        case CHECKED_F32:  return "float";
        case CHECKED_F64:  return "double";
        case CHECKED_BOOL: return "int";  // C doesn't have native bool
        default:           return NULL;
    }
}

const char* checked_type_to_box_func(CheckedTypeKind kind) {
    switch (kind) {
        case CHECKED_I8:   return "hml_val_i8";
        case CHECKED_I16:  return "hml_val_i16";
        case CHECKED_I32:  return "hml_val_i32";
        case CHECKED_I64:  return "hml_val_i64";
        case CHECKED_U8:   return "hml_val_u8";
        case CHECKED_U16:  return "hml_val_u16";
        case CHECKED_U32:  return "hml_val_u32";
        case CHECKED_U64:  return "hml_val_u64";
        case CHECKED_F32:  return "hml_val_f32";
        case CHECKED_F64:  return "hml_val_f64";
        case CHECKED_BOOL: return "hml_val_bool";
        default:           return NULL;
    }
}

const char* checked_type_to_unbox_func(CheckedTypeKind kind) {
    // Note: runtime only has hml_to_i32, hml_to_i64, hml_to_f64, hml_to_bool
    // Other types need casts, which are handled by checked_type_to_unbox_cast
    switch (kind) {
        case CHECKED_I32:  return "hml_to_i32";
        case CHECKED_I64:  return "hml_to_i64";
        case CHECKED_F64:  return "hml_to_f64";
        case CHECKED_BOOL: return "hml_to_bool";
        // Types that need casts return NULL - caller should use checked_type_to_unbox_cast
        case CHECKED_I8:
        case CHECKED_I16:
        case CHECKED_U8:
        case CHECKED_U16:
        case CHECKED_U32:
        case CHECKED_U64:
        case CHECKED_F32:
            return NULL;
        default:
            return NULL;
    }
}

// Get the cast wrapper for unboxing (e.g., "(int8_t)hml_to_i32" for i8)
const char* checked_type_to_unbox_cast(CheckedTypeKind kind) {
    switch (kind) {
        case CHECKED_I8:   return "(int8_t)hml_to_i32";
        case CHECKED_I16:  return "(int16_t)hml_to_i32";
        case CHECKED_U8:   return "(uint8_t)hml_to_i32";
        case CHECKED_U16:  return "(uint16_t)hml_to_i32";
        case CHECKED_U32:  return "(uint32_t)hml_to_i64";
        case CHECKED_U64:  return "(uint64_t)hml_to_i64";
        case CHECKED_F32:  return "(float)hml_to_f64";
        // These have direct functions
        case CHECKED_I32:  return "hml_to_i32";
        case CHECKED_I64:  return "hml_to_i64";
        case CHECKED_F64:  return "hml_to_f64";
        case CHECKED_BOOL: return "hml_to_bool";
        default:           return NULL;
    }
}

int checked_kind_is_numeric(CheckedTypeKind kind) {
    return kind == CHECKED_I8 || kind == CHECKED_I16 || kind == CHECKED_I32 || kind == CHECKED_I64 ||
           kind == CHECKED_U8 || kind == CHECKED_U16 || kind == CHECKED_U32 || kind == CHECKED_U64 ||
           kind == CHECKED_F32 || kind == CHECKED_F64;
}

int checked_kind_is_integer(CheckedTypeKind kind) {
    return kind == CHECKED_I8 || kind == CHECKED_I16 || kind == CHECKED_I32 || kind == CHECKED_I64 ||
           kind == CHECKED_U8 || kind == CHECKED_U16 || kind == CHECKED_U32 || kind == CHECKED_U64;
}

int checked_kind_is_float(CheckedTypeKind kind) {
    return kind == CHECKED_F32 || kind == CHECKED_F64;
}

// ========== IN-MEMORY BUFFER SUPPORT ==========

MemBuffer* membuf_new(void) {
    MemBuffer *buf = malloc(sizeof(MemBuffer));
    if (!buf) return NULL;
    buf->data = NULL;
    buf->size = 0;
    buf->stream = open_memstream(&buf->data, &buf->size);
    if (!buf->stream) {
        free(buf);
        return NULL;
    }
    return buf;
}

void membuf_flush_to(MemBuffer *buf, FILE *output) {
    if (!buf || !output) return;
    // Flush the stream to update data and size
    if (buf->stream) {
        fflush(buf->stream);
    }
    // Write all buffered data to output
    if (buf->data && buf->size > 0) {
        fwrite(buf->data, 1, buf->size, output);
    }
}

void membuf_free(MemBuffer *buf) {
    if (!buf) return;
    if (buf->stream) {
        fclose(buf->stream);
        buf->stream = NULL;
    }
    // open_memstream allocates data, we must free it
    if (buf->data) {
        free(buf->data);
        buf->data = NULL;
    }
    free(buf);
}
