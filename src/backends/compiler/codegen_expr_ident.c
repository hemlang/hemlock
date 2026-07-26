/*
 * Hemlock Code Generator - Identifier Expression Handling
 *
 * Handles code generation for EXPR_IDENT - variable references,
 * builtin constants, and builtin function references.
 *
 * Extracted from codegen_expr.c to reduce file size.
 */

#include "codegen_expr_internal.h"

// Forward declaration for recursive calls
char* codegen_expr(CodegenContext *ctx, Expr *expr);

/*
 * Detect a top-level (main file) name that is present in the locals list only
 * because it leaked in from the enclosing main scope when generating a closure
 * body. Closures are emitted after the main function body, which leaves its
 * locals in the array, so those names appear "local" inside main-file closures
 * even though they are not declared as C variables in the generated closure
 * function.
 *
 * Such a leaked name must resolve to its `_main_`-prefixed static global, not a
 * bare C identifier. A name is a leaked main local when, inside a closure, it is
 * a main variable that is NOT a genuine local of this closure: not a parameter,
 * not one of the closure's captured variables, and not declared in the body
 * (its most recent locals entry sits below locals_body_start, i.e. in the
 * params/captures/leaked region rather than the body region).
 */
static int codegen_is_leaked_main_local(CodegenContext *ctx, const char *name) {
    if (!ctx->current_closure) return 0;
    if (!codegen_is_main_var(ctx, name)) return 0;
    if (codegen_is_func_param(ctx, name)) return 0;

    // Most recent declaration wins (matches C shadowing). If the latest entry is
    // a body-local, this is a genuine local that shadows the leaked name.
    int last = -1;
    for (int i = 0; i < ctx->num_locals; i++) {
        if (ctx->local_vars[i] && strcmp(ctx->local_vars[i], name) == 0) last = i;
    }
    if (last < 0 || last >= ctx->locals_body_start) return 0;

    // Below locals_body_start: a captured variable is genuinely declared (read
    // from the closure environment), so it should keep its bare name.
    for (int i = 0; i < ctx->current_closure->num_captured; i++) {
        if (strcmp(ctx->current_closure->captured_vars[i], name) == 0) return 0;
    }
    return 1;
}

/*
 * Handle EXPR_IDENT - generates code for identifier expressions.
 * This includes:
 * - Signal constants (SIGINT, SIGTERM, etc.)
 * - Socket constants (AF_INET, SOCK_STREAM, etc.)
 * - Math constants (__PI, __E, etc.)
 * - Math functions (__sin, __cos, etc.)
 * - Time/datetime functions
 * - Environment functions
 * - Process functions
 * - Filesystem functions
 * - System info functions
 * - Compression functions
 * - HTTP/WebSocket functions
 * - Cryptographic functions
 * - Variable lookups (local, module, main)
 *
 * Returns the temp variable name containing the result (same as result param).
 */
char* codegen_expr_ident(CodegenContext *ctx, Expr *expr, char *result) {
    // IMPORTANT: Check for local variables/parameters FIRST before builtin lookup.
    // This allows user code to shadow builtin names like 'fork', 'exec', etc.
    // We check: function parameters, scope variables, and local variables.
    if (codegen_is_func_param(ctx, expr->as.ident.name) ||
        (ctx->current_scope && scope_is_defined(ctx->current_scope, expr->as.ident.name)) ||
        codegen_is_shadow(ctx, expr->as.ident.name) ||
        codegen_is_local(ctx, expr->as.ident.name)) {
        // This is a local variable or parameter - handle it directly
        goto handle_variable;
    }

    // Handle 'self' specially - maps to hml_self global
    if (strcmp(expr->as.ident.name, "self") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_self;", result);
    // Handle I/O builtins as first-class functions
    } else if (strcmp(expr->as.ident.name, "print") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_print, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "println") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_println, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "write") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_write, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "eprint") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_eprint, 1, 1, 0);", result);
    // Handle memory builtins as first-class functions (needed for defer free(p))
    } else if (strcmp(expr->as.ident.name, "free") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_free_fn, 1, 1, 0);", result);
    // Handle signal constants (__prefixed only - unprefixed moved to stdlib)
    } else if (strcmp(expr->as.ident.name, "__SIGINT") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGINT);", result);
    } else if (strcmp(expr->as.ident.name, "__SIGTERM") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGTERM);", result);
    } else if (strcmp(expr->as.ident.name, "__SIGHUP") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGHUP);", result);
    } else if (strcmp(expr->as.ident.name, "__SIGQUIT") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGQUIT);", result);
    } else if (strcmp(expr->as.ident.name, "__SIGABRT") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGABRT);", result);
    } else if (strcmp(expr->as.ident.name, "__SIGUSR1") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGUSR1);", result);
    } else if (strcmp(expr->as.ident.name, "__SIGUSR2") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGUSR2);", result);
    } else if (strcmp(expr->as.ident.name, "__SIGALRM") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGALRM);", result);
    } else if (strcmp(expr->as.ident.name, "__SIGCHLD") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGCHLD);", result);
    } else if (strcmp(expr->as.ident.name, "__SIGPIPE") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGPIPE);", result);
    } else if (strcmp(expr->as.ident.name, "__SIGCONT") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGCONT);", result);
    } else if (strcmp(expr->as.ident.name, "__SIGSTOP") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGSTOP);", result);
    } else if (strcmp(expr->as.ident.name, "__SIGTSTP") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGTSTP);", result);
    } else if (strcmp(expr->as.ident.name, "__SIGTTIN") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGTTIN);", result);
    } else if (strcmp(expr->as.ident.name, "__SIGTTOU") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGTTOU);", result);
    // Handle signal functions (__prefixed - moved to @stdlib/signal)
    } else if (strcmp(expr->as.ident.name, "__signal") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_signal, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__raise") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_raise, 1, 1, 0);", result);
    // Handle socket constants (__prefixed only - unprefixed moved to stdlib)
    } else if (strcmp(expr->as.ident.name, "__AF_INET") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(AF_INET);", result);
    } else if (strcmp(expr->as.ident.name, "__AF_INET6") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(AF_INET6);", result);
    } else if (strcmp(expr->as.ident.name, "__AF_UNIX") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(AF_UNIX);", result);
    } else if (strcmp(expr->as.ident.name, "__SOCK_STREAM") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SOCK_STREAM);", result);
    } else if (strcmp(expr->as.ident.name, "__SOCK_DGRAM") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SOCK_DGRAM);", result);
    } else if (strcmp(expr->as.ident.name, "__SOL_SOCKET") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SOL_SOCKET);", result);
    } else if (strcmp(expr->as.ident.name, "__SO_REUSEADDR") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SO_REUSEADDR);", result);
    } else if (strcmp(expr->as.ident.name, "__SO_KEEPALIVE") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SO_KEEPALIVE);", result);
    } else if (strcmp(expr->as.ident.name, "__SO_RCVTIMEO") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SO_RCVTIMEO);", result);
    } else if (strcmp(expr->as.ident.name, "__SO_SNDTIMEO") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SO_SNDTIMEO);", result);
    } else if (strcmp(expr->as.ident.name, "__IPPROTO_TCP") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(IPPROTO_TCP);", result);
    } else if (strcmp(expr->as.ident.name, "__IPPROTO_UDP") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(IPPROTO_UDP);", result);
    // Poll constants (__prefixed only - unprefixed moved to stdlib)
    } else if (strcmp(expr->as.ident.name, "__POLLIN") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(POLLIN);", result);
    } else if (strcmp(expr->as.ident.name, "__POLLOUT") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(POLLOUT);", result);
    } else if (strcmp(expr->as.ident.name, "__POLLERR") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(POLLERR);", result);
    } else if (strcmp(expr->as.ident.name, "__POLLHUP") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(POLLHUP);", result);
    } else if (strcmp(expr->as.ident.name, "__POLLNVAL") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(POLLNVAL);", result);
    } else if (strcmp(expr->as.ident.name, "__POLLPRI") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(POLLPRI);", result);
    // Handle standard file descriptor constants
    } else if (strcmp(expr->as.ident.name, "__STDIN_FILENO") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(0);", result);
    } else if (strcmp(expr->as.ident.name, "__STDOUT_FILENO") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(1);", result);
    } else if (strcmp(expr->as.ident.name, "__STDERR_FILENO") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(2);", result);
    // Mmap constants
    } else if (strcmp(expr->as.ident.name, "__PROT_NONE") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(PROT_NONE);", result);
    } else if (strcmp(expr->as.ident.name, "__PROT_READ") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(PROT_READ);", result);
    } else if (strcmp(expr->as.ident.name, "__PROT_WRITE") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(PROT_WRITE);", result);
    } else if (strcmp(expr->as.ident.name, "__PROT_EXEC") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(PROT_EXEC);", result);
    } else if (strcmp(expr->as.ident.name, "__MADV_NORMAL") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(MADV_NORMAL);", result);
    } else if (strcmp(expr->as.ident.name, "__MADV_SEQUENTIAL") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(MADV_SEQUENTIAL);", result);
    } else if (strcmp(expr->as.ident.name, "__MADV_RANDOM") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(MADV_RANDOM);", result);
    } else if (strcmp(expr->as.ident.name, "__MADV_WILLNEED") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(MADV_WILLNEED);", result);
    } else if (strcmp(expr->as.ident.name, "__MADV_DONTNEED") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(MADV_DONTNEED);", result);
    // Handle TYPEID constants for typeid() builtin
    } else if (strcmp(expr->as.ident.name, "TYPEID_I8") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(0);", result);
    } else if (strcmp(expr->as.ident.name, "TYPEID_I16") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(1);", result);
    } else if (strcmp(expr->as.ident.name, "TYPEID_I32") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(2);", result);
    } else if (strcmp(expr->as.ident.name, "TYPEID_I64") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(3);", result);
    } else if (strcmp(expr->as.ident.name, "TYPEID_U8") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(4);", result);
    } else if (strcmp(expr->as.ident.name, "TYPEID_U16") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(5);", result);
    } else if (strcmp(expr->as.ident.name, "TYPEID_U32") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(6);", result);
    } else if (strcmp(expr->as.ident.name, "TYPEID_U64") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(7);", result);
    } else if (strcmp(expr->as.ident.name, "TYPEID_F32") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(8);", result);
    } else if (strcmp(expr->as.ident.name, "TYPEID_F64") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(9);", result);
    } else if (strcmp(expr->as.ident.name, "TYPEID_BOOL") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(10);", result);
    } else if (strcmp(expr->as.ident.name, "TYPEID_STRING") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(11);", result);
    } else if (strcmp(expr->as.ident.name, "TYPEID_RUNE") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(12);", result);
    } else if (strcmp(expr->as.ident.name, "TYPEID_PTR") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(13);", result);
    } else if (strcmp(expr->as.ident.name, "TYPEID_BUFFER") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(14);", result);
    } else if (strcmp(expr->as.ident.name, "TYPEID_ARRAY") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(15);", result);
    } else if (strcmp(expr->as.ident.name, "TYPEID_OBJECT") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(16);", result);
    } else if (strcmp(expr->as.ident.name, "TYPEID_FILE") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(17);", result);
    } else if (strcmp(expr->as.ident.name, "TYPEID_FUNCTION") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(18);", result);
    } else if (strcmp(expr->as.ident.name, "TYPEID_TASK") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(19);", result);
    } else if (strcmp(expr->as.ident.name, "TYPEID_CHANNEL") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(20);", result);
    } else if (strcmp(expr->as.ident.name, "TYPEID_NULL") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(21);", result);
    // Handle math constants (builtins)
    } else if (strcmp(expr->as.ident.name, "__PI") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_f64(3.14159265358979323846);", result);
    } else if (strcmp(expr->as.ident.name, "__E") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_f64(2.71828182845904523536);", result);
    } else if (strcmp(expr->as.ident.name, "__TAU") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_f64(6.28318530717958647692);", result);
    } else if (strcmp(expr->as.ident.name, "__INF") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_f64(1.0/0.0);", result);
    } else if (strcmp(expr->as.ident.name, "__NAN") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_f64(0.0/0.0);", result);
    // Handle math functions (builtins)
    } else if (strcmp(expr->as.ident.name, "__sin") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_sin, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__cos") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_cos, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__tan") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_tan, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__asin") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_asin, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__acos") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_acos, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__atan") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atan, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__atan2") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atan2, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__sqrt") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_sqrt, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__pow") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_pow, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__exp") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_exp, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__log") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_log, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__log10") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_log10, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__log2") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_log2, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__floor") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_floor, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__ceil") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_ceil, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__round") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_round, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__trunc") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_trunc, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__floori") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_floori, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__ceili") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_ceili, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__roundi") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_roundi, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__trunci") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_trunci, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__div") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_div, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__divi") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_divi, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__abs") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_abs, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__min") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_min, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__max") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_max, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__clamp") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_clamp, 3, 3, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__rand") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_rand, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__rand_range") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_rand_range, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__seed") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_seed, 1, 1, 0);", result);
    // Handle time functions (builtins)
    } else if (strcmp(expr->as.ident.name, "__now") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_now, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__time_ms") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_time_ms, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__clock") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_clock, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__sleep") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_sleep, 1, 1, 0);", result);
    // Handle datetime functions (builtins)
    } else if (strcmp(expr->as.ident.name, "__localtime") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_localtime, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__gmtime") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_gmtime, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__mktime") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_mktime, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__strftime") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_strftime, 2, 2, 0);", result);
    // Handle environment functions (builtins)
    } else if (strcmp(expr->as.ident.name, "__getenv") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_getenv, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__setenv") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_setenv, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__unsetenv") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_unsetenv, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__exit") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_exit, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__get_pid") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_get_pid, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__getppid") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_getppid, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__getuid") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_getuid, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__geteuid") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_geteuid, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__getgid") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_getgid, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__getegid") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_getegid, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__exec") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_exec, 1, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__exec_argv") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_exec_argv, 1, 1, 0);", result);
    // Handle process functions (builtins)
    } else if (strcmp(expr->as.ident.name, "__kill") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_kill, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__fork") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_fork, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__wait") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_wait, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__waitpid") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_waitpid, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__posix_spawn") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_posix_spawn, 2, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__abort") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_abort, 0, 0, 0);", result);
    // Handle pipe functions (builtins)
    } else if (strcmp(expr->as.ident.name, "__pipe") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_pipe, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__close_fd") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_close_fd, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__read_fd") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_read_fd, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__write_fd") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_write_fd, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__dup2") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_dup2, 2, 2, 0);", result);
    // Handle filesystem functions (builtins)
    } else if (strcmp(expr->as.ident.name, "__exists") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_exists, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__read_file") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_read_file, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__write_file") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_write_file, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__append_file") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_append_file, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__remove_file") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_remove_file, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__rename") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_rename, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__copy_file") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_copy_file, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__is_file") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_is_file, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__is_dir") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_is_dir, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__file_stat") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_file_stat, 1, 1, 0);", result);
    // Handle directory functions (builtins)
    } else if (strcmp(expr->as.ident.name, "__make_dir") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_make_dir, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__remove_dir") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_remove_dir, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__list_dir") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_list_dir, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__cwd") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_cwd, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__chdir") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_chdir, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__absolute_path") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_absolute_path, 1, 1, 0);", result);
    // Handle system info functions (builtins)
    } else if (strcmp(expr->as.ident.name, "__platform") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_platform, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__arch") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_arch, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__hostname") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_hostname, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__username") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_username, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__homedir") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_homedir, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__cpu_count") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_cpu_count, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__total_memory") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_total_memory, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__free_memory") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_free_memory, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__os_version") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_os_version, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__os_name") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_os_name, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__tmpdir") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_tmpdir, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__uptime") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_uptime, 0, 0, 0);", result);
    // Handle compression functions (builtins)
    } else if (strcmp(expr->as.ident.name, "__zlib_compress") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_zlib_compress, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__zlib_decompress") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_zlib_decompress, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__gzip_compress") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_gzip_compress, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__gzip_decompress") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_gzip_decompress, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__zlib_compress_bound") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_zlib_compress_bound, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__crc32") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_crc32, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__adler32") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_adler32, 1, 1, 0);", result);
    // Internal helper builtins
    } else if (strcmp(expr->as.ident.name, "__read_u32") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_read_u32, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__read_u64") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_read_u64, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__read_ptr") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_read_ptr, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__strerror") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_strerror, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__dirent_name") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_dirent_name, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__string_to_cstr") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_string_to_cstr, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__cstr_to_string") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_cstr_to_string, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__string_from_bytes") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_string_from_bytes, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__to_string") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_to_string, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__string_byte_length") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_string_byte_length, 1, 1, 0);", result);
    // DNS/Networking builtins
    } else if (strcmp(expr->as.ident.name, "__dns_resolve") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_dns_resolve, 1, 1, 0);", result);
    // HTTP builtins (libwebsockets)
    } else if (strcmp(expr->as.ident.name, "__lws_http_get") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_http_get, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__lws_http_post") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_http_post, 3, 3, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__lws_http_request") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_http_request, 4, 4, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__lws_response_status") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_response_status, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__lws_response_body") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_response_body, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__lws_response_headers") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_response_headers, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__lws_response_free") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_response_free, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__lws_response_redirect") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_response_redirect, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__lws_response_body_binary") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_response_body_binary, 1, 1, 0);", result);
    // Streaming HTTP builtins
    } else if (strcmp(expr->as.ident.name, "__lws_http_stream_start") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_http_stream_start, 5, 5, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__lws_http_stream_read") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_http_stream_read, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__lws_http_stream_read_binary") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_http_stream_read_binary, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__lws_http_stream_status") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_http_stream_status, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__lws_http_stream_headers") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_http_stream_headers, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__lws_http_stream_close") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_http_stream_close, 1, 1, 0);", result);
    // Terminal control builtins
    } else if (strcmp(expr->as.ident.name, "__term_is_tty") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_term_is_tty, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__term_raw") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_term_raw, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__term_read_byte") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_term_read_byte, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__term_size") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_term_size, 0, 0, 0);", result);
    // CSPRNG builtin
    } else if (strcmp(expr->as.ident.name, "__random_bytes") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_random_bytes, 1, 1, 0);", result);
    // Cryptographic hash builtins
    } else if (strcmp(expr->as.ident.name, "__sha1") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_hash_sha1, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__sha256") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_hash_sha256, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__sha512") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_hash_sha512, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__md5") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_hash_md5, 1, 1, 0);", result);
    // ECDSA signature builtins
    } else if (strcmp(expr->as.ident.name, "__ecdsa_generate_key") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_ecdsa_generate_key, 0, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__ecdsa_free_key") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_ecdsa_free_key, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__ecdsa_sign") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_ecdsa_sign, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__ecdsa_verify") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_ecdsa_verify, 3, 3, 0);", result);
    // WebSocket builtins
    } else if (strcmp(expr->as.ident.name, "__lws_ws_connect") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_connect, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__lws_ws_send_text") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_send_text, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__lws_ws_send_binary") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_send_binary, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__lws_ws_recv") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_recv, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__lws_ws_close") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_close, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__lws_ws_is_closed") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_is_closed, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__lws_msg_type") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_msg_type, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__lws_msg_text") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_msg_text, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__lws_msg_len") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_msg_len, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__lws_msg_binary") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_msg_binary, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__lws_msg_free") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_msg_free, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__lws_ws_server_create") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_server_create, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__lws_ws_server_accept") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_server_accept, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__lws_ws_server_close") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_server_close, 1, 1, 0);", result);
    // Socket builtins
    } else if (strcmp(expr->as.ident.name, "__socket_create") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_socket_create, 3, 3, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__poll") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_poll, 2, 2, 0);", result);
    // File I/O builtins
    } else if (strcmp(expr->as.ident.name, "__open") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_open, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__open_fd") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_open_fd, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__fileno") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_fileno, 1, 1, 0);", result);
    // Debug builtins
    } else if (strcmp(expr->as.ident.name, "__task_debug_info") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_task_debug_info, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__set_stack_limit") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_set_stack_limit, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__get_stack_limit") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_get_stack_limit, 0, 0, 0);", result);
    // FFI builtins
    } else if (strcmp(expr->as.ident.name, "__callback") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_callback, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__callback_free") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_callback_free, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__ffi_sizeof") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_ffi_sizeof, 1, 1, 0);", result);
    // String builtins
    } else if (strcmp(expr->as.ident.name, "__string_concat_many") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_string_concat_many, 1, 1, 0);", result);
    // Exec builtins
    } else if (strcmp(expr->as.ident.name, "__exec") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_exec, 1, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__exec_argv") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_exec_argv, 1, 2, 0);", result);
    // Atomic operations (i32)
    } else if (strcmp(expr->as.ident.name, "atomic_load_i32") == 0 || strcmp(expr->as.ident.name, "__atomic_load_i32") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atomic_load_i32, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "atomic_store_i32") == 0 || strcmp(expr->as.ident.name, "__atomic_store_i32") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atomic_store_i32, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "atomic_add_i32") == 0 || strcmp(expr->as.ident.name, "__atomic_add_i32") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atomic_add_i32, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "atomic_sub_i32") == 0 || strcmp(expr->as.ident.name, "__atomic_sub_i32") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atomic_sub_i32, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "atomic_and_i32") == 0 || strcmp(expr->as.ident.name, "__atomic_and_i32") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atomic_and_i32, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "atomic_or_i32") == 0 || strcmp(expr->as.ident.name, "__atomic_or_i32") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atomic_or_i32, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "atomic_xor_i32") == 0 || strcmp(expr->as.ident.name, "__atomic_xor_i32") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atomic_xor_i32, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "atomic_cas_i32") == 0 || strcmp(expr->as.ident.name, "__atomic_cas_i32") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atomic_cas_i32, 3, 3, 0);", result);
    } else if (strcmp(expr->as.ident.name, "atomic_exchange_i32") == 0 || strcmp(expr->as.ident.name, "__atomic_exchange_i32") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atomic_exchange_i32, 2, 2, 0);", result);
    // Atomic operations (i64)
    } else if (strcmp(expr->as.ident.name, "atomic_load_i64") == 0 || strcmp(expr->as.ident.name, "__atomic_load_i64") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atomic_load_i64, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "atomic_store_i64") == 0 || strcmp(expr->as.ident.name, "__atomic_store_i64") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atomic_store_i64, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "atomic_add_i64") == 0 || strcmp(expr->as.ident.name, "__atomic_add_i64") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atomic_add_i64, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "atomic_sub_i64") == 0 || strcmp(expr->as.ident.name, "__atomic_sub_i64") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atomic_sub_i64, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "atomic_and_i64") == 0 || strcmp(expr->as.ident.name, "__atomic_and_i64") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atomic_and_i64, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "atomic_or_i64") == 0 || strcmp(expr->as.ident.name, "__atomic_or_i64") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atomic_or_i64, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "atomic_xor_i64") == 0 || strcmp(expr->as.ident.name, "__atomic_xor_i64") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atomic_xor_i64, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "atomic_cas_i64") == 0 || strcmp(expr->as.ident.name, "__atomic_cas_i64") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atomic_cas_i64, 3, 3, 0);", result);
    } else if (strcmp(expr->as.ident.name, "atomic_exchange_i64") == 0 || strcmp(expr->as.ident.name, "__atomic_exchange_i64") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atomic_exchange_i64, 2, 2, 0);", result);
    // Memory fence
    } else if (strcmp(expr->as.ident.name, "atomic_fence") == 0 || strcmp(expr->as.ident.name, "__atomic_fence") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atomic_fence, 0, 0, 0);", result);
    // Memory-mapped file I/O operations
    } else if (strcmp(expr->as.ident.name, "__mmap_open") == 0) {
        // (num_params=2, num_required=1): mode is optional
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_mmap_open, 2, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__mmap_open_anon") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_mmap_open_anon, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__mmap_sync") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_mmap_sync, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__mmap_close") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_mmap_close, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__mmap_size") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_mmap_size, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__mmap_advise") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_mmap_advise, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__mmap_protect") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_mmap_protect, 2, 2, 0);", result);
    // Byte order operations
    } else if (strcmp(expr->as.ident.name, "bswap16") == 0 || strcmp(expr->as.ident.name, "__bswap16") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_bswap16, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "bswap32") == 0 || strcmp(expr->as.ident.name, "__bswap32") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_bswap32, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "bswap64") == 0 || strcmp(expr->as.ident.name, "__bswap64") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_bswap64, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "htons") == 0 || strcmp(expr->as.ident.name, "__htons") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_htons, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "htonl") == 0 || strcmp(expr->as.ident.name, "__htonl") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_htonl, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "htonll") == 0 || strcmp(expr->as.ident.name, "__htonll") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_htonll, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "ntohs") == 0 || strcmp(expr->as.ident.name, "__ntohs") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_ntohs, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "ntohl") == 0 || strcmp(expr->as.ident.name, "__ntohl") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_ntohl, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "ntohll") == 0 || strcmp(expr->as.ident.name, "__ntohll") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_ntohll, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "is_little_endian") == 0 || strcmp(expr->as.ident.name, "__is_little_endian") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_is_little_endian, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "read_u16_be") == 0 || strcmp(expr->as.ident.name, "__read_u16_be") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_read_u16_be, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "read_u16_le") == 0 || strcmp(expr->as.ident.name, "__read_u16_le") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_read_u16_le, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "read_u32_be") == 0 || strcmp(expr->as.ident.name, "__read_u32_be") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_read_u32_be, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "read_u32_le") == 0 || strcmp(expr->as.ident.name, "__read_u32_le") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_read_u32_le, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "read_u64_be") == 0 || strcmp(expr->as.ident.name, "__read_u64_be") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_read_u64_be, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "read_u64_le") == 0 || strcmp(expr->as.ident.name, "__read_u64_le") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_read_u64_le, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "write_u16_be") == 0 || strcmp(expr->as.ident.name, "__write_u16_be") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_write_u16_be, 3, 3, 0);", result);
    } else if (strcmp(expr->as.ident.name, "write_u16_le") == 0 || strcmp(expr->as.ident.name, "__write_u16_le") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_write_u16_le, 3, 3, 0);", result);
    } else if (strcmp(expr->as.ident.name, "write_u32_be") == 0 || strcmp(expr->as.ident.name, "__write_u32_be") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_write_u32_be, 3, 3, 0);", result);
    } else if (strcmp(expr->as.ident.name, "write_u32_le") == 0 || strcmp(expr->as.ident.name, "__write_u32_le") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_write_u32_le, 3, 3, 0);", result);
    } else if (strcmp(expr->as.ident.name, "write_u64_be") == 0 || strcmp(expr->as.ident.name, "__write_u64_be") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_write_u64_be, 3, 3, 0);", result);
    } else if (strcmp(expr->as.ident.name, "write_u64_le") == 0 || strcmp(expr->as.ident.name, "__write_u64_le") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_write_u64_le, 3, 3, 0);", result);
    } else {
handle_variable:
        // OPTIMIZATION: Check if this is an unboxed variable (loop counter, accumulator, or typed var)
        // If so, convert the native C type back to HmlValue
        // IMPORTANT: Skip this for function parameters - they are always HmlValue
        // IMPORTANT: Skip this for main-level variables that aren't shadowed by a local
        // IMPORTANT: Skip this for shadow variables (inlined params) - they are HmlValue, not unboxed
        {
        // When inside a function and the variable is a local, it shadows any main var
        // with the same name - so the main_var check should not prevent unboxing
        // A main var is "truly main" only if it hasn't been shadowed by a local
        // (e.g., by while-loop unboxing or for-loop counter)
        int is_truly_main = codegen_is_main_var(ctx, expr->as.ident.name) &&
                            !codegen_is_local(ctx, expr->as.ident.name);
        if (ctx->optimize && ctx->type_ctx && !codegen_is_func_param(ctx, expr->as.ident.name) &&
            !is_truly_main &&
            !codegen_is_shadow(ctx, expr->as.ident.name)) {
            CheckedTypeKind native_type = type_check_get_unboxable(ctx->type_ctx, expr->as.ident.name);
            if (native_type != CHECKED_UNKNOWN) {
                // Variable is unboxed - box it for use in HmlValue context
                const char *box_func = checked_type_to_box_func(native_type);
                if (box_func) {
                    char *safe_ident = codegen_sanitize_ident(expr->as.ident.name);
                    codegen_writeln(ctx, "HmlValue %s = %s(%s);", result, box_func, safe_ident);
                    free(safe_ident);
                    // No retain needed for primitives
                    return result;
                }
            }
        }
        }

        // Captured locals: the shared environment is the source of truth
        // (closures and defers write through it), so reads must come from it
        // - the C local can be stale after a closure call.
        {
            int cap_idx = codegen_captured_var_env_index(ctx, expr->as.ident.name);
            if (cap_idx >= 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_closure_env_get(%s, %d);",
                              result, ctx->shared_env_name, cap_idx);
                // hml_closure_env_get already retains - skip the common retain below
                return result;
            }
        }

        // Check if this is an imported symbol
        ImportBinding *import_binding = NULL;
        if (ctx->current_module) {
            import_binding = module_find_import(ctx->current_module, expr->as.ident.name);
        } else {
            // In main file - check main imports
            import_binding = codegen_find_main_import(ctx, expr->as.ident.name);
        }

        if (import_binding) {
            // Use the imported module's symbol
            codegen_writeln(ctx, "HmlValue %s = %s%s;", result,
                          import_binding->module_prefix, import_binding->original_name);
        } else if (ctx->current_scope && scope_is_defined(ctx->current_scope, expr->as.ident.name)) {
            // Variable is in current lexical scope - use bare name (shadows outer/main vars)
            char *safe_ident = codegen_sanitize_ident(expr->as.ident.name);
            codegen_writeln(ctx, "HmlValue %s = %s;", result, safe_ident);
            free(safe_ident);
        } else if (codegen_is_shadow(ctx, expr->as.ident.name)) {
            // Shadow variable (like catch param) - use sanitized bare name, shadows module vars
            // Must be checked BEFORE module prefix check
            char *safe_ident = codegen_sanitize_ident(expr->as.ident.name);
            codegen_writeln(ctx, "HmlValue %s = %s;", result, safe_ident);
            free(safe_ident);
        } else if (codegen_is_leaked_main_local(ctx, expr->as.ident.name)) {
            // Top-level name that only leaked into this closure's locals from the
            // main scope - resolve to its static global, not a bare identifier.
            codegen_writeln(ctx, "HmlValue %s = _main_%s;", result, expr->as.ident.name);
        } else if (codegen_is_local(ctx, expr->as.ident.name)) {
            // Local variable - locals always shadow main vars and module exports
            if (ctx->in_function) {
                char *safe_ident = codegen_sanitize_ident(expr->as.ident.name);
                if (codegen_is_ref_param(ctx, expr->as.ident.name)) {
                    codegen_writeln(ctx, "HmlValue %s = *%s;", result, safe_ident);
                } else {
                    codegen_writeln(ctx, "HmlValue %s = %s;", result, safe_ident);
                }
                free(safe_ident);
            } else if (ctx->current_module) {
                ExportedSymbol *exp = module_find_export(ctx->current_module, expr->as.ident.name);
                if (exp) {
                    codegen_writeln(ctx, "HmlValue %s = %s;", result, exp->mangled_name);
                } else {
                    char *safe_ident = codegen_sanitize_ident(expr->as.ident.name);
                    codegen_writeln(ctx, "HmlValue %s = %s;", result, safe_ident);
                    free(safe_ident);
                }
            } else if (codegen_is_main_var(ctx, expr->as.ident.name)) {
                // Top-level local that's also a main var - use _main_ prefix
                // unless it's been unboxed (while-loop optimization)
                codegen_writeln(ctx, "HmlValue %s = _main_%s;", result, expr->as.ident.name);
            } else {
                // True local variable (not a main var) - use sanitized bare name
                char *safe_ident = codegen_sanitize_ident(expr->as.ident.name);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, safe_ident);
                free(safe_ident);
            }
        } else if (ctx->current_module) {
            // Not local, not shadow, have module - check if it's an export first
            ExportedSymbol *exp = module_find_export(ctx->current_module, expr->as.ident.name);
            if (exp) {
                // It's a module export - use the mangled name
                codegen_writeln(ctx, "HmlValue %s = %s;", result, exp->mangled_name);
            } else {
                // Not an export - use module prefix for module-level variable
                codegen_writeln(ctx, "HmlValue %s = %s%s;", result,
                              ctx->current_module->module_prefix, expr->as.ident.name);
            }
        } else if (ctx->current_closure && ctx->current_closure->source_module) {
            // Inside a closure - check if identifier is a module export from the closure's source module
            ExportedSymbol *exp = module_find_export(ctx->current_closure->source_module, expr->as.ident.name);
            if (exp) {
                // It's a module export - use the mangled name
                codegen_writeln(ctx, "HmlValue %s = %s;", result, exp->mangled_name);
            } else {
                // Not an export - fallback to bare identifier (may cause C error)
                char *safe_ident = codegen_sanitize_ident(expr->as.ident.name);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, safe_ident);
                free(safe_ident);
            }
        } else if (codegen_is_main_var(ctx, expr->as.ident.name)) {
            // Main file top-level variable - use _main_ prefix
            codegen_writeln(ctx, "HmlValue %s = _main_%s;", result, expr->as.ident.name);
        } else {
            // Undefined variable - report compile-time error
            codegen_error(ctx, expr->line, "undefined variable '%s'", expr->as.ident.name);
            codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
        }
    }
    // OPTIMIZATION: Use conditional retain to skip for primitives (i32, i64, f64, bool)
    codegen_writeln(ctx, "hml_retain_if_needed(&%s);", result);

    return result;
}
