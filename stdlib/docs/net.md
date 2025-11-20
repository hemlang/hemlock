# @stdlib/net - Networking Module

TCP/UDP networking module providing ergonomic wrappers over raw socket builtins.

## Overview

The `@stdlib/net` module provides high-level networking abstractions for TCP and UDP communication:

- **TcpListener** - TCP server socket
- **TcpStream** - TCP client connection
- **UdpSocket** - UDP datagram socket
- **Helper functions** - DNS resolution and utilities

## Quick Start

### TCP Echo Server

```hemlock
import { TcpListener } from "@stdlib/net";

async fn handle_client(stream) {
    defer stream.close();

    let data = stream.read(1024);
    stream.write(data);  // Echo back
}

let listener = TcpListener("0.0.0.0", 8080);
defer listener.close();

while (true) {
    let stream = listener.accept();
    spawn(handle_client, stream);  // Handle concurrently
}
```

### TCP Client

```hemlock
import { TcpStream } from "@stdlib/net";

let stream = TcpStream("example.com", 80);
defer stream.close();

stream.write("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");
let response = stream.read(4096);
```

### UDP Socket

```hemlock
import { UdpSocket } from "@stdlib/net";

let sock = UdpSocket("0.0.0.0", 5000);
defer sock.close();

// Receive datagram
let packet = sock.recv_from(1024);
print("Received from " + packet.address + ":" + typeof(packet.port));

// Send reply
sock.send_to(packet.address, packet.port, "Reply");
```

---

## API Reference

### TcpListener

TCP server socket for accepting incoming connections.

#### Constructor

**`TcpListener(address: string, port: i32) -> TcpListener`**

Creates a TCP server socket bound to `address:port` and starts listening.

- Automatically sets `SO_REUSEADDR` to avoid "Address already in use" errors
- Default backlog of 128 pending connections
- Throws exception on bind failure

```hemlock
let listener = TcpListener("127.0.0.1", 8080);
let listener2 = TcpListener("0.0.0.0", 9000);  // Listen on all interfaces
```

#### Methods

**`accept() -> TcpStream`**

Blocks until a client connects, returns TcpStream for the connection.

```hemlock
let stream = listener.accept();
defer stream.close();
```

**`close() -> null`**

Closes the listener socket. Idempotent (safe to call multiple times).

```hemlock
listener.close();
```

#### Properties

- `address: string` - Bound address
- `port: i32` - Bound port

---

### TcpStream

TCP connection stream for bidirectional data transfer.

#### Constructors

**`TcpStream(address: string, port: i32) -> TcpStream`**

Connects to a TCP server at `address:port`.

- Performs DNS resolution if hostname is provided
- Throws exception on connection failure

```hemlock
let stream = TcpStream("example.com", 80);
let stream2 = TcpStream("192.168.1.1", 9000);
```

**`_TcpStream_from_socket(sock: Socket) -> TcpStream`**

Internal constructor used by `TcpListener.accept()`. Creates TcpStream from existing socket.

#### Methods

**`read(size: i32) -> buffer`**

Reads up to `size` bytes from the stream. Returns buffer with received data.

- Returns empty buffer (length 0) on EOF
- May return less than `size` bytes

```hemlock
let data = stream.read(1024);
print("Received " + typeof(data.length) + " bytes");
```

**`read_all() -> buffer`**

Reads all available data from the stream (up to 64KB).

- Reads in 4KB chunks until no more data is immediately available
- Useful for reading complete messages

```hemlock
let all_data = stream.read_all();
```

**`read_line() -> string`**

Reads until newline (`\n`) and returns as string.

**Note:** Current implementation is simplified. Future versions will properly handle line buffering.

**`write(data: string | buffer) -> i32`**

Writes `data` to the stream. Returns number of bytes written.

```hemlock
let sent = stream.write("Hello, server!");
let buf = buffer(10);
let sent2 = stream.write(buf);
```

**`write_line(line: string) -> i32`**

Writes `line` followed by newline (`\n`). Returns number of bytes written.

```hemlock
stream.write_line("GET / HTTP/1.1");
stream.write_line("Host: example.com");
```

**`set_timeout(seconds: f64) -> null`**

Sets read/write timeout in seconds. Supports fractional seconds.

```hemlock
stream.set_timeout(5.0);   // 5 second timeout
stream.set_timeout(0.5);   // 500ms timeout
```

**`close() -> null`**

Closes the stream. Idempotent.

```hemlock
stream.close();
```

#### Properties

- `peer_addr: string` - Remote address (or null for client before connection)
- `peer_port: i32` - Remote port

---

### UdpSocket

UDP datagram socket for connectionless communication.

#### Constructor

**`UdpSocket(address: string, port: i32) -> UdpSocket`**

Creates a UDP socket bound to `address:port`.

- Bind to `"0.0.0.0"` to listen on all interfaces
- Bind to port `0` to let OS assign a random port

```hemlock
let sock = UdpSocket("0.0.0.0", 5000);  // Server
let sock2 = UdpSocket("0.0.0.0", 0);    // Client (any port)
```

#### Methods

**`send_to(dest_addr: string, dest_port: i32, data: string | buffer) -> i32`**

Sends datagram to `dest_addr:dest_port`. Returns number of bytes sent.

```hemlock
let sent = sock.send_to("192.168.1.100", 9000, "Hello UDP!");
```

**`recv_from(size: i32) -> { data: buffer, address: string, port: i32 }`**

Receives datagram up to `size` bytes. Returns object with:
- `data: buffer` - Received data
- `address: string` - Source IP address
- `port: i32` - Source port

```hemlock
let packet = sock.recv_from(1024);
print("From " + packet.address + ":" + typeof(packet.port));
print("Data: " + typeof(packet.data.length) + " bytes");
```

**`set_timeout(seconds: f64) -> null`**

Sets receive timeout in seconds.

```hemlock
sock.set_timeout(2.0);  // 2 second timeout
```

**`close() -> null`**

Closes the socket. Idempotent.

```hemlock
sock.close();
```

#### Properties

- `address: string` - Bound address
- `port: i32` - Bound port

---

## Helper Functions

### resolve

**`resolve(hostname: string) -> string`**

Resolves hostname to IPv4 address.

```hemlock
import { resolve } from "@stdlib/net";

let ip = resolve("localhost");      // "127.0.0.1"
let ip2 = resolve("example.com");   // "93.184.216.34"
```

### is_ipv4

**`is_ipv4(address: string) -> bool`**

Checks if `address` is a valid IPv4 address format.

**Note:** Current implementation is simplified. Returns true for basic validation.

```hemlock
import { is_ipv4 } from "@stdlib/net";

print(is_ipv4("127.0.0.1"));     // true
print(is_ipv4("example.com"));   // false (simplified check)
```

### parse_address

**`parse_address(addr_str: string) -> { host: string, port: i32 } | null`**

Parses `"host:port"` string into components.

**Note:** Current implementation returns placeholder port value. Future versions will properly parse port numbers.

```hemlock
import { parse_address } from "@stdlib/net";

let parts = parse_address("example.com:8080");
if (parts != null) {
    print("Host: " + parts.host);
    print("Port: " + typeof(parts.port));
}
```

---

## Common Patterns

### Async Echo Server

```hemlock
import { TcpListener } from "@stdlib/net";

async fn handle_client(stream) {
    defer stream.close();

    while (true) {
        let data = stream.read(1024);
        if (data.length == 0) {
            break;  // Client disconnected
        }
        stream.write(data);
    }
}

let listener = TcpListener("0.0.0.0", 8080);
defer listener.close();

print("Echo server listening on port 8080");

while (true) {
    let stream = listener.accept();
    spawn(handle_client, stream);
}
```

### HTTP Client (Simple GET)

```hemlock
import { TcpStream } from "@stdlib/net";

fn http_get(host: string, path: string) {
    let stream = TcpStream(host, 80);
    defer stream.close();

    stream.set_timeout(10.0);  // 10 second timeout

    let request = "GET " + path + " HTTP/1.1\r\n";
    request = request + "Host: " + host + "\r\n";
    request = request + "Connection: close\r\n\r\n";

    stream.write(request);

    let response = stream.read_all();
    return response;
}

let html = http_get("example.com", "/");
```

### UDP Echo Server

```hemlock
import { UdpSocket } from "@stdlib/net";

let sock = UdpSocket("0.0.0.0", 5000);
defer sock.close();

print("UDP echo server listening on port 5000");

while (true) {
    let packet = sock.recv_from(1024);
    print("Echo from " + packet.address + ":" + typeof(packet.port));
    sock.send_to(packet.address, packet.port, packet.data);
}
```

### Timeout Handling

```hemlock
import { TcpStream } from "@stdlib/net";

let stream = TcpStream("example.com", 80);
defer stream.close();

stream.set_timeout(5.0);  // 5 second timeout

try {
    let data = stream.read(1024);
    print("Received data");
} catch (e) {
    print("Timeout or error: " + e);
}
```

---

## Resource Management

Always use `defer` to ensure sockets are closed:

```hemlock
let listener = TcpListener("0.0.0.0", 8080);
defer listener.close();  // Guaranteed cleanup

let stream = TcpStream("example.com", 80);
defer stream.close();    // Guaranteed cleanup

let sock = UdpSocket("0.0.0.0", 5000);
defer sock.close();      // Guaranteed cleanup
```

Exception handling with cleanup:

```hemlock
let stream = TcpStream("example.com", 80);
defer stream.close();  // Runs even if exception is thrown

try {
    stream.write("data");
    let response = stream.read(1024);
} catch (e) {
    print("Error: " + e);
}
// stream.close() called automatically here
```

---

## Error Handling

All networking operations can throw exceptions:

```hemlock
try {
    let listener = TcpListener("0.0.0.0", 8080);
    defer listener.close();
    // ... server code
} catch (e) {
    print("Failed to start server: " + e);
}

try {
    let stream = TcpStream("example.com", 80);
    defer stream.close();
    stream.write("data");
} catch (e) {
    print("Connection failed: " + e);
}
```

Common errors:
- **"Address already in use"** - Port is already bound (use `SO_REUSEADDR` or different port)
- **"Connection refused"** - No server listening on that port
- **"Connection reset"** - Remote peer closed connection unexpectedly
- **"Failed to resolve hostname"** - DNS lookup failed

---

## Performance Tips

1. **Reuse connections** when making multiple requests to same host
2. **Use async/spawn** for handling multiple clients concurrently
3. **Set appropriate timeouts** to avoid hanging on slow/dead connections
4. **Buffer I/O** - batch small writes when possible
5. **Use read() with size** instead of read_all() when you know message size

---

## Implementation Notes

- Built on POSIX sockets (IPv4 only in current version)
- DNS resolution uses `gethostbyname()`
- All blocking operations can be interrupted by timeouts
- Thread-safe - works with Hemlock's async/spawn concurrency
- Manual resource management (explicit close required)

---

## See Also

- Low-level socket builtins: `socket_create()`, `dns_resolve()`
- Socket constants: `AF_INET`, `SOCK_STREAM`, `SOCK_DGRAM`, `SO_REUSEADDR`
- Related modules: `@stdlib/http`, `@stdlib/websocket` (coming soon)
