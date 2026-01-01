#!/bin/bash
# Hemlock Install Script
# Usage: curl -fsSL https://raw.githubusercontent.com/hemlang/hemlock/main/install.sh | bash
#
# Options:
#   --prefix DIR     Install to DIR (default: ~/.local)
#   --version VER    Install specific version (default: latest)
#   --help           Show this help message

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default values
PREFIX="$HOME/.local"
VERSION=""
GITHUB_REPO="hemlang/hemlock"

# Print helpers
info() { echo -e "${BLUE}==>${NC} $1"; }
success() { echo -e "${GREEN}==>${NC} $1"; }
warn() { echo -e "${YELLOW}warning:${NC} $1"; }
error() { echo -e "${RED}error:${NC} $1" >&2; exit 1; }

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --prefix)
            PREFIX="$2"
            shift 2
            ;;
        --prefix=*)
            PREFIX="${1#*=}"
            shift
            ;;
        --version)
            VERSION="$2"
            shift 2
            ;;
        --version=*)
            VERSION="${1#*=}"
            shift
            ;;
        --help|-h)
            echo "Hemlock Install Script"
            echo ""
            echo "Usage: curl -fsSL https://raw.githubusercontent.com/hemlang/hemlock/main/install.sh | bash"
            echo "   or: ./install.sh [options]"
            echo ""
            echo "Options:"
            echo "  --prefix DIR     Install to DIR (default: ~/.local)"
            echo "  --version VER    Install specific version (default: latest)"
            echo "  --help           Show this help message"
            echo ""
            echo "Examples:"
            echo "  # Install latest to ~/.local"
            echo "  curl -fsSL https://raw.githubusercontent.com/hemlang/hemlock/main/install.sh | bash"
            echo ""
            echo "  # Install to /usr/local (requires sudo)"
            echo "  curl -fsSL https://raw.githubusercontent.com/hemlang/hemlock/main/install.sh | sudo bash -s -- --prefix /usr/local"
            echo ""
            echo "  # Install specific version"
            echo "  curl -fsSL https://raw.githubusercontent.com/hemlang/hemlock/main/install.sh | bash -s -- --version v1.5.0"
            exit 0
            ;;
        *)
            error "Unknown option: $1 (use --help for usage)"
            ;;
    esac
done

# Detect OS
detect_os() {
    local os
    os="$(uname -s)"
    case "$os" in
        Linux)  echo "linux" ;;
        Darwin) echo "macos" ;;
        *)      error "Unsupported operating system: $os" ;;
    esac
}

# Detect architecture
detect_arch() {
    local arch
    arch="$(uname -m)"
    case "$arch" in
        x86_64|amd64)  echo "x86_64" ;;
        arm64|aarch64) echo "arm64" ;;
        *)             error "Unsupported architecture: $arch" ;;
    esac
}

# Get latest release version from GitHub
get_latest_version() {
    local url="https://api.github.com/repos/${GITHUB_REPO}/releases/latest"
    local version

    if command -v curl &> /dev/null; then
        version=$(curl -fsSL "$url" | grep '"tag_name":' | sed -E 's/.*"([^"]+)".*/\1/')
    elif command -v wget &> /dev/null; then
        version=$(wget -qO- "$url" | grep '"tag_name":' | sed -E 's/.*"([^"]+)".*/\1/')
    else
        error "Neither curl nor wget found. Please install one of them."
    fi

    if [[ -z "$version" ]]; then
        error "Failed to fetch latest version from GitHub"
    fi

    echo "$version"
}

# Download file
download() {
    local url="$1"
    local output="$2"

    if command -v curl &> /dev/null; then
        curl -fsSL "$url" -o "$output"
    elif command -v wget &> /dev/null; then
        wget -q "$url" -O "$output"
    else
        error "Neither curl nor wget found. Please install one of them."
    fi
}

# Check runtime dependencies
# Linux: Interpreter requires glibc (standard on all major distros)
# macOS: No additional dependencies needed
check_runtime_deps() {
    local os="$1"
    if [[ "$os" == "linux" ]]; then
        # Check for glibc - virtually all Linux distros have this
        if ! ldd --version &>/dev/null; then
            warn "glibc not detected. The interpreter requires glibc (standard C library)."
            warn "Alpine Linux and other musl-based distros are not supported."
        fi
    fi
}

# Main installation
main() {
    echo ""
    echo -e "${GREEN}Hemlock Installer${NC}"
    echo "=================="
    echo ""

    # Detect platform
    local os arch
    os=$(detect_os)
    arch=$(detect_arch)
    info "Detected platform: ${os}-${arch}"

    # Get version
    if [[ -z "$VERSION" ]]; then
        info "Fetching latest version..."
        VERSION=$(get_latest_version)
    fi
    info "Installing version: $VERSION"

    # Determine artifact name
    local artifact_name="hemlock-${os}-${arch}"
    local download_url="https://github.com/${GITHUB_REPO}/releases/download/${VERSION}/${artifact_name}.tar.gz"

    # Create temp directory
    local tmpdir
    tmpdir=$(mktemp -d)
    trap "rm -rf '$tmpdir'" EXIT

    # Download
    info "Downloading ${artifact_name}.tar.gz..."
    download "$download_url" "$tmpdir/hemlock.tar.gz" || error "Failed to download. Check that version '$VERSION' exists and has a release for ${os}-${arch}"

    # Extract
    info "Extracting..."
    tar -xzf "$tmpdir/hemlock.tar.gz" -C "$tmpdir"

    # Install
    info "Installing to $PREFIX..."
    mkdir -p "$PREFIX/bin"
    mkdir -p "$PREFIX/lib/hemlock"

    # Copy interpreter binary
    cp "$tmpdir/${artifact_name}/hemlock" "$PREFIX/bin/hemlock"
    chmod +x "$PREFIX/bin/hemlock"

    # Copy compiler binary (if present)
    if [[ -f "$tmpdir/${artifact_name}/hemlockc" ]]; then
        cp "$tmpdir/${artifact_name}/hemlockc" "$PREFIX/bin/hemlockc"
        chmod +x "$PREFIX/bin/hemlockc"
    fi

    # Copy stdlib
    if [[ -d "$tmpdir/${artifact_name}/stdlib" ]]; then
        rm -rf "$PREFIX/lib/hemlock/stdlib"
        cp -r "$tmpdir/${artifact_name}/stdlib" "$PREFIX/lib/hemlock/stdlib"
    fi

    # Copy runtime library (for hemlockc to compile programs)
    if [[ -f "$tmpdir/${artifact_name}/libhemlock_runtime.a" ]]; then
        cp "$tmpdir/${artifact_name}/libhemlock_runtime.a" "$PREFIX/lib/hemlock/"
    fi

    # Copy runtime headers (if present)
    if [[ -d "$tmpdir/${artifact_name}/include" ]]; then
        mkdir -p "$PREFIX/lib/hemlock/include"
        cp -r "$tmpdir/${artifact_name}/include"/* "$PREFIX/lib/hemlock/include/"
    fi

    # Check runtime dependencies
    check_runtime_deps "$os"

    # Success message
    echo ""
    success "Hemlock $VERSION installed successfully!"
    echo ""
    echo "  Interpreter: $PREFIX/bin/hemlock"
    if [[ -f "$PREFIX/bin/hemlockc" ]]; then
        echo "  Compiler:    $PREFIX/bin/hemlockc"
    fi
    echo "  Stdlib:      $PREFIX/lib/hemlock/stdlib/"
    if [[ -f "$PREFIX/lib/hemlock/libhemlock_runtime.a" ]]; then
        echo "  Runtime:     $PREFIX/lib/hemlock/libhemlock_runtime.a"
    fi
    echo ""

    # PATH instructions
    if [[ ":$PATH:" != *":$PREFIX/bin:"* ]]; then
        echo -e "${YELLOW}Note:${NC} $PREFIX/bin is not in your PATH."
        echo ""
        echo "Add it to your shell configuration:"
        echo ""

        local shell_name
        shell_name=$(basename "$SHELL")
        case "$shell_name" in
            bash)
                echo "  echo 'export PATH=\"$PREFIX/bin:\$PATH\"' >> ~/.bashrc"
                echo "  source ~/.bashrc"
                ;;
            zsh)
                echo "  echo 'export PATH=\"$PREFIX/bin:\$PATH\"' >> ~/.zshrc"
                echo "  source ~/.zshrc"
                ;;
            fish)
                echo "  fish_add_path $PREFIX/bin"
                ;;
            *)
                echo "  export PATH=\"$PREFIX/bin:\$PATH\""
                ;;
        esac
        echo ""
    fi

    # Verify installation
    if [[ -x "$PREFIX/bin/hemlock" ]]; then
        echo "Verify installation:"
        echo "  $PREFIX/bin/hemlock --version"
        echo ""
        echo "Get started:"
        echo "  $PREFIX/bin/hemlock              # Start REPL"
        echo "  $PREFIX/bin/hemlock program.hml  # Run a program"
        if [[ -x "$PREFIX/bin/hemlockc" ]]; then
            echo "  $PREFIX/bin/hemlockc program.hml -o program  # Compile to native binary"
        fi
    fi
}

main "$@"
