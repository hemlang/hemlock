/*
 * Hemlock Bytecode VM - Compiler
 *
 * AST to bytecode compiler.
 */

#ifndef HEMLOCK_VM_COMPILER_H
#define HEMLOCK_VM_COMPILER_H

#include "chunk.h"
#include "ast.h"

// Compiler state
typedef struct Compiler {
    ChunkBuilder *builder;      // Current chunk being built
    struct Compiler *enclosing; // Enclosing compiler (for nested functions)

    // Current function info
    char *function_name;
    bool is_async;
    int current_line;

    // Error state
    bool had_error;
    bool panic_mode;

    // Tracking defined globals (to allow shadowing builtins)
    char **defined_globals;
    int num_defined_globals;
    int defined_globals_capacity;
} Compiler;

// Compile a program (list of statements) to bytecode
Chunk* compile_program(Stmt **stmts, int count);

// Compile a single statement
void compile_stmt(Compiler *compiler, Stmt *stmt);

// Compile an expression
void compile_expr(Compiler *compiler, Expr *expr);

// Error reporting
void compiler_error(Compiler *compiler, const char *message);
void compiler_error_at(Compiler *compiler, int line, const char *message);

#endif // HEMLOCK_VM_COMPILER_H
