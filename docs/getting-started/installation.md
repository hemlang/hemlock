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

### Installing Dependencies

**macOS:**
```bash
# Install Homebrew if not already installed
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install Xcode Command Line Tools
xcode-select --install

# Install dependencies via Homebrew
brew install libffi openssl@3 libwebsockets
```

**Note for macOS users**: The Makefile automatically detects Homebrew installations and sets the correct include/library paths. Hemlock supports both Intel (x86_64) and Apple Silicon (arm64) architectures.

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

### 4. Test the Build

Run the test suite to ensure everything works correctly:

```bash
make test
```

All tests should pass. If you see any failures, please report them as an issue.

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
