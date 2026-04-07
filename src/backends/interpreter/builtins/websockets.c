// libwebsockets builtins for Hemlock
// Shared handle helpers and initialization.
// HTTP, streaming HTTP, and WebSocket code split into:
//   websockets_http.c, websockets_stream.c, websockets_ws.c

// _DEFAULT_SOURCE is now defined globally in Makefile for usleep() and similar

#include "internal.h"
#include "websockets_internal.h"
#include <stdatomic.h>

// ========== WEBSOCKET HANDLE HELPERS ==========

Value val_websocket(WebSocketHandle *ws) {
    Value val;
    val.type = VAL_WEBSOCKET;
    val.as.as_websocket = ws;
    return val;
}

void websocket_retain(WebSocketHandle *ws) {
    if (ws) {
        __atomic_add_fetch(&ws->ref_count, 1, __ATOMIC_SEQ_CST);
    }
}

void websocket_release(WebSocketHandle *ws) {
    if (ws) {
        int old_count = __atomic_sub_fetch(&ws->ref_count, 1, __ATOMIC_SEQ_CST);
        if (old_count == 0) {
            websocket_free(ws);
        }
    }
}

// Check if libwebsockets is available
// HAVE_LIBWEBSOCKETS is defined by Makefile if pkg-config finds libwebsockets
// or if /usr/include/libwebsockets.h exists
#ifndef HAVE_LIBWEBSOCKETS
#  define HAVE_LIBWEBSOCKETS 0
#endif

#if HAVE_LIBWEBSOCKETS

#include <libwebsockets.h>

// Suppress libwebsockets startup messages by default
// Set LWS_VERBOSE=1 environment variable to enable verbose logging
void lws_init_logging(void) {
    static int initialized = 0;
    if (!initialized) {
        initialized = 1;
        const char *verbose = getenv("LWS_VERBOSE");
        if (!verbose || strcmp(verbose, "1") != 0) {
            // Only show errors by default, suppress warnings/info/notice/debug
            lws_set_log_level(LLL_ERR, NULL);
        }
    }
}

#else  // !HAVE_LIBWEBSOCKETS

void lws_init_logging(void) {
    // No-op when libwebsockets not available
}

#endif  // HAVE_LIBWEBSOCKETS
