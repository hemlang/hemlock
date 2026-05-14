/*
 * Hemlock Runtime Library - HTTP/WebSocket Builtins
 *
 * This file implements:
 * - HTTP client support (http_get, http_post, http_request)
 * - WebSocket support
 *
 * Conditional compilation: requires HML_HAVE_LIBWEBSOCKETS
 *
 * WASM: This entire file is excluded when building for Emscripten.
 * Stub implementations are provided in wasm_shim.c.
 */

#ifndef __EMSCRIPTEN__

#include "builtins_internal.h"

// ========== HTTP/WEBSOCKET SUPPORT ==========
// Requires libwebsockets

#ifdef HML_HAVE_LIBWEBSOCKETS

#include <libwebsockets.h>
#include <unistd.h>


#define HEMLOCK_MACOS_OPENSSL_CERT_FILE "/opt/homebrew/etc/openssl@3/cert.pem"

static const char *hml_macos_homebrew_ca_filepath(void) {
#ifdef __APPLE__
    if (access(HEMLOCK_MACOS_OPENSSL_CERT_FILE, R_OK) == 0) {
        return HEMLOCK_MACOS_OPENSSL_CERT_FILE;
    }
#endif
    return NULL;
}

static void hml_lws_configure_macos_ca_file(void) {
#ifdef __APPLE__
    const char *cert_file = getenv("SSL_CERT_FILE");
    if ((!cert_file || cert_file[0] == '\0') &&
        access(HEMLOCK_MACOS_OPENSSL_CERT_FILE, R_OK) == 0) {
        setenv("SSL_CERT_FILE", HEMLOCK_MACOS_OPENSSL_CERT_FILE, 0);
    }
#endif
}

// Configure default lws context options for an HTTP/HTTPS request.
// `ssl` is 1 when the URL scheme is https (full TLS init + CA bundle),
// 0 when it's plain http. The ssl arg is currently advisory — lws sets
// up the default vhost's client SSL_CTX regardless of whether *this*
// request needs TLS, so we have to make that init path succeed even
// for plain-http calls.
//
// LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT must be set only ONCE per
// process. Setting it on every context creation works on Linux but
// is broken on macOS: the second context's lws_context_init_client_ssl
// fails ("Failed to create default vhost") because OpenSSL global
// init/cleanup races between the destroyed first context and the new
// one being built. The vhost teardown then closes file descriptors
// via a shared fd table cleanup, which corrupts an unrelated
// TcpListener fd in another thread — its accept() throws ECONNABORTED,
// then EBADF forever, killing the whole HTTP server in the process.
//
// Gate the flag behind a once-flag so we pay the global init exactly
// once per process. On macOS we additionally point at Homebrew
// openssl@3's cert.pem (system OpenSSL's compile-time default paths
// don't match the Homebrew layout, so lws's CA load fails without
// an explicit filepath).
static void hml_lws_configure_options(struct lws_context_creation_info *info, int ssl) {
    (void)ssl;
    static int ssl_global_init_done = 0;
    if (!ssl_global_init_done) {
        info->options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
        ssl_global_init_done = 1;
    } else {
        info->options = 0;
    }
#ifdef __APPLE__
    if (!info->client_ssl_ca_filepath) {
        const char *p = hml_macos_homebrew_ca_filepath();
        if (p) info->client_ssl_ca_filepath = p;
    }
#endif
}

static const char *hml_lws_context_error_message(void) {
#ifdef __APPLE__
    return "Failed to create libwebsockets context; on macOS, install openssl@3 with Homebrew or set SSL_CERT_FILE=/opt/homebrew/etc/openssl@3/cert.pem";
#else
    return "Failed to create libwebsockets context";
#endif
}

// Suppress libwebsockets startup banner. Without this, every lws_create_context
// emits a NOTICE-level line ("LWS: 4.3.3-... NET CLI SRV H1 H2 WS ...") to
// stderr — which the parity test runner captures, breaking expected-output
// comparison between interpreter (which silences via lws_init_logging in
// websockets.c) and compiled binary (which never did, until now).
//
// Set LWS_VERBOSE=1 to opt back in.
static void hml_lws_init_logging(void) {
    static int initialized = 0;
    if (!initialized) {
        initialized = 1;
        const char *verbose = getenv("LWS_VERBOSE");
        if (!verbose || strcmp(verbose, "1") != 0) {
            lws_set_log_level(LLL_ERR, NULL);
        }
    }
}

// Default timeout for the non-*_timeout one-shot LWS HTTP helpers.
// The service loop below polls in ~10ms increments, so 5000ms maps to
// 500 iterations instead of the previous 3000 iterations (~30000ms).
#define HML_LWS_HTTP_DEFAULT_TIMEOUT_MS 5000
#define HML_LWS_HTTP_SERVICE_POLL_MS 10

static int hml_lws_timeout_iterations_from_ms(int timeout_ms) {
    int iterations = timeout_ms / HML_LWS_HTTP_SERVICE_POLL_MS;
    return iterations < 1 ? 1 : iterations;
}

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
    char **custom_headers;   // Custom request headers
    int num_custom_headers;
    // Outgoing request body for POST/PUT/PATCH/DELETE-with-body. Owned by
    // this struct; freed via hml_free_request_body() once the LWS event
    // loop completes (or fails) so callers don't have to worry about it.
    char *post_body;
    size_t post_body_len;
    char *content_type;
    int body_sent;
} hml_http_response_t;

static void hml_free_custom_headers(hml_http_response_t *resp) {
    if (resp->custom_headers) {
        for (int i = 0; i < resp->num_custom_headers; i++) {
            free(resp->custom_headers[i]);
        }
        free(resp->custom_headers);
        resp->custom_headers = NULL;
        resp->num_custom_headers = 0;
    }
}

static void hml_free_request_body(hml_http_response_t *resp) {
    if (!resp) return;
    if (resp->post_body) {
        free(resp->post_body);
        resp->post_body = NULL;
    }
    if (resp->content_type) {
        free(resp->content_type);
        resp->content_type = NULL;
    }
    resp->post_body_len = 0;
}

// Stash the body + content-type on `resp` so the LWS callback can emit
// Content-Type / Content-Length headers and stream the bytes when the
// socket becomes writable. Returns 0 on success, -1 on alloc failure.
// Empty bodies are valid (some methods send Content-Length: 0).
static int hml_attach_request_body(hml_http_response_t *resp,
                                   const char *body, const char *content_type) {
    if (!resp || !body) return -1;
    size_t blen = strlen(body);
    resp->post_body = malloc(blen + 1);
    if (!resp->post_body) return -1;
    memcpy(resp->post_body, body, blen);
    resp->post_body[blen] = '\0';
    resp->post_body_len = blen;

    if (content_type && content_type[0]) {
        resp->content_type = strdup(content_type);
        if (!resp->content_type) {
            free(resp->post_body);
            resp->post_body = NULL;
            resp->post_body_len = 0;
            return -1;
        }
    }
    return 0;
}

// Detect whether the caller already supplied Content-Type so we don't
// double-emit it from the default path.
static int hml_custom_headers_have_content_type(hml_http_response_t *resp) {
    if (!resp || !resp->custom_headers) return 0;
    for (int i = 0; i < resp->num_custom_headers; i++) {
        const char *h = resp->custom_headers[i];
        if (!h) continue;
        // ASCII case-insensitive prefix compare against "content-type:"
        const char *needle = "content-type:";
        size_t nlen = strlen(needle);
        if (strlen(h) < nlen) continue;
        int matches = 1;
        for (size_t k = 0; k < nlen; k++) {
            char a = h[k]; if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            char b = needle[k];
            if (a != b) { matches = 0; break; }
        }
        if (matches) return 1;
    }
    return 0;
}

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
    hml_free_custom_headers(resp);
    hml_free_request_body(resp);
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

                if (resp && resp->custom_headers && resp->num_custom_headers > 0) {
                    // Write caller-supplied headers
                    for (int i = 0; i < resp->num_custom_headers; i++) {
                        const char *hdr = resp->custom_headers[i];
                        size_t hdr_len = strlen(hdr);
                        int needs_crlf = (hdr_len < 2 || hdr[hdr_len-2] != '\r' || hdr[hdr_len-1] != '\n');
                        size_t total = hdr_len + (needs_crlf ? 2 : 0);
                        if (end - *p >= (int)total) {
                            memcpy(*p, hdr, hdr_len);
                            *p += hdr_len;
                            if (needs_crlf) {
                                memcpy(*p, "\r\n", 2);
                                *p += 2;
                            }
                        }
                    }
                } else {
                    // Default headers when none supplied
                    const char *ua = "User-Agent: hemlock/1.0\r\n";
                    size_t ua_len = strlen(ua);
                    if (end - *p >= (int)ua_len) {
                        memcpy(*p, ua, ua_len);
                        *p += ua_len;
                    }

                    const char *accept = "Accept: application/json\r\n";
                    size_t accept_len = strlen(accept);
                    if (end - *p >= (int)accept_len) {
                        memcpy(*p, accept, accept_len);
                        *p += accept_len;
                    }
                }

                // If the caller attached a request body, emit Content-Type
                // (unless they supplied one) + Content-Length, and arm a
                // writable callback so we can stream the body bytes once
                // the socket is ready. Without this, POST/PUT/PATCH bodies
                // never make it onto the wire — which is exactly the bug
                // this branch is fixing.
                if (resp && resp->post_body && resp->post_body_len > 0) {
                    int caller_set_ct = hml_custom_headers_have_content_type(resp);
                    if (!caller_set_ct) {
                        const char *ct = (resp->content_type && resp->content_type[0])
                                             ? resp->content_type
                                             : "application/octet-stream";
                        char ct_line[256];
                        int ctn = snprintf(ct_line, sizeof(ct_line),
                                           "Content-Type: %s\r\n", ct);
                        if (ctn > 0 && (end - *p) >= ctn) {
                            memcpy(*p, ct_line, (size_t)ctn);
                            *p += ctn;
                        }
                    }
                    char cl_line[64];
                    int cln = snprintf(cl_line, sizeof(cl_line),
                                       "Content-Length: %zu\r\n",
                                       resp->post_body_len);
                    if (cln > 0 && (end - *p) >= cln) {
                        memcpy(*p, cl_line, (size_t)cln);
                        *p += cln;
                    }
                    lws_client_http_body_pending(wsi, 1);
                    lws_callback_on_writable(wsi);
                }
            }
            break;

        case LWS_CALLBACK_CLIENT_HTTP_WRITEABLE:
            if (resp && resp->post_body && resp->post_body_len > 0 && !resp->body_sent) {
                size_t body_len = resp->post_body_len;
                unsigned char *buf = malloc(LWS_PRE + body_len);
                if (!buf) {
                    resp->failed = 1;
                    resp->complete = 1;
                    return -1;
                }
                memcpy(buf + LWS_PRE, resp->post_body, body_len);
                int n = lws_write(wsi, buf + LWS_PRE, body_len, LWS_WRITE_HTTP);
                free(buf);
                lws_client_http_body_pending(wsi, 0);
                resp->body_sent = 1;
                return n < 0 ? -1 : 0;
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

// Helper: parse a Hemlock array of strings into C header strings stored in resp
static void hml_parse_headers_into_resp(hml_http_response_t *resp, HmlValue headers_val) {
    if (headers_val.type != HML_VAL_ARRAY || !headers_val.as.as_array) return;
    HmlArray *arr = headers_val.as.as_array;
    if (arr->length == 0) return;

    resp->custom_headers = calloc(arr->length, sizeof(char *));
    if (!resp->custom_headers) return;

    int count = 0;
    for (int i = 0; i < arr->length; i++) {
        HmlValue item = arr->elements[i];
        if (item.type == HML_VAL_STRING && item.as.as_string && item.as.as_string->data) {
            resp->custom_headers[count] = strdup(item.as.as_string->data);
            if (resp->custom_headers[count]) count++;
        }
    }
    resp->num_custom_headers = count;
}

// HTTP GET
HmlValue hml_lws_http_get(HmlValue url_val) {
    hml_lws_init_logging();
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
    hml_lws_configure_options(&info, ssl);
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.max_http_header_data = 16384;  // 16KB for large headers (e.g., GitHub API)

    static const struct lws_protocols protocols[] = {
        { "http", hml_http_callback, 0, 16384, 0, NULL, 0 },
        { NULL, NULL, 0, 0, 0, NULL, 0 }
    };
    info.protocols = protocols;

    struct lws_context *context = (hml_lws_configure_macos_ca_file(), hml_runtime_pin_stdio(), lws_create_context(&info));
    if (!context) {
        hml_lws_response_destroy(resp);
        hml_runtime_error("%s", hml_lws_context_error_message());
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

    int timeout = hml_lws_timeout_iterations_from_ms(HML_LWS_HTTP_DEFAULT_TIMEOUT_MS);
    while (!resp->complete && !resp->failed && timeout-- > 0) {
        lws_service(context, HML_LWS_HTTP_SERVICE_POLL_MS);
    }

    lws_context_destroy(context);

    if (resp->failed || timeout <= 0) {
        hml_lws_response_destroy(resp);
        hml_runtime_error("HTTP request failed or timed out");
    }

    return hml_val_ptr(resp);
}

// HTTP GET with custom headers
HmlValue hml_lws_http_get_with_headers(HmlValue url_val, HmlValue headers_val) {
    hml_lws_init_logging();
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

    // Parse custom headers
    hml_parse_headers_into_resp(resp, headers_val);

    resp->body_capacity = 4096;
    resp->body = malloc(resp->body_capacity);
    if (!resp->body) {
        hml_lws_response_destroy(resp);
        hml_runtime_error("Failed to allocate body buffer");
    }
    resp->body[0] = '\0';

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    hml_lws_configure_options(&info, ssl);
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.max_http_header_data = 16384;

    static const struct lws_protocols protocols[] = {
        { "http", hml_http_callback, 0, 16384, 0, NULL, 0 },
        { NULL, NULL, 0, 0, 0, NULL, 0 }
    };
    info.protocols = protocols;

    struct lws_context *context = (hml_lws_configure_macos_ca_file(), hml_runtime_pin_stdio(), lws_create_context(&info));
    if (!context) {
        hml_lws_response_destroy(resp);
        hml_runtime_error("%s", hml_lws_context_error_message());
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

    int timeout = hml_lws_timeout_iterations_from_ms(HML_LWS_HTTP_DEFAULT_TIMEOUT_MS);
    while (!resp->complete && !resp->failed && timeout-- > 0) {
        lws_service(context, HML_LWS_HTTP_SERVICE_POLL_MS);
    }

    lws_context_destroy(context);

    if (resp->failed || timeout <= 0) {
        hml_lws_response_destroy(resp);
        hml_runtime_error("HTTP request failed or timed out");
    }

    return hml_val_ptr(resp);
}

// Shared core for POST/PUT/DELETE/PATCH/etc. — handles URL parsing, response
// allocation, optional custom headers, optional request body, the LWS context
// + connect dance, and the service loop. On failure raises a runtime error
// (which is noreturn). On success returns the response as an HmlValue ptr.
//
// `headers_val` may be a non-array (e.g. null) — in that case no custom
// headers are attached. `force_body` controls whether a zero-length body is
// still attached (POST always does so; the generic request path skips for
// empty bodies, matching pre-fix behavior).
static HmlValue hml_lws_http_perform(const char *method,
                                      const char *url,
                                      const char *post_body,
                                      const char *content_type,
                                      HmlValue headers_val,
                                      int timeout_iterations,
                                      int force_body) {
    char host[256], path[512];
    int port, ssl;

    if (hml_parse_url(url, host, &port, path, &ssl) < 0) {
        hml_runtime_error("Invalid URL format");
    }

    hml_http_response_t *resp = calloc(1, sizeof(hml_http_response_t));
    if (!resp) {
        hml_runtime_error("Failed to allocate response");
    }

    hml_parse_headers_into_resp(resp, headers_val);

    resp->body_capacity = 4096;
    resp->body = malloc(resp->body_capacity);
    if (!resp->body) {
        hml_lws_response_destroy(resp);
        hml_runtime_error("Failed to allocate body buffer");
    }
    resp->body[0] = '\0';

    if (post_body && (force_body || strlen(post_body) > 0)) {
        if (hml_attach_request_body(resp, post_body, content_type) < 0) {
            hml_lws_response_destroy(resp);
            hml_runtime_error("Failed to attach request body");
        }
    }

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    hml_lws_configure_options(&info, ssl);
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.max_http_header_data = 16384;

    static const struct lws_protocols protocols[] = {
        { "http", hml_http_callback, 0, 16384, 0, NULL, 0 },
        { NULL, NULL, 0, 0, 0, NULL, 0 }
    };
    info.protocols = protocols;

    struct lws_context *context = (hml_lws_configure_macos_ca_file(), hml_runtime_pin_stdio(), lws_create_context(&info));
    if (!context) {
        hml_lws_response_destroy(resp);
        hml_runtime_error("%s", hml_lws_context_error_message());
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

    // Disable automatic redirects - handled at the hemlock layer
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
        lws_service(context, HML_LWS_HTTP_SERVICE_POLL_MS);
    }

    lws_context_destroy(context);
    hml_free_request_body(resp);

    if (resp->failed || timeout <= 0) {
        hml_lws_response_destroy(resp);
        hml_runtime_error("HTTP request failed or timed out");
    }

    return hml_val_ptr(resp);
}

// HTTP POST
HmlValue hml_lws_http_post(HmlValue url_val, HmlValue body_val, HmlValue content_type_val) {
    hml_lws_init_logging();
    if (url_val.type != HML_VAL_STRING || body_val.type != HML_VAL_STRING || content_type_val.type != HML_VAL_STRING) {
        hml_runtime_error("__lws_http_post() expects string arguments");
    }
    return hml_lws_http_perform("POST",
                                 url_val.as.as_string->data,
                                 body_val.as.as_string->data,
                                 content_type_val.as.as_string->data,
                                 hml_val_null(),
                                 hml_lws_timeout_iterations_from_ms(HML_LWS_HTTP_DEFAULT_TIMEOUT_MS),
                                 /*force_body=*/1);
}

// HTTP POST with custom headers
HmlValue hml_lws_http_post_with_headers(HmlValue url_val, HmlValue body_val,
                                         HmlValue content_type_val, HmlValue headers_val) {
    hml_lws_init_logging();
    if (url_val.type != HML_VAL_STRING || body_val.type != HML_VAL_STRING || content_type_val.type != HML_VAL_STRING) {
        hml_runtime_error("__lws_http_post() expects string arguments");
    }
    return hml_lws_http_perform("POST",
                                 url_val.as.as_string->data,
                                 body_val.as.as_string->data,
                                 content_type_val.as.as_string->data,
                                 headers_val,
                                 hml_lws_timeout_iterations_from_ms(HML_LWS_HTTP_DEFAULT_TIMEOUT_MS),
                                 /*force_body=*/1);
}

// Generic HTTP request with configurable method
HmlValue hml_lws_http_request(HmlValue method_val, HmlValue url_val, HmlValue body_val, HmlValue content_type_val) {
    hml_lws_init_logging();
    if (method_val.type != HML_VAL_STRING || url_val.type != HML_VAL_STRING ||
        body_val.type != HML_VAL_STRING || content_type_val.type != HML_VAL_STRING) {
        hml_runtime_error("__lws_http_request() expects string arguments");
    }
    return hml_lws_http_perform(method_val.as.as_string->data,
                                 url_val.as.as_string->data,
                                 body_val.as.as_string->data,
                                 content_type_val.as.as_string->data,
                                 hml_val_null(),
                                 hml_lws_timeout_iterations_from_ms(HML_LWS_HTTP_DEFAULT_TIMEOUT_MS),
                                 /*force_body=*/0);
}

// Generic HTTP request with configurable method and custom headers
HmlValue hml_lws_http_request_with_headers(HmlValue method_val, HmlValue url_val,
                                            HmlValue body_val, HmlValue content_type_val,
                                            HmlValue headers_val) {
    hml_lws_init_logging();
    if (method_val.type != HML_VAL_STRING || url_val.type != HML_VAL_STRING ||
        body_val.type != HML_VAL_STRING || content_type_val.type != HML_VAL_STRING) {
        hml_runtime_error("__lws_http_request() expects string arguments");
    }
    return hml_lws_http_perform(method_val.as.as_string->data,
                                 url_val.as.as_string->data,
                                 body_val.as.as_string->data,
                                 content_type_val.as.as_string->data,
                                 headers_val,
                                 hml_lws_timeout_iterations_from_ms(HML_LWS_HTTP_DEFAULT_TIMEOUT_MS),
                                 /*force_body=*/0);
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
        timeout_ms = HML_LWS_HTTP_DEFAULT_TIMEOUT_MS;
    }
    return hml_lws_timeout_iterations_from_ms(timeout_ms);
}

// HTTP GET with configurable timeout
HmlValue hml_lws_http_get_timeout(HmlValue url_val, HmlValue timeout_val) {
    hml_lws_init_logging();
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
    hml_lws_configure_options(&info, ssl);
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.max_http_header_data = 16384;

    static const struct lws_protocols protocols[] = {
        { "http", hml_http_callback, 0, 16384, 0, NULL, 0 },
        { NULL, NULL, 0, 0, 0, NULL, 0 }
    };
    info.protocols = protocols;

    struct lws_context *context = (hml_lws_configure_macos_ca_file(), hml_runtime_pin_stdio(), lws_create_context(&info));
    if (!context) {
        hml_lws_response_destroy(resp);
        hml_runtime_error("%s", hml_lws_context_error_message());
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
        lws_service(context, HML_LWS_HTTP_SERVICE_POLL_MS);
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
    hml_lws_init_logging();
    if (url_val.type != HML_VAL_STRING || body_val.type != HML_VAL_STRING || content_type_val.type != HML_VAL_STRING) {
        hml_runtime_error("__lws_http_post_timeout() expects string arguments");
    }
    return hml_lws_http_perform("POST",
                                 url_val.as.as_string->data,
                                 body_val.as.as_string->data,
                                 content_type_val.as.as_string->data,
                                 hml_val_null(),
                                 hml_extract_timeout_ms(timeout_val),
                                 /*force_body=*/1);
}

// HTTP POST with configurable timeout and custom headers
HmlValue hml_lws_http_post_timeout_with_headers(HmlValue url_val, HmlValue body_val,
                                                 HmlValue content_type_val, HmlValue timeout_val,
                                                 HmlValue headers_val) {
    hml_lws_init_logging();
    if (url_val.type != HML_VAL_STRING || body_val.type != HML_VAL_STRING || content_type_val.type != HML_VAL_STRING) {
        hml_runtime_error("__lws_http_post_timeout() expects string arguments");
    }
    return hml_lws_http_perform("POST",
                                 url_val.as.as_string->data,
                                 body_val.as.as_string->data,
                                 content_type_val.as.as_string->data,
                                 headers_val,
                                 hml_extract_timeout_ms(timeout_val),
                                 /*force_body=*/1);
}

// Generic HTTP request with configurable timeout
HmlValue hml_lws_http_request_timeout(HmlValue method_val, HmlValue url_val, HmlValue body_val, HmlValue content_type_val, HmlValue timeout_val) {
    hml_lws_init_logging();
    if (method_val.type != HML_VAL_STRING || url_val.type != HML_VAL_STRING ||
        body_val.type != HML_VAL_STRING || content_type_val.type != HML_VAL_STRING) {
        hml_runtime_error("__lws_http_request_timeout() expects string arguments");
    }
    return hml_lws_http_perform(method_val.as.as_string->data,
                                 url_val.as.as_string->data,
                                 body_val.as.as_string->data,
                                 content_type_val.as.as_string->data,
                                 hml_val_null(),
                                 hml_extract_timeout_ms(timeout_val),
                                 /*force_body=*/0);
}

// Generic HTTP request with configurable timeout and custom headers
HmlValue hml_lws_http_request_timeout_with_headers(HmlValue method_val, HmlValue url_val,
                                                    HmlValue body_val, HmlValue content_type_val,
                                                    HmlValue timeout_val, HmlValue headers_val) {
    hml_lws_init_logging();
    if (method_val.type != HML_VAL_STRING || url_val.type != HML_VAL_STRING ||
        body_val.type != HML_VAL_STRING || content_type_val.type != HML_VAL_STRING) {
        hml_runtime_error("__lws_http_request_timeout() expects string arguments");
    }
    return hml_lws_http_perform(method_val.as.as_string->data,
                                 url_val.as.as_string->data,
                                 body_val.as.as_string->data,
                                 content_type_val.as.as_string->data,
                                 headers_val,
                                 hml_extract_timeout_ms(timeout_val),
                                 /*force_body=*/0);
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

HmlValue hml_builtin_lws_http_get_with_headers(HmlClosureEnv *env, HmlValue url, HmlValue headers) {
    (void)env;
    return hml_lws_http_get_with_headers(url, headers);
}

HmlValue hml_builtin_lws_http_post(HmlClosureEnv *env, HmlValue url, HmlValue body, HmlValue content_type) {
    (void)env;
    return hml_lws_http_post(url, body, content_type);
}

HmlValue hml_builtin_lws_http_post_with_headers(HmlClosureEnv *env, HmlValue url, HmlValue body, HmlValue content_type, HmlValue headers) {
    (void)env;
    return hml_lws_http_post_with_headers(url, body, content_type, headers);
}

HmlValue hml_builtin_lws_http_request(HmlClosureEnv *env, HmlValue method, HmlValue url, HmlValue body, HmlValue content_type) {
    (void)env;
    return hml_lws_http_request(method, url, body, content_type);
}

HmlValue hml_builtin_lws_http_request_with_headers(HmlClosureEnv *env, HmlValue method, HmlValue url, HmlValue body, HmlValue content_type, HmlValue headers) {
    (void)env;
    return hml_lws_http_request_with_headers(method, url, body, content_type, headers);
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

// ========== STREAMING HTTP SUPPORT ==========
// For chunked/SSE responses (e.g., streaming LLM output)

#include <pthread.h>

// Chunk queue node for streaming responses
typedef struct hml_http_stream_chunk {
    char *data;
    size_t len;
    struct hml_http_stream_chunk *next;
} hml_http_stream_chunk_t;

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
    hml_http_stream_chunk_t *chunk_head;
    hml_http_stream_chunk_t *chunk_tail;
    pthread_mutex_t chunk_mutex;
    pthread_cond_t chunk_cond;
    // Outgoing request body (for POST/PUT/PATCH/etc.)
    char *post_body;
    size_t post_body_len;
    char *content_type;
    int body_sent;
} hml_http_stream_t;

static void hml_http_stream_enqueue(hml_http_stream_t *stream, const char *data, size_t len) {
    hml_http_stream_chunk_t *chunk = malloc(sizeof(hml_http_stream_chunk_t));
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
static int hml_http_stream_callback(struct lws *wsi, enum lws_callback_reasons reason,
                                    void *user, void *in, size_t len) {
    hml_http_stream_t *stream = (hml_http_stream_t *)user;

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

            // If we have a request body, emit Content-Type / Content-Length
            // and request a writable callback so we can send the body bytes.
            if (stream && stream->post_body && stream->post_body_len > 0) {
                const char *ct = (stream->content_type && stream->content_type[0])
                                     ? stream->content_type
                                     : "application/octet-stream";
                char ct_line[256];
                int ctn = snprintf(ct_line, sizeof(ct_line),
                                   "Content-Type: %s\r\n", ct);
                if (ctn > 0 && (end - *p) >= ctn) {
                    memcpy(*p, ct_line, (size_t)ctn);
                    *p += ctn;
                }
                char cl_line[64];
                int cln = snprintf(cl_line, sizeof(cl_line),
                                   "Content-Length: %zu\r\n",
                                   stream->post_body_len);
                if (cln > 0 && (end - *p) >= cln) {
                    memcpy(*p, cl_line, (size_t)cln);
                    *p += cln;
                }
                lws_client_http_body_pending(wsi, 1);
                lws_callback_on_writable(wsi);
            }
            break;
        }

        case LWS_CALLBACK_CLIENT_HTTP_WRITEABLE:
            if (stream && stream->post_body && stream->post_body_len > 0 && !stream->body_sent) {
                size_t body_len = stream->post_body_len;
                unsigned char *buf = malloc(LWS_PRE + body_len);
                if (!buf) {
                    stream->failed = 1;
                    stream->complete = 1;
                    pthread_mutex_lock(&stream->chunk_mutex);
                    pthread_cond_signal(&stream->chunk_cond);
                    pthread_mutex_unlock(&stream->chunk_mutex);
                    return -1;
                }
                memcpy(buf + LWS_PRE, stream->post_body, body_len);
                int n = lws_write(wsi, buf + LWS_PRE, body_len, LWS_WRITE_HTTP);
                free(buf);
                lws_client_http_body_pending(wsi, 0);
                stream->body_sent = 1;
                return n < 0 ? -1 : 0;
            }
            break;

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
                hml_http_stream_enqueue(stream, (const char *)in, len);
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
static void* hml_http_stream_service_thread(void *arg) {
    hml_http_stream_t *stream = (hml_http_stream_t *)arg;
    while (!stream->shutdown && !stream->complete && !stream->failed) {
        lws_service(stream->context, 50);
    }
    for (int i = 0; i < 10 && !stream->shutdown; i++) {
        lws_service(stream->context, 10);
    }
    return NULL;
}

// __lws_http_stream_start(method, url, body, content_type, timeout_ms): ptr
HmlValue hml_lws_http_stream_start(HmlValue method_val, HmlValue url_val,
                                    HmlValue body_val, HmlValue content_type_val,
                                    HmlValue timeout_val) {
    if (method_val.type != HML_VAL_STRING || url_val.type != HML_VAL_STRING) {
        hml_runtime_error("__lws_http_stream_start() expects string method and URL");
    }

    const char *method = method_val.as.as_string->data;
    const char *url = url_val.as.as_string->data;
    const char *post_body = (body_val.type == HML_VAL_STRING) ? body_val.as.as_string->data : "";
    const char *content_type = (content_type_val.type == HML_VAL_STRING) ? content_type_val.as.as_string->data : "";

    int timeout_ms = 120000;
    if (timeout_val.type == HML_VAL_I32) timeout_ms = timeout_val.as.as_i32;
    else if (timeout_val.type == HML_VAL_I64) timeout_ms = (int)timeout_val.as.as_i64;

    char host[256], path[512];
    int port, ssl;
    if (hml_parse_url(url, host, &port, path, &ssl) < 0) {
        hml_runtime_error("Invalid URL format");
    }

    hml_http_stream_t *stream = calloc(1, sizeof(hml_http_stream_t));
    if (!stream) {
        hml_runtime_error("Failed to allocate stream");
    }
    pthread_mutex_init(&stream->chunk_mutex, NULL);
    pthread_cond_init(&stream->chunk_cond, NULL);

    // Attach outgoing request body (if any) so the callback can send it.
    if (post_body && post_body[0] != '\0') {
        size_t blen = strlen(post_body);
        stream->post_body = malloc(blen + 1);
        if (!stream->post_body) {
            pthread_mutex_destroy(&stream->chunk_mutex);
            pthread_cond_destroy(&stream->chunk_cond);
            free(stream);
            hml_runtime_error("Failed to allocate request body");
        }
        memcpy(stream->post_body, post_body, blen);
        stream->post_body[blen] = '\0';
        stream->post_body_len = blen;
        if (content_type && content_type[0] != '\0') {
            stream->content_type = strdup(content_type);
            if (!stream->content_type) {
                free(stream->post_body);
                pthread_mutex_destroy(&stream->chunk_mutex);
                pthread_cond_destroy(&stream->chunk_cond);
                free(stream);
                hml_runtime_error("Failed to allocate content_type");
            }
        }
    }

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    hml_lws_configure_options(&info, ssl);
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.max_http_header_data = 16384;

    static const struct lws_protocols stream_protocols[] = {
        { "http-stream", hml_http_stream_callback, 0, 16384, 0, NULL, 0 },
        { NULL, NULL, 0, 0, 0, NULL, 0 }
    };
    info.protocols = stream_protocols;

    stream->context = (hml_lws_configure_macos_ca_file(), hml_runtime_pin_stdio(), lws_create_context(&info));
    if (!stream->context) {
        if (stream->post_body) free(stream->post_body);
        if (stream->content_type) free(stream->content_type);
        pthread_mutex_destroy(&stream->chunk_mutex);
        pthread_cond_destroy(&stream->chunk_cond);
        free(stream);
        hml_runtime_error("%s", hml_lws_context_error_message());
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
        if (stream->post_body) free(stream->post_body);
        if (stream->content_type) free(stream->content_type);
        pthread_mutex_destroy(&stream->chunk_mutex);
        pthread_cond_destroy(&stream->chunk_cond);
        free(stream);
        hml_runtime_error("Failed to connect for streaming");
    }

    // Wait for connection establishment
    int wait_iters = timeout_ms / 10;
    if (wait_iters < 1) wait_iters = 1;
    while (!stream->established && !stream->failed && !stream->complete && wait_iters-- > 0) {
        lws_service(stream->context, 10);
    }

    if (stream->failed || (!stream->established && wait_iters <= 0)) {
        lws_context_destroy(stream->context);
        if (stream->headers) free(stream->headers);
        if (stream->post_body) free(stream->post_body);
        if (stream->content_type) free(stream->content_type);
        pthread_mutex_destroy(&stream->chunk_mutex);
        pthread_cond_destroy(&stream->chunk_cond);
        free(stream);
        hml_runtime_error("Streaming HTTP connection failed or timed out");
    }

    stream->shutdown = 0;
    if (pthread_create(&stream->service_thread, NULL, hml_http_stream_service_thread, stream) != 0) {
        lws_context_destroy(stream->context);
        if (stream->headers) free(stream->headers);
        if (stream->post_body) free(stream->post_body);
        if (stream->content_type) free(stream->content_type);
        pthread_mutex_destroy(&stream->chunk_mutex);
        pthread_cond_destroy(&stream->chunk_cond);
        free(stream);
        hml_runtime_error("Failed to create stream service thread");
    }

    return hml_val_ptr(stream);
}

// __lws_http_stream_read(stream, timeout_ms): string|null
HmlValue hml_lws_http_stream_read(HmlValue stream_val, HmlValue timeout_val) {
    if (stream_val.type != HML_VAL_PTR) {
        hml_runtime_error("__lws_http_stream_read() expects ptr");
    }
    hml_http_stream_t *stream = (hml_http_stream_t *)stream_val.as.as_ptr;
    if (!stream) return hml_val_null();

    int timeout_ms = 30000;
    if (timeout_val.type == HML_VAL_I32) timeout_ms = timeout_val.as.as_i32;
    else if (timeout_val.type == HML_VAL_I64) timeout_ms = (int)timeout_val.as.as_i64;

    int iterations = timeout_ms / 10;
    if (iterations < 1) iterations = 1;

    while (iterations-- > 0) {
        pthread_mutex_lock(&stream->chunk_mutex);
        if (stream->chunk_head) {
            hml_http_stream_chunk_t *chunk = stream->chunk_head;
            stream->chunk_head = chunk->next;
            if (!stream->chunk_head) stream->chunk_tail = NULL;
            pthread_mutex_unlock(&stream->chunk_mutex);

            HmlValue result = hml_val_string(chunk->data);
            free(chunk->data);
            free(chunk);
            return result;
        }
        if (stream->complete || stream->failed) {
            pthread_mutex_unlock(&stream->chunk_mutex);
            return hml_val_null();
        }
        pthread_mutex_unlock(&stream->chunk_mutex);
        usleep(10000);
    }

    return hml_val_null();
}

// __lws_http_stream_read_binary(stream, timeout_ms): buffer|null
//
// Same poll loop as hml_lws_http_stream_read, but returns a buffer
// using the chunk's actual byte length instead of strlen-truncating
// at the first null byte. Required for binary downloads (GGUFs,
// images, archives) where 0x00 bytes are part of the payload, not
// terminators.
HmlValue hml_lws_http_stream_read_binary(HmlValue stream_val, HmlValue timeout_val) {
    if (stream_val.type != HML_VAL_PTR) {
        hml_runtime_error("__lws_http_stream_read_binary() expects ptr");
    }
    hml_http_stream_t *stream = (hml_http_stream_t *)stream_val.as.as_ptr;
    if (!stream) return hml_val_null();

    int timeout_ms = 30000;
    if (timeout_val.type == HML_VAL_I32) timeout_ms = timeout_val.as.as_i32;
    else if (timeout_val.type == HML_VAL_I64) timeout_ms = (int)timeout_val.as.as_i64;

    int iterations = timeout_ms / 10;
    if (iterations < 1) iterations = 1;

    while (iterations-- > 0) {
        pthread_mutex_lock(&stream->chunk_mutex);
        if (stream->chunk_head) {
            hml_http_stream_chunk_t *chunk = stream->chunk_head;
            stream->chunk_head = chunk->next;
            if (!stream->chunk_head) stream->chunk_tail = NULL;
            pthread_mutex_unlock(&stream->chunk_mutex);

            HmlValue buf = hml_val_buffer((int)chunk->len);
            if (buf.type == HML_VAL_BUFFER && buf.as.as_buffer && chunk->len > 0) {
                memcpy(buf.as.as_buffer->data, chunk->data, chunk->len);
            }
            free(chunk->data);
            free(chunk);
            return buf;
        }
        if (stream->complete || stream->failed) {
            pthread_mutex_unlock(&stream->chunk_mutex);
            return hml_val_null();
        }
        pthread_mutex_unlock(&stream->chunk_mutex);
        usleep(10000);
    }

    return hml_val_null();
}

// __lws_http_stream_status(stream): i32
HmlValue hml_lws_http_stream_status(HmlValue stream_val) {
    if (stream_val.type != HML_VAL_PTR) {
        hml_runtime_error("__lws_http_stream_status() expects ptr");
    }
    hml_http_stream_t *stream = (hml_http_stream_t *)stream_val.as.as_ptr;
    return hml_val_i32(stream ? stream->status_code : 0);
}

// __lws_http_stream_headers(stream): string
HmlValue hml_lws_http_stream_headers(HmlValue stream_val) {
    if (stream_val.type != HML_VAL_PTR) {
        hml_runtime_error("__lws_http_stream_headers() expects ptr");
    }
    hml_http_stream_t *stream = (hml_http_stream_t *)stream_val.as.as_ptr;
    if (!stream || !stream->headers) return hml_val_string("");
    return hml_val_string(stream->headers);
}

// __lws_http_stream_close(stream): null
HmlValue hml_lws_http_stream_close(HmlValue stream_val) {
    if (stream_val.type != HML_VAL_PTR) return hml_val_null();
    hml_http_stream_t *stream = (hml_http_stream_t *)stream_val.as.as_ptr;
    if (!stream) return hml_val_null();

    stream->shutdown = 1;
    stream->complete = 1;
    pthread_join(stream->service_thread, NULL);

    pthread_mutex_lock(&stream->chunk_mutex);
    hml_http_stream_chunk_t *chunk = stream->chunk_head;
    while (chunk) {
        hml_http_stream_chunk_t *next = chunk->next;
        free(chunk->data);
        free(chunk);
        chunk = next;
    }
    pthread_mutex_unlock(&stream->chunk_mutex);

    pthread_mutex_destroy(&stream->chunk_mutex);
    pthread_cond_destroy(&stream->chunk_cond);

    if (stream->context) lws_context_destroy(stream->context);
    if (stream->headers) free(stream->headers);
    if (stream->post_body) free(stream->post_body);
    if (stream->content_type) free(stream->content_type);
    free(stream);

    return hml_val_null();
}

// Builtin wrappers for streaming
HmlValue hml_builtin_lws_http_stream_start(HmlClosureEnv *env, HmlValue method, HmlValue url,
                                             HmlValue body, HmlValue content_type, HmlValue timeout) {
    (void)env;
    return hml_lws_http_stream_start(method, url, body, content_type, timeout);
}

HmlValue hml_builtin_lws_http_stream_read(HmlClosureEnv *env, HmlValue stream, HmlValue timeout) {
    (void)env;
    return hml_lws_http_stream_read(stream, timeout);
}

HmlValue hml_builtin_lws_http_stream_read_binary(HmlClosureEnv *env, HmlValue stream, HmlValue timeout) {
    (void)env;
    return hml_lws_http_stream_read_binary(stream, timeout);
}

HmlValue hml_builtin_lws_http_stream_status(HmlClosureEnv *env, HmlValue stream) {
    (void)env;
    return hml_lws_http_stream_status(stream);
}

HmlValue hml_builtin_lws_http_stream_headers(HmlClosureEnv *env, HmlValue stream) {
    (void)env;
    return hml_lws_http_stream_headers(stream);
}

HmlValue hml_builtin_lws_http_stream_close(HmlClosureEnv *env, HmlValue stream) {
    (void)env;
    return hml_lws_http_stream_close(stream);
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
    hml_lws_init_logging();
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
    hml_lws_configure_options(&info, ssl);
    info.port = CONTEXT_PORT_NO_LISTEN;

    static const struct lws_protocols ws_protocols[] = {
        { "ws", hml_ws_callback, 0, 4096, 0, NULL, 0 },
        { NULL, NULL, 0, 0, 0, NULL, 0 }
    };
    info.protocols = ws_protocols;

    conn->context = (hml_lws_configure_macos_ca_file(), hml_runtime_pin_stdio(), lws_create_context(&info));
    if (!conn->context) {
        free(conn);
        hml_runtime_error("%s", hml_lws_context_error_message());
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

// __lws_msg_binary(msg: ptr): buffer
// Returns the binary data from a WebSocket message as a buffer
HmlValue hml_lws_msg_binary(HmlValue msg_val) {
    if (msg_val.type != HML_VAL_PTR) {
        return hml_val_buffer(1);
    }

    hml_ws_message_t *msg = (hml_ws_message_t *)msg_val.as.as_ptr;
    if (!msg || !msg->data || msg->len == 0) {
        return hml_val_buffer(1);
    }

    HmlValue buf = hml_val_buffer((int32_t)msg->len);
    if (buf.type == HML_VAL_NULL) {
        return hml_val_buffer(1);
    }
    memcpy(buf.as.as_buffer->data, msg->data, msg->len);
    return buf;
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
    hml_lws_init_logging();
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
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT | LWS_SERVER_OPTION_ALLOW_LISTEN_SHARE;

    static const struct lws_protocols server_protocols[] = {
        { "ws", hml_ws_server_callback, sizeof(hml_ws_connection_t), 4096, 0, NULL, 0 },
        { NULL, NULL, 0, 0, 0, NULL, 0 }
    };
    info.protocols = server_protocols;

    server->context = (hml_lws_configure_macos_ca_file(), hml_runtime_pin_stdio(), lws_create_context(&info));
    if (!server->context) {
        pthread_mutex_destroy(&server->pending_mutex);
        free(server);
        hml_runtime_error("%s", hml_lws_context_error_message());
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
        // Check if server was closed before accessing its mutex
        if (server->closed) {
            return hml_val_null();
        }

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
        // Signal shutdown - accept() checks this flag before each iteration
        server->closed = 1;
        server->shutdown = 1;
        // Wait for any in-flight accept() or recv() calls to notice the
        // shutdown flag and exit.  recv() polls with 10ms sleeps and may
        // be in the middle of a 100ms timeout, so 200ms gives plenty of
        // margin.
        usleep(200000);
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

HmlValue hml_builtin_lws_msg_binary(HmlClosureEnv *env, HmlValue msg) {
    (void)env;
    return hml_lws_msg_binary(msg);
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

HmlValue hml_lws_http_get_with_headers(HmlValue url_val, HmlValue headers_val) {
    (void)url_val; (void)headers_val;
    hml_runtime_error("HTTP support not available (libwebsockets not installed)");
}

HmlValue hml_lws_http_post(HmlValue url_val, HmlValue body_val, HmlValue content_type_val) {
    (void)url_val; (void)body_val; (void)content_type_val;
    hml_runtime_error("HTTP support not available (libwebsockets not installed)");
}

HmlValue hml_lws_http_post_with_headers(HmlValue url_val, HmlValue body_val, HmlValue content_type_val, HmlValue headers_val) {
    (void)url_val; (void)body_val; (void)content_type_val; (void)headers_val;
    hml_runtime_error("HTTP support not available (libwebsockets not installed)");
}

HmlValue hml_lws_http_request(HmlValue method_val, HmlValue url_val, HmlValue body_val, HmlValue content_type_val) {
    (void)method_val; (void)url_val; (void)body_val; (void)content_type_val;
    hml_runtime_error("HTTP support not available (libwebsockets not installed)");
}

HmlValue hml_lws_http_request_with_headers(HmlValue method_val, HmlValue url_val, HmlValue body_val, HmlValue content_type_val, HmlValue headers_val) {
    (void)method_val; (void)url_val; (void)body_val; (void)content_type_val; (void)headers_val;
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

HmlValue hml_lws_http_post_timeout_with_headers(HmlValue url_val, HmlValue body_val, HmlValue content_type_val, HmlValue timeout_val, HmlValue headers_val) {
    (void)url_val; (void)body_val; (void)content_type_val; (void)timeout_val; (void)headers_val;
    hml_runtime_error("HTTP support not available (libwebsockets not installed)");
}

HmlValue hml_lws_http_request_timeout(HmlValue method_val, HmlValue url_val, HmlValue body_val, HmlValue content_type_val, HmlValue timeout_val) {
    (void)method_val; (void)url_val; (void)body_val; (void)content_type_val; (void)timeout_val;
    hml_runtime_error("HTTP support not available (libwebsockets not installed)");
}

HmlValue hml_lws_http_request_timeout_with_headers(HmlValue method_val, HmlValue url_val, HmlValue body_val, HmlValue content_type_val, HmlValue timeout_val, HmlValue headers_val) {
    (void)method_val; (void)url_val; (void)body_val; (void)content_type_val; (void)timeout_val; (void)headers_val;
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

HmlValue hml_builtin_lws_http_get_with_headers(HmlClosureEnv *env, HmlValue url, HmlValue headers) {
    (void)env;
    return hml_lws_http_get_with_headers(url, headers);
}

HmlValue hml_builtin_lws_http_post(HmlClosureEnv *env, HmlValue url, HmlValue body, HmlValue content_type) {
    (void)env;
    return hml_lws_http_post(url, body, content_type);
}

HmlValue hml_builtin_lws_http_post_with_headers(HmlClosureEnv *env, HmlValue url, HmlValue body, HmlValue content_type, HmlValue headers) {
    (void)env;
    return hml_lws_http_post_with_headers(url, body, content_type, headers);
}

HmlValue hml_builtin_lws_http_request(HmlClosureEnv *env, HmlValue method, HmlValue url, HmlValue body, HmlValue content_type) {
    (void)env;
    return hml_lws_http_request(method, url, body, content_type);
}

HmlValue hml_builtin_lws_http_request_with_headers(HmlClosureEnv *env, HmlValue method, HmlValue url, HmlValue body, HmlValue content_type, HmlValue headers) {
    (void)env;
    return hml_lws_http_request_with_headers(method, url, body, content_type, headers);
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

HmlValue hml_lws_msg_binary(HmlValue msg_val) {
    (void)msg_val;
    return hml_val_buffer(1);
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

HmlValue hml_builtin_lws_msg_binary(HmlClosureEnv *env, HmlValue msg) {
    (void)env;
    return hml_lws_msg_binary(msg);
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

// Streaming HTTP stubs
HmlValue hml_lws_http_stream_start(HmlValue method_val, HmlValue url_val,
                                    HmlValue body_val, HmlValue content_type_val,
                                    HmlValue timeout_val) {
    (void)method_val; (void)url_val; (void)body_val; (void)content_type_val; (void)timeout_val;
    hml_runtime_error("Streaming HTTP not available (libwebsockets not installed)");
}

HmlValue hml_lws_http_stream_read(HmlValue stream_val, HmlValue timeout_val) {
    (void)stream_val; (void)timeout_val;
    hml_runtime_error("Streaming HTTP not available (libwebsockets not installed)");
}

HmlValue hml_lws_http_stream_status(HmlValue stream_val) {
    (void)stream_val;
    return hml_val_i32(0);
}

HmlValue hml_lws_http_stream_headers(HmlValue stream_val) {
    (void)stream_val;
    return hml_val_string("");
}

HmlValue hml_lws_http_stream_close(HmlValue stream_val) {
    (void)stream_val;
    return hml_val_null();
}

HmlValue hml_builtin_lws_http_stream_start(HmlClosureEnv *env, HmlValue method, HmlValue url,
                                             HmlValue body, HmlValue content_type, HmlValue timeout) {
    (void)env;
    return hml_lws_http_stream_start(method, url, body, content_type, timeout);
}

HmlValue hml_builtin_lws_http_stream_read(HmlClosureEnv *env, HmlValue stream, HmlValue timeout) {
    (void)env;
    return hml_lws_http_stream_read(stream, timeout);
}

HmlValue hml_builtin_lws_http_stream_read_binary(HmlClosureEnv *env, HmlValue stream, HmlValue timeout) {
    (void)env;
    return hml_lws_http_stream_read_binary(stream, timeout);
}

HmlValue hml_builtin_lws_http_stream_status(HmlClosureEnv *env, HmlValue stream) {
    (void)env;
    return hml_lws_http_stream_status(stream);
}

HmlValue hml_builtin_lws_http_stream_headers(HmlClosureEnv *env, HmlValue stream) {
    (void)env;
    return hml_lws_http_stream_headers(stream);
}

HmlValue hml_builtin_lws_http_stream_close(HmlClosureEnv *env, HmlValue stream) {
    (void)env;
    return hml_lws_http_stream_close(stream);
}

#endif  // HML_HAVE_LIBWEBSOCKETS

#endif // !__EMSCRIPTEN__
