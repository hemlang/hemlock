/*
 * Hemlock Compiler - Borrow / Ownership Checker
 *
 * An opt-out, Rust-inspired static analysis pass that runs after type
 * checking and before code generation. It models ownership of resources
 * acquired through explicit acquisition builtins and reports the
 * memory-safety bugs Hemlock deliberately permits at runtime.
 *
 * Tracked resources and their release operations:
 *   alloc()/buffer()          raw memory / buffer   -> free()
 *   open()                    file handle           -> .close()
 *   channel()                 channel               -> .close()
 *   spawn()/spawn_with()      async task            -> join()/detach()/await
 *   ffi_open()                dynamic FFI library   -> ffi_close()
 *   mmap_open()/_anon()       memory mapping        -> mmap_close()
 *
 * Diagnostics:
 *   - use-after-free        (using a resource after it was released)
 *   - double-free           (releasing an already-released resource)
 *   - release mismatch      (releasing a resource with the wrong operation)
 *   - use-after-move        (strict mode: using a binding whose resource moved)
 *   - free-inside-loop      (a resource acquired outside a loop freed in its body)
 *   - defer/explicit double  (free() of a resource already scheduled via defer)
 *   - leaked resource        (strict mode: owner dropped without release/move/return)
 *
 * In keeping with Hemlock's "unsafe is a feature" philosophy these are
 * reported as warnings by default and never fail the build. They are
 * advisory: the programmer keeps explicit control.
 *
 * The analysis is flow-sensitive (branches/loops are merged conservatively,
 * diverging paths via return/break/throw are excluded from merges). A
 * lightweight interprocedural layer summarises which parameters each named
 * function releases, so passing a resource to a consuming function transfers
 * ownership at the call site. Resources stored into objects/arrays escape.
 * Full lifetime/region analysis remains out of scope (see
 * docs/advanced/borrow-checker.md).
 */

#ifndef HEMLOCK_BORROW_CHECK_H
#define HEMLOCK_BORROW_CHECK_H

#include "frontend/ast.h"

// A single diagnostic produced by the borrow checker. The list mirrors the
// TypeCheckError shape so the LSP can consume both uniformly.
typedef struct BorrowDiag {
    int line;
    int column;
    int end_column;
    char *message;          // owned; freed by borrow_check_free
    int is_error;           // 0 = warning, 1 = error (strict-error mode)
    struct BorrowDiag *next;
} BorrowDiag;

// Configuration / result handle for one analysis run.
typedef struct BorrowContext {
    // Configuration
    const char *filename;
    const char *source;     // optional, for future column resolution
    int strict;             // enable move tracking + leak detection
    int errors_are_fatal;   // promote warnings to errors (affects exit code)
    int collect;            // collect diagnostics instead of printing to stderr

    // Internal state (resources / scopes) — opaque to callers.
    void *resources;        // BcResource dynamic array
    int num_resources;
    int cap_resources;
    void *scope;            // BcScope* linked list (innermost first)
    int scope_depth;
    int diverged;           // set when the current path left via return/break/...
    int exit_kind;          // how the path left: see BC_EXIT_* in borrow_check.c
    int cur_line;           // line of the statement under analysis (fallback)

    // Interprocedural summaries: which parameters each named function releases.
    void *summaries;        // BcFnSummary dynamic array
    int num_summaries;
    int cap_summaries;
    int summary_mode;       // building a summary: suppress diagnostics + interproc

    // Results
    int warning_count;
    int error_count;
    BorrowDiag *diags;
    BorrowDiag *diags_tail;
} BorrowContext;

// Create / destroy an analysis context.
BorrowContext *borrow_check_new(const char *filename);
void borrow_check_free(BorrowContext *ctx);

// Collect diagnostics instead of printing them (for LSP integration).
void borrow_check_enable_collection(BorrowContext *ctx, const char *source);

// Run the analysis over a whole program. Returns the number of *errors*
// (0 unless errors_are_fatal is set and problems were found). Warnings do
// not contribute to the return value.
int borrow_check_program(BorrowContext *ctx, Stmt **stmts, int stmt_count);

#endif // HEMLOCK_BORROW_CHECK_H
