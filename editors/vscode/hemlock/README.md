# Hemlock Language Support for VS Code

Syntax highlighting and basic language support for the Hemlock programming language.

## Features

- **Syntax Highlighting** - Full syntax highlighting for all Hemlock language features:
  - Keywords: `fn`, `let`, `if`, `else`, `while`, `for`, `async`, `await`, `try`, `catch`, `defer`, etc.
  - Types: `i8`-`i64`, `u8`-`u64`, `f32`, `f64`, `bool`, `string`, `rune`, `ptr`, `buffer`, `array`, `object`
  - Operators: Arithmetic, comparison, logical, bitwise
  - String literals with escape sequences and Unicode escapes
  - Rune (character) literals
  - Comments (line and block)
  - Built-in functions: `print`, `alloc`, `free`, `spawn`, `signal`, etc.
  - Constants: `SIGINT`, `PI`, `E`, `TAU`, `true`, `false`, `null`

- **Bracket Matching** - Auto-closing and matching for brackets, parentheses, and quotes

- **Comment Toggling** - Use `Ctrl+/` (or `Cmd+/` on Mac) to toggle line comments

- **Code Folding** - Fold code blocks and region markers

## Installation

### From Source

1. Copy this extension directory to your VS Code extensions folder:
   ```bash
   cp -r . ~/.vscode/extensions/hemlock
   ```

2. Reload VS Code or run "Developer: Reload Window" from the command palette (`Ctrl+Shift+P`)

3. Open any `.hml` file - syntax highlighting will be applied automatically

### Development Mode

To work on the extension itself:

1. Open this directory in VS Code
2. Press `F5` to launch Extension Development Host
3. Make changes and reload to see updates

## Usage

Simply open any Hemlock file (`.hml` extension) and syntax highlighting will be applied automatically.

## Examples

The extension highlights all Hemlock language features:

```hemlock
// Function with type annotations
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

See the [Hemlock documentation](https://github.com/nbeerbower/hemlock) for more details.

## Upcoming Features

- **Language Server Protocol (LSP)** support:
  - Syntax error diagnostics
  - Go to definition
  - Autocomplete
  - Hover documentation
  - Find references
  - Rename refactoring

- **Code Snippets** for common patterns

- **Debug Adapter Protocol** support

## Contributing

Found a highlighting issue or want to improve the grammar?

1. Check the grammar file at `syntaxes/hemlock.tmLanguage.json`
2. Test your changes with various Hemlock code samples
3. Submit a PR to the [Hemlock repository](https://github.com/nbeerbower/hemlock)

## License

Same license as Hemlock (check repository for details)

## Resources

- [Hemlock GitHub Repository](https://github.com/nbeerbower/hemlock)
- [Language Documentation](https://github.com/nbeerbower/hemlock/blob/main/CLAUDE.md)
- [Standard Library Docs](https://github.com/nbeerbower/hemlock/tree/main/stdlib/docs)
