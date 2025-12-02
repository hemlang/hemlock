/*
 * Hemlock C Code Generator
 *
 * Translates Hemlock AST to C source code that links against
 * the Hemlock runtime library.
 */

#define _GNU_SOURCE
#include "codegen.h"
#include "../../include/lexer.h"
#include "../../include/parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>

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
void codegen_add_main_func(CodegenContext *ctx, const char *name) {
    if (ctx->num_main_funcs >= ctx->main_funcs_capacity) {
        int new_cap = (ctx->main_funcs_capacity == 0) ? 16 : ctx->main_funcs_capacity * 2;
        ctx->main_funcs = realloc(ctx->main_funcs, new_cap * sizeof(char*));
        ctx->main_funcs_capacity = new_cap;
    }
    ctx->main_funcs[ctx->num_main_funcs++] = strdup(name);
}

int codegen_is_main_func(CodegenContext *ctx, const char *name) {
    for (int i = 0; i < ctx->num_main_funcs; i++) {
        if (strcmp(ctx->main_funcs[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

// Main file import tracking (for function call resolution)
void codegen_add_main_import(CodegenContext *ctx, const char *local_name, const char *original_name, const char *module_prefix) {
    if (ctx->num_main_imports >= ctx->main_imports_capacity) {
        int new_cap = (ctx->main_imports_capacity == 0) ? 16 : ctx->main_imports_capacity * 2;
        ctx->main_imports = realloc(ctx->main_imports, new_cap * sizeof(ImportBinding));
        ctx->main_imports_capacity = new_cap;
    }
    ImportBinding *binding = &ctx->main_imports[ctx->num_main_imports++];
    binding->local_name = strdup(local_name);
    binding->original_name = strdup(original_name);
    binding->module_prefix = strdup(module_prefix);
    binding->is_function = 1;  // Assume it's a function for now
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

// ========== FREE VARIABLE ANALYSIS ==========

FreeVarSet* free_var_set_new(void) {
    FreeVarSet *set = malloc(sizeof(FreeVarSet));
    set->vars = NULL;
    set->num_vars = 0;
    set->capacity = 0;
    return set;
}

void free_var_set_free(FreeVarSet *set) {
    if (set) {
        for (int i = 0; i < set->num_vars; i++) {
            free(set->vars[i]);
        }
        free(set->vars);
        free(set);
    }
}

void free_var_set_add(FreeVarSet *set, const char *var) {
    if (!set || !var) return;

    // Check if already present
    for (int i = 0; i < set->num_vars; i++) {
        if (strcmp(set->vars[i], var) == 0) return;
    }

    // Expand if needed
    if (set->num_vars >= set->capacity) {
        int new_cap = (set->capacity == 0) ? 8 : set->capacity * 2;
        set->vars = realloc(set->vars, new_cap * sizeof(char*));
        set->capacity = new_cap;
    }

    set->vars[set->num_vars++] = strdup(var);
}

// ========== SHARED ENVIRONMENT SUPPORT ==========
// These functions help multiple closures share a single environment

// Add a variable to the shared environment if not already present
// Returns the index of the variable in the shared environment
static int shared_env_add_var(CodegenContext *ctx, const char *var) {
    // Check if already present
    for (int i = 0; i < ctx->shared_env_num_vars; i++) {
        if (strcmp(ctx->shared_env_vars[i], var) == 0) {
            return i;
        }
    }

    // Expand if needed
    if (ctx->shared_env_num_vars >= ctx->shared_env_capacity) {
        int new_cap = (ctx->shared_env_capacity == 0) ? 16 : ctx->shared_env_capacity * 2;
        ctx->shared_env_vars = realloc(ctx->shared_env_vars, new_cap * sizeof(char*));
        ctx->shared_env_capacity = new_cap;
    }

    int idx = ctx->shared_env_num_vars;
    ctx->shared_env_vars[ctx->shared_env_num_vars++] = strdup(var);
    return idx;
}

// Get the index of a variable in the shared environment (-1 if not found)
static int shared_env_get_index(CodegenContext *ctx, const char *var) {
    for (int i = 0; i < ctx->shared_env_num_vars; i++) {
        if (strcmp(ctx->shared_env_vars[i], var) == 0) {
            return i;
        }
    }
    return -1;
}

// Clear the shared environment state
static void shared_env_clear(CodegenContext *ctx) {
    if (ctx->shared_env_vars) {
        for (int i = 0; i < ctx->shared_env_num_vars; i++) {
            free(ctx->shared_env_vars[i]);
        }
        free(ctx->shared_env_vars);
    }
    if (ctx->shared_env_name) {
        free(ctx->shared_env_name);
    }
    ctx->shared_env_name = NULL;
    ctx->shared_env_vars = NULL;
    ctx->shared_env_num_vars = 0;
    ctx->shared_env_capacity = 0;
}

// Forward declarations for scanning closures in function body
static void scan_closures_expr(CodegenContext *ctx, Expr *expr, Scope *local_scope);
static void scan_closures_stmt(CodegenContext *ctx, Stmt *stmt, Scope *local_scope);

// Scan expression for closures and collect their captured variables
static void scan_closures_expr(CodegenContext *ctx, Expr *expr, Scope *local_scope) {
    if (!expr) return;

    switch (expr->type) {
        case EXPR_FUNCTION: {
            // Found a closure! Collect its captured variables
            // Create a scope for the function's parameters
            Scope *func_scope = scope_new(local_scope);
            for (int i = 0; i < expr->as.function.num_params; i++) {
                scope_add_var(func_scope, expr->as.function.param_names[i]);
            }

            // Find free variables (captured from outer scope)
            FreeVarSet *captured = free_var_set_new();
            if (expr->as.function.body) {
                if (expr->as.function.body->type == STMT_BLOCK) {
                    for (int i = 0; i < expr->as.function.body->as.block.count; i++) {
                        find_free_vars_stmt(expr->as.function.body->as.block.statements[i], func_scope, captured);
                    }
                } else {
                    find_free_vars_stmt(expr->as.function.body, func_scope, captured);
                }
            }

            // Add each captured variable to the shared environment
            for (int i = 0; i < captured->num_vars; i++) {
                shared_env_add_var(ctx, captured->vars[i]);
            }

            // Also scan for nested closures within this closure's body
            if (expr->as.function.body) {
                if (expr->as.function.body->type == STMT_BLOCK) {
                    for (int i = 0; i < expr->as.function.body->as.block.count; i++) {
                        scan_closures_stmt(ctx, expr->as.function.body->as.block.statements[i], func_scope);
                    }
                } else {
                    scan_closures_stmt(ctx, expr->as.function.body, func_scope);
                }
            }

            free_var_set_free(captured);
            scope_free(func_scope);
            break;
        }

        case EXPR_BINARY:
            scan_closures_expr(ctx, expr->as.binary.left, local_scope);
            scan_closures_expr(ctx, expr->as.binary.right, local_scope);
            break;

        case EXPR_UNARY:
            scan_closures_expr(ctx, expr->as.unary.operand, local_scope);
            break;

        case EXPR_CALL:
            scan_closures_expr(ctx, expr->as.call.func, local_scope);
            for (int i = 0; i < expr->as.call.num_args; i++) {
                scan_closures_expr(ctx, expr->as.call.args[i], local_scope);
            }
            break;

        case EXPR_GET_PROPERTY:
            scan_closures_expr(ctx, expr->as.get_property.object, local_scope);
            break;

        case EXPR_SET_PROPERTY:
            scan_closures_expr(ctx, expr->as.set_property.object, local_scope);
            scan_closures_expr(ctx, expr->as.set_property.value, local_scope);
            break;

        case EXPR_ARRAY_LITERAL:
            for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                scan_closures_expr(ctx, expr->as.array_literal.elements[i], local_scope);
            }
            break;

        case EXPR_OBJECT_LITERAL:
            for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
                scan_closures_expr(ctx, expr->as.object_literal.field_values[i], local_scope);
            }
            break;

        case EXPR_INDEX:
            scan_closures_expr(ctx, expr->as.index.object, local_scope);
            scan_closures_expr(ctx, expr->as.index.index, local_scope);
            break;

        case EXPR_INDEX_ASSIGN:
            scan_closures_expr(ctx, expr->as.index_assign.object, local_scope);
            scan_closures_expr(ctx, expr->as.index_assign.index, local_scope);
            scan_closures_expr(ctx, expr->as.index_assign.value, local_scope);
            break;

        case EXPR_ASSIGN:
            scan_closures_expr(ctx, expr->as.assign.value, local_scope);
            break;

        case EXPR_TERNARY:
            scan_closures_expr(ctx, expr->as.ternary.condition, local_scope);
            scan_closures_expr(ctx, expr->as.ternary.true_expr, local_scope);
            scan_closures_expr(ctx, expr->as.ternary.false_expr, local_scope);
            break;

        case EXPR_STRING_INTERPOLATION:
            for (int i = 0; i < expr->as.string_interpolation.num_parts; i++) {
                scan_closures_expr(ctx, expr->as.string_interpolation.expr_parts[i], local_scope);
            }
            break;

        case EXPR_AWAIT:
            scan_closures_expr(ctx, expr->as.await_expr.awaited_expr, local_scope);
            break;

        case EXPR_PREFIX_INC:
            scan_closures_expr(ctx, expr->as.prefix_inc.operand, local_scope);
            break;
        case EXPR_PREFIX_DEC:
            scan_closures_expr(ctx, expr->as.prefix_dec.operand, local_scope);
            break;
        case EXPR_POSTFIX_INC:
            scan_closures_expr(ctx, expr->as.postfix_inc.operand, local_scope);
            break;
        case EXPR_POSTFIX_DEC:
            scan_closures_expr(ctx, expr->as.postfix_dec.operand, local_scope);
            break;

        default:
            // Literals, identifiers, etc. - no closures
            break;
    }
}

// Scan statement for closures
static void scan_closures_stmt(CodegenContext *ctx, Stmt *stmt, Scope *local_scope) {
    if (!stmt) return;

    switch (stmt->type) {
        case STMT_LET:
            if (stmt->as.let.value) {
                scan_closures_expr(ctx, stmt->as.let.value, local_scope);
            }
            break;

        case STMT_CONST:
            if (stmt->as.const_stmt.value) {
                scan_closures_expr(ctx, stmt->as.const_stmt.value, local_scope);
            }
            break;

        case STMT_EXPR:
            scan_closures_expr(ctx, stmt->as.expr, local_scope);
            break;

        case STMT_RETURN:
            if (stmt->as.return_stmt.value) {
                scan_closures_expr(ctx, stmt->as.return_stmt.value, local_scope);
            }
            break;

        case STMT_IF:
            scan_closures_expr(ctx, stmt->as.if_stmt.condition, local_scope);
            scan_closures_stmt(ctx, stmt->as.if_stmt.then_branch, local_scope);
            if (stmt->as.if_stmt.else_branch) {
                scan_closures_stmt(ctx, stmt->as.if_stmt.else_branch, local_scope);
            }
            break;

        case STMT_WHILE:
            scan_closures_expr(ctx, stmt->as.while_stmt.condition, local_scope);
            scan_closures_stmt(ctx, stmt->as.while_stmt.body, local_scope);
            break;

        case STMT_FOR:
            if (stmt->as.for_loop.initializer) {
                scan_closures_stmt(ctx, stmt->as.for_loop.initializer, local_scope);
            }
            if (stmt->as.for_loop.condition) {
                scan_closures_expr(ctx, stmt->as.for_loop.condition, local_scope);
            }
            if (stmt->as.for_loop.increment) {
                scan_closures_expr(ctx, stmt->as.for_loop.increment, local_scope);
            }
            scan_closures_stmt(ctx, stmt->as.for_loop.body, local_scope);
            break;

        case STMT_FOR_IN:
            scan_closures_expr(ctx, stmt->as.for_in.iterable, local_scope);
            scan_closures_stmt(ctx, stmt->as.for_in.body, local_scope);
            break;

        case STMT_BLOCK:
            for (int i = 0; i < stmt->as.block.count; i++) {
                scan_closures_stmt(ctx, stmt->as.block.statements[i], local_scope);
            }
            break;

        // Note: Named functions are parsed as STMT_LET with EXPR_FUNCTION value
        // They are handled in the STMT_LET case above

        case STMT_TRY:
            scan_closures_stmt(ctx, stmt->as.try_stmt.try_block, local_scope);
            if (stmt->as.try_stmt.catch_block) {
                scan_closures_stmt(ctx, stmt->as.try_stmt.catch_block, local_scope);
            }
            if (stmt->as.try_stmt.finally_block) {
                scan_closures_stmt(ctx, stmt->as.try_stmt.finally_block, local_scope);
            }
            break;

        case STMT_THROW:
            scan_closures_expr(ctx, stmt->as.throw_stmt.value, local_scope);
            break;

        case STMT_SWITCH:
            scan_closures_expr(ctx, stmt->as.switch_stmt.expr, local_scope);
            for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
                if (stmt->as.switch_stmt.case_values[i]) {
                    scan_closures_expr(ctx, stmt->as.switch_stmt.case_values[i], local_scope);
                }
                if (stmt->as.switch_stmt.case_bodies[i]) {
                    scan_closures_stmt(ctx, stmt->as.switch_stmt.case_bodies[i], local_scope);
                }
            }
            break;

        case STMT_DEFER:
            scan_closures_expr(ctx, stmt->as.defer_stmt.call, local_scope);
            break;

        default:
            break;
    }
}

// Forward declaration for mutual recursion
void find_free_vars_stmt(Stmt *stmt, Scope *local_scope, FreeVarSet *free_vars);

void find_free_vars(Expr *expr, Scope *local_scope, FreeVarSet *free_vars) {
    if (!expr) return;

    switch (expr->type) {
        case EXPR_IDENT: {
            // If variable is not in local scope, it's free
            const char *name = expr->as.ident;
            if (!scope_is_defined(local_scope, name)) {
                free_var_set_add(free_vars, name);
            }
            break;
        }

        case EXPR_BINARY:
            find_free_vars(expr->as.binary.left, local_scope, free_vars);
            find_free_vars(expr->as.binary.right, local_scope, free_vars);
            break;

        case EXPR_UNARY:
            find_free_vars(expr->as.unary.operand, local_scope, free_vars);
            break;

        case EXPR_CALL:
            find_free_vars(expr->as.call.func, local_scope, free_vars);
            for (int i = 0; i < expr->as.call.num_args; i++) {
                find_free_vars(expr->as.call.args[i], local_scope, free_vars);
            }
            break;

        case EXPR_INDEX:
            find_free_vars(expr->as.index.object, local_scope, free_vars);
            find_free_vars(expr->as.index.index, local_scope, free_vars);
            break;

        case EXPR_INDEX_ASSIGN:
            find_free_vars(expr->as.index_assign.object, local_scope, free_vars);
            find_free_vars(expr->as.index_assign.index, local_scope, free_vars);
            find_free_vars(expr->as.index_assign.value, local_scope, free_vars);
            break;

        case EXPR_GET_PROPERTY:
            find_free_vars(expr->as.get_property.object, local_scope, free_vars);
            break;

        case EXPR_SET_PROPERTY:
            find_free_vars(expr->as.set_property.object, local_scope, free_vars);
            find_free_vars(expr->as.set_property.value, local_scope, free_vars);
            break;

        case EXPR_ASSIGN:
            find_free_vars(expr->as.assign.value, local_scope, free_vars);
            // Also check if target is a free var
            if (!scope_is_defined(local_scope, expr->as.assign.name)) {
                free_var_set_add(free_vars, expr->as.assign.name);
            }
            break;

        case EXPR_TERNARY:
            find_free_vars(expr->as.ternary.condition, local_scope, free_vars);
            find_free_vars(expr->as.ternary.true_expr, local_scope, free_vars);
            find_free_vars(expr->as.ternary.false_expr, local_scope, free_vars);
            break;

        case EXPR_ARRAY_LITERAL:
            for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                find_free_vars(expr->as.array_literal.elements[i], local_scope, free_vars);
            }
            break;

        case EXPR_OBJECT_LITERAL:
            for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
                find_free_vars(expr->as.object_literal.field_values[i], local_scope, free_vars);
            }
            break;

        case EXPR_FUNCTION: {
            // Create a new scope for the function body
            Scope *func_scope = scope_new(local_scope);

            // Add parameters to the function's scope
            for (int i = 0; i < expr->as.function.num_params; i++) {
                scope_add_var(func_scope, expr->as.function.param_names[i]);
            }

            // Find free vars in function body
            find_free_vars_stmt(expr->as.function.body, func_scope, free_vars);

            scope_free(func_scope);
            break;
        }

        case EXPR_STRING_INTERPOLATION:
            for (int i = 0; i < expr->as.string_interpolation.num_parts; i++) {
                find_free_vars(expr->as.string_interpolation.expr_parts[i], local_scope, free_vars);
            }
            break;

        case EXPR_AWAIT:
            find_free_vars(expr->as.await_expr.awaited_expr, local_scope, free_vars);
            break;

        case EXPR_NULL_COALESCE:
            find_free_vars(expr->as.null_coalesce.left, local_scope, free_vars);
            find_free_vars(expr->as.null_coalesce.right, local_scope, free_vars);
            break;

        case EXPR_OPTIONAL_CHAIN:
            find_free_vars(expr->as.optional_chain.object, local_scope, free_vars);
            if (expr->as.optional_chain.index) {
                find_free_vars(expr->as.optional_chain.index, local_scope, free_vars);
            }
            if (expr->as.optional_chain.args) {
                for (int i = 0; i < expr->as.optional_chain.num_args; i++) {
                    find_free_vars(expr->as.optional_chain.args[i], local_scope, free_vars);
                }
            }
            break;

        case EXPR_PREFIX_INC:
            find_free_vars(expr->as.prefix_inc.operand, local_scope, free_vars);
            break;

        case EXPR_PREFIX_DEC:
            find_free_vars(expr->as.prefix_dec.operand, local_scope, free_vars);
            break;

        case EXPR_POSTFIX_INC:
            find_free_vars(expr->as.postfix_inc.operand, local_scope, free_vars);
            break;

        case EXPR_POSTFIX_DEC:
            find_free_vars(expr->as.postfix_dec.operand, local_scope, free_vars);
            break;

        default:
            // Primitives (number, bool, string, null, rune) have no free vars
            break;
    }
}

void find_free_vars_stmt(Stmt *stmt, Scope *local_scope, FreeVarSet *free_vars) {
    if (!stmt) return;

    switch (stmt->type) {
        case STMT_LET:
            if (stmt->as.let.value) {
                find_free_vars(stmt->as.let.value, local_scope, free_vars);
            }
            scope_add_var(local_scope, stmt->as.let.name);
            break;

        case STMT_CONST:
            if (stmt->as.const_stmt.value) {
                find_free_vars(stmt->as.const_stmt.value, local_scope, free_vars);
            }
            scope_add_var(local_scope, stmt->as.const_stmt.name);
            break;

        case STMT_EXPR:
            find_free_vars(stmt->as.expr, local_scope, free_vars);
            break;

        case STMT_IF:
            find_free_vars(stmt->as.if_stmt.condition, local_scope, free_vars);
            find_free_vars_stmt(stmt->as.if_stmt.then_branch, local_scope, free_vars);
            if (stmt->as.if_stmt.else_branch) {
                find_free_vars_stmt(stmt->as.if_stmt.else_branch, local_scope, free_vars);
            }
            break;

        case STMT_WHILE:
            find_free_vars(stmt->as.while_stmt.condition, local_scope, free_vars);
            find_free_vars_stmt(stmt->as.while_stmt.body, local_scope, free_vars);
            break;

        case STMT_FOR:
            if (stmt->as.for_loop.initializer) {
                find_free_vars_stmt(stmt->as.for_loop.initializer, local_scope, free_vars);
            }
            if (stmt->as.for_loop.condition) {
                find_free_vars(stmt->as.for_loop.condition, local_scope, free_vars);
            }
            if (stmt->as.for_loop.increment) {
                find_free_vars(stmt->as.for_loop.increment, local_scope, free_vars);
            }
            find_free_vars_stmt(stmt->as.for_loop.body, local_scope, free_vars);
            break;

        case STMT_FOR_IN:
            find_free_vars(stmt->as.for_in.iterable, local_scope, free_vars);
            // Add loop variables to scope
            if (stmt->as.for_in.key_var) {
                scope_add_var(local_scope, stmt->as.for_in.key_var);
            }
            scope_add_var(local_scope, stmt->as.for_in.value_var);
            find_free_vars_stmt(stmt->as.for_in.body, local_scope, free_vars);
            break;

        case STMT_BLOCK:
            for (int i = 0; i < stmt->as.block.count; i++) {
                find_free_vars_stmt(stmt->as.block.statements[i], local_scope, free_vars);
            }
            break;

        case STMT_RETURN:
            if (stmt->as.return_stmt.value) {
                find_free_vars(stmt->as.return_stmt.value, local_scope, free_vars);
            }
            break;

        case STMT_TRY:
            find_free_vars_stmt(stmt->as.try_stmt.try_block, local_scope, free_vars);
            if (stmt->as.try_stmt.catch_block) {
                // Add catch param to scope temporarily
                if (stmt->as.try_stmt.catch_param) {
                    scope_add_var(local_scope, stmt->as.try_stmt.catch_param);
                }
                find_free_vars_stmt(stmt->as.try_stmt.catch_block, local_scope, free_vars);
            }
            if (stmt->as.try_stmt.finally_block) {
                find_free_vars_stmt(stmt->as.try_stmt.finally_block, local_scope, free_vars);
            }
            break;

        case STMT_THROW:
            find_free_vars(stmt->as.throw_stmt.value, local_scope, free_vars);
            break;

        case STMT_SWITCH:
            find_free_vars(stmt->as.switch_stmt.expr, local_scope, free_vars);
            for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
                if (stmt->as.switch_stmt.case_values[i]) {
                    find_free_vars(stmt->as.switch_stmt.case_values[i], local_scope, free_vars);
                }
                find_free_vars_stmt(stmt->as.switch_stmt.case_bodies[i], local_scope, free_vars);
            }
            break;

        case STMT_DEFER:
            find_free_vars(stmt->as.defer_stmt.call, local_scope, free_vars);
            break;

        case STMT_ENUM:
            // Enum values are constants, no free variables needed
            // Just check explicit value expressions
            for (int i = 0; i < stmt->as.enum_decl.num_variants; i++) {
                if (stmt->as.enum_decl.variant_values[i]) {
                    find_free_vars(stmt->as.enum_decl.variant_values[i], local_scope, free_vars);
                }
            }
            break;

        default:
            break;
    }
}

// ========== EXPRESSION CODE GENERATION ==========

// Forward declarations
static void codegen_function_decl(CodegenContext *ctx, Expr *func, const char *name);
ImportBinding* module_find_import(CompiledModule *module, const char *name);
static int module_is_extern_fn(CompiledModule *module, const char *name);

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
                    codegen_writeln(ctx, "HmlValue %s = hml_val_i64(%ldL);", result, expr->as.number.int_value);
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
            // Handle 'self' specially - maps to hml_self global
            if (strcmp(expr->as.ident, "self") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_self;", result);
            // Handle signal constants
            } else if (strcmp(expr->as.ident, "SIGINT") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGINT);", result);
            } else if (strcmp(expr->as.ident, "SIGTERM") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGTERM);", result);
            } else if (strcmp(expr->as.ident, "SIGHUP") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGHUP);", result);
            } else if (strcmp(expr->as.ident, "SIGQUIT") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGQUIT);", result);
            } else if (strcmp(expr->as.ident, "SIGABRT") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGABRT);", result);
            } else if (strcmp(expr->as.ident, "SIGUSR1") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGUSR1);", result);
            } else if (strcmp(expr->as.ident, "SIGUSR2") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGUSR2);", result);
            } else if (strcmp(expr->as.ident, "SIGALRM") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGALRM);", result);
            } else if (strcmp(expr->as.ident, "SIGCHLD") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGCHLD);", result);
            } else if (strcmp(expr->as.ident, "SIGPIPE") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGPIPE);", result);
            } else if (strcmp(expr->as.ident, "SIGCONT") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGCONT);", result);
            } else if (strcmp(expr->as.ident, "SIGSTOP") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGSTOP);", result);
            } else if (strcmp(expr->as.ident, "SIGTSTP") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGTSTP);", result);
            // Handle socket constants
            } else if (strcmp(expr->as.ident, "AF_INET") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_i32(AF_INET);", result);
            } else if (strcmp(expr->as.ident, "AF_INET6") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_i32(AF_INET6);", result);
            } else if (strcmp(expr->as.ident, "SOCK_STREAM") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SOCK_STREAM);", result);
            } else if (strcmp(expr->as.ident, "SOCK_DGRAM") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SOCK_DGRAM);", result);
            } else if (strcmp(expr->as.ident, "SOL_SOCKET") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SOL_SOCKET);", result);
            } else if (strcmp(expr->as.ident, "SO_REUSEADDR") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SO_REUSEADDR);", result);
            } else if (strcmp(expr->as.ident, "SO_KEEPALIVE") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SO_KEEPALIVE);", result);
            } else if (strcmp(expr->as.ident, "SO_RCVTIMEO") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SO_RCVTIMEO);", result);
            } else if (strcmp(expr->as.ident, "SO_SNDTIMEO") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SO_SNDTIMEO);", result);
            // Handle math constants (builtins)
            } else if (strcmp(expr->as.ident, "__PI") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_f64(3.14159265358979323846);", result);
            } else if (strcmp(expr->as.ident, "__E") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_f64(2.71828182845904523536);", result);
            } else if (strcmp(expr->as.ident, "__TAU") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_f64(6.28318530717958647692);", result);
            } else if (strcmp(expr->as.ident, "__INF") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_f64(1.0/0.0);", result);
            } else if (strcmp(expr->as.ident, "__NAN") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_f64(0.0/0.0);", result);
            // Handle math functions (builtins)
            } else if (strcmp(expr->as.ident, "__sin") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_sin, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__cos") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_cos, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__tan") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_tan, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__asin") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_asin, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__acos") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_acos, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__atan") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atan, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__atan2") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atan2, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "__sqrt") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_sqrt, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__pow") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_pow, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "__exp") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_exp, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__log") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_log, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__log10") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_log10, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__log2") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_log2, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__floor") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_floor, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__ceil") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_ceil, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__round") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_round, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__trunc") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_trunc, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__abs") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_abs, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__min") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_min, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "__max") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_max, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "__clamp") == 0 || strcmp(expr->as.ident, "clamp") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_clamp, 3, 0);", result);
            } else if (strcmp(expr->as.ident, "__rand") == 0 || strcmp(expr->as.ident, "rand") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_rand, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "__rand_range") == 0 || strcmp(expr->as.ident, "rand_range") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_rand_range, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "__seed") == 0 || strcmp(expr->as.ident, "seed") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_seed, 1, 0);", result);
            // Handle time functions (builtins)
            } else if (strcmp(expr->as.ident, "__now") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_now, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "__time_ms") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_time_ms, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "__clock") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_clock, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "__sleep") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_sleep, 1, 0);", result);
            // Handle datetime functions (builtins)
            } else if (strcmp(expr->as.ident, "__localtime") == 0 || strcmp(expr->as.ident, "localtime") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_localtime, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__gmtime") == 0 || strcmp(expr->as.ident, "gmtime") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_gmtime, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__mktime") == 0 || strcmp(expr->as.ident, "mktime") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_mktime, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__strftime") == 0 || strcmp(expr->as.ident, "strftime") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_strftime, 2, 0);", result);
            // Handle environment functions (builtins)
            } else if (strcmp(expr->as.ident, "__getenv") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_getenv, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__setenv") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_setenv, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "__unsetenv") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_unsetenv, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__exit") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_exit, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__get_pid") == 0 || strcmp(expr->as.ident, "get_pid") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_get_pid, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "__getppid") == 0 || strcmp(expr->as.ident, "getppid") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_getppid, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "__getuid") == 0 || strcmp(expr->as.ident, "getuid") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_getuid, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "__geteuid") == 0 || strcmp(expr->as.ident, "geteuid") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_geteuid, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "__getgid") == 0 || strcmp(expr->as.ident, "getgid") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_getgid, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "__getegid") == 0 || strcmp(expr->as.ident, "getegid") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_getegid, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "__exec") == 0 || strcmp(expr->as.ident, "exec") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_exec, 1, 0);", result);
            // Handle process functions (builtins)
            } else if (strcmp(expr->as.ident, "__kill") == 0 || strcmp(expr->as.ident, "kill") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_kill, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "__fork") == 0 || strcmp(expr->as.ident, "fork") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_fork, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "__wait") == 0 || strcmp(expr->as.ident, "wait") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_wait, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "__waitpid") == 0 || strcmp(expr->as.ident, "waitpid") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_waitpid, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "__abort") == 0 || strcmp(expr->as.ident, "abort") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_abort, 0, 0);", result);
            // Handle filesystem functions (builtins)
            } else if (strcmp(expr->as.ident, "__exists") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_exists, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__read_file") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_read_file, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__write_file") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_write_file, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "__append_file") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_append_file, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "__remove_file") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_remove_file, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__rename") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_rename, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "__copy_file") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_copy_file, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "__is_file") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_is_file, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__is_dir") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_is_dir, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__file_stat") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_file_stat, 1, 0);", result);
            // Handle directory functions (builtins)
            } else if (strcmp(expr->as.ident, "__make_dir") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_make_dir, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "__remove_dir") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_remove_dir, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__list_dir") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_list_dir, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__cwd") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_cwd, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "__chdir") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_chdir, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__absolute_path") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_absolute_path, 1, 0);", result);
            // Handle system info functions (builtins)
            } else if (strcmp(expr->as.ident, "__platform") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_platform, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "__arch") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_arch, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "__hostname") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_hostname, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "__username") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_username, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "__homedir") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_homedir, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "__cpu_count") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_cpu_count, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "__total_memory") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_total_memory, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "__free_memory") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_free_memory, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "__os_version") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_os_version, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "__os_name") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_os_name, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "__tmpdir") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_tmpdir, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "__uptime") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_uptime, 0, 0);", result);
            // Handle compression functions (builtins)
            } else if (strcmp(expr->as.ident, "__zlib_compress") == 0 || strcmp(expr->as.ident, "zlib_compress") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_zlib_compress, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "__zlib_decompress") == 0 || strcmp(expr->as.ident, "zlib_decompress") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_zlib_decompress, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "__gzip_compress") == 0 || strcmp(expr->as.ident, "gzip_compress") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_gzip_compress, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "__gzip_decompress") == 0 || strcmp(expr->as.ident, "gzip_decompress") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_gzip_decompress, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "__zlib_compress_bound") == 0 || strcmp(expr->as.ident, "zlib_compress_bound") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_zlib_compress_bound, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__crc32") == 0 || strcmp(expr->as.ident, "crc32") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_crc32, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__adler32") == 0 || strcmp(expr->as.ident, "adler32") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_adler32, 1, 0);", result);
            // Internal helper builtins
            } else if (strcmp(expr->as.ident, "__read_u32") == 0 || strcmp(expr->as.ident, "read_u32") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_read_u32, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__read_u64") == 0 || strcmp(expr->as.ident, "read_u64") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_read_u64, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__strerror") == 0 || strcmp(expr->as.ident, "strerror") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_strerror, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "__dirent_name") == 0 || strcmp(expr->as.ident, "dirent_name") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_dirent_name, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__string_to_cstr") == 0 || strcmp(expr->as.ident, "string_to_cstr") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_string_to_cstr, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__cstr_to_string") == 0 || strcmp(expr->as.ident, "cstr_to_string") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_cstr_to_string, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__to_string") == 0 || strcmp(expr->as.ident, "to_string") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_to_string, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__string_byte_length") == 0 || strcmp(expr->as.ident, "string_byte_length") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_string_byte_length, 1, 0);", result);
            // DNS/Networking builtins
            } else if (strcmp(expr->as.ident, "dns_resolve") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_dns_resolve, 1, 0);", result);
            // HTTP builtins (libwebsockets)
            } else if (strcmp(expr->as.ident, "__lws_http_get") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_http_get, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__lws_http_post") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_http_post, 3, 0);", result);
            } else if (strcmp(expr->as.ident, "__lws_response_status") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_response_status, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__lws_response_body") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_response_body, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__lws_response_headers") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_response_headers, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__lws_response_free") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_response_free, 1, 0);", result);
            // WebSocket builtins
            } else if (strcmp(expr->as.ident, "__lws_ws_connect") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_connect, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__lws_ws_send_text") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_send_text, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "__lws_ws_recv") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_recv, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "__lws_ws_close") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_close, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__lws_ws_is_closed") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_is_closed, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__lws_msg_type") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_msg_type, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__lws_msg_text") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_msg_text, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__lws_msg_len") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_msg_len, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__lws_msg_free") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_msg_free, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__lws_ws_server_create") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_server_create, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "__lws_ws_server_accept") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_server_accept, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "__lws_ws_server_close") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_server_close, 1, 0);", result);
            // Socket builtins
            } else if (strcmp(expr->as.ident, "socket_create") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_socket_create, 3, 0);", result);
            // OS info builtins
            } else if (strcmp(expr->as.ident, "platform") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_platform, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "arch") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_arch, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "hostname") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_hostname, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "username") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_username, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "homedir") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_homedir, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "cpu_count") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_cpu_count, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "total_memory") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_total_memory, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "free_memory") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_free_memory, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "os_version") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_os_version, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "os_name") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_os_name, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "tmpdir") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_tmpdir, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "uptime") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_uptime, 0, 0);", result);
            // Filesystem builtins
            } else if (strcmp(expr->as.ident, "exists") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_exists, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "read_file") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_read_file, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "write_file") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_write_file, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "append_file") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_append_file, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "remove_file") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_remove_file, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "rename") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_rename, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "copy_file") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_copy_file, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "is_file") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_is_file, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "is_dir") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_is_dir, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "file_stat") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_file_stat, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "make_dir") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_make_dir, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "remove_dir") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_remove_dir, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "list_dir") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_list_dir, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "cwd") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_cwd, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "chdir") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_chdir, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "absolute_path") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_absolute_path, 1, 0);", result);
            // Unprefixed aliases for math functions (for parity with interpreter)
            } else if (strcmp(expr->as.ident, "sin") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_sin, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "cos") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_cos, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "tan") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_tan, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "asin") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_asin, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "acos") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_acos, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "atan") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atan, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "atan2") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atan2, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "sqrt") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_sqrt, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "pow") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_pow, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "exp") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_exp, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "log") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_log, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "log10") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_log10, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "log2") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_log2, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "floor") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_floor, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "ceil") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_ceil, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "round") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_round, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "trunc") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_trunc, 1, 0);", result);
            // Unprefixed aliases for environment functions (for parity with interpreter)
            } else if (strcmp(expr->as.ident, "getenv") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_getenv, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "setenv") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_setenv, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "unsetenv") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_unsetenv, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "get_pid") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_get_pid, 0, 0);", result);
            } else {
                // Check if this is an imported symbol
                ImportBinding *import_binding = NULL;
                if (ctx->current_module) {
                    import_binding = module_find_import(ctx->current_module, expr->as.ident);
                }

                if (import_binding) {
                    // Use the imported module's symbol
                    codegen_writeln(ctx, "HmlValue %s = %s%s;", result,
                                  import_binding->module_prefix, import_binding->original_name);
                } else if (ctx->current_module && !codegen_is_local(ctx, expr->as.ident)) {
                    // Not a local variable, use module prefix
                    codegen_writeln(ctx, "HmlValue %s = %s%s;", result,
                                  ctx->current_module->module_prefix, expr->as.ident);
                } else if (ctx->current_module && codegen_is_local(ctx, expr->as.ident)) {
                    // Local variable, but check if it's a module export (self-reference in closure)
                    ExportedSymbol *exp = module_find_export(ctx->current_module, expr->as.ident);
                    if (exp) {
                        // Use the mangled export name to access module-level function
                        codegen_writeln(ctx, "HmlValue %s = %s;", result, exp->mangled_name);
                    } else {
                        codegen_writeln(ctx, "HmlValue %s = %s;", result, expr->as.ident);
                    }
                } else if (codegen_is_main_var(ctx, expr->as.ident)) {
                    // Main file top-level variable - use _main_ prefix
                    // Check this BEFORE local check since function names are added as locals for tracking
                    codegen_writeln(ctx, "HmlValue %s = _main_%s;", result, expr->as.ident);
                } else {
                    // Local variable
                    codegen_writeln(ctx, "HmlValue %s = %s;", result, expr->as.ident);
                }
            }
            codegen_writeln(ctx, "hml_retain(&%s);", result);
            break;

        case EXPR_BINARY: {
            char *left = codegen_expr(ctx, expr->as.binary.left);
            char *right = codegen_expr(ctx, expr->as.binary.right);
            codegen_writeln(ctx, "HmlValue %s = hml_binary_op(%s, %s, %s);",
                          result, codegen_hml_binary_op(expr->as.binary.op), left, right);
            codegen_writeln(ctx, "hml_release(&%s);", left);
            codegen_writeln(ctx, "hml_release(&%s);", right);
            free(left);
            free(right);
            break;
        }

        case EXPR_UNARY: {
            char *operand = codegen_expr(ctx, expr->as.unary.operand);
            codegen_writeln(ctx, "HmlValue %s = hml_unary_op(%s, %s);",
                          result, codegen_hml_unary_op(expr->as.unary.op), operand);
            codegen_writeln(ctx, "hml_release(&%s);", operand);
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

        case EXPR_CALL: {
            // Check for builtin function calls
            if (expr->as.call.func->type == EXPR_IDENT) {
                const char *fn_name = expr->as.call.func->as.ident;

                // Handle print builtin
                if (strcmp(fn_name, "print") == 0 && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "hml_print(%s);", arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                    free(arg);
                    break;
                }

                // Handle eprint builtin
                if (strcmp(fn_name, "eprint") == 0 && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "hml_eprint(%s);", arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                    free(arg);
                    break;
                }

                // Handle typeof builtin
                if (strcmp(fn_name, "typeof") == 0 && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_string(hml_typeof(%s));", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

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
                    break;
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
                    break;
                }

                // Handle exec builtin for command execution
                if ((strcmp(fn_name, "exec") == 0 || strcmp(fn_name, "__exec") == 0) && expr->as.call.num_args == 1) {
                    char *cmd = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_exec(%s);", result, cmd);
                    codegen_writeln(ctx, "hml_release(&%s);", cmd);
                    free(cmd);
                    break;
                }

                // Handle open builtin for file I/O
                if (strcmp(fn_name, "open") == 0 && (expr->as.call.num_args == 1 || expr->as.call.num_args == 2)) {
                    char *path = codegen_expr(ctx, expr->as.call.args[0]);
                    if (expr->as.call.num_args == 2) {
                        char *mode = codegen_expr(ctx, expr->as.call.args[1]);
                        codegen_writeln(ctx, "HmlValue %s = hml_open(%s, %s);", result, path, mode);
                        codegen_writeln(ctx, "hml_release(&%s);", mode);
                        free(mode);
                    } else {
                        codegen_writeln(ctx, "HmlValue %s = hml_open(%s, hml_val_string(\"r\"));", result, path);
                    }
                    codegen_writeln(ctx, "hml_release(&%s);", path);
                    free(path);
                    break;
                }

                // Handle spawn builtin for async
                if (strcmp(fn_name, "spawn") == 0 && expr->as.call.num_args >= 1) {
                    char *fn_val = codegen_expr(ctx, expr->as.call.args[0]);
                    int num_spawn_args = expr->as.call.num_args - 1;

                    if (num_spawn_args > 0) {
                        // Capture counter before generating args (which may increment it)
                        int args_counter = ctx->temp_counter++;
                        // Build args array
                        codegen_writeln(ctx, "HmlValue _spawn_args%d[%d];", args_counter, num_spawn_args);
                        for (int i = 0; i < num_spawn_args; i++) {
                            char *arg = codegen_expr(ctx, expr->as.call.args[i + 1]);
                            codegen_writeln(ctx, "_spawn_args%d[%d] = %s;", args_counter, i, arg);
                            free(arg);
                        }
                        codegen_writeln(ctx, "HmlValue %s = hml_spawn(%s, _spawn_args%d, %d);",
                                      result, fn_val, args_counter, num_spawn_args);
                    } else {
                        codegen_writeln(ctx, "HmlValue %s = hml_spawn(%s, NULL, 0);", result, fn_val);
                    }
                    codegen_writeln(ctx, "hml_release(&%s);", fn_val);
                    free(fn_val);
                    break;
                }

                // Handle join builtin
                if (strcmp(fn_name, "join") == 0 && expr->as.call.num_args == 1) {
                    char *task_val = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_join(%s);", result, task_val);
                    codegen_writeln(ctx, "hml_release(&%s);", task_val);
                    free(task_val);
                    break;
                }

                // Handle detach builtin
                // detach(task) - detach an already-spawned task
                // detach(fn, args...) - spawn and immediately detach (fire-and-forget)
                if (strcmp(fn_name, "detach") == 0 && expr->as.call.num_args >= 1) {
                    if (expr->as.call.num_args == 1) {
                        // detach(task) - existing behavior
                        char *task_val = codegen_expr(ctx, expr->as.call.args[0]);
                        codegen_writeln(ctx, "hml_detach(%s);", task_val);
                        codegen_writeln(ctx, "hml_release(&%s);", task_val);
                        codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                        free(task_val);
                    } else {
                        // detach(fn, args...) - spawn and detach
                        char *fn_val = codegen_expr(ctx, expr->as.call.args[0]);
                        int num_spawn_args = expr->as.call.num_args - 1;
                        int args_counter = ctx->temp_counter++;

                        // Build args array
                        codegen_writeln(ctx, "HmlValue _detach_args%d[%d];", args_counter, num_spawn_args);
                        for (int i = 0; i < num_spawn_args; i++) {
                            char *arg = codegen_expr(ctx, expr->as.call.args[i + 1]);
                            codegen_writeln(ctx, "_detach_args%d[%d] = %s;", args_counter, i, arg);
                            free(arg);
                        }

                        // Spawn then immediately detach
                        int task_counter = ctx->temp_counter++;
                        codegen_writeln(ctx, "HmlValue _detach_task%d = hml_spawn(%s, _detach_args%d, %d);",
                                      task_counter, fn_val, args_counter, num_spawn_args);
                        codegen_writeln(ctx, "hml_detach(_detach_task%d);", task_counter);
                        codegen_writeln(ctx, "hml_release(&_detach_task%d);", task_counter);
                        codegen_writeln(ctx, "hml_release(&%s);", fn_val);
                        codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                        free(fn_val);
                    }
                    break;
                }

                // Handle task_debug_info builtin
                if (strcmp(fn_name, "task_debug_info") == 0 && expr->as.call.num_args == 1) {
                    char *task_val = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "hml_task_debug_info(%s);", task_val);
                    codegen_writeln(ctx, "hml_release(&%s);", task_val);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                    free(task_val);
                    break;
                }

                // Handle channel builtin
                if (strcmp(fn_name, "channel") == 0 && expr->as.call.num_args == 1) {
                    char *cap = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_channel(%s.as.as_i32);", result, cap);
                    codegen_writeln(ctx, "hml_release(&%s);", cap);
                    free(cap);
                    break;
                }

                // Handle signal builtin
                if (strcmp(fn_name, "signal") == 0 && expr->as.call.num_args == 2) {
                    char *signum = codegen_expr(ctx, expr->as.call.args[0]);
                    char *handler = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_signal(%s, %s);", result, signum, handler);
                    codegen_writeln(ctx, "hml_release(&%s);", signum);
                    codegen_writeln(ctx, "hml_release(&%s);", handler);
                    free(signum);
                    free(handler);
                    break;
                }

                // Handle raise builtin
                if (strcmp(fn_name, "raise") == 0 && expr->as.call.num_args == 1) {
                    char *signum = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_raise(%s);", result, signum);
                    codegen_writeln(ctx, "hml_release(&%s);", signum);
                    free(signum);
                    break;
                }

                // Handle alloc builtin
                if (strcmp(fn_name, "alloc") == 0 && expr->as.call.num_args == 1) {
                    char *size = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_alloc(hml_to_i32(%s));", result, size);
                    codegen_writeln(ctx, "hml_release(&%s);", size);
                    free(size);
                    break;
                }

                // Handle free builtin
                if (strcmp(fn_name, "free") == 0 && expr->as.call.num_args == 1) {
                    char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "hml_free(%s);", ptr);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                    codegen_writeln(ctx, "hml_release(&%s);", ptr);
                    free(ptr);
                    break;
                }

                // Handle buffer builtin
                if (strcmp(fn_name, "buffer") == 0 && expr->as.call.num_args == 1) {
                    char *size = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_buffer(hml_to_i32(%s));", result, size);
                    codegen_writeln(ctx, "hml_release(&%s);", size);
                    free(size);
                    break;
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
                    break;
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
                    break;
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
                    break;
                }

                // ========== MATH BUILTINS ==========

                // sqrt(x)
                if ((strcmp(fn_name, "sqrt") == 0 || strcmp(fn_name, "__sqrt") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_sqrt(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // sin(x)
                if ((strcmp(fn_name, "sin") == 0 || strcmp(fn_name, "__sin") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_sin(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // cos(x)
                if ((strcmp(fn_name, "cos") == 0 || strcmp(fn_name, "__cos") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_cos(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // tan(x)
                if ((strcmp(fn_name, "tan") == 0 || strcmp(fn_name, "__tan") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_tan(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // asin(x)
                if ((strcmp(fn_name, "asin") == 0 || strcmp(fn_name, "__asin") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_asin(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // acos(x)
                if ((strcmp(fn_name, "acos") == 0 || strcmp(fn_name, "__acos") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_acos(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // atan(x)
                if ((strcmp(fn_name, "atan") == 0 || strcmp(fn_name, "__atan") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_atan(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // atan2(y, x)
                if ((strcmp(fn_name, "atan2") == 0 || strcmp(fn_name, "__atan2") == 0) && expr->as.call.num_args == 2) {
                    char *y_arg = codegen_expr(ctx, expr->as.call.args[0]);
                    char *x_arg = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_atan2(%s, %s);", result, y_arg, x_arg);
                    codegen_writeln(ctx, "hml_release(&%s);", y_arg);
                    codegen_writeln(ctx, "hml_release(&%s);", x_arg);
                    free(y_arg);
                    free(x_arg);
                    break;
                }

                // floor(x)
                if ((strcmp(fn_name, "floor") == 0 || strcmp(fn_name, "__floor") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_floor(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // ceil(x)
                if ((strcmp(fn_name, "ceil") == 0 || strcmp(fn_name, "__ceil") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_ceil(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // round(x)
                if ((strcmp(fn_name, "round") == 0 || strcmp(fn_name, "__round") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_round(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // trunc(x)
                if ((strcmp(fn_name, "trunc") == 0 || strcmp(fn_name, "__trunc") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_trunc(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // abs(x)
                if ((strcmp(fn_name, "abs") == 0 || strcmp(fn_name, "__abs") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_abs(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // pow(base, exp)
                if ((strcmp(fn_name, "pow") == 0 || strcmp(fn_name, "__pow") == 0) && expr->as.call.num_args == 2) {
                    char *base = codegen_expr(ctx, expr->as.call.args[0]);
                    char *exp_arg = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_pow(%s, %s);", result, base, exp_arg);
                    codegen_writeln(ctx, "hml_release(&%s);", base);
                    codegen_writeln(ctx, "hml_release(&%s);", exp_arg);
                    free(base);
                    free(exp_arg);
                    break;
                }

                // exp(x)
                if ((strcmp(fn_name, "exp") == 0 || strcmp(fn_name, "__exp") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_exp(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // log(x)
                if ((strcmp(fn_name, "log") == 0 || strcmp(fn_name, "__log") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_log(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // log10(x)
                if ((strcmp(fn_name, "log10") == 0 || strcmp(fn_name, "__log10") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_log10(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // log2(x)
                if ((strcmp(fn_name, "log2") == 0 || strcmp(fn_name, "__log2") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_log2(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // min(a, b)
                if ((strcmp(fn_name, "min") == 0 || strcmp(fn_name, "__min") == 0) && expr->as.call.num_args == 2) {
                    char *a = codegen_expr(ctx, expr->as.call.args[0]);
                    char *b = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_min(%s, %s);", result, a, b);
                    codegen_writeln(ctx, "hml_release(&%s);", a);
                    codegen_writeln(ctx, "hml_release(&%s);", b);
                    free(a);
                    free(b);
                    break;
                }

                // max(a, b)
                if ((strcmp(fn_name, "max") == 0 || strcmp(fn_name, "__max") == 0) && expr->as.call.num_args == 2) {
                    char *a = codegen_expr(ctx, expr->as.call.args[0]);
                    char *b = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_max(%s, %s);", result, a, b);
                    codegen_writeln(ctx, "hml_release(&%s);", a);
                    codegen_writeln(ctx, "hml_release(&%s);", b);
                    free(a);
                    free(b);
                    break;
                }

                // rand()
                if ((strcmp(fn_name, "rand") == 0 || strcmp(fn_name, "__rand") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_rand();", result);
                    break;
                }

                // seed(value)
                if ((strcmp(fn_name, "seed") == 0 || strcmp(fn_name, "__seed") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "hml_seed(%s);", arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                    free(arg);
                    break;
                }

                // rand_range(min, max) - also __rand_range
                if ((strcmp(fn_name, "rand_range") == 0 || strcmp(fn_name, "__rand_range") == 0) && expr->as.call.num_args == 2) {
                    char *min_arg = codegen_expr(ctx, expr->as.call.args[0]);
                    char *max_arg = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_rand_range(%s, %s);", result, min_arg, max_arg);
                    codegen_writeln(ctx, "hml_release(&%s);", min_arg);
                    codegen_writeln(ctx, "hml_release(&%s);", max_arg);
                    free(min_arg);
                    free(max_arg);
                    break;
                }

                // clamp(value, min, max) - also __clamp
                if ((strcmp(fn_name, "clamp") == 0 || strcmp(fn_name, "__clamp") == 0) && expr->as.call.num_args == 3) {
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
                    break;
                }

                // ========== TIME BUILTINS ==========
                // Note: For unprefixed builtins, only use builtin if NOT a local/import

                // now() - but NOT if 'now' is a local/import (e.g., from @stdlib/datetime)
                if ((strcmp(fn_name, "__now") == 0 ||
                     (strcmp(fn_name, "now") == 0 && !codegen_is_local(ctx, fn_name))) &&
                    expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_now();", result);
                    break;
                }

                // time_ms() - but NOT if 'time_ms' is a local/import
                if ((strcmp(fn_name, "__time_ms") == 0 ||
                     (strcmp(fn_name, "time_ms") == 0 && !codegen_is_local(ctx, fn_name))) &&
                    expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_time_ms();", result);
                    break;
                }

                // clock() - but NOT if 'clock' is a local/import
                if ((strcmp(fn_name, "__clock") == 0 ||
                     (strcmp(fn_name, "clock") == 0 && !codegen_is_local(ctx, fn_name))) &&
                    expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_clock();", result);
                    break;
                }

                // sleep(seconds) - but NOT if 'sleep' is a local/import
                if ((strcmp(fn_name, "__sleep") == 0 ||
                     (strcmp(fn_name, "sleep") == 0 && !codegen_is_local(ctx, fn_name))) &&
                    expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "hml_sleep(%s);", arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                    free(arg);
                    break;
                }

                // ========== DATETIME BUILTINS ==========

                // localtime(timestamp)
                if ((strcmp(fn_name, "localtime") == 0 || strcmp(fn_name, "__localtime") == 0) && expr->as.call.num_args == 1) {
                    char *ts = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_localtime(%s);", result, ts);
                    codegen_writeln(ctx, "hml_release(&%s);", ts);
                    free(ts);
                    break;
                }

                // gmtime(timestamp)
                if ((strcmp(fn_name, "gmtime") == 0 || strcmp(fn_name, "__gmtime") == 0) && expr->as.call.num_args == 1) {
                    char *ts = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_gmtime(%s);", result, ts);
                    codegen_writeln(ctx, "hml_release(&%s);", ts);
                    free(ts);
                    break;
                }

                // mktime(time_obj)
                if ((strcmp(fn_name, "mktime") == 0 || strcmp(fn_name, "__mktime") == 0) && expr->as.call.num_args == 1) {
                    char *obj = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_mktime(%s);", result, obj);
                    codegen_writeln(ctx, "hml_release(&%s);", obj);
                    free(obj);
                    break;
                }

                // strftime(format, time_obj)
                if ((strcmp(fn_name, "strftime") == 0 || strcmp(fn_name, "__strftime") == 0) && expr->as.call.num_args == 2) {
                    char *fmt = codegen_expr(ctx, expr->as.call.args[0]);
                    char *obj = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_strftime(%s, %s);", result, fmt, obj);
                    codegen_writeln(ctx, "hml_release(&%s);", fmt);
                    codegen_writeln(ctx, "hml_release(&%s);", obj);
                    free(fmt);
                    free(obj);
                    break;
                }

                // ========== ENVIRONMENT BUILTINS ==========

                // getenv(name)
                if ((strcmp(fn_name, "getenv") == 0 || strcmp(fn_name, "__getenv") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_getenv(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // setenv(name, value)
                if ((strcmp(fn_name, "setenv") == 0 || strcmp(fn_name, "__setenv") == 0) && expr->as.call.num_args == 2) {
                    char *name_arg = codegen_expr(ctx, expr->as.call.args[0]);
                    char *value_arg = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "hml_setenv(%s, %s);", name_arg, value_arg);
                    codegen_writeln(ctx, "hml_release(&%s);", name_arg);
                    codegen_writeln(ctx, "hml_release(&%s);", value_arg);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                    free(name_arg);
                    free(value_arg);
                    break;
                }

                // unsetenv(name)
                if ((strcmp(fn_name, "unsetenv") == 0 || strcmp(fn_name, "__unsetenv") == 0) && expr->as.call.num_args == 1) {
                    char *name_arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "hml_unsetenv(%s);", name_arg);
                    codegen_writeln(ctx, "hml_release(&%s);", name_arg);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                    free(name_arg);
                    break;
                }

                // exit(code)
                if ((strcmp(fn_name, "exit") == 0 || strcmp(fn_name, "__exit") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "hml_exit(%s);", arg);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                    free(arg);
                    break;
                }

                // abort()
                if ((strcmp(fn_name, "abort") == 0 || strcmp(fn_name, "__abort") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "hml_abort();");
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                    break;
                }

                // get_pid()
                if ((strcmp(fn_name, "get_pid") == 0 || strcmp(fn_name, "__get_pid") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_get_pid();", result);
                    break;
                }

                // ========== PROCESS MANAGEMENT BUILTINS ==========

                // getppid()
                if ((strcmp(fn_name, "getppid") == 0 || strcmp(fn_name, "__getppid") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_getppid();", result);
                    break;
                }

                // getuid()
                if ((strcmp(fn_name, "getuid") == 0 || strcmp(fn_name, "__getuid") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_getuid();", result);
                    break;
                }

                // geteuid()
                if ((strcmp(fn_name, "geteuid") == 0 || strcmp(fn_name, "__geteuid") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_geteuid();", result);
                    break;
                }

                // getgid()
                if ((strcmp(fn_name, "getgid") == 0 || strcmp(fn_name, "__getgid") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_getgid();", result);
                    break;
                }

                // getegid()
                if ((strcmp(fn_name, "getegid") == 0 || strcmp(fn_name, "__getegid") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_getegid();", result);
                    break;
                }

                // fork()
                if ((strcmp(fn_name, "fork") == 0 || strcmp(fn_name, "__fork") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_fork();", result);
                    break;
                }

                // wait()
                if ((strcmp(fn_name, "wait") == 0 || strcmp(fn_name, "__wait") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_wait();", result);
                    break;
                }

                // waitpid(pid, options)
                if ((strcmp(fn_name, "waitpid") == 0 || strcmp(fn_name, "__waitpid") == 0) && expr->as.call.num_args == 2) {
                    char *pid = codegen_expr(ctx, expr->as.call.args[0]);
                    char *opts = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_waitpid(%s, %s);", result, pid, opts);
                    codegen_writeln(ctx, "hml_release(&%s);", pid);
                    codegen_writeln(ctx, "hml_release(&%s);", opts);
                    free(pid);
                    free(opts);
                    break;
                }

                // kill(pid, sig)
                if ((strcmp(fn_name, "kill") == 0 || strcmp(fn_name, "__kill") == 0) && expr->as.call.num_args == 2) {
                    char *pid = codegen_expr(ctx, expr->as.call.args[0]);
                    char *sig = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_kill(%s, %s);", result, pid, sig);
                    codegen_writeln(ctx, "hml_release(&%s);", pid);
                    codegen_writeln(ctx, "hml_release(&%s);", sig);
                    free(pid);
                    free(sig);
                    break;
                }

                // ========== I/O BUILTINS ==========

                // read_line()
                if ((strcmp(fn_name, "read_line") == 0 || strcmp(fn_name, "__read_line") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_read_line();", result);
                    break;
                }

                // ========== TYPE BUILTINS ==========

                // sizeof(type_name)
                if ((strcmp(fn_name, "sizeof") == 0 || strcmp(fn_name, "__sizeof") == 0) && expr->as.call.num_args == 1) {
                    // Check if argument is a type name identifier
                    Expr *arg_expr = expr->as.call.args[0];
                    if (arg_expr->type == EXPR_IDENT) {
                        const char *type_name = arg_expr->as.ident;
                        // List of valid type names
                        if (strcmp(type_name, "i8") == 0 || strcmp(type_name, "i16") == 0 ||
                            strcmp(type_name, "i32") == 0 || strcmp(type_name, "i64") == 0 ||
                            strcmp(type_name, "u8") == 0 || strcmp(type_name, "u16") == 0 ||
                            strcmp(type_name, "u32") == 0 || strcmp(type_name, "u64") == 0 ||
                            strcmp(type_name, "f32") == 0 || strcmp(type_name, "f64") == 0 ||
                            strcmp(type_name, "bool") == 0 || strcmp(type_name, "ptr") == 0 ||
                            strcmp(type_name, "rune") == 0 || strcmp(type_name, "byte") == 0 ||
                            strcmp(type_name, "integer") == 0 || strcmp(type_name, "number") == 0) {
                            // Type name - convert to string literal
                            char *arg_temp = codegen_temp(ctx);
                            codegen_writeln(ctx, "HmlValue %s = hml_val_string(\"%s\");", arg_temp, type_name);
                            codegen_writeln(ctx, "HmlValue %s = hml_sizeof(%s);", result, arg_temp);
                            codegen_writeln(ctx, "hml_release(&%s);", arg_temp);
                            free(arg_temp);
                            break;
                        }
                    }
                    // Fall through: evaluate as expression (for dynamic type checking)
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_sizeof(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // talloc(type_name, count)
                if ((strcmp(fn_name, "talloc") == 0 || strcmp(fn_name, "__talloc") == 0) && expr->as.call.num_args == 2) {
                    // Check if first argument is a type name identifier
                    Expr *type_expr = expr->as.call.args[0];
                    char *type_arg = NULL;
                    int type_is_temp = 0;
                    if (type_expr->type == EXPR_IDENT) {
                        const char *type_name = type_expr->as.ident;
                        // List of valid type names
                        if (strcmp(type_name, "i8") == 0 || strcmp(type_name, "i16") == 0 ||
                            strcmp(type_name, "i32") == 0 || strcmp(type_name, "i64") == 0 ||
                            strcmp(type_name, "u8") == 0 || strcmp(type_name, "u16") == 0 ||
                            strcmp(type_name, "u32") == 0 || strcmp(type_name, "u64") == 0 ||
                            strcmp(type_name, "f32") == 0 || strcmp(type_name, "f64") == 0 ||
                            strcmp(type_name, "bool") == 0 || strcmp(type_name, "ptr") == 0 ||
                            strcmp(type_name, "rune") == 0 || strcmp(type_name, "byte") == 0 ||
                            strcmp(type_name, "integer") == 0 || strcmp(type_name, "number") == 0) {
                            // Type name - convert to string literal
                            type_arg = codegen_temp(ctx);
                            type_is_temp = 1;
                            codegen_writeln(ctx, "HmlValue %s = hml_val_string(\"%s\");", type_arg, type_name);
                        }
                    }
                    if (!type_arg) {
                        type_arg = codegen_expr(ctx, type_expr);
                    }
                    char *count_arg = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_talloc(%s, %s);", result, type_arg, count_arg);
                    codegen_writeln(ctx, "hml_release(&%s);", type_arg);
                    codegen_writeln(ctx, "hml_release(&%s);", count_arg);
                    if (type_is_temp) {
                        free(type_arg);
                    } else {
                        free(type_arg);
                    }
                    free(count_arg);
                    break;
                }

                // ========== SOCKET BUILTINS ==========

                // socket_create(domain, type, protocol)
                if ((strcmp(fn_name, "socket_create") == 0 || strcmp(fn_name, "__socket_create") == 0) && expr->as.call.num_args == 3) {
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
                    break;
                }

                // dns_resolve(hostname)
                if ((strcmp(fn_name, "dns_resolve") == 0 || strcmp(fn_name, "__dns_resolve") == 0) && expr->as.call.num_args == 1) {
                    char *hostname = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_dns_resolve(%s);", result, hostname);
                    codegen_writeln(ctx, "hml_release(&%s);", hostname);
                    free(hostname);
                    break;
                }

                // ========== OS INFO BUILTINS ==========

                // platform()
                if ((strcmp(fn_name, "platform") == 0 || strcmp(fn_name, "__platform") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_platform();", result);
                    break;
                }

                // arch()
                if ((strcmp(fn_name, "arch") == 0 || strcmp(fn_name, "__arch") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_arch();", result);
                    break;
                }

                // hostname()
                if ((strcmp(fn_name, "hostname") == 0 || strcmp(fn_name, "__hostname") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_hostname();", result);
                    break;
                }

                // username()
                if ((strcmp(fn_name, "username") == 0 || strcmp(fn_name, "__username") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_username();", result);
                    break;
                }

                // homedir()
                if ((strcmp(fn_name, "homedir") == 0 || strcmp(fn_name, "__homedir") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_homedir();", result);
                    break;
                }

                // cpu_count()
                if ((strcmp(fn_name, "cpu_count") == 0 || strcmp(fn_name, "__cpu_count") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_cpu_count();", result);
                    break;
                }

                // total_memory()
                if ((strcmp(fn_name, "total_memory") == 0 || strcmp(fn_name, "__total_memory") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_total_memory();", result);
                    break;
                }

                // free_memory()
                if ((strcmp(fn_name, "free_memory") == 0 || strcmp(fn_name, "__free_memory") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_free_memory();", result);
                    break;
                }

                // os_version()
                if ((strcmp(fn_name, "os_version") == 0 || strcmp(fn_name, "__os_version") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_os_version();", result);
                    break;
                }

                // os_name()
                if ((strcmp(fn_name, "os_name") == 0 || strcmp(fn_name, "__os_name") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_os_name();", result);
                    break;
                }

                // tmpdir()
                if ((strcmp(fn_name, "tmpdir") == 0 || strcmp(fn_name, "__tmpdir") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_tmpdir();", result);
                    break;
                }

                // uptime()
                if ((strcmp(fn_name, "uptime") == 0 || strcmp(fn_name, "__uptime") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_uptime();", result);
                    break;
                }

                // ========== COMPRESSION BUILTINS ==========

                // zlib_compress(data, level)
                if ((strcmp(fn_name, "zlib_compress") == 0 || strcmp(fn_name, "__zlib_compress") == 0) && expr->as.call.num_args == 2) {
                    char *data = codegen_expr(ctx, expr->as.call.args[0]);
                    char *level = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_zlib_compress(%s, %s);", result, data, level);
                    codegen_writeln(ctx, "hml_release(&%s);", data);
                    codegen_writeln(ctx, "hml_release(&%s);", level);
                    free(data);
                    free(level);
                    break;
                }

                // zlib_decompress(data, max_size)
                if ((strcmp(fn_name, "zlib_decompress") == 0 || strcmp(fn_name, "__zlib_decompress") == 0) && expr->as.call.num_args == 2) {
                    char *data = codegen_expr(ctx, expr->as.call.args[0]);
                    char *max_size = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_zlib_decompress(%s, %s);", result, data, max_size);
                    codegen_writeln(ctx, "hml_release(&%s);", data);
                    codegen_writeln(ctx, "hml_release(&%s);", max_size);
                    free(data);
                    free(max_size);
                    break;
                }

                // gzip_compress(data, level)
                if ((strcmp(fn_name, "gzip_compress") == 0 || strcmp(fn_name, "__gzip_compress") == 0) && expr->as.call.num_args == 2) {
                    char *data = codegen_expr(ctx, expr->as.call.args[0]);
                    char *level = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_gzip_compress(%s, %s);", result, data, level);
                    codegen_writeln(ctx, "hml_release(&%s);", data);
                    codegen_writeln(ctx, "hml_release(&%s);", level);
                    free(data);
                    free(level);
                    break;
                }

                // gzip_decompress(data, max_size)
                if ((strcmp(fn_name, "gzip_decompress") == 0 || strcmp(fn_name, "__gzip_decompress") == 0) && expr->as.call.num_args == 2) {
                    char *data = codegen_expr(ctx, expr->as.call.args[0]);
                    char *max_size = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_gzip_decompress(%s, %s);", result, data, max_size);
                    codegen_writeln(ctx, "hml_release(&%s);", data);
                    codegen_writeln(ctx, "hml_release(&%s);", max_size);
                    free(data);
                    free(max_size);
                    break;
                }

                // zlib_compress_bound(source_len)
                if ((strcmp(fn_name, "zlib_compress_bound") == 0 || strcmp(fn_name, "__zlib_compress_bound") == 0) && expr->as.call.num_args == 1) {
                    char *len = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_zlib_compress_bound(%s);", result, len);
                    codegen_writeln(ctx, "hml_release(&%s);", len);
                    free(len);
                    break;
                }

                // crc32(data)
                if ((strcmp(fn_name, "crc32") == 0 || strcmp(fn_name, "__crc32") == 0) && expr->as.call.num_args == 1) {
                    char *data = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_crc32_val(%s);", result, data);
                    codegen_writeln(ctx, "hml_release(&%s);", data);
                    free(data);
                    break;
                }

                // adler32(data)
                if ((strcmp(fn_name, "adler32") == 0 || strcmp(fn_name, "__adler32") == 0) && expr->as.call.num_args == 1) {
                    char *data = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_adler32_val(%s);", result, data);
                    codegen_writeln(ctx, "hml_release(&%s);", data);
                    free(data);
                    break;
                }

                // ========== STRING UTILITY BUILTINS ==========

                // to_string(value)
                if (strcmp(fn_name, "to_string") == 0 && expr->as.call.num_args == 1) {
                    char *val = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_to_string(%s);", result, val);
                    codegen_writeln(ctx, "hml_release(&%s);", val);
                    free(val);
                    break;
                }

                // string_byte_length(str)
                if (strcmp(fn_name, "string_byte_length") == 0 && expr->as.call.num_args == 1) {
                    char *str = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_string_byte_length(%s);", result, str);
                    codegen_writeln(ctx, "hml_release(&%s);", str);
                    free(str);
                    break;
                }

                // strerror()
                if (strcmp(fn_name, "strerror") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_strerror();", result);
                    break;
                }

                // string_to_cstr(str)
                if (strcmp(fn_name, "string_to_cstr") == 0 && expr->as.call.num_args == 1) {
                    char *str = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_string_to_cstr(%s);", result, str);
                    codegen_writeln(ctx, "hml_release(&%s);", str);
                    free(str);
                    break;
                }

                // cstr_to_string(ptr)
                if (strcmp(fn_name, "cstr_to_string") == 0 && expr->as.call.num_args == 1) {
                    char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_cstr_to_string(%s);", result, ptr);
                    codegen_writeln(ctx, "hml_release(&%s);", ptr);
                    free(ptr);
                    break;
                }

                // ========== INTERNAL HELPER BUILTINS ==========

                // read_u32(buffer) - read 32-bit unsigned int from buffer
                if ((strcmp(fn_name, "read_u32") == 0 || strcmp(fn_name, "__read_u32") == 0) && expr->as.call.num_args == 1) {
                    char *buf = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_read_u32(%s);", result, buf);
                    codegen_writeln(ctx, "hml_release(&%s);", buf);
                    free(buf);
                    break;
                }

                // read_u64(buffer) - read 64-bit unsigned int from buffer
                if ((strcmp(fn_name, "read_u64") == 0 || strcmp(fn_name, "__read_u64") == 0) && expr->as.call.num_args == 1) {
                    char *buf = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_read_u64(%s);", result, buf);
                    codegen_writeln(ctx, "hml_release(&%s);", buf);
                    free(buf);
                    break;
                }

                // ========== HTTP/WEBSOCKET BUILTINS ==========

                // __lws_http_get(url)
                if (strcmp(fn_name, "__lws_http_get") == 0 && expr->as.call.num_args == 1) {
                    char *url = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_http_get(%s);", result, url);
                    codegen_writeln(ctx, "hml_release(&%s);", url);
                    free(url);
                    break;
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
                    break;
                }

                // __lws_response_status(resp)
                if (strcmp(fn_name, "__lws_response_status") == 0 && expr->as.call.num_args == 1) {
                    char *resp = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_response_status(%s);", result, resp);
                    codegen_writeln(ctx, "hml_release(&%s);", resp);
                    free(resp);
                    break;
                }

                // __lws_response_body(resp)
                if (strcmp(fn_name, "__lws_response_body") == 0 && expr->as.call.num_args == 1) {
                    char *resp = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_response_body(%s);", result, resp);
                    codegen_writeln(ctx, "hml_release(&%s);", resp);
                    free(resp);
                    break;
                }

                // __lws_response_headers(resp)
                if (strcmp(fn_name, "__lws_response_headers") == 0 && expr->as.call.num_args == 1) {
                    char *resp = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_response_headers(%s);", result, resp);
                    codegen_writeln(ctx, "hml_release(&%s);", resp);
                    free(resp);
                    break;
                }

                // __lws_response_free(resp)
                if (strcmp(fn_name, "__lws_response_free") == 0 && expr->as.call.num_args == 1) {
                    char *resp = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_response_free(%s);", result, resp);
                    codegen_writeln(ctx, "hml_release(&%s);", resp);
                    free(resp);
                    break;
                }

                // ========== WEBSOCKET BUILTINS ==========

                // __lws_ws_connect(url)
                if (strcmp(fn_name, "__lws_ws_connect") == 0 && expr->as.call.num_args == 1) {
                    char *url = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_ws_connect(%s);", result, url);
                    codegen_writeln(ctx, "hml_release(&%s);", url);
                    free(url);
                    break;
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
                    break;
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
                    break;
                }

                // __lws_ws_close(conn)
                if (strcmp(fn_name, "__lws_ws_close") == 0 && expr->as.call.num_args == 1) {
                    char *conn = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_ws_close(%s);", result, conn);
                    codegen_writeln(ctx, "hml_release(&%s);", conn);
                    free(conn);
                    break;
                }

                // __lws_ws_is_closed(conn)
                if (strcmp(fn_name, "__lws_ws_is_closed") == 0 && expr->as.call.num_args == 1) {
                    char *conn = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_ws_is_closed(%s);", result, conn);
                    codegen_writeln(ctx, "hml_release(&%s);", conn);
                    free(conn);
                    break;
                }

                // __lws_msg_type(msg)
                if (strcmp(fn_name, "__lws_msg_type") == 0 && expr->as.call.num_args == 1) {
                    char *msg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_msg_type(%s);", result, msg);
                    codegen_writeln(ctx, "hml_release(&%s);", msg);
                    free(msg);
                    break;
                }

                // __lws_msg_text(msg)
                if (strcmp(fn_name, "__lws_msg_text") == 0 && expr->as.call.num_args == 1) {
                    char *msg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_msg_text(%s);", result, msg);
                    codegen_writeln(ctx, "hml_release(&%s);", msg);
                    free(msg);
                    break;
                }

                // __lws_msg_len(msg)
                if (strcmp(fn_name, "__lws_msg_len") == 0 && expr->as.call.num_args == 1) {
                    char *msg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_msg_len(%s);", result, msg);
                    codegen_writeln(ctx, "hml_release(&%s);", msg);
                    free(msg);
                    break;
                }

                // __lws_msg_free(msg)
                if (strcmp(fn_name, "__lws_msg_free") == 0 && expr->as.call.num_args == 1) {
                    char *msg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_msg_free(%s);", result, msg);
                    codegen_writeln(ctx, "hml_release(&%s);", msg);
                    free(msg);
                    break;
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
                    break;
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
                    break;
                }

                // __lws_ws_server_close(server)
                if (strcmp(fn_name, "__lws_ws_server_close") == 0 && expr->as.call.num_args == 1) {
                    char *server = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_ws_server_close(%s);", result, server);
                    codegen_writeln(ctx, "hml_release(&%s);", server);
                    free(server);
                    break;
                }

                // ========== FILESYSTEM BUILTINS ==========

                // exists(path)
                if (strcmp(fn_name, "exists") == 0 && expr->as.call.num_args == 1) {
                    char *path = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_exists(%s);", result, path);
                    codegen_writeln(ctx, "hml_release(&%s);", path);
                    free(path);
                    break;
                }

                // read_file(path)
                if (strcmp(fn_name, "read_file") == 0 && expr->as.call.num_args == 1) {
                    char *path = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_read_file(%s);", result, path);
                    codegen_writeln(ctx, "hml_release(&%s);", path);
                    free(path);
                    break;
                }

                // write_file(path, content)
                if (strcmp(fn_name, "write_file") == 0 && expr->as.call.num_args == 2) {
                    char *path = codegen_expr(ctx, expr->as.call.args[0]);
                    char *content = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_write_file(%s, %s);", result, path, content);
                    codegen_writeln(ctx, "hml_release(&%s);", path);
                    codegen_writeln(ctx, "hml_release(&%s);", content);
                    free(path);
                    free(content);
                    break;
                }

                // append_file(path, content)
                if (strcmp(fn_name, "append_file") == 0 && expr->as.call.num_args == 2) {
                    char *path = codegen_expr(ctx, expr->as.call.args[0]);
                    char *content = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_append_file(%s, %s);", result, path, content);
                    codegen_writeln(ctx, "hml_release(&%s);", path);
                    codegen_writeln(ctx, "hml_release(&%s);", content);
                    free(path);
                    free(content);
                    break;
                }

                // remove_file(path)
                if (strcmp(fn_name, "remove_file") == 0 && expr->as.call.num_args == 1) {
                    char *path = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_remove_file(%s);", result, path);
                    codegen_writeln(ctx, "hml_release(&%s);", path);
                    free(path);
                    break;
                }

                // rename(old_path, new_path)
                if (strcmp(fn_name, "rename") == 0 && expr->as.call.num_args == 2) {
                    char *old_path = codegen_expr(ctx, expr->as.call.args[0]);
                    char *new_path = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_rename_file(%s, %s);", result, old_path, new_path);
                    codegen_writeln(ctx, "hml_release(&%s);", old_path);
                    codegen_writeln(ctx, "hml_release(&%s);", new_path);
                    free(old_path);
                    free(new_path);
                    break;
                }

                // copy_file(src, dest)
                if (strcmp(fn_name, "copy_file") == 0 && expr->as.call.num_args == 2) {
                    char *src = codegen_expr(ctx, expr->as.call.args[0]);
                    char *dest = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_copy_file(%s, %s);", result, src, dest);
                    codegen_writeln(ctx, "hml_release(&%s);", src);
                    codegen_writeln(ctx, "hml_release(&%s);", dest);
                    free(src);
                    free(dest);
                    break;
                }

                // is_file(path)
                if (strcmp(fn_name, "is_file") == 0 && expr->as.call.num_args == 1) {
                    char *path = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_is_file(%s);", result, path);
                    codegen_writeln(ctx, "hml_release(&%s);", path);
                    free(path);
                    break;
                }

                // is_dir(path)
                if (strcmp(fn_name, "is_dir") == 0 && expr->as.call.num_args == 1) {
                    char *path = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_is_dir(%s);", result, path);
                    codegen_writeln(ctx, "hml_release(&%s);", path);
                    free(path);
                    break;
                }

                // file_stat(path)
                if (strcmp(fn_name, "file_stat") == 0 && expr->as.call.num_args == 1) {
                    char *path = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_file_stat(%s);", result, path);
                    codegen_writeln(ctx, "hml_release(&%s);", path);
                    free(path);
                    break;
                }

                // make_dir(path, [mode])
                if (strcmp(fn_name, "make_dir") == 0 && (expr->as.call.num_args == 1 || expr->as.call.num_args == 2)) {
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
                    break;
                }

                // remove_dir(path)
                if (strcmp(fn_name, "remove_dir") == 0 && expr->as.call.num_args == 1) {
                    char *path = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_remove_dir(%s);", result, path);
                    codegen_writeln(ctx, "hml_release(&%s);", path);
                    free(path);
                    break;
                }

                // list_dir(path)
                if (strcmp(fn_name, "list_dir") == 0 && expr->as.call.num_args == 1) {
                    char *path = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_list_dir(%s);", result, path);
                    codegen_writeln(ctx, "hml_release(&%s);", path);
                    free(path);
                    break;
                }

                // cwd()
                if (strcmp(fn_name, "cwd") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_cwd();", result);
                    break;
                }

                // chdir(path)
                if (strcmp(fn_name, "chdir") == 0 && expr->as.call.num_args == 1) {
                    char *path = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_chdir(%s);", result, path);
                    codegen_writeln(ctx, "hml_release(&%s);", path);
                    free(path);
                    break;
                }

                // absolute_path(path)
                if (strcmp(fn_name, "absolute_path") == 0 && expr->as.call.num_args == 1) {
                    char *path = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_absolute_path(%s);", result, path);
                    codegen_writeln(ctx, "hml_release(&%s);", path);
                    free(path);
                    break;
                }

                // Handle user-defined function by name (hml_fn_<name>)
                // Main file functions should use generic call path (hml_call_function)
                // to properly handle optional parameters with defaults
                if (codegen_is_main_var(ctx, fn_name)) {
                    // Main file variable (function def or closure) - use generic call path
                    // This ensures optional parameters get default values via runtime handling
                    // Fall through to generic handling
                } else if (codegen_is_local(ctx, fn_name)) {
                    // It's a local function variable - call through hml_call_function
                    // Fall through to generic handling
                } else {
                    // Check if this is an imported function
                    ImportBinding *import_binding = NULL;
                    if (ctx->current_module) {
                        import_binding = module_find_import(ctx->current_module, fn_name);
                    } else {
                        // Check main file imports
                        import_binding = codegen_find_main_import(ctx, fn_name);
                    }

                    // Try to call as hml_fn_<name> directly with NULL for closure env
                    char **arg_temps = malloc(expr->as.call.num_args * sizeof(char*));
                    for (int i = 0; i < expr->as.call.num_args; i++) {
                        arg_temps[i] = codegen_expr(ctx, expr->as.call.args[i]);
                    }

                    codegen_write(ctx, "");
                    codegen_indent(ctx);
                    // All functions use closure env as first param for uniform calling convention
                    if (import_binding) {
                        // Use the mangled function name from the import
                        // module_prefix is like "_mod1_", original_name is the export name
                        fprintf(ctx->output, "HmlValue %s = %sfn_%s(NULL", result,
                                import_binding->module_prefix, import_binding->original_name);
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
                    for (int i = 0; i < expr->as.call.num_args; i++) {
                        fprintf(ctx->output, ", %s", arg_temps[i]);
                    }
                    fprintf(ctx->output, ");\n");

                    for (int i = 0; i < expr->as.call.num_args; i++) {
                        codegen_writeln(ctx, "hml_release(&%s);", arg_temps[i]);
                        free(arg_temps[i]);
                    }
                    free(arg_temps);
                    break;
                }
            }

            // Handle method calls: obj.method(args)
            if (expr->as.call.func->type == EXPR_GET_PROPERTY) {
                Expr *obj_expr = expr->as.call.func->as.get_property.object;
                const char *method = expr->as.call.func->as.get_property.property;

                // Evaluate the object
                char *obj_val = codegen_expr(ctx, obj_expr);

                // Evaluate arguments
                char **arg_temps = malloc(expr->as.call.num_args * sizeof(char*));
                for (int i = 0; i < expr->as.call.num_args; i++) {
                    arg_temps[i] = codegen_expr(ctx, expr->as.call.args[i]);
                }

                // Methods that work on both strings and arrays - need runtime type check
                if (strcmp(method, "slice") == 0 && expr->as.call.num_args == 2) {
                    codegen_writeln(ctx, "HmlValue %s;", result);
                    codegen_writeln(ctx, "if (%s.type == HML_VAL_STRING) {", obj_val);
                    codegen_writeln(ctx, "    %s = hml_string_slice(%s, %s, %s);",
                                  result, obj_val, arg_temps[0], arg_temps[1]);
                    codegen_writeln(ctx, "} else {");
                    codegen_writeln(ctx, "    %s = hml_array_slice(%s, %s, %s);",
                                  result, obj_val, arg_temps[0], arg_temps[1]);
                    codegen_writeln(ctx, "}");
                } else if ((strcmp(method, "find") == 0 || strcmp(method, "indexOf") == 0)
                           && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s;", result);
                    codegen_writeln(ctx, "if (%s.type == HML_VAL_STRING) {", obj_val);
                    codegen_writeln(ctx, "    %s = hml_string_find(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                    codegen_writeln(ctx, "} else {");
                    codegen_writeln(ctx, "    %s = hml_array_find(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                    codegen_writeln(ctx, "}");
                } else if (strcmp(method, "contains") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s;", result);
                    codegen_writeln(ctx, "if (%s.type == HML_VAL_STRING) {", obj_val);
                    codegen_writeln(ctx, "    %s = hml_string_contains(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                    codegen_writeln(ctx, "} else {");
                    codegen_writeln(ctx, "    %s = hml_array_contains(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                    codegen_writeln(ctx, "}");
                // String-only methods
                } else if (strcmp(method, "substr") == 0 && expr->as.call.num_args == 2) {
                    codegen_writeln(ctx, "HmlValue %s = hml_string_substr(%s, %s, %s);",
                                  result, obj_val, arg_temps[0], arg_temps[1]);
                } else if (strcmp(method, "split") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_string_split(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if (strcmp(method, "trim") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_string_trim(%s);", result, obj_val);
                } else if (strcmp(method, "to_upper") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_string_to_upper(%s);", result, obj_val);
                } else if (strcmp(method, "to_lower") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_string_to_lower(%s);", result, obj_val);
                } else if (strcmp(method, "starts_with") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_string_starts_with(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if (strcmp(method, "ends_with") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_string_ends_with(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if (strcmp(method, "replace") == 0 && expr->as.call.num_args == 2) {
                    codegen_writeln(ctx, "HmlValue %s = hml_string_replace(%s, %s, %s);",
                                  result, obj_val, arg_temps[0], arg_temps[1]);
                } else if (strcmp(method, "replace_all") == 0 && expr->as.call.num_args == 2) {
                    codegen_writeln(ctx, "HmlValue %s = hml_string_replace_all(%s, %s, %s);",
                                  result, obj_val, arg_temps[0], arg_temps[1]);
                } else if (strcmp(method, "repeat") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_string_repeat(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if (strcmp(method, "char_at") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_string_char_at(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if (strcmp(method, "byte_at") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_string_byte_at(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                // Array methods - with runtime type check to also support object methods
                } else if (strcmp(method, "push") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s;", result);
                    codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY) {", obj_val);
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "hml_array_push(%s, %s);", obj_val, arg_temps[0]);
                    codegen_writeln(ctx, "%s = hml_val_null();", result);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "} else {");
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "HmlValue _push_args[1] = {%s};", arg_temps[0]);
                    codegen_writeln(ctx, "%s = hml_call_method(%s, \"push\", _push_args, 1);", result, obj_val);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "}");
                } else if (strcmp(method, "pop") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s;", result);
                    codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY) {", obj_val);
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "%s = hml_array_pop(%s);", result, obj_val);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "} else {");
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "%s = hml_call_method(%s, \"pop\", NULL, 0);", result, obj_val);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "}");
                } else if (strcmp(method, "shift") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s;", result);
                    codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY) {", obj_val);
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "%s = hml_array_shift(%s);", result, obj_val);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "} else {");
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "%s = hml_call_method(%s, \"shift\", NULL, 0);", result, obj_val);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "}");
                } else if (strcmp(method, "unshift") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s;", result);
                    codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY) {", obj_val);
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "hml_array_unshift(%s, %s);", obj_val, arg_temps[0]);
                    codegen_writeln(ctx, "%s = hml_val_null();", result);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "} else {");
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "HmlValue _unshift_args[1] = {%s};", arg_temps[0]);
                    codegen_writeln(ctx, "%s = hml_call_method(%s, \"unshift\", _unshift_args, 1);", result, obj_val);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "}");
                } else if (strcmp(method, "insert") == 0 && expr->as.call.num_args == 2) {
                    codegen_writeln(ctx, "HmlValue %s;", result);
                    codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY) {", obj_val);
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "hml_array_insert(%s, %s, %s);",
                                  obj_val, arg_temps[0], arg_temps[1]);
                    codegen_writeln(ctx, "%s = hml_val_null();", result);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "} else {");
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "HmlValue _insert_args[2] = {%s, %s};", arg_temps[0], arg_temps[1]);
                    codegen_writeln(ctx, "%s = hml_call_method(%s, \"insert\", _insert_args, 2);", result, obj_val);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "}");
                } else if (strcmp(method, "remove") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s;", result);
                    codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY) {", obj_val);
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "%s = hml_array_remove(%s, %s);", result, obj_val, arg_temps[0]);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "} else {");
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "HmlValue _remove_args[1] = {%s};", arg_temps[0]);
                    codegen_writeln(ctx, "%s = hml_call_method(%s, \"remove\", _remove_args, 1);", result, obj_val);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "}");
                // Note: find, contains, slice are handled above with runtime type checks
                } else if (strcmp(method, "join") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_array_join(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if (strcmp(method, "concat") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_array_concat(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if (strcmp(method, "reverse") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "hml_array_reverse(%s);", obj_val);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                } else if (strcmp(method, "first") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_array_first(%s);", result, obj_val);
                } else if (strcmp(method, "last") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_array_last(%s);", result, obj_val);
                } else if (strcmp(method, "clear") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "hml_array_clear(%s);", obj_val);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                // File methods
                } else if (strcmp(method, "read") == 0 && (expr->as.call.num_args == 0 || expr->as.call.num_args == 1)) {
                    if (expr->as.call.num_args == 1) {
                        codegen_writeln(ctx, "HmlValue %s = hml_file_read(%s, %s);",
                                      result, obj_val, arg_temps[0]);
                    } else {
                        codegen_writeln(ctx, "HmlValue %s = hml_file_read_all(%s);", result, obj_val);
                    }
                } else if (strcmp(method, "write") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_file_write(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if (strcmp(method, "seek") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_file_seek(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if (strcmp(method, "tell") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_file_tell(%s);", result, obj_val);
                } else if (strcmp(method, "close") == 0 && expr->as.call.num_args == 0) {
                    // Handle file.close(), channel.close(), and socket.close()
                    codegen_writeln(ctx, "if (%s.type == HML_VAL_FILE) {", obj_val);
                    codegen_writeln(ctx, "    hml_file_close(%s);", obj_val);
                    codegen_writeln(ctx, "} else if (%s.type == HML_VAL_CHANNEL) {", obj_val);
                    codegen_writeln(ctx, "    hml_channel_close(%s);", obj_val);
                    codegen_writeln(ctx, "} else if (%s.type == HML_VAL_SOCKET) {", obj_val);
                    codegen_writeln(ctx, "    hml_socket_close(%s);", obj_val);
                    codegen_writeln(ctx, "}");
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                } else if (strcmp(method, "map") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_array_map(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if (strcmp(method, "filter") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_array_filter(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if (strcmp(method, "reduce") == 0 && (expr->as.call.num_args == 1 || expr->as.call.num_args == 2)) {
                    if (expr->as.call.num_args == 2) {
                        codegen_writeln(ctx, "HmlValue %s = hml_array_reduce(%s, %s, %s);",
                                      result, obj_val, arg_temps[0], arg_temps[1]);
                    } else {
                        // No initial value - use first element
                        codegen_writeln(ctx, "HmlValue %s = hml_array_reduce(%s, %s, hml_val_null());",
                                      result, obj_val, arg_temps[0]);
                    }
                // Channel methods (also handle socket variants)
                } else if (strcmp(method, "send") == 0 && expr->as.call.num_args == 1) {
                    // Channel send or socket send
                    codegen_writeln(ctx, "if (%s.type == HML_VAL_CHANNEL) {", obj_val);
                    codegen_writeln(ctx, "    hml_channel_send(%s, %s);", obj_val, arg_temps[0]);
                    codegen_writeln(ctx, "}");
                    codegen_writeln(ctx, "HmlValue %s;", result);
                    codegen_writeln(ctx, "if (%s.type == HML_VAL_SOCKET) {", obj_val);
                    codegen_writeln(ctx, "    %s = hml_socket_send(%s, %s);", result, obj_val, arg_temps[0]);
                    codegen_writeln(ctx, "} else {");
                    codegen_writeln(ctx, "    %s = hml_val_null();", result);
                    codegen_writeln(ctx, "}");
                } else if (strcmp(method, "recv") == 0) {
                    // Channel recv (no args) or socket recv (1 arg for size)
                    codegen_writeln(ctx, "HmlValue %s;", result);
                    if (expr->as.call.num_args == 0) {
                        codegen_writeln(ctx, "%s = hml_channel_recv(%s);", result, obj_val);
                    } else {
                        codegen_writeln(ctx, "%s = hml_socket_recv(%s, %s);", result, obj_val, arg_temps[0]);
                    }
                // Socket-specific methods
                } else if (strcmp(method, "bind") == 0 && expr->as.call.num_args == 2) {
                    codegen_writeln(ctx, "hml_socket_bind(%s, %s, %s);", obj_val, arg_temps[0], arg_temps[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                } else if (strcmp(method, "listen") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "hml_socket_listen(%s, %s);", obj_val, arg_temps[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                } else if (strcmp(method, "accept") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_socket_accept(%s);", result, obj_val);
                } else if (strcmp(method, "connect") == 0 && expr->as.call.num_args == 2) {
                    codegen_writeln(ctx, "hml_socket_connect(%s, %s, %s);", obj_val, arg_temps[0], arg_temps[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                } else if (strcmp(method, "sendto") == 0 && expr->as.call.num_args == 3) {
                    codegen_writeln(ctx, "HmlValue %s = hml_socket_sendto(%s, %s, %s, %s);",
                                  result, obj_val, arg_temps[0], arg_temps[1], arg_temps[2]);
                } else if (strcmp(method, "recvfrom") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_socket_recvfrom(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if (strcmp(method, "setsockopt") == 0 && expr->as.call.num_args == 3) {
                    codegen_writeln(ctx, "hml_socket_setsockopt(%s, %s, %s, %s);",
                                  obj_val, arg_temps[0], arg_temps[1], arg_temps[2]);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                } else if (strcmp(method, "set_timeout") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "hml_socket_set_timeout(%s, %s);",
                                  obj_val, arg_temps[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                // Serialization methods
                } else if (strcmp(method, "serialize") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_serialize(%s);", result, obj_val);
                } else if (strcmp(method, "deserialize") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_deserialize(%s);", result, obj_val);
                } else {
                    // Unknown built-in method - try as object method call
                    if (expr->as.call.num_args > 0) {
                        codegen_writeln(ctx, "HmlValue _method_args%d[%d];", ctx->temp_counter, expr->as.call.num_args);
                        for (int i = 0; i < expr->as.call.num_args; i++) {
                            codegen_writeln(ctx, "_method_args%d[%d] = %s;", ctx->temp_counter, i, arg_temps[i]);
                        }
                        codegen_writeln(ctx, "HmlValue %s = hml_call_method(%s, \"%s\", _method_args%d, %d);",
                                      result, obj_val, method, ctx->temp_counter, expr->as.call.num_args);
                        ctx->temp_counter++;
                    } else {
                        codegen_writeln(ctx, "HmlValue %s = hml_call_method(%s, \"%s\", NULL, 0);",
                                      result, obj_val, method);
                    }
                }

                // Release temporaries
                codegen_writeln(ctx, "hml_release(&%s);", obj_val);
                for (int i = 0; i < expr->as.call.num_args; i++) {
                    codegen_writeln(ctx, "hml_release(&%s);", arg_temps[i]);
                    free(arg_temps[i]);
                }
                free(arg_temps);
                free(obj_val);
                break;
            }

            // Generic function call handling
            char *func_val = codegen_expr(ctx, expr->as.call.func);

            // Reserve a counter for the args array BEFORE generating arg expressions
            // (which may increment temp_counter internally)
            int args_counter = ctx->temp_counter++;

            // Generate code for arguments
            char **arg_temps = malloc(expr->as.call.num_args * sizeof(char*));
            for (int i = 0; i < expr->as.call.num_args; i++) {
                arg_temps[i] = codegen_expr(ctx, expr->as.call.args[i]);
            }

            // Build args array
            if (expr->as.call.num_args > 0) {
                codegen_writeln(ctx, "HmlValue _args%d[%d];", args_counter, expr->as.call.num_args);
                for (int i = 0; i < expr->as.call.num_args; i++) {
                    codegen_writeln(ctx, "_args%d[%d] = %s;", args_counter, i, arg_temps[i]);
                }
                codegen_writeln(ctx, "HmlValue %s = hml_call_function(%s, _args%d, %d);",
                              result, func_val, args_counter, expr->as.call.num_args);
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_call_function(%s, NULL, 0);", result, func_val);
            }

            // Release temporaries
            codegen_writeln(ctx, "hml_release(&%s);", func_val);
            for (int i = 0; i < expr->as.call.num_args; i++) {
                codegen_writeln(ctx, "hml_release(&%s);", arg_temps[i]);
                free(arg_temps[i]);
            }
            free(arg_temps);
            free(func_val);
            break;
        }

        case EXPR_ASSIGN: {
            char *value = codegen_expr(ctx, expr->as.assign.value);
            // Determine the correct variable name with prefix
            const char *var_name = expr->as.assign.name;
            char prefixed_name[256];
            if (ctx->current_module && !codegen_is_local(ctx, var_name)) {
                // Module context - use module prefix
                snprintf(prefixed_name, sizeof(prefixed_name), "%s%s",
                        ctx->current_module->module_prefix, var_name);
                var_name = prefixed_name;
            } else if (codegen_is_main_var(ctx, expr->as.assign.name)) {
                // Main file top-level variable - use _main_ prefix
                // Check this BEFORE local check since function names are added as locals for tracking
                snprintf(prefixed_name, sizeof(prefixed_name), "_main_%s", expr->as.assign.name);
                var_name = prefixed_name;
            }
            codegen_writeln(ctx, "hml_release(&%s);", var_name);
            codegen_writeln(ctx, "%s = %s;", var_name, value);
            codegen_writeln(ctx, "hml_retain(&%s);", var_name);

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

            codegen_writeln(ctx, "HmlValue %s = %s;", result, var_name);
            codegen_writeln(ctx, "hml_retain(&%s);", result);
            free(value);
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
                codegen_writeln(ctx, "%s = hml_object_get_field(%s, \"length\");", result, obj);
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
                codegen_writeln(ctx, "%s = hml_object_get_field(%s, \"fd\");", result, obj);
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
                codegen_writeln(ctx, "%s = hml_object_get_field(%s, \"address\");", result, obj);
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
                codegen_writeln(ctx, "%s = hml_object_get_field(%s, \"port\");", result, obj);
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
                codegen_writeln(ctx, "%s = hml_object_get_field(%s, \"closed\");", result, obj);
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
                codegen_writeln(ctx, "%s = hml_object_get_field(%s, \"byte_length\");", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_object_get_field(%s, \"%s\");",
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
            char *obj = codegen_expr(ctx, expr->as.index.object);
            char *idx = codegen_expr(ctx, expr->as.index.index);
            codegen_writeln(ctx, "HmlValue %s;", result);
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
            codegen_writeln(ctx, "hml_release(&%s);", obj);
            codegen_writeln(ctx, "hml_release(&%s);", idx);
            free(obj);
            free(idx);
            break;
        }

        case EXPR_INDEX_ASSIGN: {
            char *obj = codegen_expr(ctx, expr->as.index_assign.object);
            char *idx = codegen_expr(ctx, expr->as.index_assign.index);
            char *value = codegen_expr(ctx, expr->as.index_assign.value);
            codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY) {", obj);
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
            codegen_writeln(ctx, "}");
            codegen_writeln(ctx, "HmlValue %s = %s;", result, value);
            codegen_writeln(ctx, "hml_retain(&%s);", result);
            codegen_writeln(ctx, "hml_release(&%s);", obj);
            codegen_writeln(ctx, "hml_release(&%s);", idx);
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
                char *val = codegen_expr(ctx, expr->as.object_literal.field_values[i]);
                codegen_writeln(ctx, "hml_object_set_field(%s, \"%s\", %s);",
                              result, expr->as.object_literal.field_names[i], val);
                codegen_writeln(ctx, "hml_release(&%s);", val);
                free(val);
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
            closure->func_name = strdup(func_name);
            closure->func_expr = expr;
            closure->source_module = ctx->current_module;  // Save module context for function resolution
            closure->next = ctx->closures;
            ctx->closures = closure;

            if (captured->num_vars == 0) {
                // No captures - simple function pointer
                closure->captured_vars = NULL;
                closure->num_captured = 0;
                closure->shared_env_indices = NULL;
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)%s, %d, %d);",
                              result, func_name, expr->as.function.num_params, expr->as.function.is_async);
            } else if (ctx->shared_env_name) {
                // Use the shared environment
                // Store the captured variable names and their shared env indices
                closure->captured_vars = malloc(captured->num_vars * sizeof(char*));
                closure->shared_env_indices = malloc(captured->num_vars * sizeof(int));
                closure->num_captured = captured->num_vars;
                for (int i = 0; i < captured->num_vars; i++) {
                    closure->captured_vars[i] = strdup(captured->vars[i]);
                    closure->shared_env_indices[i] = shared_env_get_index(ctx, captured->vars[i]);
                }

                // Update the shared environment with current values of captured variables
                for (int i = 0; i < captured->num_vars; i++) {
                    int shared_idx = shared_env_get_index(ctx, captured->vars[i]);
                    if (shared_idx >= 0) {
                        // Determine which variable name to use:
                        // - Main file vars are stored as _main_<name> in C
                        // - Module-local vars are stored as <name> in C
                        // - Module-local vars should shadow outer (main) vars with same name
                        if (ctx->current_module && codegen_is_local(ctx, captured->vars[i])) {
                            // Inside a module with a local variable - use bare name
                            // This handles cases like Set() having local 'map' that shadows main's 'map'
                            codegen_writeln(ctx, "hml_closure_env_set(%s, %d, %s);",
                                          ctx->shared_env_name, shared_idx, captured->vars[i]);
                        } else if (codegen_is_main_var(ctx, captured->vars[i])) {
                            // Main file variable (not in a module, or not a local) - use prefix
                            codegen_writeln(ctx, "hml_closure_env_set(%s, %d, _main_%s);",
                                          ctx->shared_env_name, shared_idx, captured->vars[i]);
                        } else {
                            // Neither - use as-is
                            codegen_writeln(ctx, "hml_closure_env_set(%s, %d, %s);",
                                          ctx->shared_env_name, shared_idx, captured->vars[i]);
                        }
                    }
                }
                codegen_writeln(ctx, "HmlValue %s = hml_val_function_with_env((void*)%s, (void*)%s, %d, %d);",
                              result, func_name, ctx->shared_env_name, expr->as.function.num_params, expr->as.function.is_async);

                // Track for self-reference fixup
                ctx->last_closure_env_id = -1;  // Using shared env, different mechanism
                if (ctx->last_closure_captured) {
                    for (int i = 0; i < ctx->last_closure_num_captured; i++) {
                        free(ctx->last_closure_captured[i]);
                    }
                    free(ctx->last_closure_captured);
                }
                ctx->last_closure_captured = malloc(sizeof(char*) * captured->num_vars);
                ctx->last_closure_num_captured = captured->num_vars;
                for (int i = 0; i < captured->num_vars; i++) {
                    ctx->last_closure_captured[i] = strdup(captured->vars[i]);
                }
            } else {
                // No shared environment - create a per-closure environment (original behavior)
                closure->captured_vars = malloc(captured->num_vars * sizeof(char*));
                closure->num_captured = captured->num_vars;
                closure->shared_env_indices = NULL;  // Not using shared environment
                for (int i = 0; i < captured->num_vars; i++) {
                    closure->captured_vars[i] = strdup(captured->vars[i]);
                }

                int env_id = ctx->temp_counter;
                codegen_writeln(ctx, "HmlClosureEnv *_env_%d = hml_closure_env_new(%d);",
                              env_id, captured->num_vars);
                for (int i = 0; i < captured->num_vars; i++) {
                    // Determine which variable name to use:
                    // - Main file vars are stored as _main_<name> in C
                    // - Module-local vars are stored as <name> in C
                    // - Module-local vars should shadow outer (main) vars with same name
                    if (ctx->current_module && codegen_is_local(ctx, captured->vars[i])) {
                        // Inside a module with a local variable - use bare name
                        codegen_writeln(ctx, "hml_closure_env_set(_env_%d, %d, %s);",
                                      env_id, i, captured->vars[i]);
                    } else if (codegen_is_main_var(ctx, captured->vars[i])) {
                        // Main file variable (not in a module, or not a local) - use prefix
                        codegen_writeln(ctx, "hml_closure_env_set(_env_%d, %d, _main_%s);",
                                      env_id, i, captured->vars[i]);
                    } else {
                        // Neither - use as-is
                        codegen_writeln(ctx, "hml_closure_env_set(_env_%d, %d, %s);",
                                      env_id, i, captured->vars[i]);
                    }
                }
                codegen_writeln(ctx, "HmlValue %s = hml_val_function_with_env((void*)%s, (void*)_env_%d, %d, %d);",
                              result, func_name, env_id, expr->as.function.num_params, expr->as.function.is_async);
                ctx->temp_counter++;

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
                ctx->last_closure_num_captured = captured->num_vars;
                for (int i = 0; i < captured->num_vars; i++) {
                    ctx->last_closure_captured[i] = strdup(captured->vars[i]);
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
                const char *raw_var = expr->as.prefix_inc.operand->as.ident;
                const char *var = raw_var;
                char prefixed_name[256];
                if (ctx->current_module && !codegen_is_local(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "%s%s",
                            ctx->current_module->module_prefix, raw_var);
                    var = prefixed_name;
                } else if (codegen_is_main_var(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "_main_%s", raw_var);
                    var = prefixed_name;
                }
                codegen_writeln(ctx, "%s = hml_binary_op(HML_OP_ADD, %s, hml_val_i32(1));", var, var);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, var);
                codegen_writeln(ctx, "hml_retain(&%s);", result);
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_val_null(); // Complex prefix inc not supported", result);
            }
            break;
        }

        case EXPR_PREFIX_DEC: {
            if (expr->as.prefix_dec.operand->type == EXPR_IDENT) {
                const char *raw_var = expr->as.prefix_dec.operand->as.ident;
                const char *var = raw_var;
                char prefixed_name[256];
                if (ctx->current_module && !codegen_is_local(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "%s%s",
                            ctx->current_module->module_prefix, raw_var);
                    var = prefixed_name;
                } else if (codegen_is_main_var(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "_main_%s", raw_var);
                    var = prefixed_name;
                }
                codegen_writeln(ctx, "%s = hml_binary_op(HML_OP_SUB, %s, hml_val_i32(1));", var, var);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, var);
                codegen_writeln(ctx, "hml_retain(&%s);", result);
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            }
            break;
        }

        case EXPR_POSTFIX_INC: {
            // x++ returns old value, then increments
            if (expr->as.postfix_inc.operand->type == EXPR_IDENT) {
                const char *raw_var = expr->as.postfix_inc.operand->as.ident;
                const char *var = raw_var;
                char prefixed_name[256];
                if (ctx->current_module && !codegen_is_local(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "%s%s",
                            ctx->current_module->module_prefix, raw_var);
                    var = prefixed_name;
                } else if (codegen_is_main_var(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "_main_%s", raw_var);
                    var = prefixed_name;
                }
                codegen_writeln(ctx, "HmlValue %s = %s;", result, var);
                codegen_writeln(ctx, "hml_retain(&%s);", result);
                codegen_writeln(ctx, "%s = hml_binary_op(HML_OP_ADD, %s, hml_val_i32(1));", var, var);
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            }
            break;
        }

        case EXPR_POSTFIX_DEC: {
            if (expr->as.postfix_dec.operand->type == EXPR_IDENT) {
                const char *raw_var = expr->as.postfix_dec.operand->as.ident;
                const char *var = raw_var;
                char prefixed_name[256];
                if (ctx->current_module && !codegen_is_local(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "%s%s",
                            ctx->current_module->module_prefix, raw_var);
                    var = prefixed_name;
                } else if (codegen_is_main_var(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "_main_%s", raw_var);
                    var = prefixed_name;
                }
                codegen_writeln(ctx, "HmlValue %s = %s;", result, var);
                codegen_writeln(ctx, "hml_retain(&%s);", result);
                codegen_writeln(ctx, "%s = hml_binary_op(HML_OP_SUB, %s, hml_val_i32(1));", var, var);
            } else {
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
            // await expr is syntactic sugar for join(expr)
            char *task = codegen_expr(ctx, expr->as.await_expr.awaited_expr);
            codegen_writeln(ctx, "HmlValue %s = hml_join(%s);", result, task);
            codegen_writeln(ctx, "hml_release(&%s);", task);
            free(task);
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
                // obj?.method(args) - not yet supported
                codegen_writeln(ctx, "%s = hml_val_null(); // optional call not supported", result);
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

        default:
            codegen_writeln(ctx, "HmlValue %s = hml_val_null(); // Unsupported expression type %d", result, expr->type);
            break;
    }

    return result;
}

// ========== STATEMENT CODE GENERATION ==========

void codegen_stmt(CodegenContext *ctx, Stmt *stmt) {
    switch (stmt->type) {
        case STMT_LET: {
            codegen_add_local(ctx, stmt->as.let.name);
            if (stmt->as.let.value) {
                char *value = codegen_expr(ctx, stmt->as.let.value);
                // Check if there's a custom object type annotation (for duck typing)
                if (stmt->as.let.type_annotation &&
                    stmt->as.let.type_annotation->kind == TYPE_CUSTOM_OBJECT &&
                    stmt->as.let.type_annotation->type_name) {
                    codegen_writeln(ctx, "HmlValue %s = hml_validate_object_type(%s, \"%s\");",
                                  stmt->as.let.name, value, stmt->as.let.type_annotation->type_name);
                } else if (stmt->as.let.type_annotation &&
                           stmt->as.let.type_annotation->kind == TYPE_ARRAY) {
                    // Typed array: let arr: array<type> = [...]
                    Type *elem_type = stmt->as.let.type_annotation->element_type;
                    const char *hml_type = "HML_VAL_NULL";  // Default to untyped
                    if (elem_type) {
                        switch (elem_type->kind) {
                            case TYPE_I8:    hml_type = "HML_VAL_I8"; break;
                            case TYPE_I16:   hml_type = "HML_VAL_I16"; break;
                            case TYPE_I32:   hml_type = "HML_VAL_I32"; break;
                            case TYPE_I64:   hml_type = "HML_VAL_I64"; break;
                            case TYPE_U8:    hml_type = "HML_VAL_U8"; break;
                            case TYPE_U16:   hml_type = "HML_VAL_U16"; break;
                            case TYPE_U32:   hml_type = "HML_VAL_U32"; break;
                            case TYPE_U64:   hml_type = "HML_VAL_U64"; break;
                            case TYPE_F32:   hml_type = "HML_VAL_F32"; break;
                            case TYPE_F64:   hml_type = "HML_VAL_F64"; break;
                            case TYPE_BOOL:  hml_type = "HML_VAL_BOOL"; break;
                            case TYPE_STRING: hml_type = "HML_VAL_STRING"; break;
                            case TYPE_RUNE:  hml_type = "HML_VAL_RUNE"; break;
                            default: hml_type = "HML_VAL_NULL"; break;
                        }
                    }
                    codegen_writeln(ctx, "HmlValue %s = hml_validate_typed_array(%s, %s);",
                                  stmt->as.let.name, value, hml_type);
                } else if (stmt->as.let.type_annotation) {
                    // Primitive type annotation: let x: i64 = 0;
                    // Convert value to the annotated type with range checking
                    const char *hml_type = NULL;
                    switch (stmt->as.let.type_annotation->kind) {
                        case TYPE_I8:    hml_type = "HML_VAL_I8"; break;
                        case TYPE_I16:   hml_type = "HML_VAL_I16"; break;
                        case TYPE_I32:   hml_type = "HML_VAL_I32"; break;
                        case TYPE_I64:   hml_type = "HML_VAL_I64"; break;
                        case TYPE_U8:    hml_type = "HML_VAL_U8"; break;
                        case TYPE_U16:   hml_type = "HML_VAL_U16"; break;
                        case TYPE_U32:   hml_type = "HML_VAL_U32"; break;
                        case TYPE_U64:   hml_type = "HML_VAL_U64"; break;
                        case TYPE_F32:   hml_type = "HML_VAL_F32"; break;
                        case TYPE_F64:   hml_type = "HML_VAL_F64"; break;
                        case TYPE_BOOL:  hml_type = "HML_VAL_BOOL"; break;
                        case TYPE_STRING: hml_type = "HML_VAL_STRING"; break;
                        case TYPE_RUNE:  hml_type = "HML_VAL_RUNE"; break;
                        default: break;
                    }
                    if (hml_type) {
                        codegen_writeln(ctx, "HmlValue %s = hml_convert_to_type(%s, %s);",
                                      stmt->as.let.name, value, hml_type);
                    } else {
                        codegen_writeln(ctx, "HmlValue %s = %s;", stmt->as.let.name, value);
                    }
                } else {
                    codegen_writeln(ctx, "HmlValue %s = %s;", stmt->as.let.name, value);
                }
                free(value);

                // Check if this was a self-referential function (e.g., let factorial = fn(n) { ... factorial(n-1) ... })
                // If so, update the closure environment to point to the now-initialized variable
                if (ctx->last_closure_env_id >= 0 && ctx->last_closure_captured) {
                    for (int i = 0; i < ctx->last_closure_num_captured; i++) {
                        if (strcmp(ctx->last_closure_captured[i], stmt->as.let.name) == 0) {
                            codegen_writeln(ctx, "hml_closure_env_set(_env_%d, %d, %s);",
                                          ctx->last_closure_env_id, i, stmt->as.let.name);
                        }
                    }
                    // Reset the tracking - we've handled this closure
                    ctx->last_closure_env_id = -1;
                }
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_val_null();", stmt->as.let.name);
            }
            break;
        }

        case STMT_CONST: {
            codegen_add_local(ctx, stmt->as.const_stmt.name);
            if (stmt->as.const_stmt.value) {
                char *value = codegen_expr(ctx, stmt->as.const_stmt.value);
                codegen_writeln(ctx, "const HmlValue %s = %s;", stmt->as.const_stmt.name, value);
                free(value);
            } else {
                codegen_writeln(ctx, "const HmlValue %s = hml_val_null();", stmt->as.const_stmt.name);
            }
            break;
        }

        case STMT_EXPR: {
            char *value = codegen_expr(ctx, stmt->as.expr);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(value);
            break;
        }

        case STMT_IF: {
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
            codegen_writeln(ctx, "while (1) {");
            codegen_indent_inc(ctx);
            char *cond = codegen_expr(ctx, stmt->as.while_stmt.condition);
            codegen_writeln(ctx, "if (!hml_to_bool(%s)) { hml_release(&%s); break; }", cond, cond);
            codegen_writeln(ctx, "hml_release(&%s);", cond);
            codegen_stmt(ctx, stmt->as.while_stmt.body);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            free(cond);
            break;
        }

        case STMT_FOR: {
            codegen_writeln(ctx, "{");
            codegen_indent_inc(ctx);
            // Initializer
            if (stmt->as.for_loop.initializer) {
                codegen_stmt(ctx, stmt->as.for_loop.initializer);
            }
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
            break;
        }

        case STMT_FOR_IN: {
            // Generate for-in loop for arrays
            // for (let val in arr) or for (let key, val in arr)
            codegen_writeln(ctx, "{");
            codegen_indent_inc(ctx);

            // Evaluate the iterable
            char *iter_val = codegen_expr(ctx, stmt->as.for_in.iterable);
            codegen_writeln(ctx, "hml_retain(&%s);", iter_val);

            // Get the length
            char *len_var = codegen_temp(ctx);
            codegen_writeln(ctx, "HmlValue %s = hml_array_length(%s);", len_var, iter_val);

            // Index counter
            char *idx_var = codegen_temp(ctx);
            codegen_writeln(ctx, "int32_t %s = 0;", idx_var);

            codegen_writeln(ctx, "while (%s < %s.as.as_i32) {", idx_var, len_var);
            codegen_indent_inc(ctx);

            // Create key variable if provided
            if (stmt->as.for_in.key_var) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_i32(%s);",
                              stmt->as.for_in.key_var, idx_var);
                codegen_add_local(ctx, stmt->as.for_in.key_var);
            }

            // Get value at index
            char *idx_val = codegen_temp(ctx);
            codegen_writeln(ctx, "HmlValue %s = hml_val_i32(%s);", idx_val, idx_var);
            codegen_writeln(ctx, "HmlValue %s = hml_array_get(%s, %s);",
                          stmt->as.for_in.value_var, iter_val, idx_val);
            codegen_add_local(ctx, stmt->as.for_in.value_var);
            codegen_writeln(ctx, "hml_release(&%s);", idx_val);

            // Generate body
            codegen_stmt(ctx, stmt->as.for_in.body);

            // Release loop variables
            if (stmt->as.for_in.key_var) {
                codegen_writeln(ctx, "hml_release(&%s);", stmt->as.for_in.key_var);
            }
            codegen_writeln(ctx, "hml_release(&%s);", stmt->as.for_in.value_var);

            // Increment index
            codegen_writeln(ctx, "%s++;", idx_var);

            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");

            // Cleanup
            codegen_writeln(ctx, "hml_release(&%s);", len_var);
            codegen_writeln(ctx, "hml_release(&%s);", iter_val);

            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");

            free(iter_val);
            free(len_var);
            free(idx_var);
            free(idx_val);
            break;
        }

        case STMT_BLOCK: {
            codegen_writeln(ctx, "{");
            codegen_indent_inc(ctx);
            for (int i = 0; i < stmt->as.block.count; i++) {
                codegen_stmt(ctx, stmt->as.block.statements[i]);
            }
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            break;
        }

        case STMT_RETURN: {
            if (ctx->defer_stack) {
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
                codegen_writeln(ctx, "return %s;", ret_val);
                free(ret_val);
            } else {
                // No defers - simple return
                if (stmt->as.return_stmt.value) {
                    char *value = codegen_expr(ctx, stmt->as.return_stmt.value);
                    codegen_writeln(ctx, "return %s;", value);
                    free(value);
                } else {
                    codegen_writeln(ctx, "return hml_val_null();");
                }
            }
            break;
        }

        case STMT_BREAK:
            codegen_writeln(ctx, "break;");
            break;

        case STMT_CONTINUE:
            codegen_writeln(ctx, "continue;");
            break;

        case STMT_TRY: {
            codegen_writeln(ctx, "{");
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "HmlExceptionContext *_ex_ctx = hml_exception_push();");
            codegen_writeln(ctx, "if (setjmp(_ex_ctx->exception_buf) == 0) {");
            codegen_indent_inc(ctx);
            // Try block
            codegen_stmt(ctx, stmt->as.try_stmt.try_block);
            codegen_indent_dec(ctx);
            if (stmt->as.try_stmt.catch_block) {
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                // Catch block - declare catch param as local to prevent prefixing
                if (stmt->as.try_stmt.catch_param) {
                    codegen_add_local(ctx, stmt->as.try_stmt.catch_param);
                    codegen_writeln(ctx, "HmlValue %s = hml_exception_get_value();", stmt->as.try_stmt.catch_param);
                }
                codegen_stmt(ctx, stmt->as.try_stmt.catch_block);
                if (stmt->as.try_stmt.catch_param) {
                    codegen_writeln(ctx, "hml_release(&%s);", stmt->as.try_stmt.catch_param);
                }
                codegen_indent_dec(ctx);
            }
            codegen_writeln(ctx, "}");
            // Finally block
            if (stmt->as.try_stmt.finally_block) {
                codegen_stmt(ctx, stmt->as.try_stmt.finally_block);
            }
            codegen_writeln(ctx, "hml_exception_pop();");
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            break;
        }

        case STMT_THROW: {
            char *value = codegen_expr(ctx, stmt->as.throw_stmt.value);
            codegen_writeln(ctx, "hml_throw(%s);", value);
            free(value);
            break;
        }

        case STMT_SWITCH: {
            // Generate switch using do-while(0) pattern so break works correctly
            char *expr_val = codegen_expr(ctx, stmt->as.switch_stmt.expr);
            int has_default = 0;
            int default_idx = -1;

            // Find default case
            for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
                if (stmt->as.switch_stmt.case_values[i] == NULL) {
                    has_default = 1;
                    default_idx = i;
                    break;
                }
            }

            codegen_writeln(ctx, "do {");
            codegen_indent_inc(ctx);

            // Pre-generate all case values to avoid scoping issues
            char **case_vals = malloc(stmt->as.switch_stmt.num_cases * sizeof(char*));
            for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
                if (stmt->as.switch_stmt.case_values[i] == NULL) {
                    case_vals[i] = NULL;
                } else {
                    case_vals[i] = codegen_expr(ctx, stmt->as.switch_stmt.case_values[i]);
                }
            }

            // Generate case comparisons as if-else chain
            int first_case = 1;
            for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
                if (case_vals[i] == NULL) continue;  // Skip default

                if (first_case) {
                    codegen_writeln(ctx, "if (hml_to_bool(hml_binary_op(HML_OP_EQUAL, %s, %s))) {", expr_val, case_vals[i]);
                    first_case = 0;
                } else {
                    codegen_writeln(ctx, "} else if (hml_to_bool(hml_binary_op(HML_OP_EQUAL, %s, %s))) {", expr_val, case_vals[i]);
                }
                codegen_indent_inc(ctx);
                codegen_stmt(ctx, stmt->as.switch_stmt.case_bodies[i]);
                codegen_indent_dec(ctx);
            }

            if (has_default) {
                if (first_case) {
                    // Only default case exists
                    codegen_stmt(ctx, stmt->as.switch_stmt.case_bodies[default_idx]);
                } else {
                    codegen_writeln(ctx, "} else {");
                    codegen_indent_inc(ctx);
                    codegen_stmt(ctx, stmt->as.switch_stmt.case_bodies[default_idx]);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "}");
                }
            } else if (!first_case) {
                codegen_writeln(ctx, "}");
            }

            // Release case values
            for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
                if (case_vals[i]) {
                    codegen_writeln(ctx, "hml_release(&%s);", case_vals[i]);
                    free(case_vals[i]);
                }
            }
            free(case_vals);

            codegen_writeln(ctx, "hml_release(&%s);", expr_val);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "} while(0);");
            free(expr_val);
            break;
        }

        case STMT_DEFER: {
            // Push the expression onto the defer stack - will be executed at function return
            codegen_defer_push(ctx, stmt->as.defer_stmt.call);
            break;
        }

        case STMT_ENUM: {
            // Generate enum as a const object with variant values
            const char *raw_enum_name = stmt->as.enum_decl.name;

            // Determine the correct variable name with prefix
            char prefixed_name[256];
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

                // Map Type to HML_VAL_* type
                int type_kind = -1;  // -1 means any type
                if (field_type) {
                    switch (field_type->kind) {
                        case TYPE_I8:  type_kind = 0; break;  // HML_VAL_I8
                        case TYPE_I16: type_kind = 1; break;  // HML_VAL_I16
                        case TYPE_I32: type_kind = 2; break;  // HML_VAL_I32
                        case TYPE_I64: type_kind = 3; break;  // HML_VAL_I64
                        case TYPE_U8:  type_kind = 4; break;  // HML_VAL_U8
                        case TYPE_U16: type_kind = 5; break;  // HML_VAL_U16
                        case TYPE_U32: type_kind = 6; break;  // HML_VAL_U32
                        case TYPE_U64: type_kind = 7; break;  // HML_VAL_U64
                        case TYPE_F32: type_kind = 8; break;  // HML_VAL_F32
                        case TYPE_F64: type_kind = 9; break;  // HML_VAL_F64
                        case TYPE_BOOL: type_kind = 10; break; // HML_VAL_BOOL
                        case TYPE_STRING: type_kind = 11; break; // HML_VAL_STRING
                        default: type_kind = -1; break;
                    }
                }

                codegen_writeln(ctx, "_type_fields_%s[%d].name = \"%s\";",
                              type_name, i, field_name);
                codegen_writeln(ctx, "_type_fields_%s[%d].type_kind = %d;",
                              type_name, i, type_kind);
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
                codegen_writeln(ctx, "// WARNING: import without module cache: \"%s\"", stmt->as.import_stmt.module_path);
                break;
            }

            // Resolve the import path
            const char *importer_path = ctx->current_module ? ctx->current_module->absolute_path : NULL;
            char *resolved = module_resolve_path(ctx->module_cache, importer_path, stmt->as.import_stmt.module_path);
            if (!resolved) {
                codegen_writeln(ctx, "// ERROR: Could not resolve import \"%s\"", stmt->as.import_stmt.module_path);
                break;
            }

            // Get or compile the module
            CompiledModule *imported = module_get_cached(ctx->module_cache, resolved);
            if (!imported) {
                imported = module_compile(ctx, resolved);
            }
            free(resolved);

            if (!imported) {
                codegen_writeln(ctx, "// ERROR: Failed to compile import \"%s\"", stmt->as.import_stmt.module_path);
                break;
            }

            // Generate import binding code
            codegen_writeln(ctx, "// Import from \"%s\"", stmt->as.import_stmt.module_path);

            if (stmt->as.import_stmt.is_namespace) {
                // Namespace import: import * as name from "module"
                // Create an object containing all exports
                char *ns_name = stmt->as.import_stmt.namespace_name;

                // Create namespace object with exports
                codegen_writeln(ctx, "HmlValue %s = hml_val_object();", ns_name);
                codegen_add_local(ctx, ns_name);

                for (int i = 0; i < imported->num_exports; i++) {
                    ExportedSymbol *exp = &imported->exports[i];
                    codegen_writeln(ctx, "hml_object_set_field(%s, \"%s\", %s);", ns_name, exp->name, exp->mangled_name);
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
                        codegen_writeln(ctx, "// ERROR: '%s' not exported from module", import_name);
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
                    }

                    if (name) {
                        // Generate assignment to global mangled name (already declared as static)
                        char mangled[256];
                        snprintf(mangled, sizeof(mangled), "%s%s", ctx->current_module->module_prefix, name);

                        if (decl->type == STMT_LET && decl->as.let.value) {
                            // Check if it's a function definition
                            if (decl->as.let.value->type == EXPR_FUNCTION) {
                                Expr *func = decl->as.let.value;
                                codegen_writeln(ctx, "%s = hml_val_function((void*)%sfn_%s, %d, %d);",
                                              mangled, ctx->current_module->module_prefix, name,
                                              func->as.function.num_params, func->as.function.is_async);
                            } else {
                                char *val = codegen_expr(ctx, decl->as.let.value);
                                codegen_writeln(ctx, "%s = %s;", mangled, val);
                                free(val);
                            }
                        } else if (decl->type == STMT_CONST && decl->as.const_stmt.value) {
                            char *val = codegen_expr(ctx, decl->as.const_stmt.value);
                            codegen_writeln(ctx, "%s = %s;", mangled, val);
                            free(val);
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
            codegen_writeln(ctx, "// Unsupported statement type %d", stmt->type);
            break;
    }
}

// ========== PROGRAM CODE GENERATION ==========

// Check if a statement is a function definition (let name = fn() {} or export fn name())
static int is_function_def(Stmt *stmt, char **name_out, Expr **func_out) {
    // Direct let statement with function
    if (stmt->type == STMT_LET && stmt->as.let.value &&
        stmt->as.let.value->type == EXPR_FUNCTION) {
        *name_out = stmt->as.let.name;
        *func_out = stmt->as.let.value;
        return 1;
    }
    // Export statement with function declaration (export fn name())
    if (stmt->type == STMT_EXPORT && stmt->as.export_stmt.is_declaration &&
        stmt->as.export_stmt.declaration) {
        Stmt *decl = stmt->as.export_stmt.declaration;
        if (decl->type == STMT_LET && decl->as.let.value &&
            decl->as.let.value->type == EXPR_FUNCTION) {
            *name_out = decl->as.let.name;
            *func_out = decl->as.let.value;
            return 1;
        }
    }
    return 0;
}

// Generate a top-level function declaration
static void codegen_function_decl(CodegenContext *ctx, Expr *func, const char *name) {
    // Generate function signature with closure env for uniform calling convention
    // Even named functions take HmlClosureEnv* as first param (unused, passed as NULL)
    codegen_write(ctx, "HmlValue hml_fn_%s(HmlClosureEnv *_closure_env", name);
    for (int i = 0; i < func->as.function.num_params; i++) {
        codegen_write(ctx, ", HmlValue %s", func->as.function.param_names[i]);
    }
    codegen_write(ctx, ") {\n");
    codegen_indent_inc(ctx);
    // Suppress unused parameter warning
    codegen_writeln(ctx, "(void)_closure_env;");

    // Save locals and defer state
    int saved_num_locals = ctx->num_locals;
    DeferEntry *saved_defer_stack = ctx->defer_stack;
    ctx->defer_stack = NULL;  // Start fresh for this function

    // Reset closure env tracking to prevent cross-function pollution
    ctx->last_closure_env_id = -1;

    // Add parameters as locals
    for (int i = 0; i < func->as.function.num_params; i++) {
        codegen_add_local(ctx, func->as.function.param_names[i]);
    }

    // Apply default values for optional parameters
    // If a parameter with a default is null, assign the default
    if (func->as.function.param_defaults) {
        for (int i = 0; i < func->as.function.num_params; i++) {
            if (func->as.function.param_defaults[i]) {
                char *param_name = func->as.function.param_names[i];
                codegen_writeln(ctx, "if (%s.type == HML_VAL_NULL) {", param_name);
                codegen_indent_inc(ctx);
                char *default_val = codegen_expr(ctx, func->as.function.param_defaults[i]);
                codegen_writeln(ctx, "%s = %s;", param_name, default_val);
                free(default_val);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            }
        }
    }

    // Generate body
    if (func->as.function.body->type == STMT_BLOCK) {
        for (int i = 0; i < func->as.function.body->as.block.count; i++) {
            codegen_stmt(ctx, func->as.function.body->as.block.statements[i]);
        }
    } else {
        codegen_stmt(ctx, func->as.function.body);
    }

    // Execute any remaining defers before implicit return
    codegen_defer_execute_all(ctx);

    // Default return null
    codegen_writeln(ctx, "return hml_val_null();");

    codegen_indent_dec(ctx);
    codegen_write(ctx, "}\n\n");

    // Restore locals and defer state
    codegen_defer_clear(ctx);
    ctx->defer_stack = saved_defer_stack;
    ctx->num_locals = saved_num_locals;
}

// Generate a closure function (takes environment as first hidden parameter)
static void codegen_closure_impl(CodegenContext *ctx, ClosureInfo *closure) {
    Expr *func = closure->func_expr;

    // Generate function signature with environment parameter
    codegen_write(ctx, "HmlValue %s(HmlClosureEnv *_closure_env", closure->func_name);
    for (int i = 0; i < func->as.function.num_params; i++) {
        codegen_write(ctx, ", HmlValue %s", func->as.function.param_names[i]);
    }
    codegen_write(ctx, ") {\n");
    codegen_indent_inc(ctx);

    // Save locals, defer state, module context, and current closure
    int saved_num_locals = ctx->num_locals;
    DeferEntry *saved_defer_stack = ctx->defer_stack;
    ctx->defer_stack = NULL;  // Start fresh for this function
    CompiledModule *saved_module = ctx->current_module;
    ctx->current_module = closure->source_module;  // Restore module context for function resolution
    ClosureInfo *saved_closure = ctx->current_closure;
    ctx->current_closure = closure;  // Track current closure for mutable captured variables

    // Reset closure env tracking to prevent cross-function pollution
    ctx->last_closure_env_id = -1;

    // Add parameters as locals
    for (int i = 0; i < func->as.function.num_params; i++) {
        codegen_add_local(ctx, func->as.function.param_names[i]);
    }

    // Extract captured variables from environment
    for (int i = 0; i < closure->num_captured; i++) {
        const char *var_name = closure->captured_vars[i];

        // Check if this is a module-level export (self-reference like Set calling Set)
        int is_module_export = 0;
        if (closure->source_module) {
            ExportedSymbol *exp = module_find_export(closure->source_module, var_name);
            if (exp) {
                is_module_export = 1;
                codegen_writeln(ctx, "HmlValue %s = %s;", var_name, exp->mangled_name);
            }
        }

        if (!is_module_export) {
            // Use shared_env_indices if available (shared environment), otherwise use local ordering
            int env_index = closure->shared_env_indices ? closure->shared_env_indices[i] : i;

            if (env_index == -1) {
                // Variable not in shared environment - check if it's a main file variable
                if (codegen_is_main_var(ctx, var_name)) {
                    // Main file function/variable - use _main_ prefix
                    codegen_writeln(ctx, "HmlValue %s = _main_%s;", var_name, var_name);
                } else {
                    // Fallback - just use the variable name directly (might be a global or builtin)
                    codegen_writeln(ctx, "HmlValue %s = %s;", var_name, var_name);
                }
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_closure_env_get(_closure_env, %d);",
                              var_name, env_index);
            }
        }
        codegen_add_local(ctx, var_name);
    }

    // Apply default values for optional parameters
    if (func->as.function.param_defaults) {
        for (int i = 0; i < func->as.function.num_params; i++) {
            if (func->as.function.param_defaults[i]) {
                char *param_name = func->as.function.param_names[i];
                codegen_writeln(ctx, "if (%s.type == HML_VAL_NULL) {", param_name);
                codegen_indent_inc(ctx);
                char *default_val = codegen_expr(ctx, func->as.function.param_defaults[i]);
                codegen_writeln(ctx, "%s = %s;", param_name, default_val);
                free(default_val);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            }
        }
    }

    // Scan for all closures in the function body and set up a shared environment
    // This allows multiple closures within this function to share the same environment
    Scope *scan_scope = scope_new(NULL);
    for (int i = 0; i < func->as.function.num_params; i++) {
        scope_add_var(scan_scope, func->as.function.param_names[i]);
    }
    // Add captured variables to scan scope (they're local to this closure)
    for (int i = 0; i < closure->num_captured; i++) {
        scope_add_var(scan_scope, closure->captured_vars[i]);
    }
    shared_env_clear(ctx);  // Clear any previous shared environment
    if (func->as.function.body->type == STMT_BLOCK) {
        for (int i = 0; i < func->as.function.body->as.block.count; i++) {
            scan_closures_stmt(ctx, func->as.function.body->as.block.statements[i], scan_scope);
        }
    } else {
        scan_closures_stmt(ctx, func->as.function.body, scan_scope);
    }
    scope_free(scan_scope);

    // If there are captured variables, create the shared environment
    if (ctx->shared_env_num_vars > 0) {
        char env_name[64];
        snprintf(env_name, sizeof(env_name), "_shared_env_%d", ctx->temp_counter++);
        ctx->shared_env_name = strdup(env_name);
        codegen_writeln(ctx, "HmlClosureEnv *%s = hml_closure_env_new(%d);",
                      env_name, ctx->shared_env_num_vars);
    }

    // Generate body
    if (func->as.function.body->type == STMT_BLOCK) {
        for (int i = 0; i < func->as.function.body->as.block.count; i++) {
            codegen_stmt(ctx, func->as.function.body->as.block.statements[i]);
        }
    } else {
        codegen_stmt(ctx, func->as.function.body);
    }

    // Execute any remaining defers before implicit return
    codegen_defer_execute_all(ctx);

    // Release captured variables before default return
    for (int i = 0; i < closure->num_captured; i++) {
        codegen_writeln(ctx, "hml_release(&%s);", closure->captured_vars[i]);
    }

    // Default return null
    codegen_writeln(ctx, "return hml_val_null();");

    codegen_indent_dec(ctx);
    codegen_write(ctx, "}\n\n");

    // Restore locals, defer state, module context, current closure, and clear shared environment
    codegen_defer_clear(ctx);
    ctx->defer_stack = saved_defer_stack;
    ctx->num_locals = saved_num_locals;
    ctx->current_module = saved_module;
    ctx->current_closure = saved_closure;
    shared_env_clear(ctx);  // Clear shared environment after generating this closure
}

// Generate wrapper function for closure (to match function pointer signature)
static void codegen_closure_wrapper(CodegenContext *ctx, ClosureInfo *closure) {
    Expr *func = closure->func_expr;

    // Generate wrapper that extracts env from function value and calls real implementation
    codegen_write(ctx, "HmlValue %s_wrapper(HmlValue *_args, int _nargs, void *_env) {\n", closure->func_name);
    codegen_indent_inc(ctx);
    codegen_writeln(ctx, "HmlClosureEnv *_closure_env = (HmlClosureEnv*)_env;");

    // Call the actual closure function
    codegen_write(ctx, "");
    codegen_indent(ctx);
    fprintf(ctx->output, "return %s(_closure_env", closure->func_name);
    for (int i = 0; i < func->as.function.num_params; i++) {
        fprintf(ctx->output, ", _args[%d]", i);
    }
    fprintf(ctx->output, ");\n");

    codegen_indent_dec(ctx);
    codegen_write(ctx, "}\n\n");
}

// Helper to generate init function for a module
static void codegen_module_init(CodegenContext *ctx, CompiledModule *module) {
    codegen_write(ctx, "// Module init: %s\n", module->absolute_path);
    codegen_write(ctx, "static int %sinit_done = 0;\n", module->module_prefix);
    codegen_write(ctx, "static void %sinit(void) {\n", module->module_prefix);
    codegen_indent_inc(ctx);
    codegen_writeln(ctx, "if (%sinit_done) return;", module->module_prefix);
    codegen_writeln(ctx, "%sinit_done = 1;", module->module_prefix);
    codegen_writeln(ctx, "");

    // Save current module context
    CompiledModule *saved_module = ctx->current_module;
    ctx->current_module = module;

    // First call init functions of imported modules
    for (int i = 0; i < module->num_statements; i++) {
        Stmt *stmt = module->statements[i];
        if (stmt->type == STMT_IMPORT) {
            char *import_path = stmt->as.import_stmt.module_path;
            char *resolved = module_resolve_path(ctx->module_cache, module->absolute_path, import_path);
            if (resolved) {
                CompiledModule *imported = module_get_cached(ctx->module_cache, resolved);
                if (imported) {
                    codegen_writeln(ctx, "%sinit();", imported->module_prefix);
                }
                free(resolved);
            }
        }
    }
    codegen_writeln(ctx, "");

    // Generate code for each statement in the module
    for (int i = 0; i < module->num_statements; i++) {
        Stmt *stmt = module->statements[i];

        // Skip imports (already handled above)
        if (stmt->type == STMT_IMPORT) {
            // Generate import bindings
            codegen_stmt(ctx, stmt);
            continue;
        }

        // Handle exports
        if (stmt->type == STMT_EXPORT) {
            codegen_stmt(ctx, stmt);
            continue;
        }

        // Check if it's a function definition
        char *name = NULL;
        Expr *func = NULL;
        if (stmt->type == STMT_LET && stmt->as.let.value &&
            stmt->as.let.value->type == EXPR_FUNCTION) {
            name = stmt->as.let.name;
            func = stmt->as.let.value;
        }

        if (name && func) {
            // Function definition - already declared as global, just initialize
            char mangled[256];
            snprintf(mangled, sizeof(mangled), "%s%s", module->module_prefix, name);
            codegen_writeln(ctx, "%s = hml_val_function((void*)%sfn_%s, %d, %d);",
                          mangled, module->module_prefix, name,
                          func->as.function.num_params, func->as.function.is_async);
        } else {
            // Regular statement
            codegen_stmt(ctx, stmt);
        }
    }

    // Restore module context
    ctx->current_module = saved_module;

    codegen_indent_dec(ctx);
    codegen_write(ctx, "}\n\n");
}

// Helper to generate function declarations for a module
static void codegen_module_funcs(CodegenContext *ctx, CompiledModule *module, FILE *decl_buffer, FILE *impl_buffer) {
    FILE *saved_output = ctx->output;
    CompiledModule *saved_module = ctx->current_module;
    ctx->current_module = module;

    for (int i = 0; i < module->num_statements; i++) {
        Stmt *stmt = module->statements[i];

        // Find function definitions (both exported and regular)
        const char *name = NULL;
        Expr *func = NULL;

        if (stmt->type == STMT_EXPORT && stmt->as.export_stmt.is_declaration) {
            Stmt *decl = stmt->as.export_stmt.declaration;
            if (decl->type == STMT_LET && decl->as.let.value &&
                decl->as.let.value->type == EXPR_FUNCTION) {
                name = decl->as.let.name;
                func = decl->as.let.value;
            }
        } else if (stmt->type == STMT_LET && stmt->as.let.value &&
                   stmt->as.let.value->type == EXPR_FUNCTION) {
            name = stmt->as.let.name;
            func = stmt->as.let.value;
        }

        if (name && func) {
            char mangled_fn[256];
            snprintf(mangled_fn, sizeof(mangled_fn), "%sfn_%s", module->module_prefix, name);

            // Generate forward declaration
            ctx->output = decl_buffer;
            codegen_write(ctx, "HmlValue %s(HmlClosureEnv *_closure_env", mangled_fn);
            for (int j = 0; j < func->as.function.num_params; j++) {
                codegen_write(ctx, ", HmlValue %s", func->as.function.param_names[j]);
            }
            codegen_write(ctx, ");\n");

            // Generate implementation
            ctx->output = impl_buffer;
            codegen_write(ctx, "HmlValue %s(HmlClosureEnv *_closure_env", mangled_fn);
            for (int j = 0; j < func->as.function.num_params; j++) {
                codegen_write(ctx, ", HmlValue %s", func->as.function.param_names[j]);
            }
            codegen_write(ctx, ") {\n");
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "(void)_closure_env;");

            // Save and reset locals
            int saved_num_locals = ctx->num_locals;
            DeferEntry *saved_defer_stack = ctx->defer_stack;
            ctx->defer_stack = NULL;

            // Reset closure env tracking to prevent cross-function pollution
            ctx->last_closure_env_id = -1;

            // Add parameters as locals
            for (int j = 0; j < func->as.function.num_params; j++) {
                codegen_add_local(ctx, func->as.function.param_names[j]);
            }

            // Apply default values for optional parameters
            if (func->as.function.param_defaults) {
                for (int j = 0; j < func->as.function.num_params; j++) {
                    if (func->as.function.param_defaults[j]) {
                        char *param_name = func->as.function.param_names[j];
                        codegen_writeln(ctx, "if (%s.type == HML_VAL_NULL) {", param_name);
                        codegen_indent_inc(ctx);
                        char *default_val = codegen_expr(ctx, func->as.function.param_defaults[j]);
                        codegen_writeln(ctx, "%s = %s;", param_name, default_val);
                        free(default_val);
                        codegen_indent_dec(ctx);
                        codegen_writeln(ctx, "}");
                    }
                }
            }

            // Scan for all closures in the function body and set up a shared environment
            // This allows multiple closures to share the same environment for captured variables
            Scope *scan_scope = scope_new(NULL);
            for (int j = 0; j < func->as.function.num_params; j++) {
                scope_add_var(scan_scope, func->as.function.param_names[j]);
            }
            shared_env_clear(ctx);  // Clear any previous shared environment
            if (func->as.function.body->type == STMT_BLOCK) {
                for (int j = 0; j < func->as.function.body->as.block.count; j++) {
                    scan_closures_stmt(ctx, func->as.function.body->as.block.statements[j], scan_scope);
                }
            } else {
                scan_closures_stmt(ctx, func->as.function.body, scan_scope);
            }
            scope_free(scan_scope);

            // If there are captured variables, create the shared environment
            if (ctx->shared_env_num_vars > 0) {
                char env_name[64];
                snprintf(env_name, sizeof(env_name), "_shared_env_%d", ctx->temp_counter++);
                ctx->shared_env_name = strdup(env_name);
                codegen_writeln(ctx, "HmlClosureEnv *%s = hml_closure_env_new(%d);",
                              env_name, ctx->shared_env_num_vars);
            }

            // Generate body
            if (func->as.function.body->type == STMT_BLOCK) {
                for (int j = 0; j < func->as.function.body->as.block.count; j++) {
                    codegen_stmt(ctx, func->as.function.body->as.block.statements[j]);
                }
            } else {
                codegen_stmt(ctx, func->as.function.body);
            }

            // Execute any remaining defers before implicit return
            codegen_defer_execute_all(ctx);

            // Default return null
            codegen_writeln(ctx, "return hml_val_null();");

            // Restore locals, defer state, and clear shared environment
            codegen_defer_clear(ctx);
            ctx->defer_stack = saved_defer_stack;
            ctx->num_locals = saved_num_locals;
            shared_env_clear(ctx);

            codegen_indent_dec(ctx);
            codegen_write(ctx, "}\n\n");
        }
    }

    ctx->output = saved_output;
    ctx->current_module = saved_module;
}

// Helper to collect all extern fn statements recursively (including from block scopes)
typedef struct ExternFnList {
    Stmt **stmts;
    int count;
    int capacity;
} ExternFnList;

static void collect_extern_fn_from_stmt(Stmt *stmt, ExternFnList *list);

static void collect_extern_fn_from_stmts(Stmt **stmts, int count, ExternFnList *list) {
    for (int i = 0; i < count; i++) {
        collect_extern_fn_from_stmt(stmts[i], list);
    }
}

static void collect_extern_fn_from_stmt(Stmt *stmt, ExternFnList *list) {
    if (!stmt) return;

    if (stmt->type == STMT_EXTERN_FN) {
        // Check if already in list (avoid duplicates)
        for (int i = 0; i < list->count; i++) {
            if (strcmp(list->stmts[i]->as.extern_fn.function_name,
                      stmt->as.extern_fn.function_name) == 0) {
                return;  // Already collected
            }
        }
        // Add to list
        if (list->count >= list->capacity) {
            list->capacity = list->capacity == 0 ? 16 : list->capacity * 2;
            list->stmts = realloc(list->stmts, list->capacity * sizeof(Stmt*));
        }
        list->stmts[list->count++] = stmt;
        return;
    }

    // Recursively check block statements
    if (stmt->type == STMT_BLOCK) {
        collect_extern_fn_from_stmts(stmt->as.block.statements, stmt->as.block.count, list);
    } else if (stmt->type == STMT_IF) {
        collect_extern_fn_from_stmt(stmt->as.if_stmt.then_branch, list);
        collect_extern_fn_from_stmt(stmt->as.if_stmt.else_branch, list);
    } else if (stmt->type == STMT_WHILE) {
        collect_extern_fn_from_stmt(stmt->as.while_stmt.body, list);
    } else if (stmt->type == STMT_FOR) {
        collect_extern_fn_from_stmt(stmt->as.for_loop.body, list);
    } else if (stmt->type == STMT_FOR_IN) {
        collect_extern_fn_from_stmt(stmt->as.for_in.body, list);
    } else if (stmt->type == STMT_TRY) {
        collect_extern_fn_from_stmt(stmt->as.try_stmt.try_block, list);
        collect_extern_fn_from_stmt(stmt->as.try_stmt.catch_block, list);
        collect_extern_fn_from_stmt(stmt->as.try_stmt.finally_block, list);
    } else if (stmt->type == STMT_SWITCH) {
        for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
            collect_extern_fn_from_stmt(stmt->as.switch_stmt.case_bodies[i], list);
        }
    }
}

void codegen_program(CodegenContext *ctx, Stmt **stmts, int stmt_count) {
    // Multi-pass approach:
    // 1. First pass through imports to compile all modules
    // 2. Generate named function bodies to a buffer to collect closures
    // 3. Output header + all forward declarations (functions + closures)
    // 4. Output module global variables and init functions
    // 5. Output closure implementations
    // 6. Output named function implementations
    // 7. Output main function

    // First pass: compile all imported modules
    if (ctx->module_cache) {
        for (int i = 0; i < stmt_count; i++) {
            if (stmts[i]->type == STMT_IMPORT) {
                char *import_path = stmts[i]->as.import_stmt.module_path;
                char *resolved = module_resolve_path(ctx->module_cache, NULL, import_path);
                if (resolved) {
                    module_compile(ctx, resolved);
                    free(resolved);
                }
            }
        }
    }

    // Buffer for named function implementations
    FILE *func_buffer = tmpfile();
    FILE *main_buffer = tmpfile();
    FILE *module_decl_buffer = tmpfile();
    FILE *module_impl_buffer = tmpfile();
    FILE *saved_output = ctx->output;

    // Pre-pass: Collect all main file variable names BEFORE generating code
    // This ensures codegen_is_main_var() works during main() body generation
    for (int i = 0; i < stmt_count; i++) {
        Stmt *stmt = stmts[i];
        // Unwrap export statements
        if (stmt->type == STMT_EXPORT && stmt->as.export_stmt.is_declaration && stmt->as.export_stmt.declaration) {
            stmt = stmt->as.export_stmt.declaration;
        }

        char *name;
        Expr *func;
        if (is_function_def(stmt, &name, &func)) {
            codegen_add_main_var(ctx, name);
            codegen_add_main_func(ctx, name);  // Also track as function definition
        } else if (stmt->type == STMT_CONST) {
            codegen_add_main_var(ctx, stmt->as.const_stmt.name);
        } else if (stmt->type == STMT_LET) {
            codegen_add_main_var(ctx, stmt->as.let.name);
        } else if (stmt->type == STMT_ENUM) {
            codegen_add_main_var(ctx, stmt->as.enum_decl.name);
        }
    }

    // Pre-pass: Collect import bindings for main file function call resolution
    if (ctx->module_cache) {
        for (int i = 0; i < stmt_count; i++) {
            if (stmts[i]->type == STMT_IMPORT) {
                Stmt *import_stmt = stmts[i];
                char *import_path = import_stmt->as.import_stmt.module_path;
                char *resolved = module_resolve_path(ctx->module_cache, NULL, import_path);
                if (resolved) {
                    CompiledModule *mod = module_get_cached(ctx->module_cache, resolved);
                    if (mod) {
                        // Add import bindings for named imports
                        if (!import_stmt->as.import_stmt.is_namespace) {
                            for (int j = 0; j < import_stmt->as.import_stmt.num_imports; j++) {
                                const char *import_name = import_stmt->as.import_stmt.import_names[j];
                                const char *alias = import_stmt->as.import_stmt.import_aliases[j];
                                const char *local_name = alias ? alias : import_name;
                                codegen_add_main_import(ctx, local_name, import_name, mod->module_prefix);
                            }
                        }
                    }
                    free(resolved);
                }
            }
        }
    }

    // Generate module functions first (to collect closures)
    if (ctx->module_cache) {
        CompiledModule *mod = ctx->module_cache->modules;
        while (mod) {
            codegen_module_funcs(ctx, mod, module_decl_buffer, module_impl_buffer);
            mod = mod->next;
        }
    }

    // Pass 1: Generate named function bodies to buffer (this collects closures)
    ctx->output = func_buffer;
    for (int i = 0; i < stmt_count; i++) {
        char *name;
        Expr *func;
        if (is_function_def(stmts[i], &name, &func)) {
            codegen_function_decl(ctx, func, name);
        }
    }

    // Pass 2: Generate main function body to buffer (this collects more closures)
    ctx->output = main_buffer;
    codegen_write(ctx, "int main(int argc, char **argv) {\n");
    codegen_indent_inc(ctx);
    codegen_writeln(ctx, "hml_runtime_init(argc, argv);");
    codegen_writeln(ctx, "");

    // Generate global args array from command-line arguments
    codegen_writeln(ctx, "HmlValue args = hml_get_args();");
    codegen_add_local(ctx, "args");
    codegen_writeln(ctx, "");

    // Initialize imported modules
    if (ctx->module_cache) {
        for (int i = 0; i < stmt_count; i++) {
            if (stmts[i]->type == STMT_IMPORT) {
                char *import_path = stmts[i]->as.import_stmt.module_path;
                char *resolved = module_resolve_path(ctx->module_cache, NULL, import_path);
                if (resolved) {
                    CompiledModule *mod = module_get_cached(ctx->module_cache, resolved);
                    if (mod) {
                        codegen_writeln(ctx, "%sinit();", mod->module_prefix);
                    }
                    free(resolved);
                }
            }
        }
        codegen_writeln(ctx, "");
    }

    // Initialize top-level function variables (they're static globals now)
    // First pass: add all function names as "locals" for codegen tracking
    for (int i = 0; i < stmt_count; i++) {
        char *name;
        Expr *func;
        if (is_function_def(stmts[i], &name, &func)) {
            codegen_add_local(ctx, name);
        }
    }
    codegen_writeln(ctx, "");

    // Generate all statements
    for (int i = 0; i < stmt_count; i++) {
        Stmt *stmt = stmts[i];
        // Unwrap export statements to handle their embedded declarations
        if (stmt->type == STMT_EXPORT && stmt->as.export_stmt.is_declaration && stmt->as.export_stmt.declaration) {
            stmt = stmt->as.export_stmt.declaration;
        }

        char *name;
        Expr *func;
        if (is_function_def(stmt, &name, &func)) {
            // Function definitions: assign function value to static global
            // Use _main_ prefix to avoid C name conflicts (e.g., kill, exit, fork)
            char *value = codegen_expr(ctx, func);
            codegen_writeln(ctx, "_main_%s = %s;", name, value);
            free(value);

            // Check if this was a self-referential function (e.g., let factorial = fn(n) { ... factorial(n-1) ... })
            // If so, update the closure environment to point to the now-initialized variable
            if (ctx->last_closure_env_id >= 0 && ctx->last_closure_captured) {
                for (int j = 0; j < ctx->last_closure_num_captured; j++) {
                    if (strcmp(ctx->last_closure_captured[j], name) == 0) {
                        codegen_writeln(ctx, "hml_closure_env_set(_env_%d, %d, _main_%s);",
                                      ctx->last_closure_env_id, j, name);
                    }
                }
                // Reset the tracking - we've handled this closure
                ctx->last_closure_env_id = -1;
            }
        } else if (stmt->type == STMT_CONST) {
            // Top-level const: assign to static global instead of declaring local
            // Use _main_ prefix to avoid C name conflicts
            if (stmt->as.const_stmt.value) {
                char *value = codegen_expr(ctx, stmt->as.const_stmt.value);
                codegen_writeln(ctx, "_main_%s = %s;", stmt->as.const_stmt.name, value);
                free(value);
            } else {
                codegen_writeln(ctx, "_main_%s = hml_val_null();", stmt->as.const_stmt.name);
            }
        } else if (stmt->type == STMT_LET) {
            // Top-level let (non-function): assign to static global instead of declaring local
            // Use _main_ prefix to avoid C name conflicts
            if (stmt->as.let.value) {
                char *value = codegen_expr(ctx, stmt->as.let.value);
                // Check if there's a custom object type annotation (for duck typing)
                if (stmt->as.let.type_annotation &&
                    stmt->as.let.type_annotation->kind == TYPE_CUSTOM_OBJECT &&
                    stmt->as.let.type_annotation->type_name) {
                    codegen_writeln(ctx, "_main_%s = hml_validate_object_type(%s, \"%s\");",
                                  stmt->as.let.name, value, stmt->as.let.type_annotation->type_name);
                } else if (stmt->as.let.type_annotation) {
                    // Primitive type annotation: let x: i64 = 0;
                    // Convert value to the annotated type with range checking
                    const char *hml_type = NULL;
                    switch (stmt->as.let.type_annotation->kind) {
                        case TYPE_I8:    hml_type = "HML_VAL_I8"; break;
                        case TYPE_I16:   hml_type = "HML_VAL_I16"; break;
                        case TYPE_I32:   hml_type = "HML_VAL_I32"; break;
                        case TYPE_I64:   hml_type = "HML_VAL_I64"; break;
                        case TYPE_U8:    hml_type = "HML_VAL_U8"; break;
                        case TYPE_U16:   hml_type = "HML_VAL_U16"; break;
                        case TYPE_U32:   hml_type = "HML_VAL_U32"; break;
                        case TYPE_U64:   hml_type = "HML_VAL_U64"; break;
                        case TYPE_F32:   hml_type = "HML_VAL_F32"; break;
                        case TYPE_F64:   hml_type = "HML_VAL_F64"; break;
                        case TYPE_BOOL:  hml_type = "HML_VAL_BOOL"; break;
                        case TYPE_STRING: hml_type = "HML_VAL_STRING"; break;
                        case TYPE_RUNE:  hml_type = "HML_VAL_RUNE"; break;
                        case TYPE_ARRAY: {
                            // Typed array: let arr: array<type> = [...]
                            Type *elem_type = stmt->as.let.type_annotation->element_type;
                            const char *arr_type = "HML_VAL_NULL";
                            if (elem_type) {
                                switch (elem_type->kind) {
                                    case TYPE_I8:    arr_type = "HML_VAL_I8"; break;
                                    case TYPE_I16:   arr_type = "HML_VAL_I16"; break;
                                    case TYPE_I32:   arr_type = "HML_VAL_I32"; break;
                                    case TYPE_I64:   arr_type = "HML_VAL_I64"; break;
                                    case TYPE_U8:    arr_type = "HML_VAL_U8"; break;
                                    case TYPE_U16:   arr_type = "HML_VAL_U16"; break;
                                    case TYPE_U32:   arr_type = "HML_VAL_U32"; break;
                                    case TYPE_U64:   arr_type = "HML_VAL_U64"; break;
                                    case TYPE_F32:   arr_type = "HML_VAL_F32"; break;
                                    case TYPE_F64:   arr_type = "HML_VAL_F64"; break;
                                    case TYPE_BOOL:  arr_type = "HML_VAL_BOOL"; break;
                                    case TYPE_STRING: arr_type = "HML_VAL_STRING"; break;
                                    case TYPE_RUNE:  arr_type = "HML_VAL_RUNE"; break;
                                    default: break;
                                }
                            }
                            codegen_writeln(ctx, "_main_%s = hml_validate_typed_array(%s, %s);",
                                          stmt->as.let.name, value, arr_type);
                            break;
                        }
                        default: break;
                    }
                    if (hml_type) {
                        codegen_writeln(ctx, "_main_%s = hml_convert_to_type(%s, %s);",
                                      stmt->as.let.name, value, hml_type);
                    } else if (stmt->as.let.type_annotation->kind != TYPE_ARRAY) {
                        codegen_writeln(ctx, "_main_%s = %s;", stmt->as.let.name, value);
                    }
                } else {
                    codegen_writeln(ctx, "_main_%s = %s;", stmt->as.let.name, value);
                }
                free(value);

                // Check if this was a self-referential closure
                if (ctx->last_closure_env_id >= 0 && ctx->last_closure_captured) {
                    for (int j = 0; j < ctx->last_closure_num_captured; j++) {
                        if (strcmp(ctx->last_closure_captured[j], stmt->as.let.name) == 0) {
                            codegen_writeln(ctx, "hml_closure_env_set(_env_%d, %d, _main_%s);",
                                          ctx->last_closure_env_id, j, stmt->as.let.name);
                        }
                    }
                    ctx->last_closure_env_id = -1;
                }
            } else {
                codegen_writeln(ctx, "_main_%s = hml_val_null();", stmt->as.let.name);
            }
        } else {
            codegen_stmt(ctx, stmts[i]);  // Use original statement for non-unwrapped cases
        }
    }

    codegen_writeln(ctx, "");
    codegen_writeln(ctx, "hml_runtime_cleanup();");
    codegen_writeln(ctx, "return 0;");
    codegen_indent_dec(ctx);
    codegen_write(ctx, "}\n");

    // Now output everything in the correct order
    ctx->output = saved_output;

    // Header
    codegen_write(ctx, "/*\n");
    codegen_write(ctx, " * Generated by Hemlock Compiler\n");
    codegen_write(ctx, " */\n\n");
    codegen_write(ctx, "#include \"hemlock_runtime.h\"\n");
    codegen_write(ctx, "#include <setjmp.h>\n");
    codegen_write(ctx, "#include <signal.h>\n");
    codegen_write(ctx, "#include <sys/socket.h>\n");
    codegen_write(ctx, "#include <netinet/in.h>\n");
    codegen_write(ctx, "#include <arpa/inet.h>\n\n");

    // Signal constants
    codegen_write(ctx, "// Signal constants\n");
    codegen_write(ctx, "#define SIGINT_VAL 2\n");
    codegen_write(ctx, "#define SIGTERM_VAL 15\n");
    codegen_write(ctx, "#define SIGHUP_VAL 1\n");
    codegen_write(ctx, "#define SIGQUIT_VAL 3\n");
    codegen_write(ctx, "#define SIGABRT_VAL 6\n");
    codegen_write(ctx, "#define SIGUSR1_VAL 10\n");
    codegen_write(ctx, "#define SIGUSR2_VAL 12\n");
    codegen_write(ctx, "#define SIGALRM_VAL 14\n");
    codegen_write(ctx, "#define SIGCHLD_VAL 17\n");
    codegen_write(ctx, "#define SIGPIPE_VAL 13\n");
    codegen_write(ctx, "#define SIGCONT_VAL 18\n");
    codegen_write(ctx, "#define SIGSTOP_VAL 19\n");
    codegen_write(ctx, "#define SIGTSTP_VAL 20\n\n");

    // FFI: Global library handle and function pointer declarations
    // Collect all extern fn declarations recursively (including from block scopes and modules)
    ExternFnList all_extern_fns = {NULL, 0, 0};
    collect_extern_fn_from_stmts(stmts, stmt_count, &all_extern_fns);

    // Also collect from imported modules
    if (ctx->module_cache) {
        CompiledModule *mod = ctx->module_cache->modules;
        while (mod) {
            collect_extern_fn_from_stmts(mod->statements, mod->num_statements, &all_extern_fns);
            mod = mod->next;
        }
    }

    int has_ffi = 0;
    for (int i = 0; i < stmt_count; i++) {
        if (stmts[i]->type == STMT_IMPORT_FFI) {
            has_ffi = 1;
            break;
        }
    }
    // Also check modules for FFI imports
    if (!has_ffi && ctx->module_cache) {
        CompiledModule *mod = ctx->module_cache->modules;
        while (mod && !has_ffi) {
            for (int i = 0; i < mod->num_statements; i++) {
                if (mod->statements[i]->type == STMT_IMPORT_FFI) {
                    has_ffi = 1;
                    break;
                }
            }
            mod = mod->next;
        }
    }
    if (!has_ffi && all_extern_fns.count > 0) {
        has_ffi = 1;
    }
    if (has_ffi) {
        codegen_write(ctx, "// FFI globals\n");
        codegen_write(ctx, "static HmlValue _ffi_lib = {0};\n");
        for (int i = 0; i < all_extern_fns.count; i++) {
            codegen_write(ctx, "static void *_ffi_ptr_%s = NULL;\n",
                        all_extern_fns.stmts[i]->as.extern_fn.function_name);
        }
        codegen_write(ctx, "\n");
    }

    // Track declared static globals to avoid C redefinition errors
    // (when Hemlock code redeclares a variable - that's a semantic error caught elsewhere)
    char **declared_statics = NULL;
    int num_declared_statics = 0;
    int declared_statics_capacity = 0;

    // Helper macro to check if variable was already declared
    #define IS_STATIC_DECLARED(name) ({ \
        int found = 0; \
        for (int j = 0; j < num_declared_statics; j++) { \
            if (strcmp(declared_statics[j], (name)) == 0) { found = 1; break; } \
        } \
        found; \
    })
    #define ADD_STATIC_DECLARED(name) do { \
        if (num_declared_statics >= declared_statics_capacity) { \
            declared_statics_capacity = declared_statics_capacity == 0 ? 16 : declared_statics_capacity * 2; \
            declared_statics = realloc(declared_statics, declared_statics_capacity * sizeof(char*)); \
        } \
        declared_statics[num_declared_statics++] = strdup(name); \
    } while(0)

    // Static globals for top-level function variables (so closures can access them)
    // Use _main_ prefix to avoid C name conflicts (e.g., kill, exit, fork)
    // Note: main_vars are already collected in the pre-pass above
    int has_toplevel_funcs = 0;
    for (int i = 0; i < stmt_count; i++) {
        char *name;
        Expr *func;
        if (is_function_def(stmts[i], &name, &func)) {
            if (!IS_STATIC_DECLARED(name)) {
                if (!has_toplevel_funcs) {
                    codegen_write(ctx, "// Top-level function variables (static for closure access)\n");
                    has_toplevel_funcs = 1;
                }
                codegen_write(ctx, "static HmlValue _main_%s = {0};\n", name);
                ADD_STATIC_DECLARED(name);
            }
        }
    }
    if (has_toplevel_funcs) {
        codegen_write(ctx, "\n");
    }

    // Static globals for top-level const and let declarations (so functions can access them)
    // Use _main_ prefix to avoid C name conflicts
    // Note: main_vars are already collected in the pre-pass above
    int has_toplevel_vars = 0;
    for (int i = 0; i < stmt_count; i++) {
        Stmt *stmt = stmts[i];
        // Unwrap export statements
        if (stmt->type == STMT_EXPORT && stmt->as.export_stmt.is_declaration && stmt->as.export_stmt.declaration) {
            stmt = stmt->as.export_stmt.declaration;
        }

        if (stmt->type == STMT_CONST) {
            if (!IS_STATIC_DECLARED(stmt->as.const_stmt.name)) {
                if (!has_toplevel_vars) {
                    codegen_write(ctx, "// Top-level variables (static for function access)\n");
                    has_toplevel_vars = 1;
                }
                codegen_write(ctx, "static HmlValue _main_%s = {0};\n", stmt->as.const_stmt.name);
                ADD_STATIC_DECLARED(stmt->as.const_stmt.name);
            }
        } else if (stmt->type == STMT_LET) {
            // Check if this is NOT a function definition (those are handled above)
            char *name;
            Expr *func;
            if (!is_function_def(stmt, &name, &func)) {
                if (!IS_STATIC_DECLARED(stmt->as.let.name)) {
                    if (!has_toplevel_vars) {
                        codegen_write(ctx, "// Top-level variables (static for function access)\n");
                        has_toplevel_vars = 1;
                    }
                    codegen_write(ctx, "static HmlValue _main_%s = {0};\n", stmt->as.let.name);
                    ADD_STATIC_DECLARED(stmt->as.let.name);
                }
            }
        }
    }
    if (has_toplevel_vars) {
        codegen_write(ctx, "\n");
    }

    // Static globals for top-level enum declarations (so functions can access them)
    int has_toplevel_enums = 0;
    for (int i = 0; i < stmt_count; i++) {
        Stmt *stmt = stmts[i];
        // Unwrap export statements
        if (stmt->type == STMT_EXPORT && stmt->as.export_stmt.is_declaration && stmt->as.export_stmt.declaration) {
            stmt = stmt->as.export_stmt.declaration;
        }

        if (stmt->type == STMT_ENUM) {
            if (!IS_STATIC_DECLARED(stmt->as.enum_decl.name)) {
                if (!has_toplevel_enums) {
                    codegen_write(ctx, "// Top-level enum declarations (static for function access)\n");
                    has_toplevel_enums = 1;
                }
                codegen_write(ctx, "static HmlValue _main_%s = {0};\n", stmt->as.enum_decl.name);
                ADD_STATIC_DECLARED(stmt->as.enum_decl.name);
            }
        }
    }
    if (has_toplevel_enums) {
        codegen_write(ctx, "\n");
    }

    // Clean up helper macros and memory
    #undef IS_STATIC_DECLARED
    #undef ADD_STATIC_DECLARED
    for (int j = 0; j < num_declared_statics; j++) {
        free(declared_statics[j]);
    }
    free(declared_statics);

    // Generate closure implementations to a buffer first (this may create nested closures)
    FILE *closure_buffer = tmpfile();
    FILE *saved_for_closures = ctx->output;
    ctx->output = closure_buffer;

    // Iteratively generate closures until no new ones are created
    // This handles nested closures (functions inside functions)
    ClosureInfo *processed_tail = NULL;
    while (ctx->closures != processed_tail) {
        ClosureInfo *c = ctx->closures;
        while (c != processed_tail) {
            // Find the last one before processed_tail to process in order
            ClosureInfo *to_process = c;
            while (to_process->next != processed_tail) {
                to_process = to_process->next;
            }
            codegen_closure_impl(ctx, to_process);
            if (processed_tail == NULL) {
                processed_tail = to_process;
            } else {
                // Move processed_tail backward
                ClosureInfo *prev = ctx->closures;
                while (prev->next != processed_tail) {
                    prev = prev->next;
                }
                processed_tail = prev;
            }
            // Re-check from start in case new closures were prepended
            c = ctx->closures;
        }
    }
    ctx->output = saved_for_closures;

    // Now generate forward declarations for ALL closures (including nested ones)
    if (ctx->closures) {
        codegen_write(ctx, "// Closure forward declarations\n");
        ClosureInfo *c = ctx->closures;
        while (c) {
            Expr *func = c->func_expr;
            codegen_write(ctx, "HmlValue %s(HmlClosureEnv *_closure_env", c->func_name);
            for (int i = 0; i < func->as.function.num_params; i++) {
                codegen_write(ctx, ", HmlValue %s", func->as.function.param_names[i]);
            }
            codegen_write(ctx, ");\n");
            c = c->next;
        }
        codegen_write(ctx, "\n");
    }

    // Module global variables and forward declarations
    if (ctx->module_cache && ctx->module_cache->modules) {
        codegen_write(ctx, "// Module global variables\n");
        CompiledModule *mod = ctx->module_cache->modules;
        while (mod) {
            // Generate global variable for each export
            for (int i = 0; i < mod->num_exports; i++) {
                codegen_write(ctx, "static HmlValue %s = {0};\n", mod->exports[i].mangled_name);
            }
            // Also generate global variables for non-exported (private) variables
            for (int i = 0; i < mod->num_statements; i++) {
                Stmt *stmt = mod->statements[i];
                // Skip exports (already handled above)
                if (stmt->type == STMT_EXPORT) continue;
                // Check if it's a private const
                if (stmt->type == STMT_CONST) {
                    codegen_write(ctx, "static HmlValue %s%s = {0};\n",
                                mod->module_prefix, stmt->as.const_stmt.name);
                }
                // Check if it's a private let (function or not)
                if (stmt->type == STMT_LET) {
                    codegen_write(ctx, "static HmlValue %s%s = {0};\n",
                                mod->module_prefix, stmt->as.let.name);
                }
            }
            mod = mod->next;
        }
        codegen_write(ctx, "\n");

        // Module function forward declarations (from buffer)
        codegen_write(ctx, "// Module function forward declarations\n");
        rewind(module_decl_buffer);
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), module_decl_buffer)) > 0) {
            fwrite(buf, 1, n, ctx->output);
        }
        codegen_write(ctx, "\n");

        // Module init function forward declarations
        codegen_write(ctx, "// Module init function declarations\n");
        mod = ctx->module_cache->modules;
        while (mod) {
            codegen_write(ctx, "static void %sinit(void);\n", mod->module_prefix);
            mod = mod->next;
        }
        codegen_write(ctx, "\n");
    }

    // Forward declarations for named functions
    codegen_write(ctx, "// Named function forward declarations\n");
    for (int i = 0; i < stmt_count; i++) {
        char *name;
        Expr *func;
        if (is_function_def(stmts[i], &name, &func)) {
            // All functions use closure env as first param for uniform calling convention
            codegen_write(ctx, "HmlValue hml_fn_%s(HmlClosureEnv *_closure_env", name);
            for (int j = 0; j < func->as.function.num_params; j++) {
                codegen_write(ctx, ", HmlValue %s", func->as.function.param_names[j]);
            }
            codegen_write(ctx, ");\n");
        }
    }
    // Forward declarations for extern functions (including from block scopes)
    for (int i = 0; i < all_extern_fns.count; i++) {
        const char *fn_name = all_extern_fns.stmts[i]->as.extern_fn.function_name;
        int num_params = all_extern_fns.stmts[i]->as.extern_fn.num_params;
        codegen_write(ctx, "HmlValue hml_fn_%s(HmlClosureEnv *_closure_env", fn_name);
        for (int j = 0; j < num_params; j++) {
            codegen_write(ctx, ", HmlValue _arg%d", j);
        }
        codegen_write(ctx, ");\n");
    }
    codegen_write(ctx, "\n");

    // Output closure implementations from buffer
    if (ctx->closures) {
        codegen_write(ctx, "// Closure implementations\n");
        rewind(closure_buffer);
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), closure_buffer)) > 0) {
            fwrite(buf, 1, n, ctx->output);
        }
    }
    fclose(closure_buffer);

    // FFI extern function wrapper implementations (including from block scopes)
    for (int i = 0; i < all_extern_fns.count; i++) {
        Stmt *stmt = all_extern_fns.stmts[i];
        const char *fn_name = stmt->as.extern_fn.function_name;
        int num_params = stmt->as.extern_fn.num_params;
        Type *return_type = stmt->as.extern_fn.return_type;

        codegen_write(ctx, "// FFI wrapper for %s\n", fn_name);
        codegen_write(ctx, "HmlValue hml_fn_%s(HmlClosureEnv *_env", fn_name);
        for (int j = 0; j < num_params; j++) {
            codegen_write(ctx, ", HmlValue _arg%d", j);
        }
        codegen_write(ctx, ") {\n");
        codegen_write(ctx, "    (void)_env;\n");
        codegen_write(ctx, "    if (!_ffi_ptr_%s) {\n", fn_name);
        codegen_write(ctx, "        _ffi_ptr_%s = hml_ffi_sym(_ffi_lib, \"%s\");\n", fn_name, fn_name);
        codegen_write(ctx, "    }\n");
        codegen_write(ctx, "    HmlFFIType _types[%d];\n", num_params + 1);

        // Return type
        const char *ret_str = "HML_FFI_VOID";
        if (return_type) {
            switch (return_type->kind) {
                case TYPE_I8: ret_str = "HML_FFI_I8"; break;
                case TYPE_I16: ret_str = "HML_FFI_I16"; break;
                case TYPE_I32: ret_str = "HML_FFI_I32"; break;
                case TYPE_I64: ret_str = "HML_FFI_I64"; break;
                case TYPE_U8: ret_str = "HML_FFI_U8"; break;
                case TYPE_U16: ret_str = "HML_FFI_U16"; break;
                case TYPE_U32: ret_str = "HML_FFI_U32"; break;
                case TYPE_U64: ret_str = "HML_FFI_U64"; break;
                case TYPE_F32: ret_str = "HML_FFI_F32"; break;
                case TYPE_F64: ret_str = "HML_FFI_F64"; break;
                case TYPE_PTR: ret_str = "HML_FFI_PTR"; break;
                case TYPE_STRING: ret_str = "HML_FFI_STRING"; break;
                default: ret_str = "HML_FFI_I32"; break;
            }
        }
        codegen_write(ctx, "    _types[0] = %s;\n", ret_str);

        // Parameter types
        for (int j = 0; j < num_params; j++) {
            Type *ptype = stmt->as.extern_fn.param_types[j];
            const char *type_str = "HML_FFI_I32";
            if (ptype) {
                switch (ptype->kind) {
                    case TYPE_I8: type_str = "HML_FFI_I8"; break;
                    case TYPE_I16: type_str = "HML_FFI_I16"; break;
                    case TYPE_I32: type_str = "HML_FFI_I32"; break;
                    case TYPE_I64: type_str = "HML_FFI_I64"; break;
                    case TYPE_U8: type_str = "HML_FFI_U8"; break;
                    case TYPE_U16: type_str = "HML_FFI_U16"; break;
                    case TYPE_U32: type_str = "HML_FFI_U32"; break;
                    case TYPE_U64: type_str = "HML_FFI_U64"; break;
                    case TYPE_F32: type_str = "HML_FFI_F32"; break;
                    case TYPE_F64: type_str = "HML_FFI_F64"; break;
                    case TYPE_PTR: type_str = "HML_FFI_PTR"; break;
                    case TYPE_STRING: type_str = "HML_FFI_STRING"; break;
                    default: type_str = "HML_FFI_I32"; break;
                }
            }
            codegen_write(ctx, "    _types[%d] = %s;\n", j + 1, type_str);
        }

        if (num_params > 0) {
            codegen_write(ctx, "    HmlValue _args[%d];\n", num_params);
            for (int j = 0; j < num_params; j++) {
                codegen_write(ctx, "    _args[%d] = _arg%d;\n", j, j);
            }
            codegen_write(ctx, "    return hml_ffi_call(_ffi_ptr_%s, _args, %d, _types);\n", fn_name, num_params);
        } else {
            codegen_write(ctx, "    return hml_ffi_call(_ffi_ptr_%s, NULL, 0, _types);\n", fn_name);
        }
        codegen_write(ctx, "}\n\n");
    }
    // Free the extern fn list
    free(all_extern_fns.stmts);

    // Module function implementations (from buffer)
    if (ctx->module_cache && ctx->module_cache->modules) {
        codegen_write(ctx, "// Module function implementations\n");
        rewind(module_impl_buffer);
        char mbuf[4096];
        size_t mn;
        while ((mn = fread(mbuf, 1, sizeof(mbuf), module_impl_buffer)) > 0) {
            fwrite(mbuf, 1, mn, ctx->output);
        }

        // Module init function implementations
        codegen_write(ctx, "// Module init functions\n");
        CompiledModule *mod = ctx->module_cache->modules;
        while (mod) {
            codegen_module_init(ctx, mod);
            mod = mod->next;
        }
    }
    fclose(module_decl_buffer);
    fclose(module_impl_buffer);

    // Named function implementations (from buffer)
    codegen_write(ctx, "// Named function implementations\n");
    rewind(func_buffer);
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), func_buffer)) > 0) {
        fwrite(buf, 1, n, ctx->output);
    }
    fclose(func_buffer);

    // Main function (from buffer)
    rewind(main_buffer);
    while ((n = fread(buf, 1, sizeof(buf), main_buffer)) > 0) {
        fwrite(buf, 1, n, ctx->output);
    }
    fclose(main_buffer);
}

// ========== MODULE COMPILATION ==========

// Find the stdlib directory path
static char* find_stdlib_path(void) {
    char exe_path[PATH_MAX];
    char resolved[PATH_MAX];

    // Try to get the executable path
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';

        // Get directory of executable
        char *dir = dirname(exe_path);

        // Try: executable_dir/stdlib
        snprintf(resolved, PATH_MAX, "%s/stdlib", dir);
        if (access(resolved, F_OK) == 0) {
            return realpath(resolved, NULL);
        }

        // Try: executable_dir/../stdlib (for build directory structure)
        snprintf(resolved, PATH_MAX, "%s/../stdlib", dir);
        if (access(resolved, F_OK) == 0) {
            return realpath(resolved, NULL);
        }
    }

    // Fallback: try current working directory + stdlib
    if (getcwd(resolved, sizeof(resolved))) {
        char stdlib_path[PATH_MAX];
        int ret = snprintf(stdlib_path, sizeof(stdlib_path), "%s/stdlib", resolved);
        if (ret > 0 && ret < (int)sizeof(stdlib_path)) {
            if (access(stdlib_path, F_OK) == 0) {
                return realpath(stdlib_path, NULL);
            }
        }
    }

    // Last resort: use /usr/local/lib/hemlock/stdlib
    if (access("/usr/local/lib/hemlock/stdlib", F_OK) == 0) {
        return strdup("/usr/local/lib/hemlock/stdlib");
    }

    return NULL;
}

ModuleCache* module_cache_new(const char *main_file_path) {
    ModuleCache *cache = malloc(sizeof(ModuleCache));
    cache->modules = NULL;
    cache->module_counter = 0;

    // Get current working directory
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd))) {
        cache->current_dir = strdup(cwd);
    } else {
        cache->current_dir = strdup(".");
    }

    // Get directory of main file
    if (main_file_path) {
        char *path_copy = strdup(main_file_path);
        char *dir = dirname(path_copy);
        cache->main_file_dir = realpath(dir, NULL);
        if (!cache->main_file_dir) {
            cache->main_file_dir = strdup(dir);
        }
        free(path_copy);
    } else {
        cache->main_file_dir = strdup(cache->current_dir);
    }

    // Find stdlib path
    cache->stdlib_path = find_stdlib_path();

    return cache;
}

void module_cache_free(ModuleCache *cache) {
    if (!cache) return;

    CompiledModule *mod = cache->modules;
    while (mod) {
        CompiledModule *next = mod->next;
        free(mod->absolute_path);
        free(mod->module_prefix);

        // Free statements
        for (int i = 0; i < mod->num_statements; i++) {
            stmt_free(mod->statements[i]);
        }
        free(mod->statements);

        // Free exports
        for (int i = 0; i < mod->num_exports; i++) {
            free(mod->exports[i].name);
            free(mod->exports[i].mangled_name);
        }
        free(mod->exports);

        free(mod);
        mod = next;
    }

    free(cache->current_dir);
    free(cache->main_file_dir);
    if (cache->stdlib_path) {
        free(cache->stdlib_path);
    }
    free(cache);
}

char* module_resolve_path(ModuleCache *cache, const char *importer_path, const char *import_path) {
    char resolved[PATH_MAX];

    // Check for @stdlib alias
    if (strncmp(import_path, "@stdlib/", 8) == 0) {
        if (!cache->stdlib_path) {
            fprintf(stderr, "Error: @stdlib alias used but stdlib directory not found\n");
            return NULL;
        }

        // Replace @stdlib with actual stdlib path
        const char *module_subpath = import_path + 8;  // Skip "@stdlib/"
        snprintf(resolved, PATH_MAX, "%s/%s", cache->stdlib_path, module_subpath);
    }
    // If import_path is absolute, use it directly
    else if (import_path[0] == '/') {
        strncpy(resolved, import_path, PATH_MAX);
        resolved[PATH_MAX - 1] = '\0';
    } else {
        // Relative path - resolve relative to importer's directory
        const char *base_dir;
        char importer_dir[PATH_MAX];

        if (importer_path) {
            strncpy(importer_dir, importer_path, PATH_MAX);
            importer_dir[PATH_MAX - 1] = '\0';
            char *dir = dirname(importer_dir);
            base_dir = dir;
        } else {
            base_dir = cache->main_file_dir;
        }

        snprintf(resolved, PATH_MAX, "%s/%s", base_dir, import_path);
    }

    // Add .hml extension if not present
    size_t len = strlen(resolved);
    if (len < 4 || strcmp(resolved + len - 4, ".hml") != 0) {
        if (len + 4 < PATH_MAX) {
            strcat(resolved, ".hml");
        }
    }

    // Resolve to absolute canonical path
    char *absolute = realpath(resolved, NULL);
    if (!absolute) {
        // File doesn't exist - return the resolved path anyway for error reporting
        return strdup(resolved);
    }

    return absolute;
}

CompiledModule* module_get_cached(ModuleCache *cache, const char *absolute_path) {
    CompiledModule *mod = cache->modules;
    while (mod) {
        if (strcmp(mod->absolute_path, absolute_path) == 0) {
            return mod;
        }
        mod = mod->next;
    }
    return NULL;
}

void module_add_export(CompiledModule *module, const char *name, const char *mangled_name) {
    if (module->num_exports >= module->export_capacity) {
        module->export_capacity = module->export_capacity == 0 ? 16 : module->export_capacity * 2;
        module->exports = realloc(module->exports, sizeof(ExportedSymbol) * module->export_capacity);
    }
    module->exports[module->num_exports].name = strdup(name);
    module->exports[module->num_exports].mangled_name = strdup(mangled_name);
    module->num_exports++;
}

ExportedSymbol* module_find_export(CompiledModule *module, const char *name) {
    for (int i = 0; i < module->num_exports; i++) {
        if (strcmp(module->exports[i].name, name) == 0) {
            return &module->exports[i];
        }
    }
    return NULL;
}

void module_add_import(CompiledModule *module, const char *local_name, const char *original_name, const char *module_prefix, int is_function) {
    if (module->num_imports >= module->import_capacity) {
        module->import_capacity = module->import_capacity == 0 ? 16 : module->import_capacity * 2;
        module->imports = realloc(module->imports, sizeof(ImportBinding) * module->import_capacity);
    }
    module->imports[module->num_imports].local_name = strdup(local_name);
    module->imports[module->num_imports].original_name = strdup(original_name);
    module->imports[module->num_imports].module_prefix = strdup(module_prefix);
    module->imports[module->num_imports].is_function = is_function;
    module->num_imports++;
}

ImportBinding* module_find_import(CompiledModule *module, const char *name) {
    if (!module) return NULL;
    for (int i = 0; i < module->num_imports; i++) {
        if (strcmp(module->imports[i].local_name, name) == 0) {
            return &module->imports[i];
        }
    }
    return NULL;
}

// Check if a name is an extern function in the given module
static int module_is_extern_fn(CompiledModule *module, const char *name) {
    if (!module || !module->statements) return 0;
    for (int i = 0; i < module->num_statements; i++) {
        if (module->statements[i]->type == STMT_EXTERN_FN) {
            if (strcmp(module->statements[i]->as.extern_fn.function_name, name) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

char* module_gen_prefix(ModuleCache *cache) {
    char *prefix = malloc(32);
    snprintf(prefix, 32, "_mod%d_", cache->module_counter++);
    return prefix;
}

void codegen_set_module_cache(CodegenContext *ctx, ModuleCache *cache) {
    ctx->module_cache = cache;
}

// Parse a module file
static Stmt** parse_module_file(const char *path, int *stmt_count) {
    FILE *file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "Error: Cannot open module file '%s'\n", path);
        *stmt_count = 0;
        return NULL;
    }

    // Read entire file
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *source = malloc(file_size + 1);
    fread(source, 1, file_size, file);
    source[file_size] = '\0';
    fclose(file);

    // Parse
    Lexer lexer;
    lexer_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    Stmt **statements = parse_program(&parser, stmt_count);

    free(source);

    if (parser.had_error) {
        fprintf(stderr, "Error: Failed to parse module '%s'\n", path);
        *stmt_count = 0;
        return NULL;
    }

    return statements;
}

CompiledModule* module_compile(CodegenContext *ctx, const char *absolute_path) {
    ModuleCache *cache = ctx->module_cache;

    // Check if already compiled
    CompiledModule *cached = module_get_cached(cache, absolute_path);
    if (cached) {
        if (cached->state == MOD_LOADING) {
            fprintf(stderr, "Error: Circular dependency detected when compiling '%s'\n", absolute_path);
            return NULL;
        }
        return cached;
    }

    // Create new module
    CompiledModule *module = malloc(sizeof(CompiledModule));
    module->absolute_path = strdup(absolute_path);
    module->module_prefix = module_gen_prefix(cache);
    module->state = MOD_LOADING;
    module->exports = NULL;
    module->num_exports = 0;
    module->export_capacity = 0;

    // Add to cache (for cycle detection)
    module->next = cache->modules;
    cache->modules = module;

    // Parse the module
    module->statements = parse_module_file(absolute_path, &module->num_statements);
    if (!module->statements) {
        module->state = MOD_UNLOADED;
        return NULL;
    }

    // First pass: recursively compile imported modules and track import bindings
    for (int i = 0; i < module->num_statements; i++) {
        Stmt *stmt = module->statements[i];
        if (stmt->type == STMT_IMPORT) {
            char *import_path = stmt->as.import_stmt.module_path;
            char *resolved = module_resolve_path(cache, absolute_path, import_path);
            if (!resolved) {
                fprintf(stderr, "Error: Could not resolve import '%s' in '%s'\n", import_path, absolute_path);
                return NULL;
            }

            CompiledModule *imported = module_compile(ctx, resolved);
            free(resolved);

            if (!imported) {
                fprintf(stderr, "Error: Failed to compile imported module '%s'\n", import_path);
                return NULL;
            }

            // Track import bindings for this module
            if (stmt->as.import_stmt.is_namespace) {
                // Namespace imports don't need individual bindings - they're accessed via object
            } else {
                // Named imports: track each import binding
                for (int j = 0; j < stmt->as.import_stmt.num_imports; j++) {
                    const char *import_name = stmt->as.import_stmt.import_names[j];
                    const char *alias = stmt->as.import_stmt.import_aliases[j];
                    const char *bind_name = alias ? alias : import_name;

                    ExportedSymbol *exp = module_find_export(imported, import_name);
                    if (exp) {
                        // Track this as an import binding with original name and module prefix
                        module_add_import(module, bind_name, import_name, imported->module_prefix, 1);
                    }
                }
            }
        }
    }

    // Second pass: collect exports
    for (int i = 0; i < module->num_statements; i++) {
        Stmt *stmt = module->statements[i];
        if (stmt->type == STMT_EXPORT) {
            if (stmt->as.export_stmt.is_declaration) {
                Stmt *decl = stmt->as.export_stmt.declaration;
                const char *name = NULL;
                if (decl->type == STMT_LET) {
                    name = decl->as.let.name;
                } else if (decl->type == STMT_CONST) {
                    name = decl->as.const_stmt.name;
                }
                if (name) {
                    char mangled[256];
                    snprintf(mangled, sizeof(mangled), "%s%s", module->module_prefix, name);
                    module_add_export(module, name, mangled);
                }
            } else if (!stmt->as.export_stmt.is_reexport) {
                // Export list
                for (int j = 0; j < stmt->as.export_stmt.num_exports; j++) {
                    const char *name = stmt->as.export_stmt.export_names[j];
                    const char *alias = stmt->as.export_stmt.export_aliases[j];
                    const char *export_name = alias ? alias : name;

                    char mangled[256];
                    snprintf(mangled, sizeof(mangled), "%s%s", module->module_prefix, name);
                    module_add_export(module, export_name, mangled);
                }
            }
        }
    }

    module->state = MOD_LOADED;
    return module;
}
