#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "interpreter.h"
#include "module.h"
#include "interpreter/internal.h"
#include "lsp/lsp.h"
#include "ast_serialize.h"
#include "bundler/bundler.h"
#include "version.h"

#define HEMLOCK_BUILD_DATE __DATE__
#define HEMLOCK_BUILD_TIME __TIME__

// Magic marker for packaged executables (appended at end of file)
// Format: [hemlock binary][HMLB payload][payload_size:u64][HMLP magic:u32]
#define HMLP_MAGIC 0x504C4D48  // "HMLP" in little-endian

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

static void run_source(const char *source, int argc, char **argv, int stack_depth) {
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
    if (stack_depth > 0) {
        ctx->max_stack_depth = stack_depth;
    }

    register_builtins(env, argc, argv, ctx);

    eval_program(statements, stmt_count, env, ctx);

    // Cleanup
    exec_context_free(ctx);
    env_break_cycles(env);  // Break circular references before release
    env_release(env);
    clear_manually_freed_pointers();  // Clear after env is fully freed
    for (int i = 0; i < stmt_count; i++) {
        stmt_free(statements[i]);
    }
    free(statements);
}

// Check if this executable has an embedded HMLB payload
// Returns the payload data (caller must free) or NULL if not packaged
static uint8_t* check_embedded_payload(size_t *out_size) {
    // Read our own executable path
    char exe_path[4096];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1) {
        // Try macOS alternative
        #ifdef __APPLE__
        uint32_t bufsize = sizeof(exe_path);
        if (_NSGetExecutablePath(exe_path, &bufsize) != 0) {
            return NULL;
        }
        #else
        return NULL;
        #endif
    } else {
        exe_path[len] = '\0';
    }

    FILE *f = fopen(exe_path, "rb");
    if (!f) return NULL;

    // Seek to end - 12 bytes (8 byte offset + 4 byte magic)
    if (fseek(f, -12, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }

    uint64_t payload_size;
    uint32_t magic;
    if (fread(&payload_size, 8, 1, f) != 1 ||
        fread(&magic, 4, 1, f) != 1) {
        fclose(f);
        return NULL;
    }

    // Check for HMLP magic
    if (magic != HMLP_MAGIC) {
        fclose(f);
        return NULL;
    }

    // Get file size and calculate payload position
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    long payload_start = file_size - 12 - (long)payload_size;

    if (payload_start < 0 || payload_size == 0 || payload_size > 100000000) {  // 100MB max
        fclose(f);
        return NULL;
    }

    // Read the payload
    fseek(f, payload_start, SEEK_SET);
    uint8_t *payload = malloc(payload_size);
    if (!payload) {
        fclose(f);
        return NULL;
    }

    if (fread(payload, 1, payload_size, f) != payload_size) {
        free(payload);
        fclose(f);
        return NULL;
    }

    fclose(f);
    *out_size = payload_size;
    return payload;
}

// Run an embedded payload (HMLB compressed or HMLC uncompressed)
static int run_embedded_payload(uint8_t *payload, size_t payload_size, int argc, char **argv) {
    if (payload_size < 4) {
        fprintf(stderr, "Error: Invalid embedded payload\n");
        return 1;
    }

    uint32_t magic = *(uint32_t*)payload;
    int stmt_count;
    Stmt **statements = NULL;

    if (magic == 0x424C4D48) {  // "HMLB" (compressed)
        // Payload is in HMLB format: [magic:4][version:2][orig_size:4][compressed_data]
        if (payload_size < 10) {
            fprintf(stderr, "Error: Invalid HMLB payload\n");
            return 1;
        }

        uint16_t version = *(uint16_t*)(payload + 4);
        (void)version;  // Currently unused

        uint32_t orig_size = *(uint32_t*)(payload + 6);
        uint8_t *compressed_data = payload + 10;
        size_t compressed_size = payload_size - 10;

        // Decompress
        uint8_t *decompressed = malloc(orig_size);
        if (!decompressed) {
            fprintf(stderr, "Error: Cannot allocate memory for decompression\n");
            return 1;
        }

        uLongf dest_len = orig_size;
        int ret = uncompress(decompressed, &dest_len, compressed_data, compressed_size);
        if (ret != Z_OK) {
            fprintf(stderr, "Error: Decompression failed (%d)\n", ret);
            free(decompressed);
            return 1;
        }

        // Deserialize AST from memory
        statements = ast_deserialize(decompressed, dest_len, &stmt_count);
        free(decompressed);
    } else if (magic == 0x434C4D48) {  // "HMLC" (uncompressed)
        // Payload is already in HMLC format, deserialize directly
        statements = ast_deserialize(payload, payload_size, &stmt_count);
    } else {
        fprintf(stderr, "Error: Unknown embedded payload format (magic: 0x%08x)\n", magic);
        return 1;
    }

    if (!statements) {
        fprintf(stderr, "Error: Failed to deserialize embedded code\n");
        return 1;
    }

    // Initialize FFI
    ffi_init();

    // Set a placeholder source file
    set_current_source_file("<embedded>");

    // Create execution environment
    Environment *env = env_new(NULL);
    ExecutionContext *ctx = exec_context_new();
    register_builtins(env, argc, argv, ctx);

    // Execute
    eval_program(statements, stmt_count, env, ctx);

    // Cleanup
    exec_context_free(ctx);
    env_break_cycles(env);
    env_release(env);
    clear_manually_freed_pointers();

    for (int i = 0; i < stmt_count; i++) {
        stmt_free(statements[i]);
    }
    free(statements);

    ffi_cleanup();
    set_current_source_file(NULL);

    return 0;
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

static void run_file(const char *path, int argc, char **argv, int stack_depth) {
    char *source = read_file(path);
    if (source == NULL) {
        exit(1);
    }

    // Initialize FFI
    ffi_init();

    // Set current source file for stack traces
    set_current_source_file(path);

    // Check if file uses modules
    if (has_modules(source)) {
        // Use module system
        ExecutionContext *ctx = exec_context_new();
        if (stack_depth > 0) {
            ctx->max_stack_depth = stack_depth;
        }

        // Need to set up builtins in a global environment first
        Environment *global_env = env_new(NULL);
        register_builtins(global_env, argc, argv, ctx);

        // Execute with module system
        int result = execute_file_with_modules(path, global_env, argc, argv, ctx);

        env_break_cycles(global_env);  // Break circular references before release
        env_release(global_env);
        clear_manually_freed_pointers();  // Clear after env is fully freed
        exec_context_free(ctx);
        free(source);

        // Cleanup FFI and source file tracking
        ffi_cleanup();
        set_current_source_file(NULL);

        if (result != 0) {
            exit(1);
        }
    } else {
        // Use traditional execution
        run_source(source, argc, argv, stack_depth);
        free(source);

        // Cleanup FFI and source file tracking
        ffi_cleanup();
        set_current_source_file(NULL);
    }
}

// Compile .hml file to .hmlc binary AST format
static int compile_file(const char *input_path, const char *output_path, int debug_info) {
    char *source = read_file(input_path);
    if (source == NULL) {
        return 1;
    }

    // Parse
    Lexer lexer;
    lexer_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    int stmt_count;
    Stmt **statements = parse_program(&parser, &stmt_count);

    if (parser.had_error) {
        fprintf(stderr, "Compilation failed: parse errors in '%s'\n", input_path);
        free(source);
        return 1;
    }

    // Determine output path
    char *final_output = NULL;
    if (output_path == NULL) {
        // Default: replace .hml with .hmlc or append .hmlc
        size_t len = strlen(input_path);
        final_output = malloc(len + 6);  // Room for ".hmlc" (5) + null (1)
        strcpy(final_output, input_path);

        // Check for .hml extension
        if (len > 4 && strcmp(input_path + len - 4, ".hml") == 0) {
            strcpy(final_output + len - 4, ".hmlc");
        } else {
            strcat(final_output, ".hmlc");
        }
    } else {
        final_output = strdup(output_path);
    }

    // Serialize
    uint16_t flags = 0;
    if (debug_info) {
        flags |= HMLC_FLAG_DEBUG;
    }

    int result = ast_serialize_to_file(final_output, statements, stmt_count, flags);

    if (result == 0) {
        // Get file size for reporting
        FILE *f = fopen(final_output, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fclose(f);
            printf("Compiled '%s' -> '%s' (%ld bytes)\n", input_path, final_output, size);
        } else {
            printf("Compiled '%s' -> '%s'\n", input_path, final_output);
        }
    } else {
        fprintf(stderr, "Failed to write compiled output to '%s'\n", final_output);
    }

    // Cleanup
    free(final_output);
    free(source);
    for (int i = 0; i < stmt_count; i++) {
        stmt_free(statements[i]);
    }
    free(statements);

    return result;
}

// Show info about a compiled .hmlc file
static int show_file_info(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", path);
        return 1;
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Read header
    uint32_t magic;
    if (fread(&magic, 4, 1, f) != 1) {
        fprintf(stderr, "Error: Cannot read file header\n");
        fclose(f);
        return 1;
    }

    printf("=== File Info: %s ===\n", path);
    printf("Size: %ld bytes\n", file_size);

    if (magic == 0x434C4D48) {  // "HMLC"
        uint16_t version, flags;
        uint32_t string_count, stmt_count;

        if (fread(&version, 2, 1, f) != 1 ||
            fread(&flags, 2, 1, f) != 1 ||
            fread(&string_count, 4, 1, f) != 1 ||
            fread(&stmt_count, 4, 1, f) != 1) {
            fprintf(stderr, "Error: Cannot read HMLC header\n");
            fclose(f);
            return 1;
        }

        printf("Format: HMLC (compiled AST)\n");
        printf("Version: %d\n", version);
        printf("Flags: 0x%04x", flags);
        if (flags & 0x0001) printf(" [DEBUG]");
        if (flags & 0x0002) printf(" [COMPRESSED]");
        printf("\n");
        printf("Strings: %u\n", string_count);
        printf("Statements: %u\n", stmt_count);
    } else if (magic == 0x424C4D48) {  // "HMLB"
        uint16_t version;
        uint32_t orig_size;

        if (fread(&version, 2, 1, f) != 1 ||
            fread(&orig_size, 4, 1, f) != 1) {
            fprintf(stderr, "Error: Cannot read HMLB header\n");
            fclose(f);
            return 1;
        }

        long compressed_size = file_size - 10;  // Header is 10 bytes
        double ratio = (1.0 - (double)compressed_size / orig_size) * 100;

        printf("Format: HMLB (compressed bundle)\n");
        printf("Version: %d\n", version);
        printf("Uncompressed: %u bytes\n", orig_size);
        printf("Compressed: %ld bytes\n", compressed_size);
        printf("Ratio: %.1f%% reduction\n", ratio);
    } else {
        printf("Format: Unknown (magic: 0x%08x)\n", magic);
    }

    fclose(f);
    return 0;
}

// Bundle a .hml file with all its dependencies
static int bundle_file(const char *input_path, const char *output_path, int verbose, int compressed) {
    BundleOptions opts = bundle_options_default();
    opts.verbose = verbose;

    // Create bundle
    Bundle *bundle = bundle_create(input_path, &opts);
    if (!bundle) {
        fprintf(stderr, "Failed to create bundle from '%s'\n", input_path);
        return 1;
    }

    // Flatten all modules
    if (bundle_flatten(bundle) != 0) {
        fprintf(stderr, "Failed to flatten bundle\n");
        bundle_free(bundle);
        return 1;
    }

    if (verbose) {
        bundle_print_summary(bundle);
    }

    // Determine output path
    char *final_output = NULL;
    if (output_path == NULL) {
        size_t len = strlen(input_path);
        final_output = malloc(len + 6);  // Room for ".hmlb\0" or ".hmlc\0"
        strcpy(final_output, input_path);

        const char *ext = compressed ? ".hmlb" : ".hmlc";
        if (len > 4 && strcmp(input_path + len - 4, ".hml") == 0) {
            strcpy(final_output + len - 4, ext);
        } else {
            strcat(final_output, ext);
        }
    } else {
        final_output = strdup(output_path);
    }

    // Write output
    int result;
    if (compressed) {
        result = bundle_write_compressed(bundle, final_output);
    } else {
        result = bundle_write_hmlc(bundle, final_output, HMLC_FLAG_DEBUG);
    }

    if (result == 0) {
        FILE *f = fopen(final_output, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fclose(f);
            printf("Bundled '%s' -> '%s' (%ld bytes, %d module%s)\n",
                   input_path, final_output, size,
                   bundle->num_modules, bundle->num_modules == 1 ? "" : "s");
        }
    } else {
        fprintf(stderr, "Failed to write bundle to '%s'\n", final_output);
    }

    free(final_output);
    bundle_free(bundle);
    return result;
}

// Create a self-contained executable (.hmlp) from a .hml file
static int package_file(const char *input_path, const char *output_path, int verbose, int compress) {
    BundleOptions opts = bundle_options_default();
    opts.verbose = verbose;

    // Create bundle
    Bundle *bundle = bundle_create(input_path, &opts);
    if (!bundle) {
        fprintf(stderr, "Failed to create bundle from '%s'\n", input_path);
        return 1;
    }

    // Flatten all modules
    if (bundle_flatten(bundle) != 0) {
        fprintf(stderr, "Failed to flatten bundle\n");
        bundle_free(bundle);
        return 1;
    }

    if (verbose) {
        bundle_print_summary(bundle);
    }

    // Serialize to memory
    size_t serialized_size;
    uint8_t *serialized = ast_serialize(bundle->statements, bundle->num_statements,
                                        HMLC_FLAG_DEBUG, &serialized_size);
    if (!serialized) {
        fprintf(stderr, "Failed to serialize bundle\n");
        bundle_free(bundle);
        return 1;
    }

    uint8_t *payload_data;
    size_t payload_data_size;
    uint32_t payload_magic;
    uint32_t orig_size_for_header;

    if (compress) {
        // Compress with zlib
        uLongf compressed_size = compressBound(serialized_size);
        uint8_t *compressed = malloc(compressed_size);

        int ret = compress2(compressed, &compressed_size, serialized, serialized_size, Z_BEST_COMPRESSION);
        if (ret != Z_OK) {
            fprintf(stderr, "Compression failed\n");
            free(compressed);
            free(serialized);
            bundle_free(bundle);
            return 1;
        }

        payload_data = compressed;
        payload_data_size = compressed_size;
        payload_magic = 0x424C4D48;  // "HMLB" (compressed)
        orig_size_for_header = (uint32_t)serialized_size;
        free(serialized);
    } else {
        // Use uncompressed HMLC format for faster startup
        payload_data = serialized;
        payload_data_size = serialized_size;
        payload_magic = 0x434C4D48;  // "HMLC" (uncompressed)
        orig_size_for_header = 0;  // Not used for HMLC
    }

    // Build payload in memory
    // For HMLB: [magic:4][version:2][orig_size:4][compressed_data]
    // For HMLC: already in serialized format with its own header
    size_t hmlb_size;
    uint8_t *hmlb_payload;

    if (compress) {
        hmlb_size = 10 + payload_data_size;  // header + compressed data
        hmlb_payload = malloc(hmlb_size);
        uint16_t version = 1;

        memcpy(hmlb_payload, &payload_magic, 4);
        memcpy(hmlb_payload + 4, &version, 2);
        memcpy(hmlb_payload + 6, &orig_size_for_header, 4);
        memcpy(hmlb_payload + 10, payload_data, payload_data_size);
        free(payload_data);
    } else {
        // For HMLC, the serialized data already has its header
        hmlb_size = payload_data_size;
        hmlb_payload = payload_data;  // Transfer ownership
    }

    // Read our own executable
    char exe_path[4096];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1) {
        #ifdef __APPLE__
        uint32_t bufsize = sizeof(exe_path);
        if (_NSGetExecutablePath(exe_path, &bufsize) != 0) {
            fprintf(stderr, "Cannot determine executable path\n");
            free(hmlb_payload);
            bundle_free(bundle);
            return 1;
        }
        #else
        fprintf(stderr, "Cannot determine executable path\n");
        free(hmlb_payload);
        bundle_free(bundle);
        return 1;
        #endif
    } else {
        exe_path[len] = '\0';
    }

    FILE *exe_file = fopen(exe_path, "rb");
    if (!exe_file) {
        fprintf(stderr, "Cannot read executable '%s'\n", exe_path);
        free(hmlb_payload);
        bundle_free(bundle);
        return 1;
    }

    fseek(exe_file, 0, SEEK_END);
    long exe_size = ftell(exe_file);
    fseek(exe_file, 0, SEEK_SET);

    uint8_t *exe_data = malloc(exe_size);
    if (fread(exe_data, 1, exe_size, exe_file) != (size_t)exe_size) {
        fprintf(stderr, "Failed to read executable\n");
        free(exe_data);
        free(hmlb_payload);
        fclose(exe_file);
        bundle_free(bundle);
        return 1;
    }
    fclose(exe_file);

    // Determine output path
    char *final_output = NULL;
    if (output_path == NULL) {
        size_t input_len = strlen(input_path);
        final_output = malloc(input_len + 6);  // Room for ".hmlp\0"
        strcpy(final_output, input_path);

        if (input_len > 4 && strcmp(input_path + input_len - 4, ".hml") == 0) {
            final_output[input_len - 4] = '\0';  // Strip .hml extension
        }
    } else {
        final_output = strdup(output_path);
    }

    // Write packaged executable: [exe][hmlb_payload][payload_size:u64][HMLP:u32]
    FILE *out = fopen(final_output, "wb");
    if (!out) {
        fprintf(stderr, "Cannot create output file '%s'\n", final_output);
        free(exe_data);
        free(hmlb_payload);
        free(final_output);
        bundle_free(bundle);
        return 1;
    }

    fwrite(exe_data, 1, exe_size, out);
    fwrite(hmlb_payload, 1, hmlb_size, out);

    uint64_t payload_size_u64 = hmlb_size;
    uint32_t hmlp_magic = HMLP_MAGIC;
    fwrite(&payload_size_u64, 8, 1, out);
    fwrite(&hmlp_magic, 4, 1, out);

    fclose(out);
    free(exe_data);
    free(hmlb_payload);

    // Make executable
    chmod(final_output, 0755);

    // Get final size
    struct stat st;
    stat(final_output, &st);

    printf("Packaged '%s' -> '%s' (%ld bytes, %d module%s)\n",
           input_path, final_output, (long)st.st_size,
           bundle->num_modules, bundle->num_modules == 1 ? "" : "s");

    free(final_output);
    bundle_free(bundle);
    return 0;
}

// Run a .hmlc compiled file
static void run_hmlc_file(const char *path, int argc, char **argv, int stack_depth) {
    // Deserialize AST from file
    int stmt_count;
    Stmt **statements = ast_deserialize_from_file(path, &stmt_count);
    if (statements == NULL) {
        fprintf(stderr, "Failed to load compiled file '%s'\n", path);
        exit(1);
    }

    // Initialize FFI
    ffi_init();

    // Set current source file for stack traces
    set_current_source_file(path);

    // Create execution environment
    Environment *env = env_new(NULL);
    ExecutionContext *ctx = exec_context_new();
    if (stack_depth > 0) {
        ctx->max_stack_depth = stack_depth;
    }
    register_builtins(env, argc, argv, ctx);

    // Execute
    eval_program(statements, stmt_count, env, ctx);

    // Cleanup
    exec_context_free(ctx);
    env_break_cycles(env);
    env_release(env);
    clear_manually_freed_pointers();

    for (int i = 0; i < stmt_count; i++) {
        stmt_free(statements[i]);
    }
    free(statements);

    ffi_cleanup();
    set_current_source_file(NULL);
}

// Check if file has .hmlc extension
static int is_hmlc_extension(const char *path) {
    size_t len = strlen(path);
    return len > 5 && strcmp(path + len - 5, ".hmlc") == 0;
}

static void run_repl(int stack_depth) {
    char line[1024];
    Environment *env = env_new(NULL);

    // Create execution context for REPL (persists across lines)
    ExecutionContext *ctx = exec_context_new();
    if (stack_depth > 0) {
        ctx->max_stack_depth = stack_depth;
    }

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
    env_break_cycles(env);  // Break circular references before release
    env_release(env);
    clear_manually_freed_pointers();  // Clear after env is fully freed
}

static void print_version(void) {
    printf("Hemlock version %s (built %s %s)\n", HEMLOCK_VERSION, HEMLOCK_BUILD_DATE, HEMLOCK_BUILD_TIME);
    printf("A small, unsafe language for writing unsafe things safely.\n");
}

static void print_help(const char *program) {
    printf("Hemlock - A systems scripting language\n\n");
    printf("USAGE:\n");
    printf("    %s [OPTIONS] [FILE] [ARGS...]\n", program);
    printf("    %s --compile FILE [-o OUTPUT] [--debug]\n", program);
    printf("    %s --bundle FILE [-o OUTPUT] [--compress] [--verbose]\n", program);
    printf("    %s --package FILE [-o OUTPUT] [--no-compress] [--verbose]\n", program);
    printf("    %s lsp [--stdio | --tcp PORT]\n\n", program);
    printf("ARGUMENTS:\n");
    printf("    <FILE>       Hemlock script file to execute (.hml or .hmlc)\n");
    printf("    <ARGS>...    Arguments passed to the script (available in 'args' array)\n\n");
    printf("SUBCOMMANDS:\n");
    printf("    lsp          Start Language Server Protocol server\n");
    printf("        --stdio      Use stdio transport (default)\n");
    printf("        --tcp PORT   Use TCP transport on specified port\n\n");
    printf("OPTIONS:\n");
    printf("    -h, --help           Display this help message\n");
    printf("    -v, --version        Display version information\n");
    printf("    -i, --interactive    Start REPL after executing file\n");
    printf("    -e, -c, --command <CODE>\n");
    printf("                         Execute code string directly\n");
    printf("    --compile <FILE>     Compile .hml to binary AST (.hmlc)\n");
    printf("    --bundle <FILE>      Bundle .hml with all imports into single file\n");
    printf("    --package <FILE>     Create self-contained executable (interpreter + bundle)\n");
    printf("    --compress           Use zlib compression for bundle output (.hmlb)\n");
    printf("    --no-compress        Skip compression (faster startup, larger binary)\n");
    printf("    --info <FILE>        Show info about a .hmlc/.hmlb file\n");
    printf("    -o, --output <FILE>  Output path for compiled/bundled/packaged file\n");
    printf("    --debug              Include line numbers in compiled output\n");
    printf("    --verbose            Print progress during bundling/packaging\n");
    printf("    --stack-depth <N>    Set maximum call stack depth (default: 10000)\n\n");
    printf("EXAMPLES:\n");
    printf("    %s                     # Start interactive REPL\n", program);
    printf("    %s script.hml          # Run script.hml\n", program);
    printf("    %s script.hmlc         # Run compiled script\n", program);
    printf("    %s script.hml arg1 arg2    # Run script with arguments\n", program);
    printf("    %s -e 'print(\"Hello\");'    # Execute code string (one-liner)\n", program);
    printf("    %s -i script.hml       # Run script then start REPL\n", program);
    printf("    %s --compile script.hml    # Compile to script.hmlc\n", program);
    printf("    %s --compile src.hml -o out.hmlc --debug\n", program);
    printf("    %s --bundle app.hml        # Bundle app.hml + imports -> app.hmlc\n", program);
    printf("    %s --bundle app.hml --compress -o app.hmlb\n", program);
    printf("    %s --package app.hml       # Create ./app executable\n", program);
    printf("    %s --package app.hml --no-compress -o myapp\n", program);
    printf("    %s --info app.hmlc         # Show compiled file info\n", program);
    printf("    %s --stack-depth 50000 script.hml  # Run with larger stack\n", program);
    printf("    %s lsp                 # Start LSP server (stdio)\n", program);
    printf("    %s lsp --tcp 6969      # Start LSP server (TCP)\n\n", program);
    printf("For more information, visit: https://github.com/hemlang/hemlock\n");
}

// Run LSP server
static int run_lsp(int argc, char **argv) {
    int use_tcp = 0;
    int tcp_port = 6969;

    // Parse LSP-specific options
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--stdio") == 0) {
            use_tcp = 0;
        } else if (strcmp(argv[i], "--tcp") == 0) {
            use_tcp = 1;
            if (i + 1 < argc) {
                tcp_port = atoi(argv[i + 1]);
                i++;
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Hemlock LSP Server\n\n");
            printf("USAGE:\n");
            printf("    hemlock lsp [OPTIONS]\n\n");
            printf("OPTIONS:\n");
            printf("    --stdio          Use stdio transport (default)\n");
            printf("    --tcp PORT       Use TCP transport on specified port\n");
            printf("    -h, --help       Display this help message\n");
            return 0;
        }
    }

    LSPServer *server = lsp_server_create();

    int result;
    if (use_tcp) {
        result = lsp_server_run_tcp(server, tcp_port);
    } else {
        result = lsp_server_run_stdio(server);
    }

    lsp_server_free(server);
    return result;
}

int main(int argc, char **argv) {
    // Check for embedded payload FIRST (before any argument parsing)
    // This allows packaged executables to run their embedded code
    size_t payload_size;
    uint8_t *payload = check_embedded_payload(&payload_size);
    if (payload) {
        int result = run_embedded_payload(payload, payload_size, argc, argv);
        free(payload);
        cleanup_object_types();
        cleanup_enum_types();
        return result;
    }

    int interactive_mode = 0;
    int compile_mode = 0;
    int compile_debug = 0;
    int bundle_mode = 0;
    int bundle_compress = 0;
    int bundle_verbose = 0;
    int package_mode = 0;
    int info_mode = 0;
    int stack_depth = 0;  // 0 = use default (DEFAULT_MAX_STACK_DEPTH)
    const char *file_to_info = NULL;
    const char *file_to_run = NULL;
    const char *file_to_compile = NULL;
    const char *file_to_bundle = NULL;
    const char *file_to_package = NULL;
    const char *output_path = NULL;
    const char *command_to_run = NULL;
    int first_script_arg = 0;  // Index of first argument to pass to script

    // Check for LSP subcommand first
    if (argc >= 2 && strcmp(argv[1], "lsp") == 0) {
        return run_lsp(argc, argv);
    }

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
        } else if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--command") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -e/-c/--command requires a code argument\n");
                fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
                return 1;
            }
            command_to_run = argv[i + 1];
            i++;  // Skip the code argument
        } else if (strcmp(argv[i], "--compile") == 0) {
            compile_mode = 1;
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --compile requires a file argument\n");
                fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
                return 1;
            }
            file_to_compile = argv[i + 1];
            i++;  // Skip the file argument
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -o/--output requires a file argument\n");
                fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
                return 1;
            }
            output_path = argv[i + 1];
            i++;  // Skip the output path
        } else if (strcmp(argv[i], "--debug") == 0) {
            compile_debug = 1;
        } else if (strcmp(argv[i], "--bundle") == 0) {
            bundle_mode = 1;
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --bundle requires a file argument\n");
                fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
                return 1;
            }
            file_to_bundle = argv[i + 1];
            i++;  // Skip the file argument
        } else if (strcmp(argv[i], "--compress") == 0) {
            bundle_compress = 1;
        } else if (strcmp(argv[i], "--no-compress") == 0) {
            bundle_compress = -1;  // Explicitly disabled
        } else if (strcmp(argv[i], "--verbose") == 0) {
            bundle_verbose = 1;
        } else if (strcmp(argv[i], "--stack-depth") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --stack-depth requires a numeric argument\n");
                fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
                return 1;
            }
            stack_depth = atoi(argv[i + 1]);
            if (stack_depth <= 0) {
                fprintf(stderr, "Error: --stack-depth must be a positive integer\n");
                return 1;
            }
            i++;  // Skip the value argument
        } else if (strcmp(argv[i], "--info") == 0) {
            info_mode = 1;
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --info requires a file argument\n");
                fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
                return 1;
            }
            file_to_info = argv[i + 1];
            i++;  // Skip the file argument
        } else if (strcmp(argv[i], "--package") == 0) {
            package_mode = 1;
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --package requires a file argument\n");
                fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
                return 1;
            }
            file_to_package = argv[i + 1];
            i++;  // Skip the file argument
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

    // Handle compile mode
    if (compile_mode) {
        if (file_to_compile == NULL) {
            fprintf(stderr, "Error: No input file specified for compilation\n");
            return 1;
        }
        int result = compile_file(file_to_compile, output_path, compile_debug);
        return result;
    }

    // Handle bundle mode
    if (bundle_mode) {
        if (file_to_bundle == NULL) {
            fprintf(stderr, "Error: No input file specified for bundling\n");
            return 1;
        }
        int result = bundle_file(file_to_bundle, output_path, bundle_verbose, bundle_compress);
        return result;
    }

    // Handle info mode
    if (info_mode) {
        if (file_to_info == NULL) {
            fprintf(stderr, "Error: No input file specified for info\n");
            return 1;
        }
        return show_file_info(file_to_info);
    }

    // Handle package mode
    if (package_mode) {
        if (file_to_package == NULL) {
            fprintf(stderr, "Error: No input file specified for packaging\n");
            return 1;
        }
        // For --package, compression is ON by default (smaller binary)
        // Use --no-compress for faster startup at cost of larger binary
        int compress = (bundle_compress == -1) ? 0 : 1;  // Default to compressed
        int result = package_file(file_to_package, output_path, bundle_verbose, compress);
        return result;
    }

    if (command_to_run != NULL) {
        // Execute code string
        ffi_init();
        run_source(command_to_run, 0, NULL, stack_depth);
        ffi_cleanup();

        if (interactive_mode) {
            run_repl(stack_depth);
        }

        // Cleanup type registries before exit
        cleanup_object_types();
        cleanup_enum_types();
        return 0;
    }

    if (file_to_run != NULL) {
        // Run file with arguments
        int script_argc = argc - first_script_arg;
        char **script_argv = &argv[first_script_arg];

        // Check if it's a compiled .hmlc file
        if (is_hmlc_extension(file_to_run) || is_hmlc_file(file_to_run)) {
            run_hmlc_file(file_to_run, script_argc, script_argv, stack_depth);
        } else {
            run_file(file_to_run, script_argc, script_argv, stack_depth);
        }

        if (interactive_mode) {
            run_repl(stack_depth);
        }

        // Cleanup type registries before exit
        cleanup_object_types();
        cleanup_enum_types();
        return 0;
    }

    // No file or command specified - start REPL
    run_repl(stack_depth);

    // Cleanup type registries before exit
    cleanup_object_types();
    cleanup_enum_types();
    return 0;
}