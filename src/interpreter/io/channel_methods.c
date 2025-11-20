#include "internal.h"
#include <stdarg.h>

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
    pthread_mutex_t *mutex = (pthread_mutex_t*)ch->mutex;
    pthread_cond_t *not_empty = (pthread_cond_t*)ch->not_empty;
    pthread_cond_t *not_full = (pthread_cond_t*)ch->not_full;

    // send(value) - send a message to the channel
    if (strcmp(method, "send") == 0) {
        if (num_args != 1) {
            return throw_runtime_error(ctx, "send() expects 1 argument");
        }

        Value msg = args[0];

        pthread_mutex_lock(mutex);

        // Check if channel is closed
        if (ch->closed) {
            pthread_mutex_unlock(mutex);
            return throw_runtime_error(ctx, "cannot send to closed channel");
        }

        if (ch->capacity == 0) {
            // Unbuffered channel - would need rendezvous
            pthread_mutex_unlock(mutex);
            return throw_runtime_error(ctx, "unbuffered channels not yet supported (use buffered channel)");
        }

        // Wait while buffer is full
        while (ch->count >= ch->capacity && !ch->closed) {
            pthread_cond_wait(not_full, mutex);
        }

        // Check again if closed after waking up
        if (ch->closed) {
            pthread_mutex_unlock(mutex);
            return throw_runtime_error(ctx, "cannot send to closed channel");
        }

        // Add message to buffer
        value_retain(msg);
        ch->buffer[ch->tail] = msg;
        ch->tail = (ch->tail + 1) % ch->capacity;
        ch->count++;

        // Signal that buffer is not empty
        pthread_cond_signal(not_empty);
        pthread_mutex_unlock(mutex);

        return val_null();
    }

    // recv() - receive a message from the channel
    if (strcmp(method, "recv") == 0) {
        if (num_args != 0) {
            return throw_runtime_error(ctx, "recv() expects 0 arguments");
        }

        pthread_mutex_lock(mutex);

        // Wait while buffer is empty and channel not closed
        while (ch->count == 0 && !ch->closed) {
            pthread_cond_wait(not_empty, mutex);
        }

        // If channel is closed and empty, return null
        if (ch->count == 0 && ch->closed) {
            pthread_mutex_unlock(mutex);
            return val_null();
        }

        // Get message from buffer
        Value msg = ch->buffer[ch->head];
        ch->head = (ch->head + 1) % ch->capacity;
        ch->count--;

        // Signal that buffer is not full
        pthread_cond_signal(not_full);
        pthread_mutex_unlock(mutex);

        return msg;
    }

    // close() - close the channel
    if (strcmp(method, "close") == 0) {
        if (num_args != 0) {
            return throw_runtime_error(ctx, "close() expects 0 arguments");
        }

        pthread_mutex_lock(mutex);
        ch->closed = 1;
        // Wake up all waiting threads
        pthread_cond_broadcast(not_empty);
        pthread_cond_broadcast(not_full);
        pthread_mutex_unlock(mutex);

        return val_null();
    }

    return throw_runtime_error(ctx, "Unknown channel method '%s'", method);
}
