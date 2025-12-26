/*
 * Hemlock Runtime Library - Async/Concurrency Operations
 *
 * Task spawning, joining, channels, and synchronization primitives.
 */

#include "builtins_internal.h"
#include <pthread.h>
#include <stdatomic.h>

#ifdef HML_RT_POSIX
#include <poll.h>
#endif

static atomic_int g_next_task_id = 1;

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

// Thread wrapper function
static void* task_thread_wrapper(void* arg) {
    HmlTask *task = (HmlTask*)arg;

    // Mark as running
    pthread_mutex_lock((pthread_mutex_t*)task->mutex);
    task->state = HML_TASK_RUNNING;
    pthread_mutex_unlock((pthread_mutex_t*)task->mutex);

    // Get function info
    HmlFunction *fn = task->function.as.as_function;
    void *fn_ptr = fn->fn_ptr;
    void *closure_env = fn->closure_env;

    // Call function with arguments using libffi (supports unlimited arguments)
    HmlValue result = call_hemlock_function_ffi(fn_ptr, closure_env, task->args, task->num_args);

    // Store result and mark as completed
    pthread_mutex_lock((pthread_mutex_t*)task->mutex);
    task->result = result;
    task->state = HML_TASK_COMPLETED;
    pthread_cond_signal((pthread_cond_t*)task->cond);
    pthread_mutex_unlock((pthread_mutex_t*)task->mutex);

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
    task->ref_count = 1;

    // Store function and args
    task->function = fn;
    hml_retain(&task->function);
    task->num_args = num_args;
    if (num_args > 0) {
        task->args = malloc(sizeof(HmlValue) * num_args);
        for (int i = 0; i < num_args; i++) {
            task->args[i] = args[i];
            hml_retain(&task->args[i]);
        }
    } else {
        task->args = NULL;
    }

    // Initialize mutex and condition variable
    task->mutex = malloc(sizeof(pthread_mutex_t));
    task->cond = malloc(sizeof(pthread_cond_t));
    pthread_mutex_init((pthread_mutex_t*)task->mutex, NULL);
    pthread_cond_init((pthread_cond_t*)task->cond, NULL);

    // Create thread
    task->thread = malloc(sizeof(pthread_t));
    pthread_create((pthread_t*)task->thread, NULL, task_thread_wrapper, task);

    // Return task value
    HmlValue result;
    result.type = HML_VAL_TASK;
    result.as.as_task = task;
    return result;
}

HmlValue hml_join(HmlValue task_val) {
    if (task_val.type != HML_VAL_TASK) {
        hml_runtime_error("join() expects a task");
    }

    HmlTask *task = task_val.as.as_task;

    if (task->joined) {
        hml_runtime_error("task handle already joined");
    }

    if (task->detached) {
        hml_runtime_error("cannot join detached task");
    }

    // Wait for task to complete
    pthread_mutex_lock((pthread_mutex_t*)task->mutex);
    while (task->state != HML_TASK_COMPLETED) {
        pthread_cond_wait((pthread_cond_t*)task->cond, (pthread_mutex_t*)task->mutex);
    }
    pthread_mutex_unlock((pthread_mutex_t*)task->mutex);

    // Join the thread
    pthread_join(*(pthread_t*)task->thread, NULL);
    task->joined = 1;

    // Return result (retained)
    HmlValue result = task->result;
    hml_retain(&result);
    return result;
}

void hml_detach(HmlValue task_val) {
    if (task_val.type != HML_VAL_TASK) {
        hml_runtime_error("detach() expects a task");
    }

    HmlTask *task = task_val.as.as_task;

    if (task->joined) {
        hml_runtime_error("cannot detach already joined task");
    }

    if (task->detached) {
        return; // Already detached
    }

    task->detached = 1;
    pthread_detach(*(pthread_t*)task->thread);
}

// task_debug_info(task) - Print debug information about a task
void hml_task_debug_info(HmlValue task_val) {
    if (task_val.type != HML_VAL_TASK) {
        hml_runtime_error("task_debug_info() expects a task");
    }

    HmlTask *task = task_val.as.as_task;

    // Lock mutex to safely read task state
    pthread_mutex_lock((pthread_mutex_t*)task->mutex);

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
    printf("Ref Count: %d\n", task->ref_count);
    printf("Has Result: %s\n", task->result.type != HML_VAL_NULL ? "true" : "false");
    printf("======================\n");

    pthread_mutex_unlock((pthread_mutex_t*)task->mutex);
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

    // Use libffi to call the function with dynamic arguments
    return call_hemlock_function_ffi(func->fn_ptr, func->closure_env, arr->elements, arr->length);
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

    ch->mutex = malloc(sizeof(pthread_mutex_t));
    ch->not_empty = malloc(sizeof(pthread_cond_t));
    ch->not_full = malloc(sizeof(pthread_cond_t));
    ch->rendezvous = malloc(sizeof(pthread_cond_t));
    pthread_mutex_init((pthread_mutex_t*)ch->mutex, NULL);
    pthread_cond_init((pthread_cond_t*)ch->not_empty, NULL);
    pthread_cond_init((pthread_cond_t*)ch->not_full, NULL);
    pthread_cond_init((pthread_cond_t*)ch->rendezvous, NULL);

    // Initialize unbuffered channel fields
    ch->unbuffered_value = malloc(sizeof(HmlValue));
    if (ch->unbuffered_value) {
        *(ch->unbuffered_value) = hml_val_null();
    }
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

    pthread_mutex_lock((pthread_mutex_t*)ch->mutex);

    // Check if channel is closed
    if (ch->closed) {
        pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
        hml_runtime_error("cannot send to closed channel");
    }

    if (ch->capacity == 0) {
        // Unbuffered channel - rendezvous with receiver
        hml_retain(&value);
        *(ch->unbuffered_value) = value;
        ch->sender_waiting = 1;

        // Signal any waiting receiver that data is available
        pthread_cond_signal((pthread_cond_t*)ch->not_empty);

        // Wait for receiver to pick up the value
        while (ch->sender_waiting && !ch->closed) {
            pthread_cond_wait((pthread_cond_t*)ch->rendezvous, (pthread_mutex_t*)ch->mutex);
        }

        // Check if we were woken because channel closed
        if (ch->closed && ch->sender_waiting) {
            ch->sender_waiting = 0;
            hml_release(ch->unbuffered_value);
            *(ch->unbuffered_value) = hml_val_null();
            pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
            hml_runtime_error("cannot send to closed channel");
        }

        pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
        return;
    }

    // Buffered channel - wait while buffer is full
    while (ch->count >= ch->capacity && !ch->closed) {
        pthread_cond_wait((pthread_cond_t*)ch->not_full, (pthread_mutex_t*)ch->mutex);
    }

    // Check again if closed after waking up
    if (ch->closed) {
        pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
        hml_runtime_error("cannot send to closed channel");
    }

    // Add value to buffer
    ch->buffer[ch->tail] = value;
    hml_retain(&ch->buffer[ch->tail]);
    ch->tail = (ch->tail + 1) % ch->capacity;
    ch->count++;

    pthread_cond_signal((pthread_cond_t*)ch->not_empty);
    pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
}

HmlValue hml_channel_recv(HmlValue channel) {
    if (channel.type != HML_VAL_CHANNEL) {
        hml_runtime_error("recv() expects a channel");
    }

    HmlChannel *ch = channel.as.as_channel;

    pthread_mutex_lock((pthread_mutex_t*)ch->mutex);

    if (ch->capacity == 0) {
        // Unbuffered channel - rendezvous with sender
        // Wait for sender to have data available
        while (!ch->sender_waiting && !ch->closed) {
            pthread_cond_wait((pthread_cond_t*)ch->not_empty, (pthread_mutex_t*)ch->mutex);
        }

        // If channel is closed and no sender waiting, return null
        if (!ch->sender_waiting && ch->closed) {
            pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
            return hml_val_null();
        }

        // Get the value from sender
        HmlValue value = *(ch->unbuffered_value);
        *(ch->unbuffered_value) = hml_val_null();
        ch->sender_waiting = 0;

        // Signal sender that value was received
        pthread_cond_signal((pthread_cond_t*)ch->rendezvous);
        pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);

        return value;
    }

    // Buffered channel - wait while buffer is empty
    while (ch->count == 0 && !ch->closed) {
        pthread_cond_wait((pthread_cond_t*)ch->not_empty, (pthread_mutex_t*)ch->mutex);
    }

    if (ch->count == 0 && ch->closed) {
        pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
        return hml_val_null();
    }

    // Get value from buffer
    HmlValue value = ch->buffer[ch->head];
    ch->head = (ch->head + 1) % ch->capacity;
    ch->count--;

    pthread_cond_signal((pthread_cond_t*)ch->not_full);
    pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);

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

    pthread_mutex_lock((pthread_mutex_t*)ch->mutex);

    if (ch->capacity == 0) {
        // Unbuffered channel with timeout - rendezvous with sender
        // Wait for sender to have data available (with timeout)
        while (!ch->sender_waiting && !ch->closed) {
            int rc = pthread_cond_timedwait((pthread_cond_t*)ch->not_empty,
                                            (pthread_mutex_t*)ch->mutex, &deadline);
            if (rc == ETIMEDOUT) {
                pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
                return hml_val_null();  // Timeout
            }
        }

        // If channel is closed and no sender waiting, return null
        if (!ch->sender_waiting && ch->closed) {
            pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
            return hml_val_null();
        }

        // Get the value from sender
        HmlValue value = *(ch->unbuffered_value);
        *(ch->unbuffered_value) = hml_val_null();
        ch->sender_waiting = 0;

        // Signal sender that value was received
        pthread_cond_signal((pthread_cond_t*)ch->rendezvous);
        pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);

        return value;
    }

    // Buffered channel - wait while buffer is empty
    while (ch->count == 0 && !ch->closed) {
        int rc = pthread_cond_timedwait((pthread_cond_t*)ch->not_empty,
                                        (pthread_mutex_t*)ch->mutex, &deadline);
        if (rc == ETIMEDOUT) {
            pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
            return hml_val_null();  // Timeout
        }
    }

    if (ch->count == 0 && ch->closed) {
        pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
        return hml_val_null();
    }

    // Get value from buffer
    HmlValue value = ch->buffer[ch->head];
    ch->head = (ch->head + 1) % ch->capacity;
    ch->count--;

    pthread_cond_signal((pthread_cond_t*)ch->not_full);
    pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);

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

    pthread_mutex_lock((pthread_mutex_t*)ch->mutex);

    // Check if channel is closed
    if (ch->closed) {
        pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
        hml_runtime_error("cannot send to closed channel");
    }

    if (ch->capacity == 0) {
        // Unbuffered channel with timeout - rendezvous with receiver
        hml_retain(&value);
        *(ch->unbuffered_value) = value;
        ch->sender_waiting = 1;

        // Signal any waiting receiver that data is available
        pthread_cond_signal((pthread_cond_t*)ch->not_empty);

        // Wait for receiver to pick up the value (with timeout)
        while (ch->sender_waiting && !ch->closed) {
            int rc = pthread_cond_timedwait((pthread_cond_t*)ch->rendezvous,
                                            (pthread_mutex_t*)ch->mutex, &deadline);
            if (rc == ETIMEDOUT) {
                // Timeout - clean up and return failure
                ch->sender_waiting = 0;
                hml_release(ch->unbuffered_value);
                *(ch->unbuffered_value) = hml_val_null();
                pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
                return hml_val_bool(0);  // Timeout - send failed
            }
        }

        // Check if we were woken because channel closed
        if (ch->closed && ch->sender_waiting) {
            ch->sender_waiting = 0;
            hml_release(ch->unbuffered_value);
            *(ch->unbuffered_value) = hml_val_null();
            pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
            hml_runtime_error("cannot send to closed channel");
        }

        pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
        return hml_val_bool(1);  // Success
    }

    // Buffered channel - wait while buffer is full
    while (ch->count >= ch->capacity && !ch->closed) {
        int rc = pthread_cond_timedwait((pthread_cond_t*)ch->not_full,
                                        (pthread_mutex_t*)ch->mutex, &deadline);
        if (rc == ETIMEDOUT) {
            pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
            return hml_val_bool(0);  // Timeout - send failed
        }
    }

    // Check again if closed after waking up
    if (ch->closed) {
        pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
        hml_runtime_error("cannot send to closed channel");
    }

    // Add value to buffer
    ch->buffer[ch->tail] = value;
    hml_retain(&ch->buffer[ch->tail]);
    ch->tail = (ch->tail + 1) % ch->capacity;
    ch->count++;

    pthread_cond_signal((pthread_cond_t*)ch->not_empty);
    pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);

    return hml_val_bool(1);  // Success
}

void hml_channel_close(HmlValue channel) {
    if (channel.type != HML_VAL_CHANNEL) {
        return;
    }

    HmlChannel *ch = channel.as.as_channel;

    pthread_mutex_lock((pthread_mutex_t*)ch->mutex);
    ch->closed = 1;
    // Wake up all waiting threads
    pthread_cond_broadcast((pthread_cond_t*)ch->not_empty);
    pthread_cond_broadcast((pthread_cond_t*)ch->not_full);
    // Also wake up any unbuffered channel senders waiting on rendezvous
    pthread_cond_broadcast((pthread_cond_t*)ch->rendezvous);
    pthread_mutex_unlock((pthread_mutex_t*)ch->mutex);
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
            pthread_mutex_t *mutex = (pthread_mutex_t*)ch->mutex;

            pthread_mutex_lock(mutex);

            // Check if channel has data
            if (ch->count > 0) {
                // Read the value
                HmlValue msg = ch->buffer[ch->head];
                ch->head = (ch->head + 1) % ch->capacity;
                ch->count--;

                // Signal that buffer is not full
                pthread_cond_signal((pthread_cond_t*)ch->not_full);
                pthread_mutex_unlock(mutex);

                // Create result object { channel, value }
                HmlValue result = hml_val_object();
                hml_object_set_field(result, "channel", arr->elements[i]);
                hml_object_set_field(result, "value", msg);
                return result;
            }

            // Check if channel is closed and empty
            if (ch->closed) {
                pthread_mutex_unlock(mutex);
                // Return object with null value for closed channel
                HmlValue result = hml_val_object();
                hml_object_set_field(result, "channel", arr->elements[i]);
                hml_object_set_field(result, "value", hml_val_null());
                return result;
            }

            pthread_mutex_unlock(mutex);
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
            free(pfds);
            free(original_fds);
            hml_runtime_error("poll() array elements must be objects with 'fd' and 'events'");
        }

        // Get fd field
        HmlValue fd_val = hml_object_get_field(item, "fd");
        HmlValue events_val = hml_object_get_field(item, "events");

        int fd = hml_get_fd_from_value(fd_val);
        if (fd < 0) {
            free(pfds);
            free(original_fds);
            hml_runtime_error("poll() fd must be a socket or file");
        }

        if (!hml_is_integer(events_val)) {
            free(pfds);
            free(original_fds);
            hml_runtime_error("poll() events must be an integer");
        }

        pfds[i].fd = fd;
        pfds[i].events = (short)hml_to_i32(events_val);
        pfds[i].revents = 0;
        original_fds[i] = fd_val;
        hml_retain(&original_fds[i]);
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

