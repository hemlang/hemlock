# Hemlock Stdlib C Modules

This directory contains optional C FFI wrappers for stdlib modules that benefit from native library integration.

## libwebsockets Wrapper

### Purpose

`lws_wrapper.c` provides a simplified FFI interface to libwebsockets for both HTTP and WebSocket functionality.

### Requirements

```bash
# Ubuntu/Debian
sudo apt-get install libwebsockets-dev

# macOS
brew install libwebsockets

# Arch Linux
sudo pacman -S libwebsockets
```

### Building

```bash
# From hemlock root directory
make stdlib
```

This compiles `lws_wrapper.c` → `lws_wrapper.so`

### Usage

Two stdlib modules can use this wrapper:

1. **@stdlib/http** (`stdlib/http_lws.hml`) - HTTP client with SSL
2. **@stdlib/websocket** (`stdlib/websocket.hml`) - WebSocket client/server with SSL

### Current Status

- ✅ C wrapper code complete
- ⏳ Requires libwebsockets installation
- ⏳ Hemlock FFI modules need testing

### Alternative: Pure Hemlock Implementations

If you don't want to install libwebsockets, use the pure Hemlock versions:

- **@stdlib/http** (`stdlib/http.hml`) - Uses curl via `exec()` (already works!)
- **@stdlib/websocket_pure** (`stdlib/websocket_pure.hml`) - Pure Hemlock WebSocket (educational, no SSL)

The pure versions work TODAY without any C dependencies!

## Future C Modules

Planned FFI wrappers:
- OpenSSL wrapper for crypto functions
- zlib wrapper for compression
- SQLite wrapper for embedded database

## Design Philosophy

Hemlock stdlib follows a **hybrid approach**:

1. **Pure Hemlock first** - Most modules are pure Hemlock (no C dependencies)
2. **C FFI for performance** - Optional C wrappers for speed/features
3. **Graceful fallbacks** - Modules work without C libraries when possible

This aligns with Hemlock's philosophy of explicit control and educational value.
