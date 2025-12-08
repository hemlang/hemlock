/*
 * Hemlock Bytecode Compiler
 *
 * Compiles AST to bytecode. Single-pass for simple cases,
 * with jump patching for control flow.
 */

#ifndef HEMLOCK_VM_COMPILER_H
#define HEMLOCK_VM_COMPILER_H

#include "chunk.h"
#include "bytecode.h"
#include "../../include/ast.h"
#include <stdbool.h>

// Forward declarations
typedef struct Compiler Compiler;
typedef struct Local Local;
typedef struct Upvalue Upvalue;
typedef struct Loop Loop;
typedef struct TryBlock TryBlock;

// ========== Compiler Structures ==========

// Local variable during compilation
struct Local {
    char *name;
    int depth;          // Scope depth (-1 = uninitialized)
    bool is_const;
    bool is_captured;   // true if captured by closure
};

// Upvalue during compilation
struct Upvalue {
    uint8_t index;
    bool is_local;
};

// Loop context for break/continue
struct Loop {
    int start;          // Loop start offset (for continue)
    int scope_depth;    // Scope depth at loop start
    int *breaks;        // Break jump offsets to patch
    int break_count;
    int break_capacity;
    struct Loop *enclosing;
};

// Try block context
struct TryBlock {
    int try_start;
    int catch_jump;     // Jump to patch when try ends
    int finally_jump;   // Jump to finally block
    bool has_catch;
    bool has_finally;
    struct TryBlock *enclosing;
};

// Function type being compiled
typedef enum {
    FN_TYPE_SCRIPT,     // Top-level script
    FN_TYPE_FUNCTION,   // Named function
    FN_TYPE_CLOSURE,    // Anonymous function/closure
    FN_TYPE_METHOD,     // Object method (future)
} FunctionType;

// Compiler state
struct Compiler {
    // Current chunk being compiled
    Chunk *chunk;

    // Enclosing compiler (for nested functions)
    struct Compiler *enclosing;
    FunctionType fn_type;

    // Local variables
    Local locals[MAX_LOCALS];
    int local_count;
    int scope_depth;

    // Upvalues
    Upvalue upvalues[MAX_UPVALUES];
    int upvalue_count;

    // Register allocation
    int next_register;  // Next available register
    int max_register;   // High water mark

    // Control flow
    Loop *current_loop;
    TryBlock *current_try;

    // Defer stack
    int defer_count;

    // Error state
    bool had_error;
    bool panic_mode;
    char *error_message;

    // Source info
    const char *source_file;
    int current_line;
};

// ========== Compiler API ==========

// Create/destroy compiler
Compiler* compiler_new(const char *source_file);
void compiler_free(Compiler *compiler);

// Compile program
Chunk* compile_program(Compiler *compiler, Stmt **statements, int count);

// Compile a single statement (for REPL)
bool compile_statement(Compiler *compiler, Stmt *stmt);

// Compile expression and leave result in specified register
void compile_expression(Compiler *compiler, Expr *expr, int dest_reg);

// Check for errors
bool compiler_had_error(Compiler *compiler);
const char* compiler_get_error(Compiler *compiler);

// ========== Register Allocation ==========

// Allocate a temporary register
int compiler_alloc_register(Compiler *compiler);

// Free registers back to a previous state
void compiler_free_registers(Compiler *compiler, int to);

// Get current register state (for later freeing)
int compiler_register_state(Compiler *compiler);

// ========== Scope Management ==========

void compiler_begin_scope(Compiler *compiler);
void compiler_end_scope(Compiler *compiler);

// ========== Variable Management ==========

// Declare a local variable
int compiler_declare_local(Compiler *compiler, const char *name, bool is_const);

// Define a local variable (mark as initialized)
void compiler_define_local(Compiler *compiler, int local_index);

// Resolve a local variable
int compiler_resolve_local(Compiler *compiler, const char *name);

// Resolve an upvalue (captured variable)
int compiler_resolve_upvalue(Compiler *compiler, const char *name);

// ========== Helper Functions ==========

// Emit constant load
int compiler_emit_constant(Compiler *compiler, Expr *expr, int dest_reg);

// Emit a simple instruction
void compiler_emit(Compiler *compiler, uint32_t instruction);

// Report error
void compiler_error(Compiler *compiler, const char *message);
void compiler_error_at(Compiler *compiler, int line, const char *message);

#endif // HEMLOCK_VM_COMPILER_H
