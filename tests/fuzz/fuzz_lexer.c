/*
 * Hemlock Lexer Fuzz Harness
 *
 * Fuzz testing for the Hemlock lexer to find crashes, memory issues,
 * and edge cases in tokenization.
 *
 * Supports:
 *   - libFuzzer (LLVM) - compile with clang -fsanitize=fuzzer
 *   - AFL++ - compile with afl-clang-fast
 *   - Standalone mode - for manual testing
 *
 * Build:
 *   libFuzzer: clang -fsanitize=fuzzer,address -g fuzz_lexer.c ...
 *   AFL++:     afl-clang-fast -g fuzz_lexer.c ... -o fuzz_lexer_afl
 *   Standalone: gcc -DFUZZ_STANDALONE -g fuzz_lexer.c ... -o fuzz_lexer
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "lexer.h"

/*
 * Core fuzzing function - exercises the lexer with arbitrary input
 *
 * Returns 0 on success, -1 on error (for standalone mode)
 */
static int fuzz_lexer_input(const uint8_t *data, size_t size) {
    /* Create null-terminated string from input */
    char *source = malloc(size + 1);
    if (!source) {
        return -1;
    }
    memcpy(source, data, size);
    source[size] = '\0';

    /* Initialize lexer */
    Lexer lexer;
    lexer_init(&lexer, source);

    /* Tokenize entire input, consuming all tokens */
    Token token;
    int token_count = 0;
    const int MAX_TOKENS = 1000000;  /* Prevent infinite loops */

    do {
        token = lexer_next(&lexer);
        token_count++;

        /* Free string values allocated by lexer */
        if (token.string_value) {
            free(token.string_value);
        }

        /* Safety limit */
        if (token_count >= MAX_TOKENS) {
            break;
        }
    } while (token.type != TOK_EOF && token.type != TOK_ERROR);

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

    int result = fuzz_lexer_input(data, size);
    free(data);

    if (result == 0) {
        printf("Lexer processed input successfully (%zu bytes)\n", size);
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

    fuzz_lexer_input(data, size);
    return 0;
}
#endif
