/*
 * Executable specification of Hemlock integer binary operators, written
 * independently of both backends (see docs/language-guide/types.md):
 *
 *   - result type R = documented lattice (i8<u8<i16<u16<i32<u32<i64<u64)
 *   - both operands are converted to R (C conversion: truncate, reinterpret)
 *   - + -: computed exactly (__int128); i32/i64 raise iff the exact
 *     result is out of range, every other type wraps mod 2^bits
 *   - *: same rule, computed exactly in the backend's arithmetic shape
 *     (see spec_mul)
 *   - %: raises iff the converted divisor is 0; truncated remainder;
 *     MIN % -1 == 0
 *   - & | ^: bitwise on the converted values
 *   - << >>: both operands are converted to R, including the shift amount
 *     (read as int64, so a u64 amount >= 2^63 is negative); raise iff that
 *     amount is negative; amount >= bits gives 0 (or -1 for >> of a negative
 *     signed value); >> is arithmetic for signed R and logical for unsigned
 *     R. This is the interpreter's behavior, which is the reference.
 *   - /: raises iff the converted divisor is 0; otherwise f64, the
 *     converted operands' quotient (both convert to double exactly or by
 *     one correct rounding, as each backend does)
 *   - comparisons: on the converted values, signed or unsigned per R
 *   - unary (spec_negate, spec_bit_not): see below
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
    SPEC_DIV,
    SPEC_OP_COUNT
} SpecOp;

typedef struct { HmlTypeKind type; i128 value; } SpecInt;   /* mathematical value */

typedef struct {
    int raises;        /* operation must raise a catchable error */
    int is_bool;       /* comparison: result is `flag` */
    int flag;
    int is_float;      /* `/`: result is the f64 spec_quotient() */
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

/* Exact product / remainder of values already in R's range, computed in the
 * backend's arithmetic shape (see SPEC_MUL in spec_eval). */
static i128 spec_mul(HmlTypeKind R, i128 l, i128 r, int *raises) {
    switch (R) {
        case HML_TK_I32: { int32_t p; *raises = __builtin_mul_overflow((int32_t)l, (int32_t)r, &p); return p; }
        case HML_TK_I64: { int64_t p; *raises = __builtin_mul_overflow((int64_t)l, (int64_t)r, &p); return p; }
        case HML_TK_U64: return (i128)((uint64_t)l * (uint64_t)r);
        default: break;
    }
#ifdef SPEC_SHAPE_RUNTIME
    /* runtime: zero-extend the R-width bit pattern, multiply in uint64 */
    switch (R) {
        case HML_TK_I8:  case HML_TK_U8:  return (i128)((uint64_t)(uint8_t)l * (uint64_t)(uint8_t)r);
        case HML_TK_I16: case HML_TK_U16: return (i128)((uint64_t)(uint16_t)l * (uint64_t)(uint16_t)r);
        default:                          return (i128)((uint64_t)(uint32_t)l * (uint64_t)(uint32_t)r);
    }
#else
    /* interpreter: C promotions of R-typed operands (u16/u32 multiply unsigned) */
    switch (R) {
        case HML_TK_I8:  return (int8_t)l * (int8_t)r;
        case HML_TK_I16: return (int16_t)l * (int16_t)r;
        case HML_TK_U8:  return (uint8_t)l * (uint8_t)r;
        case HML_TK_U16: return (i128)((uint32_t)(uint16_t)l * (uint16_t)r);
        default:         return (i128)((uint32_t)l * (uint32_t)r);
    }
#endif
}

/* b != 0, and b != -1 for signed R (handled by the caller). `native_i32` is
 * the compiled runtime's i32 x i32 fast path, which works in int32. */
static i128 spec_mod(HmlTypeKind R, int native_i32, i128 a, i128 b) {
#ifdef SPEC_SHAPE_RUNTIME
    if (!native_i32 || R != HML_TK_I32) {
        /* runtime: extend the R-width value to 64 bits, divide there */
        switch (R) {
            case HML_TK_I8:  return (int64_t)(int8_t)a % (int64_t)(int8_t)b;
            case HML_TK_I16: return (int64_t)(int16_t)a % (int64_t)(int16_t)b;
            case HML_TK_I32: return (int64_t)(int32_t)a % (int64_t)(int32_t)b;
            case HML_TK_I64: return (int64_t)a % (int64_t)b;
            case HML_TK_U8:  return (i128)((uint64_t)(uint8_t)a % (uint64_t)(uint8_t)b);
            case HML_TK_U16: return (i128)((uint64_t)(uint16_t)a % (uint64_t)(uint16_t)b);
            case HML_TK_U32: return (i128)((uint64_t)(uint32_t)a % (uint64_t)(uint32_t)b);
            default:         return (i128)((uint64_t)a % (uint64_t)b);
        }
    }
#else
    (void)native_i32;
#endif
    switch (R) {
        case HML_TK_I8:  return (int8_t)a % (int8_t)b;
        case HML_TK_I16: return (int16_t)a % (int16_t)b;
        case HML_TK_I32: return (int32_t)a % (int32_t)b;
        case HML_TK_I64: return (int64_t)a % (int64_t)b;
        case HML_TK_U8:  return (uint8_t)a % (uint8_t)b;
        case HML_TK_U16: return (uint16_t)a % (uint16_t)b;
        case HML_TK_U32: return (uint32_t)a % (uint32_t)b;
        default:         return (i128)((uint64_t)a % (uint64_t)b);
    }
}

/* A value already in R's range, as a double (exact, or one correct
 * rounding for 64-bit magnitudes above 2^53). */
static double spec_to_double(HmlTypeKind R, i128 v) {
    return R == HML_TK_U64 ? (double)(uint64_t)v : (double)(int64_t)v;
}

static HmlTypeKind spec_promote(HmlTypeKind a, HmlTypeKind b) {
    return spec_rank(a) >= spec_rank(b) ? a : b;
}

/* An operand converted to R by C cast, as a double. Equal to
 * spec_to_double(R, spec_wrap(v, R)) (asserted by spec_quotient); stated as
 * a cast so the solver sees the backends' conversion shape. */
static double spec_cast_to_double(HmlTypeKind R, i128 v) {
    switch (R) {
        case HML_TK_I8:  return (double)(int8_t)v;
        case HML_TK_I16: return (double)(int16_t)v;
        case HML_TK_I32: return (double)(int32_t)v;
        case HML_TK_I64: return (double)(int64_t)v;
        case HML_TK_U8:  return (double)(uint8_t)v;
        case HML_TK_U16: return (double)(uint16_t)v;
        case HML_TK_U32: return (double)(uint32_t)v;
        default:         return (double)(uint64_t)v;
    }
}

/* `l / r` for a nonzero divisor: the quotient of the operands converted to
 * the promoted type. Kept out of SpecResult: CBMC 6.4.1 cannot encode a
 * struct with a double member for Bitwuzla (README "Method notes"). */
static double spec_quotient(SpecInt l, SpecInt r) {
    HmlTypeKind R = spec_promote(l.type, r.type);
    double a = spec_cast_to_double(R, l.value), b = spec_cast_to_double(R, r.value);
    __CPROVER_assert(a == spec_to_double(R, spec_wrap(l.value, R)) &&
                     b == spec_to_double(R, spec_wrap(r.value, R)),
                     "spec: cast conversion equals spec_wrap");
    return a / b;
}

static SpecResult spec_eval(SpecOp op, SpecInt l, SpecInt r) {
    SpecResult out = { 0, 0, 0, 0, { HML_TK_NULL, 0 } };
    HmlTypeKind R = spec_promote(l.type, r.type);
    int bits = spec_bits(R), sgn = spec_signed(R);
    i128 a = spec_wrap(l.value, R), b = spec_wrap(r.value, R);
    i128 x = 0;
    int mul_raises = 0;
    i128 shift = (R == HML_TK_U64 && b > INT64_MAX) ? b - ((i128)1 << 64) : b;

    switch (op) {
        case SPEC_ADD: x = a + b; break;
        case SPEC_SUB: x = a - b; break;
        case SPEC_MUL:
        case SPEC_MOD:
            /* Stated in the arithmetic shape the backend under proof uses
             * (SPEC_SHAPE_RUNTIME: 64-bit, then truncate; otherwise C's
             * native promotions at R's width, as the interpreter does). Both
             * are exact: promoted operands always fit, so either computes the
             * true product/remainder before wrapping. The shape matters only
             * to the solver: relating a 64-bit multiplier or divider to a
             * 32-bit one does not finish, even as an isolated lemma.
             * Operands are converted by C cast of the mathematical value, the
             * same truncation spec_wrap does (asserted). Checked i32/i64
             * products use __builtin_mul_overflow, which the proofs replace
             * with an exact __int128 product and range check
             * (stubs/overflow_builtins.h), so it is not trusted. */
            __CPROVER_assert(spec_wrap(l.value, R) == a && spec_wrap(r.value, R) == b,
                             "spec: cast conversion equals spec_wrap");
            if (op == SPEC_MUL) {
                x = spec_mul(R, l.value, r.value, &mul_raises);
                break;
            }
            if (b == 0) { out.raises = 1; break; }
            if (sgn && b == -1) { x = 0; break; }   /* MIN % -1 traps in C */
            x = spec_mod(R, l.type == HML_TK_I32 && r.type == HML_TK_I32, l.value, r.value);
            break;
        case SPEC_AND: x = a & b; break;
        case SPEC_OR:  x = a | b; break;
        case SPEC_XOR: x = a ^ b; break;
        case SPEC_SHL:
            if (shift < 0) out.raises = 1;
            else if (shift >= bits) x = 0;
            else x = (i128)((unsigned __int128)spec_wrap(a, HML_TK_U64) << (int)shift);
            break;
        case SPEC_DIV:
            out.is_float = 1;
            out.raises = b == 0;
            return out;
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
    if ((op == SPEC_ADD || op == SPEC_SUB) &&
        (R == HML_TK_I32 || R == HML_TK_I64) && spec_wrap(x, R) != x)
        out.raises = 1;
    if (op == SPEC_MUL) out.raises = mul_raises;
    out.result.type = R;
    out.result.value = spec_wrap(x, R);
    return out;
}

/*
 * Unary minus: i8/i16 wrap in-type; i32/i64 MIN raise; u8 -> i16,
 * u16 -> i32, u32 -> i64 (exact); u64 -> i64, exact when x <= INT64_MAX,
 * else raise.
 */
static SpecInt spec_negate(SpecInt x, int *raises) {
    *raises = 0;
    switch (x.type) {
        case HML_TK_I8: case HML_TK_I16:
            return (SpecInt){ x.type, spec_wrap(-x.value, x.type) };
        case HML_TK_I32: case HML_TK_I64:
            *raises = spec_wrap(-x.value, x.type) != -x.value;
            return (SpecInt){ x.type, -x.value };
        case HML_TK_U8:  return (SpecInt){ HML_TK_I16, -x.value };
        case HML_TK_U16: return (SpecInt){ HML_TK_I32, -x.value };
        case HML_TK_U32: return (SpecInt){ HML_TK_I64, -x.value };
        default:
            *raises = x.value > INT64_MAX;
            return (SpecInt){ HML_TK_I64, -x.value };
    }
}

/* ~x: same type, bitwise complement. */
static SpecInt spec_bit_not(SpecInt x) {
    return (SpecInt){ x.type, spec_wrap(~x.value, x.type) };
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
