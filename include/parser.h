#ifndef HEMLOCK_PARSER_H
#define HEMLOCK_PARSER_H

#include "lexer.h"
#include "ast.h"

// Parser state
typedef struct {
    Lexer *lexer;
    Token current;
    Token previous;
    int had_error;
    int panic_mode;
    const char *source;  // Source code for error messages
} Parser;

// Public interface
void parser_init(Parser *parser, Lexer *lexer);
Stmt** parse_program(Parser *parser, int *stmt_count);

#endif // HEMLOCK_PARSER_H
