/*
 * PROOF: the compiled-program runtime's hml_binary_op (runtime/src/
 * builtins_ops.c) implements int_spec.h for every pair of integer types,
 * every operand value and every integer operator, without UB.
 * Split the work with -DOP=SPEC_xxx and/or -DLT=/-DRT=HML_TK_xxx.
 */
#include "int_spec.h"
#include "hemlock_runtime.h"

extern int hml_cbmc_error_allowed;

static const HmlBinaryOp op_map[SPEC_OP_COUNT] = {
    HML_OP_ADD, HML_OP_SUB, HML_OP_MUL, HML_OP_MOD,
    HML_OP_BIT_AND, HML_OP_BIT_OR, HML_OP_BIT_XOR, HML_OP_LSHIFT, HML_OP_RSHIFT,
    HML_OP_EQUAL, HML_OP_NOT_EQUAL, HML_OP_LESS, HML_OP_LESS_EQUAL,
    HML_OP_GREATER, HML_OP_GREATER_EQUAL,
};

static HmlValue to_runtime(SpecInt v) {
    HmlValue h; h.as.as_u64 = 0;
    switch (v.type) {
        case HML_TK_I8:  h.type = HML_VAL_I8;  h.as.as_i8  = (int8_t)v.value;  break;
        case HML_TK_I16: h.type = HML_VAL_I16; h.as.as_i16 = (int16_t)v.value; break;
        case HML_TK_I32: h.type = HML_VAL_I32; h.as.as_i32 = (int32_t)v.value; break;
        case HML_TK_I64: h.type = HML_VAL_I64; h.as.as_i64 = (int64_t)v.value; break;
        case HML_TK_U8:  h.type = HML_VAL_U8;  h.as.as_u8  = (uint8_t)v.value;  break;
        case HML_TK_U16: h.type = HML_VAL_U16; h.as.as_u16 = (uint16_t)v.value; break;
        case HML_TK_U32: h.type = HML_VAL_U32; h.as.as_u32 = (uint32_t)v.value; break;
        default:         h.type = HML_VAL_U64; h.as.as_u64 = (uint64_t)v.value; break;
    }
    return h;
}

/* Returns 0 (and sets *out) only for integer results. */
static int from_runtime(HmlValue h, SpecInt *out) {
    switch (h.type) {
        case HML_VAL_I8:  *out = (SpecInt){ HML_TK_I8,  h.as.as_i8 };  return 1;
        case HML_VAL_I16: *out = (SpecInt){ HML_TK_I16, h.as.as_i16 }; return 1;
        case HML_VAL_I32: *out = (SpecInt){ HML_TK_I32, h.as.as_i32 }; return 1;
        case HML_VAL_I64: *out = (SpecInt){ HML_TK_I64, h.as.as_i64 }; return 1;
        case HML_VAL_U8:  *out = (SpecInt){ HML_TK_U8,  h.as.as_u8 };  return 1;
        case HML_VAL_U16: *out = (SpecInt){ HML_TK_U16, h.as.as_u16 }; return 1;
        case HML_VAL_U32: *out = (SpecInt){ HML_TK_U32, h.as.as_u32 }; return 1;
        case HML_VAL_U64: *out = (SpecInt){ HML_TK_U64, h.as.as_u64 }; return 1;
        default: return 0;
    }
}

void harness(void) {
    SpecInt l = spec_nondet_int(spec_pick_type(LT));
    SpecInt r = spec_nondet_int(spec_pick_type(RT));
    SpecOp op = spec_pick_op();
    SpecResult want = spec_eval(op, l, r);

    hml_cbmc_error_allowed = want.raises;
    HmlValue res = hml_binary_op(op_map[op], to_runtime(l), to_runtime(r));
    __CPROVER_assert(!want.raises, "raises when the spec requires it");

    if (want.is_bool) {
        __CPROVER_assert(res.type == HML_VAL_BOOL, "comparison yields bool");
        __CPROVER_assert(res.as.as_bool == want.flag, "comparison matches spec");
    } else {
        SpecInt got;
        __CPROVER_assert(from_runtime(res, &got), "integer op yields an integer");
        __CPROVER_assert(got.type == want.result.type, "result has the promoted type");
        __CPROVER_assert(got.value == want.result.value, "result value matches spec");
    }
}
