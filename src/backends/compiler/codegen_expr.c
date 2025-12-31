/*
 * Hemlock Code Generator - Expression Code Generation
 *
 * Handles code generation for all expression types.
 *
 * This file has been refactored to reduce size:
 * - EXPR_IDENT handling is in codegen_expr_ident.c
 */

#include "codegen_internal.h"
#include "codegen_expr_internal.h"

// ========== OPTIMIZATION HELPERS ==========

// OPTIMIZATION: Helper to check if an expression is likely a string
// (string literal or identifier - we can't know types at compile time for all cases)
static int is_likely_string_expr(Expr *expr) {
    return expr->type == EXPR_STRING;
}

// OPTIMIZATION: Check if a value is a power of 2 and return the exponent
// Returns -1 if not a power of 2, otherwise returns the exponent (0-63)
// Proof: A positive integer n is a power of 2 iff (n & (n-1)) == 0
// Example: 8 = 0b1000, 8-1 = 0b0111, 8 & 7 = 0 → power of 2
static int get_power_of_2_exponent(int64_t value) {
    if (value <= 0) return -1;
    if ((value & (value - 1)) != 0) return -1;  // Not a power of 2

    // Count trailing zeros to get exponent
    int exp = 0;
    while ((value & 1) == 0) {
        value >>= 1;
        exp++;
    }
    return exp;
}

// OPTIMIZATION: Check if an expression is a compile-time integer constant
// Returns 1 if constant, 0 otherwise. Sets *value to the constant value.
static int is_const_integer(Expr *expr, int64_t *value) {
    if (!expr) return 0;
    if (expr->type == EXPR_NUMBER && !expr->as.number.is_float) {
        *value = expr->as.number.int_value;
        return 1;
    }
    // Handle negation of constant
    if (expr->type == EXPR_UNARY && expr->as.unary.op == UNARY_NEGATE) {
        int64_t inner;
        if (is_const_integer(expr->as.unary.operand, &inner)) {
            *value = -inner;
            return 1;
        }
    }
    return 0;
}

// OPTIMIZATION: Check if expression is a double negation (!!x or --x)
// Returns the inner expression if it's a double negation, NULL otherwise
static Expr* get_double_negation_inner(Expr *expr) {
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
// where at least one operand is a string literal
static int is_string_concat_chain(Expr *expr, int *count) {
    if (expr->type != EXPR_BINARY || expr->as.binary.op != OP_ADD) {
        return 0;
    }

    // Collect all elements in the chain
    Expr *elements[6];
    int n = count_string_concat_chain(expr, elements, 6);

    // For it to be a string concat chain, at least one element should be a string literal
    int has_string = 0;
    for (int i = 0; i < n; i++) {
        if (is_likely_string_expr(elements[i])) {
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

char* codegen_expr(CodegenContext *ctx, Expr *expr) {
    char *result = codegen_temp(ctx);

    switch (expr->type) {
        case EXPR_NUMBER:
            if (expr->as.number.is_float) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_f64(%g);", result, expr->as.number.float_value);
            } else {
                // Check if it fits in i32
                if (expr->as.number.int_value >= INT32_MIN && expr->as.number.int_value <= INT32_MAX) {
                    codegen_writeln(ctx, "HmlValue %s = hml_val_i32(%d);", result, (int32_t)expr->as.number.int_value);
                } else {
                    codegen_writeln(ctx, "HmlValue %s = hml_val_i64(%ldL);", result, expr->as.number.int_value);
                }
            }
            break;

        case EXPR_BOOL:
            codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%d);", result, expr->as.boolean);
            break;

        case EXPR_STRING: {
            char *escaped = codegen_escape_string(expr->as.string);
            codegen_writeln(ctx, "HmlValue %s = hml_val_string(\"%s\");", result, escaped);
            free(escaped);
            break;
        }

        case EXPR_RUNE:
            codegen_writeln(ctx, "HmlValue %s = hml_val_rune(%u);", result, expr->as.rune);
            break;

        case EXPR_NULL:
            codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            break;

        case EXPR_IDENT:
            codegen_expr_ident(ctx, expr, result);
            break;

        case EXPR_BINARY: {
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
                break;
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
                break;
            }

            // OPTIMIZATION: Native C arithmetic for unboxed typed variables
            // When both operands are unboxed variables of the same numeric type,
            // use pure C arithmetic instead of HmlValue boxing/unboxing
            if (ctx->optimize && ctx->type_ctx &&
                expr->as.binary.left->type == EXPR_IDENT &&
                expr->as.binary.right->type == EXPR_IDENT) {
                CheckedTypeKind left_native = type_check_get_unboxable(ctx->type_ctx, expr->as.binary.left->as.ident.name);
                CheckedTypeKind right_native = type_check_get_unboxable(ctx->type_ctx, expr->as.binary.right->as.ident.name);

                // Both operands must be unboxed and of the same numeric type
                if (left_native != CHECKED_UNKNOWN && left_native == right_native &&
                    checked_kind_is_numeric(left_native)) {
                    const char *box_func = checked_type_to_box_func(left_native);
                    char *left_var = codegen_sanitize_ident(expr->as.binary.left->as.ident.name);
                    char *right_var = codegen_sanitize_ident(expr->as.binary.right->as.ident.name);
                    int handled = 1;

                    switch (expr->as.binary.op) {
                        case OP_ADD:
                            codegen_writeln(ctx, "HmlValue %s = %s(%s + %s);", result, box_func, left_var, right_var);
                            break;
                        case OP_SUB:
                            codegen_writeln(ctx, "HmlValue %s = %s(%s - %s);", result, box_func, left_var, right_var);
                            break;
                        case OP_MUL:
                            codegen_writeln(ctx, "HmlValue %s = %s(%s * %s);", result, box_func, left_var, right_var);
                            break;
                        case OP_MOD:
                            if (checked_kind_is_integer(left_native)) {
                                codegen_writeln(ctx, "HmlValue %s = %s(%s %% %s);", result, box_func, left_var, right_var);
                            } else {
                                codegen_writeln(ctx, "HmlValue %s = hml_val_f64(fmod(%s, %s));", result, left_var, right_var);
                            }
                            break;
                        case OP_DIV:
                            // Division always returns float
                            codegen_writeln(ctx, "HmlValue %s = hml_val_f64((double)%s / (double)%s);", result, left_var, right_var);
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
                            if (checked_kind_is_integer(left_native)) {
                                codegen_writeln(ctx, "HmlValue %s = %s(%s << %s);", result, box_func, left_var, right_var);
                            } else {
                                handled = 0;
                            }
                            break;
                        case OP_BIT_RSHIFT:
                            if (checked_kind_is_integer(left_native)) {
                                codegen_writeln(ctx, "HmlValue %s = %s(%s >> %s);", result, box_func, left_var, right_var);
                            } else {
                                handled = 0;
                            }
                            break;
                        default:
                            handled = 0;
                            break;
                    }

                    free(left_var);
                    free(right_var);
                    if (handled) break;
                }
            }

            // OPTIMIZATION: Native C arithmetic for one unboxed variable and one literal
            if (ctx->optimize && ctx->type_ctx && expr->as.binary.left->type == EXPR_IDENT &&
                expr->as.binary.right->type == EXPR_NUMBER) {
                CheckedTypeKind left_native = type_check_get_unboxable(ctx->type_ctx, expr->as.binary.left->as.ident.name);
                if (left_native != CHECKED_UNKNOWN && checked_kind_is_numeric(left_native)) {
                    const char *box_func = checked_type_to_box_func(left_native);
                    char *left_var = codegen_sanitize_ident(expr->as.binary.left->as.ident.name);
                    int handled = 1;
                    int is_float = expr->as.binary.right->as.number.is_float;
                    const char *literal_suffix = (left_native == CHECKED_I64 || left_native == CHECKED_U64) ? "LL" : "";

                    switch (expr->as.binary.op) {
                        case OP_ADD:
                            if (is_float) {
                                codegen_writeln(ctx, "HmlValue %s = %s(%s + %g);", result, box_func, left_var, expr->as.binary.right->as.number.float_value);
                            } else {
                                codegen_writeln(ctx, "HmlValue %s = %s(%s + %lld%s);", result, box_func, left_var, expr->as.binary.right->as.number.int_value, literal_suffix);
                            }
                            break;
                        case OP_SUB:
                            if (is_float) {
                                codegen_writeln(ctx, "HmlValue %s = %s(%s - %g);", result, box_func, left_var, expr->as.binary.right->as.number.float_value);
                            } else {
                                codegen_writeln(ctx, "HmlValue %s = %s(%s - %lld%s);", result, box_func, left_var, expr->as.binary.right->as.number.int_value, literal_suffix);
                            }
                            break;
                        case OP_MUL:
                            if (is_float) {
                                codegen_writeln(ctx, "HmlValue %s = %s(%s * %g);", result, box_func, left_var, expr->as.binary.right->as.number.float_value);
                            } else {
                                codegen_writeln(ctx, "HmlValue %s = %s(%s * %lld%s);", result, box_func, left_var, expr->as.binary.right->as.number.int_value, literal_suffix);
                            }
                            break;
                        case OP_LESS:
                            if (is_float) {
                                codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%s < %g);", result, left_var, expr->as.binary.right->as.number.float_value);
                            } else {
                                codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%s < %lld%s);", result, left_var, expr->as.binary.right->as.number.int_value, literal_suffix);
                            }
                            break;
                        case OP_LESS_EQUAL:
                            if (is_float) {
                                codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%s <= %g);", result, left_var, expr->as.binary.right->as.number.float_value);
                            } else {
                                codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%s <= %lld%s);", result, left_var, expr->as.binary.right->as.number.int_value, literal_suffix);
                            }
                            break;
                        case OP_GREATER:
                            if (is_float) {
                                codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%s > %g);", result, left_var, expr->as.binary.right->as.number.float_value);
                            } else {
                                codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%s > %lld%s);", result, left_var, expr->as.binary.right->as.number.int_value, literal_suffix);
                            }
                            break;
                        case OP_GREATER_EQUAL:
                            if (is_float) {
                                codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%s >= %g);", result, left_var, expr->as.binary.right->as.number.float_value);
                            } else {
                                codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%s >= %lld%s);", result, left_var, expr->as.binary.right->as.number.int_value, literal_suffix);
                            }
                            break;
                        default:
                            handled = 0;
                            break;
                    }
                    free(left_var);
                    if (handled) break;
                }
            }

            // OPTIMIZATION: Detect chained string concatenations (a + b + c + ...)
            // Use hml_string_concat3/4/5 for single-allocation efficiency
            {
                int concat_count = 0;
                if (is_string_concat_chain(expr, &concat_count)) {
                    Expr *elements[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
                    count_string_concat_chain(expr, elements, 6);

                    // Generate code for all elements
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
                    break;
                }
            }

            // OPTIMIZATION: Constant folding for number literals
            // If both operands are compile-time known constants, compute the result at compile time
            if (expr->as.binary.left->type == EXPR_NUMBER &&
                expr->as.binary.right->type == EXPR_NUMBER &&
                !expr->as.binary.left->as.number.is_float &&
                !expr->as.binary.right->as.number.is_float) {
                int64_t l = expr->as.binary.left->as.number.int_value;
                int64_t r = expr->as.binary.right->as.number.int_value;
                int64_t const_result = 0;
                int is_bool_result = 0;
                int can_fold = 1;

                // Division always returns float - handle separately before the switch
                if (expr->as.binary.op == OP_DIV) {
                    if (r != 0) {
                        codegen_writeln(ctx, "HmlValue %s = hml_val_f64(%.17g);", result, (double)l / (double)r);
                        break;  // Exit EXPR_BINARY case
                    }
                    // Division by zero - fall through to runtime handling
                }

                switch (expr->as.binary.op) {
                    case OP_ADD: const_result = l + r; break;
                    case OP_SUB: const_result = l - r; break;
                    case OP_MUL: const_result = l * r; break;
                    case OP_DIV: can_fold = 0; break;  // Handled above or div-by-zero
                    case OP_MOD:
                        if (r != 0) { const_result = l % r; } else { can_fold = 0; }
                        break;
                    case OP_LESS: const_result = l < r; is_bool_result = 1; break;
                    case OP_LESS_EQUAL: const_result = l <= r; is_bool_result = 1; break;
                    case OP_GREATER: const_result = l > r; is_bool_result = 1; break;
                    case OP_GREATER_EQUAL: const_result = l >= r; is_bool_result = 1; break;
                    case OP_EQUAL: const_result = l == r; is_bool_result = 1; break;
                    case OP_NOT_EQUAL: const_result = l != r; is_bool_result = 1; break;
                    case OP_BIT_AND: const_result = l & r; break;
                    case OP_BIT_OR: const_result = l | r; break;
                    case OP_BIT_XOR: const_result = l ^ r; break;
                    case OP_BIT_LSHIFT: const_result = l << r; break;
                    case OP_BIT_RSHIFT: const_result = l >> r; break;
                    default: can_fold = 0; break;
                }

                if (can_fold) {
                    if (is_bool_result) {
                        codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%d);", result, (int)const_result);
                    } else if (const_result >= INT32_MIN && const_result <= INT32_MAX) {
                        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(%d);", result, (int32_t)const_result);
                    } else {
                        codegen_writeln(ctx, "HmlValue %s = hml_val_i64(%ldL);", result, const_result);
                    }
                    break;
                }
            }

            // OPTIMIZATION: Identity operation elimination (checked first for efficiency)
            // These optimizations are based on algebraic identities:
            // - x + 0 = x, 0 + x = x (additive identity)
            // - x - 0 = x (subtraction identity)
            // - x * 1 = x, 1 * x = x (multiplicative identity)
            // - x * 0 = 0, 0 * x = 0 (zero multiplication)
            // - x | 0 = x, 0 | x = x (bitwise OR identity)
            // - x ^ 0 = x, 0 ^ x = x (XOR identity)
            // - x << 0 -> x (shifting by 0 does nothing)
            if (ctx->optimize) {
                int64_t const_val;

                // x + 0 or 0 + x -> x
                if (expr->as.binary.op == OP_ADD) {
                    if (is_const_integer(expr->as.binary.right, &const_val) && const_val == 0) {
                        char *left_val = codegen_expr(ctx, expr->as.binary.left);
                        codegen_writeln(ctx, "HmlValue %s = %s;", result, left_val);
                        free(left_val);
                        break;
                    }
                    if (is_const_integer(expr->as.binary.left, &const_val) && const_val == 0) {
                        char *right_val = codegen_expr(ctx, expr->as.binary.right);
                        codegen_writeln(ctx, "HmlValue %s = %s;", result, right_val);
                        free(right_val);
                        break;
                    }
                }

                // x - 0 -> x
                if (expr->as.binary.op == OP_SUB) {
                    if (is_const_integer(expr->as.binary.right, &const_val) && const_val == 0) {
                        char *left_val = codegen_expr(ctx, expr->as.binary.left);
                        codegen_writeln(ctx, "HmlValue %s = %s;", result, left_val);
                        free(left_val);
                        break;
                    }
                }

                // x * 1 or 1 * x -> x
                // x * 0 or 0 * x -> 0
                if (expr->as.binary.op == OP_MUL) {
                    if (is_const_integer(expr->as.binary.right, &const_val)) {
                        if (const_val == 1) {
                            char *left_val = codegen_expr(ctx, expr->as.binary.left);
                            codegen_writeln(ctx, "HmlValue %s = %s;", result, left_val);
                            free(left_val);
                            break;
                        }
                        if (const_val == 0) {
                            codegen_writeln(ctx, "HmlValue %s = hml_val_i32(0);", result);
                            break;
                        }
                    }
                    if (is_const_integer(expr->as.binary.left, &const_val)) {
                        if (const_val == 1) {
                            char *right_val = codegen_expr(ctx, expr->as.binary.right);
                            codegen_writeln(ctx, "HmlValue %s = %s;", result, right_val);
                            free(right_val);
                            break;
                        }
                        if (const_val == 0) {
                            codegen_writeln(ctx, "HmlValue %s = hml_val_i32(0);", result);
                            break;
                        }
                    }
                }

                // x | 0 or 0 | x -> x
                if (expr->as.binary.op == OP_BIT_OR) {
                    if (is_const_integer(expr->as.binary.right, &const_val) && const_val == 0) {
                        char *left_val = codegen_expr(ctx, expr->as.binary.left);
                        codegen_writeln(ctx, "HmlValue %s = %s;", result, left_val);
                        free(left_val);
                        break;
                    }
                    if (is_const_integer(expr->as.binary.left, &const_val) && const_val == 0) {
                        char *right_val = codegen_expr(ctx, expr->as.binary.right);
                        codegen_writeln(ctx, "HmlValue %s = %s;", result, right_val);
                        free(right_val);
                        break;
                    }
                }

                // x ^ 0 or 0 ^ x -> x
                if (expr->as.binary.op == OP_BIT_XOR) {
                    if (is_const_integer(expr->as.binary.right, &const_val) && const_val == 0) {
                        char *left_val = codegen_expr(ctx, expr->as.binary.left);
                        codegen_writeln(ctx, "HmlValue %s = %s;", result, left_val);
                        free(left_val);
                        break;
                    }
                    if (is_const_integer(expr->as.binary.left, &const_val) && const_val == 0) {
                        char *right_val = codegen_expr(ctx, expr->as.binary.right);
                        codegen_writeln(ctx, "HmlValue %s = %s;", result, right_val);
                        free(right_val);
                        break;
                    }
                }

                // x << 0 or x >> 0 -> x (shifting by 0 does nothing)
                if (expr->as.binary.op == OP_BIT_LSHIFT || expr->as.binary.op == OP_BIT_RSHIFT) {
                    if (is_const_integer(expr->as.binary.right, &const_val) && const_val == 0) {
                        char *left_val = codegen_expr(ctx, expr->as.binary.left);
                        codegen_writeln(ctx, "HmlValue %s = %s;", result, left_val);
                        free(left_val);
                        break;
                    }
                }
            }

            // OPTIMIZATION: Strength reduction for power-of-2 operations
            // These optimizations are mathematically proven:
            // - x * 2^n = x << n (left shift by n bits)
            // - x / 2^n = x >> n (right shift for positive integers)
            // - x % 2^n = x & (2^n - 1) (bitwise AND with mask)
            if (ctx->optimize) {
                int64_t const_val;
                int power;

                // Check for x * (power of 2) or (power of 2) * x
                if (expr->as.binary.op == OP_MUL) {
                    if (is_const_integer(expr->as.binary.right, &const_val) &&
                        (power = get_power_of_2_exponent(const_val)) >= 0) {
                        // x * 2^n -> x << n
                        char *left_val = codegen_expr(ctx, expr->as.binary.left);
                        // Use runtime shift (type-agnostic)
                        codegen_writeln(ctx, "HmlValue %s = hml_i32_lshift(%s, hml_val_i32(%d));",
                                      result, left_val, power);
                        codegen_writeln(ctx, "hml_release_if_needed(&%s);", left_val);
                        free(left_val);
                        break;
                    }
                    if (is_const_integer(expr->as.binary.left, &const_val) &&
                        (power = get_power_of_2_exponent(const_val)) >= 0) {
                        // 2^n * x -> x << n
                        char *right_val = codegen_expr(ctx, expr->as.binary.right);
                        // Use runtime shift (type-agnostic)
                        codegen_writeln(ctx, "HmlValue %s = hml_i32_lshift(%s, hml_val_i32(%d));",
                                      result, right_val, power);
                        codegen_writeln(ctx, "hml_release_if_needed(&%s);", right_val);
                        free(right_val);
                        break;
                    }
                }

                // Check for x % (power of 2)
                // Proof: For any n = 2^k, x % n = x & (n-1)
                // Example: x % 8 = x & 7 (keeps only the lower 3 bits)
                if (expr->as.binary.op == OP_MOD &&
                    is_const_integer(expr->as.binary.right, &const_val) &&
                    get_power_of_2_exponent(const_val) >= 0) {
                    int64_t mask = const_val - 1;
                    char *left_val = codegen_expr(ctx, expr->as.binary.left);
                    // Use runtime bit-and (type-agnostic)
                    codegen_writeln(ctx, "HmlValue %s = hml_i32_bit_and(%s, hml_val_i32(%d));",
                                  result, left_val, (int32_t)mask);
                    codegen_writeln(ctx, "hml_release_if_needed(&%s);", left_val);
                    free(left_val);
                    break;
                }
            }

            // General case: evaluate both operands
            char *left = codegen_expr(ctx, expr->as.binary.left);
            char *right = codegen_expr(ctx, expr->as.binary.right);

            // Note: Compile-time type inference for binary ops is disabled.
            // Runtime dispatch handles type-specific fast paths.
            int both_i32 = 0;  // Disabled - use runtime dispatch
            int both_i64 = 0;  // Disabled - use runtime dispatch

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
            break;
        }

        case EXPR_UNARY: {
            // OPTIMIZATION: Double negation elimination
            // Proof: !!x = x (for boolean values), and !!x is equivalent to bool(x)
            // Proof: -(-x) = x (negation is self-inverse)
            if (ctx->optimize) {
                Expr *inner = get_double_negation_inner(expr);
                if (inner) {
                    if (expr->as.unary.op == UNARY_NOT) {
                        // !!x -> convert to bool (hml_to_bool returns int, wrap in bool)
                        char *inner_val = codegen_expr(ctx, inner);
                        codegen_writeln(ctx, "HmlValue %s = hml_val_bool(hml_to_bool(%s));",
                                      result, inner_val);
                        codegen_writeln(ctx, "hml_release_if_needed(&%s);", inner_val);
                        free(inner_val);
                        break;
                    } else if (expr->as.unary.op == UNARY_NEGATE) {
                        // -(-x) -> x (identity)
                        char *inner_val = codegen_expr(ctx, inner);
                        codegen_writeln(ctx, "HmlValue %s = %s;", result, inner_val);
                        free(inner_val);
                        break;
                    }
                }
            }

            // OPTIMIZATION: Constant folding for unary operations on literals
            if (expr->as.unary.operand->type == EXPR_NUMBER &&
                !expr->as.unary.operand->as.number.is_float) {
                int64_t val = expr->as.unary.operand->as.number.int_value;
                int can_fold = 1;

                switch (expr->as.unary.op) {
                    case UNARY_NEGATE:
                        val = -val;
                        break;
                    case UNARY_BIT_NOT:
                        val = ~val;
                        break;
                    default:
                        can_fold = 0;
                        break;
                }

                if (can_fold) {
                    if (val >= INT32_MIN && val <= INT32_MAX) {
                        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(%d);", result, (int32_t)val);
                    } else {
                        codegen_writeln(ctx, "HmlValue %s = hml_val_i64(%ldL);", result, val);
                    }
                    break;
                }
            }

            // Constant folding for NOT on boolean literals
            if (expr->as.unary.op == UNARY_NOT && expr->as.unary.operand->type == EXPR_BOOL) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%d);", result, !expr->as.unary.operand->as.boolean);
                break;
            }

            char *operand = codegen_expr(ctx, expr->as.unary.operand);

            // Note: Type inference for unary ops is disabled.
            // Use generic path with runtime dispatch.
            codegen_writeln(ctx, "HmlValue %s = hml_unary_op(%s, %s);",
                          result, codegen_hml_unary_op(expr->as.unary.op), operand);
            codegen_writeln(ctx, "hml_release_if_needed(&%s);", operand);
            free(operand);
            break;
        }

        case EXPR_TERNARY: {
            char *cond = codegen_expr(ctx, expr->as.ternary.condition);
            codegen_writeln(ctx, "HmlValue %s;", result);
            codegen_writeln(ctx, "if (hml_to_bool(%s)) {", cond);
            codegen_indent_inc(ctx);
            char *true_val = codegen_expr(ctx, expr->as.ternary.true_expr);
            codegen_writeln(ctx, "%s = %s;", result, true_val);
            free(true_val);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "} else {");
            codegen_indent_inc(ctx);
            char *false_val = codegen_expr(ctx, expr->as.ternary.false_expr);
            codegen_writeln(ctx, "%s = %s;", result, false_val);
            free(false_val);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            codegen_writeln(ctx, "hml_release(&%s);", cond);
            free(cond);
            break;
        }


        case EXPR_CALL:
            codegen_expr_call(ctx, expr, result);
            break;

        case EXPR_ASSIGN: {
            // Check for const reassignment at compile time
            if (codegen_is_const(ctx, expr->as.assign.name)) {
                codegen_error(ctx, expr->line, "cannot assign to const variable '%s'",
                             expr->as.assign.name);
                codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                break;
            }

            // OPTIMIZATION: Check if assigning to an unboxed variable
            if (ctx->optimize && ctx->type_ctx) {
                CheckedTypeKind native_type = type_check_get_unboxable(ctx->type_ctx, expr->as.assign.name);
                if (native_type != CHECKED_UNKNOWN) {
                    const char *unbox_cast = checked_type_to_unbox_cast(native_type);
                    const char *box_func = checked_type_to_box_func(native_type);
                    if (unbox_cast && box_func) {
                        char *value = codegen_expr(ctx, expr->as.assign.value);
                        char *safe_var_name = codegen_sanitize_ident(expr->as.assign.name);
                        // Unbox value and assign to native variable
                        codegen_writeln(ctx, "%s = %s(%s);", safe_var_name, unbox_cast, value);
                        codegen_writeln(ctx, "hml_release(&%s);", value);
                        // Return boxed value as result
                        codegen_writeln(ctx, "HmlValue %s = %s(%s);", result, box_func, safe_var_name);
                        free(value);
                        free(safe_var_name);
                        break;
                    }
                }
            }

            // OPTIMIZATION: Detect pattern "x = x + y" for in-place string append
            // This turns O(n²) repeated string concatenation into O(n) amortized
            Expr *val_expr = expr->as.assign.value;
            if (val_expr->type == EXPR_BINARY && val_expr->as.binary.op == OP_ADD) {
                Expr *left = val_expr->as.binary.left;
                Expr *right = val_expr->as.binary.right;

                // Check if left operand is the same variable being assigned
                if (left->type == EXPR_IDENT && strcmp(left->as.ident.name, expr->as.assign.name) == 0) {
                    // Check if right operand is definitely a string (literal only)
                    // We can't assume EXPR_IDENT is a string since it could be a number
                    // String indexing returns a rune but the variable type is unknown at compile time
                    int definitely_string = (right->type == EXPR_STRING);

                    if (definitely_string) {
                        // Generate in-place append
                        char *rhs = codegen_expr(ctx, right);

                        // Determine the correct variable name with prefix
                        char *safe_var_name = NULL;
                        const char *var_name = expr->as.assign.name;
                        char prefixed_var[256];
                        if (ctx->current_module && !codegen_is_local(ctx, var_name)) {
                            snprintf(prefixed_var, sizeof(prefixed_var), "%s%s",
                                    ctx->current_module->module_prefix, var_name);
                            var_name = prefixed_var;
                        } else if (codegen_is_local(ctx, var_name) && (ctx->current_module || ctx->in_function)) {
                            // Local variable in module or function shadows main var - use sanitized bare name
                            safe_var_name = codegen_sanitize_ident(var_name);
                            var_name = safe_var_name;
                        } else if (codegen_is_main_var(ctx, expr->as.assign.name)) {
                            snprintf(prefixed_var, sizeof(prefixed_var), "_main_%s", expr->as.assign.name);
                            var_name = prefixed_var;
                        }

                        // Use in-place append - this modifies dest directly if refcount==1
                        codegen_writeln(ctx, "hml_string_append_inplace(&%s, %s);", var_name, rhs);
                        codegen_writeln(ctx, "hml_release_if_needed(&%s);", rhs);
                        free(rhs);

                        // Result is the variable itself
                        codegen_writeln(ctx, "HmlValue %s = %s;", result, var_name);
                        codegen_writeln(ctx, "hml_retain(&%s);", result);
                        if (safe_var_name) free(safe_var_name);
                        break;
                    }
                }
            }

            char *value = codegen_expr(ctx, expr->as.assign.value);
            // Determine the correct variable name with prefix
            // Note: safe_var_name is allocated when needed and must be freed
            char *safe_var_name = NULL;
            const char *var_name = expr->as.assign.name;
            char prefixed_name[CODEGEN_MANGLED_NAME_SIZE];
            if (ctx->current_module && !codegen_is_local(ctx, var_name)) {
                // Module context - use module prefix
                snprintf(prefixed_name, sizeof(prefixed_name), "%s%s",
                        ctx->current_module->module_prefix, var_name);
                var_name = prefixed_name;
            } else if (codegen_is_shadow(ctx, var_name)) {
                // Shadow variable (like catch param) - use sanitized bare name
                safe_var_name = codegen_sanitize_ident(var_name);
                var_name = safe_var_name;
            } else if (codegen_is_local(ctx, var_name) && (ctx->current_module || ctx->in_function || !codegen_is_main_var(ctx, var_name))) {
                // Local variable - use sanitized bare name
                // In module context, locals always shadow main vars
                // In function context, locals shadow main vars
                // Outside module/function, only if not a tracked main var
                safe_var_name = codegen_sanitize_ident(var_name);
                var_name = safe_var_name;
            } else if (codegen_is_main_var(ctx, expr->as.assign.name)) {
                // Main file top-level variable - use _main_ prefix
                snprintf(prefixed_name, sizeof(prefixed_name), "_main_%s", expr->as.assign.name);
                var_name = prefixed_name;
            }
            codegen_writeln(ctx, "hml_release(&%s);", var_name);
            codegen_writeln(ctx, "%s = %s;", var_name, value);
            codegen_writeln(ctx, "hml_retain(&%s);", var_name);

            // If we're inside a closure and this is a captured variable,
            // update the closure environment so the change is visible to other closures
            if (ctx->current_closure && ctx->current_closure->num_captured > 0) {
                for (int i = 0; i < ctx->current_closure->num_captured; i++) {
                    if (strcmp(ctx->current_closure->captured_vars[i], expr->as.assign.name) == 0) {
                        // Use shared_env_indices if using shared environment, otherwise use local index
                        int env_index = ctx->current_closure->shared_env_indices ?
                                       ctx->current_closure->shared_env_indices[i] : i;
                        codegen_writeln(ctx, "hml_closure_env_set(_closure_env, %d, %s);", env_index, var_name);
                        break;
                    }
                }
            }

            codegen_writeln(ctx, "HmlValue %s = %s;", result, var_name);
            codegen_writeln(ctx, "hml_retain(&%s);", result);
            free(value);
            if (safe_var_name) free(safe_var_name);
            break;
        }

        case EXPR_GET_PROPERTY: {
            char *obj = codegen_expr(ctx, expr->as.get_property.object);

            // Check for built-in properties like .length
            if (strcmp(expr->as.get_property.property, "length") == 0) {
                codegen_writeln(ctx, "HmlValue %s;", result);
                codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_array_length(%s);", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_STRING) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_string_length(%s);", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_BUFFER) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_buffer_length(%s);", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_object_get_field_required(%s, \"length\");", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            // Socket properties: fd, address, port, closed
            } else if (strcmp(expr->as.get_property.property, "fd") == 0) {
                codegen_writeln(ctx, "HmlValue %s;", result);
                codegen_writeln(ctx, "if (%s.type == HML_VAL_SOCKET) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_socket_get_fd(%s);", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_object_get_field_required(%s, \"fd\");", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            } else if (strcmp(expr->as.get_property.property, "address") == 0) {
                codegen_writeln(ctx, "HmlValue %s;", result);
                codegen_writeln(ctx, "if (%s.type == HML_VAL_SOCKET) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_socket_get_address(%s);", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_object_get_field_required(%s, \"address\");", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            } else if (strcmp(expr->as.get_property.property, "port") == 0) {
                codegen_writeln(ctx, "HmlValue %s;", result);
                codegen_writeln(ctx, "if (%s.type == HML_VAL_SOCKET) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_socket_get_port(%s);", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_object_get_field_required(%s, \"port\");", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            } else if (strcmp(expr->as.get_property.property, "closed") == 0) {
                codegen_writeln(ctx, "HmlValue %s;", result);
                codegen_writeln(ctx, "if (%s.type == HML_VAL_SOCKET) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_socket_get_closed(%s);", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_object_get_field_required(%s, \"closed\");", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            // String byte_length property
            } else if (strcmp(expr->as.get_property.property, "byte_length") == 0) {
                codegen_writeln(ctx, "HmlValue %s;", result);
                codegen_writeln(ctx, "if (%s.type == HML_VAL_STRING) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_string_byte_length(%s);", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_object_get_field_required(%s, \"byte_length\");", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            // Buffer capacity property
            } else if (strcmp(expr->as.get_property.property, "capacity") == 0) {
                codegen_writeln(ctx, "HmlValue %s;", result);
                codegen_writeln(ctx, "if (%s.type == HML_VAL_BUFFER) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_buffer_capacity(%s);", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_object_get_field_required(%s, \"capacity\");", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            } else {
                // Regular property access - throws error if field not found (parity with interpreter)
                codegen_writeln(ctx, "HmlValue %s = hml_object_get_field_required(%s, \"%s\");",
                              result, obj, expr->as.get_property.property);
            }
            codegen_writeln(ctx, "hml_release(&%s);", obj);
            free(obj);
            break;
        }

        case EXPR_SET_PROPERTY: {
            char *obj = codegen_expr(ctx, expr->as.set_property.object);
            char *value = codegen_expr(ctx, expr->as.set_property.value);
            codegen_writeln(ctx, "hml_object_set_field(%s, \"%s\", %s);",
                          obj, expr->as.set_property.property, value);
            codegen_writeln(ctx, "HmlValue %s = %s;", result, value);
            codegen_writeln(ctx, "hml_retain(&%s);", result);
            codegen_writeln(ctx, "hml_release(&%s);", obj);
            free(obj);
            free(value);
            break;
        }

        case EXPR_INDEX: {
            // Note: Type inference for index expressions is disabled.
            // We use runtime type checking for now.
            int idx_is_i32 = 0;
            int obj_is_array = 0;

            char *obj = codegen_expr(ctx, expr->as.index.object);
            char *idx = codegen_expr(ctx, expr->as.index.index);
            codegen_writeln(ctx, "HmlValue %s;", result);

            if (obj_is_array && idx_is_i32) {
                // OPTIMIZATION: Both array and i32 index known at compile time
                // Skip runtime type checks entirely
                codegen_writeln(ctx, "%s = hml_array_get_i32_fast(%s.as.as_array, %s.as.as_i32);", result, obj, idx);
            } else if (idx_is_i32) {
                // OPTIMIZATION: Index is known i32 - skip index type check
                codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_array_get_i32_fast(%s.as.as_array, %s.as.as_i32);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_STRING) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_string_index(%s, %s);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_BUFFER) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_buffer_get(%s, %s);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_PTR) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_ptr_get(%s, %s);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_val_null();", result);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            } else {
                // General case: full runtime type checking
                codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY && %s.type == HML_VAL_I32) {", obj, idx);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_array_get_i32_fast(%s.as.as_array, %s.as.as_i32);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_ARRAY) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_array_get(%s, %s);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_STRING) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_string_index(%s, %s);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_BUFFER) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_buffer_get(%s, %s);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_PTR) {", obj);
                codegen_indent_inc(ctx);
                // Raw pointer indexing - no bounds checking (unsafe!)
                codegen_writeln(ctx, "%s = hml_ptr_get(%s, %s);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_OBJECT && %s.type == HML_VAL_STRING) {", obj, idx);
                codegen_indent_inc(ctx);
                // Dynamic object property access with string key
                codegen_writeln(ctx, "%s = hml_object_get_field(%s, %s.as.as_string->data);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_val_null();", result);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            }
            // Use optimized release that skips primitives (index is often i32)
            codegen_writeln(ctx, "hml_release_if_needed(&%s);", obj);
            codegen_writeln(ctx, "hml_release_if_needed(&%s);", idx);
            free(obj);
            free(idx);
            break;
        }

        case EXPR_INDEX_ASSIGN: {
            // Note: Type inference for index assignment is disabled.
            // We use runtime type checking for now.
            int idx_is_i32 = 0;
            int obj_is_array = 0;

            char *obj = codegen_expr(ctx, expr->as.index_assign.object);
            char *idx = codegen_expr(ctx, expr->as.index_assign.index);
            char *value = codegen_expr(ctx, expr->as.index_assign.value);

            if (obj_is_array && idx_is_i32) {
                // OPTIMIZATION: Both array and i32 index known at compile time
                codegen_writeln(ctx, "hml_array_set_i32_fast(%s.as.as_array, %s.as.as_i32, %s);", obj, idx, value);
            } else if (idx_is_i32) {
                // OPTIMIZATION: Index is known i32 - skip index type check
                codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "hml_array_set_i32_fast(%s.as.as_array, %s.as.as_i32, %s);", obj, idx, value);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_STRING) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "hml_string_index_assign(%s, %s, %s);", obj, idx, value);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_BUFFER) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "hml_buffer_set(%s, %s, %s);", obj, idx, value);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_PTR) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "hml_ptr_set(%s, %s, %s);", obj, idx, value);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            } else {
                // General case: full runtime type checking
                codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY && %s.type == HML_VAL_I32) {", obj, idx);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "hml_array_set_i32_fast(%s.as.as_array, %s.as.as_i32, %s);", obj, idx, value);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_ARRAY) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "hml_array_set(%s, %s, %s);", obj, idx, value);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_STRING) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "hml_string_index_assign(%s, %s, %s);", obj, idx, value);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_BUFFER) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "hml_buffer_set(%s, %s, %s);", obj, idx, value);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_PTR) {", obj);
                codegen_indent_inc(ctx);
                // Raw pointer indexing - no bounds checking (unsafe!)
                codegen_writeln(ctx, "hml_ptr_set(%s, %s, %s);", obj, idx, value);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_OBJECT && %s.type == HML_VAL_STRING) {", obj, idx);
                codegen_indent_inc(ctx);
                // Dynamic object property assignment with string key
                codegen_writeln(ctx, "hml_object_set_field(%s, %s.as.as_string->data, %s);", obj, idx, value);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            }
            codegen_writeln(ctx, "HmlValue %s = %s;", result, value);
            codegen_writeln(ctx, "hml_retain_if_needed(&%s);", result);
            codegen_writeln(ctx, "hml_release_if_needed(&%s);", obj);
            codegen_writeln(ctx, "hml_release_if_needed(&%s);", idx);
            free(obj);
            free(idx);
            free(value);
            break;
        }

        case EXPR_ARRAY_LITERAL: {
            codegen_writeln(ctx, "HmlValue %s = hml_val_array();", result);
            for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                char *elem = codegen_expr(ctx, expr->as.array_literal.elements[i]);
                codegen_writeln(ctx, "hml_array_push(%s, %s);", result, elem);
                codegen_writeln(ctx, "hml_release(&%s);", elem);
                free(elem);
            }
            break;
        }

        case EXPR_OBJECT_LITERAL: {
            codegen_writeln(ctx, "HmlValue %s = hml_val_object();", result);
            for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
                char *val = codegen_expr(ctx, expr->as.object_literal.field_values[i]);
                codegen_writeln(ctx, "hml_object_set_field(%s, \"%s\", %s);",
                              result, expr->as.object_literal.field_names[i], val);
                codegen_writeln(ctx, "hml_release(&%s);", val);
                free(val);
            }
            break;
        }

        case EXPR_FUNCTION: {
            // Generate anonymous function with closure support
            char *func_name = codegen_anon_func(ctx);

            // Create a scope for analyzing free variables
            Scope *func_scope = scope_new(NULL);

            // Add parameters to the function's scope
            for (int i = 0; i < expr->as.function.num_params; i++) {
                scope_add_var(func_scope, expr->as.function.param_names[i]);
            }

            // Find free variables
            FreeVarSet *free_vars = free_var_set_new();
            find_free_vars_stmt(expr->as.function.body, func_scope, free_vars);

            // Filter out builtins and global functions from free vars
            // (We only want to capture actual local variables)
            FreeVarSet *captured = free_var_set_new();
            for (int i = 0; i < free_vars->num_vars; i++) {
                const char *var = free_vars->vars[i];
                // Check if it's a local variable in the current scope
                if (codegen_is_local(ctx, var)) {
                    free_var_set_add(captured, var);
                }
            }

            // Register closure for later code generation
            // If using shared environment, store indices into shared env in captured_vars
            ClosureInfo *closure = malloc(sizeof(ClosureInfo));
            closure->func_name = strdup(func_name);
            closure->func_expr = expr;
            closure->source_module = ctx->current_module;  // Save module context for function resolution
            closure->next = ctx->closures;
            ctx->closures = closure;

            // Check if any captured variable is block-scoped (defined in a block inside loops).
            // Block-scoped captures need per-iteration environments for correct JS-like semantics.
            int has_block_scoped_capture = 0;
            if (ctx->current_scope) {
                for (int i = 0; i < captured->num_vars; i++) {
                    if (scope_is_defined(ctx->current_scope, captured->vars[i])) {
                        has_block_scoped_capture = 1;
                        break;
                    }
                }
            }

            if (captured->num_vars == 0) {
                // No captures - simple function pointer
                closure->captured_vars = NULL;
                closure->num_captured = 0;
                closure->shared_env_indices = NULL;
                int num_required = count_required_params(expr->as.function.param_defaults, expr->as.function.num_params);
                int has_rest = expr->as.function.rest_param ? 1 : 0;
                codegen_writeln(ctx, "HmlValue %s = hml_val_function_rest((void*)%s, %d, %d, %d, %d);",
                              result, func_name, expr->as.function.num_params, num_required, expr->as.function.is_async, has_rest);
            } else if (ctx->shared_env_name && !has_block_scoped_capture) {
                // Use the shared environment only if no block-scoped captures.
                // Block-scoped captures (like loop-local variables) need per-closure environments.
                // Store the captured variable names and their shared env indices
                closure->captured_vars = malloc(captured->num_vars * sizeof(char*));
                closure->shared_env_indices = malloc(captured->num_vars * sizeof(int));
                closure->num_captured = captured->num_vars;
                for (int i = 0; i < captured->num_vars; i++) {
                    closure->captured_vars[i] = strdup(captured->vars[i]);
                    closure->shared_env_indices[i] = shared_env_get_index(ctx, captured->vars[i]);
                }

                // Update the shared environment with current values of captured variables
                for (int i = 0; i < captured->num_vars; i++) {
                    int shared_idx = shared_env_get_index(ctx, captured->vars[i]);
                    if (shared_idx >= 0) {
                        // Determine which variable name to use:
                        // - Main file vars are stored as _main_<name> in C
                        // - Module-local vars are stored as <name> in C
                        // - Module-local vars should shadow outer (main) vars with same name
                        if (ctx->current_module && codegen_is_local(ctx, captured->vars[i])) {
                            // Inside a module with a local variable - use bare name
                            // This handles cases like Set() having local 'map' that shadows main's 'map'
                            codegen_writeln(ctx, "hml_closure_env_set(%s, %d, %s);",
                                          ctx->shared_env_name, shared_idx, captured->vars[i]);
                        } else if (codegen_is_main_var(ctx, captured->vars[i])) {
                            // Main file variable (not in a module, or not a local) - use prefix
                            codegen_writeln(ctx, "hml_closure_env_set(%s, %d, _main_%s);",
                                          ctx->shared_env_name, shared_idx, captured->vars[i]);
                        } else {
                            // Neither - use as-is
                            codegen_writeln(ctx, "hml_closure_env_set(%s, %d, %s);",
                                          ctx->shared_env_name, shared_idx, captured->vars[i]);
                        }
                    }
                }
                int num_required = count_required_params(expr->as.function.param_defaults, expr->as.function.num_params);
                int has_rest = expr->as.function.rest_param ? 1 : 0;
                codegen_writeln(ctx, "HmlValue %s = hml_val_function_with_env_rest((void*)%s, (void*)%s, %d, %d, %d, %d);",
                              result, func_name, ctx->shared_env_name, expr->as.function.num_params, num_required, expr->as.function.is_async, has_rest);

                // Track for self-reference fixup
                ctx->last_closure_env_id = -1;  // Using shared env, different mechanism
                if (ctx->last_closure_captured) {
                    for (int i = 0; i < ctx->last_closure_num_captured; i++) {
                        free(ctx->last_closure_captured[i]);
                    }
                    free(ctx->last_closure_captured);
                }
                ctx->last_closure_captured = malloc(sizeof(char*) * captured->num_vars);
                ctx->last_closure_num_captured = captured->num_vars;
                for (int i = 0; i < captured->num_vars; i++) {
                    ctx->last_closure_captured[i] = strdup(captured->vars[i]);
                }
            } else {
                // No shared environment - create a per-closure environment (original behavior)
                closure->captured_vars = malloc(captured->num_vars * sizeof(char*));
                closure->num_captured = captured->num_vars;
                closure->shared_env_indices = NULL;  // Not using shared environment
                for (int i = 0; i < captured->num_vars; i++) {
                    closure->captured_vars[i] = strdup(captured->vars[i]);
                }

                int env_id = ctx->temp_counter;
                codegen_writeln(ctx, "HmlClosureEnv *_env_%d = hml_closure_env_new(%d);",
                              env_id, captured->num_vars);
                for (int i = 0; i < captured->num_vars; i++) {
                    // Determine which variable name to use:
                    // - Main file vars are stored as _main_<name> in C
                    // - Module-local vars are stored as <name> in C
                    // - Module-local vars should shadow outer (main) vars with same name
                    if (ctx->current_module && codegen_is_local(ctx, captured->vars[i])) {
                        // Inside a module with a local variable - use bare name
                        codegen_writeln(ctx, "hml_closure_env_set(_env_%d, %d, %s);",
                                      env_id, i, captured->vars[i]);
                    } else if (codegen_is_main_var(ctx, captured->vars[i])) {
                        // Main file variable (not in a module, or not a local) - use prefix
                        codegen_writeln(ctx, "hml_closure_env_set(_env_%d, %d, _main_%s);",
                                      env_id, i, captured->vars[i]);
                    } else {
                        // Neither - use as-is
                        codegen_writeln(ctx, "hml_closure_env_set(_env_%d, %d, %s);",
                                      env_id, i, captured->vars[i]);
                    }
                }
                int num_required = count_required_params(expr->as.function.param_defaults, expr->as.function.num_params);
                int has_rest = expr->as.function.rest_param ? 1 : 0;
                codegen_writeln(ctx, "HmlValue %s = hml_val_function_with_env_rest((void*)%s, (void*)_env_%d, %d, %d, %d, %d);",
                              result, func_name, env_id, expr->as.function.num_params, num_required, expr->as.function.is_async, has_rest);
                ctx->temp_counter++;

                // Track this closure for potential self-reference fixup in let statements
                ctx->last_closure_env_id = env_id;
                // Copy captured variable names since 'captured' will be freed
                if (ctx->last_closure_captured) {
                    for (int i = 0; i < ctx->last_closure_num_captured; i++) {
                        free(ctx->last_closure_captured[i]);
                    }
                    free(ctx->last_closure_captured);
                }
                ctx->last_closure_captured = malloc(sizeof(char*) * captured->num_vars);
                ctx->last_closure_num_captured = captured->num_vars;
                for (int i = 0; i < captured->num_vars; i++) {
                    ctx->last_closure_captured[i] = strdup(captured->vars[i]);
                }
            }

            scope_free(func_scope);
            free_var_set_free(free_vars);
            free_var_set_free(captured);
            free(func_name);
            break;
        }

        case EXPR_PREFIX_INC: {
            // ++x is equivalent to x = x + 1, returns new value
            if (expr->as.prefix_inc.operand->type == EXPR_IDENT) {
                const char *raw_var = expr->as.prefix_inc.operand->as.ident.name;
                const char *var;
                char prefixed_name[CODEGEN_MANGLED_NAME_SIZE];
                char *safe_var = NULL;
                if (ctx->current_module && !codegen_is_local(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "%s%s",
                            ctx->current_module->module_prefix, raw_var);
                    var = prefixed_name;
                } else if (codegen_is_main_var(ctx, raw_var) && !codegen_is_local(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "_main_%s", raw_var);
                    var = prefixed_name;
                } else {
                    // Local variable - sanitize to avoid C keyword conflicts
                    safe_var = codegen_sanitize_ident(raw_var);
                    var = safe_var;
                }
                // Fast path for i32, fallback to generic binary_op
                codegen_writeln(ctx, "%s = %s.type == HML_VAL_I32 ? hml_i32_inc(%s) : hml_binary_op(HML_OP_ADD, %s, hml_val_i32(1));", var, var, var, var);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, var);
                codegen_writeln(ctx, "hml_retain_if_needed(&%s);", result);
                if (safe_var) free(safe_var);
            } else if (expr->as.prefix_inc.operand->type == EXPR_INDEX) {
                // ++arr[i]
                char *arr = codegen_expr(ctx, expr->as.prefix_inc.operand->as.index.object);
                char *idx = codegen_expr(ctx, expr->as.prefix_inc.operand->as.index.index);
                char *old_val = codegen_temp(ctx);
                char *new_val = codegen_temp(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_array_get(%s, %s);", old_val, arr, idx);
                codegen_writeln(ctx, "HmlValue %s = %s.type == HML_VAL_I32 ? hml_i32_inc(%s) : hml_binary_op(HML_OP_ADD, %s, hml_val_i32(1));", new_val, old_val, old_val, old_val);
                codegen_writeln(ctx, "hml_array_set(%s, %s, %s);", arr, idx, new_val);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, new_val);
                codegen_writeln(ctx, "hml_retain(&%s);", result);
                codegen_writeln(ctx, "hml_release(&%s);", old_val);
                codegen_writeln(ctx, "hml_release(&%s);", new_val);
                codegen_writeln(ctx, "hml_release(&%s);", idx);
                codegen_writeln(ctx, "hml_release(&%s);", arr);
                free(arr); free(idx); free(old_val); free(new_val);
            } else if (expr->as.prefix_inc.operand->type == EXPR_GET_PROPERTY) {
                // ++obj.prop
                char *obj = codegen_expr(ctx, expr->as.prefix_inc.operand->as.get_property.object);
                const char *prop = expr->as.prefix_inc.operand->as.get_property.property;
                char *old_val = codegen_temp(ctx);
                char *new_val = codegen_temp(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_object_get_field(%s, \"%s\");", old_val, obj, prop);
                codegen_writeln(ctx, "HmlValue %s = %s.type == HML_VAL_I32 ? hml_i32_inc(%s) : hml_binary_op(HML_OP_ADD, %s, hml_val_i32(1));", new_val, old_val, old_val, old_val);
                codegen_writeln(ctx, "hml_object_set_field(%s, \"%s\", %s);", obj, prop, new_val);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, new_val);
                codegen_writeln(ctx, "hml_retain_if_needed(&%s);", result);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", old_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", new_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", obj);
                free(obj); free(old_val); free(new_val);
            } else {
                codegen_writeln(ctx, "hml_runtime_error(\"Invalid operand for ++\");");
                codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            }
            break;
        }

        case EXPR_PREFIX_DEC: {
            if (expr->as.prefix_dec.operand->type == EXPR_IDENT) {
                const char *raw_var = expr->as.prefix_dec.operand->as.ident.name;
                const char *var;
                char prefixed_name[CODEGEN_MANGLED_NAME_SIZE];
                char *safe_var = NULL;
                if (ctx->current_module && !codegen_is_local(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "%s%s",
                            ctx->current_module->module_prefix, raw_var);
                    var = prefixed_name;
                } else if (codegen_is_main_var(ctx, raw_var) && !codegen_is_local(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "_main_%s", raw_var);
                    var = prefixed_name;
                } else {
                    // Local variable - sanitize to avoid C keyword conflicts
                    safe_var = codegen_sanitize_ident(raw_var);
                    var = safe_var;
                }
                // Fast path for i32, fallback to generic binary_op
                codegen_writeln(ctx, "%s = %s.type == HML_VAL_I32 ? hml_i32_dec(%s) : hml_binary_op(HML_OP_SUB, %s, hml_val_i32(1));", var, var, var, var);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, var);
                codegen_writeln(ctx, "hml_retain_if_needed(&%s);", result);
                if (safe_var) free(safe_var);
            } else if (expr->as.prefix_dec.operand->type == EXPR_INDEX) {
                // --arr[i]
                char *arr = codegen_expr(ctx, expr->as.prefix_dec.operand->as.index.object);
                char *idx = codegen_expr(ctx, expr->as.prefix_dec.operand->as.index.index);
                char *old_val = codegen_temp(ctx);
                char *new_val = codegen_temp(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_array_get(%s, %s);", old_val, arr, idx);
                codegen_writeln(ctx, "HmlValue %s = %s.type == HML_VAL_I32 ? hml_i32_dec(%s) : hml_binary_op(HML_OP_SUB, %s, hml_val_i32(1));", new_val, old_val, old_val, old_val);
                codegen_writeln(ctx, "hml_array_set(%s, %s, %s);", arr, idx, new_val);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, new_val);
                codegen_writeln(ctx, "hml_retain_if_needed(&%s);", result);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", old_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", new_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", idx);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", arr);
                free(arr); free(idx); free(old_val); free(new_val);
            } else if (expr->as.prefix_dec.operand->type == EXPR_GET_PROPERTY) {
                // --obj.prop
                char *obj = codegen_expr(ctx, expr->as.prefix_dec.operand->as.get_property.object);
                const char *prop = expr->as.prefix_dec.operand->as.get_property.property;
                char *old_val = codegen_temp(ctx);
                char *new_val = codegen_temp(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_object_get_field(%s, \"%s\");", old_val, obj, prop);
                codegen_writeln(ctx, "HmlValue %s = %s.type == HML_VAL_I32 ? hml_i32_dec(%s) : hml_binary_op(HML_OP_SUB, %s, hml_val_i32(1));", new_val, old_val, old_val, old_val);
                codegen_writeln(ctx, "hml_object_set_field(%s, \"%s\", %s);", obj, prop, new_val);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, new_val);
                codegen_writeln(ctx, "hml_retain_if_needed(&%s);", result);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", old_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", new_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", obj);
                free(obj); free(old_val); free(new_val);
            } else {
                codegen_writeln(ctx, "hml_runtime_error(\"Invalid operand for --\");");
                codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            }
            break;
        }

        case EXPR_POSTFIX_INC: {
            // x++ returns old value, then increments
            if (expr->as.postfix_inc.operand->type == EXPR_IDENT) {
                const char *raw_var = expr->as.postfix_inc.operand->as.ident.name;
                const char *var;
                char prefixed_name[CODEGEN_MANGLED_NAME_SIZE];
                char *safe_var = NULL;
                if (ctx->current_module && !codegen_is_local(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "%s%s",
                            ctx->current_module->module_prefix, raw_var);
                    var = prefixed_name;
                } else if (codegen_is_main_var(ctx, raw_var) && !codegen_is_local(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "_main_%s", raw_var);
                    var = prefixed_name;
                } else {
                    // Local variable - sanitize to avoid C keyword conflicts
                    safe_var = codegen_sanitize_ident(raw_var);
                    var = safe_var;
                }
                codegen_writeln(ctx, "HmlValue %s = %s;", result, var);
                codegen_writeln(ctx, "hml_retain_if_needed(&%s);", result);
                // Fast path for i32, fallback to generic binary_op
                codegen_writeln(ctx, "%s = %s.type == HML_VAL_I32 ? hml_i32_inc(%s) : hml_binary_op(HML_OP_ADD, %s, hml_val_i32(1));", var, var, var, var);
                if (safe_var) free(safe_var);
            } else if (expr->as.postfix_inc.operand->type == EXPR_INDEX) {
                // arr[i]++
                char *arr = codegen_expr(ctx, expr->as.postfix_inc.operand->as.index.object);
                char *idx = codegen_expr(ctx, expr->as.postfix_inc.operand->as.index.index);
                char *old_val = codegen_temp(ctx);
                char *new_val = codegen_temp(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_array_get(%s, %s);", old_val, arr, idx);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, old_val);  // Return old value
                codegen_writeln(ctx, "hml_retain_if_needed(&%s);", result);
                codegen_writeln(ctx, "HmlValue %s = %s.type == HML_VAL_I32 ? hml_i32_inc(%s) : hml_binary_op(HML_OP_ADD, %s, hml_val_i32(1));", new_val, old_val, old_val, old_val);
                codegen_writeln(ctx, "hml_array_set(%s, %s, %s);", arr, idx, new_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", old_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", new_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", idx);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", arr);
                free(arr); free(idx); free(old_val); free(new_val);
            } else if (expr->as.postfix_inc.operand->type == EXPR_GET_PROPERTY) {
                // obj.prop++
                char *obj = codegen_expr(ctx, expr->as.postfix_inc.operand->as.get_property.object);
                const char *prop = expr->as.postfix_inc.operand->as.get_property.property;
                char *old_val = codegen_temp(ctx);
                char *new_val = codegen_temp(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_object_get_field(%s, \"%s\");", old_val, obj, prop);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, old_val);  // Return old value
                codegen_writeln(ctx, "hml_retain_if_needed(&%s);", result);
                codegen_writeln(ctx, "HmlValue %s = %s.type == HML_VAL_I32 ? hml_i32_inc(%s) : hml_binary_op(HML_OP_ADD, %s, hml_val_i32(1));", new_val, old_val, old_val, old_val);
                codegen_writeln(ctx, "hml_object_set_field(%s, \"%s\", %s);", obj, prop, new_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", old_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", new_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", obj);
                free(obj); free(old_val); free(new_val);
            } else {
                codegen_writeln(ctx, "hml_runtime_error(\"Invalid operand for ++\");");
                codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            }
            break;
        }

        case EXPR_POSTFIX_DEC: {
            if (expr->as.postfix_dec.operand->type == EXPR_IDENT) {
                const char *raw_var = expr->as.postfix_dec.operand->as.ident.name;
                const char *var;
                char prefixed_name[CODEGEN_MANGLED_NAME_SIZE];
                char *safe_var = NULL;
                if (ctx->current_module && !codegen_is_local(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "%s%s",
                            ctx->current_module->module_prefix, raw_var);
                    var = prefixed_name;
                } else if (codegen_is_main_var(ctx, raw_var) && !codegen_is_local(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "_main_%s", raw_var);
                    var = prefixed_name;
                } else {
                    // Local variable - sanitize to avoid C keyword conflicts
                    safe_var = codegen_sanitize_ident(raw_var);
                    var = safe_var;
                }
                codegen_writeln(ctx, "HmlValue %s = %s;", result, var);
                codegen_writeln(ctx, "hml_retain_if_needed(&%s);", result);
                // Fast path for i32, fallback to generic binary_op
                codegen_writeln(ctx, "%s = %s.type == HML_VAL_I32 ? hml_i32_dec(%s) : hml_binary_op(HML_OP_SUB, %s, hml_val_i32(1));", var, var, var, var);
                if (safe_var) free(safe_var);
            } else if (expr->as.postfix_dec.operand->type == EXPR_INDEX) {
                // arr[i]--
                char *arr = codegen_expr(ctx, expr->as.postfix_dec.operand->as.index.object);
                char *idx = codegen_expr(ctx, expr->as.postfix_dec.operand->as.index.index);
                char *old_val = codegen_temp(ctx);
                char *new_val = codegen_temp(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_array_get(%s, %s);", old_val, arr, idx);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, old_val);  // Return old value
                codegen_writeln(ctx, "hml_retain_if_needed(&%s);", result);
                codegen_writeln(ctx, "HmlValue %s = %s.type == HML_VAL_I32 ? hml_i32_dec(%s) : hml_binary_op(HML_OP_SUB, %s, hml_val_i32(1));", new_val, old_val, old_val, old_val);
                codegen_writeln(ctx, "hml_array_set(%s, %s, %s);", arr, idx, new_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", old_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", new_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", idx);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", arr);
                free(arr); free(idx); free(old_val); free(new_val);
            } else if (expr->as.postfix_dec.operand->type == EXPR_GET_PROPERTY) {
                // obj.prop--
                char *obj = codegen_expr(ctx, expr->as.postfix_dec.operand->as.get_property.object);
                const char *prop = expr->as.postfix_dec.operand->as.get_property.property;
                char *old_val = codegen_temp(ctx);
                char *new_val = codegen_temp(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_object_get_field(%s, \"%s\");", old_val, obj, prop);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, old_val);  // Return old value
                codegen_writeln(ctx, "hml_retain_if_needed(&%s);", result);
                codegen_writeln(ctx, "HmlValue %s = %s.type == HML_VAL_I32 ? hml_i32_dec(%s) : hml_binary_op(HML_OP_SUB, %s, hml_val_i32(1));", new_val, old_val, old_val, old_val);
                codegen_writeln(ctx, "hml_object_set_field(%s, \"%s\", %s);", obj, prop, new_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", old_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", new_val);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", obj);
                free(obj); free(old_val); free(new_val);
            } else {
                codegen_writeln(ctx, "hml_runtime_error(\"Invalid operand for --\");");
                codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            }
            break;
        }

        case EXPR_STRING_INTERPOLATION: {
            // Build the string by concatenating parts
            codegen_writeln(ctx, "HmlValue %s = hml_val_string(\"\");", result);

            for (int i = 0; i <= expr->as.string_interpolation.num_parts; i++) {
                // Add string part (there are num_parts+1 string parts)
                if (expr->as.string_interpolation.string_parts[i] &&
                    strlen(expr->as.string_interpolation.string_parts[i]) > 0) {
                    char *escaped = codegen_escape_string(expr->as.string_interpolation.string_parts[i]);
                    char *part_temp = codegen_temp(ctx);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_string(\"%s\");", part_temp, escaped);
                    codegen_writeln(ctx, "HmlValue _concat%d = hml_string_concat(%s, %s);", ctx->temp_counter, result, part_temp);
                    codegen_writeln(ctx, "hml_release(&%s);", result);
                    codegen_writeln(ctx, "hml_release(&%s);", part_temp);
                    codegen_writeln(ctx, "%s = _concat%d;", result, ctx->temp_counter);
                    free(escaped);
                    free(part_temp);
                }

                // Add expression part (there are num_parts expression parts)
                if (i < expr->as.string_interpolation.num_parts) {
                    char *expr_val = codegen_expr(ctx, expr->as.string_interpolation.expr_parts[i]);
                    codegen_writeln(ctx, "HmlValue _concat%d = hml_string_concat(%s, %s);", ctx->temp_counter, result, expr_val);
                    codegen_writeln(ctx, "hml_release(&%s);", result);
                    codegen_writeln(ctx, "hml_release(&%s);", expr_val);
                    codegen_writeln(ctx, "%s = _concat%d;", result, ctx->temp_counter);
                    free(expr_val);
                }
            }
            break;
        }

        case EXPR_AWAIT: {
            // await expr - if value is a task, join it; otherwise return as-is
            char *awaited = codegen_expr(ctx, expr->as.await_expr.awaited_expr);
            codegen_writeln(ctx, "HmlValue %s;", result);
            codegen_writeln(ctx, "if (%s.type == HML_VAL_TASK) {", awaited);
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "%s = hml_join(%s);", result, awaited);
            codegen_writeln(ctx, "hml_release(&%s);", awaited);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "} else {");
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "%s = %s;", result, awaited);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            free(awaited);
            break;
        }

        case EXPR_NULL_COALESCE: {
            // left ?? right
            char *left = codegen_expr(ctx, expr->as.null_coalesce.left);
            codegen_writeln(ctx, "HmlValue %s;", result);
            codegen_writeln(ctx, "if (!hml_is_null(%s)) {", left);
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "%s = %s;", result, left);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "} else {");
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "hml_release(&%s);", left);
            char *right = codegen_expr(ctx, expr->as.null_coalesce.right);
            codegen_writeln(ctx, "%s = %s;", result, right);
            free(right);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            free(left);
            break;
        }

        case EXPR_OPTIONAL_CHAIN: {
            // obj?.property or obj?.[index] or obj?.method()
            char *obj = codegen_expr(ctx, expr->as.optional_chain.object);
            codegen_writeln(ctx, "HmlValue %s;", result);
            codegen_writeln(ctx, "if (hml_is_null(%s)) {", obj);
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "%s = hml_val_null();", result);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "} else {");
            codegen_indent_inc(ctx);

            if (expr->as.optional_chain.is_property) {
                // obj?.property - check for built-in properties like .length
                const char *prop = expr->as.optional_chain.property;
                if (strcmp(prop, "length") == 0) {
                    codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY) {", obj);
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "%s = hml_array_length(%s);", result, obj);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "} else if (%s.type == HML_VAL_STRING) {", obj);
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "%s = hml_string_length(%s);", result, obj);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "} else if (%s.type == HML_VAL_BUFFER) {", obj);
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "%s = hml_buffer_length(%s);", result, obj);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "} else {");
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "%s = hml_object_get_field(%s, \"length\");", result, obj);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "}");
                } else {
                    codegen_writeln(ctx, "%s = hml_object_get_field(%s, \"%s\");", result, obj, prop);
                }
            } else if (expr->as.optional_chain.is_call) {
                // obj?.(args) - call obj directly if not null
                int num_args = expr->as.optional_chain.num_args;
                int args_counter = ctx->temp_counter++;

                // Evaluate arguments
                char **arg_temps = malloc(num_args * sizeof(char*));
                for (int i = 0; i < num_args; i++) {
                    arg_temps[i] = codegen_expr(ctx, expr->as.optional_chain.args[i]);
                }

                // Build args array and call
                if (num_args > 0) {
                    codegen_writeln(ctx, "HmlValue _args%d[%d];", args_counter, num_args);
                    for (int i = 0; i < num_args; i++) {
                        codegen_writeln(ctx, "_args%d[%d] = %s;", args_counter, i, arg_temps[i]);
                    }
                    codegen_writeln(ctx, "%s = hml_call_function(%s, _args%d, %d);",
                                  result, obj, args_counter, num_args);
                } else {
                    codegen_writeln(ctx, "%s = hml_call_function(%s, NULL, 0);", result, obj);
                }

                // Release argument temporaries
                for (int i = 0; i < num_args; i++) {
                    codegen_writeln(ctx, "hml_release(&%s);", arg_temps[i]);
                    free(arg_temps[i]);
                }
                free(arg_temps);
            } else {
                // obj?.[index]
                char *idx = codegen_expr(ctx, expr->as.optional_chain.index);
                codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_array_get(%s, %s);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_STRING) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_string_index(%s, %s);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_BUFFER) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_buffer_get(%s, %s);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_val_null();", result);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
                codegen_writeln(ctx, "hml_release(&%s);", idx);
                free(idx);
            }

            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            codegen_writeln(ctx, "hml_release(&%s);", obj);
            free(obj);
            break;
        }

        default:
            codegen_error(ctx, expr->line, "unsupported expression type %d", expr->type);
            codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            break;
    }

    return result;
}
