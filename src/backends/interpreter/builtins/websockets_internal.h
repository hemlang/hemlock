// Internal shared header for websockets subsystem
// Provides declarations shared between websockets.c, websockets_http.c,
// websockets_stream.c, and websockets_ws.c

#ifndef HEMLOCK_WEBSOCKETS_INTERNAL_H
#define HEMLOCK_WEBSOCKETS_INTERNAL_H

#include "internal.h"

// Suppress libwebsockets startup messages by default
// Defined in websockets.c, used by HTTP and WS code
void lws_init_logging(void);
void lws_configure_macos_ca_file(void);
const char *lws_context_error_message(void);

// Parse URL into host, port, path, ssl components
// Defined in websockets_http.c, used by websockets_stream.c
int parse_url(const char *url, char *host, int *port, char *path, int *ssl);

#endif // HEMLOCK_WEBSOCKETS_INTERNAL_H
