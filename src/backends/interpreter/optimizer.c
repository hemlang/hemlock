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
#include "optimizer.h"

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
    strcpy(new_str, left->as.string);
    strcat(new_str, right->as.string);

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
 * Try strength reduction for multiplication/division by powers of 2.
 */
static Expr *try_strength_reduce(BinaryOp op, Expr *left, Expr *right, int line, OptimizationStats *stats) {
    /* Only for integer operations */
    if (op == OP_MUL && is_const_int(right)) {
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
            Expr *shift_expr = make_int_expr(shift, line);
            Expr *result = malloc(sizeof(Expr));
            result->type = EXPR_BINARY;
            result->line = line;
            result->as.binary.op = OP_BIT_LSHIFT;
            result->as.binary.left = left;
            result->as.binary.right = shift_expr;
            stats->strength_reductions++;
            return result;
        }
    }

    /* x * 2 on left side too: 2 * x → x << 1 */
    if (op == OP_MUL && is_const_int(left)) {
        int64_t val = left->as.number.int_value;
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
                return make_bool_expr(!operand->as.boolean, expr->line);
            }
            /* !!x → x (if operand is also a NOT) */
            if (operand->type == EXPR_UNARY && operand->as.unary.op == UNARY_NOT) {
                stats->booleans_simplified++;
                return operand->as.unary.operand;
            }
            break;

        case UNARY_NEGATE:
            /* -5 → -5 (fold constant) */
            if (is_const_number(operand)) {
                stats->constants_folded++;
                if (operand->as.number.is_float) {
                    return make_float_expr(-operand->as.number.float_value, expr->line);
                } else {
                    return make_int_expr(-operand->as.number.int_value, expr->line);
                }
            }
            /* --x → x */
            if (operand->type == EXPR_UNARY && operand->as.unary.op == UNARY_NEGATE) {
                stats->constants_folded++;
                return operand->as.unary.operand;
            }
            break;

        case UNARY_BIT_NOT:
            /* ~constant → folded */
            if (is_const_int(operand)) {
                stats->constants_folded++;
                return make_int_expr(~operand->as.number.int_value, expr->line);
            }
            /* ~~x → x */
            if (operand->type == EXPR_UNARY && operand->as.unary.op == UNARY_BIT_NOT) {
                stats->constants_folded++;
                return operand->as.unary.operand;
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
    if (result) return result;

    /* Try constant folding for booleans */
    result = try_fold_binary_bool(op, left, right, line, stats);
    if (result) return result;

    /* Try string concatenation folding */
    result = try_fold_string_concat(op, left, right, line, stats);
    if (result) return result;

    /* Try short-circuit optimization */
    result = try_short_circuit(op, left, right, stats);
    if (result) return result;

    /* Try strength reduction */
    result = try_strength_reduce(op, left, right, line, stats);
    if (result) return result;

    /* Identity optimizations */
    /* x + 0 → x, x - 0 → x */
    if ((op == OP_ADD || op == OP_SUB) && is_const_number(right) && get_number_as_double(right) == 0.0) {
        stats->constants_folded++;
        return left;
    }
    /* 0 + x → x */
    if (op == OP_ADD && is_const_number(left) && get_number_as_double(left) == 0.0) {
        stats->constants_folded++;
        return right;
    }
    /* x * 1 → x, x / 1 → x */
    if ((op == OP_MUL || op == OP_DIV) && is_const_number(right) && get_number_as_double(right) == 1.0) {
        stats->constants_folded++;
        return left;
    }
    /* 1 * x → x */
    if (op == OP_MUL && is_const_number(left) && get_number_as_double(left) == 1.0) {
        stats->constants_folded++;
        return right;
    }
    /* x * 0 → 0 (only if x has no side effects, but we'll be conservative and skip this) */
    /* x | 0 → x, x ^ 0 → x */
    if ((op == OP_BIT_OR || op == OP_BIT_XOR) && is_const_int(right) && right->as.number.int_value == 0) {
        stats->constants_folded++;
        return left;
    }
    /* x & -1 → x (all bits set) */
    if (op == OP_BIT_AND && is_const_int(right) && right->as.number.int_value == -1) {
        stats->constants_folded++;
        return left;
    }
    /* x << 0 → x, x >> 0 → x */
    if ((op == OP_BIT_LSHIFT || op == OP_BIT_RSHIFT) && is_const_int(right) && right->as.number.int_value == 0) {
        stats->constants_folded++;
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
        if (condition->as.boolean) {
            return optimize_expr_internal(expr->as.ternary.true_expr, stats);
        } else {
            return optimize_expr_internal(expr->as.ternary.false_expr, stats);
        }
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
            if (expr->as.null_coalesce.left->type != EXPR_NULL &&
                (is_const_number(expr->as.null_coalesce.left) ||
                 is_const_bool(expr->as.null_coalesce.left) ||
                 is_const_string(expr->as.null_coalesce.left))) {
                stats->constants_folded++;
                return expr->as.null_coalesce.left;
            }
            /* If left is null literal, return right */
            if (expr->as.null_coalesce.left->type == EXPR_NULL) {
                stats->constants_folded++;
                return expr->as.null_coalesce.right;
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
            optimize_stmt_internal(stmt->as.if_stmt.then_branch, stats);
            if (stmt->as.if_stmt.else_branch) {
                optimize_stmt_internal(stmt->as.if_stmt.else_branch, stats);
            }
            break;

        case STMT_WHILE:
            stmt->as.while_stmt.condition = optimize_expr_internal(stmt->as.while_stmt.condition, stats);
            optimize_stmt_internal(stmt->as.while_stmt.body, stats);
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
    OptimizationStats stats = {0, 0, 0};

    for (int i = 0; i < count; i++) {
        optimize_stmt_internal(statements[i], &stats);
    }

    return stats;
}
