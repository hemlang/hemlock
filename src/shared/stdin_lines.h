#ifndef HEMLOCK_STDIN_LINES_H
#define HEMLOCK_STDIN_LINES_H

#include <stddef.h>

/*
 * Line-at-a-time reads from standard input that do not hold stdin's stdio
 * lock while blocked.
 *
 * getline(stdin) holds that lock for as long as it is waiting for input, and
 * glibc >= 2.39 takes every stream's lock during the exit-time flush
 * (_IO_flush_all, reached from exit()) — stdin's included. So a program with
 * one task blocked in read_line() and another calling exit() deadlocked:
 * exit() waited on a lock the blocked reader would only release once input
 * arrived, which it never did. glibc 2.35 did not lock there, which is why
 * it showed up on one host and not another. Any fatal error hung the same
 * way, since that path also exits.
 *
 * Reading with read(2) and buffering here takes no FILE lock, so exit() is
 * free to flush and the fix costs nothing at exit: exit() still flushes
 * every other stream, so buffered writes to files the program opened are not
 * lost (which a bare _exit() in exit() would have thrown away).
 *
 * This is the only stdin reader in the process — the interpreter's
 * read_line(), the compiled runtime's, and the REPL all come through here —
 * so no stdio buffer can hold input this buffer cannot see, or vice versa.
 * Calls are serialized internally: several tasks may call read_line().
 */

/*
 * One line, with the trailing newline (and a CR before it) removed. Returns
 * a malloc'd NUL-terminated string the caller owns, and writes its length to
 * *out_len when out_len is non-NULL. Returns NULL at end of input with no
 * bytes left, which is what read_line() reports as null.
 *
 * A final line with no newline is returned as a line.
 */
char *hml_stdin_read_line(size_t *out_len);

#endif /* HEMLOCK_STDIN_LINES_H */
