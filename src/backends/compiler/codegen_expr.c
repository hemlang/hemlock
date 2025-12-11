/*
 * Hemlock Code Generator - Expression Code Generation
 *
 * Handles code generation for all expression types.
 */

#include "codegen_internal.h"

// OPTIMIZATION: Helper to check if an expression is likely a string
// (string literal or identifier - we can't know types at compile time for all cases)
static int is_likely_string_expr(Expr *expr) {
    return expr->type == EXPR_STRING;
}

// OPTIMIZATION: Count chained ADD operations that look like string concatenation
// Returns the count of concatenated elements (2 = simple a+b, 3 = a+b+c, etc.)
static int count_string_concat_chain(Expr *expr, Expr **elements, int max_elements) {
    if (expr->type != EXPR_BINARY || expr->as.binary.op != OP_ADD) {
        // Not an ADD - this is a leaf
        if (max_elements > 0) {
            elements[0] = expr;
        }
        return 1;
    }

    // Check if the left side is also a string concat chain
    int left_count = count_string_concat_chain(expr->as.binary.left, elements, max_elements);
    if (left_count >= max_elements) {
        return left_count; // Already at max
    }

    // Add the right side
    elements[left_count] = expr->as.binary.right;
    return left_count + 1;
}

// OPTIMIZATION: Check if this is a chain of string concatenations
// Detects patterns like: a + b + c + d (left-associative ADD chains)
// where at least one operand is a string literal
static int is_string_concat_chain(Expr *expr, int *count) {
    if (expr->type != EXPR_BINARY || expr->as.binary.op != OP_ADD) {
        return 0;
    }

    // Collect all elements in the chain
    Expr *elements[6];
    int n = count_string_concat_chain(expr, elements, 6);

    // For it to be a string concat chain, at least one element should be a string literal
    int has_string = 0;
    for (int i = 0; i < n; i++) {
        if (is_likely_string_expr(elements[i])) {
            has_string = 1;
            break;
        }
    }

    if (has_string && n >= 3 && n <= 5) {
        *count = n;
        return 1;
    }
    return 0;
}

char* codegen_expr(CodegenContext *ctx, Expr *expr) {
    char *result = codegen_temp(ctx);

    switch (expr->type) {
        case EXPR_NUMBER:
            if (expr->as.number.is_float) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_f64(%g);", result, expr->as.number.float_value);
            } else {
                // Check if it fits in i32
                if (expr->as.number.int_value >= INT32_MIN && expr->as.number.int_value <= INT32_MAX) {
                    codegen_writeln(ctx, "HmlValue %s = hml_val_i32(%d);", result, (int32_t)expr->as.number.int_value);
                } else {
                    codegen_writeln(ctx, "HmlValue %s = hml_val_i64(%ldL);", result, expr->as.number.int_value);
                }
            }
            break;

        case EXPR_BOOL:
            codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%d);", result, expr->as.boolean);
            break;

        case EXPR_STRING: {
            char *escaped = codegen_escape_string(expr->as.string);
            codegen_writeln(ctx, "HmlValue %s = hml_val_string(\"%s\");", result, escaped);
            free(escaped);
            break;
        }

        case EXPR_RUNE:
            codegen_writeln(ctx, "HmlValue %s = hml_val_rune(%u);", result, expr->as.rune);
            break;

        case EXPR_NULL:
            codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            break;

        case EXPR_IDENT:
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
            // Poll constants
            } else if (strcmp(expr->as.ident, "POLLIN") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_i32(POLLIN);", result);
            } else if (strcmp(expr->as.ident, "POLLOUT") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_i32(POLLOUT);", result);
            } else if (strcmp(expr->as.ident, "POLLERR") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_i32(POLLERR);", result);
            } else if (strcmp(expr->as.ident, "POLLHUP") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_i32(POLLHUP);", result);
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
            } else if (strcmp(expr->as.ident, "__abs") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_abs, 1, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__min") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_min, 2, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "__max") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_max, 2, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "__clamp") == 0 || strcmp(expr->as.ident, "clamp") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_clamp, 3, 3, 0);", result);
            } else if (strcmp(expr->as.ident, "__rand") == 0 || strcmp(expr->as.ident, "rand") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_rand, 0, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "__rand_range") == 0 || strcmp(expr->as.ident, "rand_range") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_rand_range, 2, 2, 0);", result);
            } else if (strcmp(expr->as.ident, "__seed") == 0 || strcmp(expr->as.ident, "seed") == 0) {
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
            } else if (strcmp(expr->as.ident, "__strerror") == 0 || strcmp(expr->as.ident, "strerror") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_strerror, 0, 0, 0);", result);
            } else if (strcmp(expr->as.ident, "__dirent_name") == 0 || strcmp(expr->as.ident, "dirent_name") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_dirent_name, 1, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__string_to_cstr") == 0 || strcmp(expr->as.ident, "string_to_cstr") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_string_to_cstr, 1, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__cstr_to_string") == 0 || strcmp(expr->as.ident, "cstr_to_string") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_cstr_to_string, 1, 1, 0);", result);
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
            // WebSocket builtins
            } else if (strcmp(expr->as.ident, "__lws_ws_connect") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_connect, 1, 1, 0);", result);
            } else if (strcmp(expr->as.ident, "__lws_ws_send_text") == 0) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_builtin_lws_ws_send_text, 2, 2, 0);", result);
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
            // OS info builtins
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
            // Filesystem builtins
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
                    // Shadow variable (like catch param) - use bare name, shadows module vars
                    // Must be checked BEFORE module prefix check
                    codegen_writeln(ctx, "HmlValue %s = %s;", result, expr->as.ident);
                } else if (codegen_is_local(ctx, expr->as.ident)) {
                    // Local variable - check context to determine how to access
                    if (ctx->current_module) {
                        // In a module - check if it's a module export (self-reference in closure)
                        ExportedSymbol *exp = module_find_export(ctx->current_module, expr->as.ident);
                        if (exp) {
                            // Use the mangled export name to access module-level function
                            codegen_writeln(ctx, "HmlValue %s = %s;", result, exp->mangled_name);
                        } else {
                            codegen_writeln(ctx, "HmlValue %s = %s;", result, expr->as.ident);
                        }
                    } else if (codegen_is_main_var(ctx, expr->as.ident)) {
                        // Main file top-level variable - use _main_ prefix
                        codegen_writeln(ctx, "HmlValue %s = _main_%s;", result, expr->as.ident);
                    } else {
                        // True local variable (not a main var) - use bare name
                        codegen_writeln(ctx, "HmlValue %s = %s;", result, expr->as.ident);
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
                    codegen_writeln(ctx, "HmlValue %s = %s;", result, expr->as.ident);
                }
            }
            // OPTIMIZATION: Use conditional retain to skip for primitives (i32, i64, f64, bool)
            codegen_writeln(ctx, "hml_retain_if_needed(&%s);", result);
            break;

        case EXPR_BINARY: {
            // OPTIMIZATION: Short-circuit evaluation for && and ||
            // This matches the interpreter's behavior and avoids unnecessary computation
            if (expr->as.binary.op == OP_AND) {
                // Short-circuit AND: if left is false, skip right evaluation
                char *left = codegen_expr(ctx, expr->as.binary.left);
                codegen_writeln(ctx, "HmlValue %s;", result);
                codegen_writeln(ctx, "if (!hml_to_bool(%s)) {", left);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_val_bool(0);", result);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                char *right = codegen_expr(ctx, expr->as.binary.right);
                codegen_writeln(ctx, "%s = hml_val_bool(hml_to_bool(%s));", result, right);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", right);
                free(right);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", left);
                free(left);
                break;
            }

            if (expr->as.binary.op == OP_OR) {
                // Short-circuit OR: if left is true, skip right evaluation
                char *left = codegen_expr(ctx, expr->as.binary.left);
                codegen_writeln(ctx, "HmlValue %s;", result);
                codegen_writeln(ctx, "if (hml_to_bool(%s)) {", left);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_val_bool(1);", result);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                char *right = codegen_expr(ctx, expr->as.binary.right);
                codegen_writeln(ctx, "%s = hml_val_bool(hml_to_bool(%s));", result, right);
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", right);
                free(right);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
                codegen_writeln(ctx, "hml_release_if_needed(&%s);", left);
                free(left);
                break;
            }

            // OPTIMIZATION: Detect chained string concatenations (a + b + c + ...)
            // Use hml_string_concat3/4/5 for single-allocation efficiency
            {
                int concat_count = 0;
                if (is_string_concat_chain(expr, &concat_count)) {
                    Expr *elements[6];
                    count_string_concat_chain(expr, elements, 6);

                    // Generate code for all elements
                    char *temps[5];
                    for (int i = 0; i < concat_count; i++) {
                        temps[i] = codegen_expr(ctx, elements[i]);
                    }

                    // Call the appropriate concat function
                    if (concat_count == 3) {
                        codegen_writeln(ctx, "HmlValue %s = hml_string_concat3(%s, %s, %s);",
                                      result, temps[0], temps[1], temps[2]);
                    } else if (concat_count == 4) {
                        codegen_writeln(ctx, "HmlValue %s = hml_string_concat4(%s, %s, %s, %s);",
                                      result, temps[0], temps[1], temps[2], temps[3]);
                    } else if (concat_count == 5) {
                        codegen_writeln(ctx, "HmlValue %s = hml_string_concat5(%s, %s, %s, %s, %s);",
                                      result, temps[0], temps[1], temps[2], temps[3], temps[4]);
                    }

                    // Release all temps
                    for (int i = 0; i < concat_count; i++) {
                        codegen_writeln(ctx, "hml_release_if_needed(&%s);", temps[i]);
                        free(temps[i]);
                    }
                    break;
                }
            }

            // OPTIMIZATION: Constant folding for number literals
            // If both operands are compile-time known constants, compute the result at compile time
            if (expr->as.binary.left->type == EXPR_NUMBER &&
                expr->as.binary.right->type == EXPR_NUMBER &&
                !expr->as.binary.left->as.number.is_float &&
                !expr->as.binary.right->as.number.is_float) {
                int64_t l = expr->as.binary.left->as.number.int_value;
                int64_t r = expr->as.binary.right->as.number.int_value;
                int64_t const_result = 0;
                int is_bool_result = 0;
                int can_fold = 1;

                switch (expr->as.binary.op) {
                    case OP_ADD: const_result = l + r; break;
                    case OP_SUB: const_result = l - r; break;
                    case OP_MUL: const_result = l * r; break;
                    case OP_DIV:
                        if (r != 0) { const_result = l / r; } else { can_fold = 0; }
                        break;
                    case OP_MOD:
                        if (r != 0) { const_result = l % r; } else { can_fold = 0; }
                        break;
                    case OP_LESS: const_result = l < r; is_bool_result = 1; break;
                    case OP_LESS_EQUAL: const_result = l <= r; is_bool_result = 1; break;
                    case OP_GREATER: const_result = l > r; is_bool_result = 1; break;
                    case OP_GREATER_EQUAL: const_result = l >= r; is_bool_result = 1; break;
                    case OP_EQUAL: const_result = l == r; is_bool_result = 1; break;
                    case OP_NOT_EQUAL: const_result = l != r; is_bool_result = 1; break;
                    case OP_BIT_AND: const_result = l & r; break;
                    case OP_BIT_OR: const_result = l | r; break;
                    case OP_BIT_XOR: const_result = l ^ r; break;
                    case OP_BIT_LSHIFT: const_result = l << r; break;
                    case OP_BIT_RSHIFT: const_result = l >> r; break;
                    default: can_fold = 0; break;
                }

                if (can_fold) {
                    if (is_bool_result) {
                        codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%d);", result, (int)const_result);
                    } else if (const_result >= INT32_MIN && const_result <= INT32_MAX) {
                        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(%d);", result, (int32_t)const_result);
                    } else {
                        codegen_writeln(ctx, "HmlValue %s = hml_val_i64(%ldL);", result, const_result);
                    }
                    break;
                }
            }

            // General case: evaluate both operands
            char *left = codegen_expr(ctx, expr->as.binary.left);
            char *right = codegen_expr(ctx, expr->as.binary.right);

            // OPTIMIZATION: i32 and i64 fast paths for binary operations
            // This matches the interpreter's fast paths for common integer operations
            // Check at runtime: i32 first (most common), then i64, then generic
            const char *i32_fast_fn = NULL;
            const char *i64_fast_fn = NULL;
            switch (expr->as.binary.op) {
                case OP_ADD: i32_fast_fn = "hml_i32_add"; i64_fast_fn = "hml_i64_add"; break;
                case OP_SUB: i32_fast_fn = "hml_i32_sub"; i64_fast_fn = "hml_i64_sub"; break;
                case OP_MUL: i32_fast_fn = "hml_i32_mul"; i64_fast_fn = "hml_i64_mul"; break;
                case OP_DIV: i32_fast_fn = "hml_i32_div"; i64_fast_fn = "hml_i64_div"; break;
                case OP_MOD: i32_fast_fn = "hml_i32_mod"; i64_fast_fn = "hml_i64_mod"; break;
                case OP_LESS: i32_fast_fn = "hml_i32_lt"; i64_fast_fn = "hml_i64_lt"; break;
                case OP_LESS_EQUAL: i32_fast_fn = "hml_i32_le"; i64_fast_fn = "hml_i64_le"; break;
                case OP_GREATER: i32_fast_fn = "hml_i32_gt"; i64_fast_fn = "hml_i64_gt"; break;
                case OP_GREATER_EQUAL: i32_fast_fn = "hml_i32_ge"; i64_fast_fn = "hml_i64_ge"; break;
                case OP_EQUAL: i32_fast_fn = "hml_i32_eq"; i64_fast_fn = "hml_i64_eq"; break;
                case OP_NOT_EQUAL: i32_fast_fn = "hml_i32_ne"; i64_fast_fn = "hml_i64_ne"; break;
                case OP_BIT_AND: i32_fast_fn = "hml_i32_bit_and"; i64_fast_fn = "hml_i64_bit_and"; break;
                case OP_BIT_OR: i32_fast_fn = "hml_i32_bit_or"; i64_fast_fn = "hml_i64_bit_or"; break;
                case OP_BIT_XOR: i32_fast_fn = "hml_i32_bit_xor"; i64_fast_fn = "hml_i64_bit_xor"; break;
                case OP_BIT_LSHIFT: i32_fast_fn = "hml_i32_lshift"; i64_fast_fn = "hml_i64_lshift"; break;
                case OP_BIT_RSHIFT: i32_fast_fn = "hml_i32_rshift"; i64_fast_fn = "hml_i64_rshift"; break;
                default: break;
            }

            if (i32_fast_fn && i64_fast_fn) {
                // Generate cascading fast paths: i32 -> i64 -> generic
                codegen_writeln(ctx, "HmlValue %s = hml_both_i32(%s, %s) ? %s(%s, %s) : (hml_both_i64(%s, %s) ? %s(%s, %s) : hml_binary_op(%s, %s, %s));",
                              result, left, right, i32_fast_fn, left, right,
                              left, right, i64_fast_fn, left, right,
                              codegen_hml_binary_op(expr->as.binary.op), left, right);
            } else if (i32_fast_fn) {
                // Generate i32 fast path only
                codegen_writeln(ctx, "HmlValue %s = hml_both_i32(%s, %s) ? %s(%s, %s) : hml_binary_op(%s, %s, %s);",
                              result, left, right, i32_fast_fn, left, right,
                              codegen_hml_binary_op(expr->as.binary.op), left, right);
            } else {
                // No fast path available - use generic binary_op
                codegen_writeln(ctx, "HmlValue %s = hml_binary_op(%s, %s, %s);",
                              result, codegen_hml_binary_op(expr->as.binary.op), left, right);
            }

            // Use optimized release that skips primitives
            codegen_writeln(ctx, "hml_release_if_needed(&%s);", left);
            codegen_writeln(ctx, "hml_release_if_needed(&%s);", right);
            free(left);
            free(right);
            break;
        }

        case EXPR_UNARY: {
            // OPTIMIZATION: Constant folding for unary operations on literals
            if (expr->as.unary.operand->type == EXPR_NUMBER &&
                !expr->as.unary.operand->as.number.is_float) {
                int64_t val = expr->as.unary.operand->as.number.int_value;
                int can_fold = 1;

                switch (expr->as.unary.op) {
                    case UNARY_NEGATE:
                        val = -val;
                        break;
                    case UNARY_BIT_NOT:
                        val = ~val;
                        break;
                    default:
                        can_fold = 0;
                        break;
                }

                if (can_fold) {
                    if (val >= INT32_MIN && val <= INT32_MAX) {
                        codegen_writeln(ctx, "HmlValue %s = hml_val_i32(%d);", result, (int32_t)val);
                    } else {
                        codegen_writeln(ctx, "HmlValue %s = hml_val_i64(%ldL);", result, val);
                    }
                    break;
                }
            }

            // Constant folding for NOT on boolean literals
            if (expr->as.unary.op == UNARY_NOT && expr->as.unary.operand->type == EXPR_BOOL) {
                codegen_writeln(ctx, "HmlValue %s = hml_val_bool(%d);", result, !expr->as.unary.operand->as.boolean);
                break;
            }

            char *operand = codegen_expr(ctx, expr->as.unary.operand);
            codegen_writeln(ctx, "HmlValue %s = hml_unary_op(%s, %s);",
                          result, codegen_hml_unary_op(expr->as.unary.op), operand);
            codegen_writeln(ctx, "hml_release_if_needed(&%s);", operand);
            free(operand);
            break;
        }

        case EXPR_TERNARY: {
            char *cond = codegen_expr(ctx, expr->as.ternary.condition);
            codegen_writeln(ctx, "HmlValue %s;", result);
            codegen_writeln(ctx, "if (hml_to_bool(%s)) {", cond);
            codegen_indent_inc(ctx);
            char *true_val = codegen_expr(ctx, expr->as.ternary.true_expr);
            codegen_writeln(ctx, "%s = %s;", result, true_val);
            free(true_val);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "} else {");
            codegen_indent_inc(ctx);
            char *false_val = codegen_expr(ctx, expr->as.ternary.false_expr);
            codegen_writeln(ctx, "%s = %s;", result, false_val);
            free(false_val);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            codegen_writeln(ctx, "hml_release(&%s);", cond);
            free(cond);
            break;
        }

        case EXPR_CALL: {
            // Check for builtin function calls
            if (expr->as.call.func->type == EXPR_IDENT) {
                const char *fn_name = expr->as.call.func->as.ident;

                // Handle print builtin
                if (strcmp(fn_name, "print") == 0 && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "hml_print(%s);", arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                    free(arg);
                    break;
                }

                // Handle eprint builtin
                if (strcmp(fn_name, "eprint") == 0 && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "hml_eprint(%s);", arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                    free(arg);
                    break;
                }

                // Handle typeof builtin
                if (strcmp(fn_name, "typeof") == 0 && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_string(hml_typeof(%s));", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // Handle assert builtin
                if (strcmp(fn_name, "assert") == 0 && expr->as.call.num_args >= 1) {
                    char *cond = codegen_expr(ctx, expr->as.call.args[0]);
                    if (expr->as.call.num_args >= 2) {
                        char *msg = codegen_expr(ctx, expr->as.call.args[1]);
                        codegen_writeln(ctx, "hml_assert(%s, %s);", cond, msg);
                        codegen_writeln(ctx, "hml_release(&%s);", msg);
                        free(msg);
                    } else {
                        codegen_writeln(ctx, "hml_assert(%s, hml_val_null());", cond);
                    }
                    codegen_writeln(ctx, "hml_release(&%s);", cond);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                    free(cond);
                    break;
                }

                // Handle panic builtin
                if (strcmp(fn_name, "panic") == 0) {
                    if (expr->as.call.num_args >= 1) {
                        char *msg = codegen_expr(ctx, expr->as.call.args[0]);
                        codegen_writeln(ctx, "hml_panic(%s);", msg);
                        free(msg);
                    } else {
                        codegen_writeln(ctx, "hml_panic(hml_val_string(\"panic!\"));");
                    }
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                    break;
                }

                // Handle exec builtin for command execution
                if ((strcmp(fn_name, "exec") == 0 || strcmp(fn_name, "__exec") == 0) && expr->as.call.num_args == 1) {
                    char *cmd = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_exec(%s);", result, cmd);
                    codegen_writeln(ctx, "hml_release(&%s);", cmd);
                    free(cmd);
                    break;
                }

                // Handle open builtin for file I/O
                if (strcmp(fn_name, "open") == 0 && (expr->as.call.num_args == 1 || expr->as.call.num_args == 2)) {
                    char *path = codegen_expr(ctx, expr->as.call.args[0]);
                    if (expr->as.call.num_args == 2) {
                        char *mode = codegen_expr(ctx, expr->as.call.args[1]);
                        codegen_writeln(ctx, "HmlValue %s = hml_open(%s, %s);", result, path, mode);
                        codegen_writeln(ctx, "hml_release(&%s);", mode);
                        free(mode);
                    } else {
                        codegen_writeln(ctx, "HmlValue %s = hml_open(%s, hml_val_string(\"r\"));", result, path);
                    }
                    codegen_writeln(ctx, "hml_release(&%s);", path);
                    free(path);
                    break;
                }

                // Handle spawn builtin for async
                if (strcmp(fn_name, "spawn") == 0 && expr->as.call.num_args >= 1) {
                    char *fn_val = codegen_expr(ctx, expr->as.call.args[0]);
                    int num_spawn_args = expr->as.call.num_args - 1;

                    if (num_spawn_args > 0) {
                        // Capture counter before generating args (which may increment it)
                        int args_counter = ctx->temp_counter++;
                        // Build args array
                        codegen_writeln(ctx, "HmlValue _spawn_args%d[%d];", args_counter, num_spawn_args);
                        for (int i = 0; i < num_spawn_args; i++) {
                            char *arg = codegen_expr(ctx, expr->as.call.args[i + 1]);
                            codegen_writeln(ctx, "_spawn_args%d[%d] = %s;", args_counter, i, arg);
                            free(arg);
                        }
                        codegen_writeln(ctx, "HmlValue %s = hml_spawn(%s, _spawn_args%d, %d);",
                                      result, fn_val, args_counter, num_spawn_args);
                    } else {
                        codegen_writeln(ctx, "HmlValue %s = hml_spawn(%s, NULL, 0);", result, fn_val);
                    }
                    codegen_writeln(ctx, "hml_release(&%s);", fn_val);
                    free(fn_val);
                    break;
                }

                // Handle join builtin
                if (strcmp(fn_name, "join") == 0 && expr->as.call.num_args == 1) {
                    char *task_val = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_join(%s);", result, task_val);
                    codegen_writeln(ctx, "hml_release(&%s);", task_val);
                    free(task_val);
                    break;
                }

                // Handle detach builtin
                // detach(task) - detach an already-spawned task
                // detach(fn, args...) - spawn and immediately detach (fire-and-forget)
                if (strcmp(fn_name, "detach") == 0 && expr->as.call.num_args >= 1) {
                    if (expr->as.call.num_args == 1) {
                        // detach(task) - existing behavior
                        char *task_val = codegen_expr(ctx, expr->as.call.args[0]);
                        codegen_writeln(ctx, "hml_detach(%s);", task_val);
                        codegen_writeln(ctx, "hml_release(&%s);", task_val);
                        codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                        free(task_val);
                    } else {
                        // detach(fn, args...) - spawn and detach
                        char *fn_val = codegen_expr(ctx, expr->as.call.args[0]);
                        int num_spawn_args = expr->as.call.num_args - 1;
                        int args_counter = ctx->temp_counter++;

                        // Build args array
                        codegen_writeln(ctx, "HmlValue _detach_args%d[%d];", args_counter, num_spawn_args);
                        for (int i = 0; i < num_spawn_args; i++) {
                            char *arg = codegen_expr(ctx, expr->as.call.args[i + 1]);
                            codegen_writeln(ctx, "_detach_args%d[%d] = %s;", args_counter, i, arg);
                            free(arg);
                        }

                        // Spawn then immediately detach
                        int task_counter = ctx->temp_counter++;
                        codegen_writeln(ctx, "HmlValue _detach_task%d = hml_spawn(%s, _detach_args%d, %d);",
                                      task_counter, fn_val, args_counter, num_spawn_args);
                        codegen_writeln(ctx, "hml_detach(_detach_task%d);", task_counter);
                        codegen_writeln(ctx, "hml_release(&_detach_task%d);", task_counter);
                        codegen_writeln(ctx, "hml_release(&%s);", fn_val);
                        codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                        free(fn_val);
                    }
                    break;
                }

                // Handle task_debug_info builtin
                if (strcmp(fn_name, "task_debug_info") == 0 && expr->as.call.num_args == 1) {
                    char *task_val = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "hml_task_debug_info(%s);", task_val);
                    codegen_writeln(ctx, "hml_release(&%s);", task_val);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                    free(task_val);
                    break;
                }

                // Handle channel builtin
                if (strcmp(fn_name, "channel") == 0 && expr->as.call.num_args == 1) {
                    char *cap = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_channel(%s.as.as_i32);", result, cap);
                    codegen_writeln(ctx, "hml_release(&%s);", cap);
                    free(cap);
                    break;
                }

                // Handle signal builtin
                if (strcmp(fn_name, "signal") == 0 && expr->as.call.num_args == 2) {
                    char *signum = codegen_expr(ctx, expr->as.call.args[0]);
                    char *handler = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_signal(%s, %s);", result, signum, handler);
                    codegen_writeln(ctx, "hml_release(&%s);", signum);
                    codegen_writeln(ctx, "hml_release(&%s);", handler);
                    free(signum);
                    free(handler);
                    break;
                }

                // Handle raise builtin
                if (strcmp(fn_name, "raise") == 0 && expr->as.call.num_args == 1) {
                    char *signum = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_raise(%s);", result, signum);
                    codegen_writeln(ctx, "hml_release(&%s);", signum);
                    free(signum);
                    break;
                }

                // Handle alloc builtin
                if (strcmp(fn_name, "alloc") == 0 && expr->as.call.num_args == 1) {
                    char *size = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_alloc(hml_to_i32(%s));", result, size);
                    codegen_writeln(ctx, "hml_release(&%s);", size);
                    free(size);
                    break;
                }

                // Handle free builtin
                if (strcmp(fn_name, "free") == 0 && expr->as.call.num_args == 1) {
                    char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "hml_free(%s);", ptr);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                    codegen_writeln(ctx, "hml_release(&%s);", ptr);
                    free(ptr);
                    break;
                }

                // Handle buffer builtin
                if (strcmp(fn_name, "buffer") == 0 && expr->as.call.num_args == 1) {
                    char *size = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_buffer(hml_to_i32(%s));", result, size);
                    codegen_writeln(ctx, "hml_release(&%s);", size);
                    free(size);
                    break;
                }

                // Handle memset builtin
                if (strcmp(fn_name, "memset") == 0 && expr->as.call.num_args == 3) {
                    char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
                    char *byte_val = codegen_expr(ctx, expr->as.call.args[1]);
                    char *size = codegen_expr(ctx, expr->as.call.args[2]);
                    codegen_writeln(ctx, "hml_memset(%s, (uint8_t)hml_to_i32(%s), hml_to_i32(%s));", ptr, byte_val, size);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                    codegen_writeln(ctx, "hml_release(&%s);", ptr);
                    codegen_writeln(ctx, "hml_release(&%s);", byte_val);
                    codegen_writeln(ctx, "hml_release(&%s);", size);
                    free(ptr);
                    free(byte_val);
                    free(size);
                    break;
                }

                // Handle memcpy builtin
                if (strcmp(fn_name, "memcpy") == 0 && expr->as.call.num_args == 3) {
                    char *dest = codegen_expr(ctx, expr->as.call.args[0]);
                    char *src = codegen_expr(ctx, expr->as.call.args[1]);
                    char *size = codegen_expr(ctx, expr->as.call.args[2]);
                    codegen_writeln(ctx, "hml_memcpy(%s, %s, hml_to_i32(%s));", dest, src, size);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                    codegen_writeln(ctx, "hml_release(&%s);", dest);
                    codegen_writeln(ctx, "hml_release(&%s);", src);
                    codegen_writeln(ctx, "hml_release(&%s);", size);
                    free(dest);
                    free(src);
                    free(size);
                    break;
                }

                // Handle realloc builtin
                if (strcmp(fn_name, "realloc") == 0 && expr->as.call.num_args == 2) {
                    char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
                    char *size = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_realloc(%s, hml_to_i32(%s));", result, ptr, size);
                    codegen_writeln(ctx, "hml_release(&%s);", ptr);
                    codegen_writeln(ctx, "hml_release(&%s);", size);
                    free(ptr);
                    free(size);
                    break;
                }

                // ========== FFI CALLBACK BUILTINS ==========

                // callback(fn, param_types, return_type) -> ptr
                if (strcmp(fn_name, "callback") == 0 && (expr->as.call.num_args == 2 || expr->as.call.num_args == 3)) {
                    char *fn_arg = codegen_expr(ctx, expr->as.call.args[0]);
                    char *param_types = codegen_expr(ctx, expr->as.call.args[1]);
                    char *ret_type;
                    if (expr->as.call.num_args == 3) {
                        ret_type = codegen_expr(ctx, expr->as.call.args[2]);
                    } else {
                        ret_type = strdup("hml_val_string(\"void\")");
                    }
                    codegen_writeln(ctx, "HmlValue %s = hml_builtin_callback(NULL, %s, %s, %s);", result, fn_arg, param_types, ret_type);
                    codegen_writeln(ctx, "hml_release(&%s);", fn_arg);
                    codegen_writeln(ctx, "hml_release(&%s);", param_types);
                    if (expr->as.call.num_args == 3) {
                        codegen_writeln(ctx, "hml_release(&%s);", ret_type);
                    }
                    free(fn_arg);
                    free(param_types);
                    free(ret_type);
                    break;
                }

                // callback_free(ptr)
                if (strcmp(fn_name, "callback_free") == 0 && expr->as.call.num_args == 1) {
                    char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_builtin_callback_free(NULL, %s);", result, ptr);
                    codegen_writeln(ctx, "hml_release(&%s);", ptr);
                    free(ptr);
                    break;
                }

                // ptr_deref_i32(ptr) -> i32
                if (strcmp(fn_name, "ptr_deref_i32") == 0 && expr->as.call.num_args == 1) {
                    char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_deref_i32(NULL, %s);", result, ptr);
                    codegen_writeln(ctx, "hml_release(&%s);", ptr);
                    free(ptr);
                    break;
                }

                // ptr_write_i32(ptr, value)
                if (strcmp(fn_name, "ptr_write_i32") == 0 && expr->as.call.num_args == 2) {
                    char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
                    char *value = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_write_i32(NULL, %s, %s);", result, ptr, value);
                    codegen_writeln(ctx, "hml_release(&%s);", ptr);
                    codegen_writeln(ctx, "hml_release(&%s);", value);
                    free(ptr);
                    free(value);
                    break;
                }

                // ptr_offset(ptr, offset, element_size) -> ptr
                if (strcmp(fn_name, "ptr_offset") == 0 && expr->as.call.num_args == 3) {
                    char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
                    char *offset = codegen_expr(ctx, expr->as.call.args[1]);
                    char *elem_size = codegen_expr(ctx, expr->as.call.args[2]);
                    codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_offset(NULL, %s, %s, %s);", result, ptr, offset, elem_size);
                    codegen_writeln(ctx, "hml_release(&%s);", ptr);
                    codegen_writeln(ctx, "hml_release(&%s);", offset);
                    codegen_writeln(ctx, "hml_release(&%s);", elem_size);
                    free(ptr);
                    free(offset);
                    free(elem_size);
                    break;
                }

                // ptr_read_i32(ptr) -> i32 (dereference pointer-to-pointer, for qsort)
                if (strcmp(fn_name, "ptr_read_i32") == 0 && expr->as.call.num_args == 1) {
                    char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_builtin_ptr_read_i32(NULL, %s);", result, ptr);
                    codegen_writeln(ctx, "hml_release(&%s);", ptr);
                    free(ptr);
                    break;
                }

                // ========== MATH BUILTINS ==========

                // sqrt(x)
                if ((strcmp(fn_name, "sqrt") == 0 || strcmp(fn_name, "__sqrt") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_sqrt(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // sin(x)
                if ((strcmp(fn_name, "sin") == 0 || strcmp(fn_name, "__sin") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_sin(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // cos(x)
                if ((strcmp(fn_name, "cos") == 0 || strcmp(fn_name, "__cos") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_cos(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // tan(x)
                if ((strcmp(fn_name, "tan") == 0 || strcmp(fn_name, "__tan") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_tan(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // asin(x)
                if ((strcmp(fn_name, "asin") == 0 || strcmp(fn_name, "__asin") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_asin(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // acos(x)
                if ((strcmp(fn_name, "acos") == 0 || strcmp(fn_name, "__acos") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_acos(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // atan(x)
                if ((strcmp(fn_name, "atan") == 0 || strcmp(fn_name, "__atan") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_atan(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // atan2(y, x)
                if ((strcmp(fn_name, "atan2") == 0 || strcmp(fn_name, "__atan2") == 0) && expr->as.call.num_args == 2) {
                    char *y_arg = codegen_expr(ctx, expr->as.call.args[0]);
                    char *x_arg = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_atan2(%s, %s);", result, y_arg, x_arg);
                    codegen_writeln(ctx, "hml_release(&%s);", y_arg);
                    codegen_writeln(ctx, "hml_release(&%s);", x_arg);
                    free(y_arg);
                    free(x_arg);
                    break;
                }

                // floor(x)
                if ((strcmp(fn_name, "floor") == 0 || strcmp(fn_name, "__floor") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_floor(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // ceil(x)
                if ((strcmp(fn_name, "ceil") == 0 || strcmp(fn_name, "__ceil") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_ceil(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // round(x)
                if ((strcmp(fn_name, "round") == 0 || strcmp(fn_name, "__round") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_round(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // trunc(x)
                if ((strcmp(fn_name, "trunc") == 0 || strcmp(fn_name, "__trunc") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_trunc(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // abs(x)
                if ((strcmp(fn_name, "abs") == 0 || strcmp(fn_name, "__abs") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_abs(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // pow(base, exp)
                if ((strcmp(fn_name, "pow") == 0 || strcmp(fn_name, "__pow") == 0) && expr->as.call.num_args == 2) {
                    char *base = codegen_expr(ctx, expr->as.call.args[0]);
                    char *exp_arg = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_pow(%s, %s);", result, base, exp_arg);
                    codegen_writeln(ctx, "hml_release(&%s);", base);
                    codegen_writeln(ctx, "hml_release(&%s);", exp_arg);
                    free(base);
                    free(exp_arg);
                    break;
                }

                // exp(x)
                if ((strcmp(fn_name, "exp") == 0 || strcmp(fn_name, "__exp") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_exp(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // log(x)
                if ((strcmp(fn_name, "log") == 0 || strcmp(fn_name, "__log") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_log(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // log10(x)
                if ((strcmp(fn_name, "log10") == 0 || strcmp(fn_name, "__log10") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_log10(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // log2(x)
                if ((strcmp(fn_name, "log2") == 0 || strcmp(fn_name, "__log2") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_log2(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // min(a, b)
                if ((strcmp(fn_name, "min") == 0 || strcmp(fn_name, "__min") == 0) && expr->as.call.num_args == 2) {
                    char *a = codegen_expr(ctx, expr->as.call.args[0]);
                    char *b = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_min(%s, %s);", result, a, b);
                    codegen_writeln(ctx, "hml_release(&%s);", a);
                    codegen_writeln(ctx, "hml_release(&%s);", b);
                    free(a);
                    free(b);
                    break;
                }

                // max(a, b)
                if ((strcmp(fn_name, "max") == 0 || strcmp(fn_name, "__max") == 0) && expr->as.call.num_args == 2) {
                    char *a = codegen_expr(ctx, expr->as.call.args[0]);
                    char *b = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_max(%s, %s);", result, a, b);
                    codegen_writeln(ctx, "hml_release(&%s);", a);
                    codegen_writeln(ctx, "hml_release(&%s);", b);
                    free(a);
                    free(b);
                    break;
                }

                // rand()
                if ((strcmp(fn_name, "rand") == 0 || strcmp(fn_name, "__rand") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_rand();", result);
                    break;
                }

                // seed(value)
                if ((strcmp(fn_name, "seed") == 0 || strcmp(fn_name, "__seed") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "hml_seed(%s);", arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                    free(arg);
                    break;
                }

                // rand_range(min, max) - also __rand_range
                if ((strcmp(fn_name, "rand_range") == 0 || strcmp(fn_name, "__rand_range") == 0) && expr->as.call.num_args == 2) {
                    char *min_arg = codegen_expr(ctx, expr->as.call.args[0]);
                    char *max_arg = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_rand_range(%s, %s);", result, min_arg, max_arg);
                    codegen_writeln(ctx, "hml_release(&%s);", min_arg);
                    codegen_writeln(ctx, "hml_release(&%s);", max_arg);
                    free(min_arg);
                    free(max_arg);
                    break;
                }

                // clamp(value, min, max) - also __clamp
                if ((strcmp(fn_name, "clamp") == 0 || strcmp(fn_name, "__clamp") == 0) && expr->as.call.num_args == 3) {
                    char *val = codegen_expr(ctx, expr->as.call.args[0]);
                    char *min_arg = codegen_expr(ctx, expr->as.call.args[1]);
                    char *max_arg = codegen_expr(ctx, expr->as.call.args[2]);
                    codegen_writeln(ctx, "HmlValue %s = hml_clamp(%s, %s, %s);", result, val, min_arg, max_arg);
                    codegen_writeln(ctx, "hml_release(&%s);", val);
                    codegen_writeln(ctx, "hml_release(&%s);", min_arg);
                    codegen_writeln(ctx, "hml_release(&%s);", max_arg);
                    free(val);
                    free(min_arg);
                    free(max_arg);
                    break;
                }

                // ========== TIME BUILTINS ==========
                // Note: For unprefixed builtins, only use builtin if NOT a local/import

                // now() - but NOT if 'now' is a local/import (e.g., from @stdlib/datetime)
                if ((strcmp(fn_name, "__now") == 0 ||
                     (strcmp(fn_name, "now") == 0 && !codegen_is_local(ctx, fn_name))) &&
                    expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_now();", result);
                    break;
                }

                // time_ms() - but NOT if 'time_ms' is a local/import
                if ((strcmp(fn_name, "__time_ms") == 0 ||
                     (strcmp(fn_name, "time_ms") == 0 && !codegen_is_local(ctx, fn_name))) &&
                    expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_time_ms();", result);
                    break;
                }

                // clock() - but NOT if 'clock' is a local/import
                if ((strcmp(fn_name, "__clock") == 0 ||
                     (strcmp(fn_name, "clock") == 0 && !codegen_is_local(ctx, fn_name))) &&
                    expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_clock();", result);
                    break;
                }

                // sleep(seconds) - but NOT if 'sleep' is a local/import
                if ((strcmp(fn_name, "__sleep") == 0 ||
                     (strcmp(fn_name, "sleep") == 0 && !codegen_is_local(ctx, fn_name))) &&
                    expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "hml_sleep(%s);", arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                    free(arg);
                    break;
                }

                // ========== DATETIME BUILTINS ==========

                // localtime(timestamp)
                if ((strcmp(fn_name, "localtime") == 0 || strcmp(fn_name, "__localtime") == 0) && expr->as.call.num_args == 1) {
                    char *ts = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_localtime(%s);", result, ts);
                    codegen_writeln(ctx, "hml_release(&%s);", ts);
                    free(ts);
                    break;
                }

                // gmtime(timestamp)
                if ((strcmp(fn_name, "gmtime") == 0 || strcmp(fn_name, "__gmtime") == 0) && expr->as.call.num_args == 1) {
                    char *ts = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_gmtime(%s);", result, ts);
                    codegen_writeln(ctx, "hml_release(&%s);", ts);
                    free(ts);
                    break;
                }

                // mktime(time_obj)
                if ((strcmp(fn_name, "mktime") == 0 || strcmp(fn_name, "__mktime") == 0) && expr->as.call.num_args == 1) {
                    char *obj = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_mktime(%s);", result, obj);
                    codegen_writeln(ctx, "hml_release(&%s);", obj);
                    free(obj);
                    break;
                }

                // strftime(format, time_obj)
                if ((strcmp(fn_name, "strftime") == 0 || strcmp(fn_name, "__strftime") == 0) && expr->as.call.num_args == 2) {
                    char *fmt = codegen_expr(ctx, expr->as.call.args[0]);
                    char *obj = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_strftime(%s, %s);", result, fmt, obj);
                    codegen_writeln(ctx, "hml_release(&%s);", fmt);
                    codegen_writeln(ctx, "hml_release(&%s);", obj);
                    free(fmt);
                    free(obj);
                    break;
                }

                // ========== ENVIRONMENT BUILTINS ==========

                // getenv(name)
                if ((strcmp(fn_name, "getenv") == 0 || strcmp(fn_name, "__getenv") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_getenv(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // setenv(name, value)
                if ((strcmp(fn_name, "setenv") == 0 || strcmp(fn_name, "__setenv") == 0) && expr->as.call.num_args == 2) {
                    char *name_arg = codegen_expr(ctx, expr->as.call.args[0]);
                    char *value_arg = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "hml_setenv(%s, %s);", name_arg, value_arg);
                    codegen_writeln(ctx, "hml_release(&%s);", name_arg);
                    codegen_writeln(ctx, "hml_release(&%s);", value_arg);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                    free(name_arg);
                    free(value_arg);
                    break;
                }

                // unsetenv(name)
                if ((strcmp(fn_name, "unsetenv") == 0 || strcmp(fn_name, "__unsetenv") == 0) && expr->as.call.num_args == 1) {
                    char *name_arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "hml_unsetenv(%s);", name_arg);
                    codegen_writeln(ctx, "hml_release(&%s);", name_arg);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                    free(name_arg);
                    break;
                }

                // exit(code)
                if ((strcmp(fn_name, "exit") == 0 || strcmp(fn_name, "__exit") == 0) && expr->as.call.num_args == 1) {
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "hml_exit(%s);", arg);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                    free(arg);
                    break;
                }

                // abort()
                if ((strcmp(fn_name, "abort") == 0 || strcmp(fn_name, "__abort") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "hml_abort();");
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                    break;
                }

                // get_pid()
                if ((strcmp(fn_name, "get_pid") == 0 || strcmp(fn_name, "__get_pid") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_get_pid();", result);
                    break;
                }

                // ========== PROCESS MANAGEMENT BUILTINS ==========

                // getppid()
                if ((strcmp(fn_name, "getppid") == 0 || strcmp(fn_name, "__getppid") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_getppid();", result);
                    break;
                }

                // getuid()
                if ((strcmp(fn_name, "getuid") == 0 || strcmp(fn_name, "__getuid") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_getuid();", result);
                    break;
                }

                // geteuid()
                if ((strcmp(fn_name, "geteuid") == 0 || strcmp(fn_name, "__geteuid") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_geteuid();", result);
                    break;
                }

                // getgid()
                if ((strcmp(fn_name, "getgid") == 0 || strcmp(fn_name, "__getgid") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_getgid();", result);
                    break;
                }

                // getegid()
                if ((strcmp(fn_name, "getegid") == 0 || strcmp(fn_name, "__getegid") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_getegid();", result);
                    break;
                }

                // fork()
                if ((strcmp(fn_name, "fork") == 0 || strcmp(fn_name, "__fork") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_fork();", result);
                    break;
                }

                // wait()
                if ((strcmp(fn_name, "wait") == 0 || strcmp(fn_name, "__wait") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_wait();", result);
                    break;
                }

                // waitpid(pid, options)
                if ((strcmp(fn_name, "waitpid") == 0 || strcmp(fn_name, "__waitpid") == 0) && expr->as.call.num_args == 2) {
                    char *pid = codegen_expr(ctx, expr->as.call.args[0]);
                    char *opts = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_waitpid(%s, %s);", result, pid, opts);
                    codegen_writeln(ctx, "hml_release(&%s);", pid);
                    codegen_writeln(ctx, "hml_release(&%s);", opts);
                    free(pid);
                    free(opts);
                    break;
                }

                // kill(pid, sig)
                if ((strcmp(fn_name, "kill") == 0 || strcmp(fn_name, "__kill") == 0) && expr->as.call.num_args == 2) {
                    char *pid = codegen_expr(ctx, expr->as.call.args[0]);
                    char *sig = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_kill(%s, %s);", result, pid, sig);
                    codegen_writeln(ctx, "hml_release(&%s);", pid);
                    codegen_writeln(ctx, "hml_release(&%s);", sig);
                    free(pid);
                    free(sig);
                    break;
                }

                // ========== I/O BUILTINS ==========

                // read_line()
                if ((strcmp(fn_name, "read_line") == 0 || strcmp(fn_name, "__read_line") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_read_line();", result);
                    break;
                }

                // ========== TYPE BUILTINS ==========

                // sizeof(type_name)
                if ((strcmp(fn_name, "sizeof") == 0 || strcmp(fn_name, "__sizeof") == 0) && expr->as.call.num_args == 1) {
                    // Check if argument is a type name identifier
                    Expr *arg_expr = expr->as.call.args[0];
                    if (arg_expr->type == EXPR_IDENT) {
                        const char *type_name = arg_expr->as.ident;
                        // List of valid type names
                        if (strcmp(type_name, "i8") == 0 || strcmp(type_name, "i16") == 0 ||
                            strcmp(type_name, "i32") == 0 || strcmp(type_name, "i64") == 0 ||
                            strcmp(type_name, "u8") == 0 || strcmp(type_name, "u16") == 0 ||
                            strcmp(type_name, "u32") == 0 || strcmp(type_name, "u64") == 0 ||
                            strcmp(type_name, "f32") == 0 || strcmp(type_name, "f64") == 0 ||
                            strcmp(type_name, "bool") == 0 || strcmp(type_name, "ptr") == 0 ||
                            strcmp(type_name, "rune") == 0 || strcmp(type_name, "byte") == 0 ||
                            strcmp(type_name, "integer") == 0 || strcmp(type_name, "number") == 0) {
                            // Type name - convert to string literal
                            char *arg_temp = codegen_temp(ctx);
                            codegen_writeln(ctx, "HmlValue %s = hml_val_string(\"%s\");", arg_temp, type_name);
                            codegen_writeln(ctx, "HmlValue %s = hml_sizeof(%s);", result, arg_temp);
                            codegen_writeln(ctx, "hml_release(&%s);", arg_temp);
                            free(arg_temp);
                            break;
                        }
                    }
                    // Fall through: evaluate as expression (for dynamic type checking)
                    char *arg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_sizeof(%s);", result, arg);
                    codegen_writeln(ctx, "hml_release(&%s);", arg);
                    free(arg);
                    break;
                }

                // talloc(type_name, count)
                if ((strcmp(fn_name, "talloc") == 0 || strcmp(fn_name, "__talloc") == 0) && expr->as.call.num_args == 2) {
                    // Check if first argument is a type name identifier
                    Expr *type_expr = expr->as.call.args[0];
                    char *type_arg = NULL;
                    int type_is_temp = 0;
                    if (type_expr->type == EXPR_IDENT) {
                        const char *type_name = type_expr->as.ident;
                        // List of valid type names
                        if (strcmp(type_name, "i8") == 0 || strcmp(type_name, "i16") == 0 ||
                            strcmp(type_name, "i32") == 0 || strcmp(type_name, "i64") == 0 ||
                            strcmp(type_name, "u8") == 0 || strcmp(type_name, "u16") == 0 ||
                            strcmp(type_name, "u32") == 0 || strcmp(type_name, "u64") == 0 ||
                            strcmp(type_name, "f32") == 0 || strcmp(type_name, "f64") == 0 ||
                            strcmp(type_name, "bool") == 0 || strcmp(type_name, "ptr") == 0 ||
                            strcmp(type_name, "rune") == 0 || strcmp(type_name, "byte") == 0 ||
                            strcmp(type_name, "integer") == 0 || strcmp(type_name, "number") == 0) {
                            // Type name - convert to string literal
                            type_arg = codegen_temp(ctx);
                            type_is_temp = 1;
                            codegen_writeln(ctx, "HmlValue %s = hml_val_string(\"%s\");", type_arg, type_name);
                        }
                    }
                    if (!type_arg) {
                        type_arg = codegen_expr(ctx, type_expr);
                    }
                    char *count_arg = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_talloc(%s, %s);", result, type_arg, count_arg);
                    codegen_writeln(ctx, "hml_release(&%s);", type_arg);
                    codegen_writeln(ctx, "hml_release(&%s);", count_arg);
                    if (type_is_temp) {
                        free(type_arg);
                    } else {
                        free(type_arg);
                    }
                    free(count_arg);
                    break;
                }

                // ========== SOCKET BUILTINS ==========

                // socket_create(domain, type, protocol)
                if ((strcmp(fn_name, "socket_create") == 0 || strcmp(fn_name, "__socket_create") == 0) && expr->as.call.num_args == 3) {
                    char *domain = codegen_expr(ctx, expr->as.call.args[0]);
                    char *sock_type = codegen_expr(ctx, expr->as.call.args[1]);
                    char *protocol = codegen_expr(ctx, expr->as.call.args[2]);
                    codegen_writeln(ctx, "HmlValue %s = hml_socket_create(%s, %s, %s);", result, domain, sock_type, protocol);
                    codegen_writeln(ctx, "hml_release(&%s);", domain);
                    codegen_writeln(ctx, "hml_release(&%s);", sock_type);
                    codegen_writeln(ctx, "hml_release(&%s);", protocol);
                    free(domain);
                    free(sock_type);
                    free(protocol);
                    break;
                }

                // dns_resolve(hostname)
                if ((strcmp(fn_name, "dns_resolve") == 0 || strcmp(fn_name, "__dns_resolve") == 0) && expr->as.call.num_args == 1) {
                    char *hostname = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_dns_resolve(%s);", result, hostname);
                    codegen_writeln(ctx, "hml_release(&%s);", hostname);
                    free(hostname);
                    break;
                }

                // ========== OS INFO BUILTINS ==========

                // platform()
                if ((strcmp(fn_name, "platform") == 0 || strcmp(fn_name, "__platform") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_platform();", result);
                    break;
                }

                // arch()
                if ((strcmp(fn_name, "arch") == 0 || strcmp(fn_name, "__arch") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_arch();", result);
                    break;
                }

                // hostname()
                if ((strcmp(fn_name, "hostname") == 0 || strcmp(fn_name, "__hostname") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_hostname();", result);
                    break;
                }

                // username()
                if ((strcmp(fn_name, "username") == 0 || strcmp(fn_name, "__username") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_username();", result);
                    break;
                }

                // homedir()
                if ((strcmp(fn_name, "homedir") == 0 || strcmp(fn_name, "__homedir") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_homedir();", result);
                    break;
                }

                // cpu_count()
                if ((strcmp(fn_name, "cpu_count") == 0 || strcmp(fn_name, "__cpu_count") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_cpu_count();", result);
                    break;
                }

                // total_memory()
                if ((strcmp(fn_name, "total_memory") == 0 || strcmp(fn_name, "__total_memory") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_total_memory();", result);
                    break;
                }

                // free_memory()
                if ((strcmp(fn_name, "free_memory") == 0 || strcmp(fn_name, "__free_memory") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_free_memory();", result);
                    break;
                }

                // os_version()
                if ((strcmp(fn_name, "os_version") == 0 || strcmp(fn_name, "__os_version") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_os_version();", result);
                    break;
                }

                // os_name()
                if ((strcmp(fn_name, "os_name") == 0 || strcmp(fn_name, "__os_name") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_os_name();", result);
                    break;
                }

                // tmpdir()
                if ((strcmp(fn_name, "tmpdir") == 0 || strcmp(fn_name, "__tmpdir") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_tmpdir();", result);
                    break;
                }

                // uptime()
                if ((strcmp(fn_name, "uptime") == 0 || strcmp(fn_name, "__uptime") == 0) && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_uptime();", result);
                    break;
                }

                // ========== COMPRESSION BUILTINS ==========

                // zlib_compress(data, level)
                if ((strcmp(fn_name, "zlib_compress") == 0 || strcmp(fn_name, "__zlib_compress") == 0) && expr->as.call.num_args == 2) {
                    char *data = codegen_expr(ctx, expr->as.call.args[0]);
                    char *level = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_zlib_compress(%s, %s);", result, data, level);
                    codegen_writeln(ctx, "hml_release(&%s);", data);
                    codegen_writeln(ctx, "hml_release(&%s);", level);
                    free(data);
                    free(level);
                    break;
                }

                // zlib_decompress(data, max_size)
                if ((strcmp(fn_name, "zlib_decompress") == 0 || strcmp(fn_name, "__zlib_decompress") == 0) && expr->as.call.num_args == 2) {
                    char *data = codegen_expr(ctx, expr->as.call.args[0]);
                    char *max_size = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_zlib_decompress(%s, %s);", result, data, max_size);
                    codegen_writeln(ctx, "hml_release(&%s);", data);
                    codegen_writeln(ctx, "hml_release(&%s);", max_size);
                    free(data);
                    free(max_size);
                    break;
                }

                // gzip_compress(data, level)
                if ((strcmp(fn_name, "gzip_compress") == 0 || strcmp(fn_name, "__gzip_compress") == 0) && expr->as.call.num_args == 2) {
                    char *data = codegen_expr(ctx, expr->as.call.args[0]);
                    char *level = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_gzip_compress(%s, %s);", result, data, level);
                    codegen_writeln(ctx, "hml_release(&%s);", data);
                    codegen_writeln(ctx, "hml_release(&%s);", level);
                    free(data);
                    free(level);
                    break;
                }

                // gzip_decompress(data, max_size)
                if ((strcmp(fn_name, "gzip_decompress") == 0 || strcmp(fn_name, "__gzip_decompress") == 0) && expr->as.call.num_args == 2) {
                    char *data = codegen_expr(ctx, expr->as.call.args[0]);
                    char *max_size = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_gzip_decompress(%s, %s);", result, data, max_size);
                    codegen_writeln(ctx, "hml_release(&%s);", data);
                    codegen_writeln(ctx, "hml_release(&%s);", max_size);
                    free(data);
                    free(max_size);
                    break;
                }

                // zlib_compress_bound(source_len)
                if ((strcmp(fn_name, "zlib_compress_bound") == 0 || strcmp(fn_name, "__zlib_compress_bound") == 0) && expr->as.call.num_args == 1) {
                    char *len = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_zlib_compress_bound(%s);", result, len);
                    codegen_writeln(ctx, "hml_release(&%s);", len);
                    free(len);
                    break;
                }

                // crc32(data)
                if ((strcmp(fn_name, "crc32") == 0 || strcmp(fn_name, "__crc32") == 0) && expr->as.call.num_args == 1) {
                    char *data = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_crc32_val(%s);", result, data);
                    codegen_writeln(ctx, "hml_release(&%s);", data);
                    free(data);
                    break;
                }

                // adler32(data)
                if ((strcmp(fn_name, "adler32") == 0 || strcmp(fn_name, "__adler32") == 0) && expr->as.call.num_args == 1) {
                    char *data = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_adler32_val(%s);", result, data);
                    codegen_writeln(ctx, "hml_release(&%s);", data);
                    free(data);
                    break;
                }

                // ========== STRING UTILITY BUILTINS ==========

                // to_string(value)
                if (strcmp(fn_name, "to_string") == 0 && expr->as.call.num_args == 1) {
                    char *val = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_to_string(%s);", result, val);
                    codegen_writeln(ctx, "hml_release(&%s);", val);
                    free(val);
                    break;
                }

                // string_byte_length(str)
                if (strcmp(fn_name, "string_byte_length") == 0 && expr->as.call.num_args == 1) {
                    char *str = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_string_byte_length(%s);", result, str);
                    codegen_writeln(ctx, "hml_release(&%s);", str);
                    free(str);
                    break;
                }

                // strerror()
                if (strcmp(fn_name, "strerror") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_strerror();", result);
                    break;
                }

                // string_to_cstr(str)
                if (strcmp(fn_name, "string_to_cstr") == 0 && expr->as.call.num_args == 1) {
                    char *str = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_string_to_cstr(%s);", result, str);
                    codegen_writeln(ctx, "hml_release(&%s);", str);
                    free(str);
                    break;
                }

                // cstr_to_string(ptr)
                if (strcmp(fn_name, "cstr_to_string") == 0 && expr->as.call.num_args == 1) {
                    char *ptr = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_cstr_to_string(%s);", result, ptr);
                    codegen_writeln(ctx, "hml_release(&%s);", ptr);
                    free(ptr);
                    break;
                }

                // string_concat_many(array)
                if (strcmp(fn_name, "string_concat_many") == 0 && expr->as.call.num_args == 1) {
                    char *arr = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_string_concat_many(%s);", result, arr);
                    codegen_writeln(ctx, "hml_release(&%s);", arr);
                    free(arr);
                    break;
                }

                // select(channels, timeout_ms?) - wait on multiple channels
                if (strcmp(fn_name, "select") == 0 && (expr->as.call.num_args == 1 || expr->as.call.num_args == 2)) {
                    char *channels = codegen_expr(ctx, expr->as.call.args[0]);
                    if (expr->as.call.num_args == 2) {
                        char *timeout = codegen_expr(ctx, expr->as.call.args[1]);
                        codegen_writeln(ctx, "HmlValue %s = hml_select(%s, %s);", result, channels, timeout);
                        codegen_writeln(ctx, "hml_release(&%s);", timeout);
                        free(timeout);
                    } else {
                        codegen_writeln(ctx, "HmlValue %s = hml_select(%s, hml_val_null());", result, channels);
                    }
                    codegen_writeln(ctx, "hml_release(&%s);", channels);
                    free(channels);
                    break;
                }

                // poll(fds, timeout_ms) - wait for I/O events
                if (strcmp(fn_name, "poll") == 0 && expr->as.call.num_args == 2) {
                    char *fds = codegen_expr(ctx, expr->as.call.args[0]);
                    char *timeout = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_poll(%s, %s);", result, fds, timeout);
                    codegen_writeln(ctx, "hml_release(&%s);", fds);
                    codegen_writeln(ctx, "hml_release(&%s);", timeout);
                    free(fds);
                    free(timeout);
                    break;
                }

                // ========== INTERNAL HELPER BUILTINS ==========

                // read_u32(buffer) - read 32-bit unsigned int from buffer
                if ((strcmp(fn_name, "read_u32") == 0 || strcmp(fn_name, "__read_u32") == 0) && expr->as.call.num_args == 1) {
                    char *buf = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_read_u32(%s);", result, buf);
                    codegen_writeln(ctx, "hml_release(&%s);", buf);
                    free(buf);
                    break;
                }

                // read_u64(buffer) - read 64-bit unsigned int from buffer
                if ((strcmp(fn_name, "read_u64") == 0 || strcmp(fn_name, "__read_u64") == 0) && expr->as.call.num_args == 1) {
                    char *buf = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_read_u64(%s);", result, buf);
                    codegen_writeln(ctx, "hml_release(&%s);", buf);
                    free(buf);
                    break;
                }

                // ========== HTTP/WEBSOCKET BUILTINS ==========

                // __lws_http_get(url)
                if (strcmp(fn_name, "__lws_http_get") == 0 && expr->as.call.num_args == 1) {
                    char *url = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_http_get(%s);", result, url);
                    codegen_writeln(ctx, "hml_release(&%s);", url);
                    free(url);
                    break;
                }

                // __lws_http_post(url, body, content_type)
                if (strcmp(fn_name, "__lws_http_post") == 0 && expr->as.call.num_args == 3) {
                    char *url = codegen_expr(ctx, expr->as.call.args[0]);
                    char *body = codegen_expr(ctx, expr->as.call.args[1]);
                    char *content_type = codegen_expr(ctx, expr->as.call.args[2]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_http_post(%s, %s, %s);", result, url, body, content_type);
                    codegen_writeln(ctx, "hml_release(&%s);", url);
                    codegen_writeln(ctx, "hml_release(&%s);", body);
                    codegen_writeln(ctx, "hml_release(&%s);", content_type);
                    free(url);
                    free(body);
                    free(content_type);
                    break;
                }

                // __lws_response_status(resp)
                if (strcmp(fn_name, "__lws_response_status") == 0 && expr->as.call.num_args == 1) {
                    char *resp = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_response_status(%s);", result, resp);
                    codegen_writeln(ctx, "hml_release(&%s);", resp);
                    free(resp);
                    break;
                }

                // __lws_response_body(resp)
                if (strcmp(fn_name, "__lws_response_body") == 0 && expr->as.call.num_args == 1) {
                    char *resp = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_response_body(%s);", result, resp);
                    codegen_writeln(ctx, "hml_release(&%s);", resp);
                    free(resp);
                    break;
                }

                // __lws_response_headers(resp)
                if (strcmp(fn_name, "__lws_response_headers") == 0 && expr->as.call.num_args == 1) {
                    char *resp = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_response_headers(%s);", result, resp);
                    codegen_writeln(ctx, "hml_release(&%s);", resp);
                    free(resp);
                    break;
                }

                // __lws_response_free(resp)
                if (strcmp(fn_name, "__lws_response_free") == 0 && expr->as.call.num_args == 1) {
                    char *resp = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_response_free(%s);", result, resp);
                    codegen_writeln(ctx, "hml_release(&%s);", resp);
                    free(resp);
                    break;
                }

                // __lws_response_redirect(resp)
                if (strcmp(fn_name, "__lws_response_redirect") == 0 && expr->as.call.num_args == 1) {
                    char *resp = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_response_redirect(%s);", result, resp);
                    codegen_writeln(ctx, "hml_release(&%s);", resp);
                    free(resp);
                    break;
                }

                // __lws_response_body_binary(resp)
                if (strcmp(fn_name, "__lws_response_body_binary") == 0 && expr->as.call.num_args == 1) {
                    char *resp = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_response_body_binary(%s);", result, resp);
                    codegen_writeln(ctx, "hml_release(&%s);", resp);
                    free(resp);
                    break;
                }

                // ========== CRYPTOGRAPHIC HASH BUILTINS ==========

                // __sha256(input)
                if (strcmp(fn_name, "__sha256") == 0 && expr->as.call.num_args == 1) {
                    char *input = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_hash_sha256(%s);", result, input);
                    codegen_writeln(ctx, "hml_release(&%s);", input);
                    free(input);
                    break;
                }

                // __sha512(input)
                if (strcmp(fn_name, "__sha512") == 0 && expr->as.call.num_args == 1) {
                    char *input = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_hash_sha512(%s);", result, input);
                    codegen_writeln(ctx, "hml_release(&%s);", input);
                    free(input);
                    break;
                }

                // __md5(input)
                if (strcmp(fn_name, "__md5") == 0 && expr->as.call.num_args == 1) {
                    char *input = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_hash_md5(%s);", result, input);
                    codegen_writeln(ctx, "hml_release(&%s);", input);
                    free(input);
                    break;
                }

                // ========== WEBSOCKET BUILTINS ==========

                // __lws_ws_connect(url)
                if (strcmp(fn_name, "__lws_ws_connect") == 0 && expr->as.call.num_args == 1) {
                    char *url = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_ws_connect(%s);", result, url);
                    codegen_writeln(ctx, "hml_release(&%s);", url);
                    free(url);
                    break;
                }

                // __lws_ws_send_text(conn, text)
                if (strcmp(fn_name, "__lws_ws_send_text") == 0 && expr->as.call.num_args == 2) {
                    char *conn = codegen_expr(ctx, expr->as.call.args[0]);
                    char *text = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_ws_send_text(%s, %s);", result, conn, text);
                    codegen_writeln(ctx, "hml_release(&%s);", conn);
                    codegen_writeln(ctx, "hml_release(&%s);", text);
                    free(conn);
                    free(text);
                    break;
                }

                // __lws_ws_recv(conn, timeout_ms)
                if (strcmp(fn_name, "__lws_ws_recv") == 0 && expr->as.call.num_args == 2) {
                    char *conn = codegen_expr(ctx, expr->as.call.args[0]);
                    char *timeout = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_ws_recv(%s, %s);", result, conn, timeout);
                    codegen_writeln(ctx, "hml_release(&%s);", conn);
                    codegen_writeln(ctx, "hml_release(&%s);", timeout);
                    free(conn);
                    free(timeout);
                    break;
                }

                // __lws_ws_close(conn)
                if (strcmp(fn_name, "__lws_ws_close") == 0 && expr->as.call.num_args == 1) {
                    char *conn = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_ws_close(%s);", result, conn);
                    codegen_writeln(ctx, "hml_release(&%s);", conn);
                    free(conn);
                    break;
                }

                // __lws_ws_is_closed(conn)
                if (strcmp(fn_name, "__lws_ws_is_closed") == 0 && expr->as.call.num_args == 1) {
                    char *conn = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_ws_is_closed(%s);", result, conn);
                    codegen_writeln(ctx, "hml_release(&%s);", conn);
                    free(conn);
                    break;
                }

                // __lws_msg_type(msg)
                if (strcmp(fn_name, "__lws_msg_type") == 0 && expr->as.call.num_args == 1) {
                    char *msg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_msg_type(%s);", result, msg);
                    codegen_writeln(ctx, "hml_release(&%s);", msg);
                    free(msg);
                    break;
                }

                // __lws_msg_text(msg)
                if (strcmp(fn_name, "__lws_msg_text") == 0 && expr->as.call.num_args == 1) {
                    char *msg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_msg_text(%s);", result, msg);
                    codegen_writeln(ctx, "hml_release(&%s);", msg);
                    free(msg);
                    break;
                }

                // __lws_msg_len(msg)
                if (strcmp(fn_name, "__lws_msg_len") == 0 && expr->as.call.num_args == 1) {
                    char *msg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_msg_len(%s);", result, msg);
                    codegen_writeln(ctx, "hml_release(&%s);", msg);
                    free(msg);
                    break;
                }

                // __lws_msg_free(msg)
                if (strcmp(fn_name, "__lws_msg_free") == 0 && expr->as.call.num_args == 1) {
                    char *msg = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_msg_free(%s);", result, msg);
                    codegen_writeln(ctx, "hml_release(&%s);", msg);
                    free(msg);
                    break;
                }

                // __lws_ws_server_create(host, port)
                if (strcmp(fn_name, "__lws_ws_server_create") == 0 && expr->as.call.num_args == 2) {
                    char *host = codegen_expr(ctx, expr->as.call.args[0]);
                    char *port = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_ws_server_create(%s, %s);", result, host, port);
                    codegen_writeln(ctx, "hml_release(&%s);", host);
                    codegen_writeln(ctx, "hml_release(&%s);", port);
                    free(host);
                    free(port);
                    break;
                }

                // __lws_ws_server_accept(server, timeout_ms)
                if (strcmp(fn_name, "__lws_ws_server_accept") == 0 && expr->as.call.num_args == 2) {
                    char *server = codegen_expr(ctx, expr->as.call.args[0]);
                    char *timeout = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_ws_server_accept(%s, %s);", result, server, timeout);
                    codegen_writeln(ctx, "hml_release(&%s);", server);
                    codegen_writeln(ctx, "hml_release(&%s);", timeout);
                    free(server);
                    free(timeout);
                    break;
                }

                // __lws_ws_server_close(server)
                if (strcmp(fn_name, "__lws_ws_server_close") == 0 && expr->as.call.num_args == 1) {
                    char *server = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_lws_ws_server_close(%s);", result, server);
                    codegen_writeln(ctx, "hml_release(&%s);", server);
                    free(server);
                    break;
                }

                // ========== FILESYSTEM BUILTINS ==========

                // exists(path)
                if (strcmp(fn_name, "exists") == 0 && expr->as.call.num_args == 1) {
                    char *path = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_exists(%s);", result, path);
                    codegen_writeln(ctx, "hml_release(&%s);", path);
                    free(path);
                    break;
                }

                // read_file(path)
                if (strcmp(fn_name, "read_file") == 0 && expr->as.call.num_args == 1) {
                    char *path = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_read_file(%s);", result, path);
                    codegen_writeln(ctx, "hml_release(&%s);", path);
                    free(path);
                    break;
                }

                // write_file(path, content)
                if (strcmp(fn_name, "write_file") == 0 && expr->as.call.num_args == 2) {
                    char *path = codegen_expr(ctx, expr->as.call.args[0]);
                    char *content = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_write_file(%s, %s);", result, path, content);
                    codegen_writeln(ctx, "hml_release(&%s);", path);
                    codegen_writeln(ctx, "hml_release(&%s);", content);
                    free(path);
                    free(content);
                    break;
                }

                // append_file(path, content)
                if (strcmp(fn_name, "append_file") == 0 && expr->as.call.num_args == 2) {
                    char *path = codegen_expr(ctx, expr->as.call.args[0]);
                    char *content = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_append_file(%s, %s);", result, path, content);
                    codegen_writeln(ctx, "hml_release(&%s);", path);
                    codegen_writeln(ctx, "hml_release(&%s);", content);
                    free(path);
                    free(content);
                    break;
                }

                // remove_file(path)
                if (strcmp(fn_name, "remove_file") == 0 && expr->as.call.num_args == 1) {
                    char *path = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_remove_file(%s);", result, path);
                    codegen_writeln(ctx, "hml_release(&%s);", path);
                    free(path);
                    break;
                }

                // rename(old_path, new_path)
                if (strcmp(fn_name, "rename") == 0 && expr->as.call.num_args == 2) {
                    char *old_path = codegen_expr(ctx, expr->as.call.args[0]);
                    char *new_path = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_rename_file(%s, %s);", result, old_path, new_path);
                    codegen_writeln(ctx, "hml_release(&%s);", old_path);
                    codegen_writeln(ctx, "hml_release(&%s);", new_path);
                    free(old_path);
                    free(new_path);
                    break;
                }

                // copy_file(src, dest)
                if (strcmp(fn_name, "copy_file") == 0 && expr->as.call.num_args == 2) {
                    char *src = codegen_expr(ctx, expr->as.call.args[0]);
                    char *dest = codegen_expr(ctx, expr->as.call.args[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_copy_file(%s, %s);", result, src, dest);
                    codegen_writeln(ctx, "hml_release(&%s);", src);
                    codegen_writeln(ctx, "hml_release(&%s);", dest);
                    free(src);
                    free(dest);
                    break;
                }

                // is_file(path)
                if (strcmp(fn_name, "is_file") == 0 && expr->as.call.num_args == 1) {
                    char *path = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_is_file(%s);", result, path);
                    codegen_writeln(ctx, "hml_release(&%s);", path);
                    free(path);
                    break;
                }

                // is_dir(path)
                if (strcmp(fn_name, "is_dir") == 0 && expr->as.call.num_args == 1) {
                    char *path = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_is_dir(%s);", result, path);
                    codegen_writeln(ctx, "hml_release(&%s);", path);
                    free(path);
                    break;
                }

                // file_stat(path)
                if (strcmp(fn_name, "file_stat") == 0 && expr->as.call.num_args == 1) {
                    char *path = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_file_stat(%s);", result, path);
                    codegen_writeln(ctx, "hml_release(&%s);", path);
                    free(path);
                    break;
                }

                // make_dir(path, [mode])
                if (strcmp(fn_name, "make_dir") == 0 && (expr->as.call.num_args == 1 || expr->as.call.num_args == 2)) {
                    char *path = codegen_expr(ctx, expr->as.call.args[0]);
                    if (expr->as.call.num_args == 2) {
                        char *mode = codegen_expr(ctx, expr->as.call.args[1]);
                        codegen_writeln(ctx, "HmlValue %s = hml_make_dir(%s, %s);", result, path, mode);
                        codegen_writeln(ctx, "hml_release(&%s);", mode);
                        free(mode);
                    } else {
                        codegen_writeln(ctx, "HmlValue %s = hml_make_dir(%s, hml_val_u32(0755));", result, path);
                    }
                    codegen_writeln(ctx, "hml_release(&%s);", path);
                    free(path);
                    break;
                }

                // remove_dir(path)
                if (strcmp(fn_name, "remove_dir") == 0 && expr->as.call.num_args == 1) {
                    char *path = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_remove_dir(%s);", result, path);
                    codegen_writeln(ctx, "hml_release(&%s);", path);
                    free(path);
                    break;
                }

                // list_dir(path)
                if (strcmp(fn_name, "list_dir") == 0 && expr->as.call.num_args == 1) {
                    char *path = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_list_dir(%s);", result, path);
                    codegen_writeln(ctx, "hml_release(&%s);", path);
                    free(path);
                    break;
                }

                // cwd()
                if (strcmp(fn_name, "cwd") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_cwd();", result);
                    break;
                }

                // chdir(path)
                if (strcmp(fn_name, "chdir") == 0 && expr->as.call.num_args == 1) {
                    char *path = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_chdir(%s);", result, path);
                    codegen_writeln(ctx, "hml_release(&%s);", path);
                    free(path);
                    break;
                }

                // absolute_path(path)
                if (strcmp(fn_name, "absolute_path") == 0 && expr->as.call.num_args == 1) {
                    char *path = codegen_expr(ctx, expr->as.call.args[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_absolute_path(%s);", result, path);
                    codegen_writeln(ctx, "hml_release(&%s);", path);
                    free(path);
                    break;
                }

                // Handle user-defined function by name (hml_fn_<name>)
                // Main file functions should use generic call path (hml_call_function)
                // to properly handle optional parameters with defaults

                // Check if this is an imported function first (even if registered as local)
                ImportBinding *import_binding = NULL;
                if (ctx->current_module) {
                    import_binding = module_find_import(ctx->current_module, fn_name);
                } else {
                    import_binding = codegen_find_main_import(ctx, fn_name);
                }

                if (codegen_is_main_func(ctx, fn_name) && !import_binding && !ctx->current_module) {
                    // OPTIMIZATION: Main file function definition - call directly
                    // This is safe because we know the function signature at compile time
                    int expected_params = codegen_get_main_func_params(ctx, fn_name);
                    char **arg_temps = malloc(expr->as.call.num_args * sizeof(char*));
                    for (int i = 0; i < expr->as.call.num_args; i++) {
                        arg_temps[i] = codegen_expr(ctx, expr->as.call.args[i]);
                    }

                    codegen_write(ctx, "");
                    codegen_indent(ctx);
                    fprintf(ctx->output, "HmlValue %s = hml_fn_%s(NULL", result, fn_name);
                    for (int i = 0; i < expr->as.call.num_args; i++) {
                        fprintf(ctx->output, ", %s", arg_temps[i]);
                    }
                    // Fill in hml_val_null() for missing optional parameters
                    for (int i = expr->as.call.num_args; i < expected_params; i++) {
                        fprintf(ctx->output, ", hml_val_null()");
                    }
                    fprintf(ctx->output, ");\n");

                    for (int i = 0; i < expr->as.call.num_args; i++) {
                        codegen_writeln(ctx, "hml_release(&%s);", arg_temps[i]);
                        free(arg_temps[i]);
                    }
                    free(arg_temps);
                    break;
                } else if (codegen_is_main_var(ctx, fn_name) && !import_binding) {
                    // Main file variable that's NOT a function definition (e.g., assigned closure)
                    // Use generic call path to properly handle closures
                    // Fall through to generic handling
                } else if (codegen_is_local(ctx, fn_name) && !import_binding) {
                    // Check if this local is actually a captured main file function - use direct call
                    if (codegen_is_main_func(ctx, fn_name) && !ctx->current_module) {
                        int expected_params = codegen_get_main_func_params(ctx, fn_name);
                        char **arg_temps = malloc(expr->as.call.num_args * sizeof(char*));
                        for (int i = 0; i < expr->as.call.num_args; i++) {
                            arg_temps[i] = codegen_expr(ctx, expr->as.call.args[i]);
                        }

                        codegen_write(ctx, "");
                        codegen_indent(ctx);
                        fprintf(ctx->output, "HmlValue %s = hml_fn_%s(NULL", result, fn_name);
                        for (int i = 0; i < expr->as.call.num_args; i++) {
                            fprintf(ctx->output, ", %s", arg_temps[i]);
                        }
                        // Fill in hml_val_null() for missing optional parameters
                        for (int i = expr->as.call.num_args; i < expected_params; i++) {
                            fprintf(ctx->output, ", hml_val_null()");
                        }
                        fprintf(ctx->output, ");\n");

                        for (int i = 0; i < expr->as.call.num_args; i++) {
                            codegen_writeln(ctx, "hml_release(&%s);", arg_temps[i]);
                            free(arg_temps[i]);
                        }
                        free(arg_temps);
                        break;
                    }
                    // It's a true local function variable (not a main func) - call through hml_call_function
                    // Fall through to generic handling
                } else if (import_binding && !import_binding->is_function) {
                    // Imported variable that holds a function value (e.g., export let sleep = __sleep)
                    // Fall through to generic call path - will use hml_call_function on the variable
                } else {
                    // Direct call path for imported functions and module functions
                    // import_binding was already set above

                    // Determine the expected number of parameters for the function
                    int expected_params = expr->as.call.num_args;  // Default to provided args
                    if (import_binding && import_binding->is_function && import_binding->num_params > 0) {
                        expected_params = import_binding->num_params;
                    } else if (ctx->current_module && !import_binding) {
                        // Look up export in current module for self-calls
                        ExportedSymbol *exp = module_find_export(ctx->current_module, fn_name);
                        if (exp && exp->is_function && exp->num_params > 0) {
                            expected_params = exp->num_params;
                        }
                    }

                    // Try to call as hml_fn_<name> directly with NULL for closure env
                    char **arg_temps = malloc(expr->as.call.num_args * sizeof(char*));
                    for (int i = 0; i < expr->as.call.num_args; i++) {
                        arg_temps[i] = codegen_expr(ctx, expr->as.call.args[i]);
                    }

                    codegen_write(ctx, "");
                    codegen_indent(ctx);
                    // All functions use closure env as first param for uniform calling convention
                    if (import_binding) {
                        // Use the mangled function name from the import
                        // module_prefix is like "_mod1_", original_name is the export name
                        fprintf(ctx->output, "HmlValue %s = %sfn_%s(NULL", result,
                                import_binding->module_prefix, import_binding->original_name);
                    } else if (ctx->current_module) {
                        // Check if this is an extern function - externs don't get module prefix
                        if (module_is_extern_fn(ctx->current_module, fn_name)) {
                            fprintf(ctx->output, "HmlValue %s = hml_fn_%s(NULL", result, fn_name);
                        } else {
                            // Function in current module - use module prefix
                            fprintf(ctx->output, "HmlValue %s = %sfn_%s(NULL", result,
                                    ctx->current_module->module_prefix, fn_name);
                        }
                    } else {
                        // Main file function definition - call directly
                        fprintf(ctx->output, "HmlValue %s = hml_fn_%s(NULL", result, fn_name);
                    }
                    // Output provided arguments
                    for (int i = 0; i < expr->as.call.num_args; i++) {
                        fprintf(ctx->output, ", %s", arg_temps[i]);
                    }
                    // Fill in hml_val_null() for missing optional parameters
                    for (int i = expr->as.call.num_args; i < expected_params; i++) {
                        fprintf(ctx->output, ", hml_val_null()");
                    }
                    fprintf(ctx->output, ");\n");

                    for (int i = 0; i < expr->as.call.num_args; i++) {
                        codegen_writeln(ctx, "hml_release(&%s);", arg_temps[i]);
                        free(arg_temps[i]);
                    }
                    free(arg_temps);
                    break;
                }
            }

            // Handle method calls: obj.method(args)
            if (expr->as.call.func->type == EXPR_GET_PROPERTY) {
                Expr *obj_expr = expr->as.call.func->as.get_property.object;
                const char *method = expr->as.call.func->as.get_property.property;

                // Evaluate the object
                char *obj_val = codegen_expr(ctx, obj_expr);

                // Evaluate arguments
                char **arg_temps = malloc(expr->as.call.num_args * sizeof(char*));
                for (int i = 0; i < expr->as.call.num_args; i++) {
                    arg_temps[i] = codegen_expr(ctx, expr->as.call.args[i]);
                }

                // Methods that work on both strings and arrays - need runtime type check
                if (strcmp(method, "slice") == 0 && expr->as.call.num_args == 2) {
                    codegen_writeln(ctx, "HmlValue %s;", result);
                    codegen_writeln(ctx, "if (%s.type == HML_VAL_STRING) {", obj_val);
                    codegen_writeln(ctx, "    %s = hml_string_slice(%s, %s, %s);",
                                  result, obj_val, arg_temps[0], arg_temps[1]);
                    codegen_writeln(ctx, "} else {");
                    codegen_writeln(ctx, "    %s = hml_array_slice(%s, %s, %s);",
                                  result, obj_val, arg_temps[0], arg_temps[1]);
                    codegen_writeln(ctx, "}");
                } else if ((strcmp(method, "find") == 0 || strcmp(method, "indexOf") == 0)
                           && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s;", result);
                    codegen_writeln(ctx, "if (%s.type == HML_VAL_STRING) {", obj_val);
                    codegen_writeln(ctx, "    %s = hml_string_find(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                    codegen_writeln(ctx, "} else {");
                    codegen_writeln(ctx, "    %s = hml_array_find(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                    codegen_writeln(ctx, "}");
                } else if (strcmp(method, "contains") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s;", result);
                    codegen_writeln(ctx, "if (%s.type == HML_VAL_STRING) {", obj_val);
                    codegen_writeln(ctx, "    %s = hml_string_contains(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                    codegen_writeln(ctx, "} else {");
                    codegen_writeln(ctx, "    %s = hml_array_contains(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                    codegen_writeln(ctx, "}");
                // String-only methods
                } else if (strcmp(method, "substr") == 0 && expr->as.call.num_args == 2) {
                    codegen_writeln(ctx, "HmlValue %s = hml_string_substr(%s, %s, %s);",
                                  result, obj_val, arg_temps[0], arg_temps[1]);
                } else if (strcmp(method, "split") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_string_split(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if (strcmp(method, "trim") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_string_trim(%s);", result, obj_val);
                } else if (strcmp(method, "to_upper") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_string_to_upper(%s);", result, obj_val);
                } else if (strcmp(method, "to_lower") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_string_to_lower(%s);", result, obj_val);
                } else if (strcmp(method, "starts_with") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_string_starts_with(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if (strcmp(method, "ends_with") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_string_ends_with(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if (strcmp(method, "replace") == 0 && expr->as.call.num_args == 2) {
                    codegen_writeln(ctx, "HmlValue %s = hml_string_replace(%s, %s, %s);",
                                  result, obj_val, arg_temps[0], arg_temps[1]);
                } else if (strcmp(method, "replace_all") == 0 && expr->as.call.num_args == 2) {
                    codegen_writeln(ctx, "HmlValue %s = hml_string_replace_all(%s, %s, %s);",
                                  result, obj_val, arg_temps[0], arg_temps[1]);
                } else if (strcmp(method, "repeat") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_string_repeat(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if (strcmp(method, "char_at") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_string_char_at(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if (strcmp(method, "byte_at") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_string_byte_at(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if (strcmp(method, "to_bytes") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_string_to_bytes(%s);",
                                  result, obj_val);
                // Array methods - with runtime type check to also support object methods
                } else if (strcmp(method, "push") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s;", result);
                    codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY) {", obj_val);
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "hml_array_push(%s, %s);", obj_val, arg_temps[0]);
                    codegen_writeln(ctx, "%s = hml_val_null();", result);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "} else {");
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "HmlValue _push_args[1] = {%s};", arg_temps[0]);
                    codegen_writeln(ctx, "%s = hml_call_method(%s, \"push\", _push_args, 1);", result, obj_val);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "}");
                } else if (strcmp(method, "pop") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s;", result);
                    codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY) {", obj_val);
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "%s = hml_array_pop(%s);", result, obj_val);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "} else {");
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "%s = hml_call_method(%s, \"pop\", NULL, 0);", result, obj_val);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "}");
                } else if (strcmp(method, "shift") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s;", result);
                    codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY) {", obj_val);
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "%s = hml_array_shift(%s);", result, obj_val);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "} else {");
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "%s = hml_call_method(%s, \"shift\", NULL, 0);", result, obj_val);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "}");
                } else if (strcmp(method, "unshift") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s;", result);
                    codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY) {", obj_val);
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "hml_array_unshift(%s, %s);", obj_val, arg_temps[0]);
                    codegen_writeln(ctx, "%s = hml_val_null();", result);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "} else {");
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "HmlValue _unshift_args[1] = {%s};", arg_temps[0]);
                    codegen_writeln(ctx, "%s = hml_call_method(%s, \"unshift\", _unshift_args, 1);", result, obj_val);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "}");
                } else if (strcmp(method, "insert") == 0 && expr->as.call.num_args == 2) {
                    codegen_writeln(ctx, "HmlValue %s;", result);
                    codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY) {", obj_val);
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "hml_array_insert(%s, %s, %s);",
                                  obj_val, arg_temps[0], arg_temps[1]);
                    codegen_writeln(ctx, "%s = hml_val_null();", result);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "} else {");
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "HmlValue _insert_args[2] = {%s, %s};", arg_temps[0], arg_temps[1]);
                    codegen_writeln(ctx, "%s = hml_call_method(%s, \"insert\", _insert_args, 2);", result, obj_val);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "}");
                } else if (strcmp(method, "remove") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s;", result);
                    codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY) {", obj_val);
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "%s = hml_array_remove(%s, %s);", result, obj_val, arg_temps[0]);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "} else {");
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "HmlValue _remove_args[1] = {%s};", arg_temps[0]);
                    codegen_writeln(ctx, "%s = hml_call_method(%s, \"remove\", _remove_args, 1);", result, obj_val);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "}");
                // Note: find, contains, slice are handled above with runtime type checks
                } else if (strcmp(method, "join") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_array_join(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if (strcmp(method, "concat") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_array_concat(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if (strcmp(method, "reverse") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "hml_array_reverse(%s);", obj_val);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                } else if (strcmp(method, "first") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_array_first(%s);", result, obj_val);
                } else if (strcmp(method, "last") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_array_last(%s);", result, obj_val);
                } else if (strcmp(method, "clear") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "hml_array_clear(%s);", obj_val);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                // File methods
                } else if (strcmp(method, "read") == 0 && (expr->as.call.num_args == 0 || expr->as.call.num_args == 1)) {
                    if (expr->as.call.num_args == 1) {
                        codegen_writeln(ctx, "HmlValue %s = hml_file_read(%s, %s);",
                                      result, obj_val, arg_temps[0]);
                    } else {
                        codegen_writeln(ctx, "HmlValue %s = hml_file_read_all(%s);", result, obj_val);
                    }
                } else if (strcmp(method, "write") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_file_write(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if (strcmp(method, "seek") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_file_seek(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if (strcmp(method, "tell") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_file_tell(%s);", result, obj_val);
                } else if (strcmp(method, "close") == 0 && expr->as.call.num_args == 0) {
                    // Handle file.close(), channel.close(), and socket.close()
                    codegen_writeln(ctx, "if (%s.type == HML_VAL_FILE) {", obj_val);
                    codegen_writeln(ctx, "    hml_file_close(%s);", obj_val);
                    codegen_writeln(ctx, "} else if (%s.type == HML_VAL_CHANNEL) {", obj_val);
                    codegen_writeln(ctx, "    hml_channel_close(%s);", obj_val);
                    codegen_writeln(ctx, "} else if (%s.type == HML_VAL_SOCKET) {", obj_val);
                    codegen_writeln(ctx, "    hml_socket_close(%s);", obj_val);
                    codegen_writeln(ctx, "}");
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                } else if (strcmp(method, "map") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_array_map(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if (strcmp(method, "filter") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_array_filter(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if (strcmp(method, "reduce") == 0 && (expr->as.call.num_args == 1 || expr->as.call.num_args == 2)) {
                    if (expr->as.call.num_args == 2) {
                        codegen_writeln(ctx, "HmlValue %s = hml_array_reduce(%s, %s, %s);",
                                      result, obj_val, arg_temps[0], arg_temps[1]);
                    } else {
                        // No initial value - use first element
                        codegen_writeln(ctx, "HmlValue %s = hml_array_reduce(%s, %s, hml_val_null());",
                                      result, obj_val, arg_temps[0]);
                    }
                // Channel methods (also handle socket variants)
                } else if (strcmp(method, "send") == 0 && expr->as.call.num_args == 1) {
                    // Channel send or socket send
                    codegen_writeln(ctx, "if (%s.type == HML_VAL_CHANNEL) {", obj_val);
                    codegen_writeln(ctx, "    hml_channel_send(%s, %s);", obj_val, arg_temps[0]);
                    codegen_writeln(ctx, "}");
                    codegen_writeln(ctx, "HmlValue %s;", result);
                    codegen_writeln(ctx, "if (%s.type == HML_VAL_SOCKET) {", obj_val);
                    codegen_writeln(ctx, "    %s = hml_socket_send(%s, %s);", result, obj_val, arg_temps[0]);
                    codegen_writeln(ctx, "} else {");
                    codegen_writeln(ctx, "    %s = hml_val_null();", result);
                    codegen_writeln(ctx, "}");
                } else if (strcmp(method, "recv") == 0) {
                    // Channel recv (no args) or socket recv (1 arg for size)
                    codegen_writeln(ctx, "HmlValue %s;", result);
                    if (expr->as.call.num_args == 0) {
                        codegen_writeln(ctx, "%s = hml_channel_recv(%s);", result, obj_val);
                    } else {
                        codegen_writeln(ctx, "%s = hml_socket_recv(%s, %s);", result, obj_val, arg_temps[0]);
                    }
                // Socket-specific methods
                } else if (strcmp(method, "bind") == 0 && expr->as.call.num_args == 2) {
                    codegen_writeln(ctx, "hml_socket_bind(%s, %s, %s);", obj_val, arg_temps[0], arg_temps[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                } else if (strcmp(method, "listen") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "hml_socket_listen(%s, %s);", obj_val, arg_temps[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                } else if (strcmp(method, "accept") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_socket_accept(%s);", result, obj_val);
                } else if (strcmp(method, "connect") == 0 && expr->as.call.num_args == 2) {
                    codegen_writeln(ctx, "hml_socket_connect(%s, %s, %s);", obj_val, arg_temps[0], arg_temps[1]);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                } else if (strcmp(method, "sendto") == 0 && expr->as.call.num_args == 3) {
                    codegen_writeln(ctx, "HmlValue %s = hml_socket_sendto(%s, %s, %s, %s);",
                                  result, obj_val, arg_temps[0], arg_temps[1], arg_temps[2]);
                } else if (strcmp(method, "recvfrom") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_socket_recvfrom(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if (strcmp(method, "setsockopt") == 0 && expr->as.call.num_args == 3) {
                    codegen_writeln(ctx, "hml_socket_setsockopt(%s, %s, %s, %s);",
                                  obj_val, arg_temps[0], arg_temps[1], arg_temps[2]);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                } else if (strcmp(method, "set_timeout") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "hml_socket_set_timeout(%s, %s);",
                                  obj_val, arg_temps[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                } else if (strcmp(method, "set_nonblocking") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "hml_socket_set_nonblocking(%s, %s);",
                                  obj_val, arg_temps[0]);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                } else if (strcmp(method, "recv_timeout") == 0 && expr->as.call.num_args == 1) {
                    codegen_writeln(ctx, "HmlValue %s = hml_channel_recv_timeout(%s, %s);",
                                  result, obj_val, arg_temps[0]);
                } else if (strcmp(method, "send_timeout") == 0 && expr->as.call.num_args == 2) {
                    codegen_writeln(ctx, "HmlValue %s = hml_channel_send_timeout(%s, %s, %s);",
                                  result, obj_val, arg_temps[0], arg_temps[1]);
                // Serialization methods
                } else if (strcmp(method, "serialize") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_serialize(%s);", result, obj_val);
                } else if (strcmp(method, "deserialize") == 0 && expr->as.call.num_args == 0) {
                    codegen_writeln(ctx, "HmlValue %s = hml_deserialize(%s);", result, obj_val);
                } else {
                    // Unknown built-in method - try as object method call
                    if (expr->as.call.num_args > 0) {
                        codegen_writeln(ctx, "HmlValue _method_args%d[%d];", ctx->temp_counter, expr->as.call.num_args);
                        for (int i = 0; i < expr->as.call.num_args; i++) {
                            codegen_writeln(ctx, "_method_args%d[%d] = %s;", ctx->temp_counter, i, arg_temps[i]);
                        }
                        codegen_writeln(ctx, "HmlValue %s = hml_call_method(%s, \"%s\", _method_args%d, %d);",
                                      result, obj_val, method, ctx->temp_counter, expr->as.call.num_args);
                        ctx->temp_counter++;
                    } else {
                        codegen_writeln(ctx, "HmlValue %s = hml_call_method(%s, \"%s\", NULL, 0);",
                                      result, obj_val, method);
                    }
                }

                // Release temporaries
                codegen_writeln(ctx, "hml_release(&%s);", obj_val);
                for (int i = 0; i < expr->as.call.num_args; i++) {
                    codegen_writeln(ctx, "hml_release(&%s);", arg_temps[i]);
                    free(arg_temps[i]);
                }
                free(arg_temps);
                free(obj_val);
                break;
            }

            // Generic function call handling
            char *func_val = codegen_expr(ctx, expr->as.call.func);

            // Reserve a counter for the args array BEFORE generating arg expressions
            // (which may increment temp_counter internally)
            int args_counter = ctx->temp_counter++;

            // Generate code for arguments
            char **arg_temps = malloc(expr->as.call.num_args * sizeof(char*));
            for (int i = 0; i < expr->as.call.num_args; i++) {
                arg_temps[i] = codegen_expr(ctx, expr->as.call.args[i]);
            }

            // Build args array
            if (expr->as.call.num_args > 0) {
                codegen_writeln(ctx, "HmlValue _args%d[%d];", args_counter, expr->as.call.num_args);
                for (int i = 0; i < expr->as.call.num_args; i++) {
                    codegen_writeln(ctx, "_args%d[%d] = %s;", args_counter, i, arg_temps[i]);
                }
                codegen_writeln(ctx, "HmlValue %s = hml_call_function(%s, _args%d, %d);",
                              result, func_val, args_counter, expr->as.call.num_args);
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_call_function(%s, NULL, 0);", result, func_val);
            }

            // Release temporaries
            codegen_writeln(ctx, "hml_release(&%s);", func_val);
            for (int i = 0; i < expr->as.call.num_args; i++) {
                codegen_writeln(ctx, "hml_release(&%s);", arg_temps[i]);
                free(arg_temps[i]);
            }
            free(arg_temps);
            free(func_val);
            break;
        }

        case EXPR_ASSIGN: {
            // Check for const reassignment at compile time
            if (codegen_is_const(ctx, expr->as.assign.name)) {
                codegen_writeln(ctx, "hml_runtime_error(\"Cannot assign to const variable '%s'\");",
                               expr->as.assign.name);
                codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
                break;
            }

            // OPTIMIZATION: Detect pattern "x = x + y" for in-place string append
            // This turns O(n²) repeated string concatenation into O(n) amortized
            Expr *val_expr = expr->as.assign.value;
            if (val_expr->type == EXPR_BINARY && val_expr->as.binary.op == OP_ADD) {
                Expr *left = val_expr->as.binary.left;
                Expr *right = val_expr->as.binary.right;

                // Check if left operand is the same variable being assigned
                if (left->type == EXPR_IDENT && strcmp(left->as.ident, expr->as.assign.name) == 0) {
                    // Check if right operand is definitely a string (literal only)
                    // We can't assume EXPR_IDENT is a string since it could be a number
                    // String indexing returns a rune but the variable type is unknown at compile time
                    int definitely_string = (right->type == EXPR_STRING);

                    if (definitely_string) {
                        // Generate in-place append
                        char *rhs = codegen_expr(ctx, right);

                        // Determine the correct variable name with prefix
                        const char *var_name = expr->as.assign.name;
                        char prefixed_var[256];
                        if (ctx->current_module && !codegen_is_local(ctx, var_name)) {
                            snprintf(prefixed_var, sizeof(prefixed_var), "%s%s",
                                    ctx->current_module->module_prefix, var_name);
                            var_name = prefixed_var;
                        } else if (codegen_is_main_var(ctx, expr->as.assign.name)) {
                            snprintf(prefixed_var, sizeof(prefixed_var), "_main_%s", expr->as.assign.name);
                            var_name = prefixed_var;
                        }

                        // Use in-place append - this modifies dest directly if refcount==1
                        codegen_writeln(ctx, "hml_string_append_inplace(&%s, %s);", var_name, rhs);
                        codegen_writeln(ctx, "hml_release_if_needed(&%s);", rhs);
                        free(rhs);

                        // Result is the variable itself
                        codegen_writeln(ctx, "HmlValue %s = %s;", result, var_name);
                        codegen_writeln(ctx, "hml_retain(&%s);", result);
                        break;
                    }
                }
            }

            char *value = codegen_expr(ctx, expr->as.assign.value);
            // Determine the correct variable name with prefix
            const char *var_name = expr->as.assign.name;
            char prefixed_name[256];
            if (ctx->current_module && !codegen_is_local(ctx, var_name)) {
                // Module context - use module prefix
                snprintf(prefixed_name, sizeof(prefixed_name), "%s%s",
                        ctx->current_module->module_prefix, var_name);
                var_name = prefixed_name;
            } else if (codegen_is_shadow(ctx, var_name)) {
                // Shadow variable (like catch param) - use bare name
                // var_name stays as-is
            } else if (codegen_is_local(ctx, var_name) && (ctx->current_module || !codegen_is_main_var(ctx, var_name))) {
                // Local variable - use bare name
                // In module context, locals always shadow main vars
                // Outside module, only if not a tracked main var
                // var_name stays as-is
            } else if (codegen_is_main_var(ctx, expr->as.assign.name)) {
                // Main file top-level variable - use _main_ prefix
                snprintf(prefixed_name, sizeof(prefixed_name), "_main_%s", expr->as.assign.name);
                var_name = prefixed_name;
            }
            codegen_writeln(ctx, "hml_release(&%s);", var_name);
            codegen_writeln(ctx, "%s = %s;", var_name, value);
            codegen_writeln(ctx, "hml_retain(&%s);", var_name);

            // If we're inside a closure and this is a captured variable,
            // update the closure environment so the change is visible to other closures
            if (ctx->current_closure && ctx->current_closure->num_captured > 0) {
                for (int i = 0; i < ctx->current_closure->num_captured; i++) {
                    if (strcmp(ctx->current_closure->captured_vars[i], expr->as.assign.name) == 0) {
                        // Use shared_env_indices if using shared environment, otherwise use local index
                        int env_index = ctx->current_closure->shared_env_indices ?
                                       ctx->current_closure->shared_env_indices[i] : i;
                        codegen_writeln(ctx, "hml_closure_env_set(_closure_env, %d, %s);", env_index, var_name);
                        break;
                    }
                }
            }

            codegen_writeln(ctx, "HmlValue %s = %s;", result, var_name);
            codegen_writeln(ctx, "hml_retain(&%s);", result);
            free(value);
            break;
        }

        case EXPR_GET_PROPERTY: {
            char *obj = codegen_expr(ctx, expr->as.get_property.object);

            // Check for built-in properties like .length
            if (strcmp(expr->as.get_property.property, "length") == 0) {
                codegen_writeln(ctx, "HmlValue %s;", result);
                codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_array_length(%s);", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_STRING) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_string_length(%s);", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_BUFFER) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_buffer_length(%s);", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_object_get_field(%s, \"length\");", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            // Socket properties: fd, address, port, closed
            } else if (strcmp(expr->as.get_property.property, "fd") == 0) {
                codegen_writeln(ctx, "HmlValue %s;", result);
                codegen_writeln(ctx, "if (%s.type == HML_VAL_SOCKET) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_socket_get_fd(%s);", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_object_get_field(%s, \"fd\");", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            } else if (strcmp(expr->as.get_property.property, "address") == 0) {
                codegen_writeln(ctx, "HmlValue %s;", result);
                codegen_writeln(ctx, "if (%s.type == HML_VAL_SOCKET) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_socket_get_address(%s);", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_object_get_field(%s, \"address\");", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            } else if (strcmp(expr->as.get_property.property, "port") == 0) {
                codegen_writeln(ctx, "HmlValue %s;", result);
                codegen_writeln(ctx, "if (%s.type == HML_VAL_SOCKET) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_socket_get_port(%s);", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_object_get_field(%s, \"port\");", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            } else if (strcmp(expr->as.get_property.property, "closed") == 0) {
                codegen_writeln(ctx, "HmlValue %s;", result);
                codegen_writeln(ctx, "if (%s.type == HML_VAL_SOCKET) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_socket_get_closed(%s);", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_object_get_field(%s, \"closed\");", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            // String byte_length property
            } else if (strcmp(expr->as.get_property.property, "byte_length") == 0) {
                codegen_writeln(ctx, "HmlValue %s;", result);
                codegen_writeln(ctx, "if (%s.type == HML_VAL_STRING) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_string_byte_length(%s);", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_object_get_field(%s, \"byte_length\");", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            // Buffer capacity property
            } else if (strcmp(expr->as.get_property.property, "capacity") == 0) {
                codegen_writeln(ctx, "HmlValue %s;", result);
                codegen_writeln(ctx, "if (%s.type == HML_VAL_BUFFER) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_buffer_capacity(%s);", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_object_get_field(%s, \"capacity\");", result, obj);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_object_get_field(%s, \"%s\");",
                              result, obj, expr->as.get_property.property);
            }
            codegen_writeln(ctx, "hml_release(&%s);", obj);
            free(obj);
            break;
        }

        case EXPR_SET_PROPERTY: {
            char *obj = codegen_expr(ctx, expr->as.set_property.object);
            char *value = codegen_expr(ctx, expr->as.set_property.value);
            codegen_writeln(ctx, "hml_object_set_field(%s, \"%s\", %s);",
                          obj, expr->as.set_property.property, value);
            codegen_writeln(ctx, "HmlValue %s = %s;", result, value);
            codegen_writeln(ctx, "hml_retain(&%s);", result);
            codegen_writeln(ctx, "hml_release(&%s);", obj);
            free(obj);
            free(value);
            break;
        }

        case EXPR_INDEX: {
            char *obj = codegen_expr(ctx, expr->as.index.object);
            char *idx = codegen_expr(ctx, expr->as.index.index);
            codegen_writeln(ctx, "HmlValue %s;", result);
            // OPTIMIZATION: Fast path for array[i32] - the most common indexing case
            // This matches the interpreter's fast path and avoids function call overhead
            codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY && %s.type == HML_VAL_I32) {", obj, idx);
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "%s = hml_array_get_i32_fast(%s.as.as_array, %s.as.as_i32);", result, obj, idx);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "} else if (%s.type == HML_VAL_ARRAY) {", obj);
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "%s = hml_array_get(%s, %s);", result, obj, idx);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "} else if (%s.type == HML_VAL_STRING) {", obj);
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "%s = hml_string_index(%s, %s);", result, obj, idx);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "} else if (%s.type == HML_VAL_BUFFER) {", obj);
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "%s = hml_buffer_get(%s, %s);", result, obj, idx);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "} else if (%s.type == HML_VAL_OBJECT && %s.type == HML_VAL_STRING) {", obj, idx);
            codegen_indent_inc(ctx);
            // Dynamic object property access with string key
            codegen_writeln(ctx, "%s = hml_object_get_field(%s, %s.as.as_string->data);", result, obj, idx);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "} else {");
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "%s = hml_val_null();", result);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            // Use optimized release that skips primitives (index is often i32)
            codegen_writeln(ctx, "hml_release_if_needed(&%s);", obj);
            codegen_writeln(ctx, "hml_release_if_needed(&%s);", idx);
            free(obj);
            free(idx);
            break;
        }

        case EXPR_INDEX_ASSIGN: {
            char *obj = codegen_expr(ctx, expr->as.index_assign.object);
            char *idx = codegen_expr(ctx, expr->as.index_assign.index);
            char *value = codegen_expr(ctx, expr->as.index_assign.value);
            codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY) {", obj);
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "hml_array_set(%s, %s, %s);", obj, idx, value);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "} else if (%s.type == HML_VAL_STRING) {", obj);
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "hml_string_index_assign(%s, %s, %s);", obj, idx, value);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "} else if (%s.type == HML_VAL_BUFFER) {", obj);
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "hml_buffer_set(%s, %s, %s);", obj, idx, value);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "} else if (%s.type == HML_VAL_OBJECT && %s.type == HML_VAL_STRING) {", obj, idx);
            codegen_indent_inc(ctx);
            // Dynamic object property assignment with string key
            codegen_writeln(ctx, "hml_object_set_field(%s, %s.as.as_string->data, %s);", obj, idx, value);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            codegen_writeln(ctx, "HmlValue %s = %s;", result, value);
            codegen_writeln(ctx, "hml_retain(&%s);", result);
            codegen_writeln(ctx, "hml_release(&%s);", obj);
            codegen_writeln(ctx, "hml_release(&%s);", idx);
            free(obj);
            free(idx);
            free(value);
            break;
        }

        case EXPR_ARRAY_LITERAL: {
            codegen_writeln(ctx, "HmlValue %s = hml_val_array();", result);
            for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                char *elem = codegen_expr(ctx, expr->as.array_literal.elements[i]);
                codegen_writeln(ctx, "hml_array_push(%s, %s);", result, elem);
                codegen_writeln(ctx, "hml_release(&%s);", elem);
                free(elem);
            }
            break;
        }

        case EXPR_OBJECT_LITERAL: {
            codegen_writeln(ctx, "HmlValue %s = hml_val_object();", result);
            for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
                char *val = codegen_expr(ctx, expr->as.object_literal.field_values[i]);
                codegen_writeln(ctx, "hml_object_set_field(%s, \"%s\", %s);",
                              result, expr->as.object_literal.field_names[i], val);
                codegen_writeln(ctx, "hml_release(&%s);", val);
                free(val);
            }
            break;
        }

        case EXPR_FUNCTION: {
            // Generate anonymous function with closure support
            char *func_name = codegen_anon_func(ctx);

            // Create a scope for analyzing free variables
            Scope *func_scope = scope_new(NULL);

            // Add parameters to the function's scope
            for (int i = 0; i < expr->as.function.num_params; i++) {
                scope_add_var(func_scope, expr->as.function.param_names[i]);
            }

            // Find free variables
            FreeVarSet *free_vars = free_var_set_new();
            find_free_vars_stmt(expr->as.function.body, func_scope, free_vars);

            // Filter out builtins and global functions from free vars
            // (We only want to capture actual local variables)
            FreeVarSet *captured = free_var_set_new();
            for (int i = 0; i < free_vars->num_vars; i++) {
                const char *var = free_vars->vars[i];
                // Check if it's a local variable in the current scope
                if (codegen_is_local(ctx, var)) {
                    free_var_set_add(captured, var);
                }
            }

            // Register closure for later code generation
            // If using shared environment, store indices into shared env in captured_vars
            ClosureInfo *closure = malloc(sizeof(ClosureInfo));
            closure->func_name = strdup(func_name);
            closure->func_expr = expr;
            closure->source_module = ctx->current_module;  // Save module context for function resolution
            closure->next = ctx->closures;
            ctx->closures = closure;

            if (captured->num_vars == 0) {
                // No captures - simple function pointer
                closure->captured_vars = NULL;
                closure->num_captured = 0;
                closure->shared_env_indices = NULL;
                int num_required = count_required_params(expr->as.function.param_defaults, expr->as.function.num_params);
                codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)%s, %d, %d, %d);",
                              result, func_name, expr->as.function.num_params, num_required, expr->as.function.is_async);
            } else if (ctx->shared_env_name) {
                // Use the shared environment
                // Store the captured variable names and their shared env indices
                closure->captured_vars = malloc(captured->num_vars * sizeof(char*));
                closure->shared_env_indices = malloc(captured->num_vars * sizeof(int));
                closure->num_captured = captured->num_vars;
                for (int i = 0; i < captured->num_vars; i++) {
                    closure->captured_vars[i] = strdup(captured->vars[i]);
                    closure->shared_env_indices[i] = shared_env_get_index(ctx, captured->vars[i]);
                }

                // Update the shared environment with current values of captured variables
                for (int i = 0; i < captured->num_vars; i++) {
                    int shared_idx = shared_env_get_index(ctx, captured->vars[i]);
                    if (shared_idx >= 0) {
                        // Determine which variable name to use:
                        // - Main file vars are stored as _main_<name> in C
                        // - Module-local vars are stored as <name> in C
                        // - Module-local vars should shadow outer (main) vars with same name
                        if (ctx->current_module && codegen_is_local(ctx, captured->vars[i])) {
                            // Inside a module with a local variable - use bare name
                            // This handles cases like Set() having local 'map' that shadows main's 'map'
                            codegen_writeln(ctx, "hml_closure_env_set(%s, %d, %s);",
                                          ctx->shared_env_name, shared_idx, captured->vars[i]);
                        } else if (codegen_is_main_var(ctx, captured->vars[i])) {
                            // Main file variable (not in a module, or not a local) - use prefix
                            codegen_writeln(ctx, "hml_closure_env_set(%s, %d, _main_%s);",
                                          ctx->shared_env_name, shared_idx, captured->vars[i]);
                        } else {
                            // Neither - use as-is
                            codegen_writeln(ctx, "hml_closure_env_set(%s, %d, %s);",
                                          ctx->shared_env_name, shared_idx, captured->vars[i]);
                        }
                    }
                }
                int num_required = count_required_params(expr->as.function.param_defaults, expr->as.function.num_params);
                codegen_writeln(ctx, "HmlValue %s = hml_val_function_with_env((void*)%s, (void*)%s, %d, %d, %d);",
                              result, func_name, ctx->shared_env_name, expr->as.function.num_params, num_required, expr->as.function.is_async);

                // Track for self-reference fixup
                ctx->last_closure_env_id = -1;  // Using shared env, different mechanism
                if (ctx->last_closure_captured) {
                    for (int i = 0; i < ctx->last_closure_num_captured; i++) {
                        free(ctx->last_closure_captured[i]);
                    }
                    free(ctx->last_closure_captured);
                }
                ctx->last_closure_captured = malloc(sizeof(char*) * captured->num_vars);
                ctx->last_closure_num_captured = captured->num_vars;
                for (int i = 0; i < captured->num_vars; i++) {
                    ctx->last_closure_captured[i] = strdup(captured->vars[i]);
                }
            } else {
                // No shared environment - create a per-closure environment (original behavior)
                closure->captured_vars = malloc(captured->num_vars * sizeof(char*));
                closure->num_captured = captured->num_vars;
                closure->shared_env_indices = NULL;  // Not using shared environment
                for (int i = 0; i < captured->num_vars; i++) {
                    closure->captured_vars[i] = strdup(captured->vars[i]);
                }

                int env_id = ctx->temp_counter;
                codegen_writeln(ctx, "HmlClosureEnv *_env_%d = hml_closure_env_new(%d);",
                              env_id, captured->num_vars);
                for (int i = 0; i < captured->num_vars; i++) {
                    // Determine which variable name to use:
                    // - Main file vars are stored as _main_<name> in C
                    // - Module-local vars are stored as <name> in C
                    // - Module-local vars should shadow outer (main) vars with same name
                    if (ctx->current_module && codegen_is_local(ctx, captured->vars[i])) {
                        // Inside a module with a local variable - use bare name
                        codegen_writeln(ctx, "hml_closure_env_set(_env_%d, %d, %s);",
                                      env_id, i, captured->vars[i]);
                    } else if (codegen_is_main_var(ctx, captured->vars[i])) {
                        // Main file variable (not in a module, or not a local) - use prefix
                        codegen_writeln(ctx, "hml_closure_env_set(_env_%d, %d, _main_%s);",
                                      env_id, i, captured->vars[i]);
                    } else {
                        // Neither - use as-is
                        codegen_writeln(ctx, "hml_closure_env_set(_env_%d, %d, %s);",
                                      env_id, i, captured->vars[i]);
                    }
                }
                int num_required = count_required_params(expr->as.function.param_defaults, expr->as.function.num_params);
                codegen_writeln(ctx, "HmlValue %s = hml_val_function_with_env((void*)%s, (void*)_env_%d, %d, %d, %d);",
                              result, func_name, env_id, expr->as.function.num_params, num_required, expr->as.function.is_async);
                ctx->temp_counter++;

                // Track this closure for potential self-reference fixup in let statements
                ctx->last_closure_env_id = env_id;
                // Copy captured variable names since 'captured' will be freed
                if (ctx->last_closure_captured) {
                    for (int i = 0; i < ctx->last_closure_num_captured; i++) {
                        free(ctx->last_closure_captured[i]);
                    }
                    free(ctx->last_closure_captured);
                }
                ctx->last_closure_captured = malloc(sizeof(char*) * captured->num_vars);
                ctx->last_closure_num_captured = captured->num_vars;
                for (int i = 0; i < captured->num_vars; i++) {
                    ctx->last_closure_captured[i] = strdup(captured->vars[i]);
                }
            }

            scope_free(func_scope);
            free_var_set_free(free_vars);
            free_var_set_free(captured);
            free(func_name);
            break;
        }

        case EXPR_PREFIX_INC: {
            // ++x is equivalent to x = x + 1, returns new value
            if (expr->as.prefix_inc.operand->type == EXPR_IDENT) {
                const char *raw_var = expr->as.prefix_inc.operand->as.ident;
                const char *var = raw_var;
                char prefixed_name[256];
                if (ctx->current_module && !codegen_is_local(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "%s%s",
                            ctx->current_module->module_prefix, raw_var);
                    var = prefixed_name;
                } else if (codegen_is_main_var(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "_main_%s", raw_var);
                    var = prefixed_name;
                }
                codegen_writeln(ctx, "%s = hml_binary_op(HML_OP_ADD, %s, hml_val_i32(1));", var, var);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, var);
                codegen_writeln(ctx, "hml_retain(&%s);", result);
            } else if (expr->as.prefix_inc.operand->type == EXPR_INDEX) {
                // ++arr[i]
                char *arr = codegen_expr(ctx, expr->as.prefix_inc.operand->as.index.object);
                char *idx = codegen_expr(ctx, expr->as.prefix_inc.operand->as.index.index);
                char *old_val = codegen_temp(ctx);
                char *new_val = codegen_temp(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_array_get(%s, %s);", old_val, arr, idx);
                codegen_writeln(ctx, "HmlValue %s = hml_binary_op(HML_OP_ADD, %s, hml_val_i32(1));", new_val, old_val);
                codegen_writeln(ctx, "hml_array_set(%s, %s, %s);", arr, idx, new_val);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, new_val);
                codegen_writeln(ctx, "hml_retain(&%s);", result);
                codegen_writeln(ctx, "hml_release(&%s);", old_val);
                codegen_writeln(ctx, "hml_release(&%s);", new_val);
                codegen_writeln(ctx, "hml_release(&%s);", idx);
                codegen_writeln(ctx, "hml_release(&%s);", arr);
                free(arr); free(idx); free(old_val); free(new_val);
            } else if (expr->as.prefix_inc.operand->type == EXPR_GET_PROPERTY) {
                // ++obj.prop
                char *obj = codegen_expr(ctx, expr->as.prefix_inc.operand->as.get_property.object);
                const char *prop = expr->as.prefix_inc.operand->as.get_property.property;
                char *old_val = codegen_temp(ctx);
                char *new_val = codegen_temp(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_object_get_field(%s, \"%s\");", old_val, obj, prop);
                codegen_writeln(ctx, "HmlValue %s = hml_binary_op(HML_OP_ADD, %s, hml_val_i32(1));", new_val, old_val);
                codegen_writeln(ctx, "hml_object_set_field(%s, \"%s\", %s);", obj, prop, new_val);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, new_val);
                codegen_writeln(ctx, "hml_retain(&%s);", result);
                codegen_writeln(ctx, "hml_release(&%s);", old_val);
                codegen_writeln(ctx, "hml_release(&%s);", new_val);
                codegen_writeln(ctx, "hml_release(&%s);", obj);
                free(obj); free(old_val); free(new_val);
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_val_null(); // Complex prefix inc not supported", result);
            }
            break;
        }

        case EXPR_PREFIX_DEC: {
            if (expr->as.prefix_dec.operand->type == EXPR_IDENT) {
                const char *raw_var = expr->as.prefix_dec.operand->as.ident;
                const char *var = raw_var;
                char prefixed_name[256];
                if (ctx->current_module && !codegen_is_local(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "%s%s",
                            ctx->current_module->module_prefix, raw_var);
                    var = prefixed_name;
                } else if (codegen_is_main_var(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "_main_%s", raw_var);
                    var = prefixed_name;
                }
                codegen_writeln(ctx, "%s = hml_binary_op(HML_OP_SUB, %s, hml_val_i32(1));", var, var);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, var);
                codegen_writeln(ctx, "hml_retain(&%s);", result);
            } else if (expr->as.prefix_dec.operand->type == EXPR_INDEX) {
                // --arr[i]
                char *arr = codegen_expr(ctx, expr->as.prefix_dec.operand->as.index.object);
                char *idx = codegen_expr(ctx, expr->as.prefix_dec.operand->as.index.index);
                char *old_val = codegen_temp(ctx);
                char *new_val = codegen_temp(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_array_get(%s, %s);", old_val, arr, idx);
                codegen_writeln(ctx, "HmlValue %s = hml_binary_op(HML_OP_SUB, %s, hml_val_i32(1));", new_val, old_val);
                codegen_writeln(ctx, "hml_array_set(%s, %s, %s);", arr, idx, new_val);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, new_val);
                codegen_writeln(ctx, "hml_retain(&%s);", result);
                codegen_writeln(ctx, "hml_release(&%s);", old_val);
                codegen_writeln(ctx, "hml_release(&%s);", new_val);
                codegen_writeln(ctx, "hml_release(&%s);", idx);
                codegen_writeln(ctx, "hml_release(&%s);", arr);
                free(arr); free(idx); free(old_val); free(new_val);
            } else if (expr->as.prefix_dec.operand->type == EXPR_GET_PROPERTY) {
                // --obj.prop
                char *obj = codegen_expr(ctx, expr->as.prefix_dec.operand->as.get_property.object);
                const char *prop = expr->as.prefix_dec.operand->as.get_property.property;
                char *old_val = codegen_temp(ctx);
                char *new_val = codegen_temp(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_object_get_field(%s, \"%s\");", old_val, obj, prop);
                codegen_writeln(ctx, "HmlValue %s = hml_binary_op(HML_OP_SUB, %s, hml_val_i32(1));", new_val, old_val);
                codegen_writeln(ctx, "hml_object_set_field(%s, \"%s\", %s);", obj, prop, new_val);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, new_val);
                codegen_writeln(ctx, "hml_retain(&%s);", result);
                codegen_writeln(ctx, "hml_release(&%s);", old_val);
                codegen_writeln(ctx, "hml_release(&%s);", new_val);
                codegen_writeln(ctx, "hml_release(&%s);", obj);
                free(obj); free(old_val); free(new_val);
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            }
            break;
        }

        case EXPR_POSTFIX_INC: {
            // x++ returns old value, then increments
            if (expr->as.postfix_inc.operand->type == EXPR_IDENT) {
                const char *raw_var = expr->as.postfix_inc.operand->as.ident;
                const char *var = raw_var;
                char prefixed_name[256];
                if (ctx->current_module && !codegen_is_local(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "%s%s",
                            ctx->current_module->module_prefix, raw_var);
                    var = prefixed_name;
                } else if (codegen_is_main_var(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "_main_%s", raw_var);
                    var = prefixed_name;
                }
                codegen_writeln(ctx, "HmlValue %s = %s;", result, var);
                codegen_writeln(ctx, "hml_retain(&%s);", result);
                codegen_writeln(ctx, "%s = hml_binary_op(HML_OP_ADD, %s, hml_val_i32(1));", var, var);
            } else if (expr->as.postfix_inc.operand->type == EXPR_INDEX) {
                // arr[i]++
                char *arr = codegen_expr(ctx, expr->as.postfix_inc.operand->as.index.object);
                char *idx = codegen_expr(ctx, expr->as.postfix_inc.operand->as.index.index);
                char *old_val = codegen_temp(ctx);
                char *new_val = codegen_temp(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_array_get(%s, %s);", old_val, arr, idx);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, old_val);  // Return old value
                codegen_writeln(ctx, "hml_retain(&%s);", result);
                codegen_writeln(ctx, "HmlValue %s = hml_binary_op(HML_OP_ADD, %s, hml_val_i32(1));", new_val, old_val);
                codegen_writeln(ctx, "hml_array_set(%s, %s, %s);", arr, idx, new_val);
                codegen_writeln(ctx, "hml_release(&%s);", old_val);
                codegen_writeln(ctx, "hml_release(&%s);", new_val);
                codegen_writeln(ctx, "hml_release(&%s);", idx);
                codegen_writeln(ctx, "hml_release(&%s);", arr);
                free(arr); free(idx); free(old_val); free(new_val);
            } else if (expr->as.postfix_inc.operand->type == EXPR_GET_PROPERTY) {
                // obj.prop++
                char *obj = codegen_expr(ctx, expr->as.postfix_inc.operand->as.get_property.object);
                const char *prop = expr->as.postfix_inc.operand->as.get_property.property;
                char *old_val = codegen_temp(ctx);
                char *new_val = codegen_temp(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_object_get_field(%s, \"%s\");", old_val, obj, prop);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, old_val);  // Return old value
                codegen_writeln(ctx, "hml_retain(&%s);", result);
                codegen_writeln(ctx, "HmlValue %s = hml_binary_op(HML_OP_ADD, %s, hml_val_i32(1));", new_val, old_val);
                codegen_writeln(ctx, "hml_object_set_field(%s, \"%s\", %s);", obj, prop, new_val);
                codegen_writeln(ctx, "hml_release(&%s);", old_val);
                codegen_writeln(ctx, "hml_release(&%s);", new_val);
                codegen_writeln(ctx, "hml_release(&%s);", obj);
                free(obj); free(old_val); free(new_val);
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            }
            break;
        }

        case EXPR_POSTFIX_DEC: {
            if (expr->as.postfix_dec.operand->type == EXPR_IDENT) {
                const char *raw_var = expr->as.postfix_dec.operand->as.ident;
                const char *var = raw_var;
                char prefixed_name[256];
                if (ctx->current_module && !codegen_is_local(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "%s%s",
                            ctx->current_module->module_prefix, raw_var);
                    var = prefixed_name;
                } else if (codegen_is_main_var(ctx, raw_var)) {
                    snprintf(prefixed_name, sizeof(prefixed_name), "_main_%s", raw_var);
                    var = prefixed_name;
                }
                codegen_writeln(ctx, "HmlValue %s = %s;", result, var);
                codegen_writeln(ctx, "hml_retain(&%s);", result);
                codegen_writeln(ctx, "%s = hml_binary_op(HML_OP_SUB, %s, hml_val_i32(1));", var, var);
            } else if (expr->as.postfix_dec.operand->type == EXPR_INDEX) {
                // arr[i]--
                char *arr = codegen_expr(ctx, expr->as.postfix_dec.operand->as.index.object);
                char *idx = codegen_expr(ctx, expr->as.postfix_dec.operand->as.index.index);
                char *old_val = codegen_temp(ctx);
                char *new_val = codegen_temp(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_array_get(%s, %s);", old_val, arr, idx);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, old_val);  // Return old value
                codegen_writeln(ctx, "hml_retain(&%s);", result);
                codegen_writeln(ctx, "HmlValue %s = hml_binary_op(HML_OP_SUB, %s, hml_val_i32(1));", new_val, old_val);
                codegen_writeln(ctx, "hml_array_set(%s, %s, %s);", arr, idx, new_val);
                codegen_writeln(ctx, "hml_release(&%s);", old_val);
                codegen_writeln(ctx, "hml_release(&%s);", new_val);
                codegen_writeln(ctx, "hml_release(&%s);", idx);
                codegen_writeln(ctx, "hml_release(&%s);", arr);
                free(arr); free(idx); free(old_val); free(new_val);
            } else if (expr->as.postfix_dec.operand->type == EXPR_GET_PROPERTY) {
                // obj.prop--
                char *obj = codegen_expr(ctx, expr->as.postfix_dec.operand->as.get_property.object);
                const char *prop = expr->as.postfix_dec.operand->as.get_property.property;
                char *old_val = codegen_temp(ctx);
                char *new_val = codegen_temp(ctx);
                codegen_writeln(ctx, "HmlValue %s = hml_object_get_field(%s, \"%s\");", old_val, obj, prop);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, old_val);  // Return old value
                codegen_writeln(ctx, "hml_retain(&%s);", result);
                codegen_writeln(ctx, "HmlValue %s = hml_binary_op(HML_OP_SUB, %s, hml_val_i32(1));", new_val, old_val);
                codegen_writeln(ctx, "hml_object_set_field(%s, \"%s\", %s);", obj, prop, new_val);
                codegen_writeln(ctx, "hml_release(&%s);", old_val);
                codegen_writeln(ctx, "hml_release(&%s);", new_val);
                codegen_writeln(ctx, "hml_release(&%s);", obj);
                free(obj); free(old_val); free(new_val);
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            }
            break;
        }

        case EXPR_STRING_INTERPOLATION: {
            // Build the string by concatenating parts
            codegen_writeln(ctx, "HmlValue %s = hml_val_string(\"\");", result);

            for (int i = 0; i <= expr->as.string_interpolation.num_parts; i++) {
                // Add string part (there are num_parts+1 string parts)
                if (expr->as.string_interpolation.string_parts[i] &&
                    strlen(expr->as.string_interpolation.string_parts[i]) > 0) {
                    char *escaped = codegen_escape_string(expr->as.string_interpolation.string_parts[i]);
                    char *part_temp = codegen_temp(ctx);
                    codegen_writeln(ctx, "HmlValue %s = hml_val_string(\"%s\");", part_temp, escaped);
                    codegen_writeln(ctx, "HmlValue _concat%d = hml_string_concat(%s, %s);", ctx->temp_counter, result, part_temp);
                    codegen_writeln(ctx, "hml_release(&%s);", result);
                    codegen_writeln(ctx, "hml_release(&%s);", part_temp);
                    codegen_writeln(ctx, "%s = _concat%d;", result, ctx->temp_counter);
                    free(escaped);
                    free(part_temp);
                }

                // Add expression part (there are num_parts expression parts)
                if (i < expr->as.string_interpolation.num_parts) {
                    char *expr_val = codegen_expr(ctx, expr->as.string_interpolation.expr_parts[i]);
                    codegen_writeln(ctx, "HmlValue _concat%d = hml_string_concat(%s, %s);", ctx->temp_counter, result, expr_val);
                    codegen_writeln(ctx, "hml_release(&%s);", result);
                    codegen_writeln(ctx, "hml_release(&%s);", expr_val);
                    codegen_writeln(ctx, "%s = _concat%d;", result, ctx->temp_counter);
                    free(expr_val);
                }
            }
            break;
        }

        case EXPR_AWAIT: {
            // await expr - if value is a task, join it; otherwise return as-is
            char *awaited = codegen_expr(ctx, expr->as.await_expr.awaited_expr);
            codegen_writeln(ctx, "HmlValue %s;", result);
            codegen_writeln(ctx, "if (%s.type == HML_VAL_TASK) {", awaited);
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "%s = hml_join(%s);", result, awaited);
            codegen_writeln(ctx, "hml_release(&%s);", awaited);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "} else {");
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "%s = %s;", result, awaited);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            free(awaited);
            break;
        }

        case EXPR_NULL_COALESCE: {
            // left ?? right
            char *left = codegen_expr(ctx, expr->as.null_coalesce.left);
            codegen_writeln(ctx, "HmlValue %s;", result);
            codegen_writeln(ctx, "if (!hml_is_null(%s)) {", left);
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "%s = %s;", result, left);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "} else {");
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "hml_release(&%s);", left);
            char *right = codegen_expr(ctx, expr->as.null_coalesce.right);
            codegen_writeln(ctx, "%s = %s;", result, right);
            free(right);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            free(left);
            break;
        }

        case EXPR_OPTIONAL_CHAIN: {
            // obj?.property or obj?.[index] or obj?.method()
            char *obj = codegen_expr(ctx, expr->as.optional_chain.object);
            codegen_writeln(ctx, "HmlValue %s;", result);
            codegen_writeln(ctx, "if (hml_is_null(%s)) {", obj);
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "%s = hml_val_null();", result);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "} else {");
            codegen_indent_inc(ctx);

            if (expr->as.optional_chain.is_property) {
                // obj?.property - check for built-in properties like .length
                const char *prop = expr->as.optional_chain.property;
                if (strcmp(prop, "length") == 0) {
                    codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY) {", obj);
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "%s = hml_array_length(%s);", result, obj);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "} else if (%s.type == HML_VAL_STRING) {", obj);
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "%s = hml_string_length(%s);", result, obj);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "} else if (%s.type == HML_VAL_BUFFER) {", obj);
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "%s = hml_buffer_length(%s);", result, obj);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "} else {");
                    codegen_indent_inc(ctx);
                    codegen_writeln(ctx, "%s = hml_object_get_field(%s, \"length\");", result, obj);
                    codegen_indent_dec(ctx);
                    codegen_writeln(ctx, "}");
                } else {
                    codegen_writeln(ctx, "%s = hml_object_get_field(%s, \"%s\");", result, obj, prop);
                }
            } else if (expr->as.optional_chain.is_call) {
                // obj?.method(args) - not yet supported
                codegen_writeln(ctx, "%s = hml_val_null(); // optional call not supported", result);
            } else {
                // obj?.[index]
                char *idx = codegen_expr(ctx, expr->as.optional_chain.index);
                codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_array_get(%s, %s);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_STRING) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_string_index(%s, %s);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else if (%s.type == HML_VAL_BUFFER) {", obj);
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_buffer_get(%s, %s);", result, obj, idx);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_val_null();", result);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "}");
                codegen_writeln(ctx, "hml_release(&%s);", idx);
                free(idx);
            }

            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            codegen_writeln(ctx, "hml_release(&%s);", obj);
            free(obj);
            break;
        }

        default:
            codegen_writeln(ctx, "HmlValue %s = hml_val_null(); // Unsupported expression type %d", result, expr->type);
            break;
    }

    return result;
}
