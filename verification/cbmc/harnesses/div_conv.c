/*
 * PROOF: integer `/` hands the IEEE division the operands converted to the
 * promoted type, on both backends, for every pair of integer types and
 * every value: the compiled runtime's hml_int_as_promoted_f64 (the only
 * conversion its `/` uses) and the interpreter's promote_value +
 * value_to_float (its generic `/` path) both equal int_spec.h's cast
 * conversion, which equals spec_wrap.
 *
 * The quotient itself is compared end to end only for narrow types
 * (rt_div_*, in_div_*): two IEEE dividers on wide operands do not finish.
 * Build with -DBACKEND_INTERP for the interpreter.
 */
#include "int_spec.h"
#ifdef BACKEND_INTERP
#include "internal.h"
#else
#include "hemlock_runtime.h"
#include "builtins_internal.h"
#endif

#ifdef BACKEND_INTERP
static ValueType backend_type(HmlTypeKind t) {
    switch (t) {
        case HML_TK_I8:  return VAL_I8;  case HML_TK_I16: return VAL_I16;
        case HML_TK_I32: return VAL_I32; case HML_TK_I64: return VAL_I64;
        case HML_TK_U8:  return VAL_U8;  case HML_TK_U16: return VAL_U16;
        case HML_TK_U32: return VAL_U32; default:          return VAL_U64;
    }
}
static Value to_backend(SpecInt v) {
    Value h; h.as.as_u64 = 0; h.type = backend_type(v.type);
    switch (v.type) {
        case HML_TK_I8:  h.as.as_i8  = (int8_t)v.value;  break;
        case HML_TK_I16: h.as.as_i16 = (int16_t)v.value; break;
        case HML_TK_I32: h.as.as_i32 = (int32_t)v.value; break;
        case HML_TK_I64: h.as.as_i64 = (int64_t)v.value; break;
        case HML_TK_U8:  h.as.as_u8  = (uint8_t)v.value;  break;
        case HML_TK_U16: h.as.as_u16 = (uint16_t)v.value; break;
        case HML_TK_U32: h.as.as_u32 = (uint32_t)v.value; break;
        default:         h.as.as_u64 = (uint64_t)v.value; break;
    }
    return h;
}
static double convert(SpecInt v, HmlTypeKind R) {
    return value_to_float(promote_value(to_backend(v), backend_type(R)));
}
#else
static HmlValueType backend_type(HmlTypeKind t) {
    switch (t) {
        case HML_TK_I8:  return HML_VAL_I8;  case HML_TK_I16: return HML_VAL_I16;
        case HML_TK_I32: return HML_VAL_I32; case HML_TK_I64: return HML_VAL_I64;
        case HML_TK_U8:  return HML_VAL_U8;  case HML_TK_U16: return HML_VAL_U16;
        case HML_TK_U32: return HML_VAL_U32; default:          return HML_VAL_U64;
    }
}
static HmlValue to_backend(SpecInt v) {
    HmlValue h; h.as.as_u64 = 0; h.type = backend_type(v.type);
    switch (v.type) {
        case HML_TK_I8:  h.as.as_i8  = (int8_t)v.value;  break;
        case HML_TK_I16: h.as.as_i16 = (int16_t)v.value; break;
        case HML_TK_I32: h.as.as_i32 = (int32_t)v.value; break;
        case HML_TK_I64: h.as.as_i64 = (int64_t)v.value; break;
        case HML_TK_U8:  h.as.as_u8  = (uint8_t)v.value;  break;
        case HML_TK_U16: h.as.as_u16 = (uint16_t)v.value; break;
        case HML_TK_U32: h.as.as_u32 = (uint32_t)v.value; break;
        default:         h.as.as_u64 = (uint64_t)v.value; break;
    }
    return h;
}
static double convert(SpecInt v, HmlTypeKind R) {
    return hml_int_as_promoted_f64(to_backend(v), backend_type(R));
}
#endif

void harness(void) {
    SpecInt l = spec_nondet_int(spec_pick_type(LT));
    SpecInt r = spec_nondet_int(spec_pick_type(RT));
    HmlTypeKind R = spec_promote(l.type, r.type);
    double want = spec_cast_to_double(R, l.value);
    __CPROVER_assert(want == spec_to_double(R, spec_wrap(l.value, R)), "spec: cast conversion equals spec_wrap");
    __CPROVER_assert(convert(l, R) == want, "operand converted to the promoted type");
}
