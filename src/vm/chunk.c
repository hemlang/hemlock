/*
 * Hemlock Bytecode VM - Chunk Implementation
 */

#include "chunk.h"
#include "bytecode.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ========== Constant Pool ==========

void constant_pool_init(ConstantPool *pool) {
    pool->values = NULL;
    pool->count = 0;
    pool->capacity = 0;
}

void constant_pool_free(ConstantPool *pool) {
    for (int i = 0; i < pool->count; i++) {
        if (pool->values[i].type == CONST_STRING) {
            free(pool->values[i].as.as_string.data);
        }
    }
    free(pool->values);
    pool->values = NULL;
    pool->count = 0;
    pool->capacity = 0;
}

int constant_pool_add(ConstantPool *pool, Constant constant) {
    // Check for duplicates (simple linear search - could optimize later)
    for (int i = 0; i < pool->count; i++) {
        Constant *c = &pool->values[i];
        if (c->type != constant.type) continue;

        bool match = false;
        switch (c->type) {
            case CONST_NULL:
                match = true;
                break;
            case CONST_BOOL:
                match = (c->as.as_bool == constant.as.as_bool);
                break;
            case CONST_I32:
                match = (c->as.as_i32 == constant.as.as_i32);
                break;
            case CONST_I64:
                match = (c->as.as_i64 == constant.as.as_i64);
                break;
            case CONST_F64:
                match = (c->as.as_f64 == constant.as.as_f64);
                break;
            case CONST_RUNE:
                match = (c->as.as_rune == constant.as.as_rune);
                break;
            case CONST_STRING:
                if (c->as.as_string.length == constant.as.as_string.length) {
                    match = (memcmp(c->as.as_string.data, constant.as.as_string.data,
                                   constant.as.as_string.length) == 0);
                }
                break;
        }
        if (match) return i;
    }

    // Grow capacity if needed
    if (pool->count >= pool->capacity) {
        int new_capacity = pool->capacity < 8 ? 8 : pool->capacity * 2;
        pool->values = realloc(pool->values, new_capacity * sizeof(Constant));
        pool->capacity = new_capacity;
    }

    pool->values[pool->count] = constant;
    return pool->count++;
}

// ========== Chunk Creation/Destruction ==========

Chunk* chunk_new(const char *name) {
    Chunk *chunk = malloc(sizeof(Chunk));

    chunk->name = name ? strdup(name) : NULL;
    chunk->source_file = NULL;

    chunk->arity = 0;
    chunk->num_defaults = 0;
    chunk->is_variadic = false;
    chunk->is_async = false;

    chunk->code = NULL;
    chunk->code_count = 0;
    chunk->code_capacity = 0;

    constant_pool_init(&chunk->constants);

    chunk->upvalues = NULL;
    chunk->upvalue_count = 0;

    chunk->protos = NULL;
    chunk->proto_count = 0;
    chunk->proto_capacity = 0;

    chunk->max_stack_size = 0;
    chunk->locals = NULL;
    chunk->local_count = 0;
    chunk->local_capacity = 0;

    chunk->lines = NULL;
    chunk->line_count = 0;
    chunk->line_capacity = 0;

    return chunk;
}

void chunk_free(Chunk *chunk) {
    if (!chunk) return;

    free(chunk->name);
    free(chunk->source_file);
    free(chunk->code);

    constant_pool_free(&chunk->constants);

    // Free upvalues
    for (int i = 0; i < chunk->upvalue_count; i++) {
        free(chunk->upvalues[i].name);
    }
    free(chunk->upvalues);

    // Free nested protos (recursive)
    for (int i = 0; i < chunk->proto_count; i++) {
        chunk_free(chunk->protos[i]);
    }
    free(chunk->protos);

    // Free locals
    for (int i = 0; i < chunk->local_count; i++) {
        free(chunk->locals[i].name);
    }
    free(chunk->locals);

    free(chunk->lines);
    free(chunk);
}

// ========== Bytecode Emission ==========

static void ensure_code_capacity(Chunk *chunk, int needed) {
    if (chunk->code_count + needed > chunk->code_capacity) {
        int new_capacity = chunk->code_capacity < 8 ? 8 : chunk->code_capacity * 2;
        while (new_capacity < chunk->code_count + needed) {
            new_capacity *= 2;
        }
        chunk->code = realloc(chunk->code, new_capacity * sizeof(uint32_t));
        chunk->code_capacity = new_capacity;
    }
}

static void add_line_info(Chunk *chunk, int line) {
    // Skip if same line as previous instruction
    if (chunk->line_count > 0 && chunk->lines[chunk->line_count - 1].line == line) {
        return;
    }

    if (chunk->line_count >= chunk->line_capacity) {
        int new_capacity = chunk->line_capacity < 8 ? 8 : chunk->line_capacity * 2;
        chunk->lines = realloc(chunk->lines, new_capacity * sizeof(LineInfo));
        chunk->line_capacity = new_capacity;
    }

    chunk->lines[chunk->line_count].pc = chunk->code_count;
    chunk->lines[chunk->line_count].line = line;
    chunk->line_count++;
}

int chunk_emit(Chunk *chunk, uint32_t instruction, int line) {
    ensure_code_capacity(chunk, 1);
    add_line_info(chunk, line);
    chunk->code[chunk->code_count] = instruction;
    return chunk->code_count++;
}

int chunk_emit_abc(Chunk *chunk, Opcode op, uint8_t a, uint8_t b, uint8_t c, int line) {
    return chunk_emit(chunk, ENCODE_ABC(op, a, b, c), line);
}

int chunk_emit_abx(Chunk *chunk, Opcode op, uint8_t a, uint16_t bx, int line) {
    return chunk_emit(chunk, ENCODE_ABx(op, a, bx), line);
}

int chunk_emit_asbx(Chunk *chunk, Opcode op, uint8_t a, int16_t sbx, int line) {
    return chunk_emit(chunk, ENCODE_AsBx(op, a, sbx), line);
}

int chunk_emit_ax(Chunk *chunk, Opcode op, uint32_t ax, int line) {
    return chunk_emit(chunk, ENCODE_Ax(op, ax), line);
}

int chunk_emit_sax(Chunk *chunk, Opcode op, int32_t sax, int line) {
    return chunk_emit(chunk, ENCODE_sAx(op, sax), line);
}

// ========== Jump Patching ==========

void chunk_patch_jump(Chunk *chunk, int offset, int target) {
    int jump = target - offset - 1;  // -1 because we jump from after the instruction

    uint32_t instr = chunk->code[offset];
    Opcode op = DECODE_OP(instr);

    // Patch based on instruction format
    InstrFormat fmt = opcode_format(op);
    if (fmt == FMT_sAx) {
        chunk->code[offset] = ENCODE_sAx(op, jump);
    } else if (fmt == FMT_AsBx) {
        uint8_t a = DECODE_A(instr);
        chunk->code[offset] = ENCODE_AsBx(op, a, (int16_t)jump);
    }
}

void chunk_patch_sbx(Chunk *chunk, int offset, int16_t sbx) {
    uint32_t instr = chunk->code[offset];
    Opcode op = DECODE_OP(instr);
    uint8_t a = DECODE_A(instr);
    chunk->code[offset] = ENCODE_AsBx(op, a, sbx);
}

int chunk_current_offset(Chunk *chunk) {
    return chunk->code_count;
}

// ========== Constants ==========

int chunk_add_constant_null(Chunk *chunk) {
    Constant c = { .type = CONST_NULL };
    return constant_pool_add(&chunk->constants, c);
}

int chunk_add_constant_bool(Chunk *chunk, bool value) {
    Constant c = { .type = CONST_BOOL, .as.as_bool = value };
    return constant_pool_add(&chunk->constants, c);
}

int chunk_add_constant_i32(Chunk *chunk, int32_t value) {
    Constant c = { .type = CONST_I32, .as.as_i32 = value };
    return constant_pool_add(&chunk->constants, c);
}

int chunk_add_constant_i64(Chunk *chunk, int64_t value) {
    Constant c = { .type = CONST_I64, .as.as_i64 = value };
    return constant_pool_add(&chunk->constants, c);
}

int chunk_add_constant_f64(Chunk *chunk, double value) {
    Constant c = { .type = CONST_F64, .as.as_f64 = value };
    return constant_pool_add(&chunk->constants, c);
}

int chunk_add_constant_string(Chunk *chunk, const char *str, int length) {
    Constant c = {
        .type = CONST_STRING,
        .as.as_string = {
            .data = malloc(length + 1),
            .length = length
        }
    };
    memcpy(c.as.as_string.data, str, length);
    c.as.as_string.data[length] = '\0';
    return constant_pool_add(&chunk->constants, c);
}

int chunk_add_constant_rune(Chunk *chunk, uint32_t codepoint) {
    Constant c = { .type = CONST_RUNE, .as.as_rune = codepoint };
    return constant_pool_add(&chunk->constants, c);
}

Constant* chunk_get_constant(Chunk *chunk, int index) {
    if (index < 0 || index >= chunk->constants.count) {
        return NULL;
    }
    return &chunk->constants.values[index];
}

// ========== Upvalues ==========

int chunk_add_upvalue(Chunk *chunk, uint8_t index, bool is_local, const char *name) {
    // Check for existing upvalue
    for (int i = 0; i < chunk->upvalue_count; i++) {
        UpvalueDesc *uv = &chunk->upvalues[i];
        if (uv->index == index && uv->is_local == is_local) {
            return i;
        }
    }

    // Add new upvalue
    chunk->upvalues = realloc(chunk->upvalues,
                              (chunk->upvalue_count + 1) * sizeof(UpvalueDesc));

    UpvalueDesc *uv = &chunk->upvalues[chunk->upvalue_count];
    uv->index = index;
    uv->is_local = is_local;
    uv->name = name ? strdup(name) : NULL;

    return chunk->upvalue_count++;
}

// ========== Nested Prototypes ==========

int chunk_add_proto(Chunk *chunk, Chunk *proto) {
    if (chunk->proto_count >= chunk->proto_capacity) {
        int new_capacity = chunk->proto_capacity < 4 ? 4 : chunk->proto_capacity * 2;
        chunk->protos = realloc(chunk->protos, new_capacity * sizeof(Chunk*));
        chunk->proto_capacity = new_capacity;
    }

    chunk->protos[chunk->proto_count] = proto;
    return chunk->proto_count++;
}

// ========== Local Variables ==========

int chunk_add_local(Chunk *chunk, const char *name, int depth, int slot, bool is_const) {
    if (chunk->local_count >= chunk->local_capacity) {
        int new_capacity = chunk->local_capacity < 8 ? 8 : chunk->local_capacity * 2;
        chunk->locals = realloc(chunk->locals, new_capacity * sizeof(LocalVar));
        chunk->local_capacity = new_capacity;
    }

    LocalVar *local = &chunk->locals[chunk->local_count];
    local->name = name ? strdup(name) : NULL;
    local->depth = depth;
    local->slot = slot;
    local->start_pc = chunk->code_count;
    local->end_pc = -1;  // Will be set when scope ends
    local->is_const = is_const;
    local->is_captured = false;

    // Track max stack size
    if (slot + 1 > chunk->max_stack_size) {
        chunk->max_stack_size = slot + 1;
    }

    return chunk->local_count++;
}

void chunk_mark_local_end(Chunk *chunk, int local_index, int end_pc) {
    if (local_index >= 0 && local_index < chunk->local_count) {
        chunk->locals[local_index].end_pc = end_pc;
    }
}

void chunk_mark_local_captured(Chunk *chunk, int local_index) {
    if (local_index >= 0 && local_index < chunk->local_count) {
        chunk->locals[local_index].is_captured = true;
    }
}

// ========== Line Info ==========

int chunk_get_line(Chunk *chunk, int offset) {
    // Binary search through line info
    int line = 0;
    for (int i = 0; i < chunk->line_count; i++) {
        if (chunk->lines[i].pc <= offset) {
            line = chunk->lines[i].line;
        } else {
            break;
        }
    }
    return line;
}

// ========== Disassembly ==========

void chunk_disassemble(Chunk *chunk, const char *title) {
    printf("== %s ==\n", title ? title : (chunk->name ? chunk->name : "<anonymous>"));

    // Print function info
    printf("arity: %d, upvalues: %d, locals: %d, max_stack: %d\n",
           chunk->arity, chunk->upvalue_count, chunk->local_count, chunk->max_stack_size);

    // Print constants
    printf("constants (%d):\n", chunk->constants.count);
    for (int i = 0; i < chunk->constants.count; i++) {
        Constant *c = &chunk->constants.values[i];
        printf("  [%3d] ", i);
        switch (c->type) {
            case CONST_NULL:   printf("null\n"); break;
            case CONST_BOOL:   printf("%s\n", c->as.as_bool ? "true" : "false"); break;
            case CONST_I32:    printf("%d (i32)\n", c->as.as_i32); break;
            case CONST_I64:    printf("%lld (i64)\n", (long long)c->as.as_i64); break;
            case CONST_F64:    printf("%g (f64)\n", c->as.as_f64); break;
            case CONST_RUNE:   printf("'%c' (rune U+%04X)\n",
                                     c->as.as_rune < 128 ? (char)c->as.as_rune : '?',
                                     c->as.as_rune); break;
            case CONST_STRING: printf("\"%.*s\"\n", c->as.as_string.length, c->as.as_string.data); break;
        }
    }

    // Print upvalues
    if (chunk->upvalue_count > 0) {
        printf("upvalues (%d):\n", chunk->upvalue_count);
        for (int i = 0; i < chunk->upvalue_count; i++) {
            UpvalueDesc *uv = &chunk->upvalues[i];
            printf("  [%d] %s index=%d %s\n", i,
                   uv->name ? uv->name : "<unnamed>",
                   uv->index,
                   uv->is_local ? "(local)" : "(upvalue)");
        }
    }

    // Print bytecode
    printf("code (%d instructions):\n", chunk->code_count);
    for (int offset = 0; offset < chunk->code_count;) {
        offset = chunk_disassemble_instruction(chunk, offset);
    }

    // Print nested protos
    for (int i = 0; i < chunk->proto_count; i++) {
        printf("\n-- nested proto [%d] --\n", i);
        chunk_disassemble(chunk->protos[i], NULL);
    }
}

int chunk_disassemble_instruction(Chunk *chunk, int offset) {
    printf("%04d ", offset);

    // Print line number
    int line = chunk_get_line(chunk, offset);
    if (offset > 0 && line == chunk_get_line(chunk, offset - 1)) {
        printf("   | ");
    } else {
        printf("%4d ", line);
    }

    uint32_t instr = chunk->code[offset];
    Opcode op = DECODE_OP(instr);

    printf("%-16s ", opcode_name(op));

    InstrFormat fmt = opcode_format(op);
    switch (fmt) {
        case FMT_ABC: {
            uint8_t a = DECODE_A(instr);
            uint8_t b = DECODE_B(instr);
            uint8_t c = DECODE_C(instr);
            printf("R(%d) R(%d) R(%d)", a, b, c);
            break;
        }
        case FMT_AB: {
            uint8_t a = DECODE_A(instr);
            uint8_t b = DECODE_B(instr);
            printf("R(%d) R(%d)", a, b);
            break;
        }
        case FMT_A: {
            uint8_t a = DECODE_A(instr);
            printf("R(%d)", a);
            break;
        }
        case FMT_ABx: {
            uint8_t a = DECODE_A(instr);
            uint16_t bx = DECODE_Bx(instr);
            printf("R(%d) K(%d)", a, bx);
            // Print constant value if it's LOAD_CONST
            if (op == OP_LOAD_CONST && bx < (uint16_t)chunk->constants.count) {
                Constant *c = &chunk->constants.values[bx];
                printf(" ; ");
                switch (c->type) {
                    case CONST_NULL:   printf("null"); break;
                    case CONST_BOOL:   printf("%s", c->as.as_bool ? "true" : "false"); break;
                    case CONST_I32:    printf("%d", c->as.as_i32); break;
                    case CONST_I64:    printf("%lld", (long long)c->as.as_i64); break;
                    case CONST_F64:    printf("%g", c->as.as_f64); break;
                    case CONST_RUNE:   printf("'\\u%04X'", c->as.as_rune); break;
                    case CONST_STRING: printf("\"%.*s\"",
                                             c->as.as_string.length > 20 ? 20 : c->as.as_string.length,
                                             c->as.as_string.data); break;
                }
            }
            break;
        }
        case FMT_AsBx: {
            uint8_t a = DECODE_A(instr);
            int16_t sbx = DECODE_sBx(instr);
            printf("R(%d) %d -> %d", a, sbx, offset + sbx + 1);
            break;
        }
        case FMT_Ax: {
            uint32_t ax = DECODE_Ax(instr);
            printf("%u", ax);
            break;
        }
        case FMT_sAx: {
            int32_t sax = DECODE_sAx(instr);
            printf("%d -> %d", sax, offset + sax + 1);
            break;
        }
        case FMT_NONE:
            break;
    }

    printf("\n");
    return offset + 1;
}
