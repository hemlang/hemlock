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

The wrapper is used by:

1. **@stdlib/http** (`stdlib/http.hml`) - HTTP client with SSL (uses `__lws_*` builtins)
2. **@stdlib/websocket** (`stdlib/websocket.hml`) - WebSocket client/server with SSL

### Current Status

- ✅ C wrapper code complete
- ⏳ Requires libwebsockets installation
- ⏳ Hemlock FFI modules need testing

### Note on HTTP Implementation

The `@stdlib/http` module uses libwebsockets via built-in `__lws_*` functions that are
statically linked into the Hemlock interpreter. No separate FFI wrapper is needed for HTTP.

The `lws_wrapper.so` is primarily used for WebSocket functionality.

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
