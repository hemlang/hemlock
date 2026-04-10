/*
 * Hemlock Code Generator - Method Call Dispatch
 *
 * Handles EXPR_PROP_ACCESS method calls: string methods, array methods,
 * file methods, socket methods, channel methods, serialization, and
 * fallback to hml_call_method.
 */

#include "codegen_call_internal.h"

int codegen_call_methods(CodegenContext *ctx, Expr *expr, char *result,
                         Expr *callee, Expr **call_args, int num_args) {
    (void)expr;
    if (callee->type != EXPR_GET_PROPERTY) {
        return 0;
    }

    Expr *obj_expr = callee->as.get_property.object;
    const char *method = callee->as.get_property.property;

    // Evaluate the object
    char *obj_val = codegen_expr(ctx, obj_expr);

    // Evaluate arguments
    char **arg_temps = num_args > 0
        ? malloc(num_args * sizeof(char*))
        : NULL;
    for (int i = 0; i < num_args; i++) {
        arg_temps[i] = codegen_expr(ctx, call_args[i]);
    }

    // Methods that work on both strings and arrays - need runtime type check
    if (strcmp(method, "slice") == 0 && (num_args == 1 || num_args == 2) && arg_temps) {
        codegen_writeln(ctx, "HmlValue %s;", result);
        if (num_args == 1) {
            // Single-arg slice: default end to length
            codegen_writeln(ctx, "if (%s.type == HML_VAL_STRING) {", obj_val);
            codegen_writeln(ctx, "    %s = hml_string_slice(%s, %s, hml_string_length(%s));",
                          result, obj_val, arg_temps[0], obj_val);
            codegen_writeln(ctx, "} else if (%s.type == HML_VAL_BUFFER) {", obj_val);
            codegen_writeln(ctx, "    %s = hml_buffer_slice(%s, %s, hml_val_i32(%s.as.as_buffer->length));",
                          result, obj_val, arg_temps[0], obj_val);
            codegen_writeln(ctx, "} else {");
            codegen_writeln(ctx, "    %s = hml_array_slice(%s, %s, hml_val_i32(%s.as.as_array->length));",
                          result, obj_val, arg_temps[0], obj_val);
            codegen_writeln(ctx, "}");
        } else {
            codegen_writeln(ctx, "if (%s.type == HML_VAL_STRING) {", obj_val);
            codegen_writeln(ctx, "    %s = hml_string_slice(%s, %s, %s);",
                          result, obj_val, arg_temps[0], arg_temps[1]);
            codegen_writeln(ctx, "} else if (%s.type == HML_VAL_BUFFER) {", obj_val);
            codegen_writeln(ctx, "    %s = hml_buffer_slice(%s, %s, %s);",
                          result, obj_val, arg_temps[0], arg_temps[1]);
            codegen_writeln(ctx, "} else {");
            codegen_writeln(ctx, "    %s = hml_array_slice(%s, %s, %s);",
                          result, obj_val, arg_temps[0], arg_temps[1]);
            codegen_writeln(ctx, "}");
        }
    } else if ((strcmp(method, "find") == 0 || strcmp(method, "indexOf") == 0)
               && num_args == 1) {
        codegen_writeln(ctx, "HmlValue %s;", result);
        codegen_writeln(ctx, "if (%s.type == HML_VAL_STRING) {", obj_val);
        codegen_writeln(ctx, "    %s = hml_string_find(%s, %s);",
                      result, obj_val, arg_temps[0]);
        codegen_writeln(ctx, "} else if (%s.type == HML_VAL_ARRAY) {", obj_val);
        codegen_writeln(ctx, "    %s = hml_array_find(%s, %s);",
                      result, obj_val, arg_temps[0]);
        codegen_writeln(ctx, "} else {");
        codegen_writeln(ctx, "    HmlValue _find_args__%s[1] = { %s };", result, arg_temps[0]);
        codegen_writeln(ctx, "    %s = hml_call_method(%s, \"find\", _find_args__%s, 1);",
                      result, obj_val, result);
        codegen_writeln(ctx, "}");
    } else if (strcmp(method, "contains") == 0 && num_args == 1) {
        codegen_writeln(ctx, "HmlValue %s;", result);
        codegen_writeln(ctx, "if (%s.type == HML_VAL_STRING) {", obj_val);
        codegen_writeln(ctx, "    %s = hml_string_contains(%s, %s);",
                      result, obj_val, arg_temps[0]);
        codegen_writeln(ctx, "} else if (%s.type == HML_VAL_ARRAY) {", obj_val);
        codegen_writeln(ctx, "    %s = hml_array_contains(%s, %s);",
                      result, obj_val, arg_temps[0]);
        codegen_writeln(ctx, "} else {");
        codegen_writeln(ctx, "    HmlValue _contains_args__%s[1] = { %s };", result, arg_temps[0]);
        codegen_writeln(ctx, "    %s = hml_call_method(%s, \"contains\", _contains_args__%s, 1);",
                      result, obj_val, result);
        codegen_writeln(ctx, "}");
    // String-only methods
    } else if (strcmp(method, "substr") == 0 && num_args == 2) {
        codegen_writeln(ctx, "HmlValue %s = hml_string_substr(%s, %s, %s);",
                      result, obj_val, arg_temps[0], arg_temps[1]);
    } else if (strcmp(method, "split") == 0 && num_args == 1) {
        codegen_writeln(ctx, "HmlValue %s = hml_string_split(%s, %s);",
                      result, obj_val, arg_temps[0]);
    } else if (strcmp(method, "trim") == 0 && num_args == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_string_trim(%s);", result, obj_val);
    } else if (strcmp(method, "trim_start") == 0 && num_args == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_string_trim_start(%s);", result, obj_val);
    } else if (strcmp(method, "trim_end") == 0 && num_args == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_string_trim_end(%s);", result, obj_val);
    } else if (strcmp(method, "to_upper") == 0 && num_args == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_string_to_upper(%s);", result, obj_val);
    } else if (strcmp(method, "to_lower") == 0 && num_args == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_string_to_lower(%s);", result, obj_val);
    } else if (strcmp(method, "starts_with") == 0 && num_args == 1) {
        codegen_writeln(ctx, "HmlValue %s = hml_string_starts_with(%s, %s);",
                      result, obj_val, arg_temps[0]);
    } else if (strcmp(method, "ends_with") == 0 && num_args == 1) {
        codegen_writeln(ctx, "HmlValue %s = hml_string_ends_with(%s, %s);",
                      result, obj_val, arg_temps[0]);
    } else if (strcmp(method, "replace") == 0 && num_args == 2) {
        codegen_writeln(ctx, "HmlValue %s = hml_string_replace(%s, %s, %s);",
                      result, obj_val, arg_temps[0], arg_temps[1]);
    } else if (strcmp(method, "replace_all") == 0 && num_args == 2) {
        codegen_writeln(ctx, "HmlValue %s = hml_string_replace_all(%s, %s, %s);",
                      result, obj_val, arg_temps[0], arg_temps[1]);
    } else if (strcmp(method, "repeat") == 0 && num_args == 1) {
        codegen_writeln(ctx, "HmlValue %s = hml_string_repeat(%s, %s);",
                      result, obj_val, arg_temps[0]);
    } else if (strcmp(method, "char_at") == 0 && num_args == 1) {
        codegen_writeln(ctx, "HmlValue %s = hml_string_char_at(%s, %s);",
                      result, obj_val, arg_temps[0]);
    } else if (strcmp(method, "byte_at") == 0 && num_args == 1) {
        codegen_writeln(ctx, "HmlValue %s = hml_string_byte_at(%s, %s);",
                      result, obj_val, arg_temps[0]);
    } else if (strcmp(method, "byte_ptr") == 0 && num_args == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_string_byte_ptr(%s);",
                      result, obj_val);
    } else if (strcmp(method, "to_bytes") == 0 && num_args == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_string_to_bytes(%s);",
                      result, obj_val);
    // Array methods - with runtime type check to also support object methods
    } else if (strcmp(method, "push") == 0 && num_args == 1) {
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
    } else if (strcmp(method, "pop") == 0 && num_args == 0) {
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
    } else if (strcmp(method, "shift") == 0 && num_args == 0) {
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
    } else if (strcmp(method, "unshift") == 0 && num_args == 1) {
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
    } else if (strcmp(method, "insert") == 0 && num_args == 2) {
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
    } else if (strcmp(method, "remove") == 0 && num_args == 1) {
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
    } else if (strcmp(method, "join") == 0 && num_args == 1) {
        codegen_writeln(ctx, "HmlValue %s = hml_array_join(%s, %s);",
                      result, obj_val, arg_temps[0]);
    } else if (strcmp(method, "concat") == 0 && num_args == 1) {
        codegen_writeln(ctx, "HmlValue %s = hml_array_concat(%s, %s);",
                      result, obj_val, arg_temps[0]);
    } else if (strcmp(method, "reverse") == 0 && num_args == 0) {
        codegen_writeln(ctx, "hml_array_reverse(%s);", obj_val);
        codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
    } else if (strcmp(method, "first") == 0 && num_args == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_array_first(%s);", result, obj_val);
    } else if (strcmp(method, "last") == 0 && num_args == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_array_last(%s);", result, obj_val);
    } else if (strcmp(method, "clear") == 0 && num_args == 0) {
        codegen_writeln(ctx, "HmlValue %s;", result);
        codegen_writeln(ctx, "if (%s.type == HML_VAL_ARRAY) {", obj_val);
        codegen_indent_inc(ctx);
        codegen_writeln(ctx, "hml_array_clear(%s);", obj_val);
        codegen_writeln(ctx, "%s = hml_val_null();", result);
        codegen_indent_dec(ctx);
        codegen_writeln(ctx, "} else {");
        codegen_indent_inc(ctx);
        codegen_writeln(ctx, "%s = hml_call_method(%s, \"clear\", NULL, 0);", result, obj_val);
        codegen_indent_dec(ctx);
        codegen_writeln(ctx, "}");
    // File methods (with fallback to hml_call_method for generic objects)
    } else if (strcmp(method, "read") == 0 && (num_args == 0 || num_args == 1)) {
        codegen_writeln(ctx, "HmlValue %s;", result);
        codegen_writeln(ctx, "if (%s.type == HML_VAL_FILE) {", obj_val);
        if (num_args == 1) {
            codegen_writeln(ctx, "    %s = hml_file_read(%s, %s);",
                          result, obj_val, arg_temps[0]);
        } else {
            codegen_writeln(ctx, "    %s = hml_file_read_all(%s);", result, obj_val);
        }
        codegen_writeln(ctx, "} else {");
        if (num_args == 1) {
            codegen_writeln(ctx, "    HmlValue _read_args[1] = {%s};", arg_temps[0]);
            codegen_writeln(ctx, "    %s = hml_call_method(%s, \"read\", _read_args, 1);", result, obj_val);
        } else {
            codegen_writeln(ctx, "    %s = hml_call_method(%s, \"read\", NULL, 0);", result, obj_val);
        }
        codegen_writeln(ctx, "}");
    } else if (strcmp(method, "write") == 0 && num_args == 1) {
        codegen_writeln(ctx, "HmlValue %s;", result);
        codegen_writeln(ctx, "if (%s.type == HML_VAL_FILE) {", obj_val);
        codegen_writeln(ctx, "    %s = hml_file_write(%s, %s);",
                      result, obj_val, arg_temps[0]);
        codegen_writeln(ctx, "} else {");
        codegen_writeln(ctx, "    HmlValue _write_args[1] = {%s};", arg_temps[0]);
        codegen_writeln(ctx, "    %s = hml_call_method(%s, \"write\", _write_args, 1);", result, obj_val);
        codegen_writeln(ctx, "}");
    } else if (strcmp(method, "seek") == 0 && num_args == 1) {
        codegen_writeln(ctx, "HmlValue %s = hml_file_seek(%s, %s);",
                      result, obj_val, arg_temps[0]);
    } else if (strcmp(method, "tell") == 0 && num_args == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_file_tell(%s);", result, obj_val);
    } else if (strcmp(method, "close") == 0 && num_args == 0) {
        // Handle file.close(), channel.close(), socket.close(), and generic object.close()
        codegen_writeln(ctx, "if (%s.type == HML_VAL_FILE) {", obj_val);
        codegen_writeln(ctx, "    hml_file_close(%s);", obj_val);
        codegen_writeln(ctx, "} else if (%s.type == HML_VAL_CHANNEL) {", obj_val);
        codegen_writeln(ctx, "    hml_channel_close(%s);", obj_val);
        codegen_writeln(ctx, "} else if (%s.type == HML_VAL_SOCKET) {", obj_val);
        codegen_writeln(ctx, "    hml_socket_close(%s);", obj_val);
        codegen_writeln(ctx, "} else {");
        codegen_writeln(ctx, "    hml_call_method(%s, \"close\", NULL, 0);", obj_val);
        codegen_writeln(ctx, "}");
        codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
    } else if (strcmp(method, "map") == 0 && num_args == 1) {
        codegen_writeln(ctx, "HmlValue %s = hml_array_map(%s, %s);",
                      result, obj_val, arg_temps[0]);
    } else if (strcmp(method, "filter") == 0 && num_args == 1) {
        codegen_writeln(ctx, "HmlValue %s = hml_array_filter(%s, %s);",
                      result, obj_val, arg_temps[0]);
    } else if (strcmp(method, "reduce") == 0 && (num_args == 1 || num_args == 2)) {
        if (num_args == 2) {
            codegen_writeln(ctx, "HmlValue %s = hml_array_reduce(%s, %s, %s);",
                          result, obj_val, arg_temps[0], arg_temps[1]);
        } else {
            // No initial value - use first element
            codegen_writeln(ctx, "HmlValue %s = hml_array_reduce(%s, %s, hml_val_null());",
                          result, obj_val, arg_temps[0]);
        }
    } else if (strcmp(method, "every") == 0 && num_args == 1) {
        codegen_writeln(ctx, "HmlValue %s = hml_array_every(%s, %s);",
                      result, obj_val, arg_temps[0]);
    } else if (strcmp(method, "some") == 0 && num_args == 1) {
        codegen_writeln(ctx, "HmlValue %s = hml_array_some(%s, %s);",
                      result, obj_val, arg_temps[0]);
    } else if (strcmp(method, "indexOf") == 0 && num_args == 1) {
        codegen_writeln(ctx, "HmlValue %s = hml_array_index_of(%s, %s);",
                      result, obj_val, arg_temps[0]);
    } else if (strcmp(method, "lastIndexOf") == 0 && num_args == 1) {
        codegen_writeln(ctx, "HmlValue %s = hml_array_last_index_of(%s, %s);",
                      result, obj_val, arg_temps[0]);
    } else if (strcmp(method, "findIndex") == 0 && num_args == 1) {
        codegen_writeln(ctx, "HmlValue %s = hml_array_find_index(%s, %s);",
                      result, obj_val, arg_temps[0]);
    } else if (strcmp(method, "flat") == 0 && num_args == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_array_flat(%s);", result, obj_val);
    } else if (strcmp(method, "sort") == 0 && (num_args == 0 || num_args == 1)) {
        if (num_args == 1) {
            codegen_writeln(ctx, "hml_array_sort(%s, %s);", obj_val, arg_temps[0]);
        } else {
            codegen_writeln(ctx, "hml_array_sort(%s, hml_val_null());", obj_val);
        }
        codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
    } else if (strcmp(method, "reserve") == 0 && num_args == 1) {
        codegen_writeln(ctx, "hml_array_reserve(%s, %s);", obj_val, arg_temps[0]);
        codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
    } else if (strcmp(method, "fill") == 0 && (num_args >= 1 && num_args <= 3)) {
        if (num_args == 3) {
            codegen_writeln(ctx, "hml_array_fill(%s, %s, %s, %s);",
                          obj_val, arg_temps[0], arg_temps[1], arg_temps[2]);
        } else if (num_args == 2) {
            codegen_writeln(ctx, "hml_array_fill(%s, %s, %s, hml_val_null());",
                          obj_val, arg_temps[0], arg_temps[1]);
        } else {
            codegen_writeln(ctx, "hml_array_fill(%s, %s, hml_val_null(), hml_val_null());",
                          obj_val, arg_temps[0]);
        }
        codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
    // Channel methods (also handle socket variants)
    } else if (strcmp(method, "send") == 0 && num_args == 1) {
        // Channel send, socket send, or generic object send
        codegen_writeln(ctx, "HmlValue %s;", result);
        codegen_writeln(ctx, "if (%s.type == HML_VAL_CHANNEL) {", obj_val);
        codegen_writeln(ctx, "    hml_channel_send(%s, %s);", obj_val, arg_temps[0]);
        codegen_writeln(ctx, "    %s = hml_val_null();", result);
        codegen_writeln(ctx, "} else if (%s.type == HML_VAL_SOCKET) {", obj_val);
        codegen_writeln(ctx, "    %s = hml_socket_send(%s, %s);", result, obj_val, arg_temps[0]);
        codegen_writeln(ctx, "} else {");
        codegen_writeln(ctx, "    HmlValue _send_args[1] = {%s};", arg_temps[0]);
        codegen_writeln(ctx, "    %s = hml_call_method(%s, \"send\", _send_args, 1);", result, obj_val);
        codegen_writeln(ctx, "}");
    } else if (strcmp(method, "recv") == 0) {
        // Channel recv (no args), socket recv (1 arg for size), or generic object recv
        codegen_writeln(ctx, "HmlValue %s;", result);
        if (num_args == 0) {
            codegen_writeln(ctx, "if (%s.type == HML_VAL_CHANNEL) {", obj_val);
            codegen_writeln(ctx, "    %s = hml_channel_recv(%s);", result, obj_val);
            codegen_writeln(ctx, "} else {");
            codegen_writeln(ctx, "    %s = hml_call_method(%s, \"recv\", NULL, 0);", result, obj_val);
            codegen_writeln(ctx, "}");
        } else if (arg_temps) {
            codegen_writeln(ctx, "if (%s.type == HML_VAL_SOCKET) {", obj_val);
            codegen_writeln(ctx, "    %s = hml_socket_recv(%s, %s);", result, obj_val, arg_temps[0]);
            codegen_writeln(ctx, "} else {");
            codegen_writeln(ctx, "    HmlValue _recv_args[1] = {%s};", arg_temps[0]);
            codegen_writeln(ctx, "    %s = hml_call_method(%s, \"recv\", _recv_args, 1);", result, obj_val);
            codegen_writeln(ctx, "}");
        }
    // Socket-specific methods
    } else if (strcmp(method, "bind") == 0 && num_args == 2) {
        codegen_writeln(ctx, "hml_socket_bind(%s, %s, %s);", obj_val, arg_temps[0], arg_temps[1]);
        codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
    } else if (strcmp(method, "listen") == 0 && num_args == 1) {
        codegen_writeln(ctx, "hml_socket_listen(%s, %s);", obj_val, arg_temps[0]);
        codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
    } else if (strcmp(method, "accept") == 0 && num_args == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_socket_accept(%s);", result, obj_val);
    } else if (strcmp(method, "connect") == 0 && num_args == 2) {
        codegen_writeln(ctx, "hml_socket_connect(%s, %s, %s);", obj_val, arg_temps[0], arg_temps[1]);
        codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
    } else if (strcmp(method, "sendto") == 0 && num_args == 3) {
        codegen_writeln(ctx, "HmlValue %s = hml_socket_sendto(%s, %s, %s, %s);",
                      result, obj_val, arg_temps[0], arg_temps[1], arg_temps[2]);
    } else if (strcmp(method, "recvfrom") == 0 && num_args == 1) {
        codegen_writeln(ctx, "HmlValue %s = hml_socket_recvfrom(%s, %s);",
                      result, obj_val, arg_temps[0]);
    } else if (strcmp(method, "setsockopt") == 0 && num_args == 3) {
        codegen_writeln(ctx, "hml_socket_setsockopt(%s, %s, %s, %s);",
                      obj_val, arg_temps[0], arg_temps[1], arg_temps[2]);
        codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
    } else if (strcmp(method, "set_timeout") == 0 && num_args == 1) {
        codegen_writeln(ctx, "hml_socket_set_timeout(%s, %s);",
                      obj_val, arg_temps[0]);
        codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
    } else if (strcmp(method, "set_nonblocking") == 0 && num_args == 1) {
        codegen_writeln(ctx, "hml_socket_set_nonblocking(%s, %s);",
                      obj_val, arg_temps[0]);
        codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
    } else if (strcmp(method, "recv_timeout") == 0 && num_args == 1) {
        codegen_writeln(ctx, "HmlValue %s = hml_channel_recv_timeout(%s, %s);",
                      result, obj_val, arg_temps[0]);
    } else if (strcmp(method, "send_timeout") == 0 && num_args == 2) {
        codegen_writeln(ctx, "HmlValue %s = hml_channel_send_timeout(%s, %s, %s);",
                      result, obj_val, arg_temps[0], arg_temps[1]);
    // Buffer typed write methods (2 args: offset, value)
    } else if (strncmp(method, "write_", 6) == 0 && num_args == 2 &&
               (strcmp(method + 6, "u8") == 0 || strcmp(method + 6, "i8") == 0 ||
                strcmp(method + 6, "u16_le") == 0 || strcmp(method + 6, "u16_be") == 0 ||
                strcmp(method + 6, "i16_le") == 0 || strcmp(method + 6, "i16_be") == 0 ||
                strcmp(method + 6, "u32_le") == 0 || strcmp(method + 6, "u32_be") == 0 ||
                strcmp(method + 6, "i32_le") == 0 || strcmp(method + 6, "i32_be") == 0 ||
                strcmp(method + 6, "i64_le") == 0 || strcmp(method + 6, "i64_be") == 0 ||
                strcmp(method + 6, "u64_le") == 0 || strcmp(method + 6, "u64_be") == 0 ||
                strcmp(method + 6, "f32_le") == 0 || strcmp(method + 6, "f32_be") == 0 ||
                strcmp(method + 6, "f64_le") == 0 || strcmp(method + 6, "f64_be") == 0)) {
        codegen_writeln(ctx, "hml_buffer_%s(%s, %s, %s);", method, obj_val, arg_temps[0], arg_temps[1]);
        codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
    // Buffer write_bytes (3 args: offset, src_buffer, len)
    } else if (strcmp(method, "write_bytes") == 0 && num_args == 3) {
        codegen_writeln(ctx, "hml_buffer_write_bytes(%s, %s, %s, %s);",
                      obj_val, arg_temps[0], arg_temps[1], arg_temps[2]);
        codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
    // Buffer typed read methods (1 arg: offset)
    } else if (strncmp(method, "read_", 5) == 0 && num_args == 1 &&
               (strcmp(method + 5, "u8") == 0 || strcmp(method + 5, "i8") == 0 ||
                strcmp(method + 5, "u16_le") == 0 || strcmp(method + 5, "u16_be") == 0 ||
                strcmp(method + 5, "i16_le") == 0 || strcmp(method + 5, "i16_be") == 0 ||
                strcmp(method + 5, "u32_le") == 0 || strcmp(method + 5, "u32_be") == 0 ||
                strcmp(method + 5, "i32_le") == 0 || strcmp(method + 5, "i32_be") == 0 ||
                strcmp(method + 5, "i64_le") == 0 || strcmp(method + 5, "i64_be") == 0 ||
                strcmp(method + 5, "u64_le") == 0 || strcmp(method + 5, "u64_be") == 0 ||
                strcmp(method + 5, "f32_le") == 0 || strcmp(method + 5, "f32_be") == 0 ||
                strcmp(method + 5, "f64_le") == 0 || strcmp(method + 5, "f64_be") == 0)) {
        codegen_writeln(ctx, "HmlValue %s = hml_buffer_%s(%s, %s);",
                      result, method, obj_val, arg_temps[0]);
    // Buffer read_bytes (2 args: offset, len)
    } else if (strcmp(method, "read_bytes") == 0 && num_args == 2) {
        codegen_writeln(ctx, "HmlValue %s = hml_buffer_read_bytes(%s, %s, %s);",
                      result, obj_val, arg_temps[0], arg_temps[1]);
    // Serialization methods
    } else if (strcmp(method, "serialize") == 0 && num_args == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_serialize(%s);", result, obj_val);
    } else if (strcmp(method, "deserialize") == 0 && num_args == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_deserialize(%s);", result, obj_val);
    } else {
        // Unknown built-in method - try as object method call
        if (num_args > 0) {
            codegen_writeln(ctx, "HmlValue _method_args%d[%d];", ctx->temp_counter, num_args);
            for (int i = 0; i < num_args; i++) {
                codegen_writeln(ctx, "_method_args%d[%d] = %s;", ctx->temp_counter, i, arg_temps[i]);
            }
            codegen_writeln(ctx, "HmlValue %s = hml_call_method(%s, \"%s\", _method_args%d, %d);",
                          result, obj_val, method, ctx->temp_counter, num_args);
            ctx->temp_counter++;
        } else {
            codegen_writeln(ctx, "HmlValue %s = hml_call_method(%s, \"%s\", NULL, 0);",
                          result, obj_val, method);
        }
    }

    // Release temporaries
    codegen_writeln(ctx, "hml_release(&%s);", obj_val);
    for (int i = 0; i < num_args; i++) {
        codegen_writeln(ctx, "hml_release(&%s);", arg_temps[i]);
        free(arg_temps[i]);
    }
    free(arg_temps);
    free(obj_val);
    return 1;
}
