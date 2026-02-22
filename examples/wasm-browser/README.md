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

### Using the JS API wrapper (recommended)

The `hemlock-api.js` module provides a clean JavaScript API over the raw
Emscripten exports. It works in both browser and Node.js.

**Browser:**

```html
<script src="hemlock-api.js"></script>
<script src="hemlock.js"></script>
<script>
// Configure Emscripten Module for output capture
var Module = {
    print: function(text) { console.log(text); },
    printErr: function(text) { console.error(text); },
    onRuntimeInitialized: function() {
        Hemlock.init({ Module: Module }).then(function(hemlock) {
            // Stateless eval
            hemlock.eval('print("Hello from WASM!");');
            console.log('Version:', hemlock.version);

            // Persistent context
            var ctx = hemlock.createContext();
            ctx.eval('let score = 0;');
            ctx.eval('score = score + 10;');
            console.log(ctx.get('score'));      // 10 (parsed JS value)

            // Inject JS values
            ctx.set('player', { name: "Alice", hp: 100 });
            ctx.eval('print(player.name + " has " + player.hp + " hp");');

            // Error handling
            var result = ctx.eval('throw "oops";');
            if (!result.ok) {
                console.error(result.error);   // "oops"
            }

            // Cached scripts (parse once, run many)
            var tick = hemlock.compile('score = score + 1;');
            tick.run(ctx);
            tick.run(ctx);
            console.log(ctx.get('score'));      // 12
            tick.free();

            ctx.destroy();
        });
    },
    noInitialRun: true
};
</script>
```

**Node.js:**

```javascript
const { Hemlock } = require('./wasm/hemlock-api.js');

async function main() {
    const hemlock = await Hemlock.init('./wasm/hemlock.js');

    hemlock.eval('print("Hello from Node!");');

    const ctx = hemlock.createContext();
    ctx.eval('let x = 42;');
    ctx.set('y', 8);
    ctx.eval('let sum = x + y;');
    console.log(ctx.get('sum'));  // 50
    ctx.destroy();
}

main();
```

### API Reference

#### `Hemlock` (main entry point)

| Method | Returns | Description |
|--------|---------|-------------|
| `Hemlock.init(options?)` | `Promise<Hemlock>` | Initialize the WASM module and return an API instance |
| `hemlock.eval(source)` | `number` | Execute code (stateless, one-shot). Returns 0 on success |
| `hemlock.version` | `string` | Hemlock version string |
| `hemlock.createContext()` | `HemlockContext` | Create a persistent interpreter context |
| `hemlock.compile(source)` | `HemlockScript` | Pre-compile source into a cached script |
| `hemlock.getModule()` | `object` | Access the raw Emscripten Module |

**`Hemlock.init()` options:**

| Option | Type | Description |
|--------|------|-------------|
| `wasmUrl` | `string` | Path to `hemlock.js` (Node.js) |
| `Module` | `object` | Pre-configured Emscripten Module (browser) |
| `print` | `function` | stdout handler |
| `printErr` | `function` | stderr handler |

#### `HemlockContext` (persistent state)

| Method | Returns | Description |
|--------|---------|-------------|
| `ctx.eval(source)` | `{ ok, error }` | Execute code; variables persist across calls |
| `ctx.get(name)` | `any` | Read variable as parsed JS value, or `undefined` |
| `ctx.getJSON(name)` | `string\|null` | Read variable as raw JSON string |
| `ctx.set(name, value)` | `boolean` | Inject a JS value (auto-serialized to JSON) |
| `ctx.setJSON(name, json)` | `boolean` | Inject a raw JSON string |
| `ctx.lastError()` | `string\|null` | Error from last failed eval/run |
| `ctx.destroy()` | `void` | Free all context memory |

#### `HemlockScript` (cached AST)

| Method | Returns | Description |
|--------|---------|-------------|
| `script.run(ctx)` | `{ ok, error }` | Execute cached script in a context |
| `script.free()` | `void` | Release the cached AST |

### Low-level API (cwrap)

For advanced use cases, the raw Emscripten exports are still available via
`Module.cwrap()`. See the C function signatures in `wasm_interp_main.c`.

| Function | Returns | Description |
|----------|---------|-------------|
| `hemlock_eval(source)` | `0` on success | Stateless eval |
| `hemlock_version()` | version string | Version |
| `hemlock_context_create()` | handle (>0) or 0 | Allocate persistent environment |
| `hemlock_context_eval(h, src)` | 0=ok, 1=parse err, 2=runtime err, -1=bad handle | Execute in context |
| `hemlock_context_get(h, var)` | JSON string or null | Read variable as JSON |
| `hemlock_context_set(h, var, json)` | 0=ok, 1=error, -1=bad handle | Inject value from JSON |
| `hemlock_context_last_error(h)` | string or null | Last error message |
| `hemlock_context_destroy(h)` | void | Free context |
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
