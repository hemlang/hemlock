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
    ctx->main_func_params = NULL;
    ctx->main_func_has_rest = NULL;
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
    ctx->type_ctx = NULL;  // Set by caller (main.c) if type checking enabled
    ctx->optimize = 1;  // Enable optimization by default
    ctx->has_defers = 0;  // Track if any defers exist in current function
    ctx->tail_call_func_name = NULL;  // Tail call optimization tracking
    ctx->tail_call_label = NULL;
    ctx->tail_call_func_expr = NULL;
    ctx->error_count = 0;
    ctx->warning_count = 0;
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
        if (ctx->main_func_params) {
            free(ctx->main_func_params);
        }
        if (ctx->main_func_has_rest) {
            free(ctx->main_func_has_rest);
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
        int new_cap = safe_double_capacity(ctx->local_capacity, 16);
        if (new_cap < 0) {
            fprintf(stderr, "Codegen error: Local variable capacity overflow\n");
            exit(1);
        }
        ctx->local_vars = realloc(ctx->local_vars, (size_t)new_cap * sizeof(char*));
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
void codegen_add_main_func(CodegenContext *ctx, const char *name, int num_params, int has_rest) {
    if (ctx->num_main_funcs >= ctx->main_funcs_capacity) {
        int new_cap = (ctx->main_funcs_capacity == 0) ? 16 : ctx->main_funcs_capacity * 2;
        ctx->main_funcs = realloc(ctx->main_funcs, new_cap * sizeof(char*));
        ctx->main_func_params = realloc(ctx->main_func_params, new_cap * sizeof(int));
        ctx->main_func_has_rest = realloc(ctx->main_func_has_rest, new_cap * sizeof(int));
        ctx->main_funcs_capacity = new_cap;
    }
    ctx->main_funcs[ctx->num_main_funcs] = strdup(name);
    ctx->main_func_params[ctx->num_main_funcs] = num_params;
    ctx->main_func_has_rest[ctx->num_main_funcs] = has_rest;
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

int codegen_get_main_func_has_rest(CodegenContext *ctx, const char *name) {
    for (int i = 0; i < ctx->num_main_funcs; i++) {
        if (strcmp(ctx->main_funcs[i], name) == 0) {
            return ctx->main_func_has_rest[i];
        }
    }
    return 0;  // Not found, assume no rest param
}

// Main file import tracking (for function call resolution)
void codegen_add_main_import(CodegenContext *ctx, const char *local_name, const char *original_name, const char *module_prefix, int is_function, int num_params, int is_extern) {
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

// ========== FUNCTION GENERATION STATE ==========

void funcgen_save_state(CodegenContext *ctx, FuncGenState *state) {
    state->num_locals = ctx->num_locals;
    state->defer_stack = ctx->defer_stack;
    state->in_function = ctx->in_function;
    state->has_defers = ctx->has_defers;
    state->module = ctx->current_module;
    state->closure = ctx->current_closure;
    state->tail_call_func_name = ctx->tail_call_func_name;
    state->tail_call_label = ctx->tail_call_label;
    state->tail_call_func_expr = ctx->tail_call_func_expr;

    // Initialize for new function
    ctx->defer_stack = NULL;
    ctx->in_function = 1;
    ctx->has_defers = 0;
    ctx->last_closure_env_id = -1;
    ctx->tail_call_func_name = NULL;
    ctx->tail_call_label = NULL;
    ctx->tail_call_func_expr = NULL;
}

void funcgen_restore_state(CodegenContext *ctx, FuncGenState *state) {
    codegen_defer_clear(ctx);
    ctx->defer_stack = state->defer_stack;
    ctx->num_locals = state->num_locals;
    ctx->in_function = state->in_function;
    ctx->has_defers = state->has_defers;
    ctx->current_module = state->module;
    ctx->current_closure = state->closure;
    // Free any allocated tail call labels
    free(ctx->tail_call_label);
    ctx->tail_call_func_name = state->tail_call_func_name;
    ctx->tail_call_label = state->tail_call_label;
    ctx->tail_call_func_expr = state->tail_call_func_expr;
    shared_env_clear(ctx);
}

void funcgen_add_params(CodegenContext *ctx, Expr *func) {
    for (int i = 0; i < func->as.function.num_params; i++) {
        codegen_add_local(ctx, func->as.function.param_names[i]);
    }
    if (func->as.function.rest_param) {
        codegen_add_local(ctx, func->as.function.rest_param);
    }
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
    // Create scope for scanning
    Scope *scan_scope = scope_new(NULL);
    for (int i = 0; i < func->as.function.num_params; i++) {
        scope_add_var(scan_scope, func->as.function.param_names[i]);
    }
    // Add captured variables if this is a closure
    if (closure) {
        for (int i = 0; i < closure->num_captured; i++) {
            scope_add_var(scan_scope, closure->captured_vars[i]);
        }
    }

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
    }
}

void funcgen_generate_body(CodegenContext *ctx, Expr *func) {
    // OPTIMIZATION: Analyze function body for unboxable typed variables
    // This identifies variables like "let x: i32 = 0" that can use native C types
    if (ctx->optimize && ctx->type_ctx) {
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

