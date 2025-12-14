// libwebsockets FFI wrapper for Hemlock
// Provides both HTTP and WebSocket functionality in a single library

#define _DEFAULT_SOURCE  // For usleep()

// Debug output - set to 1 to enable WebSocket debugging
#define LWS_DEBUG 0
#if LWS_DEBUG
#define DEBUG_PRINT(...) fprintf(stderr, __VA_ARGS__)
#else
#define DEBUG_PRINT(...) ((void)0)
#endif

#include <libwebsockets.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
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
// Buffers: host must be 256 bytes, path must be 512 bytes
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
            // Has port
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
            // Has path, no port
            size_t host_len = slash - rest;
            if (host_len >= 256) return -1;
            strncpy(host, rest, host_len);
            host[host_len] = '\0';
            strncpy(path, slash, 511);
            path[511] = '\0';
        } else {
            // Just host
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
        return -1;  // Invalid URL
    }

    return 0;
}

// HTTP GET request
http_response_t* lws_http_get(const char *url) {
    struct lws_context_creation_info info;
    struct lws_client_connect_info connect_info;
    struct lws_context *context;
    http_response_t *resp;
    struct lws *wsi;
    char host[256];
    char path[512];
    int port, ssl;

    // Parse URL
    if (parse_url(url, host, &port, path, &ssl) < 0) {
        return NULL;
    }

    // Allocate response structure
    resp = calloc(1, sizeof(http_response_t));
    if (!resp) return NULL;

    resp->body_capacity = 4096;
    resp->body = malloc(resp->body_capacity);
    if (!resp->body) {
        free(resp);
        return NULL;
    }
    resp->body[0] = '\0';

    // Create context
    memset(&info, 0, sizeof(info));
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.port = CONTEXT_PORT_NO_LISTEN;

    static const struct lws_protocols protocols[] = {
        { "http", http_callback, 0, 4096, 0, NULL, 0 },
        { NULL, NULL, 0, 0, 0, NULL, 0 }
    };
    info.protocols = protocols;

    context = lws_create_context(&info);
    if (!context) {
        free(resp->body);
        free(resp);
        return NULL;
    }

    // Connect
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
    connect_info.pwsi = &wsi;

    if (ssl) {
        // SECURITY: Enable SSL with proper certificate validation
        // Removed LCCSCF_ALLOW_SELFSIGNED and LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK
        // to prevent MITM attacks
        connect_info.ssl_connection = LCCSCF_USE_SSL;
    }

    if (!lws_client_connect_via_info(&connect_info)) {
        lws_context_destroy(context);
        free(resp->body);
        free(resp);
        return NULL;
    }

    // Event loop (timeout after 30 seconds)
    int timeout = 3000;  // 30 seconds (100 * 10ms)
    while (!resp->complete && !resp->failed && timeout-- > 0) {
        lws_service(context, 10);
    }

    lws_context_destroy(context);

    if (resp->failed || timeout <= 0) {
        free(resp->body);
        free(resp);
        return NULL;
    }

    return resp;
}

// HTTP POST request
http_response_t* lws_http_post(const char *url, const char *body, const char *content_type) {
    struct lws_context_creation_info info;
    struct lws_client_connect_info connect_info;
    struct lws_context *context;
    http_response_t *resp;
    struct lws *wsi;
    char host[256];
    char path[512];
    int port, ssl;

    // Parse URL
    if (parse_url(url, host, &port, path, &ssl) < 0) {
        return NULL;
    }

    // Allocate response structure
    resp = calloc(1, sizeof(http_response_t));
    if (!resp) return NULL;

    resp->body_capacity = 4096;
    resp->body = malloc(resp->body_capacity);
    if (!resp->body) {
        free(resp);
        return NULL;
    }
    resp->body[0] = '\0';

    // Create context
    memset(&info, 0, sizeof(info));
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.port = CONTEXT_PORT_NO_LISTEN;

    static const struct lws_protocols protocols[] = {
        { "http", http_callback, 0, 4096, 0, NULL, 0 },
        { NULL, NULL, 0, 0, 0, NULL, 0 }
    };
    info.protocols = protocols;

    context = lws_create_context(&info);
    if (!context) {
        free(resp->body);
        free(resp);
        return NULL;
    }

    // Build POST headers
    char headers[1024];
    snprintf(headers, sizeof(headers),
             "POST %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n\r\n%s",
             path, host,
             content_type ? content_type : "application/x-www-form-urlencoded",
             body ? strlen(body) : 0,
             body ? body : "");

    // Connect
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
    connect_info.pwsi = &wsi;

    if (ssl) {
        // SECURITY: Enable SSL with proper certificate validation
        // Removed LCCSCF_ALLOW_SELFSIGNED and LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK
        // to prevent MITM attacks
        connect_info.ssl_connection = LCCSCF_USE_SSL;
    }

    if (!lws_client_connect_via_info(&connect_info)) {
        lws_context_destroy(context);
        free(resp->body);
        free(resp);
        return NULL;
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
        return NULL;
    }

    return resp;
}

// Generic HTTP request with method parameter (for PUT, DELETE, PATCH, etc.)
http_response_t* lws_http_request(const char *method, const char *url, const char *body, const char *content_type) {
    struct lws_context_creation_info info;
    struct lws_client_connect_info connect_info;
    struct lws_context *context;
    http_response_t *resp;
    struct lws *wsi;
    char host[256];
    char path[512];
    int port, ssl;

    // Parse URL
    if (parse_url(url, host, &port, path, &ssl) < 0) {
        return NULL;
    }

    // Allocate response structure
    resp = calloc(1, sizeof(http_response_t));
    if (!resp) return NULL;

    resp->body_capacity = 4096;
    resp->body = malloc(resp->body_capacity);
    if (!resp->body) {
        free(resp);
        return NULL;
    }
    resp->body[0] = '\0';

    // Create context
    memset(&info, 0, sizeof(info));
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.port = CONTEXT_PORT_NO_LISTEN;

    static const struct lws_protocols protocols[] = {
        { "http", http_callback, 0, 4096, 0, NULL, 0 },
        { NULL, NULL, 0, 0, 0, NULL, 0 }
    };
    info.protocols = protocols;

    context = lws_create_context(&info);
    if (!context) {
        free(resp->body);
        free(resp);
        return NULL;
    }

    // Connect
    memset(&connect_info, 0, sizeof(connect_info));
    connect_info.context = context;
    connect_info.address = host;
    connect_info.port = port;
    connect_info.path = path;
    connect_info.host = host;
    connect_info.origin = host;
    connect_info.method = method;  // Use the provided method
    connect_info.protocol = protocols[0].name;
    connect_info.userdata = resp;
    connect_info.pwsi = &wsi;

    if (ssl) {
        // SECURITY: Enable SSL with proper certificate validation
        // Removed LCCSCF_ALLOW_SELFSIGNED and LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK
        // to prevent MITM attacks
        connect_info.ssl_connection = LCCSCF_USE_SSL;
    }

    if (!lws_client_connect_via_info(&connect_info)) {
        lws_context_destroy(context);
        free(resp->body);
        free(resp);
        return NULL;
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
        return NULL;
    }

    return resp;
}

// Free HTTP response
void lws_http_response_free(http_response_t *resp) {
    if (resp) {
        if (resp->body) free(resp->body);
        free(resp);
    }
}

// Response accessors
int lws_response_status(http_response_t *resp) {
    return resp ? resp->status_code : 0;
}

const char* lws_response_body(http_response_t *resp) {
    return (resp && resp->body) ? resp->body : "";
}

const char* lws_response_headers(http_response_t *resp) {
    return "";  // Headers not yet implemented
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
    int established;  // 1 when connection is fully established (for client)
    char *send_buffer;
    size_t send_len;
    int send_pending;
    pthread_t service_thread;
    volatile int shutdown;
    int has_own_thread;  // 1 if this connection started its own service thread
    int owns_memory;     // 1 if we allocated this struct and should free it
} ws_connection_t;

// Service thread function - runs continuously to service the event loop
static void* ws_service_thread(void *arg) {
    ws_connection_t *conn = (ws_connection_t *)arg;

    while (!conn->shutdown) {
        lws_service(conn->context, 50);
    }

    return NULL;
}

static int ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                       void *user, void *in, size_t len) {
    ws_connection_t *conn = (ws_connection_t *)user;

    DEBUG_PRINT("[DEBUG] ws_callback: reason=%d, conn=%p, wsi=%p\n", reason, (void*)conn, (void*)wsi);

    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            DEBUG_PRINT("[DEBUG] CLIENT_ESTABLISHED: conn=%p\n", (void*)conn);
            if (conn) {
                conn->wsi = wsi;
                conn->established = 1;  // Mark connection as fully established
            }
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE:
            DEBUG_PRINT("[DEBUG] CLIENT_RECEIVE: len=%zu, conn=%p\n", len, (void*)conn);
            // Queue received message
            if (conn) {
                ws_message_t *msg = malloc(sizeof(ws_message_t));
                if (!msg) break;

                msg->len = len;
                // Allocate len+1 to ensure we can null-terminate for text messages
                msg->data = malloc(len + 1);
                if (!msg->data) {
                    free(msg);
                    break;
                }
                memcpy(msg->data, in, len);
                msg->data[len] = '\0';  // Null-terminate for text messages
                msg->is_binary = lws_frame_is_binary(wsi);
                msg->next = NULL;

                if (conn->msg_queue_tail) {
                    conn->msg_queue_tail->next = msg;
                } else {
                    conn->msg_queue_head = msg;
                }
                conn->msg_queue_tail = msg;
                DEBUG_PRINT("[DEBUG] CLIENT_RECEIVE: message queued\n");
            }
            break;

        case LWS_CALLBACK_CLIENT_WRITEABLE:
            DEBUG_PRINT("[DEBUG] CLIENT_WRITEABLE: conn=%p, send_pending=%d\n",
                    (void*)conn, conn ? conn->send_pending : -1);
            if (conn && conn->send_pending && conn->send_buffer) {
                DEBUG_PRINT("[DEBUG] CLIENT_WRITEABLE: writing %zu bytes\n", conn->send_len);
                int flags = LWS_WRITE_TEXT;
                lws_write(wsi, (unsigned char *)conn->send_buffer + LWS_PRE,
                         conn->send_len, flags);
                free(conn->send_buffer);
                conn->send_buffer = NULL;
                conn->send_pending = 0;
                DEBUG_PRINT("[DEBUG] CLIENT_WRITEABLE: write complete\n");
            }
            break;

        case LWS_CALLBACK_CLOSED:
            DEBUG_PRINT("[DEBUG] CLOSED: conn=%p\n", (void*)conn);
            if (conn) {
                conn->closed = 1;
            }
            break;

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            fprintf(stderr, "WebSocket connection error: %s\n", in ? (char *)in : "unknown");
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

// WebSocket connect
ws_connection_t* lws_ws_connect(const char *url) {
    struct lws_context_creation_info info;
    struct lws_client_connect_info connect_info;
    ws_connection_t *conn;
    char host[256];
    char path[512];
    int port, ssl;

    // Parse WebSocket URL
    ssl = 0;
    port = 80;
    // SECURITY: Use safe string initialization instead of strcpy
    path[0] = '/';
    path[1] = '\0';

    if (strncmp(url, "wss://", 6) == 0) {
        ssl = 1;
        port = 443;
        const char *rest = url + 6;
        const char *slash = strchr(rest, '/');
        const char *colon = strchr(rest, ':');

        // Handle port number
        if (colon && (!slash || colon < slash)) {
            // Has port number
            size_t host_len = colon - rest;
            if (host_len >= 256) return NULL;
            strncpy(host, rest, host_len);
            host[host_len] = '\0';
            port = atoi(colon + 1);

            if (slash) {
                strncpy(path, slash, 511);
                path[511] = '\0';
            }
        } else if (slash) {
            // Has path, no port
            size_t host_len = slash - rest;
            if (host_len >= 256) return NULL;
            strncpy(host, rest, host_len);
            host[host_len] = '\0';
            strncpy(path, slash, 511);
            path[511] = '\0';
        } else {
            // Just hostname, no port or path
            strncpy(host, rest, 255);
            host[255] = '\0';
        }
    } else if (strncmp(url, "ws://", 5) == 0) {
        const char *rest = url + 5;
        const char *slash = strchr(rest, '/');
        const char *colon = strchr(rest, ':');

        // Handle port number
        if (colon && (!slash || colon < slash)) {
            // Has port number
            size_t host_len = colon - rest;
            if (host_len >= 256) return NULL;
            strncpy(host, rest, host_len);
            host[host_len] = '\0';
            port = atoi(colon + 1);

            if (slash) {
                strncpy(path, slash, 511);
                path[511] = '\0';
            }
        } else if (slash) {
            // Has path, no port
            size_t host_len = slash - rest;
            if (host_len >= 256) return NULL;
            strncpy(host, rest, host_len);
            host[host_len] = '\0';
            strncpy(path, slash, 511);
            path[511] = '\0';
        } else {
            // Just hostname, no port or path
            strncpy(host, rest, 255);
            host[255] = '\0';
        }
    } else {
        return NULL;
    }

    conn = calloc(1, sizeof(ws_connection_t));
    if (!conn) return NULL;
    conn->owns_memory = 1;  // Client connections own their memory

    // Create context
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
        return NULL;
    }

    // Connect
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
        return NULL;
    }

    // Wait for connection to be established (timeout 10 seconds)
    int timeout = 100;
    while (timeout-- > 0 && !conn->closed && !conn->failed && !conn->established) {
        lws_service(conn->context, 100);  // 100ms per iteration
    }

    if (conn->failed || conn->closed || !conn->established) {
        lws_context_destroy(conn->context);
        free(conn);
        return NULL;
    }

    // Start service thread
    conn->shutdown = 0;
    conn->has_own_thread = 1;
    conn->owns_memory = 1;
    if (pthread_create(&conn->service_thread, NULL, ws_service_thread, conn) != 0) {
        fprintf(stderr, "Failed to create service thread\n");
        lws_context_destroy(conn->context);
        free(conn);
        return NULL;
    }

    return conn;
}

// WebSocket send text
int lws_ws_send_text(ws_connection_t *conn, const char *text) {
    if (!conn || conn->closed) return -1;

    size_t len = strlen(text);
    DEBUG_PRINT("[DEBUG] send_text called, len=%zu, text='%s'\n", len, text);

    // Allocate buffer with LWS_PRE padding
    unsigned char *buf = malloc(LWS_PRE + len);
    if (!buf) return -1;

    memcpy(buf + LWS_PRE, text, len);

    DEBUG_PRINT("[DEBUG] Calling lws_write directly, wsi=%p\n", (void*)conn->wsi);

    // Write directly - libwebsockets should be thread-safe for writes
    int written = lws_write(conn->wsi, buf + LWS_PRE, len, LWS_WRITE_TEXT);

    free(buf);

    DEBUG_PRINT("[DEBUG] lws_write returned %d\n", written);

    if (written < 0) {
        return -1;
    }

    // Wake up service thread to actually send the data
    lws_cancel_service(conn->context);

    return 0;
}

// WebSocket send binary
int lws_ws_send_binary(ws_connection_t *conn, const unsigned char *data, size_t len) {
    if (!conn || conn->closed) return -1;

    conn->send_buffer = malloc(LWS_PRE + len);
    if (!conn->send_buffer) return -1;

    memcpy(conn->send_buffer + LWS_PRE, data, len);
    conn->send_len = len;
    conn->send_pending = 1;

    lws_callback_on_writable(conn->wsi);

    // Service thread will handle the send

    return 0;
}

// WebSocket receive (blocking with timeout)
ws_message_t* lws_ws_recv(ws_connection_t *conn, int timeout_ms) {
    if (!conn || conn->closed) {
        DEBUG_PRINT("[DEBUG] recv: conn=%p, closed=%d\n", (void*)conn, conn ? conn->closed : -1);
        return NULL;
    }

    DEBUG_PRINT("[DEBUG] recv: waiting for message, timeout=%d\n", timeout_ms);
    int iterations = timeout_ms > 0 ? (timeout_ms / 10) : -1;

    while (iterations != 0) {
        // Check if we have queued messages
        if (conn->msg_queue_head) {
            ws_message_t *msg = conn->msg_queue_head;
            conn->msg_queue_head = msg->next;
            if (!conn->msg_queue_head) {
                conn->msg_queue_tail = NULL;
            }
            msg->next = NULL;
            DEBUG_PRINT("[DEBUG] recv: found message, data='%s'\n", (char*)msg->data);
            return msg;
        }

        // Service thread handles receiving, we just wait
        usleep(10000);  // 10ms sleep

        if (conn->closed) return NULL;
        if (iterations > 0) iterations--;
    }

    DEBUG_PRINT("[DEBUG] recv: timeout\n");
    return NULL;
}

// Message accessors
int lws_msg_type(ws_message_t *msg) {
    if (!msg) return 0;
    return msg->is_binary ? 2 : 1;  // 1=text, 2=binary
}

const char* lws_msg_text(ws_message_t *msg) {
    if (!msg || !msg->data) return "";
    // Data is already null-terminated when allocated
    return (const char *)msg->data;
}

const unsigned char* lws_msg_binary(ws_message_t *msg) {
    return msg ? msg->data : NULL;
}

int lws_msg_len(ws_message_t *msg) {
    return msg ? (int)msg->len : 0;
}

void lws_msg_free(ws_message_t *msg) {
    if (msg) {
        if (msg->data) free(msg->data);
        free(msg);
    }
}

// WebSocket close
void lws_ws_close(ws_connection_t *conn) {
    if (conn) {
        // Mark as closed
        conn->closed = 1;

        // Signal service thread to stop (if it has one)
        conn->shutdown = 1;

        // Wait for service thread to finish (only if this connection has its own thread)
        if (conn->has_own_thread) {
            pthread_join(conn->service_thread, NULL);
        }

        // Free queued messages
        ws_message_t *msg = conn->msg_queue_head;
        while (msg) {
            ws_message_t *next = msg->next;
            lws_msg_free(msg);
            msg = next;
        }

        if (conn->send_buffer) {
            free(conn->send_buffer);
            conn->send_buffer = NULL;
        }

        // Only destroy context if this connection owns it (has its own thread)
        // Server-side connections share the server's context
        if (conn->has_own_thread && conn->context) {
            lws_context_destroy(conn->context);
        }

        // Only free the struct if we allocated it
        // Server-side connections are allocated by libwebsockets
        if (conn->owns_memory) {
            free(conn);
        }
    }
}

// Check if closed
int lws_ws_is_closed(ws_connection_t *conn) {
    return conn ? conn->closed : 1;
}

// ========== WEBSOCKET SERVER SUPPORT ==========

typedef struct ws_server {
    struct lws_context *context;
    struct lws *pending_wsi;
    ws_connection_t *pending_conn;  // Store the actual connection struct
    int port;
    int closed;
    pthread_t service_thread;
    volatile int shutdown;
    pthread_mutex_t pending_mutex;  // Protect pending_wsi and pending_conn
} ws_server_t;

// Service thread function for server - runs continuously to service the event loop
static void* ws_server_service_thread(void *arg) {
    ws_server_t *server = (ws_server_t *)arg;

    while (!server->shutdown) {
        lws_service(server->context, 50);
    }

    return NULL;
}

static int ws_server_callback(struct lws *wsi, enum lws_callback_reasons reason,
                              void *user, void *in, size_t len) {
    ws_server_t *server = (ws_server_t *)lws_context_user(lws_get_context(wsi));
    ws_connection_t *conn = (ws_connection_t *)user;

    DEBUG_PRINT("[DEBUG] ws_server_callback: reason=%d, conn=%p, wsi=%p\n", reason, (void*)conn, (void*)wsi);

    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            fprintf(stderr, "WebSocket server connection established\n");
            // Initialize connection state
            if (conn) {
                conn->wsi = wsi;
                conn->context = lws_get_context(wsi);
                conn->shutdown = 0;
                conn->has_own_thread = 0;  // Server's thread handles this
                conn->owns_memory = 0;     // libwebsockets allocated this
            }
            // Store the wsi AND conn for accept() to pick up
            if (server) {
                pthread_mutex_lock(&server->pending_mutex);
                if (!server->pending_wsi) {
                    server->pending_wsi = wsi;
                    server->pending_conn = conn;  // Store the actual conn struct
                }
                pthread_mutex_unlock(&server->pending_mutex);
            }
            break;

        case LWS_CALLBACK_RECEIVE:
            // Queue received message
            DEBUG_PRINT("[DEBUG] SERVER RECEIVE callback, len=%zu, conn=%p\n", len, (void*)conn);
            if (conn) {
                ws_message_t *msg = malloc(sizeof(ws_message_t));
                if (!msg) break;

                msg->len = len;
                // Allocate len+1 to ensure we can null-terminate for text messages
                msg->data = malloc(len + 1);
                if (!msg->data) {
                    free(msg);
                    break;
                }
                memcpy(msg->data, in, len);
                msg->data[len] = '\0';  // Null-terminate for text messages
                msg->is_binary = lws_frame_is_binary(wsi);
                msg->next = NULL;

                if (conn->msg_queue_tail) {
                    conn->msg_queue_tail->next = msg;
                } else {
                    conn->msg_queue_head = msg;
                }
                conn->msg_queue_tail = msg;
                DEBUG_PRINT("[DEBUG] Message queued, data='%s'\n", (char*)msg->data);
            }
            break;

        case LWS_CALLBACK_SERVER_WRITEABLE:
            DEBUG_PRINT("[DEBUG] SERVER_WRITEABLE callback triggered\n");
            if (conn && conn->send_pending && conn->send_buffer) {
                DEBUG_PRINT("[DEBUG] Writing %zu bytes\n", conn->send_len);
                int flags = LWS_WRITE_TEXT;
                lws_write(wsi, (unsigned char *)conn->send_buffer + LWS_PRE,
                         conn->send_len, flags);
                free(conn->send_buffer);
                conn->send_buffer = NULL;
                conn->send_pending = 0;
                DEBUG_PRINT("[DEBUG] Write complete\n");
            } else {
                DEBUG_PRINT("[DEBUG] No pending send or buffer\n");
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

// Create WebSocket server
ws_server_t* lws_ws_server_create(const char *host, int port) {
    struct lws_context_creation_info info;
    ws_server_t *server;

    server = calloc(1, sizeof(ws_server_t));
    if (!server) return NULL;

    server->port = port;
    pthread_mutex_init(&server->pending_mutex, NULL);

    // Create context
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
        free(server);
        return NULL;
    }

    // Start service thread
    server->shutdown = 0;
    if (pthread_create(&server->service_thread, NULL, ws_server_service_thread, server) != 0) {
        fprintf(stderr, "Failed to create server service thread\n");
        lws_context_destroy(server->context);
        free(server);
        return NULL;
    }

    return server;
}

// Accept WebSocket connection (blocking with timeout)
ws_connection_t* lws_ws_server_accept(ws_server_t *server, int timeout_ms) {
    if (!server || server->closed) return NULL;

    int iterations = timeout_ms > 0 ? (timeout_ms / 10) : -1;

    while (iterations != 0) {
        // Check for pending connection (service thread handles the event loop)
        pthread_mutex_lock(&server->pending_mutex);
        ws_connection_t *conn = NULL;
        if (server->pending_wsi) {
            // Return the connection struct that was created by libwebsockets
            // and initialized in the ESTABLISHED callback
            conn = server->pending_conn;
            server->pending_wsi = NULL;
            server->pending_conn = NULL;
        }
        pthread_mutex_unlock(&server->pending_mutex);

        if (conn) {
            return conn;
        }

        // Service thread handles receiving, we just wait
        usleep(10000);  // 10ms sleep

        if (iterations > 0) iterations--;
    }

    return NULL;  // Timeout
}

// Close WebSocket server
void lws_ws_server_close(ws_server_t *server) {
    if (server) {
        server->closed = 1;

        // Signal service thread to stop
        server->shutdown = 1;

        // Wait for service thread to finish
        pthread_join(server->service_thread, NULL);

        // Destroy mutex
        pthread_mutex_destroy(&server->pending_mutex);

        if (server->context) {
            lws_context_destroy(server->context);
        }
        free(server);
    }
}
