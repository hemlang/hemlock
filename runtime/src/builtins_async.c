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
#include "chan_core.h"
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

// ========== TASKS ==========
//
// A task is reference counted: one reference for the caller's handle, one
// for the worker thread (released when the function returns). task_free()
// (value.c) detaches the pthread if it was neither joined nor detached, so
// a dropped handle never leaks thread resources.

static void *task_thread_main(void *arg) {
    HmlTask *task = (HmlTask *)arg;

    // Only the main thread handles signals: the C signal handler calls back
    // into the VM, which is not safe on a worker that may hold a runtime
    // lock. (POSIX only: Windows delivers console control events on its own
    // thread.)
#if !defined(__EMSCRIPTEN__) && !defined(_WIN32)
    sigset_t block_set;
    sigfillset(&block_set);
    pthread_sigmask(SIG_BLOCK, &block_set, NULL);
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

    // macOS coalesces timers aggressively on low-QoS threads (a process
    // launched from launchd/nohup can inherit BACKGROUND/UTILITY), stretching
    // a 30s nanosleep to minutes. Pin task threads to USER_INITIATED so
    // heartbeat/poll loops wake up on time.
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
#endif

    pthread_mutex_lock(&task->sync->mutex);
    task->state = HML_TASK_RUNNING;
    pthread_mutex_unlock(&task->sync->mutex);

    HmlFunction *fn = task->function.as.as_function;
    int arity = fn->num_params + (fn->has_rest_param ? 1 : 0);
    int direct_call = arity <= 8;  // direct dispatcher limit; libffi beyond

    // Capture exceptions so they propagate on join() instead of killing the process.
    HmlExceptionContext *ex_ctx = hml_exception_push();
    HmlValue result = hml_val_null();
    HmlValue exception = hml_val_null();
    int threw = 0;

    if (setjmp(ex_ctx->exception_buf) == 0) {
        if (direct_call) {
            result = hml_call_function(task->function, task->args, task->num_args);
        } else {
#if defined(__EMSCRIPTEN__) || defined(HEMLOCK_NO_FFI)
            hml_runtime_error("spawn(): functions with >8 parameters not supported in this build");
#else
            result = call_hemlock_function_ffi(fn->fn_ptr, fn->closure_env, task->args, task->num_args);
#endif
        }
        hml_exception_pop();
    } else {
        exception = hml_exception_get_value();  // owned reference
        hml_exception_pop();
        threw = 1;
    }

    pthread_mutex_lock(&task->sync->mutex);
    task->result = result;
    task->has_exception = threw;
    task->exception_value = exception;
    task->state = HML_TASK_COMPLETED;
    pthread_mutex_unlock(&task->sync->mutex);

    HmlValue task_val = { .type = HML_VAL_TASK, .as.as_task = task };
    hml_release(&task_val);  // the worker's reference
    return NULL;
}

// Allocate a task holding `fn` and deep copies of `args` (thread isolation).
static HmlTask *task_alloc(HmlValue fn, HmlValue *args, int num_args, const char *name) {
    HmlTask *task = calloc(1, sizeof(HmlTask));
    if (!task) {
        hml_runtime_error("Memory allocation failed in spawn()");
    }
    task->sync = malloc(sizeof(HmlTaskSync));
    if (!task->sync) {
        free(task);
        hml_runtime_error("Memory allocation failed in spawn()");
    }
    task->id = atomic_fetch_add(&g_next_task_id, 1);
    task->state = HML_TASK_READY;
    task->result = hml_val_null();
    task->exception_value = hml_val_null();
    task->ref_count = 2;  // caller's handle + worker thread
    task->function = fn;
    hml_retain(&task->function);
    task->name = name ? strdup(name) : NULL;
    pthread_mutex_init(&task->sync->mutex, NULL);

    task->num_args = num_args;
    if (num_args > 0) {
        task->args = malloc(sizeof(HmlValue) * num_args);
        for (int i = 0; i < num_args; i++) {
            task->args[i] = hml_value_deep_copy(args[i]);
        }
    }
    return task;
}

// Start the worker thread. On failure drops both references and throws,
// matching the interpreter's "Failed to create thread" error.
static HmlValue task_launch(HmlTask *task, size_t stack_size) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, stack_size);
    int rc = pthread_create(&task->sync->thread, &attr, task_thread_main, task);
    pthread_attr_destroy(&attr);

    if (rc != 0) {
        task->detached = 1;  // there is no pthread for task_free() to detach
        task->ref_count = 1;
        HmlValue task_val = { .type = HML_VAL_TASK, .as.as_task = task };
        hml_release(&task_val);
        hml_runtime_error("Failed to create thread: %d", rc);
    }

    HmlValue handle = { .type = HML_VAL_TASK, .as.as_task = task };
    return handle;
}

static HmlFunction *require_async_fn(HmlValue fn, const char *not_fn_msg, const char *not_async_msg) {
    if (fn.type != HML_VAL_FUNCTION) {
        hml_runtime_error("%s", not_fn_msg);
    }
    if (!fn.as.as_function->is_async) {
        hml_runtime_error("%s", not_async_msg);
    }
    return fn.as.as_function;
}

HmlValue hml_spawn(HmlValue fn, HmlValue *args, int num_args) {
    require_async_fn(fn, "spawn() expects a function", "spawn() requires an async function");
    HmlTask *task = task_alloc(fn, args, num_args, NULL);
    return task_launch(task, atomic_load(&g_default_stack_size));
}

// spawn_with(options, fn, args...) - options: { stack_size?: int, name?: string }
HmlValue hml_spawn_with(HmlValue options, HmlValue fn, HmlValue *args, int num_args) {
    if (options.type != HML_VAL_OBJECT) {
        hml_runtime_error("spawn_with() first argument must be an options object");
    }
    require_async_fn(fn, "spawn_with() second argument must be a function",
                     "spawn_with() requires an async function");

    size_t stack_size = atomic_load(&g_default_stack_size);
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

    // The name option's string is borrowed only until task_alloc copies it.
    HmlValue nv = hml_val_null();
    const char *name = NULL;
    if (hml_object_has_field(options, "name")) {
        nv = hml_object_get_field(options, "name");
        if (nv.type != HML_VAL_STRING) {
            hml_release(&nv);
            hml_runtime_error("spawn_with() name must be a string");
        }
        name = hml_to_string_ptr(nv);
    }
    HmlTask *task = task_alloc(fn, args, num_args, name);
    hml_release(&nv);
    return task_launch(task, stack_size);
}

HmlValue hml_get_default_stack_size(void) {
    return hml_val_i64((int64_t)atomic_load(&g_default_stack_size));
}

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

    // Claim the join under the lock so concurrent join()/detach() serialize.
    pthread_mutex_lock(&task->sync->mutex);
    if (task->joined) {
        pthread_mutex_unlock(&task->sync->mutex);
        hml_runtime_error_loc("task handle already joined");
    }
    if (task->detached) {
        pthread_mutex_unlock(&task->sync->mutex);
        hml_runtime_error_loc("cannot join detached task");
    }
    task->joined = 1;
    pthread_mutex_unlock(&task->sync->mutex);

    pthread_join(task->sync->thread, NULL);

    // The worker has exited: its outcome is final and ours to read.
    if (task->has_exception) {
        HmlValue exception = task->exception_value;
        hml_retain(&exception);
        hml_throw(exception);  // re-throw in the joining thread (parity with interpreter)
    }
    HmlValue result = task->result;
    hml_retain(&result);
    return result;
}

void hml_detach(HmlValue task_val) {
    if (task_val.type != HML_VAL_TASK) {
        hml_runtime_error("detach() expects a task");
    }
    HmlTask *task = task_val.as.as_task;

    pthread_mutex_lock(&task->sync->mutex);
    if (task->joined) {
        pthread_mutex_unlock(&task->sync->mutex);
        hml_runtime_error("cannot detach already joined task");
    }
    if (task->detached) {
        pthread_mutex_unlock(&task->sync->mutex);
        return;  // already detached
    }
    task->detached = 1;
    pthread_mutex_unlock(&task->sync->mutex);

    pthread_detach(task->sync->thread);
}

void hml_task_debug_info(HmlValue task_val) {
    if (task_val.type != HML_VAL_TASK) {
        hml_runtime_error("task_debug_info() expects a task");
    }
    HmlTask *task = task_val.as.as_task;

    pthread_mutex_lock(&task->sync->mutex);
    const char *state = "UNKNOWN";
    switch (task->state) {
        case HML_TASK_READY:     state = "READY"; break;
        case HML_TASK_RUNNING:   state = "RUNNING"; break;
        case HML_TASK_COMPLETED: state = "COMPLETED"; break;
    }
    printf("=== Task Debug Info ===\n");
    printf("Task ID: %d\n", task->id);
    printf("State: %s\n", state);
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
    // No libffi: the direct dispatcher covers up to 8 parameters.
    (void)func;
    return hml_call_function(fn, arr->elements, arr->length);
#else
    return call_hemlock_function_ffi(func->fn_ptr, func->closure_env, arr->elements, arr->length);
#endif
}

// ========== CHANNELS ==========
//
// Thin adapters over the shared channel core (src/shared/chan_core.c),
// which owns all queueing and blocking logic. These functions validate
// arguments, manage HmlValue references, and map core status codes onto
// the runtime's return/throw conventions:
//
//   send()          -> throws on closed
//   recv()          -> value, or null once closed and drained
//   recv_timeout()  -> value, or null on timeout / closed
//   send_timeout()  -> true on success, false on timeout, throws on closed
//   select()        -> { channel, value } / { channel, value: null } / null on timeout

static void channel_item_release(void *item) {
    hml_release((HmlValue *)item);
}

static HmlChan *require_channel(HmlValue channel, const char *who) {
    if (channel.type != HML_VAL_CHANNEL) {
        hml_runtime_error("%s expects a channel", who);
    }
    return channel.as.as_channel->chan;
}

// The core consumes the reference on every return path, so retain first.
static HmlChanStatus send_value(HmlChan *chan, HmlValue value, const struct timespec *deadline) {
    hml_retain(&value);
    return hml_chan_send(chan, &value, deadline);
}

static HmlValue recv_value(HmlChan *chan, const struct timespec *deadline) {
    HmlValue value;
    if (hml_chan_recv(chan, &value, deadline) == HML_CHAN_OK) {
        return value;  // caller owns the reference handed out by the core
    }
    return hml_val_null();
}

HmlValue hml_channel(int32_t capacity) {
    HmlChannel *ch = malloc(sizeof(HmlChannel));
    if (ch) {
        ch->chan = hml_chan_new(capacity, sizeof(HmlValue), channel_item_release);
        ch->ref_count = 1;
    }
    if (!ch || !ch->chan) {
        free(ch);
        hml_runtime_error("Memory allocation failed in channel()");
    }
    HmlValue result = { .type = HML_VAL_CHANNEL, .as.as_channel = ch };
    return result;
}

void hml_channel_send(HmlValue channel, HmlValue value) {
    HmlChan *chan = require_channel(channel, "send()");
    if (send_value(chan, value, NULL) == HML_CHAN_CLOSED) {
        hml_runtime_error("cannot send to closed channel");
    }
}

HmlValue hml_channel_recv(HmlValue channel) {
    return recv_value(require_channel(channel, "recv()"), NULL);
}

HmlValue hml_channel_recv_timeout(HmlValue channel, HmlValue timeout_val) {
    HmlChan *chan = require_channel(channel, "recv_timeout()");
    struct timespec deadline;
    hml_chan_deadline_after_ms(&deadline, hml_to_i32(timeout_val));
    return recv_value(chan, &deadline);
}

HmlValue hml_channel_send_timeout(HmlValue channel, HmlValue value, HmlValue timeout_val) {
    HmlChan *chan = require_channel(channel, "send_timeout()");
    struct timespec deadline;
    hml_chan_deadline_after_ms(&deadline, hml_to_i32(timeout_val));
    switch (send_value(chan, value, &deadline)) {
        case HML_CHAN_OK:      return hml_val_bool(1);
        case HML_CHAN_TIMEOUT: return hml_val_bool(0);
        case HML_CHAN_CLOSED:  break;
    }
    hml_runtime_error("cannot send to closed channel");
    return hml_val_bool(0);  // unreachable
}

void hml_channel_close(HmlValue channel) {
    if (channel.type != HML_VAL_CHANNEL) {
        return;
    }
    hml_chan_close(channel.as.as_channel->chan);
}

// Build { channel, value }. Takes ownership of `value`.
static HmlValue make_select_result(HmlValue channel, HmlValue value) {
    HmlValue result = hml_val_object();
    hml_object_set_field(result, "channel", channel);
    hml_object_set_field(result, "value", value);  // set_field retains its own copy
    hml_release(&value);
    return result;
}

// select(channels, timeout_ms?) - block (no polling) until one channel has a
// value or is closed and drained.
HmlValue hml_select(HmlValue channels, HmlValue timeout) {
    if (channels.type != HML_VAL_ARRAY) {
        hml_runtime_error("select() expects array of channels as first argument");
    }
    HmlArray *arr = channels.as.as_array;
    if (arr->length == 0) {
        hml_runtime_error("select() requires at least one channel");
    }

    HmlChan **chans = malloc(sizeof(HmlChan *) * arr->length);
    if (!chans) {
        hml_runtime_error("Memory allocation failed in select()");
    }
    for (int i = 0; i < arr->length; i++) {
        if (arr->elements[i].type != HML_VAL_CHANNEL) {
            free(chans);
            hml_runtime_error("select() array must contain only channels");
        }
        chans[i] = arr->elements[i].as.as_channel->chan;
    }

    struct timespec deadline, *deadline_ptr = NULL;
    if (timeout.type != HML_VAL_NULL) {
        int timeout_ms = hml_to_i32(timeout);
        if (timeout_ms >= 0) {  // negative = wait forever
            hml_chan_deadline_after_ms(&deadline, timeout_ms);
            deadline_ptr = &deadline;
        }
    }

    int index = 0;
    HmlValue value;
    HmlChanStatus status = hml_chan_select(chans, arr->length, deadline_ptr, &index, &value);
    free(chans);

    switch (status) {
        case HML_CHAN_OK:      return make_select_result(arr->elements[index], value);
        case HML_CHAN_CLOSED:  return make_select_result(arr->elements[index], hml_val_null());
        case HML_CHAN_TIMEOUT: break;
    }
    return hml_val_null();
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
