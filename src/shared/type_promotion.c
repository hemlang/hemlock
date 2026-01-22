/*
 * Hemlock Shared Type Promotion Implementation
 *
 * Type promotion rules for binary operations.
 * No dependencies on Value types - works with type enums only.
 *
 * Functions use hml_tk_* prefix (TK = Type Kind) to avoid conflicts
 * with backend-specific functions that work with Value types.
 */

#include "type_promotion.h"
#include <string.h>

/* ========== TYPE PRIORITY ========== */

int hml_tk_priority(HmlTypeKind type) {
    switch (type) {
        case HML_TK_I8:   return 0;
        case HML_TK_U8:   return 1;
        case HML_TK_I16:  return 2;
        case HML_TK_U16:  return 3;
        case HML_TK_I32:  return 4;
        case HML_TK_RUNE: return 4;  /* Runes promote like i32 */
        case HML_TK_U32:  return 5;
        case HML_TK_I64:  return 6;
        case HML_TK_U64:  return 7;
        case HML_TK_F32:  return 8;
        case HML_TK_F64:  return 9;
        default:         return -1;
    }
}

/* ========== TYPE PROMOTION ========== */

HmlTypeKind hml_tk_promote(HmlTypeKind a, HmlTypeKind b) {
    /* If types are the same, no promotion needed */
    if (a == b) return a;

    /* If either is f64, result is f64 */
    if (a == HML_TK_F64 || b == HML_TK_F64) return HML_TK_F64;

    /* f32 with i64/u64 should promote to f64 to preserve precision
     * (f32 has only 24-bit mantissa, i64/u64 need 53+ bits) */
    if (a == HML_TK_F32 || b == HML_TK_F32) {
        HmlTypeKind other = (a == HML_TK_F32) ? b : a;
        if (other == HML_TK_I64 || other == HML_TK_U64) {
            return HML_TK_F64;
        }
        return HML_TK_F32;
    }

    /* Runes promote to i32 when combined with other types */
    if (a == HML_TK_RUNE && b == HML_TK_RUNE) return HML_TK_I32;
    if (a == HML_TK_RUNE) {
        return (hml_tk_priority(HML_TK_I32) >= hml_tk_priority(b)) ? HML_TK_I32 : b;
    }
    if (b == HML_TK_RUNE) {
        return (hml_tk_priority(HML_TK_I32) >= hml_tk_priority(a)) ? HML_TK_I32 : a;
    }

    /* Otherwise, higher priority wins */
    return (hml_tk_priority(a) >= hml_tk_priority(b)) ? a : b;
}

/* ========== TYPE CHECKING ========== */

int hml_tk_is_signed_integer(HmlTypeKind type) {
    return type == HML_TK_I8 || type == HML_TK_I16 ||
           type == HML_TK_I32 || type == HML_TK_I64;
}

int hml_tk_is_unsigned_integer(HmlTypeKind type) {
    return type == HML_TK_U8 || type == HML_TK_U16 ||
           type == HML_TK_U32 || type == HML_TK_U64;
}

int hml_tk_is_integer(HmlTypeKind type) {
    return hml_tk_is_signed_integer(type) || hml_tk_is_unsigned_integer(type);
}

int hml_tk_is_float(HmlTypeKind type) {
    return type == HML_TK_F32 || type == HML_TK_F64;
}

int hml_tk_is_numeric(HmlTypeKind type) {
    return hml_tk_is_integer(type) || hml_tk_is_float(type);
}

/* ========== TYPE SIZEOF ========== */

int hml_tk_sizeof(HmlTypeKind type) {
    switch (type) {
        case HML_TK_BOOL: return 1;
        case HML_TK_I8:   return 1;
        case HML_TK_U8:   return 1;
        case HML_TK_I16:  return 2;
        case HML_TK_U16:  return 2;
        case HML_TK_I32:  return 4;
        case HML_TK_U32:  return 4;
        case HML_TK_F32:  return 4;
        case HML_TK_RUNE: return 4;
        case HML_TK_I64:  return 8;
        case HML_TK_U64:  return 8;
        case HML_TK_F64:  return 8;
        case HML_TK_PTR:  return 8;  /* 64-bit pointers */
        default:         return 0;
    }
}

/* ========== TYPE NAMES ========== */

const char* hml_tk_name(HmlTypeKind type) {
    switch (type) {
        case HML_TK_NULL:   return "null";
        case HML_TK_BOOL:   return "bool";
        case HML_TK_I8:     return "i8";
        case HML_TK_I16:    return "i16";
        case HML_TK_I32:    return "i32";
        case HML_TK_I64:    return "i64";
        case HML_TK_U8:     return "u8";
        case HML_TK_U16:    return "u16";
        case HML_TK_U32:    return "u32";
        case HML_TK_U64:    return "u64";
        case HML_TK_F32:    return "f32";
        case HML_TK_F64:    return "f64";
        case HML_TK_RUNE:   return "rune";
        case HML_TK_STRING: return "string";
        case HML_TK_PTR:    return "ptr";
        default:           return "unknown";
    }
}

HmlTypeKind hml_tk_from_name(const char *name) {
    if (!name) return HML_TK_OTHER;

    if (strcmp(name, "i8") == 0) return HML_TK_I8;
    if (strcmp(name, "i16") == 0) return HML_TK_I16;
    if (strcmp(name, "i32") == 0 || strcmp(name, "integer") == 0) return HML_TK_I32;
    if (strcmp(name, "i64") == 0) return HML_TK_I64;
    if (strcmp(name, "u8") == 0 || strcmp(name, "byte") == 0) return HML_TK_U8;
    if (strcmp(name, "u16") == 0) return HML_TK_U16;
    if (strcmp(name, "u32") == 0) return HML_TK_U32;
    if (strcmp(name, "u64") == 0) return HML_TK_U64;
    if (strcmp(name, "f32") == 0) return HML_TK_F32;
    if (strcmp(name, "f64") == 0 || strcmp(name, "number") == 0) return HML_TK_F64;
    if (strcmp(name, "bool") == 0) return HML_TK_BOOL;
    if (strcmp(name, "rune") == 0) return HML_TK_RUNE;
    if (strcmp(name, "string") == 0) return HML_TK_STRING;
    if (strcmp(name, "ptr") == 0) return HML_TK_PTR;
    if (strcmp(name, "null") == 0) return HML_TK_NULL;

    return HML_TK_OTHER;
}
