#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "interpreter.h"
#include "module.h"

// Read entire file into a string (caller must free)
static char* read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Error: Could not open file '%s'\n", path);
        return NULL;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    
    // Allocate buffer
    char *buffer = malloc(size + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Error: Could not allocate memory for file\n");
        fclose(file);
        return NULL;
    }
    
    // Read file
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

static void run_source(const char *source, int argc, char **argv) {
    // Parse
    Lexer lexer;
    lexer_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    int stmt_count;
    Stmt **statements = parse_program(&parser, &stmt_count);

    if (parser.had_error) {
        fprintf(stderr, "Parse failed!\n");
        exit(1);
    }

    // Interpret
    Environment *env = env_new(NULL);

    // Create execution context
    ExecutionContext *ctx = exec_context_new();

    register_builtins(env, argc, argv, ctx);

    eval_program(statements, stmt_count, env, ctx);

    // Cleanup
    exec_context_free(ctx);
    env_free(env);
    for (int i = 0; i < stmt_count; i++) {
        stmt_free(statements[i]);
    }
    free(statements);
}

// Check if source contains import or export statements
static int has_modules(const char *source) {
    // Simple check: look for "import " or "export " keywords
    // This is a heuristic - not perfect but good enough
    const char *p = source;
    while (*p) {
        // Skip whitespace
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;

        // Check for import or export at start of line or after whitespace
        if (strncmp(p, "import ", 7) == 0 || strncmp(p, "import{", 7) == 0 ||
            strncmp(p, "export ", 7) == 0 || strncmp(p, "export{", 7) == 0) {
            return 1;
        }

        // Skip to next line
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return 0;
}

static void run_file(const char *path, int argc, char **argv) {
    char *source = read_file(path);
    if (source == NULL) {
        exit(1);
    }

    // Check if file uses modules
    if (has_modules(source)) {
        // Use module system
        ExecutionContext *ctx = exec_context_new();

        // Need to set up builtins in a global environment first
        Environment *global_env = env_new(NULL);
        register_builtins(global_env, argc, argv, ctx);

        // Execute with module system
        int result = execute_file_with_modules(path, global_env, argc, argv, ctx);

        env_free(global_env);
        exec_context_free(ctx);
        free(source);

        if (result != 0) {
            exit(1);
        }
    } else {
        // Use traditional execution
        run_source(source, argc, argv);
        free(source);
    }
}

static void run_repl(void) {
    char line[1024];
    Environment *env = env_new(NULL);

    // Create execution context for REPL (persists across lines)
    ExecutionContext *ctx = exec_context_new();

    register_builtins(env, 0, NULL, ctx);

    printf("Hemlock v0.1 REPL\n");
    printf("Type 'exit' to quit\n\n");

    for (;;) {
        printf(">>> ");

        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }

        // Remove newline
        line[strcspn(line, "\n")] = 0;

        // Check for exit
        if (strcmp(line, "exit") == 0) {
            break;
        }

        // Skip empty lines
        if (strlen(line) == 0) {
            continue;
        }

        // Parse and execute
        Lexer lexer;
        lexer_init(&lexer, line);

        Parser parser;
        parser_init(&parser, &lexer);

        int stmt_count;
        Stmt **statements = parse_program(&parser, &stmt_count);

        if (parser.had_error) {
            continue;
        }

        // Execute
        for (int i = 0; i < stmt_count; i++) {
            eval_stmt(statements[i], env, ctx);
        }

        // Cleanup
        for (int i = 0; i < stmt_count; i++) {
            stmt_free(statements[i]);
        }
        free(statements);
    }

    exec_context_free(ctx);
    env_free(env);
}

static void print_usage(const char *program) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s [file.hml] [args...]    Run a hemlock file with optional arguments\n", program);
    fprintf(stderr, "  %s                          Start REPL\n", program);
}

int main(int argc, char **argv) {
    if (argc == 1) {
        // No arguments - start REPL
        run_repl();
        return 0;
    }

    if (argc >= 2) {
        // Run file with arguments
        // Pass script name and all following args to the Hemlock program
        run_file(argv[1], argc - 1, &argv[1]);
        exit(0);  // Explicitly exit with success code after file execution
    }

    // Should never reach here, but keep for safety
    print_usage(argv[0]);
    return 1;
}