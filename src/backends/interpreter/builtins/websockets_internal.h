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

// Forward-compat shim for libwebsockets' growing lws_context_creation_info.
// Hemlock is compiled against one libwebsockets version but dynamically links
// whatever libwebsockets.so the host provides; the struct grew between
// releases (704 bytes on lws 4.x, 744 on lws 4.3). When the runtime lws is
// newer, lws_create_context reads fields past the struct we zeroed — i.e.
// uninitialized stack — and crashes in lws_create_vhost/lws_snprintf. Copy
// the compile-time struct into an over-allocated, fully-zeroed buffer so the
// extra runtime fields read as 0 (NULL = "use default"). Type-agnostic
// (void*) so it needs no <libwebsockets.h>; the caller casts the result.
// Uses a thread-local scratch buffer (lws copies what it needs during
// lws_create_context, so it needn't persist afterward), keeping call sites a
// one-liner. 2048 comfortably exceeds any lws_context_creation_info (744 on
// lws 4.3) with headroom for future growth.
#include <string.h>
static inline void *hml_ws_lws_info_forward_compat(const void *src, size_t src_sz) {
    static _Thread_local char buf[2048];
    if (src_sz > sizeof(buf)) return (void *)src;  // unreachable safety net
    memset(buf, 0, sizeof(buf));
    memcpy(buf, src, src_sz);
    return buf;
}

#endif // HEMLOCK_WEBSOCKETS_INTERNAL_H
