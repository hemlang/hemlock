/*
 * Hemlock Code Generator - Binary Expression Code Generation
 * Extracted from codegen_expr.c
 */

#include "codegen_expr_internal.h"
#include "type_check_internal.h"
#include <inttypes.h>

// Forward declarations
char* codegen_expr(CodegenContext *ctx, Expr *expr);
CheckedTypeKind infer_expr_native_type(TypeCheckContext *ctx, Expr *expr);
CheckedTypeKind checked_promote(CheckedTypeKind a, CheckedTypeKind b);

// ========== NATIVE EXPRESSION GENERATION ==========

// Generate a raw C expression string for an expression with known native type.
// Returns NULL if the expression can't be fully represented in native C.
// This enables expression-level unboxing: instead of boxing every intermediate
// result, we keep values as native C types through the entire expression tree
// and only box at the outermost level.
//
// Example: (a + b) * (c - 1) where a,b,c are unboxed i32
//   Before: hml_i32_mul(hml_i32_add(a_val, b_val), hml_i32_sub(c_val, one_val))
//   After:  hml_val_i32((a + b) * (c - 1))
char* codegen_native_expr(CodegenContext *ctx, Expr *expr, CheckedTypeKind *out_type) {
    if (!ctx->optimize || !ctx->type_ctx || !expr) return NULL;

    // First check if the whole expression has a known native type
    CheckedTypeKind expr_type = infer_expr_native_type(ctx->type_ctx, expr);
    if (expr_type == CHECKED_UNKNOWN) return NULL;

    switch (expr->type) {
        case EXPR_NUMBER: {
            char buf[64];
            if (expr->as.number.is_float) {
                codegen_format_f64(buf, sizeof(buf), expr->as.number.float_value);
                *out_type = CHECKED_F64;
            } else if (expr->as.number.is_u64) {
                snprintf(buf, sizeof(buf), "%" PRIu64 "ULL", expr->as.number.uint_value);
                *out_type = CHECKED_U64;
            } else if (expr->as.number.int_value > 2147483647LL ||
                       expr->as.number.int_value < -2147483648LL) {
                codegen_format_i64(buf, sizeof(buf), expr->as.number.int_value);
                *out_type = CHECKED_I64;
            } else {
                snprintf(buf, sizeof(buf), "%" PRId64, expr->as.number.int_value);
                *out_type = CHECKED_I32;
            }
            return strdup(buf);
        }

        case EXPR_BOOL: {
            *out_type = CHECKED_BOOL;
            return strdup(expr->as.boolean ? "1" : "0");
        }

        case EXPR_IDENT: {
            // Only for unboxed variables - we can use the native name directly
            CheckedTypeKind kind = type_check_get_unboxable(ctx->type_ctx, expr->as.ident.name);
            if (kind != CHECKED_UNKNOWN) {
                // Skip function parameters and non-local main vars
                if (codegen_is_func_param(ctx, expr->as.ident.name)) return NULL;
                int is_truly_main = codegen_is_main_var(ctx, expr->as.ident.name) &&
                                    !codegen_is_local(ctx, expr->as.ident.name);
                if (is_truly_main) return NULL;

                *out_type = kind;
                return codegen_sanitize_ident(expr->as.ident.name);
            }
            return NULL;
        }

        case EXPR_BINARY: {
            // Division always returns f64 - skip for now (cast complexity)
            if (expr->as.binary.op == OP_DIV) return NULL;
            // Modulo needs a zero check that can't be expressed in a single C expression
            if (expr->as.binary.op == OP_MOD) return NULL;
            // Logical AND/OR have short-circuit semantics - can't inline
            if (expr->as.binary.op == OP_AND || expr->as.binary.op == OP_OR) return NULL;
            // Shifts need validation (negative/oversized) - use safe runtime path
            if (expr->as.binary.op == OP_BIT_LSHIFT || expr->as.binary.op == OP_BIT_RSHIFT) return NULL;

            CheckedTypeKind left_type, right_type;
            char *left = codegen_native_expr(ctx, expr->as.binary.left, &left_type);
            if (!left) return NULL;
            char *right = codegen_native_expr(ctx, expr->as.binary.right, &right_type);
            if (!right) { free(left); return NULL; }

            // Determine the C operator
            const char *op_str = NULL;
            int is_comparison = 0;
            switch (expr->as.binary.op) {
                case OP_ADD: op_str = "+"; break;
                case OP_SUB: op_str = "-"; break;
                case OP_MUL: op_str = "*"; break;
                case OP_MOD: op_str = "%"; break;
                case OP_LESS: op_str = "<"; is_comparison = 1; break;
                case OP_LESS_EQUAL: op_str = "<="; is_comparison = 1; break;
                case OP_GREATER: op_str = ">"; is_comparison = 1; break;
                case OP_GREATER_EQUAL: op_str = ">="; is_comparison = 1; break;
                case OP_EQUAL: op_str = "=="; is_comparison = 1; break;
                case OP_NOT_EQUAL: op_str = "!="; is_comparison = 1; break;
                case OP_BIT_AND: op_str = "&"; break;
                case OP_BIT_OR: op_str = "|"; break;
                case OP_BIT_XOR: op_str = "^"; break;
                case OP_BIT_LSHIFT: op_str = "<<"; break;
                case OP_BIT_RSHIFT: op_str = ">>"; break;
                default: break;
            }

            if (!op_str) { free(left); free(right); return NULL; }

            // Handle type promotion for mixed types
            CheckedTypeKind result_type = expr_type;
            char *promoted_left = left;
            char *promoted_right = right;

            // If types differ, cast to the promoted OPERAND type. For
            // comparisons expr_type is bool, so the cast target must come
            // from promoting the operand types instead. Casting applies to
            // comparisons too: Hemlock compares in the promoted type's width
            // and signedness (i8 -1 == u8 255 is true), which differs from
            // C's default integer promotions for narrow operands.
            CheckedTypeKind cast_type = is_comparison
                ? checked_promote(left_type, right_type)
                : result_type;
            if (left_type != right_type) {
                if (cast_type == CHECKED_UNKNOWN) {
                    free(left);
                    free(right);
                    return NULL;
                }
                const char *c_type = checked_type_to_c_type(cast_type);
                if (c_type && left_type != cast_type) {
                    char *cast = malloc(strlen(left) + strlen(c_type) + 8);
                    sprintf(cast, "((%s)%s)", c_type, left);
                    promoted_left = cast;
                    free(left);
                    left = NULL;
                }
                if (c_type && right_type != cast_type) {
                    char *cast = malloc(strlen(right) + strlen(c_type) + 8);
                    sprintf(cast, "((%s)%s)", c_type, right);
                    promoted_right = cast;
                    free(right);
                    right = NULL;
                }
            }

            // i32/i64 add/sub/mul must throw on overflow: emit a checked GNU
            // statement expression instead of raw C arithmetic (which, even
            // with -fwrapv, would silently wrap where the contract throws).
            if ((expr->as.binary.op == OP_ADD || expr->as.binary.op == OP_SUB ||
                 expr->as.binary.op == OP_MUL) &&
                (result_type == CHECKED_I32 || result_type == CHECKED_I64)) {
                const char *cty = (result_type == CHECKED_I32) ? "int32_t" : "int64_t";
                const char *tyname = (result_type == CHECKED_I32) ? "i32" : "i64";
                const char *builtin = (expr->as.binary.op == OP_ADD) ? "__builtin_add_overflow"
                                    : (expr->as.binary.op == OP_SUB) ? "__builtin_sub_overflow"
                                                                     : "__builtin_mul_overflow";
                const char *word = (expr->as.binary.op == OP_ADD) ? "addition"
                                 : (expr->as.binary.op == OP_SUB) ? "subtraction"
                                                                  : "multiplication";
                size_t clen = strlen(promoted_left) + strlen(promoted_right) + 192;
                char *checked = malloc(clen);
                snprintf(checked, clen,
                         "({ %s _ovt; if (%s(%s, %s, &_ovt)) hml_runtime_error(\"Integer overflow: %s %s\"); _ovt; })",
                         cty, builtin, promoted_left, promoted_right, tyname, word);
                *out_type = result_type;
                free(promoted_left);
                free(promoted_right);
                return checked;
            }

            // Build the expression: (left op right)
            size_t len = strlen(promoted_left) + strlen(op_str) + strlen(promoted_right) + 8;
            char *result_str = malloc(len);
            snprintf(result_str, len, "(%s %s %s)", promoted_left, op_str, promoted_right);

            if (is_comparison) {
                *out_type = CHECKED_BOOL;
            } else {
                *out_type = result_type;
            }

            free(promoted_left);
            free(promoted_right);
            return result_str;
        }

        case EXPR_UNARY: {
            CheckedTypeKind operand_type;
            char *operand = codegen_native_expr(ctx, expr->as.unary.operand, &operand_type);
            if (!operand) return NULL;

            const char *op_str = NULL;
            switch (expr->as.unary.op) {
                case UNARY_NEGATE:
                    // Negating an unsigned operand promotes (u8→i16, u16→i32,
                    // u32→i64, u64→i64-or-error); native emission would keep
                    // the unsigned type. Leave those to the runtime path.
                    if (operand_type == CHECKED_U8 || operand_type == CHECKED_U16 ||
                        operand_type == CHECKED_U32 || operand_type == CHECKED_U64 ||
                        operand_type == CHECKED_BOOL) {
                        free(operand);
                        return NULL;
                    }
                    // i32/i64 MIN negation must throw, not wrap.
                    if (operand_type == CHECKED_I32 || operand_type == CHECKED_I64) {
                        const char *cty = (operand_type == CHECKED_I32) ? "int32_t" : "int64_t";
                        const char *minmac = (operand_type == CHECKED_I32) ? "INT32_MIN" : "INT64_MIN";
                        const char *tyname = (operand_type == CHECKED_I32) ? "i32" : "i64";
                        size_t clen = strlen(operand) + 160;
                        char *checked = malloc(clen);
                        snprintf(checked, clen,
                                 "({ %s _ngt = %s; if (_ngt == %s) hml_runtime_error(\"Integer overflow: %s negation\"); -_ngt; })",
                                 cty, operand, minmac, tyname);
                        *out_type = operand_type;
                        free(operand);
                        return checked;
                    }
                    op_str = "-";
                    break;
                case UNARY_BIT_NOT:
                    // ~ requires an integer operand (runtime errors on floats/bools)
                    if (operand_type == CHECKED_F32 || operand_type == CHECKED_F64 ||
                        operand_type == CHECKED_BOOL) {
                        free(operand);
                        return NULL;
                    }
                    op_str = "~";
                    break;
                case UNARY_NOT: op_str = "!"; break;
                default: break;
            }
            if (!op_str) { free(operand); return NULL; }

            size_t len = strlen(operand) + 8;
            char *result_str = malloc(len);
            snprintf(result_str, len, "(%s%s)", op_str, operand);

            if (expr->as.unary.op == UNARY_NOT) {
                *out_type = CHECKED_BOOL;
            } else {
                *out_type = operand_type;
            }
            free(operand);
            return result_str;
        }

        default:
            return NULL;
    }
}

// ========== OPTIMIZATION HELPERS ==========

// OPTIMIZATION: Helper to check if an expression is a string at compile time
static int is_string_expr(CodegenContext *ctx, Expr *expr) {
    if (!expr) return 0;
    if (expr->type == EXPR_STRING || expr->type == EXPR_STRING_INTERPOLATION) {
        return 1;
    }
    if (!ctx->type_ctx) return 0;

    CheckedType *type = type_check_infer_expr(ctx->type_ctx, expr);
    return type && type->kind == CHECKED_STRING;
}

// Helper: Check if a variable is captured by the current closure
// Captured variables cannot be unboxed because they're stored as HmlValue in the closure environment
int is_captured_variable(CodegenContext *ctx, const char *name) {
    if (!ctx->current_closure || ctx->current_closure->num_captured == 0) {
        return 0;
    }
    for (int i = 0; i < ctx->current_closure->num_captured; i++) {
        if (strcmp(ctx->current_closure->captured_vars[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

// Type inference result for compile-time optimization
typedef enum {
    INFER_UNKNOWN = 0,
    INFER_I32,
    INFER_I64,
    INFER_F64,
    INFER_BOOL
} InferredNumericType;

// OPTIMIZATION: Infer the numeric type of an expression at compile time
// Returns INFER_UNKNOWN if type cannot be determined
static InferredNumericType infer_numeric_type(CodegenContext *ctx, Expr *expr) {
    if (!expr) return INFER_UNKNOWN;

    switch (expr->type) {
        case EXPR_NUMBER:
            if (expr->as.number.is_float) {
                return INFER_F64;
            }
            if (expr->as.number.is_u64) {
                return INFER_I64;  // Treat u64 as i64 for binary op inference
            }
            // Check if it fits in i32
            if (expr->as.number.int_value >= INT32_MIN &&
                expr->as.number.int_value <= INT32_MAX) {
                return INFER_I32;
            }
            return INFER_I64;

        case EXPR_BOOL:
            return INFER_BOOL;

        case EXPR_IDENT:
            // Check if this is an unboxable typed variable
            // IMPORTANT: Skip main-level variables - they're pre-declared as HmlValue
            // and can change type at runtime (Hemlock is dynamically typed)
            // IMPORTANT: Skip shadow variables (inlined params) - they are HmlValue
            if (ctx->optimize && ctx->type_ctx &&
                !codegen_is_func_param(ctx, expr->as.ident.name) &&
                !codegen_is_main_var(ctx, expr->as.ident.name) &&
                !codegen_is_shadow(ctx, expr->as.ident.name)) {
                CheckedTypeKind native_type = type_check_get_unboxable(
                    ctx->type_ctx, expr->as.ident.name);
                switch (native_type) {
                    case CHECKED_I8:
                    case CHECKED_I16:
                    case CHECKED_I32:
                    case CHECKED_U8:
                    case CHECKED_U16:
                    case CHECKED_U32:
                        return INFER_I32;
                    case CHECKED_I64:
                    case CHECKED_U64:
                        return INFER_I64;
                    case CHECKED_F32:
                    case CHECKED_F64:
                        return INFER_F64;
                    default:
                        break;
                }
                // Also check type context lookup for function-local variables
                // Note: Only match exact i32/i64/f64 to preserve Hemlock's type promotion rules
                CheckedType *var_type = type_check_lookup(ctx->type_ctx, expr->as.ident.name);
                if (var_type) {
                    switch (var_type->kind) {
                        case CHECKED_I32:
                            return INFER_I32;
                        case CHECKED_I64:
                            return INFER_I64;
                        case CHECKED_F64:
                            return INFER_F64;
                        case CHECKED_BOOL:
                            return INFER_BOOL;
                        default:
                            break;
                    }
                }
            }
            // Main-level variables and function parameters can change type at runtime,
            // so return UNKNOWN to use runtime type checks
            return INFER_UNKNOWN;

        case EXPR_BINARY:
            // Division ALWAYS returns f64 in Hemlock
            if (expr->as.binary.op == OP_DIV) {
                return INFER_F64;
            }
            // For arithmetic/bitwise ops, infer from operands
            if (expr->as.binary.op >= OP_ADD && expr->as.binary.op <= OP_BIT_RSHIFT) {
                InferredNumericType left = infer_numeric_type(ctx, expr->as.binary.left);
                InferredNumericType right = infer_numeric_type(ctx, expr->as.binary.right);

                // If both are known and compatible, return the promoted type
                if (left != INFER_UNKNOWN && right != INFER_UNKNOWN) {
                    // Float always wins
                    if (left == INFER_F64 || right == INFER_F64) return INFER_F64;
                    // i64 promotes i32
                    if (left == INFER_I64 || right == INFER_I64) return INFER_I64;
                    // Both i32
                    if (left == INFER_I32 && right == INFER_I32) return INFER_I32;
                }
            }
            // Comparison ops return bool
            if (expr->as.binary.op >= OP_EQUAL && expr->as.binary.op <= OP_GREATER_EQUAL) {
                return INFER_BOOL;
            }
            return INFER_UNKNOWN;

        case EXPR_UNARY:
            if (expr->as.unary.op == UNARY_NOT) {
                return INFER_BOOL;
            }
            // Negation and bit-not preserve type
            return infer_numeric_type(ctx, expr->as.unary.operand);

        default:
            return INFER_UNKNOWN;
    }
}

// OPTIMIZATION: Check if expression is a double negation (!!x or --x)
// Returns the inner expression if it's a double negation, NULL otherwise
Expr* get_double_negation_inner(Expr *expr) {
    if (!expr || expr->type != EXPR_UNARY) return NULL;

    Expr *inner = expr->as.unary.operand;
    if (!inner || inner->type != EXPR_UNARY) return NULL;

    // !!x (logical double negation)
    if (expr->as.unary.op == UNARY_NOT && inner->as.unary.op == UNARY_NOT) {
        return inner->as.unary.operand;
    }
    // --x as unary (negate negate) - note: this is different from prefix decrement
    if (expr->as.unary.op == UNARY_NEGATE && inner->as.unary.op == UNARY_NEGATE) {
        return inner->as.unary.operand;
    }

    return NULL;
}

// OPTIMIZATION: Count chained ADD operations that look like string concatenation
// Returns the count of concatenated elements (2 = simple a+b, 3 = a+b+c, etc.)
static int count_string_concat_chain(Expr *expr, Expr **elements, int max_elements) {
    if (expr->type != EXPR_BINARY || expr->as.binary.op != OP_ADD) {
        // Not an ADD - this is a leaf
        if (max_elements > 0) {
            elements[0] = expr;
        }
        return 1;
    }

    // Check if the left side is also a string concat chain
    int left_count = count_string_concat_chain(expr->as.binary.left, elements, max_elements);
    if (left_count >= max_elements) {
        return left_count; // Already at max
    }

    // Add the right side
    elements[left_count] = expr->as.binary.right;
    return left_count + 1;
}

// OPTIMIZATION: Check if this is a chain of string concatenations
// Detects patterns like: a + b + c + d (left-associative ADD chains)
// where at least one operand has a string type
static int is_string_concat_chain(CodegenContext *ctx, Expr *expr, int *count) {
    if (expr->type != EXPR_BINARY || expr->as.binary.op != OP_ADD) {
        return 0;
    }

    // Collect all elements in the chain
    Expr *elements[6];
    int n = count_string_concat_chain(expr, elements, 6);

    // For it to be a string concat chain, at least one element should be a string
    int has_string = 0;
    for (int i = 0; i < n; i++) {
        if (is_string_expr(ctx, elements[i])) {
            has_string = 1;
            break;
        }
    }

    if (has_string && n >= 3 && n <= 5) {
        *count = n;
        return 1;
    }
    return 0;
}

// Generate code for binary expressions
char* codegen_expr_binary(CodegenContext *ctx, Expr *expr, char *result) {
            // OPTIMIZATION: Expression-level unboxing
            // Try to generate the entire binary expression tree as native C code,
            // boxing only the final result. This eliminates intermediate HmlValue
            // boxing/unboxing in expression chains like (a + b) * (c - 1).
            if (ctx->optimize && ctx->type_ctx) {
                CheckedTypeKind native_result_type;
                char *native = codegen_native_expr(ctx, expr, &native_result_type);
                if (native) {
                    const char *box_func = checked_type_to_box_func(native_result_type);
                    if (box_func) {
                        codegen_writeln(ctx, "HmlValue %s = %s(%s);", result, box_func, native);
                        free(native);
                        return result;
                    }
                    free(native);
                }
            }

            // OPTIMIZATION: Short-circuit evaluation for && and ||
            // This matches the interpreter's behavior and avoids unnecessary computation
            if (expr->as.binary.op == OP_AND) {
                // Short-circuit AND: if left is false, skip right evaluation
                char *left = codegen_expr(ctx, expr->as.binary.left);
                codegen_writeln(ctx, "HmlValue %s;", result);
                codegen_writeln(ctx, "if (!hml_to_bool(%s)) {", left);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_val_bool(0);", result);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                char *right = codegen_expr(ctx, expr->as.binary.right);
                codegen_writeln(ctx, "%s = hml_val_bool(hml_to_bool(%s));", result, right);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", right);
                free(right);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", left);
                free(left);
                return result;
            }

            if (expr->as.binary.op == OP_OR) {
                // Short-circuit OR: if left is true, skip right evaluation
                char *left = codegen_expr(ctx, expr->as.binary.left);
                codegen_writeln(ctx, "HmlValue %s;", result);
                codegen_writeln(ctx, "if (hml_to_bool(%s)) {", left);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_val_bool(1);", result);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                char *right = codegen_expr(ctx, expr->as.binary.right);
                codegen_writeln(ctx, "%s = hml_val_bool(hml_to_bool(%s));", result, right);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", right);
                free(right);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", left);
                free(left);
                return result;
            }

            // OPTIMIZATION: Native C arithmetic for unboxed typed variables
            // When both operands are unboxed variables of the same numeric type,
            // use pure C arithmetic instead of HmlValue boxing/unboxing
            // Skip if either operand is a function parameter or main variable (always HmlValue)
            if (ctx->optimize && ctx->type_ctx &&
                expr->as.binary.left->type == EXPR_IDENT &&
                expr->as.binary.right->type == EXPR_IDENT &&
                !codegen_is_func_param(ctx, expr->as.binary.left->as.ident.name) &&
                !codegen_is_func_param(ctx, expr->as.binary.right->as.ident.name) &&
                !codegen_is_main_var(ctx, expr->as.binary.left->as.ident.name) &&
                !codegen_is_main_var(ctx, expr->as.binary.right->as.ident.name)) {
                CheckedTypeKind left_native = type_check_get_unboxable(ctx->type_ctx, expr->as.binary.left->as.ident.name);
                CheckedTypeKind right_native = type_check_get_unboxable(ctx->type_ctx, expr->as.binary.right->as.ident.name);

                // Both operands must be unboxed and of the same numeric type
                if (left_native != CHECKED_UNKNOWN && left_native == right_native &&
                    checked_kind_is_numeric(left_native)) {
                    const char *box_func = checked_type_to_box_func(left_native);
                    char *left_var = codegen_sanitize_ident(expr->as.binary.left->as.ident.name);
                    char *right_var = codegen_sanitize_ident(expr->as.binary.right->as.ident.name);
                    int handled = 1;

                    // i32/i64 add/sub/mul must throw on overflow (raw C signed
                    // arithmetic would be UB and would skip the contract error).
                    int checked_signed = (left_native == CHECKED_I32 || left_native == CHECKED_I64);
                    const char *ovf_cty = (left_native == CHECKED_I32) ? "int32_t" : "int64_t";
                    const char *ovf_ty = (left_native == CHECKED_I32) ? "i32" : "i64";

                    switch (expr->as.binary.op) {
                        case OP_ADD:
                            if (checked_signed) {
                                codegen_writeln(ctx, "HmlValue %s;", result);
                                codegen_writeln(ctx, "{ %s _ovt; if (__builtin_add_overflow(%s, %s, &_ovt)) hml_runtime_error(\"Integer overflow: %s addition\"); %s = %s(_ovt); }",
                                              ovf_cty, left_var, right_var, ovf_ty, result, box_func);
                            } else {
                                codegen_writeln(ctx, "HmlValue %s = %s(%s + %s);", result, box_func, left_var, right_var);
                            }
                            break;
                        case OP_SUB:
                            if (checked_signed) {
                                codegen_writeln(ctx, "HmlValue %s;", result);
                                codegen_writeln(ctx, "{ %s _ovt; if (__builtin_sub_overflow(%s, %s, &_ovt)) hml_runtime_error(\"Integer overflow: %s subtraction\"); %s = %s(_ovt); }",
                                              ovf_cty, left_var, right_var, ovf_ty, result, box_func);
                            } else {
                                codegen_writeln(ctx, "HmlValue %s = %s(%s - %s);", result, box_func, left_var, right_var);
                            }
                            break;
                        case OP_MUL:
                            if (checked_signed) {
                                codegen_writeln(ctx, "HmlValue %s;", result);
                                codegen_writeln(ctx, "{ %s _ovt; if (__builtin_mul_overflow(%s, %s, &_ovt)) hml_runtime_error(\"Integer overflow: %s multiplication\"); %s = %s(_ovt); }",
                                              ovf_cty, left_var, right_var, ovf_ty, result, box_func);
                            } else {
                                codegen_writeln(ctx, "HmlValue %s = %s(%s * %s);", result, box_func, left_var, right_var);
                            }
                            break;
                        case OP_MOD:
                            if (checked_kind_is_integer(left_native)) {
                                codegen_writeln(ctx, "if (%s == 0) hml_runtime_error_line(\"Division by zero\");", right_var);
                                if (checked_signed) {
                                    // Avoid INT_MIN % -1 trap; remainder is 0
                                    codegen_writeln(ctx, "HmlValue %s = (%s == -1) ? %s(0) : %s(%s %% %s);",
                                                  result, right_var, box_func, box_func, left_var, right_var);
                                } else {
                                    codegen_writeln(ctx, "HmlValue %s = %s(%s %% %s);", result, box_func, left_var, right_var);
                                }
                            } else if (left_native == CHECKED_F32) {
                                // f32 % f32 keeps f32 (promoted type)
                                codegen_writeln(ctx, "HmlValue %s = hml_val_f32((float)fmod(%s, %s));", result, left_var, right_var);
                            } else {
                                codegen_writeln(ctx, "HmlValue %s = hml_val_f64(fmod(%s, %s));", result, left_var, right_var);
                            }
                            break;
                        case OP_DIV:
                            // Division always returns float; check for zero with integer operands
                            if (checked_kind_is_integer(left_native)) {
                                codegen_writeln(ctx, "if (%s == 0) hml_runtime_error_line(\"Division by zero\");", right_var);
                            }
                            if (left_native == CHECKED_F32) {
                                // f32 / f32 keeps f32 (promoted type)
                                codegen_writeln(ctx, "HmlValue %s = hml_val_f32((float)((double)%s / (double)%s));", result, left_var, right_var);
                            } else {
                                codegen_writeln(ctx, "HmlValue %s = hml_val_f64((double)%s / (double)%s);", result, left_var, right_var);
                            }
                            break;
                        case OP_LESS:
                            codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%s < %s);", result, left_var, right_var);
                            break;
                        case OP_LESS_EQUAL:
                            codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%s <= %s);", result, left_var, right_var);
                            break;
                        case OP_GREATER:
                            codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%s > %s);", result, left_var, right_var);
                            break;
                        case OP_GREATER_EQUAL:
                            codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%s >= %s);", result, left_var, right_var);
                            break;
                        case OP_EQUAL:
                            codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%s == %s);", result, left_var, right_var);
                            break;
                        case OP_NOT_EQUAL:
                            codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%s != %s);", result, left_var, right_var);
                            break;
                        case OP_BIT_AND:
                            if (checked_kind_is_integer(left_native)) {
                                codegen_writeln(ctx, "HmlValue %s = %s(%s & %s);", result, box_func, left_var, right_var);
                            } else {
                                handled = 0;
                            }
                            break;
                        case OP_BIT_OR:
                            if (checked_kind_is_integer(left_native)) {
                                codegen_writeln(ctx, "HmlValue %s = %s(%s | %s);", result, box_func, left_var, right_var);
                            } else {
                                handled = 0;
                            }
                            break;
                        case OP_BIT_XOR:
                            if (checked_kind_is_integer(left_native)) {
                                codegen_writeln(ctx, "HmlValue %s = %s(%s ^ %s);", result, box_func, left_var, right_var);
                            } else {
                                handled = 0;
                            }
                            break;
                        case OP_BIT_LSHIFT:
                        case OP_BIT_RSHIFT:
                            // Shifts need validation - fall through to safe runtime path
                            handled = 0;
                            break;
                        default:
                            handled = 0;
                            break;
                    }

                    free(left_var);
                    free(right_var);
                    if (handled) return result;
                }
            }

            // OPTIMIZATION: Native C arithmetic for one unboxed variable and one literal
            // Skip if the variable is a function parameter or main variable (always HmlValue)
            if (ctx->optimize && ctx->type_ctx && expr->as.binary.left->type == EXPR_IDENT &&
                expr->as.binary.right->type == EXPR_NUMBER &&
                !codegen_is_func_param(ctx, expr->as.binary.left->as.ident.name) &&
                !codegen_is_main_var(ctx, expr->as.binary.left->as.ident.name)) {
                CheckedTypeKind left_native = type_check_get_unboxable(ctx->type_ctx, expr->as.binary.left->as.ident.name);
                if (left_native != CHECKED_UNKNOWN && checked_kind_is_numeric(left_native)) {
                    const char *box_func = checked_type_to_box_func(left_native);
                    char *left_var = codegen_sanitize_ident(expr->as.binary.left->as.ident.name);
                    int handled = 1;
                    int is_float = expr->as.binary.right->as.number.is_float;
                    int lit_is_u64 = !is_float && expr->as.binary.right->as.number.is_u64;
                    int64_t lit_int = (is_float || lit_is_u64) ? 0 : expr->as.binary.right->as.number.int_value;
                    int lit_fits_i32 = !is_float && !lit_is_u64 &&
                                       lit_int >= INT32_MIN && lit_int <= INT32_MAX;
                    const char *literal_suffix = (left_native == CHECKED_I64 || left_native == CHECKED_U64) ? "LL" : "";

                    // Arithmetic here must keep both the promoted type and the
                    // overflow contract:
                    //  - narrow types (i8..u16) promote to i32 with an int
                    //    literal, so boxing back into the narrow type is wrong;
                    //  - an i64-range literal promotes an i32/u32 left operand
                    //    to i64;
                    //  - f32 + literal promotes to f64;
                    //  - i32/i64 must throw on overflow (checked emission below).
                    // Only emit native arithmetic for combinations where raw C
                    // arithmetic on the left operand's type is exactly the
                    // promoted-type semantics.
                    int arith_ok = 0;
                    const char *ovf_cty = NULL, *ovf_ty = NULL;
                    switch (left_native) {
                        case CHECKED_I32:
                            arith_ok = lit_fits_i32;
                            ovf_cty = "int32_t"; ovf_ty = "i32";
                            break;
                        case CHECKED_I64:
                            arith_ok = !is_float && !lit_is_u64;
                            ovf_cty = "int64_t"; ovf_ty = "i64";
                            break;
                        case CHECKED_U32:
                            arith_ok = lit_fits_i32;  // u32 ⊕ i32 wraps as u32
                            break;
                        case CHECKED_U64:
                            arith_ok = !is_float;     // u64 ⊕ int/u64 wraps as u64
                            break;
                        case CHECKED_F64:
                            arith_ok = !lit_is_u64;   // f64 ⊕ int/float literal is f64
                            break;
                        default:
                            arith_ok = 0;
                            break;
                    }

                    switch (expr->as.binary.op) {
                        case OP_ADD:
                        case OP_SUB:
                        case OP_MUL: {
                            if (!arith_ok) { handled = 0; break; }
                            const char *c_op = (expr->as.binary.op == OP_ADD) ? "+"
                                             : (expr->as.binary.op == OP_SUB) ? "-" : "*";
                            const char *ovf_builtin = (expr->as.binary.op == OP_ADD) ? "__builtin_add_overflow"
                                                    : (expr->as.binary.op == OP_SUB) ? "__builtin_sub_overflow"
                                                                                     : "__builtin_mul_overflow";
                            const char *ovf_word = (expr->as.binary.op == OP_ADD) ? "addition"
                                                 : (expr->as.binary.op == OP_SUB) ? "subtraction"
                                                                                  : "multiplication";
                            char rhs[80];
                            if (is_float) {
                                codegen_format_f64(rhs, sizeof(rhs), expr->as.binary.right->as.number.float_value);
                            } else if (lit_is_u64) {
                                snprintf(rhs, sizeof(rhs), "%lluULL",
                                         (unsigned long long)expr->as.binary.right->as.number.uint_value);
                            } else {
                                snprintf(rhs, sizeof(rhs), "%lld%s", (long long)lit_int, literal_suffix);
                            }
                            if (left_native == CHECKED_I32 || left_native == CHECKED_I64) {
                                codegen_writeln(ctx, "HmlValue %s;", result);
                                codegen_writeln(ctx, "{ %s _ovt; if (%s(%s, %s, &_ovt)) hml_runtime_error(\"Integer overflow: %s %s\"); %s = %s(_ovt); }",
                                              ovf_cty, ovf_builtin, left_var, rhs, ovf_ty, ovf_word, result, box_func);
                            } else {
                                codegen_writeln(ctx, "HmlValue %s = %s(%s %s %s);", result, box_func, left_var, c_op, rhs);
                            }
                            break;
                        }
                        case OP_LESS:
                            if (lit_is_u64 || (left_native == CHECKED_F32 && !is_float)) { handled = 0; break; }
                            if (is_float) {
                                { char fbuf[64]; codegen_format_f64(fbuf, sizeof(fbuf), expr->as.binary.right->as.number.float_value); codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%s < %s);", result, left_var, fbuf); }
                            } else {
                                codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%s < %lld%s);", result, left_var, (long long)expr->as.binary.right->as.number.int_value, literal_suffix);
                            }
                            break;
                        case OP_LESS_EQUAL:
                            if (lit_is_u64 || (left_native == CHECKED_F32 && !is_float)) { handled = 0; break; }
                            if (is_float) {
                                { char fbuf[64]; codegen_format_f64(fbuf, sizeof(fbuf), expr->as.binary.right->as.number.float_value); codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%s <= %s);", result, left_var, fbuf); }
                            } else {
                                codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%s <= %lld%s);", result, left_var, (long long)expr->as.binary.right->as.number.int_value, literal_suffix);
                            }
                            break;
                        case OP_GREATER:
                            if (lit_is_u64 || (left_native == CHECKED_F32 && !is_float)) { handled = 0; break; }
                            if (is_float) {
                                { char fbuf[64]; codegen_format_f64(fbuf, sizeof(fbuf), expr->as.binary.right->as.number.float_value); codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%s > %s);", result, left_var, fbuf); }
                            } else {
                                codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%s > %lld%s);", result, left_var, (long long)expr->as.binary.right->as.number.int_value, literal_suffix);
                            }
                            break;
                        case OP_GREATER_EQUAL:
                            if (lit_is_u64 || (left_native == CHECKED_F32 && !is_float)) { handled = 0; break; }
                            if (is_float) {
                                { char fbuf[64]; codegen_format_f64(fbuf, sizeof(fbuf), expr->as.binary.right->as.number.float_value); codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%s >= %s);", result, left_var, fbuf); }
                            } else {
                                codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%s >= %lld%s);", result, left_var, (long long)expr->as.binary.right->as.number.int_value, literal_suffix);
                            }
                            break;
                        default:
                            handled = 0;
                            break;
                    }
                    free(left_var);
                    if (handled) return result;
                }
            }

            // OPTIMIZATION: Detect chained string concatenations (a + b + c + ...)
            // Use hml_string_concat3/4/5 for single-allocation efficiency
            {
                int concat_count = 0;
                if (is_string_concat_chain(ctx, expr, &concat_count)) {
                    Expr *elements[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
                    count_string_concat_chain(expr, elements, 6);

                    // Generate code for all elements.  While lowering concat operands,
                    // prefer integer division for identifier-based integer arithmetic so
                    // inline fixed-point formatting matches the interpreter.
                    char *temps[5] = {NULL, NULL, NULL, NULL, NULL};
                    for (int i = 0; i < concat_count; i++) {
                        temps[i] = codegen_expr(ctx, elements[i]);
                    }

                    // Call the appropriate concat function
                    if (concat_count == 3) {
                        codegen_writeln(ctx, "HmlValue %s = hml_string_concat3(%s, %s, %s);",
                                      result, temps[0], temps[1], temps[2]);
                    } else if (concat_count == 4) {
                        codegen_writeln(ctx, "HmlValue %s = hml_string_concat4(%s, %s, %s, %s);",
                                      result, temps[0], temps[1], temps[2], temps[3]);
                    } else if (concat_count == 5) {
                        codegen_writeln(ctx, "HmlValue %s = hml_string_concat5(%s, %s, %s, %s, %s);",
                                      result, temps[0], temps[1], temps[2], temps[3], temps[4]);
                    }

                    // Release all temps
                    for (int i = 0; i < concat_count; i++) {
                        codegen_writeln(ctx, "hml_release_if_needed(&%s);", temps[i]);
                        free(temps[i]);
                    }
                    return result;
                }
            }

            // NOTE: no local constant folding here. The frontend optimizer
            // already folds every constant expression whose folded literal is
            // exactly semantics-preserving; anything it left unfolded was left
            // deliberately (overflow must throw at runtime, result type must
            // stay i64/u64, shift width semantics, etc.). Re-folding here with
            // plain int64 arithmetic would reintroduce those bugs.

            // NOTE: Algebraic identity elimination (x + 0 -> x, x * 1 -> x,
            // x | 0 -> x, x << 0 -> x, ...) is deliberately NOT performed.
            // Without the operand's runtime type it changes semantics:
            // binary operators promote (u8 + 0 is i32, not u8) and
            // non-numeric operands must still raise a runtime error.

            // General case: evaluate both operands
            char *left = codegen_expr(ctx, expr->as.binary.left);
            char *right = codegen_expr(ctx, expr->as.binary.right);

            // OPTIMIZATION: Compile-time type inference for binary operations
            // When we can determine the types at compile time, skip runtime type checks
            int both_i32 = 0;
            int both_i64 = 0;
            if (ctx->optimize) {
                InferredNumericType left_type = infer_numeric_type(ctx, expr->as.binary.left);
                InferredNumericType right_type = infer_numeric_type(ctx, expr->as.binary.right);
                if (left_type == INFER_I32 && right_type == INFER_I32) {
                    both_i32 = 1;
                } else if (left_type == INFER_I64 && right_type == INFER_I64) {
                    // Both must be i64 for the i64 fast path - mixed types go through generic path
                    both_i64 = 1;
                }
            }

            // OPTIMIZATION: i32 and i64 fast paths for binary operations
            // This matches the interpreter's fast paths for common integer operations
            // Check at runtime: i32 first (most common), then i64, then generic
            const char *i32_fast_fn = NULL;
            const char *i64_fast_fn = NULL;
            switch (expr->as.binary.op) {
                case OP_ADD: i32_fast_fn = "hml_i32_add"; i64_fast_fn = "hml_i64_add"; break;
                case OP_SUB: i32_fast_fn = "hml_i32_sub"; i64_fast_fn = "hml_i64_sub"; break;
                case OP_MUL: i32_fast_fn = "hml_i32_mul"; i64_fast_fn = "hml_i64_mul"; break;
                case OP_DIV: break;  // Division always uses float - handled by generic path
                case OP_MOD: i32_fast_fn = "hml_i32_mod"; i64_fast_fn = "hml_i64_mod"; break;
                case OP_LESS: i32_fast_fn = "hml_i32_lt"; i64_fast_fn = "hml_i64_lt"; break;
                case OP_LESS_EQUAL: i32_fast_fn = "hml_i32_le"; i64_fast_fn = "hml_i64_le"; break;
                case OP_GREATER: i32_fast_fn = "hml_i32_gt"; i64_fast_fn = "hml_i64_gt"; break;
                case OP_GREATER_EQUAL: i32_fast_fn = "hml_i32_ge"; i64_fast_fn = "hml_i64_ge"; break;
                case OP_EQUAL: i32_fast_fn = "hml_i32_eq"; i64_fast_fn = "hml_i64_eq"; break;
                case OP_NOT_EQUAL: i32_fast_fn = "hml_i32_ne"; i64_fast_fn = "hml_i64_ne"; break;
                case OP_BIT_AND: i32_fast_fn = "hml_i32_bit_and"; i64_fast_fn = "hml_i64_bit_and"; break;
                case OP_BIT_OR: i32_fast_fn = "hml_i32_bit_or"; i64_fast_fn = "hml_i64_bit_or"; break;
                case OP_BIT_XOR: i32_fast_fn = "hml_i32_bit_xor"; i64_fast_fn = "hml_i64_bit_xor"; break;
                case OP_BIT_LSHIFT: i32_fast_fn = "hml_i32_lshift"; i64_fast_fn = "hml_i64_lshift"; break;
                case OP_BIT_RSHIFT: i32_fast_fn = "hml_i32_rshift"; i64_fast_fn = "hml_i64_rshift"; break;
                default: break;
            }

            // If types are known at compile time, emit direct operations (no runtime check)
            if (both_i32 && i32_fast_fn) {
                // Both operands are known i32 - use direct i32 operation
                codegen_writeln(ctx, "HmlValue %s = %s(%s, %s);", result, i32_fast_fn, left, right);
            } else if (both_i64 && i64_fast_fn) {
                // Both operands are known i64 - use direct i64 operation
                codegen_writeln(ctx, "HmlValue %s = %s(%s, %s);", result, i64_fast_fn, left, right);
            } else if (i32_fast_fn && i64_fast_fn) {
                // Types not known - generate cascading fast paths: i32 -> i64 -> generic
                codegen_writeln(ctx, "HmlValue %s = hml_both_i32(%s, %s) ? %s(%s, %s) : (hml_both_i64(%s, %s) ? %s(%s, %s) : hml_binary_op(%s, %s, %s));",
                              result, left, right, i32_fast_fn, left, right,
                              left, right, i64_fast_fn, left, right,
                              codegen_hml_binary_op(expr->as.binary.op), left, right);
            } else if (i32_fast_fn) {
                // Generate i32 fast path only
                codegen_writeln(ctx, "HmlValue %s = hml_both_i32(%s, %s) ? %s(%s, %s) : hml_binary_op(%s, %s, %s);",
                              result, left, right, i32_fast_fn, left, right,
                              codegen_hml_binary_op(expr->as.binary.op), left, right);
            } else {
                // No fast path available - use generic binary_op
                codegen_writeln(ctx, "HmlValue %s = hml_binary_op(%s, %s, %s);",
                              result, codegen_hml_binary_op(expr->as.binary.op), left, right);
            }

            // Use optimized release that skips primitives
            codegen_writeln(ctx, "hml_release_if_needed(&%s);", left);
            codegen_writeln(ctx, "hml_release_if_needed(&%s);", right);
            free(left);
            free(right);
            return result;
}
