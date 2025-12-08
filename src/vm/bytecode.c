/*
 * Hemlock Bytecode VM - Opcode Utilities
 */

#include "bytecode.h"
#include <stddef.h>

// Opcode names for debugging/disassembly
const char *opcode_names[] = {
    // Load/Store
    [OP_LOAD_CONST]     = "LOAD_CONST",
    [OP_LOAD_NULL]      = "LOAD_NULL",
    [OP_LOAD_TRUE]      = "LOAD_TRUE",
    [OP_LOAD_FALSE]     = "LOAD_FALSE",
    [OP_MOVE]           = "MOVE",
    [OP_LOAD_LOCAL]     = "LOAD_LOCAL",
    [OP_STORE_LOCAL]    = "STORE_LOCAL",
    [OP_LOAD_UPVALUE]   = "LOAD_UPVALUE",
    [OP_STORE_UPVALUE]  = "STORE_UPVALUE",
    [OP_LOAD_GLOBAL]    = "LOAD_GLOBAL",
    [OP_STORE_GLOBAL]   = "STORE_GLOBAL",

    // Arithmetic
    [OP_ADD]            = "ADD",
    [OP_SUB]            = "SUB",
    [OP_MUL]            = "MUL",
    [OP_DIV]            = "DIV",
    [OP_MOD]            = "MOD",
    [OP_POW]            = "POW",
    [OP_NEG]            = "NEG",

    // Bitwise
    [OP_BAND]           = "BAND",
    [OP_BOR]            = "BOR",
    [OP_BXOR]           = "BXOR",
    [OP_BNOT]           = "BNOT",
    [OP_SHL]            = "SHL",
    [OP_SHR]            = "SHR",

    // Comparison
    [OP_EQ]             = "EQ",
    [OP_NE]             = "NE",
    [OP_LT]             = "LT",
    [OP_LE]             = "LE",
    [OP_GT]             = "GT",
    [OP_GE]             = "GE",

    // Logical
    [OP_NOT]            = "NOT",

    // Control flow
    [OP_JMP]            = "JMP",
    [OP_JMP_IF_FALSE]   = "JMP_IF_FALSE",
    [OP_JMP_IF_TRUE]    = "JMP_IF_TRUE",
    [OP_LOOP]           = "LOOP",

    // Functions
    [OP_CALL]           = "CALL",
    [OP_RETURN]         = "RETURN",
    [OP_CLOSURE]        = "CLOSURE",
    [OP_TAILCALL]       = "TAILCALL",

    // Objects/Arrays
    [OP_NEW_ARRAY]      = "NEW_ARRAY",
    [OP_NEW_OBJECT]     = "NEW_OBJECT",
    [OP_GET_INDEX]      = "GET_INDEX",
    [OP_SET_INDEX]      = "SET_INDEX",
    [OP_GET_FIELD]      = "GET_FIELD",
    [OP_SET_FIELD]      = "SET_FIELD",
    [OP_GET_FIELD_CHAIN] = "GET_FIELD_CHAIN",

    // Type operations
    [OP_TYPEOF]         = "TYPEOF",
    [OP_CAST]           = "CAST",
    [OP_INSTANCEOF]     = "INSTANCEOF",

    // Async
    [OP_SPAWN]          = "SPAWN",
    [OP_AWAIT]          = "AWAIT",
    [OP_YIELD]          = "YIELD",

    // Exception handling
    [OP_THROW]          = "THROW",
    [OP_TRY_BEGIN]      = "TRY_BEGIN",
    [OP_TRY_END]        = "TRY_END",
    [OP_CATCH]          = "CATCH",

    // Defer
    [OP_DEFER_PUSH]     = "DEFER_PUSH",
    [OP_DEFER_POP]      = "DEFER_POP",
    [OP_DEFER_EXEC_ALL] = "DEFER_EXEC_ALL",

    // Increment/Decrement
    [OP_INC]            = "INC",
    [OP_DEC]            = "DEC",

    // String
    [OP_CONCAT]         = "CONCAT",

    // Misc
    [OP_NOP]            = "NOP",
    [OP_PANIC]          = "PANIC",
    [OP_ASSERT]         = "ASSERT",
    [OP_PRINT]          = "PRINT",

    // Module
    [OP_IMPORT]         = "IMPORT",
    [OP_EXPORT]         = "EXPORT",

    // Builtin
    [OP_CALL_BUILTIN]   = "CALL_BUILTIN",
};

const char* opcode_name(Opcode op) {
    if (op >= 0 && op < OP_COUNT) {
        return opcode_names[op];
    }
    return "UNKNOWN";
}

// Instruction format for each opcode
InstrFormat opcode_format(Opcode op) {
    switch (op) {
        // ABC format (3 registers)
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
        case OP_MOD:
        case OP_POW:
        case OP_BAND:
        case OP_BOR:
        case OP_BXOR:
        case OP_SHL:
        case OP_SHR:
        case OP_EQ:
        case OP_NE:
        case OP_LT:
        case OP_LE:
        case OP_GT:
        case OP_GE:
        case OP_CALL:
        case OP_TAILCALL:
        case OP_GET_INDEX:
        case OP_SET_INDEX:
        case OP_GET_FIELD:
        case OP_SET_FIELD:
        case OP_GET_FIELD_CHAIN:
        case OP_CAST:
        case OP_INSTANCEOF:
        case OP_SPAWN:
        case OP_CALL_BUILTIN:
            return FMT_ABC;

        // AB format (2 registers)
        case OP_MOVE:
        case OP_NEG:
        case OP_BNOT:
        case OP_NOT:
        case OP_RETURN:
        case OP_NEW_ARRAY:
        case OP_NEW_OBJECT:
        case OP_TYPEOF:
        case OP_AWAIT:
        case OP_ASSERT:
        case OP_CONCAT:
            return FMT_AB;

        // A format (1 register)
        case OP_LOAD_NULL:
        case OP_LOAD_TRUE:
        case OP_LOAD_FALSE:
        case OP_THROW:
        case OP_CATCH:
        case OP_DEFER_PUSH:
        case OP_INC:
        case OP_DEC:
        case OP_PANIC:
        case OP_PRINT:
        case OP_YIELD:
            return FMT_A;

        // ABx format (register + 16-bit unsigned)
        case OP_LOAD_CONST:
        case OP_LOAD_LOCAL:
        case OP_STORE_LOCAL:
        case OP_LOAD_UPVALUE:
        case OP_STORE_UPVALUE:
        case OP_LOAD_GLOBAL:
        case OP_STORE_GLOBAL:
        case OP_CLOSURE:
        case OP_IMPORT:
        case OP_EXPORT:
            return FMT_ABx;

        // AsBx format (register + 16-bit signed)
        case OP_JMP_IF_FALSE:
        case OP_JMP_IF_TRUE:
        case OP_TRY_BEGIN:
            return FMT_AsBx;

        // sAx format (24-bit signed)
        case OP_JMP:
        case OP_LOOP:
            return FMT_sAx;

        // No operands
        case OP_TRY_END:
        case OP_DEFER_POP:
        case OP_DEFER_EXEC_ALL:
        case OP_NOP:
            return FMT_NONE;

        default:
            return FMT_NONE;
    }
}
