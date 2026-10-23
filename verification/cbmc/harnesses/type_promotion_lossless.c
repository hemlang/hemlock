/*
 * PROPERTY: implicit promotion never loses information. CLAUDE.md lists
 * "Lose precision in implicit conversions" under What NOT to Do.
 *
 * For every pair of numeric kinds and every value v of kind a, converting v
 * to promote(a, b) and back yields v. Expected to FAIL: the per-pair
 * properties enumerate exactly which edges of the lattice are lossy.
 */
#include "nondet.h"
#include "type_promotion.h"

typedef struct { HmlTypeKind k; int64_t s; uint64_t u; double f; } Num;

static Num nondet_num(HmlTypeKind k) {
    Num n = { k, 0, 0, 0.0 };
    switch (k) {
        case HML_TK_I8:  n.s = nondet_i8();  break;
        case HML_TK_I16: n.s = nondet_i16(); break;
        case HML_TK_I32: n.s = nondet_i32(); break;
        case HML_TK_I64: n.s = nondet_i64(); break;
        case HML_TK_U8:  n.u = nondet_u8();  break;
        case HML_TK_U16: n.u = nondet_u16(); break;
        case HML_TK_U32: n.u = nondet_u32(); break;
        case HML_TK_U64: n.u = nondet_u64(); break;
        case HML_TK_F32: { float x = nondet_float(); __CPROVER_assume(x == x); n.f = x; break; }
        default:         { double x = nondet_double(); __CPROVER_assume(x == x); n.f = x; break; }
    }
    return n;
}

/* Convert n to kind t (C conversion semantics, as the backends do), then
 * report whether converting back recovers the original value. */
static int roundtrips(Num n, HmlTypeKind t) {
    int is_s = n.k >= HML_TK_I8 && n.k <= HML_TK_I64;
    int is_u = n.k >= HML_TK_U8 && n.k <= HML_TK_U64;
    switch (t) {
        case HML_TK_F64: {
            double d = is_s ? (double)n.s : is_u ? (double)n.u : n.f;
            return is_s ? (d >= -0x1p63 && d < 0x1p63 && (int64_t)d == n.s)
                 : is_u ? (d < 0x1p64 && (uint64_t)d == n.u) : d == n.f;
        }
        case HML_TK_F32: {
            float f = is_s ? (float)n.s : is_u ? (float)n.u : (float)n.f;
            return is_s ? (f >= -0x1p63f && f < 0x1p63f && (int64_t)f == n.s)
                 : is_u ? (f < 0x1p64f && (uint64_t)f == n.u) : (double)f == n.f;
        }
        case HML_TK_I8:  return is_s ? (int8_t)n.s == n.s  : is_u ? n.u <= INT8_MAX  : 0;
        case HML_TK_I16: return is_s ? (int16_t)n.s == n.s : is_u ? n.u <= INT16_MAX : 0;
        case HML_TK_I32: return is_s ? (int32_t)n.s == n.s : is_u ? n.u <= INT32_MAX : 0;
        case HML_TK_I64: return is_s ? 1                   : is_u ? n.u <= INT64_MAX : 0;
        case HML_TK_U8:  return is_s ? (n.s >= 0 && n.s <= UINT8_MAX)  : is_u ? n.u <= UINT8_MAX  : 0;
        case HML_TK_U16: return is_s ? (n.s >= 0 && n.s <= UINT16_MAX) : is_u ? n.u <= UINT16_MAX : 0;
        case HML_TK_U32: return is_s ? (n.s >= 0 && n.s <= UINT32_MAX) : is_u ? n.u <= UINT32_MAX : 0;
        case HML_TK_U64: return is_s ? n.s >= 0 : is_u ? 1 : 0;
        default: return 0;
    }
}

/* One property per ordered (a, b) pair, so the report lists every lossy
 * edge instead of stopping at the first. The property is "every value of
 * the LEFT operand's kind survives promotion"; (b, a) covers the right. */
#define PAIR(A, B) \
    if (a == HML_TK_##A && b == HML_TK_##B) \
        __CPROVER_assert(roundtrips(v, t), #A " survives promotion with " #B);

void harness(void) {
    int ka = nondet_int(), kb = nondet_int();
    __CPROVER_assume(ka >= HML_TK_I8 && ka <= HML_TK_F64);
#ifdef KA
    __CPROVER_assume(ka == KA);   /* split the 100 pairs into 10 runs */
#endif
    __CPROVER_assume(kb >= HML_TK_I8 && kb <= HML_TK_F64);
    HmlTypeKind a = (HmlTypeKind)ka, b = (HmlTypeKind)kb;
    HmlTypeKind t = hml_tk_promote(a, b);
    Num v = nondet_num(a);
#define ROW(A) PAIR(A, I8) PAIR(A, I16) PAIR(A, I32) PAIR(A, I64) PAIR(A, U8) \
               PAIR(A, U16) PAIR(A, U32) PAIR(A, U64) PAIR(A, F32) PAIR(A, F64)
    ROW(I8) ROW(I16) ROW(I32) ROW(I64) ROW(U8)
    ROW(U16) ROW(U32) ROW(U64) ROW(F32) ROW(F64)
}
