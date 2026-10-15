// Cross-platform terminal control core, shared by both backends'
// __term_* builtins (see src/backends/interpreter/builtins/os.c and
// runtime/src/builtins_terminal.c).
//
// POSIX: termios + poll + TIOCGWINSZ. Windows: console modes with
// virtual-terminal input/output enabled, so escape-sequence parsers and
// ANSI output written against POSIX terminals work unmodified.
#ifndef HEMLOCK_TERM_CORE_H
#define HEMLOCK_TERM_CORE_H

// 1 if stdin is an interactive terminal
int hml_termcore_is_tty(void);

// Enable/disable raw mode (no echo, no line buffering; Ctrl+C still
// interrupts). Saves and restores the original state. 0 on success.
int hml_termcore_raw(int enable);

// Read one byte from stdin. timeout_ms < 0 blocks, 0 polls, > 0 waits.
// Returns the byte, or -1 on timeout/EOF/error.
int hml_termcore_read_byte(int timeout_ms);

// Terminal size. 0 on success, -1 when no terminal is attached.
int hml_termcore_size(int *rows, int *cols);

#endif // HEMLOCK_TERM_CORE_H
