# Installation

This guide will help you build and install Hemlock on your system.

## Quick Install (Recommended)

The easiest way to install Hemlock is using the one-line install script:

```bash
curl -fsSL https://raw.githubusercontent.com/hemlang/hemlock/main/install.sh | bash
```

This downloads and installs the latest pre-built binary for your platform (Linux or macOS, x86_64 or arm64).

### Install Options

```bash
# Install to a custom prefix (default: ~/.local)
curl -fsSL https://raw.githubusercontent.com/hemlang/hemlock/main/install.sh | bash -s -- --prefix /usr/local

# Install a specific version
curl -fsSL https://raw.githubusercontent.com/hemlang/hemlock/main/install.sh | bash -s -- --version v1.6.0

# Install and automatically update shell PATH
curl -fsSL https://raw.githubusercontent.com/hemlang/hemlock/main/install.sh | bash -s -- --update-path
```

After installation, verify it works:

```bash
hemlock --version
```

---

## Building from Source

If you prefer to build from source or the pre-built binaries don't work for your system, follow the instructions below.

## Prerequisites

### Required Dependencies

Hemlock requires the following dependencies to build:

- **C Compiler**: GCC or Clang (C11 standard)
- **Make**: GNU Make
- **libffi**: Foreign Function Interface library (for FFI support)
- **OpenSSL**: Cryptographic library (for hash functions: md5, sha1, sha256)
- **libwebsockets**: WebSocket and HTTP client/server support
- **zlib**: Compression library

### Optional Dependencies

#### USearch (vector similarity search)

Required only if you want to use [`@stdlib/vector`](../../stdlib/docs/vector.md). Without it, the vector module is unavailable and those tests are skipped — everything else works fine.

> **Note:** `cmake --install` does **not** install the shared library for usearch v2.x. You must copy the files manually (see below).

**Linux:**
```bash
sudo apt-get install -y cmake  # build dependency

git clone --depth 1 --branch v2.24.0 --recurse-submodules \
  https://github.com/unum-cloud/usearch.git /tmp/usearch
cmake -B /tmp/usearch/build -S /tmp/usearch -DUSEARCH_BUILD_LIB_C=ON
cmake --build /tmp/usearch/build

sudo cp $(find /tmp/usearch/build -name 'libusearch_c.so' | head -1) /usr/local/lib/
sudo cp /tmp/usearch/c/usearch.h /usr/local/include/
sudo ldconfig
```

**macOS:**
```bash
brew install cmake  # build dependency

git clone --depth 1 --branch v2.24.0 --recurse-submodules \
  https://github.com/unum-cloud/usearch.git /tmp/usearch
cmake -B /tmp/usearch/build -S /tmp/usearch -DUSEARCH_BUILD_LIB_C=ON
cmake --build /tmp/usearch/build

sudo mkdir -p /usr/local/lib /usr/local/include
sudo cp $(find /tmp/usearch/build -name 'libusearch_c.dylib' | head -1) /usr/local/lib/
sudo cp /tmp/usearch/c/usearch.h /usr/local/include/
```

#### libwebsockets (HTTP and WebSocket client/server)

Required only for `@stdlib/http` and `@stdlib/websocket`. Without it those modules are unavailable and their tests are skipped.

---

### Installing Dependencies

**macOS:**
```bash
# Install Homebrew if not already installed
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install Xcode Command Line Tools
xcode-select --install

# Install build + runtime dependencies via Homebrew
brew install pkg-config libffi openssl@3 libwebsockets
```

**Notes for macOS users**:

- `pkg-config` is required even though it's not always listed as a Hemlock
  dependency. The Makefile uses it to detect libwebsockets; the brew bottle
  ships the headers at `/opt/homebrew/include/libwebsockets.h` (Apple
  Silicon) or `/usr/local/include/libwebsockets.h` (Intel), and the
  Makefile's fallback only checks `/usr/include/libwebsockets.h`. Without
  `pkg-config`, the build silently compiles the runtime *without* HTTP
  streaming support, then fails to link with `Undefined symbols: _hml_lws_http_*`.
- Make sure `/opt/homebrew/bin` is on your `PATH` when you run `make`. SSH
  non-interactive sessions inherit a stripped `PATH` that excludes it; the
  Makefile's `brew --prefix …` calls then return empty and the linker
  can't find the libs. `PATH=/opt/homebrew/bin:$PATH make` is a quick
  one-shot fix.
- Hemlock supports both Intel (x86_64) and Apple Silicon (arm64) architectures.

**Ubuntu/Debian:**
```bash
sudo apt-get update
sudo apt-get install build-essential libffi-dev libssl-dev libwebsockets-dev zlib1g-dev
```

**Fedora/RHEL:**
```bash
sudo dnf install gcc make libffi-devel openssl-devel libwebsockets-devel zlib-devel
```

**Arch Linux:**
```bash
sudo pacman -S base-devel libffi openssl libwebsockets zlib
```

## Building from Source

### 1. Clone the Repository

```bash
git clone https://github.com/hemlang/hemlock.git
cd hemlock
```

### 2. Build Hemlock

```bash
make
```

This will compile the Hemlock interpreter and place the executable in the current directory.

### 3. Verify Installation

```bash
./hemlock --version
```

You should see the Hemlock version information.

### 4. Build the Standard Library C Modules (Optional)

Some stdlib modules (`@stdlib/http`, `@stdlib/websocket`, `@stdlib/vector`) require native shared libraries. Build their wrappers with:

```bash
make stdlib
```

This compiles `lws_wrapper.so` (HTTP/WebSocket) if libwebsockets is installed. USearch (`libusearch_c.so`) must be installed separately — see [Optional Dependencies](#optional-dependencies) above.

### 5. Test the Build

Run the test suite to ensure everything works correctly:

```bash
make test
```

All tests should pass. Modules whose native dependencies aren't installed are skipped with a `⊘` notice rather than failing. If you see actual failures, please report them as an issue.

## Installing System-Wide (Optional)

To install Hemlock system-wide (e.g., to `/usr/local/bin`):

```bash
sudo make install
```

This allows you to run `hemlock` from anywhere without specifying the full path.

## Running Hemlock

### Interactive REPL

Start the Read-Eval-Print Loop:

```bash
./hemlock
```

You'll see a prompt where you can type Hemlock code:

```
Hemlock REPL
> print("Hello, World!");
Hello, World!
> let x = 42;
> print(x * 2);
84
>
```

Exit the REPL with `Ctrl+D` or `Ctrl+C`.

### Running Programs

Execute a Hemlock script:

```bash
./hemlock program.hml
```

With command-line arguments:

```bash
./hemlock program.hml arg1 arg2 "argument with spaces"
```

## Directory Structure

After building, your Hemlock directory will look like this:

```
hemlock/
├── hemlock           # Compiled interpreter executable
├── src/              # Source code
├── include/          # Header files
├── tests/            # Test suite
├── examples/         # Example programs
├── docs/             # Documentation
├── stdlib/           # Standard library
├── Makefile          # Build configuration
└── README.md         # Project README
```

## WebAssembly (WASM) Build

Hemlock can be compiled to WebAssembly via [Emscripten](https://emscripten.org/), allowing the full interpreter to run in a web browser or Node.js.

### Installing Emscripten

The Emscripten SDK (`emsdk`) provides the `emcc` compiler used to build the WASM interpreter.

**All platforms (Linux, macOS, Windows WSL):**

```bash
# Clone the emsdk repository
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk

# Install and activate the latest SDK
./emsdk install latest
./emsdk activate latest

# Add emcc to your PATH (run this in every new terminal, or add to your shell profile)
source ./emsdk_env.sh
```

Verify the installation:

```bash
emcc --version
```

You should see output like `emcc (Emscripten gcc/clang-like replacement ...) 3.x.x`.

For detailed instructions, see the [Emscripten Getting Started guide](https://emscripten.org/docs/getting_started/downloads.html).

**Optional: Add to shell profile**

To avoid running `source emsdk_env.sh` every time, add it to your shell profile:

```bash
# For bash (~/.bashrc or ~/.bash_profile)
echo 'source /path/to/emsdk/emsdk_env.sh' >> ~/.bashrc

# For zsh (~/.zshrc)
echo 'source /path/to/emsdk/emsdk_env.sh' >> ~/.zshrc
```

### WASM Dependencies

The WASM build has fewer dependencies than the native build. Emscripten provides its own versions of standard C libraries. The following native libraries are **not needed** (and not available) in the WASM build:

| Library | Native | WASM | Notes |
|---------|--------|------|-------|
| libffi | Required | Stubbed | FFI is not available in WASM |
| OpenSSL | Required | Stubbed | Crypto builtins return errors in WASM |
| libwebsockets | Optional | Stubbed | WebSocket support not available |
| zlib | Required | Emscripten provides | Automatically linked with `-sUSE_ZLIB=1` |
| pthreads | Required | Optional | Available with threaded build (requires SharedArrayBuffer) |

**In short:** You only need the Emscripten SDK installed. No additional system libraries are required for the WASM build.

### Building the WASM Interpreter

```bash
# Build the interpreter as WebAssembly
make wasm-interpreter
```

This produces two files in the `wasm/` directory:

| File | Description |
|------|-------------|
| `wasm/hemlock.js` | JavaScript loader and Emscripten glue code |
| `wasm/hemlock.wasm` | WebAssembly binary module |

### Running in Node.js

```bash
node -- wasm/hemlock.js -e 'print("Hello from WASM!");'
node -- wasm/hemlock.js examples/01-basics/fibonacci.hml
```

### Running in a Browser

WASM files must be served over HTTP with the correct MIME type (`application/wasm`). Opening the HTML file directly via `file://` will not work.

**Using the included example:**

```bash
make wasm-browser-example
# Opens http://localhost:8080/examples/wasm-browser/index.html
```

**Or manually with Python:**

```bash
python3 -m http.server 8080
# Open http://localhost:8080/wasm/playground.html
```

**Or manually with Node.js:**

```bash
npx serve .
# Open the URL shown in the terminal
```

See `examples/wasm-browser/` for a complete browser integration example with an interactive code editor.

### WASM Limitations

Some features are unavailable in the WASM environment:

- **FFI** - No shared library loading (`dlopen`/`dlsym`)
- **Crypto** - No OpenSSL (`sha256`, `md5`, etc. return errors)
- **Raw pointers** - Available. `alloc`, `buffer`, `ptr_offset`, `ptr_read_*`,
  `ptr_write_*` and `ptr_deref_*` all work: they are arithmetic and
  loads/stores over the module's linear memory, not FFI
- **File I/O** - No native filesystem access (Emscripten virtual FS only)
- **Networking** - No raw sockets or HTTP client
- **Signals** - No POSIX signal handling
- **Process** - No `fork`, `exec`, or process management
- **Threading** - `spawn`/`join`/channels require a threaded WASM build with `SharedArrayBuffer`

All core language features work: variables, functions, closures, objects, arrays, pattern matching, type annotations, try/catch, and the full standard library of pure-Hemlock modules.

### Compiling Hemlock Programs to WASM

In addition to running the interpreter in WASM, you can compile individual Hemlock programs to standalone WASM binaries using the compiler backend:

```bash
# Compile a Hemlock program to WASM (requires both hemlockc and Emscripten)
make wasm-compile FILE=program.hml

# With threading support
make wasm-compile-threaded FILE=program.hml
```

This uses `hemlockc` to generate C code, then Emscripten to compile it to WASM.

Or call the compiler directly:

```bash
./hemlockc --target wasm -o game.js game.hml       # game.js + game.wasm
./hemlockc --target wasm -o game.html game.hml     # also emits an HTML shell
```

#### Frame loops and Asyncify

`--target wasm` links with Emscripten's `-sASYNCIFY` by default. `sleep()`
compiles to `emscripten_sleep()`, which only exists when Asyncify is on;
without it the program dies the first time it sleeps with:

```
Please compile your program with async support in order to use
asynchronous operations like emscripten_sleep
```

Asyncify is not free — it instruments the module so it can unwind and rewind
the WASM stack at any suspend point, which costs both speed and `.wasm` size.
Programs that only need `sleep()` to pace a frame loop should use
[`main_loop()`](../../stdlib/docs/time.md#main_loopcallback-fps) instead, which
drives frames from the host's own scheduler (`requestAnimationFrame`), and then
drop Asyncify entirely:

```bash
./hemlockc --target wasm --no-asyncify -o game.js game.hml
```

`--no-asyncify` is only valid with `--target wasm`, and it makes `sleep()`
unusable in the resulting binary — that is the trade being made.

## Build Options

### Debug Build

Build with debug symbols and no optimization:

```bash
make debug
```

### Clean Build

Remove all compiled files:

```bash
make clean
```

Rebuild from scratch:

```bash
make clean && make
```

## Troubleshooting

### macOS: Library Not Found Errors

If you get errors about missing libraries (`-lcrypto`, `-lffi`, etc.):

1. Ensure Homebrew dependencies are installed:
   ```bash
   brew install libffi openssl@3 libwebsockets
   ```

2. Verify Homebrew paths:
   ```bash
   brew --prefix libffi
   brew --prefix openssl
   ```

3. The Makefile should auto-detect these paths. If it doesn't, check that `brew` is in your PATH:
   ```bash
   which brew
   ```

### macOS: BSD Type Errors (`u_int`, `u_char` not found)

If you see errors about unknown type names like `u_int` or `u_char`:

1. This is fixed in v1.0.0+ by using `_DARWIN_C_SOURCE` instead of `_POSIX_C_SOURCE`
2. Ensure you have the latest version of the code
3. Clean and rebuild:
   ```bash
   make clean && make
   ```

### Linux: libffi Not Found

If you get errors about missing `ffi.h` or `-lffi`:

1. Ensure `libffi-dev` is installed (see dependencies above)
2. Check if `pkg-config` can find it:
   ```bash
   pkg-config --cflags --libs libffi
   ```
3. If not found, you may need to set `PKG_CONFIG_PATH`:
   ```bash
   export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
   ```

### Compilation Errors

If you encounter compilation errors:

1. Ensure you have a C11-compatible compiler
2. On macOS, try using Clang (default):
   ```bash
   make CC=clang
   ```
3. On Linux, try using GCC:
   ```bash
   make CC=gcc
   ```
4. Check that all dependencies are installed
5. Try rebuilding from scratch:
   ```bash
   make clean && make
   ```

### Vector tests skipped (`@stdlib/vector` not available)

If you see `⊘ Skipping stdlib_vector tests (libusearch_c not installed)`, the USearch library isn't on the system library path. Install it following the [USearch optional dependency steps](#usearch-vector-similarity-search) above.

Verify it's discoverable after installation:
```bash
# Linux
ldconfig -p | grep usearch

# macOS
ls /usr/local/lib/libusearch_c.dylib
```

If the file is in a non-standard location, add it to your library path:
```bash
export LD_LIBRARY_PATH=/path/to/lib:$LD_LIBRARY_PATH  # Linux
export DYLD_LIBRARY_PATH=/path/to/lib:$DYLD_LIBRARY_PATH  # macOS
```

### Test Failures

If tests fail:

1. Check that you have the latest version of the code
2. Try rebuilding from scratch:
   ```bash
   make clean && make test
   ```
3. On macOS, ensure you have the latest Xcode Command Line Tools:
   ```bash
   xcode-select --install
   ```
4. Report the issue on GitHub with:
   - Your platform (macOS version / Linux distro)
   - Architecture (x86_64 / arm64)
   - Test output
   - Output of `make -v` and `gcc --version` (or `clang --version`)

## Next Steps

- [Quick Start Guide](quick-start.md) - Write your first Hemlock program
- [Tutorial](tutorial.md) - Learn Hemlock step-by-step
- [Language Guide](../language-guide/syntax.md) - Explore Hemlock features
