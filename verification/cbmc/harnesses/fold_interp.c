/*
 * PROOF: folds the integer spec does not cover agree with the interpreter
 * (eval_binary_values, value_is_truthy). See fold_backend.h.
 */
#include "fold_backend.h"
#include "internal.h"
#include "runtime/expressions_internal.h"

static void raise_stub(ExecutionContext *ctx) {
    __CPROVER_assert(0, "a folded expression must not raise");
    ctx->exception_state.is_throwing = 1;
}
void runtime_error(ExecutionContext *ctx, const char *format, ...) { (void)format; raise_stub(ctx); }
void runtime_error_at(ExecutionContext *ctx, int line, const char *format, ...) {
    (void)line; (void)format; raise_stub(ctx);
}

static Value to_backend(HmlNumLit l) {
    switch (l.kind) {
        case HML_LIT_F64: return val_f64(l.v.f);
        case HML_LIT_U64: return val_u64(l.v.u);
        default: return hml_lit_fits_i32(l.v.i) ? val_i32((int32_t)l.v.i) : val_i64(l.v.i);
    }
}

void harness(void) {
#ifdef TRUTHY
    HmlNumLit x = nondet_lit();
    __CPROVER_assert(hml_lit_truthy(x) == value_is_truthy(to_backend(x)),
                     "constant truthiness matches the interpreter");
#else
    HmlNumLit l = nondet_lit(), r = nondet_lit();
    __CPROVER_assume(l.kind == HML_LIT_F64 || r.kind == HML_LIT_F64);
    BinaryOp op = fold_pick_op();

    HmlFoldResult out;
    if (!hml_fold_binary(op, l, r, &out)) return;

    static ExecutionContext ctx;   /* zero-initialised: not throwing */
    Value res = eval_binary_values(op, 1, to_backend(l), to_backend(r), &ctx);
    if (out.is_bool) {
        __CPROVER_assert(res.type == VAL_BOOL, "comparison yields bool");
        __CPROVER_assert(res.as.as_bool == out.flag, "folded comparison matches the interpreter");
    } else {
        __CPROVER_assert(out.num.kind == HML_LIT_F64, "float op folds to an f64");
        __CPROVER_assert(res.type == VAL_F64, "interpreter result is f64");
        __CPROVER_assert(same_double(res.as.as_f64, out.num.v.f), "folded value matches the interpreter");
    }
#endif
}
