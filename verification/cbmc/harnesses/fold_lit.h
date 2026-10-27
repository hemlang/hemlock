/*
 * Integer literals for the fold proofs (fold_int.c, fold_unary.c).
 *
 * A literal class is 0 (an int in i32 range: typed i32), 1 (any other int:
 * i64) or 2 (u64). Proofs fix the classes with -DLK= / -DRK= (proofs.txt
 * @lits / @litpairs), so the literal kinds are constants and no f64 path of
 * const_fold.h is reachable: CBMC 6.4.1 cannot encode the f64 member for
 * Bitwuzla once it is live. Every int64/uint64 value of the class can occur,
 * since folds produce literals the lexer cannot.
 */
#ifndef HML_CBMC_FOLD_LIT_H
#define HML_CBMC_FOLD_LIT_H

#include "int_spec.h"
#include "frontend/const_fold.h"

#ifndef LK
#define LK 0
#endif
#ifndef RK
#define RK 0
#endif

static HmlNumLit nondet_int_lit(int cls) {
    if (cls == 2) return hml_lit_u64(nondet_u64());
    int64_t v = nondet_i64();
    __CPROVER_assume(hml_lit_fits_i32(v) == (cls == 0));
    return hml_lit_int(v);
}

/* The runtime value of an integer literal: its type and value. */
static SpecInt lit_value(HmlNumLit l) {
    if (l.kind == HML_LIT_U64) return (SpecInt){ HML_TK_U64, l.v.u };
    return (SpecInt){ hml_lit_fits_i32(l.v.i) ? HML_TK_I32 : HML_TK_I64, l.v.i };
}

#endif
