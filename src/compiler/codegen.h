/*
 * Hemlock C Code Generator
 *
 * Translates Hemlock AST to C source code.
 */

#ifndef HEMLOCK_CODEGEN_H
#define HEMLOCK_CODEGEN_H

#include "../../include/ast.h"
#include <stdio.h>

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

#endif // HEMLOCK_CODEGEN_H
