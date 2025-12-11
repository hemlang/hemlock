# Hemlock Language Support for VS Code

Full language support for the Hemlock programming language, including syntax highlighting, diagnostics, hover information, and code completion.

## Features

### Language Server Protocol (LSP) Support - Experimental

This extension includes an LSP client that connects to the Hemlock language server (experimental) for:

- **Real-time Diagnostics** - See syntax errors as you type
- **Hover Information** - Hover over keywords to see documentation
- **Code Completion** - Get suggestions for keywords, types, and built-in functions
- **Document Symbols** - View outline of functions, variables, enums, and types

### Syntax Highlighting

Full syntax highlighting for all Hemlock language features:

- Keywords: `fn`, `let`, `if`, `else`, `while`, `for`, `async`, `await`, `try`, `catch`, `defer`, etc.
- Types: `i8`-`i64`, `u8`-`u64`, `f32`, `f64`, `bool`, `string`, `rune`, `ptr`, `buffer`, `array`, `object`
- Operators: Arithmetic, comparison, logical, bitwise
- String literals with escape sequences and Unicode escapes
- Rune (character) literals
- Comments (line and block)
- Built-in functions: `print`, `alloc`, `free`, `spawn`, `signal`, etc.
- Constants: `SIGINT`, `PI`, `E`, `TAU`, `true`, `false`, `null`

### Editor Features

- **Bracket Matching** - Auto-closing and matching for brackets, parentheses, and quotes
- **Comment Toggling** - Use `Ctrl+/` (or `Cmd+/` on Mac) to toggle line comments
- **Code Folding** - Fold code blocks and region markers

## Requirements

The LSP features require the Hemlock interpreter to be installed and available in your PATH:

```bash
# Build hemlock from source
git clone https://github.com/hemlang/hemlock
cd hemlock
make

# Add to PATH (or use hemlock.serverPath setting)
export PATH="$PATH:/path/to/hemlock"
```

## Installation

### From Source

1. Build the extension:
   ```bash
   cd editors/vscode/hemlock
   npm install
   npm run compile
   ```

2. Copy to your VS Code extensions folder:
   ```bash
   cp -r . ~/.vscode/extensions/hemlock
   ```

3. Reload VS Code or run "Developer: Reload Window" from the command palette (`Ctrl+Shift+P`)

### Development Mode

To work on the extension itself:

1. Open this directory in VS Code
2. Run `npm install` to install dependencies
3. Press `F5` to launch Extension Development Host
4. Make changes and reload to see updates

## Configuration

The extension provides the following settings:

| Setting | Default | Description |
|---------|---------|-------------|
| `hemlock.serverPath` | `"hemlock"` | Path to the Hemlock executable |
| `hemlock.trace.server` | `"off"` | Trace communication with LSP server (`off`, `messages`, `verbose`) |

Example settings.json:
```json
{
    "hemlock.serverPath": "/usr/local/bin/hemlock",
    "hemlock.trace.server": "messages"
}
```

## Usage

Simply open any Hemlock file (`.hml` extension) and the extension will automatically:

1. Apply syntax highlighting
2. Start the Hemlock language server
3. Begin showing diagnostics, completions, and hover info

## Examples

The extension highlights and provides IntelliSense for all Hemlock language features:

```hemlock
// Function with type annotations - hover shows "fn - Function declaration keyword"
fn factorial(n: i32): i32 {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

// Async concurrency
async fn parallel_work() {
    let t1 = spawn(compute, 100);
    let t2 = spawn(compute, 200);

    let r1 = await t1;
    let r2 = await t2;
    return r1 + r2;
}

// Error handling with defer
fn process_file(path: string) {
    let f = open(path, "r");
    defer f.close();

    try {
        let content = f.read();
        print(content);
    } catch (e) {
        print("Error: " + e);
    }
}
```

## Language Features

Hemlock is a systems scripting language with:

- Manual memory management (`alloc`, `free`)
- Optional type annotations with runtime checking
- UTF-8 strings and Unicode runes
- Structured async concurrency (true OS threads)
- Exception handling (`try`/`catch`/`finally`)
- Signal handling (POSIX signals)
- File I/O and command execution
- Foreign Function Interface (FFI)

See the [Hemlock documentation](https://github.com/hemlang/hemlock) for more details.

## Troubleshooting

### Language server not starting

1. Check that `hemlock` is in your PATH or configured via `hemlock.serverPath`
2. Verify the server works: `hemlock lsp --help`
3. Enable tracing: set `hemlock.trace.server` to `"verbose"`
4. Check Output panel (View > Output > Hemlock Language Server)

### No diagnostics showing

1. Ensure the file has `.hml` extension
2. Check that the language server is running (see Output panel)
3. Try reloading the window: `Ctrl+Shift+P` > "Developer: Reload Window"

## Upcoming Features

- **Go to Definition** - Jump to function and variable definitions (planned)
- **Find References** - Find all usages of a symbol (planned)
- **Rename Refactoring** - Rename symbols across files (planned)
- **Code Snippets** for common patterns
- **Debug Adapter Protocol** support

> **Note:** The LSP implementation is experimental. Please report any issues.

## Contributing

Found an issue or want to improve the extension?

1. Check the grammar file at `syntaxes/hemlock.tmLanguage.json`
2. Check the LSP client at `src/extension.ts`
3. Test your changes with various Hemlock code samples
4. Submit a PR to the [Hemlock repository](https://github.com/hemlang/hemlock)

## License

Same license as Hemlock (check repository for details)

## Resources

- [Hemlock GitHub Repository](https://github.com/hemlang/hemlock)
- [Language Documentation](https://github.com/hemlang/hemlock/blob/main/CLAUDE.md)
- [Standard Library Docs](https://github.com/hemlang/hemlock/tree/main/stdlib/docs)
