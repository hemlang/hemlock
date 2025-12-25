/*
 * Hemlock Runtime Library - Socket and Networking Operations
 *
 * TCP/UDP sockets, DNS resolution, and low-level networking.
 */

#include "builtins_internal.h"

// ========== SOCKET OPERATIONS ==========

// socket_create(domain, type, protocol) -> socket
HmlValue hml_socket_create(HmlValue domain, HmlValue sock_type, HmlValue protocol) {
    int d = hml_to_i32(domain);
    int t = hml_to_i32(sock_type);
    int p = hml_to_i32(protocol);

    int fd = socket(d, t, p);
    if (fd < 0) {
        hml_runtime_error("Failed to create socket: %s", strerror(errno));
    }

    HmlSocket *sock = malloc(sizeof(HmlSocket));
    sock->fd = fd;
    sock->address = NULL;
    sock->port = 0;
    sock->domain = d;
    sock->type = t;
    sock->closed = 0;
    sock->listening = 0;

    return hml_val_socket(sock);
}

// socket.bind(address, port)
void hml_socket_bind(HmlValue socket_val, HmlValue address, HmlValue port) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        hml_runtime_error("bind() expects a socket");
    }
    HmlSocket *sock = socket_val.as.as_socket;

    if (sock->closed) {
        hml_runtime_error("Cannot bind closed socket");
    }

    const char *addr_str = hml_to_string_ptr(address);
    int p = hml_to_i32(port);

    if (sock->domain != AF_INET) {
        hml_runtime_error("Only AF_INET sockets supported currently");
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(p);

    if (strcmp(addr_str, "0.0.0.0") == 0) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else if (inet_pton(AF_INET, addr_str, &addr.sin_addr) != 1) {
        hml_runtime_error("Invalid IP address: %s", addr_str);
    }

    if (bind(sock->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Runtime error: Failed to bind socket to %s:%d: %s\n",
                addr_str, p, strerror(errno));
        exit(1);
    }

    if (sock->address) free(sock->address);
    sock->address = strdup(addr_str);
    sock->port = p;
}

// socket.listen(backlog)
void hml_socket_listen(HmlValue socket_val, HmlValue backlog) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        hml_runtime_error("listen() expects a socket");
    }
    HmlSocket *sock = socket_val.as.as_socket;

    if (sock->closed) {
        hml_runtime_error("Cannot listen on closed socket");
    }

    int bl = hml_to_i32(backlog);
    if (listen(sock->fd, bl) < 0) {
        hml_runtime_error("Failed to listen on socket: %s", strerror(errno));
    }

    sock->listening = 1;
}

// socket.accept() -> socket
HmlValue hml_socket_accept(HmlValue socket_val) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        hml_runtime_error("accept() expects a socket");
    }
    HmlSocket *sock = socket_val.as.as_socket;

    if (sock->closed) {
        hml_runtime_error("Cannot accept on closed socket");
    }

    if (!sock->listening) {
        hml_runtime_error("Socket must be listening before accept()");
    }

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    int client_fd = accept(sock->fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0) {
        hml_runtime_error("Failed to accept connection: %s", strerror(errno));
    }

    HmlSocket *client_sock = malloc(sizeof(HmlSocket));
    client_sock->fd = client_fd;
    client_sock->domain = sock->domain;
    client_sock->type = sock->type;
    client_sock->closed = 0;
    client_sock->listening = 0;

    char addr_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, addr_str, sizeof(addr_str));
    client_sock->address = strdup(addr_str);
    client_sock->port = ntohs(client_addr.sin_port);

    return hml_val_socket(client_sock);
}

// socket.connect(address, port)
void hml_socket_connect(HmlValue socket_val, HmlValue address, HmlValue port) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        hml_runtime_error("connect() expects a socket");
    }
    HmlSocket *sock = socket_val.as.as_socket;

    if (sock->closed) {
        hml_runtime_error("Cannot connect closed socket");
    }

    const char *addr_str = hml_to_string_ptr(address);
    int p = hml_to_i32(port);

    struct hostent *host = gethostbyname(addr_str);
    if (!host) {
        hml_runtime_error("Failed to resolve hostname '%s'", addr_str);
    }

    if (sock->domain != AF_INET) {
        hml_runtime_error("Only AF_INET sockets supported currently");
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(p);
    memcpy(&server_addr.sin_addr.s_addr, host->h_addr_list[0], host->h_length);

    if (connect(sock->fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "Runtime error: Failed to connect to %s:%d: %s\n",
                addr_str, p, strerror(errno));
        exit(1);
    }

    if (sock->address) free(sock->address);
    sock->address = strdup(addr_str);
    sock->port = p;
}

// socket.send(data) -> i32 (bytes sent)
HmlValue hml_socket_send(HmlValue socket_val, HmlValue data) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        hml_runtime_error("send() expects a socket");
    }
    HmlSocket *sock = socket_val.as.as_socket;

    if (sock->closed) {
        hml_runtime_error("Cannot send on closed socket");
    }

    const void *buf = NULL;
    size_t len = 0;

    if (data.type == HML_VAL_STRING && data.as.as_string) {
        buf = data.as.as_string->data;
        len = data.as.as_string->length;
    } else if (data.type == HML_VAL_BUFFER && data.as.as_buffer) {
        buf = data.as.as_buffer->data;
        len = data.as.as_buffer->length;
    } else {
        hml_runtime_error("send() expects string or buffer");
    }

    ssize_t sent = send(sock->fd, buf, len, 0);
    if (sent < 0) {
        hml_runtime_error("Failed to send data: %s", strerror(errno));
    }

    return hml_val_i32((int32_t)sent);
}

// socket.recv(size) -> buffer
HmlValue hml_socket_recv(HmlValue socket_val, HmlValue size) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        hml_runtime_error("recv() expects a socket");
    }
    HmlSocket *sock = socket_val.as.as_socket;

    if (sock->closed) {
        hml_runtime_error("Cannot recv on closed socket");
    }

    int sz = hml_to_i32(size);
    if (sz <= 0) {
        return hml_val_buffer(0);
    }

    void *buf = malloc(sz);
    ssize_t received = recv(sock->fd, buf, sz, 0);
    if (received < 0) {
        free(buf);
        hml_runtime_error("Failed to receive data: %s", strerror(errno));
    }

    HmlBuffer *hbuf = malloc(sizeof(HmlBuffer));
    hbuf->data = buf;
    hbuf->length = (int)received;
    hbuf->capacity = sz;
    hbuf->ref_count = 1;
    atomic_store(&hbuf->freed, 0);  // Not freed

    HmlValue result;
    result.type = HML_VAL_BUFFER;
    result.as.as_buffer = hbuf;
    return result;
}

// socket.sendto(address, port, data) -> i32
HmlValue hml_socket_sendto(HmlValue socket_val, HmlValue address, HmlValue port, HmlValue data) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        hml_runtime_error("sendto() expects a socket");
    }
    HmlSocket *sock = socket_val.as.as_socket;

    if (sock->closed) {
        hml_runtime_error("Cannot sendto on closed socket");
    }

    const char *addr_str = hml_to_string_ptr(address);
    int p = hml_to_i32(port);

    const void *buf = NULL;
    size_t len = 0;

    if (data.type == HML_VAL_STRING && data.as.as_string) {
        buf = data.as.as_string->data;
        len = data.as.as_string->length;
    } else if (data.type == HML_VAL_BUFFER && data.as.as_buffer) {
        buf = data.as.as_buffer->data;
        len = data.as.as_buffer->length;
    } else {
        hml_runtime_error("sendto() data must be string or buffer");
    }

    if (sock->domain != AF_INET) {
        hml_runtime_error("Only AF_INET sockets supported currently");
    }

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(p);

    if (inet_pton(AF_INET, addr_str, &dest_addr.sin_addr) != 1) {
        hml_runtime_error("Invalid IP address: %s", addr_str);
    }

    ssize_t sent = sendto(sock->fd, buf, len, 0,
            (struct sockaddr *)&dest_addr, sizeof(dest_addr));

    if (sent < 0) {
        fprintf(stderr, "Runtime error: Failed to sendto %s:%d: %s\n",
                addr_str, p, strerror(errno));
        exit(1);
    }

    return hml_val_i32((int32_t)sent);
}

// socket.recvfrom(size) -> { data: buffer, address: string, port: i32 }
HmlValue hml_socket_recvfrom(HmlValue socket_val, HmlValue size) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        hml_runtime_error("recvfrom() expects a socket");
    }
    HmlSocket *sock = socket_val.as.as_socket;

    if (sock->closed) {
        hml_runtime_error("Cannot recvfrom on closed socket");
    }

    int sz = hml_to_i32(size);
    if (sz <= 0) {
        hml_runtime_error("recvfrom() size must be positive");
    }

    void *buf = malloc(sz);
    struct sockaddr_in src_addr;
    socklen_t addr_len = sizeof(src_addr);

    ssize_t received = recvfrom(sock->fd, buf, sz, 0,
            (struct sockaddr *)&src_addr, &addr_len);

    if (received < 0) {
        free(buf);
        hml_runtime_error("Failed to recvfrom: %s", strerror(errno));
    }

    // Create buffer for data
    HmlBuffer *hbuf = malloc(sizeof(HmlBuffer));
    hbuf->data = buf;
    hbuf->length = (int)received;
    hbuf->capacity = sz;
    hbuf->ref_count = 1;
    atomic_store(&hbuf->freed, 0);  // Not freed

    // Get source address and port
    char addr_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &src_addr.sin_addr, addr_str, sizeof(addr_str));
    int src_port = ntohs(src_addr.sin_port);

    // Create result object { data, address, port }
    HmlValue result = hml_val_object();
    HmlValue data_val;
    data_val.type = HML_VAL_BUFFER;
    data_val.as.as_buffer = hbuf;

    hml_object_set_field(result, "data", data_val);
    hml_object_set_field(result, "address", hml_val_string(addr_str));
    hml_object_set_field(result, "port", hml_val_i32(src_port));

    return result;
}

// socket.setsockopt(level, option, value)
void hml_socket_setsockopt(HmlValue socket_val, HmlValue level, HmlValue option, HmlValue value) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        hml_runtime_error("setsockopt() expects a socket");
    }
    HmlSocket *sock = socket_val.as.as_socket;

    if (sock->closed) {
        hml_runtime_error("Cannot setsockopt on closed socket");
    }

    int lvl = hml_to_i32(level);
    int opt = hml_to_i32(option);
    int val = hml_to_i32(value);

    if (setsockopt(sock->fd, lvl, opt, &val, sizeof(val)) < 0) {
        hml_runtime_error("Failed to set socket option: %s", strerror(errno));
    }
}

// socket.set_timeout(seconds)
void hml_socket_set_timeout(HmlValue socket_val, HmlValue seconds_val) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        hml_runtime_error("set_timeout() expects a socket");
    }
    HmlSocket *sock = socket_val.as.as_socket;

    if (sock->closed) {
        hml_runtime_error("Cannot set_timeout on closed socket");
    }

    double seconds = hml_to_f64(seconds_val);

    struct timeval timeout;
    timeout.tv_sec = (long)seconds;
    timeout.tv_usec = (long)((seconds - timeout.tv_sec) * 1000000);

    // Set both recv and send timeouts
    if (setsockopt(sock->fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        hml_runtime_error("Failed to set receive timeout: %s", strerror(errno));
    }

    if (setsockopt(sock->fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        hml_runtime_error("Failed to set send timeout: %s", strerror(errno));
    }
}

// socket.set_nonblocking(enable: bool)
void hml_socket_set_nonblocking(HmlValue socket_val, HmlValue enable_val) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        hml_runtime_error("set_nonblocking() expects a socket");
    }
    HmlSocket *sock = socket_val.as.as_socket;

    if (sock->closed) {
        hml_runtime_error("Cannot set_nonblocking on closed socket");
    }

    int enable = hml_to_bool(enable_val);

    int flags = fcntl(sock->fd, F_GETFL, 0);
    if (flags < 0) {
        hml_runtime_error("Failed to get socket flags: %s", strerror(errno));
    }

    if (enable) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }

    if (fcntl(sock->fd, F_SETFL, flags) < 0) {
        hml_runtime_error("Failed to set socket flags: %s", strerror(errno));
    }

    sock->nonblocking = enable;
}

// socket.close()
void hml_socket_close(HmlValue socket_val) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        hml_runtime_error("close() expects a socket");
    }
    HmlSocket *sock = socket_val.as.as_socket;

    // Idempotent - safe to call multiple times
    if (!sock->closed && sock->fd >= 0) {
        close(sock->fd);
        sock->fd = -1;
        sock->closed = 1;
    }
}

// Socket property getters
HmlValue hml_socket_get_fd(HmlValue socket_val) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        return hml_val_i32(-1);
    }
    return hml_val_i32(socket_val.as.as_socket->fd);
}

HmlValue hml_socket_get_address(HmlValue socket_val) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        return hml_val_null();
    }
    HmlSocket *sock = socket_val.as.as_socket;
    if (sock->address) {
        return hml_val_string(sock->address);
    }
    return hml_val_null();
}

HmlValue hml_socket_get_port(HmlValue socket_val) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        return hml_val_i32(0);
    }
    return hml_val_i32(socket_val.as.as_socket->port);
}

HmlValue hml_socket_get_closed(HmlValue socket_val) {
    if (socket_val.type != HML_VAL_SOCKET || !socket_val.as.as_socket) {
        return hml_val_bool(1);
    }
    return hml_val_bool(socket_val.as.as_socket->closed);
}

// ========== DNS/NETWORKING OPERATIONS ==========

// Resolve a hostname to IP address string
HmlValue hml_dns_resolve(HmlValue hostname_val) {
    if (hostname_val.type != HML_VAL_STRING) {
        hml_runtime_error("dns_resolve() requires a string hostname");
    }

    HmlString *str = hostname_val.as.as_string;
    // Create null-terminated string
    char *hostname = malloc(str->length + 1);
    if (!hostname) {
        hml_runtime_error("dns_resolve() memory allocation failed");
    }
    memcpy(hostname, str->data, str->length);
    hostname[str->length] = '\0';

    struct hostent *host = gethostbyname(hostname);
    free(hostname);

    if (!host) {
        hml_runtime_error("Failed to resolve hostname");
    }

    // Return first IPv4 address
    char *ip = inet_ntoa(*(struct in_addr *)host->h_addr_list[0]);
    return hml_val_string(ip);
}

// ========== BUILTIN WRAPPERS ==========

HmlValue hml_builtin_dns_resolve(HmlClosureEnv *env, HmlValue hostname) {
    (void)env;
    return hml_dns_resolve(hostname);
}

HmlValue hml_builtin_socket_create(HmlClosureEnv *env, HmlValue domain, HmlValue sock_type, HmlValue protocol) {
    (void)env;
    return hml_socket_create(domain, sock_type, protocol);
}

HmlValue hml_builtin_socket_bind(HmlClosureEnv *env, HmlValue socket_val, HmlValue address, HmlValue port) {
    (void)env;
    hml_socket_bind(socket_val, address, port);
    return hml_val_null();
}

HmlValue hml_builtin_socket_listen(HmlClosureEnv *env, HmlValue socket_val, HmlValue backlog) {
    (void)env;
    hml_socket_listen(socket_val, backlog);
    return hml_val_null();
}

HmlValue hml_builtin_socket_accept(HmlClosureEnv *env, HmlValue socket_val) {
    (void)env;
    return hml_socket_accept(socket_val);
}

HmlValue hml_builtin_socket_connect(HmlClosureEnv *env, HmlValue socket_val, HmlValue address, HmlValue port) {
    (void)env;
    hml_socket_connect(socket_val, address, port);
    return hml_val_null();
}

HmlValue hml_builtin_socket_close(HmlClosureEnv *env, HmlValue socket_val) {
    (void)env;
    hml_socket_close(socket_val);
    return hml_val_null();
}

HmlValue hml_builtin_socket_send(HmlClosureEnv *env, HmlValue socket_val, HmlValue data) {
    (void)env;
    return hml_socket_send(socket_val, data);
}

HmlValue hml_builtin_socket_recv(HmlClosureEnv *env, HmlValue socket_val, HmlValue size) {
    (void)env;
    return hml_socket_recv(socket_val, size);
}

HmlValue hml_builtin_socket_sendto(HmlClosureEnv *env, HmlValue socket_val, HmlValue address, HmlValue port, HmlValue data) {
    (void)env;
    return hml_socket_sendto(socket_val, address, port, data);
}

HmlValue hml_builtin_socket_recvfrom(HmlClosureEnv *env, HmlValue socket_val, HmlValue size) {
    (void)env;
    return hml_socket_recvfrom(socket_val, size);
}

HmlValue hml_builtin_socket_setsockopt(HmlClosureEnv *env, HmlValue socket_val, HmlValue level, HmlValue option, HmlValue value) {
    (void)env;
    hml_socket_setsockopt(socket_val, level, option, value);
    return hml_val_null();
}

HmlValue hml_builtin_socket_get_fd(HmlClosureEnv *env, HmlValue socket_val) {
    (void)env;
    return hml_socket_get_fd(socket_val);
}

HmlValue hml_builtin_socket_get_address(HmlClosureEnv *env, HmlValue socket_val) {
    (void)env;
    return hml_socket_get_address(socket_val);
}

HmlValue hml_builtin_socket_get_port(HmlClosureEnv *env, HmlValue socket_val) {
    (void)env;
    return hml_socket_get_port(socket_val);
}

HmlValue hml_builtin_socket_get_closed(HmlClosureEnv *env, HmlValue socket_val) {
    (void)env;
    return hml_socket_get_closed(socket_val);
}
