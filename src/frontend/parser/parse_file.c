/**
 * parse_file.c - Shared file-to-AST parsing utility
 *
 * Used by the interpreter module system, compiler, and bundler to read
 * a source file and produce a parsed AST.
 */

#include <stdio.h>
#include <stdlib.h>
#include "frontend/parser.h"

Stmt** parse_file_to_ast(const char *path, int *stmt_count) {
    FILE *file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "error: cannot open file '%s'\n", path);
        *stmt_count = 0;
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    if (file_size < 0) {
        fprintf(stderr, "error: failed to determine size of '%s'\n", path);
        fclose(file);
        *stmt_count = 0;
        return NULL;
    }
    fseek(file, 0, SEEK_SET);

    char *source = malloc((size_t)file_size + 1);
    if (!source) {
        fprintf(stderr, "error: memory allocation failed for '%s' (%ld bytes)\n", path, file_size);
        fclose(file);
        *stmt_count = 0;
        return NULL;
    }
    if (fread(source, 1, (size_t)file_size, file) != (size_t)file_size) {
        fprintf(stderr, "error: failed to read '%s'\n", path);
        free(source);
        fclose(file);
        *stmt_count = 0;
        return NULL;
    }
    source[file_size] = '\0';
    fclose(file);

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
