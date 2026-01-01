# Hemlock Language Server Protocol (LSP) Implementation

This directory contains the LSP server implementation for Hemlock, providing IDE features through the Language Server Protocol.

## Features

- **Diagnostics**: Real-time syntax error and type checking diagnostics
- **Hover**: Type information and documentation on hover
- **Go to Definition**: Navigate to function and variable definitions
- **Find References**: Find all references to a symbol
- **Document Symbols**: Outline view of functions and definitions
- **Completion**: Symbol completion suggestions

## Architecture

```
src/lsp/
├── lsp.h          # Main LSP server types and lifecycle
├── lsp.c          # Server implementation, document management
├── protocol.h     # JSON-RPC 2.0 protocol types
├── protocol.c     # Message reading/writing, JSON parsing
├── handlers.h     # LSP method handler declarations
└── handlers.c     # Handler implementations
```

### Components

- **LSPServer**: Main server state, manages open documents and client capabilities
- **LSPDocument**: Tracks open files with content, AST cache, and diagnostics
- **Protocol layer**: JSON-RPC 2.0 implementation with Content-Length framing
- **Handlers**: Process LSP requests/notifications (initialize, didOpen, hover, etc.)

## Usage

### Stdio Transport (recommended)

```bash
hemlock lsp --stdio
```

The server communicates via stdin/stdout using the LSP protocol.

### TCP Transport

```bash
hemlock lsp --tcp 5007
```

The server listens on the specified port for a single client connection.

## Editor Integration

### VS Code

Create `.vscode/settings.json`:

```json
{
  "hemlock.lsp.path": "/path/to/hemlock",
  "hemlock.lsp.args": ["lsp", "--stdio"]
}
```

Or use a generic LSP client extension with the command `hemlock lsp --stdio`.

### Neovim (nvim-lspconfig)

Add to your Neovim configuration:

```lua
local lspconfig = require('lspconfig')
local configs = require('lspconfig.configs')

if not configs.hemlock then
  configs.hemlock = {
    default_config = {
      cmd = { 'hemlock', 'lsp', '--stdio' },
      filetypes = { 'hemlock' },
      root_dir = lspconfig.util.root_pattern('.git', 'hemlock.toml'),
      settings = {},
    },
  }
end

lspconfig.hemlock.setup({})
```

### Vim (vim-lsp)

```vim
if executable('hemlock')
  au User lsp_setup call lsp#register_server({
    \ 'name': 'hemlock',
    \ 'cmd': {server_info->['hemlock', 'lsp', '--stdio']},
    \ 'allowlist': ['hemlock'],
    \ })
endif
```

### Emacs (lsp-mode)

```elisp
(with-eval-after-load 'lsp-mode
  (add-to-list 'lsp-language-id-configuration '(hemlock-mode . "hemlock"))
  (lsp-register-client
   (make-lsp-client
    :new-connection (lsp-stdio-connection '("hemlock" "lsp" "--stdio"))
    :major-modes '(hemlock-mode)
    :server-id 'hemlock-lsp)))
```

## Supported LSP Methods

### Lifecycle

| Method | Type | Description |
|--------|------|-------------|
| `initialize` | Request | Initialize server, exchange capabilities |
| `initialized` | Notification | Client ready signal |
| `shutdown` | Request | Prepare for exit |
| `exit` | Notification | Terminate server |

### Document Synchronization

| Method | Type | Description |
|--------|------|-------------|
| `textDocument/didOpen` | Notification | Document opened |
| `textDocument/didChange` | Notification | Document content changed |
| `textDocument/didClose` | Notification | Document closed |
| `textDocument/didSave` | Notification | Document saved |

### Language Features

| Method | Type | Description |
|--------|------|-------------|
| `textDocument/hover` | Request | Get hover information |
| `textDocument/completion` | Request | Get completion items |
| `textDocument/definition` | Request | Go to definition |
| `textDocument/references` | Request | Find all references |
| `textDocument/documentSymbol` | Request | Get document symbols |

## Diagnostics

The LSP server provides two levels of diagnostics:

1. **Parse errors**: Syntax errors detected by the parser
2. **Type errors**: Type mismatches detected by the type checker

Diagnostics are published automatically when a document is opened or changed.

## Building

The LSP server is built as part of the main Hemlock build:

```bash
make           # Builds hemlock with LSP support
make test-lsp  # Run LSP test suite
```

## Testing

Run the LSP test suite:

```bash
make test-lsp
# or directly:
python3 tests/lsp/test_lsp.py
```

## Debugging

The server logs to stderr, which can be captured for debugging:

```bash
hemlock lsp --stdio 2>lsp.log
```

Log output includes:
- Server startup messages
- Received method names
- Connection status changes
