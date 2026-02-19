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
 *   Persistent Context API:
 *   hemlock_context_create()                - Allocate persistent env, return handle
 *   hemlock_context_eval(handle, source)    - Execute in persistent env
 *   hemlock_context_destroy(handle)         - Tear down persistent env
 *   hemlock_context_get(handle, varname)    - Read variable as JSON string
 *   hemlock_context_set(handle, varname, json) - Inject variable from JSON
 *
 * Usage from JavaScript:
 *   Module.ccall('hemlock_eval', 'number', ['string'], ['print("Hello!");']);
 *
 * Or with cwrap:
 *   const hemlockEval = Module.cwrap('hemlock_eval', 'number', ['string']);
 *   hemlockEval('let x = 42; print(x);');
 *
 * Persistent context usage:
 *   const ctxCreate  = Module.cwrap('hemlock_context_create',  'number', []);
 *   const ctxEval    = Module.cwrap('hemlock_context_eval',    'number', ['number','string']);
 *   const ctxDestroy = Module.cwrap('hemlock_context_destroy', null,     ['number']);
 *   const ctxGet     = Module.cwrap('hemlock_context_get',     'string', ['number','string']);
 *   const ctxSet     = Module.cwrap('hemlock_context_set',     'number', ['number','string','string']);
 *
 *   const ctx = ctxCreate();
 *   ctxEval(ctx, 'let x = 42;');
 *   ctxEval(ctx, 'x = x + 1;');
 *   console.log(ctxGet(ctx, 'x'));  // "43"
 *   ctxSet(ctx, 'name', '"Alice"');
 *   ctxEval(ctx, 'print(name);');   // Alice
 *   ctxDestroy(ctx);
 */

#ifdef __EMSCRIPTEN__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <emscripten.h>
#include "frontend.h"
#include "interpreter.h"
#include "interpreter/internal.h"
#include "interpreter/io/internal.h"
#include "version.h"

/* FFI stubs (provided by wasm_interp_shim.c) */
extern void ffi_init(void);
extern void ffi_cleanup(void);

/* Length of "import " / "import{" / "export " / "export{" prefixes */
#define MODULE_KEYWORD_LEN 7

/* ========================================================================
 * Core interpreter execution
 * ======================================================================== */

static void run_source(const char *source) {
    /* Parse */
    Lexer lexer;
    lexer_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    int stmt_count = 0;
    Stmt **statements = parse_program(&parser, &stmt_count);

    if (parser.had_error) {
        fprintf(stderr, "Parse failed!\n");
        for (int i = 0; i < stmt_count; i++) {
            stmt_free(statements[i]);
        }
        free((void *)statements);
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
    free((void *)statements);
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
 * Persistent Context API
 *
 * Allows JS to create long-lived interpreter contexts where variables
 * survive across multiple eval() calls.  Each context holds its own
 * Environment (symbol table) and ExecutionContext (control-flow state).
 *
 * Handles are small positive integers (1-based) indexing into a static
 * table.  Handle 0 is reserved as an error sentinel.
 * ======================================================================== */

/* Per-context state */
typedef struct {
    Environment      *env;   /* Global environment (variables persist here) */
    ExecutionContext  *ctx;   /* Control-flow / exception state              */
    int               alive; /* 1 while the slot is in use                  */
} WasmContext;

/* Fixed-size context table (zero-initialized → all slots start dead) */
static WasmContext context_table[HML_MAX_WASM_CONTEXTS];

/* Validate a handle and return the WasmContext, or NULL on bad handle */
static WasmContext *context_lookup(int handle) {
    if (handle < 1 || handle > HML_MAX_WASM_CONTEXTS) {
        return NULL;
    }
    WasmContext *wc = &context_table[handle - 1];
    return wc->alive ? wc : NULL;
}

/*
 * hemlock_context_create() — allocate a persistent Environment +
 * ExecutionContext and return an integer handle (1-based).
 *
 * Returns 0 on failure (table full).
 */
EMSCRIPTEN_KEEPALIVE
int hemlock_context_create(void) {
    /* Find a free slot */
    int slot = -1;
    for (int i = 0; i < HML_MAX_WASM_CONTEXTS; i++) {
        if (!context_table[i].alive) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        fprintf(stderr, "hemlock_context_create: no free context slots "
                        "(max %d)\n", HML_MAX_WASM_CONTEXTS);
        return 0;
    }

    WasmContext *wc = &context_table[slot];
    wc->env = env_new(NULL);
    wc->ctx = exec_context_new();

    ffi_init();
    register_builtins(wc->env, 0, NULL, wc->ctx);

    wc->alive = 1;
    return slot + 1;  /* 1-based handle */
}

/*
 * hemlock_context_eval(handle, source) — parse and execute `source` inside
 * the persistent environment identified by `handle`.  Variables defined in
 * earlier calls are visible to later ones.
 *
 * Returns 0 on success, -1 on bad handle, 1 on parse error.
 */
EMSCRIPTEN_KEEPALIVE
int hemlock_context_eval(int handle, const char *source) {
    WasmContext *wc = context_lookup(handle);
    if (!wc) {
        fprintf(stderr, "hemlock_context_eval: invalid handle %d\n", handle);
        return -1;
    }
    if (!source || !*source) {
        return 0;
    }

    set_current_source_file("<wasm-ctx>");
    set_current_source_code(source);

    /* Clear any leftover control-flow state from a previous eval */
    wc->ctx->return_state.is_returning = 0;
    wc->ctx->loop_state.is_breaking    = 0;
    wc->ctx->loop_state.is_continuing  = 0;
    if (wc->ctx->exception_state.is_throwing) {
        VALUE_RELEASE(wc->ctx->exception_state.exception_value);
        wc->ctx->exception_state.is_throwing = 0;
    }

    /* Parse */
    Lexer lexer;
    lexer_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    int stmt_count = 0;
    Stmt **statements = parse_program(&parser, &stmt_count);

    if (parser.had_error) {
        fprintf(stderr, "Parse failed!\n");
        for (int i = 0; i < stmt_count; i++) {
            stmt_free(statements[i]);
        }
        free((void *)statements);
        set_current_source_file(NULL);
        set_current_source_code(NULL);
        return 1;
    }

    /* Resolve and optimize */
    resolve_program(statements, stmt_count);
    optimize_program(statements, stmt_count);

    /* Execute in the persistent environment */
    eval_program(statements, stmt_count, wc->env, wc->ctx);

    /* Free AST (the env retains any values it needs) */
    for (int i = 0; i < stmt_count; i++) {
        stmt_free(statements[i]);
    }
    free((void *)statements);
    set_current_source_file(NULL);
    set_current_source_code(NULL);

    return 0;
}

/*
 * hemlock_context_destroy(handle) — tear down the persistent environment
 * and free all associated memory.
 */
EMSCRIPTEN_KEEPALIVE
void hemlock_context_destroy(int handle) {
    WasmContext *wc = context_lookup(handle);
    if (!wc) {
        fprintf(stderr, "hemlock_context_destroy: invalid handle %d\n", handle);
        return;
    }

    env_break_cycles(wc->env);
    env_release(wc->env);
    exec_context_free(wc->ctx);

    wc->env   = NULL;
    wc->ctx   = NULL;
    wc->alive = 0;
}

/*
 * hemlock_context_get(handle, varname) — read a variable from the
 * persistent environment and return its value serialized as a JSON string.
 *
 * The returned pointer is allocated with malloc(); the JS side should call
 * Module._free() on it when done (or Emscripten's UTF8ToString copies it).
 *
 * Returns NULL on bad handle or if the variable does not exist.
 */
EMSCRIPTEN_KEEPALIVE
const char *hemlock_context_get(int handle, const char *varname) {
    WasmContext *wc = context_lookup(handle);
    if (!wc || !varname) {
        return NULL;
    }

    /* Look up the variable (env_get retains for us) */
    Value val = env_get(wc->env, varname, wc->ctx);

    /* If the variable was not found, env_get sets the exception state */
    if (wc->ctx->exception_state.is_throwing) {
        VALUE_RELEASE(wc->ctx->exception_state.exception_value);
        wc->ctx->exception_state.is_throwing = 0;
        return NULL;
    }

    /* Serialize to JSON */
    SerializeVisitedSet visited;
    serialize_visited_init(&visited);

    char *json = serialize_value(val, &visited, wc->ctx);

    serialize_visited_free(&visited);
    VALUE_RELEASE(val);

    /* If serialization failed, clear the exception and return NULL */
    if (!json && wc->ctx->exception_state.is_throwing) {
        VALUE_RELEASE(wc->ctx->exception_state.exception_value);
        wc->ctx->exception_state.is_throwing = 0;
    }

    return json;  /* caller (JS) frees via Module._free() */
}

/*
 * hemlock_context_set(handle, varname, json) — parse a JSON string and
 * inject the resulting value into the persistent environment.
 *
 * If the variable already exists it is updated; otherwise it is created
 * as a mutable (let) binding.
 *
 * Returns 0 on success, -1 on bad handle, 1 on JSON parse error.
 */
EMSCRIPTEN_KEEPALIVE
int hemlock_context_set(int handle, const char *varname, const char *json) {
    WasmContext *wc = context_lookup(handle);
    if (!wc || !varname || !json) {
        return -1;
    }

    /* Parse the JSON value */
    JSONParser jp;
    jp.input = json;
    jp.pos   = 0;

    Value val = json_parse_value(&jp, wc->ctx);

    if (wc->ctx->exception_state.is_throwing) {
        VALUE_RELEASE(wc->ctx->exception_state.exception_value);
        wc->ctx->exception_state.is_throwing = 0;
        VALUE_RELEASE(val);
        return 1;
    }

    /*
     * Try env_set first (updates existing variable).  If the variable
     * doesn't exist yet, env_set will throw — catch that and fall back
     * to env_define.
     */
    env_set(wc->env, varname, val, wc->ctx);

    if (wc->ctx->exception_state.is_throwing) {
        /* Variable not found — clear exception and define it instead */
        VALUE_RELEASE(wc->ctx->exception_state.exception_value);
        wc->ctx->exception_state.is_throwing = 0;

        env_define(wc->env, varname, val, 0 /* mutable */, wc->ctx);

        if (wc->ctx->exception_state.is_throwing) {
            VALUE_RELEASE(wc->ctx->exception_state.exception_value);
            wc->ctx->exception_state.is_throwing = 0;
            VALUE_RELEASE(val);
            return 1;
        }
    }

    VALUE_RELEASE(val);
    return 0;
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

    /* Check if file uses modules (heuristic: look for import/export at line start) */
    int has_import = 0;
    const char *p = source;
    while (*p) {
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
            p++;
        }
        if (strncmp(p, "import ", MODULE_KEYWORD_LEN) == 0 ||
            strncmp(p, "import{", MODULE_KEYWORD_LEN) == 0 ||
            strncmp(p, "export ", MODULE_KEYWORD_LEN) == 0 ||
            strncmp(p, "export{", MODULE_KEYWORD_LEN) == 0) {
            has_import = 1;
            break;
        }
        while (*p && *p != '\n') {
            p++;
        }
        if (*p == '\n') {
            p++;
        }
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
