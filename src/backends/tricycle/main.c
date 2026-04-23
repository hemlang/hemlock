/*
 * Tricycle - Hemlock's Memory Safety Checker
 *
 * Command-line interface for running memory safety analysis.
 *
 * Usage:
 *   tricycle check program.hml          Check a file for memory safety issues
 *   tricycle check --strict program.hml Treat warnings as errors
 *   tricycle check --all program.hml    Show all diagnostics (not just first)
 *   tricycle check --json program.hml   Output diagnostics as JSON
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include "tricycle/tricycle.h"
#include "frontend.h"
#include "compiler/type_check.h"
#include "version.h"

// ========== COMMAND-LINE OPTIONS ==========

typedef struct {
    const char *input_file;
    int strict_mode;        // Treat warnings as errors
    int collect_all;        // Show all diagnostics
    int json_output;        // Output as JSON
    int verbose;            // Verbose output
    int help;               // Show help
    int version;            // Show version
    int type_check;         // Enable type checking first
} Options;

static void print_usage(const char *progname) {
    fprintf(stderr, "Tricycle v%s - Hemlock Memory Safety Checker\n\n", TRICYCLE_VERSION);
    fprintf(stderr, "\"Training wheels for the unsafe. Take them off when you're ready.\"\n\n");
    fprintf(stderr, "Usage: %s [command] [options] <input.hml>\n\n", progname);
    fprintf(stderr, "Commands:\n");
    fprintf(stderr, "  check             Analyze a file for memory safety issues\n\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --strict          Treat warnings as errors\n");
    fprintf(stderr, "  --all             Show all diagnostics (not just first error)\n");
    fprintf(stderr, "  --json            Output diagnostics as JSON (for IDE integration)\n");
    fprintf(stderr, "  --no-type-check   Skip type checking phase\n");
    fprintf(stderr, "  -v, --verbose     Verbose output\n");
    fprintf(stderr, "  -h, --help        Show this help message\n");
    fprintf(stderr, "  --version         Show version\n");
    fprintf(stderr, "\nExamples:\n");
    fprintf(stderr, "  %s check program.hml\n", progname);
    fprintf(stderr, "  %s check --strict --all program.hml\n", progname);
    fprintf(stderr, "  %s check --json program.hml > diagnostics.json\n", progname);
    fprintf(stderr, "\nExit codes:\n");
    fprintf(stderr, "  0  No errors (warnings may exist)\n");
    fprintf(stderr, "  1  Memory safety errors found\n");
    fprintf(stderr, "  2  Invalid arguments or file not found\n");
}

static Options parse_args(int argc, char **argv) {
    Options opts = {
        .input_file = NULL,
        .strict_mode = 0,
        .collect_all = 0,
        .json_output = 0,
        .verbose = 0,
        .help = 0,
        .version = 0,
        .type_check = 1,
    };

    static struct option long_options[] = {
        {"strict",        no_argument, 0, 's'},
        {"all",           no_argument, 0, 'a'},
        {"json",          no_argument, 0, 'j'},
        {"no-type-check", no_argument, 0, 't'},
        {"verbose",       no_argument, 0, 'v'},
        {"help",          no_argument, 0, 'h'},
        {"version",       no_argument, 0, 'V'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    // Skip "check" command if present
    int start_index = 1;
    if (argc > 1 && strcmp(argv[1], "check") == 0) {
        start_index = 2;
    }

    // Reset getopt
    optind = start_index;

    while ((opt = getopt_long(argc, argv, "savhjV", long_options, &option_index)) != -1) {
        switch (opt) {
            case 's':
                opts.strict_mode = 1;
                break;
            case 'a':
                opts.collect_all = 1;
                break;
            case 'j':
                opts.json_output = 1;
                break;
            case 't':
                opts.type_check = 0;
                break;
            case 'v':
                opts.verbose = 1;
                break;
            case 'h':
                opts.help = 1;
                break;
            case 'V':
                opts.version = 1;
                break;
            default:
                break;
        }
    }

    // Get input file
    if (optind < argc) {
        opts.input_file = argv[optind];
    }

    return opts;
}

// Read entire file into string
static char* read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "error: Could not open file '%s'\n", path);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);

    char *buffer = malloc(size + 1);
    if (!buffer) {
        fprintf(stderr, "error: Could not allocate memory for file\n");
        fclose(file);
        return NULL;
    }

    size_t bytes_read = fread(buffer, 1, size, file);
    if ((long)bytes_read < size) {
        fprintf(stderr, "error: Could not read file\n");
        free(buffer);
        fclose(file);
        return NULL;
    }

    buffer[size] = '\0';
    fclose(file);
    return buffer;
}

// ========== MAIN ==========

int main(int argc, char **argv) {
    Options opts = parse_args(argc, argv);

    // Handle help and version
    if (opts.help) {
        print_usage(argv[0]);
        return 0;
    }

    if (opts.version) {
        printf("tricycle %s (Hemlock %s)\n", TRICYCLE_VERSION, HEMLOCK_VERSION);
        return 0;
    }

    // Check for input file
    if (!opts.input_file) {
        fprintf(stderr, "error: No input file specified\n\n");
        print_usage(argv[0]);
        return 2;
    }

    // Read source file
    char *source = read_file(opts.input_file);
    if (!source) {
        return 2;
    }

    if (opts.verbose) {
        fprintf(stderr, "Analyzing: %s\n", opts.input_file);
    }

    // Lex and parse
    Lexer lexer;
    lexer_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    int stmt_count = 0;
    Stmt **statements = parse_program(&parser, &stmt_count);
    if (parser.had_error || !statements) {
        fprintf(stderr, "error: Failed to parse '%s'\n", opts.input_file);
        free(source);
        return 2;
    }

    if (opts.verbose) {
        fprintf(stderr, "Parsed %d statements\n", stmt_count);
    }

    // Resolve variables
    resolve_program(statements, stmt_count);

    // Optional: Run type checker first
    TypeCheckContext *type_ctx = NULL;
    if (opts.type_check) {
        type_ctx = type_check_new(opts.input_file);
        type_check_program(type_ctx, statements, stmt_count);

        if (opts.verbose) {
            fprintf(stderr, "Type check: %d errors, %d warnings\n",
                    type_ctx->error_count, type_ctx->warning_count);
        }
    }

    // Create tricycle context
    TricycleContext *ctx = tricycle_new(opts.input_file);
    if (!ctx) {
        fprintf(stderr, "error: Failed to create analysis context\n");
        if (type_ctx) type_check_free(type_ctx);
        free(source);
        return 2;
    }

    tricycle_set_source(ctx, source);
    if (type_ctx) {
        tricycle_set_type_ctx(ctx, type_ctx);
    }
    ctx->strict_mode = opts.strict_mode;
    ctx->collect_all = opts.collect_all;
    ctx->verbose = opts.verbose;

    // Run analysis
    (void)tricycle_analyze_program(ctx, statements, stmt_count);

    if (opts.verbose) {
        fprintf(stderr, "Analysis complete: %d errors, %d warnings\n",
                ctx->error_count, ctx->warning_count);
    }

    // Output diagnostics
    if (opts.json_output) {
        tricycle_print_diagnostics_json(ctx);
    } else {
        tricycle_print_diagnostics(ctx);
    }

    // Summary
    if (!opts.json_output) {
        if (ctx->error_count > 0 || ctx->warning_count > 0) {
            fprintf(stderr, "\n");
        }
        if (ctx->error_count > 0) {
            fprintf(stderr, "error: aborting due to %d memory safety error%s\n",
                    ctx->error_count, ctx->error_count == 1 ? "" : "s");
        } else if (ctx->warning_count > 0 && opts.strict_mode) {
            fprintf(stderr, "error: %d warning%s (--strict mode)\n",
                    ctx->warning_count, ctx->warning_count == 1 ? "" : "s");
        } else if (ctx->warning_count > 0) {
            fprintf(stderr, "warning: %d warning%s generated\n",
                    ctx->warning_count, ctx->warning_count == 1 ? "" : "s");
        } else {
            if (opts.verbose) {
                fprintf(stderr, "No memory safety issues found.\n");
            }
        }
    }

    // Determine exit code
    int exit_code = 0;
    if (ctx->error_count > 0) {
        exit_code = 1;
    } else if (ctx->warning_count > 0 && opts.strict_mode) {
        exit_code = 1;
    }

    // Cleanup
    tricycle_free(ctx);
    if (type_ctx) type_check_free(type_ctx);

    // Free AST
    for (int i = 0; i < stmt_count; i++) {
        if (statements[i]) {
            stmt_free(statements[i]);
        }
    }
    free(statements);
    free(source);

    return exit_code;
}
