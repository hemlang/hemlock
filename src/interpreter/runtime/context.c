#include "internal.h"

// ========== EXECUTION CONTEXT IMPLEMENTATION ==========

ExecutionContext* exec_context_new(void) {
    ExecutionContext *ctx = malloc(sizeof(ExecutionContext));
    if (!ctx) {
        fprintf(stderr, "Fatal error: Failed to allocate execution context\n");
        exit(1);
    }
    ctx->return_state.is_returning = 0;
    ctx->return_state.return_value = val_null();
    ctx->loop_state.is_breaking = 0;
    ctx->loop_state.is_continuing = 0;
    ctx->exception_state.is_throwing = 0;
    ctx->exception_state.exception_value = val_null();
    call_stack_init(&ctx->call_stack);
    defer_stack_init(&ctx->defer_stack);
    return ctx;
}

void exec_context_free(ExecutionContext *ctx) {
    if (ctx) {
        call_stack_free(&ctx->call_stack);
        defer_stack_free(&ctx->defer_stack);
        free(ctx);
    }
}

// ========== CALL STACK IMPLEMENTATION ==========

void call_stack_init(CallStack *stack) {
    stack->capacity = 64;
    stack->count = 0;
    stack->frames = malloc(sizeof(CallFrame) * stack->capacity);
    if (!stack->frames) {
        fprintf(stderr, "Fatal error: Failed to initialize call stack\n");
        exit(1);
    }
}

void call_stack_push(CallStack *stack, const char *function_name) {
    if (stack->capacity == 0) {
        call_stack_init(stack);
    }

    if (stack->count >= stack->capacity) {
        stack->capacity *= 2;
        CallFrame *new_frames = realloc(stack->frames, sizeof(CallFrame) * stack->capacity);
        if (!new_frames) {
            fprintf(stderr, "Fatal error: Failed to grow call stack\n");
            exit(1);
        }
        stack->frames = new_frames;
    }

    stack->frames[stack->count].function_name = strdup(function_name);
    stack->frames[stack->count].line = 0;  // Set by caller
    stack->count++;
}

// Push with line number
void call_stack_push_line(CallStack *stack, const char *function_name, int line) {
    call_stack_push(stack, function_name);
    if (stack->count > 0) {
        stack->frames[stack->count - 1].line = line;
    }
}

void call_stack_pop(CallStack *stack) {
    if (stack->count > 0) {
        stack->count--;
        free(stack->frames[stack->count].function_name);
    }
}

void call_stack_print(CallStack *stack) {
    if (stack->count == 0) {
        return;
    }

    fprintf(stderr, "\nStack trace (most recent call first):\n");
    for (int i = stack->count - 1; i >= 0; i--) {
        if (stack->frames[i].line > 0) {
            fprintf(stderr, "  at %s() (line %d)\n",
                    stack->frames[i].function_name,
                    stack->frames[i].line);
        } else {
            fprintf(stderr, "  at %s()\n", stack->frames[i].function_name);
        }
    }
}

void call_stack_free(CallStack *stack) {
    for (int i = 0; i < stack->count; i++) {
        free(stack->frames[i].function_name);
    }
    free(stack->frames);
    stack->frames = NULL;
    stack->count = 0;
    stack->capacity = 0;
}

// ========== DEFER STACK ==========

void defer_stack_init(DeferStack *stack) {
    stack->capacity = 8;
    stack->count = 0;
    stack->calls = malloc(sizeof(Expr*) * stack->capacity);
    stack->envs = malloc(sizeof(Environment*) * stack->capacity);
    if (!stack->calls || !stack->envs) {
        fprintf(stderr, "Fatal error: Failed to initialize defer stack\n");
        exit(1);
    }
}

void defer_stack_push(DeferStack *stack, Expr *call, Environment *env) {
    if (stack->capacity == 0) {
        defer_stack_init(stack);
    }

    if (stack->count >= stack->capacity) {
        stack->capacity *= 2;
        Expr **new_calls = realloc(stack->calls, sizeof(Expr*) * stack->capacity);
        Environment **new_envs = realloc(stack->envs, sizeof(Environment*) * stack->capacity);
        if (!new_calls || !new_envs) {
            fprintf(stderr, "Fatal error: Failed to grow defer stack\n");
            exit(1);
        }
        stack->calls = new_calls;
        stack->envs = new_envs;
    }

    // Clone the expression and retain the environment
    stack->calls[stack->count] = expr_clone(call);
    stack->envs[stack->count] = env;
    env_retain(env);  // Increment reference count
    stack->count++;
}

void defer_stack_execute(DeferStack *stack, ExecutionContext *ctx) {
    // Execute deferred calls in LIFO order (last defer executes first)
    for (int i = stack->count - 1; i >= 0; i--) {
        // Save current exception state
        int was_throwing = ctx->exception_state.is_throwing;
        Value saved_exception = ctx->exception_state.exception_value;

        // Temporarily clear exception state to allow defer to run
        ctx->exception_state.is_throwing = 0;

        // Execute the deferred call
        eval_expr(stack->calls[i], stack->envs[i], ctx);

        // If defer itself threw, propagate that exception (overrides previous)
        // Otherwise, restore the saved exception state
        if (!ctx->exception_state.is_throwing) {
            ctx->exception_state.is_throwing = was_throwing;
            ctx->exception_state.exception_value = saved_exception;
        }

        // Clean up the deferred expression and release environment
        expr_free(stack->calls[i]);
        env_release(stack->envs[i]);
    }

    // Clear the stack
    stack->count = 0;
}

void defer_stack_free(DeferStack *stack) {
    // Free any remaining deferred calls (shouldn't happen in normal execution)
    for (int i = 0; i < stack->count; i++) {
        expr_free(stack->calls[i]);
        env_release(stack->envs[i]);
    }
    free(stack->calls);
    free(stack->envs);
    stack->calls = NULL;
    stack->envs = NULL;
    stack->count = 0;
    stack->capacity = 0;
}

// Runtime error with stack trace
void runtime_error(ExecutionContext *ctx, const char *format, ...) {
    va_list args;
    va_start(args, format);

    fprintf(stderr, "Runtime error: ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");

    va_end(args);

    // Print stack trace if available
    if (ctx && ctx->call_stack.count > 0) {
        call_stack_print(&ctx->call_stack);
    }

    exit(1);
}
