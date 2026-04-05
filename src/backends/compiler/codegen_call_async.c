/*
 * Hemlock Code Generator - Async/Concurrency Call Builtins
 *
 * Handles: spawn, spawn_with, join, detach, channel, task_debug_info,
 * __get_default_stack_size, __set_default_stack_size, select, poll
 */

#include "codegen_call_internal.h"

int codegen_call_async(CodegenContext *ctx, Expr *expr, char *result,
                       const char *func_name, Expr **call_args, int num_args) {
    (void)expr;

    // Handle spawn builtin for async
    if (strcmp(func_name, "spawn") == 0 && num_args >= 1) {
        char *fn_val = codegen_expr(ctx, call_args[0]);
        int num_spawn_args = num_args - 1;

        if (num_spawn_args > 0) {
            // Capture counter before generating args (which may increment it)
            int args_counter = ctx->temp_counter++;
            // Build args array
            codegen_writeln(ctx, "HmlValue _spawn_args%d[%d];", args_counter, num_spawn_args);
            for (int i = 0; i < num_spawn_args; i++) {
                char *arg = codegen_expr(ctx, call_args[i + 1]);
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
        return 1;
    }

    // Handle spawn_with builtin for async with options
    if (strcmp(func_name, "spawn_with") == 0 && num_args >= 2) {
        char *opts_val = codegen_expr(ctx, call_args[0]);
        char *fn_val = codegen_expr(ctx, call_args[1]);
        int num_spawn_args = num_args - 2;

        if (num_spawn_args > 0) {
            int args_counter = ctx->temp_counter++;
            codegen_writeln(ctx, "HmlValue _spawn_with_args%d[%d];", args_counter, num_spawn_args);
            for (int i = 0; i < num_spawn_args; i++) {
                char *arg = codegen_expr(ctx, call_args[i + 2]);
                codegen_writeln(ctx, "_spawn_with_args%d[%d] = %s;", args_counter, i, arg);
                free(arg);
            }
            codegen_writeln(ctx, "HmlValue %s = hml_spawn_with(%s, %s, _spawn_with_args%d, %d);",
                          result, opts_val, fn_val, args_counter, num_spawn_args);
        } else {
            codegen_writeln(ctx, "HmlValue %s = hml_spawn_with(%s, %s, NULL, 0);",
                          result, opts_val, fn_val);
        }
        codegen_writeln(ctx, "hml_release(&%s);", opts_val);
        codegen_writeln(ctx, "hml_release(&%s);", fn_val);
        free(opts_val);
        free(fn_val);
        return 1;
    }

    // Handle __get_default_stack_size builtin
    if (strcmp(func_name, "__get_default_stack_size") == 0 && num_args == 0) {
        codegen_writeln(ctx, "HmlValue %s = hml_get_default_stack_size();", result);
        return 1;
    }

    // Handle __set_default_stack_size builtin
    if (strcmp(func_name, "__set_default_stack_size") == 0 && num_args == 1) {
        char *size = codegen_expr(ctx, call_args[0]);
        codegen_writeln(ctx, "hml_set_default_stack_size(%s);", size);
        codegen_writeln(ctx, "hml_release(&%s);", size);
        codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
        free(size);
        return 1;
    }

    // Handle join builtin
    if (strcmp(func_name, "join") == 0 && num_args == 1) {
        char *task_val = codegen_expr(ctx, call_args[0]);
        codegen_writeln(ctx, "HmlValue %s = hml_join(%s);", result, task_val);
        codegen_writeln(ctx, "hml_release(&%s);", task_val);
        free(task_val);
        return 1;
    }

    // Handle detach builtin
    // detach(task) - detach an already-spawned task
    // detach(fn, args...) - spawn and immediately detach (fire-and-forget)
    if (strcmp(func_name, "detach") == 0 && num_args >= 1) {
        if (num_args == 1) {
            // detach(task) - existing behavior
            char *task_val = codegen_expr(ctx, call_args[0]);
            codegen_writeln(ctx, "hml_detach(%s);", task_val);
            codegen_writeln(ctx, "hml_release(&%s);", task_val);
            codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
            free(task_val);
        } else {
            // detach(fn, args...) - spawn and detach
            char *fn_val = codegen_expr(ctx, call_args[0]);
            int num_spawn_args = num_args - 1;
            int args_counter = ctx->temp_counter++;

            // Build args array
            codegen_writeln(ctx, "HmlValue _detach_args%d[%d];", args_counter, num_spawn_args);
            for (int i = 0; i < num_spawn_args; i++) {
                char *arg = codegen_expr(ctx, call_args[i + 1]);
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
        return 1;
    }

    // Handle task_debug_info builtin
    if ((strcmp(func_name, "task_debug_info") == 0 || strcmp(func_name, "__task_debug_info") == 0) && num_args == 1) {
        char *task_val = codegen_expr(ctx, call_args[0]);
        codegen_writeln(ctx, "hml_task_debug_info(%s);", task_val);
        codegen_writeln(ctx, "hml_release(&%s);", task_val);
        codegen_writeln(ctx, "HmlValue %s = hml_val_null();", result);
        free(task_val);
        return 1;
    }

    // Handle channel builtin
    if (strcmp(func_name, "channel") == 0 && num_args == 1) {
        char *cap = codegen_expr(ctx, call_args[0]);
        codegen_writeln(ctx, "HmlValue %s = hml_channel(%s.as.as_i32);", result, cap);
        codegen_writeln(ctx, "hml_release(&%s);", cap);
        free(cap);
        return 1;
    }

    // Handle select(channels, timeout_ms?) - wait on multiple channels
    if (strcmp(func_name, "select") == 0 && (num_args == 1 || num_args == 2)) {
        char *channels = codegen_expr(ctx, call_args[0]);
        if (num_args == 2) {
            char *timeout = codegen_expr(ctx, call_args[1]);
            codegen_writeln(ctx, "HmlValue %s = hml_select(%s, %s);", result, channels, timeout);
            codegen_writeln(ctx, "hml_release(&%s);", timeout);
            free(timeout);
        } else {
            codegen_writeln(ctx, "HmlValue %s = hml_select(%s, hml_val_null());", result, channels);
        }
        codegen_writeln(ctx, "hml_release(&%s);", channels);
        free(channels);
        return 1;
    }

    // Handle poll(fds, timeout_ms) - wait for I/O events
    if (strcmp(func_name, "__poll") == 0 && num_args == 2) {
        char *fds = codegen_expr(ctx, call_args[0]);
        char *timeout = codegen_expr(ctx, call_args[1]);
        codegen_writeln(ctx, "HmlValue %s = hml_poll(%s, %s);", result, fds, timeout);
        codegen_writeln(ctx, "hml_release(&%s);", fds);
        codegen_writeln(ctx, "hml_release(&%s);", timeout);
        free(fds);
        free(timeout);
        return 1;
    }

    return 0;
}
