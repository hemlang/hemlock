# hemfmt - Hemlock Code Formatter

`hemfmt` is the official code formatter for Hemlock. It enforces a consistent code style across your codebase.

## Installation

Built automatically with `make` or `make hemfmt`.

## Usage

### Format files in place

```bash
hemfmt file.hml
```

### Check if files are formatted

```bash
hemfmt --check file.hml
```

Exit code 0 if formatted correctly, 1 if formatting needed.

### Show formatting diff

```bash
hemfmt --diff file.hml
```

Shows what would change without modifying the file.

## Formatting Rules

### Indentation

- **Tabs** for indentation (not spaces)
- Consistent tab width

### Spacing

- Spaces around binary operators: `x + y`, `a == b`
- Space after keywords: `if (`, `while (`, `for (`
- Space after commas: `[1, 2, 3]`, `fn(a, b)`
- No space before semicolons: `let x = 5;`

### Braces

- Opening brace on same line: `if (x) {`
- Closing brace on new line
- `else`, `catch`, `finally` on same line as closing `}`:
  ```hemlock
  if (x) {
      ...
  } else {
      ...
  }
  ```

### Functions

- Function bodies formatted as blocks
- Semicolon after function expression:
  ```hemlock
  let add = fn(a, b) {
      return a + b;
  };
  ```

### Switch Statements

- Case labels indented one level
- Case bodies indented two levels:
  ```hemlock
  switch (x) {
      case 1:
          print("one");
          break;
      default:
          print("other");
          break;
  }
  ```

### Objects and Arrays

- Compact formatting for literals:
  - `{ name: "Alice", age: 30 }`
  - `[1, 2, 3, 4, 5]`

### Line Breaks

- Statements on separate lines
- No blank lines removed/added (preserve structure)

## CI/CD Integration

Use `--check` in CI to enforce formatting:

```bash
#!/bin/bash
if ! hemfmt --check *.hml; then
    echo "Code needs formatting! Run: hemfmt *.hml"
    exit 1
fi
```

## Philosophy

`hemfmt` follows Hemlock's design philosophy:
- **Explicit over implicit** - See exactly what's being formatted
- **No magic** - Straightforward AST pretty-printing
- **Consistent** - One canonical style for all Hemlock code
- **Familiar** - C-like style with modern conventions

Like `gofmt`, the goal is to end bike-shedding about style and focus on code.
