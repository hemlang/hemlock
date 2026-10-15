/*
 * Hemlock Code Generator - Expression Internal Declarations
 *
 * Internal declarations for modular expression code generation.
 * These functions are called from the main codegen_expr() dispatcher.
 */

#ifndef HEMLOCK_CODEGEN_EXPR_INTERNAL_H
#define HEMLOCK_CODEGEN_EXPR_INTERNAL_H

#include "codegen_internal.h"

// ========== EXPRESSION HANDLER FUNCTIONS ==========

// Handle EXPR_IDENT - variable references and builtin constants/functions
// Returns the temp variable name containing the result
char* codegen_expr_ident(CodegenContext *ctx, Expr *expr, char *result);

// Handle EXPR_CALL - function calls, method calls, and builtin invocations
// Returns the temp variable name containing the result
char* codegen_expr_call(CodegenContext *ctx, Expr *expr, char *result);

// Handle EXPR_BINARY - binary operations with optimization
// Returns the temp variable name containing the result
// Check if a variable is captured by a closure
Expr *get_double_negation_inner(Expr *expr);
int is_captured_variable(CodegenContext *ctx, const char *name);

char* codegen_expr_binary(CodegenContext *ctx, Expr *expr, char *result);

// OPTIMIZATION: Generate a raw native C expression string for an expression
// with a known primitive type. Returns NULL if the expression can't be
// represented as a pure native C expression (e.g., function calls, array access).
// When non-NULL, the result is a malloc'd string like "(a + b)" that can be
// used directly in C arithmetic. The caller must free it.
// out_type receives the native type of the expression.
char* codegen_native_expr(CodegenContext *ctx, Expr *expr, CheckedTypeKind *out_type);

#endif // HEMLOCK_CODEGEN_EXPR_INTERNAL_H
