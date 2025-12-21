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
    if (strcmp(expr->as.ident, "self") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_self;", result);
    // Handle signal constants
    } else if (strcmp(expr->as.ident, "SIGINT") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGINT);", result);
    } else if (strcmp(expr->as.ident, "SIGTERM") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGTERM);", result);
    } else if (strcmp(expr->as.ident, "SIGHUP") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGHUP);", result);
    } else if (strcmp(expr->as.ident, "SIGQUIT") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGQUIT);", result);
    } else if (strcmp(expr->as.ident, "SIGABRT") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGABRT);", result);
    } else if (strcmp(expr->as.ident, "SIGUSR1") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGUSR1);", result);
    } else if (strcmp(expr->as.ident, "SIGUSR2") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGUSR2);", result);
    } else if (strcmp(expr->as.ident, "SIGALRM") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGALRM);", result);
    } else if (strcmp(expr->as.ident, "SIGCHLD") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGCHLD);", result);
    } else if (strcmp(expr->as.ident, "SIGPIPE") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGPIPE);", result);
    } else if (strcmp(expr->as.ident, "SIGCONT") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGCONT);", result);
    } else if (strcmp(expr->as.ident, "SIGSTOP") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGSTOP);", result);
    } else if (strcmp(expr->as.ident, "SIGTSTP") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGTSTP);", result);
    } else if (strcmp(expr->as.ident, "SIGTTIN") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGTTIN);", result);
    } else if (strcmp(expr->as.ident, "SIGTTOU") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SIGTTOU);", result);
    // Handle socket constants
    } else if (strcmp(expr->as.ident, "AF_INET") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(AF_INET);", result);
    } else if (strcmp(expr->as.ident, "AF_INET6") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(AF_INET6);", result);
    } else if (strcmp(expr->as.ident, "SOCK_STREAM") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SOCK_STREAM);", result);
    } else if (strcmp(expr->as.ident, "SOCK_DGRAM") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SOCK_DGRAM);", result);
    } else if (strcmp(expr->as.ident, "SOL_SOCKET") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SOL_SOCKET);", result);
    } else if (strcmp(expr->as.ident, "SO_REUSEADDR") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SO_REUSEADDR);", result);
    } else if (strcmp(expr->as.ident, "SO_KEEPALIVE") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SO_KEEPALIVE);", result);
    } else if (strcmp(expr->as.ident, "SO_RCVTIMEO") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SO_RCVTIMEO);", result);
    } else if (strcmp(expr->as.ident, "SO_SNDTIMEO") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(SO_SNDTIMEO);", result);
    } else if (strcmp(expr->as.ident, "IPPROTO_TCP") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(IPPROTO_TCP);", result);
    } else if (strcmp(expr->as.ident, "IPPROTO_UDP") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(IPPROTO_UDP);", result);
    // Poll constants
    } else if (strcmp(expr->as.ident, "POLLIN") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(POLLIN);", result);
    } else if (strcmp(expr->as.ident, "POLLOUT") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(POLLOUT);", result);
    } else if (strcmp(expr->as.ident, "POLLERR") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(POLLERR);", result);
    } else if (strcmp(expr->as.ident, "POLLHUP") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(POLLHUP);", result);
    } else if (strcmp(expr->as.ident, "POLLNVAL") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(POLLNVAL);", result);
    } else if (strcmp(expr->as.ident, "POLLPRI") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(POLLPRI);", result);
    // Handle math constants (builtins)
    } else if (strcmp(expr->as.ident, "__PI") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_f64(3.14159265358979323846);", result);
    } else if (strcmp(expr->as.ident, "__E") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_f64(2.71828182845904523536);", result);
    } else if (strcmp(expr->as.ident, "__TAU") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_f64(6.28318530717958647692);", result);
    } else if (strcmp(expr->as.ident, "__INF") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_f64(1.0/0.0);", result);
    } else if (strcmp(expr->as.ident, "__NAN") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_f64(0.0/0.0);", result);
    // Handle math functions (builtins)
    } else if (strcmp(expr->as.ident, "__sin") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_sin, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__cos") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_cos, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__tan") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_tan, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__asin") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_asin, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__acos") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_acos, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__atan") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atan, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__atan2") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atan2, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "__sqrt") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_sqrt, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__pow") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_pow, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "__exp") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_exp, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__log") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_log, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__log10") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_log10, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__log2") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_log2, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__floor") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_floor, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__ceil") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_ceil, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__round") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_round, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__trunc") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_trunc, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__floori") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_floori, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__ceili") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_ceili, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__roundi") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_roundi, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__trunci") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_trunci, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__div") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_div, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "__divi") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_divi, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "__abs") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_abs, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__min") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_min, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "__max") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_max, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "__clamp") == 0 || (!codegen_is_local(ctx, expr->as.ident) && !codegen_is_main_var(ctx, expr->as.ident) && strcmp(expr->as.ident, "clamp") == 0)) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_clamp, 3, 3, 0);", result);
    } else if (strcmp(expr->as.ident, "__rand") == 0 || (!codegen_is_local(ctx, expr->as.ident) && !codegen_is_main_var(ctx, expr->as.ident) && strcmp(expr->as.ident, "rand") == 0)) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_rand, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "__rand_range") == 0 || (!codegen_is_local(ctx, expr->as.ident) && !codegen_is_main_var(ctx, expr->as.ident) && strcmp(expr->as.ident, "rand_range") == 0)) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_rand_range, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "__seed") == 0 || (!codegen_is_local(ctx, expr->as.ident) && !codegen_is_main_var(ctx, expr->as.ident) && strcmp(expr->as.ident, "seed") == 0)) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_seed, 1, 1, 0);", result);
    // Handle time functions (builtins)
    } else if (strcmp(expr->as.ident, "__now") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_now, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "__time_ms") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_time_ms, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "__clock") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_clock, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "__sleep") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_sleep, 1, 1, 0);", result);
    // Handle datetime functions (builtins)
    } else if (strcmp(expr->as.ident, "__localtime") == 0 || strcmp(expr->as.ident, "localtime") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_localtime, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__gmtime") == 0 || strcmp(expr->as.ident, "gmtime") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_gmtime, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__mktime") == 0 || strcmp(expr->as.ident, "mktime") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_mktime, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__strftime") == 0 || strcmp(expr->as.ident, "strftime") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_strftime, 2, 2, 0);", result);
    // Handle environment functions (builtins)
    } else if (strcmp(expr->as.ident, "__getenv") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_getenv, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__setenv") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_setenv, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "__unsetenv") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_unsetenv, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__exit") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_exit, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__get_pid") == 0 || strcmp(expr->as.ident, "get_pid") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_get_pid, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "__getppid") == 0 || strcmp(expr->as.ident, "getppid") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_getppid, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "__getuid") == 0 || strcmp(expr->as.ident, "getuid") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_getuid, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "__geteuid") == 0 || strcmp(expr->as.ident, "geteuid") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_geteuid, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "__getgid") == 0 || strcmp(expr->as.ident, "getgid") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_getgid, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "__getegid") == 0 || strcmp(expr->as.ident, "getegid") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_getegid, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "__exec") == 0 || strcmp(expr->as.ident, "exec") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_exec, 1, 1, 0);", result);
    // Handle process functions (builtins)
    } else if (strcmp(expr->as.ident, "__kill") == 0 || strcmp(expr->as.ident, "kill") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_kill, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "__fork") == 0 || strcmp(expr->as.ident, "fork") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_fork, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "__wait") == 0 || strcmp(expr->as.ident, "wait") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_wait, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "__waitpid") == 0 || strcmp(expr->as.ident, "waitpid") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_waitpid, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "__abort") == 0 || strcmp(expr->as.ident, "abort") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_abort, 0, 0, 0);", result);
    // Handle filesystem functions (builtins)
    } else if (strcmp(expr->as.ident, "__exists") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_exists, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__read_file") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_read_file, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__write_file") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_write_file, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "__append_file") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_append_file, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "__remove_file") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_remove_file, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__rename") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_rename, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "__copy_file") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_copy_file, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "__is_file") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_is_file, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__is_dir") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_is_dir, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__file_stat") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_file_stat, 1, 1, 0);", result);
    // Handle directory functions (builtins)
    } else if (strcmp(expr->as.ident, "__make_dir") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_make_dir, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "__remove_dir") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_remove_dir, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__list_dir") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_list_dir, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__cwd") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_cwd, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "__chdir") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_chdir, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__absolute_path") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_absolute_path, 1, 1, 0);", result);
    // Handle system info functions (builtins)
    } else if (strcmp(expr->as.ident, "__platform") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_platform, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "__arch") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_arch, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "__hostname") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_hostname, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "__username") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_username, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "__homedir") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_homedir, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "__cpu_count") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_cpu_count, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "__total_memory") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_total_memory, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "__free_memory") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_free_memory, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "__os_version") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_os_version, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "__os_name") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_os_name, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "__tmpdir") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_tmpdir, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "__uptime") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_uptime, 0, 0, 0);", result);
    // Handle compression functions (builtins)
    } else if (strcmp(expr->as.ident, "__zlib_compress") == 0 || strcmp(expr->as.ident, "zlib_compress") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_zlib_compress, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "__zlib_decompress") == 0 || strcmp(expr->as.ident, "zlib_decompress") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_zlib_decompress, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "__gzip_compress") == 0 || strcmp(expr->as.ident, "gzip_compress") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_gzip_compress, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "__gzip_decompress") == 0 || strcmp(expr->as.ident, "gzip_decompress") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_gzip_decompress, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "__zlib_compress_bound") == 0 || strcmp(expr->as.ident, "zlib_compress_bound") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_zlib_compress_bound, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__crc32") == 0 || strcmp(expr->as.ident, "crc32") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_crc32, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__adler32") == 0 || strcmp(expr->as.ident, "adler32") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_adler32, 1, 1, 0);", result);
    // Internal helper builtins
    } else if (strcmp(expr->as.ident, "__read_u32") == 0 || strcmp(expr->as.ident, "read_u32") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_read_u32, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__read_u64") == 0 || strcmp(expr->as.ident, "read_u64") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_read_u64, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__read_ptr") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_read_ptr, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__strerror") == 0 || strcmp(expr->as.ident, "strerror") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_strerror, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "__dirent_name") == 0 || strcmp(expr->as.ident, "dirent_name") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_dirent_name, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__string_to_cstr") == 0 || strcmp(expr->as.ident, "string_to_cstr") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_string_to_cstr, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__cstr_to_string") == 0 || strcmp(expr->as.ident, "cstr_to_string") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_cstr_to_string, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__string_from_bytes") == 0 || strcmp(expr->as.ident, "string_from_bytes") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_string_from_bytes, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__to_string") == 0 || strcmp(expr->as.ident, "to_string") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_to_string, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__string_byte_length") == 0 || strcmp(expr->as.ident, "string_byte_length") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_string_byte_length, 1, 1, 0);", result);
    // DNS/Networking builtins
    } else if (strcmp(expr->as.ident, "dns_resolve") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_dns_resolve, 1, 1, 0);", result);
    // HTTP builtins (libwebsockets)
    } else if (strcmp(expr->as.ident, "__lws_http_get") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_http_get, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__lws_http_post") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_http_post, 3, 3, 0);", result);
    } else if (strcmp(expr->as.ident, "__lws_http_request") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_http_request, 4, 4, 0);", result);
    } else if (strcmp(expr->as.ident, "__lws_response_status") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_response_status, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__lws_response_body") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_response_body, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__lws_response_headers") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_response_headers, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__lws_response_free") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_response_free, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__lws_response_redirect") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_response_redirect, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__lws_response_body_binary") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_response_body_binary, 1, 1, 0);", result);
    // Cryptographic hash builtins
    } else if (strcmp(expr->as.ident, "__sha256") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_hash_sha256, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__sha512") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_hash_sha512, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__md5") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_hash_md5, 1, 1, 0);", result);
    // ECDSA signature builtins
    } else if (strcmp(expr->as.ident, "__ecdsa_generate_key") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_ecdsa_generate_key, 0, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__ecdsa_free_key") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_ecdsa_free_key, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__ecdsa_sign") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_ecdsa_sign, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "__ecdsa_verify") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_ecdsa_verify, 3, 3, 0);", result);
    // WebSocket builtins
    } else if (strcmp(expr->as.ident, "__lws_ws_connect") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_connect, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__lws_ws_send_text") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_send_text, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "__lws_ws_send_binary") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_send_binary, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "__lws_ws_recv") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_recv, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "__lws_ws_close") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_close, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__lws_ws_is_closed") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_is_closed, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__lws_msg_type") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_msg_type, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__lws_msg_text") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_msg_text, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__lws_msg_len") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_msg_len, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__lws_msg_free") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_msg_free, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "__lws_ws_server_create") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_server_create, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "__lws_ws_server_accept") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_server_accept, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "__lws_ws_server_close") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_server_close, 1, 1, 0);", result);
    // Socket builtins
    } else if (strcmp(expr->as.ident, "socket_create") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_socket_create, 3, 3, 0);", result);
    // OS info builtins (unprefixed)
    } else if (strcmp(expr->as.ident, "platform") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_platform, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "arch") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_arch, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "hostname") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_hostname, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "username") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_username, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "homedir") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_homedir, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "cpu_count") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_cpu_count, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "total_memory") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_total_memory, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "free_memory") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_free_memory, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "os_version") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_os_version, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "os_name") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_os_name, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "tmpdir") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_tmpdir, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "uptime") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_uptime, 0, 0, 0);", result);
    // Filesystem builtins (unprefixed)
    } else if (strcmp(expr->as.ident, "exists") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_exists, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "read_file") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_read_file, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "write_file") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_write_file, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "append_file") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_append_file, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "remove_file") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_remove_file, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "rename") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_rename, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "copy_file") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_copy_file, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "is_file") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_is_file, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "is_dir") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_is_dir, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "file_stat") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_file_stat, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "make_dir") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_make_dir, 2, 2, 0);", result);
    } else if (strcmp(expr->as.ident, "remove_dir") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_remove_dir, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "list_dir") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_list_dir, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "cwd") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_cwd, 0, 0, 0);", result);
    } else if (strcmp(expr->as.ident, "chdir") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_chdir, 1, 1, 0);", result);
    } else if (strcmp(expr->as.ident, "absolute_path") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_absolute_path, 1, 1, 0);", result);
    // Unprefixed aliases for math functions (for parity with interpreter)
    // NOTE: Only use builtin if not shadowed by a local variable
    } else if (!codegen_is_local(ctx, expr->as.ident) && strcmp(expr->as.ident, "sin") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_sin, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident) && strcmp(expr->as.ident, "cos") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_cos, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident) && strcmp(expr->as.ident, "tan") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_tan, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident) && strcmp(expr->as.ident, "asin") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_asin, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident) && strcmp(expr->as.ident, "acos") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_acos, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident) && strcmp(expr->as.ident, "atan") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atan, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident) && strcmp(expr->as.ident, "atan2") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_atan2, 2, 2, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident) && strcmp(expr->as.ident, "sqrt") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_sqrt, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident) && strcmp(expr->as.ident, "pow") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_pow, 2, 2, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident) && strcmp(expr->as.ident, "exp") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_exp, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident) && strcmp(expr->as.ident, "log") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_log, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident) && strcmp(expr->as.ident, "log10") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_log10, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident) && strcmp(expr->as.ident, "log2") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_log2, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident) && strcmp(expr->as.ident, "floor") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_floor, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident) && strcmp(expr->as.ident, "ceil") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_ceil, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident) && strcmp(expr->as.ident, "round") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_round, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident) && strcmp(expr->as.ident, "trunc") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_trunc, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident) && strcmp(expr->as.ident, "floori") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_floori, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident) && strcmp(expr->as.ident, "ceili") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_ceili, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident) && strcmp(expr->as.ident, "roundi") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_roundi, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident) && strcmp(expr->as.ident, "trunci") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_trunci, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident) && strcmp(expr->as.ident, "div") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_div, 2, 2, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident) && strcmp(expr->as.ident, "divi") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_divi, 2, 2, 0);", result);
    // Unprefixed aliases for environment functions (for parity with interpreter)
    } else if (!codegen_is_local(ctx, expr->as.ident) && strcmp(expr->as.ident, "getenv") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_getenv, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident) && strcmp(expr->as.ident, "setenv") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_setenv, 2, 2, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident) && strcmp(expr->as.ident, "unsetenv") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_unsetenv, 1, 1, 0);", result);
    } else if (!codegen_is_local(ctx, expr->as.ident) && strcmp(expr->as.ident, "get_pid") == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_get_pid, 0, 0, 0);", result);
    } else {
        // Check if this is an imported symbol
        ImportBinding *import_binding = NULL;
        if (ctx->current_module) {
            import_binding = module_find_import(ctx->current_module, expr->as.ident);
        }

        if (import_binding) {
            // Use the imported module's symbol
            codegen_writeln(ctx, "HmlValue %s = %s%s;", result,
                          import_binding->module_prefix, import_binding->original_name);
        } else if (codegen_is_shadow(ctx, expr->as.ident)) {
            // Shadow variable (like catch param) - use sanitized bare name, shadows module vars
            // Must be checked BEFORE module prefix check
            char *safe_ident = codegen_sanitize_ident(expr->as.ident);
            codegen_writeln(ctx, "HmlValue %s = %s;", result, safe_ident);
            free(safe_ident);
        } else if (codegen_is_local(ctx, expr->as.ident)) {
            // Local variable - check context to determine how to access
            if (ctx->current_module) {
                // In a module - check if it's a module export (self-reference in closure)
                ExportedSymbol *exp = module_find_export(ctx->current_module, expr->as.ident);
                if (exp) {
                    // Use the mangled export name to access module-level function
                    codegen_writeln(ctx, "HmlValue %s = %s;", result, exp->mangled_name);
                } else {
                    char *safe_ident = codegen_sanitize_ident(expr->as.ident);
                    codegen_writeln(ctx, "HmlValue %s = %s;", result, safe_ident);
                    free(safe_ident);
                }
            } else if (ctx->in_function) {
                // Inside a function - locals (params, loop vars) shadow main vars
                char *safe_ident = codegen_sanitize_ident(expr->as.ident);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, safe_ident);
                free(safe_ident);
            } else if (codegen_is_main_var(ctx, expr->as.ident)) {
                // In main scope, and this is a main var - use _main_ prefix
                codegen_writeln(ctx, "HmlValue %s = _main_%s;", result, expr->as.ident);
            } else {
                // True local variable (not a main var) - use sanitized bare name
                char *safe_ident = codegen_sanitize_ident(expr->as.ident);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, safe_ident);
                free(safe_ident);
            }
        } else if (ctx->current_module) {
            // Not local, not shadow, have module - use module prefix
            codegen_writeln(ctx, "HmlValue %s = %s%s;", result,
                          ctx->current_module->module_prefix, expr->as.ident);
        } else if (codegen_is_main_var(ctx, expr->as.ident)) {
            // Main file top-level variable - use _main_ prefix
            codegen_writeln(ctx, "HmlValue %s = _main_%s;", result, expr->as.ident);
        } else {
            // Undefined variable - will cause C compilation error
            char *safe_ident = codegen_sanitize_ident(expr->as.ident);
            codegen_writeln(ctx, "HmlValue %s = %s;", result, safe_ident);
            free(safe_ident);
        }
    }
    // OPTIMIZATION: Use conditional retain to skip for primitives (i32, i64, f64, bool)
    codegen_writeln(ctx, "hml_retain_if_needed(&%s);", result);

    return result;
}
