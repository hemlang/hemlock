/*
 * Hemlock Runtime Library - Operations Builtins
 *
 * This file implements:
 * - Type operations (sizeof)
 * - Binary operations (+, -, *, /, %, etc.)
 * - Unary operations (-, !, ~)
 * - Type promotion helpers
 *
 * Uses shared modules for type utilities (type_promotion.h)
 */

#include "builtins_internal.h"
#include "type_promotion.h"

/* ========== TYPE CONVERSION HELPERS ==========
 *
 * Convert between HmlValueType (runtime) and HmlTypeKind (shared module).
 * This allows us to use the shared type promotion logic.
 */

static inline HmlTypeKind hml_val_to_tk(HmlValueType vt) {
    switch (vt) {
        case HML_VAL_NULL:   return HML_TK_NULL;
        case HML_VAL_BOOL:   return HML_TK_BOOL;
        case HML_VAL_I8:     return HML_TK_I8;
        case HML_VAL_I16:    return HML_TK_I16;
        case HML_VAL_I32:    return HML_TK_I32;
        case HML_VAL_I64:    return HML_TK_I64;
        case HML_VAL_U8:     return HML_TK_U8;
        case HML_VAL_U16:    return HML_TK_U16;
        case HML_VAL_U32:    return HML_TK_U32;
        case HML_VAL_U64:    return HML_TK_U64;
        case HML_VAL_F32:    return HML_TK_F32;
        case HML_VAL_F64:    return HML_TK_F64;
        case HML_VAL_RUNE:   return HML_TK_RUNE;
        case HML_VAL_STRING: return HML_TK_STRING;
        case HML_VAL_PTR:    return HML_TK_PTR;
        default:             return HML_TK_OTHER;
    }
}

static inline HmlValueType hml_tk_to_val(HmlTypeKind tk) {
    switch (tk) {
        case HML_TK_NULL:   return HML_VAL_NULL;
        case HML_TK_BOOL:   return HML_VAL_BOOL;
        case HML_TK_I8:     return HML_VAL_I8;
        case HML_TK_I16:    return HML_VAL_I16;
        case HML_TK_I32:    return HML_VAL_I32;
        case HML_TK_I64:    return HML_VAL_I64;
        case HML_TK_U8:     return HML_VAL_U8;
        case HML_TK_U16:    return HML_VAL_U16;
        case HML_TK_U32:    return HML_VAL_U32;
        case HML_TK_U64:    return HML_VAL_U64;
        case HML_TK_F32:    return HML_VAL_F32;
        case HML_TK_F64:    return HML_VAL_F64;
        case HML_TK_RUNE:   return HML_VAL_RUNE;
        case HML_TK_STRING: return HML_VAL_STRING;
        case HML_TK_PTR:    return HML_VAL_PTR;
        default:            return HML_VAL_NULL;
    }
}

// ========== TYPE OPERATIONS ==========

HmlValue hml_sizeof(HmlValue type_name) {
    if (type_name.type != HML_VAL_STRING || !type_name.as.as_string) {
        return hml_val_i32(0);
    }
    const char *name = type_name.as.as_string->data;
    // Use shared type utilities for sizeof
    HmlTypeKind tk = hml_tk_from_name(name);
    int size = hml_tk_sizeof(tk);
    return hml_val_i32(size);
}

// ========== BINARY OPERATIONS ==========

/*
 * Type promotion using shared module.
 * These wrapper functions convert between HmlValueType and HmlTypeKind,
 * use the shared implementation, then convert back.
 */
static inline HmlValueType promote_types(HmlValueType a, HmlValueType b) {
    HmlTypeKind result = hml_tk_promote(hml_val_to_tk(a), hml_val_to_tk(b));
    return hml_tk_to_val(result);
}

// Create an integer result value with the correct type
HmlValue make_int_result(HmlValueType result_type, int64_t value) {
    switch (result_type) {
        case HML_VAL_I8:  return hml_val_i8((int8_t)value);
        case HML_VAL_I16: return hml_val_i16((int16_t)value);
        case HML_VAL_I32: return hml_val_i32((int32_t)value);
        case HML_VAL_I64: return hml_val_i64(value);
        case HML_VAL_U8:  return hml_val_u8((uint8_t)value);
        case HML_VAL_U16: return hml_val_u16((uint16_t)value);
        case HML_VAL_U32: return hml_val_u32((uint32_t)value);
        case HML_VAL_U64: return hml_val_u64((uint64_t)value);
        default:          return hml_val_i64(value);
    }
}

HmlValue hml_binary_op(HmlBinaryOp op, HmlValue left, HmlValue right) {
    // Division always uses float regardless of operand types
    if (op == HML_OP_DIV) {
        double l = hml_to_f64(left);
        double r = hml_to_f64(right);
        // IEEE 754: float / 0.0 returns Inf or NaN - do not throw for float operands.
        // Only throw for integer operands where zero division is undefined.
        int either_float = (left.type == HML_VAL_F64 || left.type == HML_VAL_F32 ||
                            right.type == HML_VAL_F64 || right.type == HML_VAL_F32);
        if (r == 0.0 && !either_float) hml_runtime_error("Division by zero");
        return hml_val_f64(l / r);
    }

    // FAST PATH: i32 operations (most common case)
    if (left.type == HML_VAL_I32 && right.type == HML_VAL_I32) {
        int32_t l = left.as.as_i32;
        int32_t r = right.as.as_i32;
        switch (op) {
            case HML_OP_ADD: return hml_val_i32(l + r);
            case HML_OP_SUB: return hml_val_i32(l - r);
            case HML_OP_MUL: return hml_val_i32(l * r);
            case HML_OP_MOD:
                if (r == 0) hml_runtime_error("Division by zero");
                return hml_val_i32(l % r);
            case HML_OP_LESS: return hml_val_bool(l < r);
            case HML_OP_LESS_EQUAL: return hml_val_bool(l <= r);
            case HML_OP_GREATER: return hml_val_bool(l > r);
            case HML_OP_GREATER_EQUAL: return hml_val_bool(l >= r);
            case HML_OP_EQUAL: return hml_val_bool(l == r);
            case HML_OP_NOT_EQUAL: return hml_val_bool(l != r);
            case HML_OP_BIT_AND: return hml_val_i32(l & r);
            case HML_OP_BIT_OR: return hml_val_i32(l | r);
            case HML_OP_BIT_XOR: return hml_val_i32(l ^ r);
            case HML_OP_LSHIFT: return hml_val_i32(l << r);
            case HML_OP_RSHIFT: return hml_val_i32(l >> r);
            default: break;
        }
    }

    // FAST PATH: i64 operations
    if (left.type == HML_VAL_I64 && right.type == HML_VAL_I64) {
        int64_t l = left.as.as_i64;
        int64_t r = right.as.as_i64;
        switch (op) {
            case HML_OP_ADD: return hml_val_i64(l + r);
            case HML_OP_SUB: return hml_val_i64(l - r);
            case HML_OP_MUL: return hml_val_i64(l * r);
            case HML_OP_DIV:
                if (r == 0) hml_runtime_error("Division by zero");
                return hml_val_i64(l / r);
            case HML_OP_MOD:
                if (r == 0) hml_runtime_error("Division by zero");
                return hml_val_i64(l % r);
            case HML_OP_LESS: return hml_val_bool(l < r);
            case HML_OP_LESS_EQUAL: return hml_val_bool(l <= r);
            case HML_OP_GREATER: return hml_val_bool(l > r);
            case HML_OP_GREATER_EQUAL: return hml_val_bool(l >= r);
            case HML_OP_EQUAL: return hml_val_bool(l == r);
            case HML_OP_NOT_EQUAL: return hml_val_bool(l != r);
            case HML_OP_BIT_AND: return hml_val_i64(l & r);
            case HML_OP_BIT_OR: return hml_val_i64(l | r);
            case HML_OP_BIT_XOR: return hml_val_i64(l ^ r);
            case HML_OP_LSHIFT: return hml_val_i64(l << r);
            case HML_OP_RSHIFT: return hml_val_i64(l >> r);
            default: break;
        }
    }

    // FAST PATH: f64 operations
    if (left.type == HML_VAL_F64 && right.type == HML_VAL_F64) {
        double l = left.as.as_f64;
        double r = right.as.as_f64;
        switch (op) {
            case HML_OP_ADD: return hml_val_f64(l + r);
            case HML_OP_SUB: return hml_val_f64(l - r);
            case HML_OP_MUL: return hml_val_f64(l * r);
            case HML_OP_DIV:
                // IEEE 754: float division by zero returns Infinity or NaN
                return hml_val_f64(l / r);
            case HML_OP_LESS: return hml_val_bool(l < r);
            case HML_OP_LESS_EQUAL: return hml_val_bool(l <= r);
            case HML_OP_GREATER: return hml_val_bool(l > r);
            case HML_OP_GREATER_EQUAL: return hml_val_bool(l >= r);
            case HML_OP_EQUAL: return hml_val_bool(l == r);
            case HML_OP_NOT_EQUAL: return hml_val_bool(l != r);
            default: break;
        }
    }

    // String concatenation
    if (op == HML_OP_ADD && (left.type == HML_VAL_STRING || right.type == HML_VAL_STRING)) {
        return hml_string_concat(left, right);
    }

    // Boolean operations
    if (op == HML_OP_AND) {
        return hml_val_bool(hml_to_bool(left) && hml_to_bool(right));
    }
    if (op == HML_OP_OR) {
        return hml_val_bool(hml_to_bool(left) || hml_to_bool(right));
    }

    // Equality/inequality work on all types
    if (op == HML_OP_EQUAL || op == HML_OP_NOT_EQUAL) {
        int equal = 0;
        if (left.type == HML_VAL_NULL || right.type == HML_VAL_NULL) {
            equal = (left.type == HML_VAL_NULL && right.type == HML_VAL_NULL);
        } else if (left.type == HML_VAL_BOOL && right.type == HML_VAL_BOOL) {
            equal = (left.as.as_bool == right.as.as_bool);
        } else if (left.type == HML_VAL_STRING && right.type == HML_VAL_STRING) {
            equal = (strcmp(left.as.as_string->data, right.as.as_string->data) == 0);
        } else if (left.type == HML_VAL_RUNE && right.type == HML_VAL_RUNE) {
            equal = (left.as.as_rune == right.as.as_rune);
        } else if (left.type == HML_VAL_PTR && right.type == HML_VAL_PTR) {
            equal = (left.as.as_ptr == right.as.as_ptr);
        } else if (left.type == HML_VAL_OBJECT && right.type == HML_VAL_OBJECT) {
            equal = (left.as.as_object == right.as.as_object);
        } else if (left.type == HML_VAL_ARRAY && right.type == HML_VAL_ARRAY) {
            equal = (left.as.as_array == right.as.as_array);
        } else if (hml_is_numeric(left) && hml_is_numeric(right)) {
            double l = hml_to_f64(left);
            double r = hml_to_f64(right);
            equal = (l == r);
        } else {
            equal = 0;  // Different types are not equal
        }
        return hml_val_bool(op == HML_OP_EQUAL ? equal : !equal);
    }

    // Rune comparison operations (ordering)
    if (left.type == HML_VAL_RUNE && right.type == HML_VAL_RUNE) {
        uint32_t l = left.as.as_rune;
        uint32_t r = right.as.as_rune;
        switch (op) {
            case HML_OP_LESS:          return hml_val_bool(l < r);
            case HML_OP_LESS_EQUAL:    return hml_val_bool(l <= r);
            case HML_OP_GREATER:       return hml_val_bool(l > r);
            case HML_OP_GREATER_EQUAL: return hml_val_bool(l >= r);
            default:
                hml_runtime_error("Invalid operation for rune type");
        }
    }

    // String comparison operations (ordering)
    if (left.type == HML_VAL_STRING && right.type == HML_VAL_STRING) {
        int cmp = strcmp(left.as.as_string->data, right.as.as_string->data);
        switch (op) {
            case HML_OP_LESS:          return hml_val_bool(cmp < 0);
            case HML_OP_LESS_EQUAL:    return hml_val_bool(cmp <= 0);
            case HML_OP_GREATER:       return hml_val_bool(cmp > 0);
            case HML_OP_GREATER_EQUAL: return hml_val_bool(cmp >= 0);
            default:
                hml_runtime_error("Invalid operation for string type");
        }
    }

    // Pointer arithmetic: ptr + int or ptr - int
    if (left.type == HML_VAL_PTR && hml_is_numeric(right)) {
        int64_t offset = hml_to_i64(right);
        switch (op) {
            case HML_OP_ADD:
                return hml_val_ptr((char*)left.as.as_ptr + offset);
            case HML_OP_SUB:
                return hml_val_ptr((char*)left.as.as_ptr - offset);
            default:
                hml_runtime_error("Invalid operation for pointer type");
        }
    }

    // Pointer comparisons (both null and non-null)
    if (left.type == HML_VAL_PTR && right.type == HML_VAL_PTR) {
        void *lp = left.as.as_ptr;
        void *rp = right.as.as_ptr;
        switch (op) {
            case HML_OP_EQUAL:         return hml_val_bool(lp == rp);
            case HML_OP_NOT_EQUAL:     return hml_val_bool(lp != rp);
            case HML_OP_LESS:          return hml_val_bool(lp < rp);
            case HML_OP_LESS_EQUAL:    return hml_val_bool(lp <= rp);
            case HML_OP_GREATER:       return hml_val_bool(lp > rp);
            case HML_OP_GREATER_EQUAL: return hml_val_bool(lp >= rp);
            default:
                hml_runtime_error("Invalid operation for pointer type");
        }
    }

    // Numeric operations
    if (!hml_is_numeric(left) || !hml_is_numeric(right)) {
        hml_runtime_error("Cannot perform numeric operation on non-numeric types");
    }

    HmlValueType result_type = promote_types(left.type, right.type);

    // Float operations
    if (result_type == HML_VAL_F64 || result_type == HML_VAL_F32) {
        double l = hml_to_f64(left);
        double r = hml_to_f64(right);
        double result;

        switch (op) {
            case HML_OP_ADD:      result = l + r; break;
            case HML_OP_SUB:      result = l - r; break;
            case HML_OP_MUL:      result = l * r; break;
            case HML_OP_DIV:
                // IEEE 754: float division by zero returns Infinity or NaN
                result = l / r;
                break;
            case HML_OP_MOD:
                // IEEE 754: fmod with zero returns NaN
                result = fmod(l, r);
                break;
            case HML_OP_LESS:         return hml_val_bool(l < r);
            case HML_OP_LESS_EQUAL:   return hml_val_bool(l <= r);
            case HML_OP_GREATER:      return hml_val_bool(l > r);
            case HML_OP_GREATER_EQUAL: return hml_val_bool(l >= r);
            default:
                hml_runtime_error("Invalid operation for floats");
        }
        // Return f32 or f64 based on the promoted type
        if (result_type == HML_VAL_F32) {
            return hml_val_f32((float)result);
        }
        return hml_val_f64(result);
    }

    // Integer operations
    int64_t l = hml_to_i64(left);
    int64_t r = hml_to_i64(right);

    switch (op) {
        case HML_OP_ADD:
            return make_int_result(result_type, l + r);
        case HML_OP_SUB:
            return make_int_result(result_type, l - r);
        case HML_OP_MUL:
            return make_int_result(result_type, l * r);
        case HML_OP_DIV:
            if (r == 0) {
                hml_runtime_error("Division by zero");
            }
            return make_int_result(result_type, l / r);
        case HML_OP_MOD:
            if (r == 0) {
                hml_runtime_error("Division by zero");
            }
            return make_int_result(result_type, l % r);
        case HML_OP_LESS:         return hml_val_bool(l < r);
        case HML_OP_LESS_EQUAL:   return hml_val_bool(l <= r);
        case HML_OP_GREATER:      return hml_val_bool(l > r);
        case HML_OP_GREATER_EQUAL: return hml_val_bool(l >= r);
        case HML_OP_BIT_AND:
            return make_int_result(result_type, l & r);
        case HML_OP_BIT_OR:
            return make_int_result(result_type, l | r);
        case HML_OP_BIT_XOR:
            return make_int_result(result_type, l ^ r);
        case HML_OP_LSHIFT:
            return make_int_result(result_type, l << r);
        case HML_OP_RSHIFT:
            return make_int_result(result_type, l >> r);
        default:
            break;
    }

    hml_runtime_error("Unknown binary operation");
}

// ========== UNARY OPERATIONS ==========

HmlValue hml_unary_op(HmlUnaryOp op, HmlValue operand) {
    switch (op) {
        case HML_UNARY_NOT:
            return hml_val_bool(!hml_to_bool(operand));

        case HML_UNARY_NEGATE:
            if (!hml_is_numeric(operand)) {
                hml_runtime_error("Cannot negate non-numeric type");
            }
            if (operand.type == HML_VAL_F64) {
                return hml_val_f64(-operand.as.as_f64);
            } else if (operand.type == HML_VAL_F32) {
                return hml_val_f32(-operand.as.as_f32);
            } else if (operand.type == HML_VAL_I64) {
                return hml_val_i64(-operand.as.as_i64);
            } else {
                return hml_val_i32(-hml_to_i32(operand));
            }

        case HML_UNARY_BIT_NOT:
            if (!hml_is_integer(operand)) {
                hml_runtime_error("Bitwise NOT requires integer type");
            }
            // Preserve the original type
            switch (operand.type) {
                case HML_VAL_I8:  return hml_val_i8(~operand.as.as_i8);
                case HML_VAL_I16: return hml_val_i16(~operand.as.as_i16);
                case HML_VAL_I32: return hml_val_i32(~operand.as.as_i32);
                case HML_VAL_I64: return hml_val_i64(~operand.as.as_i64);
                case HML_VAL_U8:  return hml_val_u8(~operand.as.as_u8);
                case HML_VAL_U16: return hml_val_u16(~operand.as.as_u16);
                case HML_VAL_U32: return hml_val_u32(~operand.as.as_u32);
                case HML_VAL_U64: return hml_val_u64(~operand.as.as_u64);
                default: return hml_val_i32(~hml_to_i32(operand));
            }
    }

    return hml_val_null();
}

