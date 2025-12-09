/*
 * Hemlock Code Generator - Closure and Free Variable Analysis
 *
 * Handles free variable detection and closure analysis for capturing
 * variables in nested functions.
 */

#include "codegen_internal.h"

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
int shared_env_add_var(CodegenContext *ctx, const char *var) {
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
int shared_env_get_index(CodegenContext *ctx, const char *var) {
    for (int i = 0; i < ctx->shared_env_num_vars; i++) {
        if (strcmp(ctx->shared_env_vars[i], var) == 0) {
            return i;
        }
    }
    return -1;
}

// Clear the shared environment state
void shared_env_clear(CodegenContext *ctx) {
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

// Helper to count required parameters (those without defaults)
int count_required_params(Expr **param_defaults, int num_params) {
    if (!param_defaults) return num_params;  // No defaults array = all required
    int required = 0;
    for (int i = 0; i < num_params; i++) {
        if (param_defaults[i] == NULL) {
            required++;
        }
    }
    return required;
}

// Forward declarations for scanning closures in function body
void scan_closures_expr(CodegenContext *ctx, Expr *expr, Scope *local_scope);
void scan_closures_stmt(CodegenContext *ctx, Stmt *stmt, Scope *local_scope);

// Scan expression for closures and collect their captured variables
void scan_closures_expr(CodegenContext *ctx, Expr *expr, Scope *local_scope) {
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
void scan_closures_stmt(CodegenContext *ctx, Stmt *stmt, Scope *local_scope) {
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
                // Add catch param to scope so closures inside catch don't capture it
                if (stmt->as.try_stmt.catch_param) {
                    scope_add_var(local_scope, stmt->as.try_stmt.catch_param);
                }
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

