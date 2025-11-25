/*
 * Hemlock C Code Generator
 *
 * Translates Hemlock AST to C source code that links against
 * the Hemlock runtime library.
 */

#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// ========== CONTEXT MANAGEMENT ==========

CodegenContext* codegen_new(FILE *output) {
    CodegenContext *ctx = malloc(sizeof(CodegenContext));
    ctx->output = output;
    ctx->indent = 0;
    ctx->temp_counter = 0;
    ctx->label_counter = 0;
    ctx->func_counter = 0;
    ctx->in_function = 0;
    ctx->local_vars = NULL;
    ctx->num_locals = 0;
    ctx->local_capacity = 0;
    return ctx;
}

void codegen_free(CodegenContext *ctx) {
    if (ctx) {
        for (int i = 0; i < ctx->num_locals; i++) {
            free(ctx->local_vars[i]);
        }
        free(ctx->local_vars);
        free(ctx);
    }
}

// ========== OUTPUT HELPERS ==========

void codegen_indent(CodegenContext *ctx) {
    for (int i = 0; i < ctx->indent; i++) {
        fprintf(ctx->output, "    ");
    }
}

void codegen_indent_inc(CodegenContext *ctx) {
    ctx->indent++;
}

void codegen_indent_dec(CodegenContext *ctx) {
    if (ctx->indent > 0) ctx->indent--;
}

void codegen_write(CodegenContext *ctx, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(ctx->output, fmt, args);
    va_end(args);
}

void codegen_writeln(CodegenContext *ctx, const char *fmt, ...) {
    codegen_indent(ctx);
    va_list args;
    va_start(args, fmt);
    vfprintf(ctx->output, fmt, args);
    va_end(args);
    fprintf(ctx->output, "\n");
}

char* codegen_temp(CodegenContext *ctx) {
    char *name = malloc(32);
    snprintf(name, 32, "_tmp%d", ctx->temp_counter++);
    return name;
}

char* codegen_label(CodegenContext *ctx) {
    char *name = malloc(32);
    snprintf(name, 32, "_L%d", ctx->label_counter++);
    return name;
}

char* codegen_anon_func(CodegenContext *ctx) {
    char *name = malloc(32);
    snprintf(name, 32, "hml_fn_anon_%d", ctx->func_counter++);
    return name;
}

void codegen_add_local(CodegenContext *ctx, const char *name) {
    if (ctx->num_locals >= ctx->local_capacity) {
        int new_cap = (ctx->local_capacity == 0) ? 16 : ctx->local_capacity * 2;
        ctx->local_vars = realloc(ctx->local_vars, new_cap * sizeof(char*));
        ctx->local_capacity = new_cap;
    }
    ctx->local_vars[ctx->num_locals++] = strdup(name);
}

int codegen_is_local(CodegenContext *ctx, const char *name) {
    for (int i = 0; i < ctx->num_locals; i++) {
        if (strcmp(ctx->local_vars[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

// ========== STRING HELPERS ==========

char* codegen_escape_string(const char *str) {
    if (!str) return strdup("");

    int len = strlen(str);
    // Worst case: every char needs escaping (2x) + quotes + null
    char *escaped = malloc(len * 2 + 3);
    char *p = escaped;

    while (*str) {
        switch (*str) {
            case '\n': *p++ = '\\'; *p++ = 'n'; break;
            case '\r': *p++ = '\\'; *p++ = 'r'; break;
            case '\t': *p++ = '\\'; *p++ = 't'; break;
            case '\\': *p++ = '\\'; *p++ = '\\'; break;
            case '"':  *p++ = '\\'; *p++ = '"'; break;
            default:   *p++ = *str; break;
        }
        str++;
    }
    *p = '\0';
    return escaped;
}

const char* codegen_binary_op_str(BinaryOp op) {
    switch (op) {
        case OP_ADD:           return "+";
        case OP_SUB:           return "-";
        case OP_MUL:           return "*";
        case OP_DIV:           return "/";
        case OP_MOD:           return "%";
        case OP_EQUAL:         return "==";
        case OP_NOT_EQUAL:     return "!=";
        case OP_LESS:          return "<";
        case OP_LESS_EQUAL:    return "<=";
        case OP_GREATER:       return ">";
        case OP_GREATER_EQUAL: return ">=";
        case OP_AND:           return "&&";
        case OP_OR:            return "||";
        case OP_BIT_AND:       return "&";
        case OP_BIT_OR:        return "|";
        case OP_BIT_XOR:       return "^";
        case OP_BIT_LSHIFT:    return "<<";
        case OP_BIT_RSHIFT:    return ">>";
        default:               return "?";
    }
}

const char* codegen_hml_binary_op(BinaryOp op) {
    switch (op) {
        case OP_ADD:           return "HML_OP_ADD";
        case OP_SUB:           return "HML_OP_SUB";
        case OP_MUL:           return "HML_OP_MUL";
        case OP_DIV:           return "HML_OP_DIV";
        case OP_MOD:           return "HML_OP_MOD";
        case OP_EQUAL:         return "HML_OP_EQUAL";
        case OP_NOT_EQUAL:     return "HML_OP_NOT_EQUAL";
        case OP_LESS:          return "HML_OP_LESS";
        case OP_LESS_EQUAL:    return "HML_OP_LESS_EQUAL";
        case OP_GREATER:       return "HML_OP_GREATER";
        case OP_GREATER_EQUAL: return "HML_OP_GREATER_EQUAL";
        case OP_AND:           return "HML_OP_AND";
        case OP_OR:            return "HML_OP_OR";
        case OP_BIT_AND:       return "HML_OP_BIT_AND";
        case OP_BIT_OR:        return "HML_OP_BIT_OR";
        case OP_BIT_XOR:       return "HML_OP_BIT_XOR";
        case OP_BIT_LSHIFT:    return "HML_OP_LSHIFT";
        case OP_BIT_RSHIFT:    return "HML_OP_RSHIFT";
        default:               return "HML_OP_ADD";
    }
}

const char* codegen_hml_unary_op(UnaryOp op) {
    switch (op) {
        case UNARY_NOT:     return "HML_UNARY_NOT";
        case UNARY_NEGATE:  return "HML_UNARY_NEGATE";
        case UNARY_BIT_NOT: return "HML_UNARY_BIT_NOT";
        default:            return "HML_UNARY_NOT";
    }
}

// ========== EXPRESSION CODE GENERATION ==========

// Forward declarations
static void codegen_function_decl(CodegenContext *ctx, Expr *func, const char *name);

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
            // Reference the variable directly
            codegen_writeln(ctx, "HmlValue %s = %s;", result, expr->as.ident);
            codegen_writeln(ctx, "hml_retain(&%s);", result);
            break;

        case EXPR_BINARY: {
            char *left = codegen_expr(ctx, expr->as.binary.left);
            char *right = codegen_expr(ctx, expr->as.binary.right);
            codegen_writeln(ctx, "HmlValue %s = hml_binary_op(%s, %s, %s);",
                          result, codegen_hml_binary_op(expr->as.binary.op), left, right);
            codegen_writeln(ctx, "hml_release(&%s);", left);
            codegen_writeln(ctx, "hml_release(&%s);", right);
            free(left);
            free(right);
            break;
        }

        case EXPR_UNARY: {
            char *operand = codegen_expr(ctx, expr->as.unary.operand);
            codegen_writeln(ctx, "HmlValue %s = hml_unary_op(%s, %s);",
                          result, codegen_hml_unary_op(expr->as.unary.op), operand);
            codegen_writeln(ctx, "hml_release(&%s);", operand);
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

                // Handle user-defined function by name (hml_fn_<name>)
                if (codegen_is_local(ctx, fn_name)) {
                    // It's a local function variable - call through hml_call_function
                    // Fall through to generic handling
                } else {
                    // Try to call as hml_fn_<name> directly
                    char **arg_temps = malloc(expr->as.call.num_args * sizeof(char*));
                    for (int i = 0; i < expr->as.call.num_args; i++) {
                        arg_temps[i] = codegen_expr(ctx, expr->as.call.args[i]);
                    }

                    codegen_write(ctx, "");
                    codegen_indent(ctx);
                    fprintf(ctx->output, "HmlValue %s = hml_fn_%s(", result, fn_name);
                    for (int i = 0; i < expr->as.call.num_args; i++) {
                        if (i > 0) fprintf(ctx->output, ", ");
                        fprintf(ctx->output, "%s", arg_temps[i]);
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

            // Generic function call handling
            char *func_val = codegen_expr(ctx, expr->as.call.func);

            // Generate code for arguments
            char **arg_temps = malloc(expr->as.call.num_args * sizeof(char*));
            for (int i = 0; i < expr->as.call.num_args; i++) {
                arg_temps[i] = codegen_expr(ctx, expr->as.call.args[i]);
            }

            // Build args array
            if (expr->as.call.num_args > 0) {
                codegen_writeln(ctx, "HmlValue _args%d[%d];", ctx->temp_counter, expr->as.call.num_args);
                for (int i = 0; i < expr->as.call.num_args; i++) {
                    codegen_writeln(ctx, "_args%d[%d] = %s;", ctx->temp_counter, i, arg_temps[i]);
                }
                codegen_writeln(ctx, "HmlValue %s = hml_call_function(%s, _args%d, %d);",
                              result, func_val, ctx->temp_counter, expr->as.call.num_args);
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
            char *value = codegen_expr(ctx, expr->as.assign.value);
            codegen_writeln(ctx, "hml_release(&%s);", expr->as.assign.name);
            codegen_writeln(ctx, "%s = %s;", expr->as.assign.name, value);
            codegen_writeln(ctx, "hml_retain(&%s);", expr->as.assign.name);
            codegen_writeln(ctx, "HmlValue %s = %s;", result, expr->as.assign.name);
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
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "%s = hml_object_get_field(%s, \"length\");", result, obj);
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
            codegen_writeln(ctx, "hml_release(&%s);", obj);
            codegen_writeln(ctx, "hml_release(&%s);", idx);
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
            // Generate anonymous function
            char *func_name = codegen_anon_func(ctx);
            // Note: In a full implementation, we'd generate the function separately
            // and capture closure variables. For MVP, we create a function pointer.
            codegen_writeln(ctx, "// TODO: Anonymous function %s", func_name);
            codegen_writeln(ctx, "HmlValue %s = hml_val_null(); // Function not yet supported", result);
            free(func_name);
            break;
        }

        case EXPR_PREFIX_INC: {
            // ++x is equivalent to x = x + 1, returns new value
            if (expr->as.prefix_inc.operand->type == EXPR_IDENT) {
                const char *var = expr->as.prefix_inc.operand->as.ident;
                codegen_writeln(ctx, "%s = hml_binary_op(HML_OP_ADD, %s, hml_val_i32(1));", var, var);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, var);
                codegen_writeln(ctx, "hml_retain(&%s);", result);
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_val_null(); // Complex prefix inc not supported", result);
            }
            break;
        }

        case EXPR_PREFIX_DEC: {
            if (expr->as.prefix_dec.operand->type == EXPR_IDENT) {
                const char *var = expr->as.prefix_dec.operand->as.ident;
                codegen_writeln(ctx, "%s = hml_binary_op(HML_OP_SUB, %s, hml_val_i32(1));", var, var);
                codegen_writeln(ctx, "HmlValue %s = %s;", result, var);
                codegen_writeln(ctx, "hml_retain(&%s);", result);
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            }
            break;
        }

        case EXPR_POSTFIX_INC: {
            // x++ returns old value, then increments
            if (expr->as.postfix_inc.operand->type == EXPR_IDENT) {
                const char *var = expr->as.postfix_inc.operand->as.ident;
                codegen_writeln(ctx, "HmlValue %s = %s;", result, var);
                codegen_writeln(ctx, "hml_retain(&%s);", result);
                codegen_writeln(ctx, "%s = hml_binary_op(HML_OP_ADD, %s, hml_val_i32(1));", var, var);
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            }
            break;
        }

        case EXPR_POSTFIX_DEC: {
            if (expr->as.postfix_dec.operand->type == EXPR_IDENT) {
                const char *var = expr->as.postfix_dec.operand->as.ident;
                codegen_writeln(ctx, "HmlValue %s = %s;", result, var);
                codegen_writeln(ctx, "hml_retain(&%s);", result);
                codegen_writeln(ctx, "%s = hml_binary_op(HML_OP_SUB, %s, hml_val_i32(1));", var, var);
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
            // await expr is syntactic sugar for join(expr)
            char *task = codegen_expr(ctx, expr->as.await_expr.awaited_expr);
            codegen_writeln(ctx, "HmlValue %s = hml_join(%s);", result, task);
            codegen_writeln(ctx, "hml_release(&%s);", task);
            free(task);
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

        default:
            codegen_writeln(ctx, "HmlValue %s = hml_val_null(); // Unsupported expression type %d", result, expr->type);
            break;
    }

    return result;
}

// ========== STATEMENT CODE GENERATION ==========

void codegen_stmt(CodegenContext *ctx, Stmt *stmt) {
    switch (stmt->type) {
        case STMT_LET: {
            codegen_add_local(ctx, stmt->as.let.name);
            if (stmt->as.let.value) {
                char *value = codegen_expr(ctx, stmt->as.let.value);
                codegen_writeln(ctx, "HmlValue %s = %s;", stmt->as.let.name, value);
                free(value);
            } else {
                codegen_writeln(ctx, "HmlValue %s = hml_val_null();", stmt->as.let.name);
            }
            break;
        }

        case STMT_CONST: {
            codegen_add_local(ctx, stmt->as.const_stmt.name);
            if (stmt->as.const_stmt.value) {
                char *value = codegen_expr(ctx, stmt->as.const_stmt.value);
                codegen_writeln(ctx, "const HmlValue %s = %s;", stmt->as.const_stmt.name, value);
                free(value);
            } else {
                codegen_writeln(ctx, "const HmlValue %s = hml_val_null();", stmt->as.const_stmt.name);
            }
            break;
        }

        case STMT_EXPR: {
            char *value = codegen_expr(ctx, stmt->as.expr);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(value);
            break;
        }

        case STMT_IF: {
            char *cond = codegen_expr(ctx, stmt->as.if_stmt.condition);
            codegen_writeln(ctx, "if (hml_to_bool(%s)) {", cond);
            codegen_indent_inc(ctx);
            codegen_stmt(ctx, stmt->as.if_stmt.then_branch);
            codegen_indent_dec(ctx);
            if (stmt->as.if_stmt.else_branch) {
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_stmt(ctx, stmt->as.if_stmt.else_branch);
                codegen_indent_dec(ctx);
            }
            codegen_writeln(ctx, "}");
            codegen_writeln(ctx, "hml_release(&%s);", cond);
            free(cond);
            break;
        }

        case STMT_WHILE: {
            codegen_writeln(ctx, "while (1) {");
            codegen_indent_inc(ctx);
            char *cond = codegen_expr(ctx, stmt->as.while_stmt.condition);
            codegen_writeln(ctx, "if (!hml_to_bool(%s)) { hml_release(&%s); break; }", cond, cond);
            codegen_writeln(ctx, "hml_release(&%s);", cond);
            codegen_stmt(ctx, stmt->as.while_stmt.body);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            free(cond);
            break;
        }

        case STMT_FOR: {
            codegen_writeln(ctx, "{");
            codegen_indent_inc(ctx);
            // Initializer
            if (stmt->as.for_loop.initializer) {
                codegen_stmt(ctx, stmt->as.for_loop.initializer);
            }
            codegen_writeln(ctx, "while (1) {");
            codegen_indent_inc(ctx);
            // Condition
            if (stmt->as.for_loop.condition) {
                char *cond = codegen_expr(ctx, stmt->as.for_loop.condition);
                codegen_writeln(ctx, "if (!hml_to_bool(%s)) { hml_release(&%s); break; }", cond, cond);
                codegen_writeln(ctx, "hml_release(&%s);", cond);
                free(cond);
            }
            // Body
            codegen_stmt(ctx, stmt->as.for_loop.body);
            // Increment
            if (stmt->as.for_loop.increment) {
                char *inc = codegen_expr(ctx, stmt->as.for_loop.increment);
                codegen_writeln(ctx, "hml_release(&%s);", inc);
                free(inc);
            }
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            break;
        }

        case STMT_BLOCK: {
            codegen_writeln(ctx, "{");
            codegen_indent_inc(ctx);
            for (int i = 0; i < stmt->as.block.count; i++) {
                codegen_stmt(ctx, stmt->as.block.statements[i]);
            }
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            break;
        }

        case STMT_RETURN: {
            if (stmt->as.return_stmt.value) {
                char *value = codegen_expr(ctx, stmt->as.return_stmt.value);
                codegen_writeln(ctx, "return %s;", value);
                free(value);
            } else {
                codegen_writeln(ctx, "return hml_val_null();");
            }
            break;
        }

        case STMT_BREAK:
            codegen_writeln(ctx, "break;");
            break;

        case STMT_CONTINUE:
            codegen_writeln(ctx, "continue;");
            break;

        case STMT_TRY: {
            codegen_writeln(ctx, "{");
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "HmlExceptionContext *_ex_ctx = hml_exception_push();");
            codegen_writeln(ctx, "if (setjmp(_ex_ctx->exception_buf) == 0) {");
            codegen_indent_inc(ctx);
            // Try block
            codegen_stmt(ctx, stmt->as.try_stmt.try_block);
            codegen_indent_dec(ctx);
            if (stmt->as.try_stmt.catch_block) {
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                // Catch block
                if (stmt->as.try_stmt.catch_param) {
                    codegen_writeln(ctx, "HmlValue %s = hml_exception_get_value();", stmt->as.try_stmt.catch_param);
                }
                codegen_stmt(ctx, stmt->as.try_stmt.catch_block);
                if (stmt->as.try_stmt.catch_param) {
                    codegen_writeln(ctx, "hml_release(&%s);", stmt->as.try_stmt.catch_param);
                }
                codegen_indent_dec(ctx);
            }
            codegen_writeln(ctx, "}");
            // Finally block
            if (stmt->as.try_stmt.finally_block) {
                codegen_stmt(ctx, stmt->as.try_stmt.finally_block);
            }
            codegen_writeln(ctx, "hml_exception_pop();");
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            break;
        }

        case STMT_THROW: {
            char *value = codegen_expr(ctx, stmt->as.throw_stmt.value);
            codegen_writeln(ctx, "hml_throw(%s);", value);
            free(value);
            break;
        }

        case STMT_SWITCH: {
            char *expr_val = codegen_expr(ctx, stmt->as.switch_stmt.expr);
            int has_default = 0;
            int default_idx = -1;

            // Find default case
            for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
                if (stmt->as.switch_stmt.case_values[i] == NULL) {
                    has_default = 1;
                    default_idx = i;
                    break;
                }
            }

            codegen_writeln(ctx, "{");
            codegen_indent_inc(ctx);
            codegen_writeln(ctx, "int _matched = 0;");

            // Generate case comparisons
            for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
                if (stmt->as.switch_stmt.case_values[i] == NULL) continue;  // Skip default

                char *case_val = codegen_expr(ctx, stmt->as.switch_stmt.case_values[i]);
                if (i == 0) {
                    codegen_writeln(ctx, "if (hml_to_bool(hml_binary_op(HML_OP_EQUAL, %s, %s))) {", expr_val, case_val);
                } else {
                    codegen_writeln(ctx, "} else if (hml_to_bool(hml_binary_op(HML_OP_EQUAL, %s, %s))) {", expr_val, case_val);
                }
                codegen_indent_inc(ctx);
                codegen_writeln(ctx, "_matched = 1;");
                codegen_stmt(ctx, stmt->as.switch_stmt.case_bodies[i]);
                codegen_indent_dec(ctx);
                codegen_writeln(ctx, "hml_release(&%s);", case_val);
                free(case_val);
            }

            if (has_default) {
                codegen_writeln(ctx, "} else {");
                codegen_indent_inc(ctx);
                codegen_stmt(ctx, stmt->as.switch_stmt.case_bodies[default_idx]);
                codegen_indent_dec(ctx);
            }
            codegen_writeln(ctx, "}");

            codegen_writeln(ctx, "hml_release(&%s);", expr_val);
            codegen_indent_dec(ctx);
            codegen_writeln(ctx, "}");
            free(expr_val);
            break;
        }

        case STMT_DEFER: {
            // For now, just execute the expression - proper defer would need more work
            codegen_writeln(ctx, "// TODO: Proper defer support");
            char *value = codegen_expr(ctx, stmt->as.defer_stmt.call);
            codegen_writeln(ctx, "hml_release(&%s);", value);
            free(value);
            break;
        }

        default:
            codegen_writeln(ctx, "// Unsupported statement type %d", stmt->type);
            break;
    }
}

// ========== PROGRAM CODE GENERATION ==========

// Check if a statement is a function definition (let name = fn() {})
static int is_function_def(Stmt *stmt, char **name_out, Expr **func_out) {
    if (stmt->type == STMT_LET && stmt->as.let.value &&
        stmt->as.let.value->type == EXPR_FUNCTION) {
        *name_out = stmt->as.let.name;
        *func_out = stmt->as.let.value;
        return 1;
    }
    return 0;
}

// Generate a top-level function declaration
static void codegen_function_decl(CodegenContext *ctx, Expr *func, const char *name) {
    // Generate function signature
    codegen_write(ctx, "HmlValue hml_fn_%s(", name);
    for (int i = 0; i < func->as.function.num_params; i++) {
        if (i > 0) codegen_write(ctx, ", ");
        codegen_write(ctx, "HmlValue %s", func->as.function.param_names[i]);
    }
    codegen_write(ctx, ") {\n");
    codegen_indent_inc(ctx);

    // Save locals state
    int saved_num_locals = ctx->num_locals;

    // Add parameters as locals
    for (int i = 0; i < func->as.function.num_params; i++) {
        codegen_add_local(ctx, func->as.function.param_names[i]);
    }

    // Generate body
    if (func->as.function.body->type == STMT_BLOCK) {
        for (int i = 0; i < func->as.function.body->as.block.count; i++) {
            codegen_stmt(ctx, func->as.function.body->as.block.statements[i]);
        }
    } else {
        codegen_stmt(ctx, func->as.function.body);
    }

    // Default return null
    codegen_writeln(ctx, "return hml_val_null();");

    codegen_indent_dec(ctx);
    codegen_write(ctx, "}\n\n");

    // Restore locals state
    ctx->num_locals = saved_num_locals;
}

void codegen_program(CodegenContext *ctx, Stmt **stmts, int stmt_count) {
    // Header
    codegen_write(ctx, "/*\n");
    codegen_write(ctx, " * Generated by Hemlock Compiler\n");
    codegen_write(ctx, " */\n\n");
    codegen_write(ctx, "#include \"hemlock_runtime.h\"\n");
    codegen_write(ctx, "#include <setjmp.h>\n\n");

    // Forward declarations for functions
    codegen_write(ctx, "// Forward declarations\n");
    for (int i = 0; i < stmt_count; i++) {
        char *name;
        Expr *func;
        if (is_function_def(stmts[i], &name, &func)) {
            codegen_write(ctx, "HmlValue hml_fn_%s(", name);
            for (int j = 0; j < func->as.function.num_params; j++) {
                if (j > 0) codegen_write(ctx, ", ");
                codegen_write(ctx, "HmlValue %s", func->as.function.param_names[j]);
            }
            codegen_write(ctx, ");\n");
        }
    }
    codegen_write(ctx, "\n");

    // Generate function definitions
    for (int i = 0; i < stmt_count; i++) {
        char *name;
        Expr *func;
        if (is_function_def(stmts[i], &name, &func)) {
            codegen_function_decl(ctx, func, name);
        }
    }

    // Generate main function
    codegen_write(ctx, "int main(int argc, char **argv) {\n");
    codegen_indent_inc(ctx);
    codegen_writeln(ctx, "hml_runtime_init(argc, argv);");
    codegen_writeln(ctx, "");

    // Generate global variable declarations for functions (as function pointers)
    for (int i = 0; i < stmt_count; i++) {
        char *name;
        Expr *func;
        if (is_function_def(stmts[i], &name, &func)) {
            codegen_writeln(ctx, "HmlValue %s = hml_val_function((void*)hml_fn_%s, %d, %d);",
                          name, name, func->as.function.num_params, func->as.function.is_async);
            codegen_add_local(ctx, name);
        }
    }
    codegen_writeln(ctx, "");

    // Generate non-function statements
    for (int i = 0; i < stmt_count; i++) {
        char *name;
        Expr *func;
        if (!is_function_def(stmts[i], &name, &func)) {
            codegen_stmt(ctx, stmts[i]);
        }
    }

    codegen_writeln(ctx, "");
    codegen_writeln(ctx, "hml_runtime_cleanup();");
    codegen_writeln(ctx, "return 0;");
    codegen_indent_dec(ctx);
    codegen_write(ctx, "}\n");
}
