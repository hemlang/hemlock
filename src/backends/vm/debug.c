/*
 * Hemlock Bytecode VM - Debug Utilities
 *
 * Disassembler and debugging tools.
 */

#include "debug.h"
#include "instruction.h"
#include <stdio.h>
#include <string.h>

// ============================================
// Constant Printing
// ============================================

void print_constant(Constant *constant) {
    switch (constant->type) {
        case CONST_I32:
            printf("%d", constant->as.i32);
            break;
        case CONST_I64:
            printf("%lld", (long long)constant->as.i64);
            break;
        case CONST_F64:
            printf("%g", constant->as.f64);
            break;
        case CONST_STRING:
            printf("\"%.*s\"", constant->as.string.length, constant->as.string.data);
            break;
        case CONST_IDENTIFIER:
            printf("'%s'", constant->as.string.data);
            break;
        case CONST_FUNCTION:
            if (constant->as.function->name) {
                printf("<fn %s>", constant->as.function->name);
            } else {
                printf("<fn>");
            }
            break;
        default:
            printf("<constant>");
    }
}

// ============================================
// Value Printing (for runtime debugging)
// ============================================

void print_value(Value value) {
    switch (value.type) {
        case VAL_NULL:
            printf("null");
            break;
        case VAL_BOOL:
            printf("%s", value.as.as_bool ? "true" : "false");
            break;
        case VAL_I8:
            printf("%d", value.as.as_i8);
            break;
        case VAL_I16:
            printf("%d", value.as.as_i16);
            break;
        case VAL_I32:
            printf("%d", value.as.as_i32);
            break;
        case VAL_I64:
            printf("%lld", (long long)value.as.as_i64);
            break;
        case VAL_U8:
            printf("%u", value.as.as_u8);
            break;
        case VAL_U16:
            printf("%u", value.as.as_u16);
            break;
        case VAL_U32:
            printf("%u", value.as.as_u32);
            break;
        case VAL_U64:
            printf("%llu", (unsigned long long)value.as.as_u64);
            break;
        case VAL_F32:
            printf("%g", value.as.as_f32);
            break;
        case VAL_F64:
            printf("%g", value.as.as_f64);
            break;
        case VAL_STRING:
            if (value.as.as_string) {
                printf("\"%s\"", value.as.as_string->data);
            } else {
                printf("\"\"");
            }
            break;
        case VAL_RUNE:
            if (value.as.as_rune < 128) {
                printf("'%c'", (char)value.as.as_rune);
            } else {
                printf("'\\u%04X'", value.as.as_rune);
            }
            break;
        case VAL_ARRAY:
            printf("<array>");
            break;
        case VAL_OBJECT:
            printf("<object>");
            break;
        case VAL_FUNCTION:
            printf("<fn>");
            break;
        case VAL_PTR:
            printf("<ptr %p>", value.as.as_ptr);
            break;
        case VAL_BUFFER:
            printf("<buffer>");
            break;
        case VAL_TASK:
            printf("<task>");
            break;
        case VAL_CHANNEL:
            printf("<channel>");
            break;
        case VAL_FILE:
            printf("<file>");
            break;
        default:
            printf("<value>");
    }
}

// ============================================
// Instruction Disassembly
// ============================================

static int simple_instruction(const char *name, int offset) {
    printf("%s\n", name);
    return offset + 1;
}

static int byte_instruction(const char *name, Chunk *chunk, int offset) {
    uint8_t slot = chunk->code[offset + 1];
    printf("%-16s %4d\n", name, slot);
    return offset + 2;
}

static int short_instruction(const char *name, Chunk *chunk, int offset) {
    uint16_t value = (chunk->code[offset + 1] << 8) | chunk->code[offset + 2];
    printf("%-16s %4d\n", name, value);
    return offset + 3;
}

static int constant_instruction(const char *name, Chunk *chunk, int offset) {
    uint16_t index = (chunk->code[offset + 1] << 8) | chunk->code[offset + 2];
    printf("%-16s %4d ", name, index);
    if (index < chunk->const_count) {
        print_constant(&chunk->constants[index]);
    }
    printf("\n");
    return offset + 3;
}

static int jump_instruction(const char *name, int sign, Chunk *chunk, int offset) {
    uint16_t jump = (chunk->code[offset + 1] << 8) | chunk->code[offset + 2];
    int target = offset + 3 + sign * jump;
    printf("%-16s %4d -> %d\n", name, jump, target);
    return offset + 3;
}

static int invoke_instruction(const char *name, Chunk *chunk, int offset) {
    uint16_t index = (chunk->code[offset + 1] << 8) | chunk->code[offset + 2];
    uint8_t argc = chunk->code[offset + 3];
    printf("%-16s %4d ", name, index);
    if (index < chunk->const_count) {
        print_constant(&chunk->constants[index]);
    }
    printf(" (%d args)\n", argc);
    return offset + 4;
}

static int closure_instruction(Chunk *chunk, int offset) {
    uint16_t index = (chunk->code[offset + 1] << 8) | chunk->code[offset + 2];
    uint8_t upvalue_count = chunk->code[offset + 3];

    printf("%-16s %4d ", "CLOSURE", index);
    if (index < chunk->const_count) {
        print_constant(&chunk->constants[index]);
    }
    printf("\n");

    // Print upvalue descriptors
    int current = offset + 4;
    for (int i = 0; i < upvalue_count; i++) {
        uint8_t is_local = chunk->code[current++];
        uint8_t idx = chunk->code[current++];
        printf("      |                     %s %d\n",
               is_local ? "local" : "upvalue", idx);
    }

    return current;
}

int disassemble_instruction(Chunk *chunk, int offset) {
    printf("%04d ", offset);

    // Print line number
    int line = chunk_get_line(chunk, offset);
    if (offset > 0 && line == chunk_get_line(chunk, offset - 1)) {
        printf("   | ");
    } else {
        printf("%4d ", line);
    }

    uint8_t instruction = chunk->code[offset];
    const InstructionInfo *info = instruction_info(instruction);

    switch (instruction) {
        // Simple instructions (no operands)
        case BC_NULL:
        case BC_TRUE:
        case BC_FALSE:
        case BC_ADD:
        case BC_SUB:
        case BC_MUL:
        case BC_DIV:
        case BC_MOD:
        case BC_NEGATE:
        case BC_INC:
        case BC_DEC:
        case BC_ADD_I32:
        case BC_SUB_I32:
        case BC_MUL_I32:
        case BC_EQ:
        case BC_NE:
        case BC_LT:
        case BC_LE:
        case BC_GT:
        case BC_GE:
        case BC_EQ_I32:
        case BC_LT_I32:
        case BC_NOT:
        case BC_BIT_NOT:
        case BC_BIT_AND:
        case BC_BIT_OR:
        case BC_BIT_XOR:
        case BC_LSHIFT:
        case BC_RSHIFT:
        case BC_GET_INDEX:
        case BC_SET_INDEX:
        case BC_CLOSE_UPVALUE:
        case BC_RETURN:
        case BC_APPLY:
        case BC_POP:
        case BC_CATCH:
        case BC_FINALLY:
        case BC_END_TRY:
        case BC_THROW:
        case BC_AWAIT:
        case BC_JOIN:
        case BC_DETACH:
        case BC_CHANNEL:
        case BC_SEND:
        case BC_RECV:
        case BC_SELECT:
        case BC_TYPEOF:
        case BC_NOP:
        case BC_ASSERT:
        case BC_DEBUG_BREAK:
        case BC_HALT:
        case BC_BREAK:
        case BC_CONTINUE:
        case BC_FOR_IN_INIT:
        case BC_GET_SELF:
        case BC_SET_SELF:
            return simple_instruction(info->name, offset);

        // Byte operand instructions
        case BC_CONST_BYTE:
        case BC_GET_LOCAL:
        case BC_SET_LOCAL:
        case BC_GET_UPVALUE:
        case BC_SET_UPVALUE:
        case BC_CALL:
        case BC_TAIL_CALL:
        case BC_SPAWN:
        case BC_PRINT:
        case BC_POPN:
        case BC_CAST:
        case BC_CHECK_TYPE:
            return byte_instruction(info->name, chunk, offset);

        // Short operand instructions (constant pool index)
        case BC_CONST:
        case BC_GET_GLOBAL:
        case BC_SET_GLOBAL:
        case BC_DEFINE_GLOBAL:
        case BC_GET_PROPERTY:
        case BC_SET_PROPERTY:
        case BC_SUPER:
        case BC_DEFER:
        case BC_DEFINE_TYPE:
        case BC_DEFINE_ENUM:
        case BC_ENUM_VALUE:
            return constant_instruction(info->name, chunk, offset);

        // Jump instructions
        case BC_JUMP:
        case BC_JUMP_IF_FALSE:
        case BC_JUMP_IF_TRUE:
        case BC_JUMP_IF_FALSE_POP:
        case BC_COALESCE:
        case BC_OPTIONAL_CHAIN:
        case BC_CASE:
        case BC_FOR_IN_NEXT:
            return jump_instruction(info->name, 1, chunk, offset);

        case BC_LOOP:
            return jump_instruction(info->name, -1, chunk, offset);

        // Array/object construction
        case BC_ARRAY:
        case BC_OBJECT:
        case BC_STRING_INTERP:
        case BC_SWITCH:
            return short_instruction(info->name, chunk, offset);

        // Invoke instructions (idx:16 + argc:8)
        case BC_CALL_METHOD:
        case BC_CALL_BUILTIN:
        case BC_INVOKE:
            return invoke_instruction(info->name, chunk, offset);

        // Closure (special handling for upvalue descriptors)
        case BC_CLOSURE:
            return closure_instruction(chunk, offset);

        // Try (catch:16 + finally:16)
        case BC_TRY: {
            uint16_t catch_offset = (chunk->code[offset + 1] << 8) | chunk->code[offset + 2];
            uint16_t finally_offset = (chunk->code[offset + 3] << 8) | chunk->code[offset + 4];
            printf("%-16s catch->%d finally->%d\n", "TRY",
                   offset + 5 + catch_offset, offset + 5 + finally_offset);
            return offset + 5;
        }

        default:
            printf("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
}

// ============================================
// Chunk Disassembly
// ============================================

void disassemble_chunk(Chunk *chunk, const char *name) {
    printf("== %s ==\n", name);

    // Print function info
    if (chunk->arity > 0 || chunk->optional_count > 0 || chunk->has_rest_param) {
        printf("arity: %d, optional: %d, rest: %s, async: %s\n",
               chunk->arity, chunk->optional_count,
               chunk->has_rest_param ? "yes" : "no",
               chunk->is_async ? "yes" : "no");
    }

    // Print constants
    if (chunk->const_count > 0) {
        printf("-- constants --\n");
        for (int i = 0; i < chunk->const_count; i++) {
            printf("  %4d: ", i);
            print_constant(&chunk->constants[i]);
            printf("\n");
        }
    }

    // Print bytecode
    printf("-- code --\n");
    int offset = 0;
    while (offset < chunk->code_count) {
        offset = disassemble_instruction(chunk, offset);
    }

    // Print nested functions
    for (int i = 0; i < chunk->const_count; i++) {
        if (chunk->constants[i].type == CONST_FUNCTION) {
            Chunk *fn = chunk->constants[i].as.function;
            printf("\n");
            disassemble_chunk(fn, fn->name ? fn->name : "<anonymous>");
        }
    }
}
