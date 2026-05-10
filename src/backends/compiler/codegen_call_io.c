/*
 * Hemlock Code Generator - I/O Call Builtins
 *
 * Handles: print, write, eprint, read_line, open
 */

#include "codegen_call_internal.h"

int codegen_call_io(CodegenContext *ctx, Expr *expr, char *result,
                    const char *func_name, Expr **call_args, int num_args) {
    (void)expr;

    // Handle print builtin (supports any number of arguments)
    if (strcmp(func_name, "print") == 0 && num_args >= 1) {
        // Print each argument, with space separator between them
        for (int i = 0; i < num_args; i++) {
            char *arg = codegen_expr(ctx, call_args[i]);
            if (i > 0) {
                codegen_writeln(ctx, "hml_print_value(hml_val_string(\" \"));");
            }
            codegen_writeln(ctx, "hml_print_value(%s);", arg);
            codegen_writeln(ctx, "hml_release(&%s);", arg);
            free(arg);
        }
        codegen_writeln(ctx, "hml_print_newline();");
        codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
        return 1;
    }

    // Handle write builtin (like print but no trailing newline)
    if (strcmp(func_name, "write") == 0 && num_args >= 1) {
        for (int i = 0; i < num_args; i++) {
            char *arg = codegen_expr(ctx, call_args[i]);
            if (i > 0) {
                codegen_writeln(ctx, "hml_print_value(hml_val_string(\" \"));");
            }
            codegen_writeln(ctx, "hml_print_value(%s);", arg);
            codegen_writeln(ctx, "hml_release(&%s);", arg);
            free(arg);
        }
        codegen_writeln(ctx, "hml_write_flush();");
        codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
        return 1;
    }

    // Handle eprint builtin (supports any number of arguments)
    if (strcmp(func_name, "eprint") == 0 && num_args >= 1) {
        // Print each argument to stderr, with space separator between them
        for (int i = 0; i < num_args; i++) {
            char *arg = codegen_expr(ctx, call_args[i]);
            if (i > 0) {
                codegen_writeln(ctx, "hml_eprint_value(hml_val_string(\" \"));");
            }
            codegen_writeln(ctx, "hml_eprint_value(%s);", arg);
            codegen_writeln(ctx, "hml_release(&%s);", arg);
            free(arg);
        }
        codegen_writeln(ctx, "hml_eprint_newline();");
        codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
        return 1;
    }

    // Handle read_line()
    if (strcmp(func_name, "read_line") == 0 && num_args == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_read_line();", result);
        return 1;
    }

    // Handle open builtin for file I/O
    if ((strcmp(func_name, "open") == 0 || strcmp(func_name, "__open") == 0) && (num_args == 1 || num_args == 2)) {
        char *path = codegen_expr(ctx, call_args[0]);
        if (num_args == 2) {
            char *mode = codegen_expr(ctx, call_args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_open(%s, %s);", result, path, mode);
            codegen_writeln(ctx, "hml_release(&%s);", mode);
            free(mode);
        } else {
            codegen_writeln(ctx, "HmlValue %s = hml_open(%s, hml_val_string(\"r\"));", result, path);
        }
        codegen_writeln(ctx, "hml_release(&%s);", path);
        free(path);
        return 1;
    }

    // Handle open_fd: returns a raw POSIX fd suitable for posix_spawn redirection
    if ((strcmp(func_name, "open_fd") == 0 || strcmp(func_name, "__open_fd") == 0) && (num_args == 1 || num_args == 2)) {
        char *path = codegen_expr(ctx, call_args[0]);
        if (num_args == 2) {
            char *mode = codegen_expr(ctx, call_args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_open_fd(%s, %s);", result, path, mode);
            codegen_writeln(ctx, "hml_release(&%s);", mode);
            free(mode);
        } else {
            codegen_writeln(ctx, "HmlValue %s = hml_open_fd(%s, hml_val_string(\"r\"));", result, path);
        }
        codegen_writeln(ctx, "hml_release(&%s);", path);
        free(path);
        return 1;
    }

    // Handle fileno: extract raw POSIX fd from a File handle
    if ((strcmp(func_name, "fileno") == 0 || strcmp(func_name, "__fileno") == 0) && num_args == 1) {
        char *file = codegen_expr(ctx, call_args[0]);
        codegen_writeln(ctx, "HmlValue %s = hml_fileno(%s);", result, file);
        codegen_writeln(ctx, "hml_release(&%s);", file);
        free(file);
        return 1;
    }

    return 0;
}
