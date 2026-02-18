#!/usr/bin/env bash
#
# Hemlock WASM Browser Example - Local Development Server
#
# Starts a local HTTP server from the project root with proper MIME types
# for WebAssembly files, then opens the browser example.
#
# Usage:
#   ./examples/wasm-browser/serve.sh [port]
#
# Prerequisites:
#   1. Build the WASM interpreter first:
#        make wasm-interpreter
#   2. Python 3 must be available
#
# Or just run:
#   make wasm-browser-example
#
set -euo pipefail

PORT="${1:-8080}"
PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

# Check that the WASM build exists
if [ ! -f "$PROJECT_ROOT/wasm/hemlock.js" ] || [ ! -f "$PROJECT_ROOT/wasm/hemlock.wasm" ]; then
    echo "Error: WASM interpreter not built yet."
    echo "Run 'make wasm-interpreter' from the project root first."
    exit 1
fi

echo "Serving Hemlock WASM browser example..."
echo "  Root:    $PROJECT_ROOT"
echo "  URL:     http://localhost:$PORT/examples/wasm-browser/index.html"
echo ""
echo "Press Ctrl+C to stop."
echo ""

cd "$PROJECT_ROOT"

python3 -c "
import http.server
import socketserver

class WasmHandler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        '.wasm': 'application/wasm',
        '.js':   'application/javascript',
        '.mjs':  'application/javascript',
    }

    def end_headers(self):
        # Add CORS headers needed for SharedArrayBuffer (threaded WASM)
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        super().end_headers()

with socketserver.TCPServer(('', $PORT), WasmHandler) as httpd:
    httpd.serve_forever()
"
