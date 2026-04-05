/*
 * Hemlock Code Generator - String Utility Call Builtins
 *
 * Handles string-related builtins called as functions (not methods):
 * to_string, string_byte_length, strerror, __dirent_name, string_to_cstr,
 * cstr_to_string, __string_from_bytes, string_concat_many
 */

#include "codegen_call_internal.h"

int codegen_call_strings(CodegenContext *ctx, Expr *expr, char *result,
                         const char *func_name, Expr **call_args, int num_args) {
    (void)expr;

    // to_string(value)
    if (strcmp(func_name, "to_string") == 0 && num_args == 1) {
        char *val = codegen_expr(ctx, call_args[0]);
        codegen_writeln(ctx, "HmlValue %s = hml_to_string(%s);", result, val);
        codegen_writeln(ctx, "hml_release(&%s);", val);
        free(val);
        return 1;
    }

    // string_byte_length(str)
    if (strcmp(func_name, "string_byte_length") == 0 && num_args == 1) {
        char *str = codegen_expr(ctx, call_args[0]);
        codegen_writeln(ctx, "HmlValue %s = hml_string_byte_length(%s);", result, str);
        codegen_writeln(ctx, "hml_release(&%s);", str);
        free(str);
        return 1;
    }

    // strerror() / __strerror()
    if ((strcmp(func_name, "strerror") == 0 || strcmp(func_name, "__strerror") == 0) && num_args == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_strerror();", result);
        return 1;
    }

    // __dirent_name(ptr) - extract directory entry name from dirent pointer
    if (strcmp(func_name, "__dirent_name") == 0 && num_args == 1) {
        char *ptr = codegen_expr(ctx, call_args[0]);
        codegen_writeln(ctx, "HmlValue %s = hml_dirent_name(%s);", result, ptr);
        codegen_writeln(ctx, "hml_release(&%s);", ptr);
        free(ptr);
        return 1;
    }

    // string_to_cstr(str) / __string_to_cstr(str)
    if ((strcmp(func_name, "string_to_cstr") == 0 || strcmp(func_name, "__string_to_cstr") == 0) && num_args == 1) {
        char *str = codegen_expr(ctx, call_args[0]);
        codegen_writeln(ctx, "HmlValue %s = hml_string_to_cstr(%s);", result, str);
        codegen_writeln(ctx, "hml_release(&%s);", str);
        free(str);
        return 1;
    }

    // cstr_to_string(ptr) / __cstr_to_string(ptr)
    if ((strcmp(func_name, "cstr_to_string") == 0 || strcmp(func_name, "__cstr_to_string") == 0) && num_args == 1) {
        char *ptr = codegen_expr(ctx, call_args[0]);
        codegen_writeln(ctx, "HmlValue %s = hml_cstr_to_string(%s);", result, ptr);
        codegen_writeln(ctx, "hml_release(&%s);", ptr);
        free(ptr);
        return 1;
    }

    // __string_from_bytes(bytes) - convert byte array/buffer to UTF-8 string
    if (strcmp(func_name, "__string_from_bytes") == 0 && num_args == 1) {
        char *bytes = codegen_expr(ctx, call_args[0]);
        codegen_writeln(ctx, "HmlValue %s = hml_string_from_bytes(%s);", result, bytes);
        codegen_writeln(ctx, "hml_release(&%s);", bytes);
        free(bytes);
        return 1;
    }

    // string_concat_many(array)
    if ((strcmp(func_name, "string_concat_many") == 0 || strcmp(func_name, "__string_concat_many") == 0) && num_args == 1) {
        char *arr = codegen_expr(ctx, call_args[0]);
        codegen_writeln(ctx, "HmlValue %s = hml_string_concat_many(%s);", result, arr);
        codegen_writeln(ctx, "hml_release(&%s);", arr);
        free(arr);
        return 1;
    }

    return 0;
}
