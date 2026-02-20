# Hemlock WASM Browser Example

An interactive browser-based playground that runs the Hemlock interpreter compiled to WebAssembly.

## Prerequisites

1. **Emscripten SDK** - Provides the `emcc` compiler for WASM builds

   ```bash
   git clone https://github.com/emscripten-core/emsdk.git
   cd emsdk
   ./emsdk install latest
   ./emsdk activate latest
   source ./emsdk_env.sh
   ```

   Verify with `emcc --version`.

2. **Python 3** - For the local development server (included on most systems)

No other dependencies are needed. The WASM build does not require libffi, OpenSSL, or libwebsockets.

## Quick Start

From the project root:

```bash
make wasm-browser-example
```

This will:
1. Build the WASM interpreter (`wasm/hemlock.js` + `wasm/hemlock.wasm`) if not already built
2. Start a local HTTP server on port 8080
3. Serve the example at `http://localhost:8080/examples/wasm-browser/index.html`

To use a different port:

```bash
make wasm-browser-example PORT=3000
```

## Manual Setup

If you prefer to build and serve separately:

```bash
# 1. Build the WASM interpreter
make wasm-interpreter

# 2. Start a server from the project root
#    (WASM files require application/wasm MIME type — file:// won't work)
python3 -m http.server 8080

# 3. Open in your browser
#    http://localhost:8080/examples/wasm-browser/index.html
```

## What's Included

### `index.html`

A self-contained browser application with:

- **Split-pane editor** - Write Hemlock code on the left, see output on the right
- **8 example programs** - Hello World, Type System, Arrays, Closures, Objects, Pattern Matching, Algorithms, JS API
- **Keyboard shortcuts** - Ctrl+Enter to run, Tab for indentation
- **Output capture** - `print()` shows as stdout, `eprint()` shows in red as stderr
- **Execution timing** - Shows line count and elapsed milliseconds

### `serve.sh`

A convenience script that starts a local HTTP server with:

- Correct `application/wasm` MIME type
- `Cross-Origin-Opener-Policy` and `Cross-Origin-Embedder-Policy` headers (needed for `SharedArrayBuffer`/threaded WASM)

## JavaScript API

### Quick eval (stateless)

```javascript
var Module = {
    print: function(text) { console.log(text); },
    printErr: function(text) { console.error(text); },
    onRuntimeInitialized: function() {
        var hemlockEval    = Module.cwrap('hemlock_eval',    'number', ['string']);
        var hemlockVersion = Module.cwrap('hemlock_version', 'string', []);

        console.log('Hemlock', hemlockVersion());
        hemlockEval('print("Hello from WASM!");');
    },
    noInitialRun: true
};
```

Then load the Emscripten JS file:

```html
<script src="path/to/hemlock.js"></script>
```

### Persistent Context API

Create long-lived interpreter contexts where variables survive across
multiple eval calls — ideal for REPLs, game scripting, or educational
sandboxes.

```javascript
// Wrap the C functions once after Module initializes
const ctxCreate    = Module.cwrap('hemlock_context_create',     'number', []);
const ctxEval      = Module.cwrap('hemlock_context_eval',       'number', ['number','string']);
const ctxDestroy   = Module.cwrap('hemlock_context_destroy',    null,     ['number']);
const ctxGet       = Module.cwrap('hemlock_context_get',        'string', ['number','string']);
const ctxSet       = Module.cwrap('hemlock_context_set',        'number', ['number','string','string']);
const ctxLastError = Module.cwrap('hemlock_context_last_error', 'string', ['number']);

// Create a context (returns a handle, or 0 on failure)
const ctx = ctxCreate();

// Evaluate code — variables persist across calls
ctxEval(ctx, 'let score = 0;');
ctxEval(ctx, 'score = score + 10;');
console.log(ctxGet(ctx, 'score'));  // "10"

// Inject values from JS (as JSON)
ctxSet(ctx, 'player', '{"name":"Alice","hp":100}');
ctxEval(ctx, 'print(player.name + " has " + player.hp + " hp");');

// Error handling
const rc = ctxEval(ctx, 'throw "oops";');
if (rc !== 0) {
    console.error(ctxLastError(ctx));  // "oops"
}

// Clean up
ctxDestroy(ctx);
```

| Function | Returns | Description |
|----------|---------|-------------|
| `hemlock_context_create()` | handle (>0) or 0 | Allocate a new persistent environment |
| `hemlock_context_eval(h, src)` | 0=ok, 1=parse err, 2=runtime err, -1=bad handle | Execute source in the context |
| `hemlock_context_get(h, var)` | JSON string or null | Read a variable as JSON |
| `hemlock_context_set(h, var, json)` | 0=ok, 1=error, -1=bad handle | Inject a value from JSON |
| `hemlock_context_last_error(h)` | string or null | Error message from last failed eval |
| `hemlock_context_destroy(h)` | void | Free the context |

### Cached Script API

Pre-compile Hemlock source once and execute repeatedly without
re-parsing — ideal for hot event handlers or per-frame callbacks.

```javascript
const compile    = Module.cwrap('hemlock_compile_script', 'number', ['string']);
const run        = Module.cwrap('hemlock_run_script',     'number', ['number','number']);
const freeScript = Module.cwrap('hemlock_free_script',    null,     ['number']);

const ctx    = ctxCreate();
const script = compile('print("tick");');
run(ctx, script);      // executes without re-parsing
run(ctx, script);      // same AST, no re-parse
freeScript(script);    // release AST when done
ctxDestroy(ctx);
```

| Function | Returns | Description |
|----------|---------|-------------|
| `hemlock_compile_script(src)` | handle (>0) or 0 | Parse + optimize, return script handle |
| `hemlock_run_script(ctx, script)` | 0=ok, 2=runtime err, -1/-2=bad handle | Execute cached AST |
| `hemlock_free_script(script)` | void | Free the cached AST |

## WASM Limitations

Some features are unavailable in the browser WASM environment:

| Feature | Status | Notes |
|---------|--------|-------|
| Core language | Works | Variables, functions, closures, objects, arrays, loops, etc. |
| Pattern matching | Works | Full destructuring, guards, OR patterns |
| Type annotations | Works | Runtime type checking |
| try/catch | Works | Exception handling |
| String/Array methods | Works | All 19 string + 23 array methods |
| FFI | Stubbed | No `dlopen`/shared library support |
| Crypto | Stubbed | No OpenSSL in WASM |
| File I/O | Limited | Emscripten virtual filesystem only |
| Networking | Unavailable | No raw sockets |
| Threading | Requires special build | Needs `SharedArrayBuffer` + COOP/COEP headers |

## Files

```
examples/wasm-browser/
├── index.html    # Interactive playground (self-contained HTML/CSS/JS)
├── serve.sh      # Local dev server with proper WASM headers
└── README.md     # This file
```

The WASM build artifacts are in the `wasm/` directory at the project root:

```
wasm/
├── hemlock.js    # Emscripten JS loader (generated by make wasm-interpreter)
├── hemlock.wasm  # WebAssembly binary (generated by make wasm-interpreter)
├── pre.js        # Emscripten pre-init script
├── playground.html  # Alternate playground UI
└── index.html       # Simple output viewer for compiled WASM programs
```
