/*
 * Hemlock Variable Resolver
 *
 * Performs a static analysis pass over the AST to resolve variable references
 * to their compile-time (depth, slot) locations, enabling O(1) runtime lookup.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "resolver.h"

/*
 * Create a new resolver scope.
 */
static ResolverScope *scope_new(ResolverScope *parent) {
    ResolverScope *scope = malloc(sizeof(ResolverScope));
    scope->count = 0;
    scope->capacity = 8;
    scope->names = malloc(sizeof(char *) * scope->capacity);
    scope->parent = parent;
    return scope;
}

/*
 * Free a resolver scope (does NOT free parent).
 */
static void scope_free(ResolverScope *scope) {
    if (!scope) return;
    for (int i = 0; i < scope->count; i++) {
        free(scope->names[i]);
    }
    free(scope->names);
    free(scope);
}

/*
 * Create a new resolver context.
 */
ResolverContext *resolver_new(void) {
    ResolverContext *ctx = malloc(sizeof(ResolverContext));
    ctx->current = scope_new(NULL);  // Global scope
    ctx->scope_depth = 0;
    return ctx;
}

/*
 * Free a resolver context and all its scopes.
 */
void resolver_free(ResolverContext *ctx) {
    if (!ctx) return;
    // Free all scopes in the chain
    ResolverScope *scope = ctx->current;
    while (scope) {
        ResolverScope *parent = scope->parent;
        scope_free(scope);
        scope = parent;
    }
    free(ctx);
}

/*
 * Enter a new scope.
 */
void resolver_enter_scope(ResolverContext *ctx) {
    ctx->current = scope_new(ctx->current);
    ctx->scope_depth++;
}

/*
 * Exit the current scope.
 */
void resolver_exit_scope(ResolverContext *ctx) {
    if (!ctx->current || !ctx->current->parent) {
        // Don't exit global scope
        return;
    }
    ResolverScope *old = ctx->current;
    ctx->current = old->parent;
    ctx->scope_depth--;
    scope_free(old);
}

/*
 * Define a variable in the current scope.
 * Returns the slot index.
 */
int resolver_define(ResolverContext *ctx, const char *name) {
    ResolverScope *scope = ctx->current;

    // Grow if needed
    if (scope->count >= scope->capacity) {
        scope->capacity *= 2;
        scope->names = realloc(scope->names, sizeof(char *) * scope->capacity);
    }

    int slot = scope->count;
    scope->names[scope->count++] = strdup(name);
    return slot;
}

/*
 * Look up a variable.
 * Returns 1 if found (and sets depth/slot), 0 if not found.
 */
int resolver_lookup(ResolverContext *ctx, const char *name, int *depth, int *slot) {
    ResolverScope *scope = ctx->current;
    int d = 0;

    while (scope) {
        // Search this scope
        for (int i = 0; i < scope->count; i++) {
            if (strcmp(scope->names[i], name) == 0) {
                *depth = d;
                *slot = i;
                return 1;
            }
        }
        // Move to parent scope
        scope = scope->parent;
        d++;
    }

    return 0;  // Not found
}

/*
 * Forward declarations for recursive resolution.
 */
static void resolve_stmt_internal(ResolverContext *ctx, Stmt *stmt);
static void resolve_expr_internal(ResolverContext *ctx, Expr *expr);

/*
 * Resolve an expression.
 */
static void resolve_expr_internal(ResolverContext *ctx, Expr *expr) {
    if (!expr) return;

    switch (expr->type) {
        case EXPR_IDENT: {
            // Look up the variable and store resolution info
            int depth, slot;
            if (resolver_lookup(ctx, expr->as.ident.name, &depth, &slot)) {
                // Only use resolved lookup if we're inside a function scope.
                // At depth 0 (global scope), builtins share the environment so slot
                // indices don't match the resolver's expectations.
                int defining_scope_depth = ctx->scope_depth - depth;
                if (defining_scope_depth > 0) {
                    // Variable is defined inside a function, safe to use resolved lookup
                    expr->as.ident.resolved.is_resolved = 1;
                    expr->as.ident.resolved.depth = depth;
                    expr->as.ident.resolved.slot = slot;
                } else {
                    // Variable is at global scope - fall back to hash lookup
                    expr->as.ident.resolved.is_resolved = 0;
                }
            } else {
                // Variable not found - could be a builtin or global
                expr->as.ident.resolved.is_resolved = 0;
            }
            break;
        }

        case EXPR_ASSIGN: {
            // First resolve the value expression
            resolve_expr_internal(ctx, expr->as.assign.value);

            // Then look up the variable being assigned
            int depth, slot;
            if (resolver_lookup(ctx, expr->as.assign.name, &depth, &slot)) {
                // Only use resolved assignment if inside a function scope
                int defining_scope_depth = ctx->scope_depth - depth;
                if (defining_scope_depth > 0) {
                    expr->as.assign.resolved.is_resolved = 1;
                    expr->as.assign.resolved.depth = depth;
                    expr->as.assign.resolved.slot = slot;
                } else {
                    // Global scope - fall back to hash lookup
                    expr->as.assign.resolved.is_resolved = 0;
                }
            } else {
                // Variable not found - implicit declaration or builtin
                expr->as.assign.resolved.is_resolved = 0;
            }
            break;
        }

        case EXPR_BINARY:
            resolve_expr_internal(ctx, expr->as.binary.left);
            resolve_expr_internal(ctx, expr->as.binary.right);
            break;

        case EXPR_UNARY:
            resolve_expr_internal(ctx, expr->as.unary.operand);
            break;

        case EXPR_TERNARY:
            resolve_expr_internal(ctx, expr->as.ternary.condition);
            resolve_expr_internal(ctx, expr->as.ternary.true_expr);
            resolve_expr_internal(ctx, expr->as.ternary.false_expr);
            break;

        case EXPR_CALL:
            resolve_expr_internal(ctx, expr->as.call.func);
            for (int i = 0; i < expr->as.call.num_args; i++) {
                resolve_expr_internal(ctx, expr->as.call.args[i]);
            }
            break;

        case EXPR_GET_PROPERTY:
            resolve_expr_internal(ctx, expr->as.get_property.object);
            break;

        case EXPR_SET_PROPERTY:
            resolve_expr_internal(ctx, expr->as.set_property.object);
            resolve_expr_internal(ctx, expr->as.set_property.value);
            break;

        case EXPR_INDEX:
            resolve_expr_internal(ctx, expr->as.index.object);
            resolve_expr_internal(ctx, expr->as.index.index);
            break;

        case EXPR_INDEX_ASSIGN:
            resolve_expr_internal(ctx, expr->as.index_assign.object);
            resolve_expr_internal(ctx, expr->as.index_assign.index);
            resolve_expr_internal(ctx, expr->as.index_assign.value);
            break;

        case EXPR_FUNCTION: {
            // Enter a new scope for the function
            resolver_enter_scope(ctx);

            // Define parameters (use param_names not params)
            for (int i = 0; i < expr->as.function.num_params; i++) {
                resolver_define(ctx, expr->as.function.param_names[i]);
            }

            // Define rest parameter if present
            if (expr->as.function.rest_param) {
                resolver_define(ctx, expr->as.function.rest_param);
            }

            // Resolve default parameter expressions
            for (int i = 0; i < expr->as.function.num_params; i++) {
                if (expr->as.function.param_defaults && expr->as.function.param_defaults[i]) {
                    resolve_expr_internal(ctx, expr->as.function.param_defaults[i]);
                }
            }

            // Resolve the function body
            resolve_stmt_internal(ctx, expr->as.function.body);

            resolver_exit_scope(ctx);
            break;
        }

        case EXPR_ARRAY_LITERAL:
            for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                resolve_expr_internal(ctx, expr->as.array_literal.elements[i]);
            }
            break;

        case EXPR_OBJECT_LITERAL:
            for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
                resolve_expr_internal(ctx, expr->as.object_literal.field_values[i]);
            }
            break;

        case EXPR_PREFIX_INC:
            resolve_expr_internal(ctx, expr->as.prefix_inc.operand);
            break;

        case EXPR_PREFIX_DEC:
            resolve_expr_internal(ctx, expr->as.prefix_dec.operand);
            break;

        case EXPR_POSTFIX_INC:
            resolve_expr_internal(ctx, expr->as.postfix_inc.operand);
            break;

        case EXPR_POSTFIX_DEC:
            resolve_expr_internal(ctx, expr->as.postfix_dec.operand);
            break;

        case EXPR_AWAIT:
            resolve_expr_internal(ctx, expr->as.await_expr.awaited_expr);
            break;

        case EXPR_STRING_INTERPOLATION:
            for (int i = 0; i < expr->as.string_interpolation.num_parts; i++) {
                resolve_expr_internal(ctx, expr->as.string_interpolation.expr_parts[i]);
            }
            break;

        case EXPR_OPTIONAL_CHAIN:
            resolve_expr_internal(ctx, expr->as.optional_chain.object);
            if (expr->as.optional_chain.index) {
                resolve_expr_internal(ctx, expr->as.optional_chain.index);
            }
            // Resolve method call arguments if present
            if (expr->as.optional_chain.is_call && expr->as.optional_chain.args) {
                for (int i = 0; i < expr->as.optional_chain.num_args; i++) {
                    resolve_expr_internal(ctx, expr->as.optional_chain.args[i]);
                }
            }
            break;

        case EXPR_NULL_COALESCE:
            resolve_expr_internal(ctx, expr->as.null_coalesce.left);
            resolve_expr_internal(ctx, expr->as.null_coalesce.right);
            break;

        // Literals - no resolution needed
        case EXPR_NUMBER:
        case EXPR_BOOL:
        case EXPR_STRING:
        case EXPR_RUNE:
        case EXPR_NULL:
            break;
    }
}

/*
 * Resolve a statement.
 */
static void resolve_stmt_internal(ResolverContext *ctx, Stmt *stmt) {
    if (!stmt) return;

    switch (stmt->type) {
        case STMT_EXPR:
            resolve_expr_internal(ctx, stmt->as.expr);
            break;

        case STMT_LET: {
            // First resolve the initializer (if any)
            if (stmt->as.let.value) {
                resolve_expr_internal(ctx, stmt->as.let.value);
            }
            // Then define the variable
            resolver_define(ctx, stmt->as.let.name);
            break;
        }

        case STMT_CONST: {
            // First resolve the initializer
            if (stmt->as.const_stmt.value) {
                resolve_expr_internal(ctx, stmt->as.const_stmt.value);
            }
            // Then define the constant
            resolver_define(ctx, stmt->as.const_stmt.name);
            break;
        }

        case STMT_RETURN:
            if (stmt->as.return_stmt.value) {
                resolve_expr_internal(ctx, stmt->as.return_stmt.value);
            }
            break;

        case STMT_IF:
            resolve_expr_internal(ctx, stmt->as.if_stmt.condition);
            resolve_stmt_internal(ctx, stmt->as.if_stmt.then_branch);
            if (stmt->as.if_stmt.else_branch) {
                resolve_stmt_internal(ctx, stmt->as.if_stmt.else_branch);
            }
            break;

        case STMT_WHILE: {
            // While loop creates ONE scope at runtime (iter_env for body).
            // Condition is evaluated in parent scope, body in iter_env.
            resolve_expr_internal(ctx, stmt->as.while_stmt.condition);

            // Enter scope for body (matches iter_env at runtime)
            resolver_enter_scope(ctx);
            resolve_stmt_internal(ctx, stmt->as.while_stmt.body);
            resolver_exit_scope(ctx);
            break;
        }

        case STMT_FOR: {
            // For loop creates TWO scopes at runtime:
            // 1. loop_env: holds the loop variable (e.g., i)
            // 2. iter_env: holds the body's local variables (cleared each iteration)
            // The resolver must match this structure.

            // Enter outer scope for loop variable
            resolver_enter_scope(ctx);

            if (stmt->as.for_loop.initializer) {
                resolve_stmt_internal(ctx, stmt->as.for_loop.initializer);
            }
            if (stmt->as.for_loop.condition) {
                resolve_expr_internal(ctx, stmt->as.for_loop.condition);
            }
            if (stmt->as.for_loop.increment) {
                resolve_expr_internal(ctx, stmt->as.for_loop.increment);
            }

            // Enter inner scope for body (matches iter_env at runtime)
            resolver_enter_scope(ctx);
            resolve_stmt_internal(ctx, stmt->as.for_loop.body);
            resolver_exit_scope(ctx);

            resolver_exit_scope(ctx);
            break;
        }

        case STMT_FOR_IN: {
            // For-in loop creates TWO scopes at runtime (like regular for):
            // 1. loop_env: intermediate scope (empty at runtime)
            // 2. iter_env: holds iterator variables and body's local variables
            // The iterable is evaluated BEFORE these scopes are created.

            // Resolve iterable in current (parent) scope first
            resolve_expr_internal(ctx, stmt->as.for_in.iterable);

            // Enter outer scope (matches loop_env at runtime)
            resolver_enter_scope(ctx);

            // Enter inner scope for iterator variables and body (matches iter_env)
            resolver_enter_scope(ctx);

            // Define the key variable if present
            if (stmt->as.for_in.key_var) {
                resolver_define(ctx, stmt->as.for_in.key_var);
            }

            // Define the value variable
            if (stmt->as.for_in.value_var) {
                resolver_define(ctx, stmt->as.for_in.value_var);
            }

            // Resolve the body
            resolve_stmt_internal(ctx, stmt->as.for_in.body);

            resolver_exit_scope(ctx);
            resolver_exit_scope(ctx);
            break;
        }

        case STMT_BLOCK: {
            // Create a new scope for blocks to enable proper lexical scoping.
            // This allows variables declared with 'let' inside a block to
            // shadow outer variables, matching JavaScript's let/const semantics.
            resolver_enter_scope(ctx);
            for (int i = 0; i < stmt->as.block.count; i++) {
                resolve_stmt_internal(ctx, stmt->as.block.statements[i]);
            }
            resolver_exit_scope(ctx);
            break;
        }

        case STMT_BREAK:
        case STMT_CONTINUE:
            // No expressions to resolve
            break;

        case STMT_SWITCH: {
            resolve_expr_internal(ctx, stmt->as.switch_stmt.expr);
            for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
                if (stmt->as.switch_stmt.case_values[i]) {
                    resolve_expr_internal(ctx, stmt->as.switch_stmt.case_values[i]);
                }
                resolve_stmt_internal(ctx, stmt->as.switch_stmt.case_bodies[i]);
            }
            break;
        }

        case STMT_DEFER:
            resolve_expr_internal(ctx, stmt->as.defer_stmt.call);
            break;

        case STMT_TRY: {
            resolve_stmt_internal(ctx, stmt->as.try_stmt.try_block);

            if (stmt->as.try_stmt.catch_block) {
                // Create scope for catch variable
                resolver_enter_scope(ctx);
                if (stmt->as.try_stmt.catch_param) {
                    resolver_define(ctx, stmt->as.try_stmt.catch_param);
                }
                resolve_stmt_internal(ctx, stmt->as.try_stmt.catch_block);
                resolver_exit_scope(ctx);
            }

            if (stmt->as.try_stmt.finally_block) {
                resolve_stmt_internal(ctx, stmt->as.try_stmt.finally_block);
            }
            break;
        }

        case STMT_THROW:
            resolve_expr_internal(ctx, stmt->as.throw_stmt.value);
            break;

        case STMT_IMPORT:
        case STMT_EXPORT:
            // Imports/exports handled at module level
            break;

        case STMT_DEFINE_OBJECT:
            // Type definitions - resolve default values
            for (int i = 0; i < stmt->as.define_object.num_fields; i++) {
                if (stmt->as.define_object.field_defaults && stmt->as.define_object.field_defaults[i]) {
                    resolve_expr_internal(ctx, stmt->as.define_object.field_defaults[i]);
                }
            }
            break;

        case STMT_ENUM:
            // Enum definitions - resolve explicit values
            for (int i = 0; i < stmt->as.enum_decl.num_variants; i++) {
                if (stmt->as.enum_decl.variant_values && stmt->as.enum_decl.variant_values[i]) {
                    resolve_expr_internal(ctx, stmt->as.enum_decl.variant_values[i]);
                }
            }
            break;

        case STMT_IMPORT_FFI:
        case STMT_EXTERN_FN:
            // FFI declarations - no expressions to resolve
            break;
    }
}

/*
 * Public API: Resolve all variables in a statement.
 */
void resolve_stmt(ResolverContext *ctx, Stmt *stmt) {
    resolve_stmt_internal(ctx, stmt);
}

/*
 * Public API: Resolve all variables in an expression.
 */
void resolve_expr(ResolverContext *ctx, Expr *expr) {
    resolve_expr_internal(ctx, expr);
}

/*
 * Resolve all variables in a program.
 */
void resolve_program(Stmt **statements, int count) {
    ResolverContext *ctx = resolver_new();

    for (int i = 0; i < count; i++) {
        resolve_stmt_internal(ctx, statements[i]);
    }

    resolver_free(ctx);
}
