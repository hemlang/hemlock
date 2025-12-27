#include "internal.h"
#include <stdarg.h>
#include <errno.h>
#include <time.h>

// ========== RUNTIME ERROR HELPER ==========

static Value throw_runtime_error(ExecutionContext *ctx, const char *format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    ctx->exception_state.exception_value = val_string(buffer);
    value_retain(ctx->exception_state.exception_value);
    ctx->exception_state.is_throwing = 1;
    return val_null();
}

// ========== CHANNEL METHODS ==========

Value call_channel_method(Channel *ch, const char *method, Value *args, int num_args, ExecutionContext *ctx) {
    hml_mutex_t *mutex = (hml_mutex_t*)ch->mutex;
    hml_cond_t *not_empty = (hml_cond_t*)ch->not_empty;
    hml_cond_t *not_full = (hml_cond_t*)ch->not_full;

    // send(value) - send a message to the channel
    if (strcmp(method, "send") == 0) {
        if (num_args != 1) {
            return throw_runtime_error(ctx, "send() expects 1 argument");
        }

        Value msg = args[0];
        hml_cond_t *rendezvous = (hml_cond_t*)ch->rendezvous;

        hml_mutex_lock(mutex);

        // Check if channel is closed
        if (ch->closed) {
            hml_mutex_unlock(mutex);
            return throw_runtime_error(ctx, "cannot send to closed channel");
        }

        if (ch->capacity == 0) {
            // Unbuffered channel - rendezvous with receiver
            value_retain(msg);
            *(ch->unbuffered_value) = msg;
            ch->sender_waiting = 1;

            // Signal any waiting receiver that data is available
            hml_cond_signal(not_empty);

            // Wait for receiver to pick up the value
            while (ch->sender_waiting && !ch->closed) {
                hml_cond_wait(rendezvous, mutex);
            }

            // Check if we were woken because channel closed
            if (ch->closed && ch->sender_waiting) {
                ch->sender_waiting = 0;
                value_release(*(ch->unbuffered_value));
                *(ch->unbuffered_value) = val_null();
                hml_mutex_unlock(mutex);
                return throw_runtime_error(ctx, "cannot send to closed channel");
            }

            hml_mutex_unlock(mutex);
            return val_null();
        }

        // Buffered channel - wait while buffer is full
        while (ch->count >= ch->capacity && !ch->closed) {
            hml_cond_wait(not_full, mutex);
        }

        // Check again if closed after waking up
        if (ch->closed) {
            hml_mutex_unlock(mutex);
            return throw_runtime_error(ctx, "cannot send to closed channel");
        }

        // Add message to buffer
        value_retain(msg);
        ch->buffer[ch->tail] = msg;
        ch->tail = (ch->tail + 1) % ch->capacity;
        ch->count++;

        // Signal that buffer is not empty
        hml_cond_signal(not_empty);
        hml_mutex_unlock(mutex);

        return val_null();
    }

    // recv() - receive a message from the channel
    if (strcmp(method, "recv") == 0) {
        if (num_args != 0) {
            return throw_runtime_error(ctx, "recv() expects 0 arguments");
        }

        hml_cond_t *rendezvous = (hml_cond_t*)ch->rendezvous;

        hml_mutex_lock(mutex);

        if (ch->capacity == 0) {
            // Unbuffered channel - rendezvous with sender
            // Wait for sender to have data available
            while (!ch->sender_waiting && !ch->closed) {
                hml_cond_wait(not_empty, mutex);
            }

            // If channel is closed and no sender waiting, return null
            if (!ch->sender_waiting && ch->closed) {
                hml_mutex_unlock(mutex);
                return val_null();
            }

            // Get the value from sender
            Value msg = *(ch->unbuffered_value);
            *(ch->unbuffered_value) = val_null();
            ch->sender_waiting = 0;

            // Signal sender that value was received
            hml_cond_signal(rendezvous);
            hml_mutex_unlock(mutex);

            return msg;
        }

        // Buffered channel - wait while buffer is empty
        while (ch->count == 0 && !ch->closed) {
            hml_cond_wait(not_empty, mutex);
        }

        // If channel is closed and empty, return null
        if (ch->count == 0 && ch->closed) {
            hml_mutex_unlock(mutex);
            return val_null();
        }

        // Get message from buffer
        Value msg = ch->buffer[ch->head];
        ch->head = (ch->head + 1) % ch->capacity;
        ch->count--;

        // Signal that buffer is not full
        hml_cond_signal(not_full);
        hml_mutex_unlock(mutex);

        return msg;
    }

    // recv_timeout(timeout_ms) - receive with timeout
    if (strcmp(method, "recv_timeout") == 0) {
        if (num_args != 1) {
            return throw_runtime_error(ctx, "recv_timeout() expects 1 argument (timeout_ms)");
        }

        if (!is_integer(args[0])) {
            return throw_runtime_error(ctx, "recv_timeout() timeout must be an integer");
        }

        int timeout_ms = value_to_int(args[0]);

        // Calculate deadline
        struct timespec deadline;
        hml_clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += timeout_ms / 1000;
        deadline.tv_nsec += (timeout_ms % 1000) * 1000000;
        if (deadline.tv_nsec >= 1000000000) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000;
        }

        hml_mutex_lock(mutex);

        if (ch->capacity == 0) {
            // Unbuffered channel with timeout - rendezvous with sender
            hml_cond_t *rendezvous = (hml_cond_t*)ch->rendezvous;

            // Wait for sender to have data available (with timeout)
            while (!ch->sender_waiting && !ch->closed) {
                int rc = hml_cond_timedwait(not_empty, mutex, &deadline);
                if (rc == ETIMEDOUT) {
                    hml_mutex_unlock(mutex);
                    return val_null();  // Timeout
                }
            }

            // If channel is closed and no sender waiting, return null
            if (!ch->sender_waiting && ch->closed) {
                hml_mutex_unlock(mutex);
                return val_null();
            }

            // Get the value from sender
            Value msg = *(ch->unbuffered_value);
            *(ch->unbuffered_value) = val_null();
            ch->sender_waiting = 0;

            // Signal sender that value was received
            hml_cond_signal(rendezvous);
            hml_mutex_unlock(mutex);

            return msg;
        }

        // Wait while buffer is empty and channel not closed
        while (ch->count == 0 && !ch->closed) {
            int rc = hml_cond_timedwait(not_empty, mutex, &deadline);
            if (rc == ETIMEDOUT) {
                hml_mutex_unlock(mutex);
                return val_null();  // Timeout
            }
        }

        // If channel is closed and empty, return null
        if (ch->count == 0 && ch->closed) {
            hml_mutex_unlock(mutex);
            return val_null();
        }

        // Get message from buffer
        Value msg = ch->buffer[ch->head];
        ch->head = (ch->head + 1) % ch->capacity;
        ch->count--;

        // Signal that buffer is not full
        hml_cond_signal(not_full);
        hml_mutex_unlock(mutex);

        return msg;
    }

    // send_timeout(value, timeout_ms) - send with timeout
    if (strcmp(method, "send_timeout") == 0) {
        if (num_args != 2) {
            return throw_runtime_error(ctx, "send_timeout() expects 2 arguments (value, timeout_ms)");
        }

        Value msg = args[0];

        if (!is_integer(args[1])) {
            return throw_runtime_error(ctx, "send_timeout() timeout must be an integer");
        }

        int timeout_ms = value_to_int(args[1]);

        // Calculate deadline
        struct timespec deadline;
        hml_clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += timeout_ms / 1000;
        deadline.tv_nsec += (timeout_ms % 1000) * 1000000;
        if (deadline.tv_nsec >= 1000000000) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000;
        }

        hml_mutex_lock(mutex);

        // Check if channel is closed
        if (ch->closed) {
            hml_mutex_unlock(mutex);
            return throw_runtime_error(ctx, "cannot send to closed channel");
        }

        if (ch->capacity == 0) {
            // Unbuffered channel with timeout - rendezvous with receiver
            hml_cond_t *rendezvous = (hml_cond_t*)ch->rendezvous;

            value_retain(msg);
            *(ch->unbuffered_value) = msg;
            ch->sender_waiting = 1;

            // Signal any waiting receiver that data is available
            hml_cond_signal(not_empty);

            // Wait for receiver to pick up the value (with timeout)
            while (ch->sender_waiting && !ch->closed) {
                int rc = hml_cond_timedwait(rendezvous, mutex, &deadline);
                if (rc == ETIMEDOUT) {
                    // Timeout - clean up and return failure
                    ch->sender_waiting = 0;
                    value_release(*(ch->unbuffered_value));
                    *(ch->unbuffered_value) = val_null();
                    hml_mutex_unlock(mutex);
                    return val_bool(0);  // Timeout - send failed
                }
            }

            // Check if we were woken because channel closed
            if (ch->closed && ch->sender_waiting) {
                ch->sender_waiting = 0;
                value_release(*(ch->unbuffered_value));
                *(ch->unbuffered_value) = val_null();
                hml_mutex_unlock(mutex);
                return throw_runtime_error(ctx, "cannot send to closed channel");
            }

            hml_mutex_unlock(mutex);
            return val_bool(1);  // Success
        }

        // Wait while buffer is full
        while (ch->count >= ch->capacity && !ch->closed) {
            int rc = hml_cond_timedwait(not_full, mutex, &deadline);
            if (rc == ETIMEDOUT) {
                hml_mutex_unlock(mutex);
                return val_bool(0);  // Timeout - send failed
            }
        }

        // Check again if closed after waking up
        if (ch->closed) {
            hml_mutex_unlock(mutex);
            return throw_runtime_error(ctx, "cannot send to closed channel");
        }

        // Add message to buffer
        value_retain(msg);
        ch->buffer[ch->tail] = msg;
        ch->tail = (ch->tail + 1) % ch->capacity;
        ch->count++;

        // Signal that buffer is not empty
        hml_cond_signal(not_empty);
        hml_mutex_unlock(mutex);

        return val_bool(1);  // Success
    }

    // close() - close the channel
    if (strcmp(method, "close") == 0) {
        if (num_args != 0) {
            return throw_runtime_error(ctx, "close() expects 0 arguments");
        }

        hml_cond_t *rendezvous = (hml_cond_t*)ch->rendezvous;

        hml_mutex_lock(mutex);
        ch->closed = 1;
        // Wake up all waiting threads
        hml_cond_broadcast(not_empty);
        hml_cond_broadcast(not_full);
        // Also wake up any unbuffered channel senders waiting on rendezvous
        hml_cond_broadcast(rendezvous);
        hml_mutex_unlock(mutex);

        return val_null();
    }

    return throw_runtime_error(ctx, "Unknown channel method '%s'", method);
}
