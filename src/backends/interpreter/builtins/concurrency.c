#include "internal.h"
#include <stdatomic.h>

// Global task ID counter (atomic for thread-safety in concurrent spawns)
static atomic_int next_task_id = 1;

// Thread wrapper function that executes a task
static void* task_thread_wrapper(void* arg) {
    Task *task = (Task*)arg;
    Function *fn = task->function;

    // Block all signals in worker thread - only main thread should handle signals
    // This prevents signal handlers from corrupting task state during execution
    sigset_t set;
    sigfillset(&set);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    // Mark as running (thread-safe)
    pthread_mutex_lock((pthread_mutex_t*)task->task_mutex);
    task->state = TASK_RUNNING;
    pthread_mutex_unlock((pthread_mutex_t*)task->task_mutex);

    // Create environment for function execution with closure env as parent
    // This gives read access to builtins and global functions
    // Arguments are deep-copied in spawn() so mutable data is isolated
    Environment *func_env = env_new(task->env);

    // Bind parameters (these are deep-copied, so safe to use directly)
    for (int i = 0; i < fn->num_params && i < task->num_args; i++) {
        Value arg = task->args[i];
        // Type check if parameter has type annotation
        if (fn->param_types[i]) {
            arg = convert_to_type(arg, fn->param_types[i], func_env, task->ctx);
        }
        env_define(func_env, fn->param_names[i], arg, 0, task->ctx);
    }

    // Execute function body
    eval_stmt(fn->body, func_env, task->ctx);

    // Get return value
    Value result = val_null();
    if (task->ctx->return_state.is_returning) {
        result = task->ctx->return_state.return_value;
        task->ctx->return_state.is_returning = 0;
    }

    // Store result and mark as completed (thread-safe)
    pthread_mutex_lock((pthread_mutex_t*)task->task_mutex);
    task->result = malloc(sizeof(Value));
    *task->result = result;
    task->state = TASK_COMPLETED;
    pthread_mutex_unlock((pthread_mutex_t*)task->task_mutex);

    // Release function environment (reference counted)
    env_release(func_env);

    // Clean up detached tasks (decrement reference count)
    // The task will be freed when both the worker thread and user-side have released it
    if (task->detached) {
        task_release(task);
    }

    return NULL;
}

Value builtin_spawn(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Not used in spawn

    if (num_args < 1) {
        fprintf(stderr, "Runtime error: spawn() expects at least 1 argument (async function)\n");
        exit(1);
    }

    Value func_val = args[0];

    if (func_val.type != VAL_FUNCTION) {
        fprintf(stderr, "Runtime error: spawn() expects an async function\n");
        exit(1);
    }

    Function *fn = func_val.as.as_function;

    if (!fn->is_async) {
        fprintf(stderr, "Runtime error: spawn() requires an async function\n");
        exit(1);
    }

    // Create task with remaining args as function arguments
    // THREAD SAFETY: Deep copy all arguments to isolate task from parent
    // This ensures tasks don't share mutable state - they communicate via channels
    Value *task_args = NULL;
    int task_num_args = num_args - 1;

    if (task_num_args > 0) {
        task_args = malloc(sizeof(Value) * task_num_args);
        for (int i = 0; i < task_num_args; i++) {
            // Deep copy each argument for thread isolation
            task_args[i] = value_deep_copy(args[i + 1]);
        }
    }

    // Create task (atomically increment task ID for thread-safety)
    // NOTE: We keep closure_env for read access to builtins and global functions
    // Arguments are deep-copied above to prevent sharing mutable data
    // Modifying parent scope variables from tasks is undefined behavior
    int task_id = atomic_fetch_add(&next_task_id, 1);
    Task *task = task_new(task_id, fn, task_args, task_num_args, fn->closure_env);

    // Allocate pthread_t
    task->thread = malloc(sizeof(pthread_t));
    if (!task->thread) {
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }

    // Create thread to execute task
    int rc = pthread_create((pthread_t*)task->thread, NULL, task_thread_wrapper, task);
    if (rc != 0) {
        fprintf(stderr, "Runtime error: Failed to create thread: %d\n", rc);
        exit(1);
    }

    return val_task(task);
}

Value builtin_join(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "join() expects 1 argument (task handle)");
        return val_null();
    }

    Value task_val = args[0];

    if (task_val.type != VAL_TASK) {
        runtime_error(ctx, "join() expects a task handle");
        return val_null();
    }

    Task *task = task_val.as.as_task;

    // Check if task is already joined or detached (thread-safe)
    pthread_mutex_lock((pthread_mutex_t*)task->task_mutex);

    if (task->joined) {
        pthread_mutex_unlock((pthread_mutex_t*)task->task_mutex);
        runtime_error(ctx, "task handle already joined");
        return val_null();
    }

    if (task->detached) {
        pthread_mutex_unlock((pthread_mutex_t*)task->task_mutex);
        runtime_error(ctx, "cannot join detached task");
        return val_null();
    }

    // Mark as joined
    task->joined = 1;

    pthread_mutex_unlock((pthread_mutex_t*)task->task_mutex);

    // Wait for thread to complete (outside of mutex to avoid deadlock)
    if (task->thread) {
        int rc = pthread_join(*(pthread_t*)task->thread, NULL);
        if (rc != 0) {
            runtime_error(ctx, "pthread_join failed: %d", rc);
            return val_null();
        }
    }

    // Access exception state and result (thread-safe)
    pthread_mutex_lock((pthread_mutex_t*)task->task_mutex);

    // Check if task threw an exception
    if (task->ctx->exception_state.is_throwing) {
        // Re-throw the exception in the current context
        ctx->exception_state = task->ctx->exception_state;
        pthread_mutex_unlock((pthread_mutex_t*)task->task_mutex);
        return val_null();
    }

    // Get the result
    Value result = val_null();
    if (task->result) {
        result = *task->result;
    }

    pthread_mutex_unlock((pthread_mutex_t*)task->task_mutex);

    // NOTE: We do NOT release the task here. The task will be released when the
    // variable goes out of scope (automatic refcounting handles this).
    // Previously task_release() was called here, but with proper refcounting,
    // this caused use-after-free when the variable still held a reference.

    return result;
}

Value builtin_detach(Value *args, int num_args, ExecutionContext *ctx) {
    // detach() supports two patterns:
    // 1. detach(task_handle) - detach an existing spawned task
    // 2. detach(function, args...) - spawn and immediately detach (fire-and-forget)

    if (num_args < 1) {
        runtime_error(ctx, "detach() expects at least 1 argument");
        return val_null();
    }

    Value first_arg = args[0];

    // Pattern 1: detach(task_handle)
    if (first_arg.type == VAL_TASK) {
        if (num_args != 1) {
            runtime_error(ctx, "detach() with task handle expects exactly 1 argument");
            return val_null();
        }

        Task *t = first_arg.as.as_task;

        // Check if already detached or joined (thread-safe)
        pthread_mutex_lock((pthread_mutex_t*)t->task_mutex);

        if (t->joined) {
            pthread_mutex_unlock((pthread_mutex_t*)t->task_mutex);
            runtime_error(ctx, "cannot detach already joined task");
            return val_null();
        }

        if (t->detached) {
            pthread_mutex_unlock((pthread_mutex_t*)t->task_mutex);
            runtime_error(ctx, "task already detached");
            return val_null();
        }

        // Mark as detached
        t->detached = 1;

        pthread_mutex_unlock((pthread_mutex_t*)t->task_mutex);

        // Detach the pthread (fire and forget)
        if (t->thread) {
            int rc = pthread_detach(*(pthread_t*)t->thread);
            if (rc != 0) {
                runtime_error(ctx, "pthread_detach failed: %d", rc);
                return val_null();
            }
        }

        return val_null();
    }

    // Pattern 2: detach(function, args...) - spawn and immediately detach
    if (first_arg.type == VAL_FUNCTION) {
        Function *fn = first_arg.as.as_function;

        if (!fn->is_async) {
            runtime_error(ctx, "detach() requires an async function");
            return val_null();
        }

        // Create task with remaining args as function arguments
        // THREAD SAFETY: Deep copy all arguments to isolate task from parent
        Value *task_args = NULL;
        int task_num_args = num_args - 1;

        if (task_num_args > 0) {
            task_args = malloc(sizeof(Value) * task_num_args);
            for (int i = 0; i < task_num_args; i++) {
                // Deep copy each argument for thread isolation
                task_args[i] = value_deep_copy(args[i + 1]);
            }
        }

        // Create task (atomically increment task ID for thread-safety)
        // NOTE: We keep closure_env for read access to builtins and global functions
        // Arguments are deep-copied above to prevent sharing mutable data
        int task_id = atomic_fetch_add(&next_task_id, 1);
        Task *task = task_new(task_id, fn, task_args, task_num_args, fn->closure_env);

        // Mark as detached before starting thread
        task->detached = 1;

        // Allocate pthread_t
        task->thread = malloc(sizeof(pthread_t));
        if (!task->thread) {
            runtime_error(ctx, "Memory allocation failed");
            return val_null();
        }

        // CRITICAL: Retain task to prevent premature cleanup during pthread_detach
        // Without this, the worker thread may complete and free the task before
        // we finish calling pthread_detach, leading to use-after-free
        task_retain(task);  // ref_count: 1 -> 2

        // Create thread to execute task
        int rc = pthread_create((pthread_t*)task->thread, NULL, task_thread_wrapper, task);
        if (rc != 0) {
            runtime_error(ctx, "Failed to create thread: %d", rc);
            free(task->thread);
            task_release(task);  // Release our temporary reference
            return val_null();
        }

        // Detach the pthread immediately (fire and forget)
        // Safe to access task->thread because we're holding a reference
        rc = pthread_detach(*(pthread_t*)task->thread);
        if (rc != 0) {
            runtime_error(ctx, "pthread_detach failed: %d", rc);
            task_release(task);  // Release our temporary reference
            return val_null();
        }

        // Release our temporary reference - worker thread will clean up when done
        // ref_count: 2 -> 1 (worker thread holds the remaining reference)
        task_release(task);

        return val_null();
    }

    // Invalid argument type
    runtime_error(ctx, "detach() expects either a task handle or an async function");
    return val_null();
}

Value builtin_channel(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;

    int capacity = 0;  // unbuffered by default

    if (num_args > 0) {
        if (args[0].type != VAL_I32 && args[0].type != VAL_U32) {
            fprintf(stderr, "Runtime error: channel() capacity must be an integer\n");
            exit(1);
        }
        capacity = value_to_int(args[0]);

        if (capacity < 0) {
            fprintf(stderr, "Runtime error: channel() capacity cannot be negative\n");
            exit(1);
        }
    }

    Channel *ch = channel_new(capacity);
    return val_channel(ch);
}

// select(channels: array<channel>, timeout_ms?: i32) -> { channel, value } | null
// Wait for any of multiple channels to have data available
Value builtin_select(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args < 1 || num_args > 2) {
        runtime_error(ctx, "select() expects 1-2 arguments (channels, timeout_ms?)");
        return val_null();
    }

    if (args[0].type != VAL_ARRAY) {
        runtime_error(ctx, "select() first argument must be an array of channels");
        return val_null();
    }

    Array *channels = args[0].as.as_array;
    int timeout_ms = -1;  // -1 means infinite

    if (num_args > 1) {
        if (!is_integer(args[1])) {
            runtime_error(ctx, "select() timeout must be an integer (milliseconds)");
            return val_null();
        }
        timeout_ms = value_to_int(args[1]);
    }

    if (channels->length == 0) {
        runtime_error(ctx, "select() requires at least one channel");
        return val_null();
    }

    // Validate all elements are channels
    for (int i = 0; i < channels->length; i++) {
        if (channels->elements[i].type != VAL_CHANNEL) {
            runtime_error(ctx, "select() array must contain only channels");
            return val_null();
        }
    }

    // Calculate deadline
    struct timespec deadline;
    struct timespec *deadline_ptr = NULL;
    if (timeout_ms >= 0) {
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += timeout_ms / 1000;
        deadline.tv_nsec += (timeout_ms % HML_MILLISECONDS_PER_SECOND) * HML_NANOSECONDS_PER_MS;
        if (deadline.tv_nsec >= HML_NANOSECONDS_PER_SECOND) {
            deadline.tv_sec++;
            deadline.tv_nsec -= HML_NANOSECONDS_PER_SECOND;
        }
        deadline_ptr = &deadline;
    }

    // Polling loop with sleep
    // Check all channels, if none ready, sleep briefly and retry
    while (1) {
        // Check each channel for available data
        for (int i = 0; i < channels->length; i++) {
            Channel *ch = channels->elements[i].as.as_channel;
            pthread_mutex_t *mutex = (pthread_mutex_t*)ch->mutex;

            pthread_mutex_lock(mutex);

            // Check if channel has data
            if (ch->count > 0) {
                // Read the value
                Value msg = ch->buffer[ch->head];
                ch->head = (ch->head + 1) % ch->capacity;
                ch->count--;

                // Signal that buffer is not full
                pthread_cond_signal((pthread_cond_t*)ch->not_full);
                pthread_mutex_unlock(mutex);

                // Create result object { channel, value }
                Object *result = object_new(NULL, 2);
                result->field_names[0] = strdup("channel");
                result->field_values[0] = channels->elements[i];
                value_retain(channels->elements[i]);
                result->num_fields = 1;

                result->field_names[1] = strdup("value");
                result->field_values[1] = msg;
                result->num_fields = 2;

                return val_object(result);
            }

            // Check if channel is closed and empty
            if (ch->closed) {
                pthread_mutex_unlock(mutex);
                // Return null for this closed channel
                Object *result = object_new(NULL, 2);
                result->field_names[0] = strdup("channel");
                result->field_values[0] = channels->elements[i];
                value_retain(channels->elements[i]);
                result->num_fields = 1;

                result->field_names[1] = strdup("value");
                result->field_values[1] = val_null();
                result->num_fields = 2;

                return val_object(result);
            }

            pthread_mutex_unlock(mutex);
        }

        // Check timeout
        if (deadline_ptr != NULL) {
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            if (now.tv_sec > deadline_ptr->tv_sec ||
                (now.tv_sec == deadline_ptr->tv_sec && now.tv_nsec >= deadline_ptr->tv_nsec)) {
                return val_null();  // Timeout
            }
        }

        // Brief sleep before retrying (1ms)
        struct timespec sleep_time = { 0, HML_POLL_SLEEP_NS };
        nanosleep(&sleep_time, NULL);
    }
}

Value builtin_task_debug_info(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;

    if (num_args != 1) {
        fprintf(stderr, "Runtime error: task_debug_info() expects 1 argument (task handle)\n");
        exit(1);
    }

    if (args[0].type != VAL_TASK) {
        fprintf(stderr, "Runtime error: task_debug_info() expects a task handle\n");
        exit(1);
    }

    Task *task = args[0].as.as_task;

    // Lock mutex to safely read task state
    pthread_mutex_lock((pthread_mutex_t*)task->task_mutex);

    printf("=== Task Debug Info ===\n");
    printf("Task ID: %d\n", task->id);
    printf("State: ");
    switch (task->state) {
        case TASK_READY: printf("READY\n"); break;
        case TASK_RUNNING: printf("RUNNING\n"); break;
        case TASK_BLOCKED: printf("BLOCKED\n"); break;
        case TASK_COMPLETED: printf("COMPLETED\n"); break;
        default: printf("UNKNOWN\n"); break;
    }
    printf("Joined: %s\n", task->joined ? "true" : "false");
    printf("Detached: %s\n", task->detached ? "true" : "false");
    printf("Ref Count: %d\n", task->ref_count);
    printf("Has Result: %s\n", task->result ? "true" : "false");
    printf("Exception: %s\n", task->ctx->exception_state.is_throwing ? "true" : "false");
    printf("======================\n");

    pthread_mutex_unlock((pthread_mutex_t*)task->task_mutex);

    return val_null();
}
