/*
 * Hemlock Bytecode VM - Chunk Implementation
 *
 * Bytecode container with constant pool and debug info.
 */

#define _POSIX_C_SOURCE 200809L

#include "chunk.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Initial capacities
#define CHUNK_CODE_INITIAL      256
#define CHUNK_CONST_INITIAL     64
#define CHUNK_LINES_INITIAL     64
#define BUILDER_LOCALS_INITIAL  32
#define BUILDER_LOOPS_INITIAL   8

// ============================================
// Chunk Lifecycle
// ============================================

Chunk* chunk_new(void) {
    Chunk *chunk = malloc(sizeof(Chunk));
    if (!chunk) return NULL;

    chunk->code = malloc(CHUNK_CODE_INITIAL);
    chunk->code_count = 0;
    chunk->code_capacity = CHUNK_CODE_INITIAL;

    chunk->constants = malloc(sizeof(Constant) * CHUNK_CONST_INITIAL);
    chunk->const_count = 0;
    chunk->const_capacity = CHUNK_CONST_INITIAL;

    chunk->lines = malloc(sizeof(int) * CHUNK_LINES_INITIAL);
    chunk->lines_count = 0;
    chunk->lines_capacity = CHUNK_LINES_INITIAL;

    chunk->name = NULL;
    chunk->arity = 0;
    chunk->optional_count = 0;
    chunk->has_rest_param = false;
    chunk->is_async = false;

    chunk->upvalues = NULL;
    chunk->upvalue_count = 0;

    chunk->param_types = NULL;
    chunk->return_type = TYPE_ID_NULL;

    chunk->local_count = 0;
    chunk->max_stack = 0;

    return chunk;
}

void chunk_free(Chunk *chunk) {
    if (!chunk) return;

    free(chunk->code);

    // Free constant pool
    for (int i = 0; i < chunk->const_count; i++) {
        Constant *c = &chunk->constants[i];
        if (c->type == CONST_STRING || c->type == CONST_IDENTIFIER) {
            free(c->as.string.data);
        } else if (c->type == CONST_FUNCTION) {
            chunk_free(c->as.function);
        }
    }
    free(chunk->constants);

    free(chunk->lines);
    free(chunk->name);
    free(chunk->upvalues);
    free(chunk->param_types);
    free(chunk);
}

// ============================================
// Bytecode Writing
// ============================================

static void ensure_code_capacity(Chunk *chunk, int needed) {
    if (chunk->code_count + needed <= chunk->code_capacity) return;

    int new_capacity = chunk->code_capacity * 2;
    while (new_capacity < chunk->code_count + needed) {
        new_capacity *= 2;
    }
    chunk->code = realloc(chunk->code, new_capacity);
    chunk->code_capacity = new_capacity;
}

static void ensure_lines_capacity(Chunk *chunk) {
    if (chunk->lines_count + 2 <= chunk->lines_capacity) return;

    chunk->lines_capacity *= 2;
    chunk->lines = realloc(chunk->lines, sizeof(int) * chunk->lines_capacity);
}

void chunk_write_byte(Chunk *chunk, uint8_t byte, int line) {
    ensure_code_capacity(chunk, 1);
    chunk->code[chunk->code_count++] = byte;

    // Run-length encode line numbers
    // Format: [count, line, count, line, ...]
    if (chunk->lines_count >= 2 &&
        chunk->lines[chunk->lines_count - 1] == line) {
        // Same line as previous, increment count
        chunk->lines[chunk->lines_count - 2]++;
    } else {
        // New line
        ensure_lines_capacity(chunk);
        chunk->lines[chunk->lines_count++] = 1;      // count
        chunk->lines[chunk->lines_count++] = line;   // line number
    }
}

void chunk_write_short(Chunk *chunk, uint16_t value, int line) {
    chunk_write_byte(chunk, (value >> 8) & 0xFF, line);
    chunk_write_byte(chunk, value & 0xFF, line);
}

int chunk_write_jump(Chunk *chunk, OpCode op, int line) {
    chunk_write_byte(chunk, op, line);
    chunk_write_byte(chunk, 0xFF, line);  // Placeholder
    chunk_write_byte(chunk, 0xFF, line);  // Placeholder
    return chunk->code_count - 2;  // Return offset to patch
}

void chunk_patch_jump(Chunk *chunk, int offset) {
    // Calculate jump distance from after the jump instruction
    int jump = chunk->code_count - offset - 2;

    if (jump > 0xFFFF) {
        fprintf(stderr, "Error: Jump too large (%d bytes)\n", jump);
        return;
    }

    chunk->code[offset] = (jump >> 8) & 0xFF;
    chunk->code[offset + 1] = jump & 0xFF;
}

// Patch a loop (backward jump)
void chunk_patch_loop(Chunk *chunk, int loop_start) {
    int offset = chunk->code_count - loop_start + 2;

    if (offset > 0xFFFF) {
        fprintf(stderr, "Error: Loop too large (%d bytes)\n", offset);
        return;
    }

    chunk_write_byte(chunk, BC_LOOP, 0);
    chunk_write_byte(chunk, (offset >> 8) & 0xFF, 0);
    chunk_write_byte(chunk, offset & 0xFF, 0);
}

// ============================================
// Constant Pool
// ============================================

static int ensure_const_capacity(Chunk *chunk) {
    if (chunk->const_count < chunk->const_capacity) {
        return 1;
    }

    chunk->const_capacity *= 2;
    chunk->constants = realloc(chunk->constants,
                               sizeof(Constant) * chunk->const_capacity);
    return chunk->constants != NULL;
}

int chunk_add_constant(Chunk *chunk, Constant constant) {
    ensure_const_capacity(chunk);
    chunk->constants[chunk->const_count] = constant;
    return chunk->const_count++;
}

int chunk_add_i32(Chunk *chunk, int32_t value) {
    // Check for existing constant
    for (int i = 0; i < chunk->const_count; i++) {
        Constant *c = &chunk->constants[i];
        if (c->type == CONST_I32 && c->as.i32 == value) {
            return i;
        }
    }

    Constant c = {.type = CONST_I32, .as.i32 = value};
    return chunk_add_constant(chunk, c);
}

int chunk_add_i64(Chunk *chunk, int64_t value) {
    // Check for existing constant
    for (int i = 0; i < chunk->const_count; i++) {
        Constant *c = &chunk->constants[i];
        if (c->type == CONST_I64 && c->as.i64 == value) {
            return i;
        }
    }

    Constant c = {.type = CONST_I64, .as.i64 = value};
    return chunk_add_constant(chunk, c);
}

int chunk_add_f64(Chunk *chunk, double value) {
    // Check for existing constant (careful with NaN comparison)
    for (int i = 0; i < chunk->const_count; i++) {
        Constant *c = &chunk->constants[i];
        if (c->type == CONST_F64 && c->as.f64 == value) {
            return i;
        }
    }

    Constant c = {.type = CONST_F64, .as.f64 = value};
    return chunk_add_constant(chunk, c);
}

// Simple hash function for string interning
static uint32_t hash_string(const char *str, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)str[i];
        hash *= 16777619;
    }
    return hash;
}

int chunk_add_string(Chunk *chunk, const char *str, int length) {
    uint32_t hash = hash_string(str, length);

    // Check for existing constant (string interning)
    for (int i = 0; i < chunk->const_count; i++) {
        Constant *c = &chunk->constants[i];
        if (c->type == CONST_STRING &&
            c->as.string.hash == hash &&
            c->as.string.length == length &&
            memcmp(c->as.string.data, str, length) == 0) {
            return i;
        }
    }

    // Add new string
    char *copy = malloc(length + 1);
    memcpy(copy, str, length);
    copy[length] = '\0';

    Constant c = {
        .type = CONST_STRING,
        .as.string = {.data = copy, .length = length, .hash = hash}
    };
    return chunk_add_constant(chunk, c);
}

int chunk_add_function(Chunk *chunk, Chunk *function) {
    Constant c = {.type = CONST_FUNCTION, .as.function = function};
    return chunk_add_constant(chunk, c);
}

int chunk_add_identifier(Chunk *chunk, const char *name) {
    int length = strlen(name);
    uint32_t hash = hash_string(name, length);

    // Check for existing identifier
    for (int i = 0; i < chunk->const_count; i++) {
        Constant *c = &chunk->constants[i];
        if (c->type == CONST_IDENTIFIER &&
            c->as.string.hash == hash &&
            c->as.string.length == length &&
            memcmp(c->as.string.data, name, length) == 0) {
            return i;
        }
    }

    // Add new identifier
    char *copy = malloc(length + 1);
    memcpy(copy, name, length);
    copy[length] = '\0';

    Constant c = {
        .type = CONST_IDENTIFIER,
        .as.string = {.data = copy, .length = length, .hash = hash}
    };
    return chunk_add_constant(chunk, c);
}

// ============================================
// Query Functions
// ============================================

int chunk_get_line(Chunk *chunk, int offset) {
    // Decode run-length encoded lines
    int current_offset = 0;
    for (int i = 0; i < chunk->lines_count; i += 2) {
        int count = chunk->lines[i];
        int line = chunk->lines[i + 1];
        current_offset += count;
        if (current_offset > offset) {
            return line;
        }
    }
    return 0;  // Unknown line
}

Constant* chunk_get_constant(Chunk *chunk, int index) {
    if (index < 0 || index >= chunk->const_count) {
        return NULL;
    }
    return &chunk->constants[index];
}

// ============================================
// Chunk Builder
// ============================================

ChunkBuilder* chunk_builder_new(ChunkBuilder *enclosing) {
    ChunkBuilder *builder = malloc(sizeof(ChunkBuilder));
    if (!builder) return NULL;

    builder->chunk = chunk_new();
    if (!builder->chunk) {
        free(builder);
        return NULL;
    }

    builder->locals = malloc(sizeof(*builder->locals) * BUILDER_LOCALS_INITIAL);
    builder->local_count = 0;
    builder->local_capacity = BUILDER_LOCALS_INITIAL;
    builder->scope_depth = 0;

    builder->upvalues = NULL;
    builder->upvalue_count = 0;

    builder->loops = malloc(sizeof(*builder->loops) * BUILDER_LOOPS_INITIAL);
    builder->loop_count = 0;
    builder->loop_capacity = BUILDER_LOOPS_INITIAL;

    builder->enclosing = enclosing;

    return builder;
}

void chunk_builder_free(ChunkBuilder *builder) {
    if (!builder) return;

    // Free local names
    for (int i = 0; i < builder->local_count; i++) {
        free(builder->locals[i].name);
    }
    free(builder->locals);

    // Free loop break lists
    for (int i = 0; i < builder->loop_count; i++) {
        free(builder->loops[i].breaks);
    }
    free(builder->loops);

    free(builder->upvalues);
    free(builder);
}

Chunk* chunk_builder_finish(ChunkBuilder *builder) {
    Chunk *chunk = builder->chunk;

    // Transfer upvalue info
    if (builder->upvalue_count > 0) {
        chunk->upvalues = malloc(sizeof(UpvalueDesc) * builder->upvalue_count);
        memcpy(chunk->upvalues, builder->upvalues,
               sizeof(UpvalueDesc) * builder->upvalue_count);
        chunk->upvalue_count = builder->upvalue_count;
    }

    chunk->local_count = builder->local_count;

    // Don't free the chunk, we're returning it
    builder->chunk = NULL;
    chunk_builder_free(builder);

    return chunk;
}

// ============================================
// Scope Management
// ============================================

void builder_begin_scope(ChunkBuilder *builder) {
    builder->scope_depth++;
}

void builder_end_scope(ChunkBuilder *builder) {
    builder->scope_depth--;

    // Pop locals in this scope
    while (builder->local_count > 0 &&
           builder->locals[builder->local_count - 1].depth > builder->scope_depth) {
        // Close upvalues if captured
        if (builder->locals[builder->local_count - 1].is_captured) {
            chunk_write_byte(builder->chunk, BC_CLOSE_UPVALUE, 0);
        } else {
            chunk_write_byte(builder->chunk, BC_POP, 0);
        }
        builder->local_count--;
    }
}

// ============================================
// Local Variable Management
// ============================================

static void ensure_locals_capacity(ChunkBuilder *builder) {
    if (builder->local_count < builder->local_capacity) return;

    builder->local_capacity *= 2;
    builder->locals = realloc(builder->locals,
                              sizeof(*builder->locals) * builder->local_capacity);
}

int builder_declare_local(ChunkBuilder *builder, const char *name, bool is_const, TypeId type) {
    // Check for duplicate in current scope
    for (int i = builder->local_count - 1; i >= 0; i--) {
        if (builder->locals[i].depth < builder->scope_depth) break;
        if (strcmp(builder->locals[i].name, name) == 0) {
            return -1;  // Duplicate
        }
    }

    ensure_locals_capacity(builder);

    int slot = builder->local_count;
    builder->locals[slot].name = strdup(name);
    builder->locals[slot].depth = builder->scope_depth;
    builder->locals[slot].is_captured = false;
    builder->locals[slot].is_const = is_const;
    builder->locals[slot].type = type;
    builder->local_count++;

    return slot;
}

int builder_resolve_local(ChunkBuilder *builder, const char *name) {
    for (int i = builder->local_count - 1; i >= 0; i--) {
        if (strcmp(builder->locals[i].name, name) == 0) {
            return i;
        }
    }
    return -1;  // Not found
}

static int add_upvalue(ChunkBuilder *builder, uint8_t index, bool is_local) {
    // Check for existing upvalue
    for (int i = 0; i < builder->upvalue_count; i++) {
        if (builder->upvalues[i].index == index &&
            builder->upvalues[i].is_local == is_local) {
            return i;
        }
    }

    // Add new upvalue
    if (builder->upvalues == NULL) {
        builder->upvalues = malloc(sizeof(UpvalueDesc) * 8);
    } else if (builder->upvalue_count >= 8) {
        // Grow capacity
        builder->upvalues = realloc(builder->upvalues,
                                    sizeof(UpvalueDesc) * builder->upvalue_count * 2);
    }

    builder->upvalues[builder->upvalue_count].index = index;
    builder->upvalues[builder->upvalue_count].is_local = is_local;
    return builder->upvalue_count++;
}

int builder_resolve_upvalue(ChunkBuilder *builder, const char *name) {
    if (builder->enclosing == NULL) return -1;

    // Look in enclosing function's locals
    int local = builder_resolve_local(builder->enclosing, name);
    if (local != -1) {
        builder->enclosing->locals[local].is_captured = true;
        return add_upvalue(builder, (uint8_t)local, true);
    }

    // Look in enclosing function's upvalues
    int upvalue = builder_resolve_upvalue(builder->enclosing, name);
    if (upvalue != -1) {
        return add_upvalue(builder, (uint8_t)upvalue, false);
    }

    return -1;
}

void builder_mark_initialized(ChunkBuilder *builder) {
    // The most recent local is now initialized
    // (For now, we don't track uninitialized state)
}

// ============================================
// Loop Management
// ============================================

void builder_begin_loop(ChunkBuilder *builder) {
    if (builder->loop_count >= builder->loop_capacity) {
        builder->loop_capacity *= 2;
        builder->loops = realloc(builder->loops,
                                 sizeof(*builder->loops) * builder->loop_capacity);
    }

    int i = builder->loop_count++;
    builder->loops[i].start = builder->chunk->code_count;
    builder->loops[i].scope_depth = builder->scope_depth;
    builder->loops[i].breaks = malloc(sizeof(int) * 8);
    builder->loops[i].break_count = 0;
    builder->loops[i].break_capacity = 8;
}

void builder_end_loop(ChunkBuilder *builder) {
    if (builder->loop_count == 0) return;

    int i = builder->loop_count - 1;

    // Patch all break jumps
    for (int j = 0; j < builder->loops[i].break_count; j++) {
        chunk_patch_jump(builder->chunk, builder->loops[i].breaks[j]);
    }

    free(builder->loops[i].breaks);
    builder->loop_count--;
}

void builder_emit_break(ChunkBuilder *builder) {
    if (builder->loop_count == 0) return;

    int i = builder->loop_count - 1;

    // Pop locals to loop scope
    int depth = builder->loops[i].scope_depth;
    for (int j = builder->local_count - 1; j >= 0 && builder->locals[j].depth > depth; j--) {
        if (builder->locals[j].is_captured) {
            chunk_write_byte(builder->chunk, BC_CLOSE_UPVALUE, 0);
        } else {
            chunk_write_byte(builder->chunk, BC_POP, 0);
        }
    }

    // Emit break as jump (to be patched)
    if (builder->loops[i].break_count >= builder->loops[i].break_capacity) {
        builder->loops[i].break_capacity *= 2;
        builder->loops[i].breaks = realloc(builder->loops[i].breaks,
                                           sizeof(int) * builder->loops[i].break_capacity);
    }
    int offset = chunk_write_jump(builder->chunk, BC_JUMP, 0);
    builder->loops[i].breaks[builder->loops[i].break_count++] = offset;
}

void builder_emit_continue(ChunkBuilder *builder) {
    if (builder->loop_count == 0) return;

    int i = builder->loop_count - 1;

    // Pop locals to loop scope
    int depth = builder->loops[i].scope_depth;
    for (int j = builder->local_count - 1; j >= 0 && builder->locals[j].depth > depth; j--) {
        if (builder->locals[j].is_captured) {
            chunk_write_byte(builder->chunk, BC_CLOSE_UPVALUE, 0);
        } else {
            chunk_write_byte(builder->chunk, BC_POP, 0);
        }
    }

    // Jump back to loop start
    chunk_patch_loop(builder->chunk, builder->loops[i].start);
}
