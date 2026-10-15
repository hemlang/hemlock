#ifndef HEMLOCK_PARSER_H
#define HEMLOCK_PARSER_H

#include "frontend/lexer.h"
#include "frontend/ast.h"

/* A collected parse diagnostic. Mirrors TypeCheckError/BorrowDiag/LintDiag so
 * tools (`hemlock check`, LSP) can consume all passes uniformly instead of
 * scraping stderr. Only populated when collection is enabled — the default
 * error path still prints to stderr with source context. */
typedef struct ParseError {
    int line;               // 1-based line of the offending token
    int column;             // 1-based column of the offending token
    int length;             // token length in bytes (0 when unknown / at EOF)
    char *message;          // owned; freed by parser_free_errors
    struct ParseError *next;
} ParseError;

// Parser state
typedef struct {
    Lexer *lexer;
    Token current;
    Token previous;
    Token next;          // One-token lookahead for named arguments
    int had_error;
    int panic_mode;
    int parse_depth;     // Current recursion depth (bounds deeply nested input)
    const char *source;  // Source code for error messages
    // Type parameters in scope (for parsing generic type definitions)
    char **type_params;    // Current type parameter names (e.g., ["T", "U"])
    int num_type_params;   // Number of type parameters in scope
    // Structured error collection (for `hemlock check` and tools)
    int collect_errors;     // when set, error_at collects instead of printing
    ParseError *errors;     // collected diagnostics, in source order
    ParseError *errors_tail;
} Parser;

// Public interface
void parser_init(Parser *parser, Lexer *lexer);
Stmt** parse_program(Parser *parser, int *stmt_count);

// Collect diagnostics instead of printing them (for `hemlock check` / tools).
void parser_enable_error_collection(Parser *parser);
void parser_free_errors(Parser *parser);

// Read a source file and parse it into an AST
Stmt** parse_file_to_ast(const char *path, int *stmt_count);

#endif // HEMLOCK_PARSER_H
