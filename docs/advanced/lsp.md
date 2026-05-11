# Language Server Protocol (LSP)

Hemlock includes a built-in LSP server for IDE integration, providing real-time diagnostics, navigation, and completion.

## Starting the Server

```bash
hemlock lsp --stdio    # Stdio transport (recommended)
hemlock lsp --tcp 5007 # TCP transport on specified port
```

## Features

- **Diagnostics** - Real-time syntax error and type checking
- **Hover** - Type information and keyword documentation
- **Go to Definition** - Navigate to function and variable definitions
- **Find References** - Find all references to a symbol
- **Document Symbols** - Outline view of functions and definitions
- **Completion** - Symbol completion suggestions

## Editor Setup

### VS Code

Use the [Hemlock VS Code extension](../../editors/vscode/hemlock/) or create `.vscode/settings.json`:

```json
{
  "hemlock.lsp.path": "/path/to/hemlock",
  "hemlock.lsp.args": ["lsp", "--stdio"]
}
```

Or use any generic LSP client extension with the command `hemlock lsp --stdio`.

### Neovim (nvim-lspconfig)

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

## Debugging

Capture server logs by redirecting stderr:

```bash
hemlock lsp --stdio 2>lsp.log
```
