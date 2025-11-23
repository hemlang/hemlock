# WebSocket Module Tests

Comprehensive test suite for the `@stdlib/websocket` module.

## Requirements

- **libwebsockets-dev** package installed
- Compiled lws_wrapper.so (`make stdlib`)
- Network connectivity (for echo server test)

## Installation

```bash
# Ubuntu/Debian
sudo apt-get install libwebsockets-dev

# macOS
brew install libwebsockets

# Build the FFI wrapper
make stdlib
```

## Test Files

### test_websocket_basic.hml

Tests WebSocket module **without network connectivity**:
- Module loading and imports
- is_secure_url() utility function
- parse_ws_url() URL parsing (ws:// and wss://)
- Error handling for invalid URLs

**Run:**
```bash
./hemlock tests/stdlib_websocket/test_websocket_basic.hml
```

**Expected output:**
```
Testing @stdlib/websocket basic functionality...

Test 1: Module import
✓ Module loaded successfully

Test 2: is_secure_url() function
✓ is_secure_url() works correctly

...

========================================
WebSocket Basic Tests Summary:
  Passed: 5
  Failed: 0
========================================
```

### test_websocket_echo.hml

Tests WebSocket client with public echo server:
- Connect to ws://echo.websocket.org
- Send and receive text messages
- Multiple message handling
- Connection close

**Run:**
```bash
./hemlock tests/stdlib_websocket/test_websocket_echo.hml
```

**Expected output:**
```
Testing WebSocket client with echo server...

Test 1: Connect to echo server
✓ Connected to ws://echo.websocket.org

Test 2: Send and receive text message
  Sent: Hello WebSocket!
  Received: Hello WebSocket!
✓ Echo test passed

...

========================================
WebSocket Echo Tests Summary:
  Passed: 4
  Failed: 0
========================================
```

**Note:** Requires network connectivity and echo.websocket.org to be accessible.

### test_websocket_server.hml

Tests WebSocket server functionality:
- Create WebSocket server
- Client-server connection
- Client to server messaging
- Server to client messaging
- Bidirectional echo

**Run:**
```bash
./hemlock tests/stdlib_websocket/test_websocket_server.hml
```

**Expected output:**
```
Testing WebSocket server...

Test 1: Create WebSocket server
✓ Server created on 127.0.0.1:9001

Test 2: Client connects to server
  Server: Waiting for client...
  Client: Connecting...
  Client: Connected
  Server: Client connected
✓ Client-server connection established

...

========================================
WebSocket Server Tests Summary:
  Passed: 5
  Failed: 0
========================================
```

**Note:** Uses local loopback (127.0.0.1), no external network required.

## Common Issues

### "Failed to load stdlib/c/lws_wrapper.so"
**Solution:** Run `make stdlib` to compile the FFI wrapper

### "libwebsockets.so: cannot open shared object file"
**Solution:** Install libwebsockets-dev:
```bash
# Ubuntu/Debian
sudo apt-get install libwebsockets-dev

# macOS
brew install libwebsockets
```

### Connection timeout on echo test
**Solutions:**
- Check internet connectivity
- Verify echo.websocket.org is accessible: `ping echo.websocket.org`
- Check firewall rules
- Try using a different public echo server

### "Port already in use" (server test)
**Solutions:**
- Change server port in test file (default: 9001)
- Kill process using the port: `lsof -ti:9001 | xargs kill`
- Wait a few seconds for OS to release the port

### SSL/TLS errors on wss:// connections
**Solutions:**
- Ensure ca-certificates package is installed
- Check system time is correct (certificates are time-sensitive)
- libwebsockets uses system certificate store by default

## Test Coverage

| Feature | Basic | Echo | Server |
|---------|-------|------|--------|
| Module loading | ✓ | ✓ | ✓ |
| is_secure_url() | ✓ | - | - |
| parse_ws_url() | ✓ | - | - |
| WebSocket client | - | ✓ | ✓ |
| send_text() | - | ✓ | ✓ |
| recv() | - | ✓ | ✓ |
| close() | - | ✓ | ✓ |
| WebSocketServer | - | - | ✓ |
| accept() | - | - | ✓ |
| Bidirectional messaging | - | - | ✓ |
| Async/concurrency | - | - | ✓ |

## Performance Testing

For load testing or benchmarking, see the examples:
- `examples/websocket_client_lws.hml` - Basic client
- `examples/websocket_server_lws.hml` - Echo server with concurrent clients

## Extending Tests

To add new tests:

1. Create new test file in `tests/stdlib_websocket/`
2. Import the WebSocket module:
   ```hemlock
   import { WebSocket, WebSocketServer } from "@stdlib/websocket";
   ```
3. Use assert() for validation
4. Follow the test structure (setup, test, assertions, summary)

### Example test template:

```hemlock
import { WebSocket } from "@stdlib/websocket";

print("Testing custom WebSocket feature...");
let tests_passed = 0;
let tests_failed = 0;

try {
    // Your test code here
    assert(condition, "Description");
    tests_passed = tests_passed + 1;
} catch (e) {
    print("✗ Test failed: " + e);
    tests_failed = tests_failed + 1;
}

print("========================================");
print("Tests Summary:");
print("  Passed: " + typeof(tests_passed));
print("  Failed: " + typeof(tests_failed));
print("========================================");
```

## See Also

- [WebSocket Module Documentation](../../stdlib/docs/websocket.md)
- [HTTP Tests](../stdlib_http/README.md)
- [libwebsockets Documentation](https://libwebsockets.org/)
- [WebSocket Protocol (RFC 6455)](https://tools.ietf.org/html/rfc6455)
