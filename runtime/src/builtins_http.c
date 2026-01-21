/*
 * Hemlock Runtime Library - HTTP/WebSocket Builtins
 *
 * This file implements:
 * - HTTP client support (http_get, http_post, http_request)
 * - WebSocket support
 *
 * Conditional compilation: requires HML_HAVE_LIBWEBSOCKETS
 */

#include "builtins_internal.h"

// ========== HTTP/WEBSOCKET SUPPORT ==========
// Requires libwebsockets

#ifdef HML_HAVE_LIBWEBSOCKETS

#include <libwebsockets.h>

// HTTP response structure
typedef struct {
    char *body;
    size_t body_len;
    size_t body_capacity;
    int status_code;
    int complete;
    int failed;
    char *redirect_url;
    char *headers;
} hml_http_response_t;

static void hml_lws_response_destroy(hml_http_response_t *resp) {
    if (!resp) {
        return;
    }
    if (resp->body) {
        free(resp->body);
    }
    if (resp->redirect_url) {
        free(resp->redirect_url);
    }
    if (resp->headers) {
        free(resp->headers);
    }
    free(resp);
}

// HTTP callback
static int hml_http_callback(struct lws *wsi, enum lws_callback_reasons reason,
                             void *user, void *in, size_t len) {
    hml_http_response_t *resp = (hml_http_response_t *)user;

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
                    char headers_buf[8192];
                    size_t headers_len = 0;
                    char value[1024];
                    int vlen;

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
                    }
                }

                // Capture Location header for redirects (3xx responses)
                if (resp->status_code >= 300 && resp->status_code < 400) {
                    char location[1024];
                    int loc_len = lws_hdr_copy(wsi, location, sizeof(location), WSI_TOKEN_HTTP_LOCATION);
                    if (loc_len > 0) {
                        location[loc_len] = '\0';
                        resp->redirect_url = strdup(location);
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
static int hml_parse_url(const char *url, char *host, int *port, char *path, int *ssl) {
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
            *port = atoi(colon + 1);
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
            *port = atoi(colon + 1);
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

// HTTP GET
HmlValue hml_lws_http_get(HmlValue url_val) {
    if (url_val.type != HML_VAL_STRING || !url_val.as.as_string) {
        hml_runtime_error("__lws_http_get() expects string URL");
    }

    const char *url = url_val.as.as_string->data;
    char host[256], path[512];
    int port, ssl;

    if (hml_parse_url(url, host, &port, path, &ssl) < 0) {
        hml_runtime_error("Invalid URL format");
    }

    hml_http_response_t *resp = calloc(1, sizeof(hml_http_response_t));
    if (!resp) {
        hml_runtime_error("Failed to allocate response");
    }

    resp->body_capacity = 4096;
    resp->body = malloc(resp->body_capacity);
    if (!resp->body) {
        free(resp);
        hml_runtime_error("Failed to allocate body buffer");
    }
    resp->body[0] = '\0';

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.max_http_header_data = 16384;  // 16KB for large headers (e.g., GitHub API)

    static const struct lws_protocols protocols[] = {
        { "http", hml_http_callback, 0, 16384, 0, NULL, 0 },
        { NULL, NULL, 0, 0, 0, NULL, 0 }
    };
    info.protocols = protocols;

    struct lws_context *context = lws_create_context(&info);
    if (!context) {
        hml_lws_response_destroy(resp);
        hml_runtime_error("Failed to create libwebsockets context");
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
    connect_info.protocol = protocols[0].name;
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
        lws_context_destroy(context);
        hml_lws_response_destroy(resp);
        hml_runtime_error("Failed to connect");
    }

    int timeout = 3000;
    while (!resp->complete && !resp->failed && timeout-- > 0) {
        lws_service(context, 10);
    }

    lws_context_destroy(context);

    if (resp->failed || timeout <= 0) {
        hml_lws_response_destroy(resp);
        hml_runtime_error("HTTP request failed or timed out");
    }

    return hml_val_ptr(resp);
}

// HTTP POST
HmlValue hml_lws_http_post(HmlValue url_val, HmlValue body_val, HmlValue content_type_val) {
    if (url_val.type != HML_VAL_STRING || body_val.type != HML_VAL_STRING || content_type_val.type != HML_VAL_STRING) {
        hml_runtime_error("__lws_http_post() expects string arguments");
    }

    const char *url = url_val.as.as_string->data;
    (void)body_val;  // Not fully implemented yet
    (void)content_type_val;
    
    char host[256], path[512];
    int port, ssl;

    if (hml_parse_url(url, host, &port, path, &ssl) < 0) {
        hml_runtime_error("Invalid URL format");
    }

    hml_http_response_t *resp = calloc(1, sizeof(hml_http_response_t));
    if (!resp) {
        hml_runtime_error("Failed to allocate response");
    }

    resp->body_capacity = 4096;
    resp->body = malloc(resp->body_capacity);
    if (!resp->body) {
        free(resp);
        hml_runtime_error("Failed to allocate body buffer");
    }
    resp->body[0] = '\0';

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.max_http_header_data = 16384;

    static const struct lws_protocols post_protocols[] = {
        { "http", hml_http_callback, 0, 16384, 0, NULL, 0 },
        { NULL, NULL, 0, 0, 0, NULL, 0 }
    };
    info.protocols = post_protocols;

    struct lws_context *context = lws_create_context(&info);
    if (!context) {
        hml_lws_response_destroy(resp);
        hml_runtime_error("Failed to create libwebsockets context");
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
    connect_info.protocol = post_protocols[0].name;
    connect_info.userdata = resp;

    struct lws *wsi;
    connect_info.pwsi = &wsi;

    connect_info.ssl_connection = LCCSCF_HTTP_NO_FOLLOW_REDIRECT;

    if (ssl) {
        // SECURITY: Enable SSL with proper certificate validation
        // Removed LCCSCF_ALLOW_SELFSIGNED and LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK
        // to prevent MITM attacks
        connect_info.ssl_connection |= LCCSCF_USE_SSL;
    }

    if (!lws_client_connect_via_info(&connect_info)) {
        lws_context_destroy(context);
        hml_lws_response_destroy(resp);
        hml_runtime_error("Failed to connect");
    }

    int timeout = 3000;
    while (!resp->complete && !resp->failed && timeout-- > 0) {
        lws_service(context, 10);
    }

    lws_context_destroy(context);

    if (resp->failed || timeout <= 0) {
        hml_lws_response_destroy(resp);
        hml_runtime_error("HTTP request failed or timed out");
    }

    return hml_val_ptr(resp);
}

// Generic HTTP request with configurable method
HmlValue hml_lws_http_request(HmlValue method_val, HmlValue url_val, HmlValue body_val, HmlValue content_type_val) {
    if (method_val.type != HML_VAL_STRING || url_val.type != HML_VAL_STRING ||
        body_val.type != HML_VAL_STRING || content_type_val.type != HML_VAL_STRING) {
        hml_runtime_error("__lws_http_request() expects string arguments");
    }

    const char *method = method_val.as.as_string->data;
    const char *url = url_val.as.as_string->data;
    (void)body_val;  // Not fully implemented yet
    (void)content_type_val;

    char host[256], path[512];
    int port, ssl;

    if (hml_parse_url(url, host, &port, path, &ssl) < 0) {
        hml_runtime_error("Invalid URL format");
    }

    hml_http_response_t *resp = calloc(1, sizeof(hml_http_response_t));
    if (!resp) {
        hml_runtime_error("Failed to allocate response");
    }

    resp->body_capacity = 4096;
    resp->body = malloc(resp->body_capacity);
    if (!resp->body) {
        free(resp);
        hml_runtime_error("Failed to allocate body buffer");
    }
    resp->body[0] = '\0';

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.max_http_header_data = 16384;

    static const struct lws_protocols req_protocols[] = {
        { "http", hml_http_callback, 0, 16384, 0, NULL, 0 },
        { NULL, NULL, 0, 0, 0, NULL, 0 }
    };
    info.protocols = req_protocols;

    struct lws_context *context = lws_create_context(&info);
    if (!context) {
        hml_lws_response_destroy(resp);
        hml_runtime_error("Failed to create libwebsockets context");
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
    connect_info.protocol = req_protocols[0].name;
    connect_info.userdata = resp;

    struct lws *wsi;
    connect_info.pwsi = &wsi;

    connect_info.ssl_connection = LCCSCF_HTTP_NO_FOLLOW_REDIRECT;

    if (ssl) {
        connect_info.ssl_connection |= LCCSCF_USE_SSL;
    }

    if (!lws_client_connect_via_info(&connect_info)) {
        lws_context_destroy(context);
        hml_lws_response_destroy(resp);
        hml_runtime_error("Failed to connect");
    }

    int timeout = 3000;
    while (!resp->complete && !resp->failed && timeout-- > 0) {
        lws_service(context, 10);
    }

    lws_context_destroy(context);

    if (resp->failed || timeout <= 0) {
        hml_lws_response_destroy(resp);
        hml_runtime_error("HTTP request failed or timed out");
    }

    return hml_val_ptr(resp);
}

// ========== TIMEOUT VERSIONS ==========

// Helper function to extract timeout_ms from HmlValue
static int hml_extract_timeout_ms(HmlValue timeout_val) {
    int timeout_ms;
    if (timeout_val.type == HML_VAL_I32) {
        timeout_ms = timeout_val.as.as_i32;
    } else if (timeout_val.type == HML_VAL_I64) {
        timeout_ms = (int)timeout_val.as.as_i64;
    } else if (timeout_val.type == HML_VAL_F64) {
        timeout_ms = (int)timeout_val.as.as_f64;
    } else {
        timeout_ms = 30000;  // Default 30 seconds
    }
    // Convert to iterations (each ~10ms)
    int iterations = timeout_ms / 10;
    return iterations < 1 ? 1 : iterations;
}

// HTTP GET with configurable timeout
HmlValue hml_lws_http_get_timeout(HmlValue url_val, HmlValue timeout_val) {
    if (url_val.type != HML_VAL_STRING) {
        hml_runtime_error("__lws_http_get_timeout() expects string URL");
    }

    int timeout_iterations = hml_extract_timeout_ms(timeout_val);

    const char *url = url_val.as.as_string->data;
    char host[256], path[512];
    int port, ssl;

    if (hml_parse_url(url, host, &port, path, &ssl) < 0) {
        hml_runtime_error("Invalid URL format");
    }

    hml_http_response_t *resp = calloc(1, sizeof(hml_http_response_t));
    if (!resp) {
        hml_runtime_error("Failed to allocate response");
    }

    resp->body_capacity = 4096;
    resp->body = malloc(resp->body_capacity);
    if (!resp->body) {
        free(resp);
        hml_runtime_error("Failed to allocate body buffer");
    }
    resp->body[0] = '\0';

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.max_http_header_data = 16384;

    static const struct lws_protocols protocols[] = {
        { "http", hml_http_callback, 0, 16384, 0, NULL, 0 },
        { NULL, NULL, 0, 0, 0, NULL, 0 }
    };
    info.protocols = protocols;

    struct lws_context *context = lws_create_context(&info);
    if (!context) {
        hml_lws_response_destroy(resp);
        hml_runtime_error("Failed to create libwebsockets context");
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
    connect_info.protocol = protocols[0].name;
    connect_info.userdata = resp;

    struct lws *wsi;
    connect_info.pwsi = &wsi;

    connect_info.ssl_connection = LCCSCF_HTTP_NO_FOLLOW_REDIRECT;

    if (ssl) {
        connect_info.ssl_connection |= LCCSCF_USE_SSL;
    }

    if (!lws_client_connect_via_info(&connect_info)) {
        lws_context_destroy(context);
        hml_lws_response_destroy(resp);
        hml_runtime_error("Failed to connect");
    }

    int timeout = timeout_iterations;
    while (!resp->complete && !resp->failed && timeout-- > 0) {
        lws_service(context, 10);
    }

    lws_context_destroy(context);

    if (resp->failed || timeout <= 0) {
        hml_lws_response_destroy(resp);
        hml_runtime_error("HTTP request failed or timed out");
    }

    return hml_val_ptr(resp);
}

// HTTP POST with configurable timeout
HmlValue hml_lws_http_post_timeout(HmlValue url_val, HmlValue body_val, HmlValue content_type_val, HmlValue timeout_val) {
    if (url_val.type != HML_VAL_STRING || body_val.type != HML_VAL_STRING || content_type_val.type != HML_VAL_STRING) {
        hml_runtime_error("__lws_http_post_timeout() expects string arguments");
    }

    int timeout_iterations = hml_extract_timeout_ms(timeout_val);

    const char *url = url_val.as.as_string->data;
    (void)body_val;
    (void)content_type_val;

    char host[256], path[512];
    int port, ssl;

    if (hml_parse_url(url, host, &port, path, &ssl) < 0) {
        hml_runtime_error("Invalid URL format");
    }

    hml_http_response_t *resp = calloc(1, sizeof(hml_http_response_t));
    if (!resp) {
        hml_runtime_error("Failed to allocate response");
    }

    resp->body_capacity = 4096;
    resp->body = malloc(resp->body_capacity);
    if (!resp->body) {
        free(resp);
        hml_runtime_error("Failed to allocate body buffer");
    }
    resp->body[0] = '\0';

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.max_http_header_data = 16384;

    static const struct lws_protocols protocols[] = {
        { "http", hml_http_callback, 0, 16384, 0, NULL, 0 },
        { NULL, NULL, 0, 0, 0, NULL, 0 }
    };
    info.protocols = protocols;

    struct lws_context *context = lws_create_context(&info);
    if (!context) {
        hml_lws_response_destroy(resp);
        hml_runtime_error("Failed to create libwebsockets context");
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
    connect_info.protocol = protocols[0].name;
    connect_info.userdata = resp;

    struct lws *wsi;
    connect_info.pwsi = &wsi;

    connect_info.ssl_connection = LCCSCF_HTTP_NO_FOLLOW_REDIRECT;

    if (ssl) {
        connect_info.ssl_connection |= LCCSCF_USE_SSL;
    }

    if (!lws_client_connect_via_info(&connect_info)) {
        lws_context_destroy(context);
        hml_lws_response_destroy(resp);
        hml_runtime_error("Failed to connect");
    }

    int timeout = timeout_iterations;
    while (!resp->complete && !resp->failed && timeout-- > 0) {
        lws_service(context, 10);
    }

    lws_context_destroy(context);

    if (resp->failed || timeout <= 0) {
        hml_lws_response_destroy(resp);
        hml_runtime_error("HTTP request failed or timed out");
    }

    return hml_val_ptr(resp);
}

// Generic HTTP request with configurable timeout
HmlValue hml_lws_http_request_timeout(HmlValue method_val, HmlValue url_val, HmlValue body_val, HmlValue content_type_val, HmlValue timeout_val) {
    if (method_val.type != HML_VAL_STRING || url_val.type != HML_VAL_STRING ||
        body_val.type != HML_VAL_STRING || content_type_val.type != HML_VAL_STRING) {
        hml_runtime_error("__lws_http_request_timeout() expects string arguments");
    }

    int timeout_iterations = hml_extract_timeout_ms(timeout_val);

    const char *method = method_val.as.as_string->data;
    const char *url = url_val.as.as_string->data;
    (void)body_val;
    (void)content_type_val;

    char host[256], path[512];
    int port, ssl;

    if (hml_parse_url(url, host, &port, path, &ssl) < 0) {
        hml_runtime_error("Invalid URL format");
    }

    hml_http_response_t *resp = calloc(1, sizeof(hml_http_response_t));
    if (!resp) {
        hml_runtime_error("Failed to allocate response");
    }

    resp->body_capacity = 4096;
    resp->body = malloc(resp->body_capacity);
    if (!resp->body) {
        free(resp);
        hml_runtime_error("Failed to allocate body buffer");
    }
    resp->body[0] = '\0';

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.max_http_header_data = 16384;

    static const struct lws_protocols protocols[] = {
        { "http", hml_http_callback, 0, 16384, 0, NULL, 0 },
        { NULL, NULL, 0, 0, 0, NULL, 0 }
    };
    info.protocols = protocols;

    struct lws_context *context = lws_create_context(&info);
    if (!context) {
        hml_lws_response_destroy(resp);
        hml_runtime_error("Failed to create libwebsockets context");
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
    connect_info.protocol = protocols[0].name;
    connect_info.userdata = resp;

    struct lws *wsi;
    connect_info.pwsi = &wsi;

    connect_info.ssl_connection = LCCSCF_HTTP_NO_FOLLOW_REDIRECT;

    if (ssl) {
        connect_info.ssl_connection |= LCCSCF_USE_SSL;
    }

    if (!lws_client_connect_via_info(&connect_info)) {
        lws_context_destroy(context);
        hml_lws_response_destroy(resp);
        hml_runtime_error("Failed to connect");
    }

    int timeout = timeout_iterations;
    while (!resp->complete && !resp->failed && timeout-- > 0) {
        lws_service(context, 10);
    }

    lws_context_destroy(context);

    if (resp->failed || timeout <= 0) {
        hml_lws_response_destroy(resp);
        hml_runtime_error("HTTP request failed or timed out");
    }

    return hml_val_ptr(resp);
}

// Get response status code
HmlValue hml_lws_response_status(HmlValue resp_val) {
    if (resp_val.type != HML_VAL_PTR) {
        return hml_val_i32(0);
    }
    hml_http_response_t *resp = (hml_http_response_t *)resp_val.as.as_ptr;
    return hml_val_i32(resp ? resp->status_code : 0);
}

// Get response body
HmlValue hml_lws_response_body(HmlValue resp_val) {
    if (resp_val.type != HML_VAL_PTR) {
        return hml_val_string("");
    }
    hml_http_response_t *resp = (hml_http_response_t *)resp_val.as.as_ptr;
    if (!resp || !resp->body) {
        return hml_val_string("");
    }
    return hml_val_string(resp->body);
}

// Get response headers
HmlValue hml_lws_response_headers(HmlValue resp_val) {
    if (resp_val.type != HML_VAL_PTR) {
        return hml_val_string("");
    }
    hml_http_response_t *resp = (hml_http_response_t *)resp_val.as.as_ptr;
    if (!resp || !resp->headers) {
        return hml_val_string("");
    }
    return hml_val_string(resp->headers);
}

// Free response
HmlValue hml_lws_response_free(HmlValue resp_val) {
    if (resp_val.type == HML_VAL_PTR) {
        hml_http_response_t *resp = (hml_http_response_t *)resp_val.as.as_ptr;
        hml_lws_response_destroy(resp);
    }
    return hml_val_null();
}

// Get redirect URL from response (if any)
HmlValue hml_lws_response_redirect(HmlValue resp_val) {
    if (resp_val.type != HML_VAL_PTR) {
        return hml_val_null();
    }
    hml_http_response_t *resp = (hml_http_response_t *)resp_val.as.as_ptr;
    if (!resp || !resp->redirect_url) {
        return hml_val_null();
    }
    return hml_val_string(resp->redirect_url);
}

// Get response body as binary buffer (preserves null bytes)
HmlValue hml_lws_response_body_binary(HmlValue resp_val) {
    if (resp_val.type != HML_VAL_PTR) {
        return hml_val_buffer(0);
    }
    hml_http_response_t *resp = (hml_http_response_t *)resp_val.as.as_ptr;
    if (!resp || !resp->body || resp->body_len == 0) {
        return hml_val_buffer(0);
    }
    // Create buffer and copy data (preserves binary data including null bytes)
    HmlValue buf = hml_val_buffer(resp->body_len);
    if (buf.type == HML_VAL_BUFFER && buf.as.as_buffer) {
        memcpy(buf.as.as_buffer->data, resp->body, resp->body_len);
    }
    return buf;
}

// Builtin wrappers
HmlValue hml_builtin_lws_http_get(HmlClosureEnv *env, HmlValue url) {
    (void)env;
    return hml_lws_http_get(url);
}

HmlValue hml_builtin_lws_http_post(HmlClosureEnv *env, HmlValue url, HmlValue body, HmlValue content_type) {
    (void)env;
    return hml_lws_http_post(url, body, content_type);
}

HmlValue hml_builtin_lws_http_request(HmlClosureEnv *env, HmlValue method, HmlValue url, HmlValue body, HmlValue content_type) {
    (void)env;
    return hml_lws_http_request(method, url, body, content_type);
}

HmlValue hml_builtin_lws_response_status(HmlClosureEnv *env, HmlValue resp) {
    (void)env;
    return hml_lws_response_status(resp);
}

HmlValue hml_builtin_lws_response_body(HmlClosureEnv *env, HmlValue resp) {
    (void)env;
    return hml_lws_response_body(resp);
}

HmlValue hml_builtin_lws_response_headers(HmlClosureEnv *env, HmlValue resp) {
    (void)env;
    return hml_lws_response_headers(resp);
}

HmlValue hml_builtin_lws_response_free(HmlClosureEnv *env, HmlValue resp) {
    (void)env;
    return hml_lws_response_free(resp);
}

HmlValue hml_builtin_lws_response_redirect(HmlClosureEnv *env, HmlValue resp) {
    (void)env;
    return hml_lws_response_redirect(resp);
}

HmlValue hml_builtin_lws_response_body_binary(HmlClosureEnv *env, HmlValue resp) {
    (void)env;
    return hml_lws_response_body_binary(resp);
}

// ========== WEBSOCKET SUPPORT ==========

// WebSocket message structure
typedef struct hml_ws_message {
    unsigned char *data;
    size_t len;
    int is_binary;
    struct hml_ws_message *next;
} hml_ws_message_t;

// WebSocket connection structure
typedef struct {
    struct lws_context *context;
    struct lws *wsi;
    hml_ws_message_t *msg_queue_head;
    hml_ws_message_t *msg_queue_tail;
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
} hml_ws_connection_t;

// WebSocket server structure
typedef struct {
    struct lws_context *context;
    struct lws *pending_wsi;
    hml_ws_connection_t *pending_conn;
    int port;
    int closed;
    pthread_t service_thread;
    volatile int shutdown;
    pthread_mutex_t pending_mutex;
} hml_ws_server_t;

// Forward declarations for callbacks
static int hml_ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                           void *user, void *in, size_t len);
static int hml_ws_server_callback(struct lws *wsi, enum lws_callback_reasons reason,
                                  void *user, void *in, size_t len);

// Service thread for WebSocket clients
static void* hml_ws_service_thread(void *arg) {
    hml_ws_connection_t *conn = (hml_ws_connection_t *)arg;
    while (!conn->shutdown) {
        lws_service(conn->context, 50);
    }
    return NULL;
}

// Service thread for WebSocket servers
static void* hml_ws_server_service_thread(void *arg) {
    hml_ws_server_t *server = (hml_ws_server_t *)arg;
    while (!server->shutdown) {
        lws_service(server->context, 50);
    }
    return NULL;
}

// WebSocket client callback
static int hml_ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                           void *user, void *in, size_t len) {
    hml_ws_connection_t *conn = (hml_ws_connection_t *)user;

    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            if (conn) {
                conn->wsi = wsi;
                conn->established = 1;
            }
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE:
            if (conn) {
                hml_ws_message_t *msg = malloc(sizeof(hml_ws_message_t));
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
static int hml_ws_server_callback(struct lws *wsi, enum lws_callback_reasons reason,
                                  void *user, void *in, size_t len) {
    hml_ws_server_t *server = (hml_ws_server_t *)lws_context_user(lws_get_context(wsi));
    hml_ws_connection_t *conn = (hml_ws_connection_t *)user;

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
                hml_ws_message_t *msg = malloc(sizeof(hml_ws_message_t));
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

// Parse WebSocket URL
static int hml_parse_ws_url(const char *url, char *host, int *port, char *path, int *ssl) {
    *ssl = 0;
    *port = 80;
    // SECURITY: Use safe string initialization instead of strcpy
    path[0] = '/';
    path[1] = '\0';

    if (strncmp(url, "wss://", 6) == 0) {
        *ssl = 1;
        *port = 443;
        const char *rest = url + 6;
        const char *slash = strchr(rest, '/');
        const char *colon = strchr(rest, ':');

        if (colon && (!slash || colon < slash)) {
            size_t host_len = colon - rest;
            if (host_len >= 256) return -1;
            strncpy(host, rest, host_len);
            host[host_len] = '\0';
            *port = atoi(colon + 1);
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
    } else if (strncmp(url, "ws://", 5) == 0) {
        const char *rest = url + 5;
        const char *slash = strchr(rest, '/');
        const char *colon = strchr(rest, ':');

        if (colon && (!slash || colon < slash)) {
            size_t host_len = colon - rest;
            if (host_len >= 256) return -1;
            strncpy(host, rest, host_len);
            host[host_len] = '\0';
            *port = atoi(colon + 1);
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

// __lws_ws_connect(url: string): ptr
HmlValue hml_lws_ws_connect(HmlValue url_val) {
    if (url_val.type != HML_VAL_STRING) {
        hml_runtime_error("__lws_ws_connect() expects string URL");
    }

    const char *url = url_val.as.as_string->data;
    char host[256], path[512];
    int port, ssl;

    if (hml_parse_ws_url(url, host, &port, path, &ssl) < 0) {
        hml_runtime_error("Invalid WebSocket URL (must start with ws:// or wss://)");
    }

    hml_ws_connection_t *conn = calloc(1, sizeof(hml_ws_connection_t));
    if (!conn) {
        hml_runtime_error("Failed to allocate WebSocket connection");
    }
    conn->owns_memory = 1;

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.port = CONTEXT_PORT_NO_LISTEN;

    static const struct lws_protocols ws_protocols[] = {
        { "ws", hml_ws_callback, 0, 4096, 0, NULL, 0 },
        { NULL, NULL, 0, 0, 0, NULL, 0 }
    };
    info.protocols = ws_protocols;

    conn->context = lws_create_context(&info);
    if (!conn->context) {
        free(conn);
        hml_runtime_error("Failed to create libwebsockets context");
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
        hml_runtime_error("Failed to connect WebSocket");
    }

    // Wait for connection (timeout 10 seconds)
    int timeout = 100;
    while (timeout-- > 0 && !conn->closed && !conn->failed && !conn->established) {
        lws_service(conn->context, 100);
    }

    if (conn->failed || conn->closed || !conn->established) {
        lws_context_destroy(conn->context);
        free(conn);
        hml_runtime_error("WebSocket connection failed or timed out");
    }

    // Start service thread
    conn->shutdown = 0;
    conn->has_own_thread = 1;
    if (pthread_create(&conn->service_thread, NULL, hml_ws_service_thread, conn) != 0) {
        lws_context_destroy(conn->context);
        free(conn);
        hml_runtime_error("Failed to create WebSocket service thread");
    }

    return hml_val_ptr(conn);
}

// __lws_ws_send_text(conn: ptr, text: string): i32
HmlValue hml_lws_ws_send_text(HmlValue conn_val, HmlValue text_val) {
    if (conn_val.type != HML_VAL_PTR || text_val.type != HML_VAL_STRING) {
        return hml_val_i32(-1);
    }

    hml_ws_connection_t *conn = (hml_ws_connection_t *)conn_val.as.as_ptr;
    if (!conn || conn->closed) {
        return hml_val_i32(-1);
    }

    const char *text = text_val.as.as_string->data;
    size_t len = strlen(text);

    unsigned char *buf = malloc(LWS_PRE + len);
    if (!buf) {
        return hml_val_i32(-1);
    }

    memcpy(buf + LWS_PRE, text, len);
    int written = lws_write(conn->wsi, buf + LWS_PRE, len, LWS_WRITE_TEXT);
    free(buf);

    if (written < 0) {
        return hml_val_i32(-1);
    }

    lws_cancel_service(conn->context);
    return hml_val_i32(0);
}

// __lws_ws_send_binary(conn: ptr, buffer: buffer): i32
HmlValue hml_lws_ws_send_binary(HmlValue conn_val, HmlValue buffer_val) {
    if (conn_val.type != HML_VAL_PTR) {
        return hml_val_i32(-1);
    }

    hml_ws_connection_t *conn = (hml_ws_connection_t *)conn_val.as.as_ptr;
    if (!conn || conn->closed) {
        return hml_val_i32(-1);
    }

    if (buffer_val.type != HML_VAL_BUFFER) {
        return hml_val_i32(-1);
    }

    HmlBuffer *hbuf = buffer_val.as.as_buffer;
    size_t len = hbuf->length;

    unsigned char *buf = malloc(LWS_PRE + len);
    if (!buf) {
        return hml_val_i32(-1);
    }

    memcpy(buf + LWS_PRE, hbuf->data, len);
    int written = lws_write(conn->wsi, buf + LWS_PRE, len, LWS_WRITE_BINARY);
    free(buf);

    if (written < 0) {
        return hml_val_i32(-1);
    }

    lws_cancel_service(conn->context);
    return hml_val_i32(0);
}

// __lws_ws_recv(conn: ptr, timeout_ms: i32): ptr
HmlValue hml_lws_ws_recv(HmlValue conn_val, HmlValue timeout_val) {
    if (conn_val.type != HML_VAL_PTR) {
        return hml_val_null();
    }

    hml_ws_connection_t *conn = (hml_ws_connection_t *)conn_val.as.as_ptr;
    if (!conn || conn->closed) {
        return hml_val_null();
    }

    int timeout_ms = hml_to_i32(timeout_val);
    int iterations = timeout_ms > 0 ? (timeout_ms / 10) : -1;

    while (iterations != 0) {
        if (conn->msg_queue_head) {
            hml_ws_message_t *msg = conn->msg_queue_head;
            conn->msg_queue_head = msg->next;
            if (!conn->msg_queue_head) {
                conn->msg_queue_tail = NULL;
            }
            msg->next = NULL;
            return hml_val_ptr(msg);
        }

        usleep(10000);  // 10ms sleep
        if (conn->closed) return hml_val_null();
        if (iterations > 0) iterations--;
    }

    return hml_val_null();
}

// __lws_msg_type(msg: ptr): i32
HmlValue hml_lws_msg_type(HmlValue msg_val) {
    if (msg_val.type != HML_VAL_PTR) {
        return hml_val_i32(0);
    }

    hml_ws_message_t *msg = (hml_ws_message_t *)msg_val.as.as_ptr;
    if (!msg) {
        return hml_val_i32(0);
    }

    return hml_val_i32(msg->is_binary ? 2 : 1);
}

// __lws_msg_text(msg: ptr): string
HmlValue hml_lws_msg_text(HmlValue msg_val) {
    if (msg_val.type != HML_VAL_PTR) {
        return hml_val_string("");
    }

    hml_ws_message_t *msg = (hml_ws_message_t *)msg_val.as.as_ptr;
    if (!msg || !msg->data) {
        return hml_val_string("");
    }

    return hml_val_string((const char *)msg->data);
}

// __lws_msg_len(msg: ptr): i32
HmlValue hml_lws_msg_len(HmlValue msg_val) {
    if (msg_val.type != HML_VAL_PTR) {
        return hml_val_i32(0);
    }

    hml_ws_message_t *msg = (hml_ws_message_t *)msg_val.as.as_ptr;
    if (!msg) {
        return hml_val_i32(0);
    }

    return hml_val_i32((int32_t)msg->len);
}

// __lws_msg_free(msg: ptr): null
HmlValue hml_lws_msg_free(HmlValue msg_val) {
    if (msg_val.type == HML_VAL_PTR) {
        hml_ws_message_t *msg = (hml_ws_message_t *)msg_val.as.as_ptr;
        if (msg) {
            if (msg->data) free(msg->data);
            free(msg);
        }
    }
    return hml_val_null();
}

// __lws_ws_close(conn: ptr): null
HmlValue hml_lws_ws_close(HmlValue conn_val) {
    if (conn_val.type != HML_VAL_PTR) {
        return hml_val_null();
    }

    hml_ws_connection_t *conn = (hml_ws_connection_t *)conn_val.as.as_ptr;
    if (conn) {
        conn->closed = 1;
        conn->shutdown = 1;

        if (conn->has_own_thread) {
            pthread_join(conn->service_thread, NULL);
        }

        hml_ws_message_t *msg = conn->msg_queue_head;
        while (msg) {
            hml_ws_message_t *next = msg->next;
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

    return hml_val_null();
}

// __lws_ws_is_closed(conn: ptr): i32
HmlValue hml_lws_ws_is_closed(HmlValue conn_val) {
    if (conn_val.type != HML_VAL_PTR) {
        return hml_val_i32(1);
    }

    hml_ws_connection_t *conn = (hml_ws_connection_t *)conn_val.as.as_ptr;
    return hml_val_i32(conn ? conn->closed : 1);
}

// __lws_ws_server_create(host: string, port: i32): ptr
HmlValue hml_lws_ws_server_create(HmlValue host_val, HmlValue port_val) {
    if (host_val.type != HML_VAL_STRING) {
        hml_runtime_error("__lws_ws_server_create() expects string host");
    }

    const char *host = host_val.as.as_string->data;
    int port = hml_to_i32(port_val);

    hml_ws_server_t *server = calloc(1, sizeof(hml_ws_server_t));
    if (!server) {
        hml_runtime_error("Failed to allocate WebSocket server");
    }

    server->port = port;
    pthread_mutex_init(&server->pending_mutex, NULL);

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = port;
    info.iface = host;
    info.user = server;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    static const struct lws_protocols server_protocols[] = {
        { "ws", hml_ws_server_callback, sizeof(hml_ws_connection_t), 4096, 0, NULL, 0 },
        { NULL, NULL, 0, 0, 0, NULL, 0 }
    };
    info.protocols = server_protocols;

    server->context = lws_create_context(&info);
    if (!server->context) {
        pthread_mutex_destroy(&server->pending_mutex);
        free(server);
        hml_runtime_error("Failed to create WebSocket server context");
    }

    server->shutdown = 0;
    if (pthread_create(&server->service_thread, NULL, hml_ws_server_service_thread, server) != 0) {
        lws_context_destroy(server->context);
        pthread_mutex_destroy(&server->pending_mutex);
        free(server);
        hml_runtime_error("Failed to create WebSocket server thread");
    }

    return hml_val_ptr(server);
}

// __lws_ws_server_accept(server: ptr, timeout_ms: i32): ptr
HmlValue hml_lws_ws_server_accept(HmlValue server_val, HmlValue timeout_val) {
    if (server_val.type != HML_VAL_PTR) {
        return hml_val_null();
    }

    hml_ws_server_t *server = (hml_ws_server_t *)server_val.as.as_ptr;
    if (!server || server->closed) {
        return hml_val_null();
    }

    int timeout_ms = hml_to_i32(timeout_val);
    int iterations = timeout_ms > 0 ? (timeout_ms / 10) : -1;

    while (iterations != 0) {
        pthread_mutex_lock(&server->pending_mutex);
        hml_ws_connection_t *conn = NULL;
        if (server->pending_wsi) {
            conn = server->pending_conn;
            server->pending_wsi = NULL;
            server->pending_conn = NULL;
        }
        pthread_mutex_unlock(&server->pending_mutex);

        if (conn) {
            return hml_val_ptr(conn);
        }

        usleep(10000);  // 10ms sleep
        if (iterations > 0) iterations--;
    }

    return hml_val_null();
}

// __lws_ws_server_close(server: ptr): null
HmlValue hml_lws_ws_server_close(HmlValue server_val) {
    if (server_val.type != HML_VAL_PTR) {
        return hml_val_null();
    }

    hml_ws_server_t *server = (hml_ws_server_t *)server_val.as.as_ptr;
    if (server) {
        server->closed = 1;
        server->shutdown = 1;
        pthread_join(server->service_thread, NULL);
        pthread_mutex_destroy(&server->pending_mutex);
        if (server->context) {
            lws_context_destroy(server->context);
        }
        free(server);
    }

    return hml_val_null();
}

// WebSocket builtin wrappers
HmlValue hml_builtin_lws_ws_connect(HmlClosureEnv *env, HmlValue url) {
    (void)env;
    return hml_lws_ws_connect(url);
}

HmlValue hml_builtin_lws_ws_send_text(HmlClosureEnv *env, HmlValue conn, HmlValue text) {
    (void)env;
    return hml_lws_ws_send_text(conn, text);
}

HmlValue hml_builtin_lws_ws_send_binary(HmlClosureEnv *env, HmlValue conn, HmlValue buffer) {
    (void)env;
    return hml_lws_ws_send_binary(conn, buffer);
}

HmlValue hml_builtin_lws_ws_recv(HmlClosureEnv *env, HmlValue conn, HmlValue timeout_ms) {
    (void)env;
    return hml_lws_ws_recv(conn, timeout_ms);
}

HmlValue hml_builtin_lws_ws_close(HmlClosureEnv *env, HmlValue conn) {
    (void)env;
    return hml_lws_ws_close(conn);
}

HmlValue hml_builtin_lws_ws_is_closed(HmlClosureEnv *env, HmlValue conn) {
    (void)env;
    return hml_lws_ws_is_closed(conn);
}

HmlValue hml_builtin_lws_msg_type(HmlClosureEnv *env, HmlValue msg) {
    (void)env;
    return hml_lws_msg_type(msg);
}

HmlValue hml_builtin_lws_msg_text(HmlClosureEnv *env, HmlValue msg) {
    (void)env;
    return hml_lws_msg_text(msg);
}

HmlValue hml_builtin_lws_msg_len(HmlClosureEnv *env, HmlValue msg) {
    (void)env;
    return hml_lws_msg_len(msg);
}

HmlValue hml_builtin_lws_msg_free(HmlClosureEnv *env, HmlValue msg) {
    (void)env;
    return hml_lws_msg_free(msg);
}

HmlValue hml_builtin_lws_ws_server_create(HmlClosureEnv *env, HmlValue host, HmlValue port) {
    (void)env;
    return hml_lws_ws_server_create(host, port);
}

HmlValue hml_builtin_lws_ws_server_accept(HmlClosureEnv *env, HmlValue server, HmlValue timeout_ms) {
    (void)env;
    return hml_lws_ws_server_accept(server, timeout_ms);
}

HmlValue hml_builtin_lws_ws_server_close(HmlClosureEnv *env, HmlValue server) {
    (void)env;
    return hml_lws_ws_server_close(server);
}

#else  // !HML_HAVE_LIBWEBSOCKETS

// Stub implementations
HmlValue hml_lws_http_get(HmlValue url_val) {
    (void)url_val;
    hml_runtime_error("HTTP support not available (libwebsockets not installed)");
}

HmlValue hml_lws_http_post(HmlValue url_val, HmlValue body_val, HmlValue content_type_val) {
    (void)url_val; (void)body_val; (void)content_type_val;
    hml_runtime_error("HTTP support not available (libwebsockets not installed)");
}

HmlValue hml_lws_http_request(HmlValue method_val, HmlValue url_val, HmlValue body_val, HmlValue content_type_val) {
    (void)method_val; (void)url_val; (void)body_val; (void)content_type_val;
    hml_runtime_error("HTTP support not available (libwebsockets not installed)");
}

HmlValue hml_lws_http_get_timeout(HmlValue url_val, HmlValue timeout_val) {
    (void)url_val; (void)timeout_val;
    hml_runtime_error("HTTP support not available (libwebsockets not installed)");
}

HmlValue hml_lws_http_post_timeout(HmlValue url_val, HmlValue body_val, HmlValue content_type_val, HmlValue timeout_val) {
    (void)url_val; (void)body_val; (void)content_type_val; (void)timeout_val;
    hml_runtime_error("HTTP support not available (libwebsockets not installed)");
}

HmlValue hml_lws_http_request_timeout(HmlValue method_val, HmlValue url_val, HmlValue body_val, HmlValue content_type_val, HmlValue timeout_val) {
    (void)method_val; (void)url_val; (void)body_val; (void)content_type_val; (void)timeout_val;
    hml_runtime_error("HTTP support not available (libwebsockets not installed)");
}

HmlValue hml_lws_response_status(HmlValue resp_val) {
    (void)resp_val;
    hml_runtime_error("HTTP support not available (libwebsockets not installed)");
}

HmlValue hml_lws_response_body(HmlValue resp_val) {
    (void)resp_val;
    hml_runtime_error("HTTP support not available (libwebsockets not installed)");
}

HmlValue hml_lws_response_headers(HmlValue resp_val) {
    (void)resp_val;
    hml_runtime_error("HTTP support not available (libwebsockets not installed)");
}

HmlValue hml_lws_response_free(HmlValue resp_val) {
    (void)resp_val;
    return hml_val_null();
}

HmlValue hml_lws_response_redirect(HmlValue resp_val) {
    (void)resp_val;
    return hml_val_null();
}

HmlValue hml_lws_response_body_binary(HmlValue resp_val) {
    (void)resp_val;
    return hml_val_buffer(0);
}

HmlValue hml_builtin_lws_http_get(HmlClosureEnv *env, HmlValue url) {
    (void)env;
    return hml_lws_http_get(url);
}

HmlValue hml_builtin_lws_http_post(HmlClosureEnv *env, HmlValue url, HmlValue body, HmlValue content_type) {
    (void)env;
    return hml_lws_http_post(url, body, content_type);
}

HmlValue hml_builtin_lws_http_request(HmlClosureEnv *env, HmlValue method, HmlValue url, HmlValue body, HmlValue content_type) {
    (void)env;
    return hml_lws_http_request(method, url, body, content_type);
}

HmlValue hml_builtin_lws_response_status(HmlClosureEnv *env, HmlValue resp) {
    (void)env;
    return hml_lws_response_status(resp);
}

HmlValue hml_builtin_lws_response_body(HmlClosureEnv *env, HmlValue resp) {
    (void)env;
    return hml_lws_response_body(resp);
}

HmlValue hml_builtin_lws_response_headers(HmlClosureEnv *env, HmlValue resp) {
    (void)env;
    return hml_lws_response_headers(resp);
}

HmlValue hml_builtin_lws_response_free(HmlClosureEnv *env, HmlValue resp) {
    (void)env;
    return hml_lws_response_free(resp);
}

HmlValue hml_builtin_lws_response_redirect(HmlClosureEnv *env, HmlValue resp) {
    (void)env;
    return hml_lws_response_redirect(resp);
}

HmlValue hml_builtin_lws_response_body_binary(HmlClosureEnv *env, HmlValue resp) {
    (void)env;
    return hml_lws_response_body_binary(resp);
}

// WebSocket stub implementations
HmlValue hml_lws_ws_connect(HmlValue url_val) {
    (void)url_val;
    hml_runtime_error("WebSocket support not available (libwebsockets not installed)");
}

HmlValue hml_lws_ws_send_text(HmlValue conn_val, HmlValue text_val) {
    (void)conn_val; (void)text_val;
    hml_runtime_error("WebSocket support not available (libwebsockets not installed)");
}

HmlValue hml_lws_ws_send_binary(HmlValue conn_val, HmlValue buffer_val) {
    (void)conn_val; (void)buffer_val;
    hml_runtime_error("WebSocket support not available (libwebsockets not installed)");
}

HmlValue hml_lws_ws_recv(HmlValue conn_val, HmlValue timeout_val) {
    (void)conn_val; (void)timeout_val;
    hml_runtime_error("WebSocket support not available (libwebsockets not installed)");
}

HmlValue hml_lws_ws_close(HmlValue conn_val) {
    (void)conn_val;
    return hml_val_null();
}

HmlValue hml_lws_ws_is_closed(HmlValue conn_val) {
    (void)conn_val;
    return hml_val_i32(1);
}

HmlValue hml_lws_msg_type(HmlValue msg_val) {
    (void)msg_val;
    return hml_val_i32(0);
}

HmlValue hml_lws_msg_text(HmlValue msg_val) {
    (void)msg_val;
    return hml_val_string("");
}

HmlValue hml_lws_msg_len(HmlValue msg_val) {
    (void)msg_val;
    return hml_val_i32(0);
}

HmlValue hml_lws_msg_free(HmlValue msg_val) {
    (void)msg_val;
    return hml_val_null();
}

HmlValue hml_lws_ws_server_create(HmlValue host_val, HmlValue port_val) {
    (void)host_val; (void)port_val;
    hml_runtime_error("WebSocket support not available (libwebsockets not installed)");
}

HmlValue hml_lws_ws_server_accept(HmlValue server_val, HmlValue timeout_val) {
    (void)server_val; (void)timeout_val;
    hml_runtime_error("WebSocket support not available (libwebsockets not installed)");
}

HmlValue hml_lws_ws_server_close(HmlValue server_val) {
    (void)server_val;
    return hml_val_null();
}

// WebSocket builtin wrapper stubs
HmlValue hml_builtin_lws_ws_connect(HmlClosureEnv *env, HmlValue url) {
    (void)env;
    return hml_lws_ws_connect(url);
}

HmlValue hml_builtin_lws_ws_send_text(HmlClosureEnv *env, HmlValue conn, HmlValue text) {
    (void)env;
    return hml_lws_ws_send_text(conn, text);
}

HmlValue hml_builtin_lws_ws_send_binary(HmlClosureEnv *env, HmlValue conn, HmlValue buffer) {
    (void)env;
    return hml_lws_ws_send_binary(conn, buffer);
}

HmlValue hml_builtin_lws_ws_recv(HmlClosureEnv *env, HmlValue conn, HmlValue timeout_ms) {
    (void)env;
    return hml_lws_ws_recv(conn, timeout_ms);
}

HmlValue hml_builtin_lws_ws_close(HmlClosureEnv *env, HmlValue conn) {
    (void)env;
    return hml_lws_ws_close(conn);
}

HmlValue hml_builtin_lws_ws_is_closed(HmlClosureEnv *env, HmlValue conn) {
    (void)env;
    return hml_lws_ws_is_closed(conn);
}

HmlValue hml_builtin_lws_msg_type(HmlClosureEnv *env, HmlValue msg) {
    (void)env;
    return hml_lws_msg_type(msg);
}

HmlValue hml_builtin_lws_msg_text(HmlClosureEnv *env, HmlValue msg) {
    (void)env;
    return hml_lws_msg_text(msg);
}

HmlValue hml_builtin_lws_msg_len(HmlClosureEnv *env, HmlValue msg) {
    (void)env;
    return hml_lws_msg_len(msg);
}

HmlValue hml_builtin_lws_msg_free(HmlClosureEnv *env, HmlValue msg) {
    (void)env;
    return hml_lws_msg_free(msg);
}

HmlValue hml_builtin_lws_ws_server_create(HmlClosureEnv *env, HmlValue host, HmlValue port) {
    (void)env;
    return hml_lws_ws_server_create(host, port);
}

HmlValue hml_builtin_lws_ws_server_accept(HmlClosureEnv *env, HmlValue server, HmlValue timeout_ms) {
    (void)env;
    return hml_lws_ws_server_accept(server, timeout_ms);
}

HmlValue hml_builtin_lws_ws_server_close(HmlClosureEnv *env, HmlValue server) {
    (void)env;
    return hml_lws_ws_server_close(server);
}

#endif  // HML_HAVE_LIBWEBSOCKETS
