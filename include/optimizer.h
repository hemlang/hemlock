/*
 * Hemlock AST Optimizer
 *
 * Performs compile-time optimizations on the AST:
 * - Constant folding (2 + 3 → 5)
 * - Boolean simplification (!true → false, !!x → x)
 * - Strength reduction (x * 2 → x << 1 for integers)
 */

#ifndef HEMLOCK_OPTIMIZER_H
#define HEMLOCK_OPTIMIZER_H

#include "ast.h"

/*
 * Optimization statistics for reporting.
 */
typedef struct {
    int constants_folded;
    int booleans_simplified;
    int strength_reductions;
} OptimizationStats;

/*
 * Optimize all statements in a program.
 * Modifies the AST in place.
 * Returns statistics about optimizations performed.
 */
OptimizationStats optimize_program(Stmt **statements, int count);

/*
 * Optimize a single expression.
 * Returns the optimized expression (may be the same or a new one).
 */
Expr *optimize_expr(Expr *expr, OptimizationStats *stats);

/*
 * Optimize a single statement.
 * Modifies the statement in place.
 */
void optimize_stmt(Stmt *stmt, OptimizationStats *stats);

#endif /* HEMLOCK_OPTIMIZER_H */
