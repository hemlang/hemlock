# @stdlib/unix_socket - Unix Domain Socket Module

Unix domain sockets for local inter-process communication over filesystem paths.

## Overview

The `@stdlib/unix_socket` module provides high-level abstractions for Unix domain sockets (AF_UNIX):

- **UnixListener** - Stream socket server (like TcpListener but over a file path)
- **UnixStream** - Stream socket client/connection
- **UnixDgramSocket** - Datagram socket for connectionless messaging
- **remove_socket** - Helper to clean up socket files

Unix domain sockets are faster than TCP loopback for local IPC because they skip the network stack entirely.

## Quick Start

### Stream Echo Server

```hemlock
import { UnixListener } from "@stdlib/unix_socket";

let listener = UnixListener("/tmp/echo.sock");
defer listener.close();
defer listener.remove();

let stream = listener.accept();
defer stream.close();

let data = stream.read(1024);
stream.write(data);  // Echo back
```

### Stream Client

```hemlock
import { UnixStream } from "@stdlib/unix_socket";

let stream = UnixStream("/tmp/echo.sock");
defer stream.close();

stream.write("Hello from client!");
let reply = stream.read(1024);
```

### Datagram Sockets

```hemlock
import { UnixDgramSocket } from "@stdlib/unix_socket";

// Receiver
let receiver = UnixDgramSocket("/tmp/recv.sock");
defer receiver.close();
defer receiver.remove();

// Sender
let sender = UnixDgramSocket("/tmp/send.sock");
defer sender.close();
defer sender.remove();

sender.send_to("/tmp/recv.sock", "Hello!");
let packet = receiver.recv_from(1024);
```

---

## API Reference

### UnixListener

Unix domain stream socket server.

#### Constructor

**`UnixListener(path: string) -> UnixListener`**

Creates a Unix domain stream socket bound to `path` and starts listening.

- Automatically removes any existing socket file at `path` before binding
- Default backlog of 128 pending connections
- Throws on bind failure

```hemlock
let listener = UnixListener("/tmp/myapp.sock");
```

#### Methods

**`accept() -> UnixStream`**

Blocks until a client connects, returns a UnixStream for the connection.

```hemlock
let stream = listener.accept();
defer stream.close();
```

**`close() -> null`**

Closes the listener socket. Idempotent.

**`remove() -> null`**

Removes the socket file from the filesystem. Call after closing.

```hemlock
listener.close();
listener.remove();
```

#### Properties

- `path: string` - Socket file path

---

### UnixStream

Unix domain stream connection for bidirectional data transfer.

#### Constructor

**`UnixStream(path: string) -> UnixStream`**

Connects to a Unix domain stream socket at `path`.

```hemlock
let stream = UnixStream("/tmp/myapp.sock");
defer stream.close();
```

#### Methods

**`read(size: i32) -> buffer`**

Reads up to `size` bytes. Returns empty buffer on EOF.

**`read_all() -> buffer`**

Reads all available data (4KB chunks until no more data).

**`read_line() -> string`**

Reads until newline (`\n`) and returns as string.

**`write(data: string | buffer) -> i32`**

Writes data, returns bytes written.

**`write_line(line: string) -> i32`**

Writes string followed by newline.

**`set_timeout(seconds: f64) -> null`**

Sets read/write timeout.

**`close() -> null`**

Closes the stream. Idempotent.

#### Properties

- `path: string` - Socket file path
- `peer_addr: string` - Peer address (may be empty for Unix sockets)

---

### UnixDgramSocket

Unix domain datagram socket for connectionless messaging.

#### Constructor

**`UnixDgramSocket(path: string) -> UnixDgramSocket`**

Creates a Unix datagram socket bound to `path`.

- Automatically removes any existing socket file at `path` before binding
- Each datagram is delivered atomically (no partial reads/writes)

```hemlock
let sock = UnixDgramSocket("/tmp/myapp_dgram.sock");
defer sock.close();
defer sock.remove();
```

#### Methods

**`send_to(dest_path: string, data: string | buffer) -> i32`**

Sends datagram to the socket at `dest_path`. Returns bytes sent.

```hemlock
sock.send_to("/tmp/other.sock", "Hello!");
```

**`recv_from(size: i32) -> { data: buffer, address: string }`**

Receives datagram up to `size` bytes. Returns object with:
- `data: buffer` - Received data
- `address: string` - Source socket path

```hemlock
let packet = sock.recv_from(1024);
print("From: " + packet.address);
```

**`set_timeout(seconds: f64) -> null`**

Sets receive timeout.

**`close() -> null`**

Closes the socket. Idempotent.

**`remove() -> null`**

Removes the socket file from the filesystem.

#### Properties

- `path: string` - Socket file path

---

### Helper Functions

#### remove_socket

**`remove_socket(path: string) -> null`**

Removes a socket file from the filesystem. No-op if file doesn't exist.

```hemlock
import { remove_socket } from "@stdlib/unix_socket";

remove_socket("/tmp/myapp.sock");
```

---

## Constants

| Constant | Description |
|----------|-------------|
| `AF_UNIX` | Unix domain socket address family |
| `SOCK_STREAM` | Stream socket type |
| `SOCK_DGRAM` | Datagram socket type |

---

## Resource Management

Always use `defer` to clean up sockets:

```hemlock
let listener = UnixListener("/tmp/myapp.sock");
defer listener.close();
defer listener.remove();  // Clean up socket file

let stream = listener.accept();
defer stream.close();
```

Socket files persist on the filesystem after the process exits. Always call `remove()` or `remove_socket()` to clean up.

---

## Common Patterns

### Async Server with Multiple Clients

```hemlock
import { UnixListener } from "@stdlib/unix_socket";

async fn handle_client(stream) {
    defer stream.close();

    while (true) {
        let data = stream.read(1024);
        if (data.length == 0) {
            break;  // Client disconnected
        }
        stream.write(data);  // Echo back
    }
}

let listener = UnixListener("/tmp/echo.sock");
defer listener.close();
defer listener.remove();

while (true) {
    let stream = listener.accept();
    spawn(handle_client, stream);
}
```

### Request-Reply over Datagrams

```hemlock
import { UnixDgramSocket } from "@stdlib/unix_socket";

// Server
async fn server() {
    let sock = UnixDgramSocket("/tmp/server.sock");
    defer sock.close();
    defer sock.remove();

    let req = sock.recv_from(1024);
    sock.send_to(req.address, "REPLY");
}

// Client
async fn client() {
    let sock = UnixDgramSocket("/tmp/client.sock");
    defer sock.close();
    defer sock.remove();

    sock.send_to("/tmp/server.sock", "REQUEST");
    let reply = sock.recv_from(1024);
}
```

---

## Error Handling

```hemlock
try {
    let stream = UnixStream("/tmp/nonexistent.sock");
} catch (e) {
    print("Connection failed: " + e);
}
```

Common errors:
- **"No such file or directory"** - Socket file doesn't exist
- **"Connection refused"** - No server listening on that socket
- **"Address already in use"** - Socket file already exists (UnixListener handles this automatically)
- **"Unix socket path too long"** - Path exceeds system limit (typically 108 chars)

---

## Performance Notes

- Unix domain sockets are significantly faster than TCP loopback (`127.0.0.1`)
- No TCP/IP overhead (no checksums, no routing, no congestion control)
- Stream sockets guarantee ordered, reliable delivery (like TCP)
- Datagram sockets guarantee atomic message delivery (unlike UDP, no packet loss on localhost)
- Ideal for microservice communication on the same host

---

## See Also

- `@stdlib/net` - TCP/UDP networking (for network communication)
- `@stdlib/ipc` - File-based IPC primitives (message queues, semaphores)
