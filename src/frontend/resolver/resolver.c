/*
 * Hemlock Variable Resolver
 *
 * Performs a static analysis pass over the AST to resolve variable references
 * to their compile-time (depth, slot) locations, enabling O(1) runtime lookup.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "frontend/resolver.h"
#include "frontend/annotations.h"
#include "hemlock_limits.h"

/*
 * Create a new resolver scope.
 */
static ResolverScope *scope_new(ResolverScope *parent) {
    ResolverScope *scope = malloc(sizeof(ResolverScope));
    scope->count = 0;
    scope->capacity = 8;
    scope->names = malloc(sizeof(char *) * scope->capacity);
    scope->annotations = malloc(sizeof(Annotation **) * scope->capacity);
    scope->annotation_counts = malloc(sizeof(int) * scope->capacity);
    scope->is_dynamic = 0;
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
        // Note: annotations are owned by AST nodes, don't free them here
    }
    free(scope->names);
    free(scope->annotations);
    free(scope->annotation_counts);
    free(scope);
}

/*
 * Create a new resolver context.
 */
ResolverContext *resolver_new(void) {
    ResolverContext *ctx = malloc(sizeof(ResolverContext));
    ctx->current = scope_new(NULL);  // Global scope
    ctx->scope_depth = 0;
    ctx->scan_only = 0;
    ctx->capture_count = 0;
    ctx->implicit_assign_count = 0;
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
int resolver_define(ResolverContext *ctx, const char *name, Annotation **annotations, int annotation_count) {
    ResolverScope *scope = ctx->current;

    // Grow if needed
    if (scope->count >= scope->capacity) {
        int new_capacity = scope->capacity * 2;

        // Allocate all three arrays first, only update scope if all succeed
        char **new_names = realloc(scope->names, sizeof(char *) * new_capacity);
        if (!new_names) {
            fprintf(stderr, "Resolver error: Memory allocation failed for names\n");
            return -1;
        }

        Annotation ***new_annotations = realloc(scope->annotations, sizeof(Annotation **) * new_capacity);
        if (!new_annotations) {
            // new_names succeeded but new_annotations failed
            // new_names is now the valid pointer (realloc moved it), so assign it back
            scope->names = new_names;
            fprintf(stderr, "Resolver error: Memory allocation failed for annotations\n");
            return -1;
        }

        int *new_annotation_counts = realloc(scope->annotation_counts, sizeof(int) * new_capacity);
        if (!new_annotation_counts) {
            // new_names and new_annotations succeeded, assign them back
            scope->names = new_names;
            scope->annotations = new_annotations;
            fprintf(stderr, "Resolver error: Memory allocation failed for annotation counts\n");
            return -1;
        }

        scope->names = new_names;
        scope->annotations = new_annotations;
        scope->annotation_counts = new_annotation_counts;
        scope->capacity = new_capacity;
    }

    int slot = scope->count;
    char *name_copy = strdup(name);
    if (!name_copy) {
        fprintf(stderr, "Resolver error: Memory allocation failed for variable name\n");
        return -1;
    }
    scope->names[slot] = name_copy;
    scope->annotations[slot] = annotations;  // Store pointer to annotations (AST owns them)
    scope->annotation_counts[slot] = annotation_count;
    scope->count++;
    return slot;
}

/*
 * Look up a variable.
 * Returns 1 if found (and sets depth/slot), 0 if not found.
 */
// As resolver_lookup, but also reports (via *in_dynamic) whether the first
// match sits in a dynamic scope. Such names are bound by the runtime
// pattern matcher, possibly only on some paths (an or-pattern binds only
// the alternative that matched), so the interpreter's inline cache must
// never pin them to one location.
static int resolver_lookup_ex(ResolverContext *ctx, const char *name, int *depth, int *slot,
                              int *in_dynamic) {
    ResolverScope *scope = ctx->current;
    int d = 0;
    *in_dynamic = 0;

    while (scope) {
        // Search this scope
        for (int i = 0; i < scope->count; i++) {
            if (strcmp(scope->names[i], name) == 0) {
                // Dynamic-scope names have no stable slot; the runtime
                // finds them by name.
                if (scope->is_dynamic) {
                    *in_dynamic = 1;
                    return 0;
                }
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

int resolver_lookup(ResolverContext *ctx, const char *name, int *depth, int *slot) {
    int in_dynamic;
    return resolver_lookup_ex(ctx, name, depth, slot, &in_dynamic);
}

/*
 * Get annotations for a variable by name.
 * Returns 1 if found (and sets annotations/count), 0 if not found.
 */
int resolver_get_annotations(ResolverContext *ctx, const char *name, Annotation ***annotations, int *count) {
    ResolverScope *scope = ctx->current;

    while (scope) {
        // Search this scope
        for (int i = 0; i < scope->count; i++) {
            if (strcmp(scope->names[i], name) == 0) {
                *annotations = scope->annotations[i];
                *count = scope->annotation_counts[i];
                return 1;
            }
        }
        // Move to parent scope
        scope = scope->parent;
    }

    *annotations = NULL;
    *count = 0;
    return 0;  // Not found
}

/*
 * Does executing `stmt` define a binding directly in the environment it
 * runs in (as opposed to a scope of its own)? Blocks and loops with their
 * own runtime environment answer no; statements that run their children in
 * the current environment (if branches, switch cases, loop bodies that are
 * not blocks) answer whatever their children answer.
 */
static int stmt_declares_in_place(Stmt *stmt) {
    if (!stmt) return 0;
    switch (stmt->type) {
        case STMT_LET:
        case STMT_CONST:
        case STMT_DEFINE_OBJECT:
        case STMT_ENUM:
        case STMT_TYPE_ALIAS:
        case STMT_IMPORT:
        case STMT_EXPORT:
        case STMT_IMPORT_FFI:
        case STMT_EXTERN_FN:
            return 1;
        case STMT_IF:
            return stmt_declares_in_place(stmt->as.if_stmt.then_branch) ||
                   stmt_declares_in_place(stmt->as.if_stmt.else_branch);
        case STMT_WHILE:
            return stmt->as.while_stmt.body->type != STMT_BLOCK &&
                   stmt_declares_in_place(stmt->as.while_stmt.body);
        case STMT_LOOP:
            return stmt->as.loop_stmt.body->type != STMT_BLOCK &&
                   stmt_declares_in_place(stmt->as.loop_stmt.body);
        case STMT_SWITCH:
            for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
                if (stmt_declares_in_place(stmt->as.switch_stmt.case_bodies[i])) return 1;
            }
            return 0;
        case STMT_TRY:
            // try/finally bodies are blocks with their own scope; the catch
            // parameter gets a dedicated environment at runtime.
            return stmt_declares_in_place(stmt->as.try_stmt.try_block) ||
                   stmt_declares_in_place(stmt->as.try_stmt.finally_block);
        case STMT_BLOCK:
        case STMT_FOR:
        case STMT_FOR_IN:
        default:
            return 0;
    }
}

static void resolve_stmt_internal(ResolverContext *ctx, Stmt *stmt);

/*
 * Walk `stmt` in scan mode under a throwaway scope and report what it
 * contains. The scan resolves nothing permanently: every node it touches is
 * re-resolved by the caller afterwards.
 */
static void scan_stmt(ResolverContext *ctx, Stmt *stmt, int *captures, int *implicit_assigns) {
    int saved_scan = ctx->scan_only;
    int saved_captures = ctx->capture_count;
    int saved_implicit = ctx->implicit_assign_count;
    ctx->scan_only = 1;
    ctx->capture_count = 0;
    ctx->implicit_assign_count = 0;
    resolver_enter_scope(ctx);
    resolve_stmt_internal(ctx, stmt);
    resolver_exit_scope(ctx);
    *captures = ctx->capture_count;
    *implicit_assigns = ctx->implicit_assign_count;
    ctx->scan_only = saved_scan;
    ctx->capture_count = saved_captures;
    ctx->implicit_assign_count = saved_implicit;
}

/*
 * Forward declarations for recursive resolution.
 */
static void resolve_stmt_internal(ResolverContext *ctx, Stmt *stmt);
static void resolve_expr_internal(ResolverContext *ctx, Expr *expr);

/*
 * Resolve an expression.
 */
/*
 * Declare every name a match pattern can bind in the current scope.
 */
static void resolve_pattern_bindings(ResolverContext *ctx, Pattern *pattern) {
    if (!pattern) return;
    switch (pattern->type) {
        case PATTERN_BINDING:
            if (pattern->as.binding.name) {
                resolver_define(ctx, pattern->as.binding.name, NULL, 0);
            }
            break;
        case PATTERN_TYPED:
            if (pattern->as.typed.name) {
                resolver_define(ctx, pattern->as.typed.name, NULL, 0);
            }
            break;
        case PATTERN_OR:
            for (int i = 0; i < pattern->as.or_pattern.num_alternatives; i++) {
                resolve_pattern_bindings(ctx, pattern->as.or_pattern.alternatives[i]);
            }
            break;
        case PATTERN_OBJECT:
            for (int i = 0; i < pattern->as.object.num_fields; i++) {
                ObjectFieldPattern *field = &pattern->as.object.fields[i];
                if (field->pattern) {
                    resolve_pattern_bindings(ctx, field->pattern);
                } else if (field->name) {
                    resolver_define(ctx, field->name, NULL, 0);
                }
            }
            if (pattern->as.object.has_rest && pattern->as.object.rest_name) {
                resolver_define(ctx, pattern->as.object.rest_name, NULL, 0);
            }
            break;
        case PATTERN_ARRAY:
            for (int i = 0; i < pattern->as.array.num_elements; i++) {
                ArrayElementPattern *el = &pattern->as.array.elements[i];
                if (el->is_rest) {
                    if (el->rest_name) resolver_define(ctx, el->rest_name, NULL, 0);
                } else {
                    resolve_pattern_bindings(ctx, el->pattern);
                }
            }
            break;
        default:
            break;
    }
}

static void resolve_expr_internal(ResolverContext *ctx, Expr *expr) {
    if (!expr) return;

    switch (expr->type) {
        case EXPR_IDENT: {
            // Look up the variable and store resolution info
            int depth, slot, in_dynamic;
            if (resolver_lookup_ex(ctx, expr->as.ident.name, &depth, &slot, &in_dynamic)) {
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

                // Check if the variable is deprecated
                Annotation **annotations;
                int annotation_count;
                if (!ctx->scan_only &&
                    resolver_get_annotations(ctx, expr->as.ident.name, &annotations, &annotation_count)) {
                    if (is_deprecated(annotations, annotation_count)) {
                        const char *msg = get_deprecation_message(annotations, annotation_count);
                        fprintf(stderr, "[line %d:%d] Warning: '%s' is deprecated: %s\n",
                                expr->line, expr->column, expr->as.ident.name, msg);
                    }
                }
            } else {
                // Variable not found - could be a builtin or global
                expr->as.ident.resolved.is_resolved = 0;
            }
            // Names a pattern may bind are found by name on every access
            expr->as.ident.resolved.ic_state =
                in_dynamic ? HML_IC_STATE_MEGAMORPHIC : HML_IC_STATE_UNINITIALIZED;
            break;
        }

        case EXPR_ASSIGN: {
            // First resolve the value expression
            resolve_expr_internal(ctx, expr->as.assign.value);

            // Then look up the variable being assigned
            int depth, slot, in_dynamic;
            if (resolver_lookup_ex(ctx, expr->as.assign.name, &depth, &slot, &in_dynamic)) {
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
                // (a pattern binding is found by name, not created)
                expr->as.assign.resolved.is_resolved = 0;
                if (!in_dynamic) ctx->implicit_assign_count++;
            }
            expr->as.assign.resolved.ic_state =
                in_dynamic ? HML_IC_STATE_MEGAMORPHIC : HML_IC_STATE_UNINITIALIZED;
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
            ctx->capture_count++;

            // Enter a new scope for the function
            resolver_enter_scope(ctx);

            // Define parameters (use param_names not params) - no annotations for params
            for (int i = 0; i < expr->as.function.num_params; i++) {
                resolver_define(ctx, expr->as.function.param_names[i], NULL, 0);
            }

            // Define rest parameter if present - no annotations for params
            if (expr->as.function.rest_param) {
                resolver_define(ctx, expr->as.function.rest_param, NULL, 0);
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

        case EXPR_MATCH:
            // Resolve the scrutinee expression
            resolve_expr_internal(ctx, expr->as.match_expr.scrutinee);
            // The interpreter evaluates each arm (pattern literals, guard,
            // body) in a fresh child environment holding the pattern
            // bindings, so resolve them one scope deeper. Binding slots
            // depend on runtime match order, so the arm scope is dynamic:
            // bindings resolve by name while outer variables still get
            // correct (depth, slot) pairs.
            for (int i = 0; i < expr->as.match_expr.num_arms; i++) {
                MatchArm *arm = &expr->as.match_expr.arms[i];

                resolver_enter_scope(ctx);
                ctx->current->is_dynamic = 1;
                resolve_pattern_bindings(ctx, arm->pattern);

                // Resolve literal patterns (they may contain expressions)
                if (arm->pattern && arm->pattern->type == PATTERN_LITERAL) {
                    resolve_expr_internal(ctx, arm->pattern->as.literal);
                }

                if (arm->guard) {
                    resolve_expr_internal(ctx, arm->guard);
                }
                resolve_expr_internal(ctx, arm->body);
                resolver_exit_scope(ctx);
            }
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
            // Then define the variable (with annotations)
            resolver_define(ctx, stmt->as.let.name, stmt->as.let.annotations, stmt->as.let.annotation_count);
            break;
        }

        case STMT_CONST: {
            // First resolve the initializer
            if (stmt->as.const_stmt.value) {
                resolve_expr_internal(ctx, stmt->as.const_stmt.value);
            }
            // Then define the constant (with annotations)
            resolver_define(ctx, stmt->as.const_stmt.name, stmt->as.const_stmt.annotations, stmt->as.const_stmt.annotation_count);
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
            // A block body provides its own scope, so the runtime evaluates
            // it directly in the enclosing environment. Only a braceless
            // body gets a dedicated (reused, cleared) iteration environment.
            resolve_expr_internal(ctx, stmt->as.while_stmt.condition);

            if (stmt->as.while_stmt.body->type == STMT_BLOCK) {
                resolve_stmt_internal(ctx, stmt->as.while_stmt.body);
            } else {
                resolver_enter_scope(ctx);
                resolve_stmt_internal(ctx, stmt->as.while_stmt.body);
                resolver_exit_scope(ctx);
            }
            break;
        }

        case STMT_LOOP: {
            // Same scope structure as while (see above).
            if (stmt->as.loop_stmt.body->type == STMT_BLOCK) {
                resolve_stmt_internal(ctx, stmt->as.loop_stmt.body);
            } else {
                resolver_enter_scope(ctx);
                resolve_stmt_internal(ctx, stmt->as.loop_stmt.body);
                resolver_exit_scope(ctx);
            }
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

            // The runtime creates a fresh iter_env each iteration with copies
            // of loop variables for per-iteration closure capture, and the
            // resolver mirrors that with an inner scope. When the body creates
            // no closure and defers nothing, no one can observe a per-iteration
            // copy, so both sides skip the inner scope and the body runs in
            // loop_env directly (a non-block body is treated as capturing to
            // keep its declarations out of loop_env).
            int elide_iter_env = 0;
            if (!ctx->scan_only && stmt->as.for_loop.body->type == STMT_BLOCK) {
                int captures, implicit_assigns;
                scan_stmt(ctx, stmt->as.for_loop.body, &captures, &implicit_assigns);
                elide_iter_env = (captures == 0);
            }
            stmt->as.for_loop.elide_iter_env = elide_iter_env;

            if (elide_iter_env) {
                resolve_stmt_internal(ctx, stmt->as.for_loop.body);
            } else {
                resolver_enter_scope(ctx);
                if (stmt->as.for_loop.initializer &&
                    stmt->as.for_loop.initializer->type == STMT_LET) {
                    resolver_define(ctx,
                        stmt->as.for_loop.initializer->as.let.name, NULL, 0);
                }
                resolve_stmt_internal(ctx, stmt->as.for_loop.body);
                resolver_exit_scope(ctx);
            }

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

            // Define the key variable if present - no annotations for loop vars
            if (stmt->as.for_in.key_var) {
                resolver_define(ctx, stmt->as.for_in.key_var, NULL, 0);
            }

            // Define the value variable - no annotations for loop vars
            if (stmt->as.for_in.value_var) {
                resolver_define(ctx, stmt->as.for_in.value_var, NULL, 0);
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
            //
            // A block that declares nothing in its own scope gets no runtime
            // environment at all (elide_env): its statements run in the
            // enclosing environment, which is observably identical except for
            // assignments to undeclared names (those create a binding in the
            // executing environment), so such blocks keep their scope.
            int elide_env = 0;
            if (!ctx->scan_only) {
                elide_env = 1;
                for (int i = 0; i < stmt->as.block.count; i++) {
                    if (stmt_declares_in_place(stmt->as.block.statements[i])) {
                        elide_env = 0;
                        break;
                    }
                }
                if (elide_env) {
                    int captures, implicit_assigns;
                    scan_stmt(ctx, stmt, &captures, &implicit_assigns);
                    if (implicit_assigns > 0) elide_env = 0;
                }
            }
            stmt->as.block.elide_env = elide_env;

            if (!elide_env) resolver_enter_scope(ctx);
            for (int i = 0; i < stmt->as.block.count; i++) {
                resolve_stmt_internal(ctx, stmt->as.block.statements[i]);
            }
            if (!elide_env) resolver_exit_scope(ctx);
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
            ctx->capture_count++;  // captures the executing environment
            resolve_expr_internal(ctx, stmt->as.defer_stmt.call);
            break;

        case STMT_TRY: {
            resolve_stmt_internal(ctx, stmt->as.try_stmt.try_block);

            if (stmt->as.try_stmt.catch_block) {
                // Create scope for catch variable
                resolver_enter_scope(ctx);
                if (stmt->as.try_stmt.catch_param) {
                    resolver_define(ctx, stmt->as.try_stmt.catch_param, NULL, 0);
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

        case STMT_TYPE_ALIAS:
            // Type aliases - no expressions to resolve
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
