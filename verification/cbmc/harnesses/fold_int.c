/*
 * PROOF: constant folding of integer literals (hml_fold_binary in
 * include/frontend/const_fold.h, which the AST optimizer runs before either
 * backend sees the program) agrees with int_spec.h for every pair of integer
 * literals and every operator, without UB:
 *
 *   - a fold never removes a runtime error (if the spec raises, the fold
 *     declines)
 *   - a folded literal has the spec's value AND, re-read as a literal, the
 *     spec's result type (i32 literals re-infer as i32, i64 as i64, u64 as
 *     u64); `/` folds to an f64 computed from the spec's converted
 *     operands, comparisons to the spec's bool
 *
 * Declining is always sound, so it is not checked. Together with rt_* and
 * in_* (both backends implement the same spec) this proves that folding is
 * invisible: a program prints the same whether or not the optimizer runs.
 *
 * One proof per operator (-DOP=SPEC_xxx) and pair of literal classes
 * (-DLK=/-DRK=, see fold_lit.h).
 */
#include "fold_lit.h"

static const BinaryOp op_map[SPEC_OP_COUNT] = {
    OP_ADD, OP_SUB, OP_MUL, OP_MOD,
    OP_BIT_AND, OP_BIT_OR, OP_BIT_XOR, OP_BIT_LSHIFT, OP_BIT_RSHIFT,
    OP_EQUAL, OP_NOT_EQUAL, OP_LESS, OP_LESS_EQUAL, OP_GREATER, OP_GREATER_EQUAL,
    OP_DIV,
};

void harness(void) {
    HmlNumLit l = nondet_int_lit(LK);
    HmlNumLit r = nondet_int_lit(RK);
    SpecOp op = spec_pick_op();

    HmlFoldResult out;
    if (!hml_fold_binary(op_map[op], l, r, &out)) return;   /* declining is sound */

    SpecResult want = spec_eval(op, lit_value(l), lit_value(r));
    __CPROVER_assert(!want.raises, "folds only what does not raise");

    if (want.is_bool) {
        __CPROVER_assert(out.is_bool, "comparison folds to a bool");
        __CPROVER_assert(out.flag == want.flag, "folded comparison matches spec");
    } else if (want.is_float) {
        /* `/`: the fold divides hml_fold_div_operand(l) by
         * hml_fold_div_operand(r); checked here is that those are the
         * spec's converted operands. Comparing the two IEEE quotients
         * themselves does not finish (README "What is not covered"). */
        SpecInt sl = lit_value(l), sr = lit_value(r);
        HmlTypeKind R = spec_promote(sl.type, sr.type);
        __CPROVER_assert(!out.is_bool && out.num.kind == HML_LIT_F64, "`/` folds to an f64");
        __CPROVER_assert(hml_fold_div_operand(l, l, r) == spec_cast_to_double(R, sl.value) &&
                         hml_fold_div_operand(r, l, r) == spec_cast_to_double(R, sr.value),
                         "`/` divides the operands converted to the promoted type");
        __CPROVER_assert(spec_cast_to_double(R, sl.value) == spec_to_double(R, spec_wrap(sl.value, R)) &&
                         spec_cast_to_double(R, sr.value) == spec_to_double(R, spec_wrap(sr.value, R)),
                         "spec: cast conversion equals spec_wrap");
    } else {
        __CPROVER_assert(!out.is_bool && out.num.kind != HML_LIT_F64, "integer op folds to an integer");
        SpecInt got = lit_value(out.num);
        __CPROVER_assert(got.type == want.result.type, "folded literal has the promoted type");
        __CPROVER_assert(got.value == want.result.value, "folded value matches spec");
    }
}
