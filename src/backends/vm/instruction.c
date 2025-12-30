/*
 * Hemlock Bytecode VM - Instruction Info
 *
 * Metadata for each opcode: name, operand bytes, stack effect.
 */

#include "instruction.h"
#include <stddef.h>
#include <string.h>

// Instruction info table indexed by opcode
static const InstructionInfo info_table[256] = {
    // Category 1: Constants & Literals (0x00-0x0F)
    [BC_CONST]          = {"CONST",          2, 1},
    [BC_CONST_BYTE]     = {"CONST_BYTE",     1, 1},
    [BC_NULL]           = {"NULL",           0, 1},
    [BC_TRUE]           = {"TRUE",           0, 1},
    [BC_FALSE]          = {"FALSE",          0, 1},
    [BC_ARRAY]          = {"ARRAY",          2, STACK_EFFECT_VARIABLE},
    [BC_OBJECT]         = {"OBJECT",         2, STACK_EFFECT_VARIABLE},
    [BC_STRING_INTERP]  = {"STRING_INTERP",  2, STACK_EFFECT_VARIABLE},
    [BC_CLOSURE]        = {"CLOSURE",        3, 1},
    [BC_ENUM_VALUE]     = {"ENUM_VALUE",     2, 1},

    // Category 2: Variables (0x10-0x1F)
    [BC_GET_LOCAL]      = {"GET_LOCAL",      1, 1},
    [BC_SET_LOCAL]      = {"SET_LOCAL",      1, 0},
    [BC_GET_UPVALUE]    = {"GET_UPVALUE",    1, 1},
    [BC_SET_UPVALUE]    = {"SET_UPVALUE",    1, 0},
    [BC_GET_GLOBAL]     = {"GET_GLOBAL",     2, 1},
    [BC_SET_GLOBAL]     = {"SET_GLOBAL",     2, 0},
    [BC_DEFINE_GLOBAL]  = {"DEFINE_GLOBAL",  2, -1},
    [BC_GET_PROPERTY]   = {"GET_PROPERTY",   2, 0},
    [BC_SET_PROPERTY]   = {"SET_PROPERTY",   2, -1},
    [BC_GET_INDEX]      = {"GET_INDEX",      0, -1},
    [BC_SET_INDEX]      = {"SET_INDEX",      0, -2},
    [BC_CLOSE_UPVALUE]  = {"CLOSE_UPVALUE",  0, 0},

    // Category 3: Arithmetic (0x20-0x2F)
    [BC_ADD]            = {"ADD",            0, -1},
    [BC_SUB]            = {"SUB",            0, -1},
    [BC_MUL]            = {"MUL",            0, -1},
    [BC_DIV]            = {"DIV",            0, -1},
    [BC_MOD]            = {"MOD",            0, -1},
    [BC_NEGATE]         = {"NEGATE",         0, 0},
    [BC_INC]            = {"INC",            0, 0},
    [BC_DEC]            = {"DEC",            0, 0},
    [BC_ADD_I32]        = {"ADD_I32",        0, -1},
    [BC_SUB_I32]        = {"SUB_I32",        0, -1},
    [BC_MUL_I32]        = {"MUL_I32",        0, -1},

    // Category 4: Comparison (0x30-0x3F)
    [BC_EQ]             = {"EQ",             0, -1},
    [BC_NE]             = {"NE",             0, -1},
    [BC_LT]             = {"LT",             0, -1},
    [BC_LE]             = {"LE",             0, -1},
    [BC_GT]             = {"GT",             0, -1},
    [BC_GE]             = {"GE",             0, -1},
    [BC_EQ_I32]         = {"EQ_I32",         0, -1},
    [BC_LT_I32]         = {"LT_I32",         0, -1},

    // Category 5: Logical & Bitwise (0x40-0x4F)
    [BC_NOT]            = {"NOT",            0, 0},
    [BC_BIT_NOT]        = {"BIT_NOT",        0, 0},
    [BC_BIT_AND]        = {"BIT_AND",        0, -1},
    [BC_BIT_OR]         = {"BIT_OR",         0, -1},
    [BC_BIT_XOR]        = {"BIT_XOR",        0, -1},
    [BC_LSHIFT]         = {"LSHIFT",         0, -1},
    [BC_RSHIFT]         = {"RSHIFT",         0, -1},
    [BC_COALESCE]       = {"COALESCE",       2, 0},
    [BC_OPTIONAL_CHAIN] = {"OPTIONAL_CHAIN", 2, 0},

    // Category 6: Control Flow (0x50-0x5F)
    [BC_JUMP]           = {"JUMP",           2, 0},
    [BC_JUMP_IF_FALSE]  = {"JUMP_IF_FALSE",  2, -1},
    [BC_JUMP_IF_TRUE]   = {"JUMP_IF_TRUE",   2, -1},
    [BC_JUMP_IF_FALSE_POP] = {"JUMP_IF_FALSE_POP", 2, -1},
    [BC_LOOP]           = {"LOOP",           2, 0},
    [BC_BREAK]          = {"BREAK",          0, 0},
    [BC_CONTINUE]       = {"CONTINUE",       0, 0},
    [BC_SWITCH]         = {"SWITCH",         2, -1},
    [BC_CASE]           = {"CASE",           2, 0},
    [BC_FOR_IN_INIT]    = {"FOR_IN_INIT",    0, 1},
    [BC_FOR_IN_NEXT]    = {"FOR_IN_NEXT",    2, 1},
    [BC_POP]            = {"POP",            0, -1},
    [BC_POPN]           = {"POPN",           1, STACK_EFFECT_VARIABLE},
    [BC_DUP]            = {"DUP",            0, 1},
    [BC_DUP2]           = {"DUP2",           0, 2},
    [BC_SWAP]           = {"SWAP",           0, 0},
    [BC_BURY3]          = {"BURY3",          0, 0},
    [BC_ROT3]           = {"ROT3",           0, 0},

    // Category 7: Functions & Calls (0x60-0x6F)
    [BC_CALL]           = {"CALL",           1, STACK_EFFECT_VARIABLE},
    [BC_CALL_METHOD]    = {"CALL_METHOD",    3, STACK_EFFECT_VARIABLE},
    [BC_CALL_BUILTIN]   = {"CALL_BUILTIN",   3, STACK_EFFECT_VARIABLE},
    [BC_RETURN]         = {"RETURN",         0, 0},
    [BC_APPLY]          = {"APPLY",          0, -1},
    [BC_TAIL_CALL]      = {"TAIL_CALL",      1, STACK_EFFECT_VARIABLE},
    [BC_SUPER]          = {"SUPER",          2, 0},
    [BC_INVOKE]         = {"INVOKE",         3, STACK_EFFECT_VARIABLE},

    // Category 8: Exception Handling (0x70-0x7F)
    [BC_TRY]            = {"TRY",            4, 0},
    [BC_CATCH]          = {"CATCH",          0, 1},
    [BC_FINALLY]        = {"FINALLY",        0, 0},
    [BC_END_TRY]        = {"END_TRY",        0, 0},
    [BC_THROW]          = {"THROW",          0, -1},
    [BC_DEFER]          = {"DEFER",          0, -1},
    [BC_GET_SELF]       = {"GET_SELF",       0, 1},
    [BC_SET_SELF]       = {"SET_SELF",       0, -1},
    [BC_GET_KEY]        = {"GET_KEY",        0, -1},  // [obj, idx] -> [key]
    [BC_SET_OBJ_TYPE]   = {"SET_OBJ_TYPE",   2, 0},   // Set object type name

    // Category 9: Async & Concurrency (0x80-0x8F)
    [BC_SPAWN]          = {"SPAWN",          1, STACK_EFFECT_VARIABLE},
    [BC_AWAIT]          = {"AWAIT",          0, 0},
    [BC_JOIN]           = {"JOIN",           0, 0},
    [BC_DETACH]         = {"DETACH",         0, -1},
    [BC_CHANNEL]        = {"CHANNEL",        0, 0},
    [BC_SEND]           = {"SEND",           0, -2},
    [BC_RECV]           = {"RECV",           0, 0},
    [BC_SELECT]         = {"SELECT",         0, 0},

    // Category 10: Type Operations (0x90-0x9F)
    [BC_TYPEOF]         = {"TYPEOF",         0, 0},
    [BC_CAST]           = {"CAST",           1, 0},
    [BC_CHECK_TYPE]     = {"CHECK_TYPE",     1, 0},
    [BC_DEFINE_TYPE]    = {"DEFINE_TYPE",    2, 0},
    [BC_DEFINE_ENUM]    = {"DEFINE_ENUM",    2, 0},

    // Category 11: Debug & Misc (0xF0-0xFF)
    [BC_NOP]            = {"NOP",            0, 0},
    [BC_PRINT]          = {"PRINT",          1, STACK_EFFECT_VARIABLE},
    [BC_ASSERT]         = {"ASSERT",         0, STACK_EFFECT_VARIABLE},
    [BC_DEBUG_BREAK]    = {"DEBUG_BREAK",    0, 0},
    [BC_HALT]           = {"HALT",           0, 0},
};

const InstructionInfo* instruction_info(OpCode op) {
    if (info_table[op].name == NULL) {
        static const InstructionInfo unknown = {"UNKNOWN", 0, 0};
        return &unknown;
    }
    return &info_table[op];
}

// Get the size of an instruction in bytes (opcode + operands)
int instruction_size(OpCode op) {
    return 1 + instruction_info(op)->operand_bytes;
}

// Builtin name table
static const char* builtin_names[BUILTIN_COUNT] = {
    // Memory (0-10)
    [BUILTIN_ALLOC] = "alloc",
    [BUILTIN_TALLOC] = "talloc",
    [BUILTIN_REALLOC] = "realloc",
    [BUILTIN_FREE] = "free",
    [BUILTIN_MEMSET] = "memset",
    [BUILTIN_MEMCPY] = "memcpy",
    [BUILTIN_SIZEOF] = "sizeof",
    [BUILTIN_BUFFER] = "buffer",
    [BUILTIN_PTR_TO_BUFFER] = "ptr_to_buffer",
    [BUILTIN_BUFFER_PTR] = "buffer_ptr",
    [BUILTIN_PTR_NULL] = "ptr_null",

    // I/O (11-14)
    [BUILTIN_PRINT] = "print",
    [BUILTIN_EPRINT] = "eprint",
    [BUILTIN_READ_LINE] = "read_line",
    [BUILTIN_OPEN] = "open",

    // Type (15-17)
    [BUILTIN_TYPEOF] = "typeof",
    [BUILTIN_ASSERT] = "assert",
    [BUILTIN_PANIC] = "panic",

    // Async (18-24)
    [BUILTIN_SPAWN] = "spawn",
    [BUILTIN_JOIN] = "join",
    [BUILTIN_DETACH] = "detach",
    [BUILTIN_CHANNEL] = "channel",
    [BUILTIN_SELECT] = "select",
    [BUILTIN_TASK_DEBUG_INFO] = "task_debug_info",
    [BUILTIN_APPLY] = "apply",

    // Signal (25-26)
    [BUILTIN_SIGNAL] = "signal",
    [BUILTIN_RAISE] = "raise",

    // Exec (27-28)
    [BUILTIN_EXEC] = "exec",
    [BUILTIN_EXEC_ARGV] = "exec_argv",

    // Pointer read (29-41)
    [BUILTIN_PTR_READ_I8] = "ptr_read_i8",
    [BUILTIN_PTR_READ_I16] = "ptr_read_i16",
    [BUILTIN_PTR_READ_I32] = "ptr_read_i32",
    [BUILTIN_PTR_READ_I64] = "ptr_read_i64",
    [BUILTIN_PTR_READ_U8] = "ptr_read_u8",
    [BUILTIN_PTR_READ_U16] = "ptr_read_u16",
    [BUILTIN_PTR_READ_U32] = "ptr_read_u32",
    [BUILTIN_PTR_READ_U64] = "ptr_read_u64",
    [BUILTIN_PTR_READ_F32] = "ptr_read_f32",
    [BUILTIN_PTR_READ_F64] = "ptr_read_f64",
    [BUILTIN_PTR_READ_PTR] = "ptr_read_ptr",
    [BUILTIN_PTR_OFFSET] = "ptr_offset",
    [BUILTIN_PTR_DEREF_I32] = "ptr_deref_i32",

    // Pointer write (42-52)
    [BUILTIN_PTR_WRITE_I8] = "ptr_write_i8",
    [BUILTIN_PTR_WRITE_I16] = "ptr_write_i16",
    [BUILTIN_PTR_WRITE_I32] = "ptr_write_i32",
    [BUILTIN_PTR_WRITE_I64] = "ptr_write_i64",
    [BUILTIN_PTR_WRITE_U8] = "ptr_write_u8",
    [BUILTIN_PTR_WRITE_U16] = "ptr_write_u16",
    [BUILTIN_PTR_WRITE_U32] = "ptr_write_u32",
    [BUILTIN_PTR_WRITE_U64] = "ptr_write_u64",
    [BUILTIN_PTR_WRITE_F32] = "ptr_write_f32",
    [BUILTIN_PTR_WRITE_F64] = "ptr_write_f64",
    [BUILTIN_PTR_WRITE_PTR] = "ptr_write_ptr",

    // Atomics i32 (53-61)
    [BUILTIN_ATOMIC_LOAD_I32] = "atomic_load_i32",
    [BUILTIN_ATOMIC_STORE_I32] = "atomic_store_i32",
    [BUILTIN_ATOMIC_ADD_I32] = "atomic_add_i32",
    [BUILTIN_ATOMIC_SUB_I32] = "atomic_sub_i32",
    [BUILTIN_ATOMIC_AND_I32] = "atomic_and_i32",
    [BUILTIN_ATOMIC_OR_I32] = "atomic_or_i32",
    [BUILTIN_ATOMIC_XOR_I32] = "atomic_xor_i32",
    [BUILTIN_ATOMIC_CAS_I32] = "atomic_cas_i32",
    [BUILTIN_ATOMIC_EXCHANGE_I32] = "atomic_exchange_i32",

    // Atomics i64 (62-70)
    [BUILTIN_ATOMIC_LOAD_I64] = "atomic_load_i64",
    [BUILTIN_ATOMIC_STORE_I64] = "atomic_store_i64",
    [BUILTIN_ATOMIC_ADD_I64] = "atomic_add_i64",
    [BUILTIN_ATOMIC_SUB_I64] = "atomic_sub_i64",
    [BUILTIN_ATOMIC_AND_I64] = "atomic_and_i64",
    [BUILTIN_ATOMIC_OR_I64] = "atomic_or_i64",
    [BUILTIN_ATOMIC_XOR_I64] = "atomic_xor_i64",
    [BUILTIN_ATOMIC_CAS_I64] = "atomic_cas_i64",
    [BUILTIN_ATOMIC_EXCHANGE_I64] = "atomic_exchange_i64",

    // Atomics misc (71)
    [BUILTIN_ATOMIC_FENCE] = "atomic_fence",

    // Callback (72-73)
    [BUILTIN_CALLBACK] = "callback",
    [BUILTIN_CALLBACK_FREE] = "callback_free",

    // Stack (74-75)
    [BUILTIN_SET_STACK_LIMIT] = "set_stack_limit",
    [BUILTIN_GET_STACK_LIMIT] = "get_stack_limit",

    // DNS/Network (76-78)
    [BUILTIN_DNS_RESOLVE] = "dns_resolve",
    [BUILTIN_SOCKET_CREATE] = "socket_create",
    [BUILTIN_POLL] = "poll",

    // Math (79-80)
    [BUILTIN_DIVI] = "divi",
    [BUILTIN_MODI] = "modi",
};

const char* builtin_name(BuiltinId id) {
    if (id >= 0 && id < BUILTIN_COUNT && builtin_names[id] != NULL) {
        return builtin_names[id];
    }
    return "unknown_builtin";
}

// Lookup builtin ID by name
BuiltinId builtin_lookup(const char *name) {
    for (int i = 0; i < BUILTIN_COUNT; i++) {
        if (builtin_names[i] != NULL && strcmp(builtin_names[i], name) == 0) {
            return (BuiltinId)i;
        }
    }
    return -1;
}

// Type name table
static const char* type_names[] = {
    [TYPE_ID_I8] = "i8",
    [TYPE_ID_I16] = "i16",
    [TYPE_ID_I32] = "i32",
    [TYPE_ID_I64] = "i64",
    [TYPE_ID_U8] = "u8",
    [TYPE_ID_U16] = "u16",
    [TYPE_ID_U32] = "u32",
    [TYPE_ID_U64] = "u64",
    [TYPE_ID_F32] = "f32",
    [TYPE_ID_F64] = "f64",
    [TYPE_ID_BOOL] = "bool",
    [TYPE_ID_STRING] = "string",
    [TYPE_ID_RUNE] = "rune",
    [TYPE_ID_ARRAY] = "array",
    [TYPE_ID_OBJECT] = "object",
    [TYPE_ID_PTR] = "ptr",
    [TYPE_ID_BUFFER] = "buffer",
    [TYPE_ID_NULL] = "null",
    [TYPE_ID_FUNCTION] = "function",
    [TYPE_ID_TASK] = "task",
    [TYPE_ID_CHANNEL] = "channel",
    [TYPE_ID_FILE] = "file",
    [TYPE_ID_ENUM] = "enum",
};

const char* type_id_name(TypeId id) {
    if (id >= 0 && id <= TYPE_ID_ENUM) {
        return type_names[id];
    }
    return "unknown";
}
