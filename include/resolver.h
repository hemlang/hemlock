/*
 * Hemlock Variable Resolver
 *
 * This module performs a resolution pass over the AST after parsing,
 * computing (scope_depth, slot_index) pairs for variable references.
 * This enables O(1) variable lookup at runtime instead of hash table probing.
 *
 * The resolver walks the AST and maintains a compile-time scope stack,
 * tracking which variables are defined at each scope level and their slot indices.
 * When a variable reference (EXPR_IDENT) or assignment (EXPR_ASSIGN) is encountered,
 * the resolver looks up the variable and stores the resolution info directly
 * in the AST node.
 */

#ifndef HEMLOCK_RESOLVER_H
#define HEMLOCK_RESOLVER_H

#include "ast.h"

/*
 * Compile-time scope for variable tracking during resolution.
 * Each scope level maintains a list of variable names and their slot indices.
 */
typedef struct ResolverScope {
    char **names;              // Variable names defined in this scope
    Annotation ***annotations; // Annotations for each variable (parallel array)
    int *annotation_counts;    // Number of annotations for each variable
    int count;                 // Number of variables
    int capacity;              // Allocated capacity
    struct ResolverScope *parent;  // Enclosing scope
} ResolverScope;

/*
 * Resolver context - maintains the scope stack during resolution.
 */
typedef struct {
    ResolverScope *current;    // Current innermost scope
    int scope_depth;           // Current nesting depth (0 = global)
} ResolverContext;

/*
 * Create a new resolver context.
 */
ResolverContext *resolver_new(void);

/*
 * Free a resolver context and all its scopes.
 */
void resolver_free(ResolverContext *ctx);

/*
 * Enter a new scope (function, block, etc.)
 */
void resolver_enter_scope(ResolverContext *ctx);

/*
 * Exit the current scope.
 */
void resolver_exit_scope(ResolverContext *ctx);

/*
 * Define a variable in the current scope.
 * Returns the slot index for this variable.
 */
int resolver_define(ResolverContext *ctx, const char *name, Annotation **annotations, int annotation_count);

/*
 * Look up a variable by name.
 * Sets *depth to the number of scope hops (0 = current scope),
 * and *slot to the index within that scope.
 * Returns 1 if found, 0 if not found.
 */
int resolver_lookup(ResolverContext *ctx, const char *name, int *depth, int *slot);

/*
 * Get annotations for a variable by name.
 * Returns annotations and count if found, NULL if not found.
 */
int resolver_get_annotations(ResolverContext *ctx, const char *name, Annotation ***annotations, int *count);

/*
 * Resolve all variables in a program (array of statements).
 * This is the main entry point - call after parsing.
 */
void resolve_program(Stmt **statements, int count);

/*
 * Resolve variables in a single statement.
 */
void resolve_stmt(ResolverContext *ctx, Stmt *stmt);

/*
 * Resolve variables in an expression.
 */
void resolve_expr(ResolverContext *ctx, Expr *expr);

#endif /* HEMLOCK_RESOLVER_H */
