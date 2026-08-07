#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdint.h>
#include <unistd.h>
#include <zlib.h>
#include "frontend.h"
#include "hemlock_compat.h"
#include "interpreter.h"
#include "interpreter/internal.h"
#include "tools/lsp/lsp.h"
#include "tools/bundler/bundler.h"
#include "tools/type_check.h"
#include "tools/borrow_check.h"
#include "tools/lint.h"
#include "version.h"
#include "profiler/profiler.h"
#include "shared/file_io.h"

#define HEMLOCK_BUILD_DATE __DATE__
#define HEMLOCK_BUILD_TIME __TIME__

// Magic marker for packaged executables (appended at end of file)
// Format: [hemlock binary][HMLB payload][payload_size:u64][HMLP magic:u32]
#define HMLP_MAGIC 0x504C4D48  // "HMLP" in little-endian
#define HMLB_MAGIC 0x424C4D48  // "HMLB" in little-endian
#define MAX_COMPILED_PAYLOAD_SIZE 100000000U  // 100MB safety limit

// FFI functions (from interpreter/ffi.c)
extern void ffi_init(void);
extern void ffi_cleanup(void);

static void run_source(const char *source, int argc, char **argv, int stack_depth, int sandbox_flags, const char *sandbox_root) {
    // Parse
    Lexer lexer;
    lexer_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    int stmt_count;
    Stmt **statements = parse_program(&parser, &stmt_count);

    if (parser.had_error) {
        fprintf(stderr, "Parse failed!\n");
        // Free any partially parsed statements before exiting
        for (int i = 0; i < stmt_count; i++) {
            stmt_free(statements[i]);
        }
        free(statements);
        exit(1);
    }

    // Resolve variables (compute depth/slot indices for O(1) lookup)
    resolve_program(statements, stmt_count);

    // Optimize AST (constant folding, boolean simplification, strength reduction)
    optimize_program(statements, stmt_count);

    // Interpret
    Environment *env = env_new(NULL);

    // Create execution context
    ExecutionContext *ctx = exec_context_new();
    if (stack_depth > 0) {
        ctx->max_stack_depth = stack_depth;
    }

    // Configure sandbox if enabled
    if (sandbox_flags != 0) {
        ctx->sandbox_flags = sandbox_flags;
        if (sandbox_root) {
            ctx->sandbox_root = strdup(sandbox_root);
        }
    }

    register_builtins(env, argc, argv, ctx);

    eval_program(statements, stmt_count, env, ctx);

    // Cleanup
    exec_context_free(ctx);
    env_break_cycles(env);  // Break circular references before release
    env_release(env);
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
#ifdef _WIN32
    ssize_t len = hml_get_executable_path(exe_path, sizeof(exe_path))
                      ? (ssize_t)strlen(exe_path) : -1;
#else
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
#endif
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

    if (payload_start < 0 || payload_size == 0 || payload_size > MAX_COMPILED_PAYLOAD_SIZE) {
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

static uint16_t read_le16(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

// Deserialize either an HMLC payload or an HMLB compressed payload.
static Stmt **deserialize_compiled_payload(const uint8_t *payload, size_t payload_size,
                                           int *out_stmt_count, const char *description) {
    if (payload_size < 4) {
        fprintf(stderr, "Error: Invalid %s payload\n", description);
        return NULL;
    }

    uint32_t magic = read_le32(payload);

    if (magic == HMLB_MAGIC) {
        // HMLB format: [magic:4][version:2][orig_size:4][compressed_data]
        if (payload_size < 10) {
            fprintf(stderr, "Error: Invalid HMLB %s payload\n", description);
            return NULL;
        }

        uint16_t version = read_le16(payload + 4);
        if (version != HMLC_VERSION) {
            fprintf(stderr, "Error: Unsupported HMLB version %u in %s payload\n",
                    version, description);
            return NULL;
        }

        uint32_t orig_size = read_le32(payload + 6);
        if (orig_size == 0 || orig_size > MAX_COMPILED_PAYLOAD_SIZE) {
            fprintf(stderr, "Error: Invalid HMLB uncompressed size %u in %s payload\n",
                    orig_size, description);
            return NULL;
        }

        const uint8_t *compressed_data = payload + 10;
        size_t compressed_size = payload_size - 10;

        uint8_t *decompressed = malloc(orig_size);
        if (!decompressed) {
            fprintf(stderr, "Error: Cannot allocate memory for HMLB decompression\n");
            return NULL;
        }

        uLongf dest_len = orig_size;
        int ret = uncompress(decompressed, &dest_len, compressed_data, compressed_size);
        if (ret != Z_OK || dest_len != orig_size) {
            fprintf(stderr, "Error: HMLB decompression failed for %s payload (%d)\n",
                    description, ret);
            free(decompressed);
            return NULL;
        }

        Stmt **statements = ast_deserialize(decompressed, dest_len, out_stmt_count);
        free(decompressed);
        return statements;
    }

    if (magic == HMLC_MAGIC) {
        return ast_deserialize(payload, payload_size, out_stmt_count);
    }

    fprintf(stderr, "Error: Unknown %s payload format (magic: 0x%08x)\n", description, magic);
    return NULL;
}

static uint8_t *read_binary_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open '%s' for reading\n", path);
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "Error: Cannot seek in '%s'\n", path);
        fclose(f);
        return NULL;
    }

    long file_size = ftell(f);
    if (file_size < 0 || file_size > (long)MAX_COMPILED_PAYLOAD_SIZE) {
        fprintf(stderr, "Error: Invalid or oversized compiled file '%s'\n", path);
        fclose(f);
        return NULL;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Error: Cannot seek in '%s'\n", path);
        fclose(f);
        return NULL;
    }

    uint8_t *data = malloc((size_t)file_size == 0 ? 1 : (size_t)file_size);
    if (!data) {
        fprintf(stderr, "Error: Cannot allocate memory to read '%s'\n", path);
        fclose(f);
        return NULL;
    }

    size_t bytes_read = fread(data, 1, (size_t)file_size, f);
    fclose(f);

    if (bytes_read != (size_t)file_size) {
        fprintf(stderr, "Error: Failed to read complete file '%s'\n", path);
        free(data);
        return NULL;
    }

    *out_size = (size_t)file_size;
    return data;
}

// Run an embedded payload (HMLB compressed or HMLC uncompressed)
static int run_embedded_payload(uint8_t *payload, size_t payload_size, int argc, char **argv) {
    int stmt_count;
    Stmt **statements = deserialize_compiled_payload(payload, payload_size, &stmt_count, "embedded");

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

static void run_file(const char *path, int argc, char **argv, int stack_depth, int sandbox_flags, const char *sandbox_root) {
    char *source = read_file(path);
    if (source == NULL) {
        exit(1);
    }

    // Initialize FFI
    ffi_init();

    // Set current source file and source code for stack traces and error context
    set_current_source_file(path);
    set_current_source_code(source);

    // Check if file uses modules
    if (has_modules(source)) {
        // Use module system
        ExecutionContext *ctx = exec_context_new();
        if (stack_depth > 0) {
            ctx->max_stack_depth = stack_depth;
        }

        // Configure sandbox if enabled
        if (sandbox_flags != 0) {
            ctx->sandbox_flags = sandbox_flags;
            if (sandbox_root) {
                ctx->sandbox_root = strdup(sandbox_root);
            }
        }

        // Need to set up builtins in a global environment first
        Environment *global_env = env_new(NULL);
        register_builtins(global_env, argc, argv, ctx);

        // Execute with module system
        int result = execute_file_with_modules(path, global_env, argc, argv, ctx);

        env_break_cycles(global_env);  // Break circular references before release
        env_release(global_env);
        exec_context_free(ctx);
        free(source);

        // Cleanup FFI and source tracking
        ffi_cleanup();
        set_current_source_file(NULL);
        set_current_source_code(NULL);

        if (result != 0) {
            exit(1);
        }
    } else {
        // Use traditional execution
        run_source(source, argc, argv, stack_depth, sandbox_flags, sandbox_root);
        free(source);

        // Cleanup FFI and source tracking
        ffi_cleanup();
        set_current_source_file(NULL);
        set_current_source_code(NULL);
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
        // Free any partially parsed statements
        for (int i = 0; i < stmt_count; i++) {
            stmt_free(statements[i]);
        }
        free(statements);
        free(source);
        return 1;
    }

    // Determine output path
    char *final_output = NULL;
    if (output_path == NULL) {
        // Default: replace .hml with .hmlc or append .hmlc
        size_t len = strlen(input_path);
        final_output = malloc(len + 6);  // Room for ".hmlc" (5) + null (1)
        memcpy(final_output, input_path, len + 1);

        // Check for .hml extension
        if (len > 4 && strcmp(input_path + len - 4, ".hml") == 0) {
            memcpy(final_output + len - 4, ".hmlc", 6);
        } else {
            memcpy(final_output + len, ".hmlc", 6);
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

        printf("Format: HMLB (compressed bundle)\n");
        printf("Version: %d\n", version);
        printf("Uncompressed: %u bytes\n", orig_size);
        printf("Compressed: %ld bytes\n", compressed_size);
        if (orig_size == 0) {
            printf("Ratio: unavailable (empty uncompressed payload)\n");
        } else {
            double ratio = (1.0 - (double)compressed_size / (double)(uint64_t)orig_size) * 100;
            printf("Ratio: %.1f%% reduction\n", ratio);
        }
    } else {
        printf("Format: Unknown (magic: 0x%08x)\n", magic);
    }

    fclose(f);
    return 0;
}

// Bundle a .hml file with all its dependencies
static int bundle_file(const char *input_path, const char *output_path, int verbose, int compressed, int tree_shake) {
    BundleOptions opts = bundle_options_default();
    opts.verbose = verbose;
    opts.tree_shake = tree_shake;

    // Create bundle
    Bundle *bundle = bundle_create(input_path, &opts);
    if (!bundle) {
        fprintf(stderr, "Failed to create bundle from '%s'\n", input_path);
        return 1;
    }

    // Perform tree shaking if enabled
    if (tree_shake) {
        if (bundle_tree_shake(bundle, verbose) != 0) {
            fprintf(stderr, "Failed to perform tree shaking\n");
            bundle_free(bundle);
            return 1;
        }
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
        memcpy(final_output, input_path, len + 1);

        const char *ext = compressed ? ".hmlb" : ".hmlc";
        if (len > 4 && strcmp(input_path + len - 4, ".hml") == 0) {
            memcpy(final_output + len - 4, ext, 6);
        } else {
            memcpy(final_output + len, ext, 6);
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
static int package_file(const char *input_path, const char *output_path, int verbose, int compress, int tree_shake) {
    BundleOptions opts = bundle_options_default();
    opts.verbose = verbose;
    opts.tree_shake = tree_shake;

    // Create bundle
    Bundle *bundle = bundle_create(input_path, &opts);
    if (!bundle) {
        fprintf(stderr, "Failed to create bundle from '%s'\n", input_path);
        return 1;
    }

    // Perform tree shaking if enabled
    if (tree_shake) {
        if (bundle_tree_shake(bundle, verbose) != 0) {
            fprintf(stderr, "Failed to perform tree shaking\n");
            bundle_free(bundle);
            return 1;
        }
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
        payload_magic = HMLB_MAGIC;  // compressed bundle
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
#ifdef _WIN32
    ssize_t len = hml_get_executable_path(exe_path, sizeof(exe_path))
                      ? (ssize_t)strlen(exe_path) : -1;
#else
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
#endif
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
        memcpy(final_output, input_path, input_len + 1);

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

// Run a compiled bundle (.hmlc uncompressed or .hmlb compressed).
static void run_compiled_file(const char *path, int argc, char **argv, int stack_depth, int sandbox_flags, const char *sandbox_root) {
    size_t payload_size;
    uint8_t *payload = read_binary_file(path, &payload_size);
    if (payload == NULL) {
        exit(1);
    }

    int stmt_count;
    Stmt **statements = deserialize_compiled_payload(payload, payload_size, &stmt_count, path);
    free(payload);
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

    // Configure sandbox if enabled
    if (sandbox_flags != 0) {
        ctx->sandbox_flags = sandbox_flags;
        if (sandbox_root) {
            ctx->sandbox_root = strdup(sandbox_root);
        }
    }

    register_builtins(env, argc, argv, ctx);

    // Execute
    eval_program(statements, stmt_count, env, ctx);

    // Cleanup
    exec_context_free(ctx);
    env_break_cycles(env);
    env_release(env);

    for (int i = 0; i < stmt_count; i++) {
        stmt_free(statements[i]);
    }
    free(statements);

    ffi_cleanup();
    set_current_source_file(NULL);
}

// Check if file has a compiled bundle extension
static int is_compiled_extension(const char *path) {
    size_t len = strlen(path);
    return (len > 5 && strcmp(path + len - 5, ".hmlc") == 0) ||
           (len > 5 && strcmp(path + len - 5, ".hmlb") == 0);
}

static int is_compiled_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return 0;

    uint8_t magic_bytes[4];
    size_t read = fread(magic_bytes, 1, 4, f);
    fclose(f);

    if (read != 4) return 0;

    uint32_t magic = read_le32(magic_bytes);
    return magic == HMLC_MAGIC || magic == HMLB_MAGIC;
}

static void run_repl(int stack_depth) {
    char line[1024];
    char input_buffer[2048];  // Buffer for input with optional semicolon
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
        size_t len = strlen(line);
        if (len == 0) {
            continue;
        }

        // REPL convenience: auto-append semicolon if missing
        // This makes single expressions like "2 + 2" work without requiring "2 + 2;"
        const char *input = line;
        if (len > 0 && line[len - 1] != ';' && line[len - 1] != '}') {
            snprintf(input_buffer, sizeof(input_buffer), "%s;", line);
            input = input_buffer;
        }

        // Parse and execute
        Lexer lexer;
        lexer_init(&lexer, input);

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
    printf("    %s --bundle FILE [-o OUTPUT] [--compress] [--tree-shake] [--verbose]\n", program);
    printf("    %s --package FILE [-o OUTPUT] [--no-compress] [--tree-shake] [--verbose]\n", program);
    printf("    %s check [--json] FILE...\n", program);
    printf("    %s format FILE [--check]\n", program);
    printf("    %s lsp [--stdio | --tcp PORT]\n\n", program);
    printf("ARGUMENTS:\n");
    printf("    <FILE>       Hemlock script file to execute (.hml, .hmlc, or .hmlb)\n");
    printf("    <ARGS>...    Arguments passed to the script (available in 'args' array)\n\n");
    printf("SUBCOMMANDS:\n");
    printf("    check        Static analysis without execution (syntax, lint, types, borrow)\n");
    printf("        --json       Output diagnostics as JSON (see 'check --help')\n");
    printf("        --strict-types / --borrow-strict / --lint-strict / --no-lint\n");
    printf("    format       Format Hemlock source code\n");
    printf("        --check      Check if file is formatted (exit 1 if not)\n");
    printf("    lsp          Start Language Server Protocol server\n");
    printf("        --stdio      Use stdio transport (default)\n");
    printf("        --tcp PORT   Use TCP transport on specified port\n");
    printf("    profile      Profile script execution (CPU time, memory, call counts)\n");
    printf("        --cpu        CPU/time profiling (default)\n");
    printf("        --memory     Memory allocation profiling\n");
    printf("        --json       Output in JSON format\n");
    printf("        --flamegraph Output in flamegraph-compatible format\n");
    printf("        --top N      Show top N entries (default: 20)\n\n");
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
    printf("    --tree-shake         Remove unused exports from bundle (dead code elimination)\n");
    printf("    --no-compress        Skip compression (faster startup, larger binary)\n");
    printf("    --info <FILE>        Show info about a .hmlc/.hmlb file\n");
    printf("    -o, --output <FILE>  Output path for compiled/bundled/packaged file\n");
    printf("    --debug              Include line numbers in compiled output\n");
    printf("    --verbose            Print progress during bundling/packaging\n");
    printf("    --stack-depth <N>    Set maximum call stack depth (default: 10000)\n");
    printf("    --sandbox [DIR]      Run in sandbox mode (restricts dangerous operations)\n");
    printf("                         Disables: FFI, network, process spawning, file writes,\n");
    printf("                         signals (signal, raise, kill, abort)\n");
    printf("                         If DIR provided, restricts file reads to that directory\n\n");
    printf("EXAMPLES:\n");
    printf("    %s                     # Start interactive REPL\n", program);
    printf("    %s script.hml          # Run script.hml\n", program);
    printf("    %s script.hmlc         # Run compiled script\n", program);
    printf("    %s app.hmlb            # Run compressed bundle\n", program);
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
    printf("    %s check script.hml    # Static analysis (no execution)\n", program);
    printf("    %s check --json script.hml  # Machine-readable diagnostics\n", program);
    printf("    %s lsp                 # Start LSP server (stdio)\n", program);
    printf("    %s lsp --tcp 6969      # Start LSP server (TCP)\n", program);
    printf("    %s --sandbox script.hml    # Run in sandbox mode\n", program);
    printf("    %s --sandbox /tmp script.hml   # Sandbox with /tmp as allowed dir\n\n", program);
    printf("For more information, visit: https://github.com/hemlang/hemlock\n");
}

// Run profiler
static int run_profile(int argc, char **argv) {
    ProfileMode mode = PROFILE_MODE_CPU;
    ProfileOutputFormat output_format = PROFILE_OUTPUT_TEXT;
    int top_n = 20;
    bool show_leaks_only = false;
    const char *output_file = NULL;
    const char *file_to_run = NULL;
    int first_script_arg = 0;

    // Parse profile-specific options
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--cpu") == 0) {
            mode = PROFILE_MODE_CPU;
        } else if (strcmp(argv[i], "--memory") == 0) {
            mode = PROFILE_MODE_MEMORY;
        } else if (strcmp(argv[i], "--calls") == 0) {
            mode = PROFILE_MODE_CALLS;
        } else if (strcmp(argv[i], "--leaks") == 0) {
            mode = PROFILE_MODE_MEMORY;  // Leaks implies memory mode
            show_leaks_only = true;
        } else if (strcmp(argv[i], "--json") == 0) {
            output_format = PROFILE_OUTPUT_JSON;
        } else if (strcmp(argv[i], "--flamegraph") == 0) {
            output_format = PROFILE_OUTPUT_FLAMEGRAPH;
        } else if (strcmp(argv[i], "--top") == 0) {
            if (i + 1 < argc) {
                top_n = (int)strtol(argv[i + 1], NULL, 10);
                i++;
            }
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 < argc) {
                output_file = argv[i + 1];
                i++;
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Hemlock Profiler\n\n");
            printf("USAGE:\n");
            printf("    hemlock profile [OPTIONS] <FILE> [ARGS...]\n\n");
            printf("OPTIONS:\n");
            printf("    --cpu            CPU/time profiling (default)\n");
            printf("    --memory         Memory allocation profiling\n");
            printf("    --leaks          Show only unfreed allocations (implies --memory)\n");
            printf("    --calls          Call counts only (minimal overhead)\n");
            printf("    --json           Output in JSON format\n");
            printf("    --flamegraph     Output in flamegraph-compatible format\n");
            printf("    --top N          Show top N entries (default: 20)\n");
            printf("    -o, --output F   Write profile to file F (default: stdout)\n");
            printf("    -h, --help       Display this help message\n\n");
            printf("EXAMPLES:\n");
            printf("    hemlock profile script.hml\n");
            printf("    hemlock profile --top 50 script.hml\n");
            printf("    hemlock profile --json -o profile.json script.hml\n");
            printf("    hemlock profile --flamegraph script.hml | flamegraph.pl > out.svg\n");
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            fprintf(stderr, "Try 'hemlock profile --help' for more information.\n");
            return 1;
        } else {
            // First non-flag argument is the file to run
            file_to_run = argv[i];
            first_script_arg = i;
            break;
        }
    }

    if (!file_to_run) {
        fprintf(stderr, "Error: No input file specified\n");
        fprintf(stderr, "Usage: hemlock profile [OPTIONS] <FILE> [ARGS...]\n");
        return 1;
    }

    // Initialize profiler
    ProfilerState *profiler = profiler_new(mode);
    profiler->output_format = output_format;
    profiler->top_n = top_n;
    profiler->show_leaks_only = show_leaks_only;

    // Read and parse the file
    char *source = NULL;
    FILE *f = fopen(file_to_run, "rb");
    if (!f) {
        fprintf(stderr, "Error: Could not open file '%s'\n", file_to_run);
        profiler_free(profiler);
        return 1;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "Error: Could not seek to end of file\n");
        fclose(f);
        profiler_free(profiler);
        return 1;
    }
    long size = ftell(f);
    if (size < 0) {
        fprintf(stderr, "Error: Could not determine file size\n");
        fclose(f);
        profiler_free(profiler);
        return 1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Error: Could not seek to beginning of file\n");
        fclose(f);
        profiler_free(profiler);
        return 1;
    }
    source = malloc((size_t)size + 1);
    if (!source) {
        fprintf(stderr, "Error: Out of memory reading file '%s'\n", file_to_run);
        fclose(f);
        profiler_free(profiler);
        return 1;
    }
    size_t bytes_read = fread(source, 1, (size_t)size, f);
    if ((long)bytes_read != size) {
        fprintf(stderr, "Error: Could not read complete file (read %zu of %ld bytes)\n", bytes_read, size);
        free(source);
        fclose(f);
        profiler_free(profiler);
        return 1;
    }
    source[size] = '\0';
    fclose(f);

    // Initialize FFI
    ffi_init();

    // Set current source file and source code for stack traces and error context
    set_current_source_file(file_to_run);
    set_current_source_code(source);

    // Parse
    Lexer lexer;
    lexer_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    int stmt_count;
    Stmt **statements = parse_program(&parser, &stmt_count);

    if (parser.had_error) {
        fprintf(stderr, "Parse failed!\n");
        free(source);
        profiler_free(profiler);
        ffi_cleanup();
        set_current_source_file(NULL);
        set_current_source_code(NULL);
        return 1;
    }

    // Resolve and optimize
    resolve_program(statements, stmt_count);
    optimize_program(statements, stmt_count);

    // Create execution environment with profiler
    Environment *env = env_new(NULL);
    ExecutionContext *ctx = exec_context_new();
    ctx->profiler = profiler;

    int script_argc = argc - first_script_arg;
    char **script_argv = &argv[first_script_arg];
    register_builtins(env, script_argc, script_argv, ctx);

    // Start profiling and run
    profiler_start(profiler);
    eval_program(statements, stmt_count, env, ctx);
    profiler_stop(profiler);

    // Output results
    FILE *output = stdout;
    if (output_file) {
        output = fopen(output_file, "w");
        if (!output) {
            fprintf(stderr, "Error: Could not open output file '%s'\n", output_file);
            output = stdout;
        }
    }

    switch (output_format) {
        case PROFILE_OUTPUT_JSON:
            profiler_print_json(profiler, output);
            break;
        case PROFILE_OUTPUT_FLAMEGRAPH:
            profiler_print_flamegraph(profiler, output);
            break;
        default:
            profiler_print_report(profiler, output);
            break;
    }

    if (output != stdout) {
        fclose(output);
    }

    // Cleanup
    exec_context_free(ctx);
    env_break_cycles(env);
    env_release(env);
    for (int i = 0; i < stmt_count; i++) {
        stmt_free(statements[i]);
    }
    free(statements);
    free(source);
    profiler_free(profiler);
    ffi_cleanup();
    set_current_source_file(NULL);
    set_current_source_code(NULL);
    cleanup_object_types();
    cleanup_enum_types();

    return 0;
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
                tcp_port = (int)strtol(argv[i + 1], NULL, 10);
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

// Run formatter subcommand
static int run_format(int argc, char **argv) {
    int check_mode = 0;
    const char *file_to_format = NULL;

    // Parse format-specific options
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--check") == 0) {
            check_mode = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Hemlock Code Formatter\n\n");
            printf("USAGE:\n");
            printf("    hemlock format [OPTIONS] <FILE>\n\n");
            printf("ARGUMENTS:\n");
            printf("    <FILE>       Hemlock source file to format (.hml)\n\n");
            printf("OPTIONS:\n");
            printf("    --check      Check if file is formatted (exit 1 if not)\n");
            printf("    -h, --help   Display this help message\n\n");
            printf("STYLE:\n");
            printf("    - Tab indentation\n");
            printf("    - K&R brace style\n");
            printf("    - Max line width: 100 characters\n");
            printf("    - Trailing commas in multiline contexts\n");
            printf("    - Single blank line maximum\n");
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            fprintf(stderr, "Try 'hemlock format --help' for more information.\n");
            return 1;
        } else {
            file_to_format = argv[i];
        }
    }

    if (file_to_format == NULL) {
        fprintf(stderr, "Error: No input file specified\n");
        fprintf(stderr, "Try 'hemlock format --help' for more information.\n");
        return 1;
    }

    if (check_mode) {
        int result = format_check(file_to_format);
        if (result == 0) {
            printf("%s: formatted\n", file_to_format);
            return 0;
        } else if (result == 1) {
            printf("%s: not formatted\n", file_to_format);
            return 1;
        } else {
            return 1;  // Error already printed
        }
    } else {
        int result = format_file(file_to_format);
        if (result == 0) {
            printf("Formatted %s\n", file_to_format);
        }
        return result;
    }
}

// ============================================================================
// `hemlock check` — static analysis without execution
//
// Runs the same static passes as the compiler (syntax, lint, type check,
// borrow check) but never executes or generates code, collects every
// diagnostic instead of stopping at the first failing pass, and can emit
// them as JSON for tools and agents.
// ============================================================================

// One collected diagnostic, normalized across all passes.
typedef struct CheckDiag {
    const char *file;       // borrowed (argv)
    int line;               // 1-based
    int column;             // 1-based when known, 0 = unknown
    int end_column;
    int is_warning;         // 0 = error, 1 = warning
    const char *pass;       // "parse" | "lint" | "types" | "borrow"
    char *message;          // owned
    int seq;                // insertion order (stable sort tiebreaker)
} CheckDiag;

typedef struct CheckDiagList {
    CheckDiag *items;
    int count;
    int capacity;
    int errors;
    int warnings;
} CheckDiagList;

typedef struct CheckOptions {
    int json;
    int lint;
    int lint_strict;
    int strict_types;
    int borrow_strict;
    int deny_warnings;
} CheckOptions;

static void check_collect(CheckDiagList *list, const char *file, int line,
                          int column, int end_column, int is_warning,
                          const char *pass, const char *message) {
    if (list->count >= list->capacity) {
        int new_capacity = list->capacity == 0 ? 64 : list->capacity * 2;
        CheckDiag *items = realloc(list->items, sizeof(CheckDiag) * new_capacity);
        if (!items) return;
        list->items = items;
        list->capacity = new_capacity;
    }

    CheckDiag *d = &list->items[list->count];
    d->file = file;
    d->line = line;
    d->column = column;
    d->end_column = end_column;
    d->is_warning = is_warning;
    d->pass = pass;
    d->message = strdup(message ? message : "");
    d->seq = list->count;
    if (!d->message) return;
    list->count++;

    if (is_warning) {
        list->warnings++;
    } else {
        list->errors++;
    }
}

// Order a file's diagnostics by source position (passes run whole-file, so
// without this the output would be grouped by pass instead).
static int check_diag_compare(const void *a, const void *b) {
    const CheckDiag *da = a;
    const CheckDiag *db = b;
    if (da->line != db->line) return da->line - db->line;
    if (da->column != db->column) return da->column - db->column;
    return da->seq - db->seq;
}

// Check a single file: parse, then (if the AST is valid) run the compiler's
// static passes in its order — resolve, lint on the source as written,
// optimize, type check, borrow check. Returns -1 only on I/O failure.
static int check_one_file(const char *path, const CheckOptions *opts,
                          CheckDiagList *list) {
    char *source = read_file(path);
    if (!source) {
        return -1;
    }

    int first = list->count;

    Lexer lexer;
    lexer_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);
    parser_enable_error_collection(&parser);

    int stmt_count = 0;
    Stmt **statements = parse_program(&parser, &stmt_count);

    for (ParseError *pe = parser.errors; pe; pe = pe->next) {
        int end_col = pe->length > 0 ? pe->column + pe->length : pe->column;
        check_collect(list, path, pe->line, pe->column, end_col, 0,
                      "parse", pe->message);
    }
    parser_free_errors(&parser);

    // The later passes need a well-formed AST; after a syntax error the
    // recovered tree contains error placeholders that would only produce
    // misleading follow-on diagnostics.
    if (!parser.had_error && statements) {
        resolve_program(statements, stmt_count);

        if (opts->lint) {
            LintContext *lc = lint_new(path);
            if (lc) {
                lc->strict = opts->lint_strict;
                lint_enable_collection(lc, source);
                lint_program(lc, statements, stmt_count);
                for (LintDiag *d = lc->diags; d; d = d->next) {
                    check_collect(list, path, d->line, d->column, d->end_column,
                                  !d->is_error, "lint", d->message);
                }
                lint_free(lc);
            }
        }

        optimize_program(statements, stmt_count);

        TypeCheckContext *tc = type_check_new(path);
        if (tc) {
            tc->warn_implicit_any = opts->strict_types;
            type_check_enable_collection(tc, source);
            type_check_program(tc, statements, stmt_count);
            for (TypeCheckError *e = type_check_get_errors(tc); e; e = e->next) {
                check_collect(list, path, e->line, e->column, e->end_column,
                              e->is_warning, "types", e->message);
            }
            type_check_free(tc);
        }

        BorrowContext *bc = borrow_check_new(path);
        if (bc) {
            bc->strict = opts->borrow_strict;
            borrow_check_enable_collection(bc, source);
            borrow_check_program(bc, statements, stmt_count);
            for (BorrowDiag *d = bc->diags; d; d = d->next) {
                check_collect(list, path, d->line, d->column, d->end_column,
                              !d->is_error, "borrow", d->message);
            }
            borrow_check_free(bc);
        }
    }

    if (list->count > first) {
        qsort(list->items + first, list->count - first, sizeof(CheckDiag),
              check_diag_compare);
    }

    if (statements) {
        for (int i = 0; i < stmt_count; i++) {
            stmt_free(statements[i]);
        }
        free(statements);
    }
    free(source);
    return 0;
}

static void check_print_json_string(const char *s) {
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
            case '"':  fputs("\\\"", stdout); break;
            case '\\': fputs("\\\\", stdout); break;
            case '\n': fputs("\\n", stdout); break;
            case '\r': fputs("\\r", stdout); break;
            case '\t': fputs("\\t", stdout); break;
            default:
                if (*p < 0x20) {
                    printf("\\u%04x", *p);
                } else {
                    putchar(*p);
                }
        }
    }
    putchar('"');
}

static void check_print_json(const CheckDiagList *list, int files_checked) {
    printf("{\n");
    printf("  \"version\": 1,\n");
    printf("  \"files\": %d,\n", files_checked);
    printf("  \"errors\": %d,\n", list->errors);
    printf("  \"warnings\": %d,\n", list->warnings);
    printf("  \"diagnostics\": [");
    for (int i = 0; i < list->count; i++) {
        const CheckDiag *d = &list->items[i];
        printf("%s\n    {\"file\": ", i == 0 ? "" : ",");
        check_print_json_string(d->file);
        printf(", \"line\": %d, \"column\": %d, \"end_column\": %d, "
               "\"severity\": \"%s\", \"pass\": \"%s\", \"message\": ",
               d->line, d->column, d->end_column,
               d->is_warning ? "warning" : "error", d->pass);
        check_print_json_string(d->message);
        printf("}");
    }
    printf("%s  ]\n}\n", list->count > 0 ? "\n" : "");
}

static void check_print_text(const CheckDiagList *list, int files_checked) {
    for (int i = 0; i < list->count; i++) {
        const CheckDiag *d = &list->items[i];
        const char *severity = d->is_warning ? "warning" : "error";
        if (d->column > 0) {
            printf("%s:%d:%d: %s: %s [%s]\n", d->file, d->line, d->column,
                   severity, d->message, d->pass);
        } else {
            printf("%s:%d: %s: %s [%s]\n", d->file, d->line,
                   severity, d->message, d->pass);
        }
    }
    printf("%d error%s, %d warning%s (%d file%s checked)\n",
           list->errors, list->errors == 1 ? "" : "s",
           list->warnings, list->warnings == 1 ? "" : "s",
           files_checked, files_checked == 1 ? "" : "s");
}

// Run static checks (parse + lint + type check + borrow check) subcommand
static int run_check(int argc, char **argv) {
    CheckOptions opts = {
        .json = 0,
        .lint = 1,
        .lint_strict = 0,
        .strict_types = 0,
        .borrow_strict = 0,
        .deny_warnings = 0,
    };
    const char **files = malloc(sizeof(char*) * (size_t)(argc > 2 ? argc : 2));
    int num_files = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) {
            opts.json = 1;
        } else if (strcmp(argv[i], "--strict-types") == 0) {
            opts.strict_types = 1;
        } else if (strcmp(argv[i], "--borrow-strict") == 0) {
            opts.borrow_strict = 1;
        } else if (strcmp(argv[i], "--lint-strict") == 0) {
            opts.lint_strict = 1;
        } else if (strcmp(argv[i], "--no-lint") == 0) {
            opts.lint = 0;
        } else if (strcmp(argv[i], "--deny-warnings") == 0) {
            opts.deny_warnings = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Hemlock Static Checker\n\n");
            printf("Parses and analyzes source files without executing them, running the\n");
            printf("same static passes as the compiler: syntax, lint, type check, borrow\n");
            printf("check. Reports every diagnostic found instead of stopping at the first\n");
            printf("failing pass.\n\n");
            printf("USAGE:\n");
            printf("    hemlock check [OPTIONS] <FILE>...\n\n");
            printf("OPTIONS:\n");
            printf("    --json           Output diagnostics as JSON (stable schema for tools)\n");
            printf("    --strict-types   Strict type checking (warn on implicit any)\n");
            printf("    --borrow-strict  Strict borrow checking (move tracking + leak detection)\n");
            printf("    --lint-strict    Strict lint (also flag unused variables)\n");
            printf("    --deny-warnings  Exit 1 when any warning is reported (for CI)\n");
            printf("    --no-lint        Disable the lint pass\n");
            printf("    -h, --help       Display this help message\n\n");
            printf("EXIT CODE:\n");
            printf("    0  no errors (warnings do not fail the check)\n");
            printf("    1  one or more errors found\n");
            printf("    2  usage or I/O error\n\n");
            printf("OUTPUT:\n");
            printf("    Text mode prints one diagnostic per line:\n");
            printf("        <file>:<line>:<column>: <severity>: <message> [<pass>]\n");
            printf("    JSON mode prints a single object:\n");
            printf("        { \"version\": 1, \"files\": N, \"errors\": N, \"warnings\": N,\n");
            printf("          \"diagnostics\": [ { \"file\", \"line\", \"column\", \"end_column\",\n");
            printf("            \"severity\": \"error\"|\"warning\",\n");
            printf("            \"pass\": \"parse\"|\"lint\"|\"types\"|\"borrow\", \"message\" } ] }\n");
            printf("    Lines and columns are 1-based; column 0 means unknown.\n");
            free(files);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            fprintf(stderr, "Try 'hemlock check --help' for more information.\n");
            free(files);
            return 2;
        } else {
            files[num_files++] = argv[i];
        }
    }

    if (num_files == 0) {
        fprintf(stderr, "Error: No input file specified\n");
        fprintf(stderr, "Try 'hemlock check --help' for more information.\n");
        free(files);
        return 2;
    }

    CheckDiagList list = {0};
    int io_error = 0;
    int files_checked = 0;
    for (int i = 0; i < num_files; i++) {
        if (check_one_file(files[i], &opts, &list) != 0) {
            io_error = 1;
        } else {
            files_checked++;
        }
    }

    if (opts.json) {
        check_print_json(&list, files_checked);
    } else {
        check_print_text(&list, files_checked);
    }

    // Warnings do not fail the check by default; `warnings_exit_zero` pins that.
    // --deny-warnings is the opt-in for a caller that wants the borrow pass to
    // gate something. Without it a proven double free exits 0, so the checker
    // could diagnose a fault it could not stop.
    int failing = list.errors > 0 || (opts.deny_warnings && list.warnings > 0);
    int exit_code = io_error ? 2 : (failing ? 1 : 0);

    for (int i = 0; i < list.count; i++) {
        free(list.items[i].message);
    }
    free(list.items);
    free(files);
    cleanup_object_types();
    cleanup_enum_types();

    return exit_code;
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
    int bundle_tree_shake = 0;
    int package_mode = 0;
    int info_mode = 0;
    int stack_depth = 0;  // 0 = use default (DEFAULT_MAX_STACK_DEPTH)
    int sandbox_flags = 0;  // 0 = no sandbox, otherwise HML_SANDBOX_RESTRICT_* flags
    const char *sandbox_root = NULL;  // Optional directory to restrict file access to
    const char *file_to_info = NULL;
    const char *file_to_run = NULL;
    const char *file_to_compile = NULL;
    const char *file_to_bundle = NULL;
    const char *file_to_package = NULL;
    const char *output_path = NULL;
    const char *command_to_run = NULL;
    int first_script_arg = 0;  // Index of first argument to pass to script

    // Check for subcommands first
    if (argc >= 2 && strcmp(argv[1], "lsp") == 0) {
        return run_lsp(argc, argv);
    }
    if (argc >= 2 && strcmp(argv[1], "profile") == 0) {
        return run_profile(argc, argv);
    }

    // Check for format subcommand
    if (argc >= 2 && strcmp(argv[1], "format") == 0) {
        return run_format(argc, argv);
    }

    // Check for check subcommand (static analysis without execution)
    if (argc >= 2 && strcmp(argv[1], "check") == 0) {
        return run_check(argc, argv);
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
            if (strcmp(argv[i + 1], "-h") == 0 || strcmp(argv[i + 1], "--help") == 0) {
                print_help(argv[0]);
                return 0;
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
        } else if (strcmp(argv[i], "--tree-shake") == 0) {
            bundle_tree_shake = 1;
        } else if (strcmp(argv[i], "--stack-depth") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --stack-depth requires a numeric argument\n");
                fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
                return 1;
            }
            stack_depth = (int)strtol(argv[i + 1], NULL, 10);
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
        } else if (strcmp(argv[i], "--sandbox") == 0) {
            // Enable sandbox mode with all restrictions
            sandbox_flags = HML_SANDBOX_RESTRICT_FFI |
                           HML_SANDBOX_RESTRICT_NETWORK |
                           HML_SANDBOX_RESTRICT_PROCESS |
                           HML_SANDBOX_RESTRICT_FILE_WRITE |
                           HML_SANDBOX_RESTRICT_SIGNALS;
            // Check if next argument is an optional directory (not a flag and not a .hml file)
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                const char *next = argv[i + 1];
                size_t len = strlen(next);
                // If it doesn't end with .hml/.hmlc/.hmlb, treat it as sandbox root
                if (len < 4 || (strcmp(next + len - 4, ".hml") != 0 &&
                               (len < 5 || strcmp(next + len - 5, ".hmlc") != 0) &&
                               (len < 5 || strcmp(next + len - 5, ".hmlb") != 0))) {
                    sandbox_root = next;
                    sandbox_flags |= HML_SANDBOX_RESTRICT_FILE_READ;  // Also restrict reads to sandbox root
                    i++;  // Skip the directory argument
                }
            }
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
        int result = bundle_file(file_to_bundle, output_path, bundle_verbose, bundle_compress, bundle_tree_shake);
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
        int result = package_file(file_to_package, output_path, bundle_verbose, compress, bundle_tree_shake);
        return result;
    }

    if (command_to_run != NULL) {
        // Execute code string
        ffi_init();
        if (has_modules(command_to_run)) {
            // Route through the module system so imports work, mirroring
            // run_file(). Without this, import statements in -e code bind
            // nothing and every imported name is undefined.
            set_current_source_file("<command-line>");
            set_current_source_code(command_to_run);

            ExecutionContext *ctx = exec_context_new();
            if (stack_depth > 0) {
                ctx->max_stack_depth = stack_depth;
            }
            if (sandbox_flags != 0) {
                ctx->sandbox_flags = sandbox_flags;
                if (sandbox_root) {
                    ctx->sandbox_root = strdup(sandbox_root);
                }
            }

            Environment *global_env = env_new(NULL);
            register_builtins(global_env, 0, NULL, ctx);

            int result = execute_source_with_modules(command_to_run, global_env, 0, NULL, ctx);

            env_break_cycles(global_env);
            env_release(global_env);
            exec_context_free(ctx);
            set_current_source_file(NULL);
            set_current_source_code(NULL);

            if (result != 0) {
                ffi_cleanup();
                cleanup_object_types();
                cleanup_enum_types();
                return 1;
            }
        } else {
            run_source(command_to_run, 0, NULL, stack_depth, sandbox_flags, sandbox_root);
        }
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

        // Check if it's a compiled bundle (.hmlc or .hmlb)
        if (is_compiled_extension(file_to_run) || is_compiled_file(file_to_run)) {
            run_compiled_file(file_to_run, script_argc, script_argv, stack_depth, sandbox_flags, sandbox_root);
        } else {
            run_file(file_to_run, script_argc, script_argv, stack_depth, sandbox_flags, sandbox_root);
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
