/*
 * Hemlock Compiler - Type Compatibility
 *
 * Type comparison, assignability checking, and common type computation.
 */

#include "type_check_internal.h"

// ========== TYPE COMPATIBILITY ==========

int type_is_numeric(CheckedType *type) {
    if (!type) return 0;
    switch (type->kind) {
        case CHECKED_I8: case CHECKED_I16: case CHECKED_I32: case CHECKED_I64:
        case CHECKED_U8: case CHECKED_U16: case CHECKED_U32: case CHECKED_U64:
        case CHECKED_F32: case CHECKED_F64:
        case CHECKED_NUMERIC: case CHECKED_INTEGER:
            return 1;
        default:
            return 0;
    }
}

int type_is_integer(CheckedType *type) {
    if (!type) return 0;
    switch (type->kind) {
        case CHECKED_I8: case CHECKED_I16: case CHECKED_I32: case CHECKED_I64:
        case CHECKED_U8: case CHECKED_U16: case CHECKED_U32: case CHECKED_U64:
        case CHECKED_INTEGER:
            return 1;
        default:
            return 0;
    }
}

int type_is_float(CheckedType *type) {
    if (!type) return 0;
    return type->kind == CHECKED_F32 || type->kind == CHECKED_F64;
}

int type_equals(CheckedType *a, CheckedType *b) {
    if (!a || !b) return 0;
    if (a->kind != b->kind) return 0;
    if (a->nullable != b->nullable) return 0;

    if (a->kind == CHECKED_CUSTOM || a->kind == CHECKED_ENUM) {
        if (!a->type_name || !b->type_name) return 0;
        return strcmp(a->type_name, b->type_name) == 0;
    }

    if (a->kind == CHECKED_ARRAY) {
        if (!a->element_type && !b->element_type) return 1;
        if (!a->element_type || !b->element_type) return 1; // Untyped array matches any
        return type_equals(a->element_type, b->element_type);
    }

    return 1;
}

int type_is_assignable(CheckedType *to, CheckedType *from) {
    if (!to || !from) return 1; // Unknown types are permissive

    // ANY type accepts anything
    if (to->kind == CHECKED_ANY || from->kind == CHECKED_ANY) return 1;
    if (to->kind == CHECKED_UNKNOWN || from->kind == CHECKED_UNKNOWN) return 1;

    // Null can be assigned to nullable types
    if (from->kind == CHECKED_NULL) {
        return to->nullable || to->kind == CHECKED_NULL;
    }

    // Exact match
    if (type_equals(to, from)) return 1;

    // Nullable target accepts non-nullable source
    if (to->nullable && !from->nullable) {
        CheckedType temp = *to;
        temp.nullable = 0;
        if (type_equals(&temp, from)) return 1;
    }

    // Numeric conversions - Hemlock allows all numeric conversions at compile time
    // and validates ranges at runtime. This is consistent with Hemlock's dynamic nature.
    if (type_is_numeric(to) && type_is_numeric(from)) {
        return 1;  // All numeric conversions allowed, runtime validates range
    }

    // Rune to integer - rune is a Unicode codepoint (essentially an integer)
    if (type_is_integer(to) && from->kind == CHECKED_RUNE) {
        return 1;  // Rune codepoint to integer is valid
    }

    // Integer to rune - integer values can represent Unicode codepoints
    if (to->kind == CHECKED_RUNE && type_is_integer(from)) {
        return 1;  // Integer to rune codepoint is valid
    }

    // Numeric/rune to bool - truthy conversion (0/0.0 = false, non-zero = true)
    if (to->kind == CHECKED_BOOL && (type_is_numeric(from) || from->kind == CHECKED_RUNE)) {
        return 1;  // Truthy conversion is valid
    }

    // Any value to string - Hemlock supports string coercion for all types
    if (to->kind == CHECKED_STRING) {
        // All basic types can be converted to string
        if (type_is_numeric(from) || from->kind == CHECKED_BOOL ||
            from->kind == CHECKED_RUNE || from->kind == CHECKED_NULL) {
            return 1;  // Value to string coercion is valid
        }
    }

    // Array compatibility
    if (to->kind == CHECKED_ARRAY && from->kind == CHECKED_ARRAY) {
        // Untyped array accepts any array
        if (!to->element_type) return 1;
        if (!from->element_type) return 1; // Untyped source to typed target - runtime check
        return type_is_assignable(to->element_type, from->element_type);
    }

    // Object to custom object (duck typing)
    // Type assignment is allowed; structural validation for object literals is done
    // separately in type_check_validate_object_literal when the source is known.
    // For dynamic sources (e.g., function returns, variables), runtime checks apply.
    if (to->kind == CHECKED_CUSTOM && from->kind == CHECKED_OBJECT) {
        return 1;
    }

    // Custom objects with same name
    if (to->kind == CHECKED_CUSTOM && from->kind == CHECKED_CUSTOM) {
        if (to->type_name && from->type_name) {
            return strcmp(to->type_name, from->type_name) == 0;
        }
    }

    // Compound type handling (A & B & C)
    // For a value to be assigned to a compound type, it must satisfy ALL constituent types
    if (to->kind == CHECKED_COMPOUND) {
        // Check that 'from' is assignable to each constituent type
        for (int i = 0; i < to->num_compound_types; i++) {
            if (!type_is_assignable(to->compound_types[i], from)) {
                return 0;  // Must satisfy all types
            }
        }
        return 1;
    }

    // A compound source can be assigned to a single target if any constituent matches
    if (from->kind == CHECKED_COMPOUND) {
        // Check if any constituent type can be assigned to 'to'
        for (int i = 0; i < from->num_compound_types; i++) {
            if (type_is_assignable(to, from->compound_types[i])) {
                return 1;  // At least one constituent satisfies target
            }
        }
        return 0;
    }

    return 0;
}

CheckedType* type_common(CheckedType *a, CheckedType *b) {
    if (!a) return b ? checked_type_clone(b) : NULL;
    if (!b) return checked_type_clone(a);

    // Same type
    if (type_equals(a, b)) return checked_type_clone(a);

    // ANY absorbs
    if (a->kind == CHECKED_ANY) return checked_type_clone(b);
    if (b->kind == CHECKED_ANY) return checked_type_clone(a);

    // Numeric promotion
    if (type_is_numeric(a) && type_is_numeric(b)) {
        // Float wins over integer
        if (type_is_float(a) || type_is_float(b)) {
            if (a->kind == CHECKED_F64 || b->kind == CHECKED_F64) {
                return checked_type_primitive(CHECKED_F64);
            }
            // f32 with i64/u64 should promote to f64 to preserve precision
            // (f32 has only 24-bit mantissa, i64/u64 need 53+ bits)
            if (a->kind == CHECKED_I64 || a->kind == CHECKED_U64 ||
                b->kind == CHECKED_I64 || b->kind == CHECKED_U64) {
                return checked_type_primitive(CHECKED_F64);
            }
            return checked_type_primitive(CHECKED_F32);
        }

        // Larger integer wins
        int sizes[] = {
            [CHECKED_I8] = 1, [CHECKED_U8] = 1,
            [CHECKED_I16] = 2, [CHECKED_U16] = 2,
            [CHECKED_I32] = 4, [CHECKED_U32] = 4,
            [CHECKED_I64] = 8, [CHECKED_U64] = 8,
        };
        int size_a = (a->kind <= CHECKED_U64) ? sizes[a->kind] : 4;
        int size_b = (b->kind <= CHECKED_U64) ? sizes[b->kind] : 4;

        if (size_a >= size_b) return checked_type_clone(a);
        return checked_type_clone(b);
    }

    // String concatenation
    if (a->kind == CHECKED_STRING || b->kind == CHECKED_STRING) {
        return checked_type_primitive(CHECKED_STRING);
    }

    // Fallback to ANY
    return checked_type_primitive(CHECKED_ANY);
}
