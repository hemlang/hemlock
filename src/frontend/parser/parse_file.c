/**
 * parse_file.c - Shared file-to-AST parsing utility
 *
 * Used by the interpreter module system, compiler, and bundler to read
 * a source file and produce a parsed AST.
 */

#include <stdio.h>
#include <stdlib.h>
#include "frontend/parser.h"
#include "shared/file_io.h"

Stmt** parse_file_to_ast(const char *path, int *stmt_count) {
    char *source = read_file(path);
    if (!source) {
        *stmt_count = 0;
        return NULL;
    }

    Lexer lexer;
    lexer_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    Stmt **statements = parse_program(&parser, stmt_count);

    free(source);

    if (parser.had_error) {
        fprintf(stderr, "error: failed to parse '%s'\n", path);
        *stmt_count = 0;
        return NULL;
    }

    return statements;
}
