/*
 * Hemlock Code Generator - Call Expression Internal Declarations
 *
 * Internal header for the split codegen_call modules.
 * Provides shared declarations for call expression code generation.
 */

#ifndef HEMLOCK_CODEGEN_CALL_INTERNAL_H
#define HEMLOCK_CODEGEN_CALL_INTERNAL_H

#include "codegen_expr_internal.h"
#include "../../../runtime/include/hemlock_value.h"

// Forward declaration for recursive calls
char* codegen_expr(CodegenContext *ctx, Expr *expr);

/*
 * Generate a pointer expression for ref parameter passing.
 * For EXPR_IDENT: returns "&_main_varname" or "&varname"
 * For other expressions: evaluates to temp and returns "&temp"
 * Returns allocated string that must be freed.
 */
char* codegen_ref_arg(CodegenContext *ctx, Expr *arg, char **writeback);

// ========== DISPATCH FUNCTIONS ==========
// Each returns 1 if handled, 0 if not its responsibility.
// The result parameter is a pre-allocated temp name to write the HmlValue into.

/*
 * I/O builtins: print, write, eprint, read_line, open
 */
int codegen_call_io(CodegenContext *ctx, Expr *expr, char *result,
                    const char *func_name, Expr **call_args, int num_args);

/*
 * Type builtins: typeof, sizeof, talloc, type constructors (i8..byte, bool, rune, string),
 * get_stack_limit, set_stack_limit, __get_default_stack_size, __set_default_stack_size
 */
int codegen_call_types(CodegenContext *ctx, Expr *expr, char *result,
                       const char *func_name, Expr **call_args, int num_args);

/*
 * Async/concurrency builtins: spawn, spawn_with, join, detach, channel,
 * task_debug_info, select, poll, __get_default_stack_size, __set_default_stack_size
 */
int codegen_call_async(CodegenContext *ctx, Expr *expr, char *result,
                       const char *func_name, Expr **call_args, int num_args);

/*
 * String utility builtins (called as functions, not methods):
 * to_string, string_byte_length, strerror, __dirent_name, string_to_cstr,
 * cstr_to_string, __string_from_bytes, string_concat_many
 */
int codegen_call_strings(CodegenContext *ctx, Expr *expr, char *result,
                         const char *func_name, Expr **call_args, int num_args);

/*
 * Array utility builtins (called as functions, not methods).
 * Currently empty but provides a hook for future array builtins.
 */
int codegen_call_arrays(CodegenContext *ctx, Expr *expr, char *result,
                        const char *func_name, Expr **call_args, int num_args);

/*
 * Method dispatch for EXPR_PROP_ACCESS method calls.
 * Handles string methods, array methods, file methods, socket methods,
 * channel methods, serialization, and fallback to hml_call_method.
 * Takes the full call expression (callee is expr->as.call.func).
 */
int codegen_call_methods(CodegenContext *ctx, Expr *expr, char *result,
                         Expr *callee, Expr **call_args, int num_args);

#endif // HEMLOCK_CODEGEN_CALL_INTERNAL_H
