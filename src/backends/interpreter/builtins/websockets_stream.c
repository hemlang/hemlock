// Streaming HTTP builtins for Hemlock (extracted from websockets.c)
// For chunked/SSE responses (e.g., streaming LLM output)

#include "websockets_internal.h"
#include <stdatomic.h>

#ifndef HAVE_LIBWEBSOCKETS
#  define HAVE_LIBWEBSOCKETS 0
#endif

#if HAVE_LIBWEBSOCKETS

#include <libwebsockets.h>
#include <pthread.h>

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
    pthread_attr_t stream_attr;
    pthread_attr_init(&stream_attr);
    pthread_attr_setstacksize(&stream_attr, HML_THREAD_STACK_SIZE);
    int stream_rc = pthread_create(&stream->service_thread, &stream_attr, http_stream_service_thread, stream);
    pthread_attr_destroy(&stream_attr);
    if (stream_rc != 0) {
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

#else  // !HAVE_LIBWEBSOCKETS

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
