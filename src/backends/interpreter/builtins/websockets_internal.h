// Internal shared header for websockets subsystem
// Provides declarations shared between websockets.c, websockets_http.c,
// websockets_stream.c, and websockets_ws.c

#ifndef HEMLOCK_WEBSOCKETS_INTERNAL_H
#define HEMLOCK_WEBSOCKETS_INTERNAL_H

#include "internal.h"

// Suppress libwebsockets startup messages by default
// Defined in websockets.c, used by HTTP and WS code
void lws_init_logging(void);

#endif // HEMLOCK_WEBSOCKETS_INTERNAL_H
