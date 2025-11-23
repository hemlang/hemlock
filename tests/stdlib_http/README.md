# HTTP Module Tests

Comprehensive test suite for the `@stdlib/http` module.

## Requirements

- **libwebsockets-dev** package installed
- Compiled lws_wrapper.so (`make stdlib`)
- Network connectivity (for request tests)

## Installation

```bash
# Ubuntu/Debian
sudo apt-get install libwebsockets-dev

# Build the FFI wrapper
make stdlib
```

## Test Files

### test_http_basic.hml

Tests HTTP module functionality **without network connectivity**:
- Status code helper functions (is_success, is_redirect, is_client_error, is_server_error)
- URL encoding function

**Run:**
```bash
./hemlock tests/stdlib_http/test_http_basic.hml
```

**Expected output:**
```
Testing @stdlib/http basic functionality...

Test 1: is_success()
✓ is_success() works correctly

Test 2: is_redirect()
✓ is_redirect() works correctly

...

========================================
HTTP Basic Tests Summary:
  Passed: 5
  Failed: 0
========================================
```

### test_http_requests.hml

Tests actual HTTP requests over the network:
- HTTP GET requests
- HTTP POST requests
- fetch() convenience function
- post_json() JSON requests
- HTTPS requests (SSL/TLS)

**Run:**
```bash
./hemlock tests/stdlib_http/test_http_requests.hml
```

**Expected output:**
```
Testing HTTP requests with libwebsockets...

Test 1: HTTP GET request
✓ GET request successful (status: i32)

Test 2: HTTP POST request
✓ POST request successful (status: i32)

...

========================================
HTTP Request Tests Summary:
  Passed: 5
  Failed: 0
========================================
```

**Note:** These tests require:
- Network connectivity
- Access to httpbin.org (or modify test URLs)
- libwebsockets-dev installed and compiled

## Common Issues

### "Failed to load stdlib/c/lws_wrapper.so"
**Solution:** Run `make stdlib` to compile the FFI wrapper

### "libwebsockets.so: cannot open shared object file"
**Solution:** Install libwebsockets-dev:
```bash
sudo apt-get install libwebsockets-dev
```

### Network tests fail
**Solutions:**
- Check internet connectivity
- Verify httpbin.org is accessible
- Check firewall rules
- Try running with a shorter timeout

### SSL/TLS errors
**Solutions:**
- Ensure ca-certificates package is installed
- Check system time is correct (SSL certificates are time-sensitive)
- libwebsockets uses system certificate store

## Test Coverage

| Feature | Basic Test | Request Test |
|---------|------------|--------------|
| Module loading | ✓ | ✓ |
| is_success() | ✓ | ✓ |
| is_redirect() | ✓ | - |
| is_client_error() | ✓ | - |
| is_server_error() | ✓ | - |
| url_encode() | ✓ | - |
| get() | - | ✓ |
| post() | - | ✓ |
| fetch() | - | ✓ |
| post_json() | - | ✓ |
| HTTPS | - | ✓ |

## Extending Tests

To add new tests:

1. Create new test file in `tests/stdlib_http/`
2. Import the http module:
   ```hemlock
   import * as http from "@stdlib/http";
   ```
3. Use assert() for validation
4. Follow the test structure (setup, test, assertions, summary)

## See Also

- [HTTP Module Documentation](../../stdlib/docs/http.md)
- [WebSocket Tests](../stdlib_websocket/README.md)
- [libwebsockets Documentation](https://libwebsockets.org/)
