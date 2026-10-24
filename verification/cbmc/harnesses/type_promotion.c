/*
 * PROOF: algebraic properties of hml_tk_promote over the numeric lattice
 * documented in CLAUDE.md / docs/language-guide/types.md:
 *
 *   i8 -> u8 -> i16 -> u16 -> i32 -> u32 -> i64 -> u64 -> f32 -> f64
 *   (rune behaves as i32; i64/u64 with f32 goes to f64)
 *
 * For all numeric kinds a, b, c:
 *   - closure:      promote(a,b) is a numeric kind (rune counts here)
 *   - selection:    promote(a,b) in {a, b, i32, f64}
 *   - idempotence:  promote(a,a) == a
 *   - commutativity / associativity  (both backends fold n-ary
 *                    expressions left-to-right, so associativity is what
 *                    makes `a + b + c` typed the same as `a + (b + c)`)
 *   - monotonicity: rank(promote(a,b)) >= max(rank a, rank b)
 *   - spec:         matches an independent table of the documented lattice
 */
#include "nondet.h"
#include "type_promotion.h"

static HmlTypeKind nondet_numeric_kind(void) {
    int k = nondet_int();
    __CPROVER_assume((k >= HML_TK_I8 && k <= HML_TK_F64) || k == HML_TK_RUNE);
    return (HmlTypeKind)k;
}

/* Independent encoding of the documented lattice (rune ranks as i32). */
static int spec_rank(HmlTypeKind t) {
    switch (t) {
        case HML_TK_I8: return 0;  case HML_TK_U8: return 1;
        case HML_TK_I16: return 2; case HML_TK_U16: return 3;
        case HML_TK_I32: case HML_TK_RUNE: return 4;
        case HML_TK_U32: return 5; case HML_TK_I64: return 6;
        case HML_TK_U64: return 7; case HML_TK_F32: return 8;
        case HML_TK_F64: return 9; default: return -1;
    }
}

static HmlTypeKind spec_promote(HmlTypeKind a, HmlTypeKind b) {
    if (a == HML_TK_RUNE) a = HML_TK_I32;
    if (b == HML_TK_RUNE) b = HML_TK_I32;
    if ((a == HML_TK_F32 && (b == HML_TK_I64 || b == HML_TK_U64)) ||
        (b == HML_TK_F32 && (a == HML_TK_I64 || a == HML_TK_U64)))
        return HML_TK_F64;
    return spec_rank(a) >= spec_rank(b) ? a : b;
}

void harness(void) {
    HmlTypeKind a = nondet_numeric_kind();
    HmlTypeKind b = nondet_numeric_kind();
    HmlTypeKind c = nondet_numeric_kind();
    HmlTypeKind ab = hml_tk_promote(a, b);

    assert(hml_tk_priority(ab) >= 0);
    assert(ab == a || ab == b || ab == HML_TK_I32 || ab == HML_TK_F64);
    assert(hml_tk_promote(a, a) == a);
    assert(ab == hml_tk_promote(b, a));
    assert(hml_tk_promote(ab, c) == hml_tk_promote(a, hml_tk_promote(b, c)));
    assert(hml_tk_priority(ab) >= hml_tk_priority(a));
    assert(hml_tk_priority(ab) >= hml_tk_priority(b));

    /* rune op rune is not covered by the documented lattice (the backends
     * don't do arithmetic on two runes); everything else must match. */
    if (!(a == HML_TK_RUNE && b == HML_TK_RUNE))
        assert(ab == spec_promote(a, b));
}
