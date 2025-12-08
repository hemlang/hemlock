/*
 * Hemlock Bytecode VM - Chunk (Function Prototype)
 *
 * A Chunk represents a compiled function, containing:
 * - Bytecode instructions
 * - Constant pool (literals, strings)
 * - Debug information (line numbers, variable names)
 * - Nested function prototypes (for closures)
 */

#ifndef HEMLOCK_CHUNK_H
#define HEMLOCK_CHUNK_H

#include "bytecode.h"
#include "../../include/interpreter.h"
#include <stdint.h>
#include <stdbool.h>

// Forward declarations
typedef struct Chunk Chunk;
typedef struct Constant Constant;
typedef struct UpvalueDesc UpvalueDesc;
typedef struct LocalVar LocalVar;
typedef struct LineInfo LineInfo;

// ========== Constant Pool ==========

// Constant types (for the constant pool)
typedef enum {
    CONST_NULL,
    CONST_BOOL,
    CONST_I32,
    CONST_I64,
    CONST_F64,
    CONST_STRING,
    CONST_RUNE,
} ConstantType;

// A constant value in the pool
struct Constant {
    ConstantType type;
    union {
        bool as_bool;
        int32_t as_i32;
        int64_t as_i64;
        double as_f64;
        uint32_t as_rune;
        struct {
            char *data;
            int length;
        } as_string;
    } as;
};

// Constant pool
typedef struct {
    Constant *values;
    int count;
    int capacity;
} ConstantPool;

// ========== Upvalue Descriptor ==========

// Describes how to capture an upvalue
struct UpvalueDesc {
    uint8_t index;      // Index in enclosing function's locals or upvalues
    bool is_local;      // true = capture from local, false = capture from upvalue
    char *name;         // Variable name (for debugging)
};

// ========== Local Variable Info ==========

// Local variable descriptor (for debugging)
struct LocalVar {
    char *name;         // Variable name
    int depth;          // Scope depth
    int slot;           // Register slot
    int start_pc;       // First instruction where var is in scope
    int end_pc;         // Last instruction where var is in scope
    bool is_const;      // true if const, false if let
    bool is_captured;   // true if captured by a closure
};

// ========== Debug Line Info ==========

// Line number information (for error reporting)
struct LineInfo {
    int pc;             // Instruction offset
    int line;           // Source line number
};

// ========== Chunk (Function Prototype) ==========

struct Chunk {
    // Identity
    char *name;                 // Function name (NULL for main/anonymous)
    char *source_file;          // Source file path

    // Parameters
    int arity;                  // Number of required parameters
    int num_defaults;           // Number of parameters with defaults
    bool is_variadic;           // true if accepts varargs
    bool is_async;              // true if async function

    // Bytecode
    uint32_t *code;             // Instruction array
    int code_count;
    int code_capacity;

    // Constants
    ConstantPool constants;

    // Upvalues (for closures)
    UpvalueDesc *upvalues;
    int upvalue_count;

    // Nested function prototypes
    Chunk **protos;             // Child function prototypes
    int proto_count;
    int proto_capacity;

    // Local variable info
    int max_stack_size;         // Maximum registers needed
    LocalVar *locals;
    int local_count;
    int local_capacity;

    // Debug info
    LineInfo *lines;
    int line_count;
    int line_capacity;
};

// ========== Chunk API ==========

// Create/destroy
Chunk* chunk_new(const char *name);
void chunk_free(Chunk *chunk);

// Bytecode emission
int chunk_emit(Chunk *chunk, uint32_t instruction, int line);
int chunk_emit_abc(Chunk *chunk, Opcode op, uint8_t a, uint8_t b, uint8_t c, int line);
int chunk_emit_abx(Chunk *chunk, Opcode op, uint8_t a, uint16_t bx, int line);
int chunk_emit_asbx(Chunk *chunk, Opcode op, uint8_t a, int16_t sbx, int line);
int chunk_emit_ax(Chunk *chunk, Opcode op, uint32_t ax, int line);
int chunk_emit_sax(Chunk *chunk, Opcode op, int32_t sax, int line);

// Patch jump instructions
void chunk_patch_jump(Chunk *chunk, int offset, int target);
void chunk_patch_sbx(Chunk *chunk, int offset, int16_t sbx);

// Current position
int chunk_current_offset(Chunk *chunk);

// Constants
int chunk_add_constant_null(Chunk *chunk);
int chunk_add_constant_bool(Chunk *chunk, bool value);
int chunk_add_constant_i32(Chunk *chunk, int32_t value);
int chunk_add_constant_i64(Chunk *chunk, int64_t value);
int chunk_add_constant_f64(Chunk *chunk, double value);
int chunk_add_constant_string(Chunk *chunk, const char *str, int length);
int chunk_add_constant_rune(Chunk *chunk, uint32_t codepoint);

// Get constant
Constant* chunk_get_constant(Chunk *chunk, int index);

// Upvalues
int chunk_add_upvalue(Chunk *chunk, uint8_t index, bool is_local, const char *name);

// Nested prototypes
int chunk_add_proto(Chunk *chunk, Chunk *proto);

// Local variables (debug info)
int chunk_add_local(Chunk *chunk, const char *name, int depth, int slot, bool is_const);
void chunk_mark_local_end(Chunk *chunk, int local_index, int end_pc);
void chunk_mark_local_captured(Chunk *chunk, int local_index);

// Line info
int chunk_get_line(Chunk *chunk, int offset);

// Disassembly (for debugging)
void chunk_disassemble(Chunk *chunk, const char *title);
int chunk_disassemble_instruction(Chunk *chunk, int offset);

// ========== Constant Pool API ==========

void constant_pool_init(ConstantPool *pool);
void constant_pool_free(ConstantPool *pool);
int constant_pool_add(ConstantPool *pool, Constant constant);

#endif // HEMLOCK_CHUNK_H
