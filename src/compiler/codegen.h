/*
 * Hemlock C Code Generator
 *
 * Translates Hemlock AST to C source code.
 */

#ifndef HEMLOCK_CODEGEN_H
#define HEMLOCK_CODEGEN_H

#include "../../include/ast.h"
#include <stdio.h>

// Forward declaration for closure info
typedef struct ClosureInfo ClosureInfo;
typedef struct DeferEntry DeferEntry;

// Deferred expression entry for LIFO execution
struct DeferEntry {
    Expr *expr;           // The expression to defer
    DeferEntry *next;     // Next entry (forms a stack)
};

// Closure information for anonymous functions
struct ClosureInfo {
    char *func_name;        // Generated function name
    char **captured_vars;   // Names of captured variables
    int num_captured;       // Number of captured variables
    Expr *func_expr;        // The function expression
    ClosureInfo *next;      // Linked list of closures
};

// Scope tracking for variable resolution
typedef struct Scope {
    char **vars;            // Variables in this scope
    int num_vars;           // Number of variables
    int capacity;           // Capacity
    struct Scope *parent;   // Parent scope
} Scope;

// Code generation context
typedef struct {
    FILE *output;           // Output file/stream
    int indent;             // Current indentation level
    int temp_counter;       // Counter for temporary variables
    int label_counter;      // Counter for labels
    int func_counter;       // Counter for anonymous functions
    int in_function;        // Whether we're inside a function
    char **local_vars;      // Stack of local variable names
    int num_locals;         // Number of local variables
    int local_capacity;     // Capacity of local vars array

    // Closure support
    Scope *current_scope;   // Current variable scope
    ClosureInfo *closures;  // List of closures to generate
    char **func_params;     // Current function parameters
    int num_func_params;    // Number of current function parameters

    // Defer support
    DeferEntry *defer_stack;  // Stack of deferred expressions (LIFO)
} CodegenContext;

// Initialize code generation context
CodegenContext* codegen_new(FILE *output);

// Free code generation context
void codegen_free(CodegenContext *ctx);

// Generate C code for a complete program
void codegen_program(CodegenContext *ctx, Stmt **stmts, int stmt_count);

// Generate C code for a single statement
void codegen_stmt(CodegenContext *ctx, Stmt *stmt);

// Generate C code for an expression
// Returns the name of the temporary variable holding the result
char* codegen_expr(CodegenContext *ctx, Expr *expr);

// Helper: Generate a new temporary variable name
char* codegen_temp(CodegenContext *ctx);

// Helper: Generate a new label name
char* codegen_label(CodegenContext *ctx);

// Helper: Generate a new anonymous function name
char* codegen_anon_func(CodegenContext *ctx);

// Helper: Write indentation
void codegen_indent(CodegenContext *ctx);

// Helper: Increase/decrease indent level
void codegen_indent_inc(CodegenContext *ctx);
void codegen_indent_dec(CodegenContext *ctx);

// Helper: Write formatted output
void codegen_write(CodegenContext *ctx, const char *fmt, ...);

// Helper: Write a line with indentation
void codegen_writeln(CodegenContext *ctx, const char *fmt, ...);

// Helper: Add a local variable to the tracking list
void codegen_add_local(CodegenContext *ctx, const char *name);

// Helper: Check if a variable is local
int codegen_is_local(CodegenContext *ctx, const char *name);

// Helper: Escape a string for C output
char* codegen_escape_string(const char *str);

// Helper: Get the C operator string for a binary op
const char* codegen_binary_op_str(BinaryOp op);

// Helper: Get the HmlBinaryOp enum name
const char* codegen_hml_binary_op(BinaryOp op);

// Helper: Get the HmlUnaryOp enum name
const char* codegen_hml_unary_op(UnaryOp op);

// ========== SCOPE MANAGEMENT ==========

// Create a new scope
Scope* scope_new(Scope *parent);

// Free a scope
void scope_free(Scope *scope);

// Add a variable to the current scope
void scope_add_var(Scope *scope, const char *name);

// Check if a variable is in the given scope (not parents)
int scope_has_var(Scope *scope, const char *name);

// Check if a variable is defined in scope or any parent
int scope_is_defined(Scope *scope, const char *name);

// Push a new scope onto the stack
void codegen_push_scope(CodegenContext *ctx);

// Pop the current scope
void codegen_pop_scope(CodegenContext *ctx);

// ========== DEFER SUPPORT ==========

// Push a deferred expression onto the defer stack
void codegen_defer_push(CodegenContext *ctx, Expr *expr);

// Generate code to execute all defers in LIFO order (and clear the stack)
void codegen_defer_execute_all(CodegenContext *ctx);

// Clear the defer stack without generating code (for cleanup)
void codegen_defer_clear(CodegenContext *ctx);

// ========== CLOSURE ANALYSIS ==========

// Free variable info for a function
typedef struct {
    char **vars;
    int num_vars;
    int capacity;
} FreeVarSet;

// Find free variables in an expression
void find_free_vars(Expr *expr, Scope *local_scope, FreeVarSet *free_vars);

// Find free variables in a statement
void find_free_vars_stmt(Stmt *stmt, Scope *local_scope, FreeVarSet *free_vars);

// Add a free variable if not already present
void free_var_set_add(FreeVarSet *set, const char *var);

// Create a new free variable set
FreeVarSet* free_var_set_new(void);

// Free a free variable set
void free_var_set_free(FreeVarSet *set);

#endif // HEMLOCK_CODEGEN_H
