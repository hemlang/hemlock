/*
 * Executable specification of Hemlock integer binary operators, written
 * independently of both backends (see docs/language-guide/types.md):
 *
 *   - result type R = documented lattice (i8<u8<i16<u16<i32<u32<i64<u64)
 *   - both operands are converted to R (C conversion: truncate, reinterpret)
 *   - + - *: computed exactly (__int128); i32/i64 raise iff the exact
 *     result is out of range, every other type wraps mod 2^bits
 *   - %: raises iff the converted divisor is 0; truncated remainder;
 *     MIN % -1 == 0
 *   - & | ^: bitwise on the converted values
 *   - << >>: raise iff the shift amount is negative; amount >= bits gives 0
 *     (or -1 for >> of a negative signed value); >> is arithmetic for
 *     signed R and logical for unsigned R, on the converted left operand
 *   - comparisons: on the converted values, signed or unsigned per R
 *
 * Types are HmlTypeKind (src/shared/type_promotion.h), which both backends
 * already map to; each harness adapts its backend's value type to SpecInt.
 */
#ifndef HML_CBMC_INT_SPEC_H
#define HML_CBMC_INT_SPEC_H

#include "nondet.h"
#include "type_promotion.h"

typedef __int128 i128;

typedef enum {
    SPEC_ADD, SPEC_SUB, SPEC_MUL, SPEC_MOD,
    SPEC_AND, SPEC_OR, SPEC_XOR, SPEC_SHL, SPEC_SHR,
    SPEC_EQ, SPEC_NE, SPEC_LT, SPEC_LE, SPEC_GT, SPEC_GE,
    SPEC_OP_COUNT
} SpecOp;

typedef struct { HmlTypeKind type; i128 value; } SpecInt;   /* mathematical value */

typedef struct {
    int raises;        /* operation must raise a catchable error */
    int is_bool;       /* comparison: result is `flag` */
    int flag;
    SpecInt result;    /* arithmetic: result type and value */
} SpecResult;

static int spec_is_int(HmlTypeKind t) { return t >= HML_TK_I8 && t <= HML_TK_U64; }

static int spec_rank(HmlTypeKind t) {
    switch (t) {
        case HML_TK_I8: return 0;  case HML_TK_U8: return 1;
        case HML_TK_I16: return 2; case HML_TK_U16: return 3;
        case HML_TK_I32: return 4; case HML_TK_U32: return 5;
        case HML_TK_I64: return 6; default: return 7;
    }
}
static int spec_bits(HmlTypeKind t) {
    switch (t) {
        case HML_TK_I8: case HML_TK_U8: return 8;
        case HML_TK_I16: case HML_TK_U16: return 16;
        case HML_TK_I32: case HML_TK_U32: return 32;
        default: return 64;
    }
}
static int spec_signed(HmlTypeKind t) { return t >= HML_TK_I8 && t <= HML_TK_I64; }

/* Reduce x into the value range of type t (wrap mod 2^bits). */
static i128 spec_wrap(i128 x, HmlTypeKind t) {
    int bits = spec_bits(t);
    unsigned __int128 m = (unsigned __int128)x & ((((unsigned __int128)1) << bits) - 1);
    if (spec_signed(t) && ((m >> (bits - 1)) & 1)) return (i128)m - ((i128)1 << bits);
    return (i128)m;
}

static SpecInt spec_nondet_int(HmlTypeKind t) {
    SpecInt v = { t, 0 };
    switch (t) {
        case HML_TK_I8:  v.value = nondet_i8(); break;
        case HML_TK_I16: v.value = nondet_i16(); break;
        case HML_TK_I32: v.value = nondet_i32(); break;
        case HML_TK_I64: v.value = nondet_i64(); break;
        case HML_TK_U8:  v.value = nondet_u8(); break;
        case HML_TK_U16: v.value = nondet_u16(); break;
        case HML_TK_U32: v.value = nondet_u32(); break;
        default:         v.value = nondet_u64(); break;
    }
    return v;
}

static HmlTypeKind spec_pick_type(int fixed) {
    if (fixed >= 0) return (HmlTypeKind)fixed;
    int t = nondet_int();
    __CPROVER_assume(spec_is_int((HmlTypeKind)t));
    return (HmlTypeKind)t;
}

static SpecResult spec_eval(SpecOp op, SpecInt l, SpecInt r) {
    SpecResult out = { 0, 0, 0, { HML_TK_NULL, 0 } };
    HmlTypeKind R = spec_rank(l.type) >= spec_rank(r.type) ? l.type : r.type;
    int bits = spec_bits(R), sgn = spec_signed(R);
    i128 a = spec_wrap(l.value, R), b = spec_wrap(r.value, R);
    i128 shift = r.value, x = 0;

    switch (op) {
        case SPEC_ADD: x = a + b; break;
        case SPEC_SUB: x = a - b; break;
        case SPEC_MUL:
            /* u64 * u64 can reach 2^128: multiply unsigned (wraps mod 2^128,
             * which preserves the low 64 bits); signed products fit in i128. */
            x = sgn ? a * b : (i128)(spec_wrap((i128)((unsigned __int128)a * (unsigned __int128)b), R));
            break;
        case SPEC_MOD:
            /* 64-bit division (a, b fit in R) keeps the SAT problem small;
             * the -1 case is spelled out because INT64_MIN % -1 traps. */
            if (b == 0) out.raises = 1;
            else if (!sgn) x = (i128)((uint64_t)a % (uint64_t)b);
            else if (b == -1) x = 0;
            else x = (i128)((int64_t)a % (int64_t)b);
            break;
        case SPEC_AND: x = a & b; break;
        case SPEC_OR:  x = a | b; break;
        case SPEC_XOR: x = a ^ b; break;
        case SPEC_SHL:
            if (shift < 0) out.raises = 1;
            else if (shift >= bits) x = 0;
            else x = (i128)((unsigned __int128)spec_wrap(a, HML_TK_U64) << (int)shift);
            break;
        case SPEC_SHR:
            if (shift < 0) out.raises = 1;
            else if (shift >= bits) x = (sgn && a < 0) ? -1 : 0;
            else x = a >> (int)shift;  /* a is in R's range: non-negative for
                                          unsigned R (logical), signed
                                          otherwise (arithmetic) */
            break;
        default:
            out.is_bool = 1;
            out.flag = op == SPEC_EQ ? a == b : op == SPEC_NE ? a != b :
                       op == SPEC_LT ? a < b  : op == SPEC_LE ? a <= b :
                       op == SPEC_GT ? a > b  : a >= b;
            return out;
    }
    if ((op == SPEC_ADD || op == SPEC_SUB || op == SPEC_MUL) &&
        (R == HML_TK_I32 || R == HML_TK_I64) && spec_wrap(x, R) != x)
        out.raises = 1;
    out.result.type = R;
    out.result.value = spec_wrap(x, R);
    return out;
}

#ifndef LT
#define LT -1
#endif
#ifndef RT
#define RT -1
#endif

static SpecOp spec_pick_op(void) {
#ifdef OP
    return OP;
#else
    int op = nondet_int();
    __CPROVER_assume(op >= 0 && op < SPEC_OP_COUNT);
    return (SpecOp)op;
#endif
}

#endif
