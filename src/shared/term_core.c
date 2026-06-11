// Cross-platform terminal control core. See term_core.h.
#include "term_core.h"

#ifdef _WIN32

#include <windows.h>
#include <conio.h>
#include <io.h>
#include <stdio.h>

// Not in older MinGW headers
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif

static DWORD hml_term_saved_in;
static DWORD hml_term_saved_out;
static int hml_term_saved_out_valid = 0;
static int hml_term_raw_active = 0;

// A console ReadFile can return a whole VT escape sequence at once;
// queue the surplus so byte-at-a-time callers (escape-sequence parsers)
// see every byte without a second blocking read.
static unsigned char hml_term_queue[64];
static int hml_term_queue_len = 0;
static int hml_term_queue_pos = 0;

int hml_termcore_is_tty(void) {
    return _isatty(_fileno(stdin)) != 0;
}

int hml_termcore_raw(int enable) {
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    if (enable) {
        if (hml_term_raw_active) {
            return 0;
        }
        DWORD in_mode = 0;
        if (!GetConsoleMode(hin, &in_mode)) {
            return -1;
        }
        hml_term_saved_in = in_mode;
        // No line buffering, no echo; ENABLE_PROCESSED_INPUT stays on so
        // Ctrl+C still interrupts (the ISIG-preserving raw mode
        // @stdlib/termios uses on POSIX). VT input makes arrows and
        // friends arrive as the same ESC sequences POSIX terminals send.
        in_mode &= ~(DWORD)(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
        in_mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
        if (!SetConsoleMode(hin, in_mode)) {
            return -1;
        }
        // Best effort: ANSI escape output (colors, cursor movement)
        HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD out_mode = 0;
        if (GetConsoleMode(hout, &out_mode)) {
            hml_term_saved_out = out_mode;
            hml_term_saved_out_valid = 1;
            SetConsoleMode(hout, out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
        hml_term_raw_active = 1;
        return 0;
    }

    if (!hml_term_raw_active) {
        return 0;
    }
    int rc = SetConsoleMode(hin, hml_term_saved_in) ? 0 : -1;
    if (hml_term_saved_out_valid) {
        SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), hml_term_saved_out);
        hml_term_saved_out_valid = 0;
    }
    hml_term_raw_active = 0;
    hml_term_queue_len = 0;
    hml_term_queue_pos = 0;
    return rc;
}

int hml_termcore_read_byte(int timeout_ms) {
    if (hml_term_queue_pos < hml_term_queue_len) {
        return hml_term_queue[hml_term_queue_pos++];
    }

    if (timeout_ms >= 0) {
        // _kbhit reports fresh keystrokes without consuming them; queued
        // surplus bytes were handled above
        DWORD waited = 0;
        while (!_kbhit()) {
            if ((int)waited >= timeout_ms) {
                return -1;
            }
            Sleep(5);
            waited += 5;
        }
    }

    DWORD got = 0;
    if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), hml_term_queue,
                  sizeof(hml_term_queue), &got, NULL)
        || got == 0) {
        return -1;
    }
    hml_term_queue_len = (int)got;
    hml_term_queue_pos = 0;
    return hml_term_queue[hml_term_queue_pos++];
}

int hml_termcore_size(int *rows, int *cols) {
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)
        && !GetConsoleScreenBufferInfo(GetStdHandle(STD_ERROR_HANDLE), &info)) {
        return -1;
    }
    *cols = info.srWindow.Right - info.srWindow.Left + 1;
    *rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    return 0;
}

#else // !_WIN32

#include <termios.h>
#include <unistd.h>
#include <poll.h>
#include <sys/ioctl.h>

static struct termios hml_term_saved;
static int hml_term_raw_active = 0;

int hml_termcore_is_tty(void) {
    return isatty(STDIN_FILENO) == 1;
}

int hml_termcore_raw(int enable) {
    if (enable) {
        if (hml_term_raw_active) {
            return 0;
        }
        if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &hml_term_saved) != 0) {
            return -1;
        }
        struct termios raw = hml_term_saved;
        raw.c_iflag &= ~(tcflag_t)(ICRNL | IXON);
        raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN);
        // ISIG stays enabled so Ctrl+C still interrupts (matches the
        // raw mode @stdlib/termios configures)
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
            return -1;
        }
        hml_term_raw_active = 1;
        return 0;
    }

    if (!hml_term_raw_active) {
        return 0;
    }
    int rc = tcsetattr(STDIN_FILENO, TCSAFLUSH, &hml_term_saved);
    hml_term_raw_active = 0;
    return rc == 0 ? 0 : -1;
}

int hml_termcore_read_byte(int timeout_ms) {
    if (timeout_ms >= 0) {
        struct pollfd pfd = { STDIN_FILENO, POLLIN, 0 };
        int r = poll(&pfd, 1, timeout_ms);
        if (r <= 0) {
            return -1;
        }
    }
    unsigned char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    return n == 1 ? (int)c : -1;
}

int hml_termcore_size(int *rows, int *cols) {
    struct winsize ws;
    // stdout may be redirected while stderr still points at the terminal
    if ((ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0)
        && (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0)
        && (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0)) {
        return -1;
    }
    *rows = ws.ws_row;
    *cols = ws.ws_col;
    return 0;
}

#endif // _WIN32
