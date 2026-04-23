/*
 * Hemlock Parser Fuzz Harness
 *
 * Fuzz testing for the Hemlock parser to find crashes, memory issues,
 * and edge cases in parsing. This exercises both the lexer and parser.
 *
 * Supports:
 *   - libFuzzer (LLVM) - compile with clang -fsanitize=fuzzer
 *   - AFL++ - compile with afl-clang-fast
 *   - Standalone mode - for manual testing
 *
 * Build:
 *   libFuzzer: clang -fsanitize=fuzzer,address -g fuzz_parser.c ...
 *   AFL++:     afl-clang-fast -g fuzz_parser.c ... -o fuzz_parser_afl
 *   Standalone: gcc -DFUZZ_STANDALONE -g fuzz_parser.c ... -o fuzz_parser
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "lexer.h"
#include "parser.h"
#include "ast.h"

/*
 * Core fuzzing function - exercises the parser with arbitrary input
 *
 * Returns 0 on success, -1 on error (for standalone mode)
 */
static int fuzz_parser_input(const uint8_t *data, size_t size) {
    /* Create null-terminated string from input */
    char *source = malloc(size + 1);
    if (!source) {
        return -1;
    }
    memcpy(source, data, size);
    source[size] = '\0';

    /* Initialize lexer and parser */
    Lexer lexer;
    Parser parser;

    lexer_init(&lexer, source);
    parser_init(&parser, &lexer);

    /* Parse the program */
    int stmt_count = 0;
    Stmt **statements = parse_program(&parser, &stmt_count);

    /* Clean up AST if parsing succeeded */
    if (statements) {
        for (int i = 0; i < stmt_count; i++) {
            if (statements[i]) {
                stmt_free(statements[i]);
            }
        }
        free(statements);
    }

    free(source);
    return 0;
}

#ifdef FUZZ_STANDALONE
/*
 * Standalone mode - read from file or stdin for manual testing
 */
int main(int argc, char **argv) {
    FILE *f;
    uint8_t *data;
    size_t size;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        fprintf(stderr, "       %s -        (read from stdin)\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "-") == 0) {
        f = stdin;
    } else {
        f = fopen(argv[1], "rb");
        if (!f) {
            perror("fopen");
            return 1;
        }
    }

    /* Read entire file */
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);

    data = malloc(size);
    if (!data) {
        perror("malloc");
        if (f != stdin) fclose(f);
        return 1;
    }

    if (fread(data, 1, size, f) != size) {
        perror("fread");
        free(data);
        if (f != stdin) fclose(f);
        return 1;
    }

    if (f != stdin) fclose(f);

    int result = fuzz_parser_input(data, size);
    free(data);

    if (result == 0) {
        printf("Parser processed input successfully (%zu bytes)\n", size);
    }

    return result;
}

#else
/*
 * libFuzzer entry point
 */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* Limit input size to prevent OOM */
    if (size > 1024 * 1024) {
        return 0;
    }

    fuzz_parser_input(data, size);
    return 0;
}
#endif
