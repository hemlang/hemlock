#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "formatter.h"
#include "lexer.h"
#include "parser.h"

// Read entire file into a string (caller must free)
static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Error: Could not open file '%s'\n", path);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);

    char *buffer = malloc(size + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Error: Could not allocate memory for file\n");
        fclose(file);
        return NULL;
    }

    size_t bytes_read = fread(buffer, 1, size, file);
    if (bytes_read < (size_t)size) {
        fprintf(stderr, "Error: Could not read file\n");
        free(buffer);
        fclose(file);
        return NULL;
    }

    buffer[size] = '\0';
    fclose(file);
    return buffer;
}

// Write string to file
static int write_file(const char *path, const char *content) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "Error: Could not open file '%s' for writing\n", path);
        return -1;
    }

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, file);
    fclose(file);

    if (written != len) {
        fprintf(stderr, "Error: Could not write to file '%s'\n", path);
        return -1;
    }

    return 0;
}

// Format source code and return the formatted string
// For now, this just returns a copy of the source (no-op)
char *format_source(const char *source) {
    // Parse to verify valid syntax
    Lexer lexer;
    lexer_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    int stmt_count;
    Stmt **statements = parse_program(&parser, &stmt_count);

    if (parser.had_error) {
        fprintf(stderr, "Format failed: parse errors\n");
        return NULL;
    }

    // TODO: Actually format the AST
    // For now, just return the original source
    char *result = strdup(source);

    // Cleanup AST
    for (int i = 0; i < stmt_count; i++) {
        stmt_free(statements[i]);
    }
    free(statements);

    return result;
}

// Format a file in place
int format_file(const char *path) {
    char *source = read_file(path);
    if (source == NULL) {
        return 1;
    }

    char *formatted = format_source(source);
    free(source);

    if (formatted == NULL) {
        return 1;
    }

    int result = write_file(path, formatted);
    free(formatted);

    return result;
}

// Check if a file is already formatted
int format_check(const char *path) {
    char *source = read_file(path);
    if (source == NULL) {
        return -1;
    }

    char *formatted = format_source(source);
    if (formatted == NULL) {
        free(source);
        return -1;
    }

    int result = strcmp(source, formatted) == 0 ? 0 : 1;

    free(source);
    free(formatted);

    return result;
}
