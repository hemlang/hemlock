/*
 * Hemlock Code Generator - I/O Call Builtins
 *
 * Handles: print, write, eprint, read_line, open
 */

#include "codegen_call_internal.h"

int codegen_call_io(CodegenContext *ctx, Expr *expr, char *result,
                    const char *func_name, Expr **call_args, int num_args) {
    (void)expr;

    // print/write/eprint require at least one argument
    if (num_args == 0 &&
        (strcmp(func_name, "print") == 0 || strcmp(func_name, "write") == 0 ||
         strcmp(func_name, "eprint") == 0)) {
        codegen_error(ctx, expr->line, "%s() expects at least 1 argument", func_name);
        codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
        return 1;
    }

    // Handle print/write/eprint builtins (any number of arguments).
    // Evaluate ALL arguments first, then emit the output calls: the
    // interpreter evaluates the full argument list before printing, so
    // interleaving evaluation with output diverges when an argument
    // expression itself writes (e.g. `print(f(), f())` where f writes).
    int is_print = (strcmp(func_name, "print") == 0);
    int is_write = (strcmp(func_name, "write") == 0);
    int is_eprint = (strcmp(func_name, "eprint") == 0);
    if ((is_print || is_write || is_eprint) && num_args >= 1) {
        const char *emit_fn = is_eprint ? "hml_eprint_value" : "hml_print_value";
        char **args = malloc((size_t)num_args * sizeof(char *));
        if (!args) {
            codegen_error(ctx, expr->line, "Memory allocation failed for %s arguments", func_name);
            codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            return 1;
        }
        for (int i = 0; i < num_args; i++) {
            args[i] = codegen_expr(ctx, call_args[i]);
        }
        for (int i = 0; i < num_args; i++) {
            if (i > 0) {
                codegen_writeln(ctx, "%s(hml_val_string(\" \"));", emit_fn);
            }
            codegen_writeln(ctx, "%s(%s);", emit_fn, args[i]);
            codegen_writeln(ctx, "hml_release(&%s);", args[i]);
            free(args[i]);
        }
        free(args);
        if (is_print) {
            codegen_writeln(ctx, "hml_print_newline();");
        } else if (is_write) {
            codegen_writeln(ctx, "hml_write_flush();");
        } else {
            codegen_writeln(ctx, "hml_eprint_newline();");
        }
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
