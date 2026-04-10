#ifndef HEMLOCK_FILE_IO_H
#define HEMLOCK_FILE_IO_H

// Read an entire file into a null-terminated string.
// Returns NULL on error (with message to stderr).
// Caller must free() the returned buffer.
char* read_file(const char *path);

#endif // HEMLOCK_FILE_IO_H
