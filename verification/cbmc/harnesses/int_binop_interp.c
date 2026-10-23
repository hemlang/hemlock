/*
 * PROOF: the interpreter's eval_binary_expr (src/backends/interpreter/
 * runtime/binary_ops.c) implements int_spec.h for every pair of integer
 * types, every operand value and every integer operator, without UB.
 *
 * Together with int_binop_runtime.c this is a parity proof: both backends
 * agree with one spec, hence with each other.
 *
 * eval_expr is stubbed to hand back the two operand values; runtime_error*
 * are stubbed to check the spec allows raising and set is_throwing, as the
 * real ones do.
 */
#include "int_spec.h"
#include "internal.h"

Value eval_binary_expr(Expr *expr, Environment *env, ExecutionContext *ctx);

static Value operand[2];
static int next_operand;
static int error_allowed;

Value eval_expr(Expr *expr, Environment *env, ExecutionContext *ctx) {
    (void)expr; (void)env; (void)ctx;
    return operand[next_operand++];
}

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
    SpecResult want = spec_eval(op, l, r);

    operand[0] = to_interp(l);
    operand[1] = to_interp(r);
    next_operand = 0;
    error_allowed = want.raises;

    static ExecutionContext ctx;   /* zero-initialised: not throwing */
    Expr expr;
    expr.line = 1;
    expr.as.binary.op = op_map[op];
    expr.as.binary.left = expr.as.binary.right = NULL;

    Value res = eval_binary_expr(&expr, NULL, &ctx);
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
