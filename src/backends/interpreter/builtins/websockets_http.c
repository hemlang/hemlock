// HTTP builtins for Hemlock (extracted from websockets.c)
// Provides HTTP client functionality using libwebsockets

#include "websockets_internal.h"
#include <stdatomic.h>

#ifndef HAVE_LIBWEBSOCKETS
#  define HAVE_LIBWEBSOCKETS 0
#endif

#if HAVE_LIBWEBSOCKETS

#include <libwebsockets.h>
#include <pthread.h>

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
int parse_url(const char *url, char *host, int *port, char *path, int *ssl) {
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

#else  // !HAVE_LIBWEBSOCKETS

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

#endif  // HAVE_LIBWEBSOCKETS
