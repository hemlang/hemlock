/*
 * Hemlock C Code Generator
 *
 * Translates Hemlock AST to C source code that links against
 * the Hemlock runtime library.
 */

#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

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

        default:
            break;
    }
}

// ========== EXPRESSION CODE GENERATION ==========

// Forward declarations
static void codegen_function_decl(CodegenContext *ctx, Expr *func, const char *name);

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
            // Reference the variable directly
            codegen_writeln(ctx, "HmlValue %s = %s;", result, expr->as.ident);
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

                // Handle user-defined function by name (hml_fn_<name>)
                if (codegen_is_local(ctx, fn_name)) {
                    // It's a local function variable - call through hml_call_function
                    // Fall through to generic handling
                } else {
                    // Try to call as hml_fn_<name> directly
                    char **arg_temps = malloc(expr->as.call.num_args * sizeof(char*));
                    for (int i = 0; i < expr->as.call.num_args; i++) {
                        arg_temps[i] = codegen_expr(ctx, expr->as.call.args[i]);
                    }

                    codegen_write(ctx, "");
                    codegen_indent(ctx);
                    fprintf(ctx->output, "HmlValue %s = hml_fn_%s(", result, fn_name);
                    for (int i = 0; i < expr->as.call.num_args; i++) {
                        if (i > 0) fprintf(ctx->output, ", ");
                        fprintf(ctx->output, "%s", arg_temps[i]);
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

                // String methods
                if (strcmp(method, "substr") == 0 && expr->as.call.num_args == 2) {
                    codegen_writeln(ctx, "HmlValue %s = hml_string_substr(%s, %s, %s);",
                                  result, obj_val, arg_temps[0], arg_temps[1]);
                } else if (strcmp(method, "slice") == 0 && expr->as.call.num_args == 2) {
                    codegen_writeln(ctx, "HmlValue %s = hml_string_slice(%s, %s, %s);",
                                  result, obj_val, arg_temps[0], arg_temps[1]);
                } else if (strcmp(method, "find") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_string_find(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if (strcmp(method, "contains") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_string_contains(%s, %s);",
                                  result, obj_val, arg_temps[0]);
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
                // Array methods
                } else if (strcmp(method, "push") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "hml_array_push(%s, %s);", obj_val, arg_temps[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                } else if (strcmp(method, "pop") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_array_pop(%s);", result, obj_val);
                } else if (strcmp(method, "shift") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_array_shift(%s);", result, obj_val);
                } else if (strcmp(method, "unshift") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "hml_array_unshift(%s, %s);", obj_val, arg_temps[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                } else if (strcmp(method, "insert") == 0 && expr->as.call.num_args == 2) {
                    codegen_writeln(ctx, "hml_array_insert(%s, %s, %s);",
                                  obj_val, arg_temps[0], arg_temps[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                } else if (strcmp(method, "remove") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_array_remove(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if ((strcmp(method, "find") == 0 || strcmp(method, "indexOf") == 0)
                           && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_array_find(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if (strcmp(method, "contains") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_array_contains(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if (strcmp(method, "slice") == 0 && expr->as.call.num_args == 2) {
                    codegen_writeln(ctx, "HmlValue %s = hml_array_slice(%s, %s, %s);",
                                  result, obj_val, arg_temps[0], arg_temps[1]);
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
                } else {
                    // Unknown method - fall through to generic handling
                    codegen_writeln(ctx, "// Unknown method: %s", method);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
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

            // Generate code for arguments
            char **arg_temps = malloc(expr->as.call.num_args * sizeof(char*));
            for (int i = 0; i < expr->as.call.num_args; i++) {
                arg_temps[i] = codegen_expr(ctx, expr->as.call.args[i]);
            }

            // Build args array
            if (expr->as.call.num_args > 0) {
                codegen_writeln(ctx, "HmlValue _args%d[%d];", ctx->temp_counter, expr->as.call.num_args);
                for (int i = 0; i < expr->as.call.num_args; i++) {
                    codegen_writeln(ctx, "_args%d[%d] = %s;", ctx->temp_counter, i, arg_temps[i]);
                }
                codegen_writeln(ctx, "HmlValue %s = hml_call_function(%s, _args%d, %d);",
                              result, func_val, ctx->temp_counter, expr->as.call.num_args);
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
            codegen_writeln(ctx, "hml_release(&%s);", expr->as.assign.name);
            codegen_writeln(ctx, "%s = %s;", expr->as.assign.name, value);
            codegen_writeln(ctx, "hml_retain(&%s);", expr->as.assign.name);
            codegen_writeln(ctx, "HmlValue %s = %s;", result, expr->as.assign.name);
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
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_object_get_field(%s, \"length\");", result, obj);
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
            ClosureInfo *closure = malloc(sizeof(ClosureInfo));
            closure->func_name = strdup(func_name);
            closure->captured_vars = malloc(captured->num_vars * sizeof(char*));
            closure->num_captured = captured->num_vars;
            for (int i = 0; i < captured->num_vars; i++) {
                closure->captured_vars[i] = strdup(captured->vars[i]);
            }
            closure->func_expr = expr;
            closure->next = ctx->closures;
            ctx->closures = closure;

            if (captured->num_vars == 0) {
                // No captures - simple function pointer
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)%s, %d, %d);",
                              result, func_name, expr->as.function.num_params, expr->as.function.is_async);
            } else {
                // Create closure environment and capture variables
                codegen_writeln(ctx, "HmlClosureEnv *_env_%d = hml_closure_env_new(%d);",
                              ctx->temp_counter, captured->num_vars);
                for (int i = 0; i < captured->num_vars; i++) {
                    codegen_writeln(ctx, "hml_closure_env_set(_env_%d, %d, %s);",
                                  ctx->temp_counter, i, captured->vars[i]);
                }
                codegen_writeln(ctx, "HmlValue %s = hml_val_function_with_env((void*)%s, (void*)_env_%d, %d, %d);",
                              result, func_name, ctx->temp_counter, expr->as.function.num_params, expr->as.function.is_async);
                ctx->temp_counter++;
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
                const char *var = expr->as.prefix_inc.operand->as.ident;
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
                const char *var = expr->as.prefix_dec.operand->as.ident;
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
                const char *var = expr->as.postfix_inc.operand->as.ident;
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
                const char *var = expr->as.postfix_dec.operand->as.ident;
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
                codegen_writeln(ctx, "HmlValue %s = %s;", stmt->as.let.name, value);
                free(value);
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
                // Catch block
                if (stmt->as.try_stmt.catch_param) {
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

            codegen_writeln(ctx, "{");
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "int _matched = 0;");

            // Generate case comparisons
            for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
                if (stmt->as.switch_stmt.case_values[i] == NULL) continue;  // Skip default

                char *case_val = codegen_expr(ctx, stmt->as.switch_stmt.case_values[i]);
                if (i == 0) {
                    codegen_writeln(ctx, "if (hml_to_bool(hml_binary_op(HML_OP_EQUAL, %s, %s))) {", expr_val, case_val);
                } else {
                    codegen_writeln(ctx, "} else if (hml_to_bool(hml_binary_op(HML_OP_EQUAL, %s, %s))) {", expr_val, case_val);
                }
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "_matched = 1;");
                codegen_stmt(ctx, stmt->as.switch_stmt.case_bodies[i]);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "hml_release(&%s);", case_val);
                free(case_val);
            }

            if (has_default) {
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_stmt(ctx, stmt->as.switch_stmt.case_bodies[default_idx]);
                codegen_indent_dec(ctx);
            }
            codegen_writeln(ctx, "}");

            codegen_writeln(ctx, "hml_release(&%s);", expr_val);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            free(expr_val);
            break;
        }

        case STMT_DEFER: {
            // Push the expression onto the defer stack - will be executed at function return
            codegen_defer_push(ctx, stmt->as.defer_stmt.call);
            break;
        }

        default:
            codegen_writeln(ctx, "// Unsupported statement type %d", stmt->type);
            break;
    }
}

// ========== PROGRAM CODE GENERATION ==========

// Check if a statement is a function definition (let name = fn() {})
static int is_function_def(Stmt *stmt, char **name_out, Expr **func_out) {
    if (stmt->type == STMT_LET && stmt->as.let.value &&
        stmt->as.let.value->type == EXPR_FUNCTION) {
        *name_out = stmt->as.let.name;
        *func_out = stmt->as.let.value;
        return 1;
    }
    return 0;
}

// Generate a top-level function declaration
static void codegen_function_decl(CodegenContext *ctx, Expr *func, const char *name) {
    // Generate function signature
    codegen_write(ctx, "HmlValue hml_fn_%s(", name);
    for (int i = 0; i < func->as.function.num_params; i++) {
        if (i > 0) codegen_write(ctx, ", ");
        codegen_write(ctx, "HmlValue %s", func->as.function.param_names[i]);
    }
    codegen_write(ctx, ") {\n");
    codegen_indent_inc(ctx);

    // Save locals and defer state
    int saved_num_locals = ctx->num_locals;
    DeferEntry *saved_defer_stack = ctx->defer_stack;
    ctx->defer_stack = NULL;  // Start fresh for this function

    // Add parameters as locals
    for (int i = 0; i < func->as.function.num_params; i++) {
        codegen_add_local(ctx, func->as.function.param_names[i]);
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

    // Save locals and defer state
    int saved_num_locals = ctx->num_locals;
    DeferEntry *saved_defer_stack = ctx->defer_stack;
    ctx->defer_stack = NULL;  // Start fresh for this function

    // Add parameters as locals
    for (int i = 0; i < func->as.function.num_params; i++) {
        codegen_add_local(ctx, func->as.function.param_names[i]);
    }

    // Extract captured variables from environment
    for (int i = 0; i < closure->num_captured; i++) {
        codegen_writeln(ctx, "HmlValue %s = hml_closure_env_get(_closure_env, %d);",
                      closure->captured_vars[i], i);
        codegen_add_local(ctx, closure->captured_vars[i]);
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

    // Restore locals and defer state
    codegen_defer_clear(ctx);
    ctx->defer_stack = saved_defer_stack;
    ctx->num_locals = saved_num_locals;
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

void codegen_program(CodegenContext *ctx, Stmt **stmts, int stmt_count) {
    // Multi-pass approach:
    // 1. First pass: Generate named function bodies to a buffer to collect closures
    // 2. Output header + all forward declarations (functions + closures)
    // 3. Output closure implementations
    // 4. Output named function implementations
    // 5. Output main function

    // Buffer for named function implementations
    FILE *func_buffer = tmpfile();
    FILE *main_buffer = tmpfile();
    FILE *saved_output = ctx->output;

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

    // Generate global variable declarations for functions
    for (int i = 0; i < stmt_count; i++) {
        char *name;
        Expr *func;
        if (is_function_def(stmts[i], &name, &func)) {
            codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_fn_%s, %d, %d);",
                          name, name, func->as.function.num_params, func->as.function.is_async);
            codegen_add_local(ctx, name);
        }
    }
    codegen_writeln(ctx, "");

    // Generate non-function statements
    for (int i = 0; i < stmt_count; i++) {
        char *name;
        Expr *func;
        if (!is_function_def(stmts[i], &name, &func)) {
            codegen_stmt(ctx, stmts[i]);
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
    codegen_write(ctx, "#include <setjmp.h>\n\n");

    // Forward declarations for closure functions (must come first!)
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

    // Forward declarations for named functions
    codegen_write(ctx, "// Named function forward declarations\n");
    for (int i = 0; i < stmt_count; i++) {
        char *name;
        Expr *func;
        if (is_function_def(stmts[i], &name, &func)) {
            codegen_write(ctx, "HmlValue hml_fn_%s(", name);
            for (int j = 0; j < func->as.function.num_params; j++) {
                if (j > 0) codegen_write(ctx, ", ");
                codegen_write(ctx, "HmlValue %s", func->as.function.param_names[j]);
            }
            codegen_write(ctx, ");\n");
        }
    }
    codegen_write(ctx, "\n");

    // Closure implementations
    if (ctx->closures) {
        codegen_write(ctx, "// Closure implementations\n");
        ClosureInfo *c = ctx->closures;
        while (c) {
            codegen_closure_impl(ctx, c);
            c = c->next;
        }
    }

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
