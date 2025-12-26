/*
 * Hemlock Bytecode VM - Chunk (Bytecode Container)
 *
 * A Chunk represents a compiled unit of bytecode (function, script, etc.)
 * with its constant pool, debug info, and metadata.
 */

#ifndef HEMLOCK_VM_CHUNK_H
#define HEMLOCK_VM_CHUNK_H

#include <stdint.h>
#include <stdbool.h>
#include "instruction.h"

// Forward declarations
typedef struct Value Value;
typedef struct Chunk Chunk;

// ============================================
// Constant Pool Entry
// ============================================

typedef enum {
    CONST_I32,          // 32-bit signed integer
    CONST_I64,          // 64-bit signed integer
    CONST_F64,          // 64-bit float
    CONST_STRING,       // Interned string
    CONST_FUNCTION,     // Compiled function (Chunk*)
    CONST_IDENTIFIER,   // Variable/property name
} ConstantType;

typedef struct {
    ConstantType type;
    union {
        int32_t i32;
        int64_t i64;
        double f64;
        struct {
            char *data;
            int length;
            uint32_t hash;  // For fast comparison
        } string;
        Chunk *function;
    } as;
} Constant;

// ============================================
// Upvalue Description (for closures)
// ============================================

typedef struct {
    uint8_t index;      // Index in enclosing scope
    bool is_local;      // true = local in enclosing, false = upvalue of enclosing
} UpvalueDesc;

// ============================================
// Chunk Structure
// ============================================

struct Chunk {
    // Bytecode
    uint8_t *code;
    int code_count;
    int code_capacity;

    // Constant pool
    Constant *constants;
    int const_count;
    int const_capacity;

    // Line number info (for error messages)
    // Run-length encoded: [count, line, count, line, ...]
    int *lines;
    int lines_count;
    int lines_capacity;

    // Function metadata
    char *name;                 // Function name (NULL for script)
    int arity;                  // Required parameter count
    int optional_count;         // Optional parameter count
    bool has_rest_param;        // Has ...rest parameter
    bool is_async;              // Is async function

    // Closure info
    UpvalueDesc *upvalues;
    int upvalue_count;

    // Type annotations (optional, for runtime checks)
    TypeId *param_types;        // Array of parameter types (NULL = any)
    TypeId return_type;         // Return type (TYPE_ID_NULL = any)

    // Scope info for locals
    int local_count;            // Number of local variable slots needed
    int max_stack;              // Maximum stack depth needed
};

// ============================================
// Chunk Operations
// ============================================

// Create/destroy
Chunk* chunk_new(void);
void chunk_free(Chunk *chunk);

// Write bytecode
void chunk_write_byte(Chunk *chunk, uint8_t byte, int line);
void chunk_write_short(Chunk *chunk, uint16_t value, int line);
int chunk_write_jump(Chunk *chunk, OpCode op, int line);  // Returns offset for patching
void chunk_patch_jump(Chunk *chunk, int offset);

// Add constants (returns index)
int chunk_add_constant(Chunk *chunk, Constant constant);
int chunk_add_i32(Chunk *chunk, int32_t value);
int chunk_add_i64(Chunk *chunk, int64_t value);
int chunk_add_f64(Chunk *chunk, double value);
int chunk_add_string(Chunk *chunk, const char *str, int length);
int chunk_add_function(Chunk *chunk, Chunk *function);
int chunk_add_identifier(Chunk *chunk, const char *name);

// Query
int chunk_get_line(Chunk *chunk, int offset);
Constant* chunk_get_constant(Chunk *chunk, int index);

// ============================================
// Debug/Disassembly
// ============================================

void chunk_disassemble(Chunk *chunk, const char *name);
int chunk_disassemble_instruction(Chunk *chunk, int offset);

// ============================================
// Serialization (for caching compiled bytecode)
// ============================================

// Write chunk to binary format
int chunk_serialize(Chunk *chunk, uint8_t **out_data, size_t *out_size);

// Read chunk from binary format
Chunk* chunk_deserialize(const uint8_t *data, size_t size);

// ============================================
// Chunk Builder (for compiler)
// ============================================

typedef struct {
    Chunk *chunk;

    // Current scope tracking
    struct {
        char *name;
        int depth;
        bool is_captured;   // Captured by closure
        bool is_const;      // Is const variable
        TypeId type;        // Declared type (or TYPE_ID_NULL)
    } *locals;
    int local_count;
    int local_capacity;
    int scope_depth;

    // Upvalue tracking
    UpvalueDesc *upvalues;
    int upvalue_count;

    // Loop tracking (for break/continue)
    struct {
        int start;          // Loop start offset
        int scope_depth;    // Scope depth at loop start
        int *breaks;        // Break jump offsets to patch
        int break_count;
        int break_capacity;
    } *loops;
    int loop_count;
    int loop_capacity;

    // Enclosing function (for closures)
    struct ChunkBuilder *enclosing;

} ChunkBuilder;

ChunkBuilder* chunk_builder_new(ChunkBuilder *enclosing);
void chunk_builder_free(ChunkBuilder *builder);
Chunk* chunk_builder_finish(ChunkBuilder *builder);

// Scope management
void builder_begin_scope(ChunkBuilder *builder);
void builder_end_scope(ChunkBuilder *builder);

// Local variable management
int builder_declare_local(ChunkBuilder *builder, const char *name, bool is_const, TypeId type);
int builder_resolve_local(ChunkBuilder *builder, const char *name);
int builder_resolve_upvalue(ChunkBuilder *builder, const char *name);
void builder_mark_initialized(ChunkBuilder *builder);

// Loop management
void builder_begin_loop(ChunkBuilder *builder);
void builder_end_loop(ChunkBuilder *builder);
void builder_emit_break(ChunkBuilder *builder);
void builder_emit_continue(ChunkBuilder *builder);

#endif // HEMLOCK_VM_CHUNK_H
