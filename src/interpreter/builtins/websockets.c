// libwebsockets builtins for Hemlock
// Provides both HTTP and WebSocket functionality as static builtins
// Compiles with stubs if libwebsockets.h is not available

#define _DEFAULT_SOURCE  // For usleep()

#include "internal.h"

// Check if libwebsockets is available
// HAVE_LIBWEBSOCKETS is defined by Makefile if pkg-config finds libwebsockets
// or if /usr/include/libwebsockets.h exists
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
} http_response_t;

static int http_callback(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len) {
    http_response_t *resp = (http_response_t *)user;

    switch (reason) {
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            fprintf(stderr, "HTTP connection error: %s\n", in ? (char *)in : "unknown");
            resp->failed = 1;
            resp->complete = 1;
            break;

        case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP:
            resp->status_code = lws_http_client_http_response(wsi);
            break;

        case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ:
            // Accumulate response body
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
            return 0;

        case LWS_CALLBACK_COMPLETED_CLIENT_HTTP:
            resp->complete = 1;
            break;

        case LWS_CALLBACK_CLOSED_CLIENT_HTTP:
            resp->complete = 1;
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
    strcpy(path, "/");

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

// __lws_http_get(url: string): ptr
Value builtin_lws_http_get(Value *args, int num_args, ExecutionContext *ctx) {
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

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.port = CONTEXT_PORT_NO_LISTEN;

    static const struct lws_protocols protocols[] = {
        { "http", http_callback, 0, 4096, 0, NULL, 0 },
        { NULL, NULL, 0, 0, 0, NULL, 0 }
    };
    info.protocols = protocols;

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
    connect_info.protocol = protocols[0].name;
    connect_info.userdata = resp;

    struct lws *wsi;
    connect_info.pwsi = &wsi;

    if (ssl) {
        connect_info.ssl_connection = LCCSCF_USE_SSL |
                                       LCCSCF_ALLOW_SELFSIGNED |
                                       LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
    }

    if (!lws_client_connect_via_info(&connect_info)) {
        lws_context_destroy(context);
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

    lws_context_destroy(context);

    if (resp->failed || timeout <= 0) {
        free(resp->body);
        free(resp);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("HTTP request failed or timed out");
        return val_null();
    }

    return val_ptr(resp);
}

// __lws_http_post(url: string, body: string, content_type: string): ptr
Value builtin_lws_http_post(Value *args, int num_args, ExecutionContext *ctx) {
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

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.port = CONTEXT_PORT_NO_LISTEN;

    static const struct lws_protocols protocols[] = {
        { "http", http_callback, 0, 4096, 0, NULL, 0 },
        { NULL, NULL, 0, 0, 0, NULL, 0 }
    };
    info.protocols = protocols;

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
    connect_info.protocol = protocols[0].name;
    connect_info.userdata = resp;

    struct lws *wsi;
    connect_info.pwsi = &wsi;

    if (ssl) {
        connect_info.ssl_connection = LCCSCF_USE_SSL |
                                       LCCSCF_ALLOW_SELFSIGNED |
                                       LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
    }

    // Note: POST body handling in libwebsockets is complex
    // This is a simplified version - real implementation would need WRITABLE callback
    (void)post_body;  // Suppress warning - not fully implemented yet
    (void)content_type;

    if (!lws_client_connect_via_info(&connect_info)) {
        lws_context_destroy(context);
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

    lws_context_destroy(context);

    if (resp->failed || timeout <= 0) {
        free(resp->body);
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

    // Headers not implemented yet
    return val_string("");
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
        free(resp);
    }

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

    strcpy(path, "/");

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
            port = atoi(colon + 1);
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
            port = atoi(colon + 1);
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
        connect_info.ssl_connection = LCCSCF_USE_SSL |
                                       LCCSCF_ALLOW_SELFSIGNED |
                                       LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
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

    return val_ptr(conn);
}

// __lws_ws_send_text(conn: ptr, text: string): i32
Value builtin_lws_ws_send_text(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_ws_send_text() expects 2 arguments");
        return val_null();
    }

    if (args[0].type != VAL_PTR || args[1].type != VAL_STRING) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_ws_send_text() expects (ptr, string)");
        return val_null();
    }

    ws_connection_t *conn = (ws_connection_t *)args[0].as.as_ptr;
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

// __lws_ws_recv(conn: ptr, timeout_ms: i32): ptr
Value builtin_lws_ws_recv(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_ws_recv() expects 2 arguments");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_ws_recv() expects ptr as first argument");
        return val_null();
    }

    ws_connection_t *conn = (ws_connection_t *)args[0].as.as_ptr;
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

// __lws_ws_close(conn: ptr): null
Value builtin_lws_ws_close(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_ws_close() expects 1 argument");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        return val_null();
    }

    ws_connection_t *conn = (ws_connection_t *)args[0].as.as_ptr;
    if (conn) {
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

    return val_null();
}

// __lws_ws_is_closed(conn: ptr): i32
Value builtin_lws_ws_is_closed(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_ws_is_closed() expects 1 argument");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        return val_i32(1);
    }

    ws_connection_t *conn = (ws_connection_t *)args[0].as.as_ptr;
    return val_i32(conn ? conn->closed : 1);
}

// __lws_ws_server_create(host: string, port: i32): ptr
Value builtin_lws_ws_server_create(Value *args, int num_args, ExecutionContext *ctx) {
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
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

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

    return val_ptr(server);
}

// __lws_ws_server_accept(server: ptr, timeout_ms: i32): ptr
Value builtin_lws_ws_server_accept(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_ws_server_accept() expects 2 arguments");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_ws_server_accept() expects ptr server");
        return val_null();
    }

    ws_server_t *server = (ws_server_t *)args[0].as.as_ptr;
    if (!server || server->closed) {
        return val_null();
    }

    int timeout_ms = value_to_int(args[1]);
    int iterations = timeout_ms > 0 ? (timeout_ms / 10) : -1;

    while (iterations != 0) {
        pthread_mutex_lock(&server->pending_mutex);
        ws_connection_t *conn = NULL;
        if (server->pending_wsi) {
            conn = server->pending_conn;
            server->pending_wsi = NULL;
            server->pending_conn = NULL;
        }
        pthread_mutex_unlock(&server->pending_mutex);

        if (conn) {
            return val_ptr(conn);
        }

        usleep(10000);  // 10ms sleep
        if (iterations > 0) iterations--;
    }

    return val_null();
}

// __lws_ws_server_close(server: ptr): null
Value builtin_lws_ws_server_close(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("__lws_ws_server_close() expects 1 argument");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        return val_null();
    }

    ws_server_t *server = (ws_server_t *)args[0].as.as_ptr;
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

    return val_null();
}

#else  // !HAVE_LIBWEBSOCKETS

// ========== STUB IMPLEMENTATIONS (libwebsockets not available) ==========

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

Value builtin_lws_response_headers(Value *args, int num_args, ExecutionContext *ctx) {
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

#endif  // HAVE_LIBWEBSOCKETS
