/*
 * Hemlock Code Generator - Array Utility Call Builtins
 *
 * Handles array-related builtins called as functions (not methods).
 * Currently a placeholder for future array builtins.
 * Array method calls (push, pop, map, etc.) are handled in codegen_call_methods.c.
 */

#include "codegen_call_internal.h"

int codegen_call_arrays(CodegenContext *ctx, Expr *expr, char *result,
                        const char *func_name, Expr **call_args, int num_args) {
    (void)ctx;
    (void)expr;
    (void)result;
    (void)func_name;
    (void)call_args;
    (void)num_args;

    // No standalone array builtins currently.
    // Array methods (push, pop, map, filter, etc.) are dispatched
    // through codegen_call_methods() via EXPR_PROP_ACCESS.
    return 0;
}
