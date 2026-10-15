// Channel methods for the interpreter: send / recv / recv_timeout /
// send_timeout / close.
//
// Every method is a thin adapter over the shared channel core
// (src/shared/chan_core.c), which owns all queueing and blocking logic.
// This file only validates arguments, manages Value references, and maps
// core status codes onto the interpreter's return/throw conventions:
//
//   send()          -> null, throws on closed
//   recv()          -> value, or null once closed and drained
//   recv_timeout()  -> value, or null on timeout / closed
//   send_timeout()  -> true on success, false on timeout, throws on closed
//   close()         -> null (idempotent)
#include "internal.h"
#include <stdarg.h>

static Value throw_runtime_error(ExecutionContext *ctx, const char *format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    ctx->exception_state.exception_value = val_string(buffer);
    ctx->exception_state.is_throwing = 1;
    return val_null();
}

// The core consumes the reference passed to send on every return path, so
// retain before handing the value over.
static HmlChanStatus send_value(HmlChan *chan, Value msg, const struct timespec *deadline) {
    value_retain(msg);
    return hml_chan_send(chan, &msg, deadline);
}

static Value recv_value(HmlChan *chan, const struct timespec *deadline) {
    Value msg;
    if (hml_chan_recv(chan, &msg, deadline) == HML_CHAN_OK) {
        return msg;  // caller owns the reference handed out by the core
    }
    return val_null();
}

Value call_channel_method(Channel *ch, const char *method, Value *args, int num_args, ExecutionContext *ctx) {
    HmlChan *chan = ch->chan;

    if (strcmp(method, "send") == 0) {
        if (num_args != 1) {
            return throw_runtime_error(ctx, "send() expects 1 argument");
        }
        if (send_value(chan, args[0], NULL) == HML_CHAN_CLOSED) {
            return throw_runtime_error(ctx, "cannot send to closed channel");
        }
        return val_null();
    }

    if (strcmp(method, "recv") == 0) {
        if (num_args != 0) {
            return throw_runtime_error(ctx, "recv() expects 0 arguments");
        }
        return recv_value(chan, NULL);
    }

    if (strcmp(method, "recv_timeout") == 0) {
        if (num_args != 1) {
            return throw_runtime_error(ctx, "recv_timeout() expects 1 argument (timeout_ms)");
        }
        if (!is_integer(args[0])) {
            return throw_runtime_error(ctx, "recv_timeout() timeout must be an integer");
        }
        struct timespec deadline;
        hml_chan_deadline_after_ms(&deadline, value_to_int(args[0]));
        return recv_value(chan, &deadline);
    }

    if (strcmp(method, "send_timeout") == 0) {
        if (num_args != 2) {
            return throw_runtime_error(ctx, "send_timeout() expects 2 arguments (value, timeout_ms)");
        }
        if (!is_integer(args[1])) {
            return throw_runtime_error(ctx, "send_timeout() timeout must be an integer");
        }
        struct timespec deadline;
        hml_chan_deadline_after_ms(&deadline, value_to_int(args[1]));
        switch (send_value(chan, args[0], &deadline)) {
            case HML_CHAN_OK:      return val_bool(1);
            case HML_CHAN_TIMEOUT: return val_bool(0);
            case HML_CHAN_CLOSED:  break;
        }
        return throw_runtime_error(ctx, "cannot send to closed channel");
    }

    if (strcmp(method, "close") == 0) {
        if (num_args != 0) {
            return throw_runtime_error(ctx, "close() expects 0 arguments");
        }
        hml_chan_close(chan);
        return val_null();
    }

    return throw_runtime_error(ctx, "Unknown channel method '%s'", method);
}
