// libwebsockets builtins for Hemlock
// Provides both HTTP and WebSocket functionality as static builtins
// Compiles with stubs if libwebsockets.h is not available

// _DEFAULT_SOURCE is now defined globally in Makefile for usleep() and similar

#include "internal.h"
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
#include <pthread.h>

// Suppress libwebsockets startup messages by default
// Set LWS_VERBOSE=1 environment variable to enable verbose logging
static void lws_init_logging(void) {
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

// ========== HTTP SUPPORT ==========

typedef struct {
    char *body;
    size_t body_len;
    size_t body_capacity;
    int status_code;
    int complete;
    int failed;
    char *redirect_url;  // Location header for 3xx responses
    char *headers;       // Response headers as string
} http_response_t;

static int http_callback(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len) {
    http_response_t *resp = (http_response_t *)user;

    switch (reason) {
        case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
            // Add custom headers to the HTTP request
            {
                unsigned char **p = (unsigned char **)in;
                unsigned char *end = (*p) + len;

                // Add User-Agent header (required by GitHub API)
                const char *ua = "User-Agent: hemlock/1.0\r\n";
                size_t ua_len = strlen(ua);
                if (end - *p >= (int)ua_len) {
                    memcpy(*p, ua, ua_len);
                    *p += ua_len;
                }

                // Add Accept header for JSON APIs
                const char *accept = "Accept: application/json\r\n";
                size_t accept_len = strlen(accept);
                if (end - *p >= (int)accept_len) {
                    memcpy(*p, accept, accept_len);
                    *p += accept_len;
                }
            }
            break;

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            fprintf(stderr, "HTTP connection error: %s\n", in ? (char *)in : "unknown");
            if (resp) {
                resp->failed = 1;
                resp->complete = 1;
            }
            break;

        case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP:
            if (resp) {
                resp->status_code = lws_http_client_http_response(wsi);

                // Capture response headers
                {
                    // Build headers string from common HTTP headers
                    char headers_buf[8192];
                    size_t headers_len = 0;
                    char value[1024];
                    int vlen;

                    // Define header tokens and their names
                    struct { enum lws_token_indexes token; const char *name; } header_list[] = {
                        { WSI_TOKEN_HTTP_CONTENT_TYPE, "Content-Type" },
                        { WSI_TOKEN_HTTP_CONTENT_LENGTH, "Content-Length" },
                        { WSI_TOKEN_HTTP_CACHE_CONTROL, "Cache-Control" },
                        { WSI_TOKEN_HTTP_DATE, "Date" },
                        { WSI_TOKEN_HTTP_ETAG, "ETag" },
                        { WSI_TOKEN_HTTP_LAST_MODIFIED, "Last-Modified" },
                        { WSI_TOKEN_HTTP_LOCATION, "Location" },
                        { WSI_TOKEN_HTTP_SERVER, "Server" },
                        { WSI_TOKEN_HTTP_SET_COOKIE, "Set-Cookie" },
                        { WSI_TOKEN_HTTP_TRANSFER_ENCODING, "Transfer-Encoding" },
                        { WSI_TOKEN_HTTP_WWW_AUTHENTICATE, "WWW-Authenticate" },
                        { WSI_TOKEN_HTTP_ACCESS_CONTROL_ALLOW_ORIGIN, "Access-Control-Allow-Origin" },
                    };

                    for (size_t i = 0; i < sizeof(header_list)/sizeof(header_list[0]); i++) {
                        vlen = lws_hdr_copy(wsi, value, sizeof(value), header_list[i].token);
                        if (vlen > 0) {
                            value[vlen] = '\0';
                            int written = snprintf(headers_buf + headers_len,
                                                   sizeof(headers_buf) - headers_len,
                                                   "%s: %s\r\n", header_list[i].name, value);
                            if (written > 0 && (size_t)written < sizeof(headers_buf) - headers_len) {
                                headers_len += written;
                            }
                        }
                    }

                    if (headers_len > 0) {
                        resp->headers = strndup(headers_buf, headers_len);
                        if (!resp->headers) {
                            resp->failed = 1;
                            resp->complete = 1;
                            return -1;
                        }
                    }
                }

                // Capture Location header for redirects (3xx responses)
                if (resp->status_code >= 300 && resp->status_code < 400) {
                    char location[1024];
                    int loc_len = lws_hdr_copy(wsi, location, sizeof(location), WSI_TOKEN_HTTP_LOCATION);
                    if (loc_len > 0) {
                        location[loc_len] = '\0';
                        resp->redirect_url = strdup(location);
                        if (!resp->redirect_url) {
                            resp->failed = 1;
                            resp->complete = 1;
                            return -1;
                        }
                        // Mark as complete - we'll handle redirect at hemlock layer
                        resp->complete = 1;
                    }
                }
            }
            break;

        case LWS_CALLBACK_RECEIVE_CLIENT_HTTP:
            // This callback tells us there's data available - we must consume it
            {
                char buffer[4096 + LWS_PRE];
                char *px = buffer + LWS_PRE;
                int lenx = sizeof(buffer) - LWS_PRE;

                if (lws_http_client_read(wsi, &px, &lenx) < 0)
                    return -1;
            }
            return 0;

        case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ:
            // Accumulate response body - this is called after lws_http_client_read
            if (resp) {
                if (resp->body_len + len >= resp->body_capacity) {
                    resp->body_capacity = (resp->body_len + len + 1) * 2;
                    char *new_body = realloc(resp->body, resp->body_capacity);
                    if (!new_body) {
                        resp->failed = 1;
                        resp->complete = 1;
                        return -1;
                    }
                    resp->body = new_body;
                }
                memcpy(resp->body + resp->body_len, in, len);
                resp->body_len += len;
                resp->body[resp->body_len] = '\0';
            }
            return 0;

        case LWS_CALLBACK_COMPLETED_CLIENT_HTTP:
            if (resp) {
                resp->complete = 1;
            }
            break;

        case LWS_CALLBACK_CLOSED_CLIENT_HTTP:
            if (resp) {
                resp->complete = 1;
            }
            break;

        default:
            break;
    }

    return 0;
}

// Parse URL into components
static int parse_url(const char *url, char *host, int *port, char *path, int *ssl) {
    *ssl = 0;
    *port = 80;
    // SECURITY: Use safe string initialization instead of strcpy
    path[0] = '/';
    path[1] = '\0';

    if (strncmp(url, "https://", 8) == 0) {
        *ssl = 1;
        *port = 443;
        const char *rest = url + 8;
        const char *slash = strchr(rest, '/');
        const char *colon = strchr(rest, ':');

        if (colon && (!slash || colon < slash)) {
            size_t host_len = colon - rest;
            if (host_len >= 256) return -1;
            strncpy(host, rest, host_len);
            host[host_len] = '\0';
            *port = (int)strtol(colon + 1, NULL, 10);
            if (slash) {
                strncpy(path, slash, 511);
                path[511] = '\0';
            }
        } else if (slash) {
            size_t host_len = slash - rest;
            if (host_len >= 256) return -1;
            strncpy(host, rest, host_len);
            host[host_len] = '\0';
            strncpy(path, slash, 511);
            path[511] = '\0';
        } else {
            strncpy(host, rest, 255);
            host[255] = '\0';
        }
    } else if (strncmp(url, "http://", 7) == 0) {
        const char *rest = url + 7;
        const char *slash = strchr(rest, '/');
        const char *colon = strchr(rest, ':');

        if (colon && (!slash || colon < slash)) {
            size_t host_len = colon - rest;
            if (host_len >= 256) return -1;
            strncpy(host, rest, host_len);
            host[host_len] = '\0';
            *port = (int)strtol(colon + 1, NULL, 10);
            if (slash) {
                strncpy(path, slash, 511);
                path[511] = '\0';
            }
        } else if (slash) {
            size_t host_len = slash - rest;
            if (host_len >= 256) return -1;
            strncpy(host, rest, host_len);
            host[host_len] = '\0';
            strncpy(path, slash, 511);
            path[511] = '\0';
        } else {
            strncpy(host, rest, 255);
            host[255] = '\0';
        }
    } else {
        return -1;
    }

    return 0;
}

static const struct lws_protocols shared_http_protocols[] = {
    { "http", http_callback, 0, 16384, 0, NULL, 0 },
    { NULL, NULL, 0, 0, 0, NULL, 0 }
};

// Persistent SSL context — kept alive to avoid OPENSSL_cleanup() on lws 4.5+.
// lws_context_destroy() calls OPENSSL_cleanup() which permanently breaks SSL
// for the entire process, so HTTPS requests reuse this single context.
// Plain HTTP requests use disposable contexts (no SSL init = safe to destroy).
static struct lws_context *ssl_http_context = NULL;

static struct lws_context* get_http_context(int needs_ssl) {
    if (needs_ssl) {
        if (!ssl_http_context) {
            struct lws_context_creation_info info;
            memset(&info, 0, sizeof(info));
            info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
            info.port = CONTEXT_PORT_NO_LISTEN;
            info.max_http_header_data = 16384;
            info.protocols = shared_http_protocols;
            ssl_http_context = lws_create_context(&info);
        }
        return ssl_http_context;
    }

    // Plain HTTP — create a fresh disposable context (safe to destroy)
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.max_http_header_data = 16384;
    info.protocols = shared_http_protocols;
    return lws_create_context(&info);
}

// __lws_http_get(url: string): ptr
Value builtin_lws_http_get(Value *args, int num_args, ExecutionContext *ctx) {
    // SANDBOX: Check if network is allowed
    if (sandbox_is_restricted(ctx, HML_SANDBOX_RESTRICT_NETWORK)) {
        sandbox_error(ctx, "HTTP requests");
        return val_null();
    }

    lws_init_logging();

    if (num_args != 1) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_http_get() expects 1 argument");
        return val_null();
    }

    if (args[0].type != VAL_STRING) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_http_get() expects string URL");
        return val_null();
    }

    const char *url = args[0].as.as_string->data;
    char host[256], path[512];
    int port, ssl;

    if (parse_url(url, host, &port, path, &ssl) < 0) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Invalid URL format");
        return val_null();
    }

    http_response_t *resp = calloc(1, sizeof(http_response_t));
    if (!resp) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to allocate response");
        return val_null();
    }

    resp->body_capacity = 4096;
    resp->body = malloc(resp->body_capacity);
    if (!resp->body) {
        free(resp);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to allocate body buffer");
        return val_null();
    }
    resp->body[0] = '\0';

    lws_init_logging();
    struct lws_context *context = get_http_context(ssl);
    if (!context) {
        free(resp->body);
        free(resp);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to create libwebsockets context");
        return val_null();
    }

    struct lws_client_connect_info connect_info;
    memset(&connect_info, 0, sizeof(connect_info));
    connect_info.context = context;
    connect_info.address = host;
    connect_info.port = port;
    connect_info.path = path;
    connect_info.host = host;
    connect_info.origin = host;
    connect_info.method = "GET";
    connect_info.protocol = shared_http_protocols[0].name;
    connect_info.userdata = resp;

    struct lws *wsi;
    connect_info.pwsi = &wsi;

    // Disable automatic redirects - we'll handle them at the hemlock layer
    connect_info.ssl_connection = LCCSCF_HTTP_NO_FOLLOW_REDIRECT;

    if (ssl) {
        // SECURITY: Enable SSL with proper certificate validation
        // Removed LCCSCF_ALLOW_SELFSIGNED and LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK
        // to prevent MITM attacks
        connect_info.ssl_connection |= LCCSCF_USE_SSL;
    }

    if (!lws_client_connect_via_info(&connect_info)) {
        if (!ssl) lws_context_destroy(context);
        free(resp->body);
        free(resp);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to connect");
        return val_null();
    }

    // Event loop (timeout after 30 seconds)
    int timeout = 3000;
    while (!resp->complete && !resp->failed && timeout-- > 0) {
        lws_service(context, 10);
    }

    if (!ssl) lws_context_destroy(context);

    if (resp->failed || timeout <= 0) {
        if (resp->body) free(resp->body);
        if (resp->headers) free(resp->headers);
        if (resp->redirect_url) free(resp->redirect_url);
        free(resp);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("HTTP request failed or timed out");
        return val_null();
    }

    return val_ptr(resp);
}

// __lws_http_post(url: string, body: string, content_type: string): ptr
Value builtin_lws_http_post(Value *args, int num_args, ExecutionContext *ctx) {
    // SANDBOX: Check if network is allowed
    if (sandbox_is_restricted(ctx, HML_SANDBOX_RESTRICT_NETWORK)) {
        sandbox_error(ctx, "HTTP requests");
        return val_null();
    }

    lws_init_logging();

    if (num_args != 3) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_http_post() expects 3 arguments");
        return val_null();
    }

    if (args[0].type != VAL_STRING || args[1].type != VAL_STRING || args[2].type != VAL_STRING) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_http_post() expects string arguments");
        return val_null();
    }

    const char *url = args[0].as.as_string->data;
    const char *post_body = args[1].as.as_string->data;
    const char *content_type = args[2].as.as_string->data;

    char host[256], path[512];
    int port, ssl;

    if (parse_url(url, host, &port, path, &ssl) < 0) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Invalid URL format");
        return val_null();
    }

    http_response_t *resp = calloc(1, sizeof(http_response_t));
    if (!resp) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to allocate response");
        return val_null();
    }

    resp->body_capacity = 4096;
    resp->body = malloc(resp->body_capacity);
    if (!resp->body) {
        free(resp);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to allocate body buffer");
        return val_null();
    }
    resp->body[0] = '\0';

    lws_init_logging();
    struct lws_context *context = get_http_context(ssl);
    if (!context) {
        free(resp->body);
        free(resp);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to create libwebsockets context");
        return val_null();
    }

    struct lws_client_connect_info connect_info;
    memset(&connect_info, 0, sizeof(connect_info));
    connect_info.context = context;
    connect_info.address = host;
    connect_info.port = port;
    connect_info.path = path;
    connect_info.host = host;
    connect_info.origin = host;
    connect_info.method = "POST";
    connect_info.protocol = shared_http_protocols[0].name;
    connect_info.userdata = resp;

    struct lws *wsi;
    connect_info.pwsi = &wsi;

    // Disable automatic redirects - we'll handle them at the hemlock layer
    connect_info.ssl_connection = LCCSCF_HTTP_NO_FOLLOW_REDIRECT;

    if (ssl) {
        // SECURITY: Enable SSL with proper certificate validation
        // Removed LCCSCF_ALLOW_SELFSIGNED and LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK
        // to prevent MITM attacks
        connect_info.ssl_connection |= LCCSCF_USE_SSL;
    }

    // Note: POST body handling in libwebsockets is complex
    // This is a simplified version - real implementation would need WRITABLE callback
    (void)post_body;  // Suppress warning - not fully implemented yet
    (void)content_type;

    if (!lws_client_connect_via_info(&connect_info)) {
        if (!ssl) lws_context_destroy(context);
        free(resp->body);
        free(resp);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to connect");
        return val_null();
    }

    // Event loop (timeout after 30 seconds)
    int timeout = 3000;
    while (!resp->complete && !resp->failed && timeout-- > 0) {
        lws_service(context, 10);
    }

    if (!ssl) lws_context_destroy(context);

    if (resp->failed || timeout <= 0) {
        if (resp->body) free(resp->body);
        if (resp->headers) free(resp->headers);
        if (resp->redirect_url) free(resp->redirect_url);
        free(resp);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("HTTP request failed or timed out");
        return val_null();
    }

    return val_ptr(resp);
}

// __lws_http_request(method: string, url: string, body: string, content_type: string): ptr
// Generic HTTP request function supporting any method (PUT, DELETE, PATCH, etc.)
Value builtin_lws_http_request(Value *args, int num_args, ExecutionContext *ctx) {
    // SANDBOX: Check if network is allowed
    if (sandbox_is_restricted(ctx, HML_SANDBOX_RESTRICT_NETWORK)) {
        sandbox_error(ctx, "HTTP requests");
        return val_null();
    }

    lws_init_logging();

    if (num_args != 4) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_http_request() expects 4 arguments (method, url, body, content_type)");
        return val_null();
    }

    if (args[0].type != VAL_STRING || args[1].type != VAL_STRING ||
        args[2].type != VAL_STRING || args[3].type != VAL_STRING) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_http_request() expects string arguments");
        return val_null();
    }

    const char *method = args[0].as.as_string->data;
    const char *url = args[1].as.as_string->data;
    const char *body = args[2].as.as_string->data;
    const char *content_type = args[3].as.as_string->data;

    char host[256], path[512];
    int port, ssl;

    if (parse_url(url, host, &port, path, &ssl) < 0) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Invalid URL format");
        return val_null();
    }

    http_response_t *resp = calloc(1, sizeof(http_response_t));
    if (!resp) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to allocate response");
        return val_null();
    }

    resp->body_capacity = 4096;
    resp->body = malloc(resp->body_capacity);
    if (!resp->body) {
        free(resp);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to allocate body buffer");
        return val_null();
    }
    resp->body[0] = '\0';

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.max_http_header_data = 16384;
    info.protocols = shared_http_protocols;

    struct lws_context *context = lws_create_context(&info);
    if (!context) {
        free(resp->body);
        free(resp);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to create libwebsockets context");
        return val_null();
    }

    struct lws_client_connect_info connect_info;
    memset(&connect_info, 0, sizeof(connect_info));
    connect_info.context = context;
    connect_info.address = host;
    connect_info.port = port;
    connect_info.path = path;
    connect_info.host = host;
    connect_info.origin = host;
    connect_info.method = method;  // Use the provided method
    connect_info.protocol = shared_http_protocols[0].name;
    connect_info.userdata = resp;

    struct lws *wsi;
    connect_info.pwsi = &wsi;

    // Disable automatic redirects
    connect_info.ssl_connection = LCCSCF_HTTP_NO_FOLLOW_REDIRECT;

    if (ssl) {
        // SECURITY: Enable SSL with proper certificate validation
        // Removed LCCSCF_ALLOW_SELFSIGNED and LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK
        // to prevent MITM attacks
        connect_info.ssl_connection |= LCCSCF_USE_SSL;
    }

    // Note: Body handling for PUT/DELETE is simplified
    (void)body;
    (void)content_type;

    if (!lws_client_connect_via_info(&connect_info)) {
        if (!ssl) lws_context_destroy(context);
        free(resp->body);
        free(resp);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to connect");
        return val_null();
    }

    // Event loop (timeout after 30 seconds)
    int timeout = 3000;
    while (!resp->complete && !resp->failed && timeout-- > 0) {
        lws_service(context, 10);
    }

    if (!ssl) lws_context_destroy(context);

    if (resp->failed || timeout <= 0) {
        if (resp->body) free(resp->body);
        if (resp->headers) free(resp->headers);
        if (resp->redirect_url) free(resp->redirect_url);
        free(resp);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("HTTP request failed or timed out");
        return val_null();
    }

    return val_ptr(resp);
}

// __lws_http_get_timeout(url: string, timeout_ms: i32): ptr
// HTTP GET with configurable timeout
Value builtin_lws_http_get_timeout(Value *args, int num_args, ExecutionContext *ctx) {
    // SANDBOX: Check if network is allowed
    if (sandbox_is_restricted(ctx, HML_SANDBOX_RESTRICT_NETWORK)) {
        sandbox_error(ctx, "HTTP requests");
        return val_null();
    }

    lws_init_logging();

    if (num_args != 2) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_http_get_timeout() expects 2 arguments (url, timeout_ms)");
        return val_null();
    }

    if (args[0].type != VAL_STRING) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_http_get_timeout() expects string URL");
        return val_null();
    }

    // Extract timeout from second argument (accept any numeric type)
    int timeout_ms;
    if (args[1].type == VAL_I32) {
        timeout_ms = args[1].as.as_i32;
    } else if (args[1].type == VAL_I64) {
        timeout_ms = (int)args[1].as.as_i64;
    } else if (args[1].type == VAL_F64) {
        timeout_ms = (int)args[1].as.as_f64;
    } else {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_http_get_timeout() expects numeric timeout_ms");
        return val_null();
    }

    // Convert timeout_ms to iterations (each iteration is ~10ms)
    int timeout_iterations = timeout_ms / 10;
    if (timeout_iterations < 1) timeout_iterations = 1;

    const char *url = args[0].as.as_string->data;
    char host[256], path[512];
    int port, ssl;

    if (parse_url(url, host, &port, path, &ssl) < 0) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Invalid URL format");
        return val_null();
    }

    http_response_t *resp = calloc(1, sizeof(http_response_t));
    if (!resp) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to allocate response");
        return val_null();
    }

    resp->body_capacity = 4096;
    resp->body = malloc(resp->body_capacity);
    if (!resp->body) {
        free(resp);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to allocate body buffer");
        return val_null();
    }
    resp->body[0] = '\0';

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.max_http_header_data = 16384;
    info.protocols = shared_http_protocols;

    struct lws_context *context = lws_create_context(&info);
    if (!context) {
        free(resp->body);
        free(resp);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to create libwebsockets context");
        return val_null();
    }

    struct lws_client_connect_info connect_info;
    memset(&connect_info, 0, sizeof(connect_info));
    connect_info.context = context;
    connect_info.address = host;
    connect_info.port = port;
    connect_info.path = path;
    connect_info.host = host;
    connect_info.origin = host;
    connect_info.method = "GET";
    connect_info.protocol = shared_http_protocols[0].name;
    connect_info.userdata = resp;

    struct lws *wsi;
    connect_info.pwsi = &wsi;

    connect_info.ssl_connection = LCCSCF_HTTP_NO_FOLLOW_REDIRECT;

    if (ssl) {
        connect_info.ssl_connection |= LCCSCF_USE_SSL;
    }

    if (!lws_client_connect_via_info(&connect_info)) {
        if (!ssl) lws_context_destroy(context);
        free(resp->body);
        free(resp);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to connect");
        return val_null();
    }

    // Event loop with custom timeout
    int timeout = timeout_iterations;
    while (!resp->complete && !resp->failed && timeout-- > 0) {
        lws_service(context, 10);
    }

    if (!ssl) lws_context_destroy(context);

    if (resp->failed || timeout <= 0) {
        if (resp->body) free(resp->body);
        if (resp->headers) free(resp->headers);
        if (resp->redirect_url) free(resp->redirect_url);
        free(resp);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("HTTP request failed or timed out");
        return val_null();
    }

    return val_ptr(resp);
}

// __lws_http_post_timeout(url: string, body: string, content_type: string, timeout_ms: i32): ptr
// HTTP POST with configurable timeout
Value builtin_lws_http_post_timeout(Value *args, int num_args, ExecutionContext *ctx) {
    // SANDBOX: Check if network is allowed
    if (sandbox_is_restricted(ctx, HML_SANDBOX_RESTRICT_NETWORK)) {
        sandbox_error(ctx, "HTTP requests");
        return val_null();
    }

    lws_init_logging();

    if (num_args != 4) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_http_post_timeout() expects 4 arguments (url, body, content_type, timeout_ms)");
        return val_null();
    }

    if (args[0].type != VAL_STRING || args[1].type != VAL_STRING || args[2].type != VAL_STRING) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_http_post_timeout() expects string arguments");
        return val_null();
    }

    // Extract timeout from fourth argument
    int timeout_ms;
    if (args[3].type == VAL_I32) {
        timeout_ms = args[3].as.as_i32;
    } else if (args[3].type == VAL_I64) {
        timeout_ms = (int)args[3].as.as_i64;
    } else if (args[3].type == VAL_F64) {
        timeout_ms = (int)args[3].as.as_f64;
    } else {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_http_post_timeout() expects numeric timeout_ms");
        return val_null();
    }

    int timeout_iterations = timeout_ms / 10;
    if (timeout_iterations < 1) timeout_iterations = 1;

    const char *url = args[0].as.as_string->data;
    (void)args[1];  // body - not fully implemented
    (void)args[2];  // content_type

    char host[256], path[512];
    int port, ssl;

    if (parse_url(url, host, &port, path, &ssl) < 0) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Invalid URL format");
        return val_null();
    }

    http_response_t *resp = calloc(1, sizeof(http_response_t));
    if (!resp) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to allocate response");
        return val_null();
    }

    resp->body_capacity = 4096;
    resp->body = malloc(resp->body_capacity);
    if (!resp->body) {
        free(resp);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to allocate body buffer");
        return val_null();
    }
    resp->body[0] = '\0';

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.max_http_header_data = 16384;
    info.protocols = shared_http_protocols;

    struct lws_context *context = lws_create_context(&info);
    if (!context) {
        free(resp->body);
        free(resp);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to create libwebsockets context");
        return val_null();
    }

    struct lws_client_connect_info connect_info;
    memset(&connect_info, 0, sizeof(connect_info));
    connect_info.context = context;
    connect_info.address = host;
    connect_info.port = port;
    connect_info.path = path;
    connect_info.host = host;
    connect_info.origin = host;
    connect_info.method = "POST";
    connect_info.protocol = shared_http_protocols[0].name;
    connect_info.userdata = resp;

    struct lws *wsi;
    connect_info.pwsi = &wsi;

    connect_info.ssl_connection = LCCSCF_HTTP_NO_FOLLOW_REDIRECT;

    if (ssl) {
        connect_info.ssl_connection |= LCCSCF_USE_SSL;
    }

    if (!lws_client_connect_via_info(&connect_info)) {
        if (!ssl) lws_context_destroy(context);
        free(resp->body);
        free(resp);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to connect");
        return val_null();
    }

    // Event loop with custom timeout
    int timeout = timeout_iterations;
    while (!resp->complete && !resp->failed && timeout-- > 0) {
        lws_service(context, 10);
    }

    if (!ssl) lws_context_destroy(context);

    if (resp->failed || timeout <= 0) {
        if (resp->body) free(resp->body);
        if (resp->headers) free(resp->headers);
        if (resp->redirect_url) free(resp->redirect_url);
        free(resp);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("HTTP request failed or timed out");
        return val_null();
    }

    return val_ptr(resp);
}

// __lws_http_request_timeout(method: string, url: string, body: string, content_type: string, timeout_ms: i32): ptr
// Generic HTTP request with configurable timeout
Value builtin_lws_http_request_timeout(Value *args, int num_args, ExecutionContext *ctx) {
    // SANDBOX: Check if network is allowed
    if (sandbox_is_restricted(ctx, HML_SANDBOX_RESTRICT_NETWORK)) {
        sandbox_error(ctx, "HTTP requests");
        return val_null();
    }

    lws_init_logging();

    if (num_args != 5) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_http_request_timeout() expects 5 arguments (method, url, body, content_type, timeout_ms)");
        return val_null();
    }

    if (args[0].type != VAL_STRING || args[1].type != VAL_STRING ||
        args[2].type != VAL_STRING || args[3].type != VAL_STRING) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_http_request_timeout() expects string arguments");
        return val_null();
    }

    // Extract timeout from fifth argument
    int timeout_ms;
    if (args[4].type == VAL_I32) {
        timeout_ms = args[4].as.as_i32;
    } else if (args[4].type == VAL_I64) {
        timeout_ms = (int)args[4].as.as_i64;
    } else if (args[4].type == VAL_F64) {
        timeout_ms = (int)args[4].as.as_f64;
    } else {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_http_request_timeout() expects numeric timeout_ms");
        return val_null();
    }

    int timeout_iterations = timeout_ms / 10;
    if (timeout_iterations < 1) timeout_iterations = 1;

    const char *method = args[0].as.as_string->data;
    const char *url = args[1].as.as_string->data;
    (void)args[2];  // body
    (void)args[3];  // content_type

    char host[256], path[512];
    int port, ssl;

    if (parse_url(url, host, &port, path, &ssl) < 0) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Invalid URL format");
        return val_null();
    }

    http_response_t *resp = calloc(1, sizeof(http_response_t));
    if (!resp) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to allocate response");
        return val_null();
    }

    resp->body_capacity = 4096;
    resp->body = malloc(resp->body_capacity);
    if (!resp->body) {
        free(resp);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to allocate body buffer");
        return val_null();
    }
    resp->body[0] = '\0';

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.max_http_header_data = 16384;
    info.protocols = shared_http_protocols;

    struct lws_context *context = lws_create_context(&info);
    if (!context) {
        free(resp->body);
        free(resp);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to create libwebsockets context");
        return val_null();
    }

    struct lws_client_connect_info connect_info;
    memset(&connect_info, 0, sizeof(connect_info));
    connect_info.context = context;
    connect_info.address = host;
    connect_info.port = port;
    connect_info.path = path;
    connect_info.host = host;
    connect_info.origin = host;
    connect_info.method = method;
    connect_info.protocol = shared_http_protocols[0].name;
    connect_info.userdata = resp;

    struct lws *wsi;
    connect_info.pwsi = &wsi;

    connect_info.ssl_connection = LCCSCF_HTTP_NO_FOLLOW_REDIRECT;

    if (ssl) {
        connect_info.ssl_connection |= LCCSCF_USE_SSL;
    }

    if (!lws_client_connect_via_info(&connect_info)) {
        if (!ssl) lws_context_destroy(context);
        free(resp->body);
        free(resp);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to connect");
        return val_null();
    }

    // Event loop with custom timeout
    int timeout = timeout_iterations;
    while (!resp->complete && !resp->failed && timeout-- > 0) {
        lws_service(context, 10);
    }

    if (!ssl) lws_context_destroy(context);

    if (resp->failed || timeout <= 0) {
        if (resp->body) free(resp->body);
        if (resp->headers) free(resp->headers);
        if (resp->redirect_url) free(resp->redirect_url);
        free(resp);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("HTTP request failed or timed out");
        return val_null();
    }

    return val_ptr(resp);
}

// __lws_response_status(resp: ptr): i32
Value builtin_lws_response_status(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_response_status() expects 1 argument");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_response_status() expects ptr");
        return val_null();
    }

    http_response_t *resp = (http_response_t *)args[0].as.as_ptr;
    if (!resp) {
        return val_i32(0);
    }

    return val_i32(resp->status_code);
}

// __lws_response_body(resp: ptr): string
Value builtin_lws_response_body(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_response_body() expects 1 argument");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_response_body() expects ptr");
        return val_null();
    }

    http_response_t *resp = (http_response_t *)args[0].as.as_ptr;
    if (!resp || !resp->body) {
        return val_string("");
    }

    return val_string(resp->body);
}

// __lws_response_body_binary(resp: ptr): buffer
// Returns the response body as a binary buffer (preserves null bytes)
Value builtin_lws_response_body_binary(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_response_body_binary() expects 1 argument");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_response_body_binary() expects ptr");
        return val_null();
    }

    http_response_t *resp = (http_response_t *)args[0].as.as_ptr;
    if (!resp || !resp->body || resp->body_len == 0) {
        // Return empty buffer
        Buffer *buf = malloc(sizeof(Buffer));
        if (!buf) {
            ctx->exception_state.is_throwing = 1;
            ctx->exception_state.exception_value = val_string("Memory allocation failed");
            return val_null();
        }
        buf->data = malloc(1);
        if (!buf->data) {
            free(buf);
            ctx->exception_state.is_throwing = 1;
            ctx->exception_state.exception_value = val_string("Memory allocation failed");
            return val_null();
        }
        buf->length = 0;
        buf->capacity = 1;
        buf->ref_count = 1;
        atomic_store(&buf->freed, 0);
        return (Value){ .type = VAL_BUFFER, .as.as_buffer = buf };
    }

    // Create buffer with full binary data
    Buffer *buf = malloc(sizeof(Buffer));
    if (!buf) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Memory allocation failed");
        return val_null();
    }

    buf->data = malloc(resp->body_len);
    if (!buf->data) {
        free(buf);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Memory allocation failed");
        return val_null();
    }

    memcpy(buf->data, resp->body, resp->body_len);
    buf->length = resp->body_len;
    buf->capacity = resp->body_len;
    buf->ref_count = 1;
    atomic_store(&buf->freed, 0);

    return (Value){ .type = VAL_BUFFER, .as.as_buffer = buf };
}

// __lws_response_headers(resp: ptr): string
Value builtin_lws_response_headers(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_response_headers() expects 1 argument");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_response_headers() expects ptr");
        return val_null();
    }

    http_response_t *resp = (http_response_t *)args[0].as.as_ptr;
    if (!resp || !resp->headers) {
        return val_string("");
    }

    return val_string(resp->headers);
}

// __lws_response_redirect(resp: ptr): string
Value builtin_lws_response_redirect(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_response_redirect() expects 1 argument");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_response_redirect() expects ptr");
        return val_null();
    }

    http_response_t *resp = (http_response_t *)args[0].as.as_ptr;
    if (!resp || !resp->redirect_url) {
        return val_null();
    }

    return val_string(resp->redirect_url);
}

// __lws_response_free(resp: ptr): null
Value builtin_lws_response_free(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_response_free() expects 1 argument");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_response_free() expects ptr");
        return val_null();
    }

    http_response_t *resp = (http_response_t *)args[0].as.as_ptr;
    if (resp) {
        if (resp->body) free(resp->body);
        if (resp->redirect_url) free(resp->redirect_url);
        if (resp->headers) free(resp->headers);
        free(resp);
    }

    return val_null();
}

// ========== STREAMING HTTP SUPPORT ==========
// For chunked/SSE responses (e.g., streaming LLM output)

// Chunk queue node for streaming responses
typedef struct http_stream_chunk {
    char *data;
    size_t len;
    struct http_stream_chunk *next;
} http_stream_chunk_t;

// Streaming HTTP connection
typedef struct {
    struct lws_context *context;
    struct lws *wsi;
    int status_code;
    char *headers;
    int established;     // Headers received
    int complete;        // Response fully received
    int failed;
    pthread_t service_thread;
    volatile int shutdown;
    // Thread-safe chunk queue
    http_stream_chunk_t *chunk_head;
    http_stream_chunk_t *chunk_tail;
    pthread_mutex_t chunk_mutex;
    pthread_cond_t chunk_cond;
} http_stream_t;

static void http_stream_enqueue(http_stream_t *stream, const char *data, size_t len) {
    http_stream_chunk_t *chunk = malloc(sizeof(http_stream_chunk_t));
    if (!chunk) return;
    chunk->data = malloc(len + 1);
    if (!chunk->data) { free(chunk); return; }
    memcpy(chunk->data, data, len);
    chunk->data[len] = '\0';
    chunk->len = len;
    chunk->next = NULL;

    pthread_mutex_lock(&stream->chunk_mutex);
    if (stream->chunk_tail) {
        stream->chunk_tail->next = chunk;
    } else {
        stream->chunk_head = chunk;
    }
    stream->chunk_tail = chunk;
    pthread_cond_signal(&stream->chunk_cond);
    pthread_mutex_unlock(&stream->chunk_mutex);
}

// libwebsockets callback for streaming HTTP
static int http_stream_callback(struct lws *wsi, enum lws_callback_reasons reason,
                                void *user, void *in, size_t len) {
    http_stream_t *stream = (http_stream_t *)user;

    switch (reason) {
        case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER: {
            unsigned char **p = (unsigned char **)in;
            unsigned char *end = (*p) + len;
            const char *ua = "User-Agent: hemlock/1.0\r\n";
            size_t ua_len = strlen(ua);
            if (end - *p >= (int)ua_len) {
                memcpy(*p, ua, ua_len);
                *p += ua_len;
            }
            const char *accept = "Accept: text/event-stream, application/json, */*\r\n";
            size_t accept_len = strlen(accept);
            if (end - *p >= (int)accept_len) {
                memcpy(*p, accept, accept_len);
                *p += accept_len;
            }
            break;
        }

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            if (stream) {
                stream->failed = 1;
                stream->complete = 1;
                pthread_mutex_lock(&stream->chunk_mutex);
                pthread_cond_signal(&stream->chunk_cond);
                pthread_mutex_unlock(&stream->chunk_mutex);
            }
            break;

        case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP:
            if (stream) {
                stream->status_code = lws_http_client_http_response(wsi);
                stream->established = 1;

                // Capture response headers
                char headers_buf[8192];
                size_t headers_len = 0;
                char value[1024];
                int vlen;
                struct { enum lws_token_indexes token; const char *name; } header_list[] = {
                    { WSI_TOKEN_HTTP_CONTENT_TYPE, "Content-Type" },
                    { WSI_TOKEN_HTTP_CONTENT_LENGTH, "Content-Length" },
                    { WSI_TOKEN_HTTP_CACHE_CONTROL, "Cache-Control" },
                    { WSI_TOKEN_HTTP_DATE, "Date" },
                    { WSI_TOKEN_HTTP_TRANSFER_ENCODING, "Transfer-Encoding" },
                    { WSI_TOKEN_HTTP_ACCESS_CONTROL_ALLOW_ORIGIN, "Access-Control-Allow-Origin" },
                };
                for (size_t i = 0; i < sizeof(header_list)/sizeof(header_list[0]); i++) {
                    vlen = lws_hdr_copy(wsi, value, sizeof(value), header_list[i].token);
                    if (vlen > 0) {
                        value[vlen] = '\0';
                        int written = snprintf(headers_buf + headers_len,
                                               sizeof(headers_buf) - headers_len,
                                               "%s: %s\r\n", header_list[i].name, value);
                        if (written > 0 && (size_t)written < sizeof(headers_buf) - headers_len) {
                            headers_len += written;
                        }
                    }
                }
                if (headers_len > 0) {
                    stream->headers = strndup(headers_buf, headers_len);
                }
            }
            break;

        case LWS_CALLBACK_RECEIVE_CLIENT_HTTP: {
            char buffer[4096 + LWS_PRE];
            char *px = buffer + LWS_PRE;
            int lenx = sizeof(buffer) - LWS_PRE;
            if (lws_http_client_read(wsi, &px, &lenx) < 0)
                return -1;
            return 0;
        }

        case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ:
            if (stream && len > 0) {
                http_stream_enqueue(stream, (const char *)in, len);
            }
            return 0;

        case LWS_CALLBACK_COMPLETED_CLIENT_HTTP:
            if (stream) {
                stream->complete = 1;
                pthread_mutex_lock(&stream->chunk_mutex);
                pthread_cond_signal(&stream->chunk_cond);
                pthread_mutex_unlock(&stream->chunk_mutex);
            }
            break;

        case LWS_CALLBACK_CLOSED_CLIENT_HTTP:
            if (stream) {
                stream->complete = 1;
                pthread_mutex_lock(&stream->chunk_mutex);
                pthread_cond_signal(&stream->chunk_cond);
                pthread_mutex_unlock(&stream->chunk_mutex);
            }
            break;

        default:
            break;
    }
    return 0;
}

// Service thread for streaming HTTP
static void* http_stream_service_thread(void *arg) {
    http_stream_t *stream = (http_stream_t *)arg;
    while (!stream->shutdown && !stream->complete && !stream->failed) {
        lws_service(stream->context, 50);
    }
    // Final service calls to drain remaining data
    for (int i = 0; i < 10 && !stream->shutdown; i++) {
        lws_service(stream->context, 10);
    }
    return NULL;
}

// __lws_http_stream_start(method, url, body, content_type, timeout_ms): ptr
Value builtin_lws_http_stream_start(Value *args, int num_args, ExecutionContext *ctx) {
    if (sandbox_is_restricted(ctx, HML_SANDBOX_RESTRICT_NETWORK)) {
        sandbox_error(ctx, "HTTP requests");
        return val_null();
    }

    lws_init_logging();

    if (num_args != 5) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_http_stream_start() expects 5 arguments");
        return val_null();
    }

    if (args[0].type != VAL_STRING || args[1].type != VAL_STRING) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_http_stream_start() expects string method and URL");
        return val_null();
    }

    const char *method = args[0].as.as_string->data;
    const char *url = args[1].as.as_string->data;
    const char *post_body = (args[2].type == VAL_STRING) ? args[2].as.as_string->data : "";
    const char *content_type = (args[3].type == VAL_STRING) ? args[3].as.as_string->data : "";
    int timeout_ms = 120000;  // Default 2 minutes for streaming
    if (args[4].type == VAL_I32) timeout_ms = args[4].as.as_i32;
    else if (args[4].type == VAL_I64) timeout_ms = (int)args[4].as.as_i64;

    (void)post_body;
    (void)content_type;

    char host[256], path[512];
    int port, ssl;
    if (parse_url(url, host, &port, path, &ssl) < 0) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Invalid URL format");
        return val_null();
    }

    http_stream_t *stream = calloc(1, sizeof(http_stream_t));
    if (!stream) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to allocate stream");
        return val_null();
    }
    pthread_mutex_init(&stream->chunk_mutex, NULL);
    pthread_cond_init(&stream->chunk_cond, NULL);

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.max_http_header_data = 16384;

    static const struct lws_protocols stream_protocols[] = {
        { "http-stream", http_stream_callback, 0, 16384, 0, NULL, 0 },
        { NULL, NULL, 0, 0, 0, NULL, 0 }
    };
    info.protocols = stream_protocols;

    stream->context = lws_create_context(&info);
    if (!stream->context) {
        pthread_mutex_destroy(&stream->chunk_mutex);
        pthread_cond_destroy(&stream->chunk_cond);
        free(stream);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to create libwebsockets context");
        return val_null();
    }

    struct lws_client_connect_info connect_info;
    memset(&connect_info, 0, sizeof(connect_info));
    connect_info.context = stream->context;
    connect_info.address = host;
    connect_info.port = port;
    connect_info.path = path;
    connect_info.host = host;
    connect_info.origin = host;
    connect_info.method = method;
    connect_info.protocol = stream_protocols[0].name;
    connect_info.userdata = stream;
    connect_info.pwsi = &stream->wsi;
    connect_info.ssl_connection = LCCSCF_HTTP_NO_FOLLOW_REDIRECT;

    if (ssl) {
        connect_info.ssl_connection |= LCCSCF_USE_SSL;
    }

    if (!lws_client_connect_via_info(&connect_info)) {
        lws_context_destroy(stream->context);
        pthread_mutex_destroy(&stream->chunk_mutex);
        pthread_cond_destroy(&stream->chunk_cond);
        free(stream);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to connect for streaming");
        return val_null();
    }

    // Wait for connection establishment (headers)
    int wait_iters = timeout_ms / 10;
    if (wait_iters < 1) wait_iters = 1;
    while (!stream->established && !stream->failed && !stream->complete && wait_iters-- > 0) {
        lws_service(stream->context, 10);
    }

    if (stream->failed || (!stream->established && wait_iters <= 0)) {
        lws_context_destroy(stream->context);
        if (stream->headers) free(stream->headers);
        pthread_mutex_destroy(&stream->chunk_mutex);
        pthread_cond_destroy(&stream->chunk_cond);
        free(stream);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Streaming HTTP connection failed or timed out");
        return val_null();
    }

    // Start background service thread to receive chunks
    stream->shutdown = 0;
    if (pthread_create(&stream->service_thread, NULL, http_stream_service_thread, stream) != 0) {
        lws_context_destroy(stream->context);
        if (stream->headers) free(stream->headers);
        pthread_mutex_destroy(&stream->chunk_mutex);
        pthread_cond_destroy(&stream->chunk_cond);
        free(stream);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to create stream service thread");
        return val_null();
    }

    return val_ptr(stream);
}

// __lws_http_stream_read(stream, timeout_ms): string|null
Value builtin_lws_http_stream_read(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_http_stream_read() expects 2 arguments");
        return val_null();
    }
    if (args[0].type != VAL_PTR) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_http_stream_read() expects ptr");
        return val_null();
    }

    http_stream_t *stream = (http_stream_t *)args[0].as.as_ptr;
    if (!stream) return val_null();

    int timeout_ms = 30000;
    if (args[1].type == VAL_I32) timeout_ms = args[1].as.as_i32;
    else if (args[1].type == VAL_I64) timeout_ms = (int)args[1].as.as_i64;

    // Poll for chunks with timeout
    int iterations = timeout_ms / 10;
    if (iterations < 1) iterations = 1;

    while (iterations-- > 0) {
        pthread_mutex_lock(&stream->chunk_mutex);
        if (stream->chunk_head) {
            http_stream_chunk_t *chunk = stream->chunk_head;
            stream->chunk_head = chunk->next;
            if (!stream->chunk_head) stream->chunk_tail = NULL;
            pthread_mutex_unlock(&stream->chunk_mutex);

            Value result = val_string(chunk->data);
            free(chunk->data);
            free(chunk);
            return result;
        }
        // Stream is done and no more chunks
        if (stream->complete || stream->failed) {
            pthread_mutex_unlock(&stream->chunk_mutex);
            return val_null();
        }
        pthread_mutex_unlock(&stream->chunk_mutex);
        usleep(10000);  // 10ms
    }

    return val_null();  // Timeout
}

// __lws_http_stream_status(stream): i32
Value builtin_lws_http_stream_status(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1 || args[0].type != VAL_PTR) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_http_stream_status() expects ptr");
        return val_i32(0);
    }
    http_stream_t *stream = (http_stream_t *)args[0].as.as_ptr;
    return val_i32(stream ? stream->status_code : 0);
}

// __lws_http_stream_headers(stream): string
Value builtin_lws_http_stream_headers(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1 || args[0].type != VAL_PTR) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_http_stream_headers() expects ptr");
        return val_string("");
    }
    http_stream_t *stream = (http_stream_t *)args[0].as.as_ptr;
    if (!stream || !stream->headers) return val_string("");
    return val_string(stream->headers);
}

// __lws_http_stream_close(stream): null
Value builtin_lws_http_stream_close(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1 || args[0].type != VAL_PTR) {
        return val_null();
    }
    http_stream_t *stream = (http_stream_t *)args[0].as.as_ptr;
    if (!stream) return val_null();

    stream->shutdown = 1;
    stream->complete = 1;
    pthread_join(stream->service_thread, NULL);

    // Drain remaining chunks
    pthread_mutex_lock(&stream->chunk_mutex);
    http_stream_chunk_t *chunk = stream->chunk_head;
    while (chunk) {
        http_stream_chunk_t *next = chunk->next;
        free(chunk->data);
        free(chunk);
        chunk = next;
    }
    pthread_mutex_unlock(&stream->chunk_mutex);

    pthread_mutex_destroy(&stream->chunk_mutex);
    pthread_cond_destroy(&stream->chunk_cond);

    if (stream->context) lws_context_destroy(stream->context);
    if (stream->headers) free(stream->headers);
    free(stream);

    return val_null();
}

// ========== WEBSOCKET SUPPORT ==========

typedef struct ws_message {
    unsigned char *data;
    size_t len;
    int is_binary;
    struct ws_message *next;
} ws_message_t;

typedef struct {
    struct lws_context *context;
    struct lws *wsi;
    ws_message_t *msg_queue_head;
    ws_message_t *msg_queue_tail;
    int closed;
    int failed;
    int established;
    char *send_buffer;
    size_t send_len;
    int send_pending;
    pthread_t service_thread;
    volatile int shutdown;
    int has_own_thread;
    int owns_memory;
} ws_connection_t;

typedef struct ws_server {
    struct lws_context *context;
    struct lws *pending_wsi;
    ws_connection_t *pending_conn;
    int port;
    int closed;
    pthread_t service_thread;
    volatile int shutdown;
    pthread_mutex_t pending_mutex;
} ws_server_t;

// Forward declarations for internal close functions
static void ws_connection_close(ws_connection_t *conn);
static void ws_server_close_internal(ws_server_t *server);

// ========== WEBSOCKET HANDLE OPERATIONS ==========

// Free a WebSocketHandle and its underlying connection
void websocket_free(WebSocketHandle *ws) {
    if (!ws) return;

    if (!ws->closed && ws->handle) {
        if (ws->is_server) {
            ws_server_close_internal((ws_server_t *)ws->handle);
        } else {
            ws_connection_close((ws_connection_t *)ws->handle);
        }
    }

    if (ws->url) free(ws->url);
    if (ws->host) free(ws->host);
    free(ws);
}

// Get a property from a WebSocket handle
Value get_websocket_property(WebSocketHandle *ws, const char *property, ExecutionContext *ctx) {
    (void)ctx;

    if (strcmp(property, "url") == 0) {
        return ws->url ? val_string(ws->url) : val_null();
    } else if (strcmp(property, "host") == 0) {
        return ws->host ? val_string(ws->host) : val_null();
    } else if (strcmp(property, "port") == 0) {
        return val_i32(ws->port);
    } else if (strcmp(property, "closed") == 0) {
        // Check actual connection state
        if (ws->closed) return val_bool(1);
        if (ws->handle) {
            if (ws->is_server) {
                return val_bool(((ws_server_t *)ws->handle)->closed);
            } else {
                return val_bool(((ws_connection_t *)ws->handle)->closed);
            }
        }
        return val_bool(0);
    }

    return val_null();
}

// Internal helper to close a client connection
static void ws_connection_close(ws_connection_t *conn) {
    if (!conn) return;

    conn->closed = 1;
    conn->shutdown = 1;

    if (conn->has_own_thread) {
        pthread_join(conn->service_thread, NULL);
    }

    ws_message_t *msg = conn->msg_queue_head;
    while (msg) {
        ws_message_t *next = msg->next;
        if (msg->data) free(msg->data);
        free(msg);
        msg = next;
    }

    if (conn->send_buffer) {
        free(conn->send_buffer);
    }

    if (conn->has_own_thread && conn->context) {
        lws_context_destroy(conn->context);
    }

    if (conn->owns_memory) {
        free(conn);
    }
}

// Internal helper to close a server
static void ws_server_close_internal(ws_server_t *server) {
    if (!server) return;

    // Signal shutdown - accept() checks this flag before each iteration
    server->closed = 1;
    server->shutdown = 1;

    // Wait for any in-flight accept() call to finish its current iteration.
    // accept() sleeps 10ms per iteration, so 50ms is sufficient.
    usleep(50000);

    pthread_join(server->service_thread, NULL);
    pthread_mutex_destroy(&server->pending_mutex);
    if (server->context) {
        lws_context_destroy(server->context);
    }
    free(server);
}

// Service thread for WebSocket clients
static void* ws_service_thread(void *arg) {
    ws_connection_t *conn = (ws_connection_t *)arg;
    while (!conn->shutdown) {
        lws_service(conn->context, 50);
    }
    return NULL;
}

// Service thread for WebSocket servers
static void* ws_server_service_thread(void *arg) {
    ws_server_t *server = (ws_server_t *)arg;
    while (!server->shutdown) {
        lws_service(server->context, 50);
    }
    return NULL;
}

// WebSocket callback (client)
static int ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                       void *user, void *in, size_t len) {
    ws_connection_t *conn = (ws_connection_t *)user;

    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            if (conn) {
                conn->wsi = wsi;
                conn->established = 1;
            }
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE:
            if (conn) {
                ws_message_t *msg = malloc(sizeof(ws_message_t));
                if (!msg) break;

                msg->len = len;
                msg->data = malloc(len + 1);
                if (!msg->data) {
                    free(msg);
                    break;
                }
                memcpy(msg->data, in, len);
                msg->data[len] = '\0';
                msg->is_binary = lws_frame_is_binary(wsi);
                msg->next = NULL;

                if (conn->msg_queue_tail) {
                    conn->msg_queue_tail->next = msg;
                } else {
                    conn->msg_queue_head = msg;
                }
                conn->msg_queue_tail = msg;
            }
            break;

        case LWS_CALLBACK_CLIENT_WRITEABLE:
            if (conn && conn->send_pending && conn->send_buffer) {
                lws_write(wsi, (unsigned char *)conn->send_buffer + LWS_PRE,
                         conn->send_len, LWS_WRITE_TEXT);
                free(conn->send_buffer);
                conn->send_buffer = NULL;
                conn->send_pending = 0;
            }
            break;

        case LWS_CALLBACK_CLOSED:
            if (conn) {
                conn->closed = 1;
            }
            break;

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            if (conn) {
                conn->failed = 1;
                conn->closed = 1;
            }
            break;

        default:
            break;
    }

    return 0;
}

// WebSocket server callback
static int ws_server_callback(struct lws *wsi, enum lws_callback_reasons reason,
                              void *user, void *in, size_t len) {
    ws_server_t *server = (ws_server_t *)lws_context_user(lws_get_context(wsi));
    ws_connection_t *conn = (ws_connection_t *)user;

    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            if (conn) {
                conn->wsi = wsi;
                conn->context = lws_get_context(wsi);
                conn->shutdown = 0;
                conn->has_own_thread = 0;
                conn->owns_memory = 0;
            }
            if (server) {
                pthread_mutex_lock(&server->pending_mutex);
                if (!server->pending_wsi) {
                    server->pending_wsi = wsi;
                    server->pending_conn = conn;
                }
                pthread_mutex_unlock(&server->pending_mutex);
            }
            break;

        case LWS_CALLBACK_RECEIVE:
            if (conn) {
                ws_message_t *msg = malloc(sizeof(ws_message_t));
                if (!msg) break;

                msg->len = len;
                msg->data = malloc(len + 1);
                if (!msg->data) {
                    free(msg);
                    break;
                }
                memcpy(msg->data, in, len);
                msg->data[len] = '\0';
                msg->is_binary = lws_frame_is_binary(wsi);
                msg->next = NULL;

                if (conn->msg_queue_tail) {
                    conn->msg_queue_tail->next = msg;
                } else {
                    conn->msg_queue_head = msg;
                }
                conn->msg_queue_tail = msg;
            }
            break;

        case LWS_CALLBACK_SERVER_WRITEABLE:
            if (conn && conn->send_pending && conn->send_buffer) {
                lws_write(wsi, (unsigned char *)conn->send_buffer + LWS_PRE,
                         conn->send_len, LWS_WRITE_TEXT);
                free(conn->send_buffer);
                conn->send_buffer = NULL;
                conn->send_pending = 0;
            }
            break;

        case LWS_CALLBACK_CLOSED:
            if (conn) {
                conn->closed = 1;
            }
            break;

        default:
            break;
    }

    return 0;
}

// __lws_ws_connect(url: string): ptr
Value builtin_lws_ws_connect(Value *args, int num_args, ExecutionContext *ctx) {
    // SANDBOX: Check if network is allowed
    if (sandbox_is_restricted(ctx, HML_SANDBOX_RESTRICT_NETWORK)) {
        sandbox_error(ctx, "WebSocket connections");
        return val_null();
    }

    lws_init_logging();

    if (num_args != 1) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_ws_connect() expects 1 argument");
        return val_null();
    }

    if (args[0].type != VAL_STRING) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_ws_connect() expects string URL");
        return val_null();
    }

    const char *url = args[0].as.as_string->data;
    char host[256], path[512];
    int port, ssl = 0;

    // SECURITY: Use safe string initialization instead of strcpy
    path[0] = '/';
    path[1] = '\0';

    if (strncmp(url, "wss://", 6) == 0) {
        ssl = 1;
        port = 443;
        const char *rest = url + 6;
        const char *slash = strchr(rest, '/');
        const char *colon = strchr(rest, ':');

        if (colon && (!slash || colon < slash)) {
            size_t host_len = colon - rest;
            if (host_len >= 256) {
                ctx->exception_state.is_throwing = 1;
                ctx->exception_state.exception_value = val_string("Host name too long");
                return val_null();
            }
            strncpy(host, rest, host_len);
            host[host_len] = '\0';
            port = (int)strtol(colon + 1, NULL, 10);
            if (slash) {
                strncpy(path, slash, 511);
                path[511] = '\0';
            }
        } else if (slash) {
            size_t host_len = slash - rest;
            if (host_len >= 256) {
                ctx->exception_state.is_throwing = 1;
                ctx->exception_state.exception_value = val_string("Host name too long");
                return val_null();
            }
            strncpy(host, rest, host_len);
            host[host_len] = '\0';
            strncpy(path, slash, 511);
            path[511] = '\0';
        } else {
            strncpy(host, rest, 255);
            host[255] = '\0';
        }
    } else if (strncmp(url, "ws://", 5) == 0) {
        port = 80;
        const char *rest = url + 5;
        const char *slash = strchr(rest, '/');
        const char *colon = strchr(rest, ':');

        if (colon && (!slash || colon < slash)) {
            size_t host_len = colon - rest;
            if (host_len >= 256) {
                ctx->exception_state.is_throwing = 1;
                ctx->exception_state.exception_value = val_string("Host name too long");
                return val_null();
            }
            strncpy(host, rest, host_len);
            host[host_len] = '\0';
            port = (int)strtol(colon + 1, NULL, 10);
            if (slash) {
                strncpy(path, slash, 511);
                path[511] = '\0';
            }
        } else if (slash) {
            size_t host_len = slash - rest;
            if (host_len >= 256) {
                ctx->exception_state.is_throwing = 1;
                ctx->exception_state.exception_value = val_string("Host name too long");
                return val_null();
            }
            strncpy(host, rest, host_len);
            host[host_len] = '\0';
            strncpy(path, slash, 511);
            path[511] = '\0';
        } else {
            strncpy(host, rest, 255);
            host[255] = '\0';
        }
    } else {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Invalid WebSocket URL (must start with ws:// or wss://)");
        return val_null();
    }

    ws_connection_t *conn = calloc(1, sizeof(ws_connection_t));
    if (!conn) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to allocate connection");
        return val_null();
    }
    conn->owns_memory = 1;

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.port = CONTEXT_PORT_NO_LISTEN;

    static const struct lws_protocols ws_protocols[] = {
        { "ws", ws_callback, 0, 4096, 0, NULL, 0 },
        { NULL, NULL, 0, 0, 0, NULL, 0 }
    };
    info.protocols = ws_protocols;

    conn->context = lws_create_context(&info);
    if (!conn->context) {
        free(conn);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to create libwebsockets context");
        return val_null();
    }

    struct lws_client_connect_info connect_info;
    memset(&connect_info, 0, sizeof(connect_info));
    connect_info.context = conn->context;
    connect_info.address = host;
    connect_info.port = port;
    connect_info.path = path;
    connect_info.host = host;
    connect_info.origin = host;
    connect_info.protocol = ws_protocols[0].name;
    connect_info.userdata = conn;
    connect_info.pwsi = &conn->wsi;

    if (ssl) {
        // SECURITY: Enable SSL with proper certificate validation
        // Removed LCCSCF_ALLOW_SELFSIGNED and LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK
        // to prevent MITM attacks
        connect_info.ssl_connection = LCCSCF_USE_SSL;
    }

    if (!lws_client_connect_via_info(&connect_info)) {
        lws_context_destroy(conn->context);
        free(conn);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to connect");
        return val_null();
    }

    // Wait for connection (timeout 10 seconds)
    int timeout = 100;
    while (timeout-- > 0 && !conn->closed && !conn->failed && !conn->established) {
        lws_service(conn->context, 100);
    }

    if (conn->failed || conn->closed || !conn->established) {
        lws_context_destroy(conn->context);
        free(conn);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("WebSocket connection failed or timed out");
        return val_null();
    }

    // Start service thread
    conn->shutdown = 0;
    conn->has_own_thread = 1;
    if (pthread_create(&conn->service_thread, NULL, ws_service_thread, conn) != 0) {
        lws_context_destroy(conn->context);
        free(conn);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to create service thread");
        return val_null();
    }

    // Create WebSocketHandle wrapper
    WebSocketHandle *ws = calloc(1, sizeof(WebSocketHandle));
    if (!ws) {
        ws_connection_close(conn);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to allocate WebSocket handle");
        return val_null();
    }
    ws->handle = conn;
    ws->url = strdup(url);
    if (!ws->url) {
        ws_connection_close(conn);
        free(ws);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to allocate WebSocket URL");
        return val_null();
    }
    ws->host = NULL;
    ws->port = port;
    ws->closed = 0;
    ws->is_server = 0;
    ws->ref_count = 1;

    return val_websocket(ws);
}

// __lws_ws_send_text(conn: websocket, text: string): i32
Value builtin_lws_ws_send_text(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_ws_send_text() expects 2 arguments");
        return val_null();
    }

    if (args[1].type != VAL_STRING) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_ws_send_text() expects string as second argument");
        return val_null();
    }

    ws_connection_t *conn = NULL;
    if (args[0].type == VAL_WEBSOCKET) {
        WebSocketHandle *ws = args[0].as.as_websocket;
        if (!ws || ws->closed) return val_i32(-1);
        conn = (ws_connection_t *)ws->handle;
    } else if (args[0].type == VAL_PTR) {
        conn = (ws_connection_t *)args[0].as.as_ptr;
    } else {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_ws_send_text() expects websocket or ptr");
        return val_null();
    }

    if (!conn || conn->closed) {
        return val_i32(-1);
    }

    const char *text = args[1].as.as_string->data;
    size_t len = strlen(text);

    unsigned char *buf = malloc(LWS_PRE + len);
    if (!buf) {
        return val_i32(-1);
    }

    memcpy(buf + LWS_PRE, text, len);
    int written = lws_write(conn->wsi, buf + LWS_PRE, len, LWS_WRITE_TEXT);
    free(buf);

    if (written < 0) {
        return val_i32(-1);
    }

    lws_cancel_service(conn->context);
    return val_i32(0);
}

// __lws_ws_send_binary(conn: websocket, data: buffer): i32
// Sends binary data over a WebSocket connection
Value builtin_lws_ws_send_binary(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_ws_send_binary() expects 2 arguments");
        return val_null();
    }

    if (args[1].type != VAL_BUFFER) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_ws_send_binary() expects buffer as second argument");
        return val_null();
    }

    ws_connection_t *conn = NULL;
    if (args[0].type == VAL_WEBSOCKET) {
        WebSocketHandle *ws = args[0].as.as_websocket;
        if (!ws || ws->closed) return val_i32(-1);
        conn = (ws_connection_t *)ws->handle;
    } else if (args[0].type == VAL_PTR) {
        conn = (ws_connection_t *)args[0].as.as_ptr;
    } else {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_ws_send_binary() expects websocket or ptr");
        return val_null();
    }

    if (!conn || conn->closed) {
        return val_i32(-1);
    }

    Buffer *buffer = args[1].as.as_buffer;
    size_t len = buffer->length;

    unsigned char *buf = malloc(LWS_PRE + len);
    if (!buf) {
        return val_i32(-1);
    }

    memcpy(buf + LWS_PRE, buffer->data, len);
    int written = lws_write(conn->wsi, buf + LWS_PRE, len, LWS_WRITE_BINARY);
    free(buf);

    if (written < 0) {
        return val_i32(-1);
    }

    lws_cancel_service(conn->context);
    return val_i32(0);
}

// __lws_ws_recv(conn: websocket, timeout_ms: i32): ptr
Value builtin_lws_ws_recv(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_ws_recv() expects 2 arguments");
        return val_null();
    }

    ws_connection_t *conn = NULL;
    if (args[0].type == VAL_WEBSOCKET) {
        WebSocketHandle *ws = args[0].as.as_websocket;
        if (!ws || ws->closed) return val_null();
        conn = (ws_connection_t *)ws->handle;
    } else if (args[0].type == VAL_PTR) {
        conn = (ws_connection_t *)args[0].as.as_ptr;
    } else {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_ws_recv() expects websocket or ptr as first argument");
        return val_null();
    }
    if (!conn || conn->closed) {
        return val_null();
    }

    int timeout_ms = value_to_int(args[1]);
    int iterations = timeout_ms > 0 ? (timeout_ms / 10) : -1;

    while (iterations != 0) {
        if (conn->msg_queue_head) {
            ws_message_t *msg = conn->msg_queue_head;
            conn->msg_queue_head = msg->next;
            if (!conn->msg_queue_head) {
                conn->msg_queue_tail = NULL;
            }
            msg->next = NULL;
            return val_ptr(msg);
        }

        usleep(10000);  // 10ms sleep
        if (conn->closed) return val_null();
        if (iterations > 0) iterations--;
    }

    return val_null();
}

// __lws_msg_type(msg: ptr): i32
Value builtin_lws_msg_type(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_msg_type() expects 1 argument");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        return val_i32(0);
    }

    ws_message_t *msg = (ws_message_t *)args[0].as.as_ptr;
    if (!msg) {
        return val_i32(0);
    }

    return val_i32(msg->is_binary ? 2 : 1);
}

// __lws_msg_text(msg: ptr): string
Value builtin_lws_msg_text(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_msg_text() expects 1 argument");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        return val_string("");
    }

    ws_message_t *msg = (ws_message_t *)args[0].as.as_ptr;
    if (!msg || !msg->data) {
        return val_string("");
    }

    return val_string((const char *)msg->data);
}

// __lws_msg_len(msg: ptr): i32
Value builtin_lws_msg_len(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_msg_len() expects 1 argument");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        return val_i32(0);
    }

    ws_message_t *msg = (ws_message_t *)args[0].as.as_ptr;
    if (!msg) {
        return val_i32(0);
    }

    return val_i32((int32_t)msg->len);
}

// __lws_msg_binary(msg: ptr): buffer
// Returns the binary data from a WebSocket message as a buffer
Value builtin_lws_msg_binary(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_msg_binary() expects 1 argument");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        return val_buffer(1);  // Return empty 1-byte buffer for safety
    }

    ws_message_t *msg = (ws_message_t *)args[0].as.as_ptr;
    if (!msg || !msg->data || msg->len == 0) {
        return val_buffer(1);  // Return empty 1-byte buffer for safety
    }

    Value buf_val = val_buffer((int)msg->len);
    if (buf_val.type == VAL_NULL) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_msg_binary() failed to allocate buffer");
        return val_null();
    }
    memcpy(buf_val.as.as_buffer->data, msg->data, msg->len);
    return buf_val;
}

// __lws_msg_free(msg: ptr): null
Value builtin_lws_msg_free(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_msg_free() expects 1 argument");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        return val_null();
    }

    ws_message_t *msg = (ws_message_t *)args[0].as.as_ptr;
    if (msg) {
        if (msg->data) free(msg->data);
        free(msg);
    }

    return val_null();
}

// __lws_ws_close(conn: websocket): null
Value builtin_lws_ws_close(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_ws_close() expects 1 argument");
        return val_null();
    }

    if (args[0].type == VAL_WEBSOCKET) {
        WebSocketHandle *ws = args[0].as.as_websocket;
        if (ws && !ws->closed && ws->handle) {
            ws_connection_close((ws_connection_t *)ws->handle);
            ws->closed = 1;
        }
    } else if (args[0].type == VAL_PTR) {
        ws_connection_t *conn = (ws_connection_t *)args[0].as.as_ptr;
        if (conn) {
            ws_connection_close(conn);
        }
    }

    return val_null();
}

// __lws_ws_is_closed(conn: websocket): i32
Value builtin_lws_ws_is_closed(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_ws_is_closed() expects 1 argument");
        return val_null();
    }

    if (args[0].type == VAL_WEBSOCKET) {
        WebSocketHandle *ws = args[0].as.as_websocket;
        if (!ws) return val_i32(1);
        if (ws->closed) return val_i32(1);
        ws_connection_t *conn = (ws_connection_t *)ws->handle;
        return val_i32(conn ? conn->closed : 1);
    } else if (args[0].type == VAL_PTR) {
        ws_connection_t *conn = (ws_connection_t *)args[0].as.as_ptr;
        return val_i32(conn ? conn->closed : 1);
    }

    return val_i32(1);
}

// __lws_ws_server_create(host: string, port: i32): ptr
Value builtin_lws_ws_server_create(Value *args, int num_args, ExecutionContext *ctx) {
    // SANDBOX: Check if network is allowed
    if (sandbox_is_restricted(ctx, HML_SANDBOX_RESTRICT_NETWORK)) {
        sandbox_error(ctx, "WebSocket server creation");
        return val_null();
    }

    lws_init_logging();

    if (num_args != 2) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_ws_server_create() expects 2 arguments");
        return val_null();
    }

    if (args[0].type != VAL_STRING) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_ws_server_create() expects string host");
        return val_null();
    }

    const char *host = args[0].as.as_string->data;
    int port = value_to_int(args[1]);

    ws_server_t *server = calloc(1, sizeof(ws_server_t));
    if (!server) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to allocate server");
        return val_null();
    }

    server->port = port;
    pthread_mutex_init(&server->pending_mutex, NULL);

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = port;
    info.iface = host;
    info.user = server;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT | LWS_SERVER_OPTION_ALLOW_LISTEN_SHARE;

    static const struct lws_protocols server_protocols[] = {
        { "ws", ws_server_callback, sizeof(ws_connection_t), 4096, 0, NULL, 0 },
        { NULL, NULL, 0, 0, 0, NULL, 0 }
    };
    info.protocols = server_protocols;

    server->context = lws_create_context(&info);
    if (!server->context) {
        pthread_mutex_destroy(&server->pending_mutex);
        free(server);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to create server context");
        return val_null();
    }

    server->shutdown = 0;
    if (pthread_create(&server->service_thread, NULL, ws_server_service_thread, server) != 0) {
        lws_context_destroy(server->context);
        pthread_mutex_destroy(&server->pending_mutex);
        free(server);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to create service thread");
        return val_null();
    }

    // Create WebSocketHandle wrapper for server
    WebSocketHandle *ws = calloc(1, sizeof(WebSocketHandle));
    if (!ws) {
        ws_server_close_internal(server);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to allocate WebSocket server handle");
        return val_null();
    }
    ws->handle = server;
    ws->url = NULL;
    ws->host = strdup(host);
    if (!ws->host) {
        ws_server_close_internal(server);
        free(ws);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to allocate WebSocket host string");
        return val_null();
    }
    ws->port = port;
    ws->closed = 0;
    ws->is_server = 1;
    ws->ref_count = 1;

    return val_websocket(ws);
}

// __lws_ws_server_accept(server: websocket, timeout_ms: i32): websocket
Value builtin_lws_ws_server_accept(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_ws_server_accept() expects 2 arguments");
        return val_null();
    }

    ws_server_t *server = NULL;
    WebSocketHandle *server_ws = NULL;

    if (args[0].type == VAL_WEBSOCKET) {
        server_ws = args[0].as.as_websocket;
        if (!server_ws || server_ws->closed || !server_ws->is_server) {
            return val_null();
        }
        server = (ws_server_t *)server_ws->handle;
    } else if (args[0].type == VAL_PTR) {
        server = (ws_server_t *)args[0].as.as_ptr;
    } else {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_ws_server_accept() expects websocket server");
        return val_null();
    }

    if (!server || server->closed) {
        return val_null();
    }

    int timeout_ms = value_to_int(args[1]);
    int iterations = timeout_ms > 0 ? (timeout_ms / 10) : -1;

    while (iterations != 0) {
        // Check if server was closed before accessing its mutex
        if (server->closed) {
            return val_null();
        }

        pthread_mutex_lock(&server->pending_mutex);
        ws_connection_t *conn = NULL;
        if (server->pending_wsi) {
            conn = server->pending_conn;
            server->pending_wsi = NULL;
            server->pending_conn = NULL;
        }
        pthread_mutex_unlock(&server->pending_mutex);

        if (conn) {
            // Create WebSocketHandle wrapper for accepted connection
            WebSocketHandle *ws = calloc(1, sizeof(WebSocketHandle));
            if (!ws) {
                ws_connection_close(conn);
                return val_null();
            }
            ws->handle = conn;
            ws->url = NULL;
            ws->host = NULL;
            if (server_ws && server_ws->host) {
                ws->host = strdup(server_ws->host);
                if (!ws->host) {
                    ws_connection_close(conn);
                    free(ws);
                    return val_null();
                }
            }
            ws->port = server ? server->port : 0;
            ws->closed = 0;
            ws->is_server = 0;  // This is a client connection accepted by server
            ws->ref_count = 1;

            return val_websocket(ws);
        }

        // Check again after sleep in case server was closed
        if (server->closed) {
            return val_null();
        }
        usleep(10000);  // 10ms sleep
        if (iterations > 0) iterations--;
    }

    return val_null();
}

// __lws_ws_server_close(server: websocket): null
Value builtin_lws_ws_server_close(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_ws_server_close() expects 1 argument");
        return val_null();
    }

    if (args[0].type == VAL_WEBSOCKET) {
        WebSocketHandle *ws = args[0].as.as_websocket;
        if (ws && !ws->closed && ws->handle && ws->is_server) {
            ws_server_close_internal((ws_server_t *)ws->handle);
            ws->closed = 1;
            ws->handle = NULL;  // Server memory is freed by ws_server_close_internal
        }
    } else if (args[0].type == VAL_PTR) {
        ws_server_t *server = (ws_server_t *)args[0].as.as_ptr;
        if (server) {
            ws_server_close_internal(server);
        }
    }

    return val_null();
}

#else  // !HAVE_LIBWEBSOCKETS

// ========== STUB IMPLEMENTATIONS (libwebsockets not available) ==========

// Stub websocket_free when libwebsockets is not available
void websocket_free(WebSocketHandle *ws) {
    if (!ws) return;
    if (ws->url) free(ws->url);
    if (ws->host) free(ws->host);
    free(ws);
}

// Stub get_websocket_property
Value get_websocket_property(WebSocketHandle *ws, const char *property, ExecutionContext *ctx) {
    (void)ws; (void)property; (void)ctx;
    return val_null();
}

Value builtin_lws_http_get(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args;
    runtime_error(ctx, "HTTP support not available (libwebsockets not installed)");
    return val_null();
}

Value builtin_lws_http_post(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args;
    runtime_error(ctx, "HTTP support not available (libwebsockets not installed)");
    return val_null();
}

Value builtin_lws_http_request(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args;
    runtime_error(ctx, "HTTP support not available (libwebsockets not installed)");
    return val_null();
}

Value builtin_lws_http_get_timeout(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args;
    runtime_error(ctx, "HTTP support not available (libwebsockets not installed)");
    return val_null();
}

Value builtin_lws_http_post_timeout(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args;
    runtime_error(ctx, "HTTP support not available (libwebsockets not installed)");
    return val_null();
}

Value builtin_lws_http_request_timeout(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args;
    runtime_error(ctx, "HTTP support not available (libwebsockets not installed)");
    return val_null();
}

Value builtin_lws_response_status(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args;
    runtime_error(ctx, "HTTP support not available (libwebsockets not installed)");
    return val_null();
}

Value builtin_lws_response_body(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args;
    runtime_error(ctx, "HTTP support not available (libwebsockets not installed)");
    return val_null();
}

Value builtin_lws_response_body_binary(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args;
    runtime_error(ctx, "HTTP support not available (libwebsockets not installed)");
    return val_null();
}

Value builtin_lws_response_headers(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args;
    runtime_error(ctx, "HTTP support not available (libwebsockets not installed)");
    return val_null();
}

Value builtin_lws_response_redirect(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args;
    runtime_error(ctx, "HTTP support not available (libwebsockets not installed)");
    return val_null();
}

Value builtin_lws_response_free(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args; (void)ctx;
    return val_null();
}

Value builtin_lws_ws_connect(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args;
    runtime_error(ctx, "WebSocket support not available (libwebsockets not installed)");
    return val_null();
}

Value builtin_lws_ws_send_text(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args;
    runtime_error(ctx, "WebSocket support not available (libwebsockets not installed)");
    return val_null();
}

Value builtin_lws_ws_send_binary(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args;
    runtime_error(ctx, "WebSocket support not available (libwebsockets not installed)");
    return val_null();
}

Value builtin_lws_ws_recv(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args;
    runtime_error(ctx, "WebSocket support not available (libwebsockets not installed)");
    return val_null();
}

Value builtin_lws_msg_type(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args;
    runtime_error(ctx, "WebSocket support not available (libwebsockets not installed)");
    return val_null();
}

Value builtin_lws_msg_text(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args;
    runtime_error(ctx, "WebSocket support not available (libwebsockets not installed)");
    return val_null();
}

Value builtin_lws_msg_len(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args;
    runtime_error(ctx, "WebSocket support not available (libwebsockets not installed)");
    return val_null();
}

Value builtin_lws_msg_binary(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args;
    runtime_error(ctx, "WebSocket support not available (libwebsockets not installed)");
    return val_null();
}

Value builtin_lws_msg_free(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args; (void)ctx;
    return val_null();
}

Value builtin_lws_ws_close(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args; (void)ctx;
    return val_null();
}

Value builtin_lws_ws_is_closed(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args; (void)ctx;
    return val_i32(1);
}

Value builtin_lws_ws_server_create(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args;
    runtime_error(ctx, "WebSocket server not available (libwebsockets not installed)");
    return val_null();
}

Value builtin_lws_ws_server_accept(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args;
    runtime_error(ctx, "WebSocket server not available (libwebsockets not installed)");
    return val_null();
}

Value builtin_lws_ws_server_close(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args; (void)ctx;
    return val_null();
}

// Streaming HTTP stubs
Value builtin_lws_http_stream_start(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args;
    runtime_error(ctx, "Streaming HTTP not available (libwebsockets not installed)");
    return val_null();
}

Value builtin_lws_http_stream_read(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args;
    runtime_error(ctx, "Streaming HTTP not available (libwebsockets not installed)");
    return val_null();
}

Value builtin_lws_http_stream_status(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args; (void)ctx;
    return val_i32(0);
}

Value builtin_lws_http_stream_headers(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args; (void)ctx;
    return val_string("");
}

Value builtin_lws_http_stream_close(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args; (void)ctx;
    return val_null();
}

#endif  // HAVE_LIBWEBSOCKETS
