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
#include "../../include/version.h"
#include "codegen.h"

#define HEMLOCK_BUILD_DATE __DATE__
#define HEMLOCK_BUILD_TIME __TIME__

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

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

// Standard install locations for runtime library
#ifndef HEMLOCK_LIBDIR
#define HEMLOCK_LIBDIR "/usr/local/lib/hemlock"
#endif

// Find the runtime library, checking multiple locations
// Returns a static buffer with the path, or NULL if not found
static const char* find_runtime_path(void) {
    static char found_path[PATH_MAX];
    char check_path[PATH_MAX];

    // List of directories to check (in order of priority)
    const char *search_dirs[4];
    int num_dirs = 0;

    // 1. Directory containing hemlockc (for development builds)
    const char *self_dir = get_self_dir();
    if (self_dir) {
        search_dirs[num_dirs++] = self_dir;
    }

    // 2. Standard install location
    search_dirs[num_dirs++] = HEMLOCK_LIBDIR;

    // 3. Current directory (fallback)
    search_dirs[num_dirs++] = ".";

    // Check each directory for libhemlock_runtime.a
    for (int i = 0; i < num_dirs; i++) {
        snprintf(check_path, sizeof(check_path), "%s/libhemlock_runtime.a", search_dirs[i]);
        if (access(check_path, R_OK) == 0) {
            strncpy(found_path, search_dirs[i], sizeof(found_path) - 1);
            found_path[sizeof(found_path) - 1] = '\0';
            return found_path;
        }
    }

    // Not found - return self_dir anyway (will fail at link time with clear error)
    if (self_dir) {
        return self_dir;
    }
    return ".";
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
    fprintf(stderr, "Hemlock Compiler v%s\n\n", HEMLOCK_VERSION);
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
            printf("hemlockc %s (built %s %s)\n", HEMLOCK_VERSION, HEMLOCK_BUILD_DATE, HEMLOCK_BUILD_TIME);
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
        if (!result) {
            fprintf(stderr, "Error: Failed to allocate output filename\n");
            return NULL;
        }
        strncpy(result, base, base_len);
        result[base_len] = '\0';
        strcat(result, ".c");
    } else {
        size_t len = strlen(base) + 3;
        result = malloc(len);
        if (!result) {
            fprintf(stderr, "Error: Failed to allocate output filename\n");
            return NULL;
        }
        snprintf(result, len, "%s.c", base);
    }

    return result;
}

// Invoke the C compiler
static int compile_c(const Options *opts, const char *c_file) {
    // Build command
    char cmd[4096];
    char opt_flag[4];
    snprintf(opt_flag, sizeof(opt_flag), "-O%d", opts->optimize);

    // Determine runtime path
    // Priority: --runtime flag > auto-detect (self dir, install dir, cwd)
    const char *runtime_path = opts->runtime_path;
    if (!runtime_path) {
        runtime_path = find_runtime_path();
    }

    // Build link command
    // Check if -lz is linkable (same check as runtime Makefile)
    char zlib_flag[8] = "";
    if (system("echo 'int main(){return 0;}' | gcc -x c - -lz -o /dev/null 2>/dev/null") == 0) {
        strcpy(zlib_flag, " -lz");
    }

    // Platform-specific library paths
    char extra_lib_paths[512] = "";
#ifdef __APPLE__
    size_t extra_lib_len = 0;  // SECURITY: Track length to prevent overflow
    // On macOS, check for Homebrew libffi and libwebsockets paths
    FILE *fp = popen("brew --prefix libffi 2>/dev/null", "r");
    if (fp) {
        char libffi_path[256];
        if (fgets(libffi_path, sizeof(libffi_path), fp)) {
            libffi_path[strcspn(libffi_path, "\n")] = 0;
            char tmp[128];
            int n = snprintf(tmp, sizeof(tmp), " -L%s/lib", libffi_path);
            // SECURITY: Bounds-checked concatenation
            if (n > 0 && extra_lib_len + (size_t)n < sizeof(extra_lib_paths) - 1) {
                strcat(extra_lib_paths + extra_lib_len, tmp);
                extra_lib_len += (size_t)n;
            }
        }
        pclose(fp);
    }
    fp = popen("brew --prefix libwebsockets 2>/dev/null", "r");
    if (fp) {
        char lws_path[256];
        if (fgets(lws_path, sizeof(lws_path), fp)) {
            lws_path[strcspn(lws_path, "\n")] = 0;
            char tmp[128];
            int n = snprintf(tmp, sizeof(tmp), " -L%s/lib", lws_path);
            // SECURITY: Bounds-checked concatenation
            if (n > 0 && extra_lib_len + (size_t)n < sizeof(extra_lib_paths) - 1) {
                strcat(extra_lib_paths + extra_lib_len, tmp);
                extra_lib_len += (size_t)n;
            }
        }
        pclose(fp);
    }
#endif

    // Check if -lwebsockets is linkable (with extra paths)
    char websockets_flag[16] = "";
    char ws_test_cmd[1024];
    snprintf(ws_test_cmd, sizeof(ws_test_cmd),
        "echo 'int main(){return 0;}' | gcc -x c - %s -lwebsockets -o /dev/null 2>/dev/null",
        extra_lib_paths);
    if (system(ws_test_cmd) == 0) {
        strcpy(websockets_flag, " -lwebsockets");
    }

    // OpenSSL/libcrypto is required for hash functions (sha256, sha512, md5)
#ifdef __APPLE__
    // On macOS, add OpenSSL library path from Homebrew
    FILE *ssl_fp = popen("brew --prefix openssl@3 2>/dev/null", "r");
    if (ssl_fp) {
        char ssl_path[256];
        if (fgets(ssl_path, sizeof(ssl_path), ssl_fp)) {
            ssl_path[strcspn(ssl_path, "\n")] = 0;
            char tmp[128];
            int n = snprintf(tmp, sizeof(tmp), " -L%s/lib", ssl_path);
            // SECURITY: Bounds-checked concatenation
            if (n > 0 && extra_lib_len + (size_t)n < sizeof(extra_lib_paths) - 1) {
                strcat(extra_lib_paths + extra_lib_len, tmp);
                extra_lib_len += (size_t)n;
            }
        }
        pclose(ssl_fp);
    }
#endif

    // OpenSSL/libcrypto is required - the runtime links against it for hash functions
    // The runtime always needs libcrypto, so always link it
    // On Linux, use --no-as-needed to ensure the library is linked even if not directly referenced
#ifdef __APPLE__
    char crypto_flag[64] = " -lcrypto";
#else
    char crypto_flag[64] = " -Wl,--no-as-needed -lcrypto";
#endif

    // Determine include path - check for both development and installed layouts
    // Development: runtime_path/runtime/include
    // Installed: runtime_path/include
    char include_path[PATH_MAX];
    char dev_include[PATH_MAX];
    char install_include[PATH_MAX];
    snprintf(dev_include, sizeof(dev_include), "%s/runtime/include", runtime_path);
    snprintf(install_include, sizeof(install_include), "%s/include", runtime_path);

    if (access(dev_include, R_OK) == 0) {
        strncpy(include_path, dev_include, sizeof(include_path) - 1);
    } else {
        strncpy(include_path, install_include, sizeof(include_path) - 1);
    }
    include_path[sizeof(include_path) - 1] = '\0';

    snprintf(cmd, sizeof(cmd),
        "%s %s -o %s %s -I%s %s/libhemlock_runtime.a%s -lm -lpthread -lffi -ldl%s%s%s",
        opts->cc, opt_flag, opts->output_file, c_file,
        include_path, runtime_path, extra_lib_paths, zlib_flag, websockets_flag, crypto_flag);

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
            free(c_file);
            free(source);
            return 1;
        }
        close(fd);
        // Rename to add .c extension
        size_t name_len = strlen(c_file) + 3;
        char *new_name = malloc(name_len);
        if (!new_name) {
            fprintf(stderr, "Error: Failed to allocate temporary filename\n");
            free(c_file);
            free(source);
            return 1;
        }
        snprintf(new_name, name_len, "%s.c", c_file);
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

    // Check for compilation errors
    int had_errors = ctx->error_count > 0;
    if (had_errors) {
        fprintf(stderr, "%d error%s generated\n", ctx->error_count, ctx->error_count > 1 ? "s" : "");
    }

    codegen_free(ctx);
    module_cache_free(module_cache);
    fclose(output);

    // If there were errors, cleanup and exit
    if (had_errors) {
        for (int i = 0; i < stmt_count; i++) {
            stmt_free(statements[i]);
        }
        free(statements);
        free(source);
        if (!opts.keep_c && !opts.c_output) {
            unlink(c_file);
        }
        if (c_file_allocated) free(c_file);
        return 1;
    }

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
