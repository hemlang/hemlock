/*
 * Hemlock Compiler (hemlockc)
 *
 * Compiles Hemlock source code to C, then optionally invokes
 * the C compiler to produce an executable.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <limits.h>
#include "../../include/lexer.h"
#include "../../include/parser.h"
#include "../../include/ast.h"
#include "codegen.h"

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#define HEMLOCKC_VERSION "0.1.0"

// Get directory containing the hemlockc executable (cross-platform)
static char* get_self_dir(void) {
    static char path[PATH_MAX];

#ifdef __APPLE__
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0) {
        return NULL;
    }
    // Resolve symlinks
    char *real = realpath(path, NULL);
    if (real) {
        strncpy(path, real, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        free(real);
    }
#elif defined(__linux__)
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len == -1) {
        return NULL;
    }
    path[len] = '\0';
#else
    // Fallback: try /proc/self/exe anyway
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len == -1) {
        return NULL;
    }
    path[len] = '\0';
#endif

    // Find last slash and truncate to get directory
    char *last_slash = strrchr(path, '/');
    if (last_slash) {
        *last_slash = '\0';
    }
    return path;
}

// Command-line options
typedef struct {
    const char *input_file;
    const char *output_file;
    const char *c_output;        // C source output (for -c option)
    int emit_c_only;             // Only emit C, don't compile
    int verbose;                 // Verbose output
    int keep_c;                  // Keep generated C file
    int optimize;                // Optimization level (0, 1, 2, 3)
    const char *cc;              // C compiler to use
    const char *runtime_path;    // Path to runtime library
} Options;

static void print_usage(const char *progname) {
    fprintf(stderr, "Hemlock Compiler v%s\n\n", HEMLOCKC_VERSION);
    fprintf(stderr, "Usage: %s [options] <input.hml>\n\n", progname);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -o <file>     Output executable name (default: a.out)\n");
    fprintf(stderr, "  -c            Emit C code only (don't compile)\n");
    fprintf(stderr, "  --emit-c <f>  Write generated C to file\n");
    fprintf(stderr, "  -k, --keep-c  Keep generated C file after compilation\n");
    fprintf(stderr, "  -O<level>     Optimization level (0-3, default: 0)\n");
    fprintf(stderr, "  --cc <path>   C compiler to use (default: gcc)\n");
    fprintf(stderr, "  --runtime <p> Path to runtime library\n");
    fprintf(stderr, "  -v, --verbose Verbose output\n");
    fprintf(stderr, "  -h, --help    Show this help message\n");
    fprintf(stderr, "  --version     Show version\n");
}

static Options parse_args(int argc, char **argv) {
    Options opts = {
        .input_file = NULL,
        .output_file = "a.out",
        .c_output = NULL,
        .emit_c_only = 0,
        .verbose = 0,
        .keep_c = 0,
        .optimize = 0,
        .cc = "gcc",
        .runtime_path = NULL
    };

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "--version") == 0) {
            printf("hemlockc %s\n", HEMLOCKC_VERSION);
            exit(0);
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            opts.output_file = argv[++i];
        } else if (strcmp(argv[i], "-c") == 0) {
            opts.emit_c_only = 1;
        } else if (strcmp(argv[i], "--emit-c") == 0 && i + 1 < argc) {
            opts.c_output = argv[++i];
        } else if (strcmp(argv[i], "-k") == 0 || strcmp(argv[i], "--keep-c") == 0) {
            opts.keep_c = 1;
        } else if (strncmp(argv[i], "-O", 2) == 0) {
            opts.optimize = atoi(argv[i] + 2);
            if (opts.optimize < 0) opts.optimize = 0;
            if (opts.optimize > 3) opts.optimize = 3;
        } else if (strcmp(argv[i], "--cc") == 0 && i + 1 < argc) {
            opts.cc = argv[++i];
        } else if (strcmp(argv[i], "--runtime") == 0 && i + 1 < argc) {
            opts.runtime_path = argv[++i];
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            opts.verbose = 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            exit(1);
        } else {
            if (opts.input_file != NULL) {
                fprintf(stderr, "Multiple input files not supported\n");
                exit(1);
            }
            opts.input_file = argv[i];
        }
    }

    if (opts.input_file == NULL) {
        fprintf(stderr, "No input file specified\n");
        print_usage(argv[0]);
        exit(1);
    }

    return opts;
}

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

// Generate C output filename from input filename
static char* make_c_filename(const char *input) {
    // Find the last '/' or use start of string
    const char *base = strrchr(input, '/');
    base = base ? base + 1 : input;

    // Find .hml extension
    const char *ext = strstr(base, ".hml");

    char *result;
    if (ext) {
        int base_len = ext - base;
        result = malloc(base_len + 3);  // base + ".c" + null
        strncpy(result, base, base_len);
        result[base_len] = '\0';
        strcat(result, ".c");
    } else {
        result = malloc(strlen(base) + 3);
        sprintf(result, "%s.c", base);
    }

    return result;
}

// Invoke the C compiler
static int compile_c(const Options *opts, const char *c_file) {
    // Build command
    char cmd[4096];
    char opt_flag[4];
    snprintf(opt_flag, sizeof(opt_flag), "-O%d", opts->optimize);

    // Determine runtime path (relative to hemlockc location)
    const char *runtime_path = opts->runtime_path;
    if (!runtime_path) {
        runtime_path = get_self_dir();
        if (!runtime_path) {
            runtime_path = ".";
        }
    }

    // Build link command
    // Check if -lz is linkable (same check as runtime Makefile)
    char zlib_flag[8] = "";
    if (system("echo 'int main(){return 0;}' | gcc -x c - -lz -o /dev/null 2>/dev/null") == 0) {
        strcpy(zlib_flag, " -lz");
    }

    // Check if -lwebsockets is linkable
    char websockets_flag[16] = "";
    if (system("echo 'int main(){return 0;}' | gcc -x c - -lwebsockets -o /dev/null 2>/dev/null") == 0) {
        strcpy(websockets_flag, " -lwebsockets");
    }

    snprintf(cmd, sizeof(cmd),
        "%s %s -o %s %s -I%s/runtime/include -L%s -lhemlock_runtime -lm -lpthread -lffi -ldl%s%s",
        opts->cc, opt_flag, opts->output_file, c_file,
        runtime_path, runtime_path, zlib_flag, websockets_flag);

    if (opts->verbose) {
        printf("Running: %s\n", cmd);
    }

    int status = system(cmd);
    return WEXITSTATUS(status);
}

int main(int argc, char **argv) {
    Options opts = parse_args(argc, argv);

    // Read input file
    char *source = read_file(opts.input_file);
    if (!source) {
        return 1;
    }

    // Parse
    if (opts.verbose) {
        printf("Parsing %s...\n", opts.input_file);
    }

    Lexer lexer;
    lexer_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    int stmt_count;
    Stmt **statements = parse_program(&parser, &stmt_count);

    if (parser.had_error) {
        fprintf(stderr, "Parse failed!\n");
        free(source);
        return 1;
    }

    if (opts.verbose) {
        printf("Parsed %d statements\n", stmt_count);
    }

    // Determine C output file
    char *c_file;
    int c_file_allocated = 0;
    if (opts.c_output) {
        c_file = (char*)opts.c_output;
    } else if (opts.emit_c_only) {
        // When -c is used with -o, use the output file as C output
        if (strcmp(opts.output_file, "a.out") != 0) {
            c_file = (char*)opts.output_file;
        } else {
            c_file = make_c_filename(opts.input_file);
            c_file_allocated = 1;
        }
    } else {
        // Generate temp file
        c_file = strdup("/tmp/hemlock_XXXXXX");
        int fd = mkstemp(c_file);
        if (fd < 0) {
            fprintf(stderr, "Error: Could not create temporary file\n");
            free(source);
            return 1;
        }
        close(fd);
        // Rename to add .c extension
        char *new_name = malloc(strlen(c_file) + 3);
        sprintf(new_name, "%s.c", c_file);
        rename(c_file, new_name);
        free(c_file);
        c_file = new_name;
        c_file_allocated = 1;
    }

    // Open output file
    FILE *output = fopen(c_file, "w");
    if (!output) {
        fprintf(stderr, "Error: Could not open output file '%s'\n", c_file);
        free(source);
        if (c_file_allocated) free(c_file);
        return 1;
    }

    // Generate C code
    if (opts.verbose) {
        printf("Generating C code to %s...\n", c_file);
    }

    // Initialize module cache for import support
    ModuleCache *module_cache = module_cache_new(opts.input_file);

    CodegenContext *ctx = codegen_new(output);
    codegen_set_module_cache(ctx, module_cache);
    codegen_program(ctx, statements, stmt_count);
    codegen_free(ctx);
    module_cache_free(module_cache);
    fclose(output);

    // Cleanup AST
    for (int i = 0; i < stmt_count; i++) {
        stmt_free(statements[i]);
    }
    free(statements);
    free(source);

    if (opts.emit_c_only) {
        if (opts.verbose) {
            printf("C code written to %s\n", c_file);
        }
        if (c_file_allocated) free(c_file);
        return 0;
    }

    // Compile C code
    if (opts.verbose) {
        printf("Compiling C code...\n");
    }

    int result = compile_c(&opts, c_file);

    // Cleanup temp file
    if (!opts.keep_c && !opts.c_output) {
        if (opts.verbose) {
            printf("Removing temporary file %s\n", c_file);
        }
        unlink(c_file);
    }

    if (c_file_allocated) free(c_file);

    if (result == 0) {
        if (opts.verbose) {
            printf("Successfully compiled to %s\n", opts.output_file);
        }
    } else {
        fprintf(stderr, "C compilation failed with status %d\n", result);
    }

    return result;
}
