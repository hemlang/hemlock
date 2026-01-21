/*
 * Hemlock Shared Type Promotion
 *
 * Type promotion rules for binary operations, shared between
 * interpreter and runtime.
 *
 * Both backends map their type enums to the HmlTypeKind enum,
 * use these functions, then map back.
 *
 * Functions use hml_tk_* prefix (TK = Type Kind) to avoid conflicts
 * with backend-specific functions that work with Value types.
 */

#ifndef HEMLOCK_TYPE_PROMOTION_H
#define HEMLOCK_TYPE_PROMOTION_H

#include <stdint.h>

/*
 * Type kind enum for type promotion.
 * Values match common numeric type ordering.
 */
typedef enum {
    HML_TK_NULL = 0,
    HML_TK_BOOL = 1,
    HML_TK_I8 = 2,
    HML_TK_I16 = 3,
    HML_TK_I32 = 4,
    HML_TK_I64 = 5,
    HML_TK_U8 = 6,
    HML_TK_U16 = 7,
    HML_TK_U32 = 8,
    HML_TK_U64 = 9,
    HML_TK_F32 = 10,
    HML_TK_F64 = 11,
    HML_TK_RUNE = 12,
    HML_TK_STRING = 13,
    HML_TK_PTR = 14,
    HML_TK_OTHER = 99,
} HmlTypeKind;

/*
 * Get the priority/rank of a type for promotion rules.
 * Higher number = higher priority in promotions.
 *
 * Returns:
 *   -1 for non-numeric types
 *   0-9 for numeric types (i8 lowest, f64 highest)
 */
int hml_tk_priority(HmlTypeKind type);

/*
 * Determine the result type when combining two types in a binary operation.
 *
 * Rules:
 *   - If either is f64, result is f64
 *   - f32 with i64/u64 promotes to f64 (preserve precision)
 *   - f32 with smaller integers stays f32
 *   - Runes promote to i32 when combined with other types
 *   - Otherwise, higher priority wins
 *
 * Returns the promoted type kind.
 */
HmlTypeKind hml_tk_promote(HmlTypeKind a, HmlTypeKind b);

/*
 * Check if a type kind is a signed integer type.
 */
int hml_tk_is_signed_integer(HmlTypeKind type);

/*
 * Check if a type kind is an unsigned integer type.
 */
int hml_tk_is_unsigned_integer(HmlTypeKind type);

/*
 * Check if a type kind is any integer type (signed or unsigned).
 */
int hml_tk_is_integer(HmlTypeKind type);

/*
 * Check if a type kind is a float type (f32 or f64).
 */
int hml_tk_is_float(HmlTypeKind type);

/*
 * Check if a type kind is any numeric type.
 */
int hml_tk_is_numeric(HmlTypeKind type);

/*
 * Get the size in bytes for a type kind.
 * Returns 0 for non-primitive types.
 */
int hml_tk_sizeof(HmlTypeKind type);

/*
 * Get the type name string for a type kind.
 * Returns a static string - do not free.
 */
const char* hml_tk_name(HmlTypeKind type);

/*
 * Parse a type name string to a type kind.
 * Returns HML_TK_OTHER if not recognized.
 */
HmlTypeKind hml_tk_from_name(const char *name);

#endif /* HEMLOCK_TYPE_PROMOTION_H */
