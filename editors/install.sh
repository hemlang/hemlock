#!/bin/bash
# Hemlock Editor Support Installation Script

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "Hemlock Editor Support Installer"
echo "================================="
echo ""

# Function to install VS Code extension
install_vscode() {
    echo "Installing VS Code extension..."

    VSCODE_EXT_DIR="$HOME/.vscode/extensions/hemlock"

    if [ -d "$VSCODE_EXT_DIR" ]; then
        echo "  Removing existing installation..."
        rm -rf "$VSCODE_EXT_DIR"
    fi

    mkdir -p "$HOME/.vscode/extensions"
    cp -r "$SCRIPT_DIR/vscode/hemlock" "$VSCODE_EXT_DIR"

    echo "  ✓ VS Code extension installed to $VSCODE_EXT_DIR"
    echo "  → Reload VS Code to activate"
    echo ""
}

# Function to install Vim syntax files
install_vim() {
    echo "Installing Vim syntax files..."

    VIM_DIR="$HOME/.vim"
    mkdir -p "$VIM_DIR/syntax" "$VIM_DIR/ftdetect"

    cp "$SCRIPT_DIR/vim/syntax/hemlock.vim" "$VIM_DIR/syntax/"
    cp "$SCRIPT_DIR/vim/ftdetect/hemlock.vim" "$VIM_DIR/ftdetect/"

    echo "  ✓ Vim syntax installed to $VIM_DIR"
    echo ""
}

# Function to install Neovim syntax files
install_neovim() {
    echo "Installing Neovim syntax files..."

    NVIM_DIR="$HOME/.config/nvim"
    mkdir -p "$NVIM_DIR/syntax" "$NVIM_DIR/ftdetect"

    cp "$SCRIPT_DIR/vim/syntax/hemlock.vim" "$NVIM_DIR/syntax/"
    cp "$SCRIPT_DIR/vim/ftdetect/hemlock.vim" "$NVIM_DIR/ftdetect/"

    echo "  ✓ Neovim syntax installed to $NVIM_DIR"
    echo ""
}

# Parse command line arguments
if [ $# -eq 0 ]; then
    echo "Usage: $0 [vscode|vim|nvim|all]"
    echo ""
    echo "Options:"
    echo "  vscode  - Install VS Code extension"
    echo "  vim     - Install Vim syntax files"
    echo "  nvim    - Install Neovim syntax files"
    echo "  all     - Install for all editors"
    echo ""
    exit 1
fi

case "$1" in
    vscode)
        install_vscode
        ;;
    vim)
        install_vim
        ;;
    nvim)
        install_neovim
        ;;
    all)
        install_vscode
        install_vim
        install_neovim
        ;;
    *)
        echo "Error: Unknown option '$1'"
        echo "Use: $0 [vscode|vim|nvim|all]"
        exit 1
        ;;
esac

echo "Installation complete!"
echo ""
echo "Test the syntax highlighting with:"
echo "  code $SCRIPT_DIR/test_syntax.hml     # VS Code"
echo "  vim $SCRIPT_DIR/test_syntax.hml      # Vim"
echo "  nvim $SCRIPT_DIR/test_syntax.hml     # Neovim"
