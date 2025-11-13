#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "interpreter.h"
#include "module.h"

#define HEMLOCK_VERSION "0.1.0"
#define HEMLOCK_BUILD_DATE __DATE__

// FFI functions (from interpreter/ffi.c)
extern void ffi_init(void);
extern void ffi_cleanup(void);

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

    // Initialize FFI
    ffi_init();

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

        // Cleanup FFI
        ffi_cleanup();

        if (result != 0) {
            exit(1);
        }
    } else {
        // Use traditional execution
        run_source(source, argc, argv);
        free(source);

        // Cleanup FFI
        ffi_cleanup();
    }
}

static void run_repl(void) {
    char line[1024];
    Environment *env = env_new(NULL);

    // Create execution context for REPL (persists across lines)
    ExecutionContext *ctx = exec_context_new();

    // Initialize FFI
    ffi_init();

    register_builtins(env, 0, NULL, ctx);

    printf("Hemlock v%s REPL\n", HEMLOCK_VERSION);
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

    // Cleanup FFI
    ffi_cleanup();

    exec_context_free(ctx);
    env_free(env);
}

static void print_version(void) {
    printf("Hemlock version %s (built %s)\n", HEMLOCK_VERSION, HEMLOCK_BUILD_DATE);
    printf("A small, unsafe language for writing unsafe things safely.\n");
}

static void print_help(const char *program) {
    printf("Hemlock - A systems scripting language\n\n");
    printf("USAGE:\n");
    printf("    %s [OPTIONS] [FILE] [ARGS...]\n\n", program);
    printf("ARGUMENTS:\n");
    printf("    <FILE>       Hemlock script file to execute (.hml)\n");
    printf("    <ARGS>...    Arguments passed to the script (available in 'args' array)\n\n");
    printf("OPTIONS:\n");
    printf("    -h, --help       Display this help message\n");
    printf("    -v, --version    Display version information\n");
    printf("    -i, --interactive    Start REPL after executing file\n");
    printf("    -c, --command <CODE>    Execute code string directly\n\n");
    printf("EXAMPLES:\n");
    printf("    %s                     # Start interactive REPL\n", program);
    printf("    %s script.hml          # Run script.hml\n", program);
    printf("    %s script.hml arg1 arg2    # Run script with arguments\n", program);
    printf("    %s -c 'print(\"Hello\");'    # Execute code string\n", program);
    printf("    %s -i script.hml       # Run script then start REPL\n\n", program);
    printf("For more information, visit: https://github.com/nbeerbower/hemlock\n");
}

int main(int argc, char **argv) {
    int interactive_mode = 0;
    const char *file_to_run = NULL;
    const char *command_to_run = NULL;
    int first_script_arg = 0;  // Index of first argument to pass to script

    // Parse command-line flags
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--interactive") == 0) {
            interactive_mode = 1;
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--command") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -c/--command requires a code argument\n");
                fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
                return 1;
            }
            command_to_run = argv[i + 1];
            i++;  // Skip the code argument
        } else if (argv[i][0] == '-') {
            // Unknown flag
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
            return 1;
        } else {
            // First non-flag argument is the file to run
            file_to_run = argv[i];
            first_script_arg = i;
            break;  // Everything after this is passed to the script
        }
    }

    // Execute based on what was specified
    if (command_to_run != NULL) {
        // Execute code string
        ffi_init();
        run_source(command_to_run, 0, NULL);
        ffi_cleanup();

        if (interactive_mode) {
            run_repl();
        }
        return 0;
    }

    if (file_to_run != NULL) {
        // Run file with arguments
        int script_argc = argc - first_script_arg;
        char **script_argv = &argv[first_script_arg];
        run_file(file_to_run, script_argc, script_argv);

        if (interactive_mode) {
            run_repl();
        }
        return 0;
    }

    // No file or command specified - start REPL
    run_repl();
    return 0;
}