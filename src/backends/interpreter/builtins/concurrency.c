#include "internal.h"
#include <stdatomic.h>

// Global task ID counter (atomic for thread-safety in concurrent spawns)
static atomic_int next_task_id = 1;

// Global default stack size for spawned threads (atomic for thread-safety)
// Initialized to HML_THREAD_STACK_SIZE (16 MB) from hemlock_limits.h
static _Atomic size_t g_default_stack_size = HML_THREAD_STACK_SIZE;

// Thread wrapper function that executes a task
static void* task_thread_wrapper(void* arg) {
    Task *task = (Task*)arg;
    Function *fn = task->function;

    // Block all signals in worker thread - only main thread should handle signals
    // This prevents signal handlers from corrupting task state during execution
    sigset_t set;
    sigfillset(&set);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    // Set thread name if provided (must be called from within the thread for macOS)
    if (task->name) {
        char name_buf[16];
        snprintf(name_buf, sizeof(name_buf), "%s", task->name);
#ifdef __APPLE__
        pthread_setname_np(name_buf);
#else
        pthread_setname_np(pthread_self(), name_buf);
#endif
    }

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
        Value param_arg = task->args[i];
        // Type check if parameter has type annotation
        if (fn->param_types[i]) {
            param_arg = convert_to_type(param_arg, fn->param_types[i], func_env, task->ctx);
        }
        env_define(func_env, fn->param_names[i], param_arg, 0, task->ctx);
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
    if (task->result) {
        *task->result = result;
    } else {
        // Memory allocation failed - log error and set exception state
        // The result will be lost, but the caller will see an exception
        fprintf(stderr, "Runtime error: Failed to allocate memory for task result\n");
        task->ctx->exception_state.exception_value = val_string("Memory allocation failed for task result");
        task->ctx->exception_state.is_throwing = 1;
    }
    task->state = TASK_COMPLETED;
    pthread_mutex_unlock((pthread_mutex_t*)task->task_mutex);

    // Release function environment (reference counted)
    env_release(func_env);

    // If ref_count is 1, only the worker holds a reference (caller discarded
    // the task handle). Auto-detach so thread resources are cleaned up.
    // If ref_count > 1, the caller still holds a reference and may call join().
    pthread_mutex_lock((pthread_mutex_t*)task->task_mutex);
    int worker_is_last = (__atomic_load_n(&task->ref_count, __ATOMIC_SEQ_CST) == 1);
    if (worker_is_last && !task->joined && !task->detached) {
        task->detached = 1;
        pthread_detach(*(pthread_t*)task->thread);
    }
    pthread_mutex_unlock((pthread_mutex_t*)task->task_mutex);

    // Release the worker thread's reference to the task.
    task_release(task);

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
        if (!task_args) {
            fprintf(stderr, "Runtime error: Memory allocation failed in spawn()\n");
            exit(1);
        }
        for (int i = 0; i < task_num_args; i++) {
            // Deep copy each argument for thread isolation
            task_args[i] = value_deep_copy(args[i + 1]);
        }
    }

    // Create task (atomically increment task ID for thread-safety)
    // NOTE: We keep closure_env for read access to builtins and global functions
    // Arguments are deep-copied above to prevent sharing mutable data
    // The closure environment is protected by a per-environment mutex, making
    // concurrent reads safe. Writes to parent scope variables are also synchronized.
    int task_id = atomic_fetch_add(&next_task_id, 1);
    Task *task = task_new(task_id, fn, task_args, task_num_args, fn->closure_env);

    // Allocate pthread_t
    task->thread = malloc(sizeof(pthread_t));
    if (!task->thread) {
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }

    // Retain task so the worker thread holds a reference.
    // Without this, if the caller discards the task handle (fire-and-forget spawn),
    // the task would be freed while the thread is still running.
    task_retain(task);

    // Configure thread attributes with larger stack size.
    // The interpreter's eval_stmt recurses for every nested statement, so
    // programs that spawn many sequential servers with closure callbacks
    // (e.g. WebSocket accept loops) can overflow the default pthread stack.
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, atomic_load(&g_default_stack_size));

    // Create thread to execute task
    int rc = pthread_create((pthread_t*)task->thread, &attr, task_thread_wrapper, task);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        fprintf(stderr, "Runtime error: Failed to create thread: %d\n", rc);
        task_release(task);
        exit(1);
    }

    return val_task(task);
}

// spawn_with(options, fn, args...) - Spawn with configuration options
// Options object supports:
//   stack_size: i32/i64 - stack size in bytes for the thread
//   name: string - debug name for the thread (pthread_setname_np)
Value builtin_spawn_with(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args < 2) {
        runtime_error(ctx, "spawn_with() expects at least 2 arguments (options, async function)");
        return val_null();
    }

    Value options_val = args[0];
    Value func_val = args[1];

    if (options_val.type != VAL_OBJECT) {
        runtime_error(ctx, "spawn_with() first argument must be an options object");
        return val_null();
    }

    if (func_val.type != VAL_FUNCTION) {
        runtime_error(ctx, "spawn_with() second argument must be an async function");
        return val_null();
    }

    Function *fn = func_val.as.as_function;
    if (!fn->is_async) {
        runtime_error(ctx, "spawn_with() requires an async function");
        return val_null();
    }

    // Extract options
    Object *opts = options_val.as.as_object;
    size_t stack_size = atomic_load(&g_default_stack_size);
    char *thread_name = NULL;

    for (int i = 0; i < opts->num_fields; i++) {
        if (strcmp(opts->fields[i].name, "stack_size") == 0) {
            Value sv = opts->fields[i].value;
            if (!is_integer(sv)) {
                runtime_error(ctx, "spawn_with() stack_size must be an integer");
                return val_null();
            }
            int64_t sz = value_to_int64(sv);
            if (sz <= 0) {
                runtime_error(ctx, "spawn_with() stack_size must be positive");
                return val_null();
            }
            stack_size = (size_t)sz;
        } else if (strcmp(opts->fields[i].name, "name") == 0) {
            Value nv = opts->fields[i].value;
            if (nv.type != VAL_STRING) {
                runtime_error(ctx, "spawn_with() name must be a string");
                return val_null();
            }
            thread_name = nv.as.as_string->data;
        }
    }

    // Deep copy arguments (skip options and function)
    Value *task_args = NULL;
    int task_num_args = num_args - 2;

    if (task_num_args > 0) {
        task_args = malloc(sizeof(Value) * task_num_args);
        if (!task_args) {
            runtime_error(ctx, "Memory allocation failed in spawn_with()");
            return val_null();
        }
        for (int i = 0; i < task_num_args; i++) {
            task_args[i] = value_deep_copy(args[i + 2]);
        }
    }

    // Create task
    int task_id = atomic_fetch_add(&next_task_id, 1);
    Task *task = task_new(task_id, fn, task_args, task_num_args, fn->closure_env);

    // Set debug name if provided
    if (thread_name) {
        task->name = strdup(thread_name);
    }

    // Allocate pthread_t
    task->thread = malloc(sizeof(pthread_t));
    if (!task->thread) {
        runtime_error(ctx, "Memory allocation failed");
        return val_null();
    }

    task_retain(task);

    // Configure thread attributes with requested stack size
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, stack_size);

    // Create thread
    int rc = pthread_create((pthread_t*)task->thread, &attr, task_thread_wrapper, task);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        runtime_error(ctx, "Failed to create thread: %d", rc);
        task_release(task);
        return val_null();
    }

    return val_task(task);
}

// get_default_stack_size() - Returns the current default thread stack size in bytes
Value builtin_get_default_stack_size(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args; (void)ctx;
    return val_i64((int64_t)atomic_load(&g_default_stack_size));
}

// set_default_stack_size(size) - Sets the default thread stack size for subsequent spawns
Value builtin_set_default_stack_size(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "set_default_stack_size() expects 1 argument (size in bytes)");
        return val_null();
    }

    if (!is_integer(args[0])) {
        runtime_error(ctx, "set_default_stack_size() argument must be an integer");
        return val_null();
    }

    int64_t size = value_to_int64(args[0]);
    if (size <= 0) {
        runtime_error(ctx, "set_default_stack_size() size must be positive");
        return val_null();
    }

    atomic_store(&g_default_stack_size, (size_t)size);
    return val_null();
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

    // Get the result and retain it for the caller
    // The task will release its reference when freed, so we need to retain
    // for the caller to have a valid reference to the result value
    Value result = val_null();
    if (task->result) {
        result = *task->result;
        VALUE_RETAIN(result);  // Caller now owns a reference
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
            if (!task_args) {
                runtime_error(ctx, "Memory allocation failed in detach()");
                return val_null();
            }
            for (int i = 0; i < task_num_args; i++) {
                // Deep copy each argument for thread isolation
                task_args[i] = value_deep_copy(args[i + 1]);
            }
        }

        // Create task (atomically increment task ID for thread-safety)
        // NOTE: We keep closure_env for read access to builtins and global functions
        // Arguments are deep-copied above to prevent sharing mutable data
        // The closure environment is protected by a per-environment mutex for thread-safety.
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

        // Configure thread with larger stack for recursive eval_stmt
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, atomic_load(&g_default_stack_size));

        // Create thread to execute task
        int rc = pthread_create((pthread_t*)task->thread, &attr, task_thread_wrapper, task);
        pthread_attr_destroy(&attr);
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

    // Adaptive polling with exponential backoff
    // Start with short sleep, increase up to max if no data found
    // This reduces CPU usage while maintaining low latency for active channels
    long sleep_ns = 50000;           // Start at 50us (much lower latency than 1ms)
    const long max_sleep_ns = HML_POLL_SLEEP_NS;  // Max 1ms
    const long min_sleep_ns = 50000; // Min 50us

    while (1) {
        int all_closed = 1;  // Track if all channels are closed

        // Check each channel for available data
        for (int i = 0; i < channels->length; i++) {
            Channel *ch = channels->elements[i].as.as_channel;
            pthread_mutex_t *mutex = (pthread_mutex_t*)ch->mutex;

            pthread_mutex_lock(mutex);

            // Track if any channel is still open
            if (!ch->closed) {
                all_closed = 0;
            }

            // Check for unbuffered channel with sender waiting (rendezvous pattern)
            if (ch->capacity == 0 && ch->sender_waiting) {
                // Get the value from sender
                Value msg = *(ch->unbuffered_value);
                *(ch->unbuffered_value) = val_null();
                ch->sender_waiting = 0;

                // Signal sender that value was received
                pthread_cond_signal((pthread_cond_t*)ch->rendezvous);
                pthread_mutex_unlock(mutex);

                // Create result object { channel, value }
                Object *result = object_new(NULL, 2);
                if (!result) {
                    value_release(msg);
                    return val_null();
                }
                result->fields[0].name = strdup("channel");
                if (!result->fields[0].name) {
                    value_release(msg);
                    object_free(result);
                    return val_null();
                }
                result->fields[0].value = channels->elements[i];
                value_retain(channels->elements[i]);
                result->num_fields = 1;

                result->fields[1].name = strdup("value");
                if (!result->fields[1].name) {
                    value_release(msg);
                    object_free(result);
                    return val_null();
                }
                result->fields[1].value = msg;
                result->num_fields = 2;

                return val_object(result);
            }

            // Check if buffered channel has data (capacity > 0 ensures no division by zero)
            if (ch->capacity > 0 && ch->count > 0) {
                // Read the value
                Value msg = ch->buffer[ch->head];
                ch->head = (ch->head + 1) % ch->capacity;
                ch->count--;

                // Signal that buffer is not full
                pthread_cond_signal((pthread_cond_t*)ch->not_full);
                pthread_mutex_unlock(mutex);

                // Create result object { channel, value }
                Object *result = object_new(NULL, 2);
                if (!result) {
                    value_release(msg);
                    return val_null();
                }
                result->fields[0].name = strdup("channel");
                if (!result->fields[0].name) {
                    value_release(msg);
                    object_free(result);
                    return val_null();
                }
                result->fields[0].value = channels->elements[i];
                value_retain(channels->elements[i]);
                result->num_fields = 1;

                result->fields[1].name = strdup("value");
                if (!result->fields[1].name) {
                    value_release(msg);
                    object_free(result);
                    return val_null();
                }
                result->fields[1].value = msg;
                result->num_fields = 2;

                return val_object(result);
            }

            // Check if channel is closed and empty
            if (ch->closed) {
                pthread_mutex_unlock(mutex);
                // Return null for this closed channel
                Object *result = object_new(NULL, 2);
                if (!result) {
                    return val_null();
                }
                result->fields[0].name = strdup("channel");
                if (!result->fields[0].name) {
                    object_free(result);
                    return val_null();
                }
                result->fields[0].value = channels->elements[i];
                value_retain(channels->elements[i]);
                result->num_fields = 1;

                result->fields[1].name = strdup("value");
                if (!result->fields[1].name) {
                    object_free(result);
                    return val_null();
                }
                result->fields[1].value = val_null();
                result->num_fields = 2;

                return val_object(result);
            }

            pthread_mutex_unlock(mutex);
        }

        // If all channels are closed, return null immediately
        if (all_closed) {
            return val_null();
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

        // Adaptive sleep with exponential backoff
        // Start short for low latency, increase if no activity
        struct timespec sleep_time = { 0, sleep_ns };
        nanosleep(&sleep_time, NULL);

        // Exponential backoff: double sleep time up to max
        sleep_ns *= 2;
        if (sleep_ns > max_sleep_ns) {
            sleep_ns = max_sleep_ns;
        }

        // Note: sleep_ns is reset when we find data (we return immediately)
        // If we had a "soft" check (data found but not taken), we could reset here
        (void)min_sleep_ns;  // Suppress unused warning
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
