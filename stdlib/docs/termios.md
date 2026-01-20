# Termios Module

Terminal I/O control for raw input mode on Unix systems (Linux, macOS).

## Overview

The termios module provides low-level terminal control, enabling:
- **Raw mode**: Keypresses available immediately without Enter
- **Arrow key detection**: Full support for cursor keys and function keys
- **Cross-platform**: Works on Linux and macOS

## Quick Start

```hemlock
import { enable_raw_mode, disable_raw_mode, read_key, KEY_UP } from "@stdlib/termios";

// Always pair enable/disable, preferably with defer
enable_raw_mode();
defer disable_raw_mode();

loop {
    let key = read_key();

    if (key.code == KEY_UP) {
        print("You pressed Up!");
    }
    if (key.char == "q") {
        break;
    }
}
```

## Functions

### `enable_raw_mode(): bool`

Enable raw terminal mode. Returns `true` on success, `false` if stdin is not a terminal.

- Disables line buffering (canonical mode)
- Disables echo
- Keypresses are immediately available

**Important**: Always call `disable_raw_mode()` before exiting, or use `defer`.

### `disable_raw_mode(): bool`

Restore original terminal settings. Safe to call multiple times.

### `is_raw_mode(): bool`

Check if currently in raw mode.

### `is_terminal(): bool`

Check if stdin is connected to a terminal (not a pipe or file).

### `read_key(): object`

Read a single keypress (blocking). Returns an object:

```hemlock
{
    char: string|null,  // Character for printable keys, null for special keys
    code: i32,          // ASCII code or KEY_* constant
    name: string        // Human-readable name ("Up", "Enter", "a", etc.)
}
```

### `read_key_timeout(timeout_ms: i32): object`

Read a keypress with timeout. Returns `{ code: KEY_NONE }` if timeout expires.

Useful for game loops that need to update while waiting for input.

## Key Constants

### Arrow Keys
- `KEY_UP`, `KEY_DOWN`, `KEY_LEFT`, `KEY_RIGHT`

### Navigation
- `KEY_HOME`, `KEY_END`, `KEY_INSERT`, `KEY_DELETE`
- `KEY_PAGE_UP`, `KEY_PAGE_DOWN`

### Function Keys
- `KEY_F1` through `KEY_F12`

### Control Keys
- `KEY_ENTER` (13)
- `KEY_TAB` (9)
- `KEY_BACKSPACE` (127)
- `KEY_ESCAPE` (27)
- `KEY_CTRL_C` (3), `KEY_CTRL_D` (4), `KEY_CTRL_Z` (26)

### Special
- `KEY_NONE` (0) - Returned on timeout or no input

## Helper Functions

### `is_key(key: object, code: i32): bool`

Check if key matches a specific code.

### `is_printable(key: object): bool`

Check if key is a printable character.

### `is_arrow(key: object): bool`

Check if key is an arrow key.

### `key_name(key: object): string`

Get human-readable key name.

### `with_raw_mode(callback: fn(): void)`

Execute callback in raw mode with automatic cleanup:

```hemlock
with_raw_mode(fn() {
    // Raw mode is active here
    let key = read_key();
});
// Raw mode automatically disabled
```

## Example: Simple Game Loop

```hemlock
import { enable_raw_mode, disable_raw_mode, read_key_timeout, KEY_UP, KEY_DOWN } from "@stdlib/termios";
import { time_ms } from "@stdlib/time";

let player_y = 10;
let last_update = time_ms();

enable_raw_mode();
defer disable_raw_mode();

loop {
    // Update game state every 100ms
    let now = time_ms();
    if (now - last_update > 100) {
        // Update enemies, animations, etc.
        last_update = now;
    }

    // Non-blocking input with 50ms timeout
    let key = read_key_timeout(50);

    if (key.code == KEY_UP) {
        player_y = player_y - 1;
    }
    if (key.code == KEY_DOWN) {
        player_y = player_y + 1;
    }
    if (key.char == "q") {
        break;
    }

    // Render game...
}
```

## Platform Notes

### Linux
- Uses `libc.so.6`
- termios struct: 60 bytes, c_lflag at offset 12

### macOS
- Uses `libSystem.B.dylib`
- termios struct: 80 bytes, c_lflag at offset 24

### Windows
- Not supported (use Windows Console API via FFI if needed)

## Fallback Pattern

For programs that should work both interactively and via pipes:

```hemlock
import { is_terminal, enable_raw_mode, disable_raw_mode, read_key } from "@stdlib/termios";

let raw_mode = false;

if (is_terminal()) {
    raw_mode = enable_raw_mode();
}

// ... your program ...

if (raw_mode) {
    // Use read_key() for instant input
} else {
    // Use read_line() for line-buffered input
}

if (raw_mode) {
    disable_raw_mode();
}
```

## See Also

- `@stdlib/terminal` - ANSI escape codes for colors and cursor control
- `examples/lego_loco.hml` - Game using termios for input
