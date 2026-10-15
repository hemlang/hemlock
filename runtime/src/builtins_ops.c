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

/*
 * Runes are intentionally NOT numeric for binary operations (mirrors the
 * interpreter): rune-rune comparison/equality is handled by dedicated
 * blocks, everything else is an error.
 */
static inline int hml_op_numeric(HmlValue v) {
    return hml_is_numeric(v) && v.type != HML_VAL_RUNE;
}

HmlValue hml_binary_op(HmlBinaryOp op, HmlValue left, HmlValue right) {
    // Division always uses float regardless of operand types
    if (op == HML_OP_DIV && hml_op_numeric(left) && hml_op_numeric(right)) {
        double l = hml_to_f64(left);
        double r = hml_to_f64(right);
        // IEEE 754: float / 0.0 returns Inf or NaN - do not throw for float operands.
        // Only throw for integer operands where zero division is undefined.
        int either_float = (left.type == HML_VAL_F64 || left.type == HML_VAL_F32 ||
                            right.type == HML_VAL_F64 || right.type == HML_VAL_F32);
        if (r == 0.0 && !either_float) hml_runtime_error_line("Division by zero");
        // When the promoted type is f32 the quotient stays f32 (mirrors the
        // interpreter); everything else divides as f64.
        if (promote_types(left.type, right.type) == HML_VAL_F32) {
            return hml_val_f32((float)(l / r));
        }
        return hml_val_f64(l / r);
    }

    // FAST PATH: i32 operations (most common case)
    if (left.type == HML_VAL_I32 && right.type == HML_VAL_I32) {
        int32_t l = left.as.as_i32;
        int32_t r = right.as.as_i32;
        switch (op) {
            case HML_OP_ADD: return hml_i32_add(left, right);
            case HML_OP_SUB: return hml_i32_sub(left, right);
            case HML_OP_MUL: return hml_i32_mul(left, right);
            case HML_OP_MOD:
                if (r == 0) hml_runtime_error_line("Division by zero");
                if (r == -1) return hml_val_i32(0);  // Avoid INT32_MIN % -1 trap
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
            case HML_OP_LSHIFT:
                if (r < 0) hml_runtime_error_line("Shift amount must be non-negative");
                return hml_val_i32(r >= 32 ? 0 : (int32_t)((uint32_t)l << r));
            case HML_OP_RSHIFT:
                if (r < 0) hml_runtime_error_line("Shift amount must be non-negative");
                return hml_val_i32(r >= 32 ? (l < 0 ? -1 : 0) : l >> r);
            default: break;
        }
    }

    // FAST PATH: i64 operations
    if (left.type == HML_VAL_I64 && right.type == HML_VAL_I64) {
        int64_t l = left.as.as_i64;
        int64_t r = right.as.as_i64;
        switch (op) {
            case HML_OP_ADD: return hml_i64_add(left, right);
            case HML_OP_SUB: return hml_i64_sub(left, right);
            case HML_OP_MUL: return hml_i64_mul(left, right);
            case HML_OP_DIV:
                if (r == 0) hml_runtime_error_line("Division by zero");
                return hml_val_i64(l / r);
            case HML_OP_MOD:
                if (r == 0) hml_runtime_error_line("Division by zero");
                if (r == -1) return hml_val_i64(0);  // Avoid INT64_MIN % -1 trap
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
            case HML_OP_LSHIFT:
                if (r < 0) hml_runtime_error_line("Shift amount must be non-negative");
                return hml_val_i64(r >= 64 ? 0 : (int64_t)((uint64_t)l << r));
            case HML_OP_RSHIFT:
                if (r < 0) hml_runtime_error_line("Shift amount must be non-negative");
                return hml_val_i64(r >= 64 ? (l < 0 ? -1 : 0) : l >> r);
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

    // Equality/inequality (mirrors the interpreter's semantics)
    if (op == HML_OP_EQUAL || op == HML_OP_NOT_EQUAL) {
        int left_numeric = hml_op_numeric(left);
        int right_numeric = hml_op_numeric(right);

        // Pointer/pointer comparisons are by address, so a NULL ptr only
        // equals another NULL ptr. Checked before the null-literal case below
        // so that ptr operands never take the null path.
        if (left.type == HML_VAL_PTR && right.type == HML_VAL_PTR) {
            int equal = (left.as.as_ptr == right.as.as_ptr);
            return hml_val_bool(op == HML_OP_EQUAL ? equal : !equal);
        }
        // A ptr holding NULL compares equal to the null literal: guards of the
        // form `if (handle == null)` are the only way to check an FFI result
        // that failed.
        int left_null = (left.type == HML_VAL_NULL) ||
                        (left.type == HML_VAL_PTR && left.as.as_ptr == NULL);
        int right_null = (right.type == HML_VAL_NULL) ||
                         (right.type == HML_VAL_PTR && right.as.as_ptr == NULL);
        if (left_null || right_null) {
            int equal = left_null && right_null;
            return hml_val_bool(op == HML_OP_EQUAL ? equal : !equal);
        }
        if (left.type == HML_VAL_BOOL && right.type == HML_VAL_BOOL) {
            int equal = (left.as.as_bool == right.as.as_bool);
            return hml_val_bool(op == HML_OP_EQUAL ? equal : !equal);
        }
        if (left.type == HML_VAL_STRING && right.type == HML_VAL_STRING) {
            int equal = (strcmp(left.as.as_string->data, right.as.as_string->data) == 0);
            return hml_val_bool(op == HML_OP_EQUAL ? equal : !equal);
        }
        if (left.type == HML_VAL_RUNE && right.type == HML_VAL_RUNE) {
            int equal = (left.as.as_rune == right.as.as_rune);
            return hml_val_bool(op == HML_OP_EQUAL ? equal : !equal);
        }
        if (left.type == HML_VAL_OBJECT && right.type == HML_VAL_OBJECT) {
            int equal = (left.as.as_object == right.as.as_object);
            return hml_val_bool(op == HML_OP_EQUAL ? equal : !equal);
        }
        if (left_numeric != right_numeric) {
            // One side numeric, the other not: never equal
            return hml_val_bool(op == HML_OP_NOT_EQUAL);
        }
        if (!left_numeric && !right_numeric && left.type != right.type) {
            // Different non-numeric types: never equal
            return hml_val_bool(op == HML_OP_NOT_EQUAL);
        }
        // Both numeric: fall through to the promoted numeric comparison below.
        // Both non-numeric of the same type (e.g. array == array): fall
        // through to the numeric check below, which raises an error - this
        // matches the interpreter.
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
    if (!hml_op_numeric(left) || !hml_op_numeric(right)) {
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
            case HML_OP_EQUAL:        return hml_val_bool(l == r);
            case HML_OP_NOT_EQUAL:    return hml_val_bool(l != r);
            default:
                hml_runtime_error("Invalid operation for floats");
        }
        // Return f32 or f64 based on the promoted type
        if (result_type == HML_VAL_F32) {
            return hml_val_f32((float)result);
        }
        return hml_val_f64(result);
    }

    // Integer operations.
    // Arithmetic, comparisons and equality must respect the promoted type's
    // width and signedness (mirrors the interpreter):
    //   - u8..u64 and i8/i16 wrap (well-defined behavior)
    //   - i32/i64 add/sub/mul throw on overflow
    //   - comparisons of unsigned promoted types compare unsigned
    int64_t l = hml_to_i64(left);
    int64_t r = hml_to_i64(right);

    int is_unsigned = (result_type == HML_VAL_U8 || result_type == HML_VAL_U16 ||
                       result_type == HML_VAL_U32 || result_type == HML_VAL_U64);
    int bits = (result_type == HML_VAL_I8 || result_type == HML_VAL_U8) ? 8 :
               (result_type == HML_VAL_I16 || result_type == HML_VAL_U16) ? 16 :
               (result_type == HML_VAL_I32 || result_type == HML_VAL_U32) ? 32 : 64;

    // Operand values converted to the promoted type's representation,
    // widened for comparison purposes.
    uint64_t ul, ur;   // unsigned view (truncated to promoted width)
    int64_t sl, sr;    // signed view (truncated to promoted width)
    switch (bits) {
        case 8:
            ul = (uint8_t)l; ur = (uint8_t)r;
            sl = (int8_t)l;  sr = (int8_t)r;
            break;
        case 16:
            ul = (uint16_t)l; ur = (uint16_t)r;
            sl = (int16_t)l;  sr = (int16_t)r;
            break;
        case 32:
            ul = (uint32_t)l; ur = (uint32_t)r;
            sl = (int32_t)l;  sr = (int32_t)r;
            break;
        default:
            ul = (uint64_t)l; ur = (uint64_t)r;
            sl = l;           sr = r;
            break;
    }

    switch (op) {
        case HML_OP_ADD:
        case HML_OP_SUB:
        case HML_OP_MUL: {
            if (result_type == HML_VAL_I32) {
                int32_t result;
                int overflow = (op == HML_OP_ADD) ? __builtin_add_overflow((int32_t)sl, (int32_t)sr, &result) :
                               (op == HML_OP_SUB) ? __builtin_sub_overflow((int32_t)sl, (int32_t)sr, &result) :
                                                    __builtin_mul_overflow((int32_t)sl, (int32_t)sr, &result);
                if (overflow) hml_runtime_error_line("Integer overflow: i32 arithmetic");
                return hml_val_i32(result);
            }
            if (result_type == HML_VAL_I64) {
                int64_t result;
                int overflow = (op == HML_OP_ADD) ? __builtin_add_overflow(sl, sr, &result) :
                               (op == HML_OP_SUB) ? __builtin_sub_overflow(sl, sr, &result) :
                                                    __builtin_mul_overflow(sl, sr, &result);
                if (overflow) hml_runtime_error_line("Integer overflow: i64 arithmetic");
                return hml_val_i64(result);
            }
            // Narrow signed and all unsigned types wrap (computed in
            // unsigned arithmetic, then truncated by make_int_result)
            uint64_t result = (op == HML_OP_ADD) ? (ul + ur) :
                              (op == HML_OP_SUB) ? (ul - ur) : (ul * ur);
            return make_int_result(result_type, (int64_t)result);
        }
        case HML_OP_DIV:
            if (r == 0) {
                hml_runtime_error_line("Division by zero");
            }
            if (is_unsigned) {
                return make_int_result(result_type, (int64_t)(ul / ur));
            }
            return make_int_result(result_type, sl / sr);
        case HML_OP_MOD:
            if (r == 0) {
                hml_runtime_error_line("Division by zero");
            }
            if (is_unsigned) {
                return make_int_result(result_type, (int64_t)(ul % ur));
            }
            if (sr == -1) {
                return make_int_result(result_type, 0);  // Avoid MIN % -1 trap
            }
            return make_int_result(result_type, sl % sr);
        case HML_OP_EQUAL:
            return hml_val_bool(is_unsigned ? (ul == ur) : (sl == sr));
        case HML_OP_NOT_EQUAL:
            return hml_val_bool(is_unsigned ? (ul != ur) : (sl != sr));
        case HML_OP_LESS:
            return hml_val_bool(is_unsigned ? (ul < ur) : (sl < sr));
        case HML_OP_LESS_EQUAL:
            return hml_val_bool(is_unsigned ? (ul <= ur) : (sl <= sr));
        case HML_OP_GREATER:
            return hml_val_bool(is_unsigned ? (ul > ur) : (sl > sr));
        case HML_OP_GREATER_EQUAL:
            return hml_val_bool(is_unsigned ? (ul >= ur) : (sl >= sr));
        case HML_OP_BIT_AND:
            return make_int_result(result_type, l & r);
        case HML_OP_BIT_OR:
            return make_int_result(result_type, l | r);
        case HML_OP_BIT_XOR:
            return make_int_result(result_type, l ^ r);
        case HML_OP_LSHIFT: {
            if (r < 0) hml_runtime_error_line("Shift amount must be non-negative");
            int bw = (result_type == HML_VAL_I8 || result_type == HML_VAL_U8) ? 8 :
                     (result_type == HML_VAL_I16 || result_type == HML_VAL_U16) ? 16 :
                     (result_type == HML_VAL_I32 || result_type == HML_VAL_U32) ? 32 : 64;
            return make_int_result(result_type, r >= bw ? 0 : (int64_t)((uint64_t)l << r));
        }
        case HML_OP_RSHIFT: {
            if (r < 0) hml_runtime_error_line("Shift amount must be non-negative");
            int bw = (result_type == HML_VAL_I8 || result_type == HML_VAL_U8) ? 8 :
                     (result_type == HML_VAL_I16 || result_type == HML_VAL_U16) ? 16 :
                     (result_type == HML_VAL_I32 || result_type == HML_VAL_U32) ? 32 : 64;
            int is_signed = (result_type == HML_VAL_I8 || result_type == HML_VAL_I16 ||
                            result_type == HML_VAL_I32 || result_type == HML_VAL_I64);
            if (r >= bw) {
                return make_int_result(result_type, is_signed && l < 0 ? -1 : 0);
            }
            // Unsigned results shift logically: `l` is the int64_t view of the
            // promoted value, so for u64 with the high bit set `l >> r` would
            // sign-extend (arithmetic shift) instead of shifting in zeros.
            if (!is_signed) {
                return make_int_result(result_type, (int64_t)((uint64_t)l >> r));
            }
            return make_int_result(result_type, l >> r);
        }
        default:
            break;
    }

    hml_runtime_error("Unknown binary operation");
}

// ========== INCREMENT / DECREMENT ==========

/*
 * ++/-- preserve the operand's type: u16 65535++ wraps to 0, i8 127++ wraps
 * to -128, floats add/subtract 1.0 keeping f32/f64. Non-numeric operands
 * raise an error (mirrors the interpreter's value_add_one/value_sub_one).
 */
static HmlValue hml_value_step(HmlValue val, int64_t delta, const char *verb) {
    switch (val.type) {
        case HML_VAL_F32: return hml_val_f32((float)(val.as.as_f32 + (double)delta));
        case HML_VAL_F64: return hml_val_f64(val.as.as_f64 + (double)delta);
        case HML_VAL_I8:  case HML_VAL_I16: case HML_VAL_I32: case HML_VAL_I64:
        case HML_VAL_U8:  case HML_VAL_U16: case HML_VAL_U32: case HML_VAL_U64: {
            // Compute in unsigned 64-bit (well-defined wrap), then truncate
            // back to the operand's own type.
            uint64_t v = (uint64_t)hml_to_i64(val);
            return make_int_result(val.type, (int64_t)(v + (uint64_t)delta));
        }
        default:
            hml_runtime_error("Can only %s numeric values", verb);
    }
}

HmlValue hml_value_inc(HmlValue val) {
    return hml_value_step(val, 1, "increment");
}

HmlValue hml_value_dec(HmlValue val) {
    return hml_value_step(val, -1, "decrement");
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
            // Mirrors the interpreter: floats keep their width; signed narrow
            // types wrap in-type; i32/i64 MIN throws (checked arithmetic);
            // unsigned types promote to the next signed type that can hold
            // the negated value.
            switch (operand.type) {
                case HML_VAL_F64: return hml_val_f64(-operand.as.as_f64);
                case HML_VAL_F32: return hml_val_f32(-operand.as.as_f32);
                case HML_VAL_I8:  return hml_val_i8((int8_t)(-(int16_t)operand.as.as_i8));
                case HML_VAL_I16: return hml_val_i16((int16_t)(-(int32_t)operand.as.as_i16));
                case HML_VAL_I32:
                    if (operand.as.as_i32 == INT32_MIN) {
                        hml_runtime_error_loc("Integer overflow: i32 negation");
                    }
                    return hml_val_i32(-operand.as.as_i32);
                case HML_VAL_I64:
                    if (operand.as.as_i64 == INT64_MIN) {
                        hml_runtime_error_loc("Integer overflow: i64 negation");
                    }
                    return hml_val_i64(-operand.as.as_i64);
                case HML_VAL_U8:  return hml_val_i16((int16_t)-(int16_t)operand.as.as_u8);
                case HML_VAL_U16: return hml_val_i32(-(int32_t)operand.as.as_u16);
                case HML_VAL_U32: return hml_val_i64(-(int64_t)operand.as.as_u32);
                case HML_VAL_U64:
                    if (operand.as.as_u64 <= (uint64_t)INT64_MAX) {
                        return hml_val_i64(-(int64_t)operand.as.as_u64);
                    }
                    hml_runtime_error("Cannot negate u64 value larger than INT64_MAX");
                default:
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

