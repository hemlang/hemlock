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
    return expr && expr->type == EXPR_NUMBER && !expr->as.number.is_float;
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
    return (double)expr->as.number.int_value;
}

/*
 * Get the integer value of a constant.
 */
static int64_t get_number_as_int(Expr *expr) {
    if (expr->as.number.is_float) {
        return (int64_t)expr->as.number.float_value;
    }
    return expr->as.number.int_value;
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
 */
static Expr *try_fold_binary_numeric(BinaryOp op, Expr *left, Expr *right, int line, OptimizationStats *stats) {
    if (!is_const_number(left) || !is_const_number(right)) {
        return NULL;
    }

    int left_is_float = left->as.number.is_float;
    int right_is_float = right->as.number.is_float;
    int result_is_float = left_is_float || right_is_float;

    /* Get values */
    double left_f = get_number_as_double(left);
    double right_f = get_number_as_double(right);
    int64_t left_i = get_number_as_int(left);
    int64_t right_i = get_number_as_int(right);

    Expr *result = NULL;

    switch (op) {
        case OP_ADD:
            if (result_is_float) {
                result = make_float_expr(left_f + right_f, line);
            } else {
                result = make_int_expr(left_i + right_i, line);
            }
            break;

        case OP_SUB:
            if (result_is_float) {
                result = make_float_expr(left_f - right_f, line);
            } else {
                result = make_int_expr(left_i - right_i, line);
            }
            break;

        case OP_MUL:
            if (result_is_float) {
                result = make_float_expr(left_f * right_f, line);
            } else {
                result = make_int_expr(left_i * right_i, line);
            }
            break;

        case OP_DIV:
            /* Division by zero - don't fold, let runtime handle it */
            if (right_f == 0.0) {
                return NULL;
            }
            /* Division always returns float in Hemlock */
            result = make_float_expr(left_f / right_f, line);
            break;

        case OP_MOD:
            /* Modulo by zero - don't fold */
            if (right_i == 0) {
                return NULL;
            }
            if (result_is_float) {
                result = make_float_expr(fmod(left_f, right_f), line);
            } else {
                result = make_int_expr(left_i % right_i, line);
            }
            break;

        /* Comparison operators - always return boolean */
        case OP_EQUAL:
            result = make_bool_expr(left_f == right_f, line);
            break;

        case OP_NOT_EQUAL:
            result = make_bool_expr(left_f != right_f, line);
            break;

        case OP_LESS:
            result = make_bool_expr(left_f < right_f, line);
            break;

        case OP_LESS_EQUAL:
            result = make_bool_expr(left_f <= right_f, line);
            break;

        case OP_GREATER:
            result = make_bool_expr(left_f > right_f, line);
            break;

        case OP_GREATER_EQUAL:
            result = make_bool_expr(left_f >= right_f, line);
            break;

        /* Bitwise operators - only work on integers */
        case OP_BIT_AND:
            if (result_is_float) return NULL;
            result = make_int_expr(left_i & right_i, line);
            break;

        case OP_BIT_OR:
            if (result_is_float) return NULL;
            result = make_int_expr(left_i | right_i, line);
            break;

        case OP_BIT_XOR:
            if (result_is_float) return NULL;
            result = make_int_expr(left_i ^ right_i, line);
            break;

        case OP_BIT_LSHIFT:
            if (result_is_float) return NULL;
            result = make_int_expr(left_i << right_i, line);
            break;

        case OP_BIT_RSHIFT:
            if (result_is_float) return NULL;
            result = make_int_expr(left_i >> right_i, line);
            break;

        /* Logical operators handled separately */
        case OP_AND:
        case OP_OR:
            return NULL;
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
    if (op == OP_AND) {
        /* false && x → false */
        if (is_const_bool(left) && !left->as.boolean) {
            stats->booleans_simplified++;
            return left;
        }
        /* true && x → x */
        if (is_const_bool(left) && left->as.boolean) {
            stats->booleans_simplified++;
            return right;
        }
        /* x && true → x */
        if (is_const_bool(right) && right->as.boolean) {
            stats->booleans_simplified++;
            return left;
        }
        /* x && false - can't simplify (need to evaluate x for side effects) */
    }

    if (op == OP_OR) {
        /* true || x → true */
        if (is_const_bool(left) && left->as.boolean) {
            stats->booleans_simplified++;
            return left;
        }
        /* false || x → x */
        if (is_const_bool(left) && !left->as.boolean) {
            stats->booleans_simplified++;
            return right;
        }
        /* x || false → x */
        if (is_const_bool(right) && !right->as.boolean) {
            stats->booleans_simplified++;
            return left;
        }
        /* x || true - can't simplify (need to evaluate x for side effects) */
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
 * Try strength reduction for multiplication/division by powers of 2.
 * Only applies when BOTH operands are known to be integers at compile time.
 */
static Expr *try_strength_reduce(BinaryOp op, Expr *left, Expr *right, int line, OptimizationStats *stats) {
    /* Only for integer operations - both operands must be constant integers */
    if ((op == OP_MUL || op == OP_DIV) && is_const_int(left) && is_const_int(right)) {
        int64_t val = right->as.number.int_value;
        /* Check if power of 2 */
        if (val > 0 && (val & (val - 1)) == 0) {
            /* Calculate shift amount */
            int shift = 0;
            int64_t temp = val;
            while (temp > 1) {
                temp >>= 1;
                shift++;
            }
            /* x * (2^n) → x << n */
            /* x / (2^n) → x >> n (right shift is arithmetic for signed types) */
            Expr *shift_expr = make_int_expr(shift, line);
            Expr *result = malloc(sizeof(Expr));
            result->type = EXPR_BINARY;
            result->line = line;
            result->as.binary.op = (op == OP_MUL) ? OP_BIT_LSHIFT : OP_BIT_RSHIFT;
            result->as.binary.left = left;
            result->as.binary.right = shift_expr;
            stats->strength_reductions++;
            return result;
        }
        if (op == OP_DIV) {
            return NULL;
        }
        /* Check left side for power of 2: 2 * x → x << 1 */
        val = left->as.number.int_value;
        if (val > 0 && (val & (val - 1)) == 0) {
            int shift = 0;
            int64_t temp = val;
            while (temp > 1) {
                temp >>= 1;
                shift++;
            }
            Expr *shift_expr = make_int_expr(shift, line);
            Expr *result = malloc(sizeof(Expr));
            result->type = EXPR_BINARY;
            result->line = line;
            result->as.binary.op = OP_BIT_LSHIFT;
            result->as.binary.left = right;
            result->as.binary.right = shift_expr;
            stats->strength_reductions++;
            return result;
        }
    }

    return NULL;
}

/*
 * Optimize a unary expression.
 */
static Expr *optimize_unary(Expr *expr, OptimizationStats *stats) {
    Expr *operand = optimize_expr_internal(expr->as.unary.operand, stats);
    expr->as.unary.operand = operand;

    switch (expr->as.unary.op) {
        case UNARY_NOT:
            /* !true → false, !false → true */
            if (is_const_bool(operand)) {
                stats->booleans_simplified++;
                Expr *result = make_bool_expr(!operand->as.boolean, expr->line);
                expr_free(expr);  /* Free the replaced unary and its operand */
                return result;
            }
            /* !!x → x (if operand is also a NOT) */
            if (operand->type == EXPR_UNARY && operand->as.unary.op == UNARY_NOT) {
                stats->booleans_simplified++;
                Expr *result = operand->as.unary.operand;
                operand->as.unary.operand = NULL;  /* Detach before freeing */
                expr_free(expr);  /* Free outer and inner unary exprs */
                return result;
            }
            break;

        case UNARY_NEGATE:
            /* -5 → -5 (fold constant) */
            if (is_const_number(operand)) {
                stats->constants_folded++;
                Expr *result;
                if (operand->as.number.is_float) {
                    result = make_float_expr(-operand->as.number.float_value, expr->line);
                } else {
                    result = make_int_expr(-operand->as.number.int_value, expr->line);
                }
                expr_free(expr);  /* Free the replaced unary and its operand */
                return result;
            }
            /* --x → x */
            if (operand->type == EXPR_UNARY && operand->as.unary.op == UNARY_NEGATE) {
                stats->constants_folded++;
                Expr *result = operand->as.unary.operand;
                operand->as.unary.operand = NULL;  /* Detach before freeing */
                expr_free(expr);  /* Free outer and inner unary exprs */
                return result;
            }
            break;

        case UNARY_BIT_NOT:
            /* ~constant → folded */
            if (is_const_int(operand)) {
                stats->constants_folded++;
                Expr *result = make_int_expr(~operand->as.number.int_value, expr->line);
                expr_free(expr);  /* Free the replaced unary and its operand */
                return result;
            }
            /* ~~x → x */
            if (operand->type == EXPR_UNARY && operand->as.unary.op == UNARY_BIT_NOT) {
                stats->constants_folded++;
                Expr *result = operand->as.unary.operand;
                operand->as.unary.operand = NULL;  /* Detach before freeing */
                expr_free(expr);  /* Free outer and inner unary exprs */
                return result;
            }
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

    /* Try strength reduction */
    result = try_strength_reduce(op, left, right, line, stats);
    if (result) {
        expr_free(expr);  /* Free the replaced binary expression and both operands */
        return result;
    }

    /* Identity optimizations */
    /* x + 0 → x, x - 0 → x */
    if ((op == OP_ADD || op == OP_SUB) && is_const_number(right) && get_number_as_double(right) == 0.0) {
        stats->constants_folded++;
        expr->as.binary.left = NULL;  /* Detach left before freeing */
        expr_free(expr);  /* Free binary expr and the 0 operand */
        return left;
    }
    /* 0 + x → x */
    if (op == OP_ADD && is_const_number(left) && get_number_as_double(left) == 0.0) {
        stats->constants_folded++;
        expr->as.binary.right = NULL;  /* Detach right before freeing */
        expr_free(expr);  /* Free binary expr and the 0 operand */
        return right;
    }
    /* x * 1 → x, x / 1 → x */
    if ((op == OP_MUL || op == OP_DIV) && is_const_number(right) && get_number_as_double(right) == 1.0) {
        stats->constants_folded++;
        expr->as.binary.left = NULL;  /* Detach left before freeing */
        expr_free(expr);  /* Free binary expr and the 1 operand */
        return left;
    }
    /* 1 * x → x */
    if (op == OP_MUL && is_const_number(left) && get_number_as_double(left) == 1.0) {
        stats->constants_folded++;
        expr->as.binary.right = NULL;  /* Detach right before freeing */
        expr_free(expr);  /* Free binary expr and the 1 operand */
        return right;
    }
    /* x * 0 → 0 (only if x has no side effects, but we'll be conservative and skip this) */
    /* x | 0 → x, x ^ 0 → x */
    if ((op == OP_BIT_OR || op == OP_BIT_XOR) && is_const_int(right) && right->as.number.int_value == 0) {
        stats->constants_folded++;
        expr->as.binary.left = NULL;  /* Detach left before freeing */
        expr_free(expr);  /* Free binary expr and the 0 operand */
        return left;
    }
    /* x & -1 → x (all bits set) */
    if (op == OP_BIT_AND && is_const_int(right) && right->as.number.int_value == -1) {
        stats->constants_folded++;
        expr->as.binary.left = NULL;  /* Detach left before freeing */
        expr_free(expr);  /* Free binary expr and the -1 operand */
        return left;
    }
    /* x << 0 → x, x >> 0 → x */
    if ((op == OP_BIT_LSHIFT || op == OP_BIT_RSHIFT) && is_const_int(right) && right->as.number.int_value == 0) {
        stats->constants_folded++;
        expr->as.binary.left = NULL;  /* Detach left before freeing */
        expr_free(expr);  /* Free binary expr and the 0 operand */
        return left;
    }

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
