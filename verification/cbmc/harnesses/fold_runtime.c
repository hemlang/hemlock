/*
 * PROOF: folds the integer spec does not cover agree with the compiled
 * runtime (hml_binary_op, hml_to_bool). See fold_backend.h.
 */
#include "fold_backend.h"
#include "hemlock_runtime.h"

extern int hml_cbmc_error_allowed;

static HmlValue to_backend(HmlNumLit l) {
    switch (l.kind) {
        case HML_LIT_F64: return hml_val_f64(l.v.f);
        case HML_LIT_U64: return hml_val_u64(l.v.u);
        default: return hml_lit_fits_i32(l.v.i) ? hml_val_i32((int32_t)l.v.i) : hml_val_i64(l.v.i);
    }
}

static HmlBinaryOp backend_op(BinaryOp op) {
    switch (op) {
        case OP_ADD: return HML_OP_ADD;
        case OP_SUB: return HML_OP_SUB;
        case OP_MUL: return HML_OP_MUL;
        case OP_DIV: return HML_OP_DIV;
        case OP_MOD: return HML_OP_MOD;
        case OP_EQUAL: return HML_OP_EQUAL;
        case OP_NOT_EQUAL: return HML_OP_NOT_EQUAL;
        case OP_LESS: return HML_OP_LESS;
        case OP_LESS_EQUAL: return HML_OP_LESS_EQUAL;
        case OP_GREATER: return HML_OP_GREATER;
        case OP_GREATER_EQUAL: return HML_OP_GREATER_EQUAL;
        case OP_BIT_AND: return HML_OP_BIT_AND;
        case OP_BIT_OR: return HML_OP_BIT_OR;
        case OP_BIT_XOR: return HML_OP_BIT_XOR;
        case OP_BIT_LSHIFT: return HML_OP_LSHIFT;
        default: return HML_OP_RSHIFT;
    }
}

void harness(void) {
#if defined(CONV)
    HmlNumLit x = nondet_lit();
    __CPROVER_assert(same_double(hml_lit_as_double(x), hml_to_f64(to_backend(x))),
                     "float folds use the backend's f64 operand");
#elif defined(TRUTHY)
    HmlNumLit x = nondet_lit();
    __CPROVER_assert(hml_lit_truthy(x) == hml_to_bool(to_backend(x)),
                     "constant truthiness matches the runtime");
#else
    HmlNumLit l = nondet_lit(), r = nondet_lit();
    __CPROVER_assume(l.kind == HML_LIT_F64 || r.kind == HML_LIT_F64);
    BinaryOp op = fold_pick_op();

    HmlFoldResult out;
    if (!hml_fold_binary(op, l, r, &out)) return;

    hml_cbmc_error_allowed = 0;   /* a folded expression must not raise */
    HmlValue res = hml_binary_op(backend_op(op), to_backend(l), to_backend(r));
    if (out.is_bool) {
        __CPROVER_assert(res.type == HML_VAL_BOOL, "comparison yields bool");
        __CPROVER_assert(res.as.as_bool == out.flag, "folded comparison matches the runtime");
    } else {
        __CPROVER_assert(out.num.kind == HML_LIT_F64, "float op folds to an f64");
        __CPROVER_assert(res.type == HML_VAL_F64, "runtime result is f64");
        __CPROVER_assert(same_double(res.as.as_f64, out.num.v.f), "folded value matches the runtime");
    }
#endif
}
