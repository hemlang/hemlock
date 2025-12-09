#include "internal.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <poll.h>
#include <stdatomic.h>

// ========== SOCKET BUILTINS ==========

// Note: SocketHandle is defined in interpreter.h

// ========== RUNTIME ERROR HELPER ==========

static Value throw_runtime_error(ExecutionContext *ctx, const char *format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    ctx->exception_state.exception_value = val_string(buffer);
    value_retain(ctx->exception_state.exception_value);
    ctx->exception_state.is_throwing = 1;
    return val_null();
}

// ========== SOCKET VALUE CONSTRUCTOR ==========

Value val_socket(SocketHandle *sock) {
    Value val;
    val.type = VAL_SOCKET;
    val.as.as_socket = sock;
    return val;
}

// ========== SOCKET CLEANUP ==========

void socket_free(SocketHandle *sock) {
    if (!sock) return;

    // Close socket if still open
    if (!sock->closed && sock->fd >= 0) {
        close(sock->fd);
    }

    // Free allocated strings
    if (sock->address) {
        free(sock->address);
    }

    free(sock);
}

// ========== SOCKET CREATION ==========

// socket_create(domain: i32, type: i32, protocol: i32) -> Socket
Value builtin_socket_create(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 3) {
        return throw_runtime_error(ctx, "socket_create() expects 3 arguments (domain, type, protocol)");
    }

    if (!is_integer(args[0]) || !is_integer(args[1]) || !is_integer(args[2])) {
        return throw_runtime_error(ctx, "socket_create() arguments must be integers");
    }

    int domain = value_to_int(args[0]);
    int type = value_to_int(args[1]);
    int protocol = value_to_int(args[2]);

    // Normalize protocol=0 to appropriate protocol for socket type
    // This is required on macOS where protocol=0 creates broken sockets
    if (protocol == 0) {
        if (type == SOCK_STREAM) {
            protocol = IPPROTO_TCP;
        } else if (type == SOCK_DGRAM) {
            protocol = IPPROTO_UDP;
        }
    }

    int fd = socket(domain, type, protocol);
    if (fd < 0) {
        return throw_runtime_error(ctx, "Failed to create socket: %s", strerror(errno));
    }

    SocketHandle *sock = malloc(sizeof(SocketHandle));
    if (!sock) {
        close(fd);
        return throw_runtime_error(ctx, "Memory allocation failed");
    }

    sock->fd = fd;
    sock->address = NULL;
    sock->port = 0;
    sock->domain = domain;
    sock->type = type;
    sock->closed = 0;
    sock->listening = 0;
    sock->nonblocking = 0;

    return val_socket(sock);
}

// ========== SERVER OPERATIONS ==========

// socket.bind(address: string, port: i32) -> null
Value socket_method_bind(SocketHandle *sock, Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        return throw_runtime_error(ctx, "bind() expects 2 arguments (address, port)");
    }

    if (args[0].type != VAL_STRING || !is_integer(args[1])) {
        return throw_runtime_error(ctx, "bind() expects (string address, integer port)");
    }

    if (sock->closed) {
        return throw_runtime_error(ctx, "Cannot bind closed socket");
    }

    const char *address = args[0].as.as_string->data;
    int port = value_to_int(args[1]);

    if (sock->domain == AF_INET) {
        // IPv4
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);

        // Handle "0.0.0.0" and other special addresses
        if (strcmp(address, "0.0.0.0") == 0) {
            addr.sin_addr.s_addr = INADDR_ANY;
        } else if (inet_pton(AF_INET, address, &addr.sin_addr) != 1) {
            return throw_runtime_error(ctx, "Invalid IPv4 address: %s", address);
        }

        if (bind(sock->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            return throw_runtime_error(ctx, "bind() failed: %s", strerror(errno));
        }
    } else if (sock->domain == AF_INET6) {
        // IPv6
        struct sockaddr_in6 addr6;
        memset(&addr6, 0, sizeof(addr6));
        addr6.sin6_family = AF_INET6;
        addr6.sin6_port = htons(port);

        // Handle "::" (all interfaces) and other addresses
        if (strcmp(address, "::") == 0 || strcmp(address, "::0") == 0) {
            addr6.sin6_addr = in6addr_any;
        } else if (inet_pton(AF_INET6, address, &addr6.sin6_addr) != 1) {
            return throw_runtime_error(ctx, "Invalid IPv6 address: %s", address);
        }

        if (bind(sock->fd, (struct sockaddr *)&addr6, sizeof(addr6)) < 0) {
            return throw_runtime_error(ctx, "bind() failed: %s", strerror(errno));
        }
    } else {
        return throw_runtime_error(ctx, "Unsupported socket domain (use AF_INET or AF_INET6)");
    }

    // Store address and port
    if (sock->address) free(sock->address);
    sock->address = strdup(address);
    sock->port = port;

    return val_null();
}

// socket.listen(backlog: i32) -> null
Value socket_method_listen(SocketHandle *sock, Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        return throw_runtime_error(ctx, "listen() expects 1 argument (backlog)");
    }

    if (!is_integer(args[0])) {
        return throw_runtime_error(ctx, "listen() backlog must be integer");
    }

    if (sock->closed) {
        return throw_runtime_error(ctx, "Cannot listen on closed socket");
    }

    int backlog = value_to_int(args[0]);

    if (listen(sock->fd, backlog) < 0) {
        return throw_runtime_error(ctx, "Failed to listen on socket: %s", strerror(errno));
    }

    sock->listening = 1;
    return val_null();
}

// socket.accept() -> Socket
Value socket_method_accept(SocketHandle *sock, Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;  // Unused parameter
    if (num_args != 0) {
        return throw_runtime_error(ctx, "accept() expects no arguments");
    }

    if (sock->closed) {
        return throw_runtime_error(ctx, "Cannot accept on closed socket");
    }

    if (!sock->listening) {
        return throw_runtime_error(ctx, "Socket must be listening before accept()");
    }

    // Use sockaddr_storage to handle both IPv4 and IPv6
    struct sockaddr_storage client_addr;
    socklen_t client_len = sizeof(client_addr);

    int client_fd = accept(sock->fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0) {
        // In non-blocking mode, EAGAIN/EWOULDBLOCK means no pending connections
        if (sock->nonblocking && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return val_null();  // No connection available
        }
        return throw_runtime_error(ctx, "Failed to accept connection: %s", strerror(errno));
    }

    // Create new socket for client connection
    SocketHandle *client_sock = malloc(sizeof(SocketHandle));
    if (!client_sock) {
        close(client_fd);
        return throw_runtime_error(ctx, "Memory allocation failed");
    }

    client_sock->fd = client_fd;
    client_sock->domain = sock->domain;
    client_sock->type = sock->type;
    client_sock->closed = 0;
    client_sock->listening = 0;
    client_sock->nonblocking = 0;

    // Get client address and port based on address family
    if (sock->domain == AF_INET6) {
        struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&client_addr;
        char addr_str[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &addr6->sin6_addr, addr_str, sizeof(addr_str));
        client_sock->address = strdup(addr_str);
        client_sock->port = ntohs(addr6->sin6_port);
    } else {
        struct sockaddr_in *addr4 = (struct sockaddr_in *)&client_addr;
        char addr_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr4->sin_addr, addr_str, sizeof(addr_str));
        client_sock->address = strdup(addr_str);
        client_sock->port = ntohs(addr4->sin_port);
    }

    return val_socket(client_sock);
}

// ========== CLIENT OPERATIONS ==========

// socket.connect(address: string, port: i32) -> null
Value socket_method_connect(SocketHandle *sock, Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        return throw_runtime_error(ctx, "connect() expects 2 arguments (address, port)");
    }

    if (args[0].type != VAL_STRING || !is_integer(args[1])) {
        return throw_runtime_error(ctx, "connect() expects (string address, integer port)");
    }

    if (sock->closed) {
        return throw_runtime_error(ctx, "Cannot connect closed socket");
    }

    const char *address = args[0].as.as_string->data;
    int port = value_to_int(args[1]);

    // Use getaddrinfo for both IPv4 and IPv6 resolution
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = sock->domain;  // AF_INET or AF_INET6
    hints.ai_socktype = sock->type;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    int gai_err = getaddrinfo(address, port_str, &hints, &result);
    if (gai_err != 0) {
        return throw_runtime_error(ctx, "Failed to resolve '%s': %s", address, gai_strerror(gai_err));
    }

    int connect_result = connect(sock->fd, result->ai_addr, result->ai_addrlen);
    freeaddrinfo(result);

    if (connect_result < 0) {
        return throw_runtime_error(ctx, "Failed to connect to %s:%d: %s",
                address, port, strerror(errno));
    }

    // Store address and port
    if (sock->address) free(sock->address);
    sock->address = strdup(address);
    sock->port = port;

    return val_null();
}

// ========== I/O OPERATIONS ==========

// socket.send(data: string | buffer) -> i32 (bytes sent)
Value socket_method_send(SocketHandle *sock, Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        return throw_runtime_error(ctx, "send() expects 1 argument (data)");
    }

    if (sock->closed) {
        return throw_runtime_error(ctx, "Cannot send on closed socket");
    }

    const void *data;
    size_t len;

    if (args[0].type == VAL_STRING) {
        String *str = args[0].as.as_string;
        data = str->data;
        len = str->length;
    } else if (args[0].type == VAL_BUFFER) {
        Buffer *buf = args[0].as.as_buffer;
        data = buf->data;
        len = buf->length;
    } else {
        return throw_runtime_error(ctx, "send() expects string or buffer argument");
    }

    ssize_t sent = send(sock->fd, data, len, 0);
    if (sent < 0) {
        // In non-blocking mode, EAGAIN/EWOULDBLOCK means socket buffer is full
        if (sock->nonblocking && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return val_i32(0);  // No bytes sent (would block)
        }
        return throw_runtime_error(ctx, "Failed to send data: %s", strerror(errno));
    }

    return val_i32((int32_t)sent);
}

// socket.recv(size: i32) -> buffer | null (null if non-blocking and no data)
Value socket_method_recv(SocketHandle *sock, Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        return throw_runtime_error(ctx, "recv() expects 1 argument (size)");
    }

    if (!is_integer(args[0])) {
        return throw_runtime_error(ctx, "recv() size must be integer");
    }

    if (sock->closed) {
        return throw_runtime_error(ctx, "Cannot recv on closed socket");
    }

    int size = value_to_int(args[0]);
    if (size <= 0) {
        // Return empty buffer
        Buffer *buf = malloc(sizeof(Buffer));
        buf->data = malloc(1);
        buf->length = 0;
        buf->capacity = 0;
        buf->ref_count = 1;  // Start with 1 - caller owns the first reference
        atomic_store(&buf->freed, 0);  // Not freed
        return (Value){ .type = VAL_BUFFER, .as.as_buffer = buf };
    }

    void *data = malloc(size);
    if (!data) {
        return throw_runtime_error(ctx, "Memory allocation failed");
    }

    ssize_t received = recv(sock->fd, data, size, 0);
    if (received < 0) {
        // In non-blocking mode, EAGAIN/EWOULDBLOCK means no data available (not an error)
        if (sock->nonblocking && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            free(data);
            return val_null();  // No data available
        }
        free(data);
        return throw_runtime_error(ctx, "Failed to receive data: %s", strerror(errno));
    }

    // received == 0 means connection closed by peer
    if (received == 0) {
        free(data);
        // Return empty buffer to indicate EOF
        Buffer *buf = malloc(sizeof(Buffer));
        buf->data = malloc(1);
        buf->length = 0;
        buf->capacity = 0;
        buf->ref_count = 1;
        atomic_store(&buf->freed, 0);  // Not freed
        return (Value){ .type = VAL_BUFFER, .as.as_buffer = buf };
    }

    Buffer *buf = malloc(sizeof(Buffer));
    buf->data = data;
    buf->length = (int)received;
    buf->capacity = size;
    buf->ref_count = 1;  // Start with 1 - caller owns the first reference
    atomic_store(&buf->freed, 0);  // Not freed

    return (Value){ .type = VAL_BUFFER, .as.as_buffer = buf };
}

// ========== UDP OPERATIONS ==========

// socket.sendto(address: string, port: i32, data: string | buffer) -> i32
Value socket_method_sendto(SocketHandle *sock, Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 3) {
        return throw_runtime_error(ctx, "sendto() expects 3 arguments (address, port, data)");
    }

    if (args[0].type != VAL_STRING || !is_integer(args[1])) {
        return throw_runtime_error(ctx, "sendto() expects (string address, integer port, data)");
    }

    if (sock->closed) {
        return throw_runtime_error(ctx, "Cannot sendto on closed socket");
    }

    const char *address = args[0].as.as_string->data;
    int port = value_to_int(args[1]);

    const void *data;
    size_t len;

    if (args[2].type == VAL_STRING) {
        String *str = args[2].as.as_string;
        data = str->data;
        len = str->length;
    } else if (args[2].type == VAL_BUFFER) {
        Buffer *buf = args[2].as.as_buffer;
        data = buf->data;
        len = buf->length;
    } else {
        return throw_runtime_error(ctx, "sendto() data must be string or buffer");
    }

    ssize_t sent;
    if (sock->domain == AF_INET) {
        struct sockaddr_in dest_addr;
        memset(&dest_addr, 0, sizeof(dest_addr));
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(port);

        if (inet_pton(AF_INET, address, &dest_addr.sin_addr) != 1) {
            return throw_runtime_error(ctx, "Invalid IPv4 address: %s", address);
        }

        sent = sendto(sock->fd, data, len, 0,
                (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    } else if (sock->domain == AF_INET6) {
        struct sockaddr_in6 dest_addr6;
        memset(&dest_addr6, 0, sizeof(dest_addr6));
        dest_addr6.sin6_family = AF_INET6;
        dest_addr6.sin6_port = htons(port);

        if (inet_pton(AF_INET6, address, &dest_addr6.sin6_addr) != 1) {
            return throw_runtime_error(ctx, "Invalid IPv6 address: %s", address);
        }

        sent = sendto(sock->fd, data, len, 0,
                (struct sockaddr *)&dest_addr6, sizeof(dest_addr6));
    } else {
        return throw_runtime_error(ctx, "Unsupported socket domain (use AF_INET or AF_INET6)");
    }

    if (sent < 0) {
        return throw_runtime_error(ctx, "Failed to sendto %s:%d: %s",
                address, port, strerror(errno));
    }

    return val_i32((int32_t)sent);
}

// socket.recvfrom(size: i32) -> { data: buffer, address: string, port: i32 }
Value socket_method_recvfrom(SocketHandle *sock, Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        return throw_runtime_error(ctx, "recvfrom() expects 1 argument (size)");
    }

    if (!is_integer(args[0])) {
        return throw_runtime_error(ctx, "recvfrom() size must be integer");
    }

    if (sock->closed) {
        return throw_runtime_error(ctx, "Cannot recvfrom on closed socket");
    }

    int size = value_to_int(args[0]);
    if (size <= 0) {
        return throw_runtime_error(ctx, "recvfrom() size must be positive");
    }

    void *data = malloc(size);
    if (!data) {
        return throw_runtime_error(ctx, "Memory allocation failed");
    }

    // Use sockaddr_storage to handle both IPv4 and IPv6
    struct sockaddr_storage src_addr;
    socklen_t addr_len = sizeof(src_addr);

    ssize_t received = recvfrom(sock->fd, data, size, 0,
            (struct sockaddr *)&src_addr, &addr_len);

    if (received < 0) {
        free(data);
        return throw_runtime_error(ctx, "Failed to recvfrom: %s", strerror(errno));
    }

    // Create buffer with received data
    Buffer *buf = malloc(sizeof(Buffer));
    buf->data = data;
    buf->length = (int)received;
    buf->capacity = size;
    buf->ref_count = 1;  // Start with 1 - caller owns the first reference
    atomic_store(&buf->freed, 0);  // Not freed

    // Get source address and port based on address family
    char addr_str[INET6_ADDRSTRLEN];
    int src_port;
    if (sock->domain == AF_INET6) {
        struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&src_addr;
        inet_ntop(AF_INET6, &addr6->sin6_addr, addr_str, sizeof(addr_str));
        src_port = ntohs(addr6->sin6_port);
    } else {
        struct sockaddr_in *addr4 = (struct sockaddr_in *)&src_addr;
        inet_ntop(AF_INET, &addr4->sin_addr, addr_str, sizeof(addr_str));
        src_port = ntohs(addr4->sin_port);
    }

    // Create result object { data, address, port }
    Object *result = object_new(NULL, 3);

    // Add data field
    Value data_val;
    data_val.type = VAL_BUFFER;
    data_val.as.as_buffer = buf;
    result->field_names[result->num_fields] = strdup("data");
    result->field_values[result->num_fields] = data_val;
    result->num_fields++;

    // Add address field
    result->field_names[result->num_fields] = strdup("address");
    result->field_values[result->num_fields] = val_string(addr_str);
    result->num_fields++;

    // Add port field
    result->field_names[result->num_fields] = strdup("port");
    result->field_values[result->num_fields] = val_i32(src_port);
    result->num_fields++;

    return val_object(result);
}

// ========== DNS RESOLUTION ==========

// dns_resolve(hostname: string) -> string (IP address)
Value builtin_dns_resolve(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        return throw_runtime_error(ctx, "dns_resolve() expects 1 argument (hostname)");
    }

    if (args[0].type != VAL_STRING) {
        return throw_runtime_error(ctx, "dns_resolve() hostname must be string");
    }

    const char *hostname = args[0].as.as_string->data;

    struct hostent *host = gethostbyname(hostname);
    if (!host) {
        return throw_runtime_error(ctx, "Failed to resolve hostname '%s'", hostname);
    }

    // Return first IPv4 address
    char *ip = inet_ntoa(*(struct in_addr *)host->h_addr_list[0]);
    return val_string(ip);
}

// ========== SOCKET OPTIONS ==========

// socket.setsockopt(level: i32, option: i32, value: i32) -> null
Value socket_method_setsockopt(SocketHandle *sock, Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 3) {
        return throw_runtime_error(ctx, "setsockopt() expects 3 arguments (level, option, value)");
    }

    if (!is_integer(args[0]) || !is_integer(args[1]) || !is_integer(args[2])) {
        return throw_runtime_error(ctx, "setsockopt() arguments must be integers");
    }

    if (sock->closed) {
        return throw_runtime_error(ctx, "Cannot setsockopt on closed socket");
    }

    int level = value_to_int(args[0]);
    int option = value_to_int(args[1]);
    int value = value_to_int(args[2]);

    if (setsockopt(sock->fd, level, option, &value, sizeof(value)) < 0) {
        return throw_runtime_error(ctx, "Failed to set socket option: %s", strerror(errno));
    }

    return val_null();
}

// socket.set_timeout(seconds: f64) -> null
Value socket_method_set_timeout(SocketHandle *sock, Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        return throw_runtime_error(ctx, "set_timeout() expects 1 argument (seconds)");
    }

    if (!is_numeric(args[0])) {
        return throw_runtime_error(ctx, "set_timeout() timeout must be numeric");
    }

    if (sock->closed) {
        return throw_runtime_error(ctx, "Cannot set_timeout on closed socket");
    }

    double seconds = value_to_float(args[0]);

    struct timeval timeout;
    timeout.tv_sec = (long)seconds;
    timeout.tv_usec = (long)((seconds - timeout.tv_sec) * 1000000);

    // Set both recv and send timeouts
    if (setsockopt(sock->fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        return throw_runtime_error(ctx, "Failed to set receive timeout: %s", strerror(errno));
    }

    if (setsockopt(sock->fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        return throw_runtime_error(ctx, "Failed to set send timeout: %s", strerror(errno));
    }

    return val_null();
}

// socket.set_nonblocking(enable: bool) -> null
Value socket_method_set_nonblocking(SocketHandle *sock, Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        return throw_runtime_error(ctx, "set_nonblocking() expects 1 argument (bool)");
    }

    if (args[0].type != VAL_BOOL) {
        return throw_runtime_error(ctx, "set_nonblocking() argument must be boolean");
    }

    if (sock->closed) {
        return throw_runtime_error(ctx, "Cannot set_nonblocking on closed socket");
    }

    int enable = args[0].as.as_bool;

    int flags = fcntl(sock->fd, F_GETFL, 0);
    if (flags < 0) {
        return throw_runtime_error(ctx, "Failed to get socket flags: %s", strerror(errno));
    }

    if (enable) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }

    if (fcntl(sock->fd, F_SETFL, flags) < 0) {
        return throw_runtime_error(ctx, "Failed to set socket flags: %s", strerror(errno));
    }

    sock->nonblocking = enable;
    return val_null();
}

// ========== RESOURCE MANAGEMENT ==========

// socket.close() -> null (idempotent)
Value socket_method_close(SocketHandle *sock, Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;  // Unused parameter
    if (num_args != 0) {
        return throw_runtime_error(ctx, "close() expects no arguments");
    }

    // Idempotent - safe to call multiple times
    if (!sock->closed && sock->fd >= 0) {
        close(sock->fd);
        sock->fd = -1;
        sock->closed = 1;
    }

    return val_null();
}

// ========== SOCKET PROPERTY ACCESS ==========

// Get socket properties (read-only)
Value get_socket_property(SocketHandle *sock, const char *property, ExecutionContext *ctx) {
    if (strcmp(property, "address") == 0) {
        if (sock->address) {
            return val_string(sock->address);
        }
        return val_null();
    }

    if (strcmp(property, "port") == 0) {
        return val_i32(sock->port);
    }

    if (strcmp(property, "closed") == 0) {
        return val_bool(sock->closed);
    }

    if (strcmp(property, "fd") == 0) {
        return val_i32(sock->fd);
    }

    if (strcmp(property, "nonblocking") == 0) {
        return val_bool(sock->nonblocking);
    }

    return throw_runtime_error(ctx, "Socket has no property '%s'", property);
}

// ========== SOCKET METHOD DISPATCH ==========

Value call_socket_method(SocketHandle *sock, const char *method, Value *args, int num_args, ExecutionContext *ctx) {
    // Server operations
    if (strcmp(method, "bind") == 0) {
        return socket_method_bind(sock, args, num_args, ctx);
    }
    if (strcmp(method, "listen") == 0) {
        return socket_method_listen(sock, args, num_args, ctx);
    }
    if (strcmp(method, "accept") == 0) {
        return socket_method_accept(sock, args, num_args, ctx);
    }

    // Client operations
    if (strcmp(method, "connect") == 0) {
        return socket_method_connect(sock, args, num_args, ctx);
    }

    // I/O operations
    if (strcmp(method, "send") == 0) {
        return socket_method_send(sock, args, num_args, ctx);
    }
    if (strcmp(method, "recv") == 0) {
        return socket_method_recv(sock, args, num_args, ctx);
    }

    // UDP operations
    if (strcmp(method, "sendto") == 0) {
        return socket_method_sendto(sock, args, num_args, ctx);
    }
    if (strcmp(method, "recvfrom") == 0) {
        return socket_method_recvfrom(sock, args, num_args, ctx);
    }

    // Socket options
    if (strcmp(method, "setsockopt") == 0) {
        return socket_method_setsockopt(sock, args, num_args, ctx);
    }
    if (strcmp(method, "set_timeout") == 0) {
        return socket_method_set_timeout(sock, args, num_args, ctx);
    }
    if (strcmp(method, "set_nonblocking") == 0) {
        return socket_method_set_nonblocking(sock, args, num_args, ctx);
    }

    // Resource management
    if (strcmp(method, "close") == 0) {
        return socket_method_close(sock, args, num_args, ctx);
    }

    return throw_runtime_error(ctx, "Socket has no method '%s'", method);
}

// ========== POLL OPERATIONS ==========

// Helper to extract file descriptor from a value
static int get_fd_from_value(Value val) {
    if (val.type == VAL_SOCKET) {
        return val.as.as_socket->fd;
    }
    if (val.type == VAL_FILE) {
        return fileno(val.as.as_file->fp);
    }
    return -1;
}

// poll(fds: array<{fd, events}>, timeout_ms: i32) -> array<{fd, revents}>
// Wait for I/O events on multiple file descriptors
Value builtin_poll(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        ctx->exception_state.exception_value = val_string("poll() expects 2 arguments (fds, timeout_ms)");
        value_retain(ctx->exception_state.exception_value);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    if (args[0].type != VAL_ARRAY) {
        ctx->exception_state.exception_value = val_string("poll() first argument must be array");
        value_retain(ctx->exception_state.exception_value);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    if (!is_integer(args[1])) {
        ctx->exception_state.exception_value = val_string("poll() second argument must be integer (timeout_ms)");
        value_retain(ctx->exception_state.exception_value);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    Array *fds_arr = args[0].as.as_array;
    int timeout_ms = value_to_int(args[1]);

    if (fds_arr->length == 0) {
        // Return empty array
        return val_array(array_new());
    }

    // Build pollfd array
    struct pollfd *pfds = malloc(sizeof(struct pollfd) * fds_arr->length);
    if (!pfds) {
        ctx->exception_state.exception_value = val_string("poll() memory allocation failed");
        value_retain(ctx->exception_state.exception_value);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    // Store original values for return
    Value *original_fds = malloc(sizeof(Value) * fds_arr->length);
    if (!original_fds) {
        free(pfds);
        ctx->exception_state.exception_value = val_string("poll() memory allocation failed");
        value_retain(ctx->exception_state.exception_value);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    for (int i = 0; i < fds_arr->length; i++) {
        Value item = fds_arr->elements[i];

        if (item.type != VAL_OBJECT) {
            free(pfds);
            free(original_fds);
            ctx->exception_state.exception_value = val_string("poll() array elements must be objects with 'fd' and 'events'");
            value_retain(ctx->exception_state.exception_value);
            ctx->exception_state.is_throwing = 1;
            return val_null();
        }

        Object *obj = item.as.as_object;

        // Find fd field
        Value fd_val = val_null();
        Value events_val = val_null();
        for (int j = 0; j < obj->num_fields; j++) {
            if (strcmp(obj->field_names[j], "fd") == 0) {
                fd_val = obj->field_values[j];
            } else if (strcmp(obj->field_names[j], "events") == 0) {
                events_val = obj->field_values[j];
            }
        }

        int fd = get_fd_from_value(fd_val);
        if (fd < 0) {
            free(pfds);
            free(original_fds);
            ctx->exception_state.exception_value = val_string("poll() fd must be a socket or file");
            value_retain(ctx->exception_state.exception_value);
            ctx->exception_state.is_throwing = 1;
            return val_null();
        }

        if (!is_integer(events_val)) {
            free(pfds);
            free(original_fds);
            ctx->exception_state.exception_value = val_string("poll() events must be an integer");
            value_retain(ctx->exception_state.exception_value);
            ctx->exception_state.is_throwing = 1;
            return val_null();
        }

        pfds[i].fd = fd;
        pfds[i].events = (short)value_to_int(events_val);
        pfds[i].revents = 0;
        original_fds[i] = fd_val;
        value_retain(original_fds[i]);
    }

    // Call poll
    int result = poll(pfds, fds_arr->length, timeout_ms);

    if (result < 0) {
        for (int i = 0; i < fds_arr->length; i++) {
            value_release(original_fds[i]);
        }
        free(pfds);
        free(original_fds);
        char err_msg[256];
        snprintf(err_msg, sizeof(err_msg), "poll() failed: %s", strerror(errno));
        ctx->exception_state.exception_value = val_string(err_msg);
        value_retain(ctx->exception_state.exception_value);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    // Build result array with only fds that have events
    Array *result_arr = array_new();

    for (int i = 0; i < fds_arr->length; i++) {
        if (pfds[i].revents != 0) {
            // Create result object { fd, revents }
            Object *res_obj = object_new(NULL, 2);

            res_obj->field_names[0] = strdup("fd");
            res_obj->field_values[0] = original_fds[i];
            value_retain(original_fds[i]);
            res_obj->num_fields = 1;

            res_obj->field_names[1] = strdup("revents");
            res_obj->field_values[1] = val_i32(pfds[i].revents);
            res_obj->num_fields = 2;

            array_push(result_arr, val_object(res_obj));
        }
    }

    // Cleanup
    for (int i = 0; i < fds_arr->length; i++) {
        value_release(original_fds[i]);
    }
    free(pfds);
    free(original_fds);

    return val_array(result_arr);
}
