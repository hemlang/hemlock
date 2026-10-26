/*
 * PROOF: constant folding of `-` and `~` on integer literals
 * (hml_fold_negate / hml_fold_bit_not in include/frontend/const_fold.h)
 * agrees with the unary spec (spec_negate / spec_bit_not in int_spec.h) for
 * every integer literal (one proof per literal class, -DLK=, see
 * fold_lit.h): a fold never removes a runtime error, and the
 * folded literal has the spec's value and, re-read as a literal, its type.
 *
 * Negative literals in source (-2147483648, -9223372036854775808) are built
 * by the parser, not folded, so nothing here exempts them: -(2147483648)
 * must stay an i64 and -(-2147483648) must throw, as at runtime.
 */
#include "fold_lit.h"

void harness(void) {
    HmlNumLit x = nondet_int_lit(LK);
    int negate = nondet_int();

    HmlNumLit out;
    if (!(negate ? hml_fold_negate(x, &out) : hml_fold_bit_not(x, &out))) return;

    int raises = 0;
    SpecInt want = negate ? spec_negate(lit_value(x), &raises) : spec_bit_not(lit_value(x));
    __CPROVER_assert(!raises, "folds only what does not raise");
    __CPROVER_assert(out.kind != HML_LIT_F64, "integer op folds to an integer");
    SpecInt got = lit_value(out);
    __CPROVER_assert(got.type == want.type, "folded literal has the result type");
    __CPROVER_assert(got.value == want.value, "folded value matches spec");
}
