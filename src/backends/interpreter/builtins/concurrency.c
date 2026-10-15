// Task and channel builtins for the interpreter:
//   spawn / spawn_with / detach / join / channel / select / task_debug_info
//   get_default_stack_size / set_default_stack_size
//
// Task lifecycle
// --------------
// A Task is reference counted. spawn() hands one reference to the caller
// (the task handle) and one to the worker thread; the worker drops its
// reference when the function returns. Whoever drops the last reference
// frees the task, and task_free() detaches the pthread if nobody joined or
// detached it, so a dropped handle never leaks thread resources and there
// is no special-casing of "who finished first".
//
//   join(task)    pthread_join, then hands the result (or the task's
//                 exception) to the caller. Second join is an error.
//   detach(task)  pthread_detach; the task can no longer be joined.
//   detach(fn, ..args)  spawn with a detached pthread (fire and forget).
//
// Arguments are deep-copied into the task so tasks never share mutable
// state by accident; channels and task handles are shared by reference.
#define _GNU_SOURCE
#include "internal.h"
#include <stdatomic.h>

static atomic_int next_task_id = 1;

// Set once (monotonic, never reset) the first time any task thread is
// created. Lets the shared per-AST-node property inline cache bypass its
// unsynchronized fast-path once concurrency is possible. See internal.h
// and runtime/expressions.c.
int g_interp_has_spawned = 0;

static _Atomic size_t g_default_stack_size = HML_THREAD_STACK_SIZE;

typedef struct {
    size_t stack_size;
    const char *name;   // optional debug name (borrowed; copied into the task)
    int detached;       // create the pthread already detached
} SpawnOptions;

// ========== WORKER THREAD ==========

static void *task_thread_main(void *arg) {
    Task *task = (Task *)arg;
    Function *fn = task->function;
    ExecutionContext *ctx = task->ctx;

    // Only the main thread handles signals (POSIX; Windows delivers console
    // control events on its own thread).
#ifndef _WIN32
    sigset_t set;
    sigfillset(&set);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
#endif

#ifndef __EMSCRIPTEN__
    if (task->name) {
        char name_buf[16];
        snprintf(name_buf, sizeof(name_buf), "%s", task->name);
#ifdef __APPLE__
        pthread_setname_np(name_buf);
#else
        pthread_setname_np(pthread_self(), name_buf);
#endif
    }
#endif

    pthread_mutex_lock((pthread_mutex_t *)task->task_mutex);
    task->state = TASK_RUNNING;
    pthread_mutex_unlock((pthread_mutex_t *)task->task_mutex);

    // Bind the (already deep-copied) arguments in a fresh scope whose parent
    // is the closure environment, giving read access to globals and builtins.
    Environment *func_env = env_new(task->env);
    for (int i = 0; i < fn->num_params && i < task->num_args; i++) {
        Value param = task->args[i];
        VALUE_RETAIN(param);  // convert_to_type consumes on failure; task_free still releases args[i]
        if (fn->param_types[i]) {
            param = convert_to_type(param, fn->param_types[i], func_env, ctx);
        }
        env_define(func_env, fn->param_names[i], param, 0, ctx);
        VALUE_RELEASE(param);
        if (ctx->exception_state.is_throwing) {
            break;
        }
    }

    if (!ctx->exception_state.is_throwing) {
        eval_stmt(fn->body, func_env, ctx);
    }

    Value result = val_null();
    if (ctx->return_state.is_returning) {
        result = ctx->return_state.return_value;
        ctx->return_state.is_returning = 0;
    }

    pthread_mutex_lock((pthread_mutex_t *)task->task_mutex);
    task->result = result;
    task->state = TASK_COMPLETED;
    pthread_mutex_unlock((pthread_mutex_t *)task->task_mutex);

    env_release(func_env);
    task_release(task);  // the worker's reference
    return NULL;
}

// ========== SPAWN ==========

// Shared by spawn(), spawn_with() and detach(fn, ...). `who` names the
// builtin for error messages. Returns the task handle, or null for a
// detached spawn or on error (with the exception set on ctx).
static Value spawn_task(const char *who, Function *fn, Value *args, int num_args,
                        const SpawnOptions *opts, ExecutionContext *ctx) {
    if (!fn->is_async) {
        runtime_error(ctx, "%s requires an async function", who);
        return val_null();
    }

    Value *task_args = NULL;
    if (num_args > 0) {
        task_args = malloc(sizeof(Value) * num_args);
        if (!task_args) {
            runtime_error(ctx, "Memory allocation failed in %s", who);
            return val_null();
        }
        for (int i = 0; i < num_args; i++) {
            task_args[i] = value_deep_copy(args[i]);  // thread isolation
        }
    }

    Task *task = task_new(atomic_fetch_add(&next_task_id, 1), fn, task_args, num_args, fn->closure_env);
    task->detached = opts->detached;
    if (opts->name) {
        task->name = strdup(opts->name);
    }
    task->thread = malloc(sizeof(pthread_t));
    if (!task->thread) {
        task_release(task);
        runtime_error(ctx, "Memory allocation failed");
        return val_null();
    }

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    // The interpreter's eval_stmt recurses per nested statement, so tasks
    // need far more than the default pthread stack.
    pthread_attr_setstacksize(&attr, opts->stack_size);
    if (opts->detached) {
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    }

    task_retain(task);  // the worker's reference (released in task_thread_main)
    __atomic_store_n(&g_interp_has_spawned, 1, __ATOMIC_SEQ_CST);
    int rc = pthread_create((pthread_t *)task->thread, &attr, task_thread_main, task);
    pthread_attr_destroy(&attr);

    if (rc != 0) {
        // No thread will ever run: drop both references. task_free() must
        // not touch the never-created pthread.
        free(task->thread);
        task->thread = NULL;
        task_release(task);
        task_release(task);
        runtime_error(ctx, "Failed to create thread: %d", rc);
        return val_null();
    }

    if (opts->detached) {
        task_release(task);  // fire and forget: only the worker keeps a reference
        return val_null();
    }
    return val_task(task);
}

Value builtin_spawn(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args < 1) {
        runtime_error(ctx, "spawn() expects at least 1 argument (async function)");
        return val_null();
    }
    if (args[0].type != VAL_FUNCTION) {
        runtime_error(ctx, "spawn() expects an async function");
        return val_null();
    }
    SpawnOptions opts = { .stack_size = atomic_load(&g_default_stack_size), .name = NULL, .detached = 0 };
    return spawn_task("spawn()", args[0].as.as_function, args + 1, num_args - 1, &opts, ctx);
}

// spawn_with(options, fn, args...) - options: { stack_size?: int, name?: string }
Value builtin_spawn_with(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args < 2) {
        runtime_error(ctx, "spawn_with() expects at least 2 arguments (options, async function)");
        return val_null();
    }
    if (args[0].type != VAL_OBJECT) {
        runtime_error(ctx, "spawn_with() first argument must be an options object");
        return val_null();
    }
    if (args[1].type != VAL_FUNCTION) {
        runtime_error(ctx, "spawn_with() second argument must be an async function");
        return val_null();
    }

    SpawnOptions opts = { .stack_size = atomic_load(&g_default_stack_size), .name = NULL, .detached = 0 };
    Object *options = args[0].as.as_object;
    for (int i = 0; i < options->num_fields; i++) {
        const char *key = options->fields[i].name;
        Value v = options->fields[i].value;
        if (strcmp(key, "stack_size") == 0) {
            if (!is_integer(v)) {
                runtime_error(ctx, "spawn_with() stack_size must be an integer");
                return val_null();
            }
            int64_t sz = value_to_int64(v);
            if (sz <= 0) {
                runtime_error(ctx, "spawn_with() stack_size must be positive");
                return val_null();
            }
            opts.stack_size = (size_t)sz;
        } else if (strcmp(key, "name") == 0) {
            if (v.type != VAL_STRING) {
                runtime_error(ctx, "spawn_with() name must be a string");
                return val_null();
            }
            opts.name = v.as.as_string->data;
        }
    }
    return spawn_task("spawn_with()", args[1].as.as_function, args + 2, num_args - 2, &opts, ctx);
}

Value builtin_get_default_stack_size(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args; (void)ctx;
    return val_i64((int64_t)atomic_load(&g_default_stack_size));
}

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

// ========== JOIN / DETACH ==========

Value builtin_join(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "join() expects 1 argument (task handle)");
        return val_null();
    }
    if (args[0].type != VAL_TASK) {
        runtime_error(ctx, "join() expects a task handle");
        return val_null();
    }
    Task *task = args[0].as.as_task;

    // Claim the join under the lock so concurrent join()/detach() serialize.
    pthread_mutex_lock((pthread_mutex_t *)task->task_mutex);
    if (task->joined) {
        pthread_mutex_unlock((pthread_mutex_t *)task->task_mutex);
        runtime_error(ctx, "task handle already joined");
        return val_null();
    }
    if (task->detached) {
        pthread_mutex_unlock((pthread_mutex_t *)task->task_mutex);
        runtime_error(ctx, "cannot join detached task");
        return val_null();
    }
    task->joined = 1;
    pthread_mutex_unlock((pthread_mutex_t *)task->task_mutex);

    int rc = pthread_join(*(pthread_t *)task->thread, NULL);
    if (rc != 0) {
        runtime_error(ctx, "pthread_join failed: %d", rc);
        return val_null();
    }

    // The worker has exited: its state is now ours to read without the lock.
    if (task->ctx->exception_state.is_throwing) {
        // Re-throw in the joining context. The exception value's single
        // reference moves to the caller (exec_context_free does not release it).
        ctx->exception_state = task->ctx->exception_state;
        return val_null();
    }
    Value result = task->result;
    VALUE_RETAIN(result);  // the task keeps its own reference until freed
    return result;
}

Value builtin_detach(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args < 1) {
        runtime_error(ctx, "detach() expects at least 1 argument");
        return val_null();
    }

    // detach(task_handle)
    if (args[0].type == VAL_TASK) {
        if (num_args != 1) {
            runtime_error(ctx, "detach() with task handle expects exactly 1 argument");
            return val_null();
        }
        Task *task = args[0].as.as_task;

        pthread_mutex_lock((pthread_mutex_t *)task->task_mutex);
        if (task->joined) {
            pthread_mutex_unlock((pthread_mutex_t *)task->task_mutex);
            runtime_error(ctx, "cannot detach already joined task");
            return val_null();
        }
        if (task->detached) {
            pthread_mutex_unlock((pthread_mutex_t *)task->task_mutex);
            runtime_error(ctx, "task already detached");
            return val_null();
        }
        task->detached = 1;
        pthread_mutex_unlock((pthread_mutex_t *)task->task_mutex);

        int rc = pthread_detach(*(pthread_t *)task->thread);
        if (rc != 0) {
            runtime_error(ctx, "pthread_detach failed: %d", rc);
        }
        return val_null();
    }

    // detach(async_fn, args...) - spawn already detached (fire and forget)
    if (args[0].type == VAL_FUNCTION) {
        SpawnOptions opts = { .stack_size = atomic_load(&g_default_stack_size), .name = NULL, .detached = 1 };
        return spawn_task("detach()", args[0].as.as_function, args + 1, num_args - 1, &opts, ctx);
    }

    runtime_error(ctx, "detach() expects either a task handle or an async function");
    return val_null();
}

// ========== CHANNELS ==========

Value builtin_channel(Value *args, int num_args, ExecutionContext *ctx) {
    int capacity = 0;  // unbuffered by default
    if (num_args > 0) {
        if (args[0].type != VAL_I32 && args[0].type != VAL_U32) {
            runtime_error(ctx, "channel() capacity must be an integer");
            return val_null();
        }
        capacity = value_to_int(args[0]);
        if (capacity < 0) {
            runtime_error(ctx, "channel() capacity cannot be negative");
            return val_null();
        }
    }
    return val_channel(channel_new(capacity));
}

// Build { channel, value }. Takes ownership of `value`; retains `channel`.
static Value make_select_result(Value channel, Value value) {
    Object *obj = object_new(NULL, 2);
    if (obj) {
        obj->fields[0].name = strdup("channel");
        obj->fields[1].name = strdup("value");
    }
    if (!obj || !obj->fields[0].name || !obj->fields[1].name) {
        if (obj) {
            object_free(obj);
        }
        value_release(value);
        return val_null();
    }
    value_retain(channel);
    obj->fields[0].value = channel;
    obj->fields[1].value = value;
    obj->num_fields = 2;
    return val_object(obj);
}

// select(channels: array<channel>, timeout_ms?: i32) -> { channel, value } | null
// Blocks (no polling) until one channel has a value or is closed and drained.
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
    if (channels->length == 0) {
        runtime_error(ctx, "select() requires at least one channel");
        return val_null();
    }

    struct timespec deadline, *deadline_ptr = NULL;
    if (num_args > 1) {
        if (!is_integer(args[1])) {
            runtime_error(ctx, "select() timeout must be an integer (milliseconds)");
            return val_null();
        }
        int timeout_ms = value_to_int(args[1]);
        if (timeout_ms >= 0) {  // negative = wait forever
            hml_chan_deadline_after_ms(&deadline, timeout_ms);
            deadline_ptr = &deadline;
        }
    }

    HmlChan **chans = malloc(sizeof(HmlChan *) * channels->length);
    if (!chans) {
        runtime_error(ctx, "Memory allocation failed in select()");
        return val_null();
    }
    for (int i = 0; i < channels->length; i++) {
        if (channels->elements[i].type != VAL_CHANNEL) {
            free(chans);
            runtime_error(ctx, "select() array must contain only channels");
            return val_null();
        }
        chans[i] = channels->elements[i].as.as_channel->chan;
    }

    int index = 0;
    Value value;
    HmlChanStatus status = hml_chan_select(chans, channels->length, deadline_ptr, &index, &value);
    free(chans);

    switch (status) {
        case HML_CHAN_OK:      return make_select_result(channels->elements[index], value);
        case HML_CHAN_CLOSED:  return make_select_result(channels->elements[index], val_null());
        case HML_CHAN_TIMEOUT: break;
    }
    return val_null();
}

// ========== DEBUG ==========

Value builtin_task_debug_info(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "task_debug_info() expects 1 argument (task handle)");
        return val_null();
    }
    if (args[0].type != VAL_TASK) {
        runtime_error(ctx, "task_debug_info() expects a task handle");
        return val_null();
    }
    Task *task = args[0].as.as_task;

    pthread_mutex_lock((pthread_mutex_t *)task->task_mutex);
    const char *state = "UNKNOWN";
    switch (task->state) {
        case TASK_READY:     state = "READY"; break;
        case TASK_RUNNING:   state = "RUNNING"; break;
        case TASK_BLOCKED:   state = "BLOCKED"; break;
        case TASK_COMPLETED: state = "COMPLETED"; break;
    }
    printf("=== Task Debug Info ===\n");
    printf("Task ID: %d\n", task->id);
    printf("State: %s\n", state);
    printf("Joined: %s\n", task->joined ? "true" : "false");
    printf("Detached: %s\n", task->detached ? "true" : "false");
    printf("Ref Count: %d\n", __atomic_load_n(&task->ref_count, __ATOMIC_SEQ_CST));
    printf("Has Result: %s\n", task->state == TASK_COMPLETED ? "true" : "false");
    printf("Exception: %s\n", task->ctx->exception_state.is_throwing ? "true" : "false");
    printf("======================\n");
    pthread_mutex_unlock((pthread_mutex_t *)task->task_mutex);

    return val_null();
}
