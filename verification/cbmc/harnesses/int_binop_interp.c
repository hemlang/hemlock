/*
 * PROOF: the interpreter's binary operators (eval_binary_values in
 * src/backends/interpreter/runtime/binary_ops.c) implement int_spec.h for every pair of integer
 * types, every operand value and every integer operator, without UB.
 *
 * Together with int_binop_runtime.c this is a parity proof: both backends
 * agree with one spec, hence with each other.
 *
 * runtime_error* are stubbed to check the spec allows raising and set
 * is_throwing, as the real ones do. (The harness calls eval_binary_values
 * rather than eval_binary_expr: an Expr in the program state trips a CBMC
 * 6.4.1 SMT2 encoding bug for unions with double members.)
 */
#include "int_spec.h"
#include "internal.h"
#include "runtime/expressions_internal.h"

static int error_allowed;

static void raise_stub(ExecutionContext *ctx) {
    __CPROVER_assert(error_allowed, "runtime error raised only when the spec allows it");
    ctx->exception_state.is_throwing = 1;
}
void runtime_error(ExecutionContext *ctx, const char *format, ...) { (void)format; raise_stub(ctx); }
void runtime_error_at(ExecutionContext *ctx, int line, const char *format, ...) {
    (void)line; (void)format; raise_stub(ctx);
}

static const BinaryOp op_map[SPEC_OP_COUNT] = {
    OP_ADD, OP_SUB, OP_MUL, OP_MOD,
    OP_BIT_AND, OP_BIT_OR, OP_BIT_XOR, OP_BIT_LSHIFT, OP_BIT_RSHIFT,
    OP_EQUAL, OP_NOT_EQUAL, OP_LESS, OP_LESS_EQUAL, OP_GREATER, OP_GREATER_EQUAL,
};

static Value to_interp(SpecInt v) {
    Value h; h.as.as_u64 = 0;
    switch (v.type) {
        case HML_TK_I8:  h.type = VAL_I8;  h.as.as_i8  = (int8_t)v.value;  break;
        case HML_TK_I16: h.type = VAL_I16; h.as.as_i16 = (int16_t)v.value; break;
        case HML_TK_I32: h.type = VAL_I32; h.as.as_i32 = (int32_t)v.value; break;
        case HML_TK_I64: h.type = VAL_I64; h.as.as_i64 = (int64_t)v.value; break;
        case HML_TK_U8:  h.type = VAL_U8;  h.as.as_u8  = (uint8_t)v.value;  break;
        case HML_TK_U16: h.type = VAL_U16; h.as.as_u16 = (uint16_t)v.value; break;
        case HML_TK_U32: h.type = VAL_U32; h.as.as_u32 = (uint32_t)v.value; break;
        default:         h.type = VAL_U64; h.as.as_u64 = (uint64_t)v.value; break;
    }
    return h;
}

static int from_interp(Value h, SpecInt *out) {
    switch (h.type) {
        case VAL_I8:  *out = (SpecInt){ HML_TK_I8,  h.as.as_i8 };  return 1;
        case VAL_I16: *out = (SpecInt){ HML_TK_I16, h.as.as_i16 }; return 1;
        case VAL_I32: *out = (SpecInt){ HML_TK_I32, h.as.as_i32 }; return 1;
        case VAL_I64: *out = (SpecInt){ HML_TK_I64, h.as.as_i64 }; return 1;
        case VAL_U8:  *out = (SpecInt){ HML_TK_U8,  h.as.as_u8 };  return 1;
        case VAL_U16: *out = (SpecInt){ HML_TK_U16, h.as.as_u16 }; return 1;
        case VAL_U32: *out = (SpecInt){ HML_TK_U32, h.as.as_u32 }; return 1;
        case VAL_U64: *out = (SpecInt){ HML_TK_U64, h.as.as_u64 }; return 1;
        default: return 0;
    }
}

void harness(void) {
    SpecInt l = spec_nondet_int(spec_pick_type(LT));
    SpecInt r = spec_nondet_int(spec_pick_type(RT));
    SpecOp op = spec_pick_op();
#ifdef ONE_SMALL_OPERAND
    /* Bounded variant for 64-bit products (see README "What is bounded"):
     * one operand within 16 bits, the other unrestricted. */
    __CPROVER_assume((l.value >= -32768 && l.value < 32768) ||
                     (r.value >= -32768 && r.value < 32768));
#endif
    SpecResult want = spec_eval(op, l, r);

    error_allowed = want.raises;

    static ExecutionContext ctx;   /* zero-initialised: not throwing */
    Value res = eval_binary_values(op_map[op], 1, to_interp(l), to_interp(r), &ctx);
    if (ctx.exception_state.is_throwing) return;   /* raise already checked */
    __CPROVER_assert(!want.raises, "raises when the spec requires it");

    if (want.is_bool) {
        __CPROVER_assert(res.type == VAL_BOOL, "comparison yields bool");
        __CPROVER_assert(res.as.as_bool == want.flag, "comparison matches spec");
    } else {
        SpecInt got;
        __CPROVER_assert(from_interp(res, &got), "integer op yields an integer");
        __CPROVER_assert(got.type == want.result.type, "result has the promoted type");
        __CPROVER_assert(got.value == want.result.value, "result value matches spec");
    }
}
