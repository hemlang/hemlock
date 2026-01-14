/*
 * terminal.h - ANSI Terminal Colors and Styling
 *
 * Provides macros for colorful terminal output in Hemlock CLI tools.
 * These are compile-time strings that can be concatenated with other strings.
 */

#ifndef HEMLOCK_TERMINAL_H
#define HEMLOCK_TERMINAL_H

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// ========== ANSI ESCAPE CODES ==========

// Reset all attributes
#define TERM_RESET      "\033[0m"

// Text styles
#define TERM_BOLD       "\033[1m"
#define TERM_DIM        "\033[2m"
#define TERM_ITALIC     "\033[3m"
#define TERM_UNDERLINE  "\033[4m"
#define TERM_BLINK      "\033[5m"
#define TERM_REVERSE    "\033[7m"

// Regular colors (foreground)
#define TERM_BLACK      "\033[30m"
#define TERM_RED        "\033[31m"
#define TERM_GREEN      "\033[32m"
#define TERM_YELLOW     "\033[33m"
#define TERM_BLUE       "\033[34m"
#define TERM_MAGENTA    "\033[35m"
#define TERM_CYAN       "\033[36m"
#define TERM_WHITE      "\033[37m"

// Bright colors (foreground)
#define TERM_GRAY         "\033[90m"
#define TERM_BRIGHT_RED   "\033[91m"
#define TERM_BRIGHT_GREEN "\033[92m"
#define TERM_BRIGHT_YELLOW "\033[93m"
#define TERM_BRIGHT_BLUE  "\033[94m"
#define TERM_BRIGHT_MAGENTA "\033[95m"
#define TERM_BRIGHT_CYAN  "\033[96m"
#define TERM_BRIGHT_WHITE "\033[97m"

// Background colors
#define TERM_BG_BLACK   "\033[40m"
#define TERM_BG_RED     "\033[41m"
#define TERM_BG_GREEN   "\033[42m"
#define TERM_BG_YELLOW  "\033[43m"
#define TERM_BG_BLUE    "\033[44m"
#define TERM_BG_MAGENTA "\033[45m"
#define TERM_BG_CYAN    "\033[46m"
#define TERM_BG_WHITE   "\033[47m"

// Bright background colors
#define TERM_BG_GRAY         "\033[100m"
#define TERM_BG_BRIGHT_RED   "\033[101m"
#define TERM_BG_BRIGHT_GREEN "\033[102m"
#define TERM_BG_BRIGHT_YELLOW "\033[103m"
#define TERM_BG_BRIGHT_BLUE  "\033[104m"
#define TERM_BG_BRIGHT_MAGENTA "\033[105m"
#define TERM_BG_BRIGHT_CYAN  "\033[106m"
#define TERM_BG_BRIGHT_WHITE "\033[107m"

// ========== SEMANTIC COLORS ==========

// For success messages
#define TERM_SUCCESS    TERM_BRIGHT_GREEN
// For error messages
#define TERM_ERROR      TERM_BRIGHT_RED
// For warnings
#define TERM_WARNING    TERM_BRIGHT_YELLOW
// For info/hints
#define TERM_INFO       TERM_BRIGHT_CYAN
// For code/paths
#define TERM_CODE       TERM_BRIGHT_WHITE
// For dimmed text
#define TERM_MUTED      TERM_GRAY

// ========== HEMLOCK BRAND COLORS ==========

// Primary brand color (forest green - hemlock is a tree!)
#define TERM_HEMLOCK    "\033[38;5;28m"
// Secondary brand color (darker green)
#define TERM_HEMLOCK_DARK "\033[38;5;22m"
// Accent color (gold/amber)
#define TERM_ACCENT     "\033[38;5;214m"

// ========== COLOR DETECTION ==========

// Check if stdout supports color output
static inline int term_supports_color(void) {
    // If not a TTY, no color
    if (!isatty(STDOUT_FILENO)) {
        return 0;
    }

    // Check NO_COLOR environment variable (standard)
    if (getenv("NO_COLOR") != NULL) {
        return 0;
    }

    // Check TERM environment variable
    const char *term = getenv("TERM");
    if (term == NULL) {
        return 0;
    }

    // "dumb" terminal has no color support
    if (strcmp(term, "dumb") == 0) {
        return 0;
    }

    // Most modern terminals support color
    return 1;
}

// Global color enable flag (set once at startup)
static int _term_color_enabled = -1;  // -1 = uninitialized

static inline int term_color_enabled(void) {
    if (_term_color_enabled < 0) {
        _term_color_enabled = term_supports_color();
    }
    return _term_color_enabled;
}

// Force color on/off (for --color/--no-color flags)
static inline void term_set_color(int enabled) {
    _term_color_enabled = enabled;
}

// ========== CONDITIONAL COLOR MACROS ==========

// These return the color code only if colors are enabled
#define TERM_C(code) (term_color_enabled() ? (code) : "")

// Convenience wrappers
#define C_RESET     TERM_C(TERM_RESET)
#define C_BOLD      TERM_C(TERM_BOLD)
#define C_DIM       TERM_C(TERM_DIM)
#define C_RED       TERM_C(TERM_RED)
#define C_GREEN     TERM_C(TERM_GREEN)
#define C_YELLOW    TERM_C(TERM_YELLOW)
#define C_BLUE      TERM_C(TERM_BLUE)
#define C_MAGENTA   TERM_C(TERM_MAGENTA)
#define C_CYAN      TERM_C(TERM_CYAN)
#define C_WHITE     TERM_C(TERM_WHITE)
#define C_GRAY      TERM_C(TERM_GRAY)
#define C_SUCCESS   TERM_C(TERM_SUCCESS)
#define C_ERROR     TERM_C(TERM_ERROR)
#define C_WARNING   TERM_C(TERM_WARNING)
#define C_INFO      TERM_C(TERM_INFO)
#define C_CODE      TERM_C(TERM_CODE)
#define C_MUTED     TERM_C(TERM_MUTED)
#define C_HEMLOCK   TERM_C(TERM_HEMLOCK)
#define C_ACCENT    TERM_C(TERM_ACCENT)
#define C_UNDERLINE TERM_C(TERM_UNDERLINE)

// ========== ASCII ART LOGO ==========

// Small hemlock logo (fits in terminal nicely)
#define HEMLOCK_LOGO_SMALL \
    "    /\\\n" \
    "   /  \\\n" \
    "  /____\\\n" \
    "    ||\n"

// Medium hemlock tree logo
#define HEMLOCK_LOGO \
    "      *\n" \
    "     /|\\\n" \
    "    / | \\\n" \
    "   /  |  \\\n" \
    "  /   |   \\\n" \
    " /_______\\\n" \
    "     |||\n" \
    "     |||\n"

// Stylized text banner
#define HEMLOCK_BANNER \
    " _   _                _            _    \n" \
    "| | | | ___ _ __ ___ | | ___   ___| | __\n" \
    "| |_| |/ _ \\ '_ ` _ \\| |/ _ \\ / __| |/ /\n" \
    "|  _  |  __/ | | | | | | (_) | (__|   < \n" \
    "|_| |_|\\___|_| |_| |_|_|\\___/ \\___|_|\\_\\\n"

// Compact text banner
#define HEMLOCK_BANNER_COMPACT \
    " _  _           _         _   \n" \
    "| || |___ _ __ | |___  __| |__\n" \
    "| __ / -_) '  \\| / _ \\/ _| / /\n" \
    "|_||_\\___|_|_|_|_\\___/\\__|_\\_\\\n"

#endif // HEMLOCK_TERMINAL_H
