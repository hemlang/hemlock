// WebSocket client/server builtins for Hemlock (extracted from websockets.c)

#include "websockets_internal.h"
#include <stdatomic.h>

#ifndef HAVE_LIBWEBSOCKETS
#  define HAVE_LIBWEBSOCKETS 0
#endif

#if HAVE_LIBWEBSOCKETS

#include <libwebsockets.h>
#include <pthread.h>

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

    // Wait for any in-flight accept() or recv() calls to notice the
    // shutdown flag and exit.  recv() polls with 10ms sleeps and may
    // be in the middle of a 100ms timeout, so 200ms gives plenty of
    // margin.  The Hemlock-level Server.close() should already join
    // spawned recv tasks before reaching this point, but we keep a
    // safety sleep to guard against direct __lws_ws_server_close use.
    usleep(200000);

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

    conn->context = (lws_configure_macos_ca_file(), lws_create_context(&info));
    if (!conn->context) {
        free(conn);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string(lws_context_error_message());
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

    // Wait for connection (timeout 10 seconds).
    //
    // Paced with usleep, like ws_server_accept below, rather than by counting
    // lws_service() calls: lws_service()'s timeout argument has been ignored
    // since lws 3.2, and under the libuv event loop (what MSYS2's lws 4.5 is
    // built with) it can return immediately. Counting 100 iterations of it was
    // therefore not a 10-second timeout but a busy-spin that could expire in
    // microseconds — losing the race on the first, coldest connect in a
    // process while every later one succeeded.
    int wait_ms = HML_WS_CONNECT_TIMEOUT_MS;
    while (wait_ms > 0 && !conn->closed && !conn->failed && !conn->established) {
        lws_service(conn->context, 0);
        usleep(HML_WS_CONNECT_POLL_US);
        wait_ms -= HML_WS_CONNECT_POLL_US / 1000;
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
    pthread_attr_t conn_attr;
    pthread_attr_init(&conn_attr);
    pthread_attr_setstacksize(&conn_attr, HML_THREAD_STACK_SIZE);
    int conn_rc = pthread_create(&conn->service_thread, &conn_attr, ws_service_thread, conn);
    pthread_attr_destroy(&conn_attr);
    if (conn_rc != 0) {
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

    server->context = (lws_configure_macos_ca_file(), lws_create_context(&info));
    if (!server->context) {
        pthread_mutex_destroy(&server->pending_mutex);
        free(server);
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string(lws_context_error_message());
        return val_null();
    }

    server->shutdown = 0;
    pthread_attr_t srv_attr;
    pthread_attr_init(&srv_attr);
    pthread_attr_setstacksize(&srv_attr, HML_THREAD_STACK_SIZE);
    int srv_rc = pthread_create(&server->service_thread, &srv_attr, ws_server_service_thread, server);
    pthread_attr_destroy(&srv_attr);
    if (srv_rc != 0) {
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

void websocket_free(WebSocketHandle *ws) {
    if (!ws) return;
    if (ws->url) free(ws->url);
    if (ws->host) free(ws->host);
    free(ws);
}

Value get_websocket_property(WebSocketHandle *ws, const char *property, ExecutionContext *ctx) {
    (void)ws; (void)property; (void)ctx;
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

#endif  // HAVE_LIBWEBSOCKETS
