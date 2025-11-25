# Hemlock Terminal Module

A standard library module providing ANSI terminal control for colors, cursor positioning, screen manipulation, and progress indicators.

## Overview

The terminal module provides comprehensive terminal control:

- **Colors** - 16 colors, bright variants, 256-color palette, RGB true color
- **Styles** - Bold, italic, underline, dim, blink, reverse, hidden, strikethrough
- **Cursor control** - Position, movement, save/restore, hide/show
- **Screen control** - Clear screen/line, scroll
- **Terminal info** - Get size, check color support
- **Progress indicators** - Progress bars, spinners with multiple styles

## Usage

```hemlock
import { RED, GREEN, BOLD, clear, color } from "@stdlib/terminal";

// Colored text
print(RED + "Error: Something went wrong" + RESET);
print(color("Success!", GREEN));

// Styled text
print(BOLD + "Important message" + RESET);

// Clear screen
clear();
```

Or import all:

```hemlock
import * as term from "@stdlib/terminal";
print(term.color("Hello", term.BLUE));
```

---

## Color Constants

### Basic Colors (Foreground)

```hemlock
import { BLACK, RED, GREEN, YELLOW, BLUE, MAGENTA, CYAN, WHITE } from "@stdlib/terminal";

print(RED + "Red text" + RESET);
print(GREEN + "Green text" + RESET);
print(BLUE + "Blue text" + RESET);
```

Available colors:
- `BLACK`, `RED`, `GREEN`, `YELLOW`, `BLUE`, `MAGENTA`, `CYAN`, `WHITE`

### Bright Colors (Foreground)

```hemlock
import { BRIGHT_RED, BRIGHT_GREEN, BRIGHT_BLUE, GRAY } from "@stdlib/terminal";

print(BRIGHT_RED + "Bright red" + RESET);
print(GRAY + "Gray text" + RESET);
```

Available bright colors:
- `GRAY`, `BRIGHT_RED`, `BRIGHT_GREEN`, `BRIGHT_YELLOW`, `BRIGHT_BLUE`, `BRIGHT_MAGENTA`, `BRIGHT_CYAN`, `BRIGHT_WHITE`

### Background Colors

```hemlock
import { BG_RED, BG_GREEN, BG_BLUE, WHITE } from "@stdlib/terminal";

print(WHITE + BG_RED + " Error " + RESET);
print(WHITE + BG_GREEN + " Success " + RESET);
```

Available background colors:
- `BG_BLACK`, `BG_RED`, `BG_GREEN`, `BG_YELLOW`, `BG_BLUE`, `BG_MAGENTA`, `BG_CYAN`, `BG_WHITE`
- `BG_GRAY`, `BG_BRIGHT_RED`, `BG_BRIGHT_GREEN`, `BG_BRIGHT_YELLOW`, `BG_BRIGHT_BLUE`, `BG_BRIGHT_MAGENTA`, `BG_BRIGHT_CYAN`, `BG_BRIGHT_WHITE`

---

## Text Styles

### Style Constants

```hemlock
import { BOLD, ITALIC, UNDERLINE, DIM } from "@stdlib/terminal";

print(BOLD + "Bold text" + RESET);
print(ITALIC + "Italic text" + RESET);
print(UNDERLINE + "Underlined text" + RESET);
print(DIM + "Dim text" + RESET);
```

Available styles:
- `BOLD` - Bold/bright text
- `DIM` - Dimmed text
- `ITALIC` - Italic text (not supported on all terminals)
- `UNDERLINE` - Underlined text
- `BLINK` - Blinking text (rarely supported)
- `REVERSE` - Reverse foreground/background
- `HIDDEN` - Hidden text (still there, just invisible)
- `STRIKETHROUGH` - Strikethrough text

### Reset Styles

```hemlock
import { BOLD, RESET_BOLD, UNDERLINE, RESET_UNDERLINE, RESET } from "@stdlib/terminal";

print(BOLD + "Bold " + RESET_BOLD + "not bold anymore");
print(UNDERLINE + "Underlined " + RESET_UNDERLINE + "not underlined");
print(BOLD + UNDERLINE + "Both " + RESET + "neither");
```

Reset specific styles:
- `RESET` - Reset all styles and colors
- `RESET_BOLD` - Reset bold only
- `RESET_ITALIC` - Reset italic only
- `RESET_UNDERLINE` - Reset underline only
- `RESET_BLINK` - Reset blink only
- `RESET_REVERSE` - Reset reverse only
- `RESET_HIDDEN` - Reset hidden only

### Combining Styles

```hemlock
import { RED, BOLD, UNDERLINE, BG_YELLOW, RESET } from "@stdlib/terminal";

print(BOLD + UNDERLINE + RED + "Bold, underlined, red" + RESET);
print(BOLD + RED + BG_YELLOW + " Alert " + RESET);
```

---

## Color Functions

### color(text, code)
Wrap text with a color code and automatically reset.

**Parameters:**
- `text: string` - Text to colorize
- `code: string` - Color code constant

**Returns:** `string` - Colorized text with reset

```hemlock
import { color, RED, GREEN, BLUE } from "@stdlib/terminal";

print(color("Error", RED));
print(color("Success", GREEN));
print(color("Info", BLUE));
```

### color_bg(text, fg, bg)
Wrap text with foreground and background colors.

**Parameters:**
- `text: string` - Text to colorize
- `fg: string` - Foreground color code
- `bg: string` - Background color code

**Returns:** `string` - Colorized text with reset

```hemlock
import { color_bg, WHITE, RED, BG_RED, BG_GREEN } from "@stdlib/terminal";

print(color_bg(" ERROR ", WHITE, BG_RED));
print(color_bg(" OK ", WHITE, BG_GREEN));
```

### rgb(r, g, b)
Create 24-bit RGB foreground color.

**Parameters:**
- `r: i32` - Red component (0-255)
- `g: i32` - Green component (0-255)
- `b: i32` - Blue component (0-255)

**Returns:** `string` - RGB color code

```hemlock
import { rgb, RESET } from "@stdlib/terminal";

print(rgb(255, 100, 50) + "Custom orange" + RESET);
print(rgb(75, 0, 130) + "Indigo text" + RESET);
```

**Note:** Requires terminal with true color support (24-bit color).

### bg_rgb(r, g, b)
Create 24-bit RGB background color.

**Parameters:**
- `r: i32` - Red component (0-255)
- `g: i32` - Green component (0-255)
- `b: i32` - Blue component (0-255)

**Returns:** `string` - RGB background color code

```hemlock
import { bg_rgb, rgb, RESET } from "@stdlib/terminal";

print(rgb(255, 255, 255) + bg_rgb(255, 0, 0) + " White on red " + RESET);
```

### color_256(n)
Use 256-color palette (foreground).

**Parameters:**
- `n: i32` - Color index (0-255)

**Returns:** `string` - 256-color code

```hemlock
import { color_256, RESET } from "@stdlib/terminal";

// 0-15: Standard colors
// 16-231: 6x6x6 color cube
// 232-255: Grayscale

print(color_256(196) + "Bright red" + RESET);
print(color_256(21) + "Deep blue" + RESET);
```

**Color ranges:**
- 0-15: Standard ANSI colors
- 16-231: 6×6×6 RGB color cube
- 232-255: Grayscale ramp

### bg_color_256(n)
Use 256-color palette (background).

**Parameters:**
- `n: i32` - Color index (0-255)

**Returns:** `string` - 256-color background code

```hemlock
import { bg_color_256, color_256, RESET } from "@stdlib/terminal";

print(color_256(16) + bg_color_256(226) + " Black on yellow " + RESET);
```

---

## Cursor Control

### move_to(row, col)
Move cursor to absolute position.

**Parameters:**
- `row: i32` - Row (1-indexed, 1 = top)
- `col: i32` - Column (1-indexed, 1 = left)

**Returns:** `string` - ANSI escape sequence

```hemlock
import { move_to } from "@stdlib/terminal";

exec("printf '" + move_to(10, 20) + "Hello'");  // Print at row 10, col 20
```

### move_up(n), move_down(n), move_left(n), move_right(n)
Move cursor relative to current position.

**Parameters:**
- `n: i32` - Number of positions to move

**Returns:** `string` - ANSI escape sequence

```hemlock
import { move_up, move_down, move_left, move_right } from "@stdlib/terminal";

// Move cursor up 3 lines
exec("printf '" + move_up(3) + "'");

// Move cursor right 5 columns
exec("printf '" + move_right(5) + "'");
```

### Cursor Save/Restore

```hemlock
import { SAVE_CURSOR, RESTORE_CURSOR } from "@stdlib/terminal";

// Save current position
exec("printf '" + SAVE_CURSOR + "'");

// ... print something elsewhere ...

// Restore saved position
exec("printf '" + RESTORE_CURSOR + "'");
```

**Constants:**
- `SAVE_CURSOR` - Save cursor position (ANSI)
- `RESTORE_CURSOR` - Restore cursor position (ANSI)
- `SAVE_CURSOR_DEC` - Save cursor (DEC mode)
- `RESTORE_CURSOR_DEC` - Restore cursor (DEC mode)

### Cursor Visibility

```hemlock
import { HIDE_CURSOR, SHOW_CURSOR } from "@stdlib/terminal";

// Hide cursor (useful for animations)
exec("printf '" + HIDE_CURSOR + "'");

// ... do animation ...

// Show cursor again
exec("printf '" + SHOW_CURSOR + "'");
```

**Note:** Always show cursor before exiting to avoid leaving terminal in bad state.

---

## Screen Control

### Clear Screen

```hemlock
import { CLEAR_SCREEN, CLEAR_TO_END, CLEAR_TO_START } from "@stdlib/terminal";

// Clear entire screen
exec("printf '" + CLEAR_SCREEN + "'");

// Clear from cursor to end of screen
exec("printf '" + CLEAR_TO_END + "'");
```

**Constants:**
- `CLEAR_SCREEN` - Clear entire screen
- `CLEAR_TO_END` - Clear from cursor to end of screen
- `CLEAR_TO_START` - Clear from cursor to start of screen

### Clear Line

```hemlock
import { CLEAR_LINE, CLEAR_LINE_TO_END } from "@stdlib/terminal";

// Clear entire line
exec("printf '\r" + CLEAR_LINE + "'");

// Clear from cursor to end of line
exec("printf '" + CLEAR_LINE_TO_END + "'");
```

**Constants:**
- `CLEAR_LINE` - Clear entire line
- `CLEAR_LINE_TO_END` - Clear from cursor to end of line
- `CLEAR_LINE_TO_START` - Clear from cursor to start of line

### Scrolling

```hemlock
import { scroll_up, scroll_down } from "@stdlib/terminal";

// Scroll entire display up 3 lines
exec("printf '" + scroll_up(3) + "'");

// Scroll entire display down 2 lines
exec("printf '" + scroll_down(2) + "'");
```

---

## Terminal Info

### size()
Get terminal dimensions.

**Parameters:** None

**Returns:** `object` - `{ rows: i32, cols: i32 }`

```hemlock
import { size } from "@stdlib/terminal";

let dimensions = size();
print("Terminal size: " + typeof(dimensions.rows) + "x" + typeof(dimensions.cols));

// Center text
let text = "Hello, World!";
let col = (dimensions.cols - text.length) / 2;
print_at(dimensions.rows / 2, col, text);
```

**Fallback:** Returns `{ rows: 24, cols: 80 }` if size cannot be determined.

### supports_color()
Check if terminal supports ANSI colors.

**Parameters:** None

**Returns:** `bool` - True if colors are supported

```hemlock
import { supports_color, RED, RESET } from "@stdlib/terminal";

if (supports_color()) {
    print(RED + "Colored output!" + RESET);
} else {
    print("No color support");
}
```

**Detection:** Checks `TERM` and `COLORTERM` environment variables.

---

## Progress Indicators

### ProgressBar(total, width?)
Create a progress bar.

**Parameters:**
- `total: i32` - Total number of items
- `width: i32` (optional) - Bar width in characters (default: 40)

**Returns:** Progress bar object

**Methods:**
- `update(value)` - Set progress to specific value
- `increment()` - Increment progress by 1
- `render()` - Manually render the bar
- `finish()` - Complete the bar and print newline

```hemlock
import { ProgressBar } from "@stdlib/terminal";
import { sleep } from "@stdlib/time";

let bar = ProgressBar(100, 50);

let i = 0;
while (i <= 100) {
    bar.update(i);
    sleep(0.05);
    i = i + 1;
}
bar.finish();

// Output: [===========>                ] 23.4% 23/100
```

### ProgressBar.increment()
Increment progress by one step.

```hemlock
import { ProgressBar } from "@stdlib/terminal";

let bar = ProgressBar(10);
let i = 0;
while (i < 10) {
    // ... process item ...
    bar.increment();
    i = i + 1;
}
bar.finish();
```

### Spinner(frames?)
Create a spinner animation.

**Parameters:**
- `frames: array` (optional) - Custom animation frames (default: `["|", "/", "-", "\\"]`)

**Returns:** Spinner object

**Methods:**
- `spin()` - Advance and display next frame
- `finish(message?)` - Stop spinner, optionally print message
- `get_frame()` - Get current frame without printing
- `next()` - Advance to next frame without printing

```hemlock
import { Spinner, SPINNER_DOTS } from "@stdlib/terminal";
import { sleep } from "@stdlib/time";

let spinner = Spinner(SPINNER_DOTS());

let i = 0;
while (i < 50) {
    spinner.spin();
    sleep(0.1);
    i = i + 1;
}
spinner.finish("Done!");
```

### Predefined Spinner Styles

```hemlock
import { SPINNER_DOTS, SPINNER_LINE, SPINNER_ARROW } from "@stdlib/terminal";

// Dots spinner (Unicode braille)
let s1 = Spinner(SPINNER_DOTS());

// Classic line spinner
let s2 = Spinner(SPINNER_LINE());

// Arrow spinner
let s3 = Spinner(SPINNER_ARROW());

// Clock emoji spinner
let s4 = Spinner(SPINNER_CLOCK());

// Bounce spinner
let s5 = Spinner(SPINNER_BOUNCE());
```

**Available styles:**
- `SPINNER_DOTS()` - Braille dots: ⠋ ⠙ ⠹ ⠸ ⠼ ⠴ ⠦ ⠧ ⠇ ⠏
- `SPINNER_LINE()` - Classic: | / - \
- `SPINNER_ARROW()` - Arrows: ← ↖ ↑ ↗ → ↘ ↓ ↙
- `SPINNER_BOUNCE()` - Bounce: ⠁ ⠂ ⠄ ⠂
- `SPINNER_CLOCK()` - Clock emojis: 🕐 🕑 🕒 ...

---

## Utility Functions

### clear()
Clear screen and move cursor to top-left.

**Parameters:** None

**Returns:** `null`

```hemlock
import { clear } from "@stdlib/terminal";

clear();  // Fresh screen
print("Starting new output...");
```

### print_at(row, col, text)
Print text at specific screen position.

**Parameters:**
- `row: i32` - Row (1-indexed)
- `col: i32` - Column (1-indexed)
- `text: string` - Text to print

**Returns:** `null`

```hemlock
import { print_at } from "@stdlib/terminal";

print_at(10, 20, "Hello at position (10, 20)");
print_at(15, 30, "Another line here");
```

### print_color(text, code)
Print colored text (convenience wrapper).

**Parameters:**
- `text: string` - Text to print
- `code: string` - Color code constant

**Returns:** `null`

```hemlock
import { print_color, RED, GREEN, BLUE } from "@stdlib/terminal";

print_color("Error message", RED);
print_color("Success!", GREEN);
print_color("Information", BLUE);
```

### print_styled(text, style)
Print styled text (convenience wrapper).

**Parameters:**
- `text: string` - Text to print
- `style: string` - Style code constant

**Returns:** `null`

```hemlock
import { print_styled, BOLD, UNDERLINE, ITALIC } from "@stdlib/terminal";

print_styled("Important!", BOLD);
print_styled("Emphasized", UNDERLINE);
print_styled("Note", ITALIC);
```

---

## Complete Examples

### Colored Logger

```hemlock
import { RED, YELLOW, GREEN, BLUE, BOLD, RESET } from "@stdlib/terminal";

fn log_error(msg: string): null {
    print(RED + BOLD + "[ERROR]" + RESET + " " + msg);
    return null;
}

fn log_warning(msg: string): null {
    print(YELLOW + BOLD + "[WARN]" + RESET + " " + msg);
    return null;
}

fn log_info(msg: string): null {
    print(BLUE + BOLD + "[INFO]" + RESET + " " + msg);
    return null;
}

fn log_success(msg: string): null {
    print(GREEN + BOLD + "[OK]" + RESET + " " + msg);
    return null;
}

// Usage
log_error("Connection failed");
log_warning("Low disk space");
log_info("Starting service...");
log_success("Operation complete");
```

### Download Progress

```hemlock
import { ProgressBar } from "@stdlib/terminal";
import { sleep } from "@stdlib/time";

fn download(url: string, size: i32): null {
    print("Downloading: " + url);

    let bar = ProgressBar(size, 50);
    let downloaded = 0;

    while (downloaded < size) {
        // Simulate download chunk
        let chunk_size = 1024;
        if (downloaded + chunk_size > size) {
            chunk_size = size - downloaded;
        }

        downloaded = downloaded + chunk_size;
        bar.update(downloaded);
        sleep(0.01);
    }

    bar.finish();
    print("Download complete!");
    return null;
}

download("http://example.com/file.zip", 102400);
```

### Loading Spinner

```hemlock
import { Spinner, SPINNER_DOTS } from "@stdlib/terminal";
import { sleep } from "@stdlib/time";

fn load_data(): null {
    let spinner = Spinner(SPINNER_DOTS());

    let i = 0;
    while (i < 100) {
        spinner.spin();
        sleep(0.05);
        // ... actual loading work ...
        i = i + 1;
    }

    spinner.finish("Data loaded successfully!");
    return null;
}

load_data();
```

### Terminal Dashboard

```hemlock
import { clear, move_to, color, GREEN, YELLOW, RED, size } from "@stdlib/terminal";
import { sleep } from "@stdlib/time";

fn dashboard(): null {
    let running = true;
    let counter = 0;

    while (running && counter < 50) {
        clear();

        let term = size();

        // Title
        exec("printf '" + move_to(1, 1) + "'");
        print(color("=== System Dashboard ===", GREEN));

        // Stats
        print("");
        print("Counter: " + typeof(counter));
        print("Terminal: " + typeof(term.rows) + "x" + typeof(term.cols));

        // Status indicators
        if (counter % 2 == 0) {
            print(color("● Service A: Running", GREEN));
        } else {
            print(color("● Service A: Idle", YELLOW));
        }

        if (counter % 3 == 0) {
            print(color("● Service B: Active", GREEN));
        } else {
            print(color("● Service B: Waiting", RED));
        }

        sleep(0.5);
        counter = counter + 1;
    }

    return null;
}

dashboard();
```

### Menu System

```hemlock
import { clear, print_at, color, GREEN, YELLOW, RESET } from "@stdlib/terminal";

fn show_menu(options: array, selected: i32): null {
    clear();

    print("=== Menu ===");
    print("");

    let i = 0;
    while (i < options.length) {
        if (i == selected) {
            print(GREEN + "> " + options[i] + RESET);
        } else {
            print("  " + options[i]);
        }
        i = i + 1;
    }

    return null;
}

let menu_items = ["Start", "Settings", "About", "Exit"];
show_menu(menu_items, 1);  // Highlight "Settings"
```

---

## Platform Support

### ANSI Escape Sequences

Most modern terminals support ANSI escape codes:
- ✅ Linux terminal emulators (GNOME Terminal, Konsole, xterm, etc.)
- ✅ macOS Terminal.app, iTerm2
- ✅ Windows Terminal, Windows 10+ (with ANSI support enabled)
- ✅ SSH/remote terminals
- ⚠️ Windows Command Prompt (legacy, limited support)

### Color Support

**True color (24-bit RGB):**
- Requires modern terminal (iTerm2, GNOME Terminal 3.16+, Windows Terminal)
- Check with `supports_color()` and test RGB codes

**256-color mode:**
- Widely supported across most terminals
- Fallback from true color

**Basic 16 colors:**
- Universal support across all ANSI-compatible terminals

---

## Best Practices

### Always Reset Styles

```hemlock
import { RED, BOLD, RESET } from "@stdlib/terminal";

// Good
print(RED + "Error" + RESET);

// Bad (affects all subsequent output)
print(RED + "Error");
print("This is red too!");
```

### Check Color Support

```hemlock
import { supports_color, RED, RESET } from "@stdlib/terminal";

if (supports_color()) {
    print(RED + "Error!" + RESET);
} else {
    print("[ERROR] Error!");
}
```

### Hide Cursor During Animations

```hemlock
import { HIDE_CURSOR, SHOW_CURSOR, Spinner } from "@stdlib/terminal";

exec("printf '" + HIDE_CURSOR + "'");
let spinner = Spinner();

// ... animation loop ...

spinner.finish();
exec("printf '" + SHOW_CURSOR + "'");
```

### Preserve Cursor Position

```hemlock
import { SAVE_CURSOR, RESTORE_CURSOR } from "@stdlib/terminal";

exec("printf '" + SAVE_CURSOR + "'");

// ... print status at specific location ...

exec("printf '" + RESTORE_CURSOR + "'");
```

---

## Error Handling

Terminal operations are generally safe but may fail if:
- Terminal doesn't support ANSI codes
- Output is redirected to a file/pipe
- Terminal size cannot be determined

```hemlock
import { size, supports_color } from "@stdlib/terminal";

// Check capabilities first
if (!supports_color()) {
    print("Warning: Color output disabled");
}

let term = size();
if (term.rows == 24 && term.cols == 80) {
    // Possibly default fallback values
    print("Note: Using default terminal size");
}
```

---

## See Also

- **Time module** - Use `@stdlib/time` for sleep and timing
- **Environment module** - Use `@stdlib/env` for `TERM` variable access

---

## License

Part of the Hemlock standard library.
