# Hemlock Editor Support

This directory contains syntax highlighting and editor configuration for Hemlock.

## VS Code

### Installation (Manual)

1. Copy the extension to your VS Code extensions folder:
   ```bash
   cp -r vscode/hemlock ~/.vscode/extensions/hemlock
   ```

2. Reload VS Code or run "Developer: Reload Window" from the command palette.

3. Open any `.hml` file and syntax highlighting will be applied automatically.

### Installation (Development)

For active development of the extension:

```bash
cd vscode/hemlock
code .
# Press F5 to launch Extension Development Host
```

### Features

- **Syntax highlighting** for all Hemlock keywords, types, and operators
- **Auto-closing** brackets, quotes, and parentheses
- **Comment toggling** (Ctrl+/ for line comments)
- **Bracket matching** and folding
- **Code snippets** (coming soon)

## Vim/Neovim

### Installation

Copy the syntax and filetype detection files to your Vim directory:

```bash
# For Vim
mkdir -p ~/.vim/syntax ~/.vim/ftdetect
cp vim/syntax/hemlock.vim ~/.vim/syntax/
cp vim/ftdetect/hemlock.vim ~/.vim/ftdetect/

# For Neovim
mkdir -p ~/.config/nvim/syntax ~/.config/nvim/ftdetect
cp vim/syntax/hemlock.vim ~/.config/nvim/syntax/
cp vim/ftdetect/hemlock.vim ~/.config/nvim/ftdetect/
```

### Features

- **Syntax highlighting** for keywords, types, operators, strings, numbers
- **Comment highlighting** (line and block comments)
- **Built-in function** highlighting
- **TODO/FIXME** highlighting in comments
- Automatic filetype detection for `.hml` files

### Manual Filetype Setting

If automatic detection doesn't work, you can manually set the filetype:

```vim
:set filetype=hemlock
```

Or add to your vimrc for specific files:

```vim
autocmd BufRead,BufNewFile *.hml set filetype=hemlock
```

## Emacs (Coming Soon)

Emacs mode for Hemlock is planned. Contributions welcome!

## Sublime Text

Sublime Text can use the TextMate grammar:

1. Open Sublime Text
2. Go to Preferences → Browse Packages
3. Create directory `Hemlock`
4. Copy `vscode/hemlock/syntaxes/hemlock.tmLanguage.json` to that directory
5. Rename to `Hemlock.tmLanguage`
6. Restart Sublime Text

## Language Server Protocol (LSP)

LSP support is planned for the future. This will provide:

- **Diagnostics** (syntax errors, type warnings)
- **Go to definition**
- **Autocomplete**
- **Hover documentation**
- **Find references**
- **Rename refactoring**

See `../docs/LSP.md` (coming soon) for development progress.

## Contributing

To improve editor support:

1. Test the syntax highlighting with various Hemlock code samples
2. Report issues or missing features
3. Submit PRs with improvements to the grammar files
4. Add support for additional editors

### Testing Syntax Highlighting

Create test files in `editors/tests/` with edge cases:

```hemlock
// Test file for syntax highlighting
fn test_all_features() {
    // Keywords and types
    let x: i32 = 42;
    let s: string = "hello \u{1F680}";
    let r: rune = '🚀';

    // Async
    async fn worker() {
        let task = spawn(compute, 100);
        let result = await task;
    }

    // Error handling
    try {
        throw "error";
    } catch (e) {
        print("Caught: " + e);
    }

    // Signals
    signal(SIGINT, handler);
}
```

## Notes

- All editors use the same core grammar/patterns
- TextMate grammar is the source of truth (used by VS Code, GitHub, Sublime)
- Vim syntax is maintained separately due to different format
- Tree-sitter grammar (coming soon) will provide more accurate parsing

## Resources

- [TextMate Grammar Guide](https://macromates.com/manual/en/language_grammars)
- [VS Code Language Extension Guide](https://code.visualstudio.com/api/language-extensions/syntax-highlight-guide)
- [Vim Syntax Highlighting](https://vim.fandom.com/wiki/Creating_your_own_syntax_files)
