/*
 * Shared part of fold_runtime.c / fold_interp.c: folds of literals that
 * int_spec.h does not cover, checked against a backend directly.
 *
 *   default:      `l op r` with at least one f64 literal, any operator. A
 *                 fold must not hide a runtime error and must produce the
 *                 backend's bool, or its f64 bit for bit (any NaN for NaN:
 *                 both compute the same C expression on the same doubles).
 *   -DCONV:       every literal, as an operand of a float operation, is
 *                 the f64 the backend computes with. For `* / %` this is
 *                 what is proved: comparing two IEEE products, quotients or
 *                 remainders does not finish on CaDiCaL, so that the fold
 *                 and the backend then apply the same C operator is by
 *                 inspection (README "What is not covered"). `+ -` and all
 *                 comparisons are checked end to end (default mode).
 *   -DTRUTHY:     constant conditions (if/while elimination, !, &&, ||,
 *                 ternary) use hml_lit_truthy, which must equal the
 *                 backend's truthiness of the literal's value, for every
 *                 literal including NaN and -0.0.
 *
 * The including harness (fold_runtime.c, fold_interp.c) converts literals
 * to its backend's values and calls the backend's operator.
 */
#ifndef HML_CBMC_FOLD_BACKEND_H
#define HML_CBMC_FOLD_BACKEND_H

#include "nondet.h"
#include <math.h>
#include "frontend/const_fold.h"

static const BinaryOp fold_ops[] = {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_EQUAL, OP_NOT_EQUAL, OP_LESS, OP_LESS_EQUAL, OP_GREATER, OP_GREATER_EQUAL,
    OP_BIT_AND, OP_BIT_OR, OP_BIT_XOR, OP_BIT_LSHIFT, OP_BIT_RSHIFT,
};
#define FOLD_OP_COUNT ((int)(sizeof(fold_ops) / sizeof(fold_ops[0])))

/* The operator: fold_ops[FOLD_OP] when given (one proof per arithmetic
 * operator: each compares two IEEE circuits, which CaDiCaL proves equal
 * only one at a time), otherwise any from fold_ops[FOLD_OP_FROM] on. */
#ifndef FOLD_OP_FROM
#define FOLD_OP_FROM 0
#endif
static BinaryOp fold_pick_op(void) {
#ifdef FOLD_OP
    return fold_ops[FOLD_OP];
#else
    int i = nondet_int();
    __CPROVER_assume(i >= FOLD_OP_FROM && i < FOLD_OP_COUNT);
    return fold_ops[i];
#endif
}

static HmlNumLit nondet_lit(void) {
    switch (nondet_int()) {
        case 0:  return hml_lit_int(nondet_i64());
        case 1:  return hml_lit_u64(nondet_u64());
        default: return hml_lit_f64(nondet_double());
    }
}

static int same_double(double a, double b) {
    if (isnan(a) || isnan(b)) return isnan(a) && isnan(b);
    return a == b && signbit(a) == signbit(b);
}

#endif
