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
    // Handle 'self' specially - maps to hml_self global
    if (strcmp(expr->as.ident.name, "self") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_self;", result);
    // Handle I/O builtins as first-class functions
    } else if (strcmp(expr->as.ident.name, "print") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_print, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "println") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_println, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "eprint") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_eprint, 1, 1, 0);", result);
    // Handle signal constants
    } else if (strcmp(expr->as.ident.name, "SIGINT") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGINT);", result);
    } else if (strcmp(expr->as.ident.name, "SIGTERM") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGTERM);", result);
    } else if (strcmp(expr->as.ident.name, "SIGHUP") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGHUP);", result);
    } else if (strcmp(expr->as.ident.name, "SIGQUIT") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGQUIT);", result);
    } else if (strcmp(expr->as.ident.name, "SIGABRT") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGABRT);", result);
    } else if (strcmp(expr->as.ident.name, "SIGUSR1") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGUSR1);", result);
    } else if (strcmp(expr->as.ident.name, "SIGUSR2") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGUSR2);", result);
    } else if (strcmp(expr->as.ident.name, "SIGALRM") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGALRM);", result);
    } else if (strcmp(expr->as.ident.name, "SIGCHLD") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGCHLD);", result);
    } else if (strcmp(expr->as.ident.name, "SIGPIPE") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGPIPE);", result);
    } else if (strcmp(expr->as.ident.name, "SIGCONT") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGCONT);", result);
    } else if (strcmp(expr->as.ident.name, "SIGSTOP") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGSTOP);", result);
    } else if (strcmp(expr->as.ident.name, "SIGTSTP") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGTSTP);", result);
    } else if (strcmp(expr->as.ident.name, "SIGTTIN") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGTTIN);", result);
    } else if (strcmp(expr->as.ident.name, "SIGTTOU") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGTTOU);", result);
    // Handle socket constants
    } else if (strcmp(expr->as.ident.name, "AF_INET") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(AF_INET);", result);
    } else if (strcmp(expr->as.ident.name, "AF_INET6") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(AF_INET6);", result);
    } else if (strcmp(expr->as.ident.name, "SOCK_STREAM") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SOCK_STREAM);", result);
    } else if (strcmp(expr->as.ident.name, "SOCK_DGRAM") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SOCK_DGRAM);", result);
    } else if (strcmp(expr->as.ident.name, "SOL_SOCKET") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SOL_SOCKET);", result);
    } else if (strcmp(expr->as.ident.name, "SO_REUSEADDR") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SO_REUSEADDR);", result);
    } else if (strcmp(expr->as.ident.name, "SO_KEEPALIVE") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SO_KEEPALIVE);", result);
    } else if (strcmp(expr->as.ident.name, "SO_RCVTIMEO") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SO_RCVTIMEO);", result);
    } else if (strcmp(expr->as.ident.name, "SO_SNDTIMEO") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SO_SNDTIMEO);", result);
    } else if (strcmp(expr->as.ident.name, "IPPROTO_TCP") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(IPPROTO_TCP);", result);
    } else if (strcmp(expr->as.ident.name, "IPPROTO_UDP") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(IPPROTO_UDP);", result);
    // Poll constants
    } else if (strcmp(expr->as.ident.name, "POLLIN") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(POLLIN);", result);
    } else if (strcmp(expr->as.ident.name, "POLLOUT") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(POLLOUT);", result);
    } else if (strcmp(expr->as.ident.name, "POLLERR") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(POLLERR);", result);
    } else if (strcmp(expr->as.ident.name, "POLLHUP") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(POLLHUP);", result);
    } else if (strcmp(expr->as.ident.name, "POLLNVAL") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(POLLNVAL);", result);
    } else if (strcmp(expr->as.ident.name, "POLLPRI") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(POLLPRI);", result);
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
    } else if (strcmp(expr->as.ident.name, "__clamp") == 0 || (!codegen_is_local(ctx, expr->as.ident.name) && !codegen_is_main_var(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "clamp") == 0)) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_clamp, 3, 3, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__rand") == 0 || (!codegen_is_local(ctx, expr->as.ident.name) && !codegen_is_main_var(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "rand") == 0)) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_rand, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__rand_range") == 0 || (!codegen_is_local(ctx, expr->as.ident.name) && !codegen_is_main_var(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "rand_range") == 0)) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_rand_range, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__seed") == 0 || (!codegen_is_local(ctx, expr->as.ident.name) && !codegen_is_main_var(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "seed") == 0)) {
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
    } else if (strcmp(expr->as.ident.name, "__localtime") == 0 || strcmp(expr->as.ident.name, "localtime") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_localtime, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__gmtime") == 0 || strcmp(expr->as.ident.name, "gmtime") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_gmtime, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__mktime") == 0 || strcmp(expr->as.ident.name, "mktime") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_mktime, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__strftime") == 0 || strcmp(expr->as.ident.name, "strftime") == 0) {
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
    } else if (strcmp(expr->as.ident.name, "__get_pid") == 0 || strcmp(expr->as.ident.name, "get_pid") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_get_pid, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__getppid") == 0 || strcmp(expr->as.ident.name, "getppid") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_getppid, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__getuid") == 0 || strcmp(expr->as.ident.name, "getuid") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_getuid, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__geteuid") == 0 || strcmp(expr->as.ident.name, "geteuid") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_geteuid, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__getgid") == 0 || strcmp(expr->as.ident.name, "getgid") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_getgid, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__getegid") == 0 || strcmp(expr->as.ident.name, "getegid") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_getegid, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__exec") == 0 || strcmp(expr->as.ident.name, "exec") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_exec, 1, 1, 0);", result);
    // Handle process functions (builtins)
    } else if (strcmp(expr->as.ident.name, "__kill") == 0 || strcmp(expr->as.ident.name, "kill") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_kill, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__fork") == 0 || strcmp(expr->as.ident.name, "fork") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_fork, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__wait") == 0 || strcmp(expr->as.ident.name, "wait") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_wait, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__waitpid") == 0 || strcmp(expr->as.ident.name, "waitpid") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_waitpid, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__abort") == 0 || strcmp(expr->as.ident.name, "abort") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_abort, 0, 0, 0);", result);
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
    } else if (strcmp(expr->as.ident.name, "__zlib_compress") == 0 || strcmp(expr->as.ident.name, "zlib_compress") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_zlib_compress, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__zlib_decompress") == 0 || strcmp(expr->as.ident.name, "zlib_decompress") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_zlib_decompress, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__gzip_compress") == 0 || strcmp(expr->as.ident.name, "gzip_compress") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_gzip_compress, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__gzip_decompress") == 0 || strcmp(expr->as.ident.name, "gzip_decompress") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_gzip_decompress, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__zlib_compress_bound") == 0 || strcmp(expr->as.ident.name, "zlib_compress_bound") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_zlib_compress_bound, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__crc32") == 0 || strcmp(expr->as.ident.name, "crc32") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_crc32, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__adler32") == 0 || strcmp(expr->as.ident.name, "adler32") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_adler32, 1, 1, 0);", result);
    // Internal helper builtins
    } else if (strcmp(expr->as.ident.name, "__read_u32") == 0 || strcmp(expr->as.ident.name, "read_u32") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_read_u32, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__read_u64") == 0 || strcmp(expr->as.ident.name, "read_u64") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_read_u64, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__read_ptr") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_read_ptr, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__strerror") == 0 || strcmp(expr->as.ident.name, "strerror") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_strerror, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__dirent_name") == 0 || strcmp(expr->as.ident.name, "dirent_name") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_dirent_name, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__string_to_cstr") == 0 || strcmp(expr->as.ident.name, "string_to_cstr") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_string_to_cstr, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__cstr_to_string") == 0 || strcmp(expr->as.ident.name, "cstr_to_string") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_cstr_to_string, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__string_from_bytes") == 0 || strcmp(expr->as.ident.name, "string_from_bytes") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_string_from_bytes, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__to_string") == 0 || strcmp(expr->as.ident.name, "to_string") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_to_string, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__string_byte_length") == 0 || strcmp(expr->as.ident.name, "string_byte_length") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_string_byte_length, 1, 1, 0);", result);
    // DNS/Networking builtins
    } else if (strcmp(expr->as.ident.name, "dns_resolve") == 0) {
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
    // Cryptographic hash builtins
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
    } else if (strcmp(expr->as.ident.name, "__lws_msg_free") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_msg_free, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__lws_ws_server_create") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_server_create, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__lws_ws_server_accept") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_server_accept, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident.name, "__lws_ws_server_close") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_server_close, 1, 1, 0);", result);
    // Socket builtins
    } else if (strcmp(expr->as.ident.name, "socket_create") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_socket_create, 3, 3, 0);", result);
    // OS info builtins (unprefixed) - must check for local shadowing
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "platform") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_platform, 0, 0, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "arch") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_arch, 0, 0, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "hostname") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_hostname, 0, 0, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "username") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_username, 0, 0, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "homedir") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_homedir, 0, 0, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "cpu_count") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_cpu_count, 0, 0, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "total_memory") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_total_memory, 0, 0, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "free_memory") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_free_memory, 0, 0, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "os_version") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_os_version, 0, 0, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "os_name") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_os_name, 0, 0, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "tmpdir") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_tmpdir, 0, 0, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "uptime") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_uptime, 0, 0, 0);", result);
    // Filesystem builtins (unprefixed) - must check for local shadowing
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "exists") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_exists, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "read_file") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_read_file, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "write_file") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_write_file, 2, 2, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "append_file") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_append_file, 2, 2, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "remove_file") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_remove_file, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "rename") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_rename, 2, 2, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "copy_file") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_copy_file, 2, 2, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "is_file") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_is_file, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "is_dir") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_is_dir, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "file_stat") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_file_stat, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "make_dir") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_make_dir, 2, 2, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "remove_dir") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_remove_dir, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "list_dir") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_list_dir, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "cwd") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_cwd, 0, 0, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "chdir") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_chdir, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "absolute_path") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_absolute_path, 1, 1, 0);", result);
    // Unprefixed aliases for math functions (for parity with interpreter)
    // NOTE: Only use builtin if not shadowed by a local variable
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "sin") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_sin, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "cos") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_cos, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "tan") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_tan, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "asin") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_asin, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "acos") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_acos, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "atan") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atan, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "atan2") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atan2, 2, 2, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "sqrt") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_sqrt, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "pow") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_pow, 2, 2, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "exp") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_exp, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "log") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_log, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "log10") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_log10, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "log2") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_log2, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "floor") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_floor, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "ceil") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_ceil, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "round") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_round, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "trunc") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_trunc, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "floori") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_floori, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "ceili") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_ceili, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "roundi") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_roundi, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "trunci") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_trunci, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "div") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_div, 2, 2, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "divi") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_divi, 2, 2, 0);", result);
    // Unprefixed aliases for environment functions (for parity with interpreter)
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "getenv") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_getenv, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "setenv") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_setenv, 2, 2, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "unsetenv") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_unsetenv, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident.name) && strcmp(expr->as.ident.name, "get_pid") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_get_pid, 0, 0, 0);", result);
    } else {
        // Check if this is an imported symbol
        ImportBinding *import_binding = NULL;
        if (ctx->current_module) {
            import_binding = module_find_import(ctx->current_module, expr->as.ident.name);
        }

        if (import_binding) {
            // Use the imported module's symbol
            codegen_writeln(ctx, "HmlValue %s = %s%s;", result,
                          import_binding->module_prefix, import_binding->original_name);
        } else if (codegen_is_shadow(ctx, expr->as.ident.name)) {
            // Shadow variable (like catch param) - use sanitized bare name, shadows module vars
            // Must be checked BEFORE module prefix check
            char *safe_ident = codegen_sanitize_ident(expr->as.ident.name);
            codegen_writeln(ctx, "HmlValue %s = %s;", result, safe_ident);
            free(safe_ident);
        } else if (codegen_is_local(ctx, expr->as.ident.name)) {
            // Local variable - check context to determine how to access
            // Local variables in functions shadow module exports
            if (ctx->in_function) {
                // Inside a function - locals (params, loop vars) ALWAYS shadow module exports
                // This must be checked BEFORE module export lookup
                char *safe_ident = codegen_sanitize_ident(expr->as.ident.name);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, safe_ident);
                free(safe_ident);
            } else if (ctx->current_module) {
                // At module level (not in function) - check if it's a module export (self-reference)
                ExportedSymbol *exp = module_find_export(ctx->current_module, expr->as.ident.name);
                if (exp) {
                    // Use the mangled export name to access module-level function
                    codegen_writeln(ctx, "HmlValue %s = %s;", result, exp->mangled_name);
                } else {
                    char *safe_ident = codegen_sanitize_ident(expr->as.ident.name);
                    codegen_writeln(ctx, "HmlValue %s = %s;", result, safe_ident);
                    free(safe_ident);
                }
            } else if (codegen_is_main_var(ctx, expr->as.ident.name)) {
                // In main scope, and this is a main var - use _main_ prefix
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
            // Undefined variable - will cause C compilation error
            char *safe_ident = codegen_sanitize_ident(expr->as.ident.name);
            codegen_writeln(ctx, "HmlValue %s = %s;", result, safe_ident);
            free(safe_ident);
        }
    }
    // OPTIMIZATION: Use conditional retain to skip for primitives (i32, i64, f64, bool)
    codegen_writeln(ctx, "hml_retain_if_needed(&%s);", result);

    return result;
}
