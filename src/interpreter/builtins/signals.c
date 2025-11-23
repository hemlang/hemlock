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
}

Value builtin_signal(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: signal() expects 2 arguments (signum, handler)\n");
        exit(1);
    }

    if (!is_integer(args[0])) {
        fprintf(stderr, "Runtime error: signal() signum must be an integer\n");
        exit(1);
    }

    int32_t signum = value_to_int(args[0]);

    if (signum < 0 || signum >= MAX_SIGNAL) {
        fprintf(stderr, "Runtime error: signal() signum %d out of range [0, %d)\n", signum, MAX_SIGNAL);
        exit(1);
    }

    // Check if handler is null (reset to default) or a function
    Function *new_handler = NULL;
    if (args[1].type != VAL_NULL) {
        if (args[1].type != VAL_FUNCTION) {
            fprintf(stderr, "Runtime error: signal() handler must be a function or null\n");
            exit(1);
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
        struct sigaction sa;
        sa.sa_handler = hemlock_signal_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;  // Restart syscalls if possible
        if (sigaction(signum, &sa, NULL) != 0) {
            fprintf(stderr, "Runtime error: signal() failed to install handler for signal %d: %s\n", signum, strerror(errno));
            exit(1);
        }
    } else {
        // Reset to default handler
        struct sigaction sa;
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        if (sigaction(signum, &sa, NULL) != 0) {
            fprintf(stderr, "Runtime error: signal() failed to reset handler for signal %d: %s\n", signum, strerror(errno));
            exit(1);
        }
    }

    return prev_val;
}

Value builtin_raise(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: raise() expects 1 argument (signum)\n");
        exit(1);
    }

    if (!is_integer(args[0])) {
        fprintf(stderr, "Runtime error: raise() signum must be an integer\n");
        exit(1);
    }

    int32_t signum = value_to_int(args[0]);

    if (signum < 0 || signum >= MAX_SIGNAL) {
        fprintf(stderr, "Runtime error: raise() signum %d out of range [0, %d)\n", signum, MAX_SIGNAL);
        exit(1);
    }

    if (raise(signum) != 0) {
        fprintf(stderr, "Runtime error: raise() failed for signal %d: %s\n", signum, strerror(errno));
        exit(1);
    }

    return val_null();
}
