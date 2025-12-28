/*
 * Hemlock Bytecode VM - Main Entry Point
 *
 * hemlockvm - Bytecode VM interpreter for Hemlock
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vm.h"
#include "compiler.h"
#include "debug.h"
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "resolver.h"
#include "optimizer.h"
#include "version.h"

// Read entire file into a string
static char* read_file(const char *path) {
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

// Run source code through the bytecode VM
static int run_source(const char *source, bool disassemble, bool trace) {
    // Parse
    Lexer lexer;
    lexer_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    int stmt_count;
    Stmt **statements = parse_program(&parser, &stmt_count);

    if (parser.had_error) {
        fprintf(stderr, "Parse failed!\n");
        return 1;
    }

    // Note: Skip resolver for VM - the VM compiler handles its own variable resolution
    // resolve_program(statements, stmt_count);

    // Optimize AST (constant folding, etc.)
    optimize_program(statements, stmt_count);

    // Compile to bytecode
    Chunk *chunk = compile_program(statements, stmt_count);
    if (!chunk) {
        fprintf(stderr, "Compilation failed!\n");
        return 1;
    }

    // Disassemble if requested
    if (disassemble) {
        disassemble_chunk(chunk, "script");
        chunk_free(chunk);
        return 0;
    }

    // Execute
    VM *vm = vm_new();
    vm_trace_execution(vm, trace);

    VMResult result = vm_run(vm, chunk);

    vm_free(vm);
    chunk_free(chunk);

    // Free AST
    for (int i = 0; i < stmt_count; i++) {
        stmt_free(statements[i]);
    }
    free(statements);

    return (result == VM_OK) ? 0 : 1;
}

// Run file
static int run_file(const char *path, bool disassemble, bool trace) {
    char *source = read_file(path);
    if (!source) {
        return 1;
    }

    int result = run_source(source, disassemble, trace);
    free(source);
    return result;
}

// REPL
static void run_repl(void) {
    printf("Hemlock Bytecode VM %s\n", HEMLOCK_VERSION);
    printf("Type 'exit' to quit.\n\n");

    VM *vm = vm_new();
    char line[1024];

    while (1) {
        printf(">>> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }

        // Remove trailing newline
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        // Check for exit
        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
            break;
        }

        // Skip empty lines
        if (strlen(line) == 0) {
            continue;
        }

        // Parse and run
        Lexer lexer;
        lexer_init(&lexer, line);

        Parser parser;
        parser_init(&parser, &lexer);

        int stmt_count;
        Stmt **statements = parse_program(&parser, &stmt_count);

        if (!parser.had_error && stmt_count > 0) {
            resolve_program(statements, stmt_count);
            optimize_program(statements, stmt_count);

            Chunk *chunk = compile_program(statements, stmt_count);
            if (chunk) {
                vm_reset(vm);
                vm_run(vm, chunk);
                chunk_free(chunk);
            }
        }

        // Free AST
        for (int i = 0; i < stmt_count; i++) {
            stmt_free(statements[i]);
        }
        free(statements);
    }

    vm_free(vm);
}

static void print_usage(const char *program) {
    fprintf(stderr, "Usage: %s [options] [file.hml]\n", program);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  --disasm, -d    Disassemble bytecode instead of running\n");
    fprintf(stderr, "  --trace, -t     Trace execution (debug)\n");
    fprintf(stderr, "  --version, -v   Show version information\n");
    fprintf(stderr, "  --help, -h      Show this help message\n");
    fprintf(stderr, "\nIf no file is provided, starts an interactive REPL.\n");
}

static void print_version(void) {
    printf("Hemlock Bytecode VM %s\n", HEMLOCK_VERSION);
    printf("Build date: %s %s\n", __DATE__, __TIME__);
}

int main(int argc, char **argv) {
    const char *file = NULL;
    bool disassemble = false;
    bool trace = false;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            print_version();
            return 0;
        } else if (strcmp(argv[i], "--disasm") == 0 || strcmp(argv[i], "-d") == 0) {
            disassemble = true;
        } else if (strcmp(argv[i], "--trace") == 0 || strcmp(argv[i], "-t") == 0) {
            trace = true;
        } else if (argv[i][0] != '-') {
            file = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (file) {
        return run_file(file, disassemble, trace);
    } else {
        run_repl();
        return 0;
    }
}
