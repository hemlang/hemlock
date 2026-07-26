/*
 * Hemlock AST Optimizer
 *
 * Performs compile-time optimizations on the AST:
 * - Constant folding (2 + 3 → 5)
 * - Boolean simplification (!true → false, !!x → x)
 * - Strength reduction (x * 2 → x << 1 for integers)
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "frontend/optimizer.h"

/* Forward declarations */
static Expr *optimize_expr_internal(Expr *expr, OptimizationStats *stats);
static void optimize_stmt_internal(Stmt *stmt, OptimizationStats *stats);

/*
 * Check if an expression is a constant number.
 */
static int is_const_number(Expr *expr) {
    return expr && expr->type == EXPR_NUMBER;
}

/*
 * Check if an expression is a constant boolean.
 */
static int is_const_bool(Expr *expr) {
    return expr && expr->type == EXPR_BOOL;
}

/*
 * Check if an expression is a constant null.
 */
static int is_const_null(Expr *expr) {
    return expr && expr->type == EXPR_NULL;
}

/*
 * Check if an expression is a constant integer (not float).
 */
static int is_const_int(Expr *expr) {
    return expr && expr->type == EXPR_NUMBER && !expr->as.number.is_float && !expr->as.number.is_u64;
}

/*
 * Check if an expression is a constant string.
 */
static int is_const_string(Expr *expr) {
    return expr && expr->type == EXPR_STRING;
}

/*
 * Check if an expression is a non-null literal.
 */
static int is_const_literal_non_null(Expr *expr) {
    if (!expr) {
        return 0;
    }

    switch (expr->type) {
        case EXPR_NUMBER:
        case EXPR_BOOL:
        case EXPR_STRING:
        case EXPR_RUNE:
        case EXPR_ARRAY_LITERAL:
        case EXPR_OBJECT_LITERAL:
            return 1;
        default:
            return 0;
    }
}

/*
 * Determine truthiness for constant expressions (bool, number, or null).
 * Returns 1 if the expression is a constant and populates truthy; 0 otherwise.
 */
static int get_const_truthiness(Expr *expr, int *truthy) {
    if (is_const_bool(expr)) {
        *truthy = expr->as.boolean != 0;
        return 1;
    }
    if (is_const_number(expr)) {
        if (expr->as.number.is_float) {
            *truthy = expr->as.number.float_value != 0.0;
        } else if (expr->as.number.is_u64) {
            *truthy = expr->as.number.uint_value != 0;
        } else {
            *truthy = expr->as.number.int_value != 0;
        }
        return 1;
    }
    if (is_const_null(expr)) {
        *truthy = 0;
        return 1;
    }
    return 0;
}

/*
 * Get the numeric value of a constant as double.
 */
static double get_number_as_double(Expr *expr) {
    if (expr->as.number.is_float) {
        return expr->as.number.float_value;
    }
    if (expr->as.number.is_u64) {
        return (double)expr->as.number.uint_value;
    }
    return (double)expr->as.number.int_value;
}

/*
 * Whether an int64 value fits the i32 literal range. Integer literals (and
 * folded integer results) evaluate to i32 when they fit, i64 otherwise, so
 * folding must respect this boundary to preserve the runtime result type.
 */
static int fits_i32(int64_t v) {
    return v >= INT32_MIN && v <= INT32_MAX;
}

/*
 * Create a new integer expression with the given line number.
 */
static Expr *make_int_expr(int64_t value, int line) {
    Expr *expr = expr_number_int(value);
    expr->line = line;
    return expr;
}

/*
 * Create a new float expression with the given line number.
 */
static Expr *make_float_expr(double value, int line) {
    Expr *expr = expr_number_float(value);
    expr->line = line;
    return expr;
}

/*
 * Create a new u64 expression with the given line number.
 */
static Expr *make_u64_expr(uint64_t value, int line) {
    Expr *expr = expr_number_u64(value);
    expr->line = line;
    return expr;
}

/*
 * Create a new boolean expression with the given line number.
 */
static Expr *make_bool_expr(int value, int line) {
    Expr *expr = expr_bool(value);
    expr->line = line;
    return expr;
}

/*
 * Try to fold a binary operation on two constant numbers.
 * Returns NULL if folding is not possible.
 *
 * Folding must be exactly semantics-preserving: the folded literal has to
 * carry the same value AND the same runtime type the unfolded expression
 * would produce, and expressions whose evaluation raises a runtime error
 * (i32/i64 overflow, division/modulo by zero) must be left unfolded so the
 * error still fires. When in doubt, decline to fold.
 *
 * Literal typing recap (mirrors EXPR_NUMBER evaluation): floats are f64,
 * u64-flagged literals are u64, other integers are i32 when they fit and
 * i64 otherwise. Promotion for two int literals is therefore i32 (both fit
 * i32), u64 (either u64), or i64 (the rest). i32 and i64 add/sub/mul throw
 * on overflow at runtime; u64 wraps; signed % -1 is 0.
 */
static Expr *try_fold_binary_numeric(BinaryOp op, Expr *left, Expr *right, int line, OptimizationStats *stats) {
    if (!is_const_number(left) || !is_const_number(right)) {
        return NULL;
    }

    int left_is_float = left->as.number.is_float;
    int right_is_float = right->as.number.is_float;
    int any_float = left_is_float || right_is_float;
    int any_u64 = (!left_is_float && left->as.number.is_u64) ||
                  (!right_is_float && right->as.number.is_u64);

    Expr *result = NULL;

    if (any_float) {
        /* Promoted type is f64 (literal floats are always f64). Runtime
         * float arithmetic is IEEE 754: division/modulo by zero produce
         * inf/NaN without throwing, so folding is always safe here. */
        double l = get_number_as_double(left);
        double r = get_number_as_double(right);
        switch (op) {
            case OP_ADD: result = make_float_expr(l + r, line); break;
            case OP_SUB: result = make_float_expr(l - r, line); break;
            case OP_MUL: result = make_float_expr(l * r, line); break;
            case OP_DIV: result = make_float_expr(l / r, line); break;
            case OP_MOD: result = make_float_expr(fmod(l, r), line); break;
            case OP_EQUAL:         result = make_bool_expr(l == r, line); break;
            case OP_NOT_EQUAL:     result = make_bool_expr(l != r, line); break;
            case OP_LESS:          result = make_bool_expr(l < r, line); break;
            case OP_LESS_EQUAL:    result = make_bool_expr(l <= r, line); break;
            case OP_GREATER:       result = make_bool_expr(l > r, line); break;
            case OP_GREATER_EQUAL: result = make_bool_expr(l >= r, line); break;
            default: return NULL;  /* bitwise/shift on floats: runtime error */
        }
    } else if (any_u64) {
        /* Promoted type is u64: unsigned 64-bit with wrapping semantics.
         * The folded literal keeps the u64 flag so its runtime type stays
         * u64 regardless of magnitude. */
        uint64_t l = left->as.number.is_u64 ? left->as.number.uint_value
                                            : (uint64_t)left->as.number.int_value;
        uint64_t r = right->as.number.is_u64 ? right->as.number.uint_value
                                             : (uint64_t)right->as.number.int_value;
        switch (op) {
            case OP_ADD: result = make_u64_expr(l + r, line); break;
            case OP_SUB: result = make_u64_expr(l - r, line); break;
            case OP_MUL: result = make_u64_expr(l * r, line); break;
            case OP_DIV:
                if (r == 0) return NULL;  /* runtime throws for int operands */
                result = make_float_expr((double)l / (double)r, line);
                break;
            case OP_MOD:
                if (r == 0) return NULL;
                result = make_u64_expr(l % r, line);
                break;
            case OP_EQUAL:         result = make_bool_expr(l == r, line); break;
            case OP_NOT_EQUAL:     result = make_bool_expr(l != r, line); break;
            case OP_LESS:          result = make_bool_expr(l < r, line); break;
            case OP_LESS_EQUAL:    result = make_bool_expr(l <= r, line); break;
            case OP_GREATER:       result = make_bool_expr(l > r, line); break;
            case OP_GREATER_EQUAL: result = make_bool_expr(l >= r, line); break;
            case OP_BIT_AND: result = make_u64_expr(l & r, line); break;
            case OP_BIT_OR:  result = make_u64_expr(l | r, line); break;
            case OP_BIT_XOR: result = make_u64_expr(l ^ r, line); break;
            default: return NULL;  /* shifts: width semantics left to runtime */
        }
    } else {
        /* Plain int literals: promoted type is i32 when both operands fit
         * i32, i64 otherwise. */
        int64_t l = left->as.number.int_value;
        int64_t r = right->as.number.int_value;
        int p32 = fits_i32(l) && fits_i32(r);
        switch (op) {
            case OP_ADD:
            case OP_SUB:
            case OP_MUL: {
                int64_t res;
                int ovf = (op == OP_ADD) ? __builtin_add_overflow(l, r, &res)
                        : (op == OP_SUB) ? __builtin_sub_overflow(l, r, &res)
                                         : __builtin_mul_overflow(l, r, &res);
                if (ovf) return NULL;              /* runtime throws i64 overflow */
                if (p32 && !fits_i32(res)) return NULL;  /* runtime throws i32 overflow */
                if (!p32 && fits_i32(res)) return NULL;  /* would demote i64 result to i32 */
                result = make_int_expr(res, line);
                break;
            }
            case OP_DIV:
                if (r == 0) return NULL;  /* runtime throws Division by zero */
                result = make_float_expr((double)l / (double)r, line);
                break;
            case OP_MOD: {
                if (r == 0) return NULL;
                int64_t res = (r == -1) ? 0 : l % r;  /* MIN % -1 is defined as 0 */
                if (!p32 && fits_i32(res)) return NULL;  /* keep runtime i64 type */
                result = make_int_expr(res, line);
                break;
            }
            case OP_EQUAL:         result = make_bool_expr(l == r, line); break;
            case OP_NOT_EQUAL:     result = make_bool_expr(l != r, line); break;
            case OP_LESS:          result = make_bool_expr(l < r, line); break;
            case OP_LESS_EQUAL:    result = make_bool_expr(l <= r, line); break;
            case OP_GREATER:       result = make_bool_expr(l > r, line); break;
            case OP_GREATER_EQUAL: result = make_bool_expr(l >= r, line); break;
            case OP_BIT_AND:
            case OP_BIT_OR:
            case OP_BIT_XOR: {
                int64_t res = (op == OP_BIT_AND) ? (l & r)
                            : (op == OP_BIT_OR)  ? (l | r) : (l ^ r);
                if (!p32 && fits_i32(res)) return NULL;  /* keep runtime i64 type */
                result = make_int_expr(res, line);
                break;
            }
            case OP_BIT_LSHIFT: {
                if (r < 0) return NULL;  /* runtime error: negative shift */
                if (!p32) return NULL;   /* i64 width semantics left to runtime */
                int32_t li = (int32_t)l;
                int32_t res = (r >= 32) ? 0 : (int32_t)((uint32_t)li << r);
                result = make_int_expr(res, line);
                break;
            }
            case OP_BIT_RSHIFT: {
                if (r < 0) return NULL;
                if (!p32) return NULL;
                int32_t li = (int32_t)l;
                int32_t res = (r >= 32) ? (li < 0 ? -1 : 0) : (li >> (int32_t)r);
                result = make_int_expr(res, line);
                break;
            }
            default: return NULL;  /* OP_AND/OP_OR handled separately */
        }
    }

    if (result) {
        stats->constants_folded++;
    }
    return result;
}

/*
 * Try to fold a binary operation on two constant booleans.
 */
static Expr *try_fold_binary_bool(BinaryOp op, Expr *left, Expr *right, int line, OptimizationStats *stats) {
    if (!is_const_bool(left) || !is_const_bool(right)) {
        return NULL;
    }

    int left_v = left->as.boolean;
    int right_v = right->as.boolean;
    Expr *result = NULL;

    switch (op) {
        case OP_AND:
            result = make_bool_expr(left_v && right_v, line);
            break;

        case OP_OR:
            result = make_bool_expr(left_v || right_v, line);
            break;

        case OP_EQUAL:
            result = make_bool_expr(left_v == right_v, line);
            break;

        case OP_NOT_EQUAL:
            result = make_bool_expr(left_v != right_v, line);
            break;

        default:
            return NULL;
    }

    if (result) {
        stats->constants_folded++;
    }
    return result;
}

/*
 * Try to fold string concatenation.
 */
static Expr *try_fold_string_concat(BinaryOp op, Expr *left, Expr *right, int line, OptimizationStats *stats) {
    if (op != OP_ADD) return NULL;
    if (!is_const_string(left) || !is_const_string(right)) return NULL;

    size_t left_len = strlen(left->as.string);
    size_t right_len = strlen(right->as.string);
    char *new_str = malloc(left_len + right_len + 1);
    if (!new_str) return NULL;  // Allocation failed, skip optimization
    // Safe: use memcpy instead of strcpy/strcat to avoid potential overflow
    memcpy(new_str, left->as.string, left_len);
    memcpy(new_str + left_len, right->as.string, right_len);
    new_str[left_len + right_len] = '\0';

    Expr *result = expr_string(new_str);
    result->line = line;
    free(new_str);

    stats->constants_folded++;
    return result;
}

/*
 * Try short-circuit optimization for && and ||.
 * Returns the simplified expression or NULL if no optimization possible.
 */
static Expr *try_short_circuit(BinaryOp op, Expr *left, Expr *right, OptimizationStats *stats) {
    if (op != OP_AND && op != OP_OR) {
        return NULL;
    }

    /* && and || always evaluate to a bool at runtime:
     *   a && b  →  truthy(a) ? bool(truthy(b)) : false
     *   a || b  →  truthy(a) ? true : bool(truthy(b))
     * A rewrite may therefore only ever produce a bool constant — returning
     * a non-bool operand (e.g. `x && true → x`) would change the result's
     * value and type. Rewrites that drop a non-constant left operand would
     * also skip its side effects, which the runtime does not. */
    int left_truthy;
    if (!get_const_truthiness(left, &left_truthy)) {
        return NULL;
    }

    /* Constant left operand decides the result on its own. */
    if (op == OP_AND && !left_truthy) {
        stats->booleans_simplified++;
        return make_bool_expr(0, left->line);
    }
    if (op == OP_OR && left_truthy) {
        stats->booleans_simplified++;
        return make_bool_expr(1, left->line);
    }

    /* Left is constant but does not decide; result is bool(truthy(right)). */
    int right_truthy;
    if (get_const_truthiness(right, &right_truthy)) {
        stats->booleans_simplified++;
        return make_bool_expr(right_truthy, left->line);
    }

    return NULL;
}

/*
 * Try to fold constant string interpolation into a single literal.
 */
static Expr *try_fold_string_interpolation(Expr *expr, OptimizationStats *stats) {
    if (!expr || expr->type != EXPR_STRING_INTERPOLATION) {
        return NULL;
    }

    for (int i = 0; i < expr->as.string_interpolation.num_parts; i++) {
        if (!is_const_string(expr->as.string_interpolation.expr_parts[i])) {
            return NULL;
        }
    }

    size_t total_len = 0;
    for (int i = 0; i < expr->as.string_interpolation.num_parts; i++) {
        total_len += strlen(expr->as.string_interpolation.string_parts[i]);
        total_len += strlen(expr->as.string_interpolation.expr_parts[i]->as.string);
    }
    total_len += strlen(expr->as.string_interpolation.string_parts[expr->as.string_interpolation.num_parts]);

    char *buffer = malloc(total_len + 1);
    if (!buffer) return NULL;  // Allocation failed, skip optimization
    char *cursor = buffer;
    for (int i = 0; i < expr->as.string_interpolation.num_parts; i++) {
        size_t part_len = strlen(expr->as.string_interpolation.string_parts[i]);
        memcpy(cursor, expr->as.string_interpolation.string_parts[i], part_len);
        cursor += part_len;

        size_t expr_len = strlen(expr->as.string_interpolation.expr_parts[i]->as.string);
        memcpy(cursor, expr->as.string_interpolation.expr_parts[i]->as.string, expr_len);
        cursor += expr_len;
    }
    size_t tail_len = strlen(expr->as.string_interpolation.string_parts[expr->as.string_interpolation.num_parts]);
    memcpy(cursor, expr->as.string_interpolation.string_parts[expr->as.string_interpolation.num_parts], tail_len);
    cursor += tail_len;
    *cursor = '\0';

    Expr *result = expr_string(buffer);
    result->line = expr->line;
    free(buffer);

    stats->constants_folded++;
    stats->literals_folded++;
    return result;
}

/*
 * Optimize a unary expression.
 */
static Expr *optimize_unary(Expr *expr, OptimizationStats *stats) {
    Expr *operand = optimize_expr_internal(expr->as.unary.operand, stats);
    expr->as.unary.operand = operand;

    switch (expr->as.unary.op) {
        case UNARY_NOT: {
            /* !const → bool. Runtime: !x is bool(!truthy(x)). Note !!x → x
             * would be unsound for non-bool x (!!5 is `true`, not 5). */
            int truthy;
            if (get_const_truthiness(operand, &truthy)) {
                stats->booleans_simplified++;
                Expr *result = make_bool_expr(!truthy, expr->line);
                expr_free(expr);  /* Free the replaced unary and its operand */
                return result;
            }
            break;
        }

        case UNARY_NEGATE:
            /* Fold negation of numeric literals. This is what makes a
             * negative literal like -2147483648 a plain i32 constant.
             * Cases that raise at runtime (negating i64 MIN, negating a u64
             * above the i64 range) are left unfolded so the error fires.
             * Note -(-x) → x is NOT applied for non-constant x: negation of
             * i32/i64 MIN throws, so double negation is not the identity. */
            if (is_const_number(operand)) {
                Expr *result = NULL;
                if (operand->as.number.is_float) {
                    result = make_float_expr(-operand->as.number.float_value, expr->line);
                } else if (operand->as.number.is_u64) {
                    uint64_t uv = operand->as.number.uint_value;
                    if (uv <= (uint64_t)INT64_MAX) {
                        result = make_int_expr(-(int64_t)uv, expr->line);
                    } else if (uv == (uint64_t)INT64_MAX + 1) {
                        result = make_int_expr(INT64_MIN, expr->line);
                    }
                    /* larger u64: runtime error, don't fold */
                } else if (operand->as.number.int_value != INT64_MIN) {
                    result = make_int_expr(-operand->as.number.int_value, expr->line);
                }
                if (result) {
                    stats->constants_folded++;
                    expr_free(expr);  /* Free the replaced unary and its operand */
                    return result;
                }
            }
            break;

        case UNARY_BIT_NOT:
            /* ~constant → folded. ~ preserves the operand's runtime type, so
             * only fold when the folded literal re-infers the same type:
             * i32 stays i32 (~ of an i32-range value is i32-range), i64
             * operands must not collapse into the i32 literal range. */
            if (is_const_int(operand)) {
                int64_t v = operand->as.number.int_value;
                int64_t res = ~v;
                if (fits_i32(v) || !fits_i32(res)) {
                    stats->constants_folded++;
                    Expr *result = make_int_expr(res, expr->line);
                    expr_free(expr);  /* Free the replaced unary and its operand */
                    return result;
                }
            }
            /* ~~x → x is sound (~ is a self-inverse bijection that keeps
             * the operand type), but only when x is integer-typed; leave
             * non-constant operands alone to preserve runtime errors. */
            break;
    }

    return expr;
}

/*
 * Optimize a binary expression.
 */
static Expr *optimize_binary(Expr *expr, OptimizationStats *stats) {
    /* First, recursively optimize operands */
    Expr *left = optimize_expr_internal(expr->as.binary.left, stats);
    Expr *right = optimize_expr_internal(expr->as.binary.right, stats);
    expr->as.binary.left = left;
    expr->as.binary.right = right;

    BinaryOp op = expr->as.binary.op;
    int line = expr->line;
    Expr *result;

    /* Try constant folding for numbers */
    result = try_fold_binary_numeric(op, left, right, line, stats);
    if (result) {
        expr_free(expr);  /* Free the replaced binary expression and both operands */
        return result;
    }

    /* Try constant folding for booleans */
    result = try_fold_binary_bool(op, left, right, line, stats);
    if (result) {
        expr_free(expr);  /* Free the replaced binary expression and both operands */
        return result;
    }

    /* Try string concatenation folding */
    result = try_fold_string_concat(op, left, right, line, stats);
    if (result) {
        expr_free(expr);  /* Free the replaced binary expression and both operands */
        return result;
    }

    /* Try short-circuit optimization - this returns one of the operands, so handle specially */
    result = try_short_circuit(op, left, right, stats);
    if (result) {
        /* Detach the returned operand before freeing */
        if (result == left) {
            expr->as.binary.left = NULL;
        } else if (result == right) {
            expr->as.binary.right = NULL;
        }
        expr_free(expr);  /* Free binary expr and the unused operand */
        return result;
    }

    /* Algebraic identity rewrites (x + 0 → x, x * 1 → x, x / 1 → x,
     * x | 0 → x, x << 0 → x, ...) are deliberately NOT applied. They are
     * unsound without knowing x's runtime type: binary operators promote
     * (u8 + 0 is i32, not u8), `/` always returns a float (x / 1 must be
     * f64 even for int x), and non-numeric operands must still raise a
     * runtime error rather than pass through unchanged. */
    return expr;
}

/*
 * Optimize a ternary expression.
 */
static Expr *optimize_ternary(Expr *expr, OptimizationStats *stats) {
    Expr *condition = optimize_expr_internal(expr->as.ternary.condition, stats);
    expr->as.ternary.condition = condition;

    /* If condition is constant, we can eliminate the branch */
    if (is_const_bool(condition)) {
        stats->booleans_simplified++;
        Expr *result;
        if (condition->as.boolean) {
            result = optimize_expr_internal(expr->as.ternary.true_expr, stats);
            expr->as.ternary.true_expr = NULL;  /* Detach before freeing */
        } else {
            result = optimize_expr_internal(expr->as.ternary.false_expr, stats);
            expr->as.ternary.false_expr = NULL;  /* Detach before freeing */
        }
        expr_free(expr);  /* Free condition and unused branch */
        return result;
    }

    /* Otherwise optimize both branches */
    expr->as.ternary.true_expr = optimize_expr_internal(expr->as.ternary.true_expr, stats);
    expr->as.ternary.false_expr = optimize_expr_internal(expr->as.ternary.false_expr, stats);

    return expr;
}

/*
 * Recursively optimize an expression.
 */
static Expr *optimize_expr_internal(Expr *expr, OptimizationStats *stats) {
    if (!expr) return NULL;

    switch (expr->type) {
        case EXPR_BINARY:
            return optimize_binary(expr, stats);

        case EXPR_UNARY:
            return optimize_unary(expr, stats);

        case EXPR_TERNARY:
            return optimize_ternary(expr, stats);

        case EXPR_CALL:
            expr->as.call.func = optimize_expr_internal(expr->as.call.func, stats);
            for (int i = 0; i < expr->as.call.num_args; i++) {
                expr->as.call.args[i] = optimize_expr_internal(expr->as.call.args[i], stats);
            }
            break;

        case EXPR_ASSIGN:
            expr->as.assign.value = optimize_expr_internal(expr->as.assign.value, stats);
            break;

        case EXPR_GET_PROPERTY:
            expr->as.get_property.object = optimize_expr_internal(expr->as.get_property.object, stats);
            break;

        case EXPR_SET_PROPERTY:
            expr->as.set_property.object = optimize_expr_internal(expr->as.set_property.object, stats);
            expr->as.set_property.value = optimize_expr_internal(expr->as.set_property.value, stats);
            break;

        case EXPR_INDEX:
            expr->as.index.object = optimize_expr_internal(expr->as.index.object, stats);
            expr->as.index.index = optimize_expr_internal(expr->as.index.index, stats);
            break;

        case EXPR_INDEX_ASSIGN:
            expr->as.index_assign.object = optimize_expr_internal(expr->as.index_assign.object, stats);
            expr->as.index_assign.index = optimize_expr_internal(expr->as.index_assign.index, stats);
            expr->as.index_assign.value = optimize_expr_internal(expr->as.index_assign.value, stats);
            break;

        case EXPR_FUNCTION:
            /* Optimize function body */
            optimize_stmt_internal(expr->as.function.body, stats);
            /* Optimize default parameter values */
            for (int i = 0; i < expr->as.function.num_params; i++) {
                if (expr->as.function.param_defaults && expr->as.function.param_defaults[i]) {
                    expr->as.function.param_defaults[i] =
                        optimize_expr_internal(expr->as.function.param_defaults[i], stats);
                }
            }
            break;

        case EXPR_ARRAY_LITERAL:
            for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                expr->as.array_literal.elements[i] =
                    optimize_expr_internal(expr->as.array_literal.elements[i], stats);
            }
            break;

        case EXPR_OBJECT_LITERAL:
            for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
                expr->as.object_literal.field_values[i] =
                    optimize_expr_internal(expr->as.object_literal.field_values[i], stats);
            }
            break;

        case EXPR_PREFIX_INC:
            expr->as.prefix_inc.operand = optimize_expr_internal(expr->as.prefix_inc.operand, stats);
            break;

        case EXPR_PREFIX_DEC:
            expr->as.prefix_dec.operand = optimize_expr_internal(expr->as.prefix_dec.operand, stats);
            break;

        case EXPR_POSTFIX_INC:
            expr->as.postfix_inc.operand = optimize_expr_internal(expr->as.postfix_inc.operand, stats);
            break;

        case EXPR_POSTFIX_DEC:
            expr->as.postfix_dec.operand = optimize_expr_internal(expr->as.postfix_dec.operand, stats);
            break;

        case EXPR_AWAIT:
            expr->as.await_expr.awaited_expr = optimize_expr_internal(expr->as.await_expr.awaited_expr, stats);
            break;

        case EXPR_STRING_INTERPOLATION:
            for (int i = 0; i < expr->as.string_interpolation.num_parts; i++) {
                expr->as.string_interpolation.expr_parts[i] =
                    optimize_expr_internal(expr->as.string_interpolation.expr_parts[i], stats);
            }
            {
                Expr *folded = try_fold_string_interpolation(expr, stats);
                if (folded) {
                    expr_free(expr);  // Free the replaced interpolation expression
                    return folded;
                }
            }
            break;

        case EXPR_OPTIONAL_CHAIN:
            expr->as.optional_chain.object = optimize_expr_internal(expr->as.optional_chain.object, stats);
            if (expr->as.optional_chain.index) {
                expr->as.optional_chain.index = optimize_expr_internal(expr->as.optional_chain.index, stats);
            }
            if (expr->as.optional_chain.is_call && expr->as.optional_chain.args) {
                for (int i = 0; i < expr->as.optional_chain.num_args; i++) {
                    expr->as.optional_chain.args[i] =
                        optimize_expr_internal(expr->as.optional_chain.args[i], stats);
                }
            }
            break;

        case EXPR_NULL_COALESCE:
            expr->as.null_coalesce.left = optimize_expr_internal(expr->as.null_coalesce.left, stats);
            expr->as.null_coalesce.right = optimize_expr_internal(expr->as.null_coalesce.right, stats);
            /* Optimization: if left is non-null constant, return it directly */
            if (is_const_literal_non_null(expr->as.null_coalesce.left)) {
                stats->constants_folded++;
                stats->literals_folded++;
                /* Save the result, free unused right child and parent node */
                Expr *result = expr->as.null_coalesce.left;
                expr_free(expr->as.null_coalesce.right);
                expr->as.null_coalesce.left = NULL;  /* Don't double-free */
                expr->as.null_coalesce.right = NULL;
                free(expr);  /* Free just the parent node */
                return result;
            }
            /* If left is null literal, return right */
            if (expr->as.null_coalesce.left->type == EXPR_NULL) {
                stats->constants_folded++;
                stats->literals_folded++;
                /* Save the result, free unused left child and parent node */
                Expr *result = expr->as.null_coalesce.right;
                expr_free(expr->as.null_coalesce.left);
                expr->as.null_coalesce.left = NULL;  /* Don't double-free */
                expr->as.null_coalesce.right = NULL;
                free(expr);  /* Free just the parent node */
                return result;
            }
            break;

        case EXPR_MATCH:
            /* Optimize the scrutinee expression */
            expr->as.match_expr.scrutinee = optimize_expr_internal(expr->as.match_expr.scrutinee, stats);
            /* Optimize each match arm */
            for (int i = 0; i < expr->as.match_expr.num_arms; i++) {
                MatchArm *arm = &expr->as.match_expr.arms[i];
                /* Optimize literal patterns */
                if (arm->pattern && arm->pattern->type == PATTERN_LITERAL) {
                    arm->pattern->as.literal = optimize_expr_internal(arm->pattern->as.literal, stats);
                }
                /* Optimize guard expression if present */
                if (arm->guard) {
                    arm->guard = optimize_expr_internal(arm->guard, stats);
                }
                /* Optimize body expression */
                arm->body = optimize_expr_internal(arm->body, stats);
            }
            break;

        /* Literals - nothing to optimize */
        case EXPR_NUMBER:
        case EXPR_BOOL:
        case EXPR_STRING:
        case EXPR_RUNE:
        case EXPR_IDENT:
        case EXPR_NULL:
            break;
    }

    return expr;
}

/*
 * Optimize a statement.
 */
static void optimize_stmt_internal(Stmt *stmt, OptimizationStats *stats) {
    if (!stmt) return;

    switch (stmt->type) {
        case STMT_EXPR:
            stmt->as.expr = optimize_expr_internal(stmt->as.expr, stats);
            break;

        case STMT_LET:
            if (stmt->as.let.value) {
                stmt->as.let.value = optimize_expr_internal(stmt->as.let.value, stats);
            }
            break;

        case STMT_CONST:
            if (stmt->as.const_stmt.value) {
                stmt->as.const_stmt.value = optimize_expr_internal(stmt->as.const_stmt.value, stats);
            }
            break;

        case STMT_RETURN:
            if (stmt->as.return_stmt.value) {
                stmt->as.return_stmt.value = optimize_expr_internal(stmt->as.return_stmt.value, stats);
            }
            break;

        case STMT_IF:
            stmt->as.if_stmt.condition = optimize_expr_internal(stmt->as.if_stmt.condition, stats);
            {
                int truthy = 0;
                if (get_const_truthiness(stmt->as.if_stmt.condition, &truthy)) {
                    Stmt *selected = truthy ? stmt->as.if_stmt.then_branch : stmt->as.if_stmt.else_branch;
                    Stmt *discarded = truthy ? stmt->as.if_stmt.else_branch : stmt->as.if_stmt.then_branch;
                    stats->dead_code_eliminated++;
                    expr_free(stmt->as.if_stmt.condition);
                    if (discarded) {
                        stmt_free(discarded);
                    }
                    if (selected) {
                        optimize_stmt_internal(selected, stats);
                        Stmt replacement = *selected;
                        free(selected);
                        *stmt = replacement;
                    } else {
                        stmt->type = STMT_BLOCK;
                        stmt->as.block.statements = NULL;
                        stmt->as.block.count = 0;
                    }
                    return;
                }
            }
            optimize_stmt_internal(stmt->as.if_stmt.then_branch, stats);
            if (stmt->as.if_stmt.else_branch) {
                optimize_stmt_internal(stmt->as.if_stmt.else_branch, stats);
            }
            break;

        case STMT_WHILE:
            stmt->as.while_stmt.condition = optimize_expr_internal(stmt->as.while_stmt.condition, stats);
            {
                int truthy = 0;
                if (get_const_truthiness(stmt->as.while_stmt.condition, &truthy) && !truthy) {
                    stats->dead_code_eliminated++;
                    expr_free(stmt->as.while_stmt.condition);
                    stmt_free(stmt->as.while_stmt.body);
                    free(stmt->as.while_stmt.label);
                    stmt->type = STMT_BLOCK;
                    stmt->as.block.statements = NULL;
                    stmt->as.block.count = 0;
                    return;
                }
            }
            optimize_stmt_internal(stmt->as.while_stmt.body, stats);
            break;

        case STMT_LOOP:
            optimize_stmt_internal(stmt->as.loop_stmt.body, stats);
            break;

        case STMT_FOR:
            if (stmt->as.for_loop.initializer) {
                optimize_stmt_internal(stmt->as.for_loop.initializer, stats);
            }
            if (stmt->as.for_loop.condition) {
                stmt->as.for_loop.condition = optimize_expr_internal(stmt->as.for_loop.condition, stats);
            }
            if (stmt->as.for_loop.increment) {
                stmt->as.for_loop.increment = optimize_expr_internal(stmt->as.for_loop.increment, stats);
            }
            optimize_stmt_internal(stmt->as.for_loop.body, stats);
            break;

        case STMT_FOR_IN:
            stmt->as.for_in.iterable = optimize_expr_internal(stmt->as.for_in.iterable, stats);
            optimize_stmt_internal(stmt->as.for_in.body, stats);
            break;

        case STMT_BLOCK:
            for (int i = 0; i < stmt->as.block.count; i++) {
                optimize_stmt_internal(stmt->as.block.statements[i], stats);
            }
            break;

        case STMT_SWITCH:
            stmt->as.switch_stmt.expr = optimize_expr_internal(stmt->as.switch_stmt.expr, stats);
            for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
                if (stmt->as.switch_stmt.case_values[i]) {
                    stmt->as.switch_stmt.case_values[i] =
                        optimize_expr_internal(stmt->as.switch_stmt.case_values[i], stats);
                }
                optimize_stmt_internal(stmt->as.switch_stmt.case_bodies[i], stats);
            }
            break;

        case STMT_DEFER:
            stmt->as.defer_stmt.call = optimize_expr_internal(stmt->as.defer_stmt.call, stats);
            break;

        case STMT_TRY:
            optimize_stmt_internal(stmt->as.try_stmt.try_block, stats);
            if (stmt->as.try_stmt.catch_block) {
                optimize_stmt_internal(stmt->as.try_stmt.catch_block, stats);
            }
            if (stmt->as.try_stmt.finally_block) {
                optimize_stmt_internal(stmt->as.try_stmt.finally_block, stats);
            }
            break;

        case STMT_THROW:
            stmt->as.throw_stmt.value = optimize_expr_internal(stmt->as.throw_stmt.value, stats);
            break;

        case STMT_DEFINE_OBJECT:
            for (int i = 0; i < stmt->as.define_object.num_fields; i++) {
                if (stmt->as.define_object.field_defaults && stmt->as.define_object.field_defaults[i]) {
                    stmt->as.define_object.field_defaults[i] =
                        optimize_expr_internal(stmt->as.define_object.field_defaults[i], stats);
                }
            }
            break;

        case STMT_ENUM:
            for (int i = 0; i < stmt->as.enum_decl.num_variants; i++) {
                if (stmt->as.enum_decl.variant_values && stmt->as.enum_decl.variant_values[i]) {
                    stmt->as.enum_decl.variant_values[i] =
                        optimize_expr_internal(stmt->as.enum_decl.variant_values[i], stats);
                }
            }
            break;

        /* No expressions to optimize */
        case STMT_BREAK:
        case STMT_CONTINUE:
        case STMT_IMPORT:
        case STMT_EXPORT:
        case STMT_IMPORT_FFI:
        case STMT_EXTERN_FN:
        case STMT_TYPE_ALIAS:
            break;
    }
}

/*
 * Public API: Optimize a single expression.
 */
Expr *optimize_expr(Expr *expr, OptimizationStats *stats) {
    return optimize_expr_internal(expr, stats);
}

/*
 * Public API: Optimize a single statement.
 */
void optimize_stmt(Stmt *stmt, OptimizationStats *stats) {
    optimize_stmt_internal(stmt, stats);
}

/*
 * Public API: Optimize all statements in a program.
 */
OptimizationStats optimize_program(Stmt **statements, int count) {
    OptimizationStats stats = {0, 0, 0, 0, 0};

    for (int i = 0; i < count; i++) {
        optimize_stmt_internal(statements[i], &stats);
    }

    return stats;
}
