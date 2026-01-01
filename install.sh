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

# Check for missing shared libraries using ldd (Linux) or otool (macOS)
check_binary_deps() {
    local binary="$1"
    local os="$2"
    local missing_libs=()

    if [[ "$os" == "linux" ]]; then
        # Use ldd to find missing shared libraries
        local ldd_output
        ldd_output=$(ldd "$binary" 2>&1)

        # Check for "not found" libraries
        while IFS= read -r line; do
            if [[ "$line" == *"not found"* ]]; then
                local lib_name
                lib_name=$(echo "$line" | awk '{print $1}')
                missing_libs+=("$lib_name")
            fi
        done <<< "$ldd_output"
    elif [[ "$os" == "macos" ]]; then
        # Use otool on macOS
        local otool_output
        otool_output=$(otool -L "$binary" 2>&1)

        while IFS= read -r line; do
            local lib_path
            lib_path=$(echo "$line" | awk '{print $1}')
            if [[ "$lib_path" == /* ]] && [[ ! -f "$lib_path" ]]; then
                missing_libs+=("$(basename "$lib_path")")
            fi
        done <<< "$otool_output"
    fi

    if [[ ${#missing_libs[@]} -gt 0 ]]; then
        return 1
    fi
    return 0
}

# Print installation instructions for missing dependencies
print_dep_instructions() {
    local os="$1"
    shift
    local missing_libs=("$@")

    echo ""
    error_no_exit "Missing required shared libraries: ${missing_libs[*]}"
    echo ""
    echo "Install the missing dependencies:"
    echo ""

    if [[ "$os" == "linux" ]]; then
        # Map library names to package names
        local apt_pkgs=()
        local dnf_pkgs=()
        local pacman_pkgs=()

        for lib in "${missing_libs[@]}"; do
            case "$lib" in
                libwebsockets*)
                    apt_pkgs+=("libwebsockets-dev")
                    dnf_pkgs+=("libwebsockets")
                    pacman_pkgs+=("libwebsockets")
                    ;;
                libffi*)
                    apt_pkgs+=("libffi-dev")
                    dnf_pkgs+=("libffi")
                    pacman_pkgs+=("libffi")
                    ;;
                libssl*|libcrypto*)
                    apt_pkgs+=("libssl-dev")
                    dnf_pkgs+=("openssl")
                    pacman_pkgs+=("openssl")
                    ;;
                libz*)
                    apt_pkgs+=("zlib1g-dev")
                    dnf_pkgs+=("zlib")
                    pacman_pkgs+=("zlib")
                    ;;
                *)
                    apt_pkgs+=("$lib")
                    dnf_pkgs+=("$lib")
                    pacman_pkgs+=("$lib")
                    ;;
            esac
        done

        # Remove duplicates
        apt_pkgs=($(echo "${apt_pkgs[@]}" | tr ' ' '\n' | sort -u | tr '\n' ' '))
        dnf_pkgs=($(echo "${dnf_pkgs[@]}" | tr ' ' '\n' | sort -u | tr '\n' ' '))
        pacman_pkgs=($(echo "${pacman_pkgs[@]}" | tr ' ' '\n' | sort -u | tr '\n' ' '))

        echo "  Ubuntu/Debian: sudo apt-get install ${apt_pkgs[*]}"
        echo "  Fedora/RHEL:   sudo dnf install ${dnf_pkgs[*]}"
        echo "  Arch Linux:    sudo pacman -S ${pacman_pkgs[*]}"
    elif [[ "$os" == "macos" ]]; then
        local brew_pkgs=()

        for lib in "${missing_libs[@]}"; do
            case "$lib" in
                libwebsockets*) brew_pkgs+=("libwebsockets") ;;
                libffi*) brew_pkgs+=("libffi") ;;
                libssl*|libcrypto*) brew_pkgs+=("openssl@3") ;;
                *) brew_pkgs+=("$lib") ;;
            esac
        done

        brew_pkgs=($(echo "${brew_pkgs[@]}" | tr ' ' '\n' | sort -u | tr '\n' ' '))
        echo "  brew install ${brew_pkgs[*]}"
    fi

    echo ""
    echo "After installing dependencies, run hemlock again:"
    echo "  $PREFIX/bin/hemlock --version"
}

# Error without exit (for warnings that need follow-up)
error_no_exit() { echo -e "${RED}error:${NC} $1" >&2; }

# Verify the installed binary actually works
verify_installation() {
    local binary="$1"
    local os="$2"

    # First, check for missing shared libraries
    if [[ "$os" == "linux" ]] && command -v ldd &> /dev/null; then
        local ldd_output
        ldd_output=$(ldd "$binary" 2>&1)

        local missing_libs=()
        while IFS= read -r line; do
            if [[ "$line" == *"not found"* ]]; then
                local lib_name
                lib_name=$(echo "$line" | awk '{print $1}')
                missing_libs+=("$lib_name")
            fi
        done <<< "$ldd_output"

        if [[ ${#missing_libs[@]} -gt 0 ]]; then
            print_dep_instructions "$os" "${missing_libs[@]}"
            return 1
        fi
    fi

    # Try to run the binary
    local version_output
    if version_output=$("$binary" --version 2>&1); then
        return 0
    else
        # Binary failed to run - check if it's a library issue
        if [[ "$version_output" == *"error while loading shared libraries"* ]]; then
            local missing_lib
            # Extract library name: "shared libraries: libfoo.so.X: cannot open..."
            missing_lib=$(echo "$version_output" | sed -n 's/.*shared libraries: \([^:]*\):.*/\1/p' | head -1)
            if [[ -n "$missing_lib" ]]; then
                print_dep_instructions "$os" "$missing_lib"
                return 1
            fi
        fi

        # Unknown error
        error_no_exit "Binary verification failed: $version_output"
        return 1
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

    # Copy binary
    cp "$tmpdir/${artifact_name}/hemlock" "$PREFIX/bin/hemlock"
    chmod +x "$PREFIX/bin/hemlock"

    # Copy stdlib
    if [[ -d "$tmpdir/${artifact_name}/stdlib" ]]; then
        rm -rf "$PREFIX/lib/hemlock/stdlib"
        cp -r "$tmpdir/${artifact_name}/stdlib" "$PREFIX/lib/hemlock/stdlib"
    fi

    # Verify the binary actually works (check for missing shared libraries)
    info "Verifying installation..."
    if ! verify_installation "$PREFIX/bin/hemlock" "$os"; then
        echo ""
        warn "Hemlock was installed but cannot run due to missing dependencies."
        echo ""
        echo "  Binary: $PREFIX/bin/hemlock"
        echo "  Stdlib: $PREFIX/lib/hemlock/stdlib/"
        echo ""
        echo "After installing the required dependencies, hemlock will work."
        exit 1
    fi

    # Success message
    echo ""
    success "Hemlock $VERSION installed successfully!"
    echo ""
    echo "  Binary: $PREFIX/bin/hemlock"
    echo "  Stdlib: $PREFIX/lib/hemlock/stdlib/"
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

    # Show version and getting started info
    echo "Verify installation:"
    echo "  $PREFIX/bin/hemlock --version"
    echo ""
    echo "Get started:"
    echo "  $PREFIX/bin/hemlock              # Start REPL"
    echo "  $PREFIX/bin/hemlock program.hml  # Run a program"
}

main "$@"
