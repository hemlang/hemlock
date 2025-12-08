/*
 * Hemlock Bytecode VM - Opcode Utilities
 */

#include "bytecode.h"
#include <stddef.h>

// Opcode names for debugging/disassembly
const char *opcode_names[] = {
    // Load/Store
    [BC_LOAD_CONST]     = "LOAD_CONST",
    [BC_LOAD_NULL]      = "LOAD_NULL",
    [BC_LOAD_TRUE]      = "LOAD_TRUE",
    [BC_LOAD_FALSE]     = "LOAD_FALSE",
    [BC_MOVE]           = "MOVE",
    [BC_LOAD_LOCAL]     = "LOAD_LOCAL",
    [BC_STORE_LOCAL]    = "STORE_LOCAL",
    [BC_LOAD_UPVALUE]   = "LOAD_UPVALUE",
    [BC_STORE_UPVALUE]  = "STORE_UPVALUE",
    [BC_LOAD_GLOBAL]    = "LOAD_GLOBAL",
    [BC_STORE_GLOBAL]   = "STORE_GLOBAL",

    // Arithmetic
    [BC_ADD]            = "ADD",
    [BC_SUB]            = "SUB",
    [BC_MUL]            = "MUL",
    [BC_DIV]            = "DIV",
    [BC_MOD]            = "MOD",
    [BC_POW]            = "POW",
    [BC_NEG]            = "NEG",

    // Bitwise
    [BC_BAND]           = "BAND",
    [BC_BOR]            = "BOR",
    [BC_BXOR]           = "BXOR",
    [BC_BNOT]           = "BNOT",
    [BC_SHL]            = "SHL",
    [BC_SHR]            = "SHR",

    // Comparison
    [BC_EQ]             = "EQ",
    [BC_NE]             = "NE",
    [BC_LT]             = "LT",
    [BC_LE]             = "LE",
    [BC_GT]             = "GT",
    [BC_GE]             = "GE",

    // Logical
    [BC_NOT]            = "NOT",

    // Control flow
    [BC_JMP]            = "JMP",
    [BC_JMP_IF_FALSE]   = "JMP_IF_FALSE",
    [BC_JMP_IF_TRUE]    = "JMP_IF_TRUE",
    [BC_LOOP]           = "LOOP",

    // Functions
    [BC_CALL]           = "CALL",
    [BC_RETURN]         = "RETURN",
    [BC_CLOSURE]        = "CLOSURE",
    [BC_TAILCALL]       = "TAILCALL",

    // Objects/Arrays
    [BC_NEW_ARRAY]      = "NEW_ARRAY",
    [BC_NEW_OBJECT]     = "NEW_OBJECT",
    [BC_GET_INDEX]      = "GET_INDEX",
    [BC_SET_INDEX]      = "SET_INDEX",
    [BC_GET_FIELD]      = "GET_FIELD",
    [BC_SET_FIELD]      = "SET_FIELD",
    [BC_GET_FIELD_CHAIN] = "GET_FIELD_CHAIN",

    // Type operations
    [BC_TYPEOF]         = "TYPEOF",
    [BC_CAST]           = "CAST",
    [BC_INSTANCEOF]     = "INSTANCEOF",

    // Async
    [BC_SPAWN]          = "SPAWN",
    [BC_AWAIT]          = "AWAIT",
    [BC_YIELD]          = "YIELD",

    // Exception handling
    [BC_THROW]          = "THROW",
    [BC_TRY_BEGIN]      = "TRY_BEGIN",
    [BC_TRY_END]        = "TRY_END",
    [BC_CATCH]          = "CATCH",

    // Defer
    [BC_DEFER_PUSH]     = "DEFER_PUSH",
    [BC_DEFER_POP]      = "DEFER_POP",
    [BC_DEFER_EXEC_ALL] = "DEFER_EXEC_ALL",

    // Increment/Decrement
    [BC_INC]            = "INC",
    [BC_DEC]            = "DEC",

    // String
    [BC_CONCAT]         = "CONCAT",

    // Misc
    [BC_NOP]            = "NOP",
    [BC_PANIC]          = "PANIC",
    [BC_ASSERT]         = "ASSERT",
    [BC_PRINT]          = "PRINT",

    // Module
    [BC_IMPORT]         = "IMPORT",
    [BC_EXPORT]         = "EXPORT",

    // Builtin
    [BC_CALL_BUILTIN]   = "CALL_BUILTIN",
};

const char* opcode_name(Opcode op) {
    if (op >= 0 && op < BC_COUNT) {
        return opcode_names[op];
    }
    return "UNKNOWN";
}

// Instruction format for each opcode
InstrFormat opcode_format(Opcode op) {
    switch (op) {
        // ABC format (3 registers)
        case BC_ADD:
        case BC_SUB:
        case BC_MUL:
        case BC_DIV:
        case BC_MOD:
        case BC_POW:
        case BC_BAND:
        case BC_BOR:
        case BC_BXOR:
        case BC_SHL:
        case BC_SHR:
        case BC_EQ:
        case BC_NE:
        case BC_LT:
        case BC_LE:
        case BC_GT:
        case BC_GE:
        case BC_CALL:
        case BC_TAILCALL:
        case BC_GET_INDEX:
        case BC_SET_INDEX:
        case BC_GET_FIELD:
        case BC_SET_FIELD:
        case BC_GET_FIELD_CHAIN:
        case BC_CAST:
        case BC_INSTANCEOF:
        case BC_SPAWN:
        case BC_CALL_BUILTIN:
            return FMT_ABC;

        // AB format (2 registers)
        case BC_MOVE:
        case BC_NEG:
        case BC_BNOT:
        case BC_NOT:
        case BC_RETURN:
        case BC_NEW_ARRAY:
        case BC_NEW_OBJECT:
        case BC_TYPEOF:
        case BC_AWAIT:
        case BC_ASSERT:
        case BC_CONCAT:
            return FMT_AB;

        // A format (1 register)
        case BC_LOAD_NULL:
        case BC_LOAD_TRUE:
        case BC_LOAD_FALSE:
        case BC_THROW:
        case BC_CATCH:
        case BC_DEFER_PUSH:
        case BC_INC:
        case BC_DEC:
        case BC_PANIC:
        case BC_PRINT:
        case BC_YIELD:
            return FMT_A;

        // ABx format (register + 16-bit unsigned)
        case BC_LOAD_CONST:
        case BC_LOAD_LOCAL:
        case BC_STORE_LOCAL:
        case BC_LOAD_UPVALUE:
        case BC_STORE_UPVALUE:
        case BC_LOAD_GLOBAL:
        case BC_STORE_GLOBAL:
        case BC_CLOSURE:
        case BC_IMPORT:
        case BC_EXPORT:
            return FMT_ABx;

        // AsBx format (register + 16-bit signed)
        case BC_JMP_IF_FALSE:
        case BC_JMP_IF_TRUE:
        case BC_TRY_BEGIN:
            return FMT_AsBx;

        // sAx format (24-bit signed)
        case BC_JMP:
        case BC_LOOP:
            return FMT_sAx;

        // No operands
        case BC_TRY_END:
        case BC_DEFER_POP:
        case BC_DEFER_EXEC_ALL:
        case BC_NOP:
            return FMT_NONE;

        default:
            return FMT_NONE;
    }
}
