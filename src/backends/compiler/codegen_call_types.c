/*
 * Hemlock Code Generator - Type Call Builtins
 *
 * Handles: typeof, sizeof, talloc, type constructors (i8..byte, bool, rune),
 * get_stack_limit, set_stack_limit
 */

#include "codegen_call_internal.h"

int codegen_call_types(CodegenContext *ctx, Expr *expr, char *result,
                       const char *func_name, Expr **call_args, int num_args) {

    // Handle typeof builtin
    if (strcmp(func_name, "typeof") == 0 && num_args == 1) {
        char *arg = codegen_expr(ctx, call_args[0]);
        codegen_writeln(ctx, "HmlValue %s = hml_val_string(hml_typeof(%s));", result, arg);
        codegen_writeln(ctx, "hml_release(&%s);", arg);
        free(arg);
        return 1;
    }

    // Handle type constructor calls: i32("42"), f64("3.14"), bool("true"), etc.
    if (num_args == 1) {
        HmlValueType target_type = (HmlValueType)-1;
        if (strcmp(func_name, "i8") == 0) target_type = HML_VAL_I8;
        else if (strcmp(func_name, "i16") == 0) target_type = HML_VAL_I16;
        else if (strcmp(func_name, "i32") == 0) target_type = HML_VAL_I32;
        else if (strcmp(func_name, "i64") == 0) target_type = HML_VAL_I64;
        else if (strcmp(func_name, "u8") == 0) target_type = HML_VAL_U8;
        else if (strcmp(func_name, "u16") == 0) target_type = HML_VAL_U16;
        else if (strcmp(func_name, "u32") == 0) target_type = HML_VAL_U32;
        else if (strcmp(func_name, "u64") == 0) target_type = HML_VAL_U64;
        else if (strcmp(func_name, "f32") == 0) target_type = HML_VAL_F32;
        else if (strcmp(func_name, "f64") == 0) target_type = HML_VAL_F64;
        else if (strcmp(func_name, "bool") == 0) target_type = HML_VAL_BOOL;
        else if (strcmp(func_name, "rune") == 0) target_type = HML_VAL_RUNE;
        else if (strcmp(func_name, "integer") == 0) target_type = HML_VAL_I32;  // alias
        else if (strcmp(func_name, "number") == 0) target_type = HML_VAL_F64;   // alias
        else if (strcmp(func_name, "byte") == 0) target_type = HML_VAL_U8;      // alias

        if (target_type != (HmlValueType)-1) {
            char *arg = codegen_expr(ctx, call_args[0]);
            // Use hml_parse_string_to_type which allows string parsing (for type constructors)
            codegen_writeln(ctx, "HmlValue %s = hml_parse_string_to_type(%s, %d);", result, arg, target_type);
            codegen_writeln(ctx, "hml_release(&%s);", arg);
            free(arg);
            return 1;
        }
    }

    // Handle get_stack_limit builtin
    if ((strcmp(func_name, "get_stack_limit") == 0 || strcmp(func_name, "__get_stack_limit") == 0) && num_args == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_get_stack_limit();", result);
        return 1;
    }

    // Handle set_stack_limit builtin
    if ((strcmp(func_name, "set_stack_limit") == 0 || strcmp(func_name, "__set_stack_limit") == 0) && num_args == 1) {
        char *limit = codegen_expr(ctx, call_args[0]);
        codegen_writeln(ctx, "HmlValue %s = hml_set_stack_limit(%s);", result, limit);
        codegen_writeln(ctx, "hml_release(&%s);", limit);
        free(limit);
        return 1;
    }

    // Handle sizeof(type_name)
    if ((strcmp(func_name, "sizeof") == 0 || strcmp(func_name, "__sizeof") == 0) && num_args == 1) {
        // Check if argument is a type name identifier
        Expr *arg_expr = call_args[0];
        if (arg_expr->type == EXPR_IDENT) {
            const char *type_name = arg_expr->as.ident.name;
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
                return 1;
            }
        }
        // Fall through: evaluate as expression (for dynamic type checking)
        char *arg = codegen_expr(ctx, call_args[0]);
        codegen_writeln(ctx, "HmlValue %s = hml_sizeof(%s);", result, arg);
        codegen_writeln(ctx, "hml_release(&%s);", arg);
        free(arg);
        return 1;
    }

    // Handle talloc(type_name, count)
    if ((strcmp(func_name, "talloc") == 0 || strcmp(func_name, "__talloc") == 0) && num_args == 2) {
        // Check if first argument is a type name identifier
        Expr *type_expr = call_args[0];
        char *type_arg = NULL;
        int type_is_temp = 0;
        if (type_expr->type == EXPR_IDENT) {
            const char *type_name = type_expr->as.ident.name;
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
        char *count_arg = codegen_expr(ctx, call_args[1]);
        codegen_writeln(ctx, "HmlValue %s = hml_talloc(%s, %s);", result, type_arg, count_arg);
        codegen_writeln(ctx, "hml_release(&%s);", type_arg);
        codegen_writeln(ctx, "hml_release(&%s);", count_arg);
        if (type_is_temp) {
            free(type_arg);
        } else {
            free(type_arg);
        }
        free(count_arg);
        return 1;
    }

    return 0;
}
