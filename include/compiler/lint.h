/*
 * Hemlock Compiler - Static Lint / Diagnostics Pass
 *
 * An opt-out static-analysis pass that runs after type checking and borrow
 * checking and before code generation. Where the type checker proves that
 * values fit their declared types and the borrow checker tracks resource
 * ownership, the lint pass flags code that is well-typed and memory-safe but
 * almost certainly a mistake — the kind of thing you want caught before you
 * ship a binary:
 *
 *   - unreachable code      (statements after return/break/continue/throw/panic,
 *                            after an if/else where both arms always diverge, or
 *                            after an infinite loop with no break bound to it)
 *   - dead branches         (`if`/`while` conditions that are constant)
 *   - self-assignment       (`x = x`, `obj.f = obj.f`, `a[i] = a[i]`)
 *   - self-comparison       (`x < x`, `x > x`, `x && x`, `x || x` — always
 *                            false / redundant; ==//!=/<=/>= are exempt since
 *                            NaN makes them type-dependent)
 *   - modulo by zero        (`x % 0` with a literal zero — traps at runtime)
 *   - duplicate fields      (`{ a: 1, a: 2 }` — the later value silently wins)
 *   - duplicate cases       (repeated literal switch case — unreachable)
 *   - unused variables      (strict mode: a let/const that is never read)
 *
 * Only the program's own top-level module is analysed; imported modules are
 * linted when they are compiled in their own right, so a clean build never
 * drowns the user in warnings about library code they did not write.
 *
 * In keeping with Hemlock's "unsafe is a feature, explicit over implicit"
 * philosophy these are reported as advisory warnings and do not fail the
 * build unless --lint-error is passed. The analysis is intentionally
 * conservative: every diagnostic is something the compiler is confident about,
 * so there are no false positives to silence.
 */

#ifndef HEMLOCK_LINT_H
#define HEMLOCK_LINT_H

#include "frontend/ast.h"

/* A single diagnostic. Mirrors BorrowDiag/TypeCheckError so the LSP can
 * consume all three uniformly. */
typedef struct LintDiag {
    int line;
    int column;
    int end_column;
    char *message;          /* owned; freed by lint_free */
    int is_error;           /* 0 = warning, 1 = error (lint-error mode) */
    struct LintDiag *next;
} LintDiag;

/* Configuration / result handle for one analysis run. */
typedef struct LintContext {
    /* Configuration */
    const char *filename;
    const char *source;     /* optional, for future column resolution */
    int strict;             /* enable the noisier checks (unused variables) */
    int errors_are_fatal;   /* promote warnings to errors (affects exit code) */
    int collect;            /* collect diagnostics instead of printing to stderr */

    /* Internal: line of the statement under analysis, used as a fallback when
     * an expression node carries no source line of its own. */
    int cur_line;

    /* Results */
    int warning_count;
    int error_count;
    LintDiag *diags;
    LintDiag *diags_tail;
} LintContext;

/* Create / destroy an analysis context. */
LintContext *lint_new(const char *filename);
void lint_free(LintContext *ctx);

/* Collect diagnostics instead of printing them (for LSP integration). */
void lint_enable_collection(LintContext *ctx, const char *source);

/* Run the analysis over a whole program. Returns the number of *errors*
 * (0 unless errors_are_fatal is set and problems were found). Warnings do
 * not contribute to the return value. */
int lint_program(LintContext *ctx, Stmt **stmts, int stmt_count);

#endif /* HEMLOCK_LINT_H */
