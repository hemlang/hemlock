/*
 * PROOF: runtime unary minus, ~, ++ and -- on integers follow the
 * documented overflow policy, without UB, for every integer type and value:
 *
 *   -x : i8/i16 wrap in-type; i32/i64 MIN raise; u8 -> i16, u16 -> i32,
 *        u32 -> i64 (exact); u64 -> i64 exact when x <= INT64_MAX, else raise
 *   ~x : same type, bitwise complement
 *   ++x / --x : same type, always wrap
 */
#include "int_spec.h"
#include "hemlock_runtime.h"

extern int hml_cbmc_error_allowed;

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

static SpecInt from_runtime(HmlValue h) {
    switch (h.type) {
        case HML_VAL_I8:  return (SpecInt){ HML_TK_I8,  h.as.as_i8 };
        case HML_VAL_I16: return (SpecInt){ HML_TK_I16, h.as.as_i16 };
        case HML_VAL_I32: return (SpecInt){ HML_TK_I32, h.as.as_i32 };
        case HML_VAL_I64: return (SpecInt){ HML_TK_I64, h.as.as_i64 };
        case HML_VAL_U8:  return (SpecInt){ HML_TK_U8,  h.as.as_u8 };
        case HML_VAL_U16: return (SpecInt){ HML_TK_U16, h.as.as_u16 };
        case HML_VAL_U32: return (SpecInt){ HML_TK_U32, h.as.as_u32 };
        case HML_VAL_U64: return (SpecInt){ HML_TK_U64, h.as.as_u64 };
        default:          return (SpecInt){ HML_TK_NULL, 0 };
    }
}

void harness(void) {
    SpecInt x = spec_nondet_int(spec_pick_type(LT));
    HmlValue v = to_runtime(x);
    int which = nondet_int();
    __CPROVER_assume(which >= 0 && which <= 3);

    SpecInt want;
    int raises = 0;
    switch (which) {
        case 0: /* negate */
            switch (x.type) {
                case HML_TK_I8: case HML_TK_I16:
                    want = (SpecInt){ x.type, spec_wrap(-x.value, x.type) }; break;
                case HML_TK_I32: case HML_TK_I64:
                    want = (SpecInt){ x.type, -x.value };
                    raises = spec_wrap(-x.value, x.type) != -x.value; break;
                case HML_TK_U8:  want = (SpecInt){ HML_TK_I16, -x.value }; break;
                case HML_TK_U16: want = (SpecInt){ HML_TK_I32, -x.value }; break;
                case HML_TK_U32: want = (SpecInt){ HML_TK_I64, -x.value }; break;
                default:
                    want = (SpecInt){ HML_TK_I64, -x.value };
                    raises = x.value > INT64_MAX; break;
            }
            break;
        case 1: want = (SpecInt){ x.type, spec_wrap(~x.value, x.type) }; break;
        case 2: want = (SpecInt){ x.type, spec_wrap(x.value + 1, x.type) }; break;
        default: want = (SpecInt){ x.type, spec_wrap(x.value - 1, x.type) }; break;
    }

    hml_cbmc_error_allowed = raises;
    HmlValue res = which == 0 ? hml_unary_op(HML_UNARY_NEGATE, v) :
                   which == 1 ? hml_unary_op(HML_UNARY_BIT_NOT, v) :
                   which == 2 ? hml_value_inc(v) : hml_value_dec(v);
    __CPROVER_assert(!raises, "raises when the spec requires it");
    SpecInt got = from_runtime(res);
    __CPROVER_assert(got.type == want.type, "result type matches spec");
    __CPROVER_assert(got.value == want.value, "result value matches spec");
}
