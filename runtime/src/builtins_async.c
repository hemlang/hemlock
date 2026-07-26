/*
 * Hemlock Runtime Library - Async/Concurrency Operations
 *
 * Task spawning, joining, channels, and synchronization primitives.
 *
 * Build modes:
 *   Native:              Full implementation with pthreads + libffi
 *   WASM with pthreads:  Pthreads via Web Workers (emcc -pthread), no libffi
 *   WASM without pthreads: Excluded entirely; stubs in wasm_shim.c
 */

#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)

#define _GNU_SOURCE
#include "builtins_internal.h"
#include <pthread.h>
#include <stdatomic.h>
#include <signal.h>

#if !defined(__EMSCRIPTEN__) && !defined(_WIN32)
#include <poll.h>
#endif

static atomic_int g_next_task_id = 1;

// Global default stack size for spawned threads (atomic for thread-safety)
static _Atomic size_t g_default_stack_size = HML_THREAD_STACK_SIZE;

// ========== FUNCTION DISPATCH ==========
//
// Native builds use libffi for calling functions with >8 parameters.
// WASM builds use direct dispatch only (libffi not available in Emscripten).

#if !defined(__EMSCRIPTEN__) && !defined(HEMLOCK_NO_FFI)
// Define ffi_type for HmlValue struct (16 bytes: 4 type + 4 padding + 8 union)
static ffi_type *hml_value_elements[] = {
    &ffi_type_uint32,   // HmlValueType (enum)
    &ffi_type_uint32,   // padding
    &ffi_type_uint64,   // union as (8 bytes)
    NULL
};

static ffi_type hml_value_ffi_type = {
    .size = 0,
    .alignment = 0,
    .type = FFI_TYPE_STRUCT,
    .elements = hml_value_elements
};

// Call a Hemlock function with arbitrary number of arguments using libffi
// Function signature: HmlValue fn(void* closure_env, HmlValue arg0, ...)
static HmlValue call_hemlock_function_ffi(void *fn_ptr, void *closure_env, HmlValue *args, int num_args) {
    // Total args = 1 (closure_env) + num_args (HmlValue args)
    int total_args = 1 + num_args;

    // Prepare argument types
    ffi_type **arg_types = malloc(sizeof(ffi_type*) * total_args);
    if (!arg_types) {
        hml_runtime_error("Failed to allocate FFI argument types for async function call");
        return hml_val_null();
    }
    arg_types[0] = &ffi_type_pointer;  // closure_env
    for (int i = 0; i < num_args; i++) {
        arg_types[i + 1] = &hml_value_ffi_type;
    }

    // Prepare call interface
    ffi_cif cif;
    ffi_status status = ffi_prep_cif(&cif, FFI_DEFAULT_ABI, total_args,
                                      &hml_value_ffi_type, arg_types);
    if (status != FFI_OK) {
        free(arg_types);
        hml_runtime_error("Failed to prepare FFI call interface for async function");
        return hml_val_null();
    }

    // Prepare argument values (pointers to the actual values)
    void **arg_values = malloc(sizeof(void*) * total_args);
    if (!arg_values) {
        free(arg_types);
        hml_runtime_error("Failed to allocate FFI argument values for async function call");
        return hml_val_null();
    }
    arg_values[0] = &closure_env;
    for (int i = 0; i < num_args; i++) {
        arg_values[i + 1] = &args[i];
    }

    // Make the call
    HmlValue result;
    ffi_call(&cif, FFI_FN(fn_ptr), &result, arg_values);

    // Cleanup
    free(arg_types);
    free(arg_values);

    return result;
}
#endif // !__EMSCRIPTEN__ && !HEMLOCK_NO_FFI

// Thread wrapper function
static void* task_thread_wrapper(void* arg) {
    HmlTask *task = (HmlTask*)arg;

    // Block all signals in worker threads so asynchronous signals
    // (SIGINT/SIGTERM/SIGHUP, ...) are always delivered to the main thread.
    // The C signal handler calls back into the VM (hml_call_function), which
    // is not safe to run on an arbitrary worker that may be holding a malloc
    // or runtime lock. Without this, a server whose accept loop runs in a
    // spawned task (e.g. serve_async) could have the signal delivered to a
    // worker and the registered shutdown handler would never run reliably,
    // leaving the process to take the default (terminating) action. This
    // matches the interpreter backend, which masks all signals in its task
    // threads for the same reason.
    // (POSIX only: Windows delivers console control events on its own thread)
#if !defined(__EMSCRIPTEN__) && !defined(_WIN32)
    sigset_t block_set;
    sigfillset(&block_set);
    pthread_sigmask(SIG_BLOCK, &block_set, NULL);
#endif

    // Set thread name if provided (must be called from within the thread for macOS)
    // Not available in Emscripten/WASM
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

    // macOS aggressively coalesces timers on threads inheriting a low QoS
    // class — a process launched from launchd / nohup / a background shell
    // can end up with QOS_CLASS_BACKGROUND or QOS_CLASS_UTILITY, in which
    // case nanosleep() can run anywhere from 30s up to ~5 minutes for the
    // *same* requested 30-second sleep. That's death for any heartbeat /
    // poll loop that depends on punctual wakeups (e.g. a worker phoning
    // home to a control plane every N seconds — the CP marks it stale,
    // even though the process is healthy and the sleep eventually returns).
    // Pin spawn'd-task threads to USER_INITIATED so timer coalescing stays
    // off and sleep() means roughly what it says.
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
#endif

    // Mark as running
    pthread_mutex_lock(&task->sync->mutex);
    task->state = HML_TASK_RUNNING;
    pthread_mutex_unlock(&task->sync->mutex);

    // Get function info
    HmlFunction *fn = task->function.as.as_function;
#if !defined(__EMSCRIPTEN__) && !defined(HEMLOCK_NO_FFI)
    void *fn_ptr = fn->fn_ptr;
    void *closure_env = fn->closure_env;
#endif

    // Prefer direct call dispatcher for common arities; fall back to libffi for large signatures.
    int can_use_direct_call = 0;
    if (fn->has_rest_param) {
        can_use_direct_call = (fn->num_params + 1) <= 8;
    } else {
        can_use_direct_call = fn->num_params <= 8;
    }

    // Set up exception handler so task exceptions are captured instead of crashing
    // the whole process. This matches the interpreter's behavior where task exceptions
    // are stored and re-thrown on join().
    HmlExceptionContext *ex_ctx = hml_exception_push();
    HmlValue result;

    if (setjmp(ex_ctx->exception_buf) == 0) {
        // Normal execution path
        if (can_use_direct_call) {
            result = hml_call_function(task->function, task->args, task->num_args);
        } else {
#if defined(__EMSCRIPTEN__) || defined(HEMLOCK_NO_FFI)
            // libffi not available, direct dispatch only supports up to 8 params
            hml_runtime_error("spawn(): functions with >8 parameters not supported in this build");
            result = hml_val_null();
#else
            result = call_hemlock_function_ffi(fn_ptr, closure_env, task->args, task->num_args);
#endif
        }
        hml_exception_pop();
    } else {
        // Exception was thrown - capture it for propagation on join()
        HmlValue exc = hml_exception_get_value();
        hml_exception_pop();

        pthread_mutex_lock(&task->sync->mutex);
        task->has_exception = 1;
        task->exception_value = exc;
        hml_retain(&task->exception_value);
        task->result = hml_val_null();
        task->state = HML_TASK_COMPLETED;
        pthread_cond_signal(&task->sync->cond);
        pthread_mutex_unlock(&task->sync->mutex);

        hml_release(&exc);

        // Release the thread's reference to the task
        HmlValue task_val = { .type = HML_VAL_TASK, .as.as_task = task };
        hml_release(&task_val);
        return NULL;
    }

    // Store result and mark as completed
    pthread_mutex_lock(&task->sync->mutex);
    task->result = result;
    task->state = HML_TASK_COMPLETED;
    pthread_cond_signal(&task->sync->cond);
    pthread_mutex_unlock(&task->sync->mutex);

    // Release the thread's reference to the task
    HmlValue task_val = { .type = HML_VAL_TASK, .as.as_task = task };
    hml_release(&task_val);
    return NULL;
}

HmlValue hml_spawn(HmlValue fn, HmlValue *args, int num_args) {
    if (fn.type != HML_VAL_FUNCTION) {
        hml_runtime_error("spawn() expects a function");
    }

    // Verify function is async (for parity with interpreter)
    HmlFunction *func = fn.as.as_function;
    if (!func->is_async) {
        hml_runtime_error("spawn() requires an async function");
    }

    // Create task
    HmlTask *task = malloc(sizeof(HmlTask));
    task->id = atomic_fetch_add(&g_next_task_id, 1);
    task->state = HML_TASK_READY;
    task->result = hml_val_null();
    task->joined = 0;
    task->detached = 0;
    task->has_exception = 0;
    task->exception_value = hml_val_null();
    // ref_count=2: one for the caller, one for the thread wrapper.
    // The thread releases its reference on completion, preventing
    // use-after-free when the caller drops the task immediately.
    task->ref_count = 2;

    // Store function and args
    task->function = fn;
    hml_retain(&task->function);
    task->num_args = num_args;
    if (num_args > 0) {
        task->args = malloc(sizeof(HmlValue) * num_args);
        for (int i = 0; i < num_args; i++) {
            // Deep copy arguments to prevent sharing mutable state between threads.
            // This matches the interpreter's behavior and prevents data races with
            // mutable types like strings, arrays, and objects.
            task->args[i] = hml_value_deep_copy(args[i]);
        }
    } else {
        task->args = NULL;
    }

    // Initialize sync structures in single allocation (reduces fragmentation)
    task->sync = malloc(sizeof(HmlTaskSync));
    pthread_mutex_init(&task->sync->mutex, NULL);
    pthread_cond_init(&task->sync->cond, NULL);

    // Initialize name
    task->name = NULL;

    // Create thread with default stack size
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, atomic_load(&g_default_stack_size));
    pthread_create(&task->sync->thread, &attr, task_thread_wrapper, task);
    pthread_attr_destroy(&attr);

    // Return task value
    HmlValue result;
    result.type = HML_VAL_TASK;
    result.as.as_task = task;
    return result;
}

// spawn_with(options, fn, args...) - Spawn with configuration options
HmlValue hml_spawn_with(HmlValue options, HmlValue fn, HmlValue *args, int num_args) {
    if (options.type != HML_VAL_OBJECT) {
        hml_runtime_error("spawn_with() first argument must be an options object");
    }

    if (fn.type != HML_VAL_FUNCTION) {
        hml_runtime_error("spawn_with() second argument must be a function");
    }

    HmlFunction *func = fn.as.as_function;
    if (!func->is_async) {
        hml_runtime_error("spawn_with() requires an async function");
    }

    // Extract options
    size_t stack_size = atomic_load(&g_default_stack_size);
    const char *thread_name = NULL;

    if (hml_object_has_field(options, "stack_size")) {
        HmlValue sv = hml_object_get_field(options, "stack_size");
        if (!hml_is_integer(sv)) {
            hml_release(&sv);
            hml_runtime_error("spawn_with() stack_size must be an integer");
        }
        int64_t sz = hml_to_i64(sv);
        hml_release(&sv);
        if (sz <= 0) {
            hml_runtime_error("spawn_with() stack_size must be positive");
        }
        stack_size = (size_t)sz;
    }

    // nv stays live until task->name is strdup'd below (thread_name borrows
    // its string data); released right after.
    HmlValue nv = hml_val_null();
    if (hml_object_has_field(options, "name")) {
        nv = hml_object_get_field(options, "name");
        if (nv.type != HML_VAL_STRING) {
            hml_release(&nv);
            hml_runtime_error("spawn_with() name must be a string");
        }
        thread_name = hml_to_string_ptr(nv);
    }

    // Create task
    HmlTask *task = malloc(sizeof(HmlTask));
    task->id = atomic_fetch_add(&g_next_task_id, 1);
    task->state = HML_TASK_READY;
    task->result = hml_val_null();
    task->joined = 0;
    task->detached = 0;
    task->has_exception = 0;
    task->exception_value = hml_val_null();
    // ref_count=2: one for the caller, one for the thread wrapper.
    // The thread releases its reference on completion, preventing
    // use-after-free when the caller drops the task immediately.
    task->ref_count = 2;

    // Store function and args
    task->function = fn;
    hml_retain(&task->function);
    task->num_args = num_args;
    if (num_args > 0) {
        task->args = malloc(sizeof(HmlValue) * num_args);
        for (int i = 0; i < num_args; i++) {
            task->args[i] = hml_value_deep_copy(args[i]);
        }
    } else {
        task->args = NULL;
    }

    // Set debug name (copies nv's data; the name option ref can go now)
    task->name = thread_name ? strdup(thread_name) : NULL;
    hml_release(&nv);

    // Initialize sync structures
    task->sync = malloc(sizeof(HmlTaskSync));
    pthread_mutex_init(&task->sync->mutex, NULL);
    pthread_cond_init(&task->sync->cond, NULL);

    // Create thread with requested stack size
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, stack_size);
    pthread_create(&task->sync->thread, &attr, task_thread_wrapper, task);
    pthread_attr_destroy(&attr);

    HmlValue result;
    result.type = HML_VAL_TASK;
    result.as.as_task = task;
    return result;
}

// get_default_stack_size() - Returns the current default thread stack size
HmlValue hml_get_default_stack_size(void) {
    return hml_val_i64((int64_t)atomic_load(&g_default_stack_size));
}

// set_default_stack_size(size) - Sets the default thread stack size
void hml_set_default_stack_size(HmlValue size) {
    if (!hml_is_integer(size)) {
        hml_runtime_error("set_default_stack_size() argument must be an integer");
    }
    int64_t sz = hml_to_i64(size);
    if (sz <= 0) {
        hml_runtime_error("set_default_stack_size() size must be positive");
    }
    atomic_store(&g_default_stack_size, (size_t)sz);
}

HmlValue hml_join(HmlValue task_val) {
    if (task_val.type != HML_VAL_TASK) {
        hml_runtime_error("join() expects a task");
    }

    HmlTask *task = task_val.as.as_task;

    // Lock mutex BEFORE checking flags to prevent TOCTOU race condition
    // Multiple threads calling join() simultaneously must be serialized
    pthread_mutex_lock(&task->sync->mutex);

    if (task->joined) {
        pthread_mutex_unlock(&task->sync->mutex);
        hml_runtime_error_loc("task handle already joined");
    }

    if (task->detached) {
        pthread_mutex_unlock(&task->sync->mutex);
        hml_runtime_error_loc("cannot join detached task");
    }

    // Mark as joined while holding mutex to prevent concurrent join/detach
    task->joined = 1;

    // Wait for task to complete
    while (task->state != HML_TASK_COMPLETED) {
        pthread_cond_wait(&task->sync->cond, &task->sync->mutex);
    }

    // Get result while holding mutex
    HmlValue result = task->result;
    hml_retain(&result);
    int had_exception = task->has_exception;
    HmlValue exception = task->exception_value;
    if (had_exception) {
        hml_retain(&exception);
    }

    pthread_mutex_unlock(&task->sync->mutex);

    // Join the thread (outside mutex to avoid blocking other operations)
    pthread_join(task->sync->thread, NULL);

    // Re-throw task exception in the joining thread (parity with interpreter)
    if (had_exception) {
        hml_release(&result);
        hml_throw(exception);
    }

    return result;
}

void hml_detach(HmlValue task_val) {
    if (task_val.type != HML_VAL_TASK) {
        hml_runtime_error("detach() expects a task");
    }

    HmlTask *task = task_val.as.as_task;

    // Lock mutex BEFORE checking flags to prevent TOCTOU race condition
    // Multiple threads calling detach() or join()/detach() simultaneously must be serialized
    pthread_mutex_lock(&task->sync->mutex);

    if (task->joined) {
        pthread_mutex_unlock(&task->sync->mutex);
        hml_runtime_error("cannot detach already joined task");
    }

    if (task->detached) {
        pthread_mutex_unlock(&task->sync->mutex);
        return; // Already detached
    }

    // Mark as detached while holding mutex to prevent concurrent join/detach
    task->detached = 1;

    pthread_mutex_unlock(&task->sync->mutex);

    // Detach the pthread (outside mutex - this is a pthread operation)
    pthread_detach(task->sync->thread);
}

// task_debug_info(task) - Print debug information about a task
void hml_task_debug_info(HmlValue task_val) {
    if (task_val.type != HML_VAL_TASK) {
        hml_runtime_error("task_debug_info() expects a task");
    }

    HmlTask *task = task_val.as.as_task;

    // Lock mutex to safely read task state
    pthread_mutex_lock(&task->sync->mutex);

    printf("=== Task Debug Info ===\n");
    printf("Task ID: %d\n", task->id);
    printf("State: ");
    switch (task->state) {
        case HML_TASK_READY: printf("READY\n"); break;
        case HML_TASK_RUNNING: printf("RUNNING\n"); break;
        case HML_TASK_COMPLETED: printf("COMPLETED\n"); break;
        default: printf("UNKNOWN\n"); break;
    }
    printf("Joined: %s\n", task->joined ? "true" : "false");
    printf("Detached: %s\n", task->detached ? "true" : "false");
    printf("Ref Count: %d\n", atomic_load(&task->ref_count));
    printf("Has Result: %s\n", task->result.type != HML_VAL_NULL ? "true" : "false");
    printf("======================\n");

    pthread_mutex_unlock(&task->sync->mutex);
}

// apply(fn, args_array) - Call a function with an array of arguments
HmlValue hml_apply(HmlValue fn, HmlValue args_array) {
    if (fn.type != HML_VAL_FUNCTION) {
        hml_runtime_error("apply() first argument must be a function");
    }

    if (args_array.type != HML_VAL_ARRAY) {
        hml_runtime_error("apply() second argument must be an array");
    }

    HmlFunction *func = fn.as.as_function;
    HmlArray *arr = args_array.as.as_array;

#if defined(__EMSCRIPTEN__) || defined(HEMLOCK_NO_FFI)
    // No libffi: use direct call dispatcher
    // Supports up to 8 parameters which covers virtually all real use cases
    (void)func;
    return hml_call_function(fn, arr->elements, arr->length);
#else
    // Native: use libffi for arbitrary parameter count
    return call_hemlock_function_ffi(func->fn_ptr, func->closure_env, arr->elements, arr->length);
#endif
}

// Channel functions
HmlValue hml_channel(int32_t capacity) {
    HmlChannel *ch = malloc(sizeof(HmlChannel));
    ch->capacity = capacity;
    ch->head = 0;
    ch->tail = 0;
    ch->count = 0;
    ch->closed = 0;
    ch->ref_count = 1;

    // Only allocate buffer for buffered channels (capacity > 0)
    if (capacity > 0) {
        ch->buffer = malloc(sizeof(HmlValue) * capacity);
    } else {
        ch->buffer = NULL;
    }

    // Initialize sync structures in single allocation (reduces fragmentation)
    ch->sync = malloc(sizeof(HmlChannelSync));
    pthread_mutex_init(&ch->sync->mutex, NULL);
    pthread_cond_init(&ch->sync->not_empty, NULL);
    pthread_cond_init(&ch->sync->not_full, NULL);
    pthread_cond_init(&ch->sync->rendezvous, NULL);

    // Initialize unbuffered channel fields (value is inline, no separate alloc)
    ch->unbuffered_value = hml_val_null();
    ch->sender_waiting = 0;
    ch->receiver_waiting = 0;

    HmlValue result;
    result.type = HML_VAL_CHANNEL;
    result.as.as_channel = ch;
    return result;
}

void hml_channel_send(HmlValue channel, HmlValue value) {
    if (channel.type != HML_VAL_CHANNEL) {
        hml_runtime_error("send() expects a channel");
    }

    HmlChannel *ch = channel.as.as_channel;

    pthread_mutex_lock(&ch->sync->mutex);

    // Check if channel is closed
    if (ch->closed) {
        pthread_mutex_unlock(&ch->sync->mutex);
        hml_runtime_error("cannot send to closed channel");
    }

    if (ch->capacity == 0) {
        // Unbuffered channel - rendezvous with receiver.
        // If another sender already staged a value, wait for its rendezvous
        // to complete first: overwriting ch->unbuffered_value here leaked the
        // other sender's retained value and silently dropped its message
        // (its send() still reported success).
        while (ch->sender_waiting && !ch->closed) {
            pthread_cond_wait(&ch->sync->not_full, &ch->sync->mutex);
        }
        if (ch->closed) {
            pthread_mutex_unlock(&ch->sync->mutex);
            hml_runtime_error("cannot send to closed channel");
        }

        hml_retain(&value);
        ch->unbuffered_value = value;
        ch->sender_waiting = 1;

        // Signal any waiting receiver that data is available
        pthread_cond_signal(&ch->sync->not_empty);

        // Wait for receiver to pick up the value
        while (ch->sender_waiting && !ch->closed) {
            pthread_cond_wait(&ch->sync->rendezvous, &ch->sync->mutex);
        }

        // Check if we were woken because channel closed
        if (ch->closed && ch->sender_waiting) {
            ch->sender_waiting = 0;
            hml_release(&ch->unbuffered_value);
            ch->unbuffered_value = hml_val_null();
            pthread_mutex_unlock(&ch->sync->mutex);
            hml_runtime_error("cannot send to closed channel");
        }

        pthread_mutex_unlock(&ch->sync->mutex);
        return;
    }

    // Buffered channel - wait while buffer is full
    while (ch->count >= ch->capacity && !ch->closed) {
        pthread_cond_wait(&ch->sync->not_full, &ch->sync->mutex);
    }

    // Check again if closed after waking up
    if (ch->closed) {
        pthread_mutex_unlock(&ch->sync->mutex);
        hml_runtime_error("cannot send to closed channel");
    }

    // Add value to buffer
    ch->buffer[ch->tail] = value;
    hml_retain(&ch->buffer[ch->tail]);
    ch->tail = (ch->tail + 1) % ch->capacity;
    ch->count++;

    pthread_cond_signal(&ch->sync->not_empty);
    pthread_mutex_unlock(&ch->sync->mutex);
}

HmlValue hml_channel_recv(HmlValue channel) {
    if (channel.type != HML_VAL_CHANNEL) {
        hml_runtime_error("recv() expects a channel");
    }

    HmlChannel *ch = channel.as.as_channel;

    pthread_mutex_lock(&ch->sync->mutex);

    if (ch->capacity == 0) {
        // Unbuffered channel - rendezvous with sender
        // Wait for sender to have data available
        while (!ch->sender_waiting && !ch->closed) {
            pthread_cond_wait(&ch->sync->not_empty, &ch->sync->mutex);
        }

        // If channel is closed and no sender waiting, return null
        if (!ch->sender_waiting && ch->closed) {
            pthread_mutex_unlock(&ch->sync->mutex);
            return hml_val_null();
        }

        // Get the value from sender
        HmlValue value = ch->unbuffered_value;
        ch->unbuffered_value = hml_val_null();
        ch->sender_waiting = 0;

        // Signal sender that value was received, and wake any sender
        // queued waiting for the rendezvous slot to free up
        pthread_cond_signal(&ch->sync->rendezvous);
        pthread_cond_signal(&ch->sync->not_full);
        pthread_mutex_unlock(&ch->sync->mutex);

        return value;
    }

    // Buffered channel - wait while buffer is empty
    while (ch->count == 0 && !ch->closed) {
        pthread_cond_wait(&ch->sync->not_empty, &ch->sync->mutex);
    }

    if (ch->count == 0 && ch->closed) {
        pthread_mutex_unlock(&ch->sync->mutex);
        return hml_val_null();
    }

    // Get value from buffer
    HmlValue value = ch->buffer[ch->head];
    ch->head = (ch->head + 1) % ch->capacity;
    ch->count--;

    pthread_cond_signal(&ch->sync->not_full);
    pthread_mutex_unlock(&ch->sync->mutex);

    return value;
}

// channel.recv_timeout(timeout_ms) - receive with timeout, returns null on timeout
HmlValue hml_channel_recv_timeout(HmlValue channel, HmlValue timeout_val) {
    if (channel.type != HML_VAL_CHANNEL) {
        hml_runtime_error("recv_timeout() expects a channel");
    }

    int timeout_ms = hml_to_i32(timeout_val);
    HmlChannel *ch = channel.as.as_channel;

    // Calculate deadline
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (deadline.tv_nsec >= 1000000000) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000;
    }

    pthread_mutex_lock(&ch->sync->mutex);

    if (ch->capacity == 0) {
        // Unbuffered channel with timeout - rendezvous with sender
        // Wait for sender to have data available (with timeout)
        while (!ch->sender_waiting && !ch->closed) {
            int rc = pthread_cond_timedwait(&ch->sync->not_empty,
                                            &ch->sync->mutex, &deadline);
            if (rc == ETIMEDOUT) {
                pthread_mutex_unlock(&ch->sync->mutex);
                return hml_val_null();  // Timeout
            }
        }

        // If channel is closed and no sender waiting, return null
        if (!ch->sender_waiting && ch->closed) {
            pthread_mutex_unlock(&ch->sync->mutex);
            return hml_val_null();
        }

        // Get the value from sender
        HmlValue value = ch->unbuffered_value;
        ch->unbuffered_value = hml_val_null();
        ch->sender_waiting = 0;

        // Signal sender that value was received, and wake any sender
        // queued waiting for the rendezvous slot to free up
        pthread_cond_signal(&ch->sync->rendezvous);
        pthread_cond_signal(&ch->sync->not_full);
        pthread_mutex_unlock(&ch->sync->mutex);

        return value;
    }

    // Buffered channel - wait while buffer is empty
    while (ch->count == 0 && !ch->closed) {
        int rc = pthread_cond_timedwait(&ch->sync->not_empty,
                                        &ch->sync->mutex, &deadline);
        if (rc == ETIMEDOUT) {
            pthread_mutex_unlock(&ch->sync->mutex);
            return hml_val_null();  // Timeout
        }
    }

    if (ch->count == 0 && ch->closed) {
        pthread_mutex_unlock(&ch->sync->mutex);
        return hml_val_null();
    }

    // Get value from buffer
    HmlValue value = ch->buffer[ch->head];
    ch->head = (ch->head + 1) % ch->capacity;
    ch->count--;

    pthread_cond_signal(&ch->sync->not_full);
    pthread_mutex_unlock(&ch->sync->mutex);

    return value;
}

// channel.send_timeout(value, timeout_ms) - send with timeout, returns bool (true if sent)
HmlValue hml_channel_send_timeout(HmlValue channel, HmlValue value, HmlValue timeout_val) {
    if (channel.type != HML_VAL_CHANNEL) {
        hml_runtime_error("send_timeout() expects a channel");
    }

    int timeout_ms = hml_to_i32(timeout_val);
    HmlChannel *ch = channel.as.as_channel;

    // Calculate deadline
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (deadline.tv_nsec >= 1000000000) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000;
    }

    pthread_mutex_lock(&ch->sync->mutex);

    // Check if channel is closed
    if (ch->closed) {
        pthread_mutex_unlock(&ch->sync->mutex);
        hml_runtime_error("cannot send to closed channel");
    }

    if (ch->capacity == 0) {
        // Unbuffered channel with timeout - rendezvous with receiver.
        // Wait for any in-progress sender's rendezvous first (see
        // hml_channel_send: staging over it leaks + drops that message).
        while (ch->sender_waiting && !ch->closed) {
            int rc = pthread_cond_timedwait(&ch->sync->not_full,
                                            &ch->sync->mutex, &deadline);
            if (rc == ETIMEDOUT) {
                pthread_mutex_unlock(&ch->sync->mutex);
                return hml_val_bool(0);  // Timeout - send failed
            }
        }
        if (ch->closed) {
            pthread_mutex_unlock(&ch->sync->mutex);
            hml_runtime_error("cannot send to closed channel");
        }

        hml_retain(&value);
        ch->unbuffered_value = value;
        ch->sender_waiting = 1;

        // Signal any waiting receiver that data is available
        pthread_cond_signal(&ch->sync->not_empty);

        // Wait for receiver to pick up the value (with timeout)
        while (ch->sender_waiting && !ch->closed) {
            int rc = pthread_cond_timedwait(&ch->sync->rendezvous,
                                            &ch->sync->mutex, &deadline);
            if (rc == ETIMEDOUT) {
                // Timeout - clean up and return failure
                ch->sender_waiting = 0;
                hml_release(&ch->unbuffered_value);
                ch->unbuffered_value = hml_val_null();
                pthread_mutex_unlock(&ch->sync->mutex);
                return hml_val_bool(0);  // Timeout - send failed
            }
        }

        // Check if we were woken because channel closed
        if (ch->closed && ch->sender_waiting) {
            ch->sender_waiting = 0;
            hml_release(&ch->unbuffered_value);
            ch->unbuffered_value = hml_val_null();
            pthread_mutex_unlock(&ch->sync->mutex);
            hml_runtime_error("cannot send to closed channel");
        }

        pthread_mutex_unlock(&ch->sync->mutex);
        return hml_val_bool(1);  // Success
    }

    // Buffered channel - wait while buffer is full
    while (ch->count >= ch->capacity && !ch->closed) {
        int rc = pthread_cond_timedwait(&ch->sync->not_full,
                                        &ch->sync->mutex, &deadline);
        if (rc == ETIMEDOUT) {
            pthread_mutex_unlock(&ch->sync->mutex);
            return hml_val_bool(0);  // Timeout - send failed
        }
    }

    // Check again if closed after waking up
    if (ch->closed) {
        pthread_mutex_unlock(&ch->sync->mutex);
        hml_runtime_error("cannot send to closed channel");
    }

    // Add value to buffer
    ch->buffer[ch->tail] = value;
    hml_retain(&ch->buffer[ch->tail]);
    ch->tail = (ch->tail + 1) % ch->capacity;
    ch->count++;

    pthread_cond_signal(&ch->sync->not_empty);
    pthread_mutex_unlock(&ch->sync->mutex);

    return hml_val_bool(1);  // Success
}

void hml_channel_close(HmlValue channel) {
    if (channel.type != HML_VAL_CHANNEL) {
        return;
    }

    HmlChannel *ch = channel.as.as_channel;

    pthread_mutex_lock(&ch->sync->mutex);
    ch->closed = 1;
    // Wake up all waiting threads
    pthread_cond_broadcast(&ch->sync->not_empty);
    pthread_cond_broadcast(&ch->sync->not_full);
    // Also wake up any unbuffered channel senders waiting on rendezvous
    pthread_cond_broadcast(&ch->sync->rendezvous);
    pthread_mutex_unlock(&ch->sync->mutex);
}

// select(channels, timeout_ms?) - wait on multiple channels
HmlValue hml_select(HmlValue channels, HmlValue timeout) {
    if (channels.type != HML_VAL_ARRAY) {
        hml_runtime_error("select() expects array of channels as first argument");
    }

    HmlArray *arr = channels.as.as_array;
    if (arr->length == 0) {
        hml_runtime_error("select() requires at least one channel");
    }

    // Validate all elements are channels
    for (int i = 0; i < arr->length; i++) {
        if (arr->elements[i].type != HML_VAL_CHANNEL) {
            hml_runtime_error("select() array must contain only channels");
        }
    }

    // Get timeout in milliseconds (-1 means infinite)
    int timeout_ms = -1;
    if (timeout.type != HML_VAL_NULL) {
        timeout_ms = hml_to_i32(timeout);
    }

    // Calculate deadline
    struct timespec deadline;
    struct timespec *deadline_ptr = NULL;
    if (timeout_ms >= 0) {
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += timeout_ms / 1000;
        deadline.tv_nsec += (timeout_ms % 1000) * 1000000;
        if (deadline.tv_nsec >= 1000000000) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000;
        }
        deadline_ptr = &deadline;
    }

    // Polling loop
    while (1) {
        // Check each channel for available data
        for (int i = 0; i < arr->length; i++) {
            HmlChannel *ch = arr->elements[i].as.as_channel;

            pthread_mutex_lock(&ch->sync->mutex);

            // Check for unbuffered channel with sender waiting (rendezvous pattern)
            if (ch->capacity == 0 && ch->sender_waiting) {
                // Get the value from sender
                HmlValue msg = ch->unbuffered_value;
                ch->unbuffered_value = hml_val_null();
                ch->sender_waiting = 0;

                // Signal sender that value was received, and wake any sender
                // queued waiting for the rendezvous slot to free up
                pthread_cond_signal(&ch->sync->rendezvous);
                pthread_cond_signal(&ch->sync->not_full);
                pthread_mutex_unlock(&ch->sync->mutex);

                // Create result object { channel, value }
                HmlValue result = hml_val_object();
                hml_object_set_field(result, "channel", arr->elements[i]);
                hml_object_set_field(result, "value", msg);
                // We took over the channel's reference to msg; set_field
                // retained its own copy, so drop ours.
                hml_release(&msg);
                return result;
            }

            // Check if buffered channel has data
            if (ch->count > 0) {
                // Read the value
                HmlValue msg = ch->buffer[ch->head];
                ch->head = (ch->head + 1) % ch->capacity;
                ch->count--;

                // Signal that buffer is not full
                pthread_cond_signal(&ch->sync->not_full);
                pthread_mutex_unlock(&ch->sync->mutex);

                // Create result object { channel, value }
                HmlValue result = hml_val_object();
                hml_object_set_field(result, "channel", arr->elements[i]);
                hml_object_set_field(result, "value", msg);
                // We took over the buffer slot's reference to msg; set_field
                // retained its own copy, so drop ours.
                hml_release(&msg);
                return result;
            }

            // Check if channel is closed and empty
            if (ch->closed) {
                pthread_mutex_unlock(&ch->sync->mutex);
                // Return object with null value for closed channel
                HmlValue result = hml_val_object();
                hml_object_set_field(result, "channel", arr->elements[i]);
                hml_object_set_field(result, "value", hml_val_null());
                return result;
            }

            pthread_mutex_unlock(&ch->sync->mutex);
        }

        // Check timeout
        if (deadline_ptr) {
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            if (now.tv_sec > deadline_ptr->tv_sec ||
                (now.tv_sec == deadline_ptr->tv_sec && now.tv_nsec >= deadline_ptr->tv_nsec)) {
                return hml_val_null();  // Timeout
            }
        }

        // Sleep briefly before polling again (1ms)
        usleep(1000);
    }
}

// ========== POSIX poll() - native only ==========
// WASM stub provided by wasm_shim.c (even with pthreads, POSIX poll not available)

#ifndef __EMSCRIPTEN__

// Helper to get fd from a socket, file, or object with fd field
static int hml_get_fd_from_value(HmlValue val) {
    if (val.type == HML_VAL_SOCKET) {
        HmlSocket *s = val.as.as_socket;
        return s ? s->fd : -1;
    }
    if (val.type == HML_VAL_FILE) {
        HmlFileHandle *f = val.as.as_file;
        if (f && f->fp) {
            return fileno(f->fp);
        }
        return -1;
    }
    if (val.type == HML_VAL_OBJECT) {
        HmlValue fd_val = hml_object_get_field(val, "fd");
        if (hml_is_integer(fd_val)) {
            return hml_to_i32(fd_val);
        }
    }
    return -1;
}

// poll(fds, timeout_ms) - wait for I/O events on file descriptors
HmlValue hml_poll(HmlValue fds, HmlValue timeout) {
    if (fds.type != HML_VAL_ARRAY) {
        hml_runtime_error("poll() expects array as first argument");
    }

    HmlArray *arr = fds.as.as_array;
    int timeout_ms = hml_to_i32(timeout);

    if (arr->length == 0) {
        // Return empty array
        HmlValue result = hml_val_array();
        return result;
    }

    // Build pollfd array
    struct pollfd *pfds = malloc(sizeof(struct pollfd) * arr->length);
    if (!pfds) {
        hml_runtime_error("poll() memory allocation failed");
    }

    // Store original fd values for return
    HmlValue *original_fds = malloc(sizeof(HmlValue) * arr->length);
    if (!original_fds) {
        free(pfds);
        hml_runtime_error("poll() memory allocation failed");
    }

    for (int i = 0; i < arr->length; i++) {
        HmlValue item = arr->elements[i];

        if (item.type != HML_VAL_OBJECT) {
            for (int j = 0; j < i; j++) hml_release(&original_fds[j]);
            free(pfds);
            free(original_fds);
            hml_runtime_error("poll() array elements must be objects with 'fd' and 'events'");
        }

        // Get fd field. get_field returns an owned (retained) reference;
        // original_fds[i] takes over that ownership - no extra retain
        // (a second retain here leaked one fd reference per entry per call).
        HmlValue fd_val = hml_object_get_field(item, "fd");
        HmlValue events_val = hml_object_get_field(item, "events");

        int fd = hml_get_fd_from_value(fd_val);
        if (fd < 0) {
            hml_release(&fd_val);
            hml_release(&events_val);
            for (int j = 0; j < i; j++) hml_release(&original_fds[j]);
            free(pfds);
            free(original_fds);
            hml_runtime_error("poll() fd must be a socket or file");
        }

        if (!hml_is_integer(events_val)) {
            hml_release(&fd_val);
            hml_release(&events_val);
            for (int j = 0; j < i; j++) hml_release(&original_fds[j]);
            free(pfds);
            free(original_fds);
            hml_runtime_error("poll() events must be an integer");
        }

        pfds[i].fd = fd;
        pfds[i].events = (short)hml_to_i32(events_val);
        pfds[i].revents = 0;
        hml_release(&events_val);
        original_fds[i] = fd_val;
    }

    // Call poll
    int result = poll(pfds, arr->length, timeout_ms);

    if (result < 0) {
        for (int i = 0; i < arr->length; i++) {
            hml_release(&original_fds[i]);
        }
        free(pfds);
        free(original_fds);
        hml_runtime_error("poll() failed");
    }

    // Build result array with fds that have events
    HmlValue result_arr = hml_val_array();

    for (int i = 0; i < arr->length; i++) {
        if (pfds[i].revents != 0) {
            HmlValue obj = hml_val_object();
            hml_object_set_field(obj, "fd", original_fds[i]);
            hml_object_set_field(obj, "revents", hml_val_i32(pfds[i].revents));
            hml_array_push(result_arr, obj);
            hml_release(&obj);
        }
        hml_release(&original_fds[i]);
    }

    free(pfds);
    free(original_fds);
    return result_arr;
}

#endif // !__EMSCRIPTEN__

#endif // !__EMSCRIPTEN__ || __EMSCRIPTEN_PTHREADS__
