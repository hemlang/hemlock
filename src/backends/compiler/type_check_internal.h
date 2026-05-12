/*
 * Hemlock Compiler - Type Check Internal Header
 *
 * Internal declarations shared between type_check split files.
 * This header is NOT part of the public API — use compiler/type_check.h instead.
 */

#ifndef HEMLOCK_TYPE_CHECK_INTERNAL_H
#define HEMLOCK_TYPE_CHECK_INTERNAL_H

#include "compiler/type_check.h"
#include "../../include/builtins_registry.h"
#include "../../include/hemlock_limits.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// ========== INTERNAL HELPERS (type_check_stmt.c) ==========

// Check function body in its own scope (used by type_check_expr and type_check_stmt)
void type_check_function_body(TypeCheckContext *ctx, Expr *func, const char *name);

// First pass: collect all function signatures, object definitions, and enums
void collect_function_signatures(TypeCheckContext *ctx, Stmt **stmts, int count);

// Infer the type of an expression for compile-time type checking.
CheckedType* type_check_infer_expr(TypeCheckContext *ctx, Expr *expr);

// ========== INTERNAL HELPERS (type_check_expr.c) ==========

// Check method call arguments for built-in types (array, string)
// Returns 1 if method was found and checked, 0 if unknown method
int type_check_method_call(TypeCheckContext *ctx, CheckedType *receiver_type,
                           const char *method_name, Expr **args, int num_args, int line);

// Check if a type contains any type parameters (generics) that can't be validated
int type_contains_param(CheckedType *type);

// Validate an object literal against a custom type definition at compile time
void type_check_validate_object_literal(TypeCheckContext *ctx, Expr *expr,
                                        const char *type_name, int line);

// Validate an object literal against a compound type (A & B & C)
void type_check_validate_compound_object(TypeCheckContext *ctx, Expr *expr,
                                         Type *compound_type, int line);

// Build a stable key for expressions that can be narrowed by null checks.
char* type_check_expr_key(Expr *expr);

// Record and query flow-sensitive non-null expression narrowings.
void type_check_add_narrowing(TypeCheckContext *ctx, const char *key, CheckedType *type);
CheckedType* type_check_lookup_narrowing(TypeCheckContext *ctx, const char *key);

// Infer declared custom-object property types.
CheckedType* type_check_infer_property_type(TypeCheckContext *ctx, CheckedType *obj_type,
                                            const char *property);

// ========== INTERNAL HELPERS (type_errors.c) ==========

// Helper to calculate column for a line in source
int calc_column_for_line(const char *source, int target_line);

// Helper to add error to collection
void add_collected_error(TypeCheckContext *ctx, int line, const char *message, int is_warning);

// ========== INTERNAL HELPERS (type_escape_analysis.c) ==========

// Escape analysis internal functions (used by unboxing)
int variable_escapes_in_expr_internal(Expr *expr, const char *var_name);
int variable_escapes_in_stmt_internal(Stmt *stmt, const char *var_name);

// Tail recursion helpers
int contains_recursive_call(Expr *expr, const char *func_name);
int stmt_contains_recursive_call(Stmt *stmt, const char *func_name);
int stmt_is_tail_recursive(Stmt *stmt, const char *func_name);

// ========== INTERNAL HELPERS (type_unboxing.c) ==========

// Check if an expression is a simple increment pattern: i = i + 1, i++, etc.
int is_simple_increment(Expr *expr, const char *var_name);

// Check if a condition is a simple comparison on the variable
int is_simple_comparison(Expr *expr, const char *var_name);

// Infer the native type of an expression for unboxing
CheckedTypeKind infer_expr_native_type(TypeCheckContext *ctx, Expr *expr);

// Check if expression is unboxable (primitive literal, arithmetic, etc.)
int is_unboxable_expr(Expr *expr);

// Check for incompatible assignments in expressions/statements
int has_incompatible_assignment_expr(Expr *expr, const char *var_name);
int has_incompatible_assignment_stmt(Stmt *stmt, const char *var_name);

#endif // HEMLOCK_TYPE_CHECK_INTERNAL_H
