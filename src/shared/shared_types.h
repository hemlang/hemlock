/*
 * Hemlock Shared Types Header
 *
 * Defines type constants and enums shared between interpreter and compiler runtime.
 * This provides a unified type system that both backends can use.
 */

#ifndef HEMLOCK_SHARED_TYPES_H
#define HEMLOCK_SHARED_TYPES_H

#include <stdint.h>

/*
 * Shared type enum for type promotion and operations.
 * Both interpreter and runtime can map their own type enums to these values.
 * The values are chosen to match the interpreter's VAL_* enum for simplicity.
 */
typedef enum {
    HML_TYPE_NULL = 0,
    HML_TYPE_BOOL = 1,
    HML_TYPE_I8 = 2,
    HML_TYPE_I16 = 3,
    HML_TYPE_I32 = 4,
    HML_TYPE_I64 = 5,
    HML_TYPE_U8 = 6,
    HML_TYPE_U16 = 7,
    HML_TYPE_U32 = 8,
    HML_TYPE_U64 = 9,
    HML_TYPE_F32 = 10,
    HML_TYPE_F64 = 11,
    HML_TYPE_STRING = 12,
    HML_TYPE_RUNE = 13,
    HML_TYPE_ARRAY = 14,
    HML_TYPE_OBJECT = 15,
    HML_TYPE_FUNCTION = 16,
    HML_TYPE_PTR = 17,
    HML_TYPE_BUFFER = 18,
} HmlSharedType;

/*
 * Binary operation enum shared between backends.
 */
typedef enum {
    HML_BINOP_ADD,
    HML_BINOP_SUB,
    HML_BINOP_MUL,
    HML_BINOP_DIV,
    HML_BINOP_MOD,
    HML_BINOP_LESS,
    HML_BINOP_LESS_EQUAL,
    HML_BINOP_GREATER,
    HML_BINOP_GREATER_EQUAL,
    HML_BINOP_EQUAL,
    HML_BINOP_NOT_EQUAL,
    HML_BINOP_AND,
    HML_BINOP_OR,
    HML_BINOP_BIT_AND,
    HML_BINOP_BIT_OR,
    HML_BINOP_BIT_XOR,
    HML_BINOP_LSHIFT,
    HML_BINOP_RSHIFT,
} HmlBinaryOp;

#endif /* HEMLOCK_SHARED_TYPES_H */
