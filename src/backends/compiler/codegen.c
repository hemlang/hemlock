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

// ========== CONTEXT MANAGEMENT ==========

CodegenContext* codegen_new(FILE *output) {
    CodegenContext *ctx = malloc(sizeof(CodegenContext));
    ctx->output = output;
    ctx->indent = 0;
    ctx->temp_counter = 0;
    ctx->label_counter = 0;
    ctx->func_counter = 0;
    ctx->in_function = 0;
    ctx->local_vars = NULL;
    ctx->num_locals = 0;
    ctx->local_capacity = 0;
    ctx->current_scope = NULL;
    ctx->closures = NULL;
    ctx->func_params = NULL;
    ctx->num_func_params = 0;
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
    ctx->num_main_funcs = 0;
    ctx->main_funcs_capacity = 0;
    ctx->main_imports = NULL;
    ctx->num_main_imports = 0;
    ctx->main_imports_capacity = 0;
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
    ctx->loop_depth = 0;
    ctx->switch_depth = 0;
    ctx->switch_end_labels = NULL;
    ctx->switch_end_capacity = 0;
    ctx->for_continue_labels = NULL;
    ctx->for_continue_depth = 0;
    ctx->for_continue_capacity = 0;
    return ctx;
}

void codegen_free(CodegenContext *ctx) {
    if (ctx) {
        for (int i = 0; i < ctx->num_locals; i++) {
            free(ctx->local_vars[i]);
        }
        free(ctx->local_vars);

        // Free closures
        ClosureInfo *c = ctx->closures;
        while (c) {
            ClosureInfo *next = c->next;
            free(c->func_name);
            for (int i = 0; i < c->num_captured; i++) {
                free(c->captured_vars[i]);
            }
            free(c->captured_vars);
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

char* codegen_temp(CodegenContext *ctx) {
    char *name = malloc(32);
    snprintf(name, 32, "_tmp%d", ctx->temp_counter++);
    return name;
}

char* codegen_label(CodegenContext *ctx) {
    char *name = malloc(32);
    snprintf(name, 32, "_L%d", ctx->label_counter++);
    return name;
}

char* codegen_anon_func(CodegenContext *ctx) {
    char *name = malloc(32);
    snprintf(name, 32, "hml_fn_anon_%d", ctx->func_counter++);
    return name;
}

void codegen_add_local(CodegenContext *ctx, const char *name) {
    if (ctx->num_locals >= ctx->local_capacity) {
        int new_cap = (ctx->local_capacity == 0) ? 16 : ctx->local_capacity * 2;
        ctx->local_vars = realloc(ctx->local_vars, new_cap * sizeof(char*));
        ctx->local_capacity = new_cap;
    }
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
void codegen_remove_local(CodegenContext *ctx, const char *name) {
    for (int i = 0; i < ctx->num_locals; i++) {
        if (strcmp(ctx->local_vars[i], name) == 0) {
            free(ctx->local_vars[i]);
            // Shift remaining elements down
            for (int j = i; j < ctx->num_locals - 1; j++) {
                ctx->local_vars[j] = ctx->local_vars[j + 1];
            }
            ctx->num_locals--;
            return;
        }
    }
}

// Shadow variable tracking (locals that shadow main vars, like catch params)
void codegen_add_shadow(CodegenContext *ctx, const char *name) {
    if (ctx->num_shadow_vars >= ctx->shadow_vars_capacity) {
        int new_cap = (ctx->shadow_vars_capacity == 0) ? 8 : ctx->shadow_vars_capacity * 2;
        ctx->shadow_vars = realloc(ctx->shadow_vars, new_cap * sizeof(char*));
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
        ctx->const_vars = realloc(ctx->const_vars, new_cap * sizeof(char*));
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

// Try-finally context tracking (for return/break to jump to finally first)
void codegen_push_try_finally(CodegenContext *ctx, const char *finally_label,
                              const char *return_value_var, const char *has_return_var) {
    if (ctx->try_finally_depth >= ctx->try_finally_capacity) {
        int new_cap = (ctx->try_finally_capacity == 0) ? 4 : ctx->try_finally_capacity * 2;
        ctx->finally_labels = realloc(ctx->finally_labels, new_cap * sizeof(char*));
        ctx->return_value_vars = realloc(ctx->return_value_vars, new_cap * sizeof(char*));
        ctx->has_return_vars = realloc(ctx->has_return_vars, new_cap * sizeof(char*));
        ctx->try_finally_capacity = new_cap;
    }
    ctx->finally_labels[ctx->try_finally_depth] = strdup(finally_label);
    ctx->return_value_vars[ctx->try_finally_depth] = strdup(return_value_var);
    ctx->has_return_vars[ctx->try_finally_depth] = strdup(has_return_var);
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
        ctx->switch_end_labels = realloc(ctx->switch_end_labels, new_cap * sizeof(char*));
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
        ctx->for_continue_labels = realloc(ctx->for_continue_labels, new_cap * sizeof(char*));
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
        ctx->main_vars = realloc(ctx->main_vars, new_cap * sizeof(char*));
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

// Main file function definitions (subset of main_vars that are actual function defs)
void codegen_add_main_func(CodegenContext *ctx, const char *name, int num_params) {
    if (ctx->num_main_funcs >= ctx->main_funcs_capacity) {
        int new_cap = (ctx->main_funcs_capacity == 0) ? 16 : ctx->main_funcs_capacity * 2;
        ctx->main_funcs = realloc(ctx->main_funcs, new_cap * sizeof(char*));
        ctx->main_func_params = realloc(ctx->main_func_params, new_cap * sizeof(int));
        ctx->main_funcs_capacity = new_cap;
    }
    ctx->main_funcs[ctx->num_main_funcs] = strdup(name);
    ctx->main_func_params[ctx->num_main_funcs] = num_params;
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

int codegen_get_main_func_params(CodegenContext *ctx, const char *name) {
    for (int i = 0; i < ctx->num_main_funcs; i++) {
        if (strcmp(ctx->main_funcs[i], name) == 0) {
            return ctx->main_func_params[i];
        }
    }
    return -1;  // Not found
}

// Main file import tracking (for function call resolution)
void codegen_add_main_import(CodegenContext *ctx, const char *local_name, const char *original_name, const char *module_prefix, int is_function, int num_params) {
    if (ctx->num_main_imports >= ctx->main_imports_capacity) {
        int new_cap = (ctx->main_imports_capacity == 0) ? 16 : ctx->main_imports_capacity * 2;
        ctx->main_imports = realloc(ctx->main_imports, new_cap * sizeof(ImportBinding));
        ctx->main_imports_capacity = new_cap;
    }
    ImportBinding *binding = &ctx->main_imports[ctx->num_main_imports++];
    binding->local_name = strdup(local_name);
    binding->original_name = strdup(original_name);
    binding->module_prefix = strdup(module_prefix);
    binding->is_function = is_function;
    binding->num_params = num_params;
}

ImportBinding* codegen_find_main_import(CodegenContext *ctx, const char *name) {
    for (int i = 0; i < ctx->num_main_imports; i++) {
        if (strcmp(ctx->main_imports[i].local_name, name) == 0) {
            return &ctx->main_imports[i];
        }
    }
    return NULL;
}

// ========== STRING HELPERS ==========

char* codegen_escape_string(const char *str) {
    if (!str) return strdup("");

    int len = strlen(str);
    // Worst case: every char needs escaping (2x) + quotes + null
    char *escaped = malloc(len * 2 + 3);
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
        scope->vars = realloc(scope->vars, new_cap * sizeof(char*));
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

