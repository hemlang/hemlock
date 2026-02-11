#include "internal.h"

// Global signal handler table (signal number -> Hemlock function)
Function *signal_handlers[MAX_SIGNAL] = {NULL};

// C signal handler that invokes Hemlock functions
static void hemlock_signal_handler(int signum) {
    if (signum < 0 || signum >= MAX_SIGNAL) {
        return;
    }

    Function *handler = signal_handlers[signum];
    if (handler == NULL) {
        return;
    }

    // Create execution context for signal handler
    ExecutionContext *ctx = exec_context_new();

    // Create environment for handler (use handler's closure environment as parent)
    Environment *func_env = env_new(handler->closure_env);

    // Signal handlers take one argument: the signal number
    Value sig_val = val_i32(signum);
    if (handler->num_params > 0) {
        env_define(func_env, handler->param_names[0], sig_val, 0, ctx);
    }

    // Execute handler body
    eval_stmt(handler->body, func_env, ctx);

    // Cleanup
    env_release(func_env);
    exec_context_free(ctx);

#ifdef HML_WINDOWS
    // Windows requires re-registering the signal handler after each invocation
    signal(signum, hemlock_signal_handler);
#endif
}

Value builtin_signal(Value *args, int num_args, ExecutionContext *ctx) {
    // SANDBOX: Check if signal operations are allowed
    if (sandbox_is_restricted(ctx, HML_SANDBOX_RESTRICT_SIGNALS)) {
        sandbox_error(ctx, "signal handler registration");
        return val_null();
    }

    if (num_args != 2) {
        runtime_error(ctx, "signal() expects 2 arguments (signum, handler), got %d", num_args);
        return val_null();
    }

    if (!is_integer(args[0])) {
        runtime_error(ctx, "signal() signum must be an integer, got %s", value_type_name(args[0].type));
        return val_null();
    }

    int32_t signum = value_to_int(args[0]);

    if (signum < 0 || signum >= MAX_SIGNAL) {
        runtime_error(ctx, "signal() signum %d out of range [0, %d)", signum, MAX_SIGNAL);
        return val_null();
    }

    // Check if handler is null (reset to default) or a function
    Function *new_handler = NULL;
    if (args[1].type != VAL_NULL) {
        if (args[1].type != VAL_FUNCTION) {
            runtime_error(ctx, "signal() handler must be a function or null, got %s", value_type_name(args[1].type));
            return val_null();
        }
        new_handler = args[1].as.as_function;
    }

    // Get previous handler (for return value)
    Function *prev_handler = signal_handlers[signum];
    Value prev_val = prev_handler ? val_function(prev_handler) : val_null();

    // Retain prev_val BEFORE we release it from signal_handlers
    // This keeps it alive for the caller
    if (prev_handler) {
        function_retain(prev_handler);
    }

    // Update handler table with proper refcounting
    if (prev_handler) {
        function_release(prev_handler);  // Release old handler from signal_handlers
    }
    signal_handlers[signum] = new_handler;
    if (new_handler) {
        function_retain(new_handler);  // Retain new handler in signal_handlers
    }

    // Install C signal handler or reset to default
    if (new_handler != NULL) {
#ifdef HML_WINDOWS
        // Windows uses simple signal() function
        if (signal(signum, hemlock_signal_handler) == SIG_ERR) {
            runtime_error(ctx, "signal() failed to install handler for signal %d: %s", signum, strerror(errno));
            return val_null();
        }
#else
        // POSIX uses sigaction for more reliable signal handling
        struct sigaction sa;
        sa.sa_handler = hemlock_signal_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;  // Restart syscalls if possible
        if (sigaction(signum, &sa, NULL) != 0) {
            runtime_error(ctx, "signal() failed to install handler for signal %d: %s", signum, strerror(errno));
            return val_null();
        }
#endif
    } else {
        // Reset to default handler
#ifdef HML_WINDOWS
        if (signal(signum, SIG_DFL) == SIG_ERR) {
            runtime_error(ctx, "signal() failed to reset handler for signal %d: %s", signum, strerror(errno));
            return val_null();
        }
#else
        struct sigaction sa;
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        if (sigaction(signum, &sa, NULL) != 0) {
            runtime_error(ctx, "signal() failed to reset handler for signal %d: %s", signum, strerror(errno));
            return val_null();
        }
#endif
    }

    return prev_val;
}

Value builtin_raise(Value *args, int num_args, ExecutionContext *ctx) {
    // SANDBOX: Check if signal operations are allowed
    if (sandbox_is_restricted(ctx, HML_SANDBOX_RESTRICT_SIGNALS)) {
        sandbox_error(ctx, "raise() signal operation");
        return val_null();
    }

    if (num_args != 1) {
        runtime_error(ctx, "raise() expects 1 argument (signum), got %d", num_args);
        return val_null();
    }

    if (!is_integer(args[0])) {
        runtime_error(ctx, "raise() signum must be an integer, got %s", value_type_name(args[0].type));
        return val_null();
    }

    int32_t signum = value_to_int(args[0]);

    if (signum < 0 || signum >= MAX_SIGNAL) {
        runtime_error(ctx, "raise() signum %d out of range [0, %d)", signum, MAX_SIGNAL);
        return val_null();
    }

    if (raise(signum) != 0) {
        runtime_error(ctx, "raise() failed for signal %d: %s", signum, strerror(errno));
        return val_null();
    }

    return val_null();
}
