/*
 * Constant folding on numeric literals, shared by the AST optimizer and its
 * proofs (verification/cbmc/harnesses/fold_*.c).
 *
 * Pure functions over literal values, no AST: the optimizer converts
 * EXPR_NUMBER nodes to HmlNumLit and back. Every fold is exactly
 * semantics-preserving: the folded literal carries the same value AND the
 * same runtime type the unfolded expression produces on both backends, and
 * anything whose evaluation raises (i32/i64 overflow, division/modulo by
 * zero, negating i32/i64 MIN) is left unfolded so the error still fires.
 * When in doubt, a fold declines (returns 0).
 *
 * Literal typing (mirrors EXPR_NUMBER evaluation in both backends): floats
 * are f64, u64 literals are u64, other integers are i32 when they fit and
 * i64 otherwise. A fold whose true result type is i64 but whose value fits
 * i32 cannot be written as a literal and declines.
 */
#ifndef HEMLOCK_CONST_FOLD_H
#define HEMLOCK_CONST_FOLD_H

#include <stdint.h>
#include <math.h>
#include "frontend/ast.h"

typedef enum { HML_LIT_INT, HML_LIT_U64, HML_LIT_F64 } HmlLitKind;

/*
 * A tagged union, which also keeps the f64 out of the proofs' way: CBMC
 * 6.4.1 cannot encode a struct with a double member for Bitwuzla, but a
 * union is one bit-vector, and a proof whose literals are all integers
 * never touches `f` (verification/README.md, "Method notes").
 */
typedef struct {
    HmlLitKind kind;
    union {
        int64_t i;      /* HML_LIT_INT */
        uint64_t u;     /* HML_LIT_U64 */
        double f;       /* HML_LIT_F64 */
    } v;
} HmlNumLit;

typedef struct {
    int is_bool;    /* comparison: result is `flag` */
    int flag;
    HmlNumLit num;  /* arithmetic: the folded literal */
} HmlFoldResult;

static inline HmlNumLit hml_lit_int(int64_t v) { HmlNumLit l; l.kind = HML_LIT_INT; l.v.i = v; return l; }
static inline HmlNumLit hml_lit_u64(uint64_t v) { HmlNumLit l; l.kind = HML_LIT_U64; l.v.u = v; return l; }
static inline HmlNumLit hml_lit_f64(double v) { HmlNumLit l; l.kind = HML_LIT_F64; l.v.f = v; return l; }

/* Whether an integer literal value is typed i32 (otherwise i64). */
static inline int hml_lit_fits_i32(int64_t v) {
    return v >= INT32_MIN && v <= INT32_MAX;
}

static inline double hml_lit_as_double(HmlNumLit l) {
    switch (l.kind) {
        case HML_LIT_F64: return l.v.f;
        case HML_LIT_U64: return (double)l.v.u;
        default:          return (double)l.v.i;
    }
}

/* Truthiness of the literal's runtime value: nonzero (NaN is truthy). */
static inline int hml_lit_truthy(HmlNumLit l) {
    switch (l.kind) {
        case HML_LIT_F64: return l.v.f != 0.0;
        case HML_LIT_U64: return l.v.u != 0;
        default:          return l.v.i != 0;
    }
}

static inline int hml_fold_bool(HmlFoldResult *out, int flag) {
    out->is_bool = 1;
    out->flag = flag;
    return 1;
}

static inline int hml_fold_num(HmlFoldResult *out, HmlNumLit v) {
    out->is_bool = 0;
    out->num = v;
    return 1;
}

/*
 * Operand `x` of an integer `l / r` (x is l or r), converted to the
 * promoted type, as the f64 the quotient is computed from: i32 when both
 * fit, else u64 if either is u64, else i64. Split out so the proofs can
 * check the conversion separately from the IEEE division (fold_div_conv).
 */
static inline double hml_fold_div_operand(HmlNumLit x, HmlNumLit l, HmlNumLit r) {
    if (l.kind == HML_LIT_U64 || r.kind == HML_LIT_U64)
        return (double)(x.kind == HML_LIT_U64 ? x.v.u : (uint64_t)x.v.i);
    if (hml_lit_fits_i32(l.v.i) && hml_lit_fits_i32(r.v.i)) return (double)(int32_t)x.v.i;
    return (double)x.v.i;
}

/*
 * Fold `l op r`. Returns 1 and sets *out if the result is a literal.
 *
 * Promotion of two literals: f64 if either is a float, else u64 if either
 * is u64, else i32 when both fit i32, else i64. i32/i64 add/sub/mul throw on
 * overflow; u64 wraps; signed % -1 is 0; `/` is always f64 of the operands
 * converted to the promoted type.
 */
static inline int hml_fold_binary(BinaryOp op, HmlNumLit l, HmlNumLit r, HmlFoldResult *out) {
    if (l.kind == HML_LIT_F64 || r.kind == HML_LIT_F64) {
        /* Promoted type is f64 (literal floats are always f64). Runtime
         * float arithmetic is IEEE 754: division/modulo by zero produce
         * inf/NaN without throwing, so folding is always safe here. */
        double a = hml_lit_as_double(l);
        double b = hml_lit_as_double(r);
        switch (op) {
            case OP_ADD: return hml_fold_num(out, hml_lit_f64(a + b));
            case OP_SUB: return hml_fold_num(out, hml_lit_f64(a - b));
            case OP_MUL: return hml_fold_num(out, hml_lit_f64(a * b));
            case OP_DIV: return hml_fold_num(out, hml_lit_f64(a / b));
            case OP_MOD: return hml_fold_num(out, hml_lit_f64(fmod(a, b)));
            case OP_EQUAL:         return hml_fold_bool(out, a == b);
            case OP_NOT_EQUAL:     return hml_fold_bool(out, a != b);
            case OP_LESS:          return hml_fold_bool(out, a < b);
            case OP_LESS_EQUAL:    return hml_fold_bool(out, a <= b);
            case OP_GREATER:       return hml_fold_bool(out, a > b);
            case OP_GREATER_EQUAL: return hml_fold_bool(out, a >= b);
            default: return 0;  /* bitwise/shift on floats: runtime error */
        }
    }

    if (l.kind == HML_LIT_U64 || r.kind == HML_LIT_U64) {
        /* Promoted type is u64: unsigned 64-bit with wrapping semantics.
         * The folded literal keeps the u64 kind so its runtime type stays
         * u64 regardless of magnitude. */
        uint64_t a = l.kind == HML_LIT_U64 ? l.v.u : (uint64_t)l.v.i;
        uint64_t b = r.kind == HML_LIT_U64 ? r.v.u : (uint64_t)r.v.i;
        switch (op) {
            case OP_ADD: return hml_fold_num(out, hml_lit_u64(a + b));
            case OP_SUB: return hml_fold_num(out, hml_lit_u64(a - b));
            case OP_MUL: return hml_fold_num(out, hml_lit_u64(a * b));
            case OP_DIV:
                if (b == 0) return 0;  /* runtime throws for int operands */
                return hml_fold_num(out, hml_lit_f64(hml_fold_div_operand(l, l, r) /
                                                     hml_fold_div_operand(r, l, r)));
            case OP_MOD:
                if (b == 0) return 0;
                return hml_fold_num(out, hml_lit_u64(a % b));
            case OP_EQUAL:         return hml_fold_bool(out, a == b);
            case OP_NOT_EQUAL:     return hml_fold_bool(out, a != b);
            case OP_LESS:          return hml_fold_bool(out, a < b);
            case OP_LESS_EQUAL:    return hml_fold_bool(out, a <= b);
            case OP_GREATER:       return hml_fold_bool(out, a > b);
            case OP_GREATER_EQUAL: return hml_fold_bool(out, a >= b);
            case OP_BIT_AND: return hml_fold_num(out, hml_lit_u64(a & b));
            case OP_BIT_OR:  return hml_fold_num(out, hml_lit_u64(a | b));
            case OP_BIT_XOR: return hml_fold_num(out, hml_lit_u64(a ^ b));
            default: return 0;  /* shifts: width semantics left to runtime */
        }
    }

    /* Plain int literals: promoted type is i32 when both operands fit i32,
     * i64 otherwise. */
    int64_t a = l.v.i;
    int64_t b = r.v.i;
    int p32 = hml_lit_fits_i32(a) && hml_lit_fits_i32(b);
    switch (op) {
        case OP_ADD:
        case OP_SUB:
        case OP_MUL: {
            if (p32) {
                /* In i32, as the backends' i32 x i32 paths compute it */
                int32_t res;
                int ovf = (op == OP_ADD) ? __builtin_add_overflow((int32_t)a, (int32_t)b, &res)
                        : (op == OP_SUB) ? __builtin_sub_overflow((int32_t)a, (int32_t)b, &res)
                                         : __builtin_mul_overflow((int32_t)a, (int32_t)b, &res);
                if (ovf) return 0;                            /* runtime throws i32 overflow */
                return hml_fold_num(out, hml_lit_int(res));
            }
            int64_t res;
            int ovf = (op == OP_ADD) ? __builtin_add_overflow(a, b, &res)
                    : (op == OP_SUB) ? __builtin_sub_overflow(a, b, &res)
                                     : __builtin_mul_overflow(a, b, &res);
            if (ovf) return 0;                                /* runtime throws i64 overflow */
            if (hml_lit_fits_i32(res)) return 0;              /* would demote i64 result to i32 */
            return hml_fold_num(out, hml_lit_int(res));
        }
        case OP_DIV:
            if (b == 0) return 0;  /* runtime throws Division by zero */
            return hml_fold_num(out, hml_lit_f64(hml_fold_div_operand(l, l, r) /
                                                 hml_fold_div_operand(r, l, r)));
        case OP_MOD: {
            if (b == 0) return 0;
            if (b == -1) {                    /* MIN % -1 is defined as 0 */
                if (!p32) return 0;           /* an i64 0 has no literal */
                return hml_fold_num(out, hml_lit_int(0));
            }
            if (p32) return hml_fold_num(out, hml_lit_int((int32_t)a % (int32_t)b));
            int64_t res = a % b;
            if (hml_lit_fits_i32(res)) return 0;              /* keep runtime i64 type */
            return hml_fold_num(out, hml_lit_int(res));
        }
        case OP_EQUAL:         return hml_fold_bool(out, a == b);
        case OP_NOT_EQUAL:     return hml_fold_bool(out, a != b);
        case OP_LESS:          return hml_fold_bool(out, a < b);
        case OP_LESS_EQUAL:    return hml_fold_bool(out, a <= b);
        case OP_GREATER:       return hml_fold_bool(out, a > b);
        case OP_GREATER_EQUAL: return hml_fold_bool(out, a >= b);
        case OP_BIT_AND:
        case OP_BIT_OR:
        case OP_BIT_XOR: {
            int64_t res = (op == OP_BIT_AND) ? (a & b)
                        : (op == OP_BIT_OR)  ? (a | b) : (a ^ b);
            if (!p32 && hml_lit_fits_i32(res)) return 0;      /* keep runtime i64 type */
            return hml_fold_num(out, hml_lit_int(res));
        }
        case OP_BIT_LSHIFT: {
            if (b < 0) return 0;  /* runtime error: negative shift */
            if (!p32) return 0;   /* i64 width semantics left to runtime */
            int32_t ai = (int32_t)a;
            int32_t res = (b >= 32) ? 0 : (int32_t)((uint32_t)ai << b);
            return hml_fold_num(out, hml_lit_int(res));
        }
        case OP_BIT_RSHIFT: {
            if (b < 0) return 0;
            if (!p32) return 0;
            int32_t ai = (int32_t)a;
            int32_t res = (b >= 32) ? (ai < 0 ? -1 : 0) : (ai >> (int32_t)b);
            return hml_fold_num(out, hml_lit_int(res));
        }
        default: return 0;  /* OP_AND/OP_OR handled separately */
    }
}

/*
 * Fold `-x`. Runtime negation keeps i32/i64 and throws on MIN, and turns u64
 * into i64 (throwing above INT64_MAX). Negative literals in source are not
 * folds: the parser builds them directly (-2147483648 is an i32 literal,
 * -9223372036854775808 an i64 one), so this only sees negation of a value,
 * e.g. -(2147483648) or -(-2147483648), which must behave as at runtime.
 */
static inline int hml_fold_negate(HmlNumLit x, HmlNumLit *out) {
    switch (x.kind) {
        case HML_LIT_F64:
            *out = hml_lit_f64(-x.v.f);
            return 1;
        case HML_LIT_U64: {
            if (x.v.u > (uint64_t)INT64_MAX) return 0;          /* runtime error */
            int64_t res = -(int64_t)x.v.u;
            if (hml_lit_fits_i32(res)) return 0;              /* result is i64 */
            *out = hml_lit_int(res);
            return 1;
        }
        default: {
            int is_i32 = hml_lit_fits_i32(x.v.i);
            if (x.v.i == (is_i32 ? INT32_MIN : INT64_MIN)) return 0;  /* overflow */
            int64_t res = -x.v.i;
            if (!is_i32 && hml_lit_fits_i32(res)) return 0;   /* -(i64 2^31) stays i64 */
            *out = hml_lit_int(res);
            return 1;
        }
    }
}

/* Fold `~x` for integer literals: same type, bitwise complement. ~ maps the
 * i32 range onto itself and the rest of i64 onto itself, so the literal
 * type is preserved. u64 and floats are left to the runtime. */
static inline int hml_fold_bit_not(HmlNumLit x, HmlNumLit *out) {
    if (x.kind != HML_LIT_INT) return 0;
    *out = hml_lit_int(~x.v.i);
    return 1;
}

#endif
