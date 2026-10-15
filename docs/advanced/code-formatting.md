# Code Formatting

Hemlock includes a built-in code formatter to enforce consistent style.

## Usage

```bash
hemlock format <FILE>         # Format a file in-place
hemlock format --check <FILE> # Check if file is formatted (exit 1 if not)
```

## Style Rules

The formatter enforces these conventions:

| Rule | Value |
|------|-------|
| Indentation | Tabs |
| Brace style | K&R (opening brace on same line) |
| Max line width | 100 characters |
| Trailing commas | Yes, in multiline contexts |
| Max consecutive blank lines | 1 |

## Automatic Line Breaking

The formatter automatically breaks long lines:

- **Function parameters** - Long parameter lists break with one parameter per line
- **Binary expressions** - Long logical/comparison chains break at operators
- **Import statements** - Long import lists break with each item on its own line
- **Method chains** - Long chains break before dots

## Example

Before:
```hemlock
fn create_user(name: string, email: string, age: i32, active: bool, role: string) { return { name: name, email: email, age: age, active: active, role: role }; }
```

After:
```hemlock
fn create_user(
	name: string,
	email: string,
	age: i32,
	active: bool,
	role: string,
) {
	return {
		name: name,
		email: email,
		age: age,
		active: active,
		role: role,
	};
}
```

## CI Integration

Use `--check` in CI pipelines to enforce formatting:

```bash
hemlock format --check src/main.hml || echo "File not formatted"
```
