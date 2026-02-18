/*
 * Hemlock Interpreter - WebAssembly Entry Point
 *
 * Provides the main() and exported API for running the Hemlock interpreter
 * in a WASM environment (browser or Node.js via Emscripten).
 *
 * Exports:
 *   hemlock_eval(source)     - Parse and execute a Hemlock source string
 *   hemlock_version()        - Return version string
 *
 * Usage from JavaScript:
 *   Module.ccall('hemlock_eval', 'number', ['string'], ['print("Hello!");']);
 *
 * Or with cwrap:
 *   const hemlockEval = Module.cwrap('hemlock_eval', 'number', ['string']);
 *   hemlockEval('let x = 42; print(x);');
 */

#ifdef __EMSCRIPTEN__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <emscripten.h>
#include "frontend.h"
#include "interpreter.h"
#include "interpreter/internal.h"
#include "version.h"

/* FFI stubs (provided by wasm_interp_shim.c) */
extern void ffi_init(void);
extern void ffi_cleanup(void);

/* ========================================================================
 * Core interpreter execution
 * ======================================================================== */

static void run_source(const char *source) {
    /* Parse */
    Lexer lexer;
    lexer_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    int stmt_count;
    Stmt **statements = parse_program(&parser, &stmt_count);

    if (parser.had_error) {
        fprintf(stderr, "Parse failed!\n");
        for (int i = 0; i < stmt_count; i++) {
            stmt_free(statements[i]);
        }
        free(statements);
        return;
    }

    /* Resolve variables (compute depth/slot indices for O(1) lookup) */
    resolve_program(statements, stmt_count);

    /* Optimize AST (constant folding, boolean simplification, strength reduction) */
    optimize_program(statements, stmt_count);

    /* Execute */
    Environment *env = env_new(NULL);
    ExecutionContext *ctx = exec_context_new();

    register_builtins(env, 0, NULL, ctx);
    eval_program(statements, stmt_count, env, ctx);

    /* Cleanup */
    exec_context_free(ctx);
    env_break_cycles(env);
    env_release(env);
    for (int i = 0; i < stmt_count; i++) {
        stmt_free(statements[i]);
    }
    free(statements);
}

/* ========================================================================
 * Exported WASM API
 * ======================================================================== */

/*
 * hemlock_eval(source) - Parse and execute Hemlock source code
 *
 * Returns 0 on success, 1 on error.
 * Output goes to stdout/stderr (captured by Module.print/printErr in JS).
 */
EMSCRIPTEN_KEEPALIVE
int hemlock_eval(const char *source) {
    if (!source || !*source) {
        return 0;
    }

    ffi_init();
    set_current_source_file("<wasm>");
    set_current_source_code(source);

    run_source(source);

    ffi_cleanup();
    set_current_source_file(NULL);
    set_current_source_code(NULL);
    cleanup_object_types();
    cleanup_enum_types();

    return 0;
}

/*
 * hemlock_version() - Return the Hemlock version string
 */
EMSCRIPTEN_KEEPALIVE
const char* hemlock_version(void) {
    return HEMLOCK_VERSION;
}

/* ========================================================================
 * Main entry point (for command-line WASM via Node.js)
 * ======================================================================== */

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Hemlock %s (WebAssembly interpreter)\n", HEMLOCK_VERSION);
        printf("Usage: hemlock <file.hml>  or  hemlock -e '<code>'\n");
        printf("\nThis is the Hemlock tree-walking interpreter compiled to WASM.\n");
        printf("Some features are unavailable: FFI, OpenSSL crypto, fork/exec.\n");
        return 0;
    }

    /* Handle -e / -c flag for inline execution */
    if (argc >= 3 && (strcmp(argv[1], "-e") == 0 || strcmp(argv[1], "-c") == 0)) {
        ffi_init();
        set_current_source_file("<cmdline>");
        run_source(argv[2]);
        ffi_cleanup();
        cleanup_object_types();
        cleanup_enum_types();
        return 0;
    }

    /* Handle --version */
    if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0) {
        printf("Hemlock version %s (WASM)\n", HEMLOCK_VERSION);
        return 0;
    }

    /* Run file from virtual filesystem */
    const char *path = argv[1];
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: Could not open file '%s'\n", path);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *source = malloc(size + 1);
    if (!source) {
        fprintf(stderr, "Error: Could not allocate memory for file\n");
        fclose(f);
        return 1;
    }

    size_t bytes_read = fread(source, 1, size, f);
    fclose(f);

    if ((long)bytes_read != size) {
        fprintf(stderr, "Error: Could not read file '%s'\n", path);
        free(source);
        return 1;
    }
    source[size] = '\0';

    ffi_init();
    set_current_source_file(path);
    set_current_source_code(source);

    /* Check if file uses modules */
    int has_import = 0;
    const char *p = source;
    while (*p) {
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
        if (strncmp(p, "import ", 7) == 0 || strncmp(p, "import{", 7) == 0 ||
            strncmp(p, "export ", 7) == 0 || strncmp(p, "export{", 7) == 0) {
            has_import = 1;
            break;
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }

    if (has_import) {
        /* Execute with module system */
        Environment *global_env = env_new(NULL);
        ExecutionContext *ctx = exec_context_new();
        register_builtins(global_env, argc - 1, argv + 1, ctx);
        execute_file_with_modules(path, global_env, argc - 1, argv + 1, ctx);
        env_break_cycles(global_env);
        env_release(global_env);
        exec_context_free(ctx);
    } else {
        run_source(source);
    }

    free(source);
    ffi_cleanup();
    set_current_source_file(NULL);
    set_current_source_code(NULL);
    cleanup_object_types();
    cleanup_enum_types();

    return 0;
}

#endif /* __EMSCRIPTEN__ */
